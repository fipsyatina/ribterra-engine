#include "ribterra/game_server.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace ribterra {
#include "game_server_lobby.inc"
namespace {
PathCommandKind path_command_kind(CommandType type) {
    switch (type) {
    case CommandType::Move:
        return PathCommandKind::Move;
    case CommandType::Gather:
        return PathCommandKind::Gather;
    case CommandType::Build:
        return PathCommandKind::Build;
    case CommandType::CancelConstruction:
    case CommandType::AssignProfession:
    case CommandType::Stop:
    case CommandType::CancelManualOrder:
    case CommandType::ReleaseControl:
    case CommandType::Recruit:
    case CommandType::NightShift:
    case CommandType::YouthWork:
    case CommandType::Production:
    case CommandType::Upgrade:
    case CommandType::CancelProduction:
    case CommandType::Wilderness:
    case CommandType::Lobby:
    case CommandType::TransferControl:
        break; // Cancellation is synchronous and never owns a new path batch.
    }
    throw std::logic_error("Unknown command type");
}
} // namespace

GameServer::GameServer(ServerConfig config)
    : config_(std::move(config)), world_(config_.seed, config_.population, generator_version, config_.climate) {
    if (config_.max_players == 0 || config_.max_players > 6 ||
        config_.command_limit_per_second == 0 || !config_.clock)
        throw std::invalid_argument("Invalid server capacity");
    if(config_.wildlife_enabled)world_.enable_wildlife();
    lobby_.enabled=config_.lobby_enabled; lobby_.capacity=static_cast<int>(std::max(std::size_t{2},config_.max_players));
}
bool GameServer::connected(PlayerId id) const {
    auto it = players_.find(id);
    return it != players_.end() && it->second.connected;
}
std::string GameServer::local_host_resume_token() const {
    const auto host = players_.find(host_);
    if (host == players_.end() || host->second.connected || host->second.bot)
        return {};
    return host->second.token;
}
JoinResult GameServer::join(std::string name, const Versions &versions,
                            std::string_view resume_token, bool observer) {
    pump_boundary();
    if (const auto mismatch=version_mismatch(config_.versions,versions);!mismatch.empty())
        return {false, 0, {}, mismatch};
    if (!valid_utf8_text(name, 64))
        return {false, 0, {}, "Invalid player name"};
    if (!resume_token.empty()) {
        if (resume_token.size() != 64 || banned_tokens_.contains(std::string(resume_token)))
            return {false, 0, {}, "Invalid session credential"};
        for (auto &[id, player] : players_)
            if (constant_time_equal(player.token, resume_token)) {
                const auto reconnect_window = std::chrono::duration<double>(
                    std::max(30.0, static_cast<double>(config_.reconnect_ticks) * tick_seconds));
                if (player.connected || (!lobby_.enabled && (!player.disconnected_clock ||
                    config_.clock() - *player.disconnected_clock > reconnect_window)))
                    return {false, 0, {}, "Session is active or reconnect expired"};
                player.connected = true;
                player.disconnected_clock.reset();
                ++revision_;
                return {true, id, player.token, {}, player.highest_request + 1};
            }
        return {false, 0, {}, "Unknown session credential"};
    }
    for (auto it = players_.begin(); it != players_.end();) {
        if (!lobby_.enabled && !it->second.connected && it->second.disconnected_clock &&
            config_.clock() - *it->second.disconnected_clock >
                std::chrono::duration<double>(
                    std::max(30.0, static_cast<double>(config_.reconnect_ticks) * tick_seconds)) &&
            std::none_of(control_leases_.begin(), control_leases_.end(),
                         [&](const auto &lease) { return lease.second.holder == it->first; }))
            it = players_.erase(it);
        else
            ++it;
    }
    if (players_.size() >= config_.max_players)
        return {false, 0, {}, "Server is full"};
    Player player;
    player.name = std::move(name);
    player.token = secure_token();
    player.connected = true;
    player.observer = observer;
    // New human participants join the host settlement, including a host in team 2–6.
    // Restored identities keep their saved team in the resume branch above.
    const auto host=players_.find(host_);
    player.team=host==players_.end()?1:host->second.team;
    const auto id = next_player_++;
    const auto token = player.token;
    if(lobby_.enabled&&lobby_.started)world_.create_nations({player.team},lobby_.capacity,config_.population);
    players_.emplace(id, std::move(player));
    ++revision_;
    if (!host_ && !observer)
        host_ = id;
    return {true, id, token, {}};
}
void GameServer::disconnect(PlayerId id) {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return;
    it->second.connected = false;
    it->second.ready = false;
    it->second.disconnected_at = world_.ticks();
    it->second.disconnected_clock = config_.clock();
    ++revision_;
}
CommandReply GameServer::enqueue(PlayerId id, GameCommand command) {
    queue_expired_leases();
    auto invalid = [&](std::string reason) {
        return CommandReply{command.request_id,          world_.ticks(),    id,
                            CommandReplyStatus::Invalid, PathFailure::None, std::move(reason)};
    };
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return invalid("Unauthenticated session");
    auto &player = it->second;
    if (command.author != id || !command.request_id || command.request_id == UINT64_MAX)
        return invalid("Invalid command identity");
    if (auto prior = player.completed.find(command.request_id); prior != player.completed.end())
        return prior->second.command == command ? prior->second.reply
                                                : invalid("Request ID conflict");
    if (auto prior = player.pending.find(command.request_id); prior != player.pending.end())
        return prior->second == command
                   ? CommandReply{command.request_id, 0,       id, CommandReplyStatus::Queued,
                                  PathFailure::None,  "Queued"}
                   : invalid("Request ID conflict");
    if (command.request_id <= player.highest_request)
        return invalid("Stale request ID");
    std::set<std::uint64_t> outstanding;
    for (const auto &reply : player.replies)
        outstanding.insert(reply.request_id);
    for (const auto &[request, ignored] : player.pending) {
        (void)ignored;
        outstanding.insert(request);
    }
    if (outstanding.size() >= 2048)
        return invalid("Unacknowledged command limit");
    auto reject = [&](std::string reason, PathFailure failure = PathFailure::None) {
        player.highest_request = command.request_id;
        CommandReply reply{command.request_id,           world_.ticks(), id,
                           CommandReplyStatus::Rejected, failure,        std::move(reason)};
        player.completed.emplace(command.request_id, Cached{command, reply});
        while (player.completed.size() > 2048)
            player.completed.erase(player.completed.begin());
        player.replies.push_back(reply);
        return reply;
    };
    if(lobby_.enabled&&!lobby_.started&&command.type!=CommandType::Lobby)return reject("lobby.waiting");
    if (player.observer)
        return reject("Observer cannot command units");
    if (command.residents.size() > max_path_batch_residents ||
        !inside_world(command.x, command.y) || !valid_layer(command.layer))
        return reject("Command bounds exceeded", PathFailure::LimitExceeded);
    std::set<int> ids;
    for (int resident : command.residents)
        if (resident <= 0 || !ids.insert(resident).second)
            return reject("Invalid or duplicate resident ID");
    if (command.issued_tick > world_.ticks() + 200)
        return reject("Command tick is too far in the future");
    const auto now = config_.clock();
    while (!player.commands_at.empty() &&
           now - player.commands_at.front() >= std::chrono::seconds(1))
        player.commands_at.pop_front();
    if (player.commands_at.size() >= config_.command_limit_per_second || pending_.size() >= 512)
        return reject("Command rate or queue limit exceeded");
    if (static_cast<int>(command.type) > static_cast<int>(CommandType::Lobby))
        return reject("Unknown command");
    if (command.control_epoch && command.control_epoch != control_epoch_) {
        auto reply = reject("Control session epoch expired");
        auto rejected_residents = command.residents;
        if (command.type == CommandType::CancelManualOrder)
            for (const auto &order : world_.manual_orders())
                if (order.order_id == command.manual_order_id && order.owner_nation == player_nation(id))
                    rejected_residents.push_back(order.resident_id);
        for (int resident : rejected_residents)
            reply.control_rejections.push_back({resident, ControlFailure::StaleEpoch});
        for (int resident : rejected_residents)
            reply.residents.push_back(
                {resident, ResidentCommandStatus::Rejected, PathFailure::ReservationConflict});
        if (!reply.residents.empty())
            reply.failure = PathFailure::ReservationConflict;
        player.completed.at(command.request_id).reply = reply;
        player.replies.back() = reply;
        return reply;
    }
    if (static_cast<unsigned>(command.manual_queue_mode) >
            static_cast<unsigned>(ManualQueueMode::Append) ||
        static_cast<unsigned>(command.gather_completion) >
            static_cast<unsigned>(GatherCompletion::OneDelivery) ||
        command.manual_order_id == UINT64_MAX ||
        ((command.type == CommandType::CancelManualOrder) != (command.manual_order_id != 0)) ||
        ((command.type == CommandType::TransferControl) != (command.target_player != 0)) ||
        (command.type == CommandType::CancelManualOrder && !command.residents.empty()) ||
        (command.type != CommandType::Move && command.type != CommandType::Gather &&
         command.manual_queue_mode != ManualQueueMode::Replace) ||
        (command.type != CommandType::Gather &&
         command.gather_completion != GatherCompletion::UntilExhausted))
        return reject("Invalid manual control command", PathFailure::TargetInvalid);
    if (!World::valid_profession_assignment(command.assignment) ||
        (command.type != CommandType::AssignProfession &&
         command.assignment != ProfessionAssignment{}) ||
        ((command.type == CommandType::AssignProfession || command.type == CommandType::Stop) &&
         (command.residents.empty() || command.x != 0 || command.y != 0 ||
          command.layer != WorldLayer::Surface || command.building != BuildingKind::House)))
        return reject("Invalid profession command", PathFailure::TargetInvalid);
    if ((command.type == CommandType::CancelConstruction &&
         (command.building_id <= 0 || !command.residents.empty())) ||
        (command.type != CommandType::CancelConstruction && command.type < CommandType::Production && command.building_id != 0))
        return reject("Invalid construction command", PathFailure::TargetInvalid);
    if (command.type >= CommandType::Recruit && command.type<=CommandType::YouthWork &&
        (command.x || command.y || command.layer!=WorldLayer::Surface || command.building!=BuildingKind::House ||
         (command.type==CommandType::YouthWork ? command.residents.size()!=1 : !command.residents.empty())))
        return reject("Invalid settlement command",PathFailure::TargetInvalid);
    if (command.type < CommandType::Recruit && command.settlement_revision)
        return reject("Unexpected settlement revision",PathFailure::TargetInvalid);
    if(command.type>=CommandType::Production && command.type<=CommandType::CancelProduction && (command.building_id<=0 || command.layer!=WorldLayer::Surface || command.building!=BuildingKind::House ||
       (command.type==CommandType::CancelProduction ? !command.residents.empty()||command.x||command.y : command.residents.size()!=1||command.x<0||command.x>=int(recipes.size())||command.y<1||command.y>20)))return reject("Invalid production command");
    player.highest_request = command.request_id;
    player.commands_at.push_back(now);
    player.pending.emplace(command.request_id, command);
    pending_.push_back({id, std::move(command), {}});
    CommandReply reply{pending_.back().command.request_id,
                       0,
                       id,
                       CommandReplyStatus::Queued,
                       PathFailure::None,
                       "Queued"};
    player.replies.push_back(reply);
    return reply;
}
CommandReply GameServer::apply(PlayerId id, const GameCommand &command) {
    if(command.type==CommandType::Lobby)return lobby_command(id,command);
    if(lobby_.enabled&&!lobby_.started)return {command.request_id,world_.ticks(),id,CommandReplyStatus::Rejected,PathFailure::None,"lobby.waiting"};
    const bool resident_command =
        command.type == CommandType::Move || command.type == CommandType::Gather ||
        command.type == CommandType::AssignProfession || command.type == CommandType::Stop ||
        command.type == CommandType::CancelManualOrder ||
        command.type == CommandType::ReleaseControl || command.type == CommandType::TransferControl;
    if (resident_command) {
        CommandReply reply{command.request_id,
                           world_.ticks(),
                           id,
                           CommandReplyStatus::Rejected,
                           PathFailure::ResidentUnavailable,
                           "No residents accepted the command"};
        std::set<int> own;
        for (const auto &resident : world_.residents())
            if (resident.nation == player_nation(id))
                own.insert(resident.id);
        std::vector<int> requested = command.residents;
        if (command.type == CommandType::CancelManualOrder) {
            auto order = std::find_if(world_.manual_orders().begin(), world_.manual_orders().end(),
                                      [&](const auto &value) {
                                          return value.order_id == command.manual_order_id &&
                                                 value.owner_nation == player_nation(id);
                                      });
            if (order == world_.manual_orders().end()) {
                reply.failure = PathFailure::TargetInvalid;
                reply.reason = "Manual order is not controlled or active";
                return reply;
            }
            requested = {order->resident_id};
        }
        const bool needs_holder = command.type == CommandType::CancelManualOrder ||
                                  command.type == CommandType::ReleaseControl ||
                                  command.type == CommandType::TransferControl;
        const auto target = players_.find(command.target_player);
        const bool transfer_valid = command.type != CommandType::TransferControl ||
                                    (target != players_.end() && target->second.connected &&
                                     !target->second.observer && player_nation(command.target_player)==player_nation(id) && command.target_player != id);
        std::vector<int> eligible;
        std::map<int, ResidentCommandOutcome> outcomes;
        for (int resident : requested) {
            if (!own.contains(resident)) {
                outcomes[resident] = {resident, ResidentCommandStatus::Rejected,
                                      PathFailure::ResidentUnavailable};
                continue;
            }
            const auto lease = control_leases_.find(resident);
            const PlayerId holder = lease == control_leases_.end() ? 0 : lease->second.holder;
            const bool bot_holder=holder&&players_.contains(holder)&&players_.at(holder).bot;
            const auto failure = holder && holder != id && !bot_holder ? ControlFailure::HeldByAnother
                                 : needs_holder && holder != id && !bot_holder ? ControlFailure::NotController
                                 : !transfer_valid              ? ControlFailure::TargetUnavailable
                                                                : ControlFailure::None;
            if (failure != ControlFailure::None) {
                outcomes[resident] = {resident, ResidentCommandStatus::Rejected,
                                      PathFailure::ReservationConflict};
                reply.control_rejections.push_back({resident, failure});
            } else
                eligible.push_back(resident);
        }
        std::vector<ResidentCommandOutcome> applied;
        if (!eligible.empty()) {
            if (command.type == CommandType::Move || command.type == CommandType::Gather) {
                if (command.type == CommandType::Gather &&
                    !world_.explored({command.x, command.y, command.layer},player_nation(id))) {
                    for (int resident : eligible)
                        applied.push_back({resident, ResidentCommandStatus::Rejected,
                                           PathFailure::TargetInvalid});
                } else {
                    ManualOrderRequest request{
                        command.type == CommandType::Move ? ManualOrderKind::Move
                                                          : ManualOrderKind::Gather,
                        CellCoord{command.x, command.y, command.layer}, command.gather_completion};
                    applied =
                        world_.issue_manual_order(eligible, request, command.manual_queue_mode,
                                                  ManualOrderIssuer{id, command.request_id});
                }
            } else if (command.type == CommandType::AssignProfession) {
                if (command.assignment.worksite &&
                    !world_
                         .tile(command.assignment.worksite->x, command.assignment.worksite->y,
                               command.assignment.worksite->layer)
                         .explored) {
                    for (int resident : eligible)
                        applied.push_back({resident, ResidentCommandStatus::Rejected,
                                           PathFailure::TargetInvalid});
                } else
                    applied = world_.assign_profession(eligible, command.assignment);
            } else if (command.type == CommandType::Stop)
                applied = world_.stop(eligible);
            else if (command.type == CommandType::CancelManualOrder)
                applied = world_.cancel_manual_order(command.manual_order_id);
            else if (command.type == CommandType::ReleaseControl)
                applied = world_.release_manual_control(eligible);
            else
                for (int resident : eligible)
                    applied.push_back(
                        {resident, ResidentCommandStatus::Applied, PathFailure::None});
        }
        for (const auto &outcome : applied) {
            outcomes[outcome.resident_id] = outcome;
            if (outcome.status == ResidentCommandStatus::Rejected)
                continue;
            auto &lease = control_leases_[outcome.resident_id];
            const PlayerId holder = command.type == CommandType::ReleaseControl ? 0
                                    : command.type == CommandType::TransferControl
                                        ? command.target_player
                                        : id;
            if (lease.holder != holder)
                lease = {holder, ++control_revision_, false};
        }
        for (int resident : requested) {
            auto result = outcomes.find(resident);
            reply.residents.push_back(result == outcomes.end()
                                          ? ResidentCommandOutcome{resident,
                                                                   ResidentCommandStatus::Rejected,
                                                                   PathFailure::ResidentUnavailable}
                                          : result->second);
            if (reply.residents.back().status != ResidentCommandStatus::Rejected) {
                reply.status = CommandReplyStatus::Applied;
                reply.failure = PathFailure::None;
                reply.reason = "Resident command accepted";
            }
        }
        if (reply.status == CommandReplyStatus::Rejected && !reply.residents.empty()) {
            reply.failure = reply.residents.front().failure;
            if (!reply.control_rejections.empty())
                reply.reason = "Resident control conflict";
        }
        return reply;
    }
    CommandResult result{false, "Session no longer exists"};
    if (players_.contains(id)) {
        if(command.type==CommandType::Wilderness){
            auto members=command.residents;
            if(int(command.building)>=int(WildernessAction::Recall)&&int(command.building)<=int(WildernessAction::Retreat))
                for(const auto&e:world_.economy(player_nation(id)).wilderness.expeditions)if(e.id==command.building_id)members=e.members;
            bool allowed=true;for(int worker:members){auto lease=control_leases_.find(worker);if(lease!=control_leases_.end()&&lease->second.holder&&lease->second.holder!=id)allowed=false;}
            result=allowed?world_.wilderness(WildernessAction(command.building),command.residents,command.x,command.y,command.building_id,command.settlement_revision,player_nation(id)):CommandResult{false,"wilderness.not_controller"};
        }else if(command.type>=CommandType::Production){
            int worker=command.residents.empty()?0:command.residents.front();
            if(command.type==CommandType::CancelProduction)for(const auto&o:world_.economy(player_nation(id)).orders)if(o.id==command.building_id)worker=o.resident_id;
            const auto lease=control_leases_.find(worker);
            if(lease!=control_leases_.end()&&lease->second.holder&&lease->second.holder!=id){
                CommandReply reply{command.request_id,world_.ticks(),id,CommandReplyStatus::Rejected,PathFailure::ReservationConflict,"economy.not_controller"};
                reply.residents.push_back({worker,ResidentCommandStatus::Rejected,PathFailure::ReservationConflict});
                reply.control_rejections.push_back({worker,ControlFailure::HeldByAnother});
                return reply;
            }
            else result=command.type==CommandType::CancelProduction?world_.cancel_production(command.building_id,command.settlement_revision,player_nation(id)):world_.production(command.building_id,worker,command.x,command.y,command.settlement_revision,command.type==CommandType::Upgrade,player_nation(id));
            if(result.accepted&&command.type!=CommandType::CancelProduction){
                auto&control=control_leases_[worker];
                if(control.holder!=id)control={id,++control_revision_,false};
            }
        } else if (command.type >= CommandType::Recruit) {
            bool allowed=true;
            if(command.type==CommandType::YouthWork) {
                const int resident=command.residents.front();
                const auto lease=control_leases_.find(resident);
                allowed=lease==control_leases_.end() || !lease->second.holder || lease->second.holder==id;
            }
            result=allowed ? world_.decide(command.type==CommandType::Recruit?SettlementDecision::Recruit:
                                          command.type==CommandType::NightShift?SettlementDecision::NightShift:SettlementDecision::YouthWork,
                                          command.settlement_revision,command.residents.empty()?0:command.residents.front(),player_nation(id))
                           : CommandResult{false,"settlement.not_controller"};
        } else if (command.type == CommandType::CancelConstruction) {
            const auto &orders = world_.construction_orders();
            const auto order = std::find_if(orders.begin(), orders.end(), [&](const auto &site) {
                return site.building_id == command.building_id && site.owner_nation == player_nation(id) &&
                       site.phase != ConstructionPhase::Complete &&
                       site.phase != ConstructionPhase::Cancelled &&
                       world_.tile(site.site.x, site.site.y, site.site.layer).explored;
            });
            result = order == orders.end()
                         ? CommandResult{false, "Construction site is not controlled or active", 0,
                                         false, PathFailure::TargetInvalid}
                         : world_.cancel_construction(command.building_id);
        } else if (command.type == CommandType::Build)
            result = world_.build(command.building, command.x, command.y, command.layer,player_nation(id));
    }
    if (result.pending && result.path_batch_id) {
        if (path_batches_
                .emplace(result.path_batch_id,
                         PathBatchLink{id, command.request_id, path_command_kind(command.type)})
                .second)
            return {command.request_id, 0,       id, CommandReplyStatus::Queued,
                    PathFailure::None,  "Queued"};
        result = {false, "Duplicate path batch identity", 0, false, PathFailure::LimitExceeded};
    } else if (result.pending)
        result = {false, "Missing path batch identity", 0, false, PathFailure::LimitExceeded};
    return {command.request_id,
            world_.ticks(),
            id,
            result.accepted ? CommandReplyStatus::Applied : CommandReplyStatus::Rejected,
            result.failure,
            result.message};
}
void GameServer::complete(PlayerId id, GameCommand command, CommandReply reply) {
    auto it = players_.find(id);
    if (it == players_.end())
        return;
    auto &player = it->second;
    player.pending.erase(command.request_id);
    player.completed.insert_or_assign(command.request_id, Cached{std::move(command), reply});
    while (player.completed.size() > 2048)
        player.completed.erase(player.completed.begin());
    player.replies.push_back(std::move(reply));
}
void GameServer::queue_expired_leases() {
    Pending event{0, {}, {}};
    const auto now = config_.clock();
    for (auto &[resident, lease] : control_leases_) {
        const auto holder = players_.find(lease.holder);
        if (!lease.holder || lease.expiry_queued || holder == players_.end() ||
            holder->second.connected || !holder->second.disconnected_clock ||
            now - *holder->second.disconnected_clock < std::chrono::seconds(30))
            continue;
        lease.expiry_queued = true;
        event.expired_controls.emplace_back(resident, lease.revision);
    }
    if (!event.expired_controls.empty())
        pending_.push_back(std::move(event));
}
void GameServer::pump_boundary() {
    queue_expired_leases();
    bool changed = false;
    while (!pending_.empty()) {
        auto next = std::move(pending_.front());
        pending_.pop_front();
        if (!next.expired_controls.empty()) {
            std::vector<int> released;
            for (const auto &[resident, generation] : next.expired_controls) {
                auto lease = control_leases_.find(resident);
                if (lease == control_leases_.end() || lease->second.revision != generation ||
                    !lease->second.holder)
                    continue;
                released.push_back(resident);
                lease->second = {0, ++control_revision_, false};
            }
            if (!released.empty()) {
                world_.release_manual_control(released);
                changed = true;
            }
            continue;
        }
        auto it = players_.find(next.player);
        if (it == players_.end())
            continue;
        auto reply = apply(next.player, next.command);
        if (reply.status != CommandReplyStatus::Queued)
            complete(next.player, next.command, std::move(reply));
        changed = true;
        queue_expired_leases();
    }
    if (changed)
        ++revision_;
}
void GameServer::tick() {
    pump_boundary();
    if(lobby_.enabled&&(!lobby_.started||lobby_.paused))return;
    tick_bots();
    world_.tick();
    for (auto &outcome : world_.take_path_results()) {
        // Internal work admission and hauling never complete a player's command.
        if (outcome.command == PathCommandKind::Logistics ||
            outcome.command == PathCommandKind::Work || outcome.command == PathCommandKind::Manual)
            continue;
        auto batch = path_batches_.find(outcome.path_batch_id);
        if (batch == path_batches_.end())
            continue;
        const auto [player_id, request_id, command_kind] = batch->second;
        path_batches_.erase(batch);
        auto player = players_.find(player_id);
        if (player == players_.end())
            continue;
        auto pending = player->second.pending.find(request_id);
        if (pending == player->second.pending.end())
            continue;
        const bool matching_command = outcome.command == command_kind;
        CommandReply reply{request_id,
                           world_.ticks(),
                           player_id,
                           matching_command && outcome.accepted ? CommandReplyStatus::Applied
                                                                : CommandReplyStatus::Rejected,
                           matching_command ? outcome.failure : PathFailure::TopologyChanged,
                           matching_command ? outcome.message
                                            : "Path result command does not match request"};
        complete(player_id, pending->second, std::move(reply));
    }
    ++revision_;
}
std::vector<CommandReply> GameServer::take_replies(PlayerId id) {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return {};
    std::vector<CommandReply> result(it->second.replies.begin(), it->second.replies.end());
    it->second.replies.clear();
    return result;
}
std::vector<CommandReply> GameServer::pending_replies(PlayerId id) const {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return {};
    return {it->second.replies.begin(), it->second.replies.end()};
}
void GameServer::acknowledge_reply(PlayerId id, ReplyReceipt receipt) {
    acknowledge_replies(id, std::span(&receipt, 1));
}
void GameServer::acknowledge_replies(PlayerId id, std::span<const ReplyReceipt> receipts) {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return;
    std::erase_if(it->second.replies, [&](const auto &reply) {
        return std::any_of(receipts.begin(), receipts.end(), [&](const auto &receipt) {
            if (receipt.request_id != reply.request_id)
                return false;
            if (receipt.status == CommandReplyStatus::Queued)
                return reply.status == CommandReplyStatus::Queued;
            return reply.status == CommandReplyStatus::Queued || reply.status == receipt.status;
        });
    });
}
const Tile &ViewSnapshot::tile(int x, int y, WorldLayer layer) const {
    static const Tile hidden{};
    if (!inside_world(x, y) || !valid_layer(layer))
        return hidden;
    const auto key = chunk_key({x, y, layer});
    auto chunk =
        std::find_if(chunks.begin(), chunks.end(), [&](const auto &c) { return c.key == key; });
    return chunk == chunks.end() ? hidden : chunk->tiles[local_index(x, y)];
}
ViewSnapshot GameServer::snapshot(PlayerId id) const {
    ViewSnapshot result;
    if (!connected(id))
        return result;
    result.terrain_generator = world_.terrain_generator();
    result.tick = world_.ticks();
    result.weather = world_.weather();
    const auto calendar = world_.calendar();
    result.day = calendar.day;
    result.winter = calendar.season == Season::Winter;
    result.revision = revision_;
    result.control_epoch = control_epoch_;
    result.lobby = lobby_snapshot();
    // Team selection is authoritative only after Start creates the settlements.
    // Do not expose the seed world's nation 1 to teammates still in the lobby.
    if(lobby_.enabled&&!lobby_.started)return result;
    for (const auto &[player_id, player] : players_)
        if (player.connected && !player.observer && player_nation(player_id)==player_nation(id))
            result.control_players.push_back({player_id, player.name});
    result.inventory = world_.inventory(player_nation(id));
    result.resources = result.inventory.total;
    result.settlement = world_.settlement(player_nation(id));
    result.economy = world_.economy(player_nation(id));
    for (const auto &resident : world_.residents())
        if (resident.nation == player_nation(id)) {
            const auto lease = control_leases_.find(resident.id);
            const auto holder_id = lease == control_leases_.end() ? 0 : lease->second.holder;
            const auto holder = players_.find(holder_id);
            result.resident_controls.push_back(
                {resident.id, holder_id,
                 holder == players_.end() ? std::string{} : holder->second.name,
                 holder_id != 0 && holder_id == id,
                 holder != players_.end() && !holder->second.connected,
                 lease == control_leases_.end() ? 0 : lease->second.revision});
            result.residents.push_back(resident);
            auto &projected = result.residents.back();
            projected.pending_path_batch = 0;
            if (!world_.tile(projected.target_x, projected.target_y, projected.layer).explored) {
                projected.target_x = static_cast<int>(std::lround(projected.x));
                projected.target_y = static_cast<int>(std::lround(projected.y));
            }
            projected.additional_xp = {};
            projected.skill_practice = {};
            projected.abilities_bp = {};
            projected.profession = {};
            ResidentProfessionView profile;
            profile.resident_id = resident.id;
            profile.assignment = resident.profession.assignment;
            if (profile.assignment.worksite &&
                !world_
                     .tile(profile.assignment.worksite->x, profile.assignment.worksite->y,
                           profile.assignment.worksite->layer)
                     .explored)
                profile.assignment.worksite.reset();
            profile.manual_override = resident.profession.manual_override;
            profile.deferred = resident.profession.deferred;
            profile.wait_reason = resident.profession.wait_reason;
            const std::array<int, 3> legacy{resident.wood_xp, resident.mining_xp,
                                            resident.farming_xp};
            for (std::size_t skill = 0; skill < skill_count; ++skill) {
                const auto kind = static_cast<Skill>(skill);
                profile.skills[skill] = {
                    kind,
                    skill < legacy.size() ? legacy[skill]
                                          : resident.additional_xp[skill - legacy.size()],
                    world_.skill_breakdown(resident.id, kind).value_or(ModifierBreakdown{})};
            }
            result.resident_professions.push_back(std::move(profile));
        }
    for (const auto &chunk : world_.chunks()) {
        if(std::none_of(chunk.tiles.begin(),chunk.tiles.end(),[](const auto&t){return t.explored;}))continue;
        bool any=false;
        Chunk filtered{chunk.key}; // Never expose revisions caused by hidden tile changes.
        // This is a presentation revision, not the authoritative chunk counter:
        // hash only cells already admitted to this player's filtered view. Hidden
        // mutations therefore cannot become a fog-of-war side channel.
        std::uint64_t visible_revision=1469598103934665603ull;
        const auto mix=[&](std::uint64_t value) {
            for(int byte=0;byte<8;++byte) {
                visible_revision^=static_cast<std::uint8_t>(value>>(byte*8));
                visible_revision*=1099511628211ull;
            }
        };
        mix(static_cast<std::uint8_t>(chunk.key.layer));
        mix(static_cast<std::uint32_t>(chunk.key.cx));
        mix(static_cast<std::uint32_t>(chunk.key.cy));
        for (std::size_t i = 0; i < chunk.tiles.size(); ++i)
            if (world_.explored({chunk.key.cx*chunk_size+int(i%chunk_size),chunk.key.cy*chunk_size+int(i/chunk_size),chunk.key.layer},player_nation(id))) {
                const auto &tile=chunk.tiles[i];
                filtered.tiles[i]=tile;
                any=true;
                mix(i);
                mix(static_cast<std::uint32_t>(tile.terrain));
                mix(static_cast<std::uint32_t>(tile.amount));
                mix(tile.explored);
                mix(static_cast<std::uint8_t>(tile.biome));
                mix(static_cast<std::uint16_t>(tile.elevation));
                mix(tile.trail_wear);
            }
        if(any) {
            filtered.revision=visible_revision;
            result.chunks.push_back(std::move(filtered));
        }
    }
    for (const auto &portal : world_.portals())
        if (world_.explored(portal.surface,player_nation(id)) &&
            world_.explored(portal.mine,player_nation(id)))
            result.portals.push_back(portal);
    std::sort(result.chunks.begin(), result.chunks.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    for (const auto &b : world_.buildings())
        if (b.nation == player_nation(id) && world_.explored({b.x, b.y, b.layer},player_nation(id)))
            result.buildings.push_back(b);
    std::map<int, std::uint32_t> storage_used;
    for (const auto &stack : world_.item_stacks())
        if (stack.owner_nation == player_nation(id) &&
            stack.location.kind == ItemLocationKind::Storage)
            storage_used[stack.location.entity_id] += stack.quantity;
    for (const auto &storage : world_.storages()) {
        const auto building =
            std::find_if(result.buildings.begin(), result.buildings.end(), [&](const auto &candidate) {
                return candidate.id == storage.building_id;
            });
        // Production buildings own internal buffers, but those are private
        // simulation details rather than player-facing stores. Project only
        // the storage buildings accepted by the public snapshot contract.
        if (building == result.buildings.end() ||
            (building->kind != BuildingKind::Camp && building->kind != BuildingKind::Stockpile))
            continue;
        result.storages.push_back(
            {storage.building_id, storage_used[storage.building_id], storage.capacity});
    }
    // These projections never include stack coordinates, source/reservation IDs,
    // or another nation's task. Resolve carry once, then emit in stable entity order.
    std::map<int, ResidentLogisticsView> logistics;
    std::set<int> own_residents;
    for (const auto &resident : result.residents)
        own_residents.insert(resident.id);
    for (const auto &task : world_.logistics_tasks()) {
        if (!own_residents.contains(task.resident_id))
            continue;
        auto found = logistics.find(task.resident_id);
        // Tasks are ordered by stable ID. A later completion retires an older
        // failure; an active assignment takes precedence over terminal history.
        const auto terminal = [](LogisticsPhase phase) {
            return phase == LogisticsPhase::Failed || phase == LogisticsPhase::Complete;
        };
        if (found != logistics.end() && !terminal(found->second.phase) && terminal(task.phase))
            continue;
        logistics[task.resident_id] = {task.resident_id,        task.item,  0,
                                       resident_carry_capacity, task.phase, task.failure};
    }
    for (const auto &stack : world_.item_stacks())
        if (stack.owner_nation == player_nation(id) && stack.location.kind == ItemLocationKind::Carried &&
            own_residents.contains(stack.location.entity_id)) {
            auto [entry, inserted] = logistics.try_emplace(stack.location.entity_id);
            auto &value = entry->second;
            value.resident_id = stack.location.entity_id;
            value.item = stack.item;
            value.quantity += stack.quantity;
            if (inserted)
                value.phase = LogisticsPhase::ToDropoff;
        }
    for (const auto &[resident_id, value] : logistics) {
        (void)resident_id;
        if (value.phase != LogisticsPhase::Complete)
            result.resident_logistics.push_back(value);
    }
    std::map<int, const WorkIntentState *> latest_work;
    const auto active_work = [](WorkIntentPhase phase) {
        return phase == WorkIntentPhase::Waiting || phase == WorkIntentPhase::PathPending;
    };
    for (const auto &intent : world_.work_intents()) {
        if (intent.owner_nation != player_nation(id) || !own_residents.contains(intent.resident_id))
            continue;
        auto found = latest_work.find(intent.resident_id);
        if (found != latest_work.end() && active_work(found->second->phase) &&
            !active_work(intent.phase))
            continue;
        latest_work[intent.resident_id] = &intent;
    }
    for (const auto &resident : result.residents) {
        auto found = latest_work.find(resident.id);
        if (found == latest_work.end())
            continue;
        const auto &intent = *found->second;
        if (intent.phase == WorkIntentPhase::Complete &&
            intent.completion != WorkCompletionReason::Exhausted)
            continue;
        if (!active_work(intent.phase)) {
            auto hauling = logistics.find(resident.id);
            const bool active_haul = hauling != logistics.end() &&
                                     hauling->second.phase != LogisticsPhase::Complete &&
                                     hauling->second.phase != LogisticsPhase::Failed;
            if (resident.job != Job::Idle || resident.order_phase == OrderPhase::PathPending ||
                active_haul)
                continue;
        }
        result.resident_work.push_back({resident.id, intent.kind, intent.phase, intent.failure,
                                        intent.completion, intent.attempts});
    }
    std::map<int, float> work;
    for (const auto &building : result.buildings)
        work.emplace(building.id, building.progress);
    std::map<int, LogisticsFailure> failures;
    for (const auto &task : world_.logistics_tasks())
        if (task.kind == LogisticsTaskKind::DeliverConstruction &&
            task.phase == LogisticsPhase::Failed)
            failures[task.destination_building_id] = task.failure;
    std::map<int, ConstructionWorkforce> workforce;
    for (const auto &value : world_.construction_workforce())
        workforce[value.building_id] = value;
    std::map<int, const WorkIntentState *> site_intents;
    for (const auto &intent : world_.work_intents())
        if (intent.owner_nation == player_nation(id) && intent.kind == WorkIntentKind::BuildAssignment)
            site_intents[intent.building_id] = &intent;
    for (const auto &order : world_.construction_orders())
        if (order.owner_nation == player_nation(id) && order.phase != ConstructionPhase::Complete &&
            order.phase != ConstructionPhase::Cancelled &&
            world_.tile(order.site.x, order.site.y, order.site.layer).explored)
            result.construction_sites.push_back(
                {order.building_id, order.kind, order.site, order.required, order.delivered,
                 work.contains(order.building_id) ? work.at(order.building_id) : 0.F, order.phase,
                 failures.contains(order.building_id) ? failures.at(order.building_id)
                                                      : LogisticsFailure::None});
    std::sort(result.construction_sites.begin(), result.construction_sites.end(),
              [](const auto &a, const auto &b) { return a.building_id < b.building_id; });
    for (auto &site : result.construction_sites) {
        if (site.phase != ConstructionPhase::Work)
            continue;
        auto workers = workforce.find(site.building_id);
        if (workers != workforce.end()) {
            site.assigned_workers =
                workers->second.working + workers->second.travelling + workers->second.planning;
            site.worker_phase = workers->second.working      ? ConstructionWorkerPhase::Working
                                : workers->second.travelling ? ConstructionWorkerPhase::Travelling
                                : workers->second.planning   ? ConstructionWorkerPhase::Planning
                                                             : ConstructionWorkerPhase::Waiting;
        } else {
            site.worker_phase = ConstructionWorkerPhase::Waiting;
        }
        if (!site.assigned_workers) {
            auto intent = site_intents.find(site.building_id);
            if (intent != site_intents.end() && intent->second->phase == WorkIntentPhase::Failed)
                site.worker_failure = intent->second->failure;
        }
    }
    for (const auto &order : world_.manual_orders()) {
        if (order.owner_nation != player_nation(id) || !own_residents.contains(order.resident_id))
            continue;
        auto target = order.request.target;
        if (target && !world_.tile(target->x, target->y, target->layer).explored)
            target.reset();
        result.manual_orders.push_back({order.order_id, order.resident_id, order.request.kind,
                                        target, order.request.gather_completion, order.phase,
                                        order.failure, order.completion});
    }
    result.housing = world_.housing(player_nation(id));
    result.tutorial_step = world_.tutorial_step();
    return result;
}
std::optional<ChatEntry> GameServer::chat(PlayerId id, std::string text, std::string &error) {
    error.clear();
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected) {
        error = "Unauthenticated session";
        return {};
    }
    if (!valid_utf8_text(text, 1024, true) ||
        std::count_if(text.begin(), text.end(),
                      [](unsigned char c) { return (c & 0xc0) != 0x80; }) > 256) {
        error = "Invalid or oversized chat message";
        return {};
    }
    if (text.front() == '/') {
        error = "Unsupported chat command";
        return {};
    }
    auto &player = it->second;
    const auto now = config_.clock();
    while (!player.chats_at.empty() && now - player.chats_at.front() >= std::chrono::seconds(10))
        player.chats_at.pop_front();
    if (player.chats_at.size() >= 5) {
        error = "Chat rate limit";
        return {};
    }
    player.chats_at.push_back(now);
    ChatEntry entry{++chat_sequence_, id, player.name, std::move(text)};
    chat_.push_back(entry);
    if (chat_.size() > 200)
        chat_.pop_front();
    return entry;
}
std::vector<ChatEntry> GameServer::chat_since(PlayerId id, std::uint64_t sequence) const {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.connected)
        return {};
    std::vector<ChatEntry> result;
    for (const auto &entry : chat_)
        if (entry.sequence > sequence && !it->second.muted.contains(entry.author))
            result.push_back(entry);
    return result;
}
bool GameServer::mute(PlayerId id, PlayerId author, bool muted) {
    if (!connected(id) || !players_.contains(author))
        return false;
    if (muted)
        players_.at(id).muted.insert(author);
    else
        players_.at(id).muted.erase(author);
    return true;
}
bool GameServer::kick(PlayerId actor, PlayerId target, bool ban) {
    if (actor != host_ || !connected(actor) || !players_.contains(target) || target == actor)
        return false;
    if (ban)
        banned_tokens_.insert(players_.at(target).token);
    disconnect(target);
    return true;
}
bool GameServer::save(const std::filesystem::path &path, std::string &error) const {
    if(!lobby_.enabled)return world_.save(path,error);
    nlohmann::json metadata={{"lobby",encode_lobby(lobby_snapshot())},{"next_player",next_player_},{"epoch",control_epoch_},{"players",nlohmann::json::array()}};
    for(const auto&[id,p]:players_)metadata["players"].push_back({{"id",id},{"token",p.token},{"highest",p.highest_request},{"observer",p.observer}});
    return world_.save(path,error,metadata.dump());
}
bool GameServer::load(const std::filesystem::path &path, std::string &error) {
    GameServer candidate(config_);
    candidate.control_epoch_=control_epoch_;
    candidate.next_player_=next_player_;
    candidate.banned_tokens_=banned_tokens_;
    candidate.revision_=revision_;
    const bool ok=candidate.load_in_place(path,error);
    load_info_=candidate.world_.last_load();
    if(!ok) {
        return false;
    }
    candidate.load_info_=load_info_;
    *this=std::move(candidate);
    return true;
}
bool GameServer::load_in_place(const std::filesystem::path &path, std::string &error) {
    if (!world_.load(path, error))
        return false;
    if(config_.wildlife_enabled)world_.enable_wildlife();
    // Loading is a new session epoch; invalidate all outstanding rights/requests and require join
    // again.
    players_.clear();
    pending_.clear();
    path_batches_.clear();
    control_leases_.clear();
    ++control_epoch_;
    control_revision_ = 0;
    chat_.clear();
    host_ = 0;
    chat_sequence_ = 0;
    lobby_={};lobby_.enabled=config_.lobby_enabled;lobby_.capacity=static_cast<int>(std::max(std::size_t{2},config_.max_players));
    const auto metadata=world_.export_state().server_metadata;
    if(!metadata.empty()){
        try{
            const auto m=nlohmann::json::parse(metadata);lobby_=decode_lobby(m.at("lobby").get<std::string>());
            if(!lobby_.enabled||m.at("players").size()>6)throw std::runtime_error("Invalid saved lobby");
            next_player_=m.at("next_player");control_epoch_=m.at("epoch").get<std::uint64_t>()+1;host_=lobby_.host;
            for(const auto&v:lobby_.players){
                const auto account=std::find_if(m.at("players").begin(),m.at("players").end(),[&](const auto&p){return p.at("id").template get<PlayerId>()==v.id;});
                if(account==m.at("players").end())throw std::runtime_error("Missing saved account");
                Player p;p.name=v.name;p.team=v.team;p.bot=v.bot;p.connected=v.bot;p.ready=v.bot;p.token=account->at("token");p.highest_request=account->at("highest");p.observer=account->at("observer");
                if(p.token.size()!=64||next_player_<=v.id)throw std::runtime_error("Invalid saved account");p.disconnected_clock=config_.clock();players_.emplace(v.id,std::move(p));
            }
            lobby_.players.clear();lobby_.paused=lobby_.started;lobby_.pause_author=host_;config_.max_players=lobby_.capacity;
        }catch(const std::exception&){error="Invalid saved online session.";return false;}
    }
    ++revision_;
    return true;
}
} // namespace ribterra

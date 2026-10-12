#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/terrain_rules.hpp"

#include "ribterra/network_session.hpp"

#include <algorithm>
#include <deque>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace ribterra {
namespace {
constexpr bool inside(int x, int y) { return inside_world(x, y); }
} // namespace

struct ClientSession::Impl {
    std::unique_ptr<GameServer> embedded;
    std::unique_ptr<net::NetworkClient> network;
    ViewSnapshot view;
    std::string player_name;
    PlayerId player{};
    std::uint64_t next_request{1};
    std::uint64_t view_generation{};
    bool remote_snapshot_received{};
    net::WorldId presented_world_id{};
    std::optional<std::string> notice;
    std::deque<CommandReply> command_replies;
    std::string last_network_error;
    SaveLoadInfo load_info;
    std::vector<ChatEntry> chat;
    std::uint64_t local_chat_sequence{};

    Impl(std::uint32_t seed, std::string name, ClimateConfig climate) : player_name(std::move(name)) {
        ServerConfig config;
        config.seed = seed;
        config.climate = climate;
        embedded = std::make_unique<GameServer>(std::move(config));
        const JoinResult joined = embedded->join(player_name, embedded->config().versions);
        if (!joined.accepted)
            throw std::invalid_argument("Cannot start local player: " + joined.reason);
        player = joined.player;
        refresh_local();
    }

    #include "client_production_group.inc"

    void refresh_local() { view = embedded->snapshot(player); ++view_generation; }

    void remember_reply(const CommandReply &reply) {
        group_reply(reply);
        if(group_sites.empty()&&!group_status.empty())group_status=std::to_string(group_applied)+" orders started; "+std::to_string(group_skipped)+" skipped.";
        command_replies.push_back(reply);
        if (command_replies.size() > 4096)
            command_replies.pop_front();
    }
    void receive_local_replies() {
        for (const CommandReply &reply : embedded->take_replies(player))
            remember_reply(reply);
    }

    CommandResult queue(CommandType type, const std::vector<int> &ids, int x, int y,
                        BuildingKind kind = BuildingKind::House,
                        WorldLayer layer = WorldLayer::Surface, int building_id = 0,
                        const ProfessionAssignment &assignment = {},
                        ManualQueueMode mode = ManualQueueMode::Replace,
                        GatherCompletion completion = GatherCompletion::UntilExhausted,
                        std::uint64_t manual_order_id = 0, PlayerId target_player = 0, std::uint64_t settlement_revision = 0) {
        if (!inside(x, y) || !valid_layer(layer))
            return {false, "Ціль поза межами долини."};
        GameCommand command{0, 0, view.tick, type, ids, x, y, kind, layer, building_id};
        command.assignment = assignment;
        command.manual_queue_mode = mode;
        command.gather_completion = completion;
        command.manual_order_id = manual_order_id;
        command.target_player = target_player;
        command.control_epoch = view.control_epoch;
        command.settlement_revision = settlement_revision;
        if (network) {
            if (!network->ready())
                return {false, "З’єднання із сервером ще не готове."};
            last_issued_request=network->command(std::move(command));
            return last_issued_request != 0
                       ? CommandResult{true, "Команду надіслано.", 0, true}
                       : CommandResult{false, network->last_error()};
        }
        command.request_id = next_request++;
        last_issued_request=command.request_id;
        command.author = player;
        const CommandReply reply = embedded->enqueue(player, std::move(command));
        if (reply.status == CommandReplyStatus::Queued)
            return {true, "Команду поставлено в чергу.", 0, true};
        return {reply.status == CommandReplyStatus::Applied, reply.reason, 0, false, reply.failure};
    }
};

ClientSession::ClientSession(std::uint32_t seed, std::string player_name, ClimateConfig climate)
    : impl_(std::make_unique<Impl>(seed, std::move(player_name), climate)) {}
ClientSession::~ClientSession() = default;
std::string ClientSession::resume_token()const{return impl_->network?impl_->network->resume_token():std::string{};}
const LobbyState &ClientSession::lobby()const{return impl_->view.lobby;}
bool ClientSession::can_change_speed() const {
    return !remote() || (ready() && lobby().enabled && lobby().started &&
                         lobby().host == player_id());
}
CommandResult ClientSession::lobby_action(LobbyAction a,int value){
    if (a == LobbyAction::Speed && !can_change_speed())
        return {false, "lobby.host_only"};
    return impl_->queue(CommandType::Lobby,{},int(a),value);
}

bool ClientSession::connect_remote(std::string_view endpoint, std::string password,
                                   std::string &error,std::string resume_token) {
    error.clear();
    net::ClientConfig config;
    config.name = impl_->player_name;
    config.password = std::move(password);
    config.resume_token=std::move(resume_token);
    config.auto_reconnect = true;
    auto client = std::make_unique<net::NetworkClient>(std::move(config));
    if (!client->connect(endpoint)) {
        error = client->last_error();
        return false;
    }
    impl_->network = std::move(client);
    impl_->view = {};
    impl_->remote_snapshot_received = false;
    impl_->presented_world_id = {};
    ++impl_->view_generation;
    impl_->last_network_error.clear();
    impl_->command_replies.clear();
    impl_->clear_group();
    return true;
}

void ClientSession::poll() {
    if (!impl_->network) {
        impl_->embedded->pump_boundary();
        impl_->receive_local_replies();
        if (impl_->view.revision != impl_->embedded->revision())
            impl_->refresh_local();
        impl_->pump_group();
        return;
    }
    impl_->network->poll();
    if (!impl_->network->ready())
        impl_->remote_snapshot_received = false;
    if (impl_->network->ready() &&
        (!impl_->remote_snapshot_received || impl_->presented_world_id != impl_->network->world_id() ||
         impl_->view.control_epoch != impl_->network->view().control_epoch ||
         impl_->view.revision != impl_->network->view().revision)) {
        impl_->view = impl_->network->view();
        impl_->remote_snapshot_received = true;
        impl_->presented_world_id = impl_->network->world_id();
        ++impl_->view_generation;
    }
    {
        auto incoming = impl_->network->take_chat();
        impl_->chat.insert(impl_->chat.end(), std::make_move_iterator(incoming.begin()),
                           std::make_move_iterator(incoming.end()));
    }
    for (const CommandReply &reply : impl_->network->take_replies())
        impl_->remember_reply(reply);
    impl_->pump_group();
    const std::string error = impl_->network->last_error();
    if (!error.empty() && error != impl_->last_network_error) {
        impl_->notice = error.starts_with("network.") ? error : "Мережа: " + error;
        impl_->last_network_error = error;
    }
}

void ClientSession::advance() {
    if (impl_->network) {
        poll();
        return;
    }
    impl_->embedded->tick();
    impl_->receive_local_replies();
    impl_->refresh_local();
    impl_->pump_group();
}

bool ClientSession::remote() const { return impl_->network != nullptr; }
std::uint64_t ClientSession::revision() const { return impl_->view_generation; }
bool ClientSession::ready() const { return impl_->network ? impl_->network->ready() : true; }
bool ClientSession::snapshot_ready() const {
    return !impl_->network || impl_->remote_snapshot_received;
}
bool ClientSession::world_ready() const {
    if (!impl_->network)
        return true;
    if (!snapshot_ready() || impl_->view.chunks.empty())
        return false;
    return !impl_->view.lobby.enabled || impl_->view.lobby.started;
}
bool ClientSession::reconnecting() const { return impl_->network && impl_->network->reconnecting(); }
bool ClientSession::recovery_incomplete() const { return impl_->network && impl_->network->recovery_incomplete(); }
bool ClientSession::send_chat(std::string text, std::string &error) {
    error.clear();
    if (impl_->network) {
        if (!impl_->network->ready()) {
            error = "Connection is not ready";
            return false;
        }
        if (!impl_->network->chat(std::move(text))) {
            error = impl_->network->last_error();
            return false;
        }
        return true;
    }
    auto entry = impl_->embedded->chat(impl_->player, std::move(text), error);
    if (!entry)
        return false;
    auto incoming = impl_->embedded->chat_since(impl_->player, impl_->local_chat_sequence);
    if (!incoming.empty())
        impl_->local_chat_sequence = incoming.back().sequence;
    impl_->chat.insert(impl_->chat.end(), std::make_move_iterator(incoming.begin()),
                       std::make_move_iterator(incoming.end()));
    return true;
}
std::vector<ChatEntry> ClientSession::take_chat() {
    std::vector<ChatEntry> result;
    result.swap(impl_->chat);
    return result;
}
CommandResult ClientSession::move(const std::vector<int> &ids, int x, int y, WorldLayer layer,
                                  ManualQueueMode mode) {
    return impl_->queue(CommandType::Move, ids, x, y, BuildingKind::House, layer, 0, {}, mode);
}
CommandResult ClientSession::wilderness(WildernessAction action,const std::vector<int>&members,int x,int y,int argument,std::uint64_t revision){
 return impl_->queue(CommandType::Wilderness,members,x,y,BuildingKind(action),WorldLayer::Surface,argument,{},ManualQueueMode::Replace,GatherCompletion::UntilExhausted,0,0,revision);
}
const EconomyState &ClientSession::economy() const {return impl_->view.economy;}
CommandResult ClientSession::production(int building,int resident,int recipe,int quantity,std::uint64_t revision,bool upgrade){
 if(upgrade&&resident==0){
  for(const auto&r:impl_->view.residents)if(impl_->worker_reason(r.id,true).empty()){resident=r.id;break;}
  if(!resident)return {false,"inspector.hint.no_worker"};
 }
 return impl_->queue(upgrade?CommandType::Upgrade:CommandType::Production,{resident},recipe,quantity,BuildingKind::House,WorldLayer::Surface,building,{},ManualQueueMode::Replace,GatherCompletion::UntilExhausted,0,0,revision);
}
const std::string& ClientSession::production_group_status() const {return impl_->group_status;}
std::string ClientSession::production_feedback(int building_id) const {
 for(const auto& entry:impl_->group_feedback)if(entry.building_id==building_id)return entry.reason;
 return {};
}
const std::vector<ClientSession::ProductionResult>& ClientSession::production_results() const {return impl_->group_feedback;}
std::string ClientSession::production_worker_reason(int id,bool automatic) const {return impl_->worker_reason(id,automatic);}
CommandResult ClientSession::production_group(const std::vector<int>& buildings,int recipe,int batches,int worker){
 if(impl_->group_request||impl_->group_wait_snapshot||!impl_->group_sites.empty())return {false,"production.group_pending"};
 if(buildings.empty()||buildings.size()>256||recipe<0||recipe>=int(recipes.size())||batches<1||batches>20||worker<0)return {false,"economy.invalid"};
 impl_->clear_group();impl_->group_applied=impl_->group_skipped=0;impl_->group_recipe=recipe;impl_->group_batches=batches;impl_->group_preferred_worker=worker;
 for(int id:buildings)if(std::find(impl_->group_sites.begin(),impl_->group_sites.end(),id)==impl_->group_sites.end()){impl_->group_sites.push_back(id);impl_->group_feedback.push_back({id,0,"production.queued"});}
 impl_->pump_group();return {true,"Group order prepared.",0,true};
}
CommandResult ClientSession::cancel_production(int order,std::uint64_t revision){
 return impl_->queue(CommandType::CancelProduction,{},0,0,BuildingKind::House,WorldLayer::Surface,order,{},ManualQueueMode::Replace,GatherCompletion::UntilExhausted,0,0,revision);
}
const SettlementState &ClientSession::settlement() const { return impl_->view.settlement; }
CommandResult ClientSession::decide(SettlementDecision decision,std::uint64_t revision,int id) {
    const auto type=decision==SettlementDecision::Recruit?CommandType::Recruit:
                    decision==SettlementDecision::NightShift?CommandType::NightShift:CommandType::YouthWork;
    return impl_->queue(type,id?std::vector<int>{id}:std::vector<int>{},0,0,BuildingKind::House,WorldLayer::Surface,
                        0,{},ManualQueueMode::Replace,GatherCompletion::UntilExhausted,0,0,revision);
}
CommandResult ClientSession::gather(const std::vector<int> &ids, int x, int y, WorldLayer layer,
                                    ManualQueueMode mode, GatherCompletion completion) {
    return impl_->queue(CommandType::Gather, ids, x, y, BuildingKind::House, layer, 0, {}, mode,
                        completion);
}
CommandResult ClientSession::build(BuildingKind kind, int x, int y, WorldLayer layer) {
    return impl_->queue(CommandType::Build, {}, x, y, kind, layer);
}
CommandResult ClientSession::cancel_construction(int building_id) {
    if (building_id <= 0)
        return {false, "Invalid construction identity", 0, false, PathFailure::TargetInvalid};
    return impl_->queue(CommandType::CancelConstruction, {}, 0, 0, BuildingKind::House,
                        WorldLayer::Surface, building_id);
}
Resources ClientSession::cost(BuildingKind kind) { return World::cost(kind); }
CommandResult ClientSession::assign_profession(const std::vector<int> &ids,
                                               const ProfessionAssignment &assignment) {
    return impl_->queue(CommandType::AssignProfession, ids, 0, 0, BuildingKind::House,
                        WorldLayer::Surface, 0, assignment);
}
CommandResult ClientSession::stop(const std::vector<int> &ids) {
    return impl_->queue(CommandType::Stop, ids, 0, 0);
}
CommandResult ClientSession::release_control(const std::vector<int> &ids) {
    return impl_->queue(CommandType::ReleaseControl, ids, 0, 0);
}
CommandResult ClientSession::transfer_control(const std::vector<int> &ids, PlayerId target) {
    return impl_->queue(CommandType::TransferControl, ids, 0, 0, BuildingKind::House,
                        WorldLayer::Surface, 0, {}, ManualQueueMode::Replace,
                        GatherCompletion::UntilExhausted, 0, target);
}
CommandResult ClientSession::cancel_manual_order(std::uint64_t order_id) {
    return impl_->queue(CommandType::CancelManualOrder, {}, 0, 0, BuildingKind::House,
                        WorldLayer::Surface, 0, {}, ManualQueueMode::Replace,
                        GatherCompletion::UntilExhausted, order_id);
}
PlayerId ClientSession::player_id() const {
    return impl_->network ? impl_->network->player() : impl_->player;
}
NationId ClientSession::nation_id() const { return impl_->controlled_nation(); }
std::uint64_t ClientSession::control_epoch() const { return impl_->view.control_epoch; }
const std::vector<ManualOrderView> &ClientSession::manual_orders() const {
    return impl_->view.manual_orders;
}
const std::vector<ResidentControlView> &ClientSession::resident_controls() const {
    return impl_->view.resident_controls;
}
const std::vector<ControlPlayerView> &ClientSession::control_players() const {
    return impl_->view.control_players;
}

bool ClientSession::can_build(BuildingKind kind, int x, int y, WorldLayer layer) const {
    const int kind_value = static_cast<int>(kind);
    if (!alpha::buildable(kind) || kind_value < static_cast<int>(BuildingKind::House) ||
        !valid_building_kind(kind) || !inside(x, y) ||
        layer != WorldLayer::Surface)
        return false;
    const Tile &target = tile(x, y, layer);
    if (!target.explored || !building_ground_allowed(kind, target))
        return false;
    if (std::any_of(buildings().begin(), buildings().end(), [=](const Building &building) {
            return building.x == x && building.y == y && building.layer == layer;
        }))
        return false;
    if (std::any_of(construction_sites().begin(), construction_sites().end(),
                    [=](const auto &site) { return site.site == CellCoord{x, y, layer}; }))
        return false;
    if (kind != BuildingKind::Bridge) {
        const auto sample = [&](int sample_x, int sample_y) {
            return tile(sample_x, sample_y, layer);
        };
        const auto support = impl_->view.terrain_generator >= stable_foundation_generator_version
                                 ? surface_foundation_support(x, y, sample)
                                 : surface_buildability(x, y, sample);
        if (support != SurfaceReason::Allowed) return false;
    }
    if (kind == BuildingKind::Bridge && !bridge_connected(x,y,[&](int a,int b){
        const auto &neighbor=tile(a,b,layer);
        if (!neighbor.explored || !inside(a,b)) return false;
        const auto occupant=std::find_if(buildings().begin(),buildings().end(),[&](const auto &v){
            return v.x==a && v.y==b && v.layer==layer;
        });
        return occupant!=buildings().end() ? building_passage(occupant->kind,occupant->progress)
            : neighbor.terrain!=Terrain::Water && neighbor.terrain!=Terrain::MineOre;
    })) return false;
    if (kind == BuildingKind::Fishing && !fishing_shore(x, y, [&](int a, int b) { return tile(a, b, layer); }))
        return false;
    if (kind == BuildingKind::Mine && (portals().size() >= 64 || std::any_of(portals().begin(), portals().end(), [&](const auto &p) {
        return p.surface.x == x && p.surface.y == y;
    }))) return false;
    const Resources price = cost(kind);
    const auto &available = inventory().available;
    return available.wood >= price.wood && available.stone >= price.stone &&
           available.food >= price.food;
}

bool ClientSession::save(const std::filesystem::path &path, std::string &error) const {
    if (impl_->network) {
        error = "Збереження доступне лише господарю локальної гри.";
        return false;
    }
    return impl_->embedded->save(path, error);
}

const SaveLoadInfo& ClientSession::last_load() const { return impl_->load_info; }

bool ClientSession::load(const std::filesystem::path &path, std::string &error) {
    if (impl_->network) {
        error = "Завантаження доступне лише господарю локальної гри.";
        return false;
    }
    auto candidate=std::make_unique<GameServer>(impl_->embedded->config());
    const bool loaded=candidate->load(path,error);
    impl_->load_info=candidate->last_load();
    if(!loaded)return false;
    const JoinResult joined = candidate->join(impl_->player_name,candidate->config().versions,
                                              candidate->local_host_resume_token());
    if (!joined.accepted) {
        error = "Invalid saved online session.";
        return false;
    }
    impl_->embedded=std::move(candidate);
    impl_->player = joined.player;
    impl_->next_request = joined.next_request;
    impl_->notice.reset();
    impl_->command_replies.clear();
    impl_->clear_group();
    impl_->refresh_local();
    return true;
}

std::optional<std::string> ClientSession::take_notice() {
    auto notice = std::move(impl_->notice);
    impl_->notice.reset();
    return notice;
}
std::optional<CommandReply> ClientSession::take_command_reply() {
    if (impl_->command_replies.empty())
        return {};
    auto reply = std::move(impl_->command_replies.front());
    impl_->command_replies.pop_front();
    return reply;
}
const Tile &ClientSession::tile(int x, int y, WorldLayer layer) const {
    return impl_->view.tile(x, y, layer);
}
const std::vector<Chunk> &ClientSession::chunks() const { return impl_->view.chunks; }
const std::vector<Portal> &ClientSession::portals() const { return impl_->view.portals; }
const std::vector<Resident> &ClientSession::residents() const { return impl_->view.residents; }
const std::vector<Building> &ClientSession::buildings() const { return impl_->view.buildings; }
const Resources &ClientSession::resources() const { return impl_->view.resources; }
const InventorySummary &ClientSession::inventory() const { return impl_->view.inventory; }
const std::vector<StorageView> &ClientSession::storages() const { return impl_->view.storages; }
const std::vector<ResidentLogisticsView> &ClientSession::resident_logistics() const {
    return impl_->view.resident_logistics;
}
const std::vector<ResidentWorkView> &ClientSession::resident_work() const {
    return impl_->view.resident_work;
}
const std::vector<ResidentProfessionView> &ClientSession::resident_professions() const {
    return impl_->view.resident_professions;
}
const std::vector<ConstructionSiteView> &ClientSession::construction_sites() const {
    return impl_->view.construction_sites;
}
std::uint64_t ClientSession::ticks() const { return impl_->view.tick; }
int ClientSession::housing() const { return impl_->view.housing; }
const WeatherView &ClientSession::weather() const {
    static const WeatherView unknown;
    return world_ready() && ready() && !reconnecting() && !recovery_incomplete() ? impl_->view.weather : unknown;
}
std::optional<CalendarView> ClientSession::calendar() const {
    const auto &current = weather();
    if (!current.known)
        return std::nullopt;
    return calendar_view(CalendarTick{current.calendar_tick});
}
std::uint32_t ClientSession::day() const { const auto value = calendar(); return value ? value->day : 0u; }
bool ClientSession::winter() const { const auto value = calendar(); return value && value->season == Season::Winter; }
int ClientSession::tutorial_step() const { return impl_->view.tutorial_step; }

} // namespace ribterra

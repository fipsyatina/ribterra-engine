#include "ribterra/game_server.hpp"
#include "ribterra/world_state.hpp"
#include "work_projection_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
int failures = 0;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n";             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace ribterra;

bool install_wood_fixture(World &world, CellCoord target) {
    auto state = world.export_state();
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    if (chunk == state.chunks.end())
        return false;
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    return world.import_state(std::move(state), error);
}

GameCommand build_command(PlayerId player, std::uint64_t request, int x = 26, int y = 24) {
    return {request, player, 0, CommandType::Build, {}, x, y, BuildingKind::House};
}
bool terminal(CommandReplyStatus status) {
    return status == CommandReplyStatus::Applied || status == CommandReplyStatus::Rejected;
}
bool tick_until_terminal(GameServer &server, PlayerId player, std::uint64_t request,
                         int limit = 2000) {
    for (int i = 0; i < limit; ++i) {
        server.tick();
        const auto replies = server.pending_replies(player);
        if (std::any_of(replies.begin(), replies.end(), [&](const auto &reply) {
                return reply.request_id == request && terminal(reply.status);
            }))
            return true;
    }
    return false;
}

void profession_commands_are_partial_authoritative_and_idempotent() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("ribterra-profession-server-" + secure_token() + ".save");
    World source(70421, 3);
    auto state = source.export_state();
    state.residents[1].nation = 2;
    state.residents[0].wood_xp = 123;
    state.residents[0].mining_xp = 456;
    std::string error;
    CHECK(source.import_state(state, error));
    CHECK(source.save(path, error));
    GameServer server;
    CHECK(server.load(path, error));
    const auto first = server.join("First", server.config().versions);
    const auto second = server.join("Second", server.config().versions);
    CHECK(first.accepted && second.accepted);
    GameCommand assign{1, first.player, 0, CommandType::AssignProfession, {1, 2, 999}};
    assign.assignment.profession = Profession::Miner;
    assign.assignment.priorities = {0, 5, 0, 0, 1};
    CHECK(server.enqueue(first.player, assign).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(first.player, assign).status == CommandReplyStatus::Queued);
    auto conflict = assign;
    conflict.assignment.profession = Profession::Woodcutter;
    CHECK(server.enqueue(first.player, conflict).status == CommandReplyStatus::Invalid);
    server.tick();
    const auto replies = server.take_replies(first.player);
    CHECK(replies.size() == 2);
    if (replies.size() != 2)
        return;
    const auto &reply = replies.back();
    CHECK(reply.status == CommandReplyStatus::Applied && reply.failure == PathFailure::None);
    CHECK(reply.residents ==
          std::vector<ResidentCommandOutcome>(
              {{1, ResidentCommandStatus::Applied, PathFailure::None},
               {2, ResidentCommandStatus::Rejected, PathFailure::ResidentUnavailable},
               {999, ResidentCommandStatus::Rejected, PathFailure::ResidentUnavailable}}));
    const auto checksum = server.world().checksum();
    CHECK(server.enqueue(first.player, assign) == reply);
    CHECK(server.world().checksum() == checksum);
    auto view = server.snapshot(first.player);
    CHECK(view.resident_professions.size() == 2 && view.residents.size() == 2);
    CHECK(view.resident_professions.front().assignment == assign.assignment);
    CHECK(view.resident_professions.front().skills[0].experience == 123);
    CHECK(view.resident_professions.front().skills[1].experience == 456);
    for (const auto &profile : view.resident_professions) {
        CHECK(profile.resident_id != 2);
        for (std::size_t skill = 0; skill < skill_count; ++skill)
            CHECK(profile.skills[skill].modifier ==
                  server.world()
                      .skill_breakdown(profile.resident_id, static_cast<Skill>(skill))
                      .value());
    }
    for (const auto &resident : view.residents) {
        CHECK(resident.profession == ProfessionState{});
        CHECK((resident.additional_xp == std::array<int, skill_count - 3>{}));
        CHECK((resident.skill_practice == std::array<SkillPractice, skill_count>{}));
        CHECK((resident.abilities_bp == std::array<std::int16_t, skill_count>{}));
    }
    // The first accepted assignment holds control; another cooperative player
    // cannot overwrite that resident's policy without an explicit transfer.
    assign.request_id = 2;
    assign.residents = {1};
    assign.assignment.profession = Profession::Woodcutter;
    GameCommand stop{1, second.player, 0, CommandType::Stop, {1, 2, 999}};
    CHECK(server.enqueue(first.player, assign).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(second.player, stop).status == CommandReplyStatus::Queued);
    server.tick();
    CHECK(server.world().residents().front().profession.assignment.automation_enabled);
    const auto stopped = server.take_replies(second.player);
    CHECK(stopped.size() == 2 && stopped.back().status == CommandReplyStatus::Rejected);
    CHECK(stopped.size() == 2 &&
          stopped.back().control_rejections ==
              std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));
    CHECK(stopped.size() == 2 && stopped.back().residents.size() == 3);
    assign.request_id = 3;
    assign.assignment.worksite = CellCoord{100, 100, WorldLayer::Mine};
    CHECK(!server.world().tile(100, 100, WorldLayer::Mine).explored);
    CHECK(server.enqueue(first.player, assign).status == CommandReplyStatus::Queued);
    server.tick();
    const auto hidden = server.enqueue(first.player, assign);
    CHECK(hidden.status == CommandReplyStatus::Rejected);
    CHECK(hidden.residents ==
          std::vector<ResidentCommandOutcome>(
              {{1, ResidentCommandStatus::Rejected, PathFailure::TargetInvalid}}));
    assign.request_id = 4;
    assign.assignment.priorities[0] = 6;
    CHECK(server.enqueue(first.player, assign).status == CommandReplyStatus::Rejected);
    auto invalid_stop = stop;
    invalid_stop.request_id = 2;
    invalid_stop.assignment.profession = Profession::Miner;
    CHECK(server.enqueue(second.player, invalid_stop).status == CommandReplyStatus::Rejected);
    const auto observer = server.join("Observer", server.config().versions, {}, true);
    stop.author = observer.player;
    CHECK(server.enqueue(observer.player, stop).status == CommandReplyStatus::Rejected);
    view = server.snapshot(first.player);
    server.disconnect(first.player);
    CHECK(server.snapshot(first.player).resident_professions.empty());
    const auto resumed = server.join("Resumed", server.config().versions, first.resume_token);
    CHECK(resumed.accepted &&
          server.snapshot(resumed.player).resident_professions == view.resident_professions);
    const auto late = server.join("Late", server.config().versions);
    CHECK(late.accepted &&
          server.snapshot(late.player).resident_professions == view.resident_professions);
    std::filesystem::remove(path);
}

void profession_commands_preserve_in_flight_cargo() {
    ServerConfig config;
    config.population = 1;
    const auto fixture_path = std::filesystem::temp_directory_path() /
                              ("ribterra-server-wood-" + secure_token() + ".save");
    World fixture(config.seed, config.population);
    CHECK(install_wood_fixture(fixture, {30, 22}));
    std::string fixture_error;
    CHECK(fixture.save(fixture_path, fixture_error));
    GameServer server(config);
    CHECK(server.load(fixture_path, fixture_error));
    const auto owner = server.join("Carrier", server.config().versions);
    GameCommand gather{1, owner.player, 0, CommandType::Gather, {1}, 30, 22};
    CHECK(server.enqueue(owner.player, gather).status == CommandReplyStatus::Queued);
    for (int tick = 0; tick < 500 && server.world().inventory().carried.wood == 0; ++tick)
        server.tick();
    CHECK(server.world().inventory().carried.wood > 0);
    if (!server.world().inventory().carried.wood) {
        std::filesystem::remove(fixture_path);
        return;
    }
    server.take_replies(owner.player);
    const auto before = server.world().inventory().total;
    GameCommand assign{2, owner.player, server.world().ticks(), CommandType::AssignProfession, {1}};
    assign.assignment.profession = Profession::Miner;
    GameCommand stop{3, owner.player, server.world().ticks(), CommandType::Stop, {1}};
    CHECK(server.enqueue(owner.player, assign).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(owner.player, stop).status == CommandReplyStatus::Queued);
    server.tick();
    auto replies = server.take_replies(owner.player);
    int terminals = 0;
    for (const auto &reply : replies)
        if (terminal(reply.status)) {
            ++terminals;
            CHECK(reply.status == CommandReplyStatus::Applied);
            CHECK(reply.residents ==
                  std::vector<ResidentCommandOutcome>(
                      {{1, ResidentCommandStatus::Deferred, PathFailure::None}}));
        }
    CHECK(terminals == 2 && server.world().inventory().total == before);
    CHECK(server.snapshot(owner.player).resident_professions.front().deferred);
    for (int tick = 0; tick < 500 && server.world().inventory().carried.wood != 0; ++tick)
        server.tick();
    CHECK(server.world().inventory().carried.wood == 0);
    CHECK(server.world().inventory().total == before);
    const auto &resident = server.world().residents().front();
    CHECK(!resident.profession.assignment.automation_enabled);
    CHECK(resident.profession.assignment.profession == Profession::Miner);
    CHECK(!resident.profession.deferred);
    CHECK(server.take_replies(owner.player).empty());
    std::filesystem::remove(fixture_path);
}

void join_rejects_invalid_clients() {
    GameServer server;
    Versions mismatch = server.config().versions;
    ++mismatch.protocol;
    CHECK(!server.join("protocol", mismatch).accepted);
    mismatch = server.config().versions;
    ++mismatch.rules;
    CHECK(!server.join("rules", mismatch).accepted);
    mismatch = server.config().versions;
    mismatch.content[0] = mismatch.content[0] == '0' ? '1' : '0';
    CHECK(!server.join("content", mismatch).accepted);

    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted);
    auto command = build_command(joined.player, 1);
    CHECK(server.enqueue(99999, command).status == CommandReplyStatus::Invalid);
    command.author = joined.player + 1;
    CHECK(server.enqueue(joined.player, command).status == CommandReplyStatus::Invalid);
    command.author = joined.player;
    command.request_id = 0;
    CHECK(server.enqueue(joined.player, command).status == CommandReplyStatus::Invalid);
    auto duplicated =
        GameCommand{1,  joined.player,      0, CommandType::Move, std::vector<int>(1500, 1), 26,
                    25, BuildingKind::House};
    CHECK(server.enqueue(joined.player, duplicated).reason == "Invalid or duplicate resident ID");
    CHECK(server.pending_replies(joined.player).size() == 1);
    CHECK(server.pending_replies(joined.player).front().status == CommandReplyStatus::Rejected);
    server.disconnect(joined.player);
    command.request_id = 2;
    CHECK(server.enqueue(joined.player, command).status == CommandReplyStatus::Invalid);

    const auto observer = server.join("observer", server.config().versions, {}, true);
    CHECK(observer.accepted);
    command = build_command(observer.player, 1);
    CHECK(server.enqueue(observer.player, command).status == CommandReplyStatus::Rejected);
}

void idempotency_and_resource_arbitration() {
    GameServer server;
    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted);
    const auto before = server.world().resources();
    const auto command = build_command(joined.player, 1);
    CHECK(server.enqueue(joined.player, command).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(joined.player, command).status == CommandReplyStatus::Queued);
    auto conflict = command;
    conflict.x = 27;
    CHECK(server.enqueue(joined.player, conflict).status == CommandReplyStatus::Invalid);
    CHECK(tick_until_terminal(server, joined.player, command.request_id));
    const auto replies = server.take_replies(joined.player);
    CHECK(replies.size() == 2);
    CHECK(replies.size() == 2 && replies[0].status == CommandReplyStatus::Queued &&
          replies[1].status == CommandReplyStatus::Applied);
    const auto cost = World::cost(BuildingKind::House);
    CHECK(server.world().inventory().available.wood == before.wood - cost.wood);
    CHECK(server.world().inventory().available.stone == before.stone - cost.stone);
    CHECK(server.world().resources().wood == before.wood);
    CHECK(server.world().resources().stone == before.stone);
    CHECK(server.world().buildings().size() == 2);
    const auto cached = server.enqueue(joined.player, command);
    CHECK(cached.status == CommandReplyStatus::Applied && cached.request_id == command.request_id);
    CHECK(server.world().buildings().size() == 2);

    auto second = build_command(joined.player, 2);
    auto third = build_command(joined.player, 3);
    CHECK(server.enqueue(joined.player, second).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(joined.player, third).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, joined.player, second.request_id));
    CHECK(tick_until_terminal(server, joined.player, third.request_id));
    const auto same_site = server.take_replies(joined.player);
    CHECK(same_site.size() == 4);
    CHECK(std::count_if(same_site.begin(), same_site.end(), [](const auto &reply) {
              return reply.status == CommandReplyStatus::Queued;
          }) == 2);
    CHECK(std::count_if(same_site.begin(), same_site.end(), [](const auto &reply) {
              return reply.status == CommandReplyStatus::Rejected;
          }) == 2);
    CHECK(server.world().inventory().available.wood == before.wood - cost.wood);
    CHECK(server.world().buildings().size() == 2);
}

void phased_ack_reconnect_and_load_replacement() {
    GameServer server;
    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted);
    GameCommand move{1,  joined.player,      server.world().ticks(), CommandType::Move, {1}, 23,
                     23, BuildingKind::House};
    CHECK(server.enqueue(joined.player, move).status == CommandReplyStatus::Queued);
    server.acknowledge_reply(joined.player, {move.request_id, CommandReplyStatus::Queued});
    CHECK(server.pending_replies(joined.player).empty());
    server.disconnect(joined.player);
    for (int i = 0; i < 1000; ++i)
        server.tick();
    const auto resumed = server.join("resume", server.config().versions, joined.resume_token);
    CHECK(resumed.accepted && resumed.player == joined.player);
    const auto terminal_replies = server.pending_replies(joined.player);
    CHECK(terminal_replies.size() == 1);
    CHECK(terminal_replies.size() == 1 && terminal(terminal_replies.front().status));
    if (!terminal_replies.empty())
        server.acknowledge_reply(joined.player, {move.request_id, terminal_replies.front().status});
    CHECK(server.pending_replies(joined.player).empty());

    GameCommand empty_move{
        2,  joined.player,      server.world().ticks(), CommandType::Move, {}, 23,
        23, BuildingKind::House};
    CHECK(server.enqueue(joined.player, empty_move).status == CommandReplyStatus::Queued);
    server.tick();
    const auto cumulative = server.pending_replies(joined.player);
    CHECK(cumulative.size() == 2);
    auto cumulative_terminal =
        std::find_if(cumulative.begin(), cumulative.end(),
                     [](const auto &r) { return r.status == CommandReplyStatus::Rejected; });
    CHECK(cumulative_terminal != cumulative.end());
    if (cumulative_terminal != cumulative.end())
        server.acknowledge_reply(joined.player,
                                 {empty_move.request_id, cumulative_terminal->status});
    CHECK(server.pending_replies(joined.player).empty());

    const auto save = std::filesystem::temp_directory_path() /
                      ("ribterra-server-load-" + secure_token() + ".save");
    std::string error;
    CHECK(server.save(save, error));
    GameCommand replacement{
        3,  joined.player,      server.world().ticks(), CommandType::Move, {1}, 24,
        23, BuildingKind::House};
    CHECK(server.enqueue(joined.player, replacement).status == CommandReplyStatus::Queued);
    CHECK(server.load(save, error));
    CHECK(!server.connected(joined.player));
    CHECK(!server.join("old epoch", server.config().versions, joined.resume_token).accepted);
    const auto fresh = server.join("fresh", server.config().versions);
    CHECK(fresh.accepted);
    CHECK(server.pending_replies(fresh.player).empty());
    std::filesystem::remove(save);
    auto backup = save;
    backup += ".bak";
    std::filesystem::remove(backup);
}

void snapshots_require_authentication() {
    GameServer server;
    const auto hidden = server.snapshot(12345);
    CHECK(hidden.tick == 0 && hidden.revision == 0);
    CHECK(hidden.resources.wood == 0 && hidden.resources.stone == 0 && hidden.resources.food == 0);
    CHECK(hidden.inventory == InventorySummary{});
    CHECK(hidden.resident_logistics.empty() && hidden.construction_sites.empty() && hidden.storages.empty() &&
          hidden.resident_work.empty());
    CHECK(hidden.residents.empty() && hidden.buildings.empty());
    CHECK(hidden.chunks.empty());
    CHECK(hidden.portals.empty());

    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted);
    const auto visible = server.snapshot(joined.player);
    CHECK(!visible.residents.empty() && !visible.buildings.empty());
    CHECK(visible.resources.wood > 0 || visible.resources.stone > 0 || visible.resources.food > 0);
    CHECK(visible.inventory == server.world().inventory(1));
    CHECK(visible.storages.size() == 1 && visible.storages.front().capacity == 10000);
    if (!visible.storages.empty()) {
        std::uint32_t expected_used{};
        for (const auto &stack : server.world().item_stacks())
            if (stack.owner_nation == 1 && stack.location.kind == ItemLocationKind::Storage &&
                stack.location.entity_id == visible.storages.front().building_id)
                expected_used += stack.quantity;
        CHECK(visible.storages.front().used == expected_used);
    }
    std::size_t known_mine_cells = 0;
    for (const auto &chunk : visible.chunks) {
        // Projected chunks carry a content-derived revision so clients can
        // refresh visible terrain incrementally without exposing the
        // authoritative revision of hidden tiles.
        CHECK(chunk.revision != 0);
        for (const auto &tile : chunk.tiles) {
            if (!tile.explored)
                CHECK(tile.terrain == Terrain::Grass && tile.amount == 0);
            if (chunk.key.layer == WorldLayer::Mine && tile.explored)
                ++known_mine_cells;
        }
    }
    CHECK(known_mine_cells == 0); // Construct a mine before any underground discovery.
    CHECK(visible.portals.empty());
    CHECK(!visible.tile(28, 24, WorldLayer::Mine).explored);
    CHECK(!visible.tile(29, 24, WorldLayer::Mine).explored);
    server.disconnect(joined.player);
    const auto after_disconnect = server.snapshot(joined.player);
    CHECK(after_disconnect.residents.empty() && after_disconnect.buildings.empty());
    CHECK(after_disconnect.resources.wood == 0 && after_disconnect.resources.stone == 0 &&
          after_disconnect.resources.food == 0);
    CHECK(after_disconnect.inventory == InventorySummary{});
    CHECK(after_disconnect.resident_logistics.empty() && after_disconnect.storages.empty() &&
          after_disconnect.construction_sites.empty() && after_disconnect.resident_work.empty());
}

void snapshots_exclude_internal_production_storage() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("ribterra-server-internal-storage-" + secure_token() + ".save");
    World source(70421, 1);
    auto state = source.export_state();
    const int sawmill_id = state.next_building_id++;
    state.buildings.push_back(
        {sawmill_id, BuildingKind::Sawmill, 27, 24, 1, WorldLayer::Surface, 1});
    state.storages.push_back({sawmill_id, 100, 7, 0});
    std::string error;
    CHECK(source.import_state(std::move(state), error));
    CHECK(source.save(path, error));

    GameServer server;
    CHECK(server.load(path, error));
    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted);
    const auto visible = server.snapshot(joined.player);
    CHECK(std::none_of(visible.storages.begin(), visible.storages.end(), [&](const auto &storage) {
        return storage.building_id == sawmill_id;
    }));
    CHECK(std::any_of(visible.storages.begin(), visible.storages.end(), [&](const auto &storage) {
        const auto building = std::find_if(visible.buildings.begin(), visible.buildings.end(),
                                           [&](const auto &candidate) {
                                               return candidate.id == storage.building_id;
                                           });
        return building != visible.buildings.end() && building->kind == BuildingKind::Camp;
    }));
    std::filesystem::remove(path);
    auto backup = path;
    backup += ".bak";
    std::filesystem::remove(backup);
}

void physical_inventory_arbitration_cancel_and_reconnect() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("ribterra-logistics-server-" + secure_token() + ".save");
    // One carrier makes cancellation and the subsequent successful return use
    // the same resident, exposing stale failure selection in the public view.
    World limited(70421, 1);
    auto state = limited.export_state();
    const auto price = World::cost(BuildingKind::House);
    for (auto &stack : state.item_stacks)
        stack.quantity = stack.item == ItemKind::Wood    ? price.wood
                         : stack.item == ItemKind::Stone ? price.stone
                                                         : 0;
    std::erase_if(state.item_stacks, [](const auto &stack) { return stack.quantity == 0; });
    state.resources = price;
    std::string error;
    CHECK(limited.import_state(std::move(state), error));
    CHECK(limited.save(path, error));
    GameServer server;
    CHECK(server.load(path, error));
    const auto host = server.join("builder", server.config().versions);
    const auto peer = server.join("competing builder", server.config().versions);
    CHECK(host.accepted && peer.accepted);
    const auto first = build_command(host.player, 1, 26, 24);
    const auto second = build_command(peer.player, 1, 26, 25);
    CHECK(server.world().can_build(BuildingKind::House, first.x, first.y));
    CHECK(server.world().can_build(BuildingKind::House, second.x, second.y));
    CHECK(server.enqueue(host.player, first).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(host.player, first).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(peer.player, second).status == CommandReplyStatus::Queued);
    server.tick();
    const auto competing = server.pending_replies(peer.player);
    CHECK(std::count_if(competing.begin(), competing.end(), [](const auto &reply) {
              return reply.status == CommandReplyStatus::Rejected;
          }) == 1);
    CHECK(server.world().inventory().total == price);
    CHECK(server.world().inventory().available == Resources(0, 0, 0));
    CHECK(server.world().inventory().reserved == price);
    CHECK(server.world().construction_orders().size() == 1);
    const int site_id = server.world().construction_orders().front().building_id;
    CHECK(server.snapshot(host.player).construction_sites.size() == 1);
    const auto token = host.resume_token;
    server.disconnect(host.player);
    bool carried = false;
    for (int i = 0; i < 1000 && !carried; ++i) {
        server.tick();
        const auto inventory = server.world().inventory();
        carried = inventory.carried.wood != 0 || inventory.carried.stone != 0;
    }
    CHECK(carried);
    const auto late = server.join("late builder", server.config().versions);
    const auto resumed = server.join("resume", server.config().versions, token);
    CHECK(late.accepted && resumed.accepted && resumed.player == host.player);
    const auto late_view = server.snapshot(late.player);
    const auto resumed_view = server.snapshot(resumed.player);
    CHECK(late_view.inventory == resumed_view.inventory);
    CHECK(late_view.inventory == server.world().inventory(1));
    CHECK(late_view.resident_logistics == resumed_view.resident_logistics);
    CHECK(late_view.construction_sites == resumed_view.construction_sites);
    CHECK(!late_view.resident_logistics.empty());
    CHECK(late_view.construction_sites.size() == 1);
    GameCommand cancel{2,
                       host.player,
                       server.world().ticks(),
                       CommandType::CancelConstruction,
                       {},
                       0,
                       0,
                       BuildingKind::House,
                       WorldLayer::Surface,
                       site_id};
    CHECK(server.enqueue(host.player, cancel).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, host.player, cancel.request_id));
    CHECK(server.enqueue(host.player, cancel).status == CommandReplyStatus::Applied);
    CHECK(server.world().inventory().total == price);
    CHECK(server.snapshot(host.player).construction_sites.empty());
    // Cancellation releases construction claims; dropped cargo is immediately
    // re-reserved by a new physical return-to-storage task in the same tick.
    for (const auto &reservation : server.world().item_reservations()) {
        const auto &tasks = server.world().logistics_tasks();
        const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto &item) {
            return item.task_id == reservation.task_id;
        });
        CHECK(task != tasks.end() && task->kind == LogisticsTaskKind::HaulToStorage);
    }
    bool returned = false;
    for (int tick = 0; tick < 1500 && !returned; ++tick) {
        server.tick();
        const auto inventory = server.world().inventory();
        returned = inventory.stored == price && inventory.carried == Resources(0, 0, 0) &&
                   inventory.reserved == Resources(0, 0, 0);
    }
    CHECK(returned);
    CHECK(server.world().inventory().total == price);
    CHECK(server.world().item_reservations().empty());
    const auto &tasks = server.world().logistics_tasks();
    const auto failed_delivery = std::find_if(tasks.begin(), tasks.end(), [](const auto &task) {
        return task.resident_id == 1 && task.kind == LogisticsTaskKind::DeliverConstruction &&
               task.phase == LogisticsPhase::Failed && task.failure == LogisticsFailure::Cancelled;
    });
    CHECK(failed_delivery != tasks.end());
    CHECK(std::any_of(tasks.begin(), tasks.end(), [&](const auto &task) {
        return failed_delivery != tasks.end() && task.task_id > failed_delivery->task_id &&
               task.resident_id == 1 && task.kind == LogisticsTaskKind::HaulToStorage &&
               task.phase == LogisticsPhase::Complete;
    }));
    const auto recovered = server.snapshot(host.player);
    CHECK(std::none_of(recovered.resident_logistics.begin(), recovered.resident_logistics.end(),
                       [](const auto &resident) { return resident.resident_id == 1; }));
    std::filesystem::remove(path);
}

void cancelling_planned_build_completes_both_requests() {
    GameServer server;
    const auto host = server.join("host", server.config().versions);
    const auto inventory = server.world().inventory();
    const auto build = build_command(host.player, 1);
    GameCommand cancel{2,
                       host.player,
                       0,
                       CommandType::CancelConstruction,
                       {},
                       0,
                       0,
                       BuildingKind::House,
                       WorldLayer::Surface,
                       2};
    CHECK(server.enqueue(host.player, build).status == CommandReplyStatus::Queued);
    CHECK(server.enqueue(host.player, cancel).status == CommandReplyStatus::Queued);
    server.tick();
    const auto replies = server.pending_replies(host.player);
    CHECK(std::count_if(replies.begin(), replies.end(), [&](const auto &reply) {
              return reply.request_id == build.request_id &&
                     reply.status == CommandReplyStatus::Rejected;
          }) == 1);
    CHECK(std::count_if(replies.begin(), replies.end(), [&](const auto &reply) {
              return reply.request_id == cancel.request_id &&
                     reply.status == CommandReplyStatus::Applied;
          }) == 1);
    CHECK(server.world().inventory() == inventory);
    CHECK(server.snapshot(host.player).construction_sites.empty());
    CHECK(server.world().item_reservations().empty());
    const auto checksum = server.world().checksum();
    CHECK(server.enqueue(host.player, cancel).status == CommandReplyStatus::Applied);
    CHECK(server.enqueue(host.player, build).status == CommandReplyStatus::Rejected);
    CHECK(server.world().checksum() == checksum);
    for (const auto &reply : replies)
        if (terminal(reply.status))
            server.acknowledge_reply(host.player, {reply.request_id, reply.status});
    CHECK(server.pending_replies(host.player).empty());
}

void work_projection_retries_and_retires_terminal_history() {
    test::WorkProjectionSave saved;
    World fixture(70421, 4);
    std::string error;
    const bool imported = fixture.import_state(test::work_projection_state(), error);
    CHECK(imported);
    if (!imported) {
        std::cerr << error << '\n';
        return;
    }
    CHECK(fixture.save(saved.path, error));
    GameServer server;
    CHECK(server.load(saved.path, error));
    const auto owner = server.join("Work owner", server.config().versions);
    const auto first = server.snapshot(owner.player);
    CHECK(first.resident_work.size() == 3);
    if (first.resident_work.size() != 3)
        return;
    CHECK(first.resident_work[0].resident_id == 1 &&
          first.resident_work[0].phase == WorkIntentPhase::Waiting &&
          first.resident_work[0].failure == PathFailure::None &&
          first.resident_work[0].attempts == 1);
    CHECK(first.resident_work[1].resident_id == 2 &&
          first.resident_work[1].phase == WorkIntentPhase::Complete &&
          first.resident_work[1].completion == WorkCompletionReason::Exhausted &&
          first.resident_work[1].failure == PathFailure::None);
    CHECK(first.resident_work[2].resident_id == 3 &&
          first.resident_work[2].phase == WorkIntentPhase::Failed &&
          first.resident_work[2].failure == PathFailure::Unreachable);
    server.disconnect(owner.player);
    CHECK(server.snapshot(owner.player).resident_work.empty());
    const auto resumed = server.join("Work owner", server.config().versions, owner.resume_token);
    CHECK(resumed.accepted && resumed.player == owner.player);
    CHECK(server.snapshot(owner.player).resident_work == first.resident_work);
    const auto late = server.join("Late owner", server.config().versions);
    CHECK(server.snapshot(late.player).resident_work == first.resident_work);

    bool path_pending = false, started = false;
    for (int tick = 0; tick < 1200 && !started; ++tick) {
        server.tick();
        for (const auto &work : server.snapshot(owner.player).resident_work)
            if (work.resident_id == 1)
                path_pending |= work.phase == WorkIntentPhase::PathPending;
        for (const auto &intent : server.world().work_intents())
            if (intent.intent_id == 1)
                started = intent.phase == WorkIntentPhase::Complete &&
                          intent.completion == WorkCompletionReason::Started;
        CHECK(server.pending_replies(owner.player).empty());
        CHECK(server.pending_replies(late.player).empty());
    }
    CHECK(started && path_pending);
    const auto started_view = server.snapshot(owner.player);
    CHECK(std::none_of(started_view.resident_work.begin(), started_view.resident_work.end(),
                       [](const auto &work) { return work.resident_id == 1; }));

    // A rejected command preserves the normal exhausted state. An accepted
    // command retires it permanently, including after the manual movement ends.
    GameCommand move{1, owner.player, 0, CommandType::Gather, {2}, 20, 24};
    CHECK(server.enqueue(owner.player, move).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, owner.player, 1));
    const auto rejected = server.take_replies(owner.player);
    CHECK(rejected.size() == 2 && rejected.back().status == CommandReplyStatus::Rejected);
    auto view = server.snapshot(owner.player);
    CHECK(std::any_of(view.resident_work.begin(), view.resident_work.end(), [](const auto &work) {
        return work.resident_id == 2 && work.completion == WorkCompletionReason::Exhausted;
    }));
    move.request_id = 2;
    move.type = CommandType::Move;
    move.x = 22;
    move.y = 25;
    CHECK(server.enqueue(owner.player, move).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, owner.player, 2));
    auto replies = server.take_replies(owner.player);
    CHECK(replies.size() == 2 && replies.back().status == CommandReplyStatus::Applied);
    for (int tick = 0; tick < 100 && server.world().residents()[1].job != Job::Idle; ++tick)
        server.tick();
    CHECK(server.world().residents()[1].job == Job::Idle);
    view = server.snapshot(owner.player);
    CHECK(std::none_of(view.resident_work.begin(), view.resident_work.end(),
                       [](const auto &work) { return work.resident_id == 2; }));
    GameCommand gather{3, owner.player, 0, CommandType::Gather, {3}, 21, 24};
    CHECK(server.enqueue(owner.player, gather).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, owner.player, 3));
    replies = server.take_replies(owner.player);
    CHECK(replies.size() == 2 && replies.back().status == CommandReplyStatus::Applied);
    view = server.snapshot(owner.player);
    CHECK(std::none_of(view.resident_work.begin(), view.resident_work.end(), [](const auto &work) {
        return work.resident_id == 3 && work.failure == PathFailure::Unreachable;
    }));
    CHECK(server.pending_replies(late.player).empty());
}

void automatic_cargo_retires_exhausted_history() {
    test::WorkProjectionSave saved;
    auto state = test::work_projection_state();
    state.item_stacks.push_back({state.next_item_stack_id++,
                                 ItemKind::Wood,
                                 1,
                                 1,
                                 {ItemLocationKind::Ground, {22, 24, WorldLayer::Surface}, 0}});
    ++state.resources.wood;
    World fixture(70421, 4);
    std::string error;
    const bool imported = fixture.import_state(std::move(state), error);
    CHECK(imported);
    if (!imported) {
        std::cerr << error << '\n';
        return;
    }
    CHECK(fixture.save(saved.path, error));
    GameServer server;
    CHECK(server.load(saved.path, error));
    const auto owner = server.join("Cargo owner", server.config().versions);
    bool carried = false, completed = false;
    for (int tick = 0; tick < 500 && !completed; ++tick) {
        server.tick();
        for (const auto &task : server.world().logistics_tasks())
            if (task.resident_id == 2 && task.kind == LogisticsTaskKind::HaulToStorage) {
                carried |= task.phase == LogisticsPhase::ToDropoff;
                completed |= task.phase == LogisticsPhase::Complete;
            }
        CHECK(server.pending_replies(owner.player).empty());
    }
    CHECK(carried && completed);
    const auto view = server.snapshot(owner.player);
    CHECK(std::none_of(view.resident_work.begin(), view.resident_work.end(),
                       [](const auto &work) { return work.resident_id == 2; }));
    CHECK(std::any_of(server.world().work_intents().begin(), server.world().work_intents().end(),
                      [](const auto &intent) {
                          return intent.intent_id == 2 &&
                                 intent.completion == WorkCompletionReason::Superseded;
                      }));
}

void construction_workforce_uses_routes_and_replaces_workers() {
    GameServer server;
    const auto owner = server.join("Worker owner", server.config().versions);
    const auto build = build_command(owner.player, 1);
    CHECK(server.enqueue(owner.player, build).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, owner.player, 1));
    bool working = false;
    for (int tick = 0; tick < 1500 && !working; ++tick) {
        server.tick();
        for (const auto &site : server.snapshot(owner.player).construction_sites)
            working |= site.worker_phase == ConstructionWorkerPhase::Working;
    }
    CHECK(working);
    auto view = server.snapshot(owner.player);
    if (view.construction_sites.empty())
        return;
    const int site_id = view.construction_sites.front().building_id;
    const auto before = server.world().inventory().total;
    std::vector<int> old_workers;
    for (const auto &resident : server.world().residents())
        if (resident.job == Job::Build)
            old_workers.push_back(resident.id);
    CHECK(!old_workers.empty());
    server.take_replies(owner.player);
    GameCommand move{2, owner.player, 0, CommandType::Move, old_workers, 20, 24};
    CHECK(server.enqueue(owner.player, move).status == CommandReplyStatus::Queued);
    CHECK(tick_until_terminal(server, owner.player, 2));
    const auto replies = server.take_replies(owner.player);
    CHECK(replies.size() == 2 && replies.back().status == CommandReplyStatus::Applied);
    bool replacement = false, finished = false;
    for (int tick = 0; tick < 1500 && !finished; ++tick) {
        server.tick();
        for (const auto &intent : server.world().work_intents())
            replacement |= intent.kind == WorkIntentKind::BuildAssignment &&
                           intent.building_id == site_id &&
                           std::find(old_workers.begin(), old_workers.end(), intent.resident_id) ==
                               old_workers.end();
        view = server.snapshot(owner.player);
        for (const auto &site : view.construction_sites) {
            CHECK(site.worker_phase != ConstructionWorkerPhase::None);
            CHECK((site.assigned_workers == 0) ==
                  (site.worker_phase == ConstructionWorkerPhase::Waiting));
        }
        finished = std::any_of(view.buildings.begin(), view.buildings.end(),
                               [&](const auto &b) { return b.id == site_id && b.progress == 1.F; });
        CHECK(server.pending_replies(owner.player).empty());
    }
    CHECK(replacement && finished);
    CHECK(server.world().inventory().total.wood == before.wood &&
          server.world().inventory().total.stone == before.stone);
    CHECK(view.construction_sites.empty());
}

void reconnect_preserves_identity_and_world() {
    GameServer server;
    const auto joined = server.join("host", server.config().versions);
    CHECK(joined.accepted && joined.resume_token.size() == 64);
    const auto people = server.world().residents().size();
    const auto checksum = server.world().checksum();
    server.disconnect(joined.player);
    const auto resumed =
        server.join("ignored-on-resume", server.config().versions, joined.resume_token);
    CHECK(resumed.accepted && resumed.player == joined.player &&
          resumed.resume_token == joined.resume_token);
    CHECK(server.world().residents().size() == people);
    CHECK(server.world().checksum() == checksum);
    CHECK(!server.join("duplicate", server.config().versions, joined.resume_token).accepted);
}

void chat_enforces_unicode_limits_history_and_mutes() {
    using Clock = std::chrono::steady_clock;
    auto now = Clock::time_point{};
    ServerConfig config;
    config.clock = [&] { return now; };
    GameServer server(config);
    const auto host = server.join("Гравець", config.versions);
    const auto peer = server.join("Peer", config.versions);
    CHECK(host.accepted && peer.accepted);
    std::string error;

    const std::string emoji = "\xF0\x9F\x90\x9F";
    const std::string exactly_256 = std::string{} + [&] {
        std::string s;
        for (int i = 0; i < 256; ++i)
            s += emoji;
        return s;
    }();
    CHECK(exactly_256.size() == 1024);
    CHECK(server.chat(host.player, exactly_256, error).has_value());
    CHECK(!server.chat(host.player, exactly_256 + "a", error).has_value());
    std::string too_many_codepoints;
    for (int i = 0; i < 257; ++i)
        too_many_codepoints += "і";
    CHECK(too_many_codepoints.size() < 1024);
    CHECK(!server.chat(host.player, too_many_codepoints, error).has_value());
    CHECK(!server.chat(host.player, "/kick Peer", error).has_value());

    now += std::chrono::seconds(10);
    for (int i = 0; i < 5; ++i)
        CHECK(server.chat(host.player, "message " + std::to_string(i), error).has_value());
    CHECK(!server.chat(host.player, "rate limited", error).has_value());
    now += std::chrono::seconds(10);
    CHECK(server.chat(host.player, "after window", error).has_value());

    CHECK(server.mute(peer.player, host.player, true));
    CHECK(server.chat_since(peer.player, 0).empty());
    CHECK(server.mute(peer.player, host.player, false));
    CHECK(!server.chat_since(peer.player, 0).empty());

    for (int batch = 0; batch < 40; ++batch) {
        now += std::chrono::seconds(10);
        for (int i = 0; i < 5; ++i)
            CHECK(server.chat(host.player, "history", error).has_value());
    }
    const auto history = server.chat_since(peer.player, 0);
    CHECK(history.size() == 200);
    CHECK(history.front().sequence + 199 == history.back().sequence);
}

void visible_chunk_revisions_ignore_hidden_authority() {
    const auto root=std::filesystem::temp_directory_path()/
                    ("ribterra-visible-revision-"+secure_token());
    std::filesystem::create_directories(root);
    World source(70421,1);
    source.create_nations({1},6,1);
    const auto original=source.export_state();
    auto hidden=original, visible=original;
    std::optional<CellCoord> hidden_cell, visible_cell;
    for(const auto&chunk:original.chunks) {
        std::optional<CellCoord> hidden_candidate,visible_candidate;
        for(std::size_t i=0;i<chunk.tiles.size();++i) {
            const CellCoord cell{chunk.key.cx*chunk_size+int(i%chunk_size),
                                 chunk.key.cy*chunk_size+int(i/chunk_size),chunk.key.layer};
            if(!hidden_candidate&&!source.explored(cell,1)) hidden_candidate=cell;
            if(!visible_candidate&&source.explored(cell,1)&&chunk.tiles[i].terrain!=Terrain::Water)
                visible_candidate=cell;
        }
        if(hidden_candidate&&visible_candidate) {
            hidden_cell=hidden_candidate; visible_cell=visible_candidate; break;
        }
    }
    CHECK(hidden_cell.has_value()&&visible_cell.has_value());
    if(!hidden_cell||!visible_cell) return;
    const auto mutate=[](WorldState&state,CellCoord cell) {
        auto chunk=std::find_if(state.chunks.begin(),state.chunks.end(),
                                [&](const auto&value){return value.key==chunk_key(cell);});
        CHECK(chunk!=state.chunks.end());
        if(chunk==state.chunks.end()) return;
        auto&tile=chunk->tiles[local_index(cell.x,cell.y)];
        tile.trail_wear=static_cast<std::uint8_t>(tile.trail_wear==255?254:tile.trail_wear+1);
        ++chunk->revision;
    };
    mutate(hidden,*hidden_cell);
    mutate(visible,*visible_cell);
    const auto save=[&](WorldState state,const char*name) {
        World world(1,0);std::string error;
        CHECK(world.import_state(std::move(state),error));
        const auto path=root/name;CHECK(world.save(path,error));return path;
    };
    const auto base_path=save(original,"base.save");
    const auto hidden_path=save(std::move(hidden),"hidden.save");
    const auto visible_path=save(std::move(visible),"visible.save");
    const auto projected=[&](const std::filesystem::path&path) {
        GameServer server;std::string error;CHECK(server.load(path,error));
        const auto joined=server.join("Revision",server.config().versions);
        CHECK(joined.accepted);return server.snapshot(joined.player).chunks;
    };
    const auto base_chunks=projected(base_path),hidden_chunks=projected(hidden_path),
               visible_chunks=projected(visible_path);
    const auto revision=[&](const auto&chunks,CellCoord cell) {
        const auto found=std::find_if(chunks.begin(),chunks.end(),
                                      [&](const auto&chunk){return chunk.key==chunk_key(cell);});
        CHECK(found!=chunks.end());return found==chunks.end()?std::uint64_t{}:found->revision;
    };
    CHECK(revision(base_chunks,*hidden_cell)==revision(hidden_chunks,*hidden_cell));
    CHECK(revision(base_chunks,*visible_cell)!=revision(visible_chunks,*visible_cell));
    std::error_code error;std::filesystem::remove_all(root,error);
}
#include "server_control_tests.inc"
} // namespace

int main() {
    manual_control_leases_are_ordered_and_expire_while_paused();
    profession_commands_are_partial_authoritative_and_idempotent();
    profession_commands_preserve_in_flight_cargo();
    join_rejects_invalid_clients();
    idempotency_and_resource_arbitration();
    phased_ack_reconnect_and_load_replacement();
    snapshots_require_authentication();
    snapshots_exclude_internal_production_storage();
    physical_inventory_arbitration_cancel_and_reconnect();
    cancelling_planned_build_completes_both_requests();
    work_projection_retries_and_retires_terminal_history();
    automatic_cargo_retires_exhausted_history();
    construction_workforce_uses_routes_and_replaces_workers();
    reconnect_preserves_identity_and_world();
    chat_enforces_unicode_limits_history_and_mutes();
    visible_chunk_revisions_ignore_hidden_authority();
    if (failures) {
        std::cerr << failures << " server test(s) failed.\n";
        return 1;
    }
    std::cout << "Server authentication, authority, idempotency, reconnect, projection and chat "
                 "tests passed.\n";
    return 0;
}

#include "existing_passage_fixture.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <iostream>
#include <string>

namespace {
int failures = 0;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";                                     \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)
using namespace ribterra;
Tile &tile(WorldState &s, CellCoord cell) {
    const auto key = chunk_key(cell);
    auto it = std::find_if(s.chunks.begin(), s.chunks.end(), [&](const auto &c) { return c.key == key; });
    return it->tiles[local_index(cell.x, cell.y)];
}
void check_inventory(const World &w) {
    const auto i = w.inventory();
    CHECK(i.total.wood == i.available.wood + i.reserved.wood);
    CHECK(i.total.stone == i.available.stone + i.reserved.stone);
    CHECK(i.total.food == i.available.food + i.reserved.food);
    for (const auto &s : w.item_stacks()) {
        CHECK(s.quantity > 0);
        if (s.location.kind == ItemLocationKind::Carried)
            CHECK(s.quantity <= resident_carry_capacity);
    }
}
void round_trip(const World &w) {
    World copy(10, 0);
    std::string error;
    const bool imported = copy.import_state(w.export_state(), error);
    if (!imported)
        std::cerr << "round trip: " << error << '\n';
    CHECK(imported);
    if (imported)
        CHECK(copy.checksum() == w.checksum());
}
void build_reserve_deliver_and_resume() {
    World w(70421, 4);
    auto state = w.export_state();
    for (auto &s : state.item_stacks) {
        if (s.item == ItemKind::Wood)
            s.quantity = 30;
        if (s.item == ItemKind::Stone)
            s.quantity = 15;
    }
    state.resources = {30, 15, 100};
    std::string error;
    CHECK(w.import_state(state, error));
    const auto accepted = w.build(BuildingKind::House, 26, 26);
    CHECK(accepted.accepted && accepted.pending);
    CHECK(!w.build(BuildingKind::House, 25, 26).accepted);
    CHECK(w.resources().wood == 30 && w.resources().stone == 15);
    CHECK(w.inventory().available.wood == 0 && w.inventory().reserved.stone == 15);
    CHECK(w.item_reservations().size() == 5);
    round_trip(w);
    bool carrying = false, partial_delivery = false, work = false, complete = false;
    World resumed(1, 0);
    bool resumed_started = false;
    std::uint64_t applied = 0;
    for (int i = 0; i < 2000; ++i) {
        w.tick();
        for (const auto &result : w.take_path_results()) {
            CHECK(result.path_batch_id == accepted.path_batch_id &&
                  result.command == PathCommandKind::Build && result.accepted);
            ++applied;
        }
        if (resumed_started) {
            resumed.tick();
            resumed.take_path_results();
            CHECK(resumed.checksum() == w.checksum());
        }
        check_inventory(w);
        const auto &order = w.construction_orders().front();
        if (!carrying && w.inventory().carried.wood + w.inventory().carried.stone > 0) {
            carrying = true;
            round_trip(w);
            CHECK(resumed.import_state(w.export_state(), error));
            resumed_started = true;
        }
        if (order.phase == ConstructionPhase::Delivery) {
            CHECK(w.buildings().back().progress == 0);
            if (!partial_delivery && order.delivered != Resources{0, 0, 0}) {
                partial_delivery = true;
                round_trip(w);
            }
        }
        if (order.phase == ConstructionPhase::Work && !work) {
            work = true;
            CHECK(order.delivered == order.required);
            round_trip(w);
        }
        if (order.phase == ConstructionPhase::Complete) {
            complete = true;
            break;
        }
    }
    CHECK(carrying && partial_delivery && work && complete);
    CHECK(applied == 1);
    CHECK(w.resources().wood == 0 && w.resources().stone == 0);
    CHECK(w.item_reservations().empty());
}
World mine_fixture() {
    World w(70421, 1);
    existing_passage(w);
    auto state = w.export_state();
    auto &resident = state.residents.front();
    resident.x = 27;
    resident.y = 24;
    resident.target_x = 27;
    resident.target_y = 24;
    tile(state, {29, 24, WorldLayer::Mine}) = {Terrain::MineFloor, 0, true};
    tile(state, {30, 24, WorldLayer::Mine}) = {Terrain::MineOre, 1, true};
    std::string error;
    CHECK(w.import_state(state, error));
    return w;
}
void mine_cargo_crosses_portal() {
    auto w = mine_fixture();
    auto result = w.gather({1}, 30, 24, WorldLayer::Mine);
    CHECK(result.accepted);
    bool cargo_in_mine = false, cargo_on_surface = false, stored = false;
    std::size_t external_results = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        w.tick();
        auto outcomes = w.take_path_results();
        external_results += outcomes.size();
        for (const auto &outcome : outcomes)
            CHECK(outcome.path_batch_id == result.path_batch_id);
        check_inventory(w);
        if (w.inventory().carried.stone == 2) {
            CHECK(w.inventory().stored.stone == 40);
            CHECK(w.resources().stone == 42);
            if (w.residents()[0].layer == WorldLayer::Mine)
                cargo_in_mine = true;
            else
                cargo_on_surface = true;
        }
        if (w.inventory().stored.stone == 42) {
            stored = true;
            break;
        }
    }
    CHECK(cargo_in_mine && cargo_on_surface && stored);
    CHECK(external_results == 1);
    CHECK(w.residents()[0].mining_xp == 1);
    CHECK(w.tile(30, 24, WorldLayer::Mine).terrain == Terrain::MineFloor);
    round_trip(w);
}
void cancellation_death_and_full_storage() {
    World planned(70421, 3);
    auto original = planned.build(BuildingKind::House, 26, 26);
    CHECK(original.accepted);
    const int id = planned.construction_orders().front().building_id;
    CHECK(planned.cancel_construction(id).accepted);
    CHECK(!planned.cancel_construction(id).accepted);
    auto outcomes = planned.take_path_results();
    CHECK(outcomes.size() == 1 && outcomes[0].path_batch_id == original.path_batch_id &&
          !outcomes[0].accepted);
    CHECK(planned.item_reservations().empty());
    CHECK(planned.resources().wood == 80 && planned.resources().stone == 40);
    round_trip(planned);
    for (bool death : {false, true}) {
        World w(70421, 3);
        CHECK(w.build(BuildingKind::House, 26, 27).accepted);
        for (int i = 0; i < 500 && w.inventory().carried.wood == 0; ++i) {
            w.tick();
            w.take_path_results();
        }
        CHECK(w.inventory().carried.wood > 0);
        if (death) {
            auto state = w.export_state();
            const auto cargo =
                std::find_if(state.item_stacks.begin(), state.item_stacks.end(),
                             [](const auto &s) { return s.location.kind == ItemLocationKind::Carried; });
            if (cargo != state.item_stacks.end()) {
                for (auto &r : state.residents)
                    if (r.id == cargo->location.entity_id)
                        r.health = 0;
                std::string error;
                CHECK(w.import_state(state, error));
            }
            w.tick();
        } else
            CHECK(w.cancel_construction(w.construction_orders().front().building_id).accepted);
        CHECK(w.inventory().total.wood == 80 && w.inventory().total.stone == 40);
        CHECK(w.construction_orders().front().phase == ConstructionPhase::Cancelled);
        CHECK(w.item_reservations().empty() ||
              death); // Released material may acquire an independent ground haul on the death tick.
        round_trip(w);
    }
    auto w = mine_fixture();
    CHECK(w.gather({1}, 30, 24, WorldLayer::Mine).accepted);
    for (int i = 0; i < 500 && w.inventory().carried.stone == 0; ++i) {
        w.tick();
        w.take_path_results();
    }
    CHECK(w.inventory().carried.stone == 2);
    auto state = w.export_state();
    std::uint32_t occupied = 0;
    for (const auto &s : state.item_stacks)
        if (s.location.kind == ItemLocationKind::Storage)
            occupied += s.quantity;
    state.storages.front().capacity = occupied;
    std::string error;
    CHECK(w.import_state(state, error));
    bool failed = false;
    for (int i = 0; i < 300 && !failed; ++i) {
        w.tick();
        w.take_path_results();
        failed = std::any_of(w.logistics_tasks().begin(), w.logistics_tasks().end(),
                             [](const auto &t) { return t.failure == LogisticsFailure::StorageFull; });
    }
    CHECK(failed);
    CHECK(w.resources().stone == 42);
    check_inventory(w);
    round_trip(w);
}
void malformed_state_is_transactional() {
    World w(70421, 3);
    CHECK(w.build(BuildingKind::House, 26, 26).accepted);
    const auto baseline = w.export_state();
    const auto checksum = w.checksum();
    auto reject = [&](auto mutation) {
        auto state = baseline;
        mutation(state);
        std::string error;
        CHECK(!w.import_state(std::move(state), error));
        CHECK(!error.empty());
        CHECK(w.checksum() == checksum);
    };
    reject([](auto &s) { s.item_reservations[0].quantity = 1000; });
    reject([](auto &s) { s.item_reservations[0].task_id = 9000; });
    reject([](auto &s) { s.item_stacks[0].quantity = 1; });
    reject([](auto &s) { s.resources.wood += 1; });
    reject([](auto &s) { s.construction_orders[0].delivered.wood = 10; });
    reject([](auto &s) { s.logistics_tasks[0].phase = LogisticsPhase::Complete; });
    reject([](auto &s) { s.storages[0].capacity = 1; });
}
World before_gather_dropoff() {
    auto w = mine_fixture();
    auto state = w.export_state();
    tile(state, {30, 24, WorldLayer::Mine}).amount = 2;
    std::string error;
    CHECK(w.import_state(state, error));
    CHECK(w.gather({1}, 30, 24, WorldLayer::Mine).accepted);
    bool arrived = false;
    for (int i = 0; i < 1000 && !arrived; ++i) {
        w.tick();
        w.take_path_results();
        auto snapshot = w.export_state();
        if (snapshot.logistics_tasks.empty())
            continue;
        const auto &t = snapshot.logistics_tasks.back();
        const auto &r = snapshot.residents.front();
        const auto &route = snapshot.routes.front();
        arrived = t.phase == LogisticsPhase::ToDropoff && !t.path_batch_id &&
                  route.cursor == route.points.size() && r.layer == WorldLayer::Surface &&
                  std::abs(r.x - 24.f) + std::abs(r.y - 24.f) == 1.f;
    }
    CHECK(arrived);
    return w;
}
void gathering_continuation_failures_are_visible() {
    auto w = before_gather_dropoff();
    auto state = w.export_state();
    CHECK(state.path_batches.empty());
    for (std::size_t i = 0; i < max_path_batches; ++i)
        state.path_results.push_back({state.next_path_batch_id++, PathCommandKind::Move, false,
                                      PathFailure::Timeout, "Expired external move."});
    std::string error;
    CHECK(w.import_state(state, error));
    World resumed(1, 0);
    CHECK(resumed.import_state(state, error));
    w.tick();
    resumed.tick();
    CHECK(w.checksum() == resumed.checksum());
    CHECK(w.inventory().stored.stone == 42 && w.inventory().carried.stone == 0);
    CHECK(w.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(w.work_intents().back().phase == WorkIntentPhase::Waiting);
    CHECK(w.take_path_results().size() == max_path_batches);
    round_trip(w);
    CHECK(w.move({1}, 26, 24).accepted);

    auto rejected = before_gather_dropoff();
    rejected.tick();
    rejected.tick();
    CHECK(rejected.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(rejected.work_intents().back().path_batch_id != 0);
    state = rejected.export_state();
    tile(state, {30, 24, WorldLayer::Mine}) = {Terrain::MineFloor, 0, true};
    ++state.topology_revision;
    CHECK(rejected.import_state(state, error));
    for (int i = 0; i < 100 && rejected.work_intents().back().phase != WorkIntentPhase::Complete; ++i)
        rejected.tick();
    CHECK(rejected.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(rejected.work_intents().back().completion == WorkCompletionReason::Exhausted);
    CHECK(rejected.inventory().stored.stone == 42);
    CHECK(rejected.take_path_results().empty());
    round_trip(rejected);
}
void carrier_boundary_and_changed_storage_filter() {
    auto world = before_gather_dropoff();
    const auto before = world.checksum();
    auto invalid = world.export_state();
    invalid.residents.front().x = float(world_max) - .125f;
    invalid.residents.front().target_x = world_max - 1;
    invalid.residents.front().health = 0;
    invalid.routes.front().points.clear();
    invalid.routes.front().cursor = 0;
    invalid.routes.front().work.reset();
    std::string error;
    CHECK(!world.import_state(invalid, error));
    CHECK(world.checksum() == before);
    CHECK(error.find("drop cell") != std::string::npos);
    invalid.residents.front().x = float(world_max - 1);
    CHECK(world.import_state(invalid, error));
    world.tick();
    const auto ground =
        std::find_if(world.item_stacks().begin(), world.item_stacks().end(), [](const auto &s) {
            return s.item == ItemKind::Stone && s.location.kind == ItemLocationKind::Ground;
        });
    CHECK(ground != world.item_stacks().end());
    if (ground != world.item_stacks().end())
        CHECK(ground->location.cell.x == world_max - 1 && ground->quantity == 2);
    round_trip(world);

    auto filtered = before_gather_dropoff();
    auto state = filtered.export_state();
    std::erase_if(state.item_stacks, [](const auto &s) {
        return s.item == ItemKind::Stone && s.location.kind == ItemLocationKind::Storage;
    });
    state.resources.stone = 2;
    state.storages.front().accepted_items = 5; // Wood and Food; the existing inventory satisfies this filter.
    CHECK(filtered.import_state(state, error));
    filtered.tick();
    CHECK(filtered.inventory().total.stone == 2);
    CHECK(filtered.inventory().stored.stone == 0 && filtered.inventory().carried.stone == 0);
    CHECK(filtered.logistics_tasks().back().phase == LogisticsPhase::Failed);
    CHECK(filtered.logistics_tasks().back().failure == LogisticsFailure::TargetInvalid);
    CHECK(std::any_of(filtered.item_stacks().begin(), filtered.item_stacks().end(), [](const auto &s) {
        return s.item == ItemKind::Stone && s.quantity == 2 && s.location.kind == ItemLocationKind::Ground;
    }));
    round_trip(filtered);
}
} // namespace
int main() {
    build_reserve_deliver_and_resume();
    mine_cargo_crosses_portal();
    cancellation_death_and_full_storage();
    malformed_state_is_transactional();
    gathering_continuation_failures_are_visible();
    carrier_boundary_and_changed_storage_filter();
    if (failures)
        std::cerr << failures << " logistics checks failed\n";
    else
        std::cout << "Physical reservations, portal haul, delivery, resume, cancellation and "
                     "recovery passed\n";
    return failures ? 1 : 0;
}

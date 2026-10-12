#include "existing_passage_fixture.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace ribterra;
int failures = 0;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)
Tile &tile(WorldState &s, CellCoord cell) {
    auto it = std::find_if(s.chunks.begin(), s.chunks.end(),
                           [&](const auto &c) { return c.key == chunk_key(cell); });
    return it->tiles[local_index(cell.x, cell.y)];
}
void import(World &w, WorldState s) {
    std::string error;
    const bool accepted = w.import_state(std::move(s), error);
    if (!accepted)
        std::cerr << "import: " << error << '\n';
    CHECK(accepted);
}
void bound(const World &w) {
    CHECK(w.work_diagnostics().visits_last_tick <= work_intent_visits_per_tick);
    CHECK(w.work_diagnostics().admission_attempts_last_tick <= work_admissions_per_tick);
}
World dropoff(int amount = 2, Terrain terrain = Terrain::MineOre) {
    World w(70421, 1);
    existing_passage(w);
    auto s = w.export_state();
    auto &r = s.residents.front();
    r.x = 27;
    r.y = 24;
    r.target_x = 27;
    r.target_y = 24;
    tile(s, {29, 24, WorldLayer::Mine}) = {Terrain::MineFloor, 0, true};
    tile(s, {30, 24, WorldLayer::Mine}) = {terrain, amount, true};
    import(w, std::move(s));
    CHECK(w.gather({1}, 30, 24, WorldLayer::Mine).accepted);
    for (int i = 0; i < 1000; ++i) {
        w.tick();
        w.take_path_results();
        const auto state = w.export_state();
        if (state.logistics_tasks.empty())
            continue;
        const auto &t = state.logistics_tasks.back();
        const auto &actor = state.residents.front();
        const auto &route = state.routes.front();
        if (t.phase == LogisticsPhase::ToDropoff && !t.path_batch_id && route.cursor == route.points.size() &&
            actor.layer == WorldLayer::Surface && std::abs(actor.x - 24.f) + std::abs(actor.y - 24.f) == 1.f)
            return w;
    }
    CHECK(false);
    return w;
}
void fill_queue(World &w, std::size_t count = max_path_batches) {
    auto s = w.export_state();
    for (std::size_t i = 0; i < count; ++i)
        s.path_results.push_back({s.next_path_batch_id++, PathCommandKind::Move, false, PathFailure::Timeout,
                                  "Expired external move."});
    import(w, std::move(s));
}
void pressure_resume_and_manual_priority() {
    auto w = dropoff();
    fill_queue(w);
    w.tick();
    w.tick();
    CHECK(w.inventory().stored.stone == 42 && w.inventory().carried.stone == 0);
    CHECK(w.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(w.work_intents().size() == 1);
    CHECK(w.work_intents().front().phase == WorkIntentPhase::Waiting);
    CHECK(w.work_intents().front().attempts == 1);
    CHECK(w.work_intents().front().next_attempt_tick == w.ticks() + 2);
    World resumed(1, 0);
    import(resumed, w.export_state());
    for (int i = 0; i < 7; ++i) {
        w.tick();
        resumed.tick();
        bound(w);
        CHECK(w.checksum() == resumed.checksum());
    }
    CHECK(w.work_intents().front().attempts == 3);
    CHECK(w.take_path_results() == resumed.take_path_results());
    for (int i = 0; i < 20 && w.work_intents().front().phase == WorkIntentPhase::Waiting; ++i) {
        w.tick();
        resumed.tick();
        CHECK(w.checksum() == resumed.checksum());
    }
    CHECK(w.work_intents().front().phase == WorkIntentPhase::PathPending);
    const auto old_path = w.work_intents().front().path_batch_id;
    fill_queue(w, max_path_batches - 1);
    const auto before = w.checksum();
    CHECK(!w.move({1}, world_min - 1, 24).accepted);
    CHECK(w.checksum() == before);
    const auto command = w.move({1}, 26, 24);
    CHECK(command.accepted && command.pending);
    CHECK(w.work_intents().front().completion == WorkCompletionReason::Superseded);
    CHECK(!w.work_intents().front().path_batch_id);
    auto state = w.export_state();
    CHECK(state.path_batches.size() == 1 &&
          state.path_batches.front().path_batch_id == command.path_batch_id);
    CHECK(command.path_batch_id != old_path);
    CHECK(w.take_path_results().size() == max_path_batches - 1);
    for (int i = 0; i < 100; ++i)
        w.tick();
    const auto outcomes = w.take_path_results();
    CHECK(outcomes.size() == 1 && outcomes.front().path_batch_id == command.path_batch_id &&
          outcomes.front().accepted);
    CHECK(w.residents().front().x == 26 && w.residents().front().y == 24);
    CHECK(w.inventory().stored.stone == 42);
}
void exhausted_before_and_during_search() {
    for (bool pending : {false, true}) {
        auto w = dropoff(pending ? 2 : 1);
        w.tick();
        if (pending) {
            w.tick();
            CHECK(w.work_intents().front().phase == WorkIntentPhase::PathPending);
            auto s = w.export_state();
            tile(s, {30, 24, WorldLayer::Mine}) = {Terrain::MineFloor, 0, true};
            ++s.topology_revision;
            import(w, std::move(s));
        }
        World copy(2, 0);
        import(copy, w.export_state());
        for (int i = 0; i < 100; ++i) {
            w.tick();
            copy.tick();
            CHECK(w.checksum() == copy.checksum());
            if (w.work_intents().front().phase == WorkIntentPhase::Complete)
                break;
        }
        CHECK(w.work_intents().front().completion == WorkCompletionReason::Exhausted);
        CHECK(w.work_intents().front().failure == PathFailure::None);
        CHECK(w.logistics_tasks().back().phase == LogisticsPhase::Complete);
        CHECK(w.inventory().stored.stone == 42);
        CHECK(w.take_path_results().empty());
    }
    auto invalid = dropoff();
    invalid.tick();
    auto state = invalid.export_state();
    tile(state, {30, 24, WorldLayer::Mine}) = {Terrain::Rock, 0, true};
    ++state.topology_revision;
    import(invalid, std::move(state));
    invalid.tick();
    CHECK(invalid.work_intents().front().phase == WorkIntentPhase::Failed);
    CHECK(invalid.work_intents().front().failure == PathFailure::TargetInvalid);
}
World paid_site(bool original_worker) {
    World w(70421, 3);
    auto s = w.export_state();
    for (int y = 20; y <= 28; ++y)
        for (int x = 20; x <= 45; ++x)
            tile(s, {x, y, WorldLayer::Surface}) = {Terrain::Grass, 0, true, Biome::Meadow, 8};
    s.buildings.push_back({2, BuildingKind::House, 42, 24, .2f, WorldLayer::Surface, 1});
    s.next_building_id = 3;
    ConstructionOrderState order;
    order.building_id = 2;
    order.kind = BuildingKind::House;
    order.site = {42, 24, WorldLayer::Surface};
    order.required = order.delivered = {30, 15, 0};
    order.phase = ConstructionPhase::Work;
    s.construction_orders.push_back(order);
    if (original_worker) {
        auto &r = s.residents.front();
        r.x = 41;
        r.y = 24;
        r.target_x = 42;
        r.target_y = 24;
        r.job = Job::Build;
        r.order_phase = OrderPhase::Active;
        s.routes.front().work = order.site;
    }
    import(w, std::move(s));
    return w;
}
void worker_replacement_requires_arrival() {
    for (bool died : {false, true}) {
        auto w = paid_site(true);
        const auto resources = w.resources();
        if (died) {
            auto s = w.export_state();
            s.residents.front().health = 0;
            import(w, std::move(s));
        } else
            CHECK(w.move({1}, 43, 26).accepted);
        const float progress = w.buildings().back().progress;
        bool travelling = false, arrived = false;
        for (int tick = 0; tick < 600; ++tick) {
            w.tick();
            w.take_path_results();
            bound(w);
            auto workforce = w.construction_workforce();
            if (!workforce.empty()) {
                travelling = travelling || workforce.front().travelling > 0;
                if (!workforce.front().working && !arrived)
                    CHECK(w.buildings().back().progress == progress);
                arrived = arrived || workforce.front().working > 0;
            }
            if (w.buildings().back().progress >= 1)
                break;
        }
        CHECK(travelling && arrived && w.buildings().back().progress == 1);
        CHECK(w.resources().wood == resources.wood && w.resources().stone == resources.stone);
        CHECK(w.item_reservations().empty());
    }
}
void blocked_site_retries_only_on_topology_change() {
    {
        auto pending = paid_site(false);
        pending.tick();
        CHECK(pending.work_intents().front().phase == WorkIntentPhase::PathPending);
        auto changed = pending.export_state();
        for (const auto cell : {CellCoord{41, 24}, CellCoord{43, 24}, CellCoord{42, 23}, CellCoord{42, 25}})
            tile(changed, cell) = {Terrain::Water, 0, true};
        ++changed.topology_revision;
        const auto revision = changed.topology_revision;
        import(pending, std::move(changed));
        for (int tick = 0; tick < 40; ++tick)
            pending.tick();
        CHECK(pending.work_intents().size() == 1);
        CHECK(pending.work_intents().front().failure == PathFailure::Unreachable);
        CHECK(pending.work_intents().front().last_attempt_topology_revision == revision);
        CHECK(pending.export_state().next_work_intent_id == 2);
    }
    auto w = paid_site(false);
    auto s = w.export_state();
    for (const auto cell : {CellCoord{41, 24}, CellCoord{43, 24}, CellCoord{42, 23}, CellCoord{42, 25}})
        tile(s, cell) = {Terrain::Water, 0, true};
    ++s.topology_revision;
    import(w, std::move(s));
    for (int i = 0;
         i < 100 && (w.work_intents().empty() || w.work_intents().back().phase != WorkIntentPhase::Failed);
         ++i)
        w.tick();
    CHECK(!w.work_intents().empty());
    CHECK(w.work_intents().back().failure == PathFailure::Unreachable);
    {
        auto invalid = w.export_state();
        auto duplicate = invalid.work_intents.back();
        duplicate.intent_id = invalid.next_work_intent_id++;
        invalid.work_intents.push_back(duplicate);
        const auto checksum = w.checksum();
        std::string error;
        CHECK(!w.import_state(std::move(invalid), error));
        CHECK(w.checksum() == checksum);
    }
    const auto counter = w.export_state().next_work_intent_id;
    for (int i = 0; i < 40; ++i)
        w.tick();
    CHECK(w.export_state().next_work_intent_id == counter);
    CHECK(w.buildings().back().progress == .2f);
    s = w.export_state();
    tile(s, {41, 24}) = {Terrain::Grass, 0, true};
    ++s.topology_revision;
    import(w, std::move(s));
    World copy(1, 0);
    import(copy, w.export_state());
    bool arrived = false;
    for (int i = 0; i < 600; ++i) {
        w.tick();
        copy.tick();
        bound(w);
        CHECK(w.checksum() == copy.checksum());
        const auto workforce = w.construction_workforce();
        arrived = arrived || (!workforce.empty() && workforce.front().working > 0);
        if (w.buildings().back().progress == 1)
            break;
    }
    CHECK(arrived && w.buildings().back().progress == 1);
    CHECK(w.resources().wood == 80 && w.resources().stone == 40);
}
void shared_builder_replan_remains_single() {
    auto w = paid_site(false);
    auto s = w.export_state();
    for (std::size_t i = 0; i < 2; ++i) {
        auto &r = s.residents[i];
        r.x = 39;
        r.y = 24;
        r.target_x = 42;
        r.target_y = 24;
        r.job = Job::Build;
        r.order_phase = OrderPhase::Active;
        s.routes[i].work = CellCoord{42, 24};
        s.routes[i].points = {{40, 24}, {41, 24}};
        s.routes[i].cursor = 0;
    }
    tile(s, {40, 24}) = {Terrain::Water, 0, true};
    ++s.topology_revision;
    s.terrain_generator = 5;
    import(w, std::move(s));
    w.tick();
    CHECK(w.work_intents().size() == 1);
    CHECK(w.work_intents().front().phase == WorkIntentPhase::Waiting);
    World copy(1, 0);
    import(copy, w.export_state());
    CHECK(copy.checksum() == w.checksum());
    auto legacy = paid_site(false);
    s = legacy.export_state();
    auto &actor = s.residents[1];
    actor.x = 39;
    actor.y = 24;
    actor.target_x = 42;
    actor.target_y = 24;
    actor.job = Job::Build;
    actor.order_phase = OrderPhase::Active;
    s.routes[1].work = CellCoord{42, 24};
    s.routes[1].points = {{40, 24}, {41, 24}};
    tile(s, {40, 24}) = {Terrain::Water, 0, true};
    ++s.topology_revision;
    s.terrain_generator = 5;
    WorkIntentState failed;
    failed.intent_id = 1;
    failed.kind = WorkIntentKind::BuildAssignment;
    failed.phase = WorkIntentPhase::Failed;
    failed.failure = PathFailure::Unreachable;
    failed.resident_id = 1;
    failed.building_id = 2;
    failed.target = {42, 24};
    failed.job = Job::Build;
    failed.admission_deadline_tick = 1200;
    failed.attempts = 1;
    failed.last_attempt_topology_revision = s.topology_revision;
    s.work_intents.push_back(failed);
    s.next_work_intent_id = 2;
    import(legacy, std::move(s));
    legacy.tick();
    CHECK(legacy.work_intents().size() == 2);
    CHECK(legacy.work_intents().front().completion == WorkCompletionReason::Superseded);
    import(copy, legacy.export_state());
    CHECK(copy.checksum() == legacy.checksum());
}
void authored_terrain_binding_survives_continuation() {
    auto w = dropoff(2, Terrain::Rock);
    w.tick();
    CHECK(w.work_intents().front().expected_terrain == Terrain::Rock);
    World copy(1, 0);
    import(copy, w.export_state());
    for (int tick = 0; tick < 300; ++tick) {
        w.tick();
        copy.tick();
        CHECK(w.checksum() == copy.checksum());
        if (w.inventory().stored.stone == 44 && w.work_diagnostics().active_intents == 0)
            break;
    }
    CHECK(w.inventory().stored.stone == 44);
    CHECK(w.residents().front().mining_xp == 2);
}
void capacity_fallback_and_retry_deadline() {
    auto w = dropoff();
    auto s = w.export_state();
    s.next_work_intent_id = std::numeric_limits<std::uint64_t>::max();
    import(w, std::move(s));
    w.tick();
    CHECK(w.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(w.work_intents().empty() && w.inventory().stored.stone == 42);
    CHECK(w.residents().front().order_phase == OrderPhase::PathFailed);
    CHECK(w.residents().front().path_failure == PathFailure::LimitExceeded);
    auto pressure = dropoff();
    fill_queue(pressure);
    pressure.tick();
    for (int i = 0; i < 1210; ++i) {
        pressure.tick();
        bound(pressure);
    }
    CHECK(pressure.work_intents().front().phase == WorkIntentPhase::Failed);
    CHECK(pressure.work_intents().front().failure == PathFailure::Timeout);
    CHECK(pressure.work_intents().front().attempts <= max_work_admission_attempts);
    CHECK(pressure.logistics_tasks().back().phase == LogisticsPhase::Complete);
    auto boundary = dropoff();
    fill_queue(boundary);
    boundary.tick();
    boundary.tick();
    auto limit = boundary.export_state();
    limit.ticks = std::numeric_limits<std::uint64_t>::max() - 2;
    auto &intent = limit.work_intents.front();
    intent.created_tick = std::numeric_limits<std::uint64_t>::max() - 1200;
    intent.admission_deadline_tick = std::numeric_limits<std::uint64_t>::max();
    intent.next_attempt_tick = limit.ticks + 1;
    intent.attempts = 5;
    // The authoritative four-day calendar horizon cannot represent this old
    // synthetic uint64 simulation-time fixture. Rejection must retain the live
    // world rather than manufacturing matching weather or relaxing validation.
    const auto before_rejection = boundary.checksum();
    std::string error;
    CHECK(!boundary.import_state(std::move(limit), error));
    CHECK(!error.empty());
    CHECK(boundary.checksum() == before_rejection);
}
void transactional_validation() {
    auto w = dropoff();
    fill_queue(w);
    w.tick();
    w.tick();
    const auto original = w.export_state();
    const auto checksum = w.checksum();
    auto reject = [&](auto mutate) {
        auto bad = original;
        mutate(bad);
        std::string error;
        CHECK(!w.import_state(std::move(bad), error));
        CHECK(!error.empty());
        CHECK(w.checksum() == checksum);
    };
    reject([](auto &s) { s.work_intents.front().owner_nation = 2; });
    reject([](auto &s) {
        s.work_intents.front().next_attempt_tick = s.work_intents.front().admission_deadline_tick + 1;
    });
    reject([](auto &s) { s.work_intents.front().failure = PathFailure::Timeout; });
    reject([](auto &s) { s.work_intents.front().last_attempt_topology_revision = s.topology_revision + 1; });
    reject([](auto &s) { s.work_intents.push_back(s.work_intents.front()); });
    reject([](auto &s) { s.work_intents.front().phase = WorkIntentPhase::PathPending; });
}
void admission_fairness_under_sustained_pressure() {
    World w(70421, 256);
    existing_passage(w);
    auto s = w.export_state();
    const CellCoord target{30, 24, WorldLayer::Mine};
    tile(s, target) = {Terrain::MineOre, 256, true};
    for (const auto &resident : s.residents) {
        WorkIntentState intent;
        intent.intent_id = s.next_work_intent_id++;
        intent.resident_id = resident.id;
        intent.owner_nation = resident.nation;
        intent.target = target;
        intent.job = Job::Mine;
        intent.expected_terrain = Terrain::MineOre;
        intent.admission_deadline_tick = work_admission_deadline_ticks;
        intent.last_attempt_topology_revision = s.topology_revision;
        s.work_intents.push_back(intent);
    }
    import(w, std::move(s));
    fill_queue(w);
    World copy(1, 0);
    import(copy, w.export_state());
    for (int tick = 0; tick < 8; ++tick) {
        w.tick();
        copy.tick();
        bound(w);
        CHECK(w.checksum() == copy.checksum());
    }
    CHECK(std::all_of(w.work_intents().begin(), w.work_intents().end(),
                      [](const auto &intent) { return intent.attempts == 1; }));
}
void terminal_diagnostics_survive_history_pruning() {
    World w(70421, 640);
    existing_passage(w);
    auto s = w.export_state();
    const CellCoord target{30, 24, WorldLayer::Mine};
    tile(s, target) = {Terrain::MineFloor, 0, true};
    for (const auto &resident : s.residents) {
        WorkIntentState intent;
        intent.intent_id = s.next_work_intent_id++;
        intent.resident_id = resident.id;
        intent.owner_nation = resident.nation;
        intent.target = target;
        intent.job = Job::Mine;
        intent.expected_terrain = Terrain::MineOre;
        intent.admission_deadline_tick = work_admission_deadline_ticks;
        intent.last_attempt_topology_revision = s.topology_revision;
        s.work_intents.push_back(intent);
    }
    import(w, std::move(s));
    for (int tick = 0; tick < 20; ++tick)
        w.tick();
    CHECK(w.work_diagnostics().active_intents == 0);
    CHECK(w.work_intents().size() == 256);
    CHECK(w.work_diagnostics().outcomes.exhausted == 640);
    CHECK(w.work_diagnostics().outcomes.created == 0); // Loaded state starts a new diagnostics epoch.
    World restored(1, 0);
    import(restored, w.export_state());
    CHECK(restored.checksum() == w.checksum());
    CHECK(restored.work_diagnostics().outcomes.exhausted == 0);
}
} // namespace
int main() {
    pressure_resume_and_manual_priority();
    exhausted_before_and_during_search();
    worker_replacement_requires_arrival();
    blocked_site_retries_only_on_topology_change();
    shared_builder_replan_remains_single();
    authored_terrain_binding_survives_continuation();
    capacity_fallback_and_retry_deadline();
    transactional_validation();
    admission_fairness_under_sustained_pressure();
    terminal_diagnostics_survive_history_pruning();
    if (failures)
        std::cerr << failures << " work intent checks failed\n";
    return failures ? 1 : 0;
}

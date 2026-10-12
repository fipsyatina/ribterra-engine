#include "existing_passage_fixture.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int run_path_adversarial_tests();

namespace {
int failures{};
#define CHECK(expression)                                                                                    \
    do {                                                                                                     \
        if (!(expression)) {                                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression "\n";                 \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

std::vector<int> ids(const ribterra::World &world) {
    std::vector<int> result;
    result.reserve(world.residents().size());
    for (const auto &resident : world.residents())
        result.push_back(resident.id);
    return result;
}

void shared_search_is_bounded_and_atomic() {
    using namespace ribterra;
    World world(6101, 1500);
    existing_passage(world);
    const auto before = world.residents();
    const auto command = world.move(ids(world), 28, 24, WorldLayer::Mine);
    CHECK(command.accepted && command.pending && command.path_batch_id != 0);
    CHECK(world.export_state().routes.front().points.empty());
    CHECK(std::all_of(world.residents().begin(), world.residents().end(), [](const Resident &resident) {
        return resident.order_phase == OrderPhase::PathPending && resident.job == Job::Idle;
    }));
    CHECK(!world.move({1}, 24, 24).accepted);

    bool completed = false;
    for (int tick = 0; tick < 1200 && !completed; ++tick) {
        world.tick();
        const auto diagnostics = world.path_diagnostics();
        CHECK(diagnostics.work_units_last_tick <= path_work_units_per_tick);
        CHECK(diagnostics.staged_route_cells <= max_staged_route_cells);
        CHECK(diagnostics.planner_bytes <= max_planner_bytes);
        const auto active =
            std::count_if(world.residents().begin(), world.residents().end(), [](const Resident &resident) {
                return resident.order_phase == OrderPhase::Active;
            });
        if (active != 0 && active != 1500) std::cerr << "partial active count=" << active << '\n';
        CHECK(active == 0 || active == 1500);
        if (active == 0)
            for (std::size_t i = 0; i < before.size(); ++i) {
                CHECK(world.residents()[i].x == before[i].x);
                CHECK(world.residents()[i].y == before[i].y);
            }
        for (const auto &outcome : world.take_path_results()) {
            if (outcome.path_batch_id == command.path_batch_id) {
                CHECK(outcome.accepted && outcome.failure == PathFailure::None);
                completed = true;
            }
        }
    }
    CHECK(completed);
    CHECK(world.path_diagnostics().total_expansions < 512);
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(std::abs(world.residents()[i].x - before[i].x) <= 0.1251F);
        CHECK(std::abs(world.residents()[i].y - before[i].y) <= 0.1251F);
    }
    CHECK(world.take_path_results().empty());
}

void pending_state_resumes_on_the_same_tick() {
    using namespace ribterra;
    World uninterrupted(6102, 1500);
    existing_passage(uninterrupted);
    const auto command = uninterrupted.move(ids(uninterrupted), 28, 24, WorldLayer::Mine);
    CHECK(command.accepted);
    uninterrupted.tick();
    CHECK(!uninterrupted.export_state().path_batches.empty());

    World resumed(1, 0);
    std::string error;
    const bool imported = resumed.import_state(uninterrupted.export_state(), error);
    if (!imported) std::cerr << "resume import: " << error << '\n';
    CHECK(imported);
    CHECK(resumed.checksum() == uninterrupted.checksum());
    bool completed = false;
    for (int tick = 0; tick < 1200 && !completed; ++tick) {
        uninterrupted.tick();
        resumed.tick();
        auto first = uninterrupted.take_path_results();
        auto second = resumed.take_path_results();
        CHECK(first == second);
        CHECK(uninterrupted.checksum() == resumed.checksum());
        CHECK(uninterrupted.residents() == resumed.residents());
        completed = std::any_of(first.begin(), first.end(), [&](const PathBatchOutcome &outcome) {
            return outcome.path_batch_id == command.path_batch_id;
        });
    }
    CHECK(completed);
}

void timeout_and_build_reservations_are_terminal_and_lossless() {
    using namespace ribterra;
    World timeout_source(6103, 1);
    existing_passage(timeout_source);
    const auto command = timeout_source.move({1}, 28, 24, WorldLayer::Mine);
    auto pending = timeout_source.export_state();
    CHECK(command.accepted && pending.path_batches.size() == 1);
    test::set_fixture_simulation_tick(pending, pending.path_batches.front().deadline_tick);
    World timed_out(1, 0);
    std::string error;
    CHECK(timed_out.import_state(std::move(pending), error));
    timed_out.tick();
    const auto timeout_results = timed_out.take_path_results();
    CHECK(timeout_results.size() == 1);
    CHECK(!timeout_results.front().accepted);
    CHECK(timeout_results.front().failure == PathFailure::Timeout);
    // The failed order remains terminal even if daily life schedules a new walk.
    CHECK(timed_out.manual_orders().front().phase == ManualOrderPhase::Failed);
    CHECK(timed_out.manual_orders().front().failure == PathFailure::Timeout);
    CHECK(timed_out.export_state().routes.front().points.empty());

    World building(6104, 0);
    const auto resources = building.resources();
    const auto first = building.build(BuildingKind::House, 21, 21);
    const auto second = building.build(BuildingKind::House, 22, 21);
    CHECK(first.accepted && second.accepted);
    CHECK(building.resources() == resources);
    CHECK(!building.build(BuildingKind::House, 21, 21).accepted);
    CHECK(!building.build(BuildingKind::House, 23, 21).accepted);
    building.tick();
    const auto outcomes = building.take_path_results();
    CHECK(outcomes.size() == 2);
    CHECK(std::all_of(outcomes.begin(), outcomes.end(),
                      [](const PathBatchOutcome &outcome) { return outcome.accepted; }));
    CHECK(building.buildings().size() == 3);
    CHECK(building.resources().wood == resources.wood);
    CHECK(building.resources().stone == resources.stone);
    CHECK(building.inventory().reserved.wood == 60);
    CHECK(building.inventory().reserved.stone == 30);
    CHECK(building.inventory().available.wood == resources.wood - 60);
    CHECK(building.inventory().available.stone == resources.stone - 30);
}

void stale_gather_target_is_rejected_at_commit() {
    using namespace ribterra;
    World source(6105, 1);
    CellCoord resource{};
    bool found = false;
    for (int y = 10; y <= 42 && !found; ++y)
        for (int x = 16; x <= 46; ++x)
            if (source.tile(x, y).terrain == Terrain::Forest && source.tile(x, y).amount > 0) {
                resource = {x, y, WorldLayer::Surface};
                found = true;
                break;
            }
    CHECK(found);
    const auto command = source.gather({1}, resource.x, resource.y, resource.layer);
    CHECK(command.accepted);
    auto state = source.export_state();
    auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                              [&](const Chunk &value) { return value.key == chunk_key(resource); });
    CHECK(chunk != state.chunks.end());
    if (chunk != state.chunks.end()) {
        chunk->tiles[local_index(resource.x, resource.y)] = {Terrain::Grass, 0, true};
        ++chunk->revision;
    }
    ++state.topology_revision;
    World loaded(1, 0);
    std::string error;
    CHECK(loaded.import_state(std::move(state), error));
    bool rejected = false;
    for (int tick = 0; tick < 1201 && !rejected; ++tick) {
        loaded.tick();
        for (const auto &outcome : loaded.take_path_results())
            if (outcome.path_batch_id == command.path_batch_id) {
                CHECK(!outcome.accepted);
                CHECK(outcome.failure == PathFailure::TargetInvalid);
                rejected = true;
            }
    }
    CHECK(rejected);
    CHECK(loaded.export_state().routes.front().points.empty());
}

void boundary_neighbors_never_encode_outside_cells() {
    using namespace ribterra;
    World world(6106, 1);
    CellCoord target{};
    bool found = false;
    for (int y = world_min; y < world_max; ++y)
        if (world.walkable(world_max - 1, y, WorldLayer::Mine)) {
            target = {world_max - 1, y, WorldLayer::Mine};
            found = true;
            break;
        }
    CHECK(found);
    const auto command = world.move({1}, target.x, target.y, target.layer);
    CHECK(command.accepted);
    bool terminal = false;
    for (int tick = 0; tick < 1201 && !terminal; ++tick) {
        world.tick();
        for (const auto &outcome : world.take_path_results())
            terminal = terminal || outcome.path_batch_id == command.path_batch_id;
    }
    CHECK(terminal);
}
} // namespace

void fractional_zero_edge_route_reaches_its_cell() {
    using namespace ribterra;
    World world(70421, 1);
    auto state = world.export_state();
    state.residents.front().x = 24.F;
    state.residents.front().y = 22.5F;
    std::string error;
    CHECK(world.import_state(std::move(state), error));
    CHECK(world.walkable(24, 23));
    CHECK(world.move({1}, 24, 23).accepted);
    world.tick();
    CHECK(world.residents().front().y == 22.625F);
    CHECK(world.residents().front().order_phase == OrderPhase::Active);
    CHECK(world.export_state().routes.front().points.size() == 1);
    World resumed(1, 0);
    CHECK(resumed.import_state(world.export_state(), error));
    for (int tick = 0; tick < 8; ++tick) {
        world.tick();
        resumed.tick();
        CHECK(world.checksum() == resumed.checksum());
    }
    CHECK(world.residents().front().x == 24.F && world.residents().front().y == 23.F);
    CHECK(world.residents().front().job == Job::Idle);
    CHECK(world.path_diagnostics().queued_batches == 0);
}

int main() {
    fractional_zero_edge_route_reaches_its_cell();
    shared_search_is_bounded_and_atomic();
    pending_state_resumes_on_the_same_tick();
    timeout_and_build_reservations_are_terminal_and_lossless();
    stale_gather_target_is_rejected_at_commit();
    boundary_neighbors_never_encode_outside_cells();
    failures += run_path_adversarial_tests();
    if (failures) {
        std::cerr << failures << " path planner test(s) failed.\n";
        return 1;
    }
    std::cout << "Bounded deterministic path planner tests passed.\n";
}

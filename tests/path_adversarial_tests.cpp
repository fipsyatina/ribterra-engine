#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures{};
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression "\n";       \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct TempSave {
    TempSave()
        : path(std::filesystem::temp_directory_path() / "ribterra_path_adversarial.ribsave") {
        cleanup();
    }
    ~TempSave() { cleanup(); }
    void cleanup() const {
        std::error_code error;
        for (const char *suffix : {"", ".tmp", ".bak", ".bak.tmp"})
            std::filesystem::remove(path.string() + suffix, error);
    }
    std::filesystem::path path;
};

ribterra::CellCoord far_walkable(const ribterra::World &world) {
    using namespace ribterra;
    for (int y = world_min; y < world_min + 24; ++y)
        for (int x = world_min; x < world_min + 24; ++x)
            if (world.walkable(x, y, WorldLayer::Surface))
                return {x, y, WorldLayer::Surface};
    return {};
}

ribterra::CellCoord buildable_farm_site(const ribterra::World &world) {
    using namespace ribterra;
    for (int y = world_min; y < world_max; ++y)
        for (int x = world_min; x < world_max; ++x)
            if (world.can_build(BuildingKind::Farm, x, y, WorldLayer::Surface))
                return {x, y, WorldLayer::Surface};
    return {};
}

void retained_outcomes_enforce_limit_until_drained() {
    using namespace ribterra;
    World world(6201, 1);
    auto state = world.export_state();
    state.next_path_batch_id = max_path_batches + 1;
    state.path_results.reserve(max_path_batches);
    for (std::uint64_t id = 1; id <= max_path_batches; ++id) {
        state.path_results.push_back(
            {id, PathCommandKind::Move, false, PathFailure::Timeout, "Path planning timed out."});
    }
    std::string error;
    CHECK(world.import_state(std::move(state), error));
    const auto target = far_walkable(world);
    CHECK(world.walkable(target.x, target.y, target.layer));

    const auto blocked = world.move({1}, target.x, target.y, target.layer);
    CHECK(!blocked.accepted);
    CHECK(blocked.failure == PathFailure::LimitExceeded);
    const auto outcomes = world.take_path_results();
    CHECK(outcomes.size() == max_path_batches);
    CHECK(std::is_sorted(outcomes.begin(), outcomes.end(), [](const auto &a, const auto &b) {
        return a.path_batch_id < b.path_batch_id;
    }));
    CHECK(outcomes.front().path_batch_id == 1);
    CHECK(outcomes.back().path_batch_id == max_path_batches);
    CHECK(world.take_path_results().empty());

    const auto recovered = world.move({1}, target.x, target.y, target.layer);
    CHECK(recovered.accepted && recovered.pending);
    CHECK(recovered.path_batch_id == max_path_batches + 1);
}

void fourth_topology_invalidation_releases_fifo() {
    using namespace ribterra;
    World world(6202, 2);
    const auto target = far_walkable(world);
    CHECK(target.x != 0 || target.y != 0);
    const auto first = world.move({1}, target.x, target.y, target.layer);
    const auto second = world.move({2}, target.x, target.y, target.layer);
    CHECK(first.accepted && second.accepted);

    bool searching = false;
    for (int tick = 0; tick < 40 && !searching; ++tick) {
        world.tick();
        const auto state = world.export_state();
        searching = !state.path_batches.empty() &&
                    state.path_batches.front().path_batch_id == first.path_batch_id &&
                    state.path_batches.front().phase == PathPlannerPhase::Search;
    }
    CHECK(searching);

    std::string error;
    for (std::uint8_t restart = 1; restart <= 4; ++restart) {
        auto state = world.export_state();
        ++state.topology_revision;
        CHECK(world.import_state(std::move(state), error));
        world.tick();
        const auto after = world.export_state();
        if (restart <= 3) {
            CHECK(after.path_results.empty());
            CHECK(!after.path_batches.empty());
            CHECK(after.path_batches.front().path_batch_id == first.path_batch_id);
            CHECK(after.path_batches.front().restart_count == restart);
        }
    }

    const auto state = world.export_state();
    CHECK(state.path_results.size() == 1);
    CHECK(state.path_results.front().path_batch_id == first.path_batch_id);
    CHECK(!state.path_results.front().accepted);
    CHECK(state.path_results.front().failure == PathFailure::TopologyChanged);
    CHECK(!state.path_batches.empty());
    CHECK(state.path_batches.front().path_batch_id == second.path_batch_id);
    CHECK(world.residents().front().order_phase == OrderPhase::PathFailed);
    CHECK(world.residents().back().order_phase == OrderPhase::PathPending);
    const auto drained = world.take_path_results();
    CHECK(drained.size() == 1 && drained.front().path_batch_id == first.path_batch_id);
    CHECK(world.export_state().path_batches.front().path_batch_id == second.path_batch_id);
}

void pending_farm_reservation_survives_upkeep_and_save() {
    using namespace ribterra;
    World world(6203, 100);
    const auto target = far_walkable(world);
    std::vector<int> residents;
    residents.reserve(world.residents().size());
    for (const auto &resident : world.residents())
        residents.push_back(resident.id);
    const auto blocker = world.move(residents, target.x, target.y, target.layer);
    CHECK(blocker.accepted);
    const auto site = buildable_farm_site(world);
    const auto farm = world.build(BuildingKind::Farm, site.x, site.y, site.layer);
    CHECK(farm.accepted && farm.pending);

    auto state = world.export_state();
    CHECK(state.path_batches.size() == 2);
    ribterra::test::set_fixture_simulation_tick(state, 199);
    std::string error;
    CHECK(world.import_state(std::move(state), error));
    world.tick();
    const auto after_upkeep = world.export_state();
    CHECK(after_upkeep.ticks == 200);
    CHECK(after_upkeep.resources.food == World::cost(BuildingKind::Farm).food);
    const auto pending_farm = std::find_if(
        after_upkeep.path_batches.begin(), after_upkeep.path_batches.end(),
        [&](const PathBatchState &batch) { return batch.path_batch_id == farm.path_batch_id; });
    CHECK(pending_farm != after_upkeep.path_batches.end());
    if (pending_farm != after_upkeep.path_batches.end())
        CHECK(pending_farm->reserved_cost == World::cost(BuildingKind::Farm));

    TempSave slot;
    CHECK(world.save(slot.path, error));
    World loaded(1, 0);
    CHECK(loaded.load(slot.path, error));
    const auto restored = loaded.export_state();
    CHECK(restored.resources.food >= 0);
    CHECK(restored.resources == after_upkeep.resources);
    CHECK(restored.path_batches == after_upkeep.path_batches);
    world.tick();
    loaded.tick();
    CHECK(world.checksum() == loaded.checksum());
}
} // namespace

int run_path_adversarial_tests() {
    retained_outcomes_enforce_limit_until_drained();
    fourth_topology_invalidation_releases_fifo();
    pending_farm_reservation_survives_upkeep_and_save();
    return failures;
}

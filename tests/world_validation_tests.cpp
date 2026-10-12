#include "existing_passage_fixture.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
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

template <typename Mutation>
void rejected_without_mutation(ribterra::World &target, const ribterra::WorldState &valid,
                               Mutation mutation) {
    auto bad = valid;
    mutation(bad);
    const auto before = target.checksum();
    std::string error;
    CHECK(!target.import_state(std::move(bad), error));
    CHECK(!error.empty());
    CHECK(target.checksum() == before);
}

void scalar_collection_and_text_validation() {
    using namespace ribterra;
    World source(1201, 2);
    existing_passage(source);
    const auto valid = source.export_state();
    World target(1202, 2);

    rejected_without_mutation(
        target, valid, [](auto &s) { s.residents[0].x = std::numeric_limits<float>::quiet_NaN(); });
    rejected_without_mutation(
        target, valid, [](auto &s) { s.residents[0].y = std::numeric_limits<float>::infinity(); });
    rejected_without_mutation(
        target, valid, [](auto &s) { s.residents[0].x = static_cast<float>(world_min) - 0.01F; });
    rejected_without_mutation(target, valid, [](auto &s) { s.residents[0].name.clear(); });
    rejected_without_mutation(target, valid, [](auto &s) { s.residents[0].name.assign(129, 'x'); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.residents[0].name = std::string("\xc3\x28", 2); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.residents[0].job = static_cast<Job>(8); });
    rejected_without_mutation(target, valid, [](auto &s) { s.residents[0].job = Job::Farm; });
    rejected_without_mutation(target, valid, [](auto &s) { s.residents[0].wood_xp = -1; });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.residents[0].mining_xp = 1'000'000'001; });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.buildings[0].kind = static_cast<BuildingKind>(4); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.chunks[0].tiles[0].amount = 1'000'001; });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.portals.resize(65, s.portals.front()); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.buildings.resize(65'537, s.buildings.front()); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.events.push_back(std::string(513, 'e')); });
    rejected_without_mutation(
        target, valid, [](auto &s) { s.events.push_back(std::string("\xf0\x28\x8c\x28", 4)); });
    rejected_without_mutation(target, valid,
                              [](auto &s) { s.ticks = std::numeric_limits<std::uint64_t>::max(); });
    rejected_without_mutation(
        target, valid, [](auto &s) { s.next_building_id = std::numeric_limits<int>::max(); });

    auto edge = valid;
    edge.residents[0].x = static_cast<float>(world_max) - 0.125F;
    edge.residents[0].y = static_cast<float>(world_min);
    edge.residents[0].target_x = world_max - 1;
    edge.residents[0].target_y = world_min;
    std::string error;
    CHECK(target.import_state(std::move(edge), error));
}

void route_continuity_accepts_runtime_states_and_rejects_teleports() {
    using namespace ribterra;
    World portal_source(1300, 1);
    existing_passage(portal_source);
    auto portal_step = portal_source.export_state();
    portal_step.residents[0].x = 28.0F;
    portal_step.residents[0].y = 24.0F;
    portal_step.residents[0].target_x = 28;
    portal_step.residents[0].target_y = 24;
    portal_step.routes[0].points = {{28, 24, WorldLayer::Mine}};
    portal_step.routes[0].cursor = 0;
    portal_step.routes[0].work = CellCoord{28, 24, WorldLayer::Mine};
    World portal_import(1, 0);
    std::string error;
    CHECK(portal_import.import_state(portal_step, error));
    rejected_without_mutation(portal_import, portal_step,
                              [](auto &s) { s.routes[0].points[0] = {29, 24, WorldLayer::Mine}; });

    World moving(1301, 2);
    CHECK(moving.move({1}, 30, 24).accepted);
    for (int i = 0; i < 6; ++i)
        moving.tick();
    const auto midstep = moving.export_state();
    CHECK(midstep.routes[0].cursor < midstep.routes[0].points.size());
    World imported(1, 0);
    CHECK(imported.import_state(midstep, error));

    rejected_without_mutation(imported, midstep, [](auto &s) {
        s.routes[0].points[0] = {100, 100, WorldLayer::Surface};
    });
    auto progressed = midstep;
    while (progressed.routes[0].cursor == 0) {
        moving.tick();
        progressed = moving.export_state();
    }
    CHECK(progressed.routes[0].cursor < progressed.routes[0].points.size());
    rejected_without_mutation(imported, progressed, [](auto &s) { s.residents[0].x += 5.0F; });

    for (int i = 0; i < 2000 && moving.residents()[0].job != Job::Idle; ++i)
        moving.tick();
    const auto exhausted = moving.export_state();
    CHECK(exhausted.routes[0].cursor == exhausted.routes[0].points.size());
    CHECK(imported.import_state(exhausted, error));
    rejected_without_mutation(imported, exhausted, [](auto &s) { s.residents[0].x += 1.0F; });
}

void resident_permutation_is_deterministic() {
    using namespace ribterra;
    World source(1401, 20);
    CHECK(source.move({1, 2, 3, 4}, 30, 24).accepted);
    for (int i = 0; i < 17; ++i)
        source.tick();
    auto normal_state = source.export_state();
    auto shuffled_state = normal_state;
    std::reverse(shuffled_state.residents.begin(), shuffled_state.residents.end());
    std::reverse(shuffled_state.routes.begin(), shuffled_state.routes.end());
    World normal(1, 0), shuffled(2, 0);
    std::string error;
    CHECK(normal.import_state(std::move(normal_state), error));
    CHECK(shuffled.import_state(std::move(shuffled_state), error));
    CHECK(normal.checksum() == shuffled.checksum());
    for (int i = 0; i < 200; ++i) {
        normal.tick();
        shuffled.tick();
    }
    CHECK(normal.checksum() == shuffled.checksum());
    CHECK(normal.export_state().residents == shuffled.export_state().residents);
}

void four_full_nations_are_accepted() {
    using namespace ribterra;
    World source(1501, 1500);
    auto state = source.export_state();
    const auto original_residents = state.residents;
    const auto original_routes = state.routes;
    for (NationId nation = 2; nation <= 4; ++nation) {
        const int offset = static_cast<int>((nation - 1) * 1500);
        for (std::size_t i = 0; i < original_residents.size(); ++i) {
            auto resident = original_residents[i];
            resident.id += offset;
            resident.nation = nation;
            state.residents.push_back(std::move(resident));
            auto route = original_routes[i];
            route.resident_id += offset;
            state.routes.push_back(std::move(route));
        }
    }
    World imported(1, 0);
    std::string error;
    CHECK(imported.import_state(std::move(state), error));
    CHECK(imported.residents().size() == 6000);
    imported.tick();
    CHECK(imported.ticks() == 1);
}

void planner_state_rejects_multiple_active_batches() {
    using namespace ribterra;
    World source(1601, 2);
    CHECK(source.move({1}, 27, 24).accepted);
    CHECK(source.move({2}, 26, 24).accepted);
    const auto valid = source.export_state();
    CHECK(valid.path_batches.size() == 2);
    World target(1602, 2);
    rejected_without_mutation(
        target, valid, [](auto &state) { state.path_batches[1].phase = PathPlannerPhase::Search; });
    rejected_without_mutation(target, valid, [](auto &state) {
        state.path_batches[0].frontier_cursor = state.path_batches[0].frontier.size() + 1;
    });
    rejected_without_mutation(target, valid, [](auto &state) {
        state.residents[0].pending_path_batch = state.next_path_batch_id;
    });

    World active_source(1603, 1500);
    existing_passage(active_source);
    std::vector<int> residents;
    for (const auto &resident : active_source.residents())
        residents.push_back(resident.id);
    CHECK(active_source.move(residents, 28, 24, WorldLayer::Mine).accepted);
    active_source.tick();
    const auto active = active_source.export_state();
    CHECK(active.path_batches.size() == 1);
    rejected_without_mutation(target, active,
                              [](auto &state) { state.path_batches[0].assigned_job = Job::Idle; });
    rejected_without_mutation(target, active,
                              [](auto &state) { state.path_batches[0].roots[0].x += 1; });
    rejected_without_mutation(target, active, [](auto &state) {
        auto &batch = state.path_batches[0];
        batch.phase = PathPlannerPhase::Stage;
        batch.reconstruction_resident = 0;
        batch.stage_cursor = 0;
    });
    rejected_without_mutation(target, active, [](auto &state) {
        auto &draft = state.path_batches[0].drafts[0];
        draft.start.x += 1;
    });
    rejected_without_mutation(target, active, [](auto &state) {
        auto &draft = state.path_batches[0].drafts[0];
        draft.reconstruction_cursor = CellCoord{159, 159, WorldLayer::Mine};
    });
}
} // namespace

int main() {
    scalar_collection_and_text_validation();
    route_continuity_accepts_runtime_states_and_rejects_teleports();
    resident_permutation_is_deterministic();
    four_full_nations_are_accepted();
    planner_state_rejects_multiple_active_batches();
    if (failures != 0) {
        std::cerr << failures << " world validation test(s) failed.\n";
        return 1;
    }
    std::cout << "World import validation tests passed.\n";
    return 0;
}

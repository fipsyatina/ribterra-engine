#include "ribterra/world_state.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n"; ++failures; \
} } while (false)

ribterra::WorldState work_order_state() {
    using namespace ribterra;
    World world(70421, 1);
    auto state = world.export_state();
    state.buildings.push_back({2, BuildingKind::House, 22, 22, .2f, WorldLayer::Surface, 1});
    state.next_building_id = 3;
    state.construction_orders.push_back({2, BuildingKind::House, {22, 22, WorldLayer::Surface},
        1, {30, 15, 0}, {30, 15, 0}, ConstructionPhase::Work, 0});
    return state;
}

void cancellation_refund_is_atomic_at_allocation_limits() {
    using namespace ribterra;
    for (bool full_collection : {false, true}) {
        auto state = work_order_state();
        if (full_collection) {
            while (state.item_stacks.size() < max_item_stacks) {
                const auto id = state.next_item_stack_id++;
                state.item_stacks.push_back({id, ItemKind::Stone, 1, 2,
                    {ItemLocationKind::Ground, {0, 0, WorldLayer::Surface}, 0}});
            }
        } else {
            // Two different resource refunds need two new ground stacks. Only one ID remains.
            state.next_item_stack_id = std::numeric_limits<std::uint64_t>::max() - 1;
        }
        World target(2, 2);
        std::string error;
        const bool imported = target.import_state(state, error);
        CHECK(imported);
        if (!imported) { std::cerr << error << '\n'; continue; }
        const auto before = target.checksum();
        const auto total = target.inventory().total;
        const auto result = target.cancel_construction(2);
        if (!result.accepted) {
            CHECK(target.checksum() == before);
        } else {
            const auto after = target.inventory().total;
            CHECK(after.wood == total.wood + 30 && after.stone == total.stone + 15 && after.food == total.food);
            CHECK(std::none_of(target.buildings().begin(), target.buildings().end(),
                               [](const auto &building) { return building.id == 2; }));
            const auto cancelled = target.checksum();
            CHECK(!target.cancel_construction(2).accepted);
            CHECK(target.checksum() == cancelled);
        }
        World validator(3, 3);
        CHECK(validator.import_state(target.export_state(), error));
    }
}

void aggregate_item_capacity_does_not_overflow_or_destroy_source() {
    using namespace ribterra;
    World world(70421, 1);
    auto state = world.export_state();
    state.item_stacks.front().quantity = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    state.resources.wood = std::numeric_limits<int>::max();
    state.storages.front().capacity = std::numeric_limits<std::uint32_t>::max();
    const CellCoord tree{21, 24, WorldLayer::Surface};
    for (auto &chunk : state.chunks)
        if (chunk.key == chunk_key(tree)) chunk.tiles[local_index(tree.x, tree.y)] = {Terrain::Forest, 20, true};
    auto &resident = state.residents.front();
    resident.x = 22; resident.y = 24;
    resident.target_x = tree.x; resident.target_y = tree.y;
    resident.job = Job::Woodcut; resident.progress = .999f; resident.order_phase = OrderPhase::Active;
    state.routes.front().points.clear(); state.routes.front().cursor = 0;
    state.routes.front().work = tree;
    std::string error;
    const bool imported = world.import_state(state, error);
    CHECK(imported);
    if (!imported) { std::cerr << error << '\n'; return; }
    for (int i = 0; i < 4; ++i) world.tick();
    CHECK(world.inventory().total.wood == std::numeric_limits<int>::max());
    CHECK(world.tile(tree.x, tree.y).amount == 20);
    CHECK(world.residents().front().wood_xp == resident.wood_xp);
    World validator(5, 1);
    CHECK(validator.import_state(world.export_state(), error));
}

void legacy_migration_rejects_arithmetic_hazards() {
    using namespace ribterra;
    World world(70421, 1);
    auto legacy = world.export_state();
    legacy.item_stacks.clear(); legacy.storages.clear();
    legacy.buildings.front().x = std::numeric_limits<int>::min();
    PathBatchState batch;
    batch.path_batch_id = 1; batch.command = PathCommandKind::Build;
    batch.build_kind = BuildingKind::House; batch.building_id = 2;
    batch.target = {22, 22, WorldLayer::Surface}; batch.reserved_cost = World::cost(BuildingKind::House);
    legacy.path_batches.push_back(batch);
    legacy.next_building_id = 3; legacy.next_path_batch_id = 2;
    std::string error;
    CHECK(!World::migrate_legacy_inventory(legacy, error));
    CHECK(!error.empty());
    CHECK(legacy.item_stacks.empty() && legacy.item_reservations.empty());
}
}

int main() {
    cancellation_refund_is_atomic_at_allocation_limits();
    aggregate_item_capacity_does_not_overflow_or_destroy_source();
    legacy_migration_rejects_arithmetic_hazards();
    if (failures) { std::cerr << failures << " adversarial logistics checks failed.\n"; return 1; }
    std::cout << "Adversarial logistics invariants passed.\n";
}

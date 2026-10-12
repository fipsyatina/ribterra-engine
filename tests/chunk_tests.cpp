#include "existing_passage_fixture.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <iostream>
#include <string>

namespace {
int failures{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #x "\n";                \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

void coordinate_contract() {
    using namespace ribterra;
    CHECK(inside_world(-96, -96));
    CHECK(inside_world(159, 159));
    CHECK(!inside_world(-97, 0));
    CHECK(!inside_world(world_max, 0));
    CHECK(chunk_key({-96, -96, WorldLayer::Surface}).cx == -3);
    CHECK(chunk_key({159, 159, WorldLayer::Mine}).cx == 4);
    for (int v : {-96, -65, -64, -33, -32, -1, 0, 31, 32, 159}) {
        const auto i = local_index(v, v);
        CHECK(i < 1024);
        const int local = v - floor_div(v, 32) * 32;
        CHECK(i == static_cast<std::size_t>(local * 32 + local));
    }
    for (auto layer : {WorldLayer::Surface, WorldLayer::Mine})
        for (int y : {-96, 159})
            for (int x : {-96, 159}) {
                auto i = world_cell_index(x, y);
                CHECK(i.has_value());
                const CellCoord expected{x, y, layer};
                CHECK(cell_from_index(*i, layer) == expected);
            }
}
void generated_chunks_and_read_stability() {
    using namespace ribterra;
    World world(123, 1);
    CHECK(world.chunks().size() == world_chunk_count);
    const auto before = world.chunks().size();
    (void)world.tile(-97, 0);
    (void)world.tile(160, 0, WorldLayer::Mine);
    CHECK(world.chunks().size() == before);
    CHECK(world.tile(-97, 0).terrain == Terrain::Water);
    CHECK(world.tile(28, 24, WorldLayer::Mine).terrain == Terrain::MineFloor);
    CHECK((world.tile(-96, -96, WorldLayer::Mine) == Tile{Terrain::MineFloor, 0, false}));
    CHECK((world.tile(-96, 159, WorldLayer::Mine) == Tile{Terrain::MineOre, 17, false}));
    CHECK((world.tile(159, 159, WorldLayer::Mine) == Tile{Terrain::MineOre, 28, false}));
    CHECK(world.portals().empty());
}
void portal_route_and_mine_depletion() {
    using namespace ribterra;
    World world(731, 1);
    existing_passage(world);
    const auto move = world.move({1}, 28, 24, WorldLayer::Mine);
    CHECK(move.accepted && move.pending);
    for (int i = 0; i < 2000 && world.residents()[0].order_phase == OrderPhase::PathPending; ++i)
        world.tick();
    auto state = world.export_state();
    const auto &r = state.routes.front();
    bool crossed = false;
    for (size_t i = 1; i < r.points.size(); ++i)
        if (r.points[i - 1].layer != r.points[i].layer) {
            crossed = true;
            CHECK(r.points[i - 1] == world.portals()[0].surface);
            CHECK(r.points[i] == world.portals()[0].mine);
        }
    CHECK(crossed);
    for (int i = 0; i < 2000 && world.residents()[0].layer != WorldLayer::Mine; ++i)
        world.tick();
    CHECK(world.residents()[0].layer == WorldLayer::Mine);
    CellCoord ore{};
    bool found = false;
    for (int radius = 1; radius < 80 && !found; ++radius)
        for (int y = 24 - radius; y <= 24 + radius && !found; ++y)
            for (int x = 28 - radius; x <= 28 + radius; ++x)
                if (inside_world(x, y) &&
                    world.tile(x, y, WorldLayer::Mine).terrain == Terrain::MineOre) {
                    ore = {x, y, WorldLayer::Mine};
                    found = true;
                    break;
                }
    CHECK(found);
    if (found) {
        const auto gather = world.gather({1}, ore.x, ore.y, ore.layer);
        CHECK(gather.accepted && gather.pending);
        for (int i = 0; i < 5000 && world.tile(ore.x, ore.y, ore.layer).amount > 0; ++i)
            world.tick();
        CHECK(world.tile(ore.x, ore.y, ore.layer).terrain == Terrain::MineFloor);
    }
}
void transactional_route_validation() {
    using namespace ribterra;
    World source(44, 1);
    existing_passage(source);
    auto state = source.export_state();
    std::string error;
    state.routes[0].work = CellCoord{-1, -1, WorldLayer::Surface};
    state.residents[0].target_x = -1;
    state.residents[0].target_y = -1;
    World accepted(1, 0);
    CHECK(accepted.import_state(state, error));
    const CellCoord valid_negative_work{-1, -1, WorldLayer::Surface};
    CHECK(accepted.export_state().routes[0].work == valid_negative_work);
    auto null_state = accepted.export_state();
    null_state.routes[0].work.reset();
    CHECK(accepted.import_state(null_state, error));
    auto bad = accepted.export_state();
    bad.routes[0].points = {{0, 0, WorldLayer::Surface}, {0, 0, WorldLayer::Mine}};
    bad.routes[0].cursor = 0;
    CHECK(!accepted.import_state(bad, error));
    auto duplicate = accepted.export_state();
    duplicate.portals.push_back(duplicate.portals.front());
    CHECK(!accepted.import_state(duplicate, error));
    auto cursor = accepted.export_state();
    cursor.routes[0].cursor = 1;
    cursor.routes[0].points.clear();
    CHECK(!accepted.import_state(cursor, error));
}
void stable_projection_and_population_limits() {
    using namespace ribterra;
    World world(99, 1500);
    auto state = world.export_state();
    std::string error;
    Resident extra = state.residents.back();
    extra.id = 1501;
    extra.nation = 2;
    state.residents.push_back(extra);
    RouteState route = state.routes.back();
    route.resident_id = 1501;
    state.routes.push_back(route);
    World multi(1, 0);
    CHECK(multi.import_state(state, error));
    CHECK(multi.residents().size() == 1501);
    CHECK(std::is_sorted(multi.residents().begin(), multi.residents().end(),
                         [](auto &a, auto &b) { return a.id < b.id; }));
    auto too_many = state;
    too_many.residents.back().nation = 1;
    CHECK(!multi.import_state(too_many, error));
    auto legacy = world.export_state();
    legacy.residents[0].x = 0;
    legacy.residents[0].y = 47;
    legacy.residents[0].target_x = 0;
    legacy.residents[0].target_y = 47;
    legacy.routes[0].work = CellCoord{0, 47, WorldLayer::Surface};
    World migrated(2, 0);
    CHECK(migrated.import_state(legacy, error));
    CHECK(migrated.residents()[0].x == 0 && migrated.residents()[0].y == 47);
}
} // namespace
int main() {
    coordinate_contract();
    generated_chunks_and_read_stability();
    portal_route_and_mine_depletion();
    transactional_route_validation();
    stable_projection_and_population_limits();
    if (failures) {
        std::cerr << failures << " chunk test(s) failed.\n";
        return 1;
    }
    std::cout << "Chunk grid, layers, portals, ECS projection and validation passed.\n";
}

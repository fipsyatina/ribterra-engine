#include "ribterra/seasons.hpp"
#include "ribterra/world_state.hpp"
#include "ribterra/world.hpp"
#include "weather_fixture.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n";             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

std::pair<int, int> find_resource(const ribterra::World &world, ribterra::Terrain terrain) {
    for (int radius = 1; radius < ribterra::world_size; ++radius) {
        for (int y = ribterra::world_min; y < ribterra::world_max; ++y)
            for (int x = ribterra::world_min; x < ribterra::world_max; ++x)
                if (std::abs(x - 24) + std::abs(y - 24) <= radius &&
                    world.tile(x, y).terrain == terrain && world.tile(x, y).amount > 0)
                    return {x, y};
    }
    return {-1, -1};
}

ribterra::PathBatchOutcome finish_batch(ribterra::World &world, std::uint64_t batch_id,
                                        int maximum_ticks = 4000) {
    for (int tick = 0; tick < maximum_ticks; ++tick) {
        world.tick();
        for (auto &outcome : world.take_path_results())
            if (outcome.path_batch_id == batch_id)
                return outcome;
    }
    return {batch_id, ribterra::PathCommandKind::Move, false, ribterra::PathFailure::Timeout,
            "test timeout"};
}

void deterministic_generation_and_population() {
    ribterra::World a(12345, 10), b(12345, 10), c(54321, 10);
    CHECK(a.checksum() == b.checksum());
    CHECK(a.checksum() != c.checksum());
    CHECK(a.residents().size() == 10);
    CHECK(a.buildings().size() == 1);
    CHECK(a.housing() == 10);
    CHECK(a.residents().front().id == 1);
    CHECK(a.residents()[9].id == 10);
    ribterra::World crowded(7, 1500);
    CHECK(crowded.residents().size() == 1500);
    for (int i = 0; i < 20; ++i) {
        crowded.tick();
    }
    CHECK(crowded.ticks() == 20);
}

void movement_avoids_water() {
    ribterra::World world(11, 1);
    const auto result = world.move({1}, 33, 10);
    CHECK(result.accepted);
    bool arrived = false;
    bool touched_water = false;
    for (int i = 0; i < 1200; ++i) {
        world.tick();
        const auto &r = world.residents()[0];
        const int x = static_cast<int>(r.x + 0.5F), y = static_cast<int>(r.y + 0.5F);
        arrived |= x==33 && y==10;
        if (world.tile(x, y).terrain == ribterra::Terrain::Water)
            touched_water = true;
    }
    CHECK(!touched_water);
    CHECK(world.residents()[0].job == ribterra::Job::Idle ||
          world.residents()[0].profession.wait_reason == ribterra::ProfessionWaitReason::Strolling);
    CHECK(arrived); // A free resident may leave again after completing the order.
    int water_x = -1, water_y = -1;
    for (int y = ribterra::world_min; y < ribterra::world_max && water_x < 0; ++y)
        for (int x = ribterra::world_min; x < ribterra::world_max; ++x)
            if (world.tile(x, y).terrain == ribterra::Terrain::Water) {
                water_x = x;
                water_y = y;
                break;
            }
    CHECK(water_x >= 0);
    CHECK(!world.move({1}, water_x, water_y).accepted);
}

void harvest_and_tutorial() {
    ribterra::World world(101, 3);
    CHECK(world.tutorial_step() == 0);
    CHECK(world.move({1}, 23, 23).accepted);
    CHECK(world.tutorial_step() == 0);
    for (int i = 0;
         i < 100 && (world.residents()[0].order_phase == ribterra::OrderPhase::PathPending ||
                     world.residents()[0].job != ribterra::Job::Idle);
         ++i)
        world.tick();
    CHECK(world.tutorial_step() == 1);
    auto [x, y] = find_resource(world, ribterra::Terrain::Forest);
    CHECK(x >= 0);
    const int wood = world.resources().wood;
    CHECK(world.gather({1, 2}, x, y).accepted);
    CHECK(world.tutorial_step() == 1);
    for (int i = 0; i < 1200 && world.resources().wood == wood; ++i)
        world.tick();
    CHECK(world.resources().wood > wood);
    CHECK(world.tile(x, y).amount >= 0);
    CHECK(world.tutorial_step() == 2);
    const auto house = world.build(ribterra::BuildingKind::House, 26, 24);
    CHECK(house.accepted);
    CHECK(world.tutorial_step() == 2);
    CHECK(finish_batch(world, house.path_batch_id).accepted);
    for (int i = 0; i < 600 && world.buildings().back().progress < 1.0F; ++i)
        world.tick();
    CHECK(world.buildings().back().progress >= 1.0F);
    CHECK(world.tutorial_step() == 3);
}

void harvest_food_stone_and_seasons() {
    for (const auto terrain : {ribterra::Terrain::Rock, ribterra::Terrain::Grass}) {
        ribterra::World world(303, 1);
        const auto [x, y] = find_resource(world, terrain);
        CHECK(x >= 0);
        const int before =
            terrain == ribterra::Terrain::Rock ? world.resources().stone : world.resources().food;
        CHECK(world.gather({1}, x, y).accepted);
        for (int i = 0; i < 1600; ++i) {
            world.tick();
            const int current = terrain == ribterra::Terrain::Rock ? world.resources().stone
                                                                   : world.resources().food;
            if (current > before)
                break;
        }
        CHECK((terrain == ribterra::Terrain::Rock ? world.resources().stone
                                                  : world.resources().food) > before);
    }
    ribterra::World calendar(44, 1);
    calendar.stop({1}); // Isolate calendar/starvation from autonomous food gathering.
    const int initial_food = calendar.resources().food;
    for (int i = 0; i < 1200; ++i) {
        calendar.tick();
    }
    CHECK(calendar.day() == 1);
    CHECK(calendar.resources().food < initial_food);
    auto calendar_state=calendar.export_state();
    ribterra::test::set_fixture_simulation_tick(calendar_state,
        3*ribterra::days_per_season*ribterra::ticks_per_day);
    std::string calendar_error;
    CHECK(calendar.import_state(std::move(calendar_state),calendar_error));
    CHECK(calendar.winter());
    CHECK(calendar.day()==31);

    ribterra::World farm(55, 1);
    const auto farm_order = farm.build(ribterra::BuildingKind::Farm, 26, 24);
    CHECK(farm_order.accepted);
    const int after_cost = farm.resources().food;
    CHECK(finish_batch(farm, farm_order.path_batch_id).accepted);
    for (int i = 0; i < 900; ++i) {
        farm.tick();
    }
    CHECK(farm.buildings().back().progress >= 1.0F);
    CHECK(farm.resources().food <= after_cost);
    const int before_labor = farm.resources().food;
    ribterra::ProfessionAssignment assignment;
    assignment.profession = ribterra::Profession::Farmer;
    assignment.worksite = ribterra::CellCoord{26, 24, ribterra::WorldLayer::Surface};
    CHECK(farm.assign_profession({1}, assignment).front().status ==
          ribterra::ResidentCommandStatus::Applied);
    for (int i = 0; i < 1200 && farm.resources().food <= before_labor; ++i)
        farm.tick();
    CHECK(farm.resources().food > before_labor);
}

void construction() {
    ribterra::World world(9, 10);
    const auto before = world.resources();
    CHECK(world.can_build(ribterra::BuildingKind::House, 26, 24));
    const auto order = world.build(ribterra::BuildingKind::House, 26, 24);
    CHECK(order.accepted && order.pending);
    CHECK(world.tutorial_step() == 0);
    const auto c = ribterra::World::cost(ribterra::BuildingKind::House);
    CHECK(world.resources() == before);
    CHECK(finish_batch(world, order.path_batch_id).accepted);
    CHECK(world.resources().wood == before.wood);
    CHECK(world.resources().stone == before.stone);
    CHECK(world.inventory().reserved.wood == c.wood);
    CHECK(world.inventory().reserved.stone == c.stone);
    const auto after = world.resources();
    CHECK(!world.build(ribterra::BuildingKind::Farm, 26, 24).accepted);
    CHECK(world.resources().wood == after.wood && world.resources().stone == after.stone &&
          world.resources().food == after.food);
    for (int i = 0; i < 600 && world.buildings().back().progress < 1.0F; ++i)
        world.tick();
    CHECK(world.buildings().back().progress >= 1.0F);
    CHECK(world.resources().wood == before.wood - c.wood);
    CHECK(world.resources().stone == before.stone - c.stone);
    CHECK(world.housing() == 16);
    CHECK(world.tutorial_step() == 0);
    CHECK(!world.build(ribterra::BuildingKind::House, -1, 0).accepted);
    ribterra::World poor(10, 0);
    const auto first = poor.build(ribterra::BuildingKind::House, 21, 21);
    const auto second = poor.build(ribterra::BuildingKind::House, 22, 21);
    CHECK(first.accepted && second.accepted);
    const auto depleted = poor.resources();
    CHECK(!poor.build(ribterra::BuildingKind::House, 23, 21).accepted);
    CHECK(poor.resources().wood == depleted.wood && poor.resources().stone == depleted.stone);
}

} // namespace

int main() {
    deterministic_generation_and_population();
    movement_avoids_water();
    harvest_and_tutorial();
    harvest_food_stone_and_seasons();
    construction();
    if (failures != 0) {
        std::cerr << failures << " simulation test(s) failed.\n";
        return 1;
    }
    std::cout << "All simulation tests passed.\n";
    return 0;
}

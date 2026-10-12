#include "ribterra/protocol.hpp"
#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace ribterra;
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
World fixture(int count = 1, bool farm = true) {
    World world(70421, count);
    auto s = world.export_state();
    s.resources.food = 2000;
    for (auto &item : s.item_stacks)
        if (item.item == ItemKind::Food)
            item.quantity = 2000;
    if (farm)
        s.buildings.push_back({s.next_building_id++, BuildingKind::Farm, 26, 26, 1, WorldLayer::Surface, 1});
    std::string error;
    require(world.import_state(std::move(s), error), error);
    return world;
}
ProfessionAssignment farmer(bool bound = true) {
    ProfessionAssignment p;
    p.profession = Profession::Farmer;
    if (bound)
        p.worksite = CellCoord{26, 26, WorldLayer::Surface};
    return p;
}
template <class Predicate> void until(World &world, Predicate predicate, int maximum = 2200) {
    for (int i = 0; i < maximum && !predicate(); ++i)
        world.tick();
    require(predicate(), "Farm did not reach expected state");
}
void vacant_and_occupied() {
    auto vacant = fixture(2), control = fixture(2, false);
    for (int i = 0; i < 1600; ++i) {
        vacant.tick();
        control.tick();
    }
    require(vacant.resources().food == control.resources().food, "Empty farm passively produced food");
    require(vacant.item_stacks().size() == control.item_stacks().size(), "Empty farm created physical crops");
    auto world = fixture();
    require(world.assign_profession({1}, farmer(false)).front().status == ResidentCommandStatus::Applied,
            "Farmer assignment rejected");
    for (int i = 0; i < 250; ++i) {
        world.tick();
        require(world.residents().front().job != Job::Farm, "Profession alone started farm work");
    }
    require(world.assign_profession({1}, farmer()).front().status == ResidentCommandStatus::Applied,
            "Explicit farm assignment rejected");
    until(world, [&] {
        return world.residents().front().job == Job::Farm && world.residents().front().progress > .25f;
    });
    const auto &actor = world.residents().front();
    require(actor.layer == WorldLayer::Surface && std::abs(actor.x - 26) + std::abs(actor.y - 26) == 1,
            "Farmer worked remotely");
    require(world.inventory().carried.food == 0, "Harvest was produced before work completed");
    require(actor.farming_xp > 0 || actor.skill_practice[2].experience_remainder > 0,
            "Farm work gave no farming practice");
    net::Snapshot snapshot;
    snapshot.full = true;
    snapshot.revision = 1;
    snapshot.world_id.front() = 1;
    snapshot.tick = world.ticks();
    snapshot.weather = world.weather();
    snapshot.day = world.calendar().day;
    snapshot.winter = world.winter();
    snapshot.residents.push_back(actor);
    std::string error;
    const auto packet = net::decode(net::encode({1, snapshot}), error);
    require(packet && std::get<net::Snapshot>(packet->payload).residents.front().job == Job::Farm &&
                std::get<net::Snapshot>(packet->payload).residents.front().progress == actor.progress,
            "Network lost farm progress");
    const auto save = std::filesystem::temp_directory_path() / "ribterra-farm-active.save";
    require(world.save(save, error), error);
    World restored(1, 0);
    require(restored.load(save, error), error);
    for (int i = 0; i < 800; ++i) {
        world.tick();
        restored.tick();
        require(world.residents() == restored.residents(), "Farmer state diverged after save/load");
        if (i % 20 == 0 || i == 799)
            require(world.checksum() == restored.checksum(), "Farming diverged after mid-crop save/load");
    }
    require(world.inventory().stored.food > 2000 - int(world.ticks() / 200),
            "Farmer never delivered food to storage");
    until(world, [&] { return world.inventory().carried.food > 0; });
    require(world.inventory().carried.food == farm_harvest_food, "Wrong physical harvest quantity");
    require(world.stop({1}).front().status == ResidentCommandStatus::Deferred,
            "Stop discarded harvest delivery");
    until(world, [&] { return world.inventory().carried.food == 0; });
    const auto stopped = world.resources().food;
    const auto tick = world.ticks();
    for (int i = 0; i < 1200; ++i)
        world.tick();
    const auto meals = int(world.ticks() / 200 - tick / 200);
    require(world.resources().food == stopped - meals, "Stopped farmer kept producing food");
    require(world.residents().front().job == Job::Idle, "Stopped farmer returned to work");
    std::filesystem::remove(save);
}
void limits_and_invalid_sites() {
    auto world = fixture(3);
    world.assign_profession({1, 2, 3}, farmer());
    for (int i = 0; i < 700; ++i) {
        world.tick();
        int occupied = 0;
        for (const auto &r : world.residents())
            occupied += r.job == Job::Farm || r.job == Job::Haul || r.order_phase == OrderPhase::PathPending;
        for (const auto &work : world.work_intents())
            occupied += work.job == Job::Farm && work.phase == WorkIntentPhase::Waiting;
        require(occupied <= farm_worker_limit, "Farm exceeded its worker limit");
    }
    auto absent = fixture(1, false);
    absent.assign_profession({1}, farmer());
    for (int i = 0; i < 600; ++i)
        absent.tick();
    require(absent.residents().front().job == Job::Idle &&
                absent.residents().front().profession.wait_reason ==
                    ProfessionWaitReason::WorksiteUnavailable,
            "Farmer worked without a completed farm");
    auto blocked = fixture();
    auto state = blocked.export_state();
    for (auto &chunk : state.chunks)
        if (chunk.key == chunk_key({26, 26, WorldLayer::Surface}))
            for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}})
                chunk.tiles[local_index(26 + dx, 26 + dy)] = {Terrain::Water, 0, true};
    std::string error;
    require(blocked.import_state(std::move(state), error), error);
    blocked.assign_profession({1}, farmer());
    for (int i = 0; i < 800; ++i)
        blocked.tick();
    require(blocked.resources().food == 1996, "Inaccessible farm produced food");
    require(blocked.residents().front().farming_xp == 0, "Unreachable farm granted XP");
    auto stopped = fixture();
    stopped.assign_profession({1}, farmer());
    until(stopped, [&] {
        return stopped.residents().front().job == Job::Farm && stopped.residents().front().progress > .3f;
    });
    stopped.stop({1});
    const auto food = stopped.resources().food;
    const auto tick = stopped.ticks();
    for (int i = 0; i < 800; ++i)
        stopped.tick();
    require(stopped.resources().food == food - int(stopped.ticks() / 200 - tick / 200),
            "Interrupted crop was harvested without work");
}
void meals_after_tree_exhaustion() {
    World world(70421, 1);
    auto state = world.export_state();
    for (auto &chunk : state.chunks)
        if (chunk.key == chunk_key({26, 26, WorldLayer::Surface}))
            chunk.tiles[local_index(26, 26)] = {Terrain::Forest, 1, true};
    std::string error;
    require(world.import_state(std::move(state), error), error);
    require(world.gather({1}, 26, 26).accepted, "Tree order rejected");
    until(world, [&] { return world.tile(26, 26).amount == 0; });
    until(world, [&] { return world.residents().front().job == Job::Idle && world.inventory().carried.wood == 0; });
    world.stop({1}); // Isolate meal consumption from the new free foraging behavior.
    const auto food = world.resources().food;
    const auto stored = world.inventory().stored.food;
    for (int i = 0; i < 600; ++i) world.tick();
    require(world.resources().food == food - 3 && world.inventory().stored.food == stored - 3,
            "Finished woodcutting prevents subsequent meals");
    require(world.residents().front().health == 100 && world.residents().front().hunger == 0,
            "Finished lumberjack became hungry despite stored food");
}

void working_meals() {
    auto check_meal = [](World &world, Job job) {
        auto state = world.export_state();
        test::set_fixture_simulation_tick(state, (state.ticks / 200 + 1) * 200 - 1);
        state.residents.front().hunger = 4;
        std::string error;
        require(world.import_state(std::move(state), error), error);
        const auto food = world.resources().food;
        require(world.residents().front().job == job, "Meal fixture is not working");
        world.tick();
        require(world.resources().food == food - 1, "Working resident did not consume a meal");
        require(world.residents().front().hunger == 3.75f, "Work prevented hunger relief");
        require(world.residents().front().health == 100, "Fed worker lost health");
        require(world.residents().front().job == job, "Meal interrupted or discarded work");
    };
    auto farm = fixture();
    farm.assign_profession({1}, farmer());
    until(farm, [&] { return farm.residents().front().job == Job::Farm && farm.residents().front().progress > .25f; });
    check_meal(farm, Job::Farm);
    until(farm, [&] { return farm.inventory().carried.food > 0; });
    check_meal(farm, Job::Haul);
    World builder(70421, 1);
    require(builder.build(BuildingKind::House, 26, 26).accepted, "Meal construction fixture rejected");
    until(builder, [&] { return builder.residents().front().job == Job::Build && builder.residents().front().progress > .1f; });
    check_meal(builder, Job::Build);
    for (const auto terrain : {Terrain::Forest, Terrain::Rock}) {
        World gatherer(70421, 1);
        auto state = gatherer.export_state();
        for (auto &chunk : state.chunks)
            if (chunk.key == chunk_key({26, 26, WorldLayer::Surface}))
                chunk.tiles[local_index(26, 26)] = {terrain, 100, true};
        std::string error;
        require(gatherer.import_state(std::move(state), error), error);
        require(gatherer.gather({1}, 26, 26).accepted, "Meal gathering fixture rejected");
        const auto job = terrain == Terrain::Forest ? Job::Woodcut : Job::Mine;
        until(gatherer, [&] { return gatherer.residents().front().job == job && gatherer.residents().front().progress > .1f; });
        check_meal(gatherer, job);
    }
}
int main() {
    try {
        meals_after_tree_exhaustion();
        working_meals();
        vacant_and_occupied();
        limits_and_invalid_sites();
        std::cout << "Farm staffing, on-site labor, physical harvest, delivery, stop, capacity, save/load "
                     "and network passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

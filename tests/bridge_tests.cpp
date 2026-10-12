#include "bridge_fixture.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace ribterra;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        auto world = bridge_fixture();
        check(!world.can_build(BuildingKind::Bridge, 28, 25), "Bridge accepted in isolated deep water");
        check(!world.can_build(BuildingKind::House, 27, 25), "House accepted on water");
        const auto file = std::filesystem::temp_directory_path() / "ribterra-bridge-test.save";
        std::string error;
        for (int x = 27; x <= 29; ++x) {
            check(world.build(BuildingKind::Bridge, x, 25).accepted, "Connected section rejected");
            check(!world.walkable(x, 25), "Blueprint is walkable before construction");
            check(!world.can_build(BuildingKind::Bridge, x, 25), "Duplicate blueprint accepted");
            bool worked = false;
            for (int t = 0; t < 3000 && !world.walkable(x, 25); ++t) {
                world.tick();
                const auto &site = world.construction_orders().back();
                if (site.phase == ConstructionPhase::Work) {
                    check(site.delivered == site.required, "Bridge work precedes delivery");
                    if (!worked) {
                        check(world.save(file, error), error.c_str());
                        World copy(1, 0);
                        check(copy.load(file, error), error.c_str());
                        check(copy.checksum() == world.checksum(), "Unfinished bridge save changed");
                    }
                    worked = true;
                }
            }
            check(worked && world.walkable(x, 25), "Bridge did not complete");
            check(world.tile(x, 25).terrain == Terrain::Water, "Bridge replaced lake terrain");
        }
        check(world.move({1}, 31, 25).accepted, "Crossing command rejected");
        bool crossed = false;
        for (int t = 0; t < 3000 && !crossed; ++t) {
            world.tick();
            const auto &r = world.residents().front();
            crossed = std::abs(r.x - 31) < .01f && std::abs(r.y - 25) < .01f;
        }
        check(crossed, "Resident failed to cross the river");
        check(world.save(file, error), error.c_str());
        World loaded(1, 0);
        check(loaded.load(file, error), error.c_str());
        check(loaded.walkable(28, 25) && loaded.checksum() == world.checksum(),
              "Completed bridge save failed");
        auto cancelled = bridge_fixture();
        check(cancelled.build(BuildingKind::Bridge, 27, 25).accepted, "Cancel fixture rejected");
        for (int t = 0; t < 3000 && cancelled.construction_orders().back().phase != ConstructionPhase::Work;
             ++t)
            cancelled.tick();
        check(cancelled.construction_orders().back().phase == ConstructionPhase::Work,
              "No materials delivered for cancellation");
        check(cancelled.cancel_construction(cancelled.construction_orders().back().building_id).accepted,
              "Bridge cancellation failed");
        check(!cancelled.walkable(27, 25), "Cancelled bridge is walkable");
        bool refunded = false;
        for (const auto &stack : cancelled.item_stacks())
            if (stack.location.kind == ItemLocationKind::Ground) {
                check(cancelled.walkable(stack.location.cell.x, stack.location.cell.y),
                      "Bridge refund stranded in water");
                refunded = true;
            }
        check(refunded, "Bridge materials were not refunded");
        std::filesystem::remove(file);
        std::cout
            << "Bridges: delivery, construction, three-section crossing, save/load, cancellation passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

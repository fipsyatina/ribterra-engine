#include "ribterra/building_catalog.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/world_state.hpp"
#include "reachable_surface_fixture.hpp"
#include "weather_fixture.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace ribterra;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void round_trip(const World &world, const std::filesystem::path &file) {
    std::string error;
    check(world.save(file, error), error);
    World loaded(1, 0);
    check(loaded.load(file, error), error);
    check(loaded.checksum() == world.checksum(), "Building save checksum mismatch");
}
int main() {
    try {
        const auto file = std::filesystem::temp_directory_path() / "ribterra-buildings-014.save";
        for (const auto &definition : building_catalog) {
            if (!alpha::buildable(definition.kind))
                continue;
            World world(70421, 4);
            // Enough physical stock to exercise every price independently of early-game gathering.
            auto supplied = world.export_state();
            supplied.resources = {200, 200, 200};
            for (auto &stack : supplied.item_stacks)
                stack.quantity = 200;
            std::string supply_error;
            check(world.import_state(std::move(supplied), supply_error), supply_error);
            check(world.portals().empty(), "New world has a free mine");
            check(!world.tile(28, 24, WorldLayer::Mine).explored,
                  "Underground exposed before mine construction");
            int build_x = 26;
            int build_y = 26;
            if (definition.kind == BuildingKind::Fishing) {
                check(!world.can_build(definition.kind, 26, 26), "Fishing allowed away from water");
                check(!test::reachable_fishing_site(world),
                      "Seed 70421 unexpectedly has a reachable shore in its initial reveal");
                const auto before_reveal = world.export_state();
                std::string reveal_error;
                check(test::reveal_surface_radius(world, 32, reveal_error), reveal_error);
                const auto after_reveal = world.export_state();
                check(before_reveal.chunks.size() == after_reveal.chunks.size(),
                      "Fishing reveal changed the generated chunk set");
                for (std::size_t chunk_index = 0; chunk_index < before_reveal.chunks.size(); ++chunk_index) {
                    const auto &before_chunk = before_reveal.chunks[chunk_index];
                    const auto &after_chunk = after_reveal.chunks[chunk_index];
                    check(before_chunk.key == after_chunk.key &&
                              before_chunk.tiles.size() == after_chunk.tiles.size(),
                          "Fishing reveal changed generated chunk identity");
                    for (std::size_t tile_index = 0; tile_index < before_chunk.tiles.size(); ++tile_index) {
                        const auto &before = before_chunk.tiles[tile_index];
                        const auto &after = after_chunk.tiles[tile_index];
                        check(before.terrain == after.terrain && before.amount == after.amount &&
                                  before.biome == after.biome && before.elevation == after.elevation &&
                                  before.trail_wear == after.trail_wear,
                              "Fishing reveal changed generated terrain content");
                    }
                }
                const auto shore = test::reachable_fishing_site(world);
                check(bool(shore), "No reachable natural fishing shore");
                build_x = shore->x;
                build_y = shore->y;
            }
            if (definition.kind == BuildingKind::Bridge) {
                check(!world.can_build(definition.kind,26,26),"Bridge accepted on dry land");
                auto state=world.export_state();
                for(auto &chunk:state.chunks) if(chunk.key==chunk_key({26,26,WorldLayer::Surface}))
                    chunk.tiles[local_index(26,26)]={Terrain::Water,0,true,Biome::Lake};
                std::string error;check(world.import_state(std::move(state),error),error);
                check(!world.walkable(26,26),"Unbuilt water is walkable");
            }
            check(world.can_build(definition.kind, build_x, build_y),
                  std::string("Cannot place ") + definition.slug);
            check(world.build(definition.kind, build_x, build_y).accepted, "Build rejected");
            check(world.portals().empty(), "Blueprint created a passage");
            round_trip(world, file);
            bool work_seen = false;
            for (int tick = 0; tick < 3000; ++tick) {
                world.tick();
                const auto &order = world.construction_orders().front();
                if (order.phase == ConstructionPhase::Complete)
                    break;
                check(world.portals().empty(), "Unfinished building opened a passage");
                if (!work_seen && order.phase == ConstructionPhase::Work) {
                    check(order.required == order.delivered, "Work began before material delivery");
                    round_trip(world, file);
                    work_seen = true;
                }
            }
            check(world.construction_orders().front().phase == ConstructionPhase::Complete,
                  std::string("Construction did not finish: ") + definition.slug);
            check(work_seen, "Construction work phase missing");
            check(world.buildings().back().kind == definition.kind, "Completed building type changed");
            round_trip(world, file);
            if(definition.kind==BuildingKind::Bridge) check(world.walkable(26,26),"Completed bridge is blocked");
            // Both new commands and completed building snapshots use the full enum on the wire.
            net::Snapshot snapshot;
            snapshot.full = true;
            snapshot.revision = 1;
            snapshot.world_id.front() = 1;
            snapshot.tick = world.ticks();
            snapshot.weather = world.weather();
            snapshot.day = world.calendar().day;
            snapshot.winter = world.winter();
            snapshot.buildings.push_back(world.buildings().back());
            std::string wire_error;
            const auto decoded = net::decode(net::encode({1, snapshot}), wire_error);
            check(decoded.has_value(), wire_error);
            check(std::get<net::Snapshot>(decoded->payload).buildings.front().kind == definition.kind,
                  "Wire building type lost");
            GameCommand command;
            command.request_id = 1;
            command.author = 1;
            command.type = CommandType::Build;
            command.building = definition.kind;
            command.x = build_x;
            command.y = build_y;
            const auto command_packet = net::decode(net::encode({2, command}), wire_error);
            check(command_packet &&
                      std::get<GameCommand>(command_packet->payload).building == definition.kind,
                  "Wire construction command lost its building kind");
            if (definition.kind == BuildingKind::Fishing) {
                // Advance the real simulation to the next production boundary;
                // the natural-shore build can finish after historical tick 399.
                const auto ticks_to_boundary = 400 - world.ticks() % 400;
                for (std::uint64_t tick = 0; tick < ticks_to_boundary; ++tick)
                    world.tick();
                check(std::none_of(world.item_stacks().begin(), world.item_stacks().end(),
                                  [&](const auto &stack) {
                                      return stack.item == ItemKind::Food && stack.quantity == 6 &&
                                             stack.location.kind == ItemLocationKind::Ground &&
                                             stack.location.cell ==
                                                 CellCoord{build_x, build_y, WorldLayer::Surface};
                                  }),
                      "Fishing station produced food without a worker");
            }
            if (definition.kind == BuildingKind::Mine) {
                check(world.portals().size() == 1, "Completed mine lacks passage");
                check(world.walkable(26, 26), "Completed mine blocks entry");
                check(world.move({1}, 26, 26, WorldLayer::Mine).accepted, "Mine entrance move rejected");
                for (int i = 0; i < 1000 && world.residents().front().layer != WorldLayer::Mine; ++i)
                    world.tick();
                check(world.residents().front().layer == WorldLayer::Mine, "Resident did not descend");
                round_trip(world, file);
                check(world.move({1}, 25, 26).accepted, "Return from mine rejected");
                for (int i = 0; i < 1000 && world.residents().front().layer != WorldLayer::Surface; ++i)
                    world.tick();
                check(world.residents().front().layer == WorldLayer::Surface, "Resident did not return");
                auto invalid = world.export_state();
                invalid.portals.clear();
                std::string error;
                check(!world.import_state(std::move(invalid), error),
                      "Completed mine without passage accepted");
            }
            std::cout << definition.slug << ": delivered, built, saved, replicated\n";
        }
        World cancelled(70421, 4);
        check(cancelled.build(BuildingKind::Mine, 26, 26).accepted, "Cancel fixture build failed");
        check(cancelled.cancel_construction(2).accepted, "Mine cancellation failed");
        for (int i = 0; i < 500; ++i)
            cancelled.tick();
        check(cancelled.portals().empty(), "Cancelled mine opened passage");
        auto rocky = cancelled.export_state();
        for (auto &chunk : rocky.chunks)
            if (chunk.key == chunk_key({26, 26, WorldLayer::Surface}))
                chunk.tiles[local_index(26, 26)] = {Terrain::Rock, 30, true, Biome::Mountain, 8};
        std::string rock_error;
        check(cancelled.import_state(std::move(rocky), rock_error), rock_error);
        check(cancelled.can_build(BuildingKind::Mine, 26, 26), "Mine cannot be placed on a rock deposit");
        check(!cancelled.can_build(BuildingKind::House, 26, 26), "Ordinary building allowed on rock");
        check(!cancelled.can_build(static_cast<BuildingKind>(building_kind_count), 26, 26), "Invalid building accepted");
        std::filesystem::remove(file);
        std::cout << "Construction catalog and mine lifecycle passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

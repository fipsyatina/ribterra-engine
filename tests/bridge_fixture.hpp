#pragma once
#include "ribterra/world_state.hpp"
#include <stdexcept>
inline ribterra::World bridge_fixture() {
    using namespace ribterra;
    World world(70421, 4);
    auto state = world.export_state();
    state.resources = {250, 250, 250};
    for (auto &stack : state.item_stacks)
        stack.quantity = 250;
    for (auto &chunk : state.chunks)
        if (chunk.key.layer == WorldLayer::Surface)
            for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                if (x >= 27 && x <= 29)
                    chunk.tiles[i] = {Terrain::Water, 0, chunk.tiles[i].explored, Biome::Lake};
                if (x >= 15 && x <= 40 && y >= 13 && y <= 37) {
                    chunk.tiles[i] = x >= 27 && x <= 29 ? Tile{Terrain::Water, 0, true, Biome::Lake}
                                                        : Tile{Terrain::Grass, 0, true, Biome::Meadow, 8};
                }
            }
    std::string error;
    if (!world.import_state(std::move(state), error))
        throw std::runtime_error(error);
    return world;
}

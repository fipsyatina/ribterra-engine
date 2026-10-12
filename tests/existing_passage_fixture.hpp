#pragma once
#include "ribterra/world_state.hpp"
#include <stdexcept>

// Explicit legacy passage fixture for tests of routing and pre-0.14 save compatibility.
// New settlements must instead complete BuildingKind::Mine (see building_tests.cpp).
inline void existing_passage(ribterra::World &world) {
    using namespace ribterra;
    auto state = world.export_state();
    state.terrain_generator = 5;
    state.portals = {{{28, 24, WorldLayer::Surface}, {28, 24, WorldLayer::Mine}}};
    for (auto &chunk : state.chunks)
        if (chunk.key == chunk_key({28, 24, WorldLayer::Mine}))
            chunk.tiles[local_index(28, 24)] = {Terrain::MineFloor, 0, true};
    std::string error;
    if (!world.import_state(std::move(state), error))
        throw std::runtime_error(error);
}

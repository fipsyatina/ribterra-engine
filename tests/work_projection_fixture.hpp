#pragma once

#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"

namespace ribterra::test {
struct WorkProjectionSave {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("ribterra-work-projection-" + secure_token() + ".save");
    ~WorkProjectionSave() {
        std::error_code error;
        std::filesystem::remove(path, error);
        auto backup = path;
        backup += ".bak";
        std::filesystem::remove(backup, error);
    }
};
// A saved retry, normal exhaustion, terminal failure, and a foreign terminal.
// This fixture contains no internal path; tests can advance the retry through
// real path admission or replace the exhausted order with a player command.
inline WorldState work_projection_state() {
    World world(70421, 4);
    auto state = world.export_state();
    for (auto &chunk : state.chunks)
        if (chunk.key == chunk_key({21, 24, WorldLayer::Surface})) {
            // Only resource/visibility state belongs to this fixture. A short
            // Tile aggregate silently resets height to zero and creates a
            // non-traversable pit inside the Gen8 starting shelf.
            const auto set_resource = [&](int x, int y, Terrain terrain, int amount) {
                auto &tile = chunk.tiles[local_index(x, y)];
                tile.terrain = terrain;
                tile.amount = amount;
                tile.explored = true;
            };
            set_resource(21, 24, Terrain::Forest, 20);
            set_resource(20, 24, Terrain::Grass, 0);
            set_resource(22, 24, Terrain::Grass, 0);
            set_resource(22, 25, Terrain::Grass, 0);
        }
    for (auto &resident : state.residents) {
        resident.x = 22;
        resident.y = 24;
        resident.target_x = 22;
        resident.target_y = 24;
        resident.job = Job::Idle;
        resident.progress = 0;
        resident.order_phase = OrderPhase::Idle;
        resident.path_failure = PathFailure::None;
        resident.pending_path_batch = 0;
        if (resident.id == 4)
            resident.nation = 2;
    }
    for (auto &route : state.routes) {
        route.points.clear();
        route.cursor = 0;
        route.work.reset();
    }
    for (int resident = 1; resident <= 4; ++resident) {
        WorkIntentState intent;
        intent.intent_id = static_cast<std::uint64_t>(resident);
        intent.resident_id = resident;
        intent.owner_nation = resident == 4 ? 2 : 1;
        intent.target = {21, 24, WorldLayer::Surface};
        intent.job = Job::Woodcut;
        intent.expected_terrain = Terrain::Forest;
        intent.admission_deadline_tick = work_admission_deadline_ticks;
        intent.attempts = 1;
        intent.last_attempt_topology_revision = state.topology_revision;
        if (resident == 1) {
            intent.next_attempt_tick = 2;
        } else if (resident == 3) {
            intent.phase = WorkIntentPhase::Failed;
            intent.failure = PathFailure::Unreachable;
        } else {
            intent.phase = WorkIntentPhase::Complete;
            intent.completion = WorkCompletionReason::Exhausted;
            intent.target = {20, 24, WorldLayer::Surface};
        }
        state.work_intents.push_back(intent);
    }
    state.next_work_intent_id = 5;
    return state;
}
} // namespace ribterra::test

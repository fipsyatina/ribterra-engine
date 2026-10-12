// Frozen 0.8 writer. Link only against the copied headers and libraries beside this file.
#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace ribterra;
using Json = nlohmann::json;
const auto output = std::filesystem::path(".deps/v6-capture");
void require(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
World load(const char *name) {
    World world(1, 1);
    std::string error;
    require(world.load(output / "fixtures" / name, error), "Cannot load genuine fixture: " + error);
    return world;
}
void capture(World &world, const char *name) {
    const auto path = output / "fixtures" / (std::string("world_save_v6_") + name + ".ribsave");
    require(!std::filesystem::exists(path), "Refusing to overwrite a frozen fixture");
    const auto state = world.export_state();
    const auto checksum = world.checksum();
    std::string error;
    require(world.save(path, error), "Cannot save: " + error);
    World restored(1, 1);
    require(restored.load(path, error), "Cannot reload: " + error);
    require(restored.checksum() == checksum, "Round trip changed the world");
    Json report{{"name", name}, {"ticks", state.ticks}, {"checksum", checksum},
        {"seed", state.seed}, {"engine", engine_version}, {"rules", rules_version},
        {"content_hash", default_content_hash()}, {"path_batches", Json::array()},
        {"work_intents", Json::array()}, {"resume_ticks", 64}};
    for (const auto &batch : state.path_batches)
        report["path_batches"].push_back({{"id", batch.path_batch_id},
            {"command", static_cast<int>(batch.command)}, {"phase", static_cast<int>(batch.phase)},
            {"work_units", batch.work_units_done}, {"frontier_size", batch.frontier.size()},
            {"frontier_cursor", batch.frontier_cursor}, {"nodes", batch.nodes.size()},
            {"drafts", batch.drafts.size()}});
    for (const auto &intent : state.work_intents)
        report["work_intents"].push_back({{"id", intent.intent_id},
            {"kind", static_cast<int>(intent.kind)}, {"phase", static_cast<int>(intent.phase)},
            {"completion", static_cast<int>(intent.completion)},
            {"failure", static_cast<int>(intent.failure)}, {"attempts", intent.attempts},
            {"path_batch", intent.path_batch_id}});
    for (int i = 0; i < 64; ++i) {
        world.tick();
        restored.tick();
        require(world.checksum() == restored.checksum(), "Resume tick diverged");
    }
    std::ofstream(output / (std::string(name) + ".capture.json")) << report.dump(2) << '\n';
    std::cout << report.dump() << '\n';
}
int main() {
    try {
        require(engine_version == "0.8.0" && rules_version == 5 &&
            default_content_hash() == "3a8700a4682e31d8c14606093966cc57a322afded4c82480dddf306d2a85fbbe",
            "This capture must use the frozen 0.8 writer");
        auto failed = load("world_save_v6_waiting.ribsave");
        bool found_failure = false;
        for (int i = 0; i < 1400 && !found_failure; ++i) {
            failed.tick(); // Retain the real external outcomes; the queue stays full.
            found_failure = std::any_of(failed.work_intents().begin(), failed.work_intents().end(),
                [](const auto &intent) { return intent.phase == WorkIntentPhase::Failed; });
        }
        require(found_failure, "The admission deadline did not produce a failure");
        require(failed.inventory().stored.wood == 83 && failed.inventory().carried.wood == 0,
            "Deadline changed a previously successful delivery");
        capture(failed, "failed");

        auto searching = load("world_save_v6_builder_planning.ribsave");
        std::vector<int> ids;
        for (const auto &resident : searching.residents()) ids.push_back(resident.id);
        bool moved = false;
        for (int y = -50; y <= -40 && !moved; ++y)
            for (int x = -50; x <= -40 && !moved; ++x)
                if (searching.walkable(x, y))
                    moved = searching.move(ids, x, y).accepted;
        require(moved, "No valid distant manual destination");
        bool found_search = false;
        for (int i = 0; i < 4000 && !found_search; ++i) {
            searching.tick();
            searching.take_path_results();
            const auto state = searching.export_state();
            found_search = std::any_of(state.path_batches.begin(), state.path_batches.end(),
                [](const auto &batch) { return batch.command == PathCommandKind::Work &&
                    batch.phase == PathPlannerPhase::Search && batch.frontier_cursor > 0 &&
                    batch.frontier_cursor < batch.frontier.size() && batch.work_units_done > 0; });
        }
        require(found_search, "No partially searched Work route from normal commands");
        require(searching.inventory().total.wood == 50 && searching.inventory().total.stone == 25,
            "Reassignment charged the paid construction twice");
        capture(searching, "work_search");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

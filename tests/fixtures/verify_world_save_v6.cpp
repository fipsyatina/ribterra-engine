#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

using namespace ribterra;
std::vector<char> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}
int main() {
    try {
        if (engine_version != "0.8.0" || rules_version != 5)
            throw std::runtime_error("Frozen 0.8 writer required");
        const auto root = std::filesystem::path(".deps/v6-capture");
        std::filesystem::create_directories(root / "verification");
        nlohmann::json report = nlohmann::json::array();
        for (const auto *name : {"carry", "waiting", "builder_planning", "builder_travelling",
                "builder_working", "exhausted", "failed", "work_search"}) {
            const auto filename = std::string("world_save_v6_") + name + ".ribsave";
            const auto source = root / "fixtures" / filename;
            const auto resaved = root / "verification" / filename;
            World world(1, 1), restored(1, 1);
            std::string error;
            if (!world.load(source, error) || !world.save(resaved, error) ||
                !restored.load(resaved, error)) throw std::runtime_error(error);
            if (read(source) != read(resaved)) throw std::runtime_error("Re-encoding changed bytes");
            const auto checksum = world.checksum();
            if (restored.checksum() != checksum) throw std::runtime_error("Round trip changed state");
            const auto tick = world.ticks();
            for (int i = 0; i < 64; ++i) {
                world.tick();
                restored.tick();
                if (world.checksum() != restored.checksum())
                    throw std::runtime_error("Resume tick diverged");
            }
            report.push_back({{"name", name}, {"initial_tick", tick}, {"checksum", checksum},
                {"resume_ticks", 64}, {"final_checksum", world.checksum()},
                {"byte_identical_reencoding", true}});
        }
        std::ofstream(root / "verification.json") << report.dump(2) << '\n';
        std::cout << "Eight genuine 0.8 fixtures: byte-identical re-encoding and 64 resume ticks each.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

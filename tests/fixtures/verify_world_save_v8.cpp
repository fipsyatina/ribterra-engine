// Compile ONLY with the unchanged frozen 5476c90 headers and libraries.
#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using namespace ribterra;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
constexpr auto commit = "5476c90676dc3152ef044e80fc0d12884d994f99";
constexpr auto content = "5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606";
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<std::uint8_t> bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read " + path.generic_string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void current_header(const std::vector<std::uint8_t> &data) {
    require(data.size() >= 96, "Truncated format 8 container");
    const auto word = [&](std::size_t offset) {
        std::uint32_t value{};
        for (unsigned i = 0; i < 4; ++i)
            value |= std::uint32_t(data[offset + i]) << (8U * i);
        return value;
    };
    require(word(12) == 8 && word(16) == 7 && word(20) == 2, "Unexpected fixture format/rules/generator");
    constexpr char hex[] = "0123456789abcdef";
    std::string hash;
    for (std::size_t i = 64; i < 96; ++i) {
        hash += hex[data[i] >> 4U];
        hash += hex[data[i] & 15U];
    }
    require(hash == content, "Unexpected fixture gameplay hash");
}
Json writer() {
    return {{"source_commit", commit},
            {"engine", engine_version},
            {"save_format", 8},
            {"rules", rules_version},
            {"protocol", protocol_version},
            {"generator", generator_version},
            {"content_hash", default_content_hash()}};
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc <= 2, "Usage: verify.exe [capture-output-root] (run from repository root)");
        require(engine_version == "0.10.0" && rules_version == 7 && protocol_version == 7 &&
                    generator_version == 2 && default_content_hash() == content,
                "Compile and link against the unchanged frozen 5476c90 writer only");
        const auto root = argc == 2 ? fs::path(argv[1]) : fs::path(".deps/v8-capture-5476c90");
        require(!fs::exists(root / "verification.json"), "Refusing to overwrite verification report");
        require(!fs::exists(root / "verification") || fs::is_empty(root / "verification"),
                "Refusing to overwrite existing verification copies");
        std::ifstream input(root / "capture-manifest.json");
        require(bool(input), "Capture manifest is missing");
        const auto manifest = Json::parse(input);
        require(manifest.at("schema") == 1 && manifest.at("writer") == writer(),
                "Manifest writer is not frozen 5476c90");
        require(manifest.at("fixtures").is_array() && !manifest.at("fixtures").empty(),
                "No captured fixtures to verify");
        fs::create_directories(root / "verification");
        Json report{{"schema", 1}, {"writer", writer()}, {"fixtures", Json::array()}};
        std::set<std::string> names;
        for (const auto &fixture : manifest.at("fixtures")) {
            const auto name = fixture.at("name").get<std::string>();
            require(names.insert(name).second && !name.empty() &&
                        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos,
                    "Invalid or duplicate fixture name");
            const auto filename = "world_save_v8_" + name + ".ribsave";
            const auto relative = fs::path("fixtures") / filename;
            require(fixture.at("file") == relative.generic_string(), "Unexpected fixture path");
            const auto source = root / relative;
            const auto resaved = root / "verification" / filename;
            const auto original = bytes(source);
            current_header(original);
            require(!fs::exists(resaved), "Refusing to overwrite re-encoding");
            World world(1, 1), restored(1, 1);
            std::string error;
            require(world.load(source, error), name + " source load failed: " + error);
            require(world.ticks() == fixture.at("initial_tick").get<std::uint64_t>() &&
                        world.checksum() == fixture.at("checksum").get<std::uint64_t>(),
                    name + " differs from capture-time checksum/tick");
            require(world.save(resaved, error), name + " re-encoding failed: " + error);
            require(bytes(resaved) == original, name + " did not re-encode byte-identically");
            require(restored.load(resaved, error), name + " re-encoded load failed: " + error);
            const auto initial_tick = world.ticks();
            const auto initial_checksum = world.checksum();
            const auto initial_outcomes = world.export_state().path_results;
            Json sequence = Json::array();
            for (int tick = 0; tick <= 64; ++tick) {
                require(world.checksum() == restored.checksum(),
                        name + " checksum diverged at resume tick " + std::to_string(tick));
                const auto state = world.export_state();
                const auto restored_state = restored.export_state();
                require(state.path_results == restored_state.path_results,
                        name + " undrained external outcomes diverged at tick " + std::to_string(tick));
                require(state.path_results.size() >= initial_outcomes.size() &&
                            std::equal(initial_outcomes.begin(), initial_outcomes.end(),
                                       state.path_results.begin()),
                        name + " lost or rewrote an existing undrained outcome");
                sequence.push_back(world.checksum());
                if (tick < 64) {
                    world.tick();
                    restored.tick();
                }
            }
            require(bytes(source) == original, "Verification modified its source fixture");
            report["fixtures"].push_back({{"name", name},
                                          {"file", relative.generic_string()},
                                          {"sha256", sha256(original)},
                                          {"bytes", original.size()},
                                          {"initial_tick", initial_tick},
                                          {"checksum", initial_checksum},
                                          {"checksum_sequence", sequence},
                                          {"resume_ticks", 64},
                                          {"final_checksum", world.checksum()},
                                          {"byte_identical_reencoding", true},
                                          {"undrained_outcomes_preserved", true},
                                          {"initial_path_outcomes", initial_outcomes.size()},
                                          {"final_path_outcomes", world.export_state().path_results.size()}});
            std::cout << "Verified " << name << ": exact bytes and 64 subsequent checksums/outcomes"
                      << std::endl;
        }
        std::ofstream output(root / "verification.json");
        require(bool(output), "Cannot write verification report");
        output << report.dump(2) << '\n';
        require(bool(output), "Could not finish verification report");
        std::cout << report["fixtures"].size() << " genuine format 8 fixtures verified.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

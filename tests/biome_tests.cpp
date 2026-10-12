#include "ribterra/biomes.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/terrain_surface.hpp"
#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace ribterra;
    try {
        auto check = [](bool value, const char *message) {
            if (!value)
                throw std::runtime_error(message);
        };
        check(world_size * world_size >= 10 * 256 * 256, "Map area did not increase tenfold");
        // Shared vertices, planar water, unknown-data isolation and moving feet.
        auto plane = [](int x, int y) {
            return Tile{Terrain::Grass, 0, true, Biome::Meadow, static_cast<std::int16_t>(x * 3 + y * 5)};
        };
        for (int x : {-33, -32, -1, 0, 31, 32}) {
            const auto a = terrain_corners(x, 4, plane), b = terrain_corners(x + 1, 4, plane);
            check(a[1] == b[0] && a[2] == b[3], "Terrain has a chunk seam");
        }
        check(std::abs(terrain_height(3.25f, 4.1f, plane) - (3.25f * 3 + 4.1f * 5)) < .001f,
              "Interpolated feet do not follow the mesh");
        auto shore = [](int x, int) {
            return Tile{x <= 0 ? Terrain::Water : Terrain::Grass, 0, true, Biome::Meadow,
                        static_cast<std::int16_t>(x <= 0 ? 0 : 40)};
        };
        for (auto h : terrain_corners(0, 0, shore))
            check(h == 0, "Lake surface is tilted");
        check(terrain_corner(
                  0, 0, [](int, int) { return Tile{Terrain::Rock, 0, false, Biome::Mountain, 400}; }) == 0,
              "Hidden height leaked into terrain");
        int minimum_height = 512, maximum_height = -32;
        std::array<int, 9> counts{};
        int coherent = 0, total = 0;
        for (auto seed : {70421U, 123U, 9917U}) {
            counts.fill(0);
            int mountain_samples=0, mountain_stone=0, mountain_gaps=0;
            for (int y = world_min; y < world_max; y += 3)
                for (int x = world_min; x < world_max; x += 3) {
                    const auto t = surface_tile(x, y, seed);
                    ++counts[static_cast<unsigned>(t.biome)];
                    minimum_height = std::min(minimum_height, int(t.elevation));
                    maximum_height = std::max(maximum_height, int(t.elevation));
                    check(t.elevation >= -32 && t.elevation <= 512, "Height outside mesh bounds");
                    if (t.terrain == Terrain::Water)
                        check(t.elevation == 0, "Lake above sea level");
                    coherent += t.biome == surface_tile(x + 1, y, seed).biome;
                    ++total;
                    if (t.biome == Biome::Lake || t.biome == Biome::SnowLake)
                        check(t.terrain == Terrain::Water && t.amount == 0, "Lake is not water");
                    if (t.biome == Biome::Mountain || t.biome == Biome::SnowMountain) {
                        ++mountain_samples;
                        if (t.terrain == Terrain::Rock) {
                            check(t.amount > 0, "Mountain stone has no harvest yield");
                            ++mountain_stone;
                        } else {
                            check(t.terrain == Terrain::Grass && t.amount == 0,
                                  "Mountain gap is not unoccupied ground");
                            ++mountain_gaps;
                        }
                    }
                }
            // Gen7 has harvestable outcrops with natural gaps, not a solid
            // resource carpet. Assert both availability and bounded density.
            check(mountain_samples>100 && mountain_gaps>0 &&
                  mountain_stone*100>=mountain_samples*28 &&
                  mountain_stone*100<=mountain_samples*42,
                  "Mountain outcrop distribution lacks stone or natural gaps");
            for (std::size_t biome = 0; biome < counts.size(); ++biome) {
                const bool narrow_water = biome == static_cast<std::size_t>(Biome::Lake) ||
                                          biome == static_cast<std::size_t>(Biome::SnowLake);
                const int minimum_samples = narrow_water ? 1 : 30;
                if (counts[biome] < minimum_samples)
                    throw std::runtime_error("Seed is missing biome region " + std::to_string(biome) +
                                             " (samples " + std::to_string(counts[biome]) + ")");
            }
        }
        check(coherent > total * .90, "Biomes are scattered instead of coherent regions");
        check(minimum_height == 0 && maximum_height > 300,
              "The single water level or elevated valley sides are missing");
        World world(70421, 1);
        check(world.chunks().size() == world_chunk_count, "Incomplete expanded chunk grid");
        std::array<bool, 513> visible_start_heights{};
        int visible_height_count = 0;
        for (int y = 20; y <= 28; ++y)
            for (int x = 20; x <= 28; ++x) {
                const auto &tile = world.tile(x, y);
                check(tile.terrain != Terrain::Water && tile.terrain != Terrain::MineOre,
                      "Spawn bowl is obstructed");
                const auto height = static_cast<std::size_t>(std::clamp(int(tile.elevation), 0, 512));
                if (!visible_start_heights[height]) {
                    visible_start_heights[height] = true;
                    ++visible_height_count;
                }
            }
        check(visible_height_count >= 2, "Spawn bowl was flattened to one elevation");
        auto state = world.export_state();
        std::string error;
        const auto initial_hash = world.checksum();
        check(initial_hash == world.checksum(), "Cached checksum is unstable");
        auto raised = state;
        raised.chunks.front().tiles.front().elevation += 1;
        check(world.import_state(std::move(raised), error), error.c_str());
        check(world.checksum() != initial_hash, "Height missing from checksum");
        auto invalid_height = state;
        invalid_height.chunks.front().tiles.front().elevation = 513;
        check(!world.import_state(std::move(invalid_height), error), "Invalid height accepted");
        check(world.import_state(state, error), error.c_str());
        auto changed = state;
        changed.chunks.front().tiles.front().biome = Biome::Desert;
        if (state.chunks.front().tiles.front().biome == Biome::Desert)
            changed.chunks.front().tiles.front().biome = Biome::SnowMountain;
        check(world.import_state(std::move(changed), error), error.c_str());
        check(world.checksum() != initial_hash, "Import reused a stale tile hash");
        check(world.import_state(state, error), error.c_str());
        check(world.checksum() == initial_hash, "Restored world checksum changed");
        const auto invalid_key = state.chunks.back().key;
        state.chunks.back().tiles.back().biome = static_cast<Biome>(255);
        check(!world.import_state(std::move(state), error), "Invalid biome accepted");
        const auto path =
            std::filesystem::temp_directory_path() / ("ribterra-biomes-" + secure_token(16) + ".save");
        check(world.save(path, error), error.c_str());
        World loaded(123, 0);
        check(loaded.load(path, error), error.c_str());
        check(loaded.chunks() == world.chunks(), "Biomes changed during save/load");
        check(loaded.checksum() == world.checksum(), "Save/load checksum differs");
        std::filesystem::remove(path);
        // Original 256x256 worlds expand without replacing any existing tile.
        auto legacy = world.export_state();
        std::erase_if(legacy.chunks, [](const Chunk &c) { return c.key.cx > 4 || c.key.cy > 4; });
        const auto old_chunks = legacy.chunks;
        check(old_chunks.size() == 128, "Invalid legacy fixture");
        check(loaded.import_state(std::move(legacy), error), error.c_str());
        for (const auto &chunk : old_chunks)
            check(std::find(loaded.chunks().begin(), loaded.chunks().end(), chunk) != loaded.chunks().end(),
                  "Expansion changed an original chunk");
        check(inside_world(invalid_key.cx * chunk_size, invalid_key.cy * chunk_size), "Far boundary invalid");
        ViewSnapshot source;
        source.revision = 1;
        source.tick = world.ticks();
        source.weather = world.weather();
        source.day = world.calendar().day;
        source.winter = world.winter();
        source.chunks.push_back(Chunk{{WorldLayer::Surface, invalid_key.cx, invalid_key.cy}});
        constexpr std::array<std::int16_t, 9> elevations{320, 0, -32, 512, 44, 97, 3, 201, -7};
        for (int i = 0; i < 9; ++i)
            source.chunks.front().tiles[i] = {Terrain::Grass, 0, true, static_cast<Biome>(i), elevations[i]};
        source.chunks.front().tiles[4].trail_wear = 73;
        net::WorldId id{};
        id.front() = 1;
        auto snapshot = net::make_snapshot(source, id);
        const auto packet = net::decode(net::encode({1, snapshot}), error);
        check(packet.has_value(), "Biome network decode failed");
        ViewSnapshot projected;
        net::WorldId received_id{};
        check(net::apply_snapshot(projected, received_id, std::get<net::Snapshot>(packet->payload), error),
              error.c_str());
        check(projected.chunks == source.chunks, "Network lost biome or far chunk coordinates");
        snapshot.chunks.front().tiles.front().tile.explored = false;
        snapshot.chunks.front().tiles.front().tile.biome = Biome::Desert;
        bool rejected = false;
        try {
            (void)net::encode({2, snapshot});
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "Network exposed a hidden biome");
        std::cout << "All nine biomes, spatial coherence, 10x area, spawn, save/load and legacy expansion "
                     "passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

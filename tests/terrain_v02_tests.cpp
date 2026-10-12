#include "ribterra/alpha_rules.hpp"
#include "ribterra/biomes.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/terrain_rules.hpp"
#include "ribterra/terrain_appearance.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>

using namespace ribterra;
namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<char> file_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void broad_seed_start_safety() {
    const WorldGenerationConfig gen8{};
    int broad_longest_straight = 0;
    constexpr std::array<std::uint32_t, 5> regressions{
        4204434016U, 572988801U, 2035859997U, 301968671U, 2247006640U};
    for (const auto seed : regressions) {
        auto config = gen8;
        config.seed = seed;
        const auto result = certify_alpine_start([config](int x, int y) { return surface_tile(x, y, config); });
        if (!result.passed())
            throw std::runtime_error("Known start certification regression for seed " +
                                     std::to_string(seed) + ": buildable=" +
                                     std::to_string(result.buildable));
    }

    std::uint32_t seed = 0x9e3779b9U;
    for (int sample = 0; sample < 20000; ++sample) {
        seed = seed * 1664525U + 1013904223U;
        auto config = gen8;
        config.seed = seed;
        const auto result = certify_alpine_start([config](int x, int y) { return surface_tile(x, y, config); });
        if (!result.passed())
            throw std::runtime_error("Broad seed start certification failed for seed " +
                                     std::to_string(seed) + ": buildable=" +
                                     std::to_string(result.buildable));
        int low = 513, high = -1, run = 0;
        int previous_center = std::numeric_limits<int>::min();
        for (int y = 8; y <= 40; ++y) {
            const int center = int(std::lround(alpine_river_center_gen8(y, seed)));
            run = center == previous_center ? run + 1 : 1;
            broad_longest_straight = std::max(broad_longest_straight, run);
            previous_center = center;
            if (y > 8) {
                bool vertical_overlap = false;
                for (int x = -8; x <= 24; ++x)
                    vertical_overlap |= surface_tile(x, y, config).terrain == Terrain::Water &&
                                        surface_tile(x, y - 1, config).terrain == Terrain::Water;
                if (!vertical_overlap)
                    throw std::runtime_error("Gen8 river disconnected at seed " +
                                             std::to_string(seed) + " row " + std::to_string(y));
            }
            for (int x = 8; x <= 40; ++x) {
                const int dx = x - alpine_start_x, dy = y - alpine_start_y;
                if (dx * dx + dy * dy > 16 * 16) continue;
                const auto tile = surface_tile(x, y, config);
                if (tile.terrain == Terrain::Water) continue;
                low = std::min(low, int(tile.elevation));
                high = std::max(high, int(tile.elevation));
            }
        }
        if (high - low < 24)
            throw std::runtime_error("Gen8 visible relief regressed for seed " +
                                     std::to_string(seed) + ": range=" +
                                     std::to_string(high - low));
    }
    check(broad_longest_straight <= 16,
          "Gen8 broad seed stream contains an overlong straight river segment");
}

void generator_profile_contract() {
    for (const auto invalid : std::array<WorldGenerationConfig, 4>{{
             {70421U, 0, alpine_valley_profile_id, terrain_rules_version},
             {70421U, generator_version + 1, alpine_valley_profile_id, terrain_rules_version},
             {70421U, generator_version, alpine_valley_profile_id + 1, terrain_rules_version},
             {70421U, generator_version, alpine_valley_profile_id, terrain_rules_version + 1}}}) {
        bool rejected = false;
        try {
            (void)surface_tile(24, 24, invalid);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Unsupported world generation config was accepted");
    }

    std::uint64_t gen7_hash = 1469598103934665603ULL;
    const auto hash_byte = [&](std::uint8_t value) {
        gen7_hash ^= value;
        gen7_hash *= 1099511628211ULL;
    };
    for (const auto baseline_seed : {70421U, 80412U, 0xdeadbeefU})
        for (int y = -32; y <= 63; ++y)
            for (int x = -32; x <= 63; ++x) {
                const auto tile = surface_tile(x, y, baseline_seed);
                hash_byte(static_cast<std::uint8_t>(tile.terrain));
                for (unsigned shift = 0; shift < 32; shift += 8)
                    hash_byte(static_cast<std::uint8_t>(std::uint32_t(tile.amount) >> shift));
                hash_byte(static_cast<std::uint8_t>(tile.biome));
                const auto elevation = static_cast<std::uint16_t>(tile.elevation);
                hash_byte(static_cast<std::uint8_t>(elevation));
                hash_byte(static_cast<std::uint8_t>(elevation >> 8));
            }
    check(gen7_hash == 2423015039328990065ULL,
          "The pinned Gen7 terrain baseline changed");

    constexpr std::array<std::uint32_t, 10> seeds{
        1U, 42U, 123U, 9917U, 70421U, 8675309U, 0x10203040U, 0x9e3779b9U,
        0xdeadbeefU, 0xffffffffU};
    int separated_cells = 0;
    for (const auto seed : seeds) {
        const WorldGenerationConfig gen7{seed, 7, alpine_valley_profile_id, terrain_rules_version};
        const WorldGenerationConfig gen8{seed, 8, alpine_valley_profile_id, terrain_rules_version};
        int minimum_height = 513, maximum_height = -1;
        int resources = 0, mixed_edges = 0;
        int straight_run = 0, longest_straight = 0;
        int previous_center = std::numeric_limits<int>::min();
        for (int y = 8; y <= 40; ++y) {
            const int center = int(std::lround(alpine_river_center_gen8(y, seed)));
            straight_run = center == previous_center ? straight_run + 1 : 1;
            longest_straight = std::max(longest_straight, straight_run);
            previous_center = center;
            std::vector<int> water;
            for (int x = 0; x <= 44; ++x) {
                const auto legacy = surface_tile(x, y, seed);
                const auto dispatched_legacy = surface_tile(x, y, gen7);
                check(legacy == dispatched_legacy, "Gen7 config dispatch changed the shipped seed profile");

                const auto tile = surface_tile(x, y, gen8);
                check(tile == surface_tile(x, y, gen8), "Gen8 generation is not deterministic");
                separated_cells += tile != legacy;
                const int dx = x - alpine_start_x, dy = y - alpine_start_y;
                if (dx * dx + dy * dy <= 16 * 16 && tile.terrain != Terrain::Water) {
                    minimum_height = std::min(minimum_height, int(tile.elevation));
                    maximum_height = std::max(maximum_height, int(tile.elevation));
                    if (tile.amount > 0) {
                        ++resources;
                        constexpr std::array<std::pair<int, int>, 4> neighbors{{
                            {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                        mixed_edges += std::any_of(neighbors.begin(), neighbors.end(), [&](const auto offset) {
                            const auto other = surface_tile(x + offset.first, y + offset.second, gen8);
                            return other.terrain != tile.terrain || other.amount == 0;
                        });
                    }
                }
                if (tile.terrain == Terrain::Water) water.push_back(x);
                if (std::hypot(double(dx), double(dy)) <= 4.35)
                    check(tile.terrain == Terrain::Grass && tile.amount == 0 && tile.elevation == 13,
                          "Gen8 camp shelf does not protect residents and the mine entrance");
            }
            check(!water.empty(), "Gen8 river row disappeared");
            if (y > 8) {
                bool connected = false;
                for (const int x : water)
                    for (int previous_x = 0; previous_x <= 44; ++previous_x)
                        connected |= surface_tile(previous_x, y - 1, gen8).terrain == Terrain::Water &&
                                     previous_x == x;
                check(connected, "Gen8 river is not four-connected between rows");
            }
        }
        check(maximum_height - minimum_height >= 24,
              "Gen8 initial reveal lacks a clearly visible elevation range");
        check(longest_straight <= 15,
              "Gen8 opening river contains an overlong straight segment");
        check(resources >= 18, "Gen8 initial reveal lacks starter resources");
        check(mixed_edges * 100 >= resources * 45,
              "Gen8 starter resources still form same-type plantations");

        World current(seed, 0);
        World legacy(seed, 0, 7);
        check(current.terrain_generator() == 8 && legacy.terrain_generator() == 7,
              "World constructor did not pin the requested terrain profile before generation");
        const auto same_generated_tile = [](const Tile &actual, const Tile &expected) {
            return actual.terrain == expected.terrain && actual.amount == expected.amount &&
                   actual.biome == expected.biome && actual.elevation == expected.elevation &&
                   actual.trail_wear == expected.trail_wear;
        };
        for (const auto [x, y] : std::array<std::pair<int, int>, 5>{{
                 {0, 0}, {24, 24}, {28, 24}, {12, 31}, {40, 18}}}) {
            check(same_generated_tile(current.tile(x, y), surface_tile(x, y, gen8)),
                  "New world did not use the Gen8 config profile");
            check(same_generated_tile(legacy.tile(x, y), surface_tile(x, y, seed)),
                  "Explicit Gen7 world did not preserve the shipped profile");
        }
    }
    check(separated_cells > 2000, "Gen8 does not materially differ from Gen7 terrain");
}

void generator_compatibility_roundtrip() {
    constexpr std::uint32_t seed = 70421U;
    const auto path = std::filesystem::temp_directory_path() / "ribterra-v02-trails.save";
    std::string error;

    World gen7(seed, 0, 7);
    const auto gen7_chunks = gen7.export_state().chunks;
    const auto gen7_checksum = gen7.checksum();
    check(gen7.save(path, error), error.c_str());
    World gen7_loaded(1, 0);
    check(gen7_loaded.load(path, error), error.c_str());
    check(gen7_loaded.terrain_generator() == 7 && gen7_loaded.checksum() == gen7_checksum,
          "Actual Gen7 save/load changed the terrain profile or checksum");
    check(gen7_loaded.export_state().chunks == gen7_chunks,
          "Actual Gen7 save/load regenerated stored terrain chunks");
    std::filesystem::remove(path);

    auto legacy_grid = gen7.export_state();
    legacy_grid.chunks.erase(
        std::remove_if(legacy_grid.chunks.begin(), legacy_grid.chunks.end(), [](const Chunk &chunk) {
            return chunk.key.cx < -3 || chunk.key.cx > 4 || chunk.key.cy < -3 || chunk.key.cy > 4;
        }),
        legacy_grid.chunks.end());
    check(legacy_grid.chunks.size() == 128,
          "Gen7 compatibility fixture did not produce the historical 128-chunk grid");
    const auto retained_chunks = legacy_grid.chunks;

    World extended(2, 0);
    check(extended.import_state(std::move(legacy_grid), error), error.c_str());
    check(extended.terrain_generator() == 7,
          "Legacy 128-chunk extension changed the stored Gen7 profile");
    const auto extended_chunks = extended.export_state().chunks;
    check(extended_chunks.size() == world_chunk_count,
          "Legacy 128-chunk Gen7 world was not extended to the current grid");
    for (const auto &retained : retained_chunks) {
        const auto found = std::find_if(extended_chunks.begin(), extended_chunks.end(),
                                        [&](const Chunk &chunk) { return chunk.key == retained.key; });
        check(found != extended_chunks.end() && *found == retained,
              "Legacy extension changed an authoritative stored Gen7 chunk");
    }
    bool appended_differs_from_gen8 = false;
    World gen8(seed, 0, 8);
    const auto gen8_chunks = gen8.export_state().chunks;
    for (const auto &chunk : extended_chunks) {
        const bool retained = std::any_of(retained_chunks.begin(), retained_chunks.end(),
                                          [&](const Chunk &value) { return value.key == chunk.key; });
        if (retained) continue;
        const auto expected = std::find_if(gen7_chunks.begin(), gen7_chunks.end(),
                                           [&](const Chunk &value) { return value.key == chunk.key; });
        const auto current = std::find_if(gen8_chunks.begin(), gen8_chunks.end(),
                                          [&](const Chunk &value) { return value.key == chunk.key; });
        check(expected != gen7_chunks.end() && chunk == *expected,
              "Legacy 128-chunk extension appended terrain from the wrong generator");
        appended_differs_from_gen8 |= current != gen8_chunks.end() && chunk != *current;
    }
    check(appended_differs_from_gen8,
          "Legacy extension fixture cannot distinguish Gen7 from Gen8 appended chunks");
}

void rendered_foundation_contract() {
    auto flat = [](int, int) {
        Tile tile{Terrain::Grass, 0, true, Biome::Meadow, 10};
        return tile;
    };
    check(surface_foundation_support(24, 24, flat) == SurfaceReason::Allowed,
          "Stable rendered foundation rejected flat explored support");

    auto hidden_dependency = [](int x, int y) {
        Tile tile{Terrain::Grass, 0, true, Biome::Meadow, 10};
        if (x == 27 && y == 27) tile.explored = false;
        return tile;
    };
    check(surface_foundation_support(24, 24, hidden_dependency) == SurfaceReason::Unexplored,
          "Foundation ignored an unknown nearby-water render dependency");

    auto diagonal_relief = [](int x, int y) {
        return Tile{Terrain::Grass, 0, true, Biome::Meadow,
                    static_cast<std::int16_t>(x == 23 && y == 23 ? 20 : 10)};
    };
    check(surface_foundation_support(24, 24, diagonal_relief) == SurfaceReason::SlopeTooSteep,
          "Foundation ignored diagonal height used by rendered terrain corners");

    auto nearby_water = [](int x, int y) {
        return x == 22 && y == 24 ? Tile{Terrain::Water, 0, true, Biome::Lake, 0}
                                  : Tile{Terrain::Grass, 0, true, Biome::Meadow, 10};
    };
    check(surface_foundation_support(24, 24, nearby_water) == SurfaceReason::SlopeTooSteep,
          "Foundation ignored nearby-water corner bending");
}

void generator_contract() {
    for (int coordinate = world_min; coordinate < world_max; ++coordinate) {
        const auto [begin, span] = terrain_material_uv_axis(coordinate);
        const auto [next, next_span] = terrain_material_uv_axis(coordinate + 1);
        check(begin + span == next, "Mirrored terrain texture breaks a shared edge");
        check(begin >= 0.f && begin <= 1.f && begin + span >= 0.f && begin + span <= 1.f,
              "Terrain material UV escapes its atlas region");
        (void)next_span;
    }
    constexpr std::array<std::uint32_t, 10> seeds{70421, 123, 9917, 80412, 1, 42, 70001, 170041,
                                                   8675309, 0xffffffffU};
    std::set<std::pair<int, int>> reference_resources;
    std::set<std::array<std::pair<int, int>, 3>> starter_layouts;
    std::vector<int> reference_centers;
    for (const auto seed : seeds) {
        std::array<std::pair<int, int>, 3> starter_layout{};
        for (unsigned slot = 0; slot < starter_layout.size(); ++slot) {
            const auto anchor = starter_anchor(seed, slot);
            starter_layout[slot] = {anchor.x, anchor.y};
        }
        starter_layouts.insert(starter_layout);
        int minimum_center = world_max, maximum_center = world_min;
        std::vector<int> centers;
        for (int y = world_min; y < world_max; ++y) {
            std::vector<int> water;
            const int center = int(std::lround(alpine_river_center(y, seed)));
            centers.push_back(center);
            minimum_center = std::min(minimum_center, center);
            maximum_center = std::max(maximum_center, center);
            for (int x = center - 6; x <= center + 6; ++x)
                if (surface_tile(x, y, seed).terrain == Terrain::Water) water.push_back(x);
            check(!water.empty(), "River row is disconnected");
            if (y > world_min) {
                bool connected = false;
                const int previous = int(std::lround(alpine_river_center(y - 1, seed)));
                for (const int x : water)
                    for (int px = previous - 6; px <= previous + 6; ++px)
                        connected |= surface_tile(px, y - 1, seed).terrain == Terrain::Water &&
                                      std::abs(px - x) <= 1;
                check(connected, "River is not four-connected between rows");
            }
        }
        check(maximum_center - minimum_center >= 7, "River does not visibly meander across the world");
        if (reference_centers.empty())
            reference_centers = centers;
        else {
            int changed_rows = 0;
            for (std::size_t i = 0; i < centers.size(); ++i) changed_rows += centers[i] != reference_centers[i];
            check(changed_rows >= world_size / 3, "Different seeds reuse substantially the same river route");
        }

        std::set<std::pair<int, int>> reached{{24, 24}};
        std::queue<std::pair<int, int>> frontier;
        frontier.push({24, 24});
        while (!frontier.empty()) {
            const auto [x, y] = frontier.front();
            frontier.pop();
            const auto from = surface_tile(x, y, seed);
            for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx > 52 || ny < 0 || ny > 48 || reached.contains({nx, ny})) continue;
                const auto to = surface_tile(nx, ny, seed);
                if (surface_edge_traversable(from, to)) {
                    reached.insert({nx, ny});
                    frontier.push({nx, ny});
                }
            }
        }
        int buildable = 0, minimum_height = 513, maximum_height = -33;
        bool wood = false, stone = false, food = false;
        std::set<std::pair<int, int>> resources;
        for (const auto [x, y] : reached) {
            const auto tile = surface_tile(x, y, seed);
            const int dx = x - alpine_start_x, dy = y - alpine_start_y;
            wood |= tile.terrain == Terrain::Forest && tile.amount > 0;
            stone |= tile.terrain == Terrain::Rock && tile.amount > 0;
            food |= tile.terrain == Terrain::Grass && tile.amount > 0;
            if (dx * dx + dy * dy <= 16 * 16 && tile.terrain != Terrain::Water) {
                minimum_height = std::min(minimum_height, int(tile.elevation));
                maximum_height = std::max(maximum_height, int(tile.elevation));
                if (tile.amount > 0 || tile.terrain == Terrain::Forest || tile.terrain == Terrain::Rock)
                    resources.insert({x, y});
            }
            if (dx * dx + dy * dy <= 14 * 14 && tile.terrain == Terrain::Grass) {
                buildable += surface_foundation_support(
                                 x, y,
                                 [seed](int sample_x, int sample_y) {
                                     return surface_tile(sample_x, sample_y, seed);
                                 },
                                 false) == SurfaceReason::Allowed;
            }
        }
        check(buildable >= 80, "Certified start has fewer than 80 reachable buildable cells");
        check(wood && stone && food, "Certified start cannot reach every starter resource group");
        check(reached.contains({28, 24}), "Mine entrance is unreachable from the camp");
        check(maximum_height - minimum_height >= 18,
              "Initial reveal lacks an 18-unit visible elevation landmark");
        check(resources.size() >= 18, "Visible starting area has too few natural resource cells");
        if (reference_resources.empty())
            reference_resources = resources;
        else
            check(resources != reference_resources, "Different seeds reuse fixed starter resource coordinates");
    }
    check(starter_layouts.size() >= seeds.size() / 2,
          "Starter resource anchors do not substantially relocate between seeds");

    for (const auto [x, y] : std::array<std::pair<int, int>, 6>{{{-33, -17}, {-32, 31}, {-1, 0},
                                                                    {0, -1}, {31, 32}, {32, -33}}}) {
        const auto a = surface_tile(x, y, 70421);
        const auto b = surface_tile(x + 1, y, 70421);
        check(surface_edge(a, b) == surface_edge(b, a), "Shared surface edge is directional");
    }
}

void distant_resource_population_contract() {
    // These fixed domains are well outside every starter patch. Mountain samples
    // live on the eastern ridges; forest samples stay within the valley's biome
    // band but are far north/south of the camp. Aggregate populations make this
    // a distribution contract rather than a brittle per-seed aesthetic check.
    constexpr std::array<std::uint32_t, 16> seeds{
        1U, 42U, 123U, 9917U, 70421U, 80412U, 170041U, 8675309U,
        0x10203040U, 0x31415926U, 0x5a17c9e3U, 0x7f4a7c15U,
        0x9e3779b9U, 0xc001d00dU, 0xdeadbeefU, 0xffffffffU};
    constexpr std::array<std::pair<int, int>, 4> mountain_blocks{{
        {112, 104}, {272, 296}, {464, 456}, {688, 672}}};
    constexpr std::array<std::pair<int, int>, 6> forest_blocks{{
        {24, 128}, {40, 224}, {56, 336}, {20, 448}, {44, 560}, {60, 688}}};
    constexpr int block_size = 32;

    std::uint64_t mountain_cells{}, mountain_rocks{};
    std::uint64_t eligible_forest_cells{}, eligible_forest_trees{};
    std::uint64_t ineligible_forest_cells{}, ineligible_forest_trees{};
    std::uint64_t eligible_forage_cells{}, eligible_forage_resources{};
    std::uint64_t ineligible_forage_cells{}, ineligible_forage_resources{};
    std::set<std::vector<std::uint8_t>> seed_masks;

    for (const auto seed : seeds) {
        std::vector<std::uint8_t> mask;
        mask.reserve((mountain_blocks.size() + forest_blocks.size()) * block_size * block_size);
        for (const auto [origin_x, origin_y] : mountain_blocks)
            for (int dy = 0; dy < block_size; ++dy)
                for (int dx = 0; dx < block_size; ++dx) {
                    const int x = origin_x + dx, y = origin_y + dy;
                    const auto tile = surface_tile(x, y, seed);
                    const auto biome = surface_biome(x, y, seed);
                    check(tile.biome == biome, "Surface tile disagrees with authoritative biome");
                    if (biome == Biome::Mountain || biome == Biome::SnowMountain) {
                        ++mountain_cells;
                        mountain_rocks += tile.terrain == Terrain::Rock;
                    }
                    mask.push_back(static_cast<std::uint8_t>(
                        tile.terrain == Terrain::Rock ? 2 :
                        tile.terrain == Terrain::Forest ? 1 : 0));
                }
        for (const auto [origin_x, origin_y] : forest_blocks)
            for (int dy = 0; dy < block_size; ++dy)
                for (int dx = 0; dx < block_size; ++dx) {
                    const int x = origin_x + dx, y = origin_y + dy;
                    const auto tile = surface_tile(x, y, seed);
                    const auto biome = surface_biome(x, y, seed);
                    check(tile.biome == biome, "Surface tile disagrees with authoritative biome");
                    if (biome == Biome::Forest || biome == Biome::SnowForest) {
                        const bool macro_eligible =
                            landscape_noise(x, y, 13, seed ^ 0x464f5245U) > .43;
                        if (macro_eligible) {
                            ++eligible_forest_cells;
                            eligible_forest_trees += tile.terrain == Terrain::Forest;
                        } else {
                            ++ineligible_forest_cells;
                            ineligible_forest_trees += tile.terrain == Terrain::Forest;
                        }
                    }
                    if (biome == Biome::Meadow || biome == Biome::SnowMeadow) {
                        const double forest_group =
                            landscape_noise(x, y, 13, seed ^ 0x464f5245U);
                        const double rock_group =
                            landscape_noise(x, y, 17, seed ^ 0x524f434bU);
                        // Restrict this population check to cells where neither
                        // earlier general-terrain branch can claim the tile.
                        if (forest_group <= .79 && rock_group <= .84) {
                            check(tile.terrain == Terrain::Grass,
                                  "Forage-only meadow was claimed by another terrain population");
                            const bool macro_eligible =
                                landscape_noise(x, y, 11, seed ^ 0x464f4f44U) > .68;
                            if (macro_eligible) {
                                ++eligible_forage_cells;
                                eligible_forage_resources += tile.amount > 0;
                            } else {
                                ++ineligible_forage_cells;
                                ineligible_forage_resources += tile.amount > 0;
                            }
                        }
                    }
                    mask.push_back(static_cast<std::uint8_t>(
                        tile.terrain == Terrain::Rock ? 2 :
                        tile.terrain == Terrain::Forest ? 1 : 0));
                }
        seed_masks.insert(std::move(mask));
    }

    check(mountain_cells > 30000, "Distant ridge sample no longer covers an ample mountain domain");
    check(mountain_rocks > 0 && mountain_rocks < mountain_cells,
          "Mountain population lost either occupied outcrops or natural gaps");
    check(mountain_rocks * 100 >= mountain_cells * 30 &&
              mountain_rocks * 100 <= mountain_cells * 40,
          "Mountain rock population left the 30-40% aggregate contract");
    check(eligible_forest_cells > 3000 && ineligible_forest_cells > 1000,
          "Distant woodland sample no longer covers both macro-noise branches");
    check(eligible_forest_trees > 0 && eligible_forest_trees < eligible_forest_cells,
          "Eligible woodland lost either occupied trees or natural gaps");
    check(eligible_forest_trees * 100 >= eligible_forest_cells * 58 &&
              eligible_forest_trees * 100 <= eligible_forest_cells * 70,
          "Eligible woodland tree population left the 58-70% aggregate contract");
    check(ineligible_forest_trees == 0,
          "Macro-ineligible forest biome cells unexpectedly became trees");
    check(eligible_forage_cells > 1000 && ineligible_forage_cells > 1000,
          "Distant meadow sample no longer covers both forage macro-noise branches");
    check(eligible_forage_resources > 0 && eligible_forage_resources < eligible_forage_cells,
          "Eligible forage population lost either occupied patches or natural gaps");
    check(eligible_forage_resources * 100 >= eligible_forage_cells * 28 &&
              eligible_forage_resources * 100 <= eligible_forage_cells * 40,
          "Eligible forage population left the 28-40% aggregate contract");
    check(ineligible_forage_resources == 0,
          "Macro-ineligible meadow cells unexpectedly received forage resources");
    check(seed_masks.size() == seeds.size(),
          "Different seeds reused an identical distant resource population mask");
}

long long cold_generation_ms() {
    const auto begin = std::chrono::steady_clock::now();
    World cold(3133731337U, 1);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();
    check(cold.tile(24, 24).terrain == Terrain::Grass, "Cold generated start is invalid");
    check(elapsed < 5000, "Cold V02 world generation exceeded five seconds");
    return elapsed;
}

int arrival_tick(World &world, int x, int y) {
    check(world.move({1}, x, y).accepted, "Movement command was rejected");
    for (int tick = 1; tick <= 4000; ++tick) {
        world.tick();
        const auto &resident = world.residents().front();
        if (std::abs(resident.x - x) < .001f && std::abs(resident.y - y) < .001f) return tick;
    }
    return -1;
}

void vertex_lighting_contract() {
    const auto flat = [](int, int) { return Tile{Terrain::Grass,0,true,Biome::Meadow,12}; };
    const auto slope = [](int x, int y) {
        return Tile{Terrain::Grass,0,true,Biome::Meadow,static_cast<std::int16_t>(100+x*2+y)};
    };
    check(std::abs(terrain_corner_light(0,0,2.4f,flat)-1.04f)<.0001f,
          "Flat-ground vertex lighting changed");
    const float lit = terrain_corner_light(15,15,2.4f,slope);
    check(lit < 1.04f && lit >= .48f, "Uphill normal did not shade the ground");
    for (int y=14; y<=18; ++y)
        for (int x=14; x<=18; ++x)
            check(std::abs(terrain_corner_light(x,y,2.4f,slope)-lit)<.0001f,
                  "A uniform slope exposed a tile/chunk lighting boundary");
}

template <class Sample>
TerrainRockFace legacy_terrain_rock_face(int x, int y, const Sample &sample) {
    const float left = terrain_corner(x - 1, y, sample);
    const float right = terrain_corner(x + 1, y, sample);
    const float back = terrain_corner(x, y - 1, sample);
    const float front = terrain_corner(x, y + 1, sample);
    const float gradient = std::hypot((right - left) * .5f, (front - back) * .5f);
    const float shore_distance = water_distance(float(x) - .5f, float(y) - .5f, sample);
    const float bank = std::clamp((3.25f - shore_distance) / 2.25f, 0.f, 1.f);
    const float onset = std::lerp(2.4f, .35f, bank);
    const float full = std::lerp(10.5f, 2.8f, bank);
    const float t = std::clamp((gradient - onset) / (full - onset), 0.f, 1.f);
    const float shaped = t * t * (3.f - 2.f * t);
    const float exposure = shaped * std::lerp(.46f, .64f, bank);
    return {exposure, exposure * .52f, 1.f};
}

void slope_material_contract() {
    MaterialWeights grass{};
    grass[0] = 1.f;
    const auto flat = [](int, int) {
        return Tile{Terrain::Grass,0,true,Biome::Meadow,18};
    };
    const auto flat_weights = terrain_slope_rock_weights(8,8,0,grass,flat);
    check(flat_weights == grass, "Flat land acquired painted slope rock");
    const auto flat_face = terrain_rock_face(8,8,flat);
    const auto flat_legacy = legacy_terrain_rock_face(8,8,flat);
    check(flat_face.exposure == flat_legacy.exposure &&
              flat_face.snow_exposure == flat_legacy.snow_exposure && flat_face.light == 1.f,
          "Flat known shelf changed from the legacy material formula");

    const auto gentle_inland = [](int x, int) {
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(80 + x * 3)};
    };
    const auto gentle_face = terrain_rock_face(18,18,gentle_inland);
    const auto gentle_legacy = legacy_terrain_rock_face(18,18,gentle_inland);
    check(std::abs(gentle_face.exposure - gentle_legacy.exposure) < .000001f &&
              std::abs(gentle_face.snow_exposure - gentle_legacy.snow_exposure) < .000001f &&
              gentle_face.light == 1.f,
          "Gentle known slope changed from the legacy material formula");

    const auto gentle_bank = [](int x, int) {
        if (x <= 0) return Tile{Terrain::Water,0,true,Biome::Lake,0};
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(4 + x)};
    };
    const auto steep_bank = [](int x, int) {
        if (x <= 0) return Tile{Terrain::Water,0,true,Biome::Lake,0};
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(4 + x * 7)};
    };
    const auto gentle = terrain_slope_rock_weights(2,5,0,grass,gentle_bank);
    const auto steep = terrain_slope_rock_weights(2,5,0,grass,steep_bank);
    check(steep[8] > gentle[8] && steep[8] > .15f,
          "A steeper known river bank did not expose more rock");

    const auto steep_inland = [](int x, int) {
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(80 + x * 8)};
    };
    const auto inland = terrain_slope_rock_weights(18,18,0,grass,steep_inland);
    const auto inland_face = terrain_rock_face(18,18,steep_inland);
    check(inland_face.known_footprint && inland_face.gradient_x == 8.f &&
              inland_face.gradient_y == 0.f &&
              inland_face.exposure >= .89f && inland_face.exposure <= .90f &&
              inland_face.snow_exposure <= .34f &&
              inland_face.light >= .82f && inland_face.light <= 1.10f && inland[8] > inland[0],
          "A steep fully known inland face did not become rock-dominant");

    MaterialWeights water{};
    water[4] = 1.f;
    const auto water_center = terrain_slope_rock_weights(0,5,4,water,steep_bank);
    check(water_center == water, "River centre acquired slope rock");

    MaterialWeights winter{};
    winter[7] = 1.f;
    const auto snowy_slope = terrain_slope_rock_weights(2,5,7,winter,steep_bank);
    check(snowy_slope[8] > 0.f && snowy_slope[7] > .65f,
          "Winter slope either hid all stone or erased its snow cover");

    bool hidden_high = false;
    const auto hidden = [&](int x, int y) {
        if (x == 3 && y == 5)
            return Tile{Terrain::Grass,0,false,Biome::Meadow,
                        static_cast<std::int16_t>(hidden_high ? 500 : 0)};
        return steep_bank(x,y);
    };
    const auto before = terrain_slope_rock_weights(2,5,0,grass,hidden);
    hidden_high = true;
    const auto after = terrain_slope_rock_weights(2,5,0,grass,hidden);
    check(before == after, "Hidden elevation leaked through slope material");

    const auto known_steep_face = terrain_rock_face(18,18,steep_inland);
    bool hide_footprint_edge = true;
    const auto footprint_hidden = [&](int x, int y) {
        auto tile = steep_inland(x,y);
        if (hide_footprint_edge && x == 21 && y == 21) tile.explored = false;
        return tile;
    };
    const auto suppressed_face = terrain_rock_face(18,18,footprint_hidden);
    const auto suppressed_legacy = legacy_terrain_rock_face(18,18,footprint_hidden);
    check(!suppressed_face.known_footprint &&
              suppressed_face.exposure < known_steep_face.exposure &&
              std::abs(suppressed_face.exposure - suppressed_legacy.exposure) < .000001f &&
              suppressed_face.light == 1.f,
          "Unknown rock-face footprint did not suppress only the added contrast");
    hide_footprint_edge = false;
    check(terrain_rock_face(18,18,footprint_hidden).exposure == known_steep_face.exposure,
          "Revealing the complete rock-face footprint did not restore known contrast");

    const auto concave = [](int x, int) {
        const int offset = x - 10;
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(100 + x * 8 + offset * offset * 2)};
    };
    const auto convex = [](int x, int) {
        const int offset = x - 10;
        return Tile{Terrain::Grass,0,true,Biome::Meadow,
                    static_cast<std::int16_t>(300 + x * 8 - offset * offset * 2)};
    };
    const auto toe = terrain_rock_face(10,10,concave);
    const auto rim = terrain_rock_face(10,10,convex);
    check(toe.light >= .82f && toe.light < 1.f && rim.light > 1.f && rim.light <= 1.10f,
          "Analytic concave toe and convex rim lost bounded curvature lighting");

    TerrainRockFace maximum_face{.90f,.34f,.82f};
    MaterialWeights mine{};
    mine[14] = 1.f;
    check(terrain_slope_rock_weights(14,mine,maximum_face) == mine,
          "Mine material was changed by surface rock-face exposure");
    check(terrain_slope_rock_weights(4,water,maximum_face) == water,
          "Pure water was changed by surface rock-face exposure");

    MaterialWeights mixed_season{};
    mixed_season[0] = .3f;
    mixed_season[3] = .2f;
    mixed_season[7] = .4f;
    mixed_season[8] = .1f;
    const auto exposed_season = terrain_slope_rock_weights(7,mixed_season,maximum_face);
    float seasonal_mass = 0.f;
    for (const float weight : exposed_season) seasonal_mass += weight;
    check(std::abs(seasonal_mass - 1.f) < .000001f && exposed_season[7] >= .264f &&
              maximum_face.snow_exposure <= .34f,
          "Rock-face material lost mass or erased the bounded seasonal snow cap");

    const auto material_at = [&](int x, int y) {
        return terrain_material(steep_bank(x,y), false);
    };
    const auto shared_base_land = terrain_material_weights(2,5,0,material_at);
    const auto shared_base_water = terrain_material_weights(2,5,4,material_at);
    const auto shared_land = terrain_slope_rock_weights(2,5,0,shared_base_land,steep_bank);
    const auto shared_water = terrain_slope_rock_weights(2,5,4,shared_base_water,steep_bank);
    float total = 0.f;
    for (std::size_t i = 0; i < shared_land.size(); ++i) {
        check(shared_land[i] >= 0.f, "Slope material produced a negative weight");
        check(std::abs(shared_land[i] - shared_water[i]) < .00001f,
              "Shared shoreline vertex received domain-dependent slope material");
        total += shared_land[i];
    }
    check(std::abs(total - 1.f) < .0001f,
          "Slope material no longer conserves normalized material mass");
}

void generated_rock_face_contract() {
    for (const auto seed : {1337U, 70421U, 4204434016U}) {
        World world(seed, 10);
        const auto checksum_before = world.checksum();
        std::vector<std::pair<CellCoord,Tile>> revealed_geometry;
        std::set<std::pair<int,int>> corners;
        for (int y=world_min; y<world_max; ++y)
            for (int x=world_min; x<world_max; ++x) {
                const auto &tile = world.tile(x,y);
                if (!tile.explored) continue;
                revealed_geometry.push_back({{x,y,WorldLayer::Surface},tile});
                corners.emplace(x,y);
                corners.emplace(x+1,y);
                corners.emplace(x,y+1);
                corners.emplace(x+1,y+1);
            }
        const auto sample = [&](int x,int y) { return world.tile(x,y); };
        int known_land=0, strong_faces=0, flat_or_gentle=0;
        for (const auto &[x,y] : corners) {
            bool fully_known=true;
            for (int sy=-4; sy<=3 && fully_known; ++sy)
                for (int sx=-4; sx<=3; ++sx)
                    if (!world.tile(x+sx,y+sy).explored) {
                        fully_known=false;
                        break;
                    }
            if (!fully_known) continue;
            const auto &tile=world.tile(x,y);
            if (!tile.explored || tile.terrain==Terrain::Water) continue;
            ++known_land;
            const float dx=(terrain_corner(x+1,y,sample)-terrain_corner(x-1,y,sample))*.5f;
            const float dy=(terrain_corner(x,y+1,sample)-terrain_corner(x,y-1,sample))*.5f;
            const float gradient=std::hypot(dx,dy);
            const auto face=terrain_rock_face(x,y,sample);
            check(face.exposure>=0.f && face.exposure<=.90f &&
                      face.snow_exposure>=0.f && face.snow_exposure<=.34f &&
                      face.light>=.82f && face.light<=1.10f,
                  "Gen8 generated rock face left its material/light bounds");
            strong_faces += face.exposure>=.7f;
            if (gradient<=4.f) {
                ++flat_or_gentle;
                const auto legacy=legacy_terrain_rock_face(x,y,sample);
                check(std::abs(face.exposure-legacy.exposure)<.000001f && face.light==1.f,
                      "Gen8 flat shelf changed from the legacy material formula");
            }
        }
        check(known_land>=400 && strong_faces>=15,
              "Gen8 ordinary start lost measurable strong known rock faces");
        check(flat_or_gentle>=250,
              "Gen8 ordinary start lost its unaffected flat/gentle shelf sample");
        check(world.checksum()==checksum_before,
              "Rock-face appearance scan changed authoritative world state");
        for (const auto &[cell,tile] : revealed_geometry)
            check(world.tile(cell.x,cell.y)==tile,
                  "Rock-face appearance scan changed revealed terrain geometry");
    }
}

void generated_bank_material_contract() {
    // Regression: synthetic steep banks passed while actual Gen7 river banks
    // received almost no rock. Reveal only this test region; do not change its
    // generated elevations, water cells or biome distribution.
    for (const auto seed : {70421U, 1337U}) {
        constexpr int begin = 3, size = 43;
        std::array<Tile, size * size> tiles{};
        for (int y=begin; y<begin+size; ++y)
            for (int x=begin; x<begin+size; ++x) {
                auto tile=surface_tile(x,y,seed);
                tile.explored=true;
                tiles[(y-begin)*size+x-begin]=tile;
            }
        const auto sample = [&](int x,int y) {
            return x>=begin && x<begin+size && y>=begin && y<begin+size
                ? tiles[(y-begin)*size+x-begin] : Tile{};
        };
        const auto material = [&](int x,int y) {
            const auto tile=sample(x,y);
            return tile.explored ? terrain_material(tile,false) : unknown_material;
        };
        int bank_corners=0, exposed_corners=0;
        float total_added=0;
        for (int y=8; y<=40; ++y)
            for (int x=8; x<=40; ++x) {
                const auto tile=sample(x,y);
                if (tile.terrain==Terrain::Water ||
                    water_distance(float(x)-.5f,float(y)-.5f,sample)>=3.25f) continue;
                const int domain=terrain_material(tile,false);
                const auto before=terrain_material_weights(x,y,domain,material);
                const auto after=terrain_slope_rock_weights(x,y,domain,before,sample);
                const float added=after[8]-before[8];
                ++bank_corners;
                total_added+=added;
                exposed_corners+=added>=.1f;
            }
        check(bank_corners>=100, "Generated bank regression fixture lost its river");
        check(exposed_corners*6>=bank_corners && total_added/bank_corners>.04f,
              "Generated river-bank material became imperceptible again");
    }
}

void road_and_desire_paths() {
    check(static_cast<int>(BuildingKind::Road) == 19, "Historical Road ID changed");
    check(!alpha::buildable(BuildingKind::Road), "Road remains in the build policy");
    check(alpha::buildable(BuildingKind::Bridge), "Bridge was removed with Road");

    World plain(70421, 1);
    World road(70421, 1);
    auto state = road.export_state();
    state.buildings.push_back(
        {state.next_building_id++, BuildingKind::Road, 27, 24, 1, WorldLayer::Surface, 1});
    std::string error;
    check(road.import_state(std::move(state), error), error.c_str());
    check(road.walkable(27, 24), "Historical Road is no longer traversable");
    check(!road.build(BuildingKind::Road, 26, 24).accepted, "New Road command was accepted");
    check(arrival_tick(plain, 31, 24) == arrival_tick(road, 31, 24),
          "Historical Road still changes movement speed");

    CellCoord worn{};
    std::uint8_t wear = 0;
    for (int y = 20; y <= 28; ++y)
        for (int x = 20; x <= 32; ++x)
            if (road.tile(x, y).trail_wear > wear) {
                worn = {x, y};
                wear = road.tile(x, y).trail_wear;
            }
    check(wear > 0, "Walking did not create desire-path wear");
    const auto path = std::filesystem::temp_directory_path() / "ribterra-v02-trails.save";
    check(road.save(path, error), error.c_str());
    World loaded(1, 0);
    check(loaded.load(path, error), error.c_str());
    check(loaded.tile(worn.x, worn.y).trail_wear == wear, "Desire path was lost during save/load");
    std::filesystem::remove(path);

    auto generator6 = loaded.export_state();
    generator6.terrain_generator = 6;
    check(loaded.import_state(std::move(generator6), error), error.c_str());
    const auto generator6_hash = loaded.checksum();
    const auto generator6_chunks = loaded.export_state().chunks;
    check(loaded.save(path, error), error.c_str());
    const auto generator6_bytes = file_bytes(path);
    World generator6_restored(3, 0);
    check(generator6_restored.load(path, error), error.c_str());
    check(generator6_restored.export_state().terrain_generator == 6 &&
              generator6_restored.checksum() == generator6_hash,
          "Generator 6 chunks changed during gen7 resave");
    check(generator6_restored.export_state().chunks == generator6_chunks,
          "Generator 6 stored chunks were regenerated by gen7");
    const auto second_path = std::filesystem::temp_directory_path() / "ribterra-v02-gen6-roundtrip.save";
    check(generator6_restored.save(second_path, error), error.c_str());
    check(file_bytes(second_path) == generator6_bytes, "Generator 6 save bytes changed during gen7 roundtrip");
    std::filesystem::remove(path);
    std::filesystem::remove(second_path);

    auto legacy = loaded.export_state();
    legacy.terrain_generator = 5;
    check(loaded.import_state(std::move(legacy), error), error.c_str());
    const auto legacy_hash = loaded.checksum();
    check(loaded.save(path, error), error.c_str());
    World restored(2, 0);
    check(restored.load(path, error), error.c_str());
    check(restored.export_state().terrain_generator == 5 && restored.checksum() == legacy_hash,
          "Legacy terrain profile changed during resave");
    std::filesystem::remove(path);
}
void trail_ribbon_contract() {
    const std::array<std::uint8_t,4> empty{};
    check(trail_ribbon_opacity(0,0,0,empty)==0,"Untrodden ground acquired a path");
    check(trail_ribbon_opacity(0,0,220,empty)>trail_ribbon_opacity(0,0,12,empty),
          "Repeated footsteps did not strengthen the path");
    check(trail_ribbon_opacity(0,0,60,empty)>.45f,
          "Meaningful real traffic remained illegible at overview scale");
    check(trail_ribbon_opacity(.5f,.5f,255,empty)==0,
          "Isolated footsteps covered the whole terrain diamond");
    const std::array<std::uint8_t,4> east{{80,0,0,0}}, west{{0,0,210,0}};
    for (int step=-5;step<=5;++step) {
        const float y=float(step)/10.f;
        check(std::abs(trail_ribbon_opacity(.5f,y,210,east)-
                       trail_ribbon_opacity(-.5f,y,80,west))<.00001f,
              "Connected desire paths have a seam between different wear levels");
    }
    check(trail_ribbon_opacity(.5f,0,210,east)>0,
          "Real neighboring footprints did not connect");
    check(trail_ribbon_opacity(-.5f,0,210,east)==0,
          "Path invented an exit into untrodden ground");
    const std::array<float,4> left_snow{{.05f,.25f,.80f,.10f}};
    const std::array<float,4> right_snow{{.25f,.95f,.30f,.80f}};
    const std::array<float,4> full_snow{{1.f,1.f,1.f,1.f}};
    check(terrain_trail_snow_factor(full_snow,.5f,.5f)>.5f,
          "Snow erased an authoritative desire path");
    for (int step=0;step<=10;++step) {
        const float v=float(step)/10.f;
        check(std::abs(terrain_trail_snow_factor(left_snow,1.f,v)-
                       terrain_trail_snow_factor(right_snow,0.f,v))<.00001f,
              "Snow attenuation introduced a seam between neighboring path tiles");
    }
}
} // namespace

int main() {
    try {
        rendered_foundation_contract();
        generator_contract();
        generator_profile_contract();
        generator_compatibility_roundtrip();
        distant_resource_population_contract();
        broad_seed_start_safety();
        vertex_lighting_contract();
        slope_material_contract();
        generated_rock_face_contract();
        generated_bank_material_contract();
        trail_ribbon_contract();
        const auto generation_ms = cold_generation_ms();
        road_and_desire_paths();
        std::cout << "V02 terrain: Gen8 20,000-seed start safety plus 5 regressions, 10 detailed seeds, "
                     "16-seed distant resource populations, "
                     "Gen7 compatibility, rendered foundation support, connected river, reachable start, "
                     "shared edges, Road policy, "
                     "desire paths and legacy profile passed; cold generation " << generation_ms << " ms.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

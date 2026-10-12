#pragma once
#include "ribterra/foundation.hpp"
#include "ribterra/terrain_rules.hpp"
#include "ribterra/world_grid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ribterra {
struct WorldGenerationConfig {
    std::uint32_t seed{};
    std::uint32_t generator{generator_version};
    std::uint32_t profile_id{alpine_valley_profile_id};
    std::uint32_t terrain_rules{terrain_rules_version};
    friend bool operator==(const WorldGenerationConfig &, const WorldGenerationConfig &) = default;
};

inline std::uint32_t biome_hash(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    return v ^ (v >> 16);
}
inline double landscape_noise(int x, int y, int scale, std::uint32_t seed) {
    const int gx = floor_div(x, scale), gy = floor_div(y, scale);
    auto value = [&](int a, int b) {
        return double(biome_hash(seed ^ std::uint32_t(a) * 73856093U ^ std::uint32_t(b) * 19349663U) &
                      65535) /
               65535.;
    };
    double u = double(x - gx * scale) / scale, v = double(y - gy * scale) / scale;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    return (value(gx, gy) * (1 - u) + value(gx + 1, gy) * u) * (1 - v) +
           (value(gx, gy + 1) * (1 - u) + value(gx + 1, gy + 1) * u) * v;
}

inline constexpr int alpine_start_x = 24;
inline constexpr int alpine_start_y = 24;

inline double seed_fraction(std::uint32_t seed, std::uint32_t salt) {
    return double(biome_hash(seed ^ salt) & 65535U) / 65535.;
}

// One world-space river function is sampled by every chunk. Gen7 varies phase,
// amplitude, width and low-frequency drift by seed while keeping the per-row
// displacement bounded so the discrete channel remains four-connected.
inline double alpine_river_center(int y, std::uint32_t seed) {
    const double phase_a = seed_fraction(seed, 0x52495641U) * 6.283185307179586;
    const double phase_b = seed_fraction(seed, 0x52495642U) * 6.283185307179586;
    const double base = 8.5 + (seed_fraction(seed, 0x52495643U) - .5) * 3.0;
    const double primary = 3.8 + seed_fraction(seed, 0x52495644U) * 1.4;
    const double secondary = 1.2 + seed_fraction(seed, 0x52495645U) * 1.1;
    const double drift = (landscape_noise(0, y, 29, seed ^ 0x52495646U) - .5) * 2.2;
    const double raw = base + primary * std::sin(double(y) / 17.0 + phase_a) +
                       secondary * std::sin(double(y) / 41.0 + phase_b) + drift;
    // Keep the winding channel in the initial reveal, but bend exceptionally
    // eastward seeds away from the camp's guaranteed buildable bowl. The
    // smooth, local correction avoids either a straight global river or a
    // seed retry/fallback map.
    double safety = std::clamp(1. - std::abs(double(y - alpine_start_y)) / 22., 0., 1.);
    safety = safety * safety * (3. - 2. * safety);
    return raw - std::max(0., raw - 12.) * safety;
}
inline double alpine_river_distance(int x, int y, std::uint32_t seed) {
    return std::abs(double(x) - alpine_river_center(y, seed));
}
inline double alpine_river_half_width(int y, std::uint32_t seed) {
    return 1.45 + .28 * landscape_noise(0, y, 37, seed ^ 0x57494454U);
}
inline bool alpine_river_water(int x, int y, std::uint32_t seed) {
    return alpine_river_distance(x, y, seed) <= alpine_river_half_width(y, seed);
}

inline Biome surface_biome(int x, int y, std::uint32_t seed) {
    const double distance = alpine_river_distance(x, y, seed);
    const double moisture = .72 * landscape_noise(x, y, 47, seed ^ 137U) +
                            .28 * landscape_noise(x, y, 19, seed ^ 991U);
    const double temperature = landscape_noise(x, y, 91, seed ^ 293U);
    const bool demo_valley = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y)) <= 92.;
    const bool snow = temperature < .32 && !demo_valley;
    if (alpine_river_water(x, y, seed)) return snow ? Biome::SnowLake : Biome::Lake;

    const double ridge = std::clamp((distance - 20.) / 72. +
                                        .22 * (landscape_noise(x, y, 61, seed ^ 71U) - .5),
                                    0., 1.);
    if (ridge > .64) return snow || (!demo_valley && ridge > .82) ? Biome::SnowMountain : Biome::Mountain;
    if (temperature > .60 && moisture < .38 && distance > 12) return Biome::Desert;
    if (moisture > .49 && distance > 6) return snow ? Biome::SnowForest : Biome::Forest;
    return snow ? Biome::SnowMeadow : Biome::Meadow;
}

inline int starter_jitter(std::uint32_t seed, std::uint32_t salt, int radius) {
    return int(biome_hash(seed ^ salt) % std::uint32_t(radius * 2 + 1)) - radius;
}

struct StarterAnchor { int x{}, y{}; };

inline StarterAnchor starter_anchor(std::uint32_t seed, unsigned slot) {
    // Seven safe directions rotate by seed. They keep guarantees on the camp's
    // landward side without preserving one compass layout across new worlds.
    constexpr std::array<StarterAnchor, 7> directions{{{0, -10}, {7, -7}, {10, 0}, {7, 7},
                                                       {0, 10}, {-4, 8}, {-4, -8}}};
    const unsigned rotation = biome_hash(seed ^ 0x414e4348U) % directions.size();
    const auto offset = directions[(rotation + slot * 2U) % directions.size()];
    const auto salt = 0x414e4300U + slot * 0x101U;
    return {alpine_start_x + offset.x + starter_jitter(seed, salt + 1U, 1),
            alpine_start_y + offset.y + starter_jitter(seed, salt + 2U, 1)};
}

inline std::int16_t surface_elevation(int x, int y, std::uint32_t seed) {
    const double distance = alpine_river_distance(x, y, seed);
    const double half_width = alpine_river_half_width(y, seed);
    if (distance <= half_width) return 0;

    const double bank_distance = distance - half_width;
    const double broad = landscape_noise(x, y, 47, seed ^ 0x454c5631U) - .5;
    const double detail = landscape_noise(x, y, 13, seed ^ 0x454c5632U) - .5;
    double height = 2. + std::min(bank_distance, 19.) * .72 + broad * 8. + detail * 2.5;
    if (bank_distance > 19.) {
        const double ridge = std::clamp((bank_distance - 19.) / 86., 0., 1.);
        height += 310. * std::pow(ridge, 1.32) + ridge * detail * 28.;
    }

    // The camp occupies a gently rolling bowl rather than a rectangular shelf.
    // Smooth seed noise keeps many foundations viable without erasing visible relief.
    const double start_distance = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y));
    // The inner bowl is wholly generated from smooth low-frequency noise. Its
    // circular footprint guarantees a useful core while retaining visible
    // height variation and blending back into the surrounding terrain.
    const double bowl_t = std::clamp((17. - start_distance) / 10., 0., 1.);
    const double bowl = bowl_t * bowl_t * (3. - 2. * bowl_t);
    const double gentle = 10. + (landscape_noise(x, y, 15, seed ^ 0x53544131U) - .5) * 7. +
                          (landscape_noise(x, y, 31, seed ^ 0x53544132U) - .5) * 3.5 +
                          double(x - alpine_start_x) * .08 + double(y - alpine_start_y) * .05;
    height = height * (1. - bowl) + gentle * bowl;

    // A seeded stone shoulder sits inside the initial reveal radius. Its smooth
    // falloff creates an approachable 18–40 unit landmark instead of a hidden
    // distant ridge or a renderer-only bump.
    const auto stone = starter_anchor(seed, 1);
    const double knoll_distance = std::hypot(double(x - stone.x), double(y - stone.y));
    if (knoll_distance < 6.25) {
        const double t = 1. - knoll_distance / 6.25;
        double inner_fade = std::clamp((start_distance - 6.75) / 1.25, 0., 1.);
        inner_fade = inner_fade * inner_fade * (3. - 2. * inner_fade);
        const double shoulder = 20. + seed_fraction(seed, 0x4b4e4f4cU) * 8.;
        height += shoulder * t * t * (3. - 2. * t) * inner_fade;
    }
    return static_cast<std::int16_t>(std::lround(std::clamp(height, 0., 512.)));
}

inline bool organic_patch(int x, int y, int cx, int cy, double rx, double ry,
                          std::uint32_t seed, std::uint32_t salt) {
    const double dx = double(x - cx) / rx, dy = double(y - cy) / ry;
    const double boundary = .78 + .38 * landscape_noise(x, y, 5, seed ^ salt);
    return dx * dx + dy * dy <= boundary;
}

inline bool starter_cluster_cell(int x, int y, int cx, int cy, double rx, double ry,
                                 std::uint32_t seed, std::uint32_t salt, unsigned density) {
    if (!organic_patch(x, y, cx, cy, rx, ry, seed, salt)) return false;
    if (x == cx && y == cy) return true;
    const auto scatter = biome_hash(seed ^ salt ^ std::uint32_t(x) * 2246822519U ^
                                    std::uint32_t(y) * 3266489917U);
    return scatter % 100U < density;
}

inline Tile surface_tile(int x, int y, std::uint32_t seed) {
    const auto biome = surface_biome(x, y, seed);
    const auto elevation = surface_elevation(x, y, seed);
    const auto resource = biome_hash(seed ^ std::uint32_t(x) * 73856093U ^
                                     std::uint32_t(y) * 19349663U ^ 0x5245534fU);
    if (alpine_river_water(x, y, seed))
        return {Terrain::Water, 0, false, biome, 0};

    // Seeded organic starter clusters guarantee the three essential resources,
    // but their centers, outlines and holes vary. The small circular camp bowl is
    // clear without flattening or clearing a rectangular plantation.
    const auto wood = starter_anchor(seed, 0);
    const auto stone = starter_anchor(seed, 1);
    const auto food = starter_anchor(seed, 2);
    const double camp_distance = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y));
    if (camp_distance <= 6.75)
        return {Terrain::Grass, 0, false, Biome::Meadow, elevation};

    // Preserve one guaranteed cell per essential resource before mixing the
    // surrounding patches into a less plantation-like starter landscape.
    if (x == wood.x && y == wood.y)
        return {Terrain::Forest, 16 + int(resource % 12), false, Biome::Forest, elevation};
    if (x == stone.x && y == stone.y)
        return {Terrain::Rock, 16 + int(resource % 14), false, Biome::Mountain, elevation};
    if (x == food.x && y == food.y)
        return {Terrain::Grass, 8 + int(resource % 9), false, Biome::Meadow, elevation};

    const auto starter_mix = biome_hash(seed ^ std::uint32_t(x) * 2654435761U ^
                                        std::uint32_t(y) * 2246822519U ^ 0x4d495845U);
    const bool woodland = organic_patch(x, y, wood.x, wood.y, 5.8, 4.4, seed, 0x574f4f44U);
    const bool food_edge = organic_patch(x, y, food.x, food.y, 7.2, 5.1, seed, 0x464f4f45U);
    if (woodland && starter_mix % 100U < 13U)
        return {Terrain::Grass, 8 + int(resource % 9), false, Biome::Meadow, elevation};
    if (starter_cluster_cell(x, y, wood.x, wood.y, 5.2, 3.8, seed, 0x574f4f44U, 56))
        return {Terrain::Forest, 16 + int(resource % 12), false, Biome::Forest, elevation};
    if (starter_cluster_cell(x, y, stone.x, stone.y, 4.6, 3.7, seed, 0x53544f4eU, 48))
        return {Terrain::Rock, 16 + int(resource % 14), false, Biome::Mountain, elevation};
    if (starter_cluster_cell(x, y, food.x, food.y, 5.8, 3.6, seed, 0x464f4f44U, 38))
        return {Terrain::Grass, 8 + int(resource % 9), false, Biome::Meadow, elevation};
    if (food_edge && starter_mix % 100U < 20U)
        return {Terrain::Forest, 10 + int(resource % 13), false, Biome::Forest, elevation};
    if (food_edge && starter_mix % 100U < 28U)
        return {Terrain::Rock, 8 + int(resource % 11), false, Biome::Mountain, elevation};

    Terrain terrain = Terrain::Grass;
    int amount = 0;
    const double forest_group = landscape_noise(x, y, 13, seed ^ 0x464f5245U);
    const double rock_group = landscape_noise(x, y, 17, seed ^ 0x524f434bU);
    const double forage_group = landscape_noise(x, y, 11, seed ^ 0x464f4f44U);
    if (biome == Biome::Mountain || biome == Biome::SnowMountain) {
        // Rock is an outcrop population, not a solid wall of identical props.
        if (resource % 100U < 35U) {
            terrain = Terrain::Rock;
            amount = 20 + int(resource % 25);
        }
    } else if (biome == Biome::Forest || biome == Biome::SnowForest) {
        // Macro noise shapes the woodland; independent seeded gaps keep its
        // interior mixed and prevent rejected starter-cluster holes refilling.
        if (forest_group > .43 && resource % 100U < 64U) {
            terrain = Terrain::Forest;
            amount = 18 + int(resource % 24);
        } else if (forage_group > .60 && resource % 100U < 13U) {
            amount = 8 + int(resource % 9);
        }
    } else if (biome == Biome::Desert) {
        if (rock_group > .82 && resource % 100U < 35U) {
            terrain = Terrain::Rock;
            amount = 10 + int(resource % 15);
        }
    } else if (forest_group > .79 && resource % 100U < 64U) {
        terrain = Terrain::Forest;
        amount = 12 + int(resource % 19);
    } else if (rock_group > .84 && resource % 100U < 35U) {
        terrain = Terrain::Rock;
        amount = 10 + int(resource % 15);
    } else if (forage_group > .68 && resource % 100U < 34U) {
        amount = 8 + int(resource % 9);
    }
    return {terrain, amount, false, biome, elevation};
}

// Gen8 is intentionally isolated from the seed-only functions above. Those
// functions are the shipped Gen7 profile and remain the source for old saves,
// deterministic fixtures, and legacy chunk-grid extension.
inline double alpine_river_center_gen8(int y, std::uint32_t seed) {
    const double phase_a = seed_fraction(seed, 0x38495641U) * 6.283185307179586;
    const double phase_b = seed_fraction(seed, 0x38495642U) * 6.283185307179586;
    const double base = 8.25 + (seed_fraction(seed, 0x38495643U) - .5) * 4.0;
    const double primary = 5.2 + seed_fraction(seed, 0x38495644U) * 1.8;
    const double secondary = 1.8 + seed_fraction(seed, 0x38495645U) * 1.4;
    const double detail = .75 + seed_fraction(seed, 0x38495647U) * .65;
    const double drift = (landscape_noise(0, y, 23, seed ^ 0x38495646U) - .5) * 3.4;
    const double raw = base + primary * std::sin(double(y) / 15.0 + phase_a) +
                       secondary * std::sin(double(y) / 33.0 + phase_b) +
                       detail * std::sin(double(y) / 7.5 + phase_a * .61 + phase_b * .39) + drift;
    double safety = std::clamp((32. - std::abs(double(y - alpine_start_y))) / 12., 0., 1.);
    safety = safety * safety * (3. - 2. * safety);
    // Smooth compression keeps the opening river between the western bank and
    // camp without pinning extreme seeds to a straight min/max line.
    const double compressed = 7.25 + 4.25 * std::tanh((raw - 7.25) / 4.25);
    const double visible_detail = 1.65 * std::sin(double(y) / 3.8 + phase_b * 1.7) +
                                  .95 * std::sin(double(y) / 1.9 + phase_a * 1.3);
    return raw * (1. - safety) + (compressed + visible_detail) * safety;
}

inline double alpine_river_distance_gen8(int x, int y, std::uint32_t seed) {
    return std::abs(double(x) - alpine_river_center_gen8(y, seed));
}

inline double alpine_river_half_width_gen8(int y, std::uint32_t seed) {
    const double broad = landscape_noise(0, y, 31, seed ^ 0x38574944U);
    const double detail = landscape_noise(0, y, 11, seed ^ 0x38574945U);
    return 1.55 + broad * .42 + detail * .18;
}

inline bool alpine_river_water_gen8(int x, int y, std::uint32_t seed) {
    return alpine_river_distance_gen8(x, y, seed) <= alpine_river_half_width_gen8(y, seed);
}

inline Biome surface_biome_gen8(int x, int y, std::uint32_t seed) {
    const double distance = alpine_river_distance_gen8(x, y, seed);
    const double moisture = .62 * landscape_noise(x, y, 41, seed ^ 0x384d4f31U) +
                            .38 * landscape_noise(x, y, 17, seed ^ 0x384d4f32U);
    const double temperature = landscape_noise(x, y, 83, seed ^ 0x3854454dU);
    const bool start_valley = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y)) <= 58.;
    const bool snow = temperature < .31 && !start_valley;
    if (alpine_river_water_gen8(x, y, seed)) return snow ? Biome::SnowLake : Biome::Lake;
    const double ridge = std::clamp((distance - 16.) / 64. +
                                        .32 * (landscape_noise(x, y, 43, seed ^ 0x38524944U) - .5),
                                    0., 1.);
    if (ridge > .58) return snow || (!start_valley && ridge > .79) ? Biome::SnowMountain : Biome::Mountain;
    if (temperature > .63 && moisture < .35 && distance > 11.) return Biome::Desert;
    if (moisture > .46 && distance > 5.) return snow ? Biome::SnowForest : Biome::Forest;
    return snow ? Biome::SnowMeadow : Biome::Meadow;
}

inline StarterAnchor starter_anchor_gen8(std::uint32_t seed, unsigned slot) {
    constexpr std::array<StarterAnchor, 12> directions{{
        {0, -11}, {5, -10}, {9, -7}, {11, -2}, {10, 5}, {6, 9},
        {0, 11}, {-4, 10}, {-7, 7}, {-8, 3}, {-7, -6}, {-4, -10}}};
    const unsigned rotation = biome_hash(seed ^ 0x38414e43U) % directions.size();
    const unsigned stride = 3U + biome_hash(seed ^ 0x38414e44U) % 3U;
    const auto offset = directions[(rotation + slot * stride) % directions.size()];
    const auto salt = 0x38414e00U + slot * 0x131U;
    return {alpine_start_x + offset.x + starter_jitter(seed, salt + 1U, 1),
            alpine_start_y + offset.y + starter_jitter(seed, salt + 2U, 1)};
}

inline double smooth_disc(int x, int y, int cx, int cy, double radius) {
    const double distance = std::hypot(double(x - cx), double(y - cy));
    if (distance >= radius) return 0.;
    const double t = 1. - distance / radius;
    return t * t * (3. - 2. * t);
}

inline std::int16_t surface_elevation_gen8(int x, int y, std::uint32_t seed) {
    const double distance = alpine_river_distance_gen8(x, y, seed);
    const double half_width = alpine_river_half_width_gen8(y, seed);
    if (distance <= half_width) return 0;

    const double bank = distance - half_width;
    const double broad = landscape_noise(x, y, 37, seed ^ 0x38454c31U) - .5;
    const double detail = landscape_noise(x, y, 11, seed ^ 0x38454c32U) - .5;
    // A broad stepped bank reads as a real valley side while every individual
    // transition stays within the eight-unit walking ramp contract.
    double height = 3. + std::min(bank, 13.) * 1.65 + broad * 9. + detail * 3.;
    if (bank > 13.) {
        const double ridge = std::clamp((bank - 13.) / 70., 0., 1.);
        height += 56. * std::pow(ridge, 1.18) + ridge * broad * 18.;
    }

    const double start_distance = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y));
    const double plateau_noise = (landscape_noise(x, y, 19, seed ^ 0x38504c31U) - .5) * 3.2 +
                                 (landscape_noise(x, y, 43, seed ^ 0x38504c32U) - .5) * 2.;
    const double plateau = 13. + plateau_noise;
    double settle = std::clamp((15.5 - start_distance) / 8.5, 0., 1.);
    settle = settle * settle * (3. - 2. * settle);
    height = height * (1. - settle) + plateau * settle;

    // The camp, initial resident grid and mine entrance sit on one dependable
    // shelf. Relief begins immediately outside it instead of erasing a 13-tile lawn.
    if (start_distance <= 4.35) return 13;
    double core_blend = std::clamp((start_distance - 4.35) / 3.25, 0., 1.);
    core_blend = core_blend * core_blend * (3. - 2. * core_blend);
    height = 13. * (1. - core_blend) + height * core_blend;

    // Three low, seed-positioned landforms remain fully walkable but rise far
    // enough to be visible in the opening camera.
    const auto stone = starter_anchor_gen8(seed, 1);
    const auto wood = starter_anchor_gen8(seed, 0);
    const int shoulder_x = alpine_start_x + starter_jitter(seed, 0x38534831U, 3) + 4;
    const int shoulder_y = alpine_start_y + starter_jitter(seed, 0x38534832U, 4) - 10;
    height += core_blend * (22. + seed_fraction(seed, 0x38484c31U) * 10.) *
              smooth_disc(x, y, stone.x, stone.y, 8.5);
    height += core_blend * (14. + seed_fraction(seed, 0x38484c32U) * 8.) *
              smooth_disc(x, y, wood.x, wood.y, 7.25);
    height += core_blend * (18. + seed_fraction(seed, 0x38484c33U) * 11.) *
              smooth_disc(x, y, shoulder_x, shoulder_y, 9.5);
    return static_cast<std::int16_t>(std::lround(std::clamp(height, 0., 512.)));
}

inline bool starter_lobe_gen8(int x, int y, StarterAnchor anchor, double radius,
                              std::uint32_t seed, std::uint32_t salt) {
    const int satellite_x = anchor.x + starter_jitter(seed, salt + 11U, 4);
    const int satellite_y = anchor.y + starter_jitter(seed, salt + 12U, 4);
    return organic_patch(x, y, anchor.x, anchor.y, radius, radius * .72, seed, salt) ||
           organic_patch(x, y, satellite_x, satellite_y, radius * .58, radius * .46,
                         seed, salt ^ 0x5a5a5a5aU);
}

inline Tile surface_tile_gen8(int x, int y, std::uint32_t seed) {
    const auto biome = surface_biome_gen8(x, y, seed);
    const auto elevation = surface_elevation_gen8(x, y, seed);
    const auto resource = biome_hash(seed ^ std::uint32_t(x) * 73856093U ^
                                     std::uint32_t(y) * 19349663U ^ 0x38524553U);
    if (alpine_river_water_gen8(x, y, seed)) return {Terrain::Water, 0, false, biome, 0};

    const auto wood = starter_anchor_gen8(seed, 0);
    const auto stone = starter_anchor_gen8(seed, 1);
    const auto food = starter_anchor_gen8(seed, 2);
    const double camp_distance = std::hypot(double(x - alpine_start_x), double(y - alpine_start_y));
    if (camp_distance <= 4.35) return {Terrain::Grass, 0, false, Biome::Meadow, elevation};
    if (x == wood.x && y == wood.y)
        return {Terrain::Forest, 18 + int(resource % 11), false, Biome::Forest, elevation};
    if (x == stone.x && y == stone.y)
        return {Terrain::Rock, 18 + int(resource % 13), false, Biome::Mountain, elevation};
    if (x == food.x && y == food.y)
        return {Terrain::Grass, 9 + int(resource % 8), false, Biome::Meadow, elevation};

    const auto local = biome_hash(resource ^ 0x384c4f42U);
    const bool wood_lobe = starter_lobe_gen8(x, y, wood, 5.9, seed, 0x38574f4fU);
    const bool stone_lobe = starter_lobe_gen8(x, y, stone, 5.0, seed, 0x3853544fU);
    const bool food_lobe = starter_lobe_gen8(x, y, food, 6.2, seed, 0x38464f4fU);
    // Competing lobes deliberately interleave their edges. This produces
    // clearings, forage beside trees, and scattered stone instead of plantations.
    if (wood_lobe && local % 100U < 43U)
        return {Terrain::Forest, 14 + int(resource % 17), false, Biome::Forest, elevation};
    if (stone_lobe && (local / 7U) % 100U < 38U)
        return {Terrain::Rock, 12 + int(resource % 19), false, Biome::Mountain, elevation};
    if (food_lobe && (local / 13U) % 100U < 34U)
        return {Terrain::Grass, 8 + int(resource % 10), false, Biome::Meadow, elevation};
    if ((wood_lobe || food_lobe) && (local / 17U) % 100U < 11U)
        return {Terrain::Rock, 7 + int(resource % 9), false, Biome::Mountain, elevation};
    if ((stone_lobe || food_lobe) && (local / 19U) % 100U < 13U)
        return {Terrain::Forest, 10 + int(resource % 12), false, Biome::Forest, elevation};

    Terrain terrain = Terrain::Grass;
    int amount = 0;
    const double forest_group = landscape_noise(x, y, 11, seed ^ 0x38465253U);
    const double rock_group = landscape_noise(x, y, 15, seed ^ 0x38524b53U);
    const double forage_group = landscape_noise(x, y, 9, seed ^ 0x38464745U);
    if (biome == Biome::Mountain || biome == Biome::SnowMountain) {
        if (rock_group > .38 && resource % 100U < 44U) {
            terrain = Terrain::Rock;
            amount = 18 + int(resource % 26);
        } else if (forest_group > .72 && resource % 100U < 16U) {
            terrain = Terrain::Forest;
            amount = 10 + int(resource % 15);
        }
    } else if (biome == Biome::Forest || biome == Biome::SnowForest) {
        if (forest_group > .38 && resource % 100U < 58U) {
            terrain = Terrain::Forest;
            amount = 16 + int(resource % 25);
        } else if (forage_group > .56 && resource % 100U < 18U) {
            amount = 8 + int(resource % 9);
        }
    } else if (biome == Biome::Desert) {
        if (rock_group > .69 && resource % 100U < 30U) {
            terrain = Terrain::Rock;
            amount = 10 + int(resource % 16);
        }
    } else if (forest_group > .70 && resource % 100U < 48U) {
        terrain = Terrain::Forest;
        amount = 12 + int(resource % 20);
    } else if (rock_group > .76 && resource % 100U < 31U) {
        terrain = Terrain::Rock;
        amount = 10 + int(resource % 16);
    } else if (forage_group > .61 && resource % 100U < 35U) {
        amount = 8 + int(resource % 9);
    }
    return {terrain, amount, false, biome, elevation};
}

inline void validate_generation_config(const WorldGenerationConfig &config) {
    if (config.generator == 0 || config.generator > generator_version ||
        config.profile_id != alpine_valley_profile_id || config.terrain_rules != terrain_rules_version)
        throw std::invalid_argument("Unsupported world generation profile");
}

inline Biome surface_biome(int x, int y, const WorldGenerationConfig &config) {
    validate_generation_config(config);
    return config.generator == 8 ? surface_biome_gen8(x, y, config.seed)
                                 : surface_biome(x, y, config.seed);
}

inline std::int16_t surface_elevation(int x, int y, const WorldGenerationConfig &config) {
    validate_generation_config(config);
    return config.generator == 8 ? surface_elevation_gen8(x, y, config.seed)
                                 : surface_elevation(x, y, config.seed);
}

inline Tile surface_tile(int x, int y, const WorldGenerationConfig &config) {
    validate_generation_config(config);
    return config.generator == 8 ? surface_tile_gen8(x, y, config.seed)
                                 : surface_tile(x, y, config.seed);
}

struct AlpineStartCertification {
    int buildable{};
    bool mine_reachable{};
    bool wood_reachable{};
    bool stone_reachable{};
    bool food_reachable{};

    constexpr bool passed() const {
        return mine_reachable && buildable >= 80 && wood_reachable && stone_reachable && food_reachable;
    }
};

// This is the single start-safety contract used by generation and seed-stream
// tests. Sampling is bounded to the same local area that generation previously
// certified, so a broad deterministic seed stream remains inexpensive.
template <class Sample> AlpineStartCertification certify_alpine_start(const Sample &sample) {
    constexpr int min_x = 0, max_x = 52, min_y = 0, max_y = 48;
    constexpr int width = max_x - min_x + 1, height = max_y - min_y + 1;
    constexpr auto index = [](int x, int y) { return (y - min_y) * width + (x - min_x); };
    constexpr std::array<std::pair<int, int>, 4> neighbors{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

    std::array<Tile, width * height> tiles{};
    for (int y = min_y; y <= max_y; ++y)
        for (int x = min_x; x <= max_x; ++x)
            tiles[std::size_t(index(x, y))] = sample(x, y);

    std::array<bool, width * height> reached{};
    std::vector<std::pair<int, int>> frontier;
    frontier.reserve(width * height);
    reached[std::size_t(index(alpine_start_x, alpine_start_y))] = true;
    frontier.push_back({alpine_start_x, alpine_start_y});
    for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
        const auto [x, y] = frontier[cursor];
        const auto &from = tiles[std::size_t(index(x, y))];
        for (const auto [dx, dy] : neighbors) {
            const int nx = x + dx, ny = y + dy;
            if (nx < min_x || nx > max_x || ny < min_y || ny > max_y ||
                reached[std::size_t(index(nx, ny))])
                continue;
            const auto &to = tiles[std::size_t(index(nx, ny))];
            if (!surface_edge_traversable(from, to)) continue;
            reached[std::size_t(index(nx, ny))] = true;
            frontier.push_back({nx, ny});
        }
    }

    AlpineStartCertification result;
    result.mine_reachable = reached[std::size_t(index(28, 24))];
    for (const auto [x, y] : frontier) {
        const auto &ground = tiles[std::size_t(index(x, y))];
        result.wood_reachable |= ground.terrain == Terrain::Forest && ground.amount > 0;
        result.stone_reachable |= ground.terrain == Terrain::Rock && ground.amount > 0;
        result.food_reachable |= ground.terrain == Terrain::Grass && ground.amount > 0;
        const int dx = x - alpine_start_x, dy = y - alpine_start_y;
        if (dx * dx + dy * dy > 14 * 14 || ground.terrain != Terrain::Grass) continue;
        result.buildable +=
            surface_foundation_support(x, y, [&](int sx, int sy) {
                if (sx < min_x || sx > max_x || sy < min_y || sy > max_y) return Tile{};
                return tiles[std::size_t(index(sx, sy))];
            }, false) == SurfaceReason::Allowed;
    }
    return result;
}
} // namespace ribterra

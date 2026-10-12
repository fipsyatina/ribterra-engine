#pragma once
#include "ribterra/terrain_surface.hpp"
#include "ribterra/world_grid.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace ribterra {
inline constexpr std::uint32_t alpine_valley_profile_id = 1;
inline constexpr std::uint32_t terrain_rules_version = 1;
inline constexpr std::uint32_t surface_rules_generator_version = 6;
inline constexpr int ramp_height_limit = 8;
inline constexpr int cliff_height_threshold = 24;
inline constexpr int foundation_height_limit = 2;
inline constexpr std::uint32_t stable_foundation_generator_version = 7;

enum class SurfaceClass : std::uint8_t { Flat, Ramp, Cliff, Water };
enum class SurfaceEdge : std::uint8_t { Continuous, Ramp, Cliff, WaterBoundary };
enum class SurfaceReason : std::uint8_t {
    Allowed,
    OutsideWorld,
    WrongLayer,
    Unexplored,
    Occupied,
    WaterRequired,
    UnsupportedGround,
    SlopeTooSteep,
    NoEntranceAccess,
    BridgeDisconnected
};

constexpr std::string_view surface_reason_key(SurfaceReason reason) {
    switch (reason) {
    case SurfaceReason::Allowed: return "build.allowed";
    case SurfaceReason::OutsideWorld: return "build.outside_world";
    case SurfaceReason::WrongLayer: return "build.wrong_layer";
    case SurfaceReason::Unexplored: return "build.unexplored";
    case SurfaceReason::Occupied: return "build.occupied";
    case SurfaceReason::WaterRequired: return "build.water_required";
    case SurfaceReason::UnsupportedGround: return "build.unsupported_ground";
    case SurfaceReason::SlopeTooSteep: return "build.slope_too_steep";
    case SurfaceReason::NoEntranceAccess: return "build.no_entrance_access";
    case SurfaceReason::BridgeDisconnected: return "build.bridge_disconnected";
    }
    return "build.unsupported_ground";
}

constexpr SurfaceEdge surface_edge(const Tile &from, const Tile &to) {
    if (from.terrain == Terrain::Water || to.terrain == Terrain::Water)
        return SurfaceEdge::WaterBoundary;
    const int difference = std::abs(int(from.elevation) - int(to.elevation));
    if (difference == 0) return SurfaceEdge::Continuous;
    if (difference <= ramp_height_limit) return SurfaceEdge::Ramp;
    return SurfaceEdge::Cliff;
}

constexpr bool surface_edge_traversable(const Tile &from, const Tile &to, bool from_passage = false,
                                        bool to_passage = false) {
    if ((from.terrain == Terrain::Water && !from_passage) ||
        (to.terrain == Terrain::Water && !to_passage) || from.terrain == Terrain::MineOre ||
        to.terrain == Terrain::MineOre)
        return false;
    return std::abs(int(from.elevation) - int(to.elevation)) <= ramp_height_limit;
}

template <class Sample> SurfaceClass surface_class(int x, int y, const Sample &sample) {
    const auto center = sample(x, y);
    if (center.terrain == Terrain::Water) return SurfaceClass::Water;
    int low = center.elevation, high = center.elevation;
    for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const auto neighbor = sample(x + dx, y + dy);
        low = std::min(low, int(neighbor.elevation));
        high = std::max(high, int(neighbor.elevation));
    }
    if (high - low >= cliff_height_threshold) return SurfaceClass::Cliff;
    if (high != low) return SurfaceClass::Ramp;
    return SurfaceClass::Flat;
}

template <class Sample> SurfaceReason surface_buildability(int x, int y, const Sample &sample) {
    const auto center = sample(x, y);
    if (!center.explored) return SurfaceReason::Unexplored;
    if (center.terrain == Terrain::Water || center.terrain == Terrain::MineOre)
        return SurfaceReason::UnsupportedGround;
    int low = center.elevation, high = center.elevation;
    for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const auto neighbor = sample(x + dx, y + dy);
        if (!neighbor.explored || neighbor.terrain == Terrain::Water) continue;
        low = std::min(low, int(neighbor.elevation));
        high = std::max(high, int(neighbor.elevation));
    }
    return high - low <= foundation_height_limit ? SurfaceReason::Allowed : SurfaceReason::SlopeTooSteep;
}

// Gen7 foundations use the same four rendered terrain corners as the client
// mesh. Every sample that can affect those corners (including the nearby-water
// bend) must already be known, so later exploration cannot visibly lift or
// sink a placed building. `require_explored=false` is reserved for generation
// certification, where the complete generated surface is available before the
// normal reveal pass.
template <class Sample>
SurfaceReason surface_foundation_support(int x, int y, const Sample &sample,
                                         bool require_explored = true) {
    auto stable_sample = [&](int sx, int sy) {
        Tile tile = sample(sx, sy);
        if (!require_explored) tile.explored = true;
        return tile;
    };
    if (require_explored) {
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
                if (!sample(x + dx, y + dy).explored) return SurfaceReason::Unexplored;
    }
    const auto center = stable_sample(x, y);
    if (center.terrain == Terrain::Water || center.terrain == Terrain::MineOre)
        return SurfaceReason::UnsupportedGround;
    const auto corners = terrain_corners(x, y, stable_sample);
    const auto [low, high] = std::minmax_element(corners.begin(), corners.end());
    return *high - *low <= float(foundation_height_limit) ? SurfaceReason::Allowed
                                                          : SurfaceReason::SlopeTooSteep;
}
} // namespace ribterra

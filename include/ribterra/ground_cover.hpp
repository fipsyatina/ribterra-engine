#pragma once
#include "ribterra/biomes.hpp"
#include <array>
#include <cstdint>

namespace ribterra {

// Cosmetic, low-growing plants and pebbles only. These never create a world
// resource, a collision or a pick target. All points stay inside their known
// cell, and the pattern is independent of the camera and zoom.
struct GroundCoverDetail {
    float dx{}, dy{}, width{};
    std::uint8_t variant{};
    std::uint8_t shade{255};
    friend bool operator==(const GroundCoverDetail &, const GroundCoverDetail &) = default;
};
struct GroundCover {
    std::array<GroundCoverDetail, 3> details{};
    unsigned count{};
    friend bool operator==(const GroundCover &, const GroundCover &) = default;
};
inline constexpr std::array<const char *, 5> ground_cover_sprites{
    "concept.grass-edge.0", "concept.grass-edge.1", "concept.flowers",
    "concept.stone.0", "concept.stone.1"};

inline float ground_cover_depth(const GroundCoverDetail &detail) {
    // A tile's surface is drawn at x+y-.5, the next diagonal at x+y+.5.
    // Back-corner jitter must not put a plant behind its own ground quad.
    return std::clamp(detail.dx + detail.dy + .05f, -.45f, .45f);
}

inline GroundCover ground_cover(int x, int y, const Tile &tile, bool winter,
                                bool clear_of_site, bool shore) {
    GroundCover result;
    if (!tile.explored || tile.terrain != Terrain::Grass || tile.amount != 0 ||
        winter || snowy(tile.biome) || tile.biome == Biome::Desert ||
        tile.trail_wear >= 24 || !clear_of_site)
        return result;
    const auto seed = biome_hash(std::uint32_t(x) * 73856093U ^
                                 std::uint32_t(y) * 19349663U ^ 0x47524e44U);
    if (seed % 100U >= 68U) return result;
    result.count = 2U + ((seed >> 8) % 3U == 0U);
    // Jittered separated strata avoid the old one identical tuft per cell
    // appearance without expensive or camera-dependent random sampling.
    constexpr std::array<std::array<float, 2>, 3> anchors{{
        {{-.29f, -.24f}}, {{.29f, -.13f}}, {{-.02f, .31f}}}};
    for (unsigned i = 0; i < result.count; ++i) {
        const auto hash = biome_hash(seed ^ (i + 1U) * 2654435761U);
        auto &detail = result.details[i];
        const auto &anchor = anchors[(i + (seed >> 16) % 3U) % 3U];
        detail.dx = anchor[0] + (int(hash % 17U) - 8) * .012f;
        detail.dy = anchor[1] + (int((hash >> 8) % 17U) - 8) * .012f;
        const auto choice = (hash >> 16) % 100U;
        detail.variant = static_cast<std::uint8_t>(
            choice < 25U ? 0 : choice < 50U ? 1 : choice < 83U ? 2 : choice < 92U ? 3 : 4);
        // Flowers are visibly low meadow plants, stones are small scattered
        // pebbles, never the large clusters used for actionable rock deposits.
        detail.width = detail.variant < 2 ? (shore ? 29.f : 26.f)
                     : detail.variant == 2 ? 21.f : 10.f;
        detail.width *= .82f + float((hash >> 24) % 37U) * .01f;
        detail.shade = static_cast<std::uint8_t>(232U + (hash >> 12) % 24U);
    }
    return result;
}

} // namespace ribterra

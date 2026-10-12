#pragma once
#include "ribterra/biomes.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ribterra {

struct ResourceShrub {
    float dx{}, dy{}, width{};
    std::uint8_t variant{};
    friend bool operator==(const ResourceShrub &, const ResourceShrub &) = default;
};
inline constexpr std::array<const char *, 8> resource_shrub_sprites{
    "concept.shrub.0", "concept.shrub.1", "concept.shrub.2", "concept.shrub.3",
    "concept.shrub.0.winter", "concept.shrub.1.winter",
    "concept.shrub.2.winter", "concept.shrub.3.winter"};

// Appearance only: the caller still draws every real berry-resource cell.
// Anchors remain inside that cell so cell-based gathering stays unambiguous.
inline ResourceShrub resource_shrub(int x, int y, bool snow) {
    const auto hash = biome_hash(std::uint32_t(x) * 73856093U ^
                                 std::uint32_t(y) * 19349663U);
    return {(int(hash % 23U) - 11) * .027f,
            (int((hash >> 5) % 23U) - 11) * .027f,
            22.f + float(hash % 5U) * 2.f,
            static_cast<std::uint8_t>((hash >> 16) % 4U + (snow ? 4U : 0U))};
}

inline float resource_shrub_depth(const ResourceShrub &shrub) {
    return std::clamp(shrub.dx + shrub.dy + .05f, -.45f, .45f);
}

} // namespace ribterra

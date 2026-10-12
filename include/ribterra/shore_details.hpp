#pragma once
#include "ribterra/biomes.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ribterra {

struct ShoreDetail {
    float dx{}, dy{}, width{};
    std::uint8_t variant{};
    friend bool operator==(const ShoreDetail &, const ShoreDetail &) = default;
};
struct ShoreDetails {
    std::array<ShoreDetail, 2> details{};
    unsigned count{};
    friend bool operator==(const ShoreDetails &, const ShoreDetails &) = default;
};
inline constexpr std::array<const char *, 8> shore_detail_sprites{
    "concept.bank.0", "concept.bank.1", "concept.bank.2", "concept.bank.3",
    "concept.bank.0.winter", "concept.bank.1.winter",
    "concept.bank.2.winter", "concept.bank.3.winter"};

inline float shore_detail_depth(const ShoreDetail &detail) {
    return std::clamp(detail.dx + detail.dy + .05f, -.45f, .45f);
}

// Decorative land-side rocks, not deposits or collision geometry. The mask
// uses known neighbors only: exploring distant terrain cannot move a bank.
template <class Sample>
ShoreDetails shore_details(int x, int y, const Tile &tile, bool winter,
                           bool clear_of_site, const Sample &sample) {
    ShoreDetails result;
    if (!tile.explored || tile.terrain != Terrain::Grass || tile.amount != 0 ||
        tile.trail_wear >= 24 || !clear_of_site)
        return result;
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{1, 0}}, {{0, 1}}, {{-1, 0}}, {{0, -1}}}};
    std::array<unsigned, 4> water_edges{};
    unsigned edge_count = 0;
    for (unsigned edge = 0; edge < directions.size(); ++edge) {
        const auto &d = directions[edge];
        const auto neighbor = sample(x + d[0], y + d[1]);
        if (neighbor.explored && neighbor.terrain == Terrain::Water)
            water_edges[edge_count++] = edge;
    }
    if (edge_count == 0) return result;
    const auto seed = biome_hash(std::uint32_t(x) * 73856093U ^
                                std::uint32_t(y) * 19349663U ^ 0x53484f52U);
    if (seed % 100U >= (edge_count > 1 ? 90U : 70U)) return result;
    result.count = 1U + ((seed >> 8) % 100U < (edge_count > 1 ? 75U : 48U));
    const bool snow = winter || snowy(tile.biome);
    const auto first_edge = (seed >> 16) % edge_count;
    for (unsigned i = 0; i < result.count; ++i) {
        const auto hash = biome_hash(seed ^ (i + 1U) * 2654435761U);
        const auto &d = directions[water_edges[(first_edge + i) % edge_count]];
        const float tangent = (i == 0 ? -.17f : .22f) +
                              (int(hash % 9U) - 4) * .015f;
        const float outward = .28f + float((hash >> 8) % 7U) * .012f;
        auto &detail = result.details[i];
        detail.dx = d[0] * outward - d[1] * tangent;
        detail.dy = d[1] * outward + d[0] * tangent;
        // Large masses collect at bends. Mix flatter silhouettes along the
        // straight edges so the bank does not become a repeated boulder chain.
        const bool large = i == 0 &&
            (edge_count > 1 || biome_hash(seed ^ 0x42414e4bU) % 100U < 58U);
        const auto shape = i == 0 ? (hash >> 12) % 4U
            : (result.details[0].variant % 4U + 1U + (hash >> 12) % 3U) % 4U;
        // Paired seasonal pages share rectangles and ground pivots. Snow must
        // not move or shrink a bank, nor turn it into a resource deposit.
        detail.variant = static_cast<std::uint8_t>(shape + (snow ? 4U : 0U));
        detail.width = i == 0 ? (large ? 36.f : 23.f) + float((hash >> 20) % 7U) * 2.f
                              : 12.f + float((hash >> 20) % 5U) * 1.5f;
    }
    return result;
}
} // namespace ribterra

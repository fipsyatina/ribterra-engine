#pragma once
#include "ribterra/shore_details.hpp"
#include "ribterra/terrain_appearance.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ribterra {

struct ReliefDetails {
    std::array<ShoreDetail, 3> details{};
    unsigned count{};
    friend bool operator==(const ReliefDetails &, const ReliefDetails &) = default;
};

// Sparse decorative outcrops follow the real, fully known surface gradient.
// They reuse bank silhouettes and never create resources or collision geometry.
inline ReliefDetails relief_details(int x, int y, const Tile &tile, bool winter,
                                    bool clear_of_site,
                                    const std::array<TerrainRockFace, 4> &corners) {
    ReliefDetails result;
    if (!tile.explored || tile.terrain != Terrain::Grass || tile.amount != 0 ||
        tile.trail_wear >= 24 || !clear_of_site ||
        std::any_of(corners.begin(), corners.end(),
                    [](const auto &corner) { return !corner.known_footprint; }))
        return result;

    float exposure = 0.f;
    float gradient = 0.f;
    float gradient_x = 0.f;
    float gradient_y = 0.f;
    float curvature = 0.f;
    for (const auto &corner : corners) {
        exposure += corner.exposure;
        gradient += std::hypot(corner.gradient_x, corner.gradient_y);
        gradient_x += corner.gradient_x;
        gradient_y += corner.gradient_y;
        curvature = std::max(curvature, std::abs(corner.light - 1.f));
    }
    bool adjacent_exposure = false;
    for (unsigned i = 0; i < corners.size(); ++i)
        adjacent_exposure |= corners[i].exposure >= .70f &&
                             corners[(i + 1U) % corners.size()].exposure >= .70f;
    if (exposure < 4.f * .60f || gradient < 4.f * 4.5f || !adjacent_exposure)
        return result;

    const auto priority = [](int px, int py) {
        return biome_hash(std::uint32_t(px) * 73856093U ^
                          std::uint32_t(py) * 19349663U ^ 0x524c4946U);
    };
    const auto centre_priority = priority(x, y);
    if (centre_priority <= priority(x + 1, y) || centre_priority <= priority(x - 1, y) ||
        centre_priority <= priority(x, y + 1) || centre_priority <= priority(x, y - 1))
        return result;

    float length = std::hypot(gradient_x, gradient_y);
    if (length < .001f) {
        const auto steepest = std::max_element(corners.begin(), corners.end(),
            [](const auto &a, const auto &b) {
                return std::hypot(a.gradient_x, a.gradient_y) <
                       std::hypot(b.gradient_x, b.gradient_y);
            });
        gradient_x = steepest->gradient_x;
        gradient_y = steepest->gradient_y;
        length = std::hypot(gradient_x, gradient_y);
    }
    const float gx = gradient_x / length;
    const float gy = gradient_y / length;
    const float tx = -gy;
    const float ty = gx;
    const auto seed = biome_hash(centre_priority ^ 0x4f555443U);
    const float tangent_jitter = (static_cast<int>((seed >> 7) % 9U) - 4) * .012f;
    const float side = (seed & 1U) != 0U ? 1.f : -1.f;
    const bool snow = winter || snowy(tile.biome);
    const auto base_shape = (seed >> 12) % 4U;

    result.count = curvature >= .08f ? 3U : 2U;
    result.details[0] = {gx * .10f + tx * tangent_jitter,
                         gy * .10f + ty * tangent_jitter,
                         32.f + float((seed >> 16) % 7U) * 2.f,
                         static_cast<std::uint8_t>(base_shape + (snow ? 4U : 0U))};
    result.details[1] = {-gx * .02f + tx * side * .24f,
                         -gy * .02f + ty * side * .24f,
                         12.f + float((seed >> 20) % 6U) * 2.f,
                         static_cast<std::uint8_t>((base_shape + 1U) % 4U +
                                                   (snow ? 4U : 0U))};
    if (result.count == 3)
        result.details[2] = {gx * .20f - tx * side * .18f,
                             gy * .20f - ty * side * .18f,
                             12.f + float((seed >> 24) % 4U) * 2.f,
                             static_cast<std::uint8_t>((base_shape + 2U) % 4U +
                                                       (snow ? 4U : 0U))};
    return result;
}

} // namespace ribterra

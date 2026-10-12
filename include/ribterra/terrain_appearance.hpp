#pragma once
#include "ribterra/ecology_appearance.hpp"
#include "ribterra/world_grid.hpp"
#include "ribterra/terrain_surface.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace ribterra {
// World-space, connected desire path. Directions: +x,+y,-x,-y. The shared
// endpoint uses the lesser wear of both cells, so neighboring ribbons meet.
inline float trail_ribbon_opacity(float x, float y, std::uint8_t wear,
                                 const std::array<std::uint8_t,4> &neighbors) {
    if (!wear) return 0;
    const float strength=.22f+.72f*std::sqrt(float(wear)/255.f);
    const float width=.10f+.26f*std::sqrt(float(wear)/255.f);
    const auto soft=[](float d,float half_width) {
        const float a=std::clamp((half_width+.10f-d)/.16f,0.f,1.f);
        return a*a*(3.f-2.f*a);
    };
    float opacity=strength*soft(std::hypot(x,y),width);
    constexpr std::array<std::pair<float,float>,4> directions{{{1.f,0.f},{0.f,1.f},{-1.f,0.f},{0.f,-1.f}}};
    for (int i=0;i<4;++i) {
        if (!neighbors[i]) continue;
        const auto [dx,dy]=directions[i];
        const float along=std::clamp(2.f*(x*dx+y*dy),0.f,1.f);
        const float distance=std::hypot(x-dx*.5f*along,y-dy*.5f*along);
        const float edge=std::sqrt(float(std::min(wear,neighbors[i]))/255.f);
        opacity=std::max(opacity,std::lerp(strength,.22f+.72f*edge,along)*
            soft(distance,std::lerp(width,.10f+.26f*edge,along)));
    }
    return opacity;
}
// Snow coverage at the four world-space corners (NW, NE, SE, SW). Opposite
// sides of a shared edge use the same two corner samples and therefore the
// same attenuation even when the neighboring tile biomes differ.
inline float terrain_trail_snow_factor(const std::array<float,4> &coverage, float u, float v) {
    const float snow = std::lerp(std::lerp(coverage[0], coverage[1], u),
                                 std::lerp(coverage[3], coverage[2], u), v);
    return std::lerp(1.f, .58f, std::clamp(snow, 0.f, 1.f));
}
// A shared vertex has one normal/illumination, irrespective of which adjacent
// quad emits it. Per-tile normals exposed the mesh as dark diamonds in snow.
template <class Sample>
float terrain_corner_light(int x, int y, float vertical_scale, const Sample &sample) {
    const float dx = (terrain_corner(x + 1, y, sample) - terrain_corner(x - 1, y, sample)) *
                     vertical_scale / 58.f;
    const float dy = (terrain_corner(x, y + 1, sample) - terrain_corner(x, y - 1, sample)) *
                     vertical_scale / 58.f;
    return std::clamp((1.04f - .62f * dx - .36f * dy) / std::sqrt(1 + dx * dx + dy * dy),
                      .48f, 1.12f);
}
// Start and span of one cell in a mirrored material period.
// Both sides of every shared edge must sample exactly the same texel.
inline constexpr std::pair<float, float> terrain_material_uv_axis(int coordinate, int cells = 4) {
    cells = std::max(1, cells);
    const float cell = 1.f / float(cells);
    const int local = (coordinate % cells + cells) % cells;
    const int period = (coordinate - local) / cells;
    const bool reversed = period % 2 != 0;
    return {reversed ? 1.f - float(local) * cell : float(local) * cell,
            reversed ? -cell : cell};
}
inline constexpr int unknown_material = 255;
inline int terrain_material(const Tile &t, bool winter) {
    const bool snow = winter || snowy(t.biome);
    if (t.terrain == Terrain::MineFloor || t.terrain == Terrain::MineOre)
        return 14;
    if (t.terrain == Terrain::Water)
        return snow ? 6 : 4;
    if (snow)
        return 7;
    if (t.biome == Biome::Desert)
        return 3;
    if (t.biome == Biome::Mountain)
        return 8;
    return 0;
}
inline bool liquid_material(int m) {
    return m == 4 || m == 6;
}
using MaterialWeights = std::array<float, 16>;
// The same world-space corner always receives the same blend, including at chunk seams.
// Hidden samples never contribute. Water blends through a shallow beach apron; mine floors remain separate.
template <class Sample>
MaterialWeights terrain_material_weights(int x, int y, int domain, const Sample &sample) {
    MaterialWeights weights{};
    float total = 0;
    bool wet_neighborhood = false;
    for (int dy = -3; dy <= 2 && !wet_neighborhood; ++dy)
        for (int dx = -3; dx <= 2; ++dx) {
            const int material = sample(x + dx, y + dy);
            if (material != unknown_material && liquid_material(material)) {
                wet_neighborhood = true;
                break;
            }
        }
    // Land-to-land biome changes need a several-cell blend. Near water the
    // compact kernel preserves the blue core of narrow rivers. Choosing the
    // kernel from the shared neighborhood, rather than the caller's domain,
    // keeps both sides of a shoreline identical.
    const float blend_scale = wet_neighborhood ? 1.15f : 4.f;
    for (int dy = -3; dy <= 2; ++dy)
        for (int dx = -3; dx <= 2; ++dx) {
            const int material = sample(x + dx, y + dy);
            if (material == unknown_material ||
                (material == 14) != (domain == 14))
                continue;
            const float distance2 = (dx + .5f) * (dx + .5f) + (dy + .5f) * (dy + .5f);
            const float weight = std::exp(-distance2 / blend_scale);
            weights[material] += weight;
            total += weight;
        }
    if (total == 0)
        weights[domain] = 1;
    else
        for (auto &weight : weights)
            weight /= total;
    // A wet sand band between temperate soil/rock and water. Snow coasts retain snow.
    const float water=weights[4]+weights[6];
    const float beach=2.2f*water*(1-water);
    const float soil=weights[0]+weights[8];
    weights[0]*=1-beach; weights[8]*=1-beach;
    weights[3]+=soil*beach;
    // Sand remains visible beneath shallow water. Shared corner weights keep
    // both sides of the shoreline continuous, including bridge landings.
    const float shallow = std::clamp((1.f-water)*2.f,0.f,1.f);
    const float clear_water = weights[4]*shallow;
    weights[4] -= clear_water;
    weights[3] += clear_water*.10f;
    weights[5] += clear_water*.90f;
    return weights;
}
struct TerrainRockFace {
    float exposure{};
    float snow_exposure{};
    float light{1.f};
    float gradient_x{}, gradient_y{};
    bool known_footprint{};
};
// Expose the existing rock material where the authoritative surface is steep.
// This changes only the material mixture on the already projected mesh: no
// ledges, collision, picking or simulation height are invented. Every input is
// sampled in world-corner space, so adjacent quads receive exactly the same
// result at their shared vertices. Unexplored elevations never contribute.
template <class Sample>
TerrainRockFace terrain_rock_face(int x, int y, const Sample &sample) {
    const float left = terrain_corner(x - 1, y, sample);
    const float right = terrain_corner(x + 1, y, sample);
    const float back = terrain_corner(x, y - 1, sample);
    const float front = terrain_corner(x, y + 1, sample);
    const float dx = (right - left) * .5f;
    const float dy = (front - back) * .5f;
    const float gradient = std::hypot(dx, dy);
    const float shore_distance = water_distance(float(x) - .5f, float(y) - .5f, sample);
    const float bank = std::clamp((3.25f - shore_distance) / 2.25f, 0.f, 1.f);
    // The shared surface eases into sea level. Its real bank gradients are
    // smaller than an inland escarpment, so they need their own range rather
    // than a threshold reached only by synthetic, exaggerated slope fixtures.
    const float onset = std::lerp(2.4f, .35f, bank);
    const float full = std::lerp(10.5f, 2.8f, bank);
    const float t = std::clamp((gradient - onset) / (full - onset), 0.f, 1.f);
    const float shaped = t * t * (3.f - 2.f * t);
    const float base = shaped * std::lerp(.46f, .64f, bank);
    TerrainRockFace face{base, base * .52f, 1.f, dx, dy, false};

    // Ordinary Gen8 banks already exceed the old full-exposure gradient, but
    // its 46--64% blend left even escarpments looking like grass. Only strengthen
    // a genuinely steep face, not the gentle slopes or flat buildable shelf.
    // Require the full bounded sampling footprint to be known: partial averages
    // at the exploration frontier must not manufacture a high-contrast cliff.
    for (int sy = -4; sy <= 3; ++sy)
        for (int sx = -4; sx <= 3; ++sx)
            if (!sample(x + sx, y + sy).explored)
                return face;
    face.known_footprint = true;
    const float steep = std::clamp((gradient - 4.f) / 4.f, 0.f, 1.f);
    const float strength = steep * steep * (3.f - 2.f * steep);
    face.exposure = std::lerp(base, .90f, strength);
    // Keep a snowy cap rather than turning the winter hillside into bare rock.
    face.snow_exposure = std::min(.34f, base * .52f + (face.exposure - base) * .10f);

    // Concave toes are darker; convex shoulders catch a narrow lighter rim.
    // This curvature is from the same true heightfield, not a painted height
    // band or per-tile pattern. Shared vertices and seasonal geometry agree.
    const float centre = terrain_corner(x, y, sample);
    const float bend = std::clamp((left + right + back + front - 4.f * centre) /
                                 (gradient + 1.f), -1.f, 1.f);
    face.light = 1.f - bend * (bend > 0.f ? .18f : .10f) * strength;
    return face;
}

inline MaterialWeights terrain_slope_rock_weights(int domain, MaterialWeights weights,
                                                  const TerrainRockFace &face) {
    if (domain == 14)
        return weights;

    // Snow remains a readable cover even on the steepest winter bank; exposed
    // stone shows through it instead of making the whole face abruptly thaw.
    // Pure water has none of these weights and cannot acquire a rock bed.
    const float from_grass = weights[0] * face.exposure;
    const float from_sand = weights[3] * face.exposure;
    const float from_snow = weights[7] * face.snow_exposure;
    weights[0] -= from_grass;
    weights[3] -= from_sand;
    weights[7] -= from_snow;
    weights[8] += from_grass + from_sand + from_snow;
    return weights;
}
template <class Sample>
MaterialWeights terrain_slope_rock_weights(int x, int y, int domain,
                                           MaterialWeights weights,
                                           const Sample &sample) {
    return terrain_slope_rock_weights(domain, weights, terrain_rock_face(x, y, sample));
}
// A rounded, several-cell falloff at the explored frontier, draped on the ground.
template <class Known> float frontier_fog(float x, float y, const Known &known) {
    float nearest2 = 16;
    const int cx = int(std::floor(x)), cy = int(std::floor(y));
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
            if (!known(cx + dx, cy + dy)) {
                const float a = cx + dx - x, b = cy + dy - y;
                nearest2 = std::min(nearest2, a * a + b * b);
            }
    const float clear = std::clamp((std::sqrt(nearest2) - .75f) / 2.5f, 0.f, 1.f);
    return 1 - clear * clear * (3 - 2 * clear);
}
} // namespace ribterra

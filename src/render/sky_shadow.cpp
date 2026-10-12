#include "ribterra/sky_shadow.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace ribterra::render {
namespace {
constexpr float direction_length_squared_tolerance = .001f;
constexpr float maximum_sun_radiance = 64.f;

struct DirectionalSource {
    std::array<float, 3> direction{};
    bool enabled{};
};

float dot(const std::array<float, 3>& left, const std::array<float, 3>& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}
std::array<float, 3> cross(const std::array<float, 3>& left,
                           const std::array<float, 3>& right) {
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}
std::array<float, 3> normalized(std::array<float, 3> value) {
    const float length = std::sqrt(dot(value, value));
    for (float& component : value) component /= length;
    return value;
}

void validate_direction(const std::array<float, 4>& direction, const char* label) {
    if (!std::all_of(direction.begin(), direction.end(),
                     [](float value) { return std::isfinite(value); }) || direction[3] != 0.f) {
        throw std::invalid_argument(std::string(label) + " direction must be finite with reserved zero");
    }
    const float length_squared = direction[0] * direction[0] +
        direction[1] * direction[1] + direction[2] * direction[2];
    if (!std::isfinite(length_squared) ||
        std::abs(length_squared - 1.f) > direction_length_squared_tolerance) {
        throw std::invalid_argument(std::string(label) + " direction must be normalized");
    }
}

DirectionalSource sun_source(const SurfaceLightingFrame& lighting) {
    validate_direction(lighting.sun_direction, "Sun");
    if (!std::all_of(lighting.sun_color_intensity.begin(), lighting.sun_color_intensity.end(),
                     [](float value) {
                         return std::isfinite(value) && value >= 0.f && value <= maximum_sun_radiance;
                     })) {
        throw std::invalid_argument("Sun color and intensity must be finite and within [0,64]");
    }
    const bool colored = lighting.sun_color_intensity[0] > 0.f ||
        lighting.sun_color_intensity[1] > 0.f || lighting.sun_color_intensity[2] > 0.f;
    return {{lighting.sun_direction[0], lighting.sun_direction[1], lighting.sun_direction[2]},
            lighting.sun_color_intensity[3] > 0.f && colored};
}

DirectionalSource moon_source(const MoonLightingGpu& moon) {
    validate_moon_lighting(moon);
    const bool colored = moon.color_intensity[0] > 0.f || moon.color_intensity[1] > 0.f ||
        moon.color_intensity[2] > 0.f;
    return {{moon.direction[0], moon.direction[1], moon.direction[2]},
            moon.color_intensity[3] > 0.f && colored};
}

void validate_triangle(const SkyShadowTriangle& triangle) {
    if (triangle.source_mask < 1u || triangle.source_mask > 3u)
        throw std::invalid_argument("Sky shadow source mask must be 1, 2 or 3");
    if (!std::isfinite(triangle.reveal) || triangle.reveal < 0.f || triangle.reveal > 1.f)
        throw std::invalid_argument("Sky shadow reveal must be finite and within [0,1]");
    for (const auto& vertex : triangle.vertices)
        for (const float value : vertex.position)
            if (!std::isfinite(value) || std::abs(value) > sky_shadow_coordinate_bound)
                throw std::invalid_argument("Sky shadow vertex exceeds the fixed camera cube");
    const auto edge_a = std::array<double, 3>{
        double(triangle.vertices[1].position[0]) - triangle.vertices[0].position[0],
        double(triangle.vertices[1].position[1]) - triangle.vertices[0].position[1],
        double(triangle.vertices[1].position[2]) - triangle.vertices[0].position[2]};
    const auto edge_b = std::array<double, 3>{
        double(triangle.vertices[2].position[0]) - triangle.vertices[0].position[0],
        double(triangle.vertices[2].position[1]) - triangle.vertices[0].position[1],
        double(triangle.vertices[2].position[2]) - triangle.vertices[0].position[2]};
    const std::array<double, 3> area{
        edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
        edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
        edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0]};
    const double area_squared = area[0] * area[0] + area[1] * area[1] + area[2] * area[2];
    if (!std::isfinite(area_squared) || area_squared == 0.0)
        throw std::invalid_argument("Sky shadow triangle must be nondegenerate");
}

ShadowSourceGpu source_gpu(const DirectionalSource& source) {
    if (!source.enabled) return {};
    const auto light_to_source = normalized(source.direction);
    if (light_to_source[2] < sky_shadow_min_direction_z)
        throw std::invalid_argument("Enabled sky shadow direction must be at least 20 degrees above the horizon");

    const std::array<float, 3> forward{-light_to_source[0], -light_to_source[1],
                                       -light_to_source[2]};
    const auto right = normalized(cross({0.f, 1.f, 0.f}, forward));
    const auto up = cross(forward, right);
    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const float x : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound})
        for (const float y : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound})
            for (const float z : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound}) {
                const std::array<float, 3> corner{x, y, z};
                const std::array<float, 3> projected{dot(corner, right), dot(corner, up),
                                                     dot(corner, forward)};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    minimum[axis] = std::min(minimum[axis], projected[axis]);
                    maximum[axis] = std::max(maximum[axis], projected[axis]);
                }
            }
    constexpr float interior_texels = sky_shadow_map_resolution - 4.f;
    const float span_u = (maximum[0] - minimum[0]) * sky_shadow_map_resolution / interior_texels;
    const float span_v = (maximum[1] - minimum[1]) * sky_shadow_map_resolution / interior_texels;
    const float pad_u = 2.f * span_u / sky_shadow_map_resolution;
    const float pad_v = 2.f * span_v / sky_shadow_map_resolution;
    const float minimum_depth = minimum[2] - sky_shadow_depth_expansion;
    const float span_depth = maximum[2] - minimum[2] + 2.f * sky_shadow_depth_expansion;
    ShadowSourceGpu result;
    result.u_min = {right[0], right[1], right[2], minimum[0] - pad_u};
    result.v_min = {up[0], up[1], up[2], minimum[1] - pad_v};
    result.f_min = {forward[0], forward[1], forward[2], minimum_depth};
    result.inverse_span_enabled = {1.f / span_u, 1.f / span_v, 1.f / span_depth, 1.f};
    result.bias_texel = {sky_shadow_receiver_bias / span_depth,
                         1.f / sky_shadow_map_resolution, 0.f, 0.f};
    return result;
}
} // namespace

PreparedSkyShadows prepare_sky_shadows(std::span<const SkyShadowTriangle> triangles,
                                       const SurfaceLightingFrame& lighting,
                                       const MoonLightingGpu& moon) {
    if (triangles.size() > sky_shadow_triangle_capacity)
        throw std::invalid_argument("Sky shadow raw triangle capacity exceeded");
    const std::array sources{sun_source(lighting), moon_source(moon)};
    std::array<std::size_t, 2> active{};
    for (const auto& triangle : triangles) {
        validate_triangle(triangle);
        if (triangle.reveal == 0.f) continue;
        for (std::size_t source = 0; source < active.size(); ++source)
            if ((triangle.source_mask & (1u << source)) != 0u &&
                ++active[source] > sky_shadow_active_triangle_capacity)
                throw std::invalid_argument("Sky shadow active per-source triangle capacity exceeded");
    }

    PreparedSkyShadows result;
    result.vertices.reserve((sources[0].enabled ? active[0] : 0u) * 3u +
                            (sources[1].enabled ? active[1] : 0u) * 3u);
    for (std::size_t source = 0; source < sources.size(); ++source) {
        result.gpu.sources[source] = source_gpu(sources[source]);
        result.first_vertex[source] = static_cast<std::uint32_t>(result.vertices.size());
        if (!sources[source].enabled) continue;
        for (const auto& triangle : triangles) {
            if (triangle.reveal > 0.f && (triangle.source_mask & (1u << source)) != 0u)
                result.vertices.insert(result.vertices.end(), triangle.vertices.begin(),
                                       triangle.vertices.end());
        }
        result.vertex_count[source] = static_cast<std::uint32_t>(result.vertices.size()) -
                                      result.first_vertex[source];
    }
    return result;
}

} // namespace ribterra::render

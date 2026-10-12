#pragma once

#include "ribterra/render_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace ribterra::render {

inline constexpr float reference_tile_width = 58.f;
inline constexpr float reference_tile_height = 29.f;
inline constexpr float reference_tile_half_width = reference_tile_width * .5f;
inline constexpr float reference_tile_half_height = reference_tile_height * .5f;
inline constexpr float legacy_elevation_screen_scale = 2.4f;
inline constexpr float render_z_per_legacy_elevation =
    legacy_elevation_screen_scale / reference_tile_height;
inline constexpr float camera_depth_axis = 0.5773502691896258f; // 1 / sqrt(3)

constexpr bool finite_projection_value(float value) noexcept {
    return value == value && value >= std::numeric_limits<float>::lowest() &&
           value <= std::numeric_limits<float>::max();
}

constexpr void require_finite_projection_value(float value) {
    if (!finite_projection_value(value))
        throw std::invalid_argument("World projection value must be finite");
}

constexpr float render_z_from_legacy_elevation(float elevation) {
    require_finite_projection_value(elevation);
    const float result = elevation * render_z_per_legacy_elevation;
    require_finite_projection_value(result);
    return result;
}

constexpr float legacy_elevation_from_render_z(float render_z) {
    require_finite_projection_value(render_z);
    const float result = render_z / render_z_per_legacy_elevation;
    require_finite_projection_value(result);
    return result;
}

constexpr std::array<float, 3> camera_relative_render_xyz(
    const std::array<float, 3>& world, const std::array<float, 3>& camera) {
    std::array<float, 3> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        require_finite_projection_value(world[i]);
        require_finite_projection_value(camera[i]);
        result[i] = world[i] - camera[i];
        require_finite_projection_value(result[i]);
    }
    return result;
}

constexpr Point project_render_xyz(const std::array<float, 3>& render_xyz) {
    for (const float value : render_xyz) require_finite_projection_value(value);
    const Point result{
        reference_tile_half_width * (render_xyz[0] - render_xyz[1]),
        reference_tile_half_height * (render_xyz[0] + render_xyz[1]) -
            reference_tile_height * render_xyz[2]};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

// Same affine basis, deliberately preserving the historical GameView float
// grouping. Reassociation changes textured Legacy pixels by an RGB LSB even
// though the resulting subpixel coordinates are mathematically equivalent.
constexpr Point project_legacy_screen_xy(float x, float y, float camera_x, float camera_y,
                                         float zoom) {
    for (float value : {x, y, camera_x, camera_y, zoom}) require_finite_projection_value(value);
    if (zoom <= 0.f) throw std::invalid_argument("World projection zoom must be positive");
    const float tile_width = reference_tile_width * zoom;
    const float tile_height = reference_tile_height * zoom;
    const Point result{(x - y - camera_x + camera_y) * tile_width * .5f,
                       (x + y - camera_x - camera_y) * tile_height * .5f};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

constexpr float camera_depth(const std::array<float, 3>& render_xyz) {
    for (const float value : render_xyz) require_finite_projection_value(value);
    const float result = camera_depth_axis * (render_xyz[0] + render_xyz[1] + render_xyz[2]);
    require_finite_projection_value(result);
    return result;
}

constexpr std::array<float, 3> reconstruct_render_xyz(Point projected, float depth) {
    require_finite_projection_value(projected.x);
    require_finite_projection_value(projected.y);
    require_finite_projection_value(depth);
    const float difference = projected.x / reference_tile_half_width;
    const float projected_sum_minus_two_z = projected.y / reference_tile_half_height;
    const float sum_plus_z = depth / camera_depth_axis;
    const float z = (sum_plus_z - projected_sum_minus_two_z) / 3.f;
    const float sum = (2.f * sum_plus_z + projected_sum_minus_two_z) / 3.f;
    const std::array<float, 3> result{(sum + difference) * .5f,
                                      (sum - difference) * .5f, z};
    for (const float value : result) require_finite_projection_value(value);
    return result;
}

constexpr Point reconstruct_render_xy(Point projected, float render_z) {
    require_finite_projection_value(projected.x);
    require_finite_projection_value(projected.y);
    require_finite_projection_value(render_z);
    const float difference = projected.x / reference_tile_half_width;
    const float sum = (projected.y + reference_tile_height * render_z) /
                      reference_tile_half_height;
    const Point result{(sum + difference) * .5f, (sum - difference) * .5f};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

constexpr std::uint16_t encode_camera_depth_r16(float depth, float depth_min, float depth_max) {
    require_finite_projection_value(depth);
    require_finite_projection_value(depth_min);
    require_finite_projection_value(depth_max);
    const float span = depth_max - depth_min;
    if (!(depth_min < depth_max) || !finite_projection_value(span) ||
        depth < depth_min || depth > depth_max)
        throw std::invalid_argument("Camera-depth value or range is invalid");
    const float normalized = (depth - depth_min) / span;
    require_finite_projection_value(normalized);
    return static_cast<std::uint16_t>(normalized * 65535.f + .5f);
}

constexpr float decode_camera_depth_r16(std::uint16_t encoded, float depth_min, float depth_max) {
    require_finite_projection_value(depth_min);
    require_finite_projection_value(depth_max);
    const float span = depth_max - depth_min;
    if (!(depth_min < depth_max) || !finite_projection_value(span))
        throw std::invalid_argument("Camera-depth range is invalid");
    const float result = depth_min + span * (static_cast<float>(encoded) / 65535.f);
    require_finite_projection_value(result);
    return result;
}

constexpr Point reference_offset_from_source_pixel(Point source_pixel, Point pivot,
                                                    float export_pixels_per_reference_pixel) {
    require_finite_projection_value(source_pixel.x);
    require_finite_projection_value(source_pixel.y);
    require_finite_projection_value(pivot.x);
    require_finite_projection_value(pivot.y);
    require_finite_projection_value(export_pixels_per_reference_pixel);
    if (!(export_pixels_per_reference_pixel > 0.f))
        throw std::invalid_argument("Asset export scale must be positive");
    const Point result{(source_pixel.x - pivot.x) / export_pixels_per_reference_pixel,
                       (source_pixel.y - pivot.y) / export_pixels_per_reference_pixel};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

constexpr Point source_pixel_from_reference_offset(Point reference_offset, Point pivot,
                                                    float export_pixels_per_reference_pixel) {
    require_finite_projection_value(reference_offset.x);
    require_finite_projection_value(reference_offset.y);
    require_finite_projection_value(pivot.x);
    require_finite_projection_value(pivot.y);
    require_finite_projection_value(export_pixels_per_reference_pixel);
    if (!(export_pixels_per_reference_pixel > 0.f))
        throw std::invalid_argument("Asset export scale must be positive");
    const Point result{pivot.x + reference_offset.x * export_pixels_per_reference_pixel,
                       pivot.y + reference_offset.y * export_pixels_per_reference_pixel};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

constexpr Point screen_offset_from_source_pixel(Point source_pixel, Point pivot,
                                                 float export_pixels_per_reference_pixel,
                                                 float zoom) {
    require_finite_projection_value(zoom);
    if (!(zoom > 0.f)) throw std::invalid_argument("Projection zoom must be positive");
    const auto reference = reference_offset_from_source_pixel(
        source_pixel, pivot, export_pixels_per_reference_pixel);
    const Point result{reference.x * zoom, reference.y * zoom};
    require_finite_projection_value(result.x);
    require_finite_projection_value(result.y);
    return result;
}

constexpr Point source_pixel_from_screen_offset(Point screen_offset, Point pivot,
                                                 float export_pixels_per_reference_pixel,
                                                 float zoom) {
    require_finite_projection_value(screen_offset.x);
    require_finite_projection_value(screen_offset.y);
    require_finite_projection_value(zoom);
    if (!(zoom > 0.f)) throw std::invalid_argument("Projection zoom must be positive");
    const Point reference{screen_offset.x / zoom, screen_offset.y / zoom};
    require_finite_projection_value(reference.x);
    require_finite_projection_value(reference.y);
    return source_pixel_from_reference_offset(reference, pivot,
                                              export_pixels_per_reference_pixel);
}

} // namespace ribterra::render

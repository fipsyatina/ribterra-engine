#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ribterra::render {

// Camera-relative render coordinates: X/Y in tiles, Z in the canonical
// tile-equivalent metric declared by world_projection.hpp. Legacy elevation
// maps to Z as elevation*2.4/29; camera origin is removed before float upload.
// This stream accompanies Vertex without changing the legacy sprite/UI ABI.
struct SurfaceVertex {
    std::array<float, 3> world_position{};
    std::array<float, 3> normal{0.f, 0.f, 1.f};
};
static_assert(sizeof(SurfaceVertex) == 24 && offsetof(SurfaceVertex, normal) == 12);
static_assert(std::is_trivially_copyable_v<SurfaceVertex>);

// Shared light ABI. Occluder ranges are supported only by GroundForwardSlice;
// compatibility kernels still reject them rather than silently ignoring them.
struct alignas(16) LightGpu {
    std::array<float, 4> anchor_radius{0.f, 0.f, 1.f, 1.f};
    std::array<float, 4> direction_cone{0.f, 0.f, -1.f, -1.f};
    std::array<float, 4> color_intensity{1.f, 1.f, 1.f, 1.f};
    // First occluder, count, receiver mask, stable light id.
    std::array<std::uint32_t, 4> occluder_range_flags{0u, 0u, 1u, 0u};
    // inner cosine, radial falloff exponent, reveal, reserved.
    // Both cone cosines -1 select a point source.
    std::array<float, 4> height_falloff_reveal{-1.f, 2.f, 1.f, 0.f};
};
static_assert(std::is_standard_layout_v<LightGpu> && sizeof(LightGpu) == 80);
static_assert(offsetof(LightGpu, anchor_radius) == 0);
static_assert(offsetof(LightGpu, direction_cone) == 16);
static_assert(offsetof(LightGpu, color_intensity) == 32);
static_assert(offsetof(LightGpu, occluder_range_flags) == 48);
static_assert(offsetof(LightGpu, height_falloff_reveal) == 64);

inline constexpr std::uint32_t surface_light_capacity = 4;
inline constexpr std::uint32_t local_occluder_capacity = 256;
inline constexpr std::uint32_t local_occluders_per_light = 32;
// Larger than the shader's 1e-5 boundary inset; sub-contract prisms fail
// validation instead of being accepted and silently becoming non-blocking.
inline constexpr float local_occluder_min_dimension = 1e-4f;

enum class OccluderShape : std::uint32_t { Obb = 1, Capsule = 2 };

// Camera-relative vertical prism. The authored footprint is not inferred from
// the visual/caster raster. Owner ids exclude only the receiver's own prisms.
struct alignas(16) OccluderGpu {
    std::array<float, 4> center_base{};   // center XY, reserved zero, base Z
    std::array<float, 4> axis_x_extent{1.f, 0.f, 1.f, 1.f}; // axis XY, half length, top Z
    std::array<float, 4> axis_y_radius{0.f, 1.f, 1.f, 0.f}; // axis XY, half extent, radius
    std::array<std::uint32_t, 4> shape_flags_id{1u, 1u, 1u, 1u}; // shape, fully revealed, stable id, owner id
};
static_assert(std::is_standard_layout_v<OccluderGpu> && std::is_trivially_copyable_v<OccluderGpu>);
static_assert(sizeof(OccluderGpu) == 64 && alignof(OccluderGpu) == 16);
static_assert(offsetof(OccluderGpu, center_base) == 0);
static_assert(offsetof(OccluderGpu, axis_x_extent) == 16);
static_assert(offsetof(OccluderGpu, axis_y_radius) == 32);
static_assert(offsetof(OccluderGpu, shape_flags_id) == 48);

// Bounded lighting ABI: linear ambient, diffuse sun and point/spot lights.
// GroundForwardSlice adds local visibility; sky/contact shadows remain absent.
// Not the completed Dynamic world/material pipeline.
struct alignas(16) SurfaceLightingFrame {
    std::array<float, 4> ambient{0.1f, 0.1f, 0.1f, 0.f};
    std::array<float, 4> sun_direction{0.f, 0.f, 1.f, 0.f}; // toward source
    std::array<float, 4> sun_color_intensity{1.f, 1.f, 1.f, 0.f};
    std::array<std::uint32_t, 4> counts{}; // active local lights; remaining fields zero
    std::array<LightGpu, surface_light_capacity> lights{};
};
static_assert(std::is_standard_layout_v<SurfaceLightingFrame>);
static_assert(std::is_trivially_copyable_v<SurfaceLightingFrame>);
static_assert(sizeof(SurfaceLightingFrame) == 384);
static_assert(offsetof(SurfaceLightingFrame, ambient) == 0);
static_assert(offsetof(SurfaceLightingFrame, sun_direction) == 16);
static_assert(offsetof(SurfaceLightingFrame, sun_color_intensity) == 32);
static_assert(offsetof(SurfaceLightingFrame, counts) == 48);
static_assert(offsetof(SurfaceLightingFrame, lights) == 64);

} // namespace ribterra::render

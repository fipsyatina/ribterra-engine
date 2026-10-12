#pragma once

#include "ribterra/moon_lighting.hpp"
#include "ribterra/surface_lighting.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace ribterra::render {

inline constexpr std::size_t sky_shadow_triangle_capacity = 4096;
inline constexpr std::size_t sky_shadow_active_triangle_capacity = 1024;
inline constexpr float sky_shadow_coordinate_bound = 32.f;
inline constexpr float sky_shadow_min_direction_z = .3420201433f;
inline constexpr float sky_shadow_depth_expansion = .08f;
inline constexpr float sky_shadow_receiver_bias = .01f;
inline constexpr float sky_shadow_map_resolution = 1024.f;

struct SkyShadowVertex { std::array<float, 3> position{}; };
struct SkyShadowTriangle {
    std::array<SkyShadowVertex, 3> vertices{};
    std::uint32_t source_mask{3u};
    float reveal{1.f};
};
static_assert(sizeof(SkyShadowVertex) == 12);
static_assert(sizeof(SkyShadowTriangle) == 44);
static_assert(std::is_standard_layout_v<SkyShadowVertex> &&
              std::is_trivially_copyable_v<SkyShadowVertex>);
static_assert(std::is_standard_layout_v<SkyShadowTriangle> &&
              std::is_trivially_copyable_v<SkyShadowTriangle>);

struct alignas(16) ShadowSourceGpu {
    // Unit world basis in xyz, followed by the corresponding minimum bound.
    std::array<float, 4> u_min{};
    std::array<float, 4> v_min{};
    std::array<float, 4> f_min{};
    // Inverse U/V/depth spans and exact enabled flag.
    std::array<float, 4> inverse_span_enabled{};
    // Normalized receiver depth bias, shadow-map texel size, reserved zero.
    std::array<float, 4> bias_texel{};
};
static_assert(sizeof(ShadowSourceGpu) == 80 && alignof(ShadowSourceGpu) == 16);
static_assert(std::is_standard_layout_v<ShadowSourceGpu> &&
              std::is_trivially_copyable_v<ShadowSourceGpu>);
static_assert(offsetof(ShadowSourceGpu, u_min) == 0);
static_assert(offsetof(ShadowSourceGpu, v_min) == 16);
static_assert(offsetof(ShadowSourceGpu, f_min) == 32);
static_assert(offsetof(ShadowSourceGpu, inverse_span_enabled) == 48);
static_assert(offsetof(ShadowSourceGpu, bias_texel) == 64);

struct alignas(16) SkyShadowGpu { std::array<ShadowSourceGpu, 2> sources{}; };
static_assert(sizeof(SkyShadowGpu) == 160 && alignof(SkyShadowGpu) == 16);
static_assert(std::is_standard_layout_v<SkyShadowGpu> &&
              std::is_trivially_copyable_v<SkyShadowGpu>);

// SDL permits at most four uniform buffers per shader stage. The new material
// shadow variant uses one explicit b3 block, not five separate buffers or any
// reserved legacy fields. Ground retains independent Moon32 + Shadow160 blocks.
struct alignas(16) SkyMoonLightingGpu {
    MoonLightingGpu moon{};
    SkyShadowGpu shadows{};
};
static_assert(sizeof(SkyMoonLightingGpu) == 192 && alignof(SkyMoonLightingGpu) == 16);
static_assert(offsetof(SkyMoonLightingGpu, moon) == 0 && offsetof(SkyMoonLightingGpu, shadows) == 32);
static_assert(std::is_standard_layout_v<SkyMoonLightingGpu> && std::is_trivially_copyable_v<SkyMoonLightingGpu>);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // CPU owner embeds the intentionally aligned GPU block.
#endif
struct PreparedSkyShadows {
    SkyShadowGpu gpu{};
    std::vector<SkyShadowVertex> vertices;
    std::array<std::uint32_t, 2> first_vertex{};
    std::array<std::uint32_t, 2> vertex_count{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

[[nodiscard]] PreparedSkyShadows prepare_sky_shadows(
    std::span<const SkyShadowTriangle> triangles,
    const SurfaceLightingFrame& lighting,
    const MoonLightingGpu& moon);

} // namespace ribterra::render

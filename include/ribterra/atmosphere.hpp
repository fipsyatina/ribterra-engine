#pragma once

#include "ribterra/surface_lighting.hpp"
#include "ribterra/world_grid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace ribterra::render {

inline constexpr std::uint32_t atmosphere_water_capacity = 32;
// AnalyticAtmosphere V1: no fullscreen veil, no additional texture or UBO slot.
// Absolute XY is used only for noise and revealed-water proximity. Depth is
// camera-relative -(P.x+P.y); height is canonical render Z, not screen Y.
struct alignas(16) AtmosphereGpu {
    std::array<float, 4> origin_time{}; // camera XY, shown seconds, morning01
    std::array<float, 4> depth{-16.f, 16.f, .12f, .20f}; // near/far, optical, max opacity
    std::array<float, 4> density{.30f, .30f, 0.f, 1.25f}; // morning, coastal, base Z, height falloff
    std::array<float, 4> far_color_noise{.42f, .57f, .68f, .08f}; // linear RGB, noise amplitude
    std::array<float, 4> morning_color{.60f, .55f, .47f, 0.f};
    std::array<float, 4> coastal_color{.34f, .49f, .60f, 0.f};
    std::array<float, 4> wind_shore{1.f, 0.f, .035f, 3.f}; // unit/zero XY, tiles/sec, shore radius
    std::array<std::uint32_t, 4> counts_version{0u, 1u, 0u, 0u}; // patches, ABI, enabled, zero
    std::array<std::array<float, 4>, atmosphere_water_capacity> water_aabbs{}; // absolute minXY/maxXY
};
static_assert(sizeof(AtmosphereGpu) == 640 && alignof(AtmosphereGpu) == 16);
static_assert(std::is_standard_layout_v<AtmosphereGpu> && std::is_trivially_copyable_v<AtmosphereGpu>);
static_assert(offsetof(AtmosphereGpu, origin_time) == 0 && offsetof(AtmosphereGpu, depth) == 16);
static_assert(offsetof(AtmosphereGpu, density) == 32 && offsetof(AtmosphereGpu, far_color_noise) == 48);
static_assert(offsetof(AtmosphereGpu, morning_color) == 64 && offsetof(AtmosphereGpu, coastal_color) == 80);
static_assert(offsetof(AtmosphereGpu, wind_shore) == 96 && offsetof(AtmosphereGpu, counts_version) == 112);
static_assert(offsetof(AtmosphereGpu, water_aabbs) == 128);
struct alignas(16) SurfaceLightingAtmosphereGpu {
    SurfaceLightingFrame lighting;
    AtmosphereGpu atmosphere;
};
static_assert(sizeof(SurfaceLightingAtmosphereGpu) == 1024 && alignof(SurfaceLightingAtmosphereGpu) == 16);
static_assert(offsetof(SurfaceLightingAtmosphereGpu, atmosphere) == 384);
static_assert(std::is_standard_layout_v<SurfaceLightingAtmosphereGpu> &&
              std::is_trivially_copyable_v<SurfaceLightingAtmosphereGpu>);

inline void validate_atmosphere_gpu(const AtmosphereGpu& value) {
    const auto finite = [](const auto& fields) {
        return std::all_of(fields.begin(), fields.end(), [](float number) { return std::isfinite(number); });
    };
    if (!finite(value.origin_time) || !finite(value.depth) || !finite(value.density) ||
        !finite(value.far_color_noise) || !finite(value.morning_color) ||
        !finite(value.coastal_color) || !finite(value.wind_shore))
        throw std::invalid_argument("Atmosphere fields must be finite");
    constexpr float first = static_cast<float>(world_min) - .5f;
    constexpr float last = static_cast<float>(world_max) - .5f;
    if (value.origin_time[0] < first || value.origin_time[0] > last ||
        value.origin_time[1] < first || value.origin_time[1] > last ||
        value.origin_time[2] < 0.f || value.origin_time[2] > 1000000.f ||
        value.origin_time[3] < 0.f || value.origin_time[3] > 1.f ||
        value.depth[0] < -64.f || value.depth[1] > 64.f || value.depth[0] >= value.depth[1] ||
        value.depth[2] < 0.f || value.depth[2] > 1.f || value.depth[3] < 0.f || value.depth[3] > .20f ||
        value.density[0] < 0.f || value.density[0] > 1.f || value.density[1] < 0.f || value.density[1] > 1.f ||
        value.density[2] < -32.f || value.density[2] > 32.f || value.density[3] <= 0.f || value.density[3] > 8.f ||
        value.far_color_noise[3] < 0.f || value.far_color_noise[3] > .15f ||
        value.morning_color[3] != 0.f || value.coastal_color[3] != 0.f ||
        value.wind_shore[2] < 0.f || value.wind_shore[2] > .06f ||
        value.wind_shore[3] <= 0.f || value.wind_shore[3] > 8.f ||
        value.counts_version[0] > atmosphere_water_capacity || value.counts_version[1] != 1u ||
        value.counts_version[2] > 1u || value.counts_version[3] != 0u)
        throw std::invalid_argument("Atmosphere ranges, version or reserved fields are invalid");
    for (const auto* color : {&value.far_color_noise, &value.morning_color, &value.coastal_color})
        for (std::size_t channel = 0; channel < 3u; ++channel)
            if ((*color)[channel] < 0.f || (*color)[channel] > 1.f)
                throw std::invalid_argument("Atmosphere colors must be bounded linear RGB");
    const float wind_length = value.wind_shore[0] * value.wind_shore[0] +
                              value.wind_shore[1] * value.wind_shore[1];
    if (wind_length != 0.f && std::abs(wind_length - 1.f) > 1.e-4f)
        throw std::invalid_argument("Atmosphere wind must be normalized or exactly zero");
    for (std::size_t index = 0; index < value.water_aabbs.size(); ++index) {
        const auto& patch = value.water_aabbs[index];
        if (!finite(patch)) throw std::invalid_argument("Atmosphere water bounds must be finite");
        if (index >= value.counts_version[0]) {
            if (patch != std::array<float, 4>{})
                throw std::invalid_argument("Inactive atmosphere water bounds must be zero");
        } else if (patch[0] < first || patch[1] < first || patch[2] > last || patch[3] > last ||
                   patch[0] >= patch[2] || patch[1] >= patch[3])
            throw std::invalid_argument("Atmosphere water bounds must be nondegenerate and inside the world");
    }
}

} // namespace ribterra::render

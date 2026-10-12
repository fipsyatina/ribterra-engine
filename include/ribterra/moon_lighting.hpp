#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ribterra::render {

// Independent directional sky source. Direction points toward the source.
// Intensity zero disables the contribution but never disables ABI validation.
struct alignas(16) MoonLightingGpu {
    std::array<float, 4> direction{0.f, 0.f, 1.f, 0.f};
    // Stable diagnostic default only, not a final authored art or time curve.
    std::array<float, 4> color_intensity{.55f, .68f, 1.f, 0.f};
};
static_assert(sizeof(MoonLightingGpu) == 32 && alignof(MoonLightingGpu) == 16);
static_assert(std::is_standard_layout_v<MoonLightingGpu> &&
              std::is_trivially_copyable_v<MoonLightingGpu>);
static_assert(offsetof(MoonLightingGpu, direction) == 0);
static_assert(offsetof(MoonLightingGpu, color_intensity) == 16);

inline constexpr float moon_direction_length_squared_tolerance = .001f;
inline constexpr float moon_max_intensity = 64.f;

inline void validate_moon_lighting(const MoonLightingGpu& moon) {
    for (const float value : moon.direction)
        if (!std::isfinite(value))
            throw std::invalid_argument("Moon lighting direction must be finite");
    for (const float value : moon.color_intensity)
        if (!std::isfinite(value))
            throw std::invalid_argument("Moon lighting color and intensity must be finite");
    if (moon.direction[3] != 0.f)
        throw std::invalid_argument("Moon lighting direction reserved component must be zero");
    const float length_squared = moon.direction[0] * moon.direction[0] +
        moon.direction[1] * moon.direction[1] + moon.direction[2] * moon.direction[2];
    if (std::abs(length_squared - 1.f) > moon_direction_length_squared_tolerance)
        throw std::invalid_argument("Moon lighting direction must be normalized");
    for (std::size_t channel = 0; channel < 3; ++channel)
        if (moon.color_intensity[channel] < 0.f || moon.color_intensity[channel] > 1.f)
            throw std::invalid_argument("Moon lighting RGB must be in [0,1]");
    if (moon.color_intensity[3] < 0.f || moon.color_intensity[3] > moon_max_intensity)
        throw std::invalid_argument("Moon lighting intensity must be in [0,64]");
}

} // namespace ribterra::render

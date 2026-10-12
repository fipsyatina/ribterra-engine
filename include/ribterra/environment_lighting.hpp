#pragma once

#include "ribterra/moon_lighting.hpp"
#include "ribterra/seasons.hpp"
#include "ribterra/surface_lighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace ribterra {

// Match clock_time's 06:00 origin while retaining the shown clock's fraction.
inline float engine_time_hour(double shown_tick) {
    if (!std::isfinite(shown_tick) || shown_tick < 0.)
        throw std::invalid_argument("Engine shown tick must be finite and nonnegative");
    constexpr double day_ticks = static_cast<double>(ticks_per_day);
    constexpr double midnight_tick = day_ticks * .75;
    const double phase_tick = std::fmod(shown_tick, day_ticks);
    const double hour = phase_tick < midnight_tick
        ? 6. + phase_tick * (24. / day_ticks)
        : (phase_tick - midnight_tick) * (24. / day_ticks);
    // A pre-midnight double can round to 24 when converted to float. Keep it
    // before midnight; the exact midnight tick still takes the zero branch.
    return std::min(static_cast<float>(hour), std::nextafter(24.f, 0.f));
}

// Diagnostic presentation input only. It is not simulation, save, network or
// weather authority, and V1 deliberately leaves all local lights untouched.
struct EngineEnvironmentSettingsV1 {
    std::uint32_t version{1u};
    float time_hour{12.f};
    float cloud01{};
    float haze01{};
    bool atmosphere_enabled{true};
};

struct EngineEnvironmentLightingV1 {
    render::SurfaceLightingFrame lighting{};
    render::MoonLightingGpu moon{};
};

inline EngineEnvironmentLightingV1 project_engine_environment(
    const EngineEnvironmentSettingsV1& environment,
    const render::SurfaceLightingFrame& local_inputs) {
    if (environment.version != 1u || !std::isfinite(environment.time_hour) ||
        !std::isfinite(environment.cloud01) || !std::isfinite(environment.haze01) ||
        environment.time_hour < 0.f || environment.time_hour >= 24.f ||
        environment.cloud01 < 0.f || environment.cloud01 > 1.f ||
        environment.haze01 < 0.f || environment.haze01 > 1.f)
        throw std::invalid_argument("Engine environment V1 version, hour, cloud or haze is invalid");

    const auto smooth = [](float first, float last, float value) {
        const float t = std::clamp((value - first) / (last - first), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    constexpr float pi = 3.14159265358979323846f;
    constexpr float degrees_to_radians = pi / 180.f;
    const float solar_phase = pi * (environment.time_hour - 6.f) / 12.f;
    // Suppress the floating-point sin(pi) residue at sunrise/sunset: an
    // effectively zero source must not allocate an active shadow source.
    const float phase_height = std::sin(solar_phase);
    const float horizon_height = std::abs(phase_height) < 1.e-6f ? 0.f : phase_height;
    const float day_height = std::max(horizon_height, 0.f);
    const float night_height = std::max(-horizon_height, 0.f);
    const float sun_azimuth = (45.f + 180.f * (environment.time_hour - 6.f) / 12.f) *
                              degrees_to_radians;
    const float moon_azimuth = sun_azimuth + pi;
    const float sun_elevation = std::lerp(20.f, 65.f, day_height) * degrees_to_radians;
    const float moon_elevation = std::lerp(20.f, 55.f, night_height) * degrees_to_radians;
    const auto direction = [](float azimuth, float elevation) {
        const float horizontal = std::cos(elevation);
        return std::array<float, 4>{horizontal * std::cos(azimuth),
                                    horizontal * std::sin(azimuth),
                                    std::sin(elevation), 0.f};
    };

    EngineEnvironmentLightingV1 result;
    result.lighting = local_inputs; // Preserve counts and every local-light byte.
    result.lighting.sun_direction = direction(sun_azimuth, sun_elevation);
    const float day_gate = smooth(0.f, .2f, day_height);
    const float sun_intensity = .9f * day_gate * (1.f - .75f * environment.cloud01);
    const float sun_color_mix = smooth(0.f, .5f, day_height);
    constexpr std::array<float, 3> warm_sun{.95f, .55f, .30f};
    constexpr std::array<float, 3> high_sun{1.f, .95f, .82f};
    for (std::size_t channel = 0; channel < 3u; ++channel)
        result.lighting.sun_color_intensity[channel] =
            std::lerp(warm_sun[channel], high_sun[channel], sun_color_mix);
    result.lighting.sun_color_intensity[3] = sun_intensity;

    const float daylight = smooth(5.f, 9.f, environment.time_hour) *
                           (1.f - smooth(16.f, 21.f, environment.time_hour));
    constexpr std::array<float, 3> night_ambient{.025f, .04f, .07f};
    constexpr std::array<float, 3> day_ambient{.22f, .24f, .25f};
    const float clouded_day = 1.f - .45f * environment.cloud01;
    for (std::size_t channel = 0; channel < 3u; ++channel)
        result.lighting.ambient[channel] = std::lerp(
            night_ambient[channel], day_ambient[channel] * clouded_day, daylight);
    result.lighting.ambient[3] = 0.f;

    result.moon.direction = direction(moon_azimuth, moon_elevation);
    result.moon.color_intensity = {
        .55f, .68f, 1.f,
        .18f * smooth(0.f, .2f, night_height) * (1.f - .85f * environment.cloud01)};
    render::validate_moon_lighting(result.moon);
    return result;
}

} // namespace ribterra

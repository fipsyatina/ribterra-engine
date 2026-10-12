#pragma once

#include "ribterra/environment_lighting.hpp"
#include "ribterra/weather.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ribterra {

inline constexpr std::uint32_t engine_weather_projection_version = 1u;

struct EngineWeatherProjectionV1 {
    std::uint32_t version{engine_weather_projection_version};
    std::uint64_t source_simulation_tick{}, source_calendar_tick{};
    double shown_calendar_tick{};
    EngineEnvironmentSettingsV1 environment{};
    WeatherKind kind{};
    PrecipitationKind precipitation{};
    std::int16_t temperature_deci_c{};
    WindDirection wind_direction{};
    float precipitation01{}, wind01{}, wetness01{}, snow_cover01{};
};

// Project the validated host sample; no client schedule, RNG or accumulated
// weather history is created. Only the displayed hour uses the shown clock.
inline EngineWeatherProjectionV1 project_engine_weather(
    const WeatherView& view, std::uint64_t simulation_tick,
    double shown_simulation_tick, bool atmosphere_enabled = true) {
    if (!view.known)
        throw std::invalid_argument("Engine weather projection requires known authoritative weather");
    validate_weather_view(view, simulation_tick);
    if (!std::isfinite(shown_simulation_tick) || shown_simulation_tick < 0.)
        throw std::invalid_argument("Engine weather shown simulation tick must be finite and nonnegative");

    const double shown_calendar_tick = shown_simulation_tick +
        static_cast<double>(view.calendar_offset_ticks);
    // A valid current day leaves room for all four forecast cards. This exact
    // integer bound is below 2^53, so its double conversion is lossless. Reject
    // a rounded value on the exclusive boundary before the hour projection.
    constexpr std::uint64_t maximum_current_day =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - forecast_days;
    constexpr double calendar_end_exclusive =
        static_cast<double>(maximum_current_day * ticks_per_day);
    if (!std::isfinite(shown_calendar_tick) || shown_calendar_tick < 0. ||
        shown_calendar_tick >= calendar_end_exclusive)
        throw std::invalid_argument("Engine weather shown calendar is outside the supported horizon");

    // Candidate visual tuning V1 for Clear/Cloudy/Precipitation/Storm. These
    // values describe environmental appearance, not simulation balance or
    // accepted reference visuals. Local lights and emission are not inputs.
    constexpr std::array<float, 4> cloud{0.f, .60f, .85f, 1.f};
    constexpr std::array<float, 4> haze{.05f, .12f, .18f, .35f};
    const auto kind_index = static_cast<std::size_t>(view.current.kind);

    EngineWeatherProjectionV1 result;
    result.source_simulation_tick = simulation_tick;
    result.source_calendar_tick = view.calendar_tick;
    result.shown_calendar_tick = shown_calendar_tick;
    result.environment = {1u, engine_time_hour(shown_calendar_tick),
        cloud[kind_index], haze[kind_index], atmosphere_enabled};
    result.kind = view.current.kind;
    result.precipitation = view.current.precipitation;
    result.temperature_deci_c = view.current.temperature_deci_c;
    result.wind_direction = view.current.wind_direction;
    result.precipitation01 = static_cast<float>(view.current.precipitation_permille) / 1000.f;
    result.wind01 = static_cast<float>(view.current.wind_permille) / 1000.f;
    result.snow_cover01 = static_cast<float>(view.snow_cover_permille) / 1000.f;
    // The signed fraction is authoritative wetness within the current integer
    // permille. It smooths presentation without integrating a client history.
    const double wetness = (static_cast<double>(view.wetness_permille) +
        static_cast<double>(view.wetness_remainder) / 28800.) / 1000.;
    result.wetness01 = static_cast<float>(std::clamp(wetness, 0., 1.));
    // Snow exposure is data only; it does not deliver snow materials or FX.
    return result;
}

} // namespace ribterra

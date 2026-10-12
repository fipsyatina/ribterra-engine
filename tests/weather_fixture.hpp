#pragma once

#include "ribterra/world_state.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace ribterra::test {

// Explicit test fixture time travel: keep accumulated cover and work state,
// replace only the canonical current sample and its evaluated tick. This does
// not simulate the skipped ticks or claim their historical accumulation.
inline void set_fixture_simulation_tick(WorldState &state, std::uint64_t new_tick) {
    const WeatherContext context{state.seed, {state.climate.algorithm_version,
        state.climate.climate_profile, state.climate.scenario}};
    const auto calendar = make_calendar_tick(new_tick, state.climate.calendar_offset_ticks);
    if (!calendar)
        throw std::invalid_argument("Fixture calendar addition overflow");
    WeatherScheduleCache cache(context, *calendar);
    auto climate = state.climate;
    climate.evaluated_simulation_tick = new_tick;
    climate.current = cache.sample(*calendar);
    validate_climate_runtime(climate, new_tick, context, cache);
    state.climate = std::move(climate);
    state.ticks = new_tick;
}

// Synthetic transport fixture, with the scenario's initial cover retained.
// Clients still validate and consume WeatherView; they do not run this helper.
template <class SnapshotLike>
inline void bind_fixture_weather(SnapshotLike &snapshot, std::uint32_t seed,
                                 ClimateConfig config, std::uint64_t tick) {
    const WeatherContext context{seed, config};
    const auto offset = scenario_calendar_offset(config.scenario);
    WeatherScheduleCache cache(context, CalendarTick{offset});
    auto climate = initialize_climate(context, cache);
    const auto calendar_tick = make_calendar_tick(tick, offset);
    if (!calendar_tick)
        throw std::invalid_argument("Fixture calendar addition overflow");
    cache.move_to(*calendar_tick);
    climate.evaluated_simulation_tick = tick;
    climate.current = cache.sample(*calendar_tick);
    validate_climate_runtime(climate, tick, context, cache);
    const auto weather = cache.view(climate);
    const auto calendar = calendar_view(*calendar_tick);
    snapshot.tick = tick;
    snapshot.weather = weather;
    snapshot.day = calendar.day;
    snapshot.winter = calendar.season == Season::Winter;
}

} // namespace ribterra::test

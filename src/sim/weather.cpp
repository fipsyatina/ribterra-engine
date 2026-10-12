#include "ribterra/weather.hpp"
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace ribterra {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}
constexpr std::uint64_t transition_ticks = 600;
constexpr std::array<int, 4> seasonal_means{80,180,70,-100};
constexpr std::array<int, 4> precipitation_chances{350,200,400,500};
constexpr std::array<int, 4> storm_chances{100,100,150,200};
constexpr std::array<int, 4> clear_chances{550,700,450,350};
bool valid_kind(WeatherKind k) { return k >= WeatherKind::Clear && k <= WeatherKind::Storm; }
bool valid_phase(PrecipitationKind p) { return p >= PrecipitationKind::None && p <= PrecipitationKind::Snow; }
std::uint64_t day_start(std::uint32_t day) {
    require(day != 0u, "Weather calendar day is zero");
    return static_cast<std::uint64_t>(day - 1u) * ticks_per_day;
}
std::uint32_t horizon_day(CalendarTick tick) {
    const auto day = calendar_day(tick);
    require(day <= std::numeric_limits<std::uint32_t>::max() - 4u,
            "Weather four-day horizon exceeds uint32");
    return day;
}
std::int64_t lerp_integer(std::int64_t a, std::int64_t b,
                          std::uint64_t elapsed, std::uint64_t span) {
    return a + (b - a) * static_cast<std::int64_t>(elapsed) / static_cast<std::int64_t>(span);
}
int anomaly_target(WeatherKind kind) {
    switch (kind) {
    case WeatherKind::Clear: return 10;
    case WeatherKind::Cloudy: return 0;
    case WeatherKind::Precipitation: return -20;
    case WeatherKind::Storm: return -60;
    }
    throw std::invalid_argument("Weather kind is invalid");
}
std::uint64_t bounded(const WeatherContext& context, std::uint32_t day,
                      WeatherPurpose purpose, std::uint64_t bound, std::uint64_t index = 0) {
    return weather_key(context, day, purpose, index) % bound;
}
std::size_t season_index(std::uint32_t day) {
    return static_cast<std::size_t>(((day - 1u) / days_per_season) % 4u);
}
struct Event {
    std::uint32_t day{};
    std::uint64_t begin{}, end{};
    WeatherKind kind{WeatherKind::Precipitation};
    std::uint16_t intensity{};
    bool forced{};
};
int event_priority(const Event& e) { return e.forced ? 3 : e.kind == WeatherKind::Storm ? 2 : 1; }
bool outranks(const Event& a, const Event& b) {
    if (event_priority(a) != event_priority(b)) return event_priority(a) > event_priority(b);
    if (a.begin != b.begin) return a.begin > b.begin;
    return a.day > b.day;
}
std::optional<Event> event_for(const WeatherContext& context, std::uint32_t day) {
    const auto first = scenario_calendar_offset(context.config.scenario) / ticks_per_day + 1u;
    if (day == 0u || day < first) return std::nullopt;
    const bool forced = context.config.scenario == ClimateScenarioId::FirstWinterStorm && day == first;
    const auto season = season_index(day);
    if (!forced && bounded(context, day, WeatherPurpose::EventPresence, 1000u) >=
                   static_cast<std::uint64_t>(precipitation_chances[season])) return std::nullopt;
    const bool storm = forced || bounded(context, day, WeatherPurpose::EventStorm, 1000u) <
                                static_cast<std::uint64_t>(storm_chances[season]);
    const std::uint64_t index = forced ? 5u : 4u;
    // Generic hours are civil 02:00..20:00, converted into the 06:00-origin day.
    const auto civil = 2u * weather_hour_ticks +
        bounded(context, day, WeatherPurpose::EventStart, 18u * weather_hour_ticks + 1u, index);
    const auto start = forced ? 8u * weather_hour_ticks +
        bounded(context, day, WeatherPurpose::FirstStormStart, 4u * weather_hour_ticks + 1u, index)
        : (civil + 18u * weather_hour_ticks) % ticks_per_day;
    const auto duration = forced ? 4u * weather_hour_ticks +
        bounded(context, day, WeatherPurpose::FirstStormDuration, 2u * weather_hour_ticks + 1u, index)
        : (storm ? 4u : 2u) * weather_hour_ticks +
          bounded(context, day, WeatherPurpose::EventDuration, 2u * weather_hour_ticks + 1u, index);
    const auto intensity = (storm ? 800u : 400u) +
        bounded(context, day, WeatherPurpose::EventIntensity, storm ? 201u : 351u, index);
    return Event{day, day_start(day) + start, day_start(day) + start + duration,
                 storm ? WeatherKind::Storm : WeatherKind::Precipitation,
                 static_cast<std::uint16_t>(intensity), forced};
}
std::uint16_t wind_for(const WeatherContext& context, std::uint32_t day,
                       WeatherKind kind, std::uint64_t index) {
    constexpr std::array<std::uint64_t, 4> low{100,200,350,700}, high{300,450,650,1000};
    const auto k = static_cast<std::size_t>(kind);
    return static_cast<std::uint16_t>(low[k] +
        bounded(context, day, WeatherPurpose::WindStrength, high[k] - low[k] + 1u, index));
}
WindDirection direction_for(const WeatherContext& context, std::uint32_t day, std::uint64_t index) {
    return static_cast<WindDirection>(bounded(context, day, WeatherPurpose::WindDirection, 8u, index));
}
void validate_cover(std::uint16_t value, std::int64_t remainder) {
    require(value <= 1000u && remainder > -static_cast<std::int64_t>(ticks_per_day) &&
            remainder < static_cast<std::int64_t>(ticks_per_day), "Weather cover/remainder is invalid");
    require((value != 0u || remainder >= 0) && (value != 1000u || remainder <= 0),
            "Weather cover has an outward remainder");
}
std::pair<int, int> cover_rates(const WeatherSample& sample) {
    require(valid_kind(sample.kind) && valid_phase(sample.precipitation) &&
            sample.temperature_deci_c >= -800 && sample.temperature_deci_c <= 600 &&
            sample.precipitation_permille <= 1000u && sample.wind_permille <= 1000u,
            "Weather cover sample fields are invalid");
    const bool event = sample.kind == WeatherKind::Precipitation || sample.kind == WeatherKind::Storm;
    require(event == (sample.precipitation != PrecipitationKind::None) &&
            event == (sample.precipitation_permille != 0u), "Weather precipitation presence is inconsistent");
    require(!event || ((sample.precipitation == PrecipitationKind::Snow) == (sample.temperature_deci_c <= 0)),
            "Weather precipitation temperature is inconsistent");
    const int snowfall = sample.precipitation == PrecipitationKind::Snow
        ? 100 + static_cast<int>(sample.precipitation_permille) * 400 / 1000 : 0;
    const int melt = sample.temperature_deci_c > 0
        ? std::min(800, 40 + static_cast<int>(sample.temperature_deci_c) * 8) : 0;
    const int rain_bonus = sample.precipitation == PrecipitationKind::Rain ? 120 : 0;
    const int gain = sample.precipitation == PrecipitationKind::Rain
        ? 600 + static_cast<int>(sample.precipitation_permille) * 600 / 1000 : 0;
    const int drying = (sample.temperature_deci_c > 0 ? 120 : 30) +
                       static_cast<int>(sample.wind_permille) * 120 / 1000;
    return {std::clamp(snowfall - melt - rain_bonus, -1000, 1000),
            std::clamp(gain - drying, -1000, 1200)};
}
std::pair<std::uint16_t, std::int64_t> integrate_cover(
    std::uint16_t value, std::int64_t remainder, int rate) {
    const auto accumulated = remainder + static_cast<std::int64_t>(rate);
    const auto delta = accumulated / static_cast<std::int64_t>(ticks_per_day);
    auto next_remainder = accumulated - delta * static_cast<std::int64_t>(ticks_per_day);
    const auto next_value = std::clamp(static_cast<std::int64_t>(value) + delta, std::int64_t{0}, std::int64_t{1000});
    if ((next_value == 0 && (rate < 0 || delta < 0)) ||
        (next_value == 1000 && (rate > 0 || delta > 0))) next_remainder = 0;
    return {static_cast<std::uint16_t>(next_value), next_remainder};
}
void validate_sample(const WeatherSample& sample, CalendarTick tick) {
    (void)cover_rates(sample);
    require(sample.wind_direction >= WindDirection::N && sample.wind_direction <= WindDirection::NW,
            "Weather wind direction is invalid");
    require(sample.period_start_calendar_tick < sample.period_end_calendar_tick &&
            sample.period_start_calendar_tick <= tick.value && tick.value < sample.period_end_calendar_tick,
            "Weather current period is invalid");
    const auto work = weather_work_modifier(sample.kind, sample.precipitation, sample.temperature_deci_c);
    require(sample.outdoor_work_rate_bp == work.rate_bp && sample.work_reason == work.reason,
            "Weather work modifier is inconsistent");
}
void validate_runtime_shape(const ClimateRuntimeState& state) {
    validate_cover(state.snow_cover_permille, state.snow_cover_remainder);
    validate_cover(state.wetness_permille, state.wetness_remainder);
    const auto rates = cover_rates(state.current);
    require(!(state.snow_cover_permille == 0u && rates.first < 0) || state.snow_cover_remainder == 0,
            "Snow lower-bound outward remainder must be zero");
    require(!(state.snow_cover_permille == 1000u && rates.first > 0) || state.snow_cover_remainder == 0,
            "Snow upper-bound outward remainder must be zero");
    require(!(state.wetness_permille == 0u && rates.second < 0) || state.wetness_remainder == 0,
            "Wetness lower-bound outward remainder must be zero");
    require(!(state.wetness_permille == 1000u && rates.second > 0) || state.wetness_remainder == 0,
            "Wetness upper-bound outward remainder must be zero");
    require(state.outdoor_work_remainders.size() <= max_outdoor_work_records,
            "Weather outdoor work record count exceeds the bound");
    int previous = 0;
    for (const auto& record : state.outdoor_work_remainders) {
        // Existing Job ordinals 2..5 are Woodcut/Mine/Forage/Build. The opaque
        // declaration keeps this domain independent of World; World must also
        // validate that the referenced resident and live Activity key match.
        const int job = static_cast<int>(record.job);
        require(record.resident_id > previous && record.remainder_bp < 10000u &&
                job >= 2 && job <= 5 && record.target.layer == WorldLayer::Surface &&
                inside_world(record.target.x, record.target.y) &&
                (job == 5 ? record.building_id > 0 : record.building_id == 0),
                "Weather outdoor work records must be bounded and sorted");
        previous = record.resident_id;
    }
}
} // namespace

std::uint64_t weather_mix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
void validate_weather_context(const WeatherContext& context) {
    require(context.config.algorithm_version == weather_algorithm_version &&
            context.config.climate_profile == ClimateProfileId::TemperateValley,
            "Weather algorithm or climate profile is invalid");
    (void)scenario_calendar_offset(context.config.scenario);
}
std::uint64_t scenario_calendar_offset(ClimateScenarioId scenario) {
    switch (scenario) {
    case ClimateScenarioId::Standard: return 0;
    case ClimateScenarioId::FirstWinterStorm: return 30u * ticks_per_day;
    }
    throw std::invalid_argument("Weather climate scenario is invalid");
}
std::uint64_t weather_key(const WeatherContext& context, std::uint32_t day,
                         WeatherPurpose purpose, std::uint64_t index) {
    validate_weather_context(context);
    require(day != 0u && purpose >= WeatherPurpose::BackgroundKind &&
            purpose <= WeatherPurpose::FirstStormDuration, "Weather key day/purpose is invalid");
    std::uint64_t h = 0x5249425745415448ULL;
    for (auto word : {static_cast<std::uint64_t>(context.seed),
                     static_cast<std::uint64_t>(context.config.climate_profile),
                     static_cast<std::uint64_t>(context.config.algorithm_version),
                     static_cast<std::uint64_t>(context.config.scenario),
                     static_cast<std::uint64_t>(day), static_cast<std::uint64_t>(purpose), index})
        h = weather_mix64(h ^ weather_mix64(word));
    return h;
}
WeatherWorkModifier weather_work_modifier(WeatherKind kind, PrecipitationKind phase,
                                         std::int16_t temperature) {
    require(valid_kind(kind) && valid_phase(phase) && temperature >= -800 && temperature <= 600,
            "Weather work inputs are invalid");
    const bool event = kind == WeatherKind::Precipitation || kind == WeatherKind::Storm;
    require(event == (phase != PrecipitationKind::None) &&
            (!event || ((phase == PrecipitationKind::Snow) == (temperature <= 0))),
            "Weather work precipitation is inconsistent");
    const int weather = !event ? 10000 : kind == WeatherKind::Storm
        ? (phase == PrecipitationKind::Snow ? 7500 : 8000)
        : (phase == PrecipitationKind::Snow ? 9000 : 9500);
    const int cold = temperature >= -100 ? 10000 : temperature > -200 ? 9500 : 8500;
    const auto rate = static_cast<std::uint16_t>(std::max(7500, std::min(weather, cold)));
    const auto reason = rate == 10000u ? WeatherWorkReason::None : cold < weather
        ? WeatherWorkReason::Cold : kind == WeatherKind::Storm ? WeatherWorkReason::Storm
        : phase == PrecipitationKind::Snow ? WeatherWorkReason::Snow : WeatherWorkReason::Rain;
    return {rate, reason};
}
bool outdoor_work_step(std::uint16_t& remainder, std::uint16_t rate) {
    require(remainder < 10000u && rate >= 7500u && rate <= 10000u, "Weather work remainder/rate is invalid");
    const auto total = static_cast<std::uint32_t>(remainder) + rate;
    const bool advance = total >= 10000u;
    remainder = static_cast<std::uint16_t>(total - (advance ? 10000u : 0u));
    return advance;
}
WeatherCoverStep weather_cover_step(const WeatherCoverStep& previous, const WeatherSample& sample) {
    validate_cover(previous.snow_cover_permille, previous.snow_cover_remainder);
    validate_cover(previous.wetness_permille, previous.wetness_remainder);
    const auto rates = cover_rates(sample);
    const auto snow = integrate_cover(previous.snow_cover_permille, previous.snow_cover_remainder, rates.first);
    const auto wet = integrate_cover(previous.wetness_permille, previous.wetness_remainder, rates.second);
    return {snow.first, wet.first, snow.second, wet.second};
}

WeatherScheduleCache::WeatherScheduleCache(WeatherContext context, CalendarTick tick) : context_(context) {
    validate_weather_context(context_);
    require(tick.value >= scenario_calendar_offset(context_.config.scenario), "Weather tick precedes scenario start");
    current_day_ = horizon_day(tick);
    for (std::size_t i = 0; i < schedules_.size(); ++i)
        schedules_[i] = build_day(current_day_ - 1u + static_cast<std::uint32_t>(i));
    for (std::size_t i = 0; i < forecasts_.size(); ++i)
        forecasts_[i] = build_forecast(current_day_ + static_cast<std::uint32_t>(i));
}
WeatherScheduleCache::DaySchedule WeatherScheduleCache::build_day(std::uint32_t day) const {
    DaySchedule result;
    result.day = day;
    const auto barrier = scenario_calendar_offset(context_.config.scenario);
    if (day == 0u || day_start(day) < barrier) return result;
    const auto begin = day_start(day), end = begin + ticks_per_day;
    std::array<Period, 4> background{};
    std::size_t background_count{};
    for (std::size_t slot = 0; slot < background.size(); ++slot) {
        const auto kind = bounded(context_, day, WeatherPurpose::BackgroundKind, 1000u, slot) <
            static_cast<std::uint64_t>(clear_chances[season_index(day)]) ? WeatherKind::Clear : WeatherKind::Cloudy;
        const auto start = begin + slot * (ticks_per_day / 4u);
        if (background_count && background[background_count - 1u].kind == kind) {
            background[background_count - 1u].end = start + ticks_per_day / 4u;
            background[background_count - 1u].metadata_end = start + ticks_per_day / 4u;
        } else {
            background[background_count++] = {start, start + ticks_per_day / 4u, start, start + ticks_per_day / 4u,
                kind, 0u, wind_for(context_, day, kind, slot), direction_for(context_, day, slot)};
        }
    }
    std::array<std::optional<Event>, 3> events{
        event_for(context_, day - 1u), event_for(context_, day),
        day == std::numeric_limits<std::uint32_t>::max() ? std::nullopt : event_for(context_, day + 1u)};
    // Merge connected generic events before clipping to a calendar day. A
    // <=7200-tick event can connect only two adjacent daily events: its late
    // carryover and the next event's early start. Every affected day therefore
    // sees the same union and retains the earlier event's wind key/metadata.
    for (std::size_t a = 0; a < events.size(); ++a) for (std::size_t b = a + 1u; b < events.size(); ++b) {
        if (!events[a] || !events[b] || events[a]->forced || events[b]->forced ||
            events[a]->kind != events[b]->kind || events[a]->intensity != events[b]->intensity ||
            events[a]->end < events[b]->begin || events[b]->end < events[a]->begin) continue;
        events[a]->begin = std::min(events[a]->begin, events[b]->begin);
        events[a]->end = std::max(events[a]->end, events[b]->end);
        events[b].reset();
    }
    std::array<std::uint64_t, 16> boundaries{};
    std::size_t count{};
    boundaries[count++] = begin; boundaries[count++] = end;
    for (std::size_t i = 0; i < background_count; ++i) {
        boundaries[count++] = background[i].begin; boundaries[count++] = background[i].end;
    }
    for (const auto& e : events) if (e && e->begin < end && e->end > begin) {
        boundaries[count++] = std::max(begin, e->begin);
        boundaries[count++] = std::min(end, e->end);
    }
    std::sort(boundaries.begin(), boundaries.begin() + static_cast<std::ptrdiff_t>(count));
    count = static_cast<std::size_t>(std::unique(boundaries.begin(), boundaries.begin() +
        static_cast<std::ptrdiff_t>(count)) - boundaries.begin());
    for (std::size_t i = 0; i + 1u < count; ++i) {
        const auto start = boundaries[i], stop = boundaries[i + 1u];
        Period row;
        for (std::size_t b = 0; b < background_count; ++b)
            if (background[b].begin <= start && start < background[b].end) row = background[b];
        const Event* winner{};
        for (const auto& e : events) if (e && e->begin <= start && start < e->end &&
            (!winner || outranks(*e, *winner))) winner = &*e;
        if (winner) {
            const auto index = winner->forced ? 5u : 4u;
            row = {start, stop, winner->begin, winner->end, winner->kind, winner->intensity,
                wind_for(context_, winner->day, winner->kind, index), direction_for(context_, winner->day, index)};
        }
        row.begin = start; row.end = stop;
        for (const auto& e : events) if (e && (!winner || outranks(*e, *winner))) {
            if (e->end <= start && e->end > row.metadata_begin) row.metadata_begin = e->end;
            if (e->begin > start && e->begin < row.metadata_end) row.metadata_end = e->begin;
        }
        if (result.count && result.periods[result.count - 1u].kind == row.kind &&
            result.periods[result.count - 1u].intensity == row.intensity) {
            // Candidate convention: merged equal-kind/intensity periods retain
            // the first period's wind. Forecast kind-only merging is separate.
            auto& last = result.periods[result.count - 1u];
            last.end = stop;
            last.metadata_end = std::max(last.metadata_end, row.metadata_end);
        } else {
            if (result.count >= 7u) throw std::logic_error("Weather generation exceeded seven periods");
            result.periods[result.count++] = row;
        }
    }
    return result;
}
const WeatherScheduleCache::DaySchedule& WeatherScheduleCache::day_schedule(CalendarTick tick) const {
    require(tick.value >= scenario_calendar_offset(context_.config.scenario), "Weather sample precedes scenario start");
    const auto day = calendar_day(tick);
    for (const auto& schedule : schedules_) if (schedule.day == day && schedule.count) return schedule;
    throw std::invalid_argument("Weather sample is outside the cache window");
}
std::int16_t WeatherScheduleCache::weather_anomaly(CalendarTick tick) const {
    const auto& current = day_schedule(tick);
    DaySchedule storage;
    const DaySchedule* previous{};
    if (day_start(current.day) > scenario_calendar_offset(context_.config.scenario)) {
        for (const auto& cached : schedules_) if (cached.day == current.day - 1u && cached.count) previous = &cached;
        if (!previous) { storage = build_day(current.day - 1u); previous = &storage; }
    }
    const auto& first = previous ? *previous : current;
    std::int64_t start_value = anomaly_target(first.periods[0].kind), target = start_value;
    auto transition_start = first.periods[0].begin;
    const auto consume = [&](const DaySchedule& schedule) {
        for (std::size_t i = 0; i < schedule.count; ++i) {
            const auto& period = schedule.periods[i];
            if (period.begin > tick.value) break;
            const auto next_target = anomaly_target(period.kind);
            if (next_target == target) continue;
            const auto achieved = lerp_integer(start_value, target,
                std::min(period.begin - transition_start, transition_ticks), transition_ticks);
            start_value = achieved; target = next_target; transition_start = period.begin;
        }
    };
    // A full previous day has <=7 periods over28800ticks, so at least one
    // >=600tick span settles any earlier transition. No older history is needed.
    if (previous) consume(*previous);
    consume(current);
    return static_cast<std::int16_t>(lerp_integer(start_value, target,
        std::min(tick.value - transition_start, transition_ticks), transition_ticks));
}
std::int16_t WeatherScheduleCache::temperature_at(CalendarTick tick) const {
    const auto& schedule = day_schedule(tick);
    require(schedule.day < std::numeric_limits<std::uint32_t>::max(), "Weather daily anomaly horizon is invalid");
    const auto seasonal_phase = (tick.value % (40u * ticks_per_day) + 5u * ticks_per_day) % (40u * ticks_per_day);
    const auto next_season = static_cast<std::size_t>(seasonal_phase / (10u * ticks_per_day));
    const auto seasonal = lerp_integer(seasonal_means[(next_season + 3u) % 4u], seasonal_means[next_season],
        seasonal_phase % (10u * ticks_per_day), 10u * ticks_per_day);
    const auto daily_value = [&](std::uint32_t day) {
        return static_cast<std::int64_t>(bounded(context_, day, WeatherPurpose::DailyAnomaly, 81u)) - 40;
    };
    const auto in_day = tick.value % ticks_per_day;
    const auto daily = lerp_integer(daily_value(schedule.day), daily_value(schedule.day + 1u), in_day, ticks_per_day);
    const auto civil_hour = static_cast<std::size_t>((in_day / weather_hour_ticks + 6u) % 24u);
    const auto diurnal = lerp_integer(weather_diurnal_offsets[civil_hour], weather_diurnal_offsets[(civil_hour + 1u) % 24u],
        in_day % weather_hour_ticks, weather_hour_ticks);
    return static_cast<std::int16_t>(std::clamp(seasonal + daily + diurnal + weather_anomaly(tick),
        std::int64_t{-800}, std::int64_t{600}));
}
WeatherSample WeatherScheduleCache::sample(CalendarTick tick) const {
    const auto& schedule = day_schedule(tick);
    for (std::size_t i = 0; i < schedule.count; ++i) {
        const auto& period = schedule.periods[i];
        if (period.begin <= tick.value && tick.value < period.end) {
            const auto temperature = temperature_at(tick);
            const auto phase = period.intensity == 0u ? PrecipitationKind::None :
                temperature <= 0 ? PrecipitationKind::Snow : PrecipitationKind::Rain;
            const auto work = weather_work_modifier(period.kind, phase, temperature);
            return {period.kind, phase, period.direction, work.reason, temperature, period.intensity,
                    period.wind, work.rate_bp, period.metadata_begin, period.metadata_end};
        }
    }
    throw std::logic_error("Weather day schedule has a gap");
}
WeatherDayForecast WeatherScheduleCache::build_forecast(std::uint32_t day) const {
    WeatherDayForecast result;
    result.calendar_day = day;
    result.minimum_temperature_deci_c = 600;
    result.maximum_temperature_deci_c = -800;
    std::array<std::uint32_t, 4> duration{};
    bool rain{}, snow{};
    const auto start = day_start(day);
    // Exact integer extrema/phase summary, not an endpoint approximation.
    for (std::uint32_t offset = 0; offset < ticks_per_day; ++offset) {
        const auto current = sample(CalendarTick{start + offset});
        result.minimum_temperature_deci_c = std::min(result.minimum_temperature_deci_c, current.temperature_deci_c);
        result.maximum_temperature_deci_c = std::max(result.maximum_temperature_deci_c, current.temperature_deci_c);
        ++duration[static_cast<std::size_t>(current.kind)];
        rain |= current.precipitation == PrecipitationKind::Rain;
        snow |= current.precipitation == PrecipitationKind::Snow;
        result.has_storm |= current.kind == WeatherKind::Storm;
        if (!result.interval_count || result.intervals[result.interval_count - 1u].kind != current.kind) {
            if (result.interval_count >= 7u) throw std::logic_error("Weather forecast exceeded seven kind intervals");
            result.intervals[result.interval_count++] = {offset, offset + 1u, current.kind};
        } else result.intervals[result.interval_count - 1u].end_tick_in_day = offset + 1u;
    }
    std::size_t dominant{};
    for (std::size_t k = 1; k < duration.size(); ++k) if (duration[k] >= duration[dominant]) dominant = k;
    result.dominant_kind = static_cast<WeatherKind>(dominant);
    result.precipitation_summary = rain && snow ? PrecipitationSummary::Mixed :
        rain ? PrecipitationSummary::Rain : snow ? PrecipitationSummary::Snow : PrecipitationSummary::None;
    return result;
}
void WeatherScheduleCache::move_to(CalendarTick tick) {
    require(tick.value >= scenario_calendar_offset(context_.config.scenario), "Weather cache tick precedes scenario start");
    const auto day = horizon_day(tick);
    if (day == current_day_) return;
    if (day != current_day_ + 1u) {
        WeatherScheduleCache candidate(context_, tick);
        *this = candidate;
        return;
    }
    auto candidate = *this;
    candidate.current_day_ = day;
    for (std::size_t i = 0; i + 1u < candidate.schedules_.size(); ++i) candidate.schedules_[i] = schedules_[i + 1u];
    candidate.schedules_.back() = candidate.build_day(day + 4u);
    for (std::size_t i = 0; i + 1u < candidate.forecasts_.size(); ++i) candidate.forecasts_[i] = forecasts_[i + 1u];
    candidate.forecasts_.back() = candidate.build_forecast(day + 3u);
    *this = candidate;
}
WeatherView WeatherScheduleCache::view(const ClimateRuntimeState& state) const {
    validate_climate_runtime(state, state.evaluated_simulation_tick, context_, *this);
    const auto tick = *make_calendar_tick(state.evaluated_simulation_tick, state.calendar_offset_ticks);
    require(calendar_day(tick) == current_day_, "Weather view requires the current cache day");
    WeatherView result;
    result.known = true; result.algorithm_version = state.algorithm_version;
    result.climate_profile = state.climate_profile; result.scenario = state.scenario;
    result.calendar_offset_ticks = state.calendar_offset_ticks;
    result.evaluated_simulation_tick = state.evaluated_simulation_tick;
    result.calendar_tick = tick.value; result.current = state.current;
    result.snow_cover_permille = state.snow_cover_permille;
    result.wetness_permille = state.wetness_permille; result.wetness_remainder = state.wetness_remainder;
    result.forecast = forecasts_;
    const auto horizon_end = day_start(current_day_) + forecast_days * ticks_per_day;
    if (state.current.kind == WeatherKind::Storm) {
        result.next_storm_start_calendar_tick = state.current.period_start_calendar_tick;
        result.next_storm_end_calendar_tick = state.current.period_end_calendar_tick;
    } else {
        for (const auto& schedule : schedules_) {
            if (schedule.day < current_day_ || schedule.day > current_day_ + 3u) continue;
            for (std::size_t i = 0; i < schedule.count; ++i) {
                const auto& period = schedule.periods[i];
                if (period.kind == WeatherKind::Storm && period.metadata_begin > tick.value &&
                    period.metadata_end <= horizon_end && (!result.next_storm_start_calendar_tick ||
                    period.metadata_begin < result.next_storm_start_calendar_tick)) {
                    result.next_storm_start_calendar_tick = period.metadata_begin;
                    result.next_storm_end_calendar_tick = period.metadata_end;
                }
            }
        }
    }
    validate_weather_view(result, state.evaluated_simulation_tick);
    return result;
}
ClimateRuntimeState initialize_climate(const WeatherContext& context, const WeatherScheduleCache& cache) {
    validate_weather_context(context);
    require(cache.context() == context, "Weather initialization context differs from cache");
    ClimateRuntimeState result;
    result.algorithm_version = context.config.algorithm_version;
    result.climate_profile = context.config.climate_profile; result.scenario = context.config.scenario;
    result.calendar_offset_ticks = scenario_calendar_offset(context.config.scenario);
    require(cache.current_day() == calendar_day(CalendarTick{result.calendar_offset_ticks}),
            "Weather initialization requires the scenario start cache");
    result.current = cache.sample(CalendarTick{result.calendar_offset_ticks});
    result.snow_cover_permille = context.config.scenario == ClimateScenarioId::FirstWinterStorm ? 650u : 0u;
    validate_climate_runtime(result, 0, context, cache);
    return result;
}
void validate_climate_runtime(const ClimateRuntimeState& state, std::uint64_t tick,
                              const WeatherContext& context, const WeatherScheduleCache& cache) {
    validate_weather_context(context);
    require(cache.context() == context && state.algorithm_version == context.config.algorithm_version &&
            state.climate_profile == context.config.climate_profile && state.scenario == context.config.scenario &&
            state.calendar_offset_ticks == scenario_calendar_offset(state.scenario) &&
            state.evaluated_simulation_tick == tick, "Weather runtime context/tick is inconsistent");
    const auto calendar = make_calendar_tick(tick, state.calendar_offset_ticks);
    require(calendar.has_value(), "Weather runtime calendar addition overflow");
    (void)horizon_day(*calendar);
    validate_sample(state.current, *calendar);
    require(state.current == cache.sample(*calendar), "Weather runtime sample differs from canonical schedule");
    validate_runtime_shape(state);
}
void advance_climate_tick(ClimateRuntimeState& state, std::uint64_t tick, WeatherScheduleCache& cache) {
    require(state.evaluated_simulation_tick != std::numeric_limits<std::uint64_t>::max() &&
            tick == state.evaluated_simulation_tick + 1u, "Weather advance requires exactly one simulation tick");
    validate_climate_runtime(state, state.evaluated_simulation_tick, cache.context(), cache);
    const auto calendar = make_calendar_tick(tick, state.calendar_offset_ticks);
    require(calendar.has_value(), "Weather advance calendar overflow");
    (void)horizon_day(*calendar);
    cache.move_to(*calendar);
    const auto current = cache.sample(*calendar);
    const auto cover = weather_cover_step({state.snow_cover_permille, state.wetness_permille,
        state.snow_cover_remainder, state.wetness_remainder}, current);
    state.current = current; state.evaluated_simulation_tick = tick;
    state.snow_cover_permille = cover.snow_cover_permille; state.snow_cover_remainder = cover.snow_cover_remainder;
    state.wetness_permille = cover.wetness_permille; state.wetness_remainder = cover.wetness_remainder;
}
void validate_weather_view(const WeatherView& view, std::uint64_t tick) {
    require(view.known, "Unknown weather cannot enter a wire snapshot");
    const WeatherContext context{0u, {view.algorithm_version, view.climate_profile, view.scenario}};
    validate_weather_context(context);
    require(view.evaluated_simulation_tick == tick &&
            view.calendar_offset_ticks == scenario_calendar_offset(view.scenario), "Weather wire tick/offset differs");
    const auto calendar = make_calendar_tick(tick, view.calendar_offset_ticks);
    require(calendar && calendar->value == view.calendar_tick, "Weather wire calendar is invalid");
    const auto day = horizon_day(*calendar);
    const auto horizon_start = day_start(day), horizon_end = horizon_start + forecast_days * ticks_per_day;
    validate_sample(view.current, *calendar);
    require(view.current.period_start_calendar_tick >= view.calendar_offset_ticks &&
            view.current.period_end_calendar_tick <= horizon_end,
            "Weather current period exceeds the scenario/horizon bounds");
    require(view.snow_cover_permille <= 1000u, "Weather wire snow cover is invalid");
    validate_cover(view.wetness_permille, view.wetness_remainder);
    const auto rates = cover_rates(view.current);
    require(!(view.wetness_permille == 0u && rates.second < 0) || view.wetness_remainder == 0,
            "Weather wire wetness outward remainder must be zero");
    require(!(view.wetness_permille == 1000u && rates.second > 0) || view.wetness_remainder == 0,
            "Weather wire wetness upper remainder must be zero");
    for (std::size_t i = 0; i < view.forecast.size(); ++i) {
        const auto& forecast = view.forecast[i];
        require(forecast.calendar_day == day + i && forecast.minimum_temperature_deci_c >= -800 &&
                forecast.maximum_temperature_deci_c <= 600 &&
                forecast.minimum_temperature_deci_c <= forecast.maximum_temperature_deci_c &&
                valid_kind(forecast.dominant_kind) && forecast.precipitation_summary >= PrecipitationSummary::None &&
                forecast.precipitation_summary <= PrecipitationSummary::Mixed && forecast.interval_count >= 1u &&
                forecast.interval_count <= max_forecast_intervals_per_day, "Weather forecast fields are invalid");
        std::array<std::uint32_t, 4> duration{};
        std::uint32_t next{};
        bool storm{}, precipitation{};
        for (std::size_t p = 0; p < forecast.intervals.size(); ++p) {
            const auto& interval = forecast.intervals[p];
            if (p >= forecast.interval_count) {
                require(interval == WeatherIntervalView{}, "Inactive weather interval is not default");
                continue;
            }
            require(valid_kind(interval.kind) && interval.start_tick_in_day == next &&
                    interval.start_tick_in_day < interval.end_tick_in_day && interval.end_tick_in_day <= ticks_per_day,
                    "Weather intervals must cover the day without gaps or overlap");
            require(p == 0u || forecast.intervals[p - 1u].kind != interval.kind,
                    "Adjacent identical weather intervals must be merged");
            duration[static_cast<std::size_t>(interval.kind)] += interval.end_tick_in_day - interval.start_tick_in_day;
            next = interval.end_tick_in_day; storm |= interval.kind == WeatherKind::Storm;
            precipitation |= interval.kind == WeatherKind::Precipitation || interval.kind == WeatherKind::Storm;
        }
        std::size_t dominant{};
        for (std::size_t k = 1; k < duration.size(); ++k) if (duration[k] >= duration[dominant]) dominant = k;
        require(next == ticks_per_day && forecast.has_storm == storm &&
                forecast.dominant_kind == static_cast<WeatherKind>(dominant) &&
                precipitation == (forecast.precipitation_summary != PrecipitationSummary::None),
                "Weather forecast summary/dominant kind is inconsistent");
    }
    const auto& today = view.forecast[0];
    const auto in_day = view.calendar_tick % ticks_per_day;
    bool current_matches{};
    for (std::size_t p = 0; p < today.interval_count; ++p) {
        const auto& interval = today.intervals[p];
        if (interval.start_tick_in_day <= in_day && in_day < interval.end_tick_in_day)
            current_matches = interval.kind == view.current.kind;
    }
    require(current_matches && view.current.temperature_deci_c >= today.minimum_temperature_deci_c &&
            view.current.temperature_deci_c <= today.maximum_temperature_deci_c,
            "Weather current sample differs from today's forecast");
    require(view.current.precipitation == PrecipitationKind::None || today.precipitation_summary == PrecipitationSummary::Mixed ||
            (view.current.precipitation == PrecipitationKind::Rain && today.precipitation_summary == PrecipitationSummary::Rain) ||
            (view.current.precipitation == PrecipitationKind::Snow && today.precipitation_summary == PrecipitationSummary::Snow),
            "Weather current precipitation differs from forecast summary");
    const auto start = view.next_storm_start_calendar_tick, end = view.next_storm_end_calendar_tick;
    std::uint64_t first_future_start{}, first_future_end{};
    for (const auto& forecast : view.forecast) for (std::size_t p = 0; p < forecast.interval_count; ++p) {
        const auto& interval = forecast.intervals[p];
        const auto a = day_start(forecast.calendar_day) + interval.start_tick_in_day;
        const auto b = day_start(forecast.calendar_day) + interval.end_tick_in_day;
        if (interval.kind != WeatherKind::Storm || b <= view.calendar_tick) continue;
        if (!first_future_start && a > view.calendar_tick) {
            first_future_start = a; first_future_end = b;
        } else if (first_future_start && a == first_future_end) first_future_end = b;
    }
    if (start == 0u && end == 0u) {
        require(view.current.kind != WeatherKind::Storm, "Active storm requires a storm pair");
        require(!first_future_start || first_future_end == horizon_end,
                "A fully contained future storm requires a storm pair");
        return;
    }
    require(start < end && end <= horizon_end, "Weather next storm pair is invalid");
    const bool active = start <= view.calendar_tick && view.calendar_tick < end;
    require(active ? view.current.kind == WeatherKind::Storm && start == view.current.period_start_calendar_tick &&
                end == view.current.period_end_calendar_tick
        : view.current.kind != WeatherKind::Storm && start > view.calendar_tick && start >= horizon_start &&
          start == first_future_start && end <= first_future_end, "Weather storm pair mode is inconsistent");
    // Future kind-only forecast intervals cannot prove the intensity-period end
    // within an adjacent Storm run. Host validation checks the exact schedule;
    // this seedless wire validator checks earliest start and complete coverage.
    auto cursor = std::max(start, horizon_start);
    for (const auto& forecast : view.forecast) for (std::size_t p = 0; p < forecast.interval_count; ++p) {
        const auto& interval = forecast.intervals[p];
        const auto a = day_start(forecast.calendar_day) + interval.start_tick_in_day;
        const auto b = day_start(forecast.calendar_day) + interval.end_tick_in_day;
        if (a <= cursor && cursor < b && cursor < end) {
            require(interval.kind == WeatherKind::Storm, "Storm pair crosses a non-storm forecast interval");
            cursor = std::min(b, end);
        }
    }
    require(cursor == end, "Storm pair is outside the forecast horizon");
}

} // namespace ribterra

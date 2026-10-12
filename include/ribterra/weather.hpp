#pragma once

#include "ribterra/seasons.hpp"
#include "ribterra/world_grid.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ribterra {

// The existing definition in world.hpp has this exact underlying type. Keeping
// only the declaration here prevents weather/domain -> World include cycles.
enum class Job : int;
enum class ClimateProfileId : std::uint16_t { TemperateValley = 1 };
enum class ClimateScenarioId : std::uint16_t { Standard = 0, FirstWinterStorm = 1 };
enum class WeatherKind : std::uint8_t { Clear, Cloudy, Precipitation, Storm };
enum class PrecipitationKind : std::uint8_t { None, Rain, Snow };
enum class PrecipitationSummary : std::uint8_t { None, Rain, Snow, Mixed };
enum class WindDirection : std::uint8_t { N, NE, E, SE, S, SW, W, NW };
enum class WeatherWorkReason : std::uint8_t { None, Cold, Rain, Snow, Storm };
enum class WeatherPurpose : std::uint64_t {
    BackgroundKind = 1, DailyAnomaly, EventPresence, EventStorm, EventStart,
    EventDuration, EventIntensity, WindDirection, WindStrength,
    FirstStormStart, FirstStormDuration
};
inline constexpr std::uint16_t weather_algorithm_version = 1;
inline constexpr std::size_t forecast_days = 4;
inline constexpr std::size_t max_forecast_intervals_per_day = 8;
inline constexpr std::size_t weather_schedule_days = 6;
inline constexpr std::size_t max_outdoor_work_records = 6000;
inline constexpr std::uint64_t weather_hour_ticks = ticks_per_day / 24;
// New candidate V1 defaults: these exact offsets and mixer tags were not fixed
// by the earlier specification. Tuning/certified seeds require separate gates.
inline constexpr std::array<std::int16_t, 24> weather_diurnal_offsets{
    -25,-30,-35,-40,-45,-50,-50,-40,-25,-10,5,20,
    30,40,45,50,45,35,20,5,-5,-10,-15,-20};

struct ClimateConfig {
    std::uint16_t algorithm_version{weather_algorithm_version};
    ClimateProfileId climate_profile{ClimateProfileId::TemperateValley};
    ClimateScenarioId scenario{ClimateScenarioId::Standard};
    bool operator==(const ClimateConfig&) const = default;
};
struct WeatherContext {
    std::uint32_t seed{};
    ClimateConfig config;
    bool operator==(const WeatherContext&) const = default;
};
struct WeatherSample {
    WeatherKind kind{WeatherKind::Clear};
    PrecipitationKind precipitation{PrecipitationKind::None};
    WindDirection wind_direction{WindDirection::N};
    WeatherWorkReason work_reason{WeatherWorkReason::None};
    std::int16_t temperature_deci_c{};
    std::uint16_t precipitation_permille{}, wind_permille{};
    std::uint16_t outdoor_work_rate_bp{10000};
    std::uint64_t period_start_calendar_tick{}, period_end_calendar_tick{};
    bool operator==(const WeatherSample&) const = default;
};
struct WeatherIntervalView {
    std::uint32_t start_tick_in_day{}, end_tick_in_day{};
    WeatherKind kind{WeatherKind::Clear};
    bool operator==(const WeatherIntervalView&) const = default;
};
struct WeatherDayForecast {
    std::uint32_t calendar_day{};
    std::int16_t minimum_temperature_deci_c{}, maximum_temperature_deci_c{};
    WeatherKind dominant_kind{WeatherKind::Clear};
    PrecipitationSummary precipitation_summary{PrecipitationSummary::None};
    bool has_storm{};
    std::array<WeatherIntervalView, max_forecast_intervals_per_day> intervals{};
    std::uint8_t interval_count{};
    bool operator==(const WeatherDayForecast&) const = default;
};
struct OutdoorWorkRemainderState {
    int resident_id{};
    Job job{}; // Existing value 0 is Job::Idle; World validates affected jobs.
    CellCoord target;
    int building_id{};
    std::uint16_t remainder_bp{};
    bool operator==(const OutdoorWorkRemainderState&) const = default;
};
struct ClimateRuntimeState {
    std::uint16_t algorithm_version{weather_algorithm_version};
    ClimateProfileId climate_profile{ClimateProfileId::TemperateValley};
    ClimateScenarioId scenario{ClimateScenarioId::Standard};
    std::uint64_t calendar_offset_ticks{}, evaluated_simulation_tick{};
    WeatherSample current;
    std::uint16_t snow_cover_permille{};
    std::int64_t snow_cover_remainder{};
    std::uint16_t wetness_permille{};
    std::int64_t wetness_remainder{};
    // Bounded sorted import/export records. Future World owns live Activity keys;
    // no per-tick copy or forecast/cache is serialized in this representation.
    std::vector<OutdoorWorkRemainderState> outdoor_work_remainders;
    bool operator==(const ClimateRuntimeState&) const = default;
};
struct WeatherView {
    bool known{};
    std::uint16_t algorithm_version{};
    ClimateProfileId climate_profile{ClimateProfileId::TemperateValley};
    ClimateScenarioId scenario{ClimateScenarioId::Standard};
    std::uint64_t calendar_offset_ticks{}, evaluated_simulation_tick{}, calendar_tick{};
    WeatherSample current;
    std::uint16_t snow_cover_permille{}, wetness_permille{};
    // Required by the newer V04 host-wetness contract. Snow/work remainders are
    // runtime/save fields only; clients never integrate this wire remainder.
    std::int64_t wetness_remainder{};
    std::uint64_t next_storm_start_calendar_tick{}, next_storm_end_calendar_tick{};
    std::array<WeatherDayForecast, forecast_days> forecast{};
    bool operator==(const WeatherView&) const = default;
};
struct WeatherWorkModifier {
    std::uint16_t rate_bp{10000};
    WeatherWorkReason reason{WeatherWorkReason::None};
    bool operator==(const WeatherWorkModifier&) const = default;
};
struct WeatherCoverStep {
    std::uint16_t snow_cover_permille{}, wetness_permille{};
    std::int64_t snow_cover_remainder{}, wetness_remainder{};
    bool operator==(const WeatherCoverStep&) const = default;
};

std::uint64_t weather_mix64(std::uint64_t value) noexcept;
std::uint64_t weather_key(const WeatherContext&, std::uint32_t calendar_day,
                         WeatherPurpose, std::uint64_t period_index = 0);
std::uint64_t scenario_calendar_offset(ClimateScenarioId);
void validate_weather_context(const WeatherContext&);
WeatherWorkModifier weather_work_modifier(WeatherKind, PrecipitationKind,
                                         std::int16_t temperature_deci_c);
WeatherCoverStep weather_cover_step(const WeatherCoverStep&, const WeatherSample&);
bool outdoor_work_step(std::uint16_t& remainder_bp, std::uint16_t rate_bp);

class WeatherScheduleCache {
  public:
    explicit WeatherScheduleCache(WeatherContext, CalendarTick current);
    const WeatherContext& context() const noexcept { return context_; }
    std::uint32_t current_day() const noexcept { return current_day_; }
    void move_to(CalendarTick);
    WeatherSample sample(CalendarTick) const;
    std::int16_t temperature_at(CalendarTick) const;
    WeatherView view(const ClimateRuntimeState&) const;
  private:
    struct Period {
        std::uint64_t begin{}, end{}, metadata_begin{}, metadata_end{};
        WeatherKind kind{WeatherKind::Clear};
        std::uint16_t intensity{}, wind{};
        WindDirection direction{WindDirection::N};
    };
    struct DaySchedule {
        std::uint32_t day{};
        std::array<Period, max_forecast_intervals_per_day> periods{};
        std::uint8_t count{};
    };
    WeatherContext context_;
    std::uint32_t current_day_{};
    std::array<DaySchedule, weather_schedule_days> schedules_{};
    std::array<WeatherDayForecast, forecast_days> forecasts_{};
    DaySchedule build_day(std::uint32_t) const;
    WeatherDayForecast build_forecast(std::uint32_t) const;
    const DaySchedule& day_schedule(CalendarTick) const;
    std::int16_t weather_anomaly(CalendarTick) const;
};

ClimateRuntimeState initialize_climate(const WeatherContext&, const WeatherScheduleCache&);
void advance_climate_tick(ClimateRuntimeState&, std::uint64_t simulation_tick,
                         WeatherScheduleCache&);
void validate_climate_runtime(const ClimateRuntimeState&, std::uint64_t simulation_tick,
                              const WeatherContext&, const WeatherScheduleCache&);
// Structural host-wire validation, without seedless client schedule generation.
void validate_weather_view(const WeatherView&, std::uint64_t snapshot_tick);

} // namespace ribterra

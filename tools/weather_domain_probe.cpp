#include "ribterra/weather.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace allocation_probe {
bool enabled{};
std::uint64_t count{}, bytes{};
void record(std::size_t size) noexcept {
    if (enabled) { ++count; bytes += static_cast<std::uint64_t>(size); }
}
void* ordinary(std::size_t size) {
    if (void* p = std::malloc(size ? size : 1u)) { record(size); return p; }
    throw std::bad_alloc{};
}
void* aligned(std::size_t size, std::size_t alignment) {
#ifdef _MSC_VER
    void* p = _aligned_malloc(size ? size : 1u, alignment);
#else
    const auto rounded = ((size ? size : 1u) + alignment - 1u) / alignment * alignment;
    void* p = std::aligned_alloc(alignment, rounded);
#endif
    if (!p) throw std::bad_alloc{};
    record(size); return p;
}
void free_aligned(void* p) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}
void* operator new(std::size_t size) { return allocation_probe::ordinary(size); }
void* operator new[](std::size_t size) { return allocation_probe::ordinary(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_probe::aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocation_probe::aligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }

namespace {
using namespace ribterra;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class F> void invalid(Json& controls, const char* id, F&& action) {
    bool rejected{};
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, id);
    controls.push_back({{"id",id},{"rejected",true}});
}
std::uint64_t day_begin(std::uint32_t day) { return static_cast<std::uint64_t>(day - 1u) * ticks_per_day; }
double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
Json sample_json(const WeatherSample& s) {
    return {{"kind",static_cast<unsigned>(s.kind)}, {"precipitation",static_cast<unsigned>(s.precipitation)},
        {"temperature_deci_c",s.temperature_deci_c},{"intensity_permille",s.precipitation_permille},
        {"wind_direction",static_cast<unsigned>(s.wind_direction)}, {"wind_permille",s.wind_permille},
        {"work_rate_bp",s.outdoor_work_rate_bp},{"work_reason",static_cast<unsigned>(s.work_reason)},
        {"period_start",s.period_start_calendar_tick},{"period_end",s.period_end_calendar_tick}};
}
Json cover_json(const WeatherCoverStep& s) {
    return {{"snow",s.snow_cover_permille},{"snow_remainder",s.snow_cover_remainder},
        {"wetness",s.wetness_permille},{"wetness_remainder",s.wetness_remainder}};
}
Json forecast_json(const WeatherDayForecast& f) {
    auto intervals = Json::array();
    for (std::size_t p = 0; p < f.interval_count; ++p) intervals.push_back({
        {"start",f.intervals[p].start_tick_in_day},{"end",f.intervals[p].end_tick_in_day},
        {"kind",static_cast<unsigned>(f.intervals[p].kind)}});
    return {{"day",f.calendar_day},{"minimum_deci_c",f.minimum_temperature_deci_c},
        {"maximum_deci_c",f.maximum_temperature_deci_c},{"dominant_kind",static_cast<unsigned>(f.dominant_kind)},
        {"precipitation_summary",static_cast<unsigned>(f.precipitation_summary)},
        {"has_storm",f.has_storm},{"interval_count",f.interval_count},{"intervals",intervals}};
}
WeatherSample fixed_sample(WeatherKind kind, PrecipitationKind phase, std::int16_t temperature,
                           std::uint16_t intensity, std::uint16_t wind = 0) {
    WeatherSample result;
    result.kind = kind; result.precipitation = phase; result.temperature_deci_c = temperature;
    result.precipitation_permille = intensity; result.wind_permille = wind;
    const auto work = weather_work_modifier(kind, phase, temperature);
    result.outdoor_work_rate_bp = work.rate_bp; result.work_reason = work.reason;
    result.period_end_calendar_tick = ticks_per_day;
    return result;
}
WeatherCoverStep integrate(WeatherCoverStep value, const WeatherSample& sample, std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) value = weather_cover_step(value, sample);
    return value;
}
WeatherView synthetic_view(bool active) {
    WeatherView v;
    v.known = true; v.algorithm_version = weather_algorithm_version;
    v.current = fixed_sample(WeatherKind::Clear, PrecipitationKind::None, 10, 0);
    v.current.period_end_calendar_tick = 1000;
    for (std::size_t i = 0; i < v.forecast.size(); ++i) {
        auto& f = v.forecast[i]; f.calendar_day = static_cast<std::uint32_t>(i) + 1u;
        f.minimum_temperature_deci_c = -100; f.maximum_temperature_deci_c = 100;
        f.interval_count = 1; f.intervals[0] = {0,static_cast<std::uint32_t>(ticks_per_day),WeatherKind::Clear};
    }
    auto& today = v.forecast[0]; today.interval_count = 3;
    today.intervals[0] = {0,1000,WeatherKind::Clear};
    today.intervals[1] = {1000,3000,WeatherKind::Storm};
    today.intervals[2] = {3000,static_cast<std::uint32_t>(ticks_per_day),WeatherKind::Clear};
    today.has_storm = true; today.precipitation_summary = PrecipitationSummary::Rain;
    v.next_storm_start_calendar_tick = 1000; v.next_storm_end_calendar_tick = active ? 2000u : 3000u;
    if (active) {
        v.calendar_tick = 1500; v.evaluated_simulation_tick = 1500;
        v.current = fixed_sample(WeatherKind::Storm, PrecipitationKind::Rain, 10, 800, 700);
        v.current.period_start_calendar_tick = 1000; v.current.period_end_calendar_tick = 2000;
    }
    return v;
}
Json calendar_controls(Json& negatives) {
    auto result = Json::array();
    constexpr std::array<std::uint64_t, 5> ticks{0,7200,14400,21600,28800};
    constexpr std::array<std::uint16_t, 5> minutes{360,720,1080,0,360};
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        const auto c = calendar_view(CalendarTick{ticks[i]});
        check(c.clock_minute == minutes[i] && c.day == (i == 4u ? 2u : 1u), "CAL-06-origin");
        result.push_back({{"simulation_tick",ticks[i]},{"day",c.day},{"clock_minute",c.clock_minute},
                          {"season",static_cast<unsigned>(c.season)}});
    }
    const auto offset = scenario_calendar_offset(ClimateScenarioId::FirstWinterStorm);
    const auto winter = calendar_view(*make_calendar_tick(0,offset));
    check(offset == 864000u && winter.day == 31u && winter.season == Season::Winter && winter.clock_minute == 360u,
          "CAL-winter-offset");
    result.push_back({{"simulation_tick",0},{"calendar_tick",offset},{"day",winter.day},{"clock_minute",winter.clock_minute}});
    check(!make_calendar_tick(std::numeric_limits<std::uint64_t>::max(),1), "CAL-add-overflow");
    negatives.push_back({{"id","CAL-add-overflow"},{"rejected",true}});
    invalid(negatives,"CAL-day-overflow",[]{ (void)calendar_day(CalendarTick{std::numeric_limits<std::uint64_t>::max()}); });
    invalid(negatives,"CAL-forecast-horizon",[]{
        WeatherScheduleCache c({70421u,{}},CalendarTick{day_begin(std::numeric_limits<std::uint32_t>::max() - 3u)});
        (void)c;
    });
    return result;
}
Json cover_controls(Json& negatives) {
    const auto snow = fixed_sample(WeatherKind::Precipitation,PrecipitationKind::Snow,-50,1000);
    const auto rain = fixed_sample(WeatherKind::Precipitation,PrecipitationKind::Rain,60,1000);
    const auto dry = fixed_sample(WeatherKind::Clear,PrecipitationKind::None,100,0);
    const auto snow_day = integrate({},snow,ticks_per_day);
    check(snow_day == WeatherCoverStep{500,0,0,0}, "COVER-snow-500-per-day");
    const auto rain_day = integrate({650,0,0,0},rain,ticks_per_day);
    check(rain_day == WeatherCoverStep{10,1000,0,0}, "COVER-rain-snow640-wet1080-clamp");
    const auto dry_day = integrate({900,900,0,0},dry,ticks_per_day);
    check(dry_day == WeatherCoverStep{100,780,0,0}, "COVER-dry-melt800-dry120");
    const auto signed_step = weather_cover_step({500,500,0,0},dry);
    check(signed_step.snow_cover_remainder == -800 && signed_step.wetness_remainder == -120,
          "COVER-signed-negative-remainders");
    const auto half = integrate({500,200,7,-9},snow,12345);
    const auto resumed = integrate(half,snow,ticks_per_day - 12345u);
    check(resumed == integrate({500,200,7,-9},snow,ticks_per_day), "COVER-value-copy-continuation");
    check(weather_cover_step({0,0,0,0},dry) == WeatherCoverStep{}, "COVER-lower-clamp-no-debt");
    check(weather_cover_step({1000,0,0,0},snow).snow_cover_remainder == 0, "COVER-upper-clamp-no-debt");
    invalid(negatives,"COVER-range",[&]{ (void)weather_cover_step({1001,0,0,0},snow); });
    invalid(negatives,"COVER-positive-remainder-bound",[&]{ (void)weather_cover_step({1,1,28800,0},snow); });
    invalid(negatives,"COVER-negative-remainder-bound",[&]{ (void)weather_cover_step({1,1,-28800,0},snow); });
    invalid(negatives,"COVER-lower-outward",[&]{ (void)weather_cover_step({0,1,-1,0},snow); });
    invalid(negatives,"COVER-upper-outward",[&]{ (void)weather_cover_step({1000,1,1,0},snow); });
    invalid(negatives,"COVER-phase-temperature",[&]{ auto s=snow; s.temperature_deci_c=1; (void)weather_cover_step({},s); });
    invalid(negatives,"COVER-missing-intensity",[&]{ auto s=snow; s.precipitation_permille=0; (void)weather_cover_step({},s); });
    return {{"snow_day",cover_json(snow_day)},{"rain_day",cover_json(rain_day)},
        {"dry_day",cover_json(dry_day)},{"signed_step",cover_json(signed_step)},
        {"domain_value_copy_continuation",cover_json(resumed)}, {"actual_save_roundtrip",false}};
}
Json work_controls(Json& negatives) {
    auto result = Json::array();
    constexpr std::array<std::uint16_t, 6> rates{10000,9500,9000,8500,8000,7500};
    for (auto rate : rates) {
        std::uint16_t remainder{}; unsigned steps{};
        for (unsigned i = 0; i < 100; ++i) steps += outdoor_work_step(remainder,rate) ? 1u : 0u;
        check(steps == rate / 100u && remainder == 0u, "WORK-fixed-point-100-ticks");
        std::uint16_t split{}; unsigned split_steps{};
        for (unsigned i = 0; i < 37; ++i) split_steps += outdoor_work_step(split,rate) ? 1u : 0u;
        auto copied = split;
        for (unsigned i = 37; i < 100; ++i) split_steps += outdoor_work_step(copied,rate) ? 1u : 0u;
        check(split_steps == steps && copied == remainder, "WORK-remainder-value-copy");
        result.push_back({{"rate_bp",rate},{"ticks",100},{"steps",steps},{"midpoint_remainder",split},
                          {"final_remainder",remainder},{"domain_value_copy_continuation",true}});
    }
    check(weather_work_modifier(WeatherKind::Clear,PrecipitationKind::None,-100) == WeatherWorkModifier{}, "WORK-cold-minus10");
    check(weather_work_modifier(WeatherKind::Clear,PrecipitationKind::None,-101) == WeatherWorkModifier{9500,WeatherWorkReason::Cold}, "WORK-cold-below10");
    check(weather_work_modifier(WeatherKind::Clear,PrecipitationKind::None,-200) == WeatherWorkModifier{8500,WeatherWorkReason::Cold}, "WORK-cold-minus20");
    check(weather_work_modifier(WeatherKind::Precipitation,PrecipitationKind::Rain,1) == WeatherWorkModifier{9500,WeatherWorkReason::Rain}, "WORK-rain");
    check(weather_work_modifier(WeatherKind::Storm,PrecipitationKind::Snow,-300) == WeatherWorkModifier{7500,WeatherWorkReason::Storm}, "WORK-storm-priority");
    invalid(negatives,"WORK-rate-low",[]{ std::uint16_t r=123; try { (void)outdoor_work_step(r,7499); } catch (...) { check(r==123,"WORK-invalid-mutated"); throw; } });
    invalid(negatives,"WORK-rate-high",[]{ std::uint16_t r{}; (void)outdoor_work_step(r,10001); });
    invalid(negatives,"WORK-remainder",[]{ std::uint16_t r=10000; (void)outdoor_work_step(r,7500); });
    invalid(negatives,"WORK-kind",[]{ (void)weather_work_modifier(static_cast<WeatherKind>(255),PrecipitationKind::None,0); });
    return result;
}
void wire_controls(Json& negatives) {
    const auto future = synthetic_view(false), active = synthetic_view(true);
    validate_weather_view(future,0); validate_weather_view(active,1500);
    auto reject = [&](const char* id, auto change) {
        invalid(negatives,id,[&]{ auto bad=future; change(bad); validate_weather_view(bad,bad.evaluated_simulation_tick); });
    };
    reject("WIRE-unknown",[](auto& v){v.known=false;});
    reject("WIRE-version",[](auto& v){v.algorithm_version=0;});
    reject("WIRE-profile",[](auto& v){v.climate_profile=static_cast<ClimateProfileId>(0);});
    reject("WIRE-scenario",[](auto& v){v.scenario=static_cast<ClimateScenarioId>(2);});
    reject("WIRE-offset",[](auto& v){v.calendar_offset_ticks=1;});
    reject("WIRE-calendar",[](auto& v){v.calendar_tick=1;});
    invalid(negatives,"WIRE-snapshot-tick",[&]{validate_weather_view(future,1);});
    reject("WIRE-current-kind",[](auto& v){v.current.kind=static_cast<WeatherKind>(255);});
    reject("WIRE-wind-direction",[](auto& v){v.current.wind_direction=static_cast<WindDirection>(8);});
    reject("WIRE-wind-strength",[](auto& v){v.current.wind_permille=1001;});
    reject("WIRE-temperature",[](auto& v){v.current.temperature_deci_c=601;});
    reject("WIRE-work-rate",[](auto& v){v.current.outdoor_work_rate_bp=9999;});
    reject("WIRE-period",[](auto& v){v.current.period_end_calendar_tick=0;});
    reject("WIRE-period-after-horizon",[](auto& v){v.current.period_end_calendar_tick=forecast_days*ticks_per_day+1u;});
    reject("WIRE-snow-range",[](auto& v){v.snow_cover_permille=1001;});
    reject("WIRE-wet-range",[](auto& v){v.wetness_permille=1001;});
    reject("WIRE-wet-remainder",[](auto& v){v.wetness_permille=1;v.wetness_remainder=28800;});
    reject("WIRE-wet-outward",[](auto& v){v.wetness_remainder=1;});
    reject("WIRE-card-day",[](auto& v){v.forecast[1].calendar_day=1;});
    reject("WIRE-card-minmax",[](auto& v){v.forecast[0].minimum_temperature_deci_c=101;});
    reject("WIRE-interval-count",[](auto& v){v.forecast[0].interval_count=9;});
    reject("WIRE-interval-gap",[](auto& v){v.forecast[0].intervals[1].start_tick_in_day=1001;});
    reject("WIRE-interval-overlap",[](auto& v){v.forecast[0].intervals[1].start_tick_in_day=999;});
    reject("WIRE-interval-kind",[](auto& v){v.forecast[0].intervals[1].kind=static_cast<WeatherKind>(9);});
    reject("WIRE-inactive-slot",[](auto& v){v.forecast[0].intervals[3].end_tick_in_day=1;});
    reject("WIRE-summary",[](auto& v){v.forecast[0].precipitation_summary=PrecipitationSummary::None;});
    reject("WIRE-dominant",[](auto& v){v.forecast[0].dominant_kind=WeatherKind::Storm;});
    reject("WIRE-has-storm",[](auto& v){v.forecast[0].has_storm=false;});
    reject("WIRE-hidden-future-storm",[](auto& v){v.next_storm_start_calendar_tick=0;v.next_storm_end_calendar_tick=0;});
    reject("WIRE-not-earliest-storm",[](auto& v){v.next_storm_start_calendar_tick=1001;});
    reject("WIRE-storm-crosses-clear",[](auto& v){v.next_storm_end_calendar_tick=3001;});
    invalid(negatives,"WIRE-active-extended-period",[&]{auto bad=active;bad.next_storm_end_calendar_tick=3000;validate_weather_view(bad,1500);});
    invalid(negatives,"WIRE-active-hidden",[&]{auto bad=active;bad.next_storm_start_calendar_tick=0;bad.next_storm_end_calendar_tick=0;validate_weather_view(bad,1500);});
    const WeatherContext winter{70421u,{weather_algorithm_version,ClimateProfileId::TemperateValley,ClimateScenarioId::FirstWinterStorm}};
    const WeatherScheduleCache winter_cache(winter,CalendarTick{scenario_calendar_offset(winter.config.scenario)});
    const auto winter_view=winter_cache.view(initialize_climate(winter,winter_cache));
    validate_weather_view(winter_view,0);
    invalid(negatives,"WIRE-period-before-scenario",[&]{auto bad=winter_view;bad.current.period_start_calendar_tick=bad.calendar_offset_ticks-1u;validate_weather_view(bad,0);});
    // A storm continuing beyond the final card is deliberately not advertised.
    auto edge = future; edge.forecast[0] = edge.forecast[1]; edge.forecast[0].calendar_day=1;
    auto& last = edge.forecast.back(); last.interval_count=2;
    last.intervals[0]={0,28000,WeatherKind::Clear};last.intervals[1]={28000,28800,WeatherKind::Storm};
    last.has_storm=true;last.precipitation_summary=PrecipitationSummary::Rain;
    edge.next_storm_start_calendar_tick=0;edge.next_storm_end_calendar_tick=0;
    validate_weather_view(edge,0);
}
void host_controls(Json& negatives) {
    const WeatherContext context{70421u,{}};
    WeatherScheduleCache cache(context,CalendarTick{0});
    const auto state=initialize_climate(context,cache);
    const auto view=cache.view(state);
    auto reject = [&](const char* id, auto change) {
        invalid(negatives,id,[&]{auto bad=state;change(bad);validate_climate_runtime(bad,0,context,cache);});
    };
    reject("HOST-version",[](auto& s){s.algorithm_version=2;});
    reject("HOST-offset",[](auto& s){s.calendar_offset_ticks=1;});
    reject("HOST-evaluated-tick",[](auto& s){s.evaluated_simulation_tick=1;});
    reject("HOST-noncanonical-temperature",[](auto& s){++s.current.temperature_deci_c;});
    reject("HOST-noncanonical-wind",[](auto& s){s.current.wind_direction=static_cast<WindDirection>((static_cast<unsigned>(s.current.wind_direction)+1u)%8u);});
    reject("HOST-snow-remainder",[](auto& s){s.snow_cover_remainder=28800;});
    reject("HOST-wet-remainder",[](auto& s){s.wetness_remainder=-28800;});
    const OutdoorWorkRemainderState key{1,static_cast<Job>(2),{0,0,WorldLayer::Surface},0,7500};
    auto valid=state;valid.outdoor_work_remainders.push_back(key);validate_climate_runtime(valid,0,context,cache);
    invalid(negatives,"HOST-duplicate-resident",[&]{auto bad=valid;bad.outdoor_work_remainders.push_back(key);validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-unaffected-job",[&]{auto bad=valid;bad.outdoor_work_remainders[0].job=static_cast<Job>(7);validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-unknown-job",[&]{auto bad=valid;bad.outdoor_work_remainders[0].job=static_cast<Job>(99);validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-work-mine-layer",[&]{auto bad=valid;bad.outdoor_work_remainders[0].target.layer=WorldLayer::Mine;validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-work-target-outside",[&]{auto bad=valid;bad.outdoor_work_remainders[0].target.x=world_max;validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-build-missing-id",[&]{auto bad=valid;bad.outdoor_work_remainders[0].job=static_cast<Job>(5);validate_climate_runtime(bad,0,context,cache);});
    invalid(negatives,"HOST-work-record-bound",[&]{auto bad=state;bad.outdoor_work_remainders.resize(max_outdoor_work_records+1u);validate_climate_runtime(bad,0,context,cache);});
    auto live=state;
    invalid(negatives,"HOST-skip-tick-preserves-state-cache",[&]{try {advance_climate_tick(live,2,cache);} catch (...) {check(live==state && cache.view(state)==view,"HOST-invalid-publication");throw;}});
    invalid(negatives,"CACHE-outside-window",[&]{(void)cache.sample(CalendarTick{6u*ticks_per_day});});
    invalid(negatives,"CACHE-invalid-move-preserves",[&]{try {cache.move_to(CalendarTick{std::numeric_limits<std::uint64_t>::max()});} catch (...) {check(cache.view(state)==view,"CACHE-invalid-move-published");throw;}});
    const WeatherContext winter{70421u,{weather_algorithm_version,ClimateProfileId::TemperateValley,ClimateScenarioId::FirstWinterStorm}};
    invalid(negatives,"CACHE-scenario-start-barrier",[&]{WeatherScheduleCache bad(winter,CalendarTick{scenario_calendar_offset(winter.config.scenario)-1u});(void)bad;});
    invalid(negatives,"KEY-day-zero",[&]{(void)weather_key(context,0,WeatherPurpose::DailyAnomaly);});
    invalid(negatives,"KEY-purpose",[&]{(void)weather_key(context,1,static_cast<WeatherPurpose>(12));});
}
Json boundary_controls() {
    auto result=Json::array();
    constexpr std::array<std::uint32_t,3> seeds{70421u,0u,std::numeric_limits<std::uint32_t>::max()};
    constexpr std::array<std::uint32_t,7> days{1,10,11,30,31,40,41};
    constexpr std::array<std::uint64_t,6> offsets{0,599,600,1199,1200,28799};
    for (auto seed:seeds) {
        const WeatherContext context{seed,{}}; WeatherScheduleCache cache(context,CalendarTick{0});
        for (auto day:days) {
            cache.move_to(CalendarTick{day_begin(day)});
            const WeatherScheduleCache rebuilt(context,CalendarTick{day_begin(day)});
            for (auto offset:offsets) {
                const CalendarTick tick{day_begin(day)+offset}; const auto sample=cache.sample(tick);
                check(sample==rebuilt.sample(tick),"CACHE-jump-rebuild-equality");
                result.push_back({{"seed",seed},{"calendar_tick",tick.value},{"sample",sample_json(sample)}});
            }
        }
        cache.move_to(CalendarTick{0});
        const WeatherScheduleCache fresh(context,CalendarTick{0});
        check(cache.view(initialize_climate(context,cache))==fresh.view(initialize_climate(context,fresh)),"CACHE-rollback-equality");
    }
    return result;
}
Json seeded_key_goldens() {
    const WeatherContext context{286479u,{}};
    constexpr std::array<std::uint64_t,9> expected{
        5851049354480010929ULL,3825483856398810768ULL,13195172845923090279ULL,
        3952134337344217059ULL,8099500713818125661ULL,87010721489369ULL,
        2761471186849154308ULL,17531926826607087551ULL,10142257412091890343ULL};
    auto result=Json::array();
    for (std::size_t i=0;i<expected.size();++i) {
        const auto purpose=static_cast<WeatherPurpose>(i+1u);
        const std::uint64_t period=i>=4u ? 4u:0u;
        const auto actual=weather_key(context,31,purpose,period);
        check(actual==expected[i],"KEY-independent-literal-fold-golden");
        result.push_back({{"seed",286479},{"day",31},{"purpose",i+1u},{"period",period},
                          {"expected",expected[i]},{"actual",actual}});
    }
    return result;
}
Json generic_carryover_golden() {
    // Independently derived with unsigned64 mixer folding and a forward
    // integer reference timeline. These are candidate regression vectors,
    // not a certified shipping weather seed or a simulated/save-loaded World.
    const WeatherContext context{286479u,{}};
    WeatherScheduleCache first(context,CalendarTick{day_begin(31)});
    const WeatherScheduleCache second(context,CalendarTick{day_begin(32)});
    constexpr std::array<std::uint64_t,10> ticks{
        891172,891173,891772,891773,892799,892800,893381,893382,900538,900539};
    constexpr std::array<std::int16_t,10> temperatures{-73,-73,-134,-135,-135,-135,-131,-131,-64,-64};
    auto vectors=Json::array();
    for (std::size_t i=0;i<ticks.size();++i) {
        const auto sample=first.sample(CalendarTick{ticks[i]});
        check(sample==second.sample(CalendarTick{ticks[i]}),"CARRYOVER-cross-cache-sample-equality");
        const auto kind=i==0u ? WeatherKind::Cloudy : i+1u==ticks.size() ? WeatherKind::Clear : WeatherKind::Storm;
        check(sample.kind==kind && sample.temperature_deci_c==temperatures[i],"CARRYOVER-literal-kind-temperature");
        if (kind==WeatherKind::Storm) {
            check(sample.period_start_calendar_tick==891173u && sample.period_end_calendar_tick==900539u &&
                sample.precipitation_permille==837u && sample.wind_permille==951u && sample.wind_direction==WindDirection::NW &&
                sample.precipitation==PrecipitationKind::Snow && sample.outdoor_work_rate_bp==7500u &&
                sample.work_reason==WeatherWorkReason::Storm,"CARRYOVER-literal-period-earliest-wind");
        } else check(sample.precipitation==PrecipitationKind::None,"CARRYOVER-exclusive-event-boundaries");
        vectors.push_back({{"tick",ticks[i]},{"expected_temperature_deci_c",temperatures[i]},
                           {"expected_kind",static_cast<unsigned>(kind)},{"sample",sample_json(sample)}});
    }
    ClimateRuntimeState state;
    state.evaluated_simulation_tick=892799u;state.current=first.sample(CalendarTick{892799u});
    state.snow_cover_permille=650u;
    const auto before=first.view(state);
    advance_climate_tick(state,892800u,first);
    const auto after=first.view(state);
    check(before.next_storm_start_calendar_tick==891173u && after.next_storm_start_calendar_tick==891173u &&
        before.next_storm_end_calendar_tick==900539u && after.next_storm_end_calendar_tick==900539u,
        "CARRYOVER-midnight-active-countdown-stable");
    return {{"seed",286479},{"scenario",0},{"days",{31,32}},
        {"source_events",{{{"start",891173},{"end",897792},{"intensity",837},{"wind_direction",7},{"wind",951}},
                          {{"start",893382},{"end",900539},{"intensity",837},{"wind_direction",0},{"wind",909}}}},
        {"union_start",891173},{"union_end",900539},{"retained_wind",951},{"retained_direction",7},
        {"vectors",vectors},{"midnight_one_tick_advance_preserves_period",true},
        {"candidate_literal_regression",true},{"certified_shipping_seed",false},
        {"derivation","independent unsigned64 mixer and forward integer reference; bounded search cap2500000 checked286480"}};
}
Json thirty_days() {
    const WeatherContext context{70421u,{}};WeatherScheduleCache cache(context,CalendarTick{0});
    auto state=initialize_climate(context,cache);const auto initial=cache.view(state);
    auto days=Json::array();std::array<std::uint64_t,4> kinds{};std::uint64_t rain{},snow{};
    std::int16_t minimum=600,maximum=-800;
    auto recorded_day=initial.forecast[0];std::int16_t day_minimum=600,day_maximum=-800;
    bool day_rain{},day_snow{};
    for (std::uint64_t tick=0;tick<30u*ticks_per_day;++tick) {
        if (tick) advance_climate_tick(state,tick,cache);
        const auto& s=state.current;++kinds[static_cast<std::size_t>(s.kind)];
        rain += s.precipitation==PrecipitationKind::Rain ? 1u:0u;
        snow += s.precipitation==PrecipitationKind::Snow ? 1u:0u;
        minimum=std::min(minimum,s.temperature_deci_c);maximum=std::max(maximum,s.temperature_deci_c);
        day_minimum=std::min(day_minimum,s.temperature_deci_c);day_maximum=std::max(day_maximum,s.temperature_deci_c);
        day_rain |= s.precipitation==PrecipitationKind::Rain;day_snow |= s.precipitation==PrecipitationKind::Snow;
        if (tick%ticks_per_day==ticks_per_day-1u) {
            check(day_minimum==recorded_day.minimum_temperature_deci_c && day_maximum==recorded_day.maximum_temperature_deci_c,
                  "FORECAST-exhaustive-realized-minmax");
            const auto summary=day_rain && day_snow ? PrecipitationSummary::Mixed : day_rain ? PrecipitationSummary::Rain :
                day_snow ? PrecipitationSummary::Snow : PrecipitationSummary::None;
            check(summary==recorded_day.precipitation_summary,"FORECAST-exhaustive-realized-phase");
            const auto view=cache.view(state);check(view.current==state.current && view.snow_cover_permille==state.snow_cover_permille &&
                view.wetness_permille==state.wetness_permille && view.wetness_remainder==state.wetness_remainder,"VIEW-runtime-authority");
            days.push_back({{"forecast",forecast_json(recorded_day)},{"end_snow",state.snow_cover_permille},
                {"end_snow_remainder",state.snow_cover_remainder},{"end_wetness",state.wetness_permille},
                {"end_wetness_remainder",state.wetness_remainder}});
            if (tick+1u<30u*ticks_per_day) {
                recorded_day=view.forecast[1];day_minimum=600;day_maximum=-800;day_rain=false;day_snow=false;
            }
        }
    }
    // Domain-only restart by value: no save codec or World state is exercised.
    auto copied_state=state;WeatherScheduleCache copied_cache(context,CalendarTick{state.evaluated_simulation_tick});
    for (std::uint64_t tick=state.evaluated_simulation_tick+1u,end=tick+1200u;tick<end;++tick) {
        advance_climate_tick(state,tick,cache);advance_climate_tick(copied_state,tick,copied_cache);
    }
    check(state==copied_state && cache.view(state)==copied_cache.view(copied_state),"HOST-domain-value-reconstruction-continuation");
    return {{"seed",70421},{"observed_ticks",30u*ticks_per_day},{"days",days},{"kind_ticks",kinds},
        {"rain_ticks",rain},{"snow_ticks",snow},{"minimum_deci_c",minimum},{"maximum_deci_c",maximum},
        {"domain_value_reconstruction_continuation",true},{"actual_world_save_wire_integration",false},
        {"tuning_acceptance","OPEN"}};
}
Json first_winter() {
    const WeatherContext context{70421u,{weather_algorithm_version,ClimateProfileId::TemperateValley,ClimateScenarioId::FirstWinterStorm}};
    const auto offset=scenario_calendar_offset(context.config.scenario);
    WeatherScheduleCache cache(context,CalendarTick{offset});const auto state=initialize_climate(context,cache);
    const auto view=cache.view(state);
    check(state.snow_cover_permille==650u && state.evaluated_simulation_tick==0u,"WINTER-initial-cover-no-replay");
    std::uint64_t start{},end{};bool all_snow=true;std::int16_t maximum=-800;
    for (std::uint64_t t=offset;t<offset+ticks_per_day;++t) {
        const auto s=cache.sample(CalendarTick{t});
        if (s.kind!=WeatherKind::Storm) continue;
        if (!start) {start=s.period_start_calendar_tick;end=s.period_end_calendar_tick;}
        check(s.period_start_calendar_tick==start && s.period_end_calendar_tick==end,"WINTER-forced-period-continuity");
        all_snow &= s.precipitation==PrecipitationKind::Snow;maximum=std::max(maximum,s.temperature_deci_c);
    }
    check(start>=offset+9600u && start<=offset+14400u && end-start>=4800u && end-start<=7200u,"WINTER-forced-start-duration");
    check(view.next_storm_start_calendar_tick==start && view.next_storm_end_calendar_tick==end,"WINTER-countdown-authority");
    const auto before=cache.sample(CalendarTick{start-1u}), at=cache.sample(CalendarTick{start});
    const auto last=cache.sample(CalendarTick{end-1u}), after=cache.sample(CalendarTick{end});
    check(before.kind!=WeatherKind::Storm && at.kind==WeatherKind::Storm && last.kind==WeatherKind::Storm && after.kind!=WeatherKind::Storm,
          "WINTER-event-boundaries");
    return {{"seed",70421},{"offset",offset},{"start",start},{"end",end},{"duration",end-start},
        {"all_ticks_snow",all_snow},{"maximum_storm_temperature_deci_c",maximum},
        {"before",sample_json(before)},{"at_start",sample_json(at)}, {"before_end",sample_json(last)},{"at_end",sample_json(after)},
        {"certified_shipping_seed",false},{"seed_acceptance","OPEN"}};
}
Json measurements() {
    const WeatherContext context{70421u,{}};
    std::vector<double> cold,roll; cold.reserve(100);roll.reserve(100);
    std::uint64_t witness{};
    for (unsigned i=0;i<100;++i) {
        const auto begin=Clock::now();WeatherScheduleCache cache(context,CalendarTick{0});
        witness += cache.sample(CalendarTick{0}).wind_permille;const auto end=Clock::now();cold.push_back(elapsed_ms(begin,end));
    }
    WeatherScheduleCache rollover(context,CalendarTick{0});
    allocation_probe::count=0;allocation_probe::bytes=0;allocation_probe::enabled=true;
    try {
        for (std::uint32_t day=2;day<=101;++day) {
            const auto begin=Clock::now();rollover.move_to(CalendarTick{day_begin(day)});
            witness += rollover.sample(CalendarTick{day_begin(day)}).wind_permille;const auto end=Clock::now();roll.push_back(elapsed_ms(begin,end));
        }
    } catch (...) { allocation_probe::enabled=false;throw; }
    allocation_probe::enabled=false;
    const auto rollover_allocations=allocation_probe::count,rollover_bytes=allocation_probe::bytes;
    check(rollover_allocations==0u && rollover_bytes==0u,"PERF-rollover-no-heap");
    WeatherScheduleCache cache(context,CalendarTick{0});auto state=initialize_climate(context,cache);
    state.outdoor_work_remainders.push_back({1,static_cast<Job>(2),{0,0,WorldLayer::Surface},0,7500});
    constexpr std::uint64_t steady_ticks=10000;
    allocation_probe::count=0;allocation_probe::bytes=0;allocation_probe::enabled=true;
    const auto begin=Clock::now();
    try {
        for (std::uint64_t tick=1;tick<=steady_ticks;++tick) advance_climate_tick(state,tick,cache);
        witness += cache.view(state).current.wind_permille;
    } catch (...) { allocation_probe::enabled=false;throw; }
    const auto end=Clock::now();allocation_probe::enabled=false;
    check(allocation_probe::count==0u && allocation_probe::bytes==0u,"PERF-steady-no-heap");
    auto sorted_cold=cold,sorted_roll=roll;std::sort(sorted_cold.begin(),sorted_cold.end());std::sort(sorted_roll.begin(),sorted_roll.end());
    const auto cold_p95=sorted_cold[94],roll_p95=sorted_roll[94],steady_ms=elapsed_ms(begin,end);
    return {{"cold_cache_init_ms",cold_p95},{"cold_cache_samples_ms",cold},{"day_rollover_ms",roll},
        {"day_rollover_p95_ms",roll_p95},{"cold_samples_per_build",115200},{"roll_samples_per_build",28800},
        {"day_rollover_allocations",rollover_allocations},{"day_rollover_allocated_bytes",rollover_bytes},
        {"steady_ticks",steady_ticks},{"steady_elapsed_ms",steady_ms},
        {"steady_ns_per_tick",steady_ms*1000000.0/static_cast<double>(steady_ticks)},
        {"steady_allocations",allocation_probe::count},{"steady_allocated_bytes",allocation_probe::bytes},
        {"cold_init_total_under10ms",cold_p95<=10.0},{"pure_forecast_day_10ms_goal_met",roll_p95<=10.0},
        {"allocation_scope","global operator-new instrumentation; pure domain, no allocator bypass/platform heap profiling"},
        {"scope","pure domain; no full simulation/renderer/network overhead measured"},{"measurement_witness",witness}};
}
void write_report(const std::filesystem::path& file,const Json& report) {
    const auto text=report.dump(2);check(text.size()<=128u*1024u*1024u,"REPORT-output-budget");
    std::ofstream out(file,std::ios::binary|std::ios::trunc);check(out.good(),"REPORT-open");
    out.write(text.data(),static_cast<std::streamsize>(text.size()));out.put('\n');out.flush();check(out.good(),"REPORT-flush");
}
}

int main(int argc,char** argv) {
    Json report={{"schema",1},{"complete",false},{"deterministic",Json::object()},{"measured",Json::object()}};
    std::filesystem::path output;
    bool owns_output{};
    try {
        check(argc==2,"Usage: WeatherDomainProbe.exe <fresh-output>");output=std::filesystem::absolute(std::filesystem::path(argv[1]));
        check(!std::filesystem::exists(output),"Output directory must be fresh; existing files are preserved");
        check(std::filesystem::is_directory(output.parent_path()),"Output parent directory must exist");
        check(std::filesystem::create_directory(output),"Cannot create fresh output directory");owns_output=true;
        auto negatives=Json::array();auto& d=report["deterministic"];
        d["candidate_version"]=weather_algorithm_version;
        d["scope"]="pure host weather domain foundation; candidate tuning";
        d["constants"]={{"ticks_per_day",ticks_per_day},{"hour_ticks",weather_hour_ticks},
            {"diurnal_offsets",weather_diurnal_offsets},{"cache_schedules",weather_schedule_days},
            {"forecast_cards",forecast_days},{"wire_interval_limit",max_forecast_intervals_per_day},
            {"generated_kind_interval_limit",7},{"mixer_initial_fold","5249425745415448"},
            {"mixer_constants",{"9e3779b97f4a7c15","bf58476d1ce4e5b9","94d049bb133111eb"}},
            {"purpose_ids",{1,2,3,4,5,6,7,8,9,10,11}},
            {"candidate_merge_wind_convention","earliest equal-kind/intensity period wind retained; connected generic events coalesced across midnight; background periods remain day-local"}};
        check(weather_mix64(0)==0xe220a8397b1dcdafULL && weather_mix64(1)==0x910a2dec89025cc1ULL,"MIX-literal-goldens");
        d["mixer_goldens"]={{{"input",0},{"expected_hex","e220a8397b1dcdaf"},{"value",weather_mix64(0)}},
                              {{"input",1},{"expected_hex","910a2dec89025cc1"},{"value",weather_mix64(1)}}};
        d["calendar_goldens"]=calendar_controls(negatives);d["cover_goldens"]=cover_controls(negatives);
        d["work_goldens"]=work_controls(negatives);wire_controls(negatives);host_controls(negatives);
        d["seeded_key_goldens"]=seeded_key_goldens();d["generic_carryover_golden"]=generic_carryover_golden();
        d["boundary_samples"]=boundary_controls();d["candidate_30_day"]=thirty_days();d["first_winter_seed_check"]=first_winter();
        d["invalid_controls"]=negatives;
        d["forecast_checks"]={{"exact_realized_minmax_days",30},{"exact_realized_phase_days",30},
            {"rollback_rebuild_seed_count",3},{"runtime_current_cover_source",true},
            {"active_storm_pair_exact_period",true},{"future_storm_structural_limit","kind-only cards cannot prove distinct-intensity period end within adjacent Storm run"}};
        d["open_delivery"]={"World authority and typed calendar call-site migration","World Activity key matching and cancellation",
            "actual save/checksum round trips","actual full/delta/split wire and ClientSession","HUD unknown/current/forecast",
            "weather FX assets and renderer","first-winter seed certification and tuning acceptance","full simulation 1-percent overhead"};
        report["measured"]=measurements();report["complete"]=true;
        write_report(output/"weather-domain-report.json",report);
        std::cout<<"weather domain checks passed\n";return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled=false;report["complete"]=false;report["error"]=error.what();
        if (owns_output) { try {write_report(output/"weather-domain-report.json",report);} catch (...) {} }
        std::cerr<<"weather domain checks failed: "<<error.what()<<'\n';return 1;
    }
}

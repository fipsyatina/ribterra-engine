#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
namespace ribterra {
enum class Season { Spring, Summer, Autumn, Winter };
// Real minutes at simulation speed 1x; 20 simulation ticks per second.
inline constexpr int days_per_season=10;
inline constexpr std::uint64_t daylight_ticks=12*60*20;
inline constexpr std::uint64_t ticks_per_day=daylight_ticks*2;
inline const char *season_label(Season season) {
    switch(season) {
    case Season::Spring: return "season.spring";
    case Season::Summer: return "season.summer";
    case Season::Autumn: return "season.autumn";
    default: return "season.winter";
    }
}

// Calendar presentation/policy uses this distinct type. Simulation deadlines
// remain raw uint64 ticks and must never receive the calendar offset.
struct CalendarTick {
    std::uint64_t value{};
    bool operator==(const CalendarTick&) const = default;
};
struct CalendarView {
    CalendarTick tick;
    std::uint32_t day{};
    Season season{Season::Spring};
    bool night{};
    std::uint16_t clock_minute{};
    float night_amount{};
    bool operator==(const CalendarView&) const = default;
};
inline std::optional<CalendarTick> make_calendar_tick(
    std::uint64_t simulation_tick, std::uint64_t offset_ticks) noexcept {
    if (simulation_tick > std::numeric_limits<std::uint64_t>::max() - offset_ticks)
        return std::nullopt;
    return CalendarTick{simulation_tick + offset_ticks};
}
inline std::uint32_t calendar_day(CalendarTick tick) {
    const auto day = tick.value / ticks_per_day;
    if (day >= std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Calendar day exceeds uint32");
    return static_cast<std::uint32_t>(day + 1u);
}
inline bool is_night(CalendarTick tick) { return tick.value % ticks_per_day >= daylight_ticks; }
inline float night_amount(CalendarTick tick) {
    const float phase=float(tick.value%ticks_per_day)/float(ticks_per_day);
    const float sun=std::sin(phase*6.28318530718f);
    const float fade=std::clamp(.5f-sun*4.f,0.f,1.f);
    return fade*fade*(3-2*fade);
}
inline std::string clock_time(CalendarTick tick) {
    const int minutes=int((tick.value%ticks_per_day)*1440/ticks_per_day+360)%1440;
    char buffer[6];std::snprintf(buffer,sizeof(buffer),"%02d:%02d",minutes/60,minutes%60);return buffer;
}
inline CalendarView calendar_view(CalendarTick tick) {
    const auto day = calendar_day(tick);
    const auto season = static_cast<Season>(((day - 1u) / days_per_season) % 4u);
    const auto minute = static_cast<std::uint16_t>(
        ((tick.value % ticks_per_day) * 1440u / ticks_per_day + 360u) % 1440u);
    return {tick, day, season, is_night(tick), minute, night_amount(tick)};
}
}

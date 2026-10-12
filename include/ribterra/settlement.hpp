#pragma once
#include "ribterra/seasons.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>
namespace ribterra {
inline constexpr int recruitment_food = 50;
inline constexpr std::uint64_t recruitment_ticks = 2 * 60 * 20;
inline constexpr std::uint64_t youth_ticks = 3 * 60 * 20;
inline constexpr int night_shift_discontent = 5, youth_work_discontent = 10;
enum class SettlementDecision : std::uint8_t { Recruit, NightShift, YouthWork };
struct YoungResident {
    int resident_id{};
    std::uint64_t adult_tick{};
    bool forced_work{};
    bool operator==(const YoungResident &) const = default;
};
struct SettlementState {
    std::uint64_t revision{}, arrival_tick{}, forced_night{}; // Calendar day, zero means never.
    int night_discontent{}, youth_discontent{};
    std::vector<YoungResident> young;
    bool reduced_rations{};
    int discontent() const { return std::min(100, night_discontent + youth_discontent + (reduced_rations?10:0)); }
    bool night_shift(CalendarTick tick) const { return is_night(tick) && forced_night == calendar_day(tick); }
    const YoungResident *youth(int id, std::uint64_t tick) const {
        auto it=std::find_if(young.begin(),young.end(),[&](const auto &v){return v.resident_id==id && tick<v.adult_tick;});
        return it==young.end()?nullptr:&*it;
    }
    bool operator==(const SettlementState &) const = default;
};
inline bool valid_settlement(const SettlementState &s, std::uint64_t simulation_tick, CalendarTick calendar) {
    if(s.night_discontent<0 || s.night_discontent>100 || s.youth_discontent<0 || s.youth_discontent>100 ||
       s.night_discontent%night_shift_discontent || s.youth_discontent%youth_work_discontent ||
       s.forced_night>calendar_day(calendar) || s.young.size()>1500 ||
       (s.arrival_tick && (s.arrival_tick<=simulation_tick || s.arrival_tick-simulation_tick>recruitment_ticks))) return false;
    int previous=0;
    for(const auto &y:s.young) {
        if(y.resident_id<=previous || y.adult_tick<=simulation_tick || y.adult_tick-simulation_tick>youth_ticks) return false;
        previous=y.resident_id;
    }
    return true;
}
}

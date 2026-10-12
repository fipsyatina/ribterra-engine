#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ribterra {
enum class Skill : std::uint8_t {
    Woodcutting,
    Mining,
    Farming,
    Construction,
    Hauling,
    Smithing,
    Medicine,
    Combat,
    Count
};
inline constexpr std::size_t skill_count = static_cast<std::size_t>(Skill::Count);
inline constexpr int maximum_skill_experience = 1'000'000'000;
// Useful work is measured in 1/100 of a standard working tick. Stored integers
// make fractional learning and familiarity independent of polling frequency.
struct SkillPractice {
    std::uint32_t experience_remainder{};
    std::uint32_t familiarity_debt{};
    std::uint64_t last_practice_tick{};
    friend bool operator==(const SkillPractice &, const SkillPractice &) = default;
};
struct ModifierBreakdown {
    int skill_sum_bp{}, ability_sum_bp{}, familiarity_bp{}, uncapped_bp{}, applied_bp{};
    int speed_bp{10000};
    bool capped{};
    friend bool operator==(const ModifierBreakdown &, const ModifierBreakdown &) = default;
};
// One combined cap for all relevant skills and abilities; no per-source cap.
ModifierBreakdown combine_skill_modifiers(std::span<const int> skills, std::span<const int> abilities,
                                          int familiarity_bp);
ModifierBreakdown skill_modifier(int experience, const SkillPractice &, int ability_bp, std::uint64_t now);
void record_skill_work(int &experience, SkillPractice &, std::uint32_t useful_work_units, std::uint64_t now);
bool valid_skill_practice(int experience, const SkillPractice &, std::uint64_t now);
std::uint32_t familiarity_debt_at(int experience, const SkillPractice &, std::uint64_t now);
std::uint64_t skill_day_ticks();
float skill_gather_base_progress();
float skill_build_base_progress();
std::uint32_t skill_work_units_per_tick();
std::uint32_t skill_gather_work_units();
} // namespace ribterra

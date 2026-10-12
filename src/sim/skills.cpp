#include "ribterra/skills.hpp"
#include "skill_balance_generated.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ribterra {
namespace {
namespace balance = skill_balance;
constexpr std::uint64_t grace_ticks = std::uint64_t(balance::ticks_per_day) * balance::familiarity_grace_days;
constexpr std::uint64_t decay_ticks = std::uint64_t(balance::ticks_per_day) * balance::familiarity_decay_days;
constexpr auto max_debt = static_cast<std::uint32_t>(
    std::uint64_t(balance::ticks_per_day) * balance::work_units_per_tick *
    balance::familiarity_recovery_day_numerator / balance::familiarity_recovery_day_denominator);
int experience_effect(int experience) {
    const auto &curve = balance::experience_curve;
    for (std::size_t i = 1; i < curve.size(); ++i) {
        if (experience >= curve[i][0])
            continue;
        const auto [x0, y0] = curve[i - 1];
        const auto [x1, y1] = curve[i];
        return y0 + static_cast<int>(std::int64_t(experience - x0) * (y1 - y0) / (x1 - x0));
    }
    return curve.back()[1];
}
int sum(std::span<const int> values) {
    // Bound contributors, not individual skill/ability effects. The cap below
    // is applied exactly once after their complete sum.
    if (values.size() > 64)
        throw std::invalid_argument("Too many skill contributors");
    std::int64_t total{};
    for (int value : values)
        total += value;
    if (total < std::numeric_limits<int>::min() || total > std::numeric_limits<int>::max())
        throw std::invalid_argument("Skill contributor sum overflow");
    return static_cast<int>(total);
}
} // namespace
ModifierBreakdown combine_skill_modifiers(std::span<const int> skills, std::span<const int> abilities,
                                          int familiarity_bp) {
    if (familiarity_bp < -static_cast<int>(balance::familiarity_cap_bp) || familiarity_bp > 0)
        throw std::invalid_argument("Familiarity exceeds the author limit");
    ModifierBreakdown result;
    result.skill_sum_bp = sum(skills);
    result.ability_sum_bp = sum(abilities);
    result.familiarity_bp = familiarity_bp;
    const auto combined = std::int64_t(result.skill_sum_bp) + result.ability_sum_bp + familiarity_bp;
    if (combined < std::numeric_limits<int>::min() || combined > std::numeric_limits<int>::max())
        throw std::invalid_argument("Combined skill sum overflow");
    result.uncapped_bp = static_cast<int>(combined);
    result.applied_bp = std::clamp(result.uncapped_bp, -static_cast<int>(balance::skill_cap_bp),
                                   static_cast<int>(balance::skill_cap_bp));
    result.speed_bp = 10000 + result.applied_bp;
    result.capped = result.applied_bp != result.uncapped_bp;
    return result;
}
bool valid_skill_practice(int experience, const SkillPractice &practice, std::uint64_t now) {
    return experience >= 0 && experience <= maximum_skill_experience &&
           practice.experience_remainder < balance::work_units_per_xp &&
           practice.familiarity_debt <= max_debt && practice.last_practice_tick <= now &&
           (experience != maximum_skill_experience || practice.experience_remainder == 0) &&
           (experience != 0 || practice.experience_remainder != 0 || practice.familiarity_debt == 0);
}
std::uint32_t familiarity_debt_at(int experience, const SkillPractice &practice, std::uint64_t now) {
    if (!valid_skill_practice(experience, practice, now))
        throw std::invalid_argument("Invalid skill practice");
    if (experience == 0 && practice.experience_remainder == 0)
        return 0;
    const auto absent = now - practice.last_practice_tick;
    if (absent <= grace_ticks)
        return practice.familiarity_debt;
    const auto decay = absent - grace_ticks;
    if (decay >= decay_ticks)
        return max_debt;
    const auto added = decay * max_debt / decay_ticks; // bounded multiplication, even at UINT64_MAX tick
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(max_debt, practice.familiarity_debt + added));
}
ModifierBreakdown skill_modifier(int experience, const SkillPractice &practice, int ability_bp,
                                 std::uint64_t now) {
    if (ability_bp < -1500 || ability_bp > 1500)
        throw std::invalid_argument("Invalid resident ability");
    const auto debt = familiarity_debt_at(experience, practice, now);
    const auto familiarity = -static_cast<int>(std::uint64_t(debt) * balance::familiarity_cap_bp / max_debt);
    const std::array skills{experience_effect(experience)};
    const std::array abilities{ability_bp};
    return combine_skill_modifiers(skills, abilities, familiarity);
}
void record_skill_work(int &experience, SkillPractice &practice, std::uint32_t useful_work_units,
                       std::uint64_t now) {
    const auto debt = familiarity_debt_at(experience, practice, now);
    if (!useful_work_units)
        return; // Waiting and repeated completion checks cannot reset the decay clock.
    practice.familiarity_debt = useful_work_units >= debt ? 0 : debt - useful_work_units;
    practice.last_practice_tick = now;
    const auto total = std::uint64_t(practice.experience_remainder) + useful_work_units;
    const auto earned = total / balance::work_units_per_xp;
    experience += static_cast<int>(std::min<std::uint64_t>(maximum_skill_experience - experience, earned));
    practice.experience_remainder = experience == maximum_skill_experience
                                        ? 0
                                        : static_cast<std::uint32_t>(total % balance::work_units_per_xp);
}
std::uint64_t skill_day_ticks() {
    return balance::ticks_per_day;
}
float skill_gather_base_progress() {
    return 1.f / balance::gather_base_work_ticks;
}
float skill_build_base_progress() {
    return 1.f / balance::build_base_work_ticks;
}
std::uint32_t skill_work_units_per_tick() {
    return balance::work_units_per_tick;
}
std::uint32_t skill_gather_work_units() {
    return balance::gather_base_work_ticks * balance::work_units_per_tick;
}
} // namespace ribterra

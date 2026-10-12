#include "ribterra/skills.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace ribterra;
int failures{};
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)
template <class F> void rejects(F function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
}
void combined_cap() {
    const std::array skills{1200, 900, 400};
    const std::array abilities{600, 400};
    const auto value = combine_skill_modifiers(skills, abilities, -200);
    CHECK(value.skill_sum_bp == 2500 && value.ability_sum_bp == 1000);
    CHECK(value.uncapped_bp == 3300 && value.applied_bp == 1500 && value.speed_bp == 11500 && value.capped);
    const std::array mixed_skills{2000};
    const std::array mixed_abilities{-1000};
    const auto mixed = combine_skill_modifiers(mixed_skills, mixed_abilities, 0);
    CHECK(mixed.applied_bp == 1000 && !mixed.capped); // individually clamping the +2000 would be wrong
    for (int skill = -4000; skill <= 4000; skill += 137)
        for (int ability = -3000; ability <= 3000; ability += 181) {
            const std::array s{skill}, a{ability};
            const auto b = combine_skill_modifiers(s, a, -200);
            CHECK(b.applied_bp >= -1500 && b.applied_bp <= 1500);
            CHECK(b.speed_bp == 10000 + b.applied_bp);
            CHECK(b.capped == (b.uncapped_bp != b.applied_bp));
        }
    const std::array maximum{std::numeric_limits<int>::max()};
    rejects([&] { combine_skill_modifiers(maximum, maximum, 0); });
    rejects([&] { combine_skill_modifiers({}, {}, -201); });
    rejects([&] { combine_skill_modifiers({}, {}, 1); });
}
void permanent_experience_and_lazy_familiarity() {
    int xp = 1440;
    SkillPractice practice{0, 0, 100};
    const auto day = skill_day_ticks();
    CHECK(day == 1200);
    CHECK(skill_modifier(xp, practice, 0, 100).applied_bp == 1200);
    CHECK(skill_modifier(xp, practice, 0, 100 + 3 * day).familiarity_bp == 0);
    CHECK(skill_modifier(xp, practice, 0, 100 + 8 * day).familiarity_bp == -100);
    CHECK(skill_modifier(xp, practice, 0, 100 + 13 * day).applied_bp == 1000);
    const auto before = practice;
    for (std::uint64_t tick = 100; tick < 100 + 20 * day; tick += 17)
        CHECK(skill_modifier(xp, practice, 0, tick).familiarity_bp >= -200);
    CHECK(xp == 1440 && practice == before); // queries never materialize decay or erase XP
    const auto far = std::numeric_limits<std::uint64_t>::max();
    CHECK(skill_modifier(xp, practice, 0, far).familiarity_bp == -200);
    record_skill_work(xp, practice, 0, far);
    CHECK(xp == 1440 && practice == before);
    record_skill_work(xp, practice, 15000, 100 + 20 * day);
    CHECK(xp == 1443 && practice.experience_remainder == 3000);
    CHECK(practice.familiarity_debt == 15000);
    CHECK(skill_modifier(xp, practice, 0, practice.last_practice_tick).familiarity_bp == -100);
    record_skill_work(xp, practice, 15000, practice.last_practice_tick + 1);
    CHECK(xp == 1447 && practice.experience_remainder == 2000 && practice.familiarity_debt == 0);
}
void work_credit_and_independence() {
    std::array<int, skill_count> xp{};
    std::array<SkillPractice, skill_count> practice{};
    record_skill_work(xp[0], practice[0], skill_gather_work_units(), 40);
    CHECK(xp[0] == 1);
    record_skill_work(xp[1], practice[1], 16000, 200);
    CHECK(xp[0] == 1 && xp[1] == 4 && xp[2] == 0);
    record_skill_work(xp[0], practice[0], 4000, 400);
    CHECK(xp[0] == 2 && xp[1] == 4);
    CHECK(skill_modifier(0, {}, 0, 1000000).familiarity_bp == 0); // never-used skill does not forget practice
    int whole_xp{}, split_xp{};
    SkillPractice whole, split;
    record_skill_work(whole_xp, whole, 9999, 1);
    for (int i = 0; i < 9999; ++i)
        record_skill_work(split_xp, split, 1, 1);
    CHECK(whole_xp == split_xp && whole == split);
    CHECK(whole_xp == 2 && whole.experience_remainder == 1999);
    const auto original = whole;
    record_skill_work(whole_xp, whole, 0, 1000);
    CHECK(whole == original);
    int saturated = maximum_skill_experience - 1;
    SkillPractice p{3999, 0, 0};
    record_skill_work(saturated, p, std::numeric_limits<std::uint32_t>::max(), 1);
    CHECK(saturated == maximum_skill_experience && p.experience_remainder == 0);
    record_skill_work(saturated, p, 4000, 2);
    CHECK(saturated == maximum_skill_experience && p.experience_remainder == 0);
}
void curve_and_validation() {
    int previous = -1501;
    for (int xp = 0; xp <= 3000; ++xp) {
        const auto b = skill_modifier(xp, {}, 0, 0);
        CHECK(b.skill_sum_bp >= previous);
        CHECK(b.speed_bp >= 8500 && b.speed_bp <= 11500);
        previous = b.skill_sum_bp;
    }
    CHECK(skill_modifier(0, {}, 0, 0).applied_bp == -800);
    CHECK(skill_modifier(120, {}, 0, 0).applied_bp == 0);
    CHECK(skill_modifier(480, {}, 0, 0).applied_bp == 500);
    CHECK(skill_modifier(2880, {}, 1500, 0).applied_bp == 1500);
    CHECK(!valid_skill_practice(-1, {}, 0));
    CHECK(!valid_skill_practice(1, {4000, 0, 0}, 0));
    CHECK(!valid_skill_practice(1, {0, 30001, 0}, 0));
    CHECK(!valid_skill_practice(1, {0, 0, 10}, 9));
    CHECK(!valid_skill_practice(0, {0, 1, 0}, 0));
    CHECK(!valid_skill_practice(maximum_skill_experience, {1, 0, 0}, 0));
    rejects([] { skill_modifier(0, {}, 1501, 0); });
    rejects([] { skill_modifier(1, {0, 0, 10}, 0, 9); });
}
} // namespace
int main() {
    combined_cap();
    permanent_experience_and_lazy_familiarity();
    work_credit_and_independence();
    curve_and_validation();
    if (!failures)
        std::cout << "Skill caps, independent XP, lazy decay, recovery and credit bounds passed.\n";
    return failures ? 1 : 0;
}

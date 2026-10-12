#include "ribterra/moon_lighting.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
using namespace ribterra::render;
std::size_t checks{};

void check(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
    ++checks;
}

template<class Function>
void reject(Function&& function, std::string_view message) {
    bool rejected{};
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}

void abi_tests() {
    check(sizeof(MoonLightingGpu) == 32, "Moon lighting ABI size changed");
    check(alignof(MoonLightingGpu) == 16, "Moon lighting ABI alignment changed");
    check(offsetof(MoonLightingGpu, direction) == 0, "Moon direction offset changed");
    check(offsetof(MoonLightingGpu, color_intensity) == 16, "Moon color offset changed");
    check(std::is_standard_layout_v<MoonLightingGpu>, "Moon lighting is not standard layout");
    check(std::is_trivially_copyable_v<MoonLightingGpu>, "Moon lighting is not trivially copyable");
    const MoonLightingGpu moon;
    check(moon.direction == std::array<float, 4>{0.f, 0.f, 1.f, 0.f},
          "Default moon direction changed");
    check(moon.color_intensity == std::array<float, 4>{.55f, .68f, 1.f, 0.f},
          "Default cold moon color or disabled intensity changed");
    validate_moon_lighting(moon);
}

void boundary_tests() {
    MoonLightingGpu moon;
    moon.color_intensity[3] = moon_max_intensity;
    validate_moon_lighting(moon);
    check(moon.color_intensity[3] == 64.f, "Maximum moon intensity changed");
    moon.color_intensity = {0.f, 1.f, .5f, 0.f};
    validate_moon_lighting(moon);
    check(moon.color_intensity[0] == 0.f && moon.color_intensity[1] == 1.f,
          "Moon RGB endpoints changed");

    moon = {};
    moon.direction = {std::sqrt(.5f), 0.f, std::sqrt(.5f), 0.f};
    validate_moon_lighting(moon);
    moon.direction = {1.f, 0.f, 0.f, 0.f};
    validate_moon_lighting(moon);
    moon.direction = {0.f, -1.f, 0.f, 0.f};
    validate_moon_lighting(moon);

    moon = {};
    moon.direction = {std::sqrt(1.f + .5f * moon_direction_length_squared_tolerance), 0.f, 0.f, 0.f};
    validate_moon_lighting(moon);
    moon.direction = {std::sqrt(1.f + 2.f * moon_direction_length_squared_tolerance), 0.f, 0.f, 0.f};
    reject([&] { validate_moon_lighting(moon); },
           "Above-tolerance moon direction length accepted");
    moon = {};
    moon.direction = {0.f, 0.f, std::sqrt(1.f - .5f * moon_direction_length_squared_tolerance), 0.f};
    validate_moon_lighting(moon);
    moon.direction = {0.f, 0.f, std::sqrt(1.f - 2.f * moon_direction_length_squared_tolerance), 0.f};
    reject([&] { validate_moon_lighting(moon); },
           "Below-tolerance moon direction length accepted");
}

void negative_tests() {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    for (const float invalid : {nan, infinity, -infinity}) {
        for (std::size_t lane = 0; lane < 4; ++lane) {
            auto moon = MoonLightingGpu{};
            moon.direction[lane] = invalid;
            reject([&] { validate_moon_lighting(moon); },
                   "Non-finite moon direction component accepted");
            moon = {};
            moon.color_intensity[lane] = invalid;
            reject([&] { validate_moon_lighting(moon); },
                   "Non-finite moon color/intensity component accepted");
        }
    }

    for (const std::array<float, 3> direction : {
             std::array<float, 3>{0.f, 0.f, 0.f},
             std::array<float, 3>{2.f, 0.f, 0.f},
             std::array<float, 3>{.5f, .5f, .5f}}) {
        auto moon = MoonLightingGpu{};
        moon.direction = {direction[0], direction[1], direction[2], 0.f};
        reject([&] { validate_moon_lighting(moon); }, "Nonnormal moon direction accepted");
    }
    auto moon = MoonLightingGpu{};
    moon.direction[3] = 1.f;
    reject([&] { validate_moon_lighting(moon); }, "Reserved moon direction component accepted");
    for (std::size_t channel = 0; channel < 3; ++channel) {
        moon = {}; moon.color_intensity[channel] = -.001f;
        reject([&] { validate_moon_lighting(moon); }, "Negative moon RGB accepted");
        moon = {}; moon.color_intensity[channel] = 1.001f;
        reject([&] { validate_moon_lighting(moon); }, "Moon RGB above one accepted");
    }
    moon = {}; moon.color_intensity[3] = -.001f;
    reject([&] { validate_moon_lighting(moon); }, "Negative moon intensity accepted");
    moon = {}; moon.color_intensity[3] = std::nextafter(moon_max_intensity, infinity);
    reject([&] { validate_moon_lighting(moon); }, "Moon intensity above 64 accepted");

    // Disabled lights still validate every field instead of hiding malformed ABI.
    moon = {}; moon.color_intensity[3] = 0.f; moon.direction[3] = 1.f;
    reject([&] { validate_moon_lighting(moon); },
           "Disabled moon bypassed reserved-component validation");
    moon = {}; moon.color_intensity = {1.1f, .5f, .5f, 0.f};
    reject([&] { validate_moon_lighting(moon); },
           "Disabled moon bypassed RGB validation");
    moon = {}; moon.direction = {0.f, 0.f, 0.f, 0.f};
    reject([&] { validate_moon_lighting(moon); },
           "Disabled moon bypassed direction validation");
}
} // namespace

int main() {
    try {
        abi_tests();
        boundary_tests();
        negative_tests();
        std::cout << "Moon lighting CPU contract: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Moon lighting CPU contract failed: " << error.what() << '\n';
        return 1;
    }
}

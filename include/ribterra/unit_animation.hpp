#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ribterra {
// Visual cadence only: simulation movement and game speed remain unchanged.
inline constexpr std::uint64_t walk_animation_slowdown = 2;
inline int animal_walk_frame(double tick) {
    return int(std::fmod(std::floor(tick / (5 * walk_animation_slowdown)), 4.0));
}

// Follows displayed displacement and fractional presentation ticks. A paused
// presentation clock leaves both direction and walk phase unchanged.
struct UnitAnimation {
    float x{}, y{}, distance{};
    double tick{};
    int direction{};
    // Atlas columns follow screen space clockwise: E, SE, S, SW, W, NW, N, NE.
    int facing{2};
    bool initialized{}, moving{}, flipped{};
    void update(float next_x, float next_y, double next_tick) {
        if (initialized && tick == next_tick) return;
        const float dx = next_x-x, dy = next_y-y;
        const float step = std::abs(dx)+std::abs(dy);
        moving = initialized && next_tick > tick && step > .0001f &&
                 step <= std::max(2.0, (next_tick - tick) * .25 + 1.0);
        if (moving) {
            distance = std::fmod(distance+step/float(walk_animation_slowdown), .9f);
            direction = dx+dy < 0 ? 1 : 0;
            flipped = dx-dy < 0;
            constexpr float quarter_pi = .78539816339f;
            facing = (int(std::round(std::atan2(.5f*(dx+dy), dx-dy) / quarter_pi)) + 8) % 8;
        } else {
            distance = 0;
        }
        x = next_x; y = next_y; tick = next_tick; initialized = true;
    }
    int frame() const { return moving ? int(distance/.9f*4.f)%4 : 1; }
};
}

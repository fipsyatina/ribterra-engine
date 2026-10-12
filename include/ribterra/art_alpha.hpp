#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ribterra {
// Decode source mattes once at load; preserve original artwork on disk.
inline void decode_art_magenta(std::uint8_t *rgba, bool foliage = false) {
    const float r = rgba[0], g = rgba[1], b = rgba[2];
    const float spill = std::max(0.f, std::min(r, b) - g);
    if (foliage && spill > 1.f) {
        // The matte contaminates fine leaves as well as their alpha edge.
        rgba[0] = static_cast<std::uint8_t>(std::min(r,g+8.f));
        rgba[2] = static_cast<std::uint8_t>(std::min(b,g));
    }
    if (spill <= 24.f) return;
    const float alpha = std::clamp(1.f - (spill - 24.f) / 210.f, 0.f, 1.f);
    if (alpha < .08f) {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
        return;
    }
    rgba[0] = static_cast<std::uint8_t>(std::clamp((r - 255.f * (1-alpha))/alpha, 0.f, 255.f));
    rgba[1] = static_cast<std::uint8_t>(std::clamp(g/alpha, 0.f, 255.f));
    rgba[2] = static_cast<std::uint8_t>(std::clamp((b - 255.f * (1-alpha))/alpha, 0.f, 255.f));
    if (foliage) {
        rgba[0] = std::min(rgba[0],static_cast<std::uint8_t>(std::min(255,int(rgba[1])+8)));
        rgba[2] = std::min(rgba[2],rgba[1]);
    }
    rgba[3] = static_cast<std::uint8_t>(std::lround(rgba[3] * alpha));
}
inline void decode_art_luminance(std::uint8_t *rgba) {
    const auto value = std::max({rgba[0], rgba[1], rgba[2]});
    rgba[3] = static_cast<std::uint8_t>((unsigned(rgba[3]) * value) / 255);
    rgba[0] = rgba[1] = rgba[2] = 255;
}
} // namespace ribterra

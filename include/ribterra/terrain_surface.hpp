#pragma once
#include "ribterra/world_grid.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace ribterra {
// Each corner is shared by four cells, including across chunk boundaries.
// Only visible samples contribute; hidden terrain cannot leak through the mesh.
template <class Sample> float terrain_corner(int x, int y, const Sample &sample) {
    float sum = 0;
    int count = 0;
    for (int dy = -1; dy <= 0; ++dy)
        for (int dx = -1; dx <= 0; ++dx) {
            const auto t = sample(x + dx, y + dy);
            if (!t.explored)
                continue;
            if (t.terrain == Terrain::Water)
                return 0;
            sum += t.elevation;
            ++count;
        }
    if (!count) return 0;
    float nearest2=16;
    for(int dy=-3;dy<=2;++dy) for(int dx=-3;dx<=2;++dx) {
        const auto t=sample(x+dx,y+dy);
        if(t.explored && t.terrain==Terrain::Water)
            nearest2=std::min(nearest2,(dx+.5f)*(dx+.5f)+(dy+.5f)*(dy+.5f));
    }
    const float slope=std::clamp((std::sqrt(nearest2)-.75f)/2.5f,0.f,1.f);
    return sum/count * slope*slope*(3-2*slope);
}
template <class Sample> std::array<float, 4> terrain_corners(int x, int y, const Sample &sample) {
    return {terrain_corner(x, y, sample), terrain_corner(x + 1, y, sample),
            terrain_corner(x + 1, y + 1, sample), terrain_corner(x, y + 1, sample)};
}
// Interpolate on the same NW-SE triangle diagonal used by the textured mesh.
// This keeps feet on the surface even halfway through a movement step.
template <class Sample> float terrain_height(float x, float y, const Sample &sample) {
    const int cx = static_cast<int>(std::floor(x + .5f)), cy = static_cast<int>(std::floor(y + .5f));
    const float u = x - cx + .5f, v = y - cy + .5f;
    const auto h = terrain_corners(cx, cy, sample);
    return u >= v ? h[0] * (1 - u) + h[1] * (u - v) + h[2] * v : h[0] * (1 - v) + h[2] * u + h[3] * (v - u);
}
} // namespace ribterra

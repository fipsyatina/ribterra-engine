#pragma once

#include "ribterra/render_frame.hpp"
#include <algorithm>
#include <cmath>

namespace ribterra {

// An overhead map keeps the world's screen-facing orientation, without the
// main camera's vertical foreshortening. Fit only known cells, not the empty
// corners of their world-axis bounding rectangle.
struct MinimapBounds {
    float min_u{}, min_v{}, max_u{}, max_v{};
    bool ready{};

    void include_cell(float x, float y) {
        const float u = x - y, v = x + y;
        if (!ready) {
            min_u = u - 1.f; max_u = u + 1.f;
            min_v = v - 1.f; max_v = v + 1.f;
            ready = true;
        } else {
            min_u = std::min(min_u, u - 1.f); max_u = std::max(max_u, u + 1.f);
            min_v = std::min(min_v, v - 1.f); max_v = std::max(max_v, v + 1.f);
        }
    }
};

struct MinimapProjection {
    render::Rect screen{};
    float center_u{}, center_v{}, scale{1.f};

    render::Point project(float world_x, float world_y) const {
        const float u=world_x-world_y;
        const float v=world_x+world_y;
        return {screen.x+screen.w*.5f+(u-center_u)*scale,
                screen.y+screen.h*.5f+(v-center_v)*scale};
    }
    render::Point unproject(float screen_x, float screen_y) const {
        const float u=center_u+(screen_x-(screen.x+screen.w*.5f))/scale;
        const float v=center_v+(screen_y-(screen.y+screen.h*.5f))/scale;
        return {(v+u)*.5f,(v-u)*.5f};
    }
    render::Point normalized(float world_x, float world_y) const {
        const auto point=project(world_x,world_y);
        return {(point.x-screen.x)/screen.w,(point.y-screen.y)/screen.h};
    }
};

inline MinimapProjection make_minimap_projection(render::Rect screen, const MinimapBounds &bounds,
                                                  float margin=1.f) {
    const float span_u=std::max(1.f,bounds.max_u-bounds.min_u+margin*2.f);
    const float span_v=std::max(1.f,bounds.max_v-bounds.min_v+margin*2.f);
    return {screen,(bounds.min_u+bounds.max_u)*.5f,(bounds.min_v+bounds.max_v)*.5f,
            std::max(.001f,std::min(screen.w/span_u,screen.h/span_v))};
}

} // namespace ribterra

#pragma once

#include "ribterra/building_catalog.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ribterra {

// Presentation-only selection for the authored building plates. Gameplay
// footprints, entry cells and upgrade rules remain authoritative elsewhere.
struct BuildingVisual {
    std::string asset_id;
    float width{};
    bool winter_tint{};
};

// Presentation-only ground treatment for a building which actually exists.
// Values are expressed in world cells and never affect placement or traversal.
struct BuildingApronVisual {
    float radius{};
    float entrance_reach{};
    float entrance_half_width{};
};

inline constexpr BuildingApronVisual building_apron_visual(BuildingKind kind) {
    switch (kind) {
    case BuildingKind::Farm: return {2.8f, 1.0f, .78f};
    case BuildingKind::Stockpile: return {2.6f, 1.15f, .82f};
    case BuildingKind::Workshop: return {2.7f, 1.0f, .74f};
    case BuildingKind::Mill: return {2.8f, 1.05f, .76f};
    case BuildingKind::Kitchen: return {2.55f, 1.0f, .72f};
    case BuildingKind::House: return {2.35f, .9f, .68f};
    case BuildingKind::Camp: return {2.3f, 1.1f, .8f};
    default: return {2.1f, .75f, .62f};
    }
}

struct BuildingApronMask { float cover{}, contact{}; };
inline BuildingApronMask building_apron_mask(BuildingKind kind, float bx, float by,
                                             float wx, float wy) {
    if (kind == BuildingKind::Road || kind == BuildingKind::Bridge) return {};
    const auto apron = building_apron_visual(kind);
    const float dx=wx-bx-.35f, dy=wy-by-.35f;
    const float irregular=.12f*std::sin(wx*2.3f+wy*.7f)+
                          .08f*std::sin(wy*3.1f-wx*.5f);
    constexpr float diagonal=.70710678f;
    const float along=(dx+dy)*diagonal, across=(dx-dy)*diagonal;
    const float distance=std::hypot(across/(apron.radius*1.08f),along/(apron.radius*.84f));
    const float footprint=std::clamp((1.f+irregular/apron.radius-distance)/.28f,0.f,1.f);
    const float entrance_distance=std::hypot(across/apron.entrance_half_width,
        (along-apron.radius*.72f)/(apron.radius*.32f+apron.entrance_reach));
    const float edge=std::max(footprint,std::clamp((1.f-entrance_distance)/.26f,0.f,1.f));
    const float shadow=std::clamp((.82f-distance)/.24f,0.f,1.f);
    return {edge*edge*(3.f-2.f*edge),shadow*shadow*(3.f-2.f*shadow)};
}

inline bool building_apron_clear(BuildingKind kind, float bx, float by, float wx, float wy) {
    // Follow the actual elliptical yard and forward entrance, not a large
    // empty circle. A small footprint buffer keeps low plants off the path.
    constexpr std::array<std::array<float,2>,5> offsets{{
        {{0,0}}, {{.2f,0}}, {{-.2f,0}}, {{0,.2f}}, {{0,-.2f}}}};
    for (const auto &offset : offsets)
        if (building_apron_mask(kind,bx,by,wx+offset[0],wy+offset[1]).cover > .02f)
            return false;
    return true;
}

constexpr bool building_has_level2_visual(BuildingKind kind) {
    return kind == BuildingKind::House || kind == BuildingKind::Workshop;
}

inline BuildingVisual resolve_building_visual(BuildingKind kind, int level, float construction_progress,
                                               bool winter) {
    const auto &definition = building_definition(kind);
    const auto progress = std::clamp(construction_progress, 0.f, 1.f);
    const char *state = nullptr;
    bool winter_tint = false;
    if (progress < 1.f) {
        state = progress < 1.f / 3 ? "stage1" : progress < 2.f / 3 ? "stage2" : "stage3";
    } else if (level >= 2 && building_has_level2_visual(kind)) {
        // Explicit snow artwork preserves the upgraded silhouette. Tinting the
        // summer plate never represented actual snow on its horizontal surfaces.
        state = winter ? "level2-winter" : "level2";
    } else {
        state = winter ? "winter" : "ready";
    }

    // Register the authored plates at a readable village scale. Decks retain
    // their exact cell coverage; object scale never changes traversal rules.
    const float scale = kind == BuildingKind::Road || kind == BuildingKind::Bridge ? 1.f
                        : kind == BuildingKind::Stockpile ? 2.4f
                        : kind == BuildingKind::Farm ? 2.5f
                        : kind == BuildingKind::House ? 2.1f
                        : kind == BuildingKind::Camp ? 1.8f
                        : kind == BuildingKind::Mine ? 1.8f : 2.f;
    return {"building." + std::string(definition.slug) + "." + state, definition.width * scale, winter_tint};
}

inline std::string building_preview_path(BuildingKind kind, int level) {
    return "art/building-" + std::string(building_definition(kind).slug) +
           (level >= 2 && building_has_level2_visual(kind) ? "-level2.png" : ".png");
}

} // namespace ribterra

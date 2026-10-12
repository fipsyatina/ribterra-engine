#include "ribterra/building_visual.hpp"
#include <iostream>
#include <stdexcept>

using namespace ribterra;

void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        check(resolve_building_visual(BuildingKind::House, 1, 0.f, false).asset_id ==
                  "building.house.stage1",
              "Empty construction site did not select stage1");
        check(resolve_building_visual(BuildingKind::House, 1, .34f, false).asset_id ==
                  "building.house.stage2",
              "Construction did not select stage2");
        check(resolve_building_visual(BuildingKind::House, 1, .67f, false).asset_id ==
                  "building.house.stage3",
              "Construction did not select stage3");
        check(resolve_building_visual(BuildingKind::House, 1, 1.f, false).asset_id ==
                  "building.house.ready",
              "Completed level-I house did not select ready art");
        check(resolve_building_visual(BuildingKind::House, 1, 1.f, true).asset_id ==
                  "building.house.winter",
              "Completed level-I house did not select winter art");

        const auto house2 = resolve_building_visual(BuildingKind::House, 2, 1.f, true);
        check(house2.asset_id == "building.house.level2-winter" && !house2.winter_tint,
              "Winter downgraded House II");
        const auto workshop2 = resolve_building_visual(BuildingKind::Workshop, 2, 1.f, true);
        check(workshop2.asset_id == "building.workshop.level2-winter" && !workshop2.winter_tint,
              "Winter downgraded Workshop II");

        for (const auto &definition : building_catalog) {
            const auto visual = resolve_building_visual(definition.kind, 2, 1.f, false);
            if (building_has_level2_visual(definition.kind))
                check(visual.asset_id.ends_with(".level2"), "Supported upgrade did not select level2 art");
            else
                check(!visual.asset_id.ends_with(".level2"), "Unavailable upgrade exposed level2 art");
            check(visual.width >= definition.width, "V03 object scale shrank a building");
        }
        check(resolve_building_visual(BuildingKind::House, 1, .4f, false).width ==
                  resolve_building_visual(BuildingKind::House, 1, 1.f, false).width,
              "Construction state changed the footprint-aligned visual width");
        check(resolve_building_visual(BuildingKind::House, 1, 1.f, false).width >= 100.f,
              "House is too small to read at the village zoom");
        check(resolve_building_visual(BuildingKind::Bridge, 1, 1.f, false).width ==
                  building_definition(BuildingKind::Bridge).width,
              "Village art scaling changed the bridge deck footprint");
        check(building_preview_path(BuildingKind::House, 2) == "art/building-house-level2.png",
              "House II preview path is not stable");
        check(building_preview_path(BuildingKind::Stockpile, 2) == "art/building-stockpile.png",
              "Unavailable Stockpile II preview was exposed");
        std::cout << "V03 building state, upgrade, winter and scale resolver checks passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

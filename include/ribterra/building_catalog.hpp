#pragma once
#include "ribterra/alpha_rules.hpp"
#include <array>
namespace ribterra {
struct BuildingDefinition {
    BuildingKind kind;
    const char *slug, *label_key, *name, *name_ua;
    Resources cost;
    float width;
};
inline constexpr std::array<BuildingDefinition, building_kind_count> building_catalog{{
    {BuildingKind::Camp, "camp", "app.title", "Camp", "Табір", {60, 30, 0}, 76},
    {BuildingKind::House, "house", "hud.house", "House", "Будинок", {30, 15, 0}, 76},
    {BuildingKind::Farm, "farm", "hud.farm", "Farm", "Ферма", {20, 10, 5}, 76},
    {BuildingKind::Stockpile, "stockpile", "hud.stockpile", "Stockpile", "Склад", {15, 5, 0}, 76},
    {BuildingKind::Mine, "mine", "building.mine", "Mine", "Шахта", {40, 30, 0}, 108},
    {BuildingKind::Workshop, "workshop", "building.workshop", "Workshop", "Майстерня", {45, 25, 0}, 96},
    {BuildingKind::Temple, "temple", "building.temple", "Temple", "Дім релігії", {50, 60, 0}, 100},
    {BuildingKind::Tavern, "tavern", "building.tavern", "Pub", "Паб", {60, 35, 10}, 96},
    {BuildingKind::Cartographer,
     "cartographer",
     "building.cartographer",
     "Cartographer",
     "Дім картографа",
     {40, 25, 0},
     96},
    {BuildingKind::Research,
     "research",
     "building.research",
     "Research house",
     "Дім досліджень",
     {65, 65, 0},
     100},
    {BuildingKind::Smelter, "smelter", "building.smelter", "Smeltery", "Плавильня", {45, 80, 0}, 104},
    {BuildingKind::Fishing,
     "fishing",
     "building.fishing",
     "Fishing station",
     "Рибальська станція",
     {30, 15, 0},
     104},
    {BuildingKind::Library, "library", "building.library", "Library", "Бібліотека", {70, 55, 0}, 100},
    {BuildingKind::Barracks, "barracks", "building.barracks", "Barracks", "Військовий дім", {75, 65, 0}, 104},
    {BuildingKind::Bridge, "bridge", "building.bridge", "Bridge", "Міст", {12, 4, 0}, 74},
    {BuildingKind::Sawmill, "lumbermill", "building.sawmill", "Sawmill", "Лісопилка", {35,20,0}, 96},
    {BuildingKind::Mill, "mill", "building.mill", "Mill", "Млин", {35,25,0}, 96},
    {BuildingKind::Kitchen, "kitchen", "building.kitchen", "Kitchen", "Кухня", {30,20,0}, 96},
    {BuildingKind::ClayPit, "claypit", "building.claypit", "Clay pit", "Глиняний кар’єр", {15,10,0}, 96},
    {BuildingKind::Road, "road", "building.road", "Road", "Дорога", {1,2,0}, 64},
}};
inline const BuildingDefinition &building_definition(BuildingKind kind) {
    return building_catalog[valid_building_kind(kind) ? static_cast<unsigned>(kind) : 0];
}
constexpr bool building_ground_allowed(BuildingKind kind, const Tile &tile) {
    return kind == BuildingKind::Bridge ? tile.terrain == Terrain::Water
                                        : tile.terrain == Terrain::Grass ||
                                              (kind == BuildingKind::Mine && tile.terrain == Terrain::Rock);
}
// Completed deck sections turn water into traversable ground without changing the lake.
constexpr bool building_passage(BuildingKind kind, float progress) {
    return progress == 1 && (kind == BuildingKind::Mine || kind == BuildingKind::Bridge || kind == BuildingKind::Road);
}
template <class Reachable> bool bridge_connected(int x, int y, const Reachable &reachable) {
    for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}})
        if (reachable(x + dx, y + dy))
            return true;
    return false;
}
template <class Sample> bool fishing_shore(int x, int y, const Sample &sample) {
    for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}}) {
        const auto &tile = sample(x + dx, y + dy);
        if (tile.explored && tile.terrain == Terrain::Water)
            return true;
    }
    return false;
}
} // namespace ribterra

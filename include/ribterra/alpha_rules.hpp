#pragma once
#include "ribterra/world.hpp"

// One policy for the simulation, local client, network host and dedicated server.
// Catalog entries, recipe IDs, assets and saved historical objects remain intact.
namespace ribterra::alpha {
inline constexpr bool research = false;
inline constexpr bool expeditions = false;
inline constexpr bool animal_attacks = false;
inline constexpr bool forced_work = false;

constexpr bool buildable(BuildingKind kind) {
    switch (kind) {
    case BuildingKind::House: case BuildingKind::Farm: case BuildingKind::Stockpile:
    case BuildingKind::Mine: case BuildingKind::Workshop: case BuildingKind::Fishing:
    case BuildingKind::Bridge: case BuildingKind::Sawmill: case BuildingKind::Mill:
    case BuildingKind::Kitchen: case BuildingKind::ClayPit:
        return true;
    default: return false;
    }
}
constexpr bool recipe_available(int id) {
    return id >= 0 && id < int(recipes.size()) &&
           recipes[id].output != int(ItemKind::Tools) && recipes[id].output != int(ItemKind::Spear);
}
constexpr bool active_skill(std::size_t id) { return id <= std::size_t(Skill::Hauling); }
} // namespace ribterra::alpha

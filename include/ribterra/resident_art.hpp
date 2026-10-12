#pragma once
#include "ribterra/world.hpp"
#include <string>

namespace ribterra {
// Presentation follows authoritative carried items, not a job title or a planned pickup.
enum class ResidentLoad { Empty, Logs, Sack, Cart };
inline ResidentLoad resident_load(ItemKind item, std::uint32_t quantity) {
    if (!quantity) return ResidentLoad::Empty;
    if (item == ItemKind::Wood || item == ItemKind::Firewood) return ResidentLoad::Logs;
    if (quantity >= 6 && (item == ItemKind::Stone || item == ItemKind::Clay ||
                         item == ItemKind::Bricks || item == ItemKind::Planks ||
                         item == ItemKind::Tools || item == ItemKind::Spear)) return ResidentLoad::Cart;
    return ResidentLoad::Sack;
}
inline std::string resident_sprite(int id, ResidentLoad load, int facing, int phase) {
    static constexpr const char *loads[]{"walk", "logs", "sack", "cart"};
    return std::string("resident.") + (id % 2 ? "man." : "woman.") +
           loads[static_cast<int>(load)] + "." + std::to_string(facing) + "." + std::to_string(phase);
}
// All atlas cells are registered at a common body scale. The cart plates have
// slightly smaller people to leave room for the prop, so compensate in presentation.
inline float resident_sprite_width(ResidentLoad load) {
    return load == ResidentLoad::Cart ? 45.f : 39.f;
}
inline float resident_contact_shadow_half_width(ResidentLoad load) {
    return load == ResidentLoad::Cart ? 14.f : 9.5f;
}
inline constexpr float resident_contact_shadow_half_height = 3.2f;
}

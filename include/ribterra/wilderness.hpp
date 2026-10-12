#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace ribterra {
enum class AnimalSpecies { Deer, Hare, Boar, Wolf };
enum class AnimalBehaviour { Feeding, Resting, Fleeing, Attacking, Dead };
enum class ExpeditionKind { Hunt, Resources, Explore };
enum class ExpeditionPhase { Travel, Search, Approach, Hunt, Processing, Returning, Complete, Defence, Retreat };
enum class WildernessAction { Hunt, Resources, Explore, Recall, ContinueNight, Attack, Retreat, Rations, Populate };
struct Animal {
 int id{},herd{}; AnimalSpecies species{}; AnimalBehaviour behaviour{};
 float x{},y{},home_x{},home_y{}; int health{30},target{};
 std::uint64_t alarm_until{},next_attack{};
 bool operator==(const Animal&) const=default;
};
struct Expedition {
 int id{}; ExpeditionKind kind{}; ExpeditionPhase phase{};
 std::vector<int> members; int x{},y{},radius{12},quota{1},completed{},target{},specific_target{};
 int progress{},spears{}; std::uint64_t deadline{},night_prompt{},night_permission{};
 std::string outcome;
 bool operator==(const Expedition&) const=default;
};
struct WildernessState {
 bool enabled{},reduced_rations{}; int next_animal{1},next_expedition{1};
 std::uint64_t revision{}; std::vector<Animal> animals; std::vector<Expedition> expeditions;
 bool operator==(const WildernessState&) const=default;
};
inline bool expedition_active(const Expedition&e){return e.phase!=ExpeditionPhase::Complete;}
bool valid_wilderness(const WildernessState&);
}

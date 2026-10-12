#pragma once
#include <array>
#include "ribterra/wilderness.hpp"
#include "ribterra/settlement.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace ribterra {
inline constexpr int economy_item_count=15;
// Item indices match ItemKind; the original Food remains cooked/ready-to-eat food.
inline constexpr std::array<const char*,economy_item_count> economy_item_keys{
 "wood","stone","food","planks","clay","bricks","grain","vegetables","fish","meat","flour","tools","firewood","hide","spear"};
struct Recipe {
 const char *key; int building; int input1,quantity1,input2,quantity2,output,quantity; int ticks;
};
inline constexpr Recipe upgrade_recipe{"upgrade",-1,3,6,5,4,-1,0,800};
inline constexpr std::array<Recipe,15> recipes{{
 {"grain",2,-1,0,-1,0,6,8,400}, {"vegetables",2,-1,0,-1,0,7,8,400},
 {"fish",11,-1,0,-1,0,8,6,400}, {"planks",15,0,3,-1,0,3,6,300},
 {"flour",16,6,4,-1,0,10,4,300}, {"bread",17,10,3,12,1,2,8,300},
 {"fish_meal",17,8,3,12,1,2,8,300}, {"meat_meal",17,9,3,12,1,2,8,300},
 {"bricks",5,4,4,12,1,5,4,400}, {"tools",5,3,2,1,2,11,2,400},
 {"firewood",15,0,2,-1,0,12,6,200}, {"clay",18,-1,0,-1,0,4,6,400},
 {"husbandry",2,6,6,-1,0,9,4,600}, {"vegetable_meal",17,7,4,12,1,2,8,300}, {"spear",5,3,2,1,1,14,1,300}
}};
enum class ProductionPhase { Materials, Delivery, Travelling, Working, Output, Complete, Cancelled, Blocked };
struct ProductionOrder {
 int id{},building_id{},resident_id{},recipe{},requested{1},completed{},work{};
 ProductionPhase phase{ProductionPhase::Materials};
 std::uint64_t logistics_id{};
 bool upgrade{};
 std::string reason;
 bool cancel_requested{}; // Persisted intent; keep the physical phase until its delivery boundary.
 bool operator==(const ProductionOrder&) const=default;
};
struct BuildingUpgrade {int building_id{},level{2};bool operator==(const BuildingUpgrade&) const=default;};
struct NationState {
 int id{},x{},y{}; SettlementState policy; std::string explored;
 bool operator==(const NationState&)const=default;
};
inline constexpr std::size_t nation_fog_size=2*832*832/4;
struct EconomyState {
 std::vector<NationState> nations;
 WildernessState wilderness;
 std::uint64_t revision{};int next_order_id{1};
 std::vector<ProductionOrder> orders;
 std::vector<BuildingUpgrade> upgrades;
 std::array<int,economy_item_count> stored{},reserved{},carried{},ground{}; // Snapshot projection, never authoritative inventory.
 bool operator==(const EconomyState&) const=default;
};
std::string encode_economy(const EconomyState&);
EconomyState decode_economy(const std::string&);
bool valid_economy(const EconomyState&);
inline bool production_active(const ProductionOrder &o){return o.phase!=ProductionPhase::Complete && o.phase!=ProductionPhase::Cancelled;}
}

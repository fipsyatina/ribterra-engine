#include "ribterra/economy.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace ribterra {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(YoungResident,resident_id,adult_tick,forced_work)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SettlementState,revision,arrival_tick,forced_night,night_discontent,youth_discontent,young,reduced_rations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NationState,id,x,y,policy,explored)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Animal,id,herd,species,behaviour,x,y,home_x,home_y,health,target,alarm_until,next_attack)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Expedition,id,kind,phase,members,x,y,radius,quota,completed,target,specific_target,progress,spears,deadline,night_prompt,night_permission,outcome)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WildernessState,enabled,reduced_rations,next_animal,next_expedition,revision,animals,expeditions)
bool valid_wilderness(const WildernessState&w){
 if(w.animals.size()>128||w.expeditions.size()>64||w.next_animal<1||w.next_expedition<1)return false;
 std::set<int> ids,orders,members;
 for(const auto&a:w.animals)if(a.id<1||a.id>=w.next_animal||!ids.insert(a.id).second||int(a.species)<0||int(a.species)>3||int(a.behaviour)<0||int(a.behaviour)>4||a.health<0||a.health>100||!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.home_x)||!std::isfinite(a.home_y)||a.x<-96||a.x>=736||a.y<-96||a.y>=736||a.home_x<-96||a.home_x>=736||a.home_y<-96||a.home_y>=736)return false;
 for(const auto&e:w.expeditions){
  if(e.id<1||e.id>=w.next_expedition||!orders.insert(e.id).second||int(e.kind)<0||int(e.kind)>2||int(e.phase)<0||int(e.phase)>8||e.members.empty()||e.members.size()>16||e.x<-96||e.x>=736||e.y<-96||e.y>=736||e.radius!=12||e.quota<1||e.quota>10||e.completed<0||e.completed>e.quota||e.progress<0||e.progress>200||e.spears<0||e.spears>int(e.members.size())||e.outcome.size()>80)return false;
  std::set<int> own;for(int id:e.members)if(id<1||!own.insert(id).second||(expedition_active(e)&&!members.insert(id).second))return false;
 }
 return true;
}
std::string encode_economy(const EconomyState&s){
 if(s==EconomyState{})return {};
 json j={{"revision",s.revision},{"next",s.next_order_id},{"stored",s.stored},{"reserved",s.reserved},{"orders",json::array()},{"upgrades",json::array()}};
 if(s.carried!=decltype(s.carried){})j["carried"]=s.carried;
 if(s.ground!=decltype(s.ground){})j["ground"]=s.ground;
 for(const auto&o:s.orders)j["orders"].push_back({o.id,o.building_id,o.resident_id,o.recipe,o.requested,o.completed,o.work,int(o.phase),o.logistics_id,o.upgrade,o.reason,o.cancel_requested});
 for(const auto&u:s.upgrades)j["upgrades"].push_back({u.building_id,u.level});
 if(!s.nations.empty())j["nations"]=s.nations;
 if(s.wilderness!=WildernessState{})j["wilderness"]=s.wilderness;
 return j.dump();
}
bool valid_economy(const EconomyState&s){
 if(!valid_wilderness(s.wilderness))return false;
 if(s.nations.size()>6)return false;
 std::set<int> nations;for(const auto&n:s.nations)if(n.id<1||n.id>6||!nations.insert(n.id).second||n.x<-80||n.x>720||n.y<-80||n.y>720||n.explored.size()!=nation_fog_size||n.explored.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;
 if(s.orders.size()>256||s.upgrades.size()>2048||s.next_order_id<1)return false;
 for(int i=0;i<economy_item_count;++i)if(s.stored[i]<0||s.reserved[i]<0||s.carried[i]<0||s.ground[i]<0)return false;
 std::set<int>ids,workers,buildings,upgraded;
 for(const auto&o:s.orders){
  if(o.id<1||o.id>=s.next_order_id||!ids.insert(o.id).second||o.building_id<1||o.resident_id<1||o.recipe<0||o.recipe>=int(recipes.size())||o.requested<1||o.requested>20||o.completed<0||o.completed>o.requested||o.work<0||o.work>800||int(o.phase)<0||int(o.phase)>7||o.reason.size()>80)return false;
  if(production_active(o)&&(!workers.insert(o.resident_id).second||!buildings.insert(o.building_id).second))return false;
  if(o.cancel_requested&&!production_active(o))return false;
 }
 for(const auto&u:s.upgrades)if(u.building_id<1||u.level!=2||!upgraded.insert(u.building_id).second)return false;
 return true;
}
EconomyState decode_economy(const std::string&text){
 if(text.empty())return {};
 if(text.size()>4*1024*1024)throw std::runtime_error("Economy payload too large");
 auto j=json::parse(text);EconomyState s;
 if(j.contains("nations")){if(j["nations"].size()>6)throw std::runtime_error("Too many nations");s.nations=j["nations"].get<std::vector<NationState>>();}
 s.revision=j.at("revision").get<std::uint64_t>();s.next_order_id=j.at("next").get<int>();
 for(auto key:{"stored","reserved","carried","ground"}){
  if(!j.contains(key)&&(std::string_view(key)=="carried"||std::string_view(key)=="ground"))continue;
  const auto&v=j.at(key);if(!v.is_array()||(v.size()!=13&&v.size()!=15))throw std::runtime_error("Invalid item projection");
  auto&out=std::string_view(key)=="stored"?s.stored:std::string_view(key)=="reserved"?s.reserved:std::string_view(key)=="carried"?s.carried:s.ground;
  for(std::size_t i=0;i<v.size();++i)out[i]=v[i].get<int>();
 }
 if(j.contains("wilderness")){
  const auto&w=j.at("wilderness");if(w.at("animals").size()>128||w.at("expeditions").size()>64)throw std::runtime_error("Wilderness collections too large");
  s.wilderness=w.get<WildernessState>();
 }
 if(j.at("orders").size()>256||j.at("upgrades").size()>2048)throw std::runtime_error("Economy collections too large");
 for(const auto&o:j.at("orders")){
  if(!o.is_array()||(o.size()!=11&&o.size()!=12))throw std::runtime_error("Invalid production order");
  s.orders.push_back({o.at(0).get<int>(),o.at(1).get<int>(),o.at(2).get<int>(),o.at(3).get<int>(),o.at(4).get<int>(),o.at(5).get<int>(),o.at(6).get<int>(),ProductionPhase(o.at(7).get<int>()),o.at(8).get<std::uint64_t>(),o.at(9).get<bool>(),o.at(10).get<std::string>(),o.size()==12?o.at(11).get<bool>():false});
 }
 for(const auto&u:j.at("upgrades"))s.upgrades.push_back({u.at(0).get<int>(),u.at(1).get<int>()});
 if(!valid_economy(s))throw std::runtime_error("Invalid economy state");return s;
}
}

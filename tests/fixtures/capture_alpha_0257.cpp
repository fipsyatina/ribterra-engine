// Compile only against the frozen 0fe2454 (0.25.7) source and libraries.
// Scenarios edit WorldState deliberately, validate it with the old simulation,
// and serialize with the old writer. No save header or payload is rewritten.
#include "ribterra/world_state.hpp"
#include "ribterra/foundation.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
using Json=nlohmann::json;
void check(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
void load(World& w,WorldState state){std::string error;check(w.import_state(std::move(state),error),error);}
Json observe(const World& world) {
 auto s=world.export_state();Json j={{"buildings",Json::array()},{"upgrades",Json::array()},{"people",Json::array()},{"items",Json::array()}};
 for(auto&b:s.buildings)j["buildings"].push_back({b.id,int(b.kind),b.x,b.y,b.progress,int(b.layer),b.nation});
 for(auto&u:s.economy.upgrades)j["upgrades"].push_back({u.building_id,u.level});
 for(auto&r:s.residents)j["people"].push_back({r.id,r.name,r.nation,r.health,r.x,r.y});
 for(auto&i:s.item_stacks)j["items"].push_back({i.stack_id,int(i.item),i.quantity,i.owner_nation,int(i.location.kind),i.location.cell.x,i.location.cell.y,int(i.location.cell.layer),i.location.entity_id});
 return j;
}
int main(int argc,char**argv){try{
 check(argc==2,"Output directory required");check(engine_version=="0.25.7"&&rules_version==14,"Wrong writer version");
 const auto out=std::filesystem::path(argv[1]);std::filesystem::create_directories(out);
 World original(70421,2);original.create_nations({1,2},2,2);auto state=original.export_state();
 const auto camp=state.buildings.front();int x=camp.x+2;
 for(auto kind:{BuildingKind::Temple,BuildingKind::Tavern,BuildingKind::Cartographer,BuildingKind::Research,BuildingKind::Smelter,BuildingKind::Library,BuildingKind::Barracks,BuildingKind::House,BuildingKind::Workshop}){
  const auto id=state.next_building_id++;state.buildings.push_back({id,kind,x++,camp.y+4,1,WorldLayer::Surface,id%2?1u:2u});
  if(kind==BuildingKind::House||kind==BuildingKind::Workshop)state.economy.upgrades.push_back({id,2});
 }
 const int sawmill=state.next_building_id++;state.buildings.push_back({sawmill,BuildingKind::Sawmill,camp.x+3,camp.y+2,1,WorldLayer::Surface,1});
 for(const auto&b:state.buildings)if(b.kind==BuildingKind::Camp)for(int item=3;item<economy_item_count;++item)
  state.item_stacks.push_back({state.next_item_stack_id++,ItemKind(item),std::uint32_t(item)+b.nation,b.nation,{ItemLocationKind::Storage,{},b.id}});
 state.economy.wilderness.enabled=true;state.economy.wilderness.next_animal=2;
 state.economy.wilderness.animals={{1,1,AnimalSpecies::Wolf,AnimalBehaviour::Feeding,float(camp.x+1),float(camp.y+1),float(camp.x+1),float(camp.y+1),36}};
 load(original,state);state=original.export_state();Json manifest={{"writer","0.25.7"},{"source_commit","0fe2454f45aeb4087f5832fe0c42d7b8303bac99"},{"rules",rules_version},{"generator",generator_version},{"content",default_content_hash()},{"method","Synthetic states validated and saved by frozen 0.25.7; ordinary cargo produced by real production"},{"fixtures",Json::array()}};
 auto save=[&](const char* name,WorldState data,bool supported){World world(1,0);load(world,std::move(data));std::string error;const auto file=out/(std::string(name)+".ribsave");check(world.save(file,error),error);std::ifstream in(file,std::ios::binary);std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};auto expected=observe(world);std::ofstream(out/(std::string(name)+".expected.json"))<<expected.dump(2);manifest["fixtures"].push_back({{"name",name},{"supported",supported},{"sha256",sha256(bytes)},{"bytes",bytes.size()}});};
 save("historical",state,true);
 auto ration=state;ration.settlement.reduced_rations=ration.economy.wilderness.reduced_rations=true;for(auto&n:ration.economy.nations)n.policy.reduced_rations=true;save("rations",ration,true);
 auto attack=state;attack.economy.wilderness.animals[0].behaviour=AnimalBehaviour::Attacking;attack.economy.wilderness.animals[0].target=state.residents.front().id;attack.economy.wilderness.animals[0].next_attack=state.ticks;save("attack",attack,true);
 World cargo(1,0);auto production_state=state;production_state.economy.wilderness.enabled=false;load(cargo,production_state);check(cargo.production(sawmill,state.residents.front().id,3,1,0).accepted,"Production refused");bool found=false;
 for(int tick=0;tick<3000&&!found;++tick){cargo.tick();for(auto&i:cargo.item_stacks())if(i.item==ItemKind::Planks&&i.location.kind==ItemLocationKind::Carried)found=true;}
 if(!found){for(auto&o:cargo.economy().orders)std::cerr<<"production phase "<<int(o.phase)<<" reason "<<o.reason<<" work "<<o.work<<"\n";for(auto&r:cargo.residents())std::cerr<<"resident "<<r.id<<" nation "<<r.nation<<" pos "<<r.x<<","<<r.y<<" job "<<int(r.job)<<"\n";}
 check(found,"Cargo not produced");auto carried=cargo.export_state();carried.economy.wilderness=state.economy.wilderness;save("cargo",carried,true);
 carried.settlement.reduced_rations=carried.economy.wilderness.reduced_rations=true;for(auto&n:carried.economy.nations)n.policy.reduced_rations=true;
 carried.economy.wilderness.animals[0].behaviour=AnimalBehaviour::Attacking;carried.economy.wilderness.animals[0].target=state.residents.front().id;save("combined",carried,true);
 for(auto [phase,name]:{std::pair{ExpeditionPhase::Travel,"expedition-travel"},std::pair{ExpeditionPhase::Hunt,"expedition-hunt"},std::pair{ExpeditionPhase::Returning,"expedition-returning"},std::pair{ExpeditionPhase::Defence,"expedition-defence"}}){auto exp=state;Expedition e;e.id=1;e.members={state.residents.front().id};e.x=camp.x+6;e.y=camp.y;e.phase=phase;exp.economy.wilderness.next_expedition=2;exp.economy.wilderness.expeditions={e};save(name,exp,false);}
 std::ofstream(out/"provenance.json")<<manifest.dump(2)<<'\n';std::cout<<manifest.dump(2)<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

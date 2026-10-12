#include "ribterra/world_state.hpp"
#include "ribterra/protocol.hpp"
#include "weather_fixture.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>
using namespace ribterra;
void check(bool b,const std::string&m){if(!b)throw std::runtime_error(m);}
void load(World&w,WorldState s){std::string error;check(w.import_state(std::move(s),error),error);}
World fixture(int count=3){
 World w(70421,count);auto s=w.export_state();
 for(auto&c:s.chunks)if(c.key.layer==WorldLayer::Surface)for(auto&t:c.tiles)t={Terrain::Grass,0,true,Biome::Meadow};
 s.economy.wilderness.enabled=true;
 for(auto&r:s.residents){r.x=32+float(r.id%2);r.y=30;r.target_x=int(r.x);r.target_y=30;}
 load(w,s);return w;
}
CommandResult send(World&w,WildernessAction a,std::vector<int>members={},int x=34,int y=30,int argument=1){return w.wilderness(a,members,x,y,argument,w.economy().wilderness.revision);}
void roundtrip(World&w){auto path=std::filesystem::temp_directory_path()/"ribterra-wilderness.save";std::string error;check(w.save(path,error),error);World restored(0,0);check(restored.load(path,error),error);check(w.checksum()==restored.checksum(),"Save checksum mismatch");for(int i=0;i<5;++i){w.tick();restored.tick();check(w.checksum()==restored.checksum(),"Continuation diverged");}std::filesystem::remove(path);}
void empty_and_recall(){auto w=fixture();check(send(w,WildernessAction::Hunt,{1,2}).accepted,"Hunt rejected");check(!send(w,WildernessAction::Explore,{1}).accepted,"Double assignment");roundtrip(w);for(int i=0;i<4200&&expedition_active(w.economy().wilderness.expeditions.front());++i)w.tick();auto e=w.economy().wilderness.expeditions.front();check(!expedition_active(e)&&e.completed==0,"Empty hunt did not return");check(w.economy().stored[9]==0,"Empty hunt created meat");auto r=fixture();check(send(r,WildernessAction::Explore,{1},50,30).accepted,"Explore rejected");for(int i=0;i<50;++i)r.tick();check(send(r,WildernessAction::Recall).accepted,"Recall rejected");for(int i=0;i<700&&expedition_active(r.economy().wilderness.expeditions.front());++i)r.tick();check(!expedition_active(r.economy().wilderness.expeditions.front()),"Recall never returned");}
void night(){auto w=fixture();check(send(w,WildernessAction::Hunt,{1,2}).accepted,"Night fixture hunt rejected");auto s=w.export_state();test::set_fixture_simulation_tick(s,daylight_ticks-1);s.economy.wilderness.expeditions[0].deadline=daylight_ticks+1000;load(w,s);w.tick();check(w.economy().wilderness.expeditions[0].outcome=="night","Night did not default return");check(send(w,WildernessAction::ContinueNight).accepted,"Night permission rejected");check(w.settlement().night_discontent==5,"Night cost wrong");check(send(w,WildernessAction::ContinueNight).accepted,"Repeated permission rejected");check(w.settlement().night_discontent==5,"Repeated permission charged");check(!w.settlement().night_shift(w.calendar().tick),"Party permission became global");roundtrip(w);}
void night_processing(){
 auto w=fixture();auto s=w.export_state();test::set_fixture_simulation_tick(s,daylight_ticks-1);
 auto&v=s.economy.wilderness;v.animals={{1,1,AnimalSpecies::Deer,AnimalBehaviour::Dead,34,30,34,30,0}};v.next_animal=2;
 Expedition e;e.id=1;e.members={1,2};e.x=34;e.y=30;e.target=1;e.specific_target=1;e.phase=ExpeditionPhase::Processing;e.progress=50;e.deadline=daylight_ticks+3000;v.expeditions={e};v.next_expedition=2;load(w,s);
 w.tick();check(w.economy().wilderness.expeditions[0].outcome=="night","Processing did not return at night");roundtrip(w);
 check(send(w,WildernessAction::ContinueNight).accepted,"Processing continuation rejected");
 for(int i=0;i<1000&&expedition_active(w.economy().wilderness.expeditions[0]);++i)w.tick();
 check(w.economy().stored[9]==8&&w.economy().stored[13]==1,"Night continuation lost carcass");
}
void hunt_and_cargo(){auto w=fixture();auto s=w.export_state();s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Spear,3,1,{ItemLocationKind::Storage,{},1}});auto&v=s.economy.wilderness;v.animals.push_back({1,1,AnimalSpecies::Deer,AnimalBehaviour::Resting,34,30,34,30,24});v.next_animal=2;load(w,s);check(send(w,WildernessAction::Hunt,{1,2,3},34,30,17).accepted,"Target hunt rejected");bool physical=false;
 for(int i=0;i<2000&&expedition_active(w.economy().wilderness.expeditions.front());++i){w.tick();for(const auto&item:w.item_stacks())if((item.item==ItemKind::Meat||item.item==ItemKind::Hide)&&item.location.kind==ItemLocationKind::Carried){physical=true;check(w.economy().stored[9]==0,"Meat credited before delivery");}if(i==190)roundtrip(w);}
 check(physical,"No physical cargo");check(w.economy().stored[9]==8&&w.economy().stored[13]==1,"Meat/hide not delivered");check(!expedition_active(w.economy().wilderness.expeditions.front()),"Hunt never finished");roundtrip(w);}
void defence_and_resources(){
 auto w=fixture();auto s=w.export_state();s.economy.wilderness.animals={{1,1,AnimalSpecies::Wolf,AnimalBehaviour::Attacking,32,30,32,30,36,1}};s.economy.wilderness.next_animal=2;load(w,s);
 check(send(w,WildernessAction::Explore,{1,2,3},45,30).accepted,"Defence fixture rejected");w.tick();
 check(w.economy().wilderness.expeditions[0].phase==ExpeditionPhase::Defence,"Actual hit did not interrupt work");
 check(w.residents()[0].health<100,"Attack did no damage");
 check(send(w,WildernessAction::Retreat,{},28,26).accepted,"Retreat rejected");roundtrip(w);
 auto resources=fixture();s=resources.export_state();for(auto&c:s.chunks)if(c.key==chunk_key({34,30}))c.tiles[local_index(34,30)]={Terrain::Forest,30,true,Biome::Forest};load(resources,s);
 check(send(resources,WildernessAction::Resources,{1,2},34,30,5).accepted,"Resource expedition rejected");
 for(int i=0;i<1600&&expedition_active(resources.economy().wilderness.expeditions[0]);++i)resources.tick();
 check(resources.economy().wilderness.expeditions[0].completed==5,"Resource quota wrong");check(resources.inventory().stored.wood==85,"Resources not delivered");roundtrip(resources);
}
void ecology(){World w(70421,3);w.enable_wildlife();const auto before=w.economy().wilderness;check(before.animals.size()>20,"Wildlife not populated");for(const auto&a:before.animals)check(w.tile(int(a.x),int(a.y)).biome!=Biome::Desert,"Deer in desert");for(int i=0;i<60;++i)w.tick();check(w.economy().wilderness.animals!=before.animals,"Animals static");roundtrip(w);auto s=w.export_state();s.economy.wilderness.animals[0].x=99999;std::string error;check(!w.import_state(s,error),"Invalid animal accepted");}
void rations_wire(){auto w=fixture();check(send(w,WildernessAction::Rations).accepted,"Rations rejected");check(w.economy().wilderness.reduced_rations&&w.settlement().discontent()==10,"Rations missing or cost wrong");auto s=w.economy();check(decode_economy(encode_economy(s))==s,"Wilderness codec loss");roundtrip(w);check(send(w,WildernessAction::Rations,{},0,0,0).accepted,"Rations restore rejected");GameCommand c;c.type=CommandType::Wilderness;c.building=BuildingKind(WildernessAction::Rations);c.author=1;c.request_id=1;auto bytes=net::encode({1,c});std::string error;auto message=net::decode(bytes,error);check(message.has_value(),"Wilderness command rejected by wire");}
int main(){try{
 ecology();auto w=fixture();const auto before=w.checksum();
 for(auto action:{WildernessAction::Hunt,WildernessAction::Explore,WildernessAction::Resources,WildernessAction::Attack,WildernessAction::Rations,WildernessAction::Recall,WildernessAction::ContinueNight})
  check(!send(w,action,{1}).accepted&&w.checksum()==before,"Alpha accepted or mutated a deferred action");
 auto s=w.export_state();s.economy.wilderness.animals={{1,1,AnimalSpecies::Wolf,AnimalBehaviour::Feeding,32,30,32,30,36},{2,2,AnimalSpecies::Boar,AnimalBehaviour::Feeding,33,30,33,30,48}};s.economy.wilderness.next_animal=3;load(w,s);
 for(int i=0;i<300;++i){w.tick();for(const auto&r:w.residents())check(r.health==100,"Passive wildlife damaged a resident");for(const auto&a:w.economy().wilderness.animals)check(a.behaviour!=AnimalBehaviour::Attacking,"Passive wildlife started an attack");}
 roundtrip(w);std::cout<<"Alpha passive wildlife, deferred action rejection, persistence and ecology passed.\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

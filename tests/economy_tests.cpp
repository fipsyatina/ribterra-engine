#include "ribterra/world_state.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/building_catalog.hpp"
#include "reachable_surface_fixture.hpp"
#include "weather_fixture.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
void check(bool b,const std::string&m){if(!b)throw std::runtime_error(m);}
void load(World&w,WorldState s){std::string error;check(w.import_state(std::move(s),error),error);}
World fixture(BuildingKind kind,int input1=-1,int q1=0,int input2=-1,int q2=0){
 World w(70421,1);auto s=w.export_state();int x=26,y=26;
 if(kind==BuildingKind::Fishing){std::string error;check(test::reveal_surface_radius(w,32,error),error);s=w.export_state();const auto shore=test::reachable_fishing_site(w);check(bool(shore),"No reachable test shore");x=shore->x;y=shore->y;}
 s.buildings.push_back({s.next_building_id++,kind,x,y,1,WorldLayer::Surface,1});
 if(kind==BuildingKind::Stockpile)s.storages.push_back({2,5000,7,0});
 for(auto [k,q]:{std::pair{input1,q1},std::pair{input2,q2}})if(k>=0){
  s.item_stacks.push_back({s.next_item_stack_id++,ItemKind(k),unsigned(q),1,{ItemLocationKind::Storage,{},1}});
  if(k==0)s.resources.wood+=q;if(k==1)s.resources.stone+=q;if(k==2)s.resources.food+=q;
 }
 load(w,s);return w;
}
void roundtrip(World&w){
 auto path=std::filesystem::temp_directory_path()/"ribterra-economy-test.save";std::string error;
 check(w.save(path,error),error);World restored(1,0);check(restored.load(path,error),error);
 check(w.checksum()==restored.checksum(),"Economy save checksum changed");
 for(int n=0;n<3;++n){w.tick();restored.tick();check(w.checksum()==restored.checksum(),"Economy continuation diverged");}
 std::filesystem::remove(path);
}
void recipes_test(){
 for(int i=0;i<int(recipes.size());++i){
  const auto&r=recipes[i];auto w=fixture(BuildingKind(r.building),r.input1,r.quantity1,r.input2,r.quantity2);
  if(!alpha::recipe_available(i)){auto before=w.checksum();check(!w.production(2,1,i,1,0).accepted&&w.checksum()==before,"Deferred recipe mutated world");continue;}
  check(w.production(2,1,i,1,0).accepted,"Production rejected "+std::to_string(i));
  check(!w.production(2,1,i,1,0).accepted,"Duplicate order admitted");
  bool snapshots[8]{};
  for(int n=0;n<4000 && production_active(w.economy().orders.front());++n){
   w.tick();auto phase=int(w.economy().orders.front().phase);
   if(!snapshots[phase]){snapshots[phase]=true;roundtrip(w);}
  }
  const auto e=w.economy();check(e.orders.front().phase==ProductionPhase::Complete,"Recipe did not finish "+std::to_string(i)+" phase "+std::to_string(int(e.orders.front().phase))+" "+e.orders.front().reason);
  check(e.stored[r.output]>=r.quantity,"Output was not physically delivered");
  for(int n=0;n<50;++n)w.tick();check(w.economy().orders.front().completed==1,"Finite order restarted");
 }
}
void constraints(){
 auto w=fixture(BuildingKind::Fishing);for(int n=0;n<800;++n)w.tick();check(w.economy().stored[8]==0,"Fishing produced without worker");
 auto missing=fixture(BuildingKind::Mill);check(missing.production(2,1,4,1,0).accepted,"Missing material order rejected");for(int n=0;n<50;++n)missing.tick();
 check(missing.economy().orders.front().phase==ProductionPhase::Materials&&missing.economy().stored[10]==0,"Recipe fabricated inputs");
 check(missing.cancel_production(1,missing.economy().revision).accepted,"Cancel failed");
 auto night=fixture(BuildingKind::Sawmill);auto state=night.export_state();test::set_fixture_simulation_tick(state,daylight_ticks);load(night,state);
 check(night.production(2,1,3,1,0).accepted,"Night order not queued");for(int n=0;n<70;++n)night.tick();check(night.economy().orders.front().work==0,"Night production advanced");
 check(!night.decide(SettlementDecision::NightShift,0).accepted,"Alpha forced night work allowed");state=night.export_state();test::set_fixture_simulation_tick(state,ticks_per_day);load(night,state);for(int n=0;n<1500&&production_active(night.economy().orders.front());++n)night.tick();check(night.economy().stored[3]==6,"Forced night shift failed");
 auto upgrade=fixture(BuildingKind::House,3,6,5,4);int housing=upgrade.housing();check(upgrade.production(2,1,0,1,0,true).accepted,"Upgrade rejected");
 for(int n=0;n<2500&&production_active(upgrade.economy().orders.front());++n)upgrade.tick();check(upgrade.housing()==housing+4,"House upgrade has no effect");roundtrip(upgrade);
 check(!upgrade.production(2,1,0,1,upgrade.economy().revision,true).accepted,"Upgrade applied twice");
}
void other_upgrades(){
 for(auto kind:{BuildingKind::Stockpile,BuildingKind::Workshop}){
  auto w=fixture(kind,3,8,5,4);if(kind==BuildingKind::Workshop){auto s=w.export_state();s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Clay,4,1,{ItemLocationKind::Storage,{},1}});s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Firewood,1,1,{ItemLocationKind::Storage,{},1}});load(w,s);}check(w.production(2,1,0,1,0,true).accepted,"Upgrade rejected");
  for(int n=0;n<2500&&production_active(w.economy().orders.front());++n)w.tick();
  check(w.economy().upgrades.size()==1,"Upgrade did not finish");roundtrip(w);
  if(kind==BuildingKind::Stockpile){for(const auto&s:w.storages())if(s.building_id==2)check(s.capacity==10000,"Storage upgrade capacity wrong");}
  else{
   check(w.production(2,1,8,1,w.economy().revision).accepted,"Upgraded workshop rejected bricks");
   int working=0;for(int n=0;n<2000&&production_active(w.economy().orders.back());++n){w.tick();working+=w.economy().orders.back().phase==ProductionPhase::Working;}
   check(w.economy().stored[5]==4&&working<=200,"Workshop upgrade did not halve work time");
  }
 }
}
void cancel_and_network_authority(){
 auto w=fixture(BuildingKind::Sawmill);check(w.production(2,1,3,2,0).accepted,"Two-batch order rejected");
 for(int n=0;n<1000&&w.economy().orders.front().work<10;++n)w.tick();
 check(w.cancel_production(1,w.economy().revision).accepted,"Working cancellation rejected");
 check(w.inventory().total.wood==80,"Cancellation lost consumed wood");roundtrip(w);
 auto path=std::filesystem::temp_directory_path()/"ribterra-economy-server.save";auto fresh=fixture(BuildingKind::Sawmill);std::string error;check(fresh.save(path,error),error);
 ServerConfig config;config.population=1;GameServer server(config);check(server.load(path,error),error);
 auto one=server.join("One",{}),two=server.join("Two",{});
 GameCommand a;a.type=CommandType::Production;a.request_id=1;a.author=one.player;a.residents={1};a.building_id=2;a.x=3;a.y=1;
 auto b=a;b.author=two.player;server.enqueue(one.player,a);server.enqueue(two.player,b);server.pump_boundary();
 check(server.world().economy().orders.size()==1,"Cooperative order executed twice");
 check(server.snapshot(one.player).economy.orders.size()==1,"Snapshot lost authoritative economy");
 std::filesystem::remove(path);
}
void wire(){
 auto w=fixture(BuildingKind::Sawmill);w.production(2,1,3,1,0);w.tick();
 ServerConfig config;config.population=1;GameServer server(config);auto joined=server.join("Economy",{});auto view=server.snapshot(joined.player);view.economy=w.economy();
 net::WorldId id{};id[0]=1;auto s=net::make_snapshot(view,id);std::string error;
 auto packet=net::decode(net::encode({1,s}),error);check(bool(packet),error);
 check(std::get<net::Snapshot>(packet->payload).economy==view.economy,"Wire lost economy");
 GameCommand c;c.request_id=1;c.author=1;c.type=CommandType::Production;c.residents={1};c.building_id=2;c.x=3;c.y=5;c.settlement_revision=8;
 auto decoded=net::decode(net::encode({2,c}),error);check(decoded&&std::get<GameCommand>(decoded->payload)==c,"Wire lost production intent");
}
int main(){try{recipes_test();constraints();other_upgrades();cancel_and_network_authority();wire();std::cout<<"Economy recipes, physical delivery, input conservation, persistence, limits, night, upgrades and wire passed.\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

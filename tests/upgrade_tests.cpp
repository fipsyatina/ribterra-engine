#include "ribterra/world_state.hpp"
#include "ribterra/foundation.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
World fixture(BuildingKind kind){
 World w(70421,2);auto s=w.export_state();
 for(auto&c:s.chunks)if(c.key.layer==WorldLayer::Surface)for(std::size_t i=0;i<c.tiles.size();++i){
  int x=c.key.cx*chunk_size+int(i%chunk_size),y=c.key.cy*chunk_size+int(i/chunk_size);
  if(x>=18&&x<=36&&y>=18&&y<=36)c.tiles[i]={Terrain::Grass,0,true,Biome::Meadow,0};
 }
 s.buildings.push_back({s.next_building_id++,kind,29,29,1,WorldLayer::Surface,1});
 s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Planks,6,1,{ItemLocationKind::Storage,{},1}});
 s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Bricks,4,1,{ItemLocationKind::Storage,{},1}});
 for(auto&r:s.residents)r.profession.assignment.automation_enabled=false;
 std::string error;check(w.import_state(std::move(s),error),error);return w;
}
int total(const World&w,ItemKind kind){int n=0;for(const auto&i:w.item_stacks())if(i.item==kind)n+=i.quantity;return n;}
ProductionOrder order(const World&w){return w.economy().orders.back();}
template<class F>void until(World&w,F predicate){for(int i=0;i<6000&&!predicate();++i)w.tick();check(predicate(),"Upgrade stuck: "+order(w).reason);}
void roundtrip(World&w){
 const auto file=std::filesystem::temp_directory_path()/("ribterra-upgrade-"+secure_token(16)+".save");
 std::string error;check(w.save(file,error),error);World restored(1,0);check(restored.load(file,error),error);
 check(restored.checksum()==w.checksum(),"Save changed upgrade or material state");
 for(int i=0;i<3;++i){w.tick();restored.tick();check(restored.checksum()==w.checksum(),"Resumed upgrade diverged");}
 std::filesystem::remove(file);w=std::move(restored);
}
bool carrying(const World&w){for(const auto&i:w.item_stacks())if(i.location.kind==ItemLocationKind::Carried)return true;return false;}
void run(BuildingKind kind){
 for(int stage=0;stage<5;++stage){
  auto w=fixture(kind);check(w.production(2,1,0,1,0,true).accepted,"Upgrade refused");
  if(stage==1)until(w,[&]{return order(w).phase==ProductionPhase::Delivery&&!carrying(w);});
  if(stage==2)until(w,[&]{return carrying(w);});
  if(stage==3)until(w,[&]{return order(w).phase==ProductionPhase::Working&&order(w).work>=100;});
  if(stage==4)until(w,[&]{return order(w).work==upgrade_recipe.ticks-1;});
  // Do not advance across completion before cancelling the final work tick.
  if(stage!=4)roundtrip(w);
  check(w.cancel_production(order(w).id,w.economy().revision).accepted,"Cancel refused");
  if(stage==2)check(order(w).cancel_requested&&carrying(w),"Cancellation lost carried materials");
  roundtrip(w);until(w,[&]{return !production_active(order(w));});
  check(order(w).phase==ProductionPhase::Cancelled&&w.economy().upgrades.empty(),"Cancelled upgrade completed");
  check(total(w,ItemKind::Planks)==6&&total(w,ItemKind::Bricks)==4,"Cancellation lost or duplicated either material");
  auto checksum=w.checksum();check(!w.cancel_production(order(w).id,w.economy().revision).accepted&&w.checksum()==checksum,"Repeated cancel refunded twice");
  check(w.production(2,1,0,1,w.economy().revision,true).accepted,"Cancelled worker stayed busy");
 }
 auto w=fixture(kind);const auto capacity=w.housing();auto checksum=w.checksum();
 check(!w.production(2,1,0,1,0,true,2).accepted&&w.checksum()==checksum,"Foreign upgrade changed the world");
 check(w.production(2,1,0,1,0,true).accepted,"Upgrade refused");
 checksum=w.checksum();check(!w.production(2,2,0,1,w.economy().revision,true).accepted&&w.checksum()==checksum,"Concurrent upgrade with another worker changed state");
 until(w,[&]{return order(w).work>=100;});roundtrip(w);roundtrip(w);
 until(w,[&]{return !production_active(order(w));});
 check(w.economy().upgrades.size()==1&&total(w,ItemKind::Planks)==0&&total(w,ItemKind::Bricks)==0,"Upgrade charged more or less than one price");
 if(kind==BuildingKind::House)check(w.housing()==capacity+4,"House did not add four places");
 roundtrip(w);checksum=w.checksum();check(!w.production(2,1,0,1,w.economy().revision,true).accepted&&w.checksum()==checksum,"Completed upgrade charged again");
}
}
int main(){try{run(BuildingKind::House);run(BuildingKind::Workshop);std::cout<<"Both upgrades: five cancellation boundaries each, delivery completion, two work reloads, one payment, housing and duplicate refusal passed.\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

#include "ribterra/world_state.hpp"
#include "ribterra/protocol.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void load(World&w,WorldState s){std::string error;check(w.import_state(std::move(s),error),error);}
World fixture(int recipe=3){
 World w(70421,1);auto s=w.export_state();
 for(auto&c:s.chunks)if(c.key.layer==WorldLayer::Surface)for(std::size_t i=0;i<c.tiles.size();++i){
  int x=c.key.cx*chunk_size+int(i%chunk_size),y=c.key.cy*chunk_size+int(i/chunk_size);
  if(x>=18&&x<=36&&y>=18&&y<=36)c.tiles[i]={Terrain::Grass,0,true,Biome::Meadow,0};
 }
 s.buildings.push_back({s.next_building_id++,BuildingKind(recipes[recipe].building),29,29,1,WorldLayer::Surface,1});
 s.buildings.push_back({s.next_building_id++,BuildingKind(recipes[recipe].building),33,29,1,WorldLayer::Surface,1});
 for(auto&r:s.residents)r.profession.assignment.automation_enabled=false;
 if(recipe==8){s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Clay,12,1,{ItemLocationKind::Storage,{},1}});s.item_stacks.push_back({s.next_item_stack_id++,ItemKind::Firewood,40,1,{ItemLocationKind::Storage,{},1}});}
 load(w,std::move(s));return w;
}
ProductionOrder order(const World&w){return w.economy().orders.front();}
template<class F>void until(World&w,F condition,int limit=5000){
 for(int n=0;n<limit&&!condition();++n)w.tick();
 check(condition(),"State not reached: phase="+std::to_string(int(order(w).phase))+" reason="+order(w).reason);
}
int total(const World&w,ItemKind kind){int n=0;for(const auto&s:w.item_stacks())if(s.item==kind)n+=int(s.quantity);return n;}
int cargo(const World&w){int n=0;for(const auto&s:w.item_stacks())if(s.location.kind==ItemLocationKind::Carried)n+=int(s.quantity);return n;}
void compare_continuation(World&w){
 auto file=std::filesystem::temp_directory_path()/"ribterra-production-control.save";std::string error;
 check(w.save(file,error),error);World restored(1,0);check(restored.load(file,error),error);
 check(w.checksum()==restored.checksum(),"Reload changed state");
 for(int i=0;i<5000&&production_active(order(w));++i){w.tick();restored.tick();check(w.checksum()==restored.checksum(),"Continuation diverged");}
 check(!production_active(order(w)),"Worker stuck after reload");std::filesystem::remove(file);
}
void can_reassign(World&w){
 auto next=w.production(3,1,3,1,w.economy().revision);check(next.accepted,"Finished worker remains reserved: "+next.message+" job="+std::to_string(int(w.residents().front().job))+" wait="+std::to_string(int(w.residents().front().profession.wait_reason)));
 check(w.cancel_production(w.economy().orders.back().id,w.economy().revision).accepted,"Cleanup rejected");
}
void two_batches(){
 auto w=fixture();check(w.production(2,1,3,2,0).accepted,"Two batches rejected");
 until(w,[&]{return order(w).completed==1;});compare_continuation(w);
 check(order(w).phase==ProductionPhase::Complete&&order(w).completed==2,"Wrong batch count");
 check(w.economy().stored[int(ItemKind::Planks)]==12,"Two batches must deliver 12 planks");
 check(total(w,ItemKind::Wood)==74,"Two batches must consume 6 wood");can_reassign(w);
}
void professions(){
 auto w=fixture();auto s=w.export_state();auto&a=s.residents.front().profession.assignment;
 a.automation_enabled=true;a.profession=Profession::Farmer;a.worksite=CellCoord{29,29};a.priorities={2,3,4,1,2};auto original=a;load(w,s);
 const auto before=w.checksum();auto rejected=w.production(2,1,3,2,0);
 check(!rejected.accepted&&rejected.message=="economy.assigned","Permanent farmer stolen");check(before==w.checksum(),"Rejected start mutated world");
 check(w.stop({1}).front().status==ResidentCommandStatus::Applied,"Explicit stop failed");original.automation_enabled=false;
 check(w.production(2,1,3,2,0).accepted,"Explicitly stopped farmer cannot be chosen");
 check(!w.production(3,1,3,1,w.economy().revision).accepted,"Worker assigned twice");
 check(w.assign_profession({1},original).front().status==ResidentCommandStatus::Rejected,"Conflicting profession admitted");
 ManualOrderRequest request;request.kind=ManualOrderKind::Move;request.target={30,30};
 check(w.issue_manual_order({1},request,ManualQueueMode::Replace).front().status==ResidentCommandStatus::Rejected,"Conflicting manual order admitted");
 until(w,[&]{return order(w).phase==ProductionPhase::Working;});
 check(w.cancel_production(1,w.economy().revision).accepted,"Farmer cancellation failed");compare_continuation(w);
 check(w.residents().front().profession.assignment==original,"Profession or worksite erased");can_reassign(w);
}
void cancellations(){
 for(int stage=0;stage<8;++stage){
  auto w=fixture();check(w.production(2,1,3,2,0).accepted,"Start rejected");
  if(stage==1)until(w,[&]{return order(w).phase==ProductionPhase::Delivery&&!cargo(w);});
  if(stage==2)until(w,[&]{return order(w).phase==ProductionPhase::Delivery&&cargo(w)>0;});
  if(stage==3)until(w,[&]{return order(w).phase==ProductionPhase::Working&&order(w).work==1;});
  if(stage==4)until(w,[&]{return order(w).phase==ProductionPhase::Working&&order(w).work>=100;});
  if(stage==5)until(w,[&]{return order(w).phase==ProductionPhase::Output&&cargo(w)>0;});
  if(stage==6)until(w,[&]{return order(w).phase==ProductionPhase::Output&&!cargo(w);});
  if(stage==7)until(w,[&]{return order(w).completed==1;});
  const bool carrying=cargo(w)>0;
  check(w.cancel_production(1,w.economy().revision).accepted,"Cancellation rejected at stage "+std::to_string(stage));
  if(carrying){check(order(w).cancel_requested,"Carried cancellation did not wait");check(cargo(w)>0,"Cancellation teleported cargo");}
  compare_continuation(w);
  check(order(w).phase==ProductionPhase::Cancelled&&!order(w).cancel_requested,"Cancellation did not finish");
  check(2*total(w,ItemKind::Wood)+total(w,ItemKind::Planks)==160,"Cargo lost or duplicated at stage "+std::to_string(stage));
  check(order(w).completed<=1,"Cancellation started another batch");
  for(const auto&t:w.logistics_tasks())if(t.resident_id==1)check(t.phase==LogisticsPhase::Complete||t.phase==LogisticsPhase::Failed,"Cancelled worker still owns a logistics task");can_reassign(w);
 }
}
void failed_delivery(){
 auto w=fixture();w.production(2,1,3,2,0);until(w,[&]{return order(w).phase==ProductionPhase::Output&&cargo(w)>0;});
 auto s=w.export_state();s.residents.front().health=0;load(w,s);w.tick();
 check(w.cancel_production(1,w.economy().revision).accepted,"Failed output cancellation rejected");compare_continuation(w);
 check(total(w,ItemKind::Wood)==77&&total(w,ItemKind::Planks)==6,"Failed output refunded consumed inputs or lost goods");
}
void refund_two_inputs(){
 auto w=fixture(8);int planks=total(w,ItemKind::Clay),stone=total(w,ItemKind::Firewood);
 w.production(2,1,8,2,0);until(w,[&]{return order(w).phase==ProductionPhase::Working;});
 w.cancel_production(1,w.economy().revision);compare_continuation(w);
 check(total(w,ItemKind::Clay)==planks&&total(w,ItemKind::Firewood)==stone,"Multi-input refund changed quantities");
 check(total(w,ItemKind::Bricks)==0,"Cancelled work fabricated bricks");
}
void blocked_refund_is_atomic(){
 auto w=fixture(8);w.production(2,1,8,2,0);until(w,[&]{return order(w).phase==ProductionPhase::Working;});
 auto s=w.export_state();for(auto&item:s.item_stacks)if(item.item==ItemKind::Firewood){item.quantity=INT_MAX;item.location={ItemLocationKind::Ground,{20,20},0};}
 load(w,s);const int planks=total(w,ItemKind::Clay);
 check(w.cancel_production(1,w.economy().revision).accepted,"Blocked refund rejected");
 for(int i=0;i<5;++i)w.tick();
 check(order(w).cancel_requested&&order(w).reason=="refund_wait","Refund limit not explained");
 check(total(w,ItemKind::Clay)==planks,"Retry partially refunded first ingredient");
 s=w.export_state();s.item_reservations.clear();s.logistics_tasks.clear();
 for(auto&item:s.item_stacks)if(item.item==ItemKind::Firewood)item.quantity=39;
 load(w,s);compare_continuation(w);
 check(total(w,ItemKind::Clay)==planks+4&&total(w,ItemKind::Firewood)==40,"Retry duplicated or lost a refund");
}
void travelling_cancel(){
 auto w=fixture(0);w.production(2,1,0,2,0);until(w,[&]{return order(w).phase==ProductionPhase::Travelling;});
 check(w.cancel_production(1,w.economy().revision).accepted,"Pending route cancellation rejected");compare_continuation(w);
 check(order(w).phase==ProductionPhase::Cancelled&&total(w,ItemKind::Grain)==0,"Pending route started work after cancellation");
}
void wire_and_legacy(){
 auto w=fixture();w.production(2,1,3,2,0);until(w,[&]{return order(w).phase==ProductionPhase::Output&&cargo(w)>0;});w.cancel_production(1,w.economy().revision);
 const auto state=w.economy();check(decode_economy(encode_economy(state))==state,"Cancellation JSON lost intent");
 auto old=nlohmann::json::parse(encode_economy(state));old["orders"][0].erase(11);
 check(!decode_economy(old.dump()).orders.front().cancel_requested,"Old order invented cancellation");
 ServerConfig config;config.population=1;GameServer server(config);auto player=server.join("Production",{});auto view=server.snapshot(player.player);view.economy=state;
 net::WorldId id{};id[0]=1;std::string error;auto decoded=net::decode(net::encode({1,net::make_snapshot(view,id)}),error);
 check(decoded&&std::get<net::Snapshot>(decoded->payload).economy==state,"Wire lost cancellation");
}
}
int main(){try{two_batches();professions();cancellations();travelling_cancel();failed_delivery();refund_two_inputs();blocked_refund_is_atomic();wire_and_legacy();std::cout<<"Production control: two batches, 9 cancellation boundaries, cargo failure, atomic two-input refunds, worker exclusivity, profession retention, save continuation and wire passed.\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

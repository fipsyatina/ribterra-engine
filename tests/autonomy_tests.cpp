#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"
#include <iostream>
#include <stdexcept>
using namespace ribterra;
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
void finite_orders(int population, bool append) {
 World w(70421,population);
 auto state=w.export_state();
 for(auto &chunk:state.chunks) if(chunk.key==chunk_key(CellCoord{22,19}))
  chunk.tiles[local_index(22,19)]={Terrain::Forest,20,true};
 std::string error; check(w.import_state(state,error),error.c_str());
 std::vector<int> ids; for(int id=1;id<=population;++id) ids.push_back(id);
 ProfessionAssignment profession; profession.profession=Profession::Woodcutter;
 w.assign_profession(ids,profession);
 const ManualOrderRequest request{ManualOrderKind::Gather,CellCoord{22,19},GatherCompletion::OneDelivery};
 const auto issued=w.issue_manual_order(ids,request,ManualQueueMode::Replace);
 check(issued.size()==ids.size(),"Missing group members");
 for(const auto &result:issued) check(result.status==ResidentCommandStatus::Applied,"Order rejected");
 if(append) w.issue_manual_order(ids,request,ManualQueueMode::Append);
 World copy(1,0); bool saved=false,done=false,domestic=false;
 for(int tick=0;tick<1600;++tick) {
  w.tick();
  if(saved){copy.tick();check(copy.checksum()==w.checksum(),"Finite orders diverged after cargo reload");}
  else if(w.inventory().carried.wood>0){
   check(w.save("finite-orders.save",error),error.c_str());
   check(copy.load("finite-orders.save",error),error.c_str());saved=true;
  }
  bool live=false;for(const auto &order:w.manual_orders()) live|=order.phase<=ManualOrderPhase::Active;
  if(!live){
   done=true;
   for(const auto &r:w.residents()) {
    check(!r.profession.manual_override,"Finished order retained manual override");
    check(r.job!=Job::Woodcut && r.job!=Job::Mine && r.job!=Job::Forage,"Extraction restarted");
    domestic|=r.profession.wait_reason==ProfessionWaitReason::Strolling ||
              r.profession.wait_reason==ProfessionWaitReason::Socializing;
   }
  }
 }
 check(saved&&done&&domestic,"Missing cargo save, completion, or return to daily life");
 check(w.inventory().stored.wood==80+3*population*(append?2:1),"Wrong finite delivery total");
 check(w.inventory().carried.wood==0,"Cargo left after completed orders");
 check(w.tile(22,19).amount==20-population*(append?2:1),"Resource extraction exceeded requested amount");
}
void old_automatic_worker_full_storage() {
 World w(70421,1); auto state=w.export_state();
 for(auto &chunk:state.chunks) if(chunk.key==chunk_key(CellCoord{22,19})) {
  chunk.tiles[local_index(22,19)]={Terrain::Forest,20,true};
  chunk.tiles[local_index(23,19)]={Terrain::Grass,0,true};
 }
 auto &r=state.residents.front();r.x=23;r.y=19;r.target_x=22;r.target_y=19;
 r.job=Job::Woodcut;r.order_phase=OrderPhase::Active;
 state.routes.front().work=CellCoord{22,19};
 auto &storage=state.storages.front();storage.capacity=0;
 for(const auto &stack:state.item_stacks)
  if(stack.location.kind==ItemLocationKind::Storage && stack.location.entity_id==storage.building_id)
   storage.capacity+=stack.quantity;
 std::string error;check(w.import_state(state,error),error.c_str());
 for(int tick=0;tick<180;++tick)w.tick();
 check(w.tile(22,19).amount==19,"Saved automatic worker kept extracting at full storage");
 check(w.inventory().total.wood==83,"Last output was lost or duplicated at full storage");
}
void player_order_grace_period() {
 World w(70421,3);auto state=w.export_state();test::set_fixture_simulation_tick(state,500);
 // A clear destination away from the camp, with nearby cargo to tempt autonomous haulers.
 state.item_stacks.push_back({state.next_item_stack_id++,ItemKind::Wood,3,1,{ItemLocationKind::Ground,{34,30},0}});state.resources.wood+=3;
 for(auto&chunk:state.chunks)if(chunk.key.layer==WorldLayer::Surface)
  for(int y=20;y<=40;++y)for(int x=20;x<=40;++x)if(chunk.key==chunk_key({x,y}))chunk.tiles[local_index(x,y)]={Terrain::Grass,0,true};
 std::string error;check(w.import_state(state,error),error.c_str());
 const std::vector<int> ids{1,2,3};
 ManualOrderRequest first{ManualOrderKind::Move,CellCoord{34,30}};
 const auto result=w.issue_manual_order(ids,first,ManualQueueMode::Replace);
 check(result.size()==3,"Group command missing members");
 for(const auto&r:result)check(r.status==ResidentCommandStatus::Applied,"Group move rejected");
 bool done=false;for(int tick=0;tick<400;++tick){w.tick();done=true;for(const auto&o:w.manual_orders())done&=o.phase>ManualOrderPhase::Active;if(done)break;}
 check(done,"Group move did not finish");
 auto deadline=w.residents()[0].profession.next_evaluation_tick;
 const auto first_deadline=deadline;for(const auto&r:w.residents())deadline=std::min(deadline,r.profession.next_evaluation_tick);
 check(deadline>w.ticks()&&deadline-w.ticks()<=600,"Missing bounded autonomy timer");
 for(int tick=0;tick<100;++tick){w.tick();for(const auto&r:w.residents())check(r.x==34&&r.y==30&&r.job==Job::Idle,"Resident left commanded position during grace period");}
 check(w.save("autonomy-grace.save",error),error.c_str());World restored(1,0);check(restored.load("autonomy-grace.save",error),error.c_str());
 check(restored.residents()[0].profession.next_evaluation_tick==first_deadline,"Loading reset grace period");
 auto death_state=restored.export_state();death_state.residents[0].health=0;World death(1,0);
 check(death.import_state(death_state,error),error.c_str());death.tick();check(death.save("autonomy-grace-death.save",error),error.c_str());
 auto stopped_state=restored.export_state();stopped_state.residents[0].profession.assignment.automation_enabled=false;World stopped(1,0);
 check(stopped.import_state(stopped_state,error),error.c_str());stopped.tick();check(stopped.save("autonomy-grace-stopped.save",error),error.c_str());
 World released(1,0);check(released.import_state(restored.export_state(),error),error.c_str());released.release_manual_control(ids);
 for(const auto&r:released.residents())check(r.profession.next_evaluation_tick==released.ticks(),"Explicit release retained timeout");
 while(w.ticks()+1<deadline){w.tick();restored.tick();check(w.residents()==restored.residents(),"Grace period continuation diverged");for(const auto&r:w.residents())check(r.x==34&&r.y==30,"Autonomy resumed too early");}
 bool resumed=false;for(int i=0;i<250;++i){w.tick();for(const auto&r:w.residents())resumed|=r.job==Job::Move;}
 check(resumed,"Autonomy never resumed after timeout");
 // A new order interrupts the grace period immediately and the next queued leg never waits 30 s.
 ManualOrderRequest next{ManualOrderKind::Move,CellCoord{36,30}};
 auto issued=restored.issue_manual_order(ids,next,ManualQueueMode::Replace);for(const auto&r:issued)check(r.status==ResidentCommandStatus::Applied,"New order was blocked by timeout");
 next.target=CellCoord{38,30};restored.issue_manual_order(ids,next,ManualQueueMode::Append);
 bool arrived=false;for(int i=0;i<160;++i){restored.tick();arrived=true;for(const auto&r:restored.residents())arrived&=r.x==38&&r.y==30&&r.job==Job::Idle;if(arrived)break;}
 check(arrived,"Grace period delayed a queued player command");
}
int main(){try{
 player_order_grace_period();
 old_automatic_worker_full_storage();
 finite_orders(1,false); finite_orders(3,false); finite_orders(3,true);
 World w(70421,10); bool walk=false,talk=false,gather=false,delivery=false;
 for(int t=0;t<1800;++t){w.tick();for(const auto&r:w.residents()){
  walk|=r.profession.wait_reason==ProfessionWaitReason::Strolling;
  talk|=r.profession.wait_reason==ProfessionWaitReason::Socializing;
  gather|=r.job==Job::Woodcut||r.job==Job::Mine||r.job==Job::Forage;
  delivery|=r.job==Job::Haul;
  check(r.health==100,"Autonomous settler missed food");
 }}
 check(walk&&talk&&!gather&&!delivery,"Daily life missing or unrequested extraction started");
 std::string error;check(w.save("autonomy.save",error),error.c_str());World loaded(70421,0);
 check(loaded.load("autonomy.save",error),error.c_str());
 for(int t=0;t<100;++t){w.tick();loaded.tick();check(w.residents()==loaded.residents(),"Autonomy diverged after reload");}
 w.stop({1}); for(int t=0;t<300;++t) w.tick();
 const auto at=w.residents().front();for(int t=0;t<300;++t)w.tick();
 check(w.residents().front().x==at.x&&w.residents().front().y==at.y,"Stopped automation still wanders");
 ManualOrderRequest request;request.target=CellCoord{25,24,WorldLayer::Surface};
 auto result=w.issue_manual_order({2},request,ManualQueueMode::Replace);
 check(!result.empty() && result.front().status!=ResidentCommandStatus::Rejected,"Manual move rejected");
 bool arrived=false;
 for(int t=0;t<400;++t){w.tick();const auto&r=w.residents()[1];if(r.x==25&&r.y==24){arrived=true;break;}}
 check(arrived,"Autonomy prevented manual arrival");
 std::cout<<"No autonomous gathering, walks, paired conversations, meals, persistence and manual control passed.\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}

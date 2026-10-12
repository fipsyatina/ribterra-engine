#include "ribterra/world_state.hpp"
#include "ribterra/game_server.hpp"
#include "ribterra/protocol.hpp"
#include "weather_fixture.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
void check(bool v,const char *message){if(!v)throw std::runtime_error(message);}
void load_state(World &w,WorldState state){std::string error;check(w.import_state(std::move(state),error),error.c_str());}
void install_wood_fixture(World &w,CellCoord target={22,19}){
 auto state=w.export_state();auto chunk=std::find_if(state.chunks.begin(),state.chunks.end(),[&](const auto &candidate){return candidate.key==chunk_key(target);});
 check(chunk!=state.chunks.end(),"Wood fixture chunk is missing");auto &resource=chunk->tiles[local_index(target.x,target.y)];
 resource.terrain=Terrain::Forest;resource.amount=64;resource.biome=Biome::Forest;resource.explored=true;++state.topology_revision;load_state(w,std::move(state));
}
void steps(World &w,int n){for(int i=0;i<n;++i)w.tick();}
void roundtrip(World &w,const char *name){
 std::string error;check(w.save(name,error),error.c_str());World copy(1,0);check(copy.load(name,error),error.c_str());
 check(copy.settlement()==w.settlement(),"Settlement save fields changed");
 for(int i=0;i<30;++i){w.tick();copy.tick();check(w.checksum()==copy.checksum(),"Settlement resume diverged");}
 std::filesystem::remove(name);
}
void population(){
 World w(70421,1);install_wood_fixture(w);const auto food=w.inventory().stored.food;
 check(w.decide(SettlementDecision::Recruit,0).accepted,"Recruit rejected");
 check(w.inventory().stored.food==food-50,"Recruit did not debit exactly 50 food");
 check(!w.decide(SettlementDecision::Recruit,0).accepted,"Stale recruit applied twice");
 check(!w.decide(SettlementDecision::Recruit,1).accepted,"Concurrent arrival admitted");
 steps(w,int(recruitment_ticks)-1);check(w.residents().size()==1,"Arrival happened early");w.tick();
 check(w.residents().size()==2 && w.settlement().young.size()==1,"Arrival missing");
 const int child=w.settlement().young.front().resident_id;
 check(!w.work_allowed(child,Job::Woodcut),"Young settler can work without forced decision");
 check(w.issue_manual_order({child},{ManualOrderKind::Gather,CellCoord{22,19},GatherCompletion::OneDelivery},ManualQueueMode::Replace).front().status==ResidentCommandStatus::Rejected,"Child gather accepted without permission");
 ProfessionAssignment p;p.profession=Profession::Farmer;p.worksite=CellCoord{24,24};
 check(w.assign_profession({child},p).front().status==ResidentCommandStatus::Rejected,"Child enterprise assignment accepted");
 roundtrip(w,"settlement-young.save");
 bool domestic=false;for(int i=0;i<350;++i){w.tick();domestic|=w.residents().back().job==Job::Move;}
 check(domestic,"Young settler did not participate in daily life");
 const auto revision=w.settlement().revision;
 check(!w.decide(SettlementDecision::YouthWork,revision,child).accepted,"Alpha youth work allowed");
 check(w.settlement().discontent()==0&&!w.work_allowed(child,Job::Woodcut),"Rejected forced work mutated policy");
 roundtrip(w,"settlement-young-rest.save");
 const auto adult=w.settlement().young.front().adult_tick;
 while(w.ticks()<adult-1)w.tick();check(!w.settlement().young.empty(),"Matured early");w.tick();
 check(w.settlement().young.empty() && w.residents().size()==2,"Adult transition missing");
 check(w.settlement().discontent()==0,"Maturity invented a decision consequence");
 World poor(70421,1);auto state=poor.export_state();
 for(auto &stack:state.item_stacks)if(stack.item==ItemKind::Food)stack.quantity=49;
 state.resources.food=49;load_state(poor,state);const auto before=poor.checksum();
 check(!poor.decide(SettlementDecision::Recruit,0).accepted && before==poor.checksum(),"Insufficient-food decision mutated state");
}
void night(){
 World w(70421,1);install_wood_fixture(w);auto state=w.export_state();test::set_fixture_simulation_tick(state,daylight_ticks);
 state.buildings.push_back({2,BuildingKind::Tavern,25,25,1,WorldLayer::Surface,1});
 state.buildings.push_back({3,BuildingKind::Temple,27,25,1,WorldLayer::Surface,1});state.next_building_id=4;
 load_state(w,state);
 check(!w.work_allowed(1,Job::Woodcut),"Ordinary work open at night");
 check(w.work_allowed(1,Job::Build,CellCoord{25,25}) && w.work_allowed(1,Job::Build,CellCoord{27,25}),"Night venues are closed");
 check(w.issue_manual_order({1},{ManualOrderKind::Gather,CellCoord{22,19},GatherCompletion::OneDelivery},ManualQueueMode::Replace).front().status==ResidentCommandStatus::Applied,"Night order not retained");
 const auto wood=w.inventory().total.wood;steps(w,180);
 check(w.inventory().total.wood==wood && w.residents().front().progress==0,"Night work advanced without decision");
 roundtrip(w,"settlement-night.save");
 check(!w.decide(SettlementDecision::NightShift,w.settlement().revision).accepted,"Alpha night override allowed");
 check(w.settlement().discontent()==0,"Rejected night override mutated policy");
 state=w.export_state();test::set_fixture_simulation_tick(state,ticks_per_day);load_state(w,state);
 check(!w.settlement().night_shift(w.calendar().tick) && w.work_allowed(1,Job::Woodcut),"Shift did not expire at dawn");
 check(w.residents().front().skill_practice==state.residents.front().skill_practice,"Night shift changed skill practice at dawn");
 state=w.export_state();test::set_fixture_simulation_tick(state,ticks_per_day+daylight_ticks);load_state(w,state);
 check(!w.work_allowed(1,Job::Woodcut),"One-night override leaked into next night");
 check(!w.decide(SettlementDecision::NightShift,w.settlement().revision).accepted && w.settlement().discontent()==0,"Next night consequence wrong");
}
void protected_delivery_and_last_food(){
 World w(70421,1);install_wood_fixture(w);
 w.issue_manual_order({1},{ManualOrderKind::Gather,CellCoord{22,19},GatherCompletion::OneDelivery},ManualQueueMode::Replace);
 for(int n=0;n<300 && !w.inventory().carried.wood;++n)w.tick();
 check(w.inventory().carried.wood==3,"Cargo boundary not reached");
 auto state=w.export_state();const auto delta=daylight_ticks-state.ticks;test::set_fixture_simulation_tick(state,daylight_ticks);
 for(auto &task:state.logistics_tasks)task.deadline_tick+=delta;
 load_state(w,state);steps(w,200);check(w.inventory().stored.wood==83 && !w.inventory().carried.wood,"Night lost protected cargo");
 World exact(70421,1);state=exact.export_state();for(auto &stack:state.item_stacks)if(stack.item==ItemKind::Food)stack.quantity=50;
 state.resources.food=50;load_state(exact,state);check(exact.decide(SettlementDecision::Recruit,0).accepted,"Exact food rejected");
 roundtrip(exact,"settlement-last-food.save");
}
void cooperation(){
 ServerConfig config;config.population=1;GameServer server(config);
 auto a=server.join("One",{}),b=server.join("Two",{});check(a.accepted&&b.accepted,"Join failed");
 GameCommand one;one.type=CommandType::Recruit;one.request_id=1;one.author=a.player;
 GameCommand two=one;two.author=b.player;
 check(server.enqueue(a.player,one).status==CommandReplyStatus::Queued,"First decision not queued");
 check(server.enqueue(b.player,two).status==CommandReplyStatus::Queued,"Second decision not queued");
 server.pump_boundary();auto view=server.snapshot(a.player);
 check(view.resources.food==50 && view.settlement.arrival_tick==recruitment_ticks,"Two players double charged recruitment");
 server.enqueue(a.player,one);server.pump_boundary();check(server.snapshot(a.player).resources.food==50,"Retry double charged decision");
 using namespace ribterra::net;
 WorldId id{};id.front()=1;auto snap=make_snapshot(view,id);
 std::string error;
 auto encoded=encode(Packet{1,snap});auto decoded=decode(encoded,error);check(decoded.has_value(),error.c_str());
 check(std::get<Snapshot>(decoded->payload).settlement==view.settlement,"Settlement wire round trip changed fields");
 one.settlement_revision=42;auto wire_command=decode(encode(Packet{2,one}),error);
 check(wire_command && std::get<GameCommand>(wire_command->payload)==one,"Decision revision lost on wire");
 auto parts=split_snapshot(snap);ViewSnapshot restored;WorldId received{};
 for(const auto &part:parts) check(apply_snapshot(restored,received,part,error),error.c_str());
 check(restored.settlement==view.settlement,"Snapshot omitted settlement state");
 auto invalid=view.settlement;invalid.night_discontent=101;check(!valid_settlement(invalid,view.tick,CalendarTick{view.weather.calendar_tick}),"Invalid discontent accepted");
}
int main(){try{population();night();protected_delivery_and_last_food();cooperation();std::cout<<"Population timers, youth labour, day/night, bounded consequences, persistence and cooperative dedup passed.\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

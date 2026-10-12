#include "ribterra/game_server.hpp"
#include "ribterra/biomes.hpp"
#include "ribterra/world_state.hpp"
#include <iostream>
#include <stdexcept>
#include <map>
#include <queue>
#include <set>
#include <tuple>
using namespace ribterra;
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
int main(){try{
 ServerConfig config;config.lobby_enabled=true;config.max_players=6;config.population=3;GameServer server(config);
 std::vector<JoinResult> players;for(int i=0;i<6;++i)players.push_back(server.join("Player "+std::to_string(i),config.versions));
 for(const auto&p:players)require(p.accepted,"Six players must join");require(!server.join("Extra",config.versions).accepted,"Seventh player joined");
 auto lobby=server.snapshot(players[0].player).lobby;for(int i=0;i<6;++i)require(lobby.players[i].team==1,"New players must share the host team");
 for(int i=0;i<20;++i)server.tick();require(server.world().ticks()==0,"World advanced before ready");
 std::map<PlayerId,std::uint64_t> requests;
 auto command=[&](PlayerId player,LobbyAction action,int value){GameCommand c;c.request_id=++requests[player];c.author=player;c.type=CommandType::Lobby;c.x=int(action);c.y=value;server.enqueue(player,c);server.pump_boundary();auto replies=server.take_replies(player);return replies.back();};
 for(int i=2;i<6;++i)require(command(players[i].player,LobbyAction::Team,i+1).status==CommandReplyStatus::Applied,"Independent team choice failed");
 require(command(players[1].player,LobbyAction::Start,0).status==CommandReplyStatus::Rejected,"Guest started world");
 require(command(players[1].player,LobbyAction::Team,1).status==CommandReplyStatus::Applied,"Team change failed");
 for(const auto&p:players)require(command(p.player,LobbyAction::Ready,1).status==CommandReplyStatus::Applied,"Ready failed");
 require(command(players[0].player,LobbyAction::Start,0).status==CommandReplyStatus::Applied,"Host start failed");
 const auto first=server.snapshot(players[0].player),ally=server.snapshot(players[1].player),other=server.snapshot(players[2].player);
 require(first.residents==ally.residents,"Teammates do not share residents");require(first.residents[0].nation!=other.residents[0].nation,"Other team shares nation");
 GameCommand foreign;foreign.author=players[0].player;foreign.request_id=++requests[foreign.author];foreign.type=CommandType::Move;foreign.residents={other.residents[0].id};foreign.x=int(first.residents[0].x);foreign.y=int(first.residents[0].y);server.enqueue(foreign.author,foreign);server.pump_boundary();require(server.take_replies(foreign.author).back().status==CommandReplyStatus::Rejected,"Foreign residents accepted an order");
 require(first.inventory.stored.wood==80&&other.inventory.stored.wood==80,"Independent starter inventories missing");
 require(!first.tile(int(other.residents[0].x),int(other.residents[0].y)).explored,"Foreign fog leaked");
 GameCommand recruit;recruit.author=players[0].player;recruit.request_id=++requests[recruit.author];recruit.type=CommandType::Recruit;recruit.settlement_revision=first.settlement.revision;server.enqueue(recruit.author,recruit);
 recruit.author=players[1].player;recruit.request_id=++requests[recruit.author];server.enqueue(recruit.author,recruit);server.pump_boundary();
 require(server.snapshot(players[0].player).inventory.stored.food==50,"Duplicate shared decision spent twice");require(server.snapshot(players[2].player).inventory.stored.food==100,"Recruit spent another nation's food");
 require(command(players[2].player,LobbyAction::Pause,1).status==CommandReplyStatus::Applied,"Guest pause failed");auto tick=server.world().ticks();server.tick();require(server.world().ticks()==tick,"Paused world advanced");
 require(command(players[2].player,LobbyAction::Pause,0).status==CommandReplyStatus::Rejected,"Guest unpaused");require(command(players[0].player,LobbyAction::Pause,0).status==CommandReplyStatus::Applied,"Host unpause failed");server.tick();require(server.world().ticks()==tick+1,"World stayed paused");
 for(int speed:{1,2,4}){
  require(command(players[0].player,LobbyAction::Speed,speed).status==CommandReplyStatus::Applied,"Host cannot set speed");
  for(int i=1;i<6;++i){require(command(players[i].player,LobbyAction::Speed,speed==1?4:1).status==CommandReplyStatus::Rejected,"Guest changed speed");require(server.simulation_speed()==speed,"Rejected speed changed the world");}
 }
 std::string error;const bool lobby_saved=server.save("lobby-test.save",error);require(lobby_saved,error.c_str());GameServer restored(config);const bool lobby_loaded=restored.load("lobby-test.save",error);require(lobby_loaded,error.c_str());
 require(server.local_host_resume_token().empty(),"Active host exposed a recovery credential");
 require(restored.local_host_resume_token()==players[0].resume_token,"Local continuation lost saved host credential");
 auto resumed=restored.join("Different form name",config.versions,restored.local_host_resume_token());require(resumed.accepted&&resumed.player==players[0].player,"Resume lost identity");require(restored.snapshot(resumed.player).inventory==server.snapshot(players[0].player).inventory,"Save lost nation inventory");
 require(restored.snapshot(resumed.player).lobby.host==resumed.player&&restored.snapshot(resumed.player).lobby.players.size()==6,"Continuation changed host or created another player");
 require(restored.local_host_resume_token().empty(),"Connected host can be claimed again");
 require(restored.snapshot(resumed.player).lobby.paused,"Restored world must wait visibly on pause");
 require(restored.snapshot(resumed.player).lobby.started,"Save lost started lobby");
 World late(80412,3);late.create_nations({1,3},6,3);late.tick();const auto prior=late.residents();const auto inventory=late.inventory(1);late.create_nations({2},6,3);
 require(late.ticks()==1&&late.inventory(1)==inventory&&late.residents().size()==prior.size()+3,"Late nation reset the world");
 for(const auto&r:prior){const auto found=std::find_if(late.residents().begin(),late.residents().end(),[&](const auto&v){return v.id==r.id;});require(found!=late.residents().end()&&*found==r,"Late join changed an existing resident");}
 const bool late_saved=late.save("lobby-late.save",error);require(late_saved,error.c_str());
 World old(70421,3);old.tick();auto old_people=old.residents();auto old_inventory=old.inventory();old.create_nations({1,2},6,3);
 require(old.inventory()==old_inventory,"Hosting an existing world reset inventory");for(const auto&r:old_people){auto found=std::find_if(old.residents().begin(),old.residents().end(),[&](const auto&v){return v.id==r.id;});require(found!=old.residents().end()&&*found==r,"Hosting reset existing residents");}
 World sixth(70421,3);sixth.create_nations({6},6,3);require(sixth.inventory(6).stored.food==100,"Team six has no inventory");const bool sixth_saved=sixth.save("lobby-sixth.save",error);require(sixth_saved,error.c_str());

 auto starter_layout=[](std::uint32_t seed){
 World generated(seed,3);generated.create_nations({1,2},6,3);const auto state=generated.export_state();
  const WorldGenerationConfig generation{seed,generated.terrain_generator(),alpine_valley_profile_id,terrain_rules_version};
  std::set<std::tuple<int,int,int>> signature;
  for(const auto&nation:state.economy.nations){
   std::set<std::pair<int,int>> reached{{nation.x,nation.y}};std::queue<std::pair<int,int>> frontier;frontier.push({nation.x,nation.y});
   while(!frontier.empty()){auto [x,y]=frontier.front();frontier.pop();for(auto [dx,dy]:{std::pair{1,0},{-1,0},{0,1},{0,-1}}){
    const int nx=x+dx,ny=y+dy;if((nx-nation.x)*(nx-nation.x)+(ny-nation.y)*(ny-nation.y)>121||reached.contains({nx,ny}))continue;
    if(generated.tile(nx,ny).terrain==Terrain::Grass){reached.insert({nx,ny});frontier.push({nx,ny});}
   }}
   std::array<std::array<bool,4>,3> directions{};std::array<bool,3> reachable{};
   for(int dy=-11;dy<=11;++dy)for(int dx=-11;dx<=11;++dx){if(dx*dx+dy*dy>110)continue;const auto&t=generated.tile(nation.x+dx,nation.y+dy);
    const int kind=t.amount<=0?-1:t.terrain==Terrain::Forest?0:t.terrain==Terrain::Rock?1:t.terrain==Terrain::Grass?2:-1;if(kind<0)continue;
    directions[kind][0]|=dx<0;directions[kind][1]|=dx>0;directions[kind][2]|=dy<0;directions[kind][3]|=dy>0;
    signature.emplace(kind,dx,dy);
    for(auto [ax,ay]:{std::pair{1,0},{-1,0},{0,1},{0,-1}})reachable[kind]|=reached.contains({nation.x+dx+ax,nation.y+dy+ay});
   }
   for(int kind=0;kind<3;++kind){require(std::all_of(directions[kind].begin(),directions[kind].end(),[](bool v){return v;}),"Starter resource remains confined to one side");require(reachable[kind],"Starter resource is unreachable");}
   for(auto [dx,dy]:{std::pair{14,14},{14,-14},{-14,14},{-14,-14}})require(generated.tile(nation.x+dx,nation.y+dy)==surface_tile(nation.x+dx,nation.y+dy,generation),"Nation start erased surrounding natural terrain");
  }
  return signature;
 };
 require(starter_layout(70421)!=starter_layout(80412),"Nation starter resources only rotate an identical layout");

 ServerConfig bc;bc.lobby_enabled=true;bc.max_players=4;bc.population=4;GameServer bots(bc);
 auto owner=bots.join("Host",bc.versions);auto guest=bots.join("Guest",bc.versions);
 std::uint64_t seq=0,guest_seq=0;
 auto bot_command=[&](LobbyAction action,int value=0,bool as_guest=false){auto actor=as_guest?guest.player:owner.player;GameCommand c;c.type=CommandType::Lobby;c.author=actor;c.request_id=as_guest?++guest_seq:++seq;c.x=int(action);c.y=value;bots.enqueue(actor,c);bots.pump_boundary();return bots.take_replies(actor).back();};
 require(bot_command(LobbyAction::AddBot,0,true).status==CommandReplyStatus::Rejected,"Guest added bot");
 require(bot_command(LobbyAction::AddBot).status==CommandReplyStatus::Applied,"Host failed to add bot");
 auto roster=bots.snapshot(owner.player).lobby;auto bot_id=roster.players.back().id;
 require(roster.players.back().bot&&roster.players.back().ready&&roster.players.back().team==2,"Bot defaults invalid");
 require(bot_command(LobbyAction::BotTeam,int(bot_id)*10+4).status==CommandReplyStatus::Applied,"Bot team failed");
 require(bot_command(LobbyAction::RemoveBot,int(owner.player)).status==CommandReplyStatus::Rejected,"Removed human as bot");
 require(bot_command(LobbyAction::RemoveBot,int(bot_id)).status==CommandReplyStatus::Applied,"Bot removal failed");
 require(bots.snapshot(owner.player).lobby.players.size()==2,"Empty place not restored");
 require(bot_command(LobbyAction::AddBot).status==CommandReplyStatus::Applied,"Re-add failed");
 bot_id=bots.snapshot(owner.player).lobby.players.back().id;
 bot_command(LobbyAction::Ready,1);bot_command(LobbyAction::Ready,1,true);
 require(bot_command(LobbyAction::Start).status==CommandReplyStatus::Applied,"Empty places blocked start");
 require(bot_command(LobbyAction::AddBot).status==CommandReplyStatus::Rejected,"Added bot during gameplay");
 bots.tick();bool bot_order=false;for(const auto&o:bots.world().manual_orders())if(o.issuer&&o.issuer->player_id==bot_id)bot_order=true;
 require(bot_order,"Bot did not issue real bounded work");
 bool bot_house=false;for(const auto&b:bots.world().construction_orders())if(b.owner_nation==2&&b.kind==BuildingKind::House)bot_house=true;
 require(bot_house,"Bot did not begin settlement construction");
 for(int i=0;i<2400;++i)bots.tick();
 bool delivered=false;for(const auto&o:bots.world().manual_orders())if(o.issuer&&o.issuer->player_id==bot_id&&o.completion==ManualOrderCompletion::Delivered)delivered=true;
 require(delivered,"Bot did not physically deliver gathered resources");
 const bool bots_saved=bots.save("lobby-bots.save",error);require(bots_saved,error.c_str());GameServer loaded_bots(bc);const bool bots_loaded=loaded_bots.load("lobby-bots.save",error);require(bots_loaded,error.c_str());
 auto resumed_bot_host=loaded_bots.join("Host",bc.versions,owner.resume_token);require(resumed_bot_host.accepted,"Bot world host resume failed");
 auto saved_roster=loaded_bots.snapshot(resumed_bot_host.player).lobby;
 auto saved_bot=std::find_if(saved_roster.players.begin(),saved_roster.players.end(),[&](const auto&p){return p.id==bot_id;});
 require(saved_bot!=saved_roster.players.end()&&saved_bot->bot&&saved_bot->connected,"Bot lost AI ownership after save");
 ServerConfig solo_config;solo_config.lobby_enabled=true;solo_config.max_players=4;GameServer alone(solo_config);auto solo_host=alone.join("Solo",solo_config.versions);
 for(int action=0;action<2;++action){GameCommand c;c.author=solo_host.player;c.request_id=action+1;c.type=CommandType::Lobby;c.x=int(action?LobbyAction::Start:LobbyAction::Ready);c.y=1;alone.enqueue(c.author,c);alone.pump_boundary();require(alone.take_replies(c.author).back().status==CommandReplyStatus::Applied,"Open empty places blocked solo host");}

 GameServer shared(bc);auto sh=shared.join("Shared",bc.versions);std::uint64_t sr=0;
 auto send_shared=[&](GameCommand c){c.author=sh.player;c.request_id=++sr;shared.enqueue(sh.player,c);shared.pump_boundary();return shared.take_replies(sh.player).back();};
 GameCommand lc;lc.type=CommandType::Lobby;lc.x=int(LobbyAction::AddBot);require(send_shared(lc).status==CommandReplyStatus::Applied,"Shared bot add failed");
 const auto shared_bot=shared.snapshot(sh.player).lobby.players.back().id;
 lc.x=int(LobbyAction::BotTeam);lc.y=int(shared_bot)*10+1;send_shared(lc);lc.x=int(LobbyAction::Ready);lc.y=1;send_shared(lc);lc.x=int(LobbyAction::Start);send_shared(lc);
 shared.tick();require(shared.world().manual_orders().empty(),"Shared bot took unassigned people");
 const auto resident=shared.world().residents().front();GameCommand move;move.type=CommandType::Move;move.residents={resident.id};move.x=int(resident.x);move.y=int(resident.y);require(send_shared(move).status==CommandReplyStatus::Applied,"Initial human order failed");
 GameCommand transfer;transfer.type=CommandType::TransferControl;transfer.residents={resident.id};transfer.target_player=shared_bot;require(send_shared(transfer).status==CommandReplyStatus::Applied,"Could not delegate resident to bot");
 for(int i=0;i<800;++i)shared.tick();
 bool delegated=false;for(const auto&o:shared.world().manual_orders())if(o.issuer&&o.issuer->player_id==shared_bot&&o.resident_id==resident.id)delegated=true;
 require(delegated,"Shared bot ignored delegated resident");
 require(send_shared(move).status==CommandReplyStatus::Applied,"Human could not reclaim bot-controlled resident");
 std::cout<<"Six-player lobby, teams, fog, decisions, pause and restart passed.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

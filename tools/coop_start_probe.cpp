#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
struct Pair {
 net::NetworkServer server;
 ClientSession host{70421,"Host"},guest{70421,"Guest"};
 std::string endpoint,error;
 static net::HostConfig config(int port){net::HostConfig c;c.transport.bind_address="127.0.0.1";c.transport.port=std::uint16_t(port);c.game.lobby_enabled=true;c.game.max_players=4;c.game.population=4;return c;}
 Pair(NationId team,bool independent,int port):server(config(port)){
  check(server.listen(),server.last_error());endpoint="127.0.0.1:"+std::to_string(server.port());
  check(host.connect_remote(endpoint,"",error),error);pump([&]{return host.ready();},"host join");
  if(team!=1){host.lobby_action(LobbyAction::Team,team);pump([&]{return host.nation_id()==team;},"host team");}
  check(guest.connect_remote(endpoint,"",error),error);pump([&]{return guest.ready()&&host.lobby().players.size()==2;},"guest join");
  check(host.nation_id()==team&&guest.nation_id()==team,"New player did not inherit host team");
  if(independent){guest.lobby_action(LobbyAction::Team,2);pump([&]{return guest.nation_id()==2&&host.lobby().players.back().team==2;},"independent choice");}
 }
 template<class F>void pump(F done,const std::string&stage){
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  do{server.poll();host.poll();guest.poll();if(done())return;SDL_Delay(2);}while(std::chrono::steady_clock::now()<deadline);
  const auto describe=[](const auto&orders){std::string out="[";for(std::size_t i=0;i<orders.size()&&i<8;++i){if(i)out+=",";out+="#"+std::to_string(orders[i].id)+"@"+std::to_string(orders[i].building_id)+":"+std::to_string(int(orders[i].phase));}return out+"]";};
  const auto authoritative=server.game().world().economy(host.nation_id());
  throw std::runtime_error("Timeout: "+stage+" / status="+host.production_group_status()+"|"+guest.production_group_status()+" / orders server="+describe(authoritative.orders)+" host="+describe(host.economy().orders)+" guest="+describe(guest.economy().orders)+" / ticks="+std::to_string(server.game().world().ticks())+","+std::to_string(host.ticks())+","+std::to_string(guest.ticks()));
 }
 void sync(){for(int i=0;i<4;++i)server.tick();const auto tick=server.game().world().ticks();pump([&]{return host.ticks()>=tick&&host.ticks()==guest.ticks();},"snapshot sync");}
 void start(){
  host.lobby_action(LobbyAction::Ready,1);guest.lobby_action(LobbyAction::Ready,1);
  pump([&]{return std::all_of(host.lobby().players.begin(),host.lobby().players.end(),[](const auto&p){return p.ready;});},"ready");
  host.lobby_action(LobbyAction::Start);pump([&]{return host.world_ready()&&guest.world_ready();},"world start");
 }
 template<class F>void tick_until(F done,const std::string&stage){
  for(int n=0;n<4000&&!done();++n){server.tick();server.poll();host.poll();guest.poll();if(n%20==0)SDL_Delay(2);}
  if(!done()){
   for(const auto&o:server.game().world().construction_orders())std::cerr<<"construction #"<<o.building_id<<" phase="<<int(o.phase)<<" delivered="<<o.delivered.wood<<","<<o.delivered.stone<<"\n";
   for(const auto&r:server.game().world().residents())std::cerr<<"resident #"<<r.id<<" job="<<int(r.job)<<" order="<<int(r.order_phase)<<" failure="<<int(r.path_failure)<<" at="<<r.x<<","<<r.y<<"\n";
  }
  check(done(),"Simulation did not reach "+stage);sync();
 }
 CommandReply reply(ClientSession&client){
  std::optional<CommandReply> answer;
  pump([&]{while(auto r=client.take_command_reply())if(r->status!=CommandReplyStatus::Queued)answer=*r;return bool(answer);},"command reply");return *answer;
 }
 void drain(){while(host.take_command_reply()){}while(guest.take_command_reply()){};}
 int build(ClientSession&client){
  auto camp=std::find_if(client.buildings().begin(),client.buildings().end(),[](const auto&b){return b.kind==BuildingKind::Camp;});check(camp!=client.buildings().end(),"Camp absent");
  CellCoord target{};bool found=false;for(int radius=2;radius<7&&!found;++radius)for(int dy=-radius;dy<=radius&&!found;++dy)for(int dx=-radius;dx<=radius&&!found;++dx)
   if(client.can_build(BuildingKind::Sawmill,camp->x+dx,camp->y+dy)){target={camp->x+dx,camp->y+dy};found=true;}
  check(found,"No sawmill location");drain();check(client.build(BuildingKind::Sawmill,target.x,target.y).accepted,"Build not queued");
  pump([&]{server.tick();return !server.game().world().export_state().path_batches.empty()||std::any_of(server.game().world().buildings().begin(),server.game().world().buildings().end(),[&](const auto&b){return b.kind==BuildingKind::Sawmill&&b.nation==client.nation_id();});},"build admission");
  int site=0;bool delivered=false;
  tick_until([&]{for(const auto&o:server.game().world().construction_orders())if(o.owner_nation==client.nation_id())delivered|=o.delivered.wood>0;
   for(const auto&b:server.game().world().buildings())if(b.kind==BuildingKind::Sawmill&&b.nation==client.nation_id()&&b.progress==1){site=b.id;return true;}return false;},"construction delivery");
  check(delivered,"Construction bypassed physical materials");
  check(std::any_of(client.buildings().begin(),client.buildings().end(),[&](const auto&b){return b.id==site&&b.nation==client.nation_id();}),"Own completed building missing");
  std::vector<int>ids;for(const auto&r:client.residents())ids.push_back(r.id);drain();check(client.stop(ids).accepted,"Stop not queued");check(reply(client).status==CommandReplyStatus::Applied,"Explicit preparation stop rejected");
  tick_until([&]{for(const auto&r:server.game().world().residents())if(r.nation==client.nation_id()&&(r.order_phase==OrderPhase::PathPending||r.profession.deferred))return false;return true;},"free workers");
  return site;
 }
};
}
int main(int argc,char**argv){try{
 check(argc>1,"Output directory required");const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
 {
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>window(SDL_CreateWindow("Ribterra 0.25.10 cooperative acceptance",1440,1000,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 check(bool(window),SDL_GetError());const auto base=std::filesystem::path(SDL_GetBasePath());render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"ui.json")<<R"({"locale":"uk","ui_scale":1})";GameUi ui(renderer,assets,locale,output/"ui.json");GameView view(renderer);view.load_art(assets);render::Frame2D frame;int captures=0;
 auto draw=[&](ClientSession&client,int site,const std::string&name,const std::string&status=""){
  frame.clear_commands();if(client.world_ready()){view.focus_group(client,{client.residents().front().id});view.compose(client,0,frame,false);ui.show_world();}
  ui.set_view_state(&client,{},false,1,WorldLayer::Surface,site?std::vector<int>{site}:std::vector<int>{});ui.update(client,status);ui.compose(frame);renderer.render(frame,output/(name+".png"));++captures;
 };
 auto press_key=[&](SDL_Keycode code,SDL_Keymod mods=SDL_KMOD_NONE){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;e.key.mod=mods;ui.process_event(e);e.type=SDL_EVENT_KEY_UP;ui.process_event(e);};
 for(NationId team:{NationId{1},NationId{2}}){
  ui.reset_world_state();Pair pair(team,false,29930+team);const std::string prefix="team-"+std::to_string(team);
  draw(pair.host,0,prefix+"-lobby");check(ui.element_rml_for_test("lobby-coop")==locale.format("lobby.shared_settlement",{{"team",team}}),"Lobby does not explain shared settlement");
  pair.start();check(pair.host.residents()==pair.guest.residents()&&pair.host.buildings()==pair.guest.buildings(),"Shared starting settlement differs");
  const int site=pair.build(pair.host);draw(pair.guest,site,prefix+"-guest-building");check(ui.element_rml_for_test("bp-owner").find("#"+std::to_string(team))!=std::string::npos,"Wrong inspector owner");
  check(ui.activate_for_test("building-summary-details"),"Compact building details action missing");draw(pair.guest,site,prefix+"-guest-building-details");
  check(ui.visible_for_test("bp-form"),"Own production form hidden");ui.activate_for_test("economy-open");ui.set_view_state(&pair.guest,{},false,1,WorldLayer::Surface,{site});ui.update(pair.guest);check(ui.element_rml_for_test("economy-sites").find("production-"+std::to_string(site)+"-3")!=std::string::npos,"Own enterprise missing from economy overview");ui.activate_for_test("economy-close");ui.take_actions();
  draw(pair.host,site,prefix+"-order");check(ui.focus_for_test("bp-quantity"),"Batch input missing");press_key(SDLK_A,SDL_KMOD_CTRL);SDL_Event input{};input.type=SDL_EVENT_TEXT_INPUT;input.text.text="2";ui.process_event(input);ui.update(pair.host);ui.activate_for_test("bp-main");auto actions=ui.take_actions();
  check(actions.size()==1&&actions[0].type==UiActionType::BuildingProduction&&actions[0].buildings==std::vector<int>{site}&&actions[0].quantity==2,"Owned UI cannot issue production");
  pair.drain();check(pair.host.production_group(actions[0].buildings,actions[0].recipe,actions[0].quantity).accepted,"Production group not queued");
  pair.pump([&]{const auto economy=pair.server.game().world().economy(team);return std::any_of(economy.orders.begin(),economy.orders.end(),[&](const auto&o){return o.building_id==site&&o.recipe==actions[0].recipe&&o.requested==actions[0].quantity;});},"authoritative production acceptance");
  const auto authoritative=pair.server.game().world().economy(team);const auto accepted=std::find_if(authoritative.orders.begin(),authoritative.orders.end(),[&](const auto&o){return o.building_id==site&&o.recipe==actions[0].recipe&&o.requested==actions[0].quantity;});check(accepted!=authoritative.orders.end(),"Accepted production order vanished from server");const int accepted_id=accepted->id;
  pair.sync();const auto projected=[&](const ClientSession&client){return std::find_if(client.economy().orders.begin(),client.economy().orders.end(),[&](const auto&o){return o.id==accepted_id&&o.building_id==site&&o.recipe==actions[0].recipe&&o.requested==actions[0].quantity;});};check(projected(pair.host)!=pair.host.economy().orders.end()&&projected(pair.guest)!=pair.guest.economy().orders.end(),"Accepted server production order #"+std::to_string(accepted_id)+" did not synchronize to both teammates");
  const auto active=*projected(pair.host);check(pair.guest.production_worker_reason(active.resident_id)=="economy.not_controller","Peer does not see worker controller");
  pair.drain();pair.guest.cancel_production(active.id,pair.guest.economy().revision);const auto rejection=pair.reply(pair.guest);check(rejection.status==CommandReplyStatus::Rejected&&rejection.reason=="economy.not_controller","Peer cancelled another controller's order");
  ui.set_view_state(&pair.guest,{},false,1,WorldLayer::Surface,{site});draw(pair.guest,site,prefix+"-rejected",ui.format_command_reply(rejection));check(ui.element_rml_for_test("status").find("Host")!=std::string::npos,"Rejection omits controlling player's name");
  pair.tick_until([&]{auto e=pair.server.game().world().economy(NationId(team));return e.orders.front().phase==ProductionPhase::Output;},"physical production output");
  check(pair.host.economy()==pair.guest.economy()&&pair.host.resident_logistics()==pair.guest.resident_logistics(),"Clients disagree about output in transit");draw(pair.host,site,prefix+"-host-output");draw(pair.guest,site,prefix+"-guest-output");
  pair.tick_until([&]{return pair.server.game().world().economy(NationId(team)).orders.front().phase==ProductionPhase::Complete;},"two delivered batches");
  check(pair.host.economy()==pair.guest.economy()&&pair.host.inventory()==pair.guest.inventory(),"Production totals differ");check(pair.host.economy().stored[int(ItemKind::Planks)]==12,"Two batches did not deliver 12 planks");draw(pair.guest,site,prefix+"-delivered");
  // Explicit handoff lets the teammate start the next order with the same person.
  pair.drain();pair.host.transfer_control({active.resident_id},pair.guest.player_id());check(pair.reply(pair.host).status==CommandReplyStatus::Applied,"Worker handoff failed");pair.sync();
  pair.drain();pair.guest.production(site,active.resident_id,3,1,pair.guest.economy().revision);check(pair.reply(pair.guest).status==CommandReplyStatus::Applied,"Transferred worker cannot produce");pair.sync();
  pair.tick_until([&]{return pair.server.game().world().economy(NationId(team)).orders.back().phase==ProductionPhase::Complete;},"teammate delivery after handoff");
  check(pair.host.economy()==pair.guest.economy()&&pair.guest.economy().stored[int(ItemKind::Planks)]==18,"Teammate output missing or differs between clients");draw(pair.guest,site,prefix+"-handoff-delivered");
  const auto before=pair.host.inventory();ClientSession late(70421,"Late teammate");std::string error;check(late.connect_remote(pair.endpoint,"",error),error);
  pair.pump([&]{late.poll();return late.world_ready();},"late teammate");check(late.nation_id()==team&&late.inventory()==before,"Late join created/reset a settlement");
 }
 {
  ui.reset_world_state();Pair pair(1,true,29933);draw(pair.guest,0,"independent-lobby");check(ui.element_rml_for_test("lobby-coop")==locale.format("lobby.separate_settlements",{{"team",2}}),"Independent team choice is misleading");pair.start();
  const auto foreign_before=pair.host.inventory();const int site=pair.build(pair.guest);draw(pair.guest,site,"nation-2-owned-building");
  check(pair.host.inventory().total.wood==foreign_before.total.wood&&pair.host.inventory().total.stone==foreign_before.total.stone,"Build spent another nation's resources");
  check(std::none_of(pair.host.buildings().begin(),pair.host.buildings().end(),[&](const auto&b){return b.id==site;}),"Foreign building leaked into owned list");
  pair.drain();pair.host.production(site,pair.host.residents().front().id,3,1,pair.host.economy().revision);auto rejection=pair.reply(pair.host);
  check(rejection.status==CommandReplyStatus::Rejected&&rejection.reason=="economy.foreign_building","Foreign building accepted production");draw(pair.host,0,"foreign-building-rejected",ui.format_command_reply(rejection));
  check(pair.server.game().world().economy(2).orders.empty(),"Rejected foreign command mutated production");
  pair.drain();check(pair.guest.production_group({site},3,2).accepted,"Nation 2 group rejected");pair.pump([&]{const auto economy=pair.server.game().world().economy(2);return std::any_of(economy.orders.begin(),economy.orders.end(),[&](const auto&o){return o.building_id==site&&o.recipe==3&&o.requested==2;});},"authoritative independent production");const auto independent=pair.server.game().world().economy(2);const auto independent_order=std::find_if(independent.orders.begin(),independent.orders.end(),[&](const auto&o){return o.building_id==site&&o.recipe==3&&o.requested==2;});check(independent_order!=independent.orders.end(),"Accepted independent order vanished from server");const int independent_id=independent_order->id;pair.sync();check(std::any_of(pair.guest.economy().orders.begin(),pair.guest.economy().orders.end(),[&](const auto&o){return o.id==independent_id&&o.building_id==site&&o.recipe==3&&o.requested==2;}),"Accepted independent production order #"+std::to_string(independent_id)+" did not synchronize to its client");
  pair.tick_until([&]{return pair.server.game().world().economy(2).orders.front().phase==ProductionPhase::Complete;},"independent delivery");
  check(pair.guest.economy().stored[int(ItemKind::Planks)]==12&&pair.host.economy().stored[int(ItemKind::Planks)]==0,"Independent output crossed ownership");draw(pair.guest,site,"nation-2-delivered");
  for(int kind=0;kind<3;++kind){net::ClientConfig config;config.name="Incompatible";if(kind==0)++config.versions.protocol;else if(kind==1)++config.versions.rules;else config.versions.content=std::string(64,'0');net::NetworkClient incompatible(config);check(incompatible.connect(pair.endpoint),"Mismatch connect failed early");
   pair.pump([&]{incompatible.poll();return !incompatible.last_error().empty();},"version rejection");const std::string key=kind==0?"network.version_protocol":kind==1?"network.version_rules":"network.version_content";check(incompatible.last_error()==key&&!incompatible.ready(),"Wrong version rejection");
   ui.show_menu();ui.update(nullptr,key);frame.clear_commands();ui.compose(frame);renderer.render(frame,output/(key+".png"));check(ui.element_rml_for_test("menu-status")==locale.text(key),"Version explanation not rendered");++captures;
  }
 }
 std::ofstream(output/"report.json")<<nlohmann::json{{"version",engine_version},{"physical_windows_pcs",1},{"two_physical_pcs","not_run"},{"shared_teams",{1,2}},{"network_clients_per_scenario",2},{"delivered_planks_per_scenario",12},{"independent_nation_2",true},{"foreign_building_rejected",true},{"controller_handoff",true},{"late_join_preserves_settlement",true},{"version_rejections",3},{"rendered_captures",captures}}.dump(2);
 }SDL_Quit();std::cout<<"Cooperative start, team 1/2 ownership, physical building/production delivery, matching clients, controller handoff and version errors passed on one Windows PC.\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace ribterra;
void require(bool v,const char*m){if(!v)throw std::runtime_error(m);}
int main(int argc,char**argv){try{
 require(argc>1,"Output directory required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
 {
 const int width=argc>3?std::stoi(argv[3]):1440,height=argc>4?std::stoi(argv[4]):900;
 auto window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>(SDL_CreateWindow("Ribterra online verification",width,height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 auto base=std::filesystem::path(SDL_GetBasePath());render::Renderer2D renderer(window.get(),base/"assets",{argc>2&&std::string(argv[2])=="gpu"?render::Backend::Direct3D12:render::Backend::Software});const auto logical_size=renderer.fitted_logical_size();renderer.set_logical_size(logical_size);AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"ui.json")<<R"({"locale":"uk","ui_scale":1})";GameUi ui(renderer,assets,locale,output/"ui.json",logical_size);GameView view(renderer);view.set_logical_size(logical_size);view.load_art(assets);render::Frame2D frame;frame.logical_size=logical_size;
 ui.update(nullptr);require(ui.activate_for_test("open-online"),"Online menu button missing");ui.update(nullptr);ui.compose(frame);renderer.render(frame,output/"online-menu.png");
 const auto host_button=ui.element_center_for_test("online-host");require(host_button.x>0&&host_button.x<width&&host_button.y>0&&host_button.y<height,"Host button outside viewport");
 SDL_Event click{};click.type=SDL_EVENT_MOUSE_MOTION;click.motion.x=host_button.x;click.motion.y=host_button.y;ui.process_event(click);
 click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.button=SDL_BUTTON_LEFT;ui.process_event(click);click.type=SDL_EVENT_MOUSE_BUTTON_UP;ui.process_event(click);
 auto actions=ui.take_actions();require(actions.size()==1&&actions[0].type==UiActionType::HostGame,"Host form did not submit");require(nlohmann::json::parse(actions[0].text).at("capacity")=="4","Default capacity must be four");
 net::HostConfig config;config.game.lobby_enabled=true;config.game.max_players=6;config.game.population=3;config.transport.port=29875;config.password="lobby-test";net::NetworkServer server(config);require(server.listen(),"Could not start server");
 std::vector<std::unique_ptr<ClientSession>> clients;std::string error;
 for(int i=0;i<6;++i){auto c=std::make_unique<ClientSession>(70421,"Player "+std::to_string(i+1));require(c->connect_remote("127.0.0.1:29875","lobby-test",error),error.c_str());clients.push_back(std::move(c));}
 ui.show_world();ui.update(clients[0].get());
 require(!ui.world_visible(),"Handshake exposed the world before a snapshot");
 require(ui.visible_for_test("menu-card"),"Connecting client has no visible menu");
 require(!ui.element_rml_for_test("menu-status").empty(),"Connecting client has no status feedback");
 require(ui.element_rml_for_test("menu-status")==locale.text("network.connecting"),"Contacting host incorrectly claims world download");
 frame.clear_commands();frame.logical_size=logical_size;ui.compose(frame);renderer.render(frame,output/"connecting.png");
 ui.update(clients[0].get(),"network.connect_timeout");
 require(ui.element_rml_for_test("menu-status")==locale.text("network.connect_timeout"),"Timeout does not explain internet connection requirements");
 frame.clear_commands();ui.compose(frame);renderer.render(frame,output/"connection-timeout.png");
 std::string stage="join";
 auto pump=[&](auto predicate){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(std::chrono::steady_clock::now()<deadline){server.poll();for(auto&c:clients)c->poll();if(predicate())return;SDL_Delay(2);}for(auto&c:clients){std::cerr<<"player="<<c->player_id()<<" ready="<<c->ready()<<" tick="<<c->ticks()<<" server="<<server.game().world().ticks()<<"\n";if(auto error=c->take_notice())std::cerr<<*error<<"\n";}throw std::runtime_error("Online lobby synchronization timed out: "+stage);};
 pump([&]{for(auto&c:clients)if(!c->ready()||c->lobby().players.size()!=6)return false;return true;});
 std::sort(clients.begin(),clients.end(),[](const auto&a,const auto&b){return a->player_id()<b->player_id();});
 clients[0]->connection_addresses={{"192.0.2.10:29875","Wi-Fi — 192.0.2.10:29875"},{"198.51.100.9:29875","VPN — 198.51.100.9:29875"}};
 auto draw=[&](const char*name){frame.clear_commands();if(clients[0]->lobby().started)view.compose(*clients[0],clients[0]->ticks()*tick_seconds,frame,false);ui.set_view_state(clients[0].get(),{},false,1);ui.update(clients[0].get());ui.compose(frame);renderer.render(frame,output/name);};
 draw("lobby-default-coop.png");
 require(ui.element_rml_for_test("lobby-coop").find(locale.format("lobby.shared_settlement",{{"team",1}}))!=std::string::npos,"Default shared settlement is not explained");
 for(int i=1;i<6;++i)clients[i]->lobby_action(LobbyAction::Team,i+1);
 pump([&]{for(int i=1;i<6;++i)if(clients[i]->nation_id()!=NationId(i+1))return false;return true;});
 draw("lobby-separate.png");require(ui.visible_for_test("lobby-card"),"Ready room not visible");
 require(ui.select_option_rml_for_test("lobby-address-select",0).find("Wi-Fi")!=std::string::npos,"Preferred LAN adapter is not first");
 require(ui.select_option_rml_for_test("lobby-address-select",1).find("VPN")!=std::string::npos,"VPN address alternative is missing");
 require(ui.focus_for_test("lobby-address-select"),"Address selector cannot receive keyboard input");
 SDL_Event choose_address{};choose_address.type=SDL_EVENT_KEY_DOWN;choose_address.key.key=SDLK_DOWN;ui.process_event(choose_address);
 choose_address.type=SDL_EVENT_KEY_UP;ui.process_event(choose_address);
 require(ui.activate_for_test("lobby-invite"),"Copy address button missing");
 char* copied_address=SDL_GetClipboardText();require(copied_address&&std::string(copied_address)=="198.51.100.9:29875","Copy address ignored the selected VPN endpoint");SDL_free(copied_address);
 ui.update(clients[0].get());require(ui.element_rml_for_test("lobby-invite")==locale.text("lobby.address_copied"),"Successful address copy has no confirmation");
 choose_address.type=SDL_EVENT_KEY_DOWN;choose_address.key.key=SDLK_UP;ui.process_event(choose_address);
 choose_address.type=SDL_EVENT_KEY_UP;ui.process_event(choose_address);ui.update(clients[0].get());
 require(ui.element_rml_for_test("lobby-invite")==locale.text("lobby.invite_address"),"Changing invitation address kept stale copied confirmation");
 stage="team";clients[1]->lobby_action(LobbyAction::Team,1);pump([&]{for(auto&c:clients)if(c->lobby().players[1].team!=1)return false;return true;});
 stage="ready";for(auto&c:clients)c->lobby_action(LobbyAction::Ready,1);
 pump([&]{for(const auto&p:clients[0]->lobby().players)if(!p.ready)return false;return true;});draw("lobby-team.png");
 stage="start";clients[0]->lobby_action(LobbyAction::Start);pump([&]{for(auto&c:clients)if(!c->lobby().started)return false;return true;});
 require(clients[0]->residents()==clients[1]->residents(),"Team snapshot differs");require(clients[0]->residents()[0].nation!=clients[2]->residents()[0].nation,"Separate nation missing");view.focus_group(*clients[0],{clients[0]->residents()[0].id});draw("online-world.png");
 require(ui.world_visible(),"Start did not enter gameplay");
 // Exercise the actual RmlUi controls and keyboard paths with host/guest sessions.
 require(clients[0]->can_change_speed()&&!clients[1]->can_change_speed(),"Speed ownership incorrect");
 ui.set_view_state(clients[0].get(),{},false,1);ui.update(clients[0].get());
 ui.activate_for_test("speed-4");auto speed_actions=ui.take_actions();require(speed_actions.size()==1&&speed_actions[0].type==UiActionType::Speed&&speed_actions[0].value==4,"Host speed button unavailable");
 ui.set_view_state(clients[1].get(),{},false,1);ui.update(clients[1].get());
 for(auto id:{"speed-1","speed-2","speed-4"}){require(!ui.focus_for_test(id),"Guest speed control is enabled");ui.activate_for_test(id);require(ui.take_actions().empty(),"Guest speed click queued a command");}
 for(int speed:{1,2,4})require(!clients[1]->lobby_action(LobbyAction::Speed,speed).accepted,"Guest speed reached transport");
 for(auto code:{SDLK_1,SDLK_2,SDLK_3}){SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.key=code;view.handle_event(key,*clients[1]);}
 stage="pause controls";clients[1]->lobby_action(LobbyAction::Pause,1);pump([&]{return clients[0]->lobby().paused&&clients[1]->lobby().paused;});
 view.set_paused(clients[1]->lobby().paused);view.set_speed(clients[1]->lobby().speed);require(view.paused(),"Speed refresh hid server pause");
 ui.set_view_state(clients[1].get(),{},clients[1]->lobby().paused,1);ui.update(clients[1].get());const auto pause_markup=ui.element_rml_for_test("pause");if(!ui.visible_for_test("pause")||pause_markup!="&gt;")throw std::runtime_error("Paused network game does not show the Resume glyph: lobby.paused="+std::to_string(clients[1]->lobby().paused)+" visible="+std::to_string(ui.visible_for_test("pause"))+" markup=["+pause_markup+"]");
 frame.clear_commands();view.compose(*clients[1],0,frame,false);ui.compose(frame);renderer.render(frame,output/"guest-speed-locked.png");
 clients[0]->lobby_action(LobbyAction::Speed,4);pump([&]{return clients[0]->lobby().speed==4&&clients[1]->lobby().speed==4;});require(clients[1]->lobby().paused,"Speed change cleared server pause");
 stage="resume controls";clients[0]->lobby_action(LobbyAction::Pause,0);pump([&]{return !clients[0]->lobby().paused&&!clients[1]->lobby().paused;});
 ui.set_view_state(clients[0].get(),{},false,4);ui.update(clients[0].get());require(ui.focus_for_test("speed-1"),"Returning to host did not unlock speed controls");
 stage="chat";require(clients[0]->send_chat("Hello from the lobby test",error),error.c_str());pump([&]{return !clients[5]->take_chat().empty();});
 stage="tick";for(int i=0;i<4;++i)server.tick();pump([&]{for(auto&c:clients)if(c->ticks()!=server.game().world().ticks())return false;return true;});

 net::HostConfig bot_config;bot_config.game.lobby_enabled=true;bot_config.game.max_players=4;bot_config.game.population=4;bot_config.transport.port=29876;
 net::NetworkServer bot_server(bot_config);require(bot_server.listen(),"Bot server failed");ClientSession host(70421,"Taras");require(host.connect_remote("127.0.0.1:29876","",error),error.c_str());
 auto bot_pump=[&](auto predicate){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(std::chrono::steady_clock::now()<deadline){bot_server.poll();host.poll();if(predicate())return;SDL_Delay(2);}throw std::runtime_error("Bot lobby synchronization failed");};
 bot_pump([&]{return host.ready();});host.connection_label="127.0.0.1:29876";
 auto bot_draw=[&](const char*name){frame.clear_commands();frame.logical_size=logical_size;ui.update(host);ui.compose(frame);renderer.render(frame,output/name);};
 bot_draw("lobby-empty.png");require(ui.visible_for_test("lobby-slot-action-1"),"Add bot action hidden");
 require(ui.activate_for_test("lobby-slot-action-1"),"Add bot button missing");auto bot_actions=ui.take_actions();require(bot_actions.size()==1&&bot_actions[0].value==int(LobbyAction::AddBot),"Add bot UI failed");
 host.lobby_action(LobbyAction::AddBot);bot_pump([&]{return host.lobby().players.size()==2;});
 bot_draw("lobby-bot.png");require(ui.activate_for_test("lobby-slot-action-1"),"Remove bot button missing");bot_actions=ui.take_actions();require(bot_actions.size()==1&&bot_actions[0].value==int(LobbyAction::RemoveBot),"Remove bot UI failed");
 host.lobby_action(LobbyAction::RemoveBot,bot_actions[0].quantity);bot_pump([&]{return host.lobby().players.size()==1;});
 host.lobby_action(LobbyAction::AddBot);bot_pump([&]{return host.lobby().players.size()==2;});host.lobby_action(LobbyAction::AddBot);bot_pump([&]{return host.lobby().players.size()==3;});
 host.lobby_action(LobbyAction::Ready,1);bot_pump([&]{return host.lobby().players[0].ready;});bot_draw("lobby-bots-ready.png");
 auto start=ui.element_center_for_test("lobby-start");require(start.x>0&&start.x<width&&start.y>0&&start.y<height,"Start button outside viewport");
 host.lobby_action(LobbyAction::Start);bot_pump([&]{return host.lobby().started;});for(int i=0;i<4;++i)bot_server.tick();bot_pump([&]{return host.ticks()==bot_server.game().world().ticks();});
 require(bot_server.game().world().residents().size()==12,"Bot settlements did not spawn");
 std::ofstream(output/"verification.txt")<<"Six real UDP clients: ready room, teams, start, isolated nations, chat and synchronized tick passed. Host UI added and removed bots, then started with two bots and one open slot.\n";
 }
 SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

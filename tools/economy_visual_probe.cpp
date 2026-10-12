#include "ribterra/asset_catalog.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
using namespace ribterra;
void check(bool b,const std::string&m){if(!b)throw std::runtime_error(m);}
int main(int argc,char**argv){try{
 check(argc>1,"Output folder required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());{
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>window(SDL_CreateWindow("Ribterra 0.23",1440,900,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 auto base=std::filesystem::path(SDL_GetBasePath());render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});const auto logical_size=renderer.fitted_logical_size();renderer.set_logical_size(logical_size);AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"settings.json")<<R"({"locale":"uk","ui_scale":1})";
 GameUi ui(renderer,assets,locale,output/"settings.json",logical_size);GameView view(renderer);view.set_logical_size(logical_size);view.load_art(assets);ClientSession session(70421,"Economy probe");
 World world(70421,4);auto state=world.export_state();
 state.buildings.push_back({2,BuildingKind::Sawmill,26,26,1,WorldLayer::Surface,1});
 state.buildings.push_back({3,BuildingKind::Workshop,29,26,1,WorldLayer::Surface,1});
 state.buildings.push_back({4,BuildingKind::Farm,26,29,1,WorldLayer::Surface,1});
 state.buildings.push_back({5,BuildingKind::Kitchen,29,29,1,WorldLayer::Surface,1});state.next_building_id=6;
 std::string error;check(world.import_state(state,error),error);check(world.save(output/"fixture.save",error),error);check(session.load(output/"fixture.save",error),error);
 ui.show_world();render::Frame2D frame;
 auto draw=[&](const char*name){view.compose(session,session.ticks()*tick_seconds,frame,false);ui.set_view_state(&session,{1},false,1);ui.update(session);ui.compose(frame);renderer.render(frame,name?output/name:std::filesystem::path{});};
 auto click=[&](const char*id){check(ui.visible_for_test(id),std::string("UI target hidden: ")+id);check(ui.focus_for_test(id),std::string("UI focus missed: ")+id);draw(nullptr);auto p=ui.element_center_for_test(id);SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=p.x;e.motion.y=p.y;ui.process_event(e);e={};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=p.x;e.button.y=p.y;const bool down=ui.process_event(e);e.type=SDL_EVENT_MOUSE_BUTTON_UP;const bool up=ui.process_event(e);if(!down&&!up)throw std::runtime_error(std::string("UI click missed: ")+id+" center="+std::to_string(p.x)+","+std::to_string(p.y)+" focus="+ui.focused_element_for_test()+" markup="+ui.element_rml_for_test(id));};
 draw(nullptr);click("hud-settings-toggle");draw(nullptr);check(ui.visible_for_test("system-controls"),"Utility tray did not open after RmlUi update");click("economy-open");draw("orders.png");click("production-2-3");auto actions=ui.take_actions();check(actions.size()==1&&actions.front().type==UiActionType::Production,"Production button lost typed action");auto a=actions.front();
 check(session.production(a.value,a.residents.front(),a.recipe,a.quantity,a.settlement_revision).accepted,"UI order rejected");session.advance();draw("delivery.png");
 check(session.economy().orders.size()==1,"Server snapshot lost order");
 for(int n=0;n<1000&&session.economy().orders.front().phase!=ProductionPhase::Working;++n)session.advance();draw("working.png");
 check(session.economy().orders.front().phase==ProductionPhase::Working,"Worker never reached sawmill");
 check(SDL_SetWindowSize(window.get(),1100,700),SDL_GetError());SDL_PumpEvents();draw("compact.png");
 for(int n=0;n<1200&&production_active(session.economy().orders.front());++n)session.advance();draw("complete.png");
 check(session.economy().stored[3]==6,"Finished planks missing from UI snapshot");
 }SDL_Quit();std::cout<<"Economy rendered UI, authoritative order, delivery, work and compact layout passed.\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

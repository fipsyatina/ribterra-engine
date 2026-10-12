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
void require(bool v,const char *m){if(!v)throw std::runtime_error(m);}
int main(int argc,char **argv){try{
 require(argc>1,"Output directory required");const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
 {
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ribterra 0.21.2",1440,900,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 require(bool(window),SDL_GetError());const auto base=std::filesystem::path(SDL_GetBasePath());
 render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});
 AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"settings.json")<<R"({"locale":"uk","ui_scale":1})";
 GameUi ui(renderer,assets,locale,output/"settings.json");GameView view(renderer);view.load_art(assets);
 ClientSession session(70421,"Settlement probe");ui.show_world();render::Frame2D frame;
 auto draw=[&](const char *name){
  view.compose(session,session.ticks()*tick_seconds,frame,false);ui.set_view_state(&session,{},false,1);ui.update(session);ui.compose(frame);
  renderer.render(frame,name?output/name:std::filesystem::path{});
 };
 auto click=[&](const char *id){
  auto point=ui.element_center_for_test(id);SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=point.x;e.motion.y=point.y;ui.process_event(e);
  e={};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=point.x;e.button.y=point.y;require(ui.process_event(e),"Button press missed UI");
  e.type=SDL_EVENT_MOUSE_BUTTON_UP;require(ui.process_event(e),"Button release missed UI");
 };
 draw(nullptr);click("settlement-open");draw("day.png");click("settlement-recruit");
 auto actions=ui.take_actions();require(actions.size()==1 && actions.front().type==UiActionType::SettlementDecision,"Recruit click did not emit one decision");
 require(session.decide(SettlementDecision::Recruit,actions.front().settlement_revision).accepted,"Recruit not queued");session.advance();draw("arrival.png");
 require(session.settlement().arrival_tick && session.inventory().stored.food==50,"Recruit UI did not reach authority");
 for(int i=1;i<int(recruitment_ticks);++i)session.advance();draw("young.png");
 require(session.settlement().young.size()==1 && session.residents().size()==11,"Young settler not visible");
 // Forced night and youth work are deliberately hidden by current alpha rules.
 require(!ui.visible_for_test("settlement-night"),"Deferred night control is visible");
 require(!ui.visible_for_test(("youth-force-"+std::to_string(session.settlement().young.front().resident_id)).c_str()),"Deferred youth control is visible");
 require(SDL_SetWindowSize(window.get(),1100,700),SDL_GetError());SDL_PumpEvents();draw("compact.png");
 require(!ui.visible_for_test("settlement-discontent"),"Deferred discontent meter is visible");
 }
 SDL_Quit();std::cout<<"Settlement UI recruitment, timers, young residents, alpha-hidden controls and compact rendering passed.\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

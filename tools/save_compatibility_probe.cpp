#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace ribterra;
namespace fs=std::filesystem;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
 check(argc>1,"Output directory required");const auto out=fs::absolute(argv[1]);fs::create_directories(out);
 const auto fixtures=fs::path(__FILE__).parent_path().parent_path()/"tests/fixtures/alpha-0257";
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());{
 const int width=argc>2?std::stoi(argv[2]):1440,height=argc>3?std::stoi(argv[3]):900;
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ribterra save compatibility",width,height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 const auto base=fs::path(SDL_GetBasePath());render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(out/"ui.json")<<R"({"locale":"uk","ui_scale":1})";GameUi ui(renderer,assets,locale,out/"ui.json",{unsigned(width),unsigned(height)});GameView view(renderer);view.load_art(assets);render::Frame2D frame;
 ClientSession session;std::string error;int captures=0;
 auto draw=[&](const char* name,const std::string& status){frame.clear_commands();view.compose(session,0,frame,false);frame.logical_size={unsigned(width),unsigned(height)};ui.set_view_state(&session,{1},true,1,WorldLayer::Surface);ui.update(session,status);ui.compose(frame);renderer.render(frame,out/(std::string(name)+".png"));++captures;};
 const auto source=out/"combined.ribsave";fs::copy_file(fixtures/"combined.ribsave",source,fs::copy_options::overwrite_existing);check(session.load(source,error),error);
 const auto status=ui.load_status(session.last_load());check(status.find(locale.text("save.rations_reset"))!=std::string::npos&&status.find(locale.text("save.attacks_reset"))!=std::string::npos,"Migration changes not explained");
 ui.show_world();view.focus_group(session,{1});ui.activate_for_test("stocks-toggle");draw("migration-success",status);check(ui.element_rml_for_test("status").find("0.25.7")!=std::string::npos,"Source version absent in HUD");
 ui.activate_for_test("stocks-close");ui.show_menu();
 ClientSession rejected;
 for(auto name:{"expedition-travel","expedition-hunt","expedition-returning","expedition-defence"}){
  const auto file=out/(std::string(name)+".ribsave");fs::copy_file(fixtures/(std::string(name)+".ribsave"),file,fs::copy_options::overwrite_existing);check(!rejected.load(file,error)&&error=="alpha.save_expedition","Expedition accepted");draw(name,ui.load_status(rejected.last_load(),error));check(ui.element_rml_for_test("menu-status").find(locale.text("alpha.save_expedition"))!=std::string::npos,"Refusal missing in actual menu");
 }
 const auto corrupted=out/"damaged.save";fs::copy_file(source,corrupted,fs::copy_options::overwrite_existing);{std::fstream f(corrupted,std::ios::binary|std::ios::in|std::ios::out);f.seekp(-1,std::ios::end);f.put(char(0x31));}
 check(!rejected.load(corrupted,error),"Corrupt input accepted");draw("damaged-file",ui.load_status(rejected.last_load(),error));check(ui.element_rml_for_test("menu-status").find(locale.text("save.checksum"))!=std::string::npos,"Checksum reason absent");
 const auto unknown=out/"unknown.save";std::ofstream(unknown)<<"not a Ribterra world";check(!rejected.load(unknown,error),"Unknown format accepted");draw("unknown-format",ui.load_status(rejected.last_load(),error));
 net::HostConfig config;config.save_path=out/"expedition-returning.ribsave";std::string host_error;try{net::NetworkServer host(config);}catch(const std::exception&e){host_error=e.what();}
 check(host_error.starts_with("save.load_failed|"),"Host did not preserve reason/version");draw("host-refusal",host_error);check(ui.element_rml_for_test("menu-status").find(locale.text("alpha.save_expedition"))!=std::string::npos,"Host refusal not localized");
 ui.activate_for_test("new-game");const auto actions=ui.take_actions();check(std::any_of(actions.begin(),actions.end(),[](const auto&a){return a.type==UiActionType::NewGame;}),"Refused save blocks a new game");
 std::ofstream(out/"report.txt")<<"Native UI: migration/copy destination, normalized rations and attacks, four expedition refusals, corruption, unknown format, host refusal and New Game passed. Captures: "<<captures<<"\n";
 }SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

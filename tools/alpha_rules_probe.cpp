#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace ribterra;
void check(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
 check(argc>1,"Output directory required");const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());{
 const int width=argc>2?std::stoi(argv[2]):1440,height=argc>3?std::stoi(argv[3]):900;
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ribterra alpha rules",width,height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 const auto base=std::filesystem::path(SDL_GetBasePath());render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});
 AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");std::ofstream(output/"ui.json")<<R"({"locale":"uk","ui_scale":1})";
 GameUi ui(renderer,assets,locale,output/"ui.json",{unsigned(width),unsigned(height)});GameView view(renderer);view.load_art(assets);render::Frame2D frame;frame.logical_size={unsigned(width),unsigned(height)};
 World world(70421,1);auto state=world.export_state();
 for(auto [kind,x]:{std::pair{BuildingKind::Workshop,27},std::pair{BuildingKind::ClayPit,29},std::pair{BuildingKind::Research,31}})state.buildings.push_back({state.next_building_id++,kind,x,29,1,WorldLayer::Surface,1});
 state.buildings.push_back({state.next_building_id++,BuildingKind::Sawmill,33,29,1,WorldLayer::Surface,1});
 state.item_stacks.push_back({state.next_item_stack_id++,ItemKind::Grain,7,1,{ItemLocationKind::Ground,{24,24},0}});
 state.item_stacks.push_back({state.next_item_stack_id++,ItemKind::Tools,2,1,{ItemLocationKind::Storage,{},1}});
 std::string error;check(world.import_state(state,error),error);check(world.production(5,1,3,1,0).accepted,"Cargo production rejected");
 for(int i=0;i<1800&&!world.economy().carried[int(ItemKind::Planks)];++i)world.tick();
 check(world.economy().carried[int(ItemKind::Planks)]==6,"Production cargo never reached delivery");check(world.save(output/"scenario.save",error),error);
 ClientSession session;check(session.load(output/"scenario.save",error),error);ui.show_world();view.focus_group(session,{1});
 int captures=0;auto draw=[&](const char* name,std::vector<int> buildings={}){
  frame.clear_commands();view.compose(session,0,frame,false);frame.logical_size={unsigned(width),unsigned(height)};ui.set_view_state(&session,{1},false,1,WorldLayer::Surface,buildings);ui.update(session);ui.compose(frame);renderer.render(frame,output/(std::string(name)+".png"));++captures;
 };
 draw("alpha-hud");
 for(const char* id:{"resource-coal","resource-iron","resource-copper","resource-gold","resource-silver","resource-steel","resource-crystals","temperature-panel","nav-defense"})check(!ui.visible_for_test(id),std::string("Placeholder visible: ")+id);
 ui.activate_for_test("build-more");draw("alpha-catalog");int count=0;
 for(const auto& d:building_catalog)if(d.kind!=BuildingKind::Camp){const auto id=std::string("catalog-")+d.slug;const bool visible=ui.visible_for_test(id);check(visible==alpha::buildable(d.kind),"Wrong catalog availability");count+=visible;if(!visible){ui.activate_for_test(id);check(ui.take_actions().empty(),"Hidden construction still emits UI actions");}}
 check(count==11,"Catalog is not 11 types");ui.activate_for_test("catalog-close");ui.activate_for_test("stocks-toggle");draw("alpha-stocks");
 const auto stock=ui.element_rml_for_test("stocks-table");for(const char* key:{"alpha.materials","alpha.ingredients","alpha.meals","alpha.historical_items","alpha.carried"})check(stock.find(locale.text(key))!=std::string::npos,"Stock group missing");
 check(stock.find("data-item=\"3\"")!=std::string::npos,"Carried material absent");check(!ui.visible_for_test("future-resources-note"),"Future resources remain in stocks");
 SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;const auto pos=ui.element_center_for_test("stocks-table");move.motion.x=pos.x;move.motion.y=pos.y;ui.process_event(move);
 SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=-60;ui.process_event(wheel);ui.update(session);SDL_Delay(400);draw("alpha-stocks-scrolled");
 ui.activate_for_test("stocks-close");draw("alpha-workshop",{2});check(ui.activate_for_test("building-summary-details"),"Compact building details action missing");draw("alpha-workshop-details",{2});
 check(ui.select_option_rml_for_test("bp-recipe",0).find(locale.text("inspector.recipe.bricks"))!=std::string::npos,"Workshop does not offer bricks");
 check(ui.select_option_rml_for_test("bp-recipe",1).empty(),"Deferred workshop recipe visible");
 check(ui.visible_for_test("bp-upgrade")&&!ui.visible_for_test("bp-research"),"Useful upgrade hidden or research preview exposed");
 ui.activate_for_test("bp-research");check(!ui.visible_for_test("building-research"),"Hidden research action opened preview");
 draw("alpha-claypit",{3});check(ui.visible_for_test("bp-form"),"Clay pit unavailable");
 draw("alpha-old-research",{4});check(!ui.visible_for_test("bp-main")&&ui.element_rml_for_test("bp-info")==locale.text("alpha.old_building"),"Historical building promises future functions");
 ui.activate_for_test("bp-close");ui.take_actions();draw("alpha-resident");ui.activate_for_test("profession-open");draw("alpha-skills");
 for(int i=5;i<8;++i)check(!ui.visible_for_test("profession-skill-"+std::to_string(i)),"Inactive skill bonus visible");
 ui.activate_for_test("profession-close");ui.activate_for_test("settlement-open");draw("alpha-settlement");
 for(const char* id:{"settlement-discontent","settlement-causes","settlement-night","settlement-night-hint"})check(!ui.visible_for_test(id),"Deferred settlement control visible");
 std::ofstream(output/"report.txt")<<"Alpha native UI: 11 construction types, materials/ingredients/meals/cargo, Workshop/ClayPit, historical research, hidden placeholders and inactive skills passed. Captures: "<<captures<<"\n";
 }SDL_Quit();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

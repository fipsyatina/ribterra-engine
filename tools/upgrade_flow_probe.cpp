#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
using namespace ribterra;
namespace { void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);} }
int main(int argc,char**argv){try{
 check(argc>1,"Output directory required");const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());{
 const int width=argc>2?1100:1440,height=argc>2?700:1000;
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>window(SDL_CreateWindow("Ribterra upgrades",width,height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
 check(bool(window),SDL_GetError());const auto base=std::filesystem::path(SDL_GetBasePath());
 render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"settings.json")<<R"({"locale":"uk","ui_scale":1})";
 GameUi ui(renderer,assets,locale,output/"settings.json");GameView view(renderer);view.load_art(assets);render::Frame2D frame;
 check(ui.activate_for_test("new-game"),"New Game inaccessible");auto initial=ui.take_actions();check(initial.size()==1&&initial[0].type==UiActionType::NewGame,"New Game action missing");
 ClientSession session(70421,"Upgrade acceptance");ui.show_world();
 check(session.resources().wood==80&&session.resources().stone==40&&session.resources().food==100&&session.buildings().size()==1,"Not an ordinary new game");
 std::string error,status;int selected=0,captures=0;
 auto refresh=[&]{view.compose(session,0,frame,false);ui.set_view_state(&session,view.selected_ids(),false,1,WorldLayer::Surface,selected?std::vector<int>{selected}:std::vector<int>{});ui.update(session,status);ui.compose(frame);};
 auto draw=[&](const std::string&name,const char*focus=nullptr){refresh();if(focus){check(ui.focus_for_test(focus),"Cannot focus "+std::string(focus));ui.update(session,status);ui.compose(frame);}renderer.render(frame,output/(name+".png"));++captures;};
 auto pump=[&](auto predicate,const std::string&why,int limit=60000){for(int tick=0;tick<limit&&!predicate();++tick)session.advance();check(predicate(),why+" at tick "+std::to_string(session.ticks()));};
 auto click=[&](SDL_FPoint p,Uint8 button){const auto point=renderer.logical_to_window({p.x,p.y});for(auto type:{SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_EVENT_MOUSE_BUTTON_UP}){SDL_Event e{};e.type=type;e.button.button=button;e.button.x=point.x;e.button.y=point.y;renderer.convert_event(e);view.handle_event(e,session);}if(auto notice=view.take_status_text(locale))status=*notice;};
 auto key=[&](SDL_Keycode code,SDL_Keymod modifiers=SDL_KMOD_NONE){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;e.key.mod=modifiers;ui.process_event(e);e.type=SDL_EVENT_KEY_UP;e.key.mod=SDL_KMOD_NONE;ui.process_event(e);};
 auto actions=[&]{for(const auto&a:ui.take_actions()){
  CommandResult result{true,""};
  if(a.type==UiActionType::Build)view.arm_build(a.building);
  else if(a.type==UiActionType::BuildingProduction)result=session.production_group(a.buildings,a.recipe,a.quantity,a.residents.empty()?0:a.residents.front());
  else if(a.type==UiActionType::Upgrade)result=session.production(a.value,a.residents.empty()?0:a.residents.front(),a.recipe,a.quantity,a.settlement_revision,true);
  else if(a.type==UiActionType::CancelProduction)result=session.cancel_production(a.value,a.settlement_revision);
  else if(a.type==UiActionType::Save)check(session.save(output/"progress.save",error),error);
  else if(a.type==UiActionType::Load){check(session.load(output/"progress.save",error),error);ui.reset_world_state();view.reset_world_state();ui.show_world();}
  else throw std::runtime_error("Unexpected UI action");
  check(result.accepted,"UI command rejected: "+result.message);
 }};
 refresh();renderer.render(frame);
 // Gather through the ordinary map input; never import a prepared world or add items.
 SDL_Event all{};all.type=SDL_EVENT_KEY_DOWN;all.key.key=SDLK_A;view.handle_event(all,session);
 check(!view.selected_ids().empty(),"Select-all did not select gatherers");
 for(const auto [terrain,target]:{std::pair{Terrain::Forest,180},std::pair{Terrain::Rock,100}}){
  std::set<std::pair<int,int>> attempted;
  for(int attempt=0;attempt<100&&(terrain==Terrain::Forest?session.resources().wood:session.resources().stone)<target;++attempt){
   refresh();bool found=false;
   for(int radius=1;radius<=12&&!found;++radius)for(int y=24-radius;y<=24+radius&&!found;++y)for(int x=24-radius;x<=24+radius&&!found;++x){
    const auto&t=session.tile(x,y);const auto point=view.tile_screen(float(x),float(y));const auto picked=view.cell_at_screen(point.x,point.y);
    if(t.explored&&t.terrain==terrain&&t.amount>0&&!attempted.contains({x,y})&&picked.x==x&&picked.y==y){attempted.insert({x,y});click(point,SDL_BUTTON_RIGHT);found=true;}
   }
   check(found,"No nearby resource to gather");for(int i=0;i<5;++i)session.advance();
   pump([&]{return std::none_of(session.manual_orders().begin(),session.manual_orders().end(),[](const auto&o){return o.phase!=ManualOrderPhase::Complete&&o.phase!=ManualOrderPhase::Cancelled&&o.phase!=ManualOrderPhase::Failed;});},"Gather did not finish");
  }
  check((terrain==Terrain::Forest?session.resources().wood:session.resources().stone)>=target,"Gather failed to provide building supplies: wood="+std::to_string(session.resources().wood)+" stone="+std::to_string(session.resources().stone)+" selected="+std::to_string(view.selected_ids().size())+" notice="+status+" tick="+std::to_string(session.ticks()));
 }
 draw("01-gathered");
 auto build=[&](BuildingKind kind){
  selected=0;refresh();ui.activate_for_test("catalog-all");refresh();const auto button=std::string("catalog-")+building_definition(kind).slug;
  check(ui.visible_for_test(button)&&ui.activate_for_test(button),"Catalog action missing: "+button);actions();refresh();
  bool placed=false;int xsite=0,ysite=0;const auto before=session.buildings().size();
  for(int radius=4;radius<=9&&!placed;++radius)for(int y=24-radius;y<=24+radius&&!placed;++y)for(int x=24-radius;x<=24+radius&&!placed;++x){
   if(!session.can_build(kind,x,y)||std::any_of(session.buildings().begin(),session.buildings().end(),[&](const auto&b){return std::hypot(float(x-b.x),float(y-b.y))<3.5f;}))continue;
   const auto point=view.tile_screen(float(x),float(y));const auto picked=view.cell_at_screen(point.x,point.y);if(picked.x!=x||picked.y!=y)continue;
   click(point,SDL_BUTTON_LEFT);xsite=x;ysite=y;placed=true;
  }
  check(placed,"No build location");pump([&]{return session.buildings().size()>before;},"Build command did not create a site");
  int id=0;for(const auto&b:session.buildings())if(b.kind==kind&&b.x==xsite&&b.y==ysite)id=b.id;check(id>0,"Wrong placement");
  pump([&]{for(const auto&b:session.buildings())if(b.id==id)return b.progress==1;return false;},"Construction did not finish");return id;
 };
 const int sawmill=build(BuildingKind::Sawmill),clay=build(BuildingKind::ClayPit),workshop=build(BuildingKind::Workshop),house=build(BuildingKind::House);
 auto select=[&](int id){
  selected=0;refresh();bool hit=false;
  for(const auto&b:session.buildings())if(b.id==id){auto p=view.tile_screen(float(b.x),float(b.y));for(float dy:{-25.f,-45.f,-65.f,0.f}){click({p.x,p.y+dy},SDL_BUTTON_LEFT);if(!view.selected_building_ids().empty()&&view.selected_building_ids().back()==id){hit=true;break;}}break;}
   check(hit,"Map could not select building #"+std::to_string(id));selected=id;refresh();
   check(ui.activate_for_test("building-summary-details"),"Compact building details action missing");refresh();
  };
 auto last=[&]()->ProductionOrder{for(auto it=session.economy().orders.rbegin();it!=session.economy().orders.rend();++it)if(it->building_id==selected)return *it;throw std::runtime_error("No order for selection");};
 auto start_upgrade=[&]{refresh();check(ui.visible_for_test("bp-upgrade-start"),"Upgrade action hidden");check(ui.activate_for_test("bp-upgrade-start"),"Upgrade click failed");const int previous=session.economy().next_order_id;actions();pump([&]{return session.economy().next_order_id>previous;},"Upgrade not accepted");};
 select(house);check(!ui.visible_for_test("bp-form")&&ui.visible_for_test("bp-worker-controls"),"House exposes recipes or hides worker");
 draw("02-house-price","bp-upgrade-start");check(ui.element_rml_for_test("bp-upgrade-hint").find("Бракує")!=std::string::npos,"Missing materials not explained");
 start_upgrade();pump([&]{return last().reason=="materials";},"Upgrade did not wait for missing supplies");draw("03-missing-materials","bp-main");
 check(ui.activate_for_test("bp-main"),"Cancel unavailable");actions();pump([&]{return !production_active(last());},"Cancel did not release worker");
 auto produce=[&](int site,int recipe,int batches,const std::string&name){
  select(site);check(ui.focus_for_test("bp-recipe"),"Recipe control inaccessible");key(SDLK_HOME);
  for(int r=0;r<recipe;++r)if(alpha::recipe_available(r)&&recipes[r].building==int(std::find_if(session.buildings().begin(),session.buildings().end(),[&](const auto&b){return b.id==site;})->kind))key(SDLK_DOWN);
  key(SDLK_RETURN);refresh();check(ui.focus_for_test("bp-quantity"),"Quantity inaccessible");key(SDLK_A,SDL_KMOD_CTRL);const auto count=std::to_string(batches);SDL_Event input{};input.type=SDL_EVENT_TEXT_INPUT;input.text.text=count.c_str();ui.process_event(input);refresh();
  const int before=session.economy().next_order_id;check(ui.activate_for_test("bp-main"),"Production start unavailable");actions();pump([&]{return session.economy().next_order_id>before;},"Production not accepted");
  check(last().recipe==recipe&&last().requested==batches,"UI selected wrong recipe or quantity");
  int work=0;for(int i=0;i<60000&&production_active(last());++i){session.advance();work=std::max(work,last().work);}
  check(last().phase==ProductionPhase::Complete,"Production did not finish: "+last().reason);draw(name);return work;
 };
 produce(sawmill,3,2,"04-planks");produce(sawmill,10,1,"05-firewood");produce(clay,11,2,"06-clay");
 const int before_work=produce(workshop,8,2,"07-bricks");check(before_work==400,"Unexpected base brick work");
 check(session.economy().stored[3]==12&&session.economy().stored[5]==8,"Produced materials missing");
 select(workshop);draw("08-workshop-price","bp-upgrade-start");start_upgrade();pump([&]{return last().work>=100;},"Upgrade work not started");draw("09-workshop-progress","bp-main");
 const auto saved_order=last();const auto saved_economy=session.economy();
 check(ui.activate_for_test("save"),"Save button unavailable");actions();check(ui.activate_for_test("load"),"Load button unavailable");actions();
 check(session.economy()==saved_economy&&last()==saved_order,"Save/load changed paid materials or progress");
 refresh();pump([&]{return !production_active(last());},"Resumed upgrade did not finish");draw("10-workshop-II","bp-upgrade");
 check(!ui.visible_for_test("bp-upgrade-start")&&ui.element_rml_for_test("bp-level").find("II")!=std::string::npos,"Completed upgrade remains purchasable");
 const int after_work=produce(workshop,8,1,"11-faster-bricks");check(after_work*2==before_work,"Workshop II did not halve work phase");
 select(house);const int housing=session.housing();start_upgrade();pump([&]{return !production_active(last());},"House upgrade did not finish");draw("12-house-II","bp-upgrade");
 check(session.housing()==housing+4&&session.economy().upgrades.size()==2,"Both upgrade effects missing");
 check(session.economy().stored[3]==0&&session.economy().stored[5]==4,"Upgrades paid more than 12 planks and 8 bricks");
 check(!ui.visible_for_test("bp-upgrade-start"),"House upgrade can be bought twice");
 const int stockpile=build(BuildingKind::Stockpile);select(stockpile);draw("13-stockpile-no-upgrade");
 check(!ui.visible_for_test("bp-upgrade")&&!ui.visible_for_test("bp-upgrade-start"),"Stockpile II exposed without capacity evidence");
 // A current revision cannot buy a completed upgrade even through a direct command.
 const auto paid=session.economy();check(session.production(house,0,0,1,paid.revision,true).accepted,"Duplicate command transport failed");
 for(int i=0;i<3;++i)session.advance();check(session.economy().orders==paid.orders&&session.economy().upgrades==paid.upgrades&&session.economy().stored[3]==paid.stored[3]&&session.economy().stored[5]==paid.stored[5],"Duplicate upgrade changed inventory or orders");
 const auto final_economy=session.economy();
 check(ui.activate_for_test("save"),"Final save unavailable");actions();check(ui.activate_for_test("load"),"Final load unavailable");actions();check(session.economy()==final_economy,"Final save lost upgrades");
 std::ofstream(output/"report.txt")<<"Ordinary start, no imported world or added items. UI gather, five buildings, 12 planks, 6 firewood, 12 clay, 12 bricks; both upgrades cost 12 planks and 8 bricks total. Brick work "<<before_work<<" -> "<<after_work<<" ticks. House places +4. Missing resources, cancellation, work save/load, final save/load, duplicate rejection and hidden Stockpile II passed. Captures: "<<captures<<". Simulation ticks: "<<session.ticks()<<"\n";
 std::cout<<"Upgrade material chain and UI acceptance passed: "<<captures<<" captures\n";
 }SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

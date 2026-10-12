#include "ribterra/asset_catalog.hpp"
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
#include <set>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string&why){if(!ok)throw std::runtime_error(why);}
World fixture(int population){
 World w(70421,population);auto s=w.export_state();
 for(auto&c:s.chunks)if(c.key.layer==WorldLayer::Surface)for(std::size_t i=0;i<c.tiles.size();++i){
  int x=c.key.cx*chunk_size+int(i%chunk_size),y=c.key.cy*chunk_size+int(i/chunk_size);
  if(x>=18&&x<=36&&y>=18&&y<=36)c.tiles[i]={Terrain::Grass,0,true,Biome::Meadow,0};
 }
 for(int x:{29,33,35})s.buildings.push_back({s.next_building_id++,BuildingKind::Sawmill,x,29,1,WorldLayer::Surface,1});
 std::string error;check(w.import_state(s,error),error);return w;
}
}
int main(int argc,char**argv){try{
 check(argc>1,"Output path required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());{
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>window(SDL_CreateWindow("Ribterra 0.25.9 production",1440,1000,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
 check(bool(window),SDL_GetError());auto base=std::filesystem::path(SDL_GetBasePath());
 render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Software});AssetCatalog assets(base/"assets");Localization locale(base/"assets/locales");
 std::ofstream(output/"settings.json")<<R"({"locale":"uk","ui_scale":1})";GameUi ui(renderer,assets,locale,output/"settings.json");GameView view(renderer);view.load_art(assets);
 ClientSession session;render::Frame2D frame;std::string error;int cases=0;
 auto snapshot=[&](World&w){check(w.save(output/"scenario.save",error),error);check(session.load(output/"scenario.save",error),error);ui.reset_world_state();ui.show_world();};
 auto draw=[&](const std::string&name,int site=2){ui.set_view_state(&session,{},false,1,WorldLayer::Surface,{site});ui.update(session);view.compose(session,0,frame,false);ui.compose(frame);renderer.render(frame,output/(name+".png"));++cases;};
 auto key=[&](SDL_Keycode k,SDL_Keymod mod=SDL_KMOD_NONE){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=k;e.key.mod=mod;ui.process_event(e);e.type=SDL_EVENT_KEY_UP;ui.process_event(e);};
 auto set_batches=[&](const char*value){check(ui.focus_for_test("bp-quantity"),"Batch input cannot focus");key(SDLK_A,SDL_KMOD_CTRL);SDL_Event e{};e.type=SDL_EVENT_TEXT_INPUT;e.text.text=value;ui.process_event(e);ui.update(session);};
 auto pump=[&]{for(int i=0;i<20;++i)session.poll();};
 auto world=fixture(1);snapshot(world);draw("ready");set_batches("2");draw("two-batches");
 ui.activate_for_test("bp-main");auto actions=ui.take_actions();check(actions.size()==1&&actions.front().quantity==2&&actions.front().residents.empty(),"UI did not issue two batches with automatic worker");
 check(session.production_group(actions.front().buildings,actions.front().recipe,actions.front().quantity).accepted,"UI order rejected");pump();
 check(session.economy().orders.size()==1&&session.economy().orders.front().requested==2,"Server lost batch count");
 for(int n=0;n<2500&&session.economy().orders.front().phase!=ProductionPhase::Output;++n)session.advance();
 check(session.economy().orders.front().phase==ProductionPhase::Output,"Output not reached");draw("carrying-output");
 ui.activate_for_test("bp-main");actions=ui.take_actions();check(actions.size()==1&&actions.front().type==UiActionType::CancelProduction,"Stop did not issue cancellation");
 check(session.cancel_production(actions.front().value,actions.front().settlement_revision).accepted,"Carried cancellation rejected");pump();draw("finishing-delivery");
 check(ui.element_rml_for_test("bp-status")==locale.text("inspector.state.finishing_delivery"),"Carried cancellation not explained");
 ui.activate_for_test("bp-main");check(ui.take_actions().empty(),"Pending cancellation button is still active");
 check(session.save(output/"cancelling.save",error),error);check(session.load(output/"cancelling.save",error),error);
 check(session.economy().orders.front().cancel_requested,"Load lost pending cancellation");
 for(int n=0;n<2500&&production_active(session.economy().orders.front());++n)session.advance();
 check(session.economy().orders.front().phase==ProductionPhase::Cancelled,"Cancellation after reload stuck");draw("cancelled-after-reload");
 check(session.economy().stored[int(ItemKind::Planks)]==6,"Cancellation lost delivered batch");
 // Explicitly release a farmer through the existing manager, preserving the profession.
 world=fixture(1);ProfessionAssignment farmer;farmer.profession=Profession::Farmer;farmer.worksite=CellCoord{29,29};check(world.assign_profession({1},farmer).front().status==ResidentCommandStatus::Applied,"Farmer fixture assignment failed");check(world.residents().front().profession.assignment.profession==Profession::Farmer,"Farmer fixture lost assignment");snapshot(world);draw("farmer-automatic");
 check(!session.production_worker_reason(1,true).empty(),"Automatic choice stole farmer; profession="+std::to_string(int(session.resident_professions().front().assignment.profession)));
 check(ui.focus_for_test("bp-worker"),"Worker selector cannot focus");key(SDLK_DOWN);draw("farmer-busy");check(ui.focus_for_test("bp-worker-manage"),"Worker management button inaccessible");ui.update(session);view.compose(session,0,frame,false);ui.compose(frame);renderer.render(frame,output/"farmer-busy-details.png");
 check(ui.element_rml_for_test("bp-worker-detail").find(locale.text("economy.assigned"))!=std::string::npos,"Busy farmer has no explanation");
 ui.activate_for_test("bp-main");check(ui.take_actions().empty(),"Busy farmer can start");
 ui.activate_for_test("bp-worker-manage");actions=ui.take_actions();check(actions.size()==1&&actions.front().type==UiActionType::ManageProductionWorker&&actions.front().value==1,"Worker management route missing");
 view.focus_group(session,{1});ui.set_view_state(&session,{1},false,1,WorldLayer::Surface,{2});ui.update(session);ui.show_profession();ui.update(session);view.compose(session,0,frame,false);ui.compose(frame);renderer.render(frame,output/"farmer-management.png");
 check(ui.visible_for_test("profession-panel"),"Profession manager hidden");ui.activate_for_test("profession-stop");actions=ui.take_actions();
 check(actions.size()==1&&actions.front().type==UiActionType::Stop,"Manager must explicitly stop farmer");check(session.stop(actions.front().residents).accepted,"Explicit stop rejected");pump();ui.activate_for_test("profession-close");ui.take_actions();draw("farmer-released");
 check(session.production_worker_reason(1).empty(),"Stopped farmer cannot be explicitly chosen");check(!session.production_worker_reason(1,true).empty(),"Automatic choice should still skip retained profession");
 set_batches("2");ui.activate_for_test("bp-main");actions=ui.take_actions();check(actions.size()==1&&actions.front().residents==std::vector<int>{1},"Chosen worker lost");
 check(session.production_group({2},3,2,1).accepted,"Chosen worker rejected");pump();check(session.resident_professions().front().assignment.profession==Profession::Farmer,"Production erased profession");draw("farmer-production");
 // Deduplicate sites and reserve each worker at most once across the entire group.
 world=fixture(2);snapshot(world);check(session.production_group({2,3,4,2},3,2).accepted,"Group rejected");pump();draw("group-partial");
 check(session.production_results().size()==3&&session.economy().orders.size()==2,"Group skipped or duplicated wrong sites");
 std::set<int>workers;for(const auto&o:session.economy().orders)workers.insert(o.resident_id);check(workers.size()==2,"Group reused a worker");
 check(ui.element_rml_for_test("bp-group-feedback").find(locale.text("production.skipped"))!=std::string::npos,"Group skips invisible");
 world=fixture(2);snapshot(world);check(session.production_group({2,3},3,1,1).accepted,"Explicit group rejected");pump();draw("group-worker-used");
 check(session.economy().orders.size()==1&&session.production_results()[1].reason=="production.worker.used","Explicit group reused worker");
 draw("selection-reset",3);ui.activate_for_test("bp-main");actions=ui.take_actions();check(actions.size()==1&&actions.front().residents.empty()&&actions.front().quantity==1,"Selection retained stale worker or batches");
 check(SDL_SetWindowSize(window.get(),1100,700),SDL_GetError());SDL_PumpEvents();draw("compact");check(ui.focus_for_test("bp-main"),"Compact action cannot focus");ui.update(session);ui.compose(frame);renderer.render(frame,output/"compact-action.png");
 auto point=renderer.logical_to_window(ui.element_center_for_test("bp-main"));check(point.y>0&&point.y<700,"Compact action offscreen");
 std::ofstream(output/"report.txt")<<cases<<" native UI scenarios passed: batches, chosen workers, explicit farmer stop, carried cancellation and reload, group acceptance and skips, distinct workers, selection refresh, compact scrolling.\n";
 }SDL_Quit();return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}

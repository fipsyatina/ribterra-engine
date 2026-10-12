#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/seasons.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string &message) { if(!ok) throw std::runtime_error(message); }
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
}
}
int main(int argc,char **argv) {
    try {
        const auto args=utf8_arguments(argc,argv);
        check(args.size()==2,"Expected output directory");
        const auto output=std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        {
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra directional residents verification",1440,900,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
            check(window!=nullptr,SDL_GetError());
            const auto base=path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Direct3D12});
            AssetCatalog assets(base/"assets");
            GameArt art(renderer);art.load(assets);
            // Actual engine texture loading, blending and UVs, for all 256 frames.
            for(int sex=0;sex<2;++sex) for(int phase=0;phase<4;++phase) {
                render::Frame2D frame;frame.clear={39,48,39,255};
                const char *directions[]{"E","SE","S","SW","W","NW","N","NE"};
                const char *loads[]{"Walking","Firewood","Sack","Handcart"};
                for(int col=0;col<8;++col) {
                    frame.commands.emplace_back(render::TextDraw{render::Pass::Ui,directions[col],{74.f+col*172,30},22});
                    for(int row=0;row<4;++row) {
                        const auto load=static_cast<ResidentLoad>(row);
                        const auto sprite=resident_sprite(sex+1,load,col,phase);
                        check(art.contains(sprite),"Missing directional sprite: "+sprite);
                        art.sprite(frame,sprite,{90.f+col*172,225.f+row*200},resident_sprite_width(load)*5,
                                   render::Pass::Ui);
                        frame.commands.emplace_back(render::TextDraw{render::Pass::Ui,loads[row],{45.f+col*172,245.f+row*200},16});
                    }
                }
                renderer.render(frame,output/((sex?"woman-":"man-")+std::to_string(phase)+".png"));
            }
            nlohmann::json deliveries=nlohmann::json::array();
            for(bool stockpile:{false,true}) {
                World source(70421,6);auto state=source.export_state();
                state.ticks=ticks_per_day/4;
                const auto camp=*std::find_if(state.buildings.begin(),state.buildings.end(),[](const auto &b){return b.kind==BuildingKind::Camp;});
                int destination=camp.id;
                if(stockpile) {
                    // A full camp forces these deliveries to the new stockpile.
                    for(auto &storage:state.storages) if(storage.building_id==camp.id) {
                        storage.capacity=0;
                        for(const auto &stack:state.item_stacks)
                            if(stack.location.kind==ItemLocationKind::Storage && stack.location.entity_id==camp.id)
                                storage.capacity+=stack.quantity;
                    }
                    destination=state.next_building_id++;
                    state.buildings.push_back({destination,BuildingKind::Stockpile,29,24,1,WorldLayer::Surface,1});
                    state.storages.push_back({destination,1000,7,10});
                }
                for(auto &chunk:state.chunks) if(chunk.key.layer==WorldLayer::Surface)
                    for(std::size_t i=0;i<chunk.tiles.size();++i) {
                        const int x=chunk.key.cx*chunk_size+int(i%chunk_size),y=chunk.key.cy*chunk_size+int(i/chunk_size);
                        if(x>=14 && x<=34 && y>=18 && y<=29) chunk.tiles[i]={Terrain::Grass,0,true,Biome::Meadow,0};
                    }
                const ItemKind items[]{ItemKind::Wood,ItemKind::Food,ItemKind::Stone};
                for(int i=0;i<6;++i) {
                    auto &r=state.residents[i];r.x=float(16+i);r.y=21;r.target_x=16+i;r.target_y=21;
                    state.item_stacks.push_back({state.next_item_stack_id++,items[i%3],10,1,
                                                {ItemLocationKind::Ground,{16+i,22,WorldLayer::Surface},0}});
                }
                state.resources.wood+=20;state.resources.stone+=20;state.resources.food+=20;
                std::string error;
                World world(70421,0);check(world.import_state(std::move(state),error),"Hauling fixture: "+error);
                const std::string name=stockpile?"stockpile":"camp";
                const auto initial=output/(name+"-initial.save");check(world.save(initial,error),error);
                ClientSession session;check(session.load(initial,error),error);
                GameView view(renderer);view.load_art(assets);
                SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=12;view.handle_event(e,session);
                view.focus_group(session,{3});
                std::set<ResidentLoad> seen;
                bool captured=false,complete=false;int completed_tick=0;
                int carried_frames=0;
                double presentation_time=0;
                for(int tick=0;tick<1600;++tick) {
                    session.advance();
                    const auto &inv=session.inventory();
                    check(inv.total.wood==100 && inv.total.stone==60,"Cargo lost or duplicated");
                    check(inv.stored.wood+inv.carried.wood<=100 && inv.available.wood+inv.reserved.wood==100,
                          "Wood storage/carry conservation failed");
                    for(const auto &cargo:session.resident_logistics()) if(cargo.quantity) {
                        check(cargo.quantity<=resident_carry_capacity,"Visual cart changed carrying capacity");
                        seen.insert(resident_load(cargo.item,cargo.quantity));
                    }
                    render::Frame2D frame;
                    for(int sub=0;sub<3;++sub) {
                        presentation_time+=1./60;
                        view.compose(session,presentation_time,frame,false);
                    }
                    if(seen.size()==3 && inv.carried.wood && inv.carried.stone && inv.carried.food) {
                        ++carried_frames;
                        if(carried_frames==25) {
                            renderer.render(frame,output/(name+"-carrying.png"));captured=true;
                            check(session.save(output/(name+"-carrying.save"),error),error);
                            ClientSession restored;check(restored.load(output/(name+"-carrying.save"),error),error);
                            check(restored.resident_logistics()==session.resident_logistics(),"Cargo presentation changed on reload");
                        }
                    }
                    // Residents consume meals every 200 ticks while this real simulation runs.
                    if(inv.stored.wood==100 && inv.stored.stone==60 && inv.stored.food==inv.total.food &&
                       inv.carried.wood==0 && inv.carried.stone==0 && inv.carried.food==0) {
                        renderer.render(frame,output/(name+"-delivered.png"));
                        check(session.save(output/(name+"-delivered.save"),error),error);
                        World delivered(1,0);check(delivered.load(output/(name+"-delivered.save"),error),error);
                        for(const auto item:{ItemKind::Wood,ItemKind::Stone,ItemKind::Food}) {
                            unsigned deposited=0;
                            for(const auto &s:delivered.item_stacks())
                                if(s.item==item && s.location.kind==ItemLocationKind::Storage && s.location.entity_id==destination) deposited+=s.quantity;
                            check(deposited>=20,"Cargo did not arrive at requested storage building");
                        }
                        complete=true;completed_tick=tick;break;
                    }
                }
                const auto &last=session.inventory();
                check(captured && complete && seen.size()==3,"Real hauling incomplete: "+name+
                    " captured="+std::to_string(captured)+" stored="+std::to_string(last.stored.wood)+","+
                    std::to_string(last.stored.stone)+","+std::to_string(last.stored.food)+" carried="+
                    std::to_string(last.carried.wood)+","+std::to_string(last.carried.stone)+","+std::to_string(last.carried.food));
                deliveries.push_back({{"destination",name},{"completed_tick",completed_tick},{"actual_cargo",true},{"save_resume",true}});
            }
            const auto &device=renderer.device_info();
            std::ofstream(output/"report.json")<<nlohmann::json{{"backend",device.backend},{"device",device.device},
                {"content_sha256",assets.content_hash()},
                {"sprite_frames",256},{"deliveries",deliveries}}.dump(2);
        }
        SDL_Quit();std::cout<<"256 frames and actual camp/stockpile deliveries passed.\n";return 0;
    } catch(const std::exception &e) {SDL_Quit();std::cerr<<e.what()<<'\n';return 1;}
}

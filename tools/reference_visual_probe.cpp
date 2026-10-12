#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
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
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool ok,const std::string &error) { if(!ok) throw std::runtime_error(error); }
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
}
}
int main(int argc,char **argv) {
    try {
        const auto args=utf8_arguments(argc,argv);
        check(args.size()>1,"Output directory required");
        const auto output=std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        {
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra reference artwork verification",1440,900,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
            check(window!=nullptr,SDL_GetError());
            const auto base=path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(),base/"assets",{render::Backend::Direct3D12});
            AssetCatalog assets(base/"assets");
            Localization localization(base/"assets/locales");
            std::ofstream(output/"settings.json")<<R"({"locale":"en","ui_scale":1})";
            GameUi ui(renderer,assets,localization,output/"settings.json");
            ui.show_world();
            World source(70421,18);
            auto arranged=source.export_state();
            // Deliberately arranged art fixture, not a world-generation benchmark.
            auto add=[&](BuildingKind kind,int x,int y) {
                if(std::none_of(arranged.buildings.begin(),arranged.buildings.end(),[&](const auto &b){return b.x==x && b.y==y;})) {
                    arranged.buildings.push_back({arranged.next_building_id++,kind,x,y,1,WorldLayer::Surface,1});
                    if(kind==BuildingKind::Stockpile) arranged.storages.push_back({arranged.buildings.back().id,1000,7,0});
                }
            };
            add(BuildingKind::House,21,23); add(BuildingKind::House,25,21); add(BuildingKind::House,27,24);
            add(BuildingKind::Kitchen,21,26); add(BuildingKind::Tavern,24,26);
            add(BuildingKind::Research,28,22); add(BuildingKind::Temple,23,18);
            add(BuildingKind::Mill,28,27); add(BuildingKind::Fishing,21,27);
            add(BuildingKind::Workshop,18,23); add(BuildingKind::Stockpile,18,26); add(BuildingKind::Farm,19,20);
            for(int x=16;x<=32;++x) add(BuildingKind::Road,x,25);
            for(int y=17;y<=27;++y) add(BuildingKind::Road,23,y);
            for(int y=28;y<=30;++y) add(BuildingKind::Bridge,23,y);
            for(auto &chunk:arranged.chunks) if(chunk.key.layer==WorldLayer::Surface)
                for(std::size_t i=0;i<chunk.tiles.size();++i) {
                    const int x=chunk.key.cx*chunk_size+int(i%chunk_size),y=chunk.key.cy*chunk_size+int(i/chunk_size);
                    if(x<9 || x>40 || y<9 || y>40) continue;
                    auto &t=chunk.tiles[i];t={Terrain::Grass,0,true,Biome::Meadow,0};
                    if(y>=28 && y<=30) t={Terrain::Water,0,true,Biome::Lake,-4};
                    else {
                        const bool near=std::any_of(arranged.buildings.begin(),arranged.buildings.end(),[&](const auto &b){return std::abs(b.x-x)<=1 && std::abs(b.y-y)<=1;});
                        if(!near && (x*31+y*17)%5<2) t={Terrain::Forest,50,true,Biome::Forest,0};
                        else if(!near && (x*17+y*13)%17==0) t={Terrain::Rock,50,true,Biome::Meadow,0};
                    }
                }
            for(std::size_t i=0;i<arranged.residents.size();++i) {
                auto &r=arranged.residents[i];r.x=17.f+float(i%15);r.y=25.1f+float(i%3)*.2f;
                r.target_x=int(r.x);r.target_y=int(r.y);
            }
            std::string error;
            for(int scene=0;scene<3;++scene) {
                auto state=arranged;
                state.ticks=(scene==1?3*days_per_season*ticks_per_day:0)+(scene==2?3*ticks_per_day/4:ticks_per_day/4);
                World world(70421,0);check(world.import_state(std::move(state),error),error);
                const std::string name=scene==0?"village-day":scene==1?"village-winter":"village-night";
                const auto save=output/(name+".save");check(world.save(save,error),error);
                ClientSession session;check(session.load(save,error),error);
                GameView view(renderer);view.load_art(assets);
                SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=12;view.handle_event(e,session);
                render::Frame2D frame;
                for(int f=0;f<60;++f) view.compose(session,1./60,frame,false);
                ui.set_view_state(&session,{},false,1,WorldLayer::Surface);ui.update(&session,"");ui.compose(frame);
                renderer.render(frame,output/(name+".png"));
                if(scene==2) {
                    for(int f=0;f<120;++f) view.compose(session,1./60,frame,false);
                    ui.compose(frame);renderer.render(frame,output/"village-night-motion.png");
                }
            }
            const auto &device=renderer.device_info();
            std::ofstream(output/"report.json")<<nlohmann::json{{"fixture","arranged art showcase"},{"backend",device.backend},{"device",device.device},{"day_winter_night",true},{"zoom",2.38},{"effects_seconds",nlohmann::json::array({1,3})}}.dump(2);
        }
        SDL_Quit();return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
}

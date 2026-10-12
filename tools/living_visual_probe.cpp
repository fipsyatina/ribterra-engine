#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

int main(int argc, char **argv) {
    try {
        using namespace ribterra;
        const auto args=utf8_arguments(argc,argv);
        if(args.size()<2) return 2;
        const auto output=std::filesystem::absolute(args[1]);
        std::filesystem::create_directories(output);
        if(!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        {
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra living world verification",1440,900,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
            if(!window) throw std::runtime_error(SDL_GetError());
            const auto base=std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(),base/"assets",
                {args.size()>2 ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base/"assets");
            GameArt art(renderer); art.load(assets);
            render::Frame2D frame;
            for(int phase=0;phase<4;++phase) {
                frame.clear_commands();
                for(int i=0;i<6;++i)
                    art.tile(frame,"grass.018."+std::to_string(i),{float(120+(i%3)*190),float(620+(i/3)*170)},85,65);
                for(int variant=0;variant<4;++variant)
                    for(int direction=0;direction<2;++direction)
                        art.sprite(frame,"walk."+std::to_string(variant)+"."+std::to_string(direction)+"."+std::to_string(phase),
                            {float(110+variant*150),float(220+direction*230)},100);
                for(int i=0;i<12;++i)
                    art.sprite(frame,"trees.018."+std::to_string(i),{float(750+(i%4)*170),float(240+(i/4)*260)},125);
                renderer.render(frame,output/("gallery-"+std::to_string(phase)+".png"));
            }
            World world(70421,12);
            auto state=world.export_state();
            for(auto &chunk:state.chunks)
                if(chunk.key.layer==WorldLayer::Surface)
                    for(auto &tile:chunk.tiles) tile.explored=true;
            for(auto &chunk:state.chunks)
                if(chunk.key==chunk_key({24,24,WorldLayer::Surface}))
                    for(int y=20;y<30;++y) for(int x=20;x<32;++x)
                        chunk.tiles[local_index(x,y)]={Terrain::Grass,0,true};
            std::string error;
            if(!world.import_state(std::move(state),error) || !world.save(output/"walk.save",error))
                throw std::runtime_error(error);
            ClientSession session;
            if(!session.load(output/"walk.save",error)) throw std::runtime_error(error);
            GameView view(renderer); view.load_art(assets);
            const auto settings=output/"ui.json";
            std::ofstream(settings)<<R"({"locale":"uk","ui_scale":1})";
            Localization locale(base/"assets/locales");
            GameUi ui(renderer,assets,locale,settings); ui.show_world();
            std::vector<int> ids;
            for(auto &u:session.residents()) ids.push_back(u.id);
            if(!session.move(ids,30,24).accepted) throw std::runtime_error("Walk command rejected");
            const auto initial=session.residents();
            for(int step=0;step<48;++step) {
                session.advance();
                view.compose(session,session.ticks()*tick_seconds,frame,false);
                ui.set_view_state(&session,{ids.front()},false,1);
                ui.update(&session,""); ui.compose(frame);
                if(step%4==0) renderer.render(frame,output/("walking-"+std::to_string(step/4)+".png"));
            }
            bool moved=false;
            for(size_t i=0;i<initial.size();++i)
                moved |= initial[i].x!=session.residents()[i].x || initial[i].y!=session.residents()[i].y;
            if(!moved) throw std::runtime_error("Residents did not move");
            if(ui.element_rml_for_test("resident-hunger")!=locale.format("hud.hunger",{{"count",10}}))
                throw std::runtime_error("Fed resident did not show satiety 10/10");
            bool finished=false;
            for(int i=0;i<200&&!finished;++i){session.advance();finished=true;for(const auto&r:session.residents())finished&=r.job==Job::Idle&&r.x==30&&r.y==24;}
            if(!finished)throw std::runtime_error("Player move did not finish");
            for(int i=0;i<200;++i)session.advance();
            for(const auto&r:session.residents())if(r.x!=30||r.y!=24||r.job!=Job::Idle)throw std::runtime_error("Autonomy interrupted the player grace period");
            view.compose(session,session.ticks()*tick_seconds,frame,false);
            ui.set_view_state(&session,{ids.front()},false,1);ui.update(&session,"");ui.compose(frame);
            if(ui.element_rml_for_test("resident-order")!=locale.text("profession.await_command"))throw std::runtime_error("Waiting status is missing from the resident card");
            renderer.render(frame,output/"command-wait.png");
            std::ofstream(output/"verification.json")<<nlohmann::json{
                {"variants",4},{"walk_frames",32},{"trees",12},{"grass",6},{"satiety",10},
                {"moved",moved},{"backend",renderer.device_info().backend},
                {"software",renderer.device_info().software}}.dump(2);
        }
        SDL_Quit(); return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; SDL_Quit(); return 1; }
}

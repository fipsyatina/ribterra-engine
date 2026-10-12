#include "ribterra/seasons.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/network_session.hpp"
#include <chrono>
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        require(args.size() >= 2, "Output directory required");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        const bool software = args.size() > 2 && args[2] == "--software";
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            using namespace ribterra;
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra construction verification", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base / "assets");
            // The lifecycle is exercised through real construction in BuildingTests. This fixture
            // arranges the finished buildings with room to inspect every silhouette in the real renderer.
            World world(70421, 10);
            auto state = world.export_state();
            for (auto &chunk : state.chunks)
                if (chunk.key.layer == WorldLayer::Surface)
                    for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                        const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                        const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                        if (x >= 15 && x <= 36 && y >= 12 && y <= 35)
                            chunk.tiles[i] = {Terrain::Grass, 0, true, Biome::Meadow, 0};
                        if (x >= 33 && x <= 36 && y >= 20 && y <= 29)
                            chunk.tiles[i] = {Terrain::Water, 0, true, Biome::Lake, -6};
                    }
            constexpr std::array<std::pair<int, int>, 16> sites{{{21, 16},
                                                                 {26, 16},
                                                                 {31, 16},
                                                                 {21, 21},
                                                                 {26, 21},
                                                                 {31, 21},
                                                                 {21, 26},
                                                                 {32, 26},
                                                                 {26, 26},
                                                                 {26, 31}, {33,26}, {16,16}, {16,21}, {16,26}, {16,31}, {31,31}}};
            static_assert(sites.size()==building_kind_count-4);
            for (unsigned i = 4; i < building_kind_count; ++i) {
                const auto [x, y] = sites[i - 4];
                state.buildings.push_back({state.next_building_id++, static_cast<BuildingKind>(i), x, y, 1,
                                           WorldLayer::Surface, 1});
                if (i == static_cast<unsigned>(BuildingKind::Mine)) {
                    state.portals.push_back({{x, y, WorldLayer::Surface}, {x, y, WorldLayer::Mine}});
                    for (auto &chunk : state.chunks)
                        if (chunk.key == chunk_key({x, y, WorldLayer::Mine}))
                            chunk.tiles[local_index(x, y)] = {Terrain::MineFloor, 0, true};
                }
            }
            std::string error;
            for(int k=0;k<4;++k)if(std::none_of(state.buildings.begin(),state.buildings.end(),[&](const auto&b){return int(b.kind)==k;})) {
                const int id=state.next_building_id++;state.buildings.push_back({id,BuildingKind(k),18+k*3,33,1,WorldLayer::Surface,1});
                if(k==3)state.storages.push_back({id,1000,7,0});
            }
            state.buildings.push_back({state.next_building_id++,BuildingKind::Farm,18,30,1,WorldLayer::Surface,1});
            require(world.import_state(std::move(state), error), error);
            const auto settings = output / "building-ui-settings.json";
            std::ofstream(settings) << R"({"locale":"en","ui_scale":1})";
            Localization localization(base / "assets/locales");
            GameUi ui(renderer, assets, localization, settings);
            ui.show_world();
            const auto save=output/"panels.save";
            require(world.save(save,error),error);
            ClientSession session; require(session.load(save,error),error);
            GameView view(renderer);view.load_art(assets);
            render::Frame2D frame;
            int captured=0;
            for(const auto &definition:building_catalog){
                auto found=std::find_if(session.buildings().begin(),session.buildings().end(),[&](const auto&b){return b.kind==definition.kind;});
                require(found!=session.buildings().end(),"Missing building fixture");
                view.compose(session,0,frame,false);
                ui.set_view_state(&session,{},false,1,WorldLayer::Surface,{found->id});
                ui.update(&session,"");ui.compose(frame);
                require(ui.visible_for_test("building-panel"),"Building panel is hidden");
                const auto close=ui.element_center_for_test("bp-close");
                require(close.x>700&&close.x<1440&&close.y>0&&close.y<900,"Panel close is offscreen");
                renderer.render(frame,output/(std::string(definition.slug)+".png"));++captured;
                if(definition.kind==BuildingKind::Research){
                    require(ui.activate_for_test("bp-main"),"Research button");
                    ui.update(&session,"");view.compose(session,0,frame,false);ui.compose(frame);
                    require(ui.visible_for_test("building-research"),"Research chooser hidden");
                    require(ui.activate_for_test("br-card-15"),"Technology free choice");
                    require(ui.element_rml_for_test("br-name")=="Sawmill II","Wrong selected technology");
                    ui.update(&session,"");view.compose(session,0,frame,false);ui.compose(frame);
                    renderer.render(frame,output/"choose-research.png");ui.activate_for_test("br-close");
                }
                if(definition.kind==BuildingKind::Workshop){
                    ui.activate_for_test("bp-main");auto actions=ui.take_actions();
                    require(actions.size()==1&&actions[0].type==UiActionType::BuildingProduction&&actions[0].buildings==std::vector<int>{found->id},"Production intent does not identify selected building");
                }
            }
            // Exercise real world input rather than only injecting selected IDs into the inspector.
            const auto house=std::find_if(session.buildings().begin(),session.buildings().end(),[](const auto&b){return b.kind==BuildingKind::House;});
            view.compose(session,0,frame,false);
            const auto point=view.tile_screen(float(house->x),float(house->y));
            SDL_Event click{};click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.button=SDL_BUTTON_LEFT;click.button.x=point.x;click.button.y=point.y-22;
            view.handle_event(click,session);
            require(!view.selected_building_ids().empty(),"Map click did not select a building");
            ui.set_view_state(&session,{},false,1,WorldLayer::Surface,view.selected_building_ids());ui.update(&session,"");
            ui.activate_for_test("bp-close");auto closed=ui.take_actions();
            require(closed.size()==1&&closed[0].type==UiActionType::ClearBuildingSelection,"Close should clear building selection");
            view.clear_building_selection();ui.set_view_state(&session,{},false,1,WorldLayer::Surface,view.selected_building_ids());
            ui.update(&session,"");require(!ui.visible_for_test("building-panel"),"Close reopened inspector");
            // Group commands use distinct workers and current server revisions, including paused worlds.
            std::vector<int> farms;for(const auto&b:session.buildings())if(b.kind==BuildingKind::Farm)farms.push_back(b.id);
            ui.set_view_state(&session,{},false,1,WorldLayer::Surface,{farms.front()});ui.update(&session,"");
            require(ui.focus_for_test("bp-scope"),"Cannot focus scope selector");
            SDL_Event down{};down.type=SDL_EVENT_KEY_DOWN;down.key.key=SDLK_DOWN;
            ui.process_event(down);ui.process_event(down);ui.update(&session,"");
            ui.activate_for_test("bp-main");auto grouped=ui.take_actions();
            require(grouped.size()==1&&grouped[0].type==UiActionType::BuildingProduction&&grouped[0].buildings.size()==2,"All-of-type selector did not address both farms");
            require(session.production_group(grouped[0].buildings,grouped[0].recipe,grouped[0].quantity).accepted,"Group request rejected");
            for(int i=0;i<20;++i)session.poll();
            require(session.economy().orders.size()==2,"Two farms must receive two authoritative orders");
            require(session.economy().orders[0].resident_id!=session.economy().orders[1].resident_id,"Group orders must use distinct workers");
            auto farm=std::find_if(session.buildings().begin(),session.buildings().end(),[](const auto&b){return b.kind==BuildingKind::Farm;});
            ui.set_view_state(&session,{},false,1,WorldLayer::Surface,{farm->id});ui.update(&session,"");view.compose(session,0,frame,false);ui.compose(frame);
            renderer.render(frame,output/"farm-active-order.png");
            require(session.save(output/"orders.save",error),error);
            ClientSession resumed;require(resumed.load(output/"orders.save",error),error);
            require(resumed.economy().orders.size()==2,"Group orders did not survive save/load");
            // Two real network clients share a fresh fixture. No simulation ticks are needed to issue orders.
            net::HostConfig host;host.transport.port=29913;host.save_path=save;
            net::NetworkServer server(host);require(server.listen(),server.last_error());
            ClientSession online(70421,"Panel host"),peer(70421,"Panel peer");
            const auto endpoint="127.0.0.1:"+std::to_string(server.port());
            require(online.connect_remote(endpoint,"",error),error);require(peer.connect_remote(endpoint,"",error),error);
            const auto pump=[&](auto predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(std::chrono::steady_clock::now()<deadline){server.poll();online.poll();peer.poll();if(predicate())return;SDL_Delay(2);}throw std::runtime_error("Network group verification timed out: "+online.production_group_status());};
            pump([&]{return online.ready()&&peer.ready();});
            farms.push_back(farms.front()); // Repeated IDs must not produce duplicate requests.
            require(online.production_group(farms,0,1).accepted,"Network group request rejected");
            require(!online.production_group(farms,0,1).accepted,"Overlapping batch accepted");
            pump([&]{return online.economy().orders.size()==2&&peer.economy().orders.size()==2&&online.production_group_status().find("pending")==std::string::npos;});
            require(online.economy().orders[0].resident_id!=online.economy().orders[1].resident_id,"Online group reused a worker");
            server.shutdown();
            require(SDL_SetWindowSize(window.get(),1100,700),SDL_GetError());SDL_PumpEvents();
            renderer.render(frame,output/"panel-compact.png");
            const auto &device = renderer.device_info();
            std::ofstream(output / "building-render-report.json")
                << nlohmann::json{{"building_panels", captured},{"network_clients",2},{"group_orders",2},{"save_load",true},
                                  {"research_chooser", true},
                                  {"building_thumbnails", 20},
                                  {"scene", "spring fixture"},
                                  {"map_click_and_close", true},
                                  {"compact_capture", true},
                                  {"backend", device.backend},
                                  {"device", device.device},
                                  {"software", device.software}}
                       .dump(2);
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

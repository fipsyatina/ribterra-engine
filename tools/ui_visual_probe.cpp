#include "ribterra/world_state.hpp"
#include "ribterra/seasons.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        if (args.size() < 2 || args.size() > 3)
            return 2;
        const bool software = args.size() == 3 && args[2] == "--software";
        if (args.size() == 3 && !software)
            return 2;
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra UI visual probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            if (!window)
                throw std::runtime_error(SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            ribterra::render::Renderer2D renderer(
                window.get(), base / "assets",
                {software ? ribterra::render::Backend::Software : ribterra::render::Backend::Direct3D12});
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            ribterra::AssetCatalog assets(base / "assets");
            ribterra::ClientSession session;
            ribterra::GameView view(renderer);
            view.load_art(assets);
            for (const auto &locale : ribterra::Localization::supported_locales()) {
                const float scale = locale == "en" ? 1.f : 1.3f;
                const auto settings = output / (locale + "-settings.json");
                {
                    std::ofstream file(settings);
                    file << nlohmann::json{{"locale", locale}, {"ui_scale", scale}};
                }
                ribterra::Localization localization(base / "assets/locales");
                ribterra::GameUi ui(renderer, assets, localization, settings);
                ribterra::render::Frame2D frame;
                ui.update(nullptr);
                ui.compose(frame);
                renderer.render(frame, output / (locale + "-menu.png"));
                ui.activate_for_test("open-settings");
                ui.update(nullptr);
                frame.clear_commands();
                ui.compose(frame);
                renderer.render(frame, output / (locale + "-settings.png"));
                if (!ui.activate_for_test("settings-back") || !ui.activate_for_test("open-help"))
                    throw std::runtime_error("Help navigation is unavailable: " + locale);
                ui.update(nullptr);
                frame.clear_commands();
                ui.compose(frame);
                renderer.render(frame, output / (locale + "-help.png"));
                ui.show_world();
                ui.set_view_state(&session, {session.residents().front().id}, locale == "uk",
                                  locale == "uk" ? 2 : 1);
                ui.update(session, localization.text("status.layer_surface"));
                for (const auto *control :
                     {"build-more", "stocks-toggle", "pause", "save", "load", "hud-help",
                      "hud-menu", "surface-tab", "mine-tab", "use-passage"}) {
                    const auto center = ui.element_center_for_test(control);
                    if (center.x <= 0 || center.x >= 1440 || center.y <= 0 || center.y >= 900)
                        throw std::runtime_error(std::string("UI control outside viewport: ") + locale + ":" +
                                                 control);
                }
                view.compose(session, 0, frame, false);
                ui.compose(frame);
                renderer.render(frame, output / (locale + "-hud.png"));
                if(ui.visible_for_test("chat")) throw std::runtime_error("Chat visible before Enter");
                SDL_Event chat_key{};chat_key.type=SDL_EVENT_KEY_DOWN;chat_key.key.key=SDLK_RETURN;
                if(!ui.process_event(chat_key)||!ui.visible_for_test("chat")) throw std::runtime_error("Enter failed to open chat");
                view.compose(session,0,frame,false);ui.compose(frame);
                renderer.render(frame,output/(locale+"-chat-open.png"));
                chat_key.key.key=SDLK_ESCAPE;ui.process_event(chat_key);

                ui.activate_for_test("build-more");
                ui.update(&session, "");
                view.compose(session, 0, frame, false);
                ui.compose(frame);
                renderer.render(frame, output / (locale + "-construction-catalog.png"));
                const auto first_building = ui.element_center_for_test("catalog-house");
                if (first_building.x <= 0 || first_building.y <= 0)
                    throw std::runtime_error("Construction catalog did not render");
                if (locale == "uk") {
                    if (!SDL_SetWindowSize(window.get(), 1100, 700))
                        throw std::runtime_error(SDL_GetError());
                    SDL_PumpEvents();
                    renderer.render(frame, output / "uk-compact-catalog.png");
                    if (!SDL_SetWindowSize(window.get(), 1440, 900))
                        throw std::runtime_error(SDL_GetError());
                    SDL_PumpEvents();
                }
            }
            {
                ribterra::Localization localization(base / "assets/locales");
                const auto settings=output / "uk-season-settings.json";
                std::ofstream(settings) << R"({"locale":"uk","ui_scale":1})";
                ribterra::GameUi ui(renderer,assets,localization,settings);
                ui.show_world();
                for(int season=0;season<4;++season) {
                    ribterra::World source(70421,10);
                    auto state=source.export_state(); state.ticks=std::uint64_t(season)*ribterra::days_per_season*ribterra::ticks_per_day;
                    std::string error;
                    if(!source.import_state(std::move(state),error) || !source.save(output/"season.save",error) ||
                       !session.load(output/"season.save",error)) throw std::runtime_error(error);
                    view.reset_world_state();
                    ui.update(session);
                    if(ui.element_rml_for_test("season")!=localization.text(ribterra::season_label(ribterra::Season(season))) ||
                       ui.element_rml_for_test("temperature")!="+18 °C") throw std::runtime_error("Season HUD mismatch");
                    ribterra::render::Frame2D frame;
                    view.compose(session,0,frame,false); ui.compose(frame);
                    renderer.render(frame,output/("season-"+std::to_string(season)+".png"));
                }
                for(int phase=0;phase<4;++phase) {
                    ribterra::World source(70421,10);auto state=source.export_state();
                    state.ticks=std::uint64_t(phase)*ribterra::ticks_per_day/4;
                    std::string error;
                    if(!source.import_state(std::move(state),error)||!source.save(output/"clock.save",error)||!session.load(output/"clock.save",error))
                        throw std::runtime_error(error);
                    view.reset_world_state();ui.update(session);
                    ribterra::render::Frame2D frame;view.compose(session,0,frame,false);ui.compose(frame);
                    renderer.render(frame,output/("day-night-"+std::to_string(phase)+".png"));
                }
                ribterra::World free_world(70421,10);
                bool captured[3]{};
                for(int tick=0;tick<1600;++tick) {
                    free_world.tick();
                    for(const auto &resident:free_world.residents()) {
                        const int kind=int(resident.profession.wait_reason)-int(ribterra::ProfessionWaitReason::Strolling);
                        if(kind<0 || kind>2 || captured[kind]) continue;
                        std::string error;
                        if(!free_world.save(output/"autonomy.save",error) || !session.load(output/"autonomy.save",error))
                            throw std::runtime_error(error);
                        view.reset_world_state();
                        ui.set_view_state(&session,{resident.id},true,1);ui.update(session);
                        ribterra::render::Frame2D frame;
                        view.compose(session,0,frame,false);ui.compose(frame);
                        renderer.render(frame,output/("autonomy-"+std::to_string(kind)+".png"));captured[kind]=true;
                    }
                    if(captured[0]&&captured[1]&&captured[2]) break;
                }
                if(!captured[0]||!captured[1]||!captured[2]) throw std::runtime_error("Autonomous visual scene missing");

            }
        }
        SDL_Quit();
        std::cout << "Five locale menu/settings/HUD captures completed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

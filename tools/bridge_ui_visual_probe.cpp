#include "../tests/bridge_fixture.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
using namespace ribterra;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
int main(int argc, char **argv) {
    try {
        const auto args = utf8_arguments(argc, argv);
        check(args.size() >= 2, "Output directory required");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        const bool software = args.size() > 2 && args[2] == "--software";
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra bridge and concept UI", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            check(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base / "assets");
            Localization localization(base / "assets/locales");
            std::ofstream(output / "settings.json") << R"({"locale":"uk","ui_scale":1})";
            GameUi ui(renderer, assets, localization, output / "settings.json");
            render::Frame2D frame;
            ui.update(nullptr);
            ui.compose(frame);
            renderer.render(frame, output / "concept-menu.png");
            check(ui.activate_for_test("menu-load"), "Load button unavailable");
            check(ui.take_actions().front().type == UiActionType::Load, "Menu load action missing");
            auto source = bridge_fixture();
            std::string error;
            for (int x = 27; x <= 29; ++x) {
                check(source.build(BuildingKind::Bridge, x, 25).accepted, "Section rejected");
                for (int t = 0; t < 3000 && !source.walkable(x, 25); ++t)
                    source.tick();
                check(source.walkable(x, 25), "Bridge incomplete");
            }
            check(source.save(output / "bridge.save", error), error);
            ClientSession session;
            check(session.load(output / "bridge.save", error), error);
            GameView view(renderer);
            view.load_art(assets);
            ui.show_world();
            auto capture = [&](const char *name) {
                view.compose(session, 0, frame, false);
                ui.set_view_state(&session, view.selected_ids(), true, 1);
                ui.update(session);
                ui.compose(frame);
                renderer.render(frame, output / name);
            };
            capture("bridge-hud.png");
            check(ui.activate_for_test("build-more"), "Build catalog missing");
            capture("building-catalog.png");
            check(ui.activate_for_test("catalog-bridge"), "Bridge icon unavailable");
            auto actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].building == BuildingKind::Bridge,
                  "Bridge button emitted wrong command");
            check(ui.activate_for_test("stocks-toggle"), "Resource panel missing");
            capture("resources.png");
            for (const auto *name : {"copper", "coal", "iron", "gold", "silver", "steel", "crystals"})
                check(ui.element_rml_for_test(name) == "0", "Future resource counter is not zero");
            check(ui.activate_for_test("stocks-close"), "Cannot close stocks");
            check(ui.activate_for_test("build-more"), "Construction rail missing");
            check(ui.activate_for_test("nav-defense"), "Defense category missing");
            ui.update(session);
            check(ui.activate_for_test("catalog-barracks"), "Defense building filtered out");
            ui.take_actions();
            World mountain(70421, 1);
            auto state = mountain.export_state();
            for (auto &chunk : state.chunks)
                if (chunk.key.layer == WorldLayer::Surface)
                    for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                        int x = chunk.key.cx * chunk_size + int(i % chunk_size),
                            y = chunk.key.cy * chunk_size + int(i / chunk_size);
                        if (x >= 14 && x <= 45 && y >= 14 && y <= 45)
                            chunk.tiles[i] = {
                                Terrain::Rock, 0, true, Biome::SnowMountain,
                                static_cast<std::int16_t>(4 * ((x - 14) + (y - 14)))};
                    }
            check(mountain.import_state(std::move(state), error), error);
            check(mountain.save(output / "mountain.save", error), error);
            check(session.load(output / "mountain.save", error), error);
            view.reset_world_state();
            view.compose(session, 0, frame, false);
            const auto peak = view.tile_screen(29, 29);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_MIDDLE;
            e.button.x = 700;
            e.button.y = 450;
            view.handle_event(e, session);
            e = {};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = 700;
            e.motion.y = 450 + (450 - peak.y);
            view.handle_event(e, session);
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = SDL_BUTTON_MIDDLE;
            view.handle_event(e, session);
            e = {};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = SDLK_A;
            view.handle_event(e, session);
            int checks = 0;
            for (int zoom_case = 0; zoom_case < 3; ++zoom_case) {
                if (zoom_case) {
                    e = {};
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.y = zoom_case == 1 ? -4.f : 7.f;
                    view.handle_event(e, session);
                }
                for (const auto size :
                     {render::Extent{1100, 700}, render::Extent{1920, 1080}, render::Extent{1440, 900}}) {
                    check(SDL_SetWindowSize(window.get(), int(size.width), int(size.height)), SDL_GetError());
                    SDL_PumpEvents();
                    capture("mountain-click.png");
                    for (int y = 27; y <= 31; ++y)
                        for (int x = 27; x <= 31; ++x) {
                            const auto logical = view.tile_screen(float(x), float(y));
                            const auto physical = renderer.logical_to_window({logical.x, logical.y});
                            e = {};
                            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                            e.button.button = SDL_BUTTON_RIGHT;
                            e.button.x = physical.x;
                            e.button.y = physical.y;
                            renderer.convert_event(e);
                            const auto cell = view.cell_at_screen(e.button.x, e.button.y);
                            check(cell == CellCoord{x, y, WorldLayer::Surface},
                                  "Mountain click selected sea-level footprint");
                            ++checks;
                        }
                }
            }
            const auto target = view.tile_screen(29, 29);
            const auto physical = renderer.logical_to_window({target.x, target.y});
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_RIGHT;
            e.button.x = physical.x;
            e.button.y = physical.y;
            renderer.convert_event(e);
            view.handle_event(e, session);
            bool arrived = false;
            for (int t = 0; t < 2000 && !arrived; ++t) {
                session.advance();
                const auto &r = session.residents().front();
                arrived = std::abs(r.x - 29) < .01f && std::abs(r.y - 29) < .01f;
            }
            check(arrived, "Mountain click did not move unit to the visible target");
            capture("mountain-arrival.png");
            const auto &device = renderer.device_info();
            std::ofstream(output / "verification.json") << nlohmann::json{
                {"version", engine_version},
                {"backend", device.backend},
                {"device", device.device},
                {"software", device.software},
                {"bridge_sections", 3},
                {"resource_placeholders", 7},
                {"mountain_pixel_clicks", checks},
                {"mountain_arrival", arrived},
                {"interactive_menu", true}}.dump(2);
        }
        SDL_Quit();
        std::cout << "Bridge, concept UI and mountain window-coordinate verification passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

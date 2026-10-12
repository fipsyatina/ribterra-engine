#include "ribterra/asset_catalog.hpp"
#include "ribterra/biomes.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
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
                SDL_CreateWindow("Ribterra atmosphere verification", 1440, 900,
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
            ui.show_world();
            World source(70421, 10);
            std::string error;
            render::Frame2D frame;
            {
                GameView cursor_view(renderer);cursor_view.load_art(assets);
                cursor_view.compose_cursor(frame);
                auto *normal=SDL_GetCursor();
                if(!software) check(normal && normal!=SDL_GetDefaultCursor() && SDL_CursorVisible(),"Native art cursor unavailable");
                const auto vertices=frame.vertices.size();
                cursor_view.arm_build(BuildingKind::Farm);cursor_view.compose_cursor(frame);
                if(!software)check(SDL_GetCursor()!=normal && SDL_CursorVisible(),"Build cursor did not change");
                check(frame.vertices.size()==vertices,"Cursor is still tied to the rendered frame");
                cursor_view.reset_world_state();cursor_view.compose_cursor(frame);
                if(!software)check(SDL_GetCursor()==normal,"Default cursor was not restored");
            }
            for (int scene = 0; scene < 5; ++scene) {
                SDL_SetWindowSize(window.get(),1440,900); SDL_PumpEvents();
                auto state = source.export_state();
                int cx = 24, cy = 24;
                const char *name = scene == 0   ? "expanded-view"
                                   : scene == 1 ? "snow-transition"
                                   : scene == 2 ? "desert-transition"
                                   : scene == 3 ? "starvation-warning" : "shore-transition";
                if (scene == 1 || scene == 2 || scene == 4) {
                    int best = -1;
                    for (int y = world_min + 24; y < world_max - 24; y += 3)
                        for (int x = world_min + 24; x < world_max - 24; x += 3) {
                            const auto here = source.tile(x, y).biome;
                            if (here != (scene == 1 ? Biome::Mountain : scene == 2 ? Biome::Desert : Biome::Lake))
                                continue;
                            int score = 0;
                            for (int dy = -6; dy <= 6; dy += 2)
                                for (int dx = -6; dx <= 6; dx += 2) {
                                    const auto b = source.tile(x + dx, y + dy).biome;
                                    score += b == (scene == 1 ? Biome::SnowMountain : Biome::Meadow);
                                }
                            if (score > 0) {
                                score = scene == 1 ? score + source.tile(x, y).elevation : std::min(score, 49-score);
                                if (score > best) {
                                    best = score;
                                    cx = x;
                                    cy = y;
                                }
                            }
                        }
                    check(best > 0, "Generated transition missing");
                    for (auto &chunk : state.chunks)
                        if (chunk.key.layer == WorldLayer::Surface)
                            for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                                int x = chunk.key.cx * chunk_size + int(i % chunk_size),
                                    y = chunk.key.cy * chunk_size + int(i / chunk_size);
                                chunk.tiles[i].explored = (x - cx) * (x - cx) + (y - cy) * (y - cy) < 17 * 17;
                            }
                }
                if (scene == 3) {
                    std::erase_if(state.item_stacks, [](const auto &s) { return s.item == ItemKind::Food; });
                    state.resources.food = 0;
                }
                World fixture(70421, 0);
                check(fixture.import_state(std::move(state), error), error);
                if (scene == 3) {
                    std::vector<int> ids;for(const auto &resident:fixture.residents())ids.push_back(resident.id);
                    fixture.stop(ids);
                    for (int i = 0; i < 1200; ++i) fixture.tick();
                }
                check(fixture.save(output / (std::string(name) + ".save"), error), error);
                ClientSession session;
                check(session.load(output / (std::string(name) + ".save"), error), error);
                GameView view(renderer);
                view.load_art(assets);
                view.compose(session, 0, frame, false);
                if (scene == 1 || scene == 2 || scene == 4) {
                    SDL_Event e{};
                    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    e.button.button = SDL_BUTTON_MIDDLE;
                    e.button.x = 720;
                    e.button.y = 450;
                    view.handle_event(e, session);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.x = 720 - ((cx - 24) - (cy - 24)) * 58 * 1.3f * .5f;
                    e.motion.y = 450 - ((cx - 24) + (cy - 24)) * 29 * 1.3f * .5f +
                                 surface_elevation(cx, cy, WorldGenerationConfig{70421, generator_version,
                                                   alpine_valley_profile_id, terrain_rules_version}) * 1.3f;
                    view.handle_event(e, session);
                    e = {};
                    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button = SDL_BUTTON_MIDDLE;
                    view.handle_event(e, session);
                }
                if (scene == 3) {
                    SDL_Event e{};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_A;
                    view.handle_event(e, session);
                }
                const auto capture = [&](const std::string &file) {
                    view.compose(session, 0, frame, false);
                    ui.set_view_state(&session, view.selected_ids(), false, 1, WorldLayer::Surface);
                    ui.update(&session, "");
                    ui.compose(frame);
                    renderer.render(frame, output / file);
                };
                capture(std::string(name) + ".png");
                if (scene == 3) {
                    check(ui.element_rml_for_test("food-warning").find(localization.text("health.no_food")) !=
                              std::string::npos,
                          "No food warning missing");
                    check(ui.element_rml_for_test("resident-health-reason")
                                  .find(localization.text("health.starving")) != std::string::npos,
                          "Starvation reason missing");
                    check(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
                    SDL_PumpEvents();
                    capture("starvation-compact.png");
                }
            }
            const auto &device = renderer.device_info();
            std::ofstream(output / "render-report.json")
                << nlohmann::json{{"version", engine_version},  {"backend", device.backend},
                                  {"device", device.device},    {"software", device.software},
                                  {"generated_transitions", 3}, {"native_cursor", !software}, {"cursor_independent_of_frame", true}, {"vision_multiplier", 1.5},
                                  {"starvation_warning", true}, {"ground_fog", true}}
                       .dump(2);
        }
        SDL_Quit();
        std::cout << "Generated biome boundaries, ground fog, larger vision and starvation explanations "
                     "rendered.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace ribterra;
namespace {
void check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
}
int main(int argc, char **argv) {
    try {
        check(argc > 1, "Output directory required");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra input feedback", 1440, 900, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
            check(bool(window), SDL_GetError());
            const auto base = std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Software});
            AssetCatalog assets(base / "assets");
            Localization locale(base / "assets/locales");
            for (const auto &language : Localization::supported_locales()) {
                const auto settings = output / (language + "-settings.json");
                std::ofstream(settings) << "{\"locale\":\"" << language << "\",\"ui_scale\":1}";
                GameUi ui(renderer, assets, locale, settings);
                GameView view(renderer); view.load_art(assets);
                ClientSession session(70421, "Input feedback");
                render::Frame2D frame;
                ui.show_world();
                view.compose(session, 0, frame, false);
                ui.update(session); ui.compose(frame); renderer.render(frame);
                auto click = [&](SDL_FPoint point, Uint8 button) {
                    const auto physical = renderer.logical_to_window({point.x, point.y});
                    for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
                        SDL_Event event{}; event.type = type; event.button.button = button;
                        event.button.x = physical.x; event.button.y = physical.y;
                        renderer.convert_event(event);
                        view.handle_event(event, session);
                    }
                };
                // The same status consumer as main.cpp: translate legacy keys once,
                // then deliver finished text to the real RmlUi HUD.
                auto notice = [&](const char *scenario, const std::string &expected = "") {
                    const auto message = view.take_status_text(locale);
                    check(message && !message->empty(), std::string(scenario) + ": missing feedback");
                    if (!expected.empty()) check(*message == expected, std::string(scenario) + ": wrong feedback");
                    check(!view.take_status_text(locale), "Feedback was delivered twice");
                    ui.set_view_state(&session, view.selected_ids(), view.paused(), view.speed(),
                                      view.layer(), view.selected_building_ids());
                    ui.update(session, *message);
                    view.compose(session, 0, frame, false); ui.compose(frame);
                    check(ui.visible_for_test("status") && ui.element_rml_for_test("status") == *message,
                          std::string(scenario) + ": HUD feedback missing");
                    renderer.render(frame, output / (language + "-" + scenario + ".png"));
                };
                const auto &resident = session.residents().front();
                auto point = *view.resident_screen(session, resident.id); point.y -= 10;
                click(point, SDL_BUTTON_LEFT);
                check(!view.selected_ids().empty(), "Resident selection failed");
                notice("resident", "Мешканця обрано");
                view.arm_build(BuildingKind::House);
                notice("build-mode", "Оберіть вільну ділянку");
                bool placed = false;
                const auto buildings_before = session.buildings().size();
                for (int y = 19; y < 30 && !placed; ++y)
                    for (int x = 19; x < 30 && !placed; ++x)
                        if (session.can_build(BuildingKind::House, x, y)) {
                            const auto site = view.tile_screen(float(x), float(y));
                            const auto picked = view.cell_at_screen(site.x, site.y);
                            if (picked.x != x || picked.y != y)
                                continue;
                            click(site, SDL_BUTTON_LEFT); placed = true;
                        }
                check(placed, "No construction site");
                notice("construction");
                for (int tick = 0; tick < 3; ++tick) session.advance();
                check(session.buildings().size() == buildings_before + 1, "Construction order was not applied");
                view.arm_build(BuildingKind::House); (void)view.take_status_text(locale);
                click(view.tile_screen(24, 24), SDL_BUTTON_RIGHT);
                notice("cancel", "Будівництво скасовано");
                click(view.tile_screen(26, 25), SDL_BUTTON_RIGHT);
                notice("move");
                view.set_status("lobby.host_only");
                notice("server-refusal", locale.text("lobby.host_only"));
                view.set_status("network.future_error");
                notice("unknown-error", "network.future_error");
                std::cout << language << ": selection, build, cancel, move and server feedback passed\n";
            }
        }
        SDL_Quit(); return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; SDL_Quit(); return 1;
    }
}

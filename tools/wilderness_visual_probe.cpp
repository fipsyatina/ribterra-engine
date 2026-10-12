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
#include <stdexcept>

using namespace ribterra;
void check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        check(argc > 1, "Output folder required");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra simplified HUD", 1440, 900, SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            check(bool(window), SDL_GetError());
            const auto base = std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Software});
            AssetCatalog assets(base / "assets");
            Localization locale(base / "assets/locales");
            std::ofstream(output / "settings.json") << R"({"locale":"uk","ui_scale":1})";
            GameUi ui(renderer, assets, locale, output / "settings.json");
            GameView view(renderer);
            view.load_art(assets);
            ClientSession session(70421, "Simplified HUD probe");
            std::string error;
            const auto load = [&](World &world, const char *name) {
                check(world.save(output / name, error), error);
                check(session.load(output / name, error), error);
                ui.reset_world_state();
                view.reset_world_state();
                ui.show_world();
                view.focus_group(session, {1, 2, 3});
            };
            render::Frame2D frame;
            const auto draw = [&](const char *name) {
                view.compose(session, session.ticks() * tick_seconds, frame, false);
                ui.set_view_state(&session, view.selected_ids(), false, 1);
                ui.update(session);
                ui.compose(frame);
                renderer.render(frame, output / name);
                check(!ui.visible_for_test("wilderness-panel"), "Expedition dialog reopened");
                check(!ui.activate_for_test("wilderness-open"), "Expedition entry still exists");
                check(ui.take_actions().empty(), "Removed dialog emitted an action");
            };
            // Food shortage used to force the expedition dialog over the map.
            World low_food(70421, 30);
            auto state = low_food.export_state();
            state.economy.wilderness.enabled = true;
            check(low_food.import_state(state, error), error);
            load(low_food, "low-food.save");
            check(session.inventory().available.food < int(session.residents().size()) * 5,
                  "Fixture must trigger the former food-shortage popup");
            draw("low-food.png");
            check(ui.activate_for_test("economy-open"), "Production entry disappeared");
            draw("production.png");
            check(ui.visible_for_test("economy-panel"), "Production panel stopped working");
            check(ui.activate_for_test("economy-close"), "Production close disappeared");

            // Clicking wildlife must use normal selection, with no hidden preflight state.
            World wildlife(70421, 4);
            state = wildlife.export_state();
            state.economy.wilderness.enabled = true;
            state.economy.wilderness.animals.push_back(
                {1, 1, AnimalSpecies::Deer, AnimalBehaviour::Feeding, 30, 26, 30, 26, 24});
            state.economy.wilderness.next_animal = 2;
            check(wildlife.import_state(state, error), error);
            load(wildlife, "wildlife.save");
            draw("world.png");
            const auto point = view.tile_screen(30, 26);
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = point.x;
            event.button.y = point.y - 13;
            view.handle_event(event, session);
            check(view.selection_drag_active(), "Animal click still intercepts map selection");
            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            view.handle_event(event, session);
            draw("animal-click.png");
            check(session.economy().wilderness.expeditions.empty(), "Animal click started a hunt");

            // Night itself must not reopen the removed expedition prompt. Active
            // expedition saves are intentionally rejected by current alpha rules.
            World night(70421, 4);
            state = night.export_state();
            state.ticks = daylight_ticks;
            state.economy.wilderness.enabled = true;
            check(night.import_state(state, error), error);
            load(night, "night.save");
            draw("night.png");
            check(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
            SDL_PumpEvents();
            draw("compact.png");
        }
        SDL_Quit();
        std::cout << "Simplified HUD: food/night do not open dialogs; animal selection and production work.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

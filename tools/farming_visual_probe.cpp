#include "ribterra/asset_catalog.hpp"
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
        using namespace ribterra;
        const auto args = utf8_arguments(argc, argv);
        require(args.size() >= 2, "Output directory required");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        const bool software = args.size() > 2 && args[2] == "--software";
        World source(70421, 2);
        require(source.build(BuildingKind::Farm, 26, 26).accepted, "Farm construction rejected");
        for (int i = 0; i < 2000 && (source.buildings().size() < 2 || source.buildings().back().progress < 1);
             ++i)
            source.tick();
        require(source.buildings().size() == 2 && source.buildings().back().progress == 1,
                "Farm did not finish");
        std::string error;
        require(source.save(output / "farm-ready.save", error), error);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra farm labor verification", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base / "assets");
            ClientSession session;
            require(session.load(output / "farm-ready.save", error), error);
            GameView view(renderer);
            view.load_art(assets);
            const auto settings = output / "farm-ui-settings.json";
            std::ofstream(settings) << R"({"locale":"uk","ui_scale":1})";
            Localization localization(base / "assets/locales");
            GameUi ui(renderer, assets, localization, settings);
            ui.show_world();
            render::Frame2D frame;
            const auto capture = [&](const char *name) {
                view.compose(session, double(session.ticks()) * tick_seconds, frame, false);
                ui.set_view_state(&session, view.selected_ids(), false, 1, WorldLayer::Surface);
                ui.update(&session, "");
                ui.compose(frame);
                renderer.render(frame, output / name);
            };
            // Verify real client snapshots and the rendered counter, not just World quantities.
            const auto food_before_meal = session.inventory().available.food;
            const auto stored_before_meal = session.inventory().stored.food;
            const auto living = std::count_if(session.residents().begin(), session.residents().end(),
                                             [](const auto &r) { return r.health > 0; });
            capture("food-before-meal.png");
            for (int i = 0; i < 200; ++i) session.advance();
            capture("food-after-meal.png");
            require(session.inventory().stored.food == stored_before_meal - living,
                    "Residents did not consume stored meals in the live client");
            require(session.inventory().available.food == food_before_meal - living,
                    "Client food counter did not reflect consumed meals");
            require(ui.element_rml_for_test("food") == localization.format("hud.number", {{"count", food_before_meal - living}}),
                    "Rendered HUD did not reflect consumed meals");
            std::ofstream(output / "food-consumption.json") << nlohmann::json{
                {"ticks", 200}, {"seconds_at_1x", 10}, {"living_residents", living},
                {"food_before", food_before_meal}, {"food_after", session.inventory().available.food},
                {"stored_before", stored_before_meal}, {"stored_after", session.inventory().stored.food},
                {"hud_verified", true}}.dump(2);
            capture("farm-vacant.png");
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_A;
            view.handle_event(event, session);
            require(view.selected_ids().size() == 2, "Native resident selection failed");
            const auto center = view.tile_screen(26, 26);
            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_RIGHT;
            event.button.x = center.x;
            event.button.y = center.y - 30; // Click the painted building, above its ground tile.
            view.handle_event(event, session);
            bool working = false, harvest = false;
            for (int i = 0; i < 1500; ++i) {
                session.advance();
                if (!working &&
                    std::all_of(session.residents().begin(), session.residents().end(),
                                [](const auto &r) { return r.job == Job::Farm && r.progress > .4f; })) {
                    working = true;
                    capture("farm-working.png");
                    require(
                        ui.element_rml_for_test("resident-work").find(localization.text("farm.working")) !=
                            std::string::npos,
                        "HUD did not show active farming");
                    require(session.save(output / "farm-working.save", error), error);
                    ClientSession restored;
                    require(restored.load(output / "farm-working.save", error), error);
                    require(restored.residents().front().progress == session.residents().front().progress,
                            "Saved farm progress changed");
                    ui.activate_for_test("profession-open");
                    capture("farmer-profession.png");
                    ui.activate_for_test("profession-close");
                    require(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
                    SDL_PumpEvents();
                    capture("farm-working-compact.png");
                    require(SDL_SetWindowSize(window.get(), 1440, 900), SDL_GetError());
                    SDL_PumpEvents();
                }
                if (working && session.inventory().carried.food > 0) {
                    harvest = true;
                    capture("farm-harvest-delivery.png");
                    break;
                }
            }
            require(working && harvest, "Farmers did not work and carry a harvest");
            require(session.stop(view.selected_ids()).accepted, "Stop command rejected");
            for (int i = 0; i < 1200 && (session.inventory().carried.food ||
                                         std::any_of(
                                             session.residents().begin(), session.residents().end(),
                                             [](const auto &r) { return r.job != Job::Idle; }));
                 ++i)
                session.advance();
            require(session.inventory().carried.food == 0, "Stop abandoned the physical harvest");
            capture("farm-stopped.png");
            const auto &device = renderer.device_info();
            std::ofstream(output / "farming-render-report.json") << nlohmann::json{
                {"native_right_click_assignment", true},
                {"farmers", 2},
                {"on_site_work", working},
                {"physical_harvest", harvest},
                {"mid_crop_save", true},
                {"stop_delivery_boundary", true},
                {"backend", device.backend},
                {"device", device.device},
                {"software", device.software}}.dump(2);
        }
        SDL_Quit();
        std::cout << "Farm construction, native assignment, working animation/HUD, harvest, save and stop "
                     "verified.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

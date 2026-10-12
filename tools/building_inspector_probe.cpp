#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
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
#include <stdexcept>

using namespace ribterra;
namespace {
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void load(World& world, WorldState state) {
    std::string error;
    check(world.import_state(std::move(state), error), error);
}
World fixture(int recipe, bool supplies = true) {
    World world(70421, 1);
    auto state = world.export_state();
    for (auto& chunk : state.chunks) if (chunk.key.layer == WorldLayer::Surface)
        for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
            const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
            const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
            if (x >= 18 && x <= 36 && y >= 18 && y <= 36)
                chunk.tiles[i] = {Terrain::Grass, 0, true, Biome::Meadow, 0};
        }
    const auto& r = recipes[recipe];
    state.buildings.push_back({state.next_building_id++, BuildingKind(r.building), 29, 29, 1, WorldLayer::Surface, 1});
    state.buildings.push_back({state.next_building_id++, BuildingKind(r.building), 33, 29, 1, WorldLayer::Surface, 1});
    state.item_stacks.clear();
    state.resources = {0, 0, 0};
    if (supplies) for (auto [kind, quantity] : {std::pair{r.input1, r.quantity1}, std::pair{r.input2, r.quantity2}}) {
        if (kind < 0) continue;
        state.item_stacks.push_back({state.next_item_stack_id++, ItemKind(kind), unsigned(quantity), 1, {ItemLocationKind::Storage, {}, 1}});
        if (kind == 0) state.resources.wood += quantity;
        if (kind == 1) state.resources.stone += quantity;
    }
    load(world, std::move(state));
    return world;
}
template<class Predicate> void until(World& world, Predicate predicate) {
    for (int n = 0; n < 3000 && !predicate(world.economy().orders.front()); ++n) world.tick();
    const auto order = world.economy().orders.front();
    check(predicate(order), "State not reached: " + order.reason + " phase " + std::to_string(int(order.phase)));
}
}
int main(int argc, char** argv) {
    try {
        check(argc > 1, "Output directory required");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra 0.25.8 inspector", 1440, 1000, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
            check(bool(window), SDL_GetError());
            const auto base = std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Software});
            AssetCatalog assets(base / "assets");
            Localization locale(base / "assets/locales");
            std::ofstream(output / "settings.json") << R"({"locale":"uk","ui_scale":1})";
            GameUi ui(renderer, assets, locale, output / "settings.json");
            GameView view(renderer); view.load_art(assets);
            ClientSession session;
            render::Frame2D frame;
            std::string error;
            int cases = 0;
            auto draw = [&](int building, const std::string& name, const std::string& expected, const std::string& notice = "") {
                ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface, {building});
                if (!ui.visible_for_test("building-panel"))
                    check(ui.activate_for_test("building-summary-details"), "Compact inspector details action missing");
                ui.update(session, notice);
                view.compose(session, 0, frame, false); ui.compose(frame);
                const auto status = ui.element_rml_for_test("bp-status");
                check(status == locale.text("inspector.state." + expected), name + ": unexpected status " + status);
                check(!ui.element_rml_for_test("bp-info").empty(), name + ": missing next step");
                check(ui.element_rml_for_test("bp-owner").find("#1") != std::string::npos, "Missing owner");
                check(ui.element_rml_for_test("bp-purpose").find("inspector.") == std::string::npos, "Untranslated purpose");
                renderer.render(frame, output / (name + ".png"));
                const auto close = renderer.logical_to_window(ui.element_center_for_test("bp-close"));
                int width{}, height{}; SDL_GetWindowSize(window.get(), &width, &height);
                check(close.x > 0 && close.y > 0 && close.x < width && close.y < height, "Close button offscreen");
                ++cases;
            };
            auto snapshot = [&](World& world) {
                check(world.save(output / "scenario.save", error), error);
                check(session.load(output / "scenario.save", error), error);
                ui.reset_world_state(); ui.show_world();
            };
            for (int recipe : {3, 8, 13}) {
                const auto slug = std::string(building_definition(BuildingKind(recipes[recipe].building)).slug);
                auto world = fixture(recipe);
                snapshot(world); draw(2, slug + "-ready", "no_order");
                check(world.production(2, 1, recipe, 1, 0).accepted, "Order rejected");
                until(world, [](const auto& o) { return o.phase == ProductionPhase::Delivery; });
                snapshot(world); draw(2, slug + "-supplies", "delivery");
                until(world, [](const auto& o) { return o.phase == ProductionPhase::Working; });
                snapshot(world); draw(2, slug + "-working", "working");
                check(ui.element_rml_for_test("bp-stats").find(locale.text("inspector.recipe." + std::string(recipes[recipe].key))) != std::string::npos, "Wrong active recipe");
                // Selection changes on a paused snapshot must immediately replace all fields.
                draw(3, slug + "-other", "no_order");
                check(ui.visible_for_test("bp-form"), "Another building retained active order form");
                draw(2, slug + "-reselected", "working");
                check(!ui.visible_for_test("bp-form"), "Active order exposed new-order form");
                until(world, [](const auto& o) { return o.phase == ProductionPhase::Output; });
                snapshot(world); draw(2, slug + "-output", "output");
                check(world.economy().orders.front().completed == 0, "Delivery unexpectedly completed");
                check(ui.element_rml_for_test("bp-stats").find("1 / 1") != std::string::npos, "Made batch is not counted during delivery");
                check(ui.element_rml_for_test("bp-info").find(locale.text("inspector.hint.output")) != std::string::npos, "Delivery warning missing");
                const auto output_state = world.export_state();
                auto night_output = output_state;
                night_output.ticks = daylight_ticks;
                for (auto& task : night_output.logistics_tasks) task.deadline_tick = daylight_ticks + 10000;
                load(world, night_output);
                snapshot(world); draw(2, slug + "-night-output", "output");
                load(world, output_state);
                // A destination fills after the goods were made: completion must not imply delivery.
                World dropped(70421, 0);
                auto dropped_state = output_state;
                for (auto& storage : dropped_state.storages) if (storage.building_id == 1) storage.capacity = 1;
                load(dropped, dropped_state);
                until(dropped, [](const auto& o) { return o.phase == ProductionPhase::Complete; });
                snapshot(dropped); draw(2, slug + "-output-ground", "output_ground");
                until(world, [](const auto& o) { return o.phase == ProductionPhase::Complete; });
                snapshot(world); draw(2, slug + "-complete", "complete");
                check(world.economy().stored[recipes[recipe].output] >= recipes[recipe].quantity, "Completed product not in stock");

                auto missing = fixture(recipe, false);
                check(missing.production(2, 1, recipe, 1, 0).accepted, "Missing-material order rejected"); missing.tick();
                snapshot(missing); draw(2, slug + "-materials", "materials");
                auto state = missing.export_state(); state.ticks = daylight_ticks; load(missing, state); missing.tick();
                snapshot(missing); draw(2, slug + "-night", "night");
                check(!missing.decide(SettlementDecision::NightShift, 0).accepted, "Alpha forced work allowed"); state=missing.export_state();state.ticks=ticks_per_day;load(missing,state);missing.tick();
                snapshot(missing); draw(2, slug + "-morning", "materials");

                auto blocked = fixture(recipe, false);
                check(blocked.production(2, 1, recipe, 1, 0).accepted, "Path order rejected");
                state = blocked.export_state();
                const auto& r = recipes[recipe];
                for (auto [kind, quantity] : {std::pair{r.input1, r.quantity1}, std::pair{r.input2, r.quantity2}}) if (kind >= 0) {
                    state.item_stacks.push_back({state.next_item_stack_id++, ItemKind(kind), unsigned(quantity), 1, {ItemLocationKind::Storage, {}, 2}});
                    if (kind == 0) state.resources.wood += quantity;
                    if (kind == 1) state.resources.stone += quantity;
                }
                for (auto& chunk : state.chunks) if (chunk.key.layer == WorldLayer::Surface)
                    for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
                        if (chunk.key == chunk_key({29 + dx, 29 + dy, WorldLayer::Surface}))
                            chunk.tiles[local_index(29 + dx, 29 + dy)] = {Terrain::Water, 0, true, Biome::Lake, -6};
                load(blocked, state); blocked.tick();
                snapshot(blocked); draw(2, slug + "-no-route", "unreachable");

                auto full = fixture(recipe);
                check(full.production(2, 1, recipe, 1, 0).accepted, "Full-storage order rejected");
                until(full, [](const auto& o) { return o.phase == ProductionPhase::Working; });
                state = full.export_state();
                for (auto& storage : state.storages) if (storage.building_id == 1) storage.capacity = 1;
                load(full, state);
                until(full, [](const auto& o) { return o.reason == "storage_full"; });
                snapshot(full); draw(2, slug + "-storage-full", "storage_full");
                check(full.economy().orders.front().completed == 0, "Blocked output counted as delivered");
                state = full.export_state();
                for (auto& storage : state.storages) if (storage.building_id == 1) storage.capacity = 10000;
                load(full, state); full.tick();
                snapshot(full); draw(2, slug + "-storage-restored", "output");

                auto unavailable = fixture(recipe);
                state = unavailable.export_state(); state.residents.front().health = 0; load(unavailable, state);
                snapshot(unavailable);
                check(session.production_group({2}, recipe, 1).accepted, "Group rejected before worker selection");
                for (int i = 0; i < 5; ++i) session.poll();
                draw(2, slug + "-no-worker", "no_worker");
                check(!ui.element_rml_for_test("bp-feedback").empty(), "Worker error hidden");
                draw(3, slug + "-feedback-cleared", "no_order");
                check(ui.element_rml_for_test("bp-feedback").empty(), "Another building inherited an error");
            }
            // Messages from map input must reach the real RmlUi status element exactly once.
            auto world = fixture(3); snapshot(world);
            view.compose(session, 0, frame, false);
            SDL_Event event{}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_RIGHT; event.button.x = 500; event.button.y = 500;
            view.handle_event(event, session);
            const auto notice = view.take_status_text(locale); check(notice && !notice->empty(), "Map input message lost");
            check(!view.take_status_text(locale), "Message not consumed");
            draw(2, "map-input-feedback", "no_order", *notice);
            check(ui.visible_for_test("status") && ui.element_rml_for_test("status") == *notice, "RmlUi feedback not visible");
            check(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError()); SDL_PumpEvents();
            draw(2, "compact", "no_order");
            check(ui.focus_for_test("bp-main"), "Cannot focus action in compact panel");
            ui.update(session); view.compose(session, 0, frame, false); ui.compose(frame);
            const auto action = renderer.logical_to_window(ui.element_center_for_test("bp-main"));
            check(action.y > 0 && action.y < 700, "Action inaccessible after scrolling");
            renderer.render(frame, output / "compact-action.png");
            std::ofstream(output / "report.txt") << cases << " rendered scenarios passed for Sawmill, Workshop and Kitchen; real simulation, selection, feedback and compact scrolling.\n";
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; SDL_Quit(); return 1;
    }
}

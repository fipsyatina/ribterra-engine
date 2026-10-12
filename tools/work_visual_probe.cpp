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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace ribterra;
struct Scene {
    std::string name;
    std::filesystem::path save;
    int resident_id{};
    bool stocks{};
};
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void install_wood_fixture(World &world, CellCoord target = {22, 19}) {
    auto state = world.export_state();
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    require(chunk != state.chunks.end(), "Wood fixture chunk is missing");
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    require(world.import_state(std::move(state), error), "Wood fixture import failed: " + error);
}
std::filesystem::path path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}
void step(World &world, bool drain = true) {
    world.tick();
    if (drain)
        world.take_path_results();
    const auto budget = world.work_diagnostics();
    require(budget.visits_last_tick <= work_intent_visits_per_tick &&
                budget.admission_attempts_last_tick <= work_admissions_per_tick,
            "Work scheduler exceeded a per-tick budget");
    const auto inv = world.inventory();
    require(inv.total.wood == inv.available.wood + inv.reserved.wood &&
                inv.total.stone == inv.available.stone + inv.reserved.stone &&
                inv.total.food == inv.available.food + inv.reserved.food,
            "Inventory conservation failed");
}
template <class Predicate>
void until(World &world, Predicate ready, const char *message, int ticks = 5000, bool drain = true) {
    for (int i = 0; i < ticks && !ready(); ++i)
        step(world, drain);
    require(ready(), message);
}
ConstructionWorkforce workforce(const World &world, int id) {
    for (const auto &item : world.construction_workforce())
        if (item.building_id == id)
            return item;
    return {id, 0, 0};
}
int worker(const World &world) {
    for (const auto &intent : world.work_intents())
        if (intent.kind == WorkIntentKind::BuildAssignment &&
            (intent.phase == WorkIntentPhase::Waiting || intent.phase == WorkIntentPhase::PathPending))
            return intent.resident_id;
    for (const auto &resident : world.residents())
        if (resident.job == Job::Build)
            return resident.id;
    return world.residents().front().id;
}
void save_scene(World &world, const std::filesystem::path &output, std::vector<Scene> &scenes,
                const std::string &name, int selected, bool stocks) {
    const auto file = output / (name + ".save");
    std::string error;
    require(world.save(file, error), "Could not save " + name + ": " + error);
    World restored(1, 1);
    require(restored.load(file, error), "Could not reload " + name + ": " + error);
    require(restored.checksum() == world.checksum(), "Scene round trip changed state");
    scenes.push_back({name, file, selected, stocks});
}
std::vector<Scene> prepare(const std::filesystem::path &output, nlohmann::json &report) {
    std::vector<Scene> scenes;
    World world(70421, 10);
    CellCoord site{};
    bool found = false;
    for (int y = 20; y <= 30 && !found; ++y)
        for (int x = 20; x <= 30 && !found; ++x)
            if (world.can_build(BuildingKind::House, x, y)) {
                site = {x, y};
                found = true;
            }
    require(found, "No reachable House site");
    require(world.build(BuildingKind::House, site.x, site.y).accepted, "House order rejected");
    until(
        world,
        [&] {
            return std::any_of(world.construction_orders().begin(), world.construction_orders().end(),
                               [](const auto &order) { return order.phase == ConstructionPhase::Work; });
        },
        "House did not reach paid Work phase");
    const int building = world.construction_orders().front().building_id;
    const int former = worker(world);
    const auto paid = world.inventory().total;
    require(paid.wood == 50 && paid.stone == 25, "Unexpected paid House cost");
    std::vector<int> all;
    for (const auto &resident : world.residents())
        all.push_back(resident.id);
    CellCoord away{};
    found = false;
    for (int y = 34; y <= 40 && !found; ++y)
        for (int x = 34; x <= 40 && !found; ++x)
            if (world.walkable(x, y)) {
                away = {x, y};
                found = true;
            }
    require(found && world.move(all, away.x, away.y).accepted, "Could not reassign the workers");
    step(world);
    require(workforce(world, building).working == 0 && workforce(world, building).travelling == 0,
            "Reassigned residents still counted as builders");
    save_scene(world, output, scenes, "builder-waiting", former, true);
    until(
        world,
        [&] {
            return std::any_of(world.work_intents().begin(), world.work_intents().end(),
                               [](const auto &intent) {
                                   return intent.kind == WorkIntentKind::BuildAssignment &&
                                          intent.phase == WorkIntentPhase::PathPending;
                               });
        },
        "No replacement builder was assigned");
    const int replacement = worker(world);
    save_scene(world, output, scenes, "builder-planning", replacement, true);
    until(
        world,
        [&] {
            for (const auto &resident : world.residents())
                if (resident.id == replacement)
                    return resident.job == Job::Build && resident.order_phase == OrderPhase::Active &&
                           workforce(world, building).working == 0 &&
                           workforce(world, building).travelling > 0;
            return false;
        },
        "Replacement builder did not travel to the site");
    save_scene(world, output, scenes, "builder-travelling", replacement, true);
    const auto travel_progress = world.buildings().back().progress;
    until(
        world,
        [&] {
            if (workforce(world, building).working > 0)
                return true;
            require(world.buildings().back().progress == travel_progress,
                    "Construction progressed before physical arrival");
            return false;
        },
        "Replacement builder never physically arrived");
    save_scene(world, output, scenes, "builder-working", replacement, true);
    until(
        world,
        [&] {
            for (const auto &item : world.buildings())
                if (item.id == building)
                    return item.progress >= 1;
            return false;
        },
        "Replacement builder did not finish the House");
    require(world.inventory().total.wood == paid.wood && world.inventory().total.stone == paid.stone,
            "Worker replacement charged building materials again");
    save_scene(world, output, scenes, "builder-complete", replacement, true);
    report["replacement"] = {{"former_worker", former},  {"replacement_worker", replacement},
                             {"building", building},     {"complete_tick", world.ticks()},
                             {"no_second_charge", true}, {"physical_arrival", true}};

    World gathering(70421, 1);
    // This scene verifies work completion/yield accounting, not random tree
    // placement inside the old 20..30 start square (now a safe clearing).
    install_wood_fixture(gathering);
    const CellCoord resource{22,19};
    require(gathering.tile(resource.x,resource.y).terrain==Terrain::Forest &&
            gathering.tile(resource.x,resource.y).amount>0 &&
            gathering.gather({1},resource.x,resource.y).accepted,
            "Authored work-completion resource is not reachable");
    const int amount = gathering.tile(resource.x, resource.y).amount;
    until(
        gathering,
        [&] {
            return std::any_of(gathering.work_intents().begin(), gathering.work_intents().end(),
                               [](const auto &intent) {
                                   return intent.phase == WorkIntentPhase::Complete &&
                                          intent.completion == WorkCompletionReason::Exhausted;
                               });
        },
        "Depleted resource was not a normal completion", 20000);
    require(gathering.tile(resource.x, resource.y).amount == 0, "Exhausted resource still has yield");
    require(gathering.inventory().total.wood == 80 + 3 * amount, "Repeated or lost gathering yield");
    require(gathering.inventory().carried.wood == 0, "Exhaustion hid undelivered cargo");
    save_scene(gathering, output, scenes, "resource-exhausted", 1, false);
    report["exhaustion"] = {{"resource", {resource.x, resource.y}},
                            {"source_units", amount},
                            {"wood", gathering.inventory().total.wood},
                            {"tick", gathering.ticks()},
                            {"normal_completion", true}};
    // Every task and intent comes from normal commands, including the pressure
    // from external replies deliberately not consumed until after this snapshot.
    World pressure(70421, 2);
    install_wood_fixture(pressure);
    pressure.stop({1, 2}); // Isolate planner pressure from free-time walks.
    require(pressure.walkable(26, 24), "Pressure fixture move target is blocked");
    require(pressure.move({2}, 26, 24).accepted, "Initial pressure move rejected");
    until(
        pressure,
        [&] {
            const auto &resident = pressure.residents()[1];
            return pressure.path_diagnostics().queued_batches == 0 && resident.x == 26.F &&
                   resident.y == 24.F;
        },
        "Pressure fixture resident did not reach the command target", 1000, false);
    for (std::size_t i = 1; i < max_path_batches - 2; ++i) {
        require(pressure.move({2}, 26, 24).accepted, "Repeated pressure move rejected");
        until(
            pressure, [&] { return pressure.path_diagnostics().queued_batches == 0; },
            "Repeated pressure move did not finish", 20, false);
    }
    require(pressure.export_state().path_results.size() == max_path_batches - 2,
            "Pressure fixture lost retained move outcomes");
    require(pressure.gather({1}, 22, 19).accepted, "Pressure fixture gather rejected");
    until(
        pressure,
        [&] {
            if (pressure.inventory().carried.wood != 3)
                return false;
            const auto snapshot = pressure.export_state();
            const auto &resident = snapshot.residents.front();
            const auto route = std::find_if(snapshot.routes.begin(), snapshot.routes.end(),
                                            [](const auto &value) { return value.resident_id == 1; });
            return route != snapshot.routes.end() && route->cursor == route->points.size() &&
                   resident.layer == WorldLayer::Surface &&
                   std::abs(resident.x - 24.F) + std::abs(resident.y - 24.F) == 1.F;
        },
        "Pressure fixture never reached the physical dropoff", 2000, false);
    require(pressure.move({2}, 26, 24).accepted, "Could not fill the final external path slot");
    until(
        pressure,
        [&] {
            return std::any_of(pressure.work_intents().begin(), pressure.work_intents().end(),
                               [](const auto &intent) {
                                   return intent.resident_id == 1 &&
                                          intent.phase == WorkIntentPhase::Waiting && intent.attempts > 0;
                               });
        },
        "Full path queue did not produce a bounded retry", 20, false);
    require(pressure.inventory().stored.wood == 83 && pressure.inventory().carried.wood == 0,
            "Waiting continuation lost its completed delivery");
    save_scene(pressure, output, scenes, "retry-waiting", 1, false);
    report["queue_pressure"] = {{"pending_paths", pressure.path_diagnostics().queued_batches},
                                {"tick", pressure.ticks()},
                                {"retained_external_replies", pressure.export_state().path_results.size()},
                                {"stored_wood", 83},
                                {"natural_dropoff_and_retry", true}};
    return scenes;
}
void same(const ClientSession &a, const ClientSession &b) {
    require(a.ticks() == b.ticks() && a.residents() == b.residents() && a.buildings() == b.buildings() &&
                a.inventory() == b.inventory() && a.resident_work() == b.resident_work() &&
                a.resident_logistics() == b.resident_logistics() &&
                a.construction_sites() == b.construction_sites(),
            "Work scene changed after save/resume");
}
void click(GameUi &ui, const char *id) {
    const auto center = ui.element_center_for_test(id);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = center.x;
    event.motion.y = center.y;
    ui.process_event(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    require(ui.process_event(event), "Work UI did not consume pointer press");
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(ui.process_event(event), "Work UI did not consume pointer release");
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        require(args.size() == 2 || (args.size() == 3 && args[2] == "--software"),
                "Usage: WorkVisualProbe output-directory [--software]");
        const bool software = args.size() == 3;
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        nlohmann::json report;
        const auto scenes = prepare(output, report);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra work scheduler probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "Window creation failed");
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            AssetCatalog assets(base / "assets");
            report["locales"] = nlohmann::json::array();
            for (const auto &locale : Localization::supported_locales()) {
                const auto settings = output / (locale + "-settings.json");
                std::ofstream(settings) << nlohmann::json{{"locale", locale}, {"ui_scale", 1.15}};
                Localization localization(base / "assets/locales");
                GameUi ui(renderer, assets, localization, settings);
                GameView view(renderer);
                ui.show_world();
                bool stocks_open = false;
                std::size_t resumed_scenes = 0;
                for (const auto &scene : scenes) {
                    if (scene.name == "retry-waiting" && locale != "uk")
                        continue;
                    ClientSession session(1, "Work visual probe"), restored(1, "Work resume probe");
                    std::string error;
                    require(session.load(scene.save, error), "Cannot load client work scene: " + error);
                    require(restored.load(scene.save, error), "Cannot load restored work scene: " + error);
                    same(session, restored);
                    const std::vector<int> selected{scene.resident_id};
                    ui.set_view_state(&session, selected, false, 1);
                    ui.update(session);
                    render::Frame2D initial;
                    view.compose(session, 0, initial, false);
                    ui.compose(initial);
                    if (stocks_open != scene.stocks) {
                        click(ui, "resource-wood");
                        stocks_open = scene.stocks;
                    }
                    render::Frame2D frame;
                    ui.set_view_state(&session, selected, false, 1);
                    ui.update(session);
                    view.compose(session, 0, frame, false);
                    ui.compose(frame);
                    if (scene.name == "resource-exhausted")
                        require(ui.element_rml_for_test("resident-work")
                                        .find(localization.text("work.exhausted")) != std::string::npos,
                                "Exhaustion is missing from the localized HUD");
                    if (scene.name == "retry-waiting") {
                        require(ui.element_rml_for_test("resident-work")
                                            .find(localization.text("work.waiting")) != std::string::npos &&
                                    ui.element_rml_for_test("resident-work-detail")
                                            .find(localization.text("work.retry")) != std::string::npos,
                                "Automatic retry is missing from the localized HUD");
                    }
                    for (const auto &construction : session.construction_sites()) {
                        if (construction.phase != ConstructionPhase::Work)
                            continue;
                        const char *key = construction.worker_phase == ConstructionWorkerPhase::Working
                                              ? "construction.work_active"
                                          : construction.worker_phase == ConstructionWorkerPhase::Travelling
                                              ? "construction.work_travelling"
                                          : construction.worker_phase == ConstructionWorkerPhase::Planning
                                              ? "construction.work_planning"
                                              : "construction.work_waiting";
                        const auto id = "site-phase-" + std::to_string(construction.building_id);
                        const auto expected =
                            construction.worker_phase == ConstructionWorkerPhase::Working
                                ? localization.format(key, {{"count", construction.assigned_workers}})
                                : localization.text(key);
                        require(ui.element_rml_for_test(id.c_str()).find(expected) != std::string::npos,
                                "Construction workforce is missing from the localized HUD");
                    }
                    if (locale == "uk" || scene.name == "builder-waiting" ||
                        scene.name == "builder-planning" || scene.name == "resource-exhausted")
                        renderer.render(frame, output / (locale + "-" + scene.name + ".png"));
                    if (locale == "uk" &&
                        (scene.name == "builder-planning" || scene.name == "retry-waiting")) {
                        require(SDL_SetWindowSize(window.get(), 1100, 700), "Compact resize failed");
                        SDL_PumpEvents();
                        render::Frame2D compact;
                        ui.update(session);
                        view.compose(session, 0, compact, false);
                        ui.compose(compact);
                        renderer.render(compact, output / (locale + "-compact-" + scene.name + ".png"));
                        require(SDL_SetWindowSize(window.get(), 1440, 900), "Viewport restore failed");
                        SDL_PumpEvents();
                    }
                    for (int tick = 0; tick < 8; ++tick) {
                        session.advance();
                        restored.advance();
                        same(session, restored);
                    }
                    ++resumed_scenes;
                }
                report["locales"].push_back(
                    {{"locale", locale}, {"resume_scenes", resumed_scenes}, {"resume_ticks_per_scene", 8}});
            }
        }
        SDL_Quit();
        std::ofstream(output / "results.json") << report.dump(2);
        std::cout << "Builder waiting, replacement, arrival, exhaustion and five-locale "
                     "save/resume HUD passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace ribterra;
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
CellCoord building_site(const ClientSession &session) {
    for (int y = 20; y <= 32; ++y)
        for (int x = 30; x >= 18; --x)
            if (session.can_build(BuildingKind::House, x, y))
                return {x, y};
    throw std::runtime_error("Visual scenario has no buildable site");
}
void same_view(const ClientSession &a, const ClientSession &b) {
    require(a.ticks() == b.ticks() && a.residents() == b.residents() &&
                a.buildings() == b.buildings() && a.inventory() == b.inventory() &&
                a.resident_logistics() == b.resident_logistics() &&
                a.construction_sites() == b.construction_sites(),
            "Carrying save/resume changed authoritative visible state");
}
void invariants(const ClientSession &session) {
    const auto &s = session.inventory();
    require(s.total.wood == s.available.wood + s.reserved.wood &&
                s.total.stone == s.available.stone + s.reserved.stone &&
                s.total.food == s.available.food + s.reserved.food,
            "Inventory buckets no longer conserve physical items");
    for (const auto &carrier : session.resident_logistics())
        require(carrier.quantity <= carrier.capacity && carrier.capacity == resident_carry_capacity,
                "Resident exceeded physical carrying capacity");
    for (const auto &site : session.construction_sites()) {
        require(site.delivered.wood <= site.required.wood &&
                    site.delivered.stone <= site.required.stone &&
                    site.delivered.food <= site.required.food,
                "Construction received duplicate materials");
        if (site.delivered != site.required)
            require(site.work == 0, "Construction started before every material was delivered");
    }
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
    require(ui.process_event(event), "Logistics control did not consume pointer press");
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(ui.process_event(event), "Logistics control did not consume pointer release");
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        require(args.size() == 2 || (args.size() == 3 && args[2] == "--software"),
                "Usage: LogisticsVisualProbe output-directory [--software]");
        const bool software = args.size() == 3;
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra physical logistics probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "Window creation failed");
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(
                window.get(), base / "assets",
                {software ? render::Backend::Software : render::Backend::Direct3D12});
            const auto logical_size = renderer.fitted_logical_size();
            renderer.set_logical_size(logical_size);
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            AssetCatalog assets(base / "assets");
            nlohmann::json results = nlohmann::json::array();
            for (const auto &locale : Localization::supported_locales()) {
                const auto settings = output / (locale + "-settings.json");
                std::ofstream(settings) << nlohmann::json{{"locale", locale}, {"ui_scale", 1.15}};
                Localization localization(base / "assets/locales");
                GameUi ui(renderer, assets, localization, settings, logical_size);
                GameView view(renderer);
                view.set_logical_size(logical_size);
                ClientSession session(70421, "Logistics visual probe");
                std::unique_ptr<ClientSession> restored;
                const auto site = building_site(session);
                require(session.build(BuildingKind::House, site.x, site.y).accepted,
                        "House order was not submitted");
                session.advance();
                require(!session.construction_sites().empty(),
                        "Accepted order lacks a visible site");
                require(session.inventory().total.wood == 80 &&
                            session.inventory().reserved.wood == 30 &&
                            session.inventory().available.wood == 50,
                        "Build did not reserve wood without consuming it");
                ui.show_world();
                require(ui.activate_for_test("hud-settings-toggle"),
                        "Logistics probe could not open the utility tray");
                ui.update(session);
                require(ui.visible_for_test("system-controls"),
                        "Logistics probe utility tray stayed hidden after RmlUi update");
                std::vector<int> selected{session.residents().front().id};
                const auto render_frame = [&] {
                    ui.set_view_state(&session, selected, false, 1);
                    ui.update(session);
                    render::Frame2D frame;
                    view.compose(session, 0, frame, false);
                    ui.compose(frame);
                    return frame;
                };
                render_frame();
                click(ui, "selection-details");
                render_frame();
                click(ui, "resource-wood");
                const auto capture = [&](const std::string &suffix) {
                    auto frame = render_frame();
                    // Changing the selected carrier intentionally collapses the
                    // compact resident details. Reopen them through the same
                    // pointer path a player uses before validating that panel.
                    if (!ui.visible_for_test("resident-carry")) {
                        click(ui, "selection-details");
                        frame = render_frame();
                    }
                    int width{}, height{};
                    require(SDL_GetWindowSize(window.get(), &width, &height),
                            "Viewport unavailable");
                    for (const auto *control :
                         {"stocks-title", "stocks-table", "stocks-close",
                          "resident-carry", "surface-tab", "mine-tab"}) {
                        require(ui.visible_for_test(control),
                                ("Logistics HUD control unexpectedly hidden: " +
                                 std::string(control)).c_str());
                        const auto logical = ui.element_center_for_test(control);
                        const auto center = renderer.logical_to_window(logical);
                        if (!(center.x > 0 && center.x < width && center.y > 0 && center.y < height))
                            throw std::runtime_error(
                                "Logistics HUD lies outside the viewport: control=" +
                                std::string(control) + " logical=" + std::to_string(logical.x) + "," +
                                std::to_string(logical.y) + " window=" + std::to_string(center.x) + "," +
                                std::to_string(center.y) + " viewport=" + std::to_string(width) + "x" +
                                std::to_string(height));
                    }
                    require(ui.element_center_for_test("stocks-table").y -
                                    ui.element_center_for_test("stocks-title").y <
                                300,
                            "Inventory header collapsed when its delivery list overflowed");
                    renderer.render(frame, output / (locale + "-" + suffix + ".png"));
                };
                capture("reserved");
                bool carrying = false, worked = false, complete = false;
                std::uint64_t carrying_tick{}, completed_tick{};
                for (int step = 0; step < 5000; ++step) {
                    session.advance();
                    if (restored) {
                        restored->advance();
                        same_view(session, *restored);
                    }
                    invariants(session);
                    if (!carrying) {
                        const auto carrier =
                            std::find_if(session.resident_logistics().begin(),
                                         session.resident_logistics().end(),
                                         [](const auto &v) { return v.quantity > 0; });
                        if (carrier != session.resident_logistics().end()) {
                            selected = {carrier->resident_id};
                            carrying = true;
                            carrying_tick = session.ticks();
                            capture("carrying");
                            if (locale == "uk") {
                                click(ui, "stocks-close");
                                renderer.render(render_frame(), output / "uk-carrying-world.png");
                                click(ui, "resource-wood");
                            }
                            require(ui.element_rml_for_test("resident-carry")
                                            .find(localization.text(
                                                carrier->item == ItemKind::Wood    ? "hud.wood"
                                                : carrier->item == ItemKind::Stone ? "hud.stone"
                                                                                   : "hud.food")) !=
                                        std::string::npos,
                                    "Cargo name did not use the active locale");
                            std::string error;
                            const auto save = output / (locale + "-carrying.save");
                            require(session.save(save, error), "Carrying save failed");
                            restored = std::make_unique<ClientSession>(1, "Restored logistics");
                            require(restored->load(save, error), "Carrying restore failed");
                            same_view(session, *restored);
                        }
                    }
                    for (const auto &order : session.construction_sites())
                        worked |= order.phase == ConstructionPhase::Work && order.work > 0;
                    const auto house = std::find_if(
                        session.buildings().begin(), session.buildings().end(), [&](const auto &v) {
                            return v.kind == BuildingKind::House && v.x == site.x && v.y == site.y;
                        });
                    if (house != session.buildings().end() && house->progress >= 1) {
                        complete = true;
                        completed_tick = session.ticks();
                        break;
                    }
                }
                require(carrying && worked && complete,
                        "House did not pass carry, work and completion phases");
                require(session.inventory().total.wood == 50 &&
                            session.inventory().total.stone == 25,
                        "Completed house did not consume exactly its delivered materials");
                capture("complete");
                bool cancellation = false;
                if (locale == "uk") {
                    restored.reset();
                    const auto second_site = building_site(session);
                    require(
                        session.build(BuildingKind::House, second_site.x, second_site.y).accepted,
                        "Cancellation scenario could not submit second house");
                    for (int step = 0; step < 3000; ++step) {
                        session.advance();
                        if (session.inventory().carried.wood + session.inventory().carried.stone >
                            0)
                            break;
                    }
                    require(!session.construction_sites().empty() &&
                                session.inventory().carried.wood +
                                        session.inventory().carried.stone >
                                    0,
                            "Cancellation scenario did not reach carrying");
                    const auto order = session.construction_sites().front();
                    require(order.phase == ConstructionPhase::Delivery,
                            "Cancellation must precede material consumption");
                    const auto before = session.inventory().total;
                    render_frame();
                    const auto id = "cancel-site-" + std::to_string(order.building_id);
                    require(ui.activate_for_test(id), "Cancel control unavailable");
                    auto actions = ui.take_actions();
                    require(actions.size() == 1 &&
                                actions.front().type == UiActionType::CancelConstruction &&
                                actions.front().value == order.building_id,
                            "Cancel control lost its stable building ID");
                    require(session.cancel_construction(actions.front().value).accepted,
                            "Cancellation command was not accepted");
                    session.advance();
                    invariants(session);
                    require(session.construction_sites().empty() &&
                                session.inventory().total.wood == before.wood &&
                                session.inventory().total.stone == before.stone,
                            "Cancelling a delivery lost materials or retained its building site");
                    cancellation = true;
                    require(SDL_SetWindowSize(window.get(), 1100, 700), "Compact resize failed");
                    SDL_PumpEvents();
                    capture("compact-cancelled");
                    require(SDL_SetWindowSize(window.get(), 1440, 900), "Viewport restore failed");
                    SDL_PumpEvents();
                }
                results.push_back({{"locale", locale},
                                   {"carrying_tick", carrying_tick},
                                   {"completed_tick", completed_tick},
                                   {"carrying_save_resume", true},
                                   {"delivery_before_work", true},
                                   {"capacity_and_conservation", true},
                                   {"cancel_during_carry", cancellation}});
            }
            std::ofstream(output / "results.json") << results.dump(2);
        }
        SDL_Quit();
        std::cout << "Physical delivery, carrying save/resume, construction and five-locale HUD "
                     "passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

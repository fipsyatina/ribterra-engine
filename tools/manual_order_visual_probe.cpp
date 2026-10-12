#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace ribterra;
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void load_wood_fixture(ClientSession &session, const std::filesystem::path &save) {
    constexpr CellCoord target{22, 19};
    World fixture(70421, 10);
    auto state = fixture.export_state();
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
    require(fixture.import_state(std::move(state), error), "Wood fixture import failed: " + error);
    require(fixture.save(save, error), "Wood fixture save failed: " + error);
    require(session.load(save, error), "Wood fixture client load failed: " + error);
    std::filesystem::remove(save);
}
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
bool terminal(ManualOrderPhase phase) {
    return phase == ManualOrderPhase::Complete || phase == ManualOrderPhase::Cancelled ||
           phase == ManualOrderPhase::Failed;
}
std::vector<ManualOrderView> orders(const ClientSession &session, int resident) {
    std::vector<ManualOrderView> rows;
    for (const auto &row : session.manual_orders())
        if (row.resident_id == resident && !terminal(row.phase))
            rows.push_back(row);
    return rows;
}
bool no_control(const ClientSession &session) {
    return std::none_of(session.resident_controls().begin(), session.resident_controls().end(),
                        [](const auto &control) { return control.holder_id != 0; });
}
struct Hud {
    GameUi &ui;
    GameView &view;
    render::Renderer2D &renderer;
    ClientSession &session;
    std::vector<int> selected;
    std::string status{};

    render::Frame2D frame() {
        ui.set_view_state(&session, selected, view.paused(), view.speed());
        ui.update(session, status);
        render::Frame2D frame;
        view.compose(session, 0, frame, false);
        ui.compose(frame);
        return frame;
    }
    bool event(SDL_Event value) {
        view.observe_input(value);
        const bool consumed = ui.process_event(value);
        if (!consumed)
            view.handle_event(value, session);
        return consumed;
    }
    void key(SDL_Keycode code, bool down, bool repeat = false, SDL_Keymod mod = SDL_KMOD_NONE) {
        SDL_Event value{};
        value.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        value.key.key = code;
        value.key.mod = mod;
        value.key.repeat = repeat;
        event(value);
    }
    void motion(render::Point p) {
        SDL_Event value{};
        value.type = SDL_EVENT_MOUSE_MOTION;
        value.motion.x = p.x;
        value.motion.y = p.y;
        event(value);
    }
    void pointer(render::Point p, Uint8 button, bool expect_ui) {
        motion(p);
        SDL_Event value{};
        value.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        value.button.button = button;
        value.button.x = p.x;
        value.button.y = p.y;
        require(event(value) == expect_ui, "Pointer press went to the wrong surface");
        value.type = SDL_EVENT_MOUSE_BUTTON_UP;
        require(event(value) == expect_ui, "Pointer release went to the wrong surface");
    }
    void click(const std::string &id) { pointer(ui.element_center_for_test(id), SDL_BUTTON_LEFT, true); }
    void map(CellCoord target) {
        const auto p = view.tile_screen(static_cast<float>(target.x), static_cast<float>(target.y));
        pointer({p.x, p.y}, SDL_BUTTON_RIGHT, false);
        session.poll();
    }
    UiAction action(UiActionType type) {
        auto actions = ui.take_actions();
        require(actions.size() == 1 && actions.front().type == type,
                "Expected exactly one typed manual UI action; got " + std::to_string(actions.size()));
        return std::move(actions.front());
    }
    void choose_gather() {
        require(ui.focus_for_test("orders-gather"), "Gather select rejected focus");
        key(SDLK_RETURN, true);
        key(SDLK_RETURN, false);
        frame();
        key(SDLK_UP, true);
        key(SDLK_UP, false);
        key(SDLK_RETURN, true);
        key(SDLK_RETURN, false);
        require(action(UiActionType::GatherMode).value == 0, "Keyboard did not choose tile scope");
        key(SDLK_RETURN, true);
        key(SDLK_RETURN, false);
        frame();
        key(SDLK_DOWN, true);
        key(SDLK_DOWN, false);
        key(SDLK_RETURN, true);
        key(SDLK_RETURN, false);
        const auto change = action(UiActionType::GatherMode);
        require(change.value == 1, "Keyboard did not choose OneDelivery");
        view.set_gather_completion(GatherCompletion::OneDelivery);
    }
    void scroll(bool bottom) {
        motion(ui.element_center_for_test("orders-body"));
        SDL_Event value{};
        value.type = SDL_EVENT_MOUSE_WHEEL;
        value.wheel.y = bottom ? -100.f : 100.f;
        require(event(value), "Orders panel did not consume scrolling");
        float previous = ui.element_center_for_test("orders-authority").y;
        int stable = 0;
        for (int i = 0; i < 120 && stable < 4; ++i) {
            SDL_Delay(16);
            frame();
            const auto current = ui.element_center_for_test("orders-authority").y;
            stable = i >= 4 && std::abs(current - previous) < .1f ? stable + 1 : 0;
            previous = current;
        }
        require(stable == 4, "Orders scrolling did not settle");
    }
    void capture(const std::filesystem::path &file) { renderer.render(frame(), file); }
};
std::vector<CellCoord> visible_sites(const ClientSession &session, const GameView &view, bool resource) {
    std::vector<CellCoord> sites;
    for (int y = 16; y < 32; ++y)
        for (int x = 16; x < 32; ++x) {
            const auto &tile = session.tile(x, y);
            const auto p = view.tile_screen(static_cast<float>(x), static_cast<float>(y));
            const bool occupied = std::any_of(
                session.buildings().begin(), session.buildings().end(), [&](const auto &building) {
                    return building.x == x && building.y == y && building.layer == WorldLayer::Surface;
                });
            if (!occupied && tile.explored && (tile.amount > 0) == resource &&
                tile.terrain != Terrain::Water && p.x > 340 && p.x < 1040 && p.y > 160 && p.y < 620 &&
                (resource || tile.terrain != Terrain::Rock))
                sites.push_back({x, y});
        }
    require(sites.size() >= 2, "Need visible command targets away from HUD cards");
    return sites;
}
template <class Predicate>
void network_until(net::NetworkServer &server, ClientSession &a, ClientSession &b, Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        server.poll();
        a.poll();
        b.poll();
        server.poll();
        a.poll();
        b.poll();
        if (ready())
            return;
        SDL_Delay(5);
    }
    throw std::runtime_error("Paused network control projection did not converge: " + server.last_error());
}
void physical_queue(GameUi &ui, GameView &view, render::Renderer2D &renderer,
                    const std::filesystem::path &output) {
    ClientSession session(70421, "Queue delivery probe");
    load_wood_fixture(session, output / "manual-order-wood-fixture.save");
    ui.reset_world_state();
    view.reset_world_state();
    ui.show_world();
    Hud hud{ui, view, renderer, session, {}};
    hud.frame();
    const auto &person = session.residents().front();
    const auto point = view.tile_screen(person.x, person.y);
    hud.pointer({point.x, point.y - 13}, SDL_BUTTON_LEFT, false);
    hud.selected = view.selected_ids();
    require(hud.selected.size() == 1, "Physical queue scene needs one pointer-selected settler");
    const int resident = hud.selected.front();
    hud.frame();
    hud.click("action-gather");
    hud.frame();
    hud.choose_gather();
    hud.click("orders-close");
    hud.frame();
    hud.map({22, 19});
    hud.key(SDLK_LSHIFT, true);
    hud.map({26, 25});
    hud.key(SDLK_LSHIFT, false);
    require(orders(session, resident).size() == 2, "Physical gather and return queue was not accepted");
    for (int i = 0; i < 2000 && session.inventory().carried.wood == 0; ++i)
        session.advance();
    require(session.inventory().carried.wood == 3,
            "OneDelivery did not produce the expected physical Wood cargo");
    view.set_paused(true);
    hud.frame();
    hud.click("action-gather");
    hud.frame();
    hud.scroll(false);
    hud.capture(output / "uk-cargo-and-next-order.png");
    const auto save = output / "manual-cargo.ribsave";
    std::string error;
    require(session.save(save, error), "Live queue save failed: " + error);
    ClientSession restored(1, "Queue resume");
    require(restored.load(save, error), "Live queue load failed: " + error);
    const auto same = [&] {
        return session.ticks() == restored.ticks() && session.residents() == restored.residents() &&
               session.manual_orders() == restored.manual_orders() &&
               session.inventory() == restored.inventory() &&
               session.resident_work() == restored.resident_work() &&
               session.resident_logistics() == restored.resident_logistics();
    };
    require(same(), "Live queue/cargo projection changed on resume");
    for (int i = 0; i < 2000 && (!orders(session, resident).empty() || i < 64); ++i) {
        session.advance();
        restored.advance();
        require(same(), "Live queue/cargo diverged after resume");
    }
    require(orders(session, resident).empty() && session.inventory().carried.wood == 0,
            "Gather-deliver-move queue did not finish");
    const auto finished = std::find_if(session.residents().begin(), session.residents().end(),
                                       [&](const auto &value) { return value.id == resident; });
    require(finished != session.residents().end() && std::abs(finished->x - 26.f) < .01f &&
                std::abs(finished->y - 25.f) < .01f,
            "Completed Move has not physically arrived");
    bool delivered = false, arrived = false;
    for (const auto &row : session.manual_orders()) {
        delivered |= row.resident_id == resident && row.completion == ManualOrderCompletion::Delivered;
        arrived |= row.resident_id == resident && row.completion == ManualOrderCompletion::Arrived;
    }
    require(delivered && arrived, "Queue completion reasons do not match delivery and arrival");
    hud.frame();
    hud.scroll(false);
    hud.capture(output / "uk-delivered-and-arrived.png");
}
void network_controls(GameUi &ui, GameView &view, render::Renderer2D &renderer,
                      const std::filesystem::path &output) {
    std::unique_ptr<net::NetworkServer> server;
    for (std::uint16_t port = 27820; port < 27840 && !server; ++port) {
        net::HostConfig config;
        config.transport.bind_address = "127.0.0.1";
        config.transport.port = port;
        auto candidate = std::make_unique<net::NetworkServer>(config);
        if (candidate->listen())
            server = std::move(candidate);
    }
    require(server != nullptr, "Could not bind a loopback control probe");
    ClientSession a(70421, "Olena"), b(70421, "Taras <&>");
    std::string error;
    const auto endpoint = "127.0.0.1:" + std::to_string(server->port());
    require(a.connect_remote(endpoint, "", error) && b.connect_remote(endpoint, "", error), error);
    network_until(*server, a, b, [&] { return a.ready() && b.ready() && a.control_players().size() == 2; });
    const int id = a.residents().front().id;
    require(a.move({id}, 26, 25).accepted, "First player could not queue a claim");
    network_until(*server, a, b, [&] {
        return !a.resident_controls().empty() && a.resident_controls().front().held_by_recipient &&
               !b.resident_controls().empty();
    });
    ui.reset_world_state();
    view.reset_world_state();
    view.set_paused(true);
    ui.show_world();
    Hud hud{ui, view, renderer, a, {id}};
    hud.frame();
    hud.click("action-gather");
    hud.frame();
    hud.scroll(true);
    require(ui.select_option_rml_for_test("orders-player", 0).find("Taras &lt;&amp;&gt;") !=
                std::string::npos,
            "Transfer player name was not safely escaped");
    const auto old_orders = a.manual_orders();
    hud.capture(output / "uk-network-transfer.png");
    hud.click("orders-transfer");
    const auto transfer = hud.action(UiActionType::TransferControl);
    require(transfer.residents == std::vector<int>{id} && transfer.target_player == b.player_id() &&
                transfer.control_epoch == a.control_epoch(),
            "Transfer action lost its actor, target or epoch");
    require(a.transfer_control(transfer.residents, transfer.target_player).accepted,
            "Transfer submission failed");
    network_until(*server, a, b, [&] {
        return !a.resident_controls().front().held_by_recipient &&
               b.resident_controls().front().held_by_recipient;
    });
    require(a.manual_orders() == old_orders && a.ticks() == 0,
            "Paused transfer changed orders or advanced simulation");
    hud.frame();
    hud.capture(output / "uk-network-other-controller.png");
    require(a.stop({id}).accepted, "Conflict test was not queued");
    std::optional<CommandReply> conflict;
    network_until(*server, a, b, [&] {
        while (auto reply = a.take_command_reply())
            if (!reply->control_rejections.empty())
                conflict = *reply;
        return conflict.has_value();
    });
    require(conflict->control_rejections.front().failure == ControlFailure::HeldByAnother,
            "Control conflict was not typed");
    hud.status = ui.format_command_reply(*conflict);
    hud.click("orders-close");
    hud.capture(output / "uk-network-conflict.png");
    ui.reset_world_state();
    Hud second{ui, view, renderer, b, {id}};
    second.frame();
    second.click("action-gather");
    second.frame();
    second.scroll(true);
    second.click("orders-release");
    const auto release = second.action(UiActionType::ReleaseControl);
    require(b.release_control(release.residents).accepted, "Second player release failed");
    network_until(*server, a, b, [&] { return no_control(a) && no_control(b); });
    require(a.ticks() == 0 && b.ticks() == 0, "Release while paused ticked the simulation");
    server->shutdown();
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = utf8_arguments(argc, argv);
        require(args.size() == 2 || (args.size() == 3 && args[2] == "--software"),
                "Usage: ManualOrderVisualProbe output-directory [--software]");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        nlohmann::json report{{"locales", nlohmann::json::array()},
                              {"network", "loopback, two clients, no physics ticks"}};
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra manual orders probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(
                window.get(), base / "assets",
                {args.size() == 3 ? render::Backend::Software : render::Backend::Direct3D12});
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            AssetCatalog assets(base / "assets");
            for (const auto &locale : Localization::supported_locales()) {
                const auto settings = output / (locale + "-settings.json");
                std::ofstream(settings) << nlohmann::json{{"locale", locale}, {"ui_scale", 1.15}};
                Localization localization(base / "assets/locales");
                GameUi ui(renderer, assets, localization, settings);
                CommandReply partial;
                partial.status = CommandReplyStatus::Applied;
                partial.residents = {{1, ResidentCommandStatus::Applied, PathFailure::None, 55},
                                     {999, ResidentCommandStatus::Rejected, PathFailure::ResidentUnavailable},
                                     {2, ResidentCommandStatus::Rejected, PathFailure::ReservationConflict}};
                partial.control_rejections = {{2, ControlFailure::HeldByAnother}};
                const auto explanation = ui.format_command_reply(partial);
                require(
                    explanation.find("#999") != std::string::npos &&
                        explanation.find("#2") != std::string::npos &&
                        explanation.find(localization.text("manual.control_failure.1")) !=
                            std::string::npos &&
                        explanation.find(localization.text("path_failure.reservation_conflict")) ==
                            std::string::npos,
                    "Partial outcome lost actor IDs or described a controller conflict as reserved cargo");
                GameView view(renderer);
                ClientSession session(70421, "Manual orders probe");
                ui.show_world();
                view.set_paused(true);
                Hud hud{ui, view, renderer, session, {}};
                hud.frame();
                hud.key(SDLK_A, true);
                hud.key(SDLK_A, false);
                hud.selected = view.selected_ids();
                require(hud.selected.size() == session.residents().size(), "A did not select the settlement");
                const int first = hud.selected.front(), other = hud.selected.back();
                const auto move_sites = visible_sites(session, view, false);
                const auto gather_sites = visible_sites(session, view, true);
                hud.map(move_sites[0]);
                hud.key(SDLK_LSHIFT, true);
                hud.map(move_sites[1]);
                require(orders(session, first).size() == 2,
                        "Shift right-click replaced instead of appending");
                hud.frame();
                hud.click("action-gather");
                hud.frame();
                // Modifier release over a focused RmlUi select must still reach modifier tracking.
                hud.key(SDLK_LSHIFT, false);
                hud.choose_gather();
                hud.frame();
                hud.click("orders-close");
                hud.frame();
                hud.key(SDLK_LSHIFT, true);
                hud.map(gather_sites[0]);
                hud.key(SDLK_LSHIFT, false);
                const auto original = orders(session, first);
                require(original.size() == 3 &&
                            original.back().gather_completion == GatherCompletion::OneDelivery &&
                            original.back().kind == ManualOrderKind::Gather,
                        "Map gather lost explicit completion mode or queue order");
                require(session.ticks() == 0, "Command admission advanced a paused world");
                hud.frame();
                require(ui.element_rml_for_test("resident-order").find(
                            localization.text("v01.group_common_actions")) != std::string::npos,
                        "Multi-selection summary must stay aggregate instead of copying one resident's order");
                hud.click("action-gather");
                hud.frame();
                hud.scroll(false);
                require(ui.element_rml_for_test("orders-list").find(localization.text("manual.gather.1")) !=
                            std::string::npos,
                        "Visible queue does not name OneDelivery");
                hud.capture(output / (locale + "-queue.png"));
                // Focus scrolls a live cancel button into view. Pointer activation must carry the exact
                // stable ID.
                const auto cancel_id = "order-cancel-" + std::to_string(original[1].order_id);
                require(ui.focus_for_test(cancel_id), "Queue cancel button rejected focus");
                hud.frame();
                hud.click(cancel_id);
                const auto cancel = hud.action(UiActionType::CancelManualOrder);
                require(cancel.order_id == original[1].order_id &&
                            cancel.control_epoch == session.control_epoch(),
                        "Cancel action lost order ID/epoch");
                require(session.cancel_manual_order(cancel.order_id).accepted,
                        "Cancel command was not admitted");
                session.poll();
                require(orders(session, first).size() == 2 && orders(session, other).size() == 3,
                        "Cancel removed a peer's order or another queue entry");
                const auto cancelled =
                    std::find_if(session.manual_orders().begin(), session.manual_orders().end(),
                                 [&](const auto &row) { return row.order_id == cancel.order_id; });
                require(cancelled != session.manual_orders().end() &&
                            cancelled->completion == ManualOrderCompletion::Cancelled && !no_control(session),
                        "Single-order cancellation was confused with releasing resident control");
                hud.frame();
                hud.scroll(true);
                hud.capture(output / (locale + "-control.png"));
                const auto paused_tick = session.ticks();
                require(ui.focus_for_test("orders-release"), "Release rejected keyboard focus");
                hud.key(SDLK_SPACE, true);
                const auto release = hud.action(UiActionType::ReleaseControl);
                for (int i = 0; i < 3; ++i)
                    hud.key(SDLK_SPACE, true, true);
                hud.key(SDLK_SPACE, false);
                require(ui.take_actions().empty() && view.paused() && session.ticks() == paused_tick,
                        "Held Space repeated the control action or unpaused the world");
                require(release.residents == hud.selected &&
                            session.release_control(release.residents).accepted,
                        "Release did not preserve the frozen group selection");
                session.poll();
                require(orders(session, first).empty() && no_control(session),
                        "Release retained unstarted orders or authority");
                if (locale == "uk") {
                    require(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
                    SDL_PumpEvents();
                    hud.capture(output / "uk-compact-control.png");
                    require(SDL_SetWindowSize(window.get(), 1440, 900), SDL_GetError());
                    SDL_PumpEvents();
                }
                // Replacement/reset must discard the pending form/action even if numeric resident IDs repeat.
                hud.click("orders-close");
                hud.frame();
                hud.map(move_sites[0]);
                require(orders(session, first).size() == 1,
                        "Shift release over UI was lost and kept appending");
                hud.frame();
                hud.click("action-gather");
                hud.frame();
                hud.scroll(true);
                hud.click("orders-release");
                ui.reset_world_state();
                view.reset_world_state();
                require(ui.take_actions().empty() && view.selected_ids().empty() &&
                            view.gather_completion() == GatherCompletion::OneDelivery,
                        "World reset retained control actions, selection or gather mode");
                hud.selected.clear();
                require(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
                SDL_PumpEvents();
                for (const auto &message : {"reconnecting", "resumed", "reconnect_failed", "recovery_incomplete"}) {
                    hud.status = localization.text(std::string("network.") + message);
                    hud.frame();
                    require(ui.element_rml_for_test("status").find(hud.status) != std::string::npos,
                            "Recovery status was not presented in the HUD");
                    hud.capture(output / (locale + "-" + message + ".png"));
                }
                hud.status.clear();
                require(SDL_SetWindowSize(window.get(), 1440, 900), SDL_GetError());
                SDL_PumpEvents();
                report["locales"].push_back({{"locale", locale},
                                             {"queue", true},
                                             {"cancel_one", true},
                                             {"release_group", true},
                                             {"paused_tick", paused_tick},
                                             {"keyboard_repeat_guard", true},
                                             {"recovery_statuses", 4},
                                             {"reset", true}});
                if (locale == "uk") {
                    physical_queue(ui, view, renderer, output);
                    network_controls(ui, view, renderer, output);
                }
            }
        }
        SDL_Quit();
        std::ofstream(output / "manual-order-ui.json") << report.dump(2) << '\n';
        std::cout << "Manual order UI: five locales, Shift queue, explicit gather, exact cancellation, "
                     "release, reset and two-client paused transfer passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

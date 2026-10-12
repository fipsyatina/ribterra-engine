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
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void write_fixture(const std::filesystem::path &destination, bool isolated_target) {
    ribterra::World world(70421, 1500);
    {
        auto state = world.export_state();
        const auto corridor_elevation = world.tile(24, 24).elevation;
        // This probe tests the path UI, independently of procedural lake placement.
        for (auto &chunk : state.chunks)
            if (chunk.key.layer == ribterra::WorldLayer::Surface)
                for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                    const int x = chunk.key.cx * ribterra::chunk_size + int(i % ribterra::chunk_size);
                    const int y = chunk.key.cy * ribterra::chunk_size + int(i / ribterra::chunk_size);
                    if ((y == 24 && x >= 24 && x <= 159) || (x == 159 && y >= 24 && y <= 159))
                        chunk.tiles[i] = {ribterra::Terrain::Grass, 0, true,
                                          ribterra::Biome::Meadow, corridor_elevation};
                }
        std::string error;
        require(world.import_state(std::move(state), error), "Path corridor fixture import failed");
    }
    if (isolated_target) {
        auto state = world.export_state();
        const auto set = [&](int x, int y, ribterra::Terrain terrain) {
            const ribterra::CellCoord cell{x, y, ribterra::WorldLayer::Surface};
            const auto key = ribterra::chunk_key(cell);
            auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                      [&](const auto &value) { return value.key == key; });
            require(chunk != state.chunks.end(), "Unreachable fixture chunk missing");
            chunk->tiles[ribterra::local_index(x, y)] = {terrain, 0, true};
        };
        set(72, 72, ribterra::Terrain::Grass);
        for (const auto &[dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
            set(72 + dx, 72 + dy, ribterra::Terrain::Water);
        std::string error;
        require(world.import_state(std::move(state), error), "Unreachable fixture import failed");
    }
    std::string error;
    require(world.save(destination, error), "Visual fixture save failed");
}
void same_view(const ribterra::ClientSession &left, const ribterra::ClientSession &right) {
    require(left.ticks() == right.ticks() && left.residents() == right.residents() &&
                left.buildings() == right.buildings() && left.resources() == right.resources(),
            "Pending save/resume changed visible entities or completion timing");
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto arguments = ribterra::utf8_arguments(argc, argv);
        if (arguments.size() < 2 || arguments.size() > 3)
            return 2;
        const bool software = arguments.size() == 3 && arguments[2] == "--software";
        if (arguments.size() == 3 && !software)
            return 2;
        const auto output = std::filesystem::absolute(path(arguments[1]));
        std::filesystem::create_directories(output);
        const auto normal = output / "normal-fixture.save";
        const auto unreachable = output / "unreachable-fixture.save";
        write_fixture(normal, false);
        write_fixture(unreachable, true);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra path queue visual probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "Window creation failed");
            const auto base = path(SDL_GetBasePath());
            ribterra::render::Renderer2D renderer(
                window.get(), base / "assets",
                {software ? ribterra::render::Backend::Software : ribterra::render::Backend::Direct3D12});
            const auto logical_size = renderer.fitted_logical_size();
            renderer.set_logical_size(logical_size);
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            ribterra::AssetCatalog assets(base / "assets");
            nlohmann::json cases = nlohmann::json::array();
            for (const auto &locale : ribterra::Localization::supported_locales()) {
                const auto settings = output / (locale + "-settings.json");
                std::ofstream(settings) << nlohmann::json{{"locale", locale}, {"ui_scale", 1.15}};
                ribterra::Localization localization(base / "assets/locales");
                ribterra::GameUi ui(renderer, assets, localization, settings, logical_size);
                ribterra::GameView view(renderer);
                view.set_logical_size(logical_size);
                ribterra::ClientSession session(1, "Path visual probe");
                std::string error;
                require(session.load(normal, error), "Initial fixture load failed");
                std::vector<int> selected;
                for (const auto &resident : session.residents())
                    selected.push_back(resident.id);
                require(selected.size() == 1500, "Visual scenario must include all 1500 residents");
                require(session.move(selected, 159, 159).pending, "Mass order was not queued");
                session.advance();
                const auto queued_reply = session.take_command_reply();
                require(queued_reply && queued_reply->status == ribterra::CommandReplyStatus::Queued,
                        "Mass order lacked its Queued reply");
                require(ui.format_command_reply(*queued_reply) == localization.text("status.order_queued"),
                        "Queued notice was not localized");
                require(std::all_of(session.residents().begin(), session.residents().end(),
                                    [](const auto &resident) {
                                        return resident.order_phase == ribterra::OrderPhase::PathPending;
                                    }),
                        "Mass order did not expose all pending residents");
                ui.show_world();
                require(ui.activate_for_test("hud-settings-toggle"),
                        "Path probe could not open the utility tray");
                const auto capture = [&](ribterra::ClientSession &current, std::string_view suffix,
                                         std::string_view status) {
                    ui.set_view_state(&current, selected, false, 1);
                    ui.update(current, status);
                    require(ui.visible_for_test("system-controls"),
                            "Path probe utility tray stayed hidden after RmlUi update");
                    ribterra::render::Frame2D frame;
                    view.compose(current, 0, frame, false);
                    ui.compose(frame);
                    int viewport_width{}, viewport_height{};
                    require(SDL_GetWindowSize(window.get(), &viewport_width, &viewport_height),
                            "Logical viewport size unavailable");
                    for (const auto *control : {"resident-order", "pause", "save", "load"}) {
                        require(ui.visible_for_test(control), "Path HUD control unexpectedly hidden");
                        const auto logical = ui.element_center_for_test(control);
                        const auto center = renderer.logical_to_window(ui.element_center_for_test(control));
                        if (!(center.x > 0 && center.x < viewport_width && center.y >= 0 &&
                              center.y < viewport_height))
                            throw std::runtime_error(
                                "Path HUD content lies outside the viewport: control=" +
                                std::string(control) + " logical=" + std::to_string(logical.x) + "," +
                                std::to_string(logical.y) + " window=" + std::to_string(center.x) + "," +
                                std::to_string(center.y) + " viewport=" +
                                std::to_string(viewport_width) + "x" + std::to_string(viewport_height));
                    }
                    renderer.render(frame, output / (locale + "-" + std::string(suffix) + ".png"));
                };
                capture(session, "pending", localization.text("status.order_queued"));
                require(!ui.element_rml_for_test("resident-order").empty(),
                        "Pending state is missing from the compact inspector");
                const auto saved = output / (locale + "-pending.save");
                require(session.save(saved, error), "Pending session save failed");
                ribterra::ClientSession restored(2, "Restored path probe");
                require(restored.load(saved, error), "Pending session restore failed");
                same_view(session, restored);
                capture(restored, "resumed", localization.text("status.order_queued"));
                int planning_ticks = 0;
                for (; planning_ticks < 1202 && std::any_of(
                                                    session.residents().begin(), session.residents().end(),
                                                    [](const auto &resident) {
                                                        return resident.order_phase ==
                                                               ribterra::OrderPhase::PathPending;
                                                    });
                     ++planning_ticks) {
                    session.advance();
                    restored.advance();
                    same_view(session, restored);
                }
                require(std::all_of(session.residents().begin(), session.residents().end(),
                                    [](const auto &resident) {
                                        return resident.order_phase == ribterra::OrderPhase::Active;
                                    }),
                        "Mass order did not apply after bounded planning");
                auto terminal = session.take_command_reply();
                require(terminal && terminal->status == ribterra::CommandReplyStatus::Applied,
                        "Mass order lacked its terminal Applied reply");
                require(terminal->residents.size() == selected.size() &&
                            std::all_of(terminal->residents.begin(), terminal->residents.end(),
                                        [](const auto &row) {
                                            return row.order_id != 0 &&
                                                   row.status == ribterra::ResidentCommandStatus::Applied;
                                        }),
                        "Accepted mass order did not preserve per-resident stable IDs");
                require(
                    ui.format_command_reply(*terminal) ==
                        localization.format("profession.results",
                                            {{"applied", selected.size()}, {"deferred", 0}, {"rejected", 0}}),
                    "Per-resident acceptance notice was not localized");

                require(session.load(unreachable, error), "Unreachable fixture load failed");
                require(session.move(selected, 72, 72).pending, "Unreachable order did not enter planning");
                terminal.reset();
                for (int tick = 0; tick < 1202; ++tick) {
                    session.advance();
                    while (auto reply = session.take_command_reply())
                        if (reply->status != ribterra::CommandReplyStatus::Queued)
                            terminal = std::move(reply);
                    if (std::none_of(session.residents().begin(), session.residents().end(),
                                     [](const auto &resident) {
                                         return resident.order_phase == ribterra::OrderPhase::PathPending;
                                     }))
                        break;
                }
                require(session.residents().front().order_phase == ribterra::OrderPhase::PathFailed &&
                            session.residents().front().path_failure == ribterra::PathFailure::Unreachable,
                        "Sealed target did not report an explicit Unreachable failure");
                require(terminal && terminal->status == ribterra::CommandReplyStatus::Applied &&
                            terminal->residents.size() == selected.size(),
                        "Unreachable execution must retain its original queue-acceptance receipt");
                require(!session.manual_orders().empty() &&
                            std::all_of(session.manual_orders().begin(), session.manual_orders().end(),
                                        [](const auto &order) {
                                            return order.phase == ribterra::ManualOrderPhase::Failed &&
                                                   order.failure == ribterra::PathFailure::Unreachable;
                                        }),
                        "Unreachable execution did not produce typed manual order failures");
                capture(session, "unreachable", localization.text("path_failure.unreachable"));
                require(ui.element_rml_for_test("resident-order-detail") ==
                            localization.text("path_failure.unreachable"),
                        "Failure detail did not use the active locale");
                if (locale == "uk") {
                    require(SDL_SetWindowSize(window.get(), 1100, 700), "Compact resize failed");
                    SDL_PumpEvents();
                    capture(session, "compact-unreachable", localization.text("path_failure.unreachable"));
                    require(SDL_SetWindowSize(window.get(), 1440, 900), "Window restore failed");
                    SDL_PumpEvents();
                }
                cases.push_back({{"locale", locale},
                                 {"population", selected.size()},
                                 {"planning_ticks_after_resume", planning_ticks},
                                 {"pending_save_resume", true},
                                 {"unreachable_reason", true}});
            }
            std::ofstream(output / "results.json") << cases.dump(2);
        }
        SDL_Quit();
        std::cout << "1500-resident pending, save/resume, terminal replies and five-locale HUD passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

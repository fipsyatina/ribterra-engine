#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ribterra;

namespace {
using Clock = std::chrono::steady_clock;

void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

struct Samples {
    std::vector<double> values;
    void add(Clock::duration duration) { values.push_back(milliseconds(duration)); }
};

nlohmann::json statistics(const Samples &samples) {
    require(!samples.values.empty(), "Cannot summarize an empty sample set");
    auto sorted = samples.values;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](double p) {
        const auto rank = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::ceil(p * static_cast<double>(sorted.size()))));
        return sorted[std::min(rank - 1, sorted.size() - 1)];
    };
    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    return {{"samples", sorted.size()},
            {"mean_ms", sum / static_cast<double>(sorted.size())},
            {"p50_ms", percentile(0.50)},
            {"p95_ms", percentile(0.95)},
            {"p99_ms", percentile(0.99)},
            {"max_ms", sorted.back()}};
}

template <class Predicate>
void pump_until(net::NetworkServer &server,
                const std::vector<std::unique_ptr<ClientSession>> &clients,
                std::string_view stage, Predicate predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (Clock::now() < deadline) {
        server.poll();
        for (const auto &client : clients)
            client->poll();
        if (predicate())
            return;
        SDL_Delay(1);
    }
    std::string details;
    for (const auto &client : clients)
        details += " player=" + std::to_string(client->player_id()) +
                   " ready=" + std::to_string(client->ready()) +
                   " tick=" + std::to_string(client->ticks());
    throw std::runtime_error("Timed out during " + std::string(stage) + details);
}

struct ClientPresentation {
    std::unique_ptr<GameView> view;
    render::Frame2D frame;
    Samples poll, view_compose, ui_update_compose, render;
    std::optional<SDL_FPoint> previous_position;
    std::size_t moving_frames{}, moving_between_snapshots{};
    float max_screen_step{};
    float max_position_lag_pixels{};
};

CellCoord nearest_resource(const ClientSession &client, CellCoord origin, Terrain terrain) {
    std::optional<CellCoord> result;
    long long best = std::numeric_limits<long long>::max();
    for (const auto &chunk : client.chunks()) {
        if (chunk.key.layer != WorldLayer::Surface)
            continue;
        for (int local_y = 0; local_y < chunk_size; ++local_y)
            for (int local_x = 0; local_x < chunk_size; ++local_x) {
                const auto &tile = chunk.tiles[static_cast<std::size_t>(local_y * chunk_size + local_x)];
                if (!tile.explored || tile.terrain != terrain || tile.amount <= 0)
                    continue;
                const CellCoord cell{chunk.key.cx * chunk_size + local_x,
                                     chunk.key.cy * chunk_size + local_y};
                const long long dx = cell.x - origin.x, dy = cell.y - origin.y;
                const long long distance = dx * dx + dy * dy;
                if (distance < best) {
                    best = distance;
                    result = cell;
                }
            }
    }
    require(result.has_value(), "No visible resource target for active scenario");
    return *result;
}

CellCoord nearby_walkable(const ClientSession &client, CellCoord origin) {
    std::optional<CellCoord> result;
    long long best_delta = std::numeric_limits<long long>::max();
    for (const auto &chunk : client.chunks()) {
        if (chunk.key.layer != WorldLayer::Surface)
            continue;
        for (int local_y = 0; local_y < chunk_size; ++local_y)
            for (int local_x = 0; local_x < chunk_size; ++local_x) {
                const auto &tile = chunk.tiles[static_cast<std::size_t>(local_y * chunk_size + local_x)];
                if (!tile.explored || tile.terrain == Terrain::Water ||
                    tile.terrain == Terrain::MineOre)
                    continue;
                const CellCoord cell{chunk.key.cx * chunk_size + local_x,
                                     chunk.key.cy * chunk_size + local_y};
                const long long dx = cell.x - origin.x, dy = cell.y - origin.y;
                const long long distance = dx * dx + dy * dy;
                if (distance < 36 || distance > 196)
                    continue;
                const long long delta = std::llabs(distance - 81);
                if (delta < best_delta) {
                    best_delta = delta;
                    result = cell;
                }
            }
    }
    require(result.has_value(), "No modest-distance visible move target for active scenario");
    return *result;
}

struct ScenarioStats {
    int queued{}, applied{}, rejected{}, invalid{};
    std::set<std::uint64_t> workload_requests;
    std::vector<std::string> terminal_reasons;
    std::array<CellCoord, 3> gather_targets{};
    CellCoord move_target{};
};

void drain_replies(ClientSession &client, ScenarioStats &stats) {
    while (auto reply = client.take_command_reply()) {
        switch (reply->status) {
        case CommandReplyStatus::Queued:
            if (stats.queued < 4 && stats.workload_requests.insert(reply->request_id).second) ++stats.queued;
            break;
        case CommandReplyStatus::Applied:
            if (!stats.workload_requests.contains(reply->request_id)) break;
            ++stats.applied;
            stats.terminal_reasons.push_back(reply->reason);
            break;
        case CommandReplyStatus::Rejected:
            if (!stats.workload_requests.contains(reply->request_id)) break;
            ++stats.rejected;
            stats.terminal_reasons.push_back(reply->reason);
            break;
        case CommandReplyStatus::Invalid:
            if (!stats.workload_requests.contains(reply->request_id)) break;
            ++stats.invalid;
            stats.terminal_reasons.push_back(reply->reason);
            break;
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc >= 2,
                "Usage: CoopPerformanceProbe <output-dir> [gpu|software] [1|2|6] "
                "[--no-render] [--frames N]");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);

        std::string backend_name = "software";
        int client_count = 2;
        int frame_count = 200;
        int speed = 1;
        bool no_render = false;
        int positional = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--no-render") {
                no_render = true;
            } else if (argument == "--frames") {
                require(i + 1 < argc, "--frames requires a value");
                frame_count = std::stoi(argv[++i]);
            } else if (argument == "--speed") {
                require(i + 1 < argc, "--speed requires a value");
                speed = std::stoi(argv[++i]);
            } else if (argument == "gpu" || argument == "software") {
                require(positional == 0, "Backend must precede client count");
                backend_name = argument;
                ++positional;
            } else {
                client_count = std::stoi(argument);
                positional = 2;
            }
        }
        require(client_count == 1 || client_count == 2 || client_count == 6,
                "Client count must be 1, 2, or 6");
        require(frame_count >= 180 && frame_count <= 600,
                "Frame count must be between 180 and 600");
        require(speed == 1 || speed == 2 || speed == 4, "Invalid simulation speed");

        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            constexpr int width = 1280;
            constexpr int height = 720;
            auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
                SDL_CreateWindow("Ribterra cooperative performance probe", width, height,
                                 SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());

            const auto base = std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(
                window.get(), base / "assets",
                {backend_name == "gpu" ? render::Backend::Direct3D12
                                       : render::Backend::Software});
            AssetCatalog assets(base / "assets");
            Localization localization(base / "assets/locales");

            net::HostConfig config;
            config.game.seed = 70421;
            config.game.population = 10;
            config.game.wildlife_enabled = true;
            config.game.lobby_enabled = true;
            config.game.max_players = static_cast<std::size_t>(std::max(2, client_count));
            config.transport.port = 29883;
            net::NetworkServer server(config);
            require(server.listen(), "Server listen failed: " + server.last_error());

            std::vector<std::unique_ptr<ClientSession>> clients;
            std::string error;
            const std::string endpoint = "127.0.0.1:" + std::to_string(server.port());
            for (int index = 0; index < client_count; ++index) {
                auto client =
                    std::make_unique<ClientSession>(70421, "Probe " + std::to_string(index + 1));
                require(client->connect_remote(endpoint, "", error), error);
                clients.push_back(std::move(client));
            }

            pump_until(server, clients, "join", [&] {
                return std::all_of(clients.begin(), clients.end(), [&](const auto &client) {
                    return client->ready() &&
                           client->lobby().players.size() == static_cast<std::size_t>(client_count);
                });
            });
            std::sort(clients.begin(), clients.end(), [](const auto &left, const auto &right) {
                return left->player_id() < right->player_id();
            });
            // This stress scenario explicitly exercises independent settlements.
            for (int i=0;i<client_count;++i)clients[i]->lobby_action(LobbyAction::Team,i+1);
            pump_until(server,clients,"independent teams",[&]{for(int i=0;i<client_count;++i)if(clients[i]->nation_id()!=NationId(i+1))return false;return true;});
            for (auto &client : clients)
                require(client->lobby_action(LobbyAction::Ready, 1).accepted,
                        "Ready action was not queued");
            pump_until(server, clients, "ready", [&] {
                return std::all_of(clients.begin(), clients.end(), [](const auto &client) {
                    return std::all_of(client->lobby().players.begin(),
                                       client->lobby().players.end(),
                                       [](const LobbyPlayer &player) { return player.ready; });
                });
            });
            require(clients.front()->lobby_action(LobbyAction::Start).accepted,
                    "Start action was not queued");
            pump_until(server, clients, "start", [&] {
                return std::all_of(clients.begin(), clients.end(), [](const auto &client) {
                    return client->lobby().started && !client->residents().empty();
                });
            });

            std::vector<NationId> nations;
            for (const auto &client : clients) {
                const auto nation = client->residents().front().nation;
                require(nation != NationId{}, "Client has no valid nation");
                nations.push_back(nation);
            }
            std::sort(nations.begin(), nations.end());
            require(std::adjacent_find(nations.begin(), nations.end()) == nations.end(),
                    "Explicit independent teams did not create separate nations");

            const auto settings = output / "ui.json";
            {
                std::ofstream file(settings);
                require(bool(file), "Could not write UI settings");
                file << R"({"locale":"uk","ui_scale":1})";
            }
            GameUi ui(renderer, assets, localization, settings,
                      render::Extent{static_cast<unsigned>(width),
                                     static_cast<unsigned>(height)});
            ui.show_world();
            std::vector<ClientPresentation> presentations(client_count);
            for (int index = 0; index < client_count; ++index) {
                auto &presentation = presentations[index];
                presentation.view = std::make_unique<GameView>(renderer);
                presentation.view->load_art(assets);
                presentation.view->focus_group(*clients[index],
                                                {clients[index]->residents().front().id});
                presentation.frame.logical_size = {static_cast<unsigned>(width),
                                                   static_cast<unsigned>(height)};

                presentation.view->compose(*clients[index],
                                           clients[index]->ticks() * tick_seconds,
                                           presentation.frame, false);
                ui.set_view_state(clients[index].get(), {}, false, 1);
                ui.update(clients[index].get());
                ui.compose(presentation.frame);
                renderer.render(presentation.frame,
                                output / ("client-" + std::to_string(index + 1) + "-camp.png"));
            }

            // Reach the same autonomous-work phase as a live settlement before measuring.
            for (int tick = 0; tick < 500; ++tick) {
                server.tick();
                server.poll();
                for (auto &client : clients)
                    client->poll();
            }
            pump_until(server, clients, "warmup synchronization", [&] {
                const auto tick = server.game().world().ticks();
                return std::all_of(clients.begin(), clients.end(), [&](const auto &client) {
                    return client->ticks() == tick;
                });
            });

            // Discard lobby command replies, then start bounded resource and path workloads.
            for (auto &client : clients)
                while (client->take_command_reply()) {
                }
            std::vector<ScenarioStats> scenarios(client_count);
            constexpr std::array<Terrain, 3> resource_terrains{
                Terrain::Forest, Terrain::Rock, Terrain::Grass};
            for (int index = 0; index < client_count; ++index) {
                auto &client = *clients[index];
                auto residents = client.residents();
                require(residents.size() >= 9,
                        "Active scenario requires at least nine residents per nation");
                std::sort(residents.begin(), residents.end(),
                          [](const Resident &left, const Resident &right) {
                              return left.id < right.id;
                          });
                const CellCoord origin{static_cast<int>(std::lround(residents.front().x)),
                                       static_cast<int>(std::lround(residents.front().y))};
                for (int resource = 0; resource < 3; ++resource) {
                    const auto target = nearest_resource(client, origin,
                                                         resource_terrains[resource]);
                    scenarios[index].gather_targets[resource] = target;
                    require(client
                                .gather({residents[resource * 2].id,
                                         residents[resource * 2 + 1].id},
                                        target.x, target.y, target.layer,
                                        ManualQueueMode::Replace,
                                        GatherCompletion::OneDelivery)
                                .accepted,
                            "Finite gather command was not queued");
                }
                scenarios[index].move_target = nearby_walkable(client, origin);
                require(client
                            .move({residents[6].id, residents[7].id, residents[8].id},
                                  scenarios[index].move_target.x,
                                  scenarios[index].move_target.y,
                                  scenarios[index].move_target.layer,
                                  ManualQueueMode::Replace)
                            .accepted,
                        "Move command was not queued");
            }

            const auto queued_deadline = Clock::now() + std::chrono::seconds(10);
            while (Clock::now() < queued_deadline) {
                server.poll();
                for (int index = 0; index < client_count; ++index) {
                    clients[index]->poll();
                    drain_replies(*clients[index], scenarios[index]);
                }
                if (std::all_of(scenarios.begin(), scenarios.end(),
                                [](const ScenarioStats &stats) { return stats.queued >= 4; }))
                    break;
                SDL_Delay(1);
            }
            require(std::all_of(scenarios.begin(), scenarios.end(),
                                [](const ScenarioStats &stats) { return stats.queued >= 4; }),
                    "Server did not acknowledge all active-scenario commands");
            require(std::none_of(scenarios.begin(), scenarios.end(), [](const ScenarioStats &stats) {
                        return stats.rejected != 0 || stats.invalid != 0;
                    }),
                    "Active scenario was rejected before measurement");

            Samples server_poll, server_tick, all_client_poll, all_view_compose,
                all_ui_update_compose, all_render, frame_cpu, frame_wall;
            std::vector<std::uint64_t> prior_client_ticks(client_count);
            for (int index = 0; index < client_count; ++index)
                prior_client_ticks[index] = clients[index]->ticks();
            const auto initial_server_tick = server.game().world().ticks();
            require(clients.front()->lobby_action(LobbyAction::Speed, speed).accepted, "Speed rejected");
            pump_until(server, clients, "speed", [&] {
                return std::all_of(clients.begin(), clients.end(), [&](const auto &c) { return c->lobby().speed == speed; });
            });
            const auto benchmark_start = Clock::now();
            auto next_frame = benchmark_start;
            auto previous_frame = benchmark_start;
            double accumulator = 0;

            for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
                const auto wall_start = Clock::now();
                const auto cpu_start = wall_start;

                auto start = Clock::now();
                server.poll();
                auto end = Clock::now();
                auto poll_duration = end - start;

                start = Clock::now();
                for (int index = 0; index < client_count; ++index) {
                    const auto client_start = Clock::now();
                    clients[index]->poll();
                    const auto client_end = Clock::now();
                    presentations[index].poll.add(client_end - client_start);
                }
                end = Clock::now();
                all_client_poll.add(end - start);

                start = Clock::now();
                accumulator += std::chrono::duration<double>(wall_start - previous_frame).count();
                previous_frame = wall_start;
                while (accumulator >= tick_seconds) {
                    server.tick();
                    accumulator -= tick_seconds;
                }
                end = Clock::now();
                server_tick.add(end - start);

                start = Clock::now();
                server.poll();
                end = Clock::now();
                poll_duration += end - start;
                server_poll.add(poll_duration);

                start = Clock::now();
                for (int index = 0; index < client_count; ++index) {
                    const auto client_start = Clock::now();
                    clients[index]->poll();
                    const auto client_end = Clock::now();
                    presentations[index].poll.values.back() +=
                        milliseconds(client_end - client_start);
                    drain_replies(*clients[index], scenarios[index]);
                }
                end = Clock::now();
                all_client_poll.values.back() += milliseconds(end - start);

                start = Clock::now();
                for (int index = 0; index < client_count; ++index) {
                    auto &presentation = presentations[index];
                    presentation.frame.clear_commands();
                    presentation.frame.logical_size = {static_cast<unsigned>(width),
                                                       static_cast<unsigned>(height)};
                    const auto item_start = Clock::now();
                    presentation.view->compose(*clients[index],
                                               std::chrono::duration<double>(Clock::now() - benchmark_start).count(),
                                               presentation.frame, false);
                    const auto point = presentation.view->resident_screen(*clients[index], clients[index]->residents()[6].id);
                    if (point && presentation.previous_position) {
                        const float step = std::hypot(point->x - presentation.previous_position->x,
                                                      point->y - presentation.previous_position->y);
                        if (step > .01f) {
                            ++presentation.moving_frames;
                            if (clients[index]->ticks() == prior_client_ticks[index]) ++presentation.moving_between_snapshots;
                        }
                        presentation.max_screen_step = std::max(presentation.max_screen_step, step);
                    }
                    presentation.previous_position = point;
                    if (point && frame_index > 60) {
                        const auto &resident = clients[index]->residents()[6];
                        const auto authority = presentation.view->tile_screen(resident.x, resident.y);
                        presentation.max_position_lag_pixels = std::max(presentation.max_position_lag_pixels,
                            std::hypot(point->x - authority.x, point->y - authority.y));
                        require(presentation.max_position_lag_pixels < 100 * speed,
                                "Presentation accumulated stale movement history");
                    }
                    const auto item_end = Clock::now();
                    presentation.view_compose.add(item_end - item_start);
                }
                end = Clock::now();
                all_view_compose.add(end - start);

                start = Clock::now();
                for (int index = 0; index < client_count; ++index) {
                    auto &presentation = presentations[index];
                    const auto item_start = Clock::now();
                    ui.set_view_state(clients[index].get(), {}, false, 1);
                    ui.update(clients[index].get());
                    ui.compose(presentation.frame);
                    const auto item_end = Clock::now();
                    presentation.ui_update_compose.add(item_end - item_start);
                }
                end = Clock::now();
                all_ui_update_compose.add(end - start);

                start = Clock::now();
                if (!no_render) {
                    for (int index = 0; index < client_count; ++index) {
                        const auto item_start = Clock::now();
                        renderer.render(presentations[index].frame);
                        const auto item_end = Clock::now();
                        presentations[index].render.add(item_end - item_start);
                    }
                } else {
                    for (auto &presentation : presentations)
                        presentation.render.values.push_back(0.0);
                }
                end = Clock::now();
                all_render.add(end - start);
                frame_cpu.add(Clock::now() - cpu_start);

                for (int index = 0; index < client_count; ++index) {
                    require(clients[index]->ticks() >= prior_client_ticks[index],
                            "Client tick moved backwards");
                    prior_client_ticks[index] = clients[index]->ticks();
                }

                next_frame += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next_frame);
                frame_wall.add(Clock::now() - wall_start);
            }

            const auto benchmark_end = Clock::now();
            require(clients.front()->lobby_action(LobbyAction::Pause, 1).accepted, "Pause rejected");
            pump_until(server, clients, "final synchronization", [&] {
                const auto tick = server.game().world().ticks();
                return std::all_of(clients.begin(), clients.end(), [&](const auto &client) {
                    return client->lobby().paused && client->ticks() == tick;
                });
            });
            const auto final_server_tick = server.game().world().ticks();
            for (int index = 0; index < client_count; ++index)
                drain_replies(*clients[index], scenarios[index]);
            for (int index = 0; index < client_count; ++index) {
                require(scenarios[index].applied == 4 && scenarios[index].rejected == 0 &&
                            scenarios[index].invalid == 0,
                        "Active scenario did not produce four successful terminal replies for client " +
                            std::to_string(index + 1));
                require(std::none_of(clients[index]->manual_orders().begin(),
                                     clients[index]->manual_orders().end(),
                                     [](const ManualOrderView &order) {
                                         return order.phase == ManualOrderPhase::Failed ||
                                                order.phase == ManualOrderPhase::Cancelled;
                                     }),
                        "Active scenario produced a failed or cancelled manual order");
            }
            require(final_server_tick > initial_server_tick, "Server tick did not advance");
            const double elapsed_seconds =
                std::chrono::duration<double>(benchmark_end - benchmark_start).count();

            nlohmann::json result = {
                {"schema", 2},
                {"target_render_fps", 60},
                {"speed", speed},
                {"seed", config.game.seed},
                {"population_per_nation", config.game.population},
                {"wildlife_enabled", config.game.wildlife_enabled},
                {"warmup_ticks", 500},
                {"commands_per_client", 4},
                {"gather_residents_per_resource", 2},
                {"move_residents_per_client", 3},
                {"clients", client_count},
                {"frames", frame_count},
                {"backend_requested", backend_name},
                {"backend", renderer.device_info().backend},
                {"device", renderer.device_info().device},
                {"no_render", no_render},
                {"elapsed_seconds", elapsed_seconds},
                {"ticks_advanced", final_server_tick - initial_server_tick},
                {"simulation_ticks_per_second",
                 double(final_server_tick - initial_server_tick) / elapsed_seconds},
                {"frames_per_second", double(frame_count) / elapsed_seconds},
                {"aggregate",
                 {{"server_poll", statistics(server_poll)},
                  {"server_tick", statistics(server_tick)},
                  {"client_poll", statistics(all_client_poll)},
                  {"view_compose", statistics(all_view_compose)},
                  {"ui_update_compose", statistics(all_ui_update_compose)},
                  {"render", statistics(all_render)},
                  {"frame_cpu", statistics(frame_cpu)},
                  {"frame_wall", statistics(frame_wall)}}},
                {"per_client", nlohmann::json::array()}};
            for (int index = 0; index < client_count; ++index) {
                const auto &presentation = presentations[index];
                nlohmann::json gather_targets = nlohmann::json::array();
                for (const auto &target : scenarios[index].gather_targets)
                    gather_targets.push_back({{"x", target.x}, {"y", target.y}});
                nlohmann::json manual_orders = nlohmann::json::array();
                for (const auto &order : clients[index]->manual_orders())
                    manual_orders.push_back(
                        {{"resident_id", order.resident_id},
                         {"kind", static_cast<int>(order.kind)},
                         {"phase", static_cast<int>(order.phase)},
                         {"completion", static_cast<int>(order.completion)},
                         {"failure", static_cast<int>(order.failure)}});
                result["per_client"].push_back(
                    {{"index", index + 1},
                     {"moving_frames", presentation.moving_frames},
                     {"moving_between_snapshots", presentation.moving_between_snapshots},
                     {"max_screen_step", presentation.max_screen_step},
                     {"max_position_lag_pixels", presentation.max_position_lag_pixels},
                     {"player_id", clients[index]->player_id()},
                     {"nation", clients[index]->residents().front().nation},
                     {"poll", statistics(presentation.poll)},
                     {"view_compose", statistics(presentation.view_compose)},
                     {"ui_update_compose", statistics(presentation.ui_update_compose)},
                     {"render", statistics(presentation.render)},
                     {"scenario",
                      {{"gather_targets", std::move(gather_targets)},
                       {"move_target",
                        {{"x", scenarios[index].move_target.x},
                         {"y", scenarios[index].move_target.y}}},
                       {"command_replies",
                        {{"queued", scenarios[index].queued},
                         {"applied", scenarios[index].applied},
                         {"rejected", scenarios[index].rejected},
                         {"invalid", scenarios[index].invalid},
                         {"terminal_reasons", scenarios[index].terminal_reasons}}},
                       {"manual_orders", std::move(manual_orders)}}}});
            }
            std::ofstream report(output / "performance.json");
            require(bool(report), "Could not write performance report");
            report << result.dump(2) << '\n';
            std::cout << result.dump(2) << '\n';
            server.shutdown();
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

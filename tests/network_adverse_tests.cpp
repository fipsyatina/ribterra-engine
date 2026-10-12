#include "ribterra/network_session.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace {
using namespace ribterra;
using namespace ribterra::net;
using namespace std::chrono_literals;
constexpr auto test_password = "Ribterra isolated impairment fixture";
const auto run_started = std::chrono::steady_clock::now();
void trace_stage(const std::string &name) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - run_started).count();
    std::cerr << "Adverse stage at " << elapsed << " ms: " << name << '\n';
}
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool terminal(CommandReplyStatus status) {
    return status == CommandReplyStatus::Applied || status == CommandReplyStatus::Rejected;
}
bool same_view(const ViewSnapshot &a, const ViewSnapshot &b, const WorldId &id) {
    if (a.revision != b.revision || a.tick != b.tick)
        return false;
    const auto left = split_snapshot(make_snapshot(a, id));
    const auto right = split_snapshot(make_snapshot(b, id));
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (encode({1, left[i]}) != encode({1, right[i]}))
            return false;
    return true;
}
void install_wood_fixture(WorldState &state) {
    const CellCoord target{22, 19};
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    require(chunk != state.chunks.end(), "Wood fixture chunk is missing");
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
}
struct Scenario {
    NetworkServer server;
    NetworkClient first, second;
    std::filesystem::path directory;
    std::map<std::pair<PlayerId, std::uint64_t>, CommandReply> replies;
    std::vector<std::string> stages;
    bool second_online{true};
    explicit Scenario(std::uint16_t port, std::filesystem::path root)
        : server([&] {
              HostConfig config;
              config.transport.port = port;
              config.game.population = 3;
              config.password = test_password;
              // Controlled network fixture: reveal the generated world to force
              // full multipart traffic. Gameplay commands below use normal APIs.
              World fixture(config.game.seed, config.game.population);
              auto state = fixture.export_state();
              install_wood_fixture(state);
              for (auto &chunk : state.chunks)
                  for (auto &tile : chunk.tiles)
                      tile.explored = true;
              std::string error;
              require(fixture.import_state(std::move(state), error), error);
              config.save_path = root / "network-world.save";
              require(fixture.save(config.save_path, error), error);
              return config;
          }()),
          first([] { ClientConfig c; c.name = "Adverse A"; c.password = test_password; return c; }()),
          second([] { ClientConfig c; c.name = "Adverse B"; c.password = test_password; c.auto_reconnect = true; return c; }()),
          directory(std::move(root)) {}

    void service(bool tick = false) {
        server.poll();
        first.poll();
        if (second_online)
            second.poll();
        if (tick)
            server.tick();
        for (auto *client : {&first, &second})
            for (const auto &reply : client->take_replies())
                if (terminal(reply.status)) {
                    const auto key = std::pair{reply.author, reply.request_id};
                    require(!replies.contains(key), "Duplicate terminal reply surfaced to client");
                    replies.emplace(key, reply);
                }
    }
    template <class Predicate>
    void until(const std::string &name, Predicate predicate, bool tick = false,
               std::chrono::seconds timeout = 12s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            service(tick);
            if (predicate()) {
                stages.push_back(name);
                trace_stage(name);
                return;
            }
            std::this_thread::sleep_for(3ms);
        }
        throw std::runtime_error(name + " timeout; first=" + first.last_error() +
                                 "; second=" + second.last_error() + "; server=" + server.last_error());
    }
    void converge(const std::string &name) {
        until(name, [&] {
            return first.ready() && (!second_online || second.ready()) &&
                   same_view(first.view(), server.game().snapshot(first.player()), first.world_id()) &&
                   (!second_online || same_view(second.view(), server.game().snapshot(second.player()),
                                               second.world_id()));
        });
    }
    CommandReply command(NetworkClient &client, GameCommand value, const std::string &name) {
        const auto player = client.player();
        const auto request = client.command(std::move(value));
        require(request != 0, name + " was not sent");
        until(name, [&] { return replies.contains({player, request}); });
        converge(name + " projection");
        return replies.at({player, request});
    }
    void cut(bool active) {
        if (active) {
            std::ofstream marker(directory / "link.cut");
            marker << "cut";
        } else {
            std::filesystem::remove(directory / "link.cut");
        }
        until(active ? "relay cut acknowledged" : "relay restored", [&] {
            // Windows may transiently deny a status query while the relay
            // removes its marker. Retry until the same existing deadline.
            std::error_code error;
            const bool exists = std::filesystem::exists(directory / "link.cut.active", error);
            return !error && exists == active;
        });
    }
};
GameCommand move(int resident, int x, int y, ManualQueueMode mode = ManualQueueMode::Replace) {
    GameCommand command;
    command.type = CommandType::Move;
    command.residents = {resident};
    command.x = x;
    command.y = y;
    command.manual_queue_mode = mode;
    return command;
}
}

int main(int argc, char **argv) {
    try {
        require(argc == 5, "Expected server port, two relay endpoints and scratch directory");
        const auto port = std::stoi(argv[1]);
        require(port > 0 && port <= 65535, "Invalid server port");
        trace_stage("constructing full-world fixture");
        Scenario test(static_cast<std::uint16_t>(port), argv[4]);
        trace_stage("full-world fixture ready");
        require(test.server.listen(), test.server.last_error());
        require(test.first.connect(argv[2]) && test.second.connect(argv[3]), "Connect failed");
        test.converge("initial multipart snapshots");
        const auto initial_parts = split_snapshot(make_snapshot(test.first.view(), test.first.world_id())).size();
        require(initial_parts > 1, "Scenario did not exercise multipart snapshots");
        const auto first_id = test.first.player(), second_id = test.second.player();
        const auto first_request = test.first.command(move(1, 26, 25));
        const auto second_request = test.second.command(move(1, 26, 25));
        require(first_request && second_request, "Conflicting commands not sent");
        test.until("simultaneous conflicting orders", [&] {
            return test.replies.contains({first_id, first_request}) &&
                   test.replies.contains({second_id, second_request});
        });
        const auto &a = test.replies.at({first_id, first_request});
        const auto &b = test.replies.at({second_id, second_request});
        require((a.status == CommandReplyStatus::Applied) != (b.status == CommandReplyStatus::Applied),
                "Conflict did not have exactly one winner");
        const auto &loser = a.status == CommandReplyStatus::Applied ? b : a;
        require(loser.control_rejections == std::vector<ResidentControlRejection>{{1, ControlFailure::HeldByAnother}},
                "Conflict omitted its actor or typed reason");
        test.converge("conflict projections");
        if (a.status == CommandReplyStatus::Applied) {
            GameCommand transfer;
            transfer.type = CommandType::TransferControl;
            transfer.residents = {1};
            transfer.target_player = second_id;
            require(test.command(test.first, transfer, "transfer while paused").status == CommandReplyStatus::Applied,
                    "Transfer failed");
        }
        require(test.server.game().world().ticks() == 0, "Paused commands advanced simulation");
        require(test.command(test.first, move(1, 27, 25), "former controller rejected").status == CommandReplyStatus::Rejected,
                "Former controller retained authority");
        const auto appended = test.command(test.second, move(1, 27, 25, ManualQueueMode::Append), "append queue");
        require(appended.status == CommandReplyStatus::Applied && appended.residents.front().order_id != 0,
                "Append did not retain a stable order ID");
        GameCommand cancel;
        cancel.type = CommandType::CancelManualOrder;
        cancel.manual_order_id = appended.residents.front().order_id;
        require(test.command(test.second, cancel, "cancel exact tail").status == CommandReplyStatus::Applied,
                "Tail cancellation failed");

        // Cut the socket below GNS until its live connection fails. Unlike a
        // graceful close, this cannot flush its reliable queue first. The real
        // client's automatic recovery must retain identity and apply once.
        test.cut(true);
        const auto before_orders = test.server.game().world().manual_orders().size();
        const auto uncertain = test.second.command(move(2, 27, 25));
        require(uncertain != 0, "Uncertain command was not accepted by transport");
        const auto outage_end = std::chrono::steady_clock::now() + 1500ms;
        while (std::chrono::steady_clock::now() < outage_end) {
            test.service();
            std::this_thread::sleep_for(3ms);
        }
        require(test.server.game().world().manual_orders().size() == before_orders,
                "Outage did not isolate the queued command");
        test.until("transport detects the outage", [&] {
            return !test.second.ready() && test.second.reconnecting();
        }, false, 25s);
        test.cut(false);
        test.until("uncertain command recovered after automatic reconnect", [&] {
            return test.second.ready() && test.replies.contains({second_id, uncertain});
        }, false, 20s);
        require(test.second.player() == second_id, "Reconnect changed player identity");
        require(test.replies.at({second_id, uncertain}).status == CommandReplyStatus::Applied,
                "Recovered command was not applied");
        require(test.server.game().world().manual_orders().size() == before_orders + 1,
                "Recovered command executed more than once");
        test.converge("resumed queue and controller");

        GameCommand gather;
        gather.type = CommandType::Gather;
        gather.residents = {1};
        gather.x = 22;
        gather.y = 19;
        gather.gather_completion = GatherCompletion::OneDelivery;
        require(test.command(test.second, gather, "physical gather").status == CommandReplyStatus::Applied,
                "Gather was not accepted");
        test.until("physical cargo reached", [&] { return test.server.game().world().inventory().carried.wood == 3; }, true);
        while (test.server.game().world().ticks() % 4 != 0)
            test.service(true);
        require(test.server.game().world().inventory().carried.wood == 3, "Cargo boundary was not observable");
        test.converge("cargo visible to both clients");
        GameCommand release;
        release.type = CommandType::ReleaseControl;
        release.residents = {1};
        require(test.command(test.second, release, "release under cargo").status == CommandReplyStatus::Applied,
                "Release under cargo failed");
        require(test.server.game().world().inventory().carried.wood == 3, "Release lost physical cargo");
        test.until("physical cargo delivered once", [&] {
            const auto inventory = test.server.game().world().inventory();
            return inventory.carried.wood == 0 && inventory.stored.wood == 83;
        }, true);
        // Reach a normal snapshot boundary, then compare the entire visible world.
        while (test.server.game().world().ticks() % 4 != 0)
            test.service(true);
        test.converge("final world convergence");
        const auto held = test.command(test.second, move(2, 28, 25), "claim before grace expiry");
        require(held.status == CommandReplyStatus::Applied, "Could not claim resident before disconnect");
        const auto paused_tick = test.server.game().world().ticks();
        test.second.disconnect();
        test.second_online = false;
        test.until("disconnected controller visible", [&] {
            const auto &controls = test.first.view().resident_controls;
            return std::any_of(controls.begin(), controls.end(), [&](const auto &entry) {
                return entry.resident_id == 2 && entry.holder_id == second_id && entry.disconnected_grace;
            });
        });
        test.until("real thirty-second grace expires while paused", [&] {
            const auto &controls = test.first.view().resident_controls;
            return std::any_of(controls.begin(), controls.end(), [](const auto &entry) {
                return entry.resident_id == 2 && entry.holder_id == 0;
            });
        }, false, 38s);
        require(test.server.game().world().ticks() == paused_tick, "Lease expiry advanced simulation");
        require(test.second.connect(argv[3]), "Reconnect after grace failed");
        test.second_online = true;
        test.converge("expired controller reconnects without old rights");
        require(test.second.player() == second_id, "Expired lease unexpectedly changed the session identity");
        require(test.command(test.first, move(2, 27, 25), "new controller after expiry").status == CommandReplyStatus::Applied,
                "Expired lease blocked a new controller");
        const auto old_control = test.command(test.second, move(2, 26, 25), "old controller remains rejected");
        require(old_control.control_rejections == std::vector<ResidentControlRejection>{{2, ControlFailure::HeldByAnother}},
                "Reconnect restored expired authority");
        std::cout << nlohmann::json{{"passed", true}, {"stages", test.stages},
            {"initial_snapshot_parts", initial_parts}, {"terminal_replies", test.replies.size()},
            {"final_tick", test.server.game().world().ticks()},
            {"stored_wood", test.server.game().world().inventory().stored.wood},
            {"cargo_wood", test.server.game().world().inventory().carried.wood}}.dump() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "NetworkAdverseTests: " << error.what() << '\n';
        return 1;
    }
}

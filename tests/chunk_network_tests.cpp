#include "existing_passage_fixture.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/world_state.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {
using namespace ribterra;
using namespace ribterra::net;
using namespace std::chrono_literals;
int failures{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " #x " failed\n";                                          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct TempWorld {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("ribterra-chunks-" + secure_token());
    TempWorld() { std::filesystem::create_directory(directory); }
    ~TempWorld() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

std::unique_ptr<NetworkServer> listen(HostConfig config) {
    config.transport.bind_address = "127.0.0.1";
    for (std::uint16_t port = 27420; port < 27520; ++port) {
        config.transport.port = port;
        auto server = std::make_unique<NetworkServer>(config);
        if (server->listen())
            return server;
    }
    throw std::runtime_error("No test port available");
}

template <class Predicate>
bool pump(NetworkServer &server, std::initializer_list<NetworkClient *> clients, Predicate done,
          bool tick = false) {
    const auto deadline = std::chrono::steady_clock::now() + 28s;
    while (std::chrono::steady_clock::now() < deadline) {
        server.poll();
        if (tick)
            server.tick();
        for (auto *client : clients)
            client->poll();
        if (done())
            return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

void full_world_over_real_transport() {
    TempWorld temporary;
    const auto path = temporary.directory / "world.save";
    World original(612, 10);
    existing_passage(original);
    auto state = original.export_state();
    for (auto &chunk : state.chunks)
        for (auto &tile : chunk.tiles)
            tile.explored = true;
    // A second nation's person exists in authority, but not in this co-op view.
    auto foreign = state.residents.front();
    foreign.id = 11;
    foreign.nation = 2;
    state.residents.push_back(foreign);
    auto route = state.routes.front();
    route.resident_id = foreign.id;
    state.routes.push_back(route);
    // A physical foreign stack must not change our inventory projection or add a payload location.
    const auto own_inventory = original.inventory(1);
    state.item_stacks.push_back({state.next_item_stack_id++,
                                 ItemKind::Stone,
                                 99,
                                 2,
                                 {ItemLocationKind::Ground, {-96, -96, WorldLayer::Mine}, 0}});
    std::string error;
    CHECK(original.import_state(std::move(state), error));
    CHECK(original.save(path, error));
    HostConfig config;
    auto host_clock_offset = 0s;
    config.game.clock = [&] { return std::chrono::steady_clock::now() + host_clock_offset; };
    config.save_path = path;
    auto server = listen(config);
    const auto address = "127.0.0.1:" + std::to_string(server->port());
    ClientConfig first_config;
    first_config.name = "First";
    NetworkClient first(first_config);
    CHECK(first.connect(address));
    CHECK(pump(*server, {&first}, [&] { return first.ready(); }));
    if (!first.ready())
        throw std::runtime_error(first.last_error() + " / " + server->last_error());
    CHECK(first.view().chunks.size() == world_chunk_count);
    CHECK(first.view().residents.size() == 10);
    CHECK(first.view().inventory == own_inventory);
    CHECK(first.view().resident_logistics.empty());
    CHECK(first.view().construction_sites.empty());
    const auto authoritative = server->game().snapshot(first.player());
    const auto parts = split_snapshot(make_snapshot(authoritative, first.world_id()));
    CHECK(parts.size() > 4);
    CHECK(first.view().chunks == authoritative.chunks);
    CHECK(first.view().portals == original.portals());
    CHECK(first.view().tile(-96, -96, WorldLayer::Mine) ==
          original.tile(-96, -96, WorldLayer::Mine));
    CHECK(first.view().tile(159, 159) == original.tile(159, 159));

    ClientConfig late_config;
    late_config.name = "Late";
    NetworkClient late(late_config);
    CHECK(late.connect(address));
    CHECK(pump(*server, {&first, &late}, [&] { return late.ready(); }));
    CHECK(late.view().chunks == first.view().chunks);
    const auto old_revision = first.view().revision;
    GameCommand move;
    move.type = CommandType::Move;
    move.residents = {1};
    move.x = 28;
    move.y = 24;
    move.layer = WorldLayer::Mine;
    const auto request = first.command(move);
    CHECK(request != 0);
    bool applied = false;
    CHECK(pump(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies())
                if (reply.request_id == request && reply.status == CommandReplyStatus::Applied)
                    applied = true;
            return applied && first.view().residents.front().layer == WorldLayer::Mine &&
                   late.view().residents.front().layer == WorldLayer::Mine;
        },
        true));
    CHECK(first.view().revision > old_revision);

    move.residents = {11};
    const auto forbidden = first.command(move);
    bool denied = false;
    CHECK(pump(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies())
                if (reply.request_id == forbidden && reply.status == CommandReplyStatus::Rejected)
                    denied = true;
            return denied;
        },
        true));
    const auto foreign_after = std::find_if(server->game().world().residents().begin(),
                                            server->game().world().residents().end(),
                                            [](const auto &resident) { return resident.id == 11; });
    CHECK(foreign_after != server->game().world().residents().end());
    CHECK(foreign_after->layer == WorldLayer::Surface);

    const auto player = late.player();
    late.disconnect();
    CHECK(pump(*server, {&first}, [&] { return !server->game().connected(player); }));
    CHECK(late.connect(address));
    CHECK(late.view().chunks.empty());
    CHECK(pump(*server, {&first, &late}, [&] { return late.ready(); }));
    CHECK(late.player() == player);
    CHECK(late.view().chunks == server->game().snapshot(player).chunks);
    CHECK(server->save());
    World resumed(1, 0);
    CHECK(resumed.load(path, error));
    CHECK(resumed.checksum() == server->game().world().checksum());

    // Discard all but the first part, force the client assembly timeout, and
    // recover through the real server's resync state without reconnecting.
    NetTransport raw;
    const auto connection = raw.connect(address);
    CHECK(connection != 0);
    std::optional<Welcome> welcome;
    std::uint64_t sequence{};
    CHECK(pump(*server, {&first, &late}, [&] {
        for (const auto &event : raw.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            auto packet = decode(event.bytes, error);
            if (!packet)
                continue;
            if (auto *challenge = std::get_if<Challenge>(&packet->payload)) {
                CHECK(raw.send(
                    connection,
                    encode({++sequence, Hello{"Early ack", challenge->versions, {}, {}}})));
            } else if (auto *joined = std::get_if<Welcome>(&packet->payload))
                welcome = *joined;
        }
        return welcome.has_value();
    }));
    if (welcome) {
        CHECK(raw.send(connection, encode({++sequence, SnapshotAck{0, true}})));
        const auto revision = server->game().snapshot(welcome->player).revision;
        auto client_clock = std::chrono::steady_clock::time_point{};
        SnapshotAssembler assembler([&] { return client_clock; });
        ViewSnapshot raw_view;
        WorldId raw_id = welcome->world_id;
        std::size_t received = 0;
        std::vector<Snapshot> delayed_parts;
        CHECK(pump(*server, {&first, &late}, [&] {
            for (const auto &event : raw.poll()) {
                if (event.type != Event::Type::Message)
                    continue;
                auto packet = decode(event.bytes, error);
                if (packet && std::holds_alternative<Snapshot>(packet->payload)) {
                    const auto &part = std::get<Snapshot>(packet->payload);
                    if (part.part_index != 0)
                        delayed_parts.push_back(part);
                    if (++received == 1)
                        CHECK(assembler.accept(raw_view, raw_id, part, error) ==
                              AssemblyResult::Pending);
                }
            }
            return received == parts.size();
        }));
        CHECK(raw_view.chunks.empty());
        client_clock += 11s;
        CHECK(assembler.expired());
        assembler.reset();
        // Model original packets arriving after timeout but before replay.
        // Part zero of the replay can now finish the view while the server
        // is still queuing repeated parts, so its valid ACK must be accepted.
        for (const auto &part : delayed_parts)
            CHECK(assembler.accept(raw_view, raw_id, part, error) == AssemblyResult::Pending);
        host_clock_offset += 2s;
        CHECK(raw.send(connection, encode({++sequence, SnapshotAck{0, true}})));
        bool recovered = false;
        CHECK(pump(*server, {&first, &late}, [&] {
            for (const auto &event : raw.poll()) {
                if (event.type != Event::Type::Message)
                    continue;
                auto packet = decode(event.bytes, error);
                if (packet && std::holds_alternative<Snapshot>(packet->payload) && !recovered) {
                    const auto result = assembler.accept(
                        raw_view, raw_id, std::get<Snapshot>(packet->payload), error);
                    CHECK(result != AssemblyResult::Rejected);
                    recovered |= result == AssemblyResult::Applied;
                }
            }
            return recovered;
        }));
        CHECK(raw_view.chunks == server->game().snapshot(welcome->player).chunks);
        CHECK(raw.send(connection, encode({++sequence, SnapshotAck{revision, false}})));
        CHECK(raw.send(connection, encode({++sequence, Ping{42}})));
        CHECK(pump(*server, {&first, &late}, [&] {
            for (const auto &event : raw.poll()) {
                if (event.type != Event::Type::Message)
                    continue;
                auto packet = decode(event.bytes, error);
                if (packet && std::holds_alternative<Ping>(packet->payload))
                    return true;
            }
            return false;
        }));
        // Request another full snapshot, then claim receipt before all parts
        // were queued. The command lane preserves this malicious ACK order.
        host_clock_offset += 2s;
        CHECK(raw.send(connection, encode({++sequence, SnapshotAck{revision, true}})));
        CHECK(raw.send(connection, encode({++sequence, SnapshotAck{revision, false}})));
        CHECK(pump(*server, {&first, &late},
                   [&] { return !server->game().connected(welcome->player); }));
        CHECK(first.ready() && late.ready());
    }
    server->shutdown();
    std::cout << "Full expanded-world transfer: " << parts.size()
              << " parts; late join, portal command, rights, reconnect, save, timeout resync and "
                 "premature ACK checked.\n";
}
} // namespace

int main() {
    try {
        full_world_over_real_transport();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}

#include "ribterra/network_session.hpp"
#include "ribterra/world_state.hpp"
#include "weather_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;
using ribterra::GameServer;
using ribterra::ServerConfig;
using ribterra::ViewSnapshot;
using ribterra::net::Challenge;
using ribterra::net::ClientConfig;
using ribterra::net::Event;
using ribterra::net::Hello;
using ribterra::net::NetTransport;
using ribterra::net::NetworkClient;
using ribterra::net::Snapshot;
using ribterra::net::SnapshotAck;
using ribterra::net::Welcome;
using ribterra::net::WorldId;

int failures = 0;
#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression     \
                      << '\n';                                                               \
            ++failures;                                                                      \
        }                                                                                    \
    } while (false)

constexpr auto operation_timeout = 3s;

std::optional<std::string> unused_loopback_endpoint() {
    NetTransport reservation;
    if (!reservation.listen("127.0.0.1", 0)) {
        for (std::uint16_t port = 28220; port < 28320; ++port)
            if (reservation.listen("127.0.0.1", port))
                break;
    }
    if (reservation.local_port() == 0)
        return {};
    const std::string endpoint =
        "127.0.0.1:" + std::to_string(reservation.local_port());
    reservation.shutdown();
    return endpoint;
}

struct FakeServer {
    NetTransport transport;
    std::uint32_t connection{};
    std::uint64_t sequence{};

    bool listen() {
        for (std::uint16_t port = 28120; port < 28220; ++port)
            if (transport.listen("127.0.0.1", port))
                return true;
        return false;
    }

    std::string endpoint() const {
        return "127.0.0.1:" + std::to_string(transport.local_port());
    }

    bool send(ribterra::net::Payload payload) {
        return connection != 0 &&
               transport.send(connection, ribterra::net::encode({++sequence, std::move(payload)}));
    }
};

template <typename Predicate>
bool wait_until(FakeServer &server, NetworkClient &client, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + operation_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.poll();
        (void)server.transport.poll();
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool connect_through_challenge(FakeServer &server, NetworkClient &client) {
    if (!client.connect(server.endpoint()))
        return false;
    bool hello_received = false;
    const auto deadline = std::chrono::steady_clock::now() + operation_timeout;
    while (!hello_received && std::chrono::steady_clock::now() < deadline) {
        for (auto &event : server.transport.poll()) {
            if (event.type == Event::Type::Connected) {
                server.connection = event.connection;
                if (!server.send(Challenge{std::string(64, 'a'), false, {}}))
                    return false;
            } else if (event.type == Event::Type::Message) {
                std::string error;
                const auto packet = ribterra::net::decode(event.bytes, error);
                hello_received = packet && std::holds_alternative<Hello>(packet->payload);
            }
        }
        client.poll();
        std::this_thread::sleep_for(1ms);
    }
    return hello_received;
}

struct AuthoritativeLobby {
    GameServer game;
    ribterra::JoinResult host;
    ViewSnapshot view;
    WorldId world_id;

    AuthoritativeLobby()
        : game([] {
              ServerConfig config;
              config.lobby_enabled = true;
              config.max_players = 4;
              config.population = 10;
              return config;
          }()),
          host(game.join("Host", game.config().versions)), view(game.snapshot(host.player)),
          world_id(game.world().export_state().world_id) {}
};

void add_explored_chunks_for_multipart(ViewSnapshot &view) {
    constexpr std::size_t additional_chunks = 12;
    std::size_t added = 0;
    for (int cy = ribterra::world_min / ribterra::chunk_size;
         cy <= (ribterra::world_max - 1) / ribterra::chunk_size && added < additional_chunks;
         ++cy) {
        for (int cx = ribterra::world_min / ribterra::chunk_size;
             cx <= (ribterra::world_max - 1) / ribterra::chunk_size && added < additional_chunks;
             ++cx) {
            const ribterra::ChunkKey key{ribterra::WorldLayer::Surface, cx, cy};
            if (std::any_of(view.chunks.begin(), view.chunks.end(),
                            [&](const auto &chunk) { return chunk.key == key; }))
                continue;
            ribterra::Chunk chunk{key};
            for (auto &tile : chunk.tiles)
                tile.explored = true;
            view.chunks.push_back(std::move(chunk));
            ++added;
        }
    }
    CHECK(added == additional_chunks);
    std::sort(view.chunks.begin(), view.chunks.end(),
              [](const auto &left, const auto &right) { return left.key < right.key; });
}

std::optional<SnapshotAck> wait_for_resync_ack(FakeServer &server, NetworkClient &client,
                                                std::string_view expected_error = {}) {
    std::optional<SnapshotAck> resync;
    const auto deadline = std::chrono::steady_clock::now() + operation_timeout;
    while (!resync && std::chrono::steady_clock::now() < deadline) {
        client.poll();
        for (const auto &event : server.transport.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            std::string error;
            const auto packet = ribterra::net::decode(event.bytes, error);
            if (const auto *ack = packet ? std::get_if<SnapshotAck>(&packet->payload) : nullptr;
                ack && ack->resync &&
                (expected_error.empty() || client.last_error() == expected_error))
                resync = *ack;
        }
        std::this_thread::sleep_for(1ms);
    }
    return resync;
}

void remote_join_waits_for_complete_full_snapshot() {
    AuthoritativeLobby source;
    CHECK(source.host.accepted);
    add_explored_chunks_for_multipart(source.view);
    const auto parts = ribterra::net::split_snapshot(
        ribterra::net::make_snapshot(source.view, source.world_id));
    CHECK(parts.size() > 1);

    FakeServer server;
    CHECK(server.listen());
    NetworkClient client(ClientConfig{"Remote player"});
    CHECK(connect_through_challenge(server, client));
    CHECK(!client.ready());

    CHECK(server.send(Welcome{source.host.player, source.host.resume_token, source.world_id, 1}));
    for (int exchange = 0; exchange < 20; ++exchange) {
        client.poll();
        (void)server.transport.poll();
        std::this_thread::sleep_for(1ms);
    }
    CHECK(!client.ready());
    CHECK(client.view().chunks.empty());

    for (std::size_t index = 0; index < parts.size(); ++index) {
        CHECK(server.send(parts[index]));
        if (index + 1 < parts.size()) {
            for (int exchange = 0; exchange < 10; ++exchange) {
                client.poll();
                (void)server.transport.poll();
                std::this_thread::sleep_for(1ms);
            }
            CHECK(!client.ready());
            CHECK(client.view().chunks.empty());
        }
    }

    CHECK(wait_until(server, client, [&] { return client.ready(); }));
    CHECK(client.view().revision == source.view.revision);
    CHECK(client.view().tick == source.view.tick);
    CHECK(client.view().chunks == source.view.chunks);
    CHECK(client.view().residents == source.view.residents);
    CHECK(client.view().inventory == source.view.inventory);
    CHECK(client.view().lobby == source.view.lobby);
    CHECK(client.view().lobby.enabled);
    CHECK(!client.view().lobby.started);
    CHECK(client.view().lobby.players.size() == 1);
}

void initial_delta_does_not_create_a_playable_baseline() {
    AuthoritativeLobby source;
    CHECK(source.host.accepted);
    ViewSnapshot empty_baseline;
    ViewSnapshot empty_delta;
    ribterra::test::bind_fixture_weather(empty_baseline, 70421, {}, 0);
    ribterra::test::bind_fixture_weather(empty_delta, 70421, {}, 0);
    empty_delta.revision = 1;
    const Snapshot delta =
        ribterra::net::make_snapshot(empty_delta, source.world_id, &empty_baseline);
    CHECK(!delta.full);
    CHECK(delta.baseline == 0);

    FakeServer server;
    CHECK(server.listen());
    NetworkClient client(ClientConfig{"Remote delta player"});
    CHECK(connect_through_challenge(server, client));
    CHECK(server.send(Welcome{source.host.player, source.host.resume_token, source.world_id, 1}));
    const auto welcome_resync = wait_for_resync_ack(server, client);
    CHECK(welcome_resync.has_value());
    CHECK(welcome_resync && welcome_resync->revision == 0);
    CHECK(client.last_error().empty());
    CHECK(server.send(delta));

    const auto resync =
        wait_for_resync_ack(server, client, "Full world snapshot required");
    CHECK(!client.ready());
    CHECK(client.view().revision == 0);
    CHECK(client.view().chunks.empty());
    CHECK(client.last_error() == "Full world snapshot required");
    CHECK(resync.has_value());
    CHECK(resync && resync->revision == 0);

    const auto full_parts = ribterra::net::split_snapshot(
        ribterra::net::make_snapshot(source.view, source.world_id));
    for (const auto &part : full_parts) {
        CHECK(server.send(part));
        client.poll();
        (void)server.transport.poll();
    }
    CHECK(wait_until(server, client, [&] { return client.ready(); }));
    CHECK(client.view().revision == source.view.revision);
    CHECK(client.view().chunks == source.view.chunks);
    CHECK(client.view().lobby == source.view.lobby);
}

void initial_transport_timeout_uses_structured_cause() {
    const auto endpoint = unused_loopback_endpoint();
    CHECK(endpoint.has_value());
    if (!endpoint)
        return;

    NetworkClient client(ClientConfig{"Unreachable host"});
    CHECK(client.connect(*endpoint));
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (client.last_error() != "network.connect_timeout" &&
           std::chrono::steady_clock::now() < deadline) {
        client.poll();
        std::this_thread::sleep_for(2ms);
    }
    CHECK(client.last_error() == "network.connect_timeout");
    CHECK(!client.ready());
    CHECK(client.player() == 0);
}

} // namespace

int main() {
    remote_join_waits_for_complete_full_snapshot();
    initial_delta_does_not_create_a_playable_baseline();
    initial_transport_timeout_uses_structured_cause();
    if (failures != 0) {
        std::cerr << failures << " remote join regression test(s) failed.\n";
        return 1;
    }
    std::cout << "Remote join waits for an authoritative full world snapshot.\n";
    return 0;
}

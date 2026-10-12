#include "ribterra/network_session.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace ribterra;
using namespace ribterra::net;
using namespace std::chrono_literals;
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x " failed\n"; ++failures; } } while (false)

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("ribterra-lobby-" + secure_token());
    TempDir() { std::filesystem::create_directory(path); }
    ~TempDir() { std::error_code e; std::filesystem::remove_all(path, e); }
};

std::unique_ptr<NetworkServer> listen(HostConfig c) {
    c.transport.bind_address = "127.0.0.1";
    for (std::uint16_t port = 27520; port < 27620; ++port) {
        c.transport.port = port;
        auto server = std::make_unique<NetworkServer>(c);
        if (server->listen()) return server;
    }
    throw std::runtime_error("No test port available");
}

template<class Predicate>
bool pump(NetworkServer& server, std::initializer_list<NetworkClient*> clients, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    while (std::chrono::steady_clock::now() < deadline) {
        server.poll();
        for (auto* client : clients) client->poll();
        if (done()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

GameCommand lobby(LobbyAction action, int value = 0) {
    GameCommand c;
    c.type = CommandType::Lobby;
    c.x = static_cast<int>(action);
    c.y = value;
    return c;
}

bool apply(NetworkServer& server, NetworkClient& client, GameCommand command,
           std::initializer_list<NetworkClient*> clients) {
    const auto id = client.command(command);
    if (!id) return false;
    bool applied = false;
    return pump(server, clients, [&] {
        for (const auto& reply : client.take_replies())
            if (reply.request_id == id) applied = reply.status == CommandReplyStatus::Applied;
        return applied;
    });
}

void lobby_join_snapshot_roundtrip() {
    TempDir temp;
    const auto save = temp.path / "online.save";
    HostConfig config;
    config.game.lobby_enabled = true;
    config.game.max_players = 4;
    config.game.population = 3;
    config.save_path = save;
    auto server = listen(config);
    const auto endpoint = "127.0.0.1:" + std::to_string(server->port());
    NetworkClient host(ClientConfig{"Host"}), guest(ClientConfig{"Guest"});
    CHECK(host.connect(endpoint));
    CHECK(pump(*server, {&host}, [&] { return host.ready(); }));
    CHECK(guest.connect(endpoint));
    CHECK(pump(*server, {&host, &guest}, [&] { return host.ready() && guest.ready(); }));
    CHECK(host.view().lobby.enabled && !host.view().lobby.started);
    CHECK(guest.view().lobby.enabled && !guest.view().lobby.started);
    CHECK(guest.view().chunks.empty() && guest.view().residents.empty());

    auto& owner = host.view().lobby.host == host.player() ? host : guest;
    auto& other = host.view().lobby.host == host.player() ? guest : host;
    CHECK(apply(*server, owner, lobby(LobbyAction::Ready, 1), {&host, &guest}));
    CHECK(apply(*server, other, lobby(LobbyAction::Ready, 1), {&host, &guest}));
    CHECK(apply(*server, owner, lobby(LobbyAction::Start), {&host, &guest}));
    CHECK(pump(*server, {&host, &guest}, [&] {
        return host.view().lobby.started && guest.view().lobby.started &&
               !guest.view().chunks.empty() && !guest.view().residents.empty();
    }));
    CHECK(host.view().inventory.total.food > 0 && guest.view().inventory.total.food > 0);
    const auto saved_host = server->game().snapshot(host.player());
    CHECK(server->save());

    NetworkClient late(ClientConfig{"Late"});
    auto resumed = listen(config);
    const auto resumed_endpoint = "127.0.0.1:" + std::to_string(resumed->port());
    CHECK(late.connect(resumed_endpoint));
    CHECK(pump(*resumed, {&late}, [&] { return late.ready(); }));
    CHECK(late.view().lobby.started);
    CHECK(!late.view().chunks.empty() && !late.view().residents.empty());
    const auto authoritative = resumed->game().snapshot(late.player());
    CHECK(late.view().chunks == authoritative.chunks);
    CHECK(late.view().residents == authoritative.residents);
    CHECK(late.view().inventory == authoritative.inventory);
    ClientConfig returning_config;
    returning_config.name = "Returning host";
    returning_config.resume_token = host.resume_token();
    NetworkClient returning_host(returning_config);
    CHECK(returning_host.connect(resumed_endpoint));
    CHECK(pump(*resumed, {&late, &returning_host}, [&] { return returning_host.ready(); }));
    CHECK(returning_host.player() == host.player());
    const auto preserved_host = resumed->game().snapshot(host.player());
    CHECK(preserved_host.tick == saved_host.tick);
    CHECK(preserved_host.chunks == saved_host.chunks);
    CHECK(preserved_host.residents == saved_host.residents);
    CHECK(preserved_host.inventory == saved_host.inventory);
}
} // namespace

int main() {
    lobby_join_snapshot_roundtrip();
    if (failures) return failures;
    std::cout << "Network lobby join snapshots passed.\n";
    return 0;
}

#pragma once
#include "ribterra/net_transport.hpp"
#include "ribterra/protocol.hpp"
#include <memory>

namespace ribterra::net {
struct HostConfig {
    ServerConfig game;
    TransportOptions transport;
    std::string password;
    std::filesystem::path save_path;
    std::chrono::seconds autosave_interval{60};
};
class NetworkServer final {
public:
    explicit NetworkServer(HostConfig = {});
    ~NetworkServer();
    bool listen();
    void poll();
    void tick();
    bool save();
    void shutdown();
    std::uint16_t port() const;
    std::string last_error() const;
    const GameServer& game() const;
    std::filesystem::path save_path() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct ClientConfig {
    std::string name{"Player"};
    Versions versions;
    std::string password, resume_token;
    bool auto_reconnect{};
    std::function<std::chrono::steady_clock::time_point()> clock{
        [] { return std::chrono::steady_clock::now(); }};
};
class NetworkClient final {
public:
    explicit NetworkClient(ClientConfig = {});
    ~NetworkClient();
    bool connect(std::string_view endpoint);
    void poll();
    void disconnect();
    bool ready() const;
    bool reconnecting() const;
    bool recovery_incomplete() const;
    PlayerId player() const;
    const std::string& resume_token() const;
    const ViewSnapshot& view() const;
    const WorldId& world_id() const;
    std::uint64_t command(GameCommand);
    bool chat(std::string text);
    std::vector<CommandReply> take_replies();
    std::vector<ChatEntry> take_chat();
    std::string last_error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ribterra::net

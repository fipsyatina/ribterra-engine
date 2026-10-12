#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ribterra::net {

enum class Delivery : std::uint8_t { Reliable, Unreliable };
enum class DisconnectCause : std::uint8_t { None, Timeout };

// Lower-numbered lanes have higher transmission priority in the GNS adapter.
enum class Lane : std::uint8_t { Commands = 0, Chat = 1, Snapshot = 2 };

struct Event {
    enum class Type : std::uint8_t { Connected, Disconnected, Message };

    Type type{};
    std::uint32_t connection{};
    std::vector<std::uint8_t> bytes;
    std::string reason;
    DisconnectCause disconnect_cause{};
};

struct TransportOptions {
    std::string bind_address{"127.0.0.1"};
    std::uint16_t port{27020};
    std::size_t max_poll_events{256};
    std::size_t max_message_size{256U * 1024U};
    std::size_t max_send_backlog_bytes{4U * 1024U * 1024U};
    std::size_t send_rate_bytes_per_second{512U * 1024U};
};

// Standalone encrypted UDP transport powered by Valve GameNetworkingSockets.
// The public surface deliberately keeps all GNS types behind an opaque PImpl.
class NetTransport final {
public:
    explicit NetTransport(TransportOptions options = {});
    ~NetTransport();

    NetTransport(const NetTransport&) = delete;
    NetTransport& operator=(const NetTransport&) = delete;
    NetTransport(NetTransport&&) noexcept;
    NetTransport& operator=(NetTransport&&) noexcept;

    bool listen();
    bool listen(std::string_view address, std::uint16_t port);
    std::uint32_t connect(std::string_view endpoint);
    std::vector<Event> poll();
    bool send(std::uint32_t connection, std::span<const std::uint8_t> bytes,
              Delivery delivery = Delivery::Reliable, Lane lane = Lane::Commands);
    void close(std::uint32_t connection, std::string_view reason = {});
    void shutdown();

    [[nodiscard]] std::uint16_t local_port() const;
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra::net

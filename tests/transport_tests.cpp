#include "ribterra/net_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using ribterra::net::Delivery;
using ribterra::net::Event;
using ribterra::net::Lane;
using ribterra::net::NetTransport;

int failures = 0;
#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression "\n";                       \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

template <typename Predicate>
bool pump_until(NetTransport& first, NetTransport& second, Predicate predicate,
                std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto first_events = first.poll();
        auto second_events = second.poll();
        if (predicate(first_events, second_events)) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

bool begin_listening(NetTransport& server) {
    if (server.listen("127.0.0.1", 0)) return server.local_port() != 0;
    for (std::uint16_t port = 27120; port < 27220; ++port) {
        if (server.listen("127.0.0.1", port)) return server.local_port() == port;
    }
    std::cerr << "Could not allocate a loopback test port: " << server.last_error() << '\n';
    return false;
}

struct Pair {
    NetTransport server;
    NetTransport client;
    std::uint32_t server_connection{};
    std::uint32_t client_connection{};

    bool connect() {
        if (!begin_listening(server)) return false;
        client_connection = client.connect("127.0.0.1:" + std::to_string(server.local_port()));
        if (client_connection == 0) {
            std::cerr << "Connect failed: " << client.last_error() << '\n';
            return false;
        }
        bool client_connected = false;
        bool server_connected = false;
        const bool ready = pump_until(server, client, [&](const auto& server_events, const auto& client_events) {
            for (const auto& event : server_events) {
                if (event.type == Event::Type::Connected) {
                    server_connection = event.connection;
                    server_connected = true;
                }
            }
            for (const auto& event : client_events) {
                if (event.type == Event::Type::Connected && event.connection == client_connection) {
                    client_connected = true;
                }
            }
            return server_connected && client_connected;
        });
        if (!ready) std::cerr << "Timed out waiting for loopback connection\n";
        return ready;
    }
};

void invalid_input_and_occupied_port() {
    NetTransport invalid;
    CHECK(!invalid.listen("this-is-not-an-ip", 27020));
    CHECK(!invalid.last_error().empty());
    CHECK(invalid.connect("not-an-endpoint") == 0);
    CHECK(invalid.connect("127.0.0.1") == 0);

    NetTransport first;
    CHECK(begin_listening(first));
    NetTransport second;
    CHECK(!second.listen("127.0.0.1", first.local_port()));
    CHECK(second.last_error().find("already in use") != std::string::npos);
}

void reliable_unreliable_and_disconnect() {
    Pair pair;
    CHECK(pair.connect());
    if (pair.server_connection == 0 || pair.client_connection == 0) return;

    const std::vector<std::uint8_t> reliable{'r', 'i', 'b', 't', 'e', 'r', 'r', 'a'};
    CHECK(pair.client.send(pair.client_connection, reliable, Delivery::Reliable, Lane::Commands));
    bool reliable_received = false;
    CHECK(pump_until(pair.server, pair.client, [&](const auto& server_events, const auto&) {
        for (const auto& event : server_events) {
            if (event.type == Event::Type::Message && event.connection == pair.server_connection &&
                event.bytes == reliable) {
                reliable_received = true;
            }
        }
        return reliable_received;
    }));

    const std::vector<std::uint8_t> chat{'h', 'e', 'l', 'l', 'o'};
    CHECK(pair.server.send(pair.server_connection, chat, Delivery::Reliable, Lane::Chat));
    bool chat_received = false;
    CHECK(pump_until(pair.server, pair.client, [&](const auto&, const auto& client_events) {
        for (const auto& event : client_events) {
            if (event.type == Event::Type::Message && event.connection == pair.client_connection &&
                event.bytes == chat) {
                chat_received = true;
            }
        }
        return chat_received;
    }));

    const std::vector<std::uint8_t> snapshot{'s', 'n', 'a', 'p'};
    bool unreliable_received = false;
    for (int attempt = 0; attempt < 8 && !unreliable_received; ++attempt) {
        CHECK(pair.client.send(pair.client_connection, snapshot, Delivery::Unreliable, Lane::Snapshot));
        pump_until(pair.server, pair.client, [&](const auto& server_events, const auto&) {
            for (const auto& event : server_events) {
                if (event.type == Event::Type::Message && event.connection == pair.server_connection &&
                    event.bytes == snapshot) {
                    unreliable_received = true;
                }
            }
            return unreliable_received;
        }, 500ms);
    }
    CHECK(unreliable_received);

    const std::vector<std::uint8_t> oversized(256U * 1024U + 1U, 0x5a);
    CHECK(!pair.client.send(pair.client_connection, oversized, Delivery::Reliable, Lane::Commands));
    CHECK(pair.client.last_error().find("256 KiB") != std::string::npos);

    pair.client.close(pair.client_connection, "test disconnect");
    bool local_disconnected = false;
    bool remote_disconnected = false;
    CHECK(pump_until(pair.server, pair.client, [&](const auto& server_events, const auto& client_events) {
        for (const auto& event : server_events) {
            if (event.type == Event::Type::Disconnected && event.connection == pair.server_connection) {
                remote_disconnected = true;
            }
        }
        for (const auto& event : client_events) {
            if (event.type == Event::Type::Disconnected && event.connection == pair.client_connection) {
                local_disconnected = true;
            }
        }
        return local_disconnected && remote_disconnected;
    }));
}

void poll_is_bounded() {
    ribterra::net::TransportOptions options;
    options.max_poll_events = 3;
    Pair pair{NetTransport(options), NetTransport(options)};
    CHECK(pair.connect());
    if (pair.client_connection == 0) return;

    const std::vector<std::uint8_t> message{1, 2, 3};
    for (int index = 0; index < 12; ++index) {
        CHECK(pair.client.send(pair.client_connection, message, Delivery::Reliable, Lane::Commands));
    }
    bool saw_messages = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && !saw_messages) {
        const auto events = pair.server.poll();
        CHECK(events.size() <= options.max_poll_events);
        saw_messages = std::any_of(events.begin(), events.end(), [](const Event& event) {
            return event.type == Event::Type::Message;
        });
        pair.client.poll();
        std::this_thread::sleep_for(2ms);
    }
    CHECK(saw_messages);
}

void send_backlog_is_bounded() {
    ribterra::net::TransportOptions options;
    options.max_send_backlog_bytes = 2;
    Pair pair{NetTransport(options), NetTransport(options)};
    CHECK(pair.connect());
    if (pair.client_connection == 0) return;
    const std::vector<std::uint8_t> message{1, 2, 3};
    CHECK(!pair.client.send(pair.client_connection, message));
    CHECK(pair.client.last_error().find("backlog limit") != std::string::npos);
}

} // namespace

int main() {
    invalid_input_and_occupied_port();
    reliable_unreliable_and_disconnect();
    poll_is_bounded();
    send_backlog_is_bounded();
    if (failures != 0) {
        std::cerr << failures << " transport test(s) failed.\n";
        return 1;
    }
    std::cout << "GNS loopback, delivery modes, bounds, validation and disconnect passed.\n";
    return 0;
}

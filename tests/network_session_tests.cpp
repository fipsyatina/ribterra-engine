#include "ribterra/network_session.hpp"
#include "work_projection_fixture.hpp"
#include "weather_fixture.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using ribterra::BuildingKind;
using ribterra::CommandReply;
using ribterra::CommandReplyStatus;
using ribterra::CommandType;
using ribterra::GameCommand;
using ribterra::PlayerId;
using ribterra::Versions;
using ribterra::net::Challenge;
using ribterra::net::ClientConfig;
using ribterra::net::Event;
using ribterra::net::Hello;
using ribterra::net::HostConfig;
using ribterra::net::NetTransport;
using ribterra::net::NetworkClient;
using ribterra::net::NetworkServer;
using ribterra::net::Packet;
using ribterra::net::Ping;
using ribterra::net::ReplyAck;
using ribterra::net::Snapshot;
using ribterra::net::SnapshotAck;
using ribterra::net::Welcome;

int failures = 0;
#define CHECK(expression)                                                                                    \
    do {                                                                                                     \
        if (!(expression)) {                                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression "\n";                 \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

const auto test_deadline = std::chrono::steady_clock::now() + 30s;

void service(NetworkServer &server, std::vector<NetworkClient *> clients, bool tick) {
    server.poll();
    for (auto *client : clients)
        client->poll();
    if (tick)
        server.tick();
    server.poll();
    for (auto *client : clients)
        client->poll();
}

template <typename Predicate>
bool wait_until(NetworkServer &server, std::vector<NetworkClient *> clients, Predicate predicate,
                std::chrono::milliseconds timeout, bool tick = true) {
    const auto deadline = std::min(test_deadline, std::chrono::steady_clock::now() + timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        service(server, clients, tick);
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

template <typename Predicate>
bool wait_raw(NetworkServer &server, std::vector<NetworkClient *> clients, NetTransport &raw,
              Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::min(test_deadline, std::chrono::steady_clock::now() + timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        server.poll();
        for (auto *client : clients)
            client->poll();
        auto events = raw.poll();
        if (predicate(events))
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

std::unique_ptr<NetworkServer> listening_server(HostConfig config) {
    for (std::uint16_t port = 27320; port < 27420; ++port) {
        config.transport.bind_address = "127.0.0.1";
        config.transport.port = port;
        auto server = std::make_unique<NetworkServer>(config);
        if (server->listen())
            return server;
    }
    return {};
}

std::string endpoint(const NetworkServer &server) { return "127.0.0.1:" + std::to_string(server.port()); }

bool same_shared_view(const NetworkClient &first, const NetworkClient &second) {
    const auto &a = first.view();
    const auto &b = second.view();
    if (a.revision != b.revision || a.tick != b.tick || a.buildings.size() != b.buildings.size() ||
        a.residents.size() != b.residents.size() || a.resources.wood != b.resources.wood ||
        a.resources.stone != b.resources.stone || a.resources.food != b.resources.food ||
        a.inventory != b.inventory || a.resident_logistics != b.resident_logistics ||
        a.construction_sites != b.construction_sites || a.resident_work != b.resident_work ||
        a.resident_professions != b.resident_professions || a.manual_orders != b.manual_orders ||
        a.control_players != b.control_players || a.control_epoch != b.control_epoch ||
        a.resident_controls.size() != b.resident_controls.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.resident_controls.size(); ++i) {
        auto left_control = a.resident_controls[i];
        auto right_control = b.resident_controls[i];
        left_control.held_by_recipient = right_control.held_by_recipient = false;
        if (left_control != right_control)
            return false;
    }
    if (a.buildings.empty())
        return true;
    const auto &left = a.buildings.back();
    const auto &right = b.buildings.back();
    return left.id == right.id && left.kind == right.kind && left.x == right.x && left.y == right.y &&
           left.progress == right.progress;
}

void reordered_and_duplicate_replies() {
    NetTransport fake_server;
    bool listening = false;
    for (std::uint16_t port = 27420; port < 27520 && !listening; ++port)
        listening = fake_server.listen("127.0.0.1", port);
    CHECK(listening);
    if (!listening)
        return;

    ClientConfig reply_config;
    reply_config.name = "Reply ordering";
    NetworkClient client(std::move(reply_config));
    CHECK(client.connect("127.0.0.1:" + std::to_string(fake_server.local_port())));
    std::uint32_t connection = 0;
    bool hello_received = false;
    const auto id = [] {
        ribterra::net::WorldId value{};
        value.front() = 1;
        return value;
    }();
    auto view = ribterra::ViewSnapshot{};
    view.revision = 1;
    view.tick = 1;
    ribterra::test::bind_fixture_weather(view, 70421, {}, view.tick);

    const auto handshake_deadline = std::chrono::steady_clock::now() + 3s;
    while (!client.ready() && std::chrono::steady_clock::now() < handshake_deadline) {
        for (const auto &event : fake_server.poll()) {
            if (event.type == Event::Type::Connected) {
                connection = event.connection;
                CHECK(fake_server.send(
                    connection,
                    ribterra::net::encode(Packet{1, Challenge{std::string(64, 'a'), false, Versions{}}})));
            } else if (event.type == Event::Type::Message) {
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (packet && std::holds_alternative<Hello>(packet->payload) && !hello_received) {
                    hello_received = true;
                    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{
                                                           2, Welcome{7, std::string(64, 'b'), id, 78}})));
                    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{
                                                           3, ribterra::net::make_snapshot(view, id)})));
                }
            }
        }
        client.poll();
        std::this_thread::sleep_for(1ms);
    }
    CHECK(client.ready());
    if (!client.ready())
        return;

    const CommandReply terminal{
        77, 5, 7, CommandReplyStatus::Applied, ribterra::PathFailure::None, "Path applied."};
    const CommandReply queued{77, 0, 7, CommandReplyStatus::Queued, ribterra::PathFailure::None, "Queued"};
    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{4, terminal})));
    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{5, queued})));

    std::set<ribterra::ReplyReceipt> receipts;
    const auto receipt_deadline = std::chrono::steady_clock::now() + 3s;
    while (receipts.size() < 2 && std::chrono::steady_clock::now() < receipt_deadline) {
        client.poll();
        for (const auto &event : fake_server.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            std::string error;
            auto packet = ribterra::net::decode(event.bytes, error);
            if (const auto *ack = packet ? std::get_if<ReplyAck>(&packet->payload) : nullptr)
                receipts.insert(ack->receipts.begin(), ack->receipts.end());
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK(receipts.contains({77, CommandReplyStatus::Queued}));
    CHECK(receipts.contains({77, CommandReplyStatus::Applied}));
    const auto surfaced = client.take_replies();
    CHECK(surfaced == std::vector<CommandReply>{terminal});

    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{6, terminal})));
    for (int iteration = 0; iteration < 20; ++iteration) {
        client.poll();
        (void)fake_server.poll();
        std::this_thread::sleep_for(1ms);
    }
    CHECK(client.take_replies().empty());

    const CommandReply conflicting{
        77, 5, 7, CommandReplyStatus::Rejected, ribterra::PathFailure::Unreachable, "No route"};
    CHECK(fake_server.send(connection, ribterra::net::encode(Packet{7, conflicting})));
    const auto conflict_deadline = std::chrono::steady_clock::now() + 3s;
    while (client.ready() && std::chrono::steady_clock::now() < conflict_deadline) {
        client.poll();
        std::this_thread::sleep_for(1ms);
    }
    CHECK(!client.ready());
    CHECK(client.last_error() == "Conflicting command reply");
}

void full_network_session() {
    HostConfig host;
    host.password = "correct horse battery staple";
    auto logical_now = std::chrono::steady_clock::time_point{};
    host.game.clock = [&] { return logical_now; };
    auto server = listening_server(host);
    CHECK(server != nullptr);
    if (server == nullptr)
        return;

    HostConfig occupied_config = host;
    occupied_config.transport.bind_address = "127.0.0.1";
    occupied_config.transport.port = server->port();
    NetworkServer occupied(occupied_config);
    CHECK(!occupied.listen());
    CHECK(occupied.last_error().find("already in use") != std::string::npos);

    {
        ClientConfig config;
        config.name = "Wrong password";
        config.password = "incorrect";
        NetworkClient rejected(config);
        CHECK(rejected.connect(endpoint(*server)));
        CHECK(wait_until(
            *server, {&rejected},
            [&] { return rejected.last_error().find("Invalid password") != std::string::npos; }, 3s));
        CHECK(!rejected.ready());
        CHECK(rejected.player() == 0);
    }

    {
        ClientConfig config;
        config.name = "Wrong version";
        config.password = host.password;
        ++config.versions.protocol;
        NetworkClient rejected(config);
        CHECK(rejected.connect(endpoint(*server)));
        CHECK(wait_until(
            *server, {&rejected},
            [&] { return rejected.last_error() == "network.version_protocol"; }, 3s));
        CHECK(!rejected.ready());
        CHECK(rejected.player() == 0);
    }

    ClientConfig alice_config;
    alice_config.name = "Alice";
    alice_config.password = host.password;
    ClientConfig bob_config;
    bob_config.name = "Bob";
    bob_config.password = host.password;
    NetworkClient alice(alice_config);
    NetworkClient bob(bob_config);
    CHECK(alice.connect(endpoint(*server)));
    CHECK(bob.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&alice, &bob}, [&] { return alice.ready() && bob.ready(); }, 5s));
    if (!alice.ready() || !bob.ready())
        return;
    CHECK(alice.player() != 0);
    CHECK(bob.player() != 0);
    CHECK(alice.player() != bob.player());
    CHECK(alice.view().revision <= server->game().revision());
    CHECK(bob.view().revision <= server->game().revision());

    const auto people_before = server->game().world().residents().size();
    const PlayerId alice_id = alice.player();
    const std::string alice_token = alice.resume_token();
    GameCommand build;
    build.author = std::numeric_limits<PlayerId>::max();
    build.type = CommandType::Build;
    build.x = 26;
    build.y = 24;
    build.building = BuildingKind::House;
    const auto request = alice.command(build);
    CHECK(request != 0);

    bool applied = false;
    CHECK(wait_until(
        *server, {&alice, &bob},
        [&] {
            for (const auto &reply : alice.take_replies()) {
                if (reply.request_id == request && reply.status == CommandReplyStatus::Applied &&
                    reply.author == alice_id)
                    applied = true;
            }
            return applied && server->game().world().buildings().size() == 2 &&
                   server->game().world().ticks() % 4 == 0;
        },
        5s));
    CHECK(applied);

    CHECK(wait_until(
        *server, {&alice, &bob},
        [&] {
            return alice.view().buildings.size() == 2 && bob.view().buildings.size() == 2 &&
                   same_shared_view(alice, bob);
        },
        3s));
    CHECK(alice.view().buildings.size() == 2);
    CHECK(bob.view().buildings.size() == 2);
    CHECK(same_shared_view(alice, bob));

    CHECK(alice.chat("Welcome to Ribterra"));
    std::vector<ribterra::ChatEntry> bob_chat;
    CHECK(wait_until(
        *server, {&alice, &bob},
        [&] {
            auto received = bob.take_chat();
            bob_chat.insert(bob_chat.end(), received.begin(), received.end());
            return !bob_chat.empty();
        },
        3s));
    CHECK(!bob_chat.empty());
    if (!bob_chat.empty()) {
        CHECK(bob_chat.back().author == alice_id);
        CHECK(bob_chat.back().name == "Alice");
        CHECK(bob_chat.back().text == "Welcome to Ribterra");
    }

    ClientConfig carol_config;
    carol_config.name = "Carol";
    carol_config.password = host.password;
    NetworkClient carol(carol_config);
    CHECK(carol.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&alice, &bob, &carol}, [&] { return carol.ready(); }, 3s, false));
    const auto late_authority = server->game().snapshot(carol.player());
    CHECK(carol.view().inventory == late_authority.inventory);
    CHECK(carol.view().resident_logistics == late_authority.resident_logistics);
    CHECK(carol.view().construction_sites == late_authority.construction_sites);
    std::vector<ribterra::ChatEntry> carol_chat;
    CHECK(wait_until(
        *server, {&alice, &bob, &carol},
        [&] {
            auto received = carol.take_chat();
            carol_chat.insert(carol_chat.end(), received.begin(), received.end());
            return !carol_chat.empty();
        },
        3s, false));
    for (int iteration = 0; iteration < 50 && std::chrono::steady_clock::now() < test_deadline; ++iteration) {
        service(*server, {&alice, &bob, &carol}, false);
        auto received = carol.take_chat();
        carol_chat.insert(carol_chat.end(), received.begin(), received.end());
        std::this_thread::sleep_for(1ms);
    }
    CHECK(carol_chat.size() == 1);
    if (carol_chat.size() == 1) {
        CHECK(carol_chat.front().author == alice_id);
        CHECK(carol_chat.front().text == "Welcome to Ribterra");
    }

    NetTransport raw;
    const auto raw_connection = raw.connect(endpoint(*server));
    CHECK(raw_connection != 0);
    std::optional<Challenge> challenge;
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (packet && std::holds_alternative<Challenge>(packet->payload)) {
                    challenge = std::get<Challenge>(std::move(packet->payload));
                }
            }
            return challenge.has_value();
        },
        3s));
    if (challenge) {
        Hello hello{"Flood", challenge->versions, {}, {}};
        hello.password_proof = ribterra::net::password_proof(host.password, *challenge, hello);
        const auto hello_bytes = ribterra::net::encode(Packet{1, std::move(hello)});
        CHECK(raw.send(raw_connection, hello_bytes));
    }

    std::optional<Welcome> welcome;
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (packet && std::holds_alternative<Welcome>(packet->payload)) {
                    welcome = std::get<Welcome>(std::move(packet->payload));
                }
            }
            return welcome.has_value();
        },
        3s));

    std::size_t snapshot_count = 0;
    std::uint64_t snapshot_revision = 0;
    if (welcome) {
        const auto ack_bytes = ribterra::net::encode(Packet{2, SnapshotAck{0, true}});
        CHECK(raw.send(raw_connection, ack_bytes));
    }
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (packet && std::holds_alternative<Snapshot>(packet->payload)) {
                    ++snapshot_count;
                    snapshot_revision = std::get<Snapshot>(packet->payload).revision;
                }
            }
            return snapshot_count != 0;
        },
        3s));
    if (snapshot_count != 0) {
        const auto baseline_ack = ribterra::net::encode(Packet{3, SnapshotAck{snapshot_revision, false}});
        CHECK(raw.send(raw_connection, baseline_ack));
        for (std::uint64_t sequence = 4; sequence < 36; ++sequence) {
            const auto resync = ribterra::net::encode(Packet{sequence, SnapshotAck{snapshot_revision, true}});
            CHECK(raw.send(raw_connection, resync));
        }
        const auto ping = ribterra::net::encode(Packet{36, Ping{0x52494254}});
        CHECK(raw.send(raw_connection, ping));
    }

    bool saw_ping = false;
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (!packet)
                    continue;
                if (std::holds_alternative<Snapshot>(packet->payload))
                    ++snapshot_count;
                if (const auto *reply = std::get_if<Ping>(&packet->payload);
                    reply && reply->value == 0x52494254) {
                    saw_ping = true;
                }
            }
            return saw_ping;
        },
        3s));
    for (int iteration = 0; iteration < 100 && std::chrono::steady_clock::now() < test_deadline;
         ++iteration) {
        server->poll();
        alice.poll();
        bob.poll();
        carol.poll();
        for (const auto &event : raw.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            std::string error;
            auto packet = ribterra::net::decode(event.bytes, error);
            if (packet && std::holds_alternative<Snapshot>(packet->payload))
                ++snapshot_count;
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK(saw_ping);
    CHECK(snapshot_count >= 1);
    CHECK(snapshot_count <= 2);

    int raw_build_x = -1;
    int raw_build_y = -1;
    for (int y = 0; y < ribterra::world_size && raw_build_x < 0; ++y) {
        for (int x = 0; x < ribterra::world_size; ++x) {
            if (server->game().world().can_build(BuildingKind::House, x, y)) {
                raw_build_x = x;
                raw_build_y = y;
                break;
            }
        }
    }
    CHECK(raw_build_x >= 0);
    const auto raw_player = welcome ? welcome->player : 0;
    const auto raw_token = welcome ? welcome->resume_token : std::string{};
    const auto raw_request = welcome ? welcome->next_request : 0;
    if (raw_build_x >= 0 && raw_player != 0 && raw_request != 0) {
        GameCommand raw_command;
        raw_command.request_id = raw_request;
        raw_command.author = raw_player;
        raw_command.issued_tick = server->game().world().ticks();
        raw_command.type = CommandType::Build;
        raw_command.x = raw_build_x;
        raw_command.y = raw_build_y;
        raw_command.building = BuildingKind::House;
        raw_command.control_epoch = server->game().snapshot(raw_player).control_epoch;
        const auto command_bytes = ribterra::net::encode(Packet{37, raw_command});
        CHECK(raw.send(raw_connection, command_bytes));
    }

    std::set<ribterra::ReplyReceipt> outstanding_receipts;
    std::optional<CommandReply> outstanding_terminal;
    std::size_t withheld_delta_count = 0;
    for (int iteration = 0;
         iteration < 12 && (outstanding_receipts.size() < 2 || withheld_delta_count == 0) &&
         std::chrono::steady_clock::now() < test_deadline;
         ++iteration) {
        server->poll();
        server->tick();
        server->poll();
        alice.poll();
        bob.poll();
        carol.poll();
        for (const auto &event : raw.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            std::string error;
            auto packet = ribterra::net::decode(event.bytes, error);
            if (!packet)
                continue;
            if (const auto *reply = std::get_if<CommandReply>(&packet->payload);
                reply && reply->request_id == raw_request) {
                outstanding_receipts.insert({reply->request_id, reply->status});
                if (reply->status == CommandReplyStatus::Applied ||
                    reply->status == CommandReplyStatus::Rejected)
                    outstanding_terminal = *reply;
            }
            if (std::holds_alternative<Snapshot>(packet->payload))
                ++withheld_delta_count;
        }
        std::this_thread::sleep_for(1ms);
    }
    CHECK(outstanding_receipts.contains({raw_request, CommandReplyStatus::Queued}));
    CHECK(outstanding_receipts.contains({raw_request, CommandReplyStatus::Applied}));
    CHECK(outstanding_terminal.has_value());
    CHECK(server->game().pending_replies(raw_player).size() == 2);
    CHECK(withheld_delta_count == 1);

    logical_now += 3s;
    for (int iteration = 0; iteration < 12 && std::chrono::steady_clock::now() < test_deadline; ++iteration) {
        server->tick();
        server->poll();
        alice.poll();
        bob.poll();
        carol.poll();
        for (const auto &event : raw.poll()) {
            if (event.type != Event::Type::Message)
                continue;
            std::string error;
            auto packet = ribterra::net::decode(event.bytes, error);
            if (packet && std::holds_alternative<Snapshot>(packet->payload))
                ++withheld_delta_count;
        }
    }
    CHECK(withheld_delta_count == 1);

    std::set<ribterra::ReplyReceipt> raw_received_replies = outstanding_receipts;
    std::set<std::uint64_t> raw_terminal_requests;
    std::uint64_t raw_packet_sequence = 38;
    constexpr std::size_t retained_reply_count = 200;
    for (std::size_t batch = 0; batch < retained_reply_count / 40; ++batch) {
        logical_now += 1s;
        for (std::size_t offset = 0; offset < 40; ++offset) {
            const auto request_id = raw_request + 1 + batch * 40 + offset;
            GameCommand command;
            command.request_id = request_id;
            command.author = raw_player;
            command.issued_tick = server->game().world().ticks();
            command.type = CommandType::Move;
            command.control_epoch = server->game().snapshot(raw_player).control_epoch;
            // An empty batch is applied by the command queue and then rejected
            // immediately by the world, giving this retention test both phases
            // without filling the path planner with 200 unrelated routes.
            command.x = 23;
            command.y = 23;
            const auto bytes = ribterra::net::encode(Packet{raw_packet_sequence++, command});
            CHECK(raw.send(raw_connection, bytes));
        }
        const auto barrier_value = 0xB000U + batch;
        const auto barrier = ribterra::net::encode(Packet{raw_packet_sequence++, Ping{barrier_value}});
        CHECK(raw.send(raw_connection, barrier));
        bool barrier_received = false;
        CHECK(wait_raw(
            *server, {&alice, &bob, &carol}, raw,
            [&](const std::vector<Event> &events) {
                for (const auto &event : events) {
                    if (event.type != Event::Type::Message)
                        continue;
                    std::string error;
                    auto packet = ribterra::net::decode(event.bytes, error);
                    if (!packet)
                        continue;
                    if (const auto *reply = std::get_if<CommandReply>(&packet->payload)) {
                        raw_received_replies.insert({reply->request_id, reply->status});
                        if (reply->status == CommandReplyStatus::Applied ||
                            reply->status == CommandReplyStatus::Rejected)
                            raw_terminal_requests.insert(reply->request_id);
                    }
                    if (const auto *reply = std::get_if<Ping>(&packet->payload);
                        reply && reply->value == barrier_value) {
                        barrier_received = true;
                    }
                }
                return barrier_received;
            },
            3s));
        std::vector<ribterra::ReplyReceipt> queued_acks;
        for (std::size_t offset = 0; offset < 40; ++offset)
            queued_acks.push_back({raw_request + 1 + batch * 40 + offset, CommandReplyStatus::Queued});
        const auto queued_ack = ribterra::net::encode(Packet{raw_packet_sequence++, ReplyAck{queued_acks}});
        CHECK(raw.send(raw_connection, queued_ack));
        server->tick();
        CHECK(wait_raw(
            *server, {&alice, &bob, &carol}, raw,
            [&](const std::vector<Event> &events) {
                for (const auto &event : events) {
                    if (event.type != Event::Type::Message)
                        continue;
                    std::string error;
                    auto packet = ribterra::net::decode(event.bytes, error);
                    if (packet) {
                        if (const auto *reply = std::get_if<CommandReply>(&packet->payload)) {
                            raw_received_replies.insert({reply->request_id, reply->status});
                            if (reply->status == CommandReplyStatus::Applied ||
                                reply->status == CommandReplyStatus::Rejected)
                                raw_terminal_requests.insert(reply->request_id);
                        }
                    }
                }
                // Terminals may precede this newly sent Queued ACK. Observe
                // both delivery and its server-side effect before inspecting retention.
                return raw_terminal_requests.size() >= (batch + 1) * 40 &&
                       server->game().pending_replies(raw_player).size() == 2 + (batch + 1) * 40;
            },
            3s));
    }
    CHECK(raw_terminal_requests.size() == retained_reply_count);
    CHECK(raw_received_replies.size() == 2 + retained_reply_count * 2);
    CHECK(server->game().pending_replies(raw_player).size() == retained_reply_count + 2);

    raw.close(raw_connection, "reconnect outstanding reply");
    CHECK(wait_until(
        *server, {&alice, &bob, &carol}, [&] { return !server->game().connected(raw_player); }, 3s, false));

    NetTransport resumed_raw;
    const auto resumed_raw_connection = resumed_raw.connect(endpoint(*server));
    CHECK(resumed_raw_connection != 0);
    std::optional<Challenge> resumed_challenge;
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, resumed_raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (packet && std::holds_alternative<Challenge>(packet->payload)) {
                    resumed_challenge = std::get<Challenge>(std::move(packet->payload));
                }
            }
            return resumed_challenge.has_value();
        },
        3s));
    if (resumed_challenge) {
        Hello hello{"Flood resumed", resumed_challenge->versions, raw_token, {}};
        hello.password_proof = ribterra::net::password_proof(host.password, *resumed_challenge, hello);
        const auto hello_bytes = ribterra::net::encode(Packet{1, std::move(hello)});
        CHECK(resumed_raw.send(resumed_raw_connection, hello_bytes));
    }
    std::optional<Welcome> resumed_welcome;
    std::set<ribterra::ReplyReceipt> replayed_initial;
    std::optional<CommandReply> replayed_terminal;
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, resumed_raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (!packet)
                    continue;
                if (std::holds_alternative<Welcome>(packet->payload)) {
                    resumed_welcome = std::get<Welcome>(packet->payload);
                }
            }
            return resumed_welcome.has_value();
        },
        3s));
    if (resumed_welcome) {
        CHECK(resumed_welcome->player == raw_player);
        CHECK(resumed_welcome->resume_token == raw_token);
        CHECK(resumed_welcome->next_request > raw_request);
    }
    CHECK(server->game().pending_replies(raw_player).size() == retained_reply_count + 2);
    server->tick();
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, resumed_raw,
        [&](const std::vector<Event> &events) {
            for (const auto &event : events) {
                if (event.type != Event::Type::Message)
                    continue;
                std::string error;
                auto packet = ribterra::net::decode(event.bytes, error);
                if (const auto *reply = packet ? std::get_if<CommandReply>(&packet->payload) : nullptr;
                    reply && reply->request_id == raw_request) {
                    replayed_initial.insert({reply->request_id, reply->status});
                    if (reply->status == CommandReplyStatus::Applied ||
                        reply->status == CommandReplyStatus::Rejected)
                        replayed_terminal = *reply;
                }
            }
            return replayed_initial.size() == 2;
        },
        3s));
    CHECK(replayed_initial == outstanding_receipts);
    CHECK(replayed_terminal == outstanding_terminal);
    CHECK(server->game().pending_replies(raw_player).size() == retained_reply_count + 2);
    if (outstanding_terminal) {
        const auto reply_ack =
            ribterra::net::encode(Packet{2, ReplyAck{{{raw_request, outstanding_terminal->status}}}});
        CHECK(resumed_raw.send(resumed_raw_connection, reply_ack));
    }
    CHECK(wait_raw(
        *server, {&alice, &bob, &carol}, resumed_raw,
        [&](const std::vector<Event> &) {
            const auto pending = server->game().pending_replies(raw_player);
            return pending.size() == retained_reply_count &&
                   std::none_of(pending.begin(), pending.end(),
                                [&](const CommandReply &reply) { return reply.request_id == raw_request; });
        },
        3s));
    resumed_raw.close(resumed_raw_connection, "reply replay complete");
    CHECK(wait_until(
        *server, {&alice, &bob, &carol}, [&] { return !server->game().connected(raw_player); }, 3s, false));

    ClientConfig retained_config;
    retained_config.name = "Flood retained replies";
    retained_config.password = host.password;
    retained_config.resume_token = raw_token;
    NetworkClient retained_client(retained_config);
    CHECK(retained_client.connect(endpoint(*server)));
    std::vector<CommandReply> retained_replies;
    CHECK(wait_until(
        *server, {&alice, &bob, &carol, &retained_client},
        [&] {
            auto replies = retained_client.take_replies();
            retained_replies.insert(retained_replies.end(), replies.begin(), replies.end());
            return retained_client.ready() && server->game().pending_replies(raw_player).empty();
        },
        5s));
    CHECK(retained_client.ready());
    CHECK(retained_client.player() == raw_player);
    CHECK(retained_replies.size() == retained_reply_count);
    std::set<std::uint64_t> retained_ids;
    for (const auto &reply : retained_replies) {
        retained_ids.insert(reply.request_id);
        CHECK(reply.status == CommandReplyStatus::Rejected);
    }
    CHECK(retained_ids.size() == retained_reply_count);
    CHECK(!retained_ids.contains(raw_request));
    CHECK(server->game().pending_replies(raw_player).empty());

    alice.disconnect();
    CHECK(wait_until(
        *server, {&alice, &bob, &carol, &retained_client},
        [&] { return !server->game().connected(alice_id); }, 3s));
    ClientConfig resumed_alice_config;
    resumed_alice_config.name = "Alice on a fresh client";
    resumed_alice_config.password = host.password;
    resumed_alice_config.resume_token = alice_token;
    NetworkClient resumed_alice(resumed_alice_config);
    CHECK(resumed_alice.connect(endpoint(*server)));
    CHECK(wait_until(
        *server, {&resumed_alice, &bob, &carol, &retained_client}, [&] { return resumed_alice.ready(); },
        5s));
    CHECK(resumed_alice.player() == alice_id);
    CHECK(resumed_alice.resume_token() == alice_token);
    CHECK(server->game().world().residents().size() == people_before);
    GameCommand resumed_command;
    resumed_command.author = std::numeric_limits<PlayerId>::max();
    resumed_command.type = CommandType::Move;
    resumed_command.residents = {1};
    resumed_command.x = 23;
    resumed_command.y = 23;
    const auto resumed_request = resumed_alice.command(resumed_command);
    CHECK(resumed_request > request);
    bool resumed_applied = false;
    CHECK(wait_until(
        *server, {&resumed_alice, &bob, &carol, &retained_client},
        [&] {
            for (const auto &reply : resumed_alice.take_replies()) {
                if (reply.request_id == resumed_request && reply.author == alice_id &&
                    reply.status == CommandReplyStatus::Applied) {
                    resumed_applied = true;
                }
            }
            return resumed_applied;
        },
        3s));
    CHECK(resumed_applied);
    CHECK(wait_until(
        *server, {&resumed_alice, &bob, &carol, &retained_client},
        [&] { return resumed_alice.view().buildings.size() >= 2 && bob.view().buildings.size() >= 2; }, 3s));
}

void work_projection_save_reconnect_and_removal_over_transport() {
    ribterra::test::WorkProjectionSave saved;
    ribterra::World fixture(70421, 4);
    std::string error;
    const bool imported = fixture.import_state(ribterra::test::work_projection_state(), error);
    CHECK(imported);
    if (!imported) {
        std::cerr << error << '\n';
        return;
    }
    CHECK(fixture.save(saved.path, error));
    HostConfig config;
    config.save_path = saved.path;
    auto server = listening_server(config);
    CHECK(server != nullptr);
    if (!server)
        return;
    ClientConfig first_config;
    first_config.name = "Retry owner";
    ClientConfig late_config;
    late_config.name = "Late retry owner";
    NetworkClient first(first_config), late(late_config);
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first}, [&] { return first.ready(); }, 3s, false));
    const auto initial = first.view().resident_work;
    CHECK(initial.size() == 3);
    CHECK(initial == server->game().snapshot(first.player()).resident_work);
    CHECK(
        std::none_of(initial.begin(), initial.end(), [](const auto &work) { return work.resident_id == 4; }));
    CHECK(server->save());
    ribterra::World restored(1, 1);
    CHECK(restored.load(saved.path, error));
    CHECK(restored.checksum() == server->game().world().checksum());
    CHECK(late.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return late.ready(); }, 3s, false));
    CHECK(late.view().resident_work == initial);
    const auto player = first.player();
    first.disconnect();
    CHECK(wait_until(*server, {&late}, [&] { return !server->game().connected(player); }, 3s, false));
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return first.ready(); }, 3s, false));
    CHECK(first.player() == player && first.view().resident_work == initial);

    // Replace both the saved retry and normal exhaustion. The remote delta
    // removes their old work rows; internal work admission emits no reply.
    GameCommand move;
    move.type = CommandType::Move;
    move.residents = {1, 2, 3};
    move.x = 22;
    move.y = 25;
    const auto request = first.command(move);
    CHECK(request != 0);
    int terminals = 0;
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies()) {
                CHECK(reply.request_id == request);
                if (reply.status == CommandReplyStatus::Applied)
                    ++terminals;
                CHECK(reply.status != CommandReplyStatus::Rejected);
            }
            CHECK(late.take_replies().empty());
            return terminals == 1 && first.view().resident_work.empty() &&
                   late.view().resident_work.empty() &&
                   std::all_of(first.view().residents.begin(), first.view().residents.end(),
                               [](const auto &resident) { return resident.job == ribterra::Job::Idle; });
        },
        3s));
    CHECK(terminals == 1);
    CHECK(server->game().snapshot(player).resident_work.empty());
    CHECK(first.view().inventory == server->game().world().inventory(1));
}

void professions_partial_results_save_and_reconnect_over_transport() {
    ribterra::test::WorkProjectionSave saved;
    HostConfig config;
    config.save_path = saved.path;
    auto server = listening_server(config);
    CHECK(server != nullptr);
    if (!server)
        return;
    ClientConfig first_config;
    first_config.name = "Profession owner";
    ClientConfig late_config;
    late_config.name = "Late profession owner";
    NetworkClient first(first_config), late(late_config);
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first}, [&] { return first.ready(); }, 3s, false));
    CHECK(first.view().resident_professions.size() == 10);
    CHECK(first.view().resident_professions == server->game().snapshot(first.player()).resident_professions);
    GameCommand assign;
    assign.type = CommandType::AssignProfession;
    assign.residents = {1, 999};
    assign.assignment.profession = ribterra::Profession::Farmer;
    assign.assignment.priorities = {0, 0, 0, 0, 0};
    const auto request = first.command(assign);
    CHECK(request != 0);
    std::optional<CommandReply> result;
    CHECK(wait_until(
        *server, {&first},
        [&] {
            for (const auto &reply : first.take_replies()) {
                CHECK(reply.request_id == request);
                if (reply.status != CommandReplyStatus::Queued)
                    result = reply;
            }
            return result && first.view().resident_professions.front().assignment == assign.assignment;
        },
        3s));
    CHECK(result && result->status == CommandReplyStatus::Applied);
    CHECK(result && result->residents ==
                        std::vector<ribterra::ResidentCommandOutcome>(
                            {{1, ribterra::ResidentCommandStatus::Applied, ribterra::PathFailure::None},
                             {999, ribterra::ResidentCommandStatus::Rejected,
                              ribterra::PathFailure::ResidentUnavailable}}));
    CHECK(server->save());
    ribterra::World restored(1, 1);
    std::string error;
    CHECK(restored.load(saved.path, error));
    CHECK(restored.checksum() == server->game().world().checksum());
    CHECK(late.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return late.ready(); }, 3s, false));
    CHECK(late.view().resident_professions == first.view().resident_professions);
    const auto player = first.player();
    const auto profiles = first.view().resident_professions;
    first.disconnect();
    CHECK(wait_until(*server, {&late}, [&] { return !server->game().connected(player); }, 3s, false));
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return first.ready(); }, 3s, false));
    CHECK(first.player() == player && first.view().resident_professions == profiles);
    GameCommand stop;
    stop.type = CommandType::Stop;
    stop.residents = {1};
    const auto stop_request = first.command(stop);
    int terminals = 0;
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies()) {
                CHECK(reply.request_id == stop_request);
                if (reply.status == CommandReplyStatus::Applied) {
                    ++terminals;
                    CHECK(reply.residents.size() == 1 &&
                          reply.residents.front().status == ribterra::ResidentCommandStatus::Applied);
                }
                CHECK(reply.status != CommandReplyStatus::Rejected);
            }
            CHECK(late.take_replies().empty());
            return terminals == 1 &&
                   !first.view().resident_professions.front().assignment.automation_enabled &&
                   first.view().resident_professions == late.view().resident_professions &&
                   first.view().resident_professions == server->game().snapshot(player).resident_professions;
        },
        3s));
    CHECK(terminals == 1);
}

void logistics_projection_and_cancel_over_transport() {
    HostConfig config;
    auto server = listening_server(config);
    CHECK(server != nullptr);
    if (!server)
        return;
    ClientConfig first_config;
    first_config.name = "Carrier owner";
    ClientConfig second_config;
    second_config.name = "Late carrier observer";
    NetworkClient first(first_config), late(second_config);
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first}, [&] { return first.ready(); }, 3s, false));
    const auto initial = server->game().world().inventory(1).total;
    GameCommand build;
    build.type = CommandType::Build;
    build.x = 26;
    build.y = 24;
    const auto build_request = first.command(build);
    CHECK(build_request != 0);
    bool applied = false;
    CHECK(wait_until(
        *server, {&first},
        [&] {
            for (const auto &reply : first.take_replies())
                if (reply.request_id == build_request && reply.status == CommandReplyStatus::Applied)
                    applied = true;
            return applied && !first.view().construction_sites.empty() &&
                   std::any_of(first.view().resident_logistics.begin(), first.view().resident_logistics.end(),
                               [](const auto &resident) { return resident.quantity != 0; });
        },
        5s));
    CHECK(first.view().inventory.total.wood == initial.wood);
    CHECK(first.view().inventory.total.stone == initial.stone);
    CHECK(first.view().construction_sites.size() == 1);
    if (first.view().construction_sites.empty())
        return;
    const int site_id = first.view().construction_sites.front().building_id;
    CHECK(late.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return late.ready(); }, 3s, false));
    const auto authority = server->game().snapshot(late.player());
    CHECK(late.view().inventory == authority.inventory);
    CHECK(late.view().resident_logistics == authority.resident_logistics);
    CHECK(late.view().construction_sites == authority.construction_sites);
    const auto player = first.player();
    first.disconnect();
    CHECK(wait_until(*server, {&late}, [&] { return !server->game().connected(player); }, 3s, false));
    CHECK(first.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &late}, [&] { return first.ready(); }, 3s, false));
    CHECK(first.player() == player);
    CHECK(first.view().inventory == server->game().snapshot(player).inventory);
    CHECK(first.view().construction_sites == server->game().snapshot(player).construction_sites);
    first.take_replies();
    GameCommand cancel;
    cancel.type = CommandType::CancelConstruction;
    cancel.building_id = site_id;
    const auto cancel_request = first.command(cancel);
    CHECK(cancel_request > build_request);
    int terminals = 0;
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies()) {
                CHECK(reply.request_id == cancel_request);
                if (reply.request_id == cancel_request && reply.status == CommandReplyStatus::Applied)
                    ++terminals;
            }
            return terminals == 1 && first.view().construction_sites.empty() &&
                   late.view().construction_sites.empty();
        },
        5s));
    CHECK(terminals == 1);
    CHECK(server->game().world().inventory(1).total.wood == initial.wood);
    CHECK(server->game().world().inventory(1).total.stone == initial.stone);
    for (const auto &reservation : server->game().world().item_reservations()) {
        const auto &tasks = server->game().world().logistics_tasks();
        const auto task = std::find_if(tasks.begin(), tasks.end(),
                                       [&](const auto &item) { return item.task_id == reservation.task_id; });
        CHECK(task != tasks.end() && task->kind == ribterra::LogisticsTaskKind::HaulToStorage);
    }
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            const auto &inventory = first.view().inventory;
            return inventory.stored.wood == initial.wood && inventory.stored.stone == initial.stone &&
                   inventory.carried.wood == 0 && inventory.carried.stone == 0 &&
                   inventory.reserved.wood == 0 && inventory.reserved.stone == 0;
        },
        5s));
    CHECK(server->game().world().inventory(1).total.wood == initial.wood);
    CHECK(server->game().world().inventory(1).total.stone == initial.stone);
    CHECK(server->game().world().item_reservations().empty());

    const auto second_build = first.command(build);
    CHECK(second_build > cancel_request);
    bool second_applied = false;
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            for (const auto &reply : first.take_replies())
                if (reply.request_id == second_build) {
                    if (reply.status == CommandReplyStatus::Rejected)
                        std::cerr << "Second construction rejected: " << reply.reason << '\n';
                    CHECK(reply.status != CommandReplyStatus::Rejected);
                    second_applied |= reply.status == CommandReplyStatus::Applied;
                }
            return second_applied;
        },
        3s));
    bool saw_workforce = false;
    CHECK(wait_until(
        *server, {&first, &late},
        [&] {
            for (const auto &site : first.view().construction_sites)
                if (site.phase == ribterra::ConstructionPhase::Work) {
                    CHECK(site.worker_phase != ribterra::ConstructionWorkerPhase::None);
                    CHECK((site.assigned_workers == 0) ==
                          (site.worker_phase == ribterra::ConstructionWorkerPhase::Waiting));
                    saw_workforce = true;
                }
            return saw_workforce && first.view().construction_sites.empty() &&
                   late.view().construction_sites.empty() && first.view().buildings.size() == 2 &&
                   first.view().buildings.back().progress == 1.F;
        },
        5s));
    CHECK(saw_workforce);
    if (!saw_workforce) {
        const auto &world = server->game().world();
        std::cerr << "Construction diagnostic at tick " << world.ticks() << ": "
                  << world.path_diagnostics().queued_batches << " pending paths\n";
        for (const auto &site : world.construction_orders())
            std::cerr << "site " << site.building_id << " phase " << static_cast<int>(site.phase)
                      << " delivered " << site.delivered.wood << '/' << site.delivered.stone << '\n';
        for (const auto &task : world.logistics_tasks())
            std::cerr << "task " << task.task_id << " kind " << static_cast<int>(task.kind) << " phase "
                      << static_cast<int>(task.phase) << " failure " << static_cast<int>(task.failure)
                      << " resident " << task.resident_id << " destination " << task.destination_building_id
                      << '\n';
        for (const auto &resident : world.residents())
            std::cerr << "resident " << resident.id << " job " << static_cast<int>(resident.job) << " order "
                      << static_cast<int>(resident.order_phase) << " position " << resident.x << ','
                      << resident.y << '\n';
    }
}

std::size_t snapshot_updates_at_speed(int speed, int starting_phase = 0) {
    HostConfig host;
    host.game.lobby_enabled = true;
    host.game.max_players = 2;
    host.game.population = 3;
    auto server = listening_server(host);
    CHECK(server != nullptr);
    if (!server)
        return 0;
    NetworkClient first(ClientConfig{"Cadence host"}), second(ClientConfig{"Cadence guest"});
    CHECK(first.connect(endpoint(*server)) && second.connect(endpoint(*server)));
    CHECK(wait_until(*server, {&first, &second}, [&] { return first.ready() && second.ready(); }, 3s,
                     false));
    const auto command = [&](NetworkClient &client, ribterra::LobbyAction action, int value = 0) {
        GameCommand request;
        request.type = CommandType::Lobby;
        request.x = static_cast<int>(action);
        request.y = value;
        const auto id = client.command(request);
        bool applied = false;
        CHECK(wait_until(
            *server, {&first, &second},
            [&] {
                for (const auto &reply : client.take_replies())
                    if (reply.request_id == id)
                        applied |= reply.status == CommandReplyStatus::Applied;
                return applied;
            },
            3s, false));
    };
    command(first, ribterra::LobbyAction::Ready, 1);
    command(second, ribterra::LobbyAction::Ready, 1);
    // UDP connection acceptance order is not the construction order.
    auto &host_client = first.view().lobby.host == first.player() ? first : second;
    command(host_client, ribterra::LobbyAction::Start);
    // A saved world can resume at a tick that is not divisible by four.
    // Advancing by four from that phase must not starve both clients forever.
    for (int phase = 0; phase < starting_phase; ++phase)
        server->tick();
    command(host_client, ribterra::LobbyAction::Speed, speed);
    CHECK(wait_until(*server, {&first, &second},
                     [&] {
                         return first.view().revision == server->game().revision() &&
                                second.view().revision == server->game().revision();
                     },
                     3s, false));
    for (int settle = 0; settle < 2; ++settle)
        service(*server, {&first, &second}, false);

    std::size_t updates = 0;
    auto revision = first.view().revision;
    for (int host_tick = 0; host_tick < 20; ++host_tick) {
        server->tick();
        const bool boundary = (host_tick + 1 + starting_phase) % 4 == 0;
        if (boundary)
            CHECK(wait_until(*server, {&first, &second},
                             [&] {
                                 return first.view().revision != revision &&
                                        second.view().revision == first.view().revision;
                             },
                             1s, false));
        else
            for (int exchange = 0; exchange < 2; ++exchange)
                service(*server, {&first, &second}, false);
        if (first.view().revision != revision) {
            revision = first.view().revision;
            ++updates;
        }
        CHECK(second.view().revision == first.view().revision);
    }
    server->shutdown();
    return updates;
}

void snapshot_cadence_is_independent_of_simulation_speed() {
    for (const int speed : {1, 2, 4})
        CHECK(snapshot_updates_at_speed(speed) == 5);
    CHECK(snapshot_updates_at_speed(4, 3) == 5);
}

#include "network_control_tests.inc"
#include "network_recovery_tests.inc"
} // namespace

int main() {
    snapshot_cadence_is_independent_of_simulation_speed();
    full_network_session();
    manual_control_paused_transport_and_reconnect();
    for (const auto mode : {RecoveryInterruption::BeforeAcceptance, RecoveryInterruption::AfterAcceptance,
                            RecoveryInterruption::ExpiredIntent, RecoveryInterruption::ChangedEpoch})
        client_recovers_uncertain_commands(mode);
    reordered_and_duplicate_replies();
    logistics_projection_and_cancel_over_transport();
    work_projection_save_reconnect_and_removal_over_transport();
    professions_partial_results_save_and_reconnect_over_transport();
    if (failures != 0) {
        std::cerr << failures << " network session test(s) failed.\n";
        return 1;
    }
    std::cout << "Two-client authority, convergence, chat, rejection, reconnect and port checks "
                 "passed.\n";
    return 0;
}

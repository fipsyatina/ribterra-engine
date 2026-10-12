// Deliberately interrupt between transport delivery and application acceptance,
// or between application acceptance and terminal receipt. This tests uncertainty
// independently of GNS's graceful-close flushing of reliable datagrams.
enum class RecoveryInterruption { BeforeAcceptance, AfterAcceptance, ExpiredIntent, ChangedEpoch };

void client_recovers_uncertain_commands(RecoveryInterruption mode) {
    using namespace ribterra;
    auto clock_offset = 0s;
    ServerConfig settings;
    settings.population = 1;
    GameServer game(settings);
    NetTransport host;
    bool listening = false;
    for (std::uint16_t port = 27620; port < 27720 && !listening; ++port)
        listening = host.listen("127.0.0.1", port);
    CHECK(listening);
    if (!listening)
        return;
    ClientConfig config;
    config.name = "Recovery player";
    config.auto_reconnect = true;
    config.clock = [&] { return std::chrono::steady_clock::now() + clock_offset; };
    NetworkClient client(config);
    CHECK(client.connect("127.0.0.1:" + std::to_string(host.local_port())));
    struct Peer {
        PlayerId player{};
        bool ready{};
        std::uint64_t revision{};
        std::set<ReplyReceipt> sent;
    };
    std::map<std::uint32_t, Peer> peers;
    const auto world_id = game.world().export_state().world_id;
    std::uint64_t sequence = 0;
    int connections = 0, command_packets = 0;
    bool interrupted = false;
    std::optional<GameCommand> original_command;
    auto send = [&](std::uint32_t connection, ribterra::net::Payload payload) {
        CHECK(host.send(connection, ribterra::net::encode({++sequence, std::move(payload)})));
    };
    auto service_recovery = [&] {
        for (const auto &event : host.poll()) {
            if (event.type == Event::Type::Connected) {
                ++connections;
                peers.emplace(event.connection, Peer{});
                send(event.connection, Challenge{std::string(64, 'a'), false, Versions{}});
                continue;
            }
            const auto found = peers.find(event.connection);
            if (found == peers.end())
                continue;
            if (event.type == Event::Type::Disconnected) {
                game.disconnect(found->second.player);
                peers.erase(found);
                continue;
            }
            std::string error;
            const auto packet = ribterra::net::decode(event.bytes, error);
            CHECK(packet.has_value());
            if (!packet)
                continue;
            auto &peer = found->second;
            if (const auto *hello = std::get_if<Hello>(&packet->payload)) {
                const auto joined = game.join(hello->name, hello->versions, hello->resume_token);
                CHECK(joined.accepted);
                if (!joined.accepted)
                    continue;
                peer.player = joined.player;
                send(event.connection, Welcome{joined.player, joined.resume_token, world_id, joined.next_request});
                // A resumed peer receives retained history; the existing client
                // must not show the same entry a second time.
                send(event.connection, ChatEntry{1, joined.player, "Recovery player", "Retained chat"});
            } else if (std::holds_alternative<SnapshotAck>(packet->payload)) {
                peer.ready = true;
            } else if (const auto *ack = std::get_if<ReplyAck>(&packet->payload)) {
                game.acknowledge_replies(peer.player, ack->receipts);
            } else if (const auto *command = std::get_if<GameCommand>(&packet->payload)) {
                ++command_packets;
                if (!interrupted) {
                    interrupted = true;
                    original_command = *command;
                    if (mode == RecoveryInterruption::AfterAcceptance) {
                        CHECK(game.enqueue(peer.player, *command).status == CommandReplyStatus::Queued);
                        game.pump_boundary();
                    }
                    if (mode == RecoveryInterruption::ExpiredIntent)
                        clock_offset += 31s;
                    game.disconnect(peer.player);
                    host.close(event.connection, "Injected interruption at application acceptance boundary");
                    peers.erase(found);
                    continue;
                }
                CHECK(original_command && *original_command == *command);
                const auto reply = game.enqueue(peer.player, *command);
                CHECK(reply.status != CommandReplyStatus::Invalid);
                send(event.connection, reply);
            }
        }
        game.pump_boundary();
        for (auto &[connection, peer] : peers) {
            if (!peer.player || !peer.ready)
                continue;
            if (peer.revision != game.revision()) {
                auto view = game.snapshot(peer.player);
                if (connections > 1 && mode == RecoveryInterruption::ChangedEpoch)
                    ++view.control_epoch;
                for (const auto &part : ribterra::net::split_snapshot(ribterra::net::make_snapshot(view, world_id)))
                    send(connection, part);
                peer.revision = game.revision();
            }
            for (const auto &reply : game.pending_replies(peer.player))
                if (peer.sent.insert({reply.request_id, reply.status}).second)
                    send(connection, reply);
        }
        client.poll();
    };
    auto wait_recovery = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            service_recovery();
            if (predicate())
                return true;
            std::this_thread::sleep_for(2ms);
        }
        std::cerr << "Recovery mode " << static_cast<int>(mode) << ": " << client.last_error() << '\n';
        return false;
    };
    CHECK(wait_recovery([&] { return client.ready(); }));
    if (!client.ready())
        return;
    const auto initial_chat = client.take_chat();
    CHECK(initial_chat.size() == 1);
    const auto player = client.player();
    const auto token = client.resume_token();
    GameCommand move;
    move.type = CommandType::Move;
    move.residents = {1};
    move.x = 26;
    move.y = 25;
    const auto request = client.command(move);
    CHECK(request != 0);
    std::vector<CommandReply> terminals;
    const bool should_recover = mode == RecoveryInterruption::BeforeAcceptance ||
                                mode == RecoveryInterruption::AfterAcceptance;
    CHECK(wait_recovery([&] {
        for (const auto &reply : client.take_replies())
            if (reply.status == CommandReplyStatus::Applied || reply.status == CommandReplyStatus::Rejected)
                terminals.push_back(reply);
        return connections >= 2 && client.ready() && !client.reconnecting() &&
               (should_recover ? !terminals.empty() : client.recovery_incomplete());
    }));
    CHECK(client.player() == player && client.resume_token() == token);
    CHECK(client.take_chat().empty());
    if (should_recover) {
        CHECK(terminals.size() == 1);
        CHECK(!client.recovery_incomplete());
        if (!terminals.empty()) {
            CHECK(terminals.front().request_id == request);
            CHECK(terminals.front().status == CommandReplyStatus::Applied);
            CHECK(terminals.front().residents.size() == 1);
        }
        CHECK(game.world().manual_orders().size() == 1);
        if (mode == RecoveryInterruption::BeforeAcceptance)
            CHECK(command_packets == 2);
        else
            CHECK(command_packets == 1 || command_packets == 2);
    } else {
        CHECK(terminals.empty());
        CHECK(command_packets == 1);
        CHECK(game.world().manual_orders().empty());
    }
    client.disconnect();
    clock_offset += 2s;
    const auto explicit_disconnect_connections = connections;
    for (int poll = 0; poll < 30; ++poll) {
        service_recovery();
        std::this_thread::sleep_for(2ms);
    }
    CHECK(connections == explicit_disconnect_connections);
    CHECK(!client.ready() && !client.reconnecting());
}

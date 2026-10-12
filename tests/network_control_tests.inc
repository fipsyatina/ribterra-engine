void manual_control_paused_transport_and_reconnect() {
    using namespace ribterra;
    auto now = std::chrono::steady_clock::time_point{};
    HostConfig config;
    config.game.population = 3;
    config.game.clock = [&] { return now; };
    auto server = listening_server(config);
    CHECK(server != nullptr);
    if (!server)
        return;
    ClientConfig first_config;
    first_config.name = "Controller A";
    NetworkClient first(std::move(first_config));
    ClientConfig second_config;
    second_config.name = "Controller B";
    NetworkClient second(std::move(second_config));
    CHECK(first.connect(endpoint(*server)) && second.connect(endpoint(*server)));
    CHECK(wait_until(
        *server, {&first, &second},
        [&] {
            return first.ready() && second.ready() && first.view().control_players.size() == 2 &&
                   same_shared_view(first, second);
        },
        3s, false));
    if (!first.ready() || !second.ready())
        return;
    auto send = [&](NetworkClient &client, GameCommand command) {
        const auto request = client.command(std::move(command));
        CHECK(request != 0);
        CommandReply result;
        std::size_t terminals = 0;
        CHECK(wait_until(
            *server, {&first, &second},
            [&] {
                for (const auto &reply : client.take_replies())
                    if (reply.request_id == request && (reply.status == CommandReplyStatus::Applied ||
                                                        reply.status == CommandReplyStatus::Rejected)) {
                        result = reply;
                        ++terminals;
                    }
                return terminals != 0 && first.view().revision == server->game().revision() &&
                       second.view().revision == server->game().revision() && same_shared_view(first, second);
            },
            3s, false));
        CHECK(terminals == 1);
        return result;
    };
    GameCommand move;
    move.type = CommandType::Move;
    move.residents = {1};
    move.x = 26;
    move.y = 25;
    const auto accepted = send(first, move);
    CHECK(accepted.status == CommandReplyStatus::Applied && accepted.residents.size() == 1);
    if (accepted.residents.empty())
        return;
    const auto order = accepted.residents.front().order_id;
    CHECK(order != 0 && first.view().manual_orders.size() == 1);
    CHECK(first.view().manual_orders.front().phase == ManualOrderPhase::Queued);
    CHECK(first.view().resident_controls.front().held_by_recipient);
    CHECK(!second.view().resident_controls.front().held_by_recipient);
    auto conflict = send(second, move);
    CHECK(conflict.status == CommandReplyStatus::Rejected);
    CHECK(conflict.control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));
    GameCommand transfer;
    transfer.type = CommandType::TransferControl;
    transfer.residents = {1};
    transfer.target_player = second.player();
    const auto before_transfer = server->game().world().checksum();
    CHECK(send(first, transfer).status == CommandReplyStatus::Applied);
    CHECK(server->game().world().checksum() == before_transfer);
    CHECK(second.view().resident_controls.front().held_by_recipient);
    GameCommand cancel;
    cancel.type = CommandType::CancelManualOrder;
    cancel.manual_order_id = order;
    CHECK(send(first, cancel).control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));
    CHECK(send(second, cancel).status == CommandReplyStatus::Applied);
    CHECK(send(second, move).status == CommandReplyStatus::Applied);
    const auto second_id = second.player();
    const auto token = second.resume_token();
    second.disconnect();
    CHECK(wait_until(
        *server, {&first}, [&] { return first.view().resident_controls.front().disconnected_grace; }, 3s,
        false));
    now += 29s;
    service(*server, {&first}, false);
    CHECK(server->game().snapshot(first.player()).resident_controls.front().holder_id == second_id);
    CHECK(second.connect(endpoint(*server)));
    CHECK(wait_until(
        *server, {&first, &second},
        [&] {
            return second.ready() && same_shared_view(first, second) &&
                   second.view().resident_controls.front().held_by_recipient;
        },
        3s, false));
    CHECK(second.player() == second_id && second.resume_token() == token);
    second.disconnect();
    CHECK(wait_until(
        *server, {&first}, [&] { return first.view().resident_controls.front().disconnected_grace; }, 3s,
        false));
    // Keep servicing connected peers while the lease clock advances. An atomic
    // 31-second jump also expires an unrelated snapshot ACK still in transit.
    for (int second_elapsed = 0; second_elapsed < 31; ++second_elapsed) {
        now += 1s;
        service(*server, {&first}, false);
        std::this_thread::sleep_for(5ms);
    }
    CHECK(wait_until(
        *server, {&first}, [&] { return first.view().resident_controls.front().holder_id == 0; }, 3s, false));
    CHECK(server->game().world().ticks() == 0);
    CHECK(std::none_of(first.view().manual_orders.begin(), first.view().manual_orders.end(),
                       [](const auto &entry) {
                           return entry.phase == ManualOrderPhase::Queued ||
                                  entry.phase == ManualOrderPhase::WaitingBoundary;
                       }));
    CHECK(second.connect(endpoint(*server)));
    CHECK(wait_until(
        *server, {&first, &second}, [&] { return second.ready() && same_shared_view(first, second); }, 3s,
        false));
    CHECK(!second.view().resident_controls.front().held_by_recipient);
    CHECK(send(first, move).status == CommandReplyStatus::Applied);
    GameCommand release;
    release.type = CommandType::ReleaseControl;
    release.residents = {1};
    CHECK(send(first, release).status == CommandReplyStatus::Applied);
    CHECK(first.view().resident_controls.front().holder_id == 0);
    CHECK(server->game().world().ticks() == 0);
    // Drain the final paused snapshot acknowledgement before testing cadence.
    CHECK(!wait_until(
        *server, {&first, &second}, [&] { return first.view().tick != 0 || second.view().tick != 0; }, 50ms,
        false));
    // Ordinary simulation keeps the established five snapshots per second;
    // paused control updates above do not turn every poll/tick into a snapshot.
    for (int tick = 0; tick < 3; ++tick) {
        server->tick();
        service(*server, {&first, &second}, false);
    }
    CHECK(!wait_until(
        *server, {&first, &second}, [&] { return first.view().tick != 0 || second.view().tick != 0; }, 100ms,
        false));
    server->tick();
    CHECK(wait_until(
        *server, {&first, &second},
        [&] { return first.view().tick == 4 && second.view().tick == 4 && same_shared_view(first, second); },
        3s, false));
    const auto stable_revision = server->game().revision();
    for (int poll = 0; poll < 10; ++poll)
        service(*server, {&first, &second}, false);
    CHECK(server->game().revision() == stable_revision);
    // Two cadence boundaries before either client can ACK. The second must
    // remain pending even when simulation stops immediately afterward.
    for (int tick = 0; tick < 8; ++tick)
        server->tick();
    CHECK(wait_until(
        *server, {&first, &second},
        [&] { return first.view().tick == 12 && second.view().tick == 12 && same_shared_view(first, second); },
        3s, false));
}

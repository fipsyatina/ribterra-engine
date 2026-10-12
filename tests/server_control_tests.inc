void manual_control_leases_are_ordered_and_expire_while_paused() {
    auto now = std::chrono::steady_clock::time_point{};
    ServerConfig config;
    config.population = 3;
    config.clock = [&] { return now; };
    GameServer server(config);
    const auto first = server.join("First controller", config.versions);
    const auto second = server.join("Second controller", config.versions);
    const auto observer = server.join("Observer", config.versions, {}, true);
    const auto initial = server.snapshot(first.player);
    CHECK(initial.control_epoch != 0 && initial.control_players.size() == 2);
    CHECK(std::all_of(initial.resident_controls.begin(), initial.resident_controls.end(),
                      [](const auto &control) { return control.holder_id == 0; }));
    auto run = [&](PlayerId player, GameCommand command) {
        command.author = player;
        command.control_epoch = server.snapshot(player).control_epoch;
        CHECK(server.enqueue(player, command).status == CommandReplyStatus::Queued);
        server.pump_boundary();
        auto replies = server.take_replies(player);
        CHECK(replies.size() == 2 && terminal(replies.back().status));
        return replies.empty() ? CommandReply{} : replies.back();
    };
    GameCommand move{1, first.player, 0, CommandType::Move, {1, 999}, 26, 25};
    move.control_epoch = initial.control_epoch;
    const auto accepted = run(first.player, move);
    CHECK(accepted.status == CommandReplyStatus::Applied && accepted.residents.size() == 2);
    CHECK(accepted.residents.front().order_id != 0);
    CHECK(accepted.residents.back().status == ResidentCommandStatus::Rejected);
    CHECK(server.world().ticks() == 0);
    const auto unchanged = server.world().checksum();
    CHECK(server.enqueue(first.player, move) == accepted);
    CHECK(server.world().checksum() == unchanged && server.pending_replies(first.player).empty());
    GameCommand assign{1, second.player, 0, CommandType::AssignProfession, {1, 2}};
    assign.assignment.profession = Profession::Hauler;
    const auto partial = run(second.player, assign);
    CHECK(partial.status == CommandReplyStatus::Applied);
    CHECK(partial.control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));
    CHECK(server.snapshot(first.player).resident_controls[0].held_by_recipient);
    CHECK(!server.snapshot(second.player).resident_controls[0].held_by_recipient);
    CHECK(server.snapshot(second.player).resident_controls[1].held_by_recipient);

    GameCommand transfer{2, first.player, 0, CommandType::TransferControl, {1}};
    transfer.target_player = observer.player;
    const auto bad_transfer = run(first.player, transfer);
    CHECK(bad_transfer.control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::TargetUnavailable}}));
    transfer.request_id = 3;
    transfer.target_player = second.player;
    const auto before_transfer = server.world().checksum();
    CHECK(run(first.player, transfer).status == CommandReplyStatus::Applied);
    CHECK(server.world().checksum() == before_transfer);
    CHECK(server.snapshot(second.player).resident_controls[0].held_by_recipient);
    GameCommand release{4, first.player, 0, CommandType::ReleaseControl, {1}};
    CHECK(run(first.player, release).control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));
    GameCommand cancel;
    cancel.request_id = 2;
    cancel.author = second.player;
    cancel.type = CommandType::CancelManualOrder;
    cancel.manual_order_id = accepted.residents.front().order_id;
    CHECK(run(second.player, cancel).status == CommandReplyStatus::Applied);
    move.request_id = 3;
    move.residents = {1};
    CHECK(run(second.player, move).status == CommandReplyStatus::Applied);

    server.disconnect(second.player);
    now += std::chrono::seconds(29);
    server.pump_boundary();
    auto control = server.snapshot(first.player).resident_controls[0];
    CHECK(control.holder_id == second.player && control.disconnected_grace);
    const auto rejoined = server.join("Same session", config.versions, second.resume_token);
    CHECK(rejoined.accepted && rejoined.player == second.player);
    CHECK(server.snapshot(second.player).resident_controls[0].held_by_recipient);
    server.disconnect(second.player);
    now += std::chrono::seconds(31);
    server.pump_boundary();
    CHECK(server.world().ticks() == 0);
    CHECK(server.snapshot(first.player).resident_controls[0].holder_id == 0);
    CHECK(server.snapshot(first.player).resident_controls[1].holder_id == 0);
    move.request_id = 5;
    CHECK(run(first.player, move).status == CommandReplyStatus::Applied);
    const auto expired_rejoin = server.join("Expired holder", config.versions, second.resume_token);
    CHECK(expired_rejoin.accepted);
    CHECK(!server.snapshot(second.player).resident_controls[0].held_by_recipient);
    move.request_id = 4;
    CHECK(run(second.player, move).control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::HeldByAnother}}));

    test::WorkProjectionSave saved;
    std::string error;
    CHECK(server.save(saved.path, error));
    const auto old_epoch = server.snapshot(first.player).control_epoch;
    const auto orders = server.world().manual_orders();
    CHECK(server.load(saved.path, error));
    CHECK(!server.join("Old credential", config.versions, first.resume_token).accepted);
    const auto fresh = server.join("New epoch", config.versions);
    CHECK(fresh.accepted && server.world().manual_orders() == orders);
    const auto view = server.snapshot(fresh.player);
    CHECK(view.control_epoch != old_epoch);
    CHECK(std::all_of(view.resident_controls.begin(), view.resident_controls.end(),
                      [](const auto &value) { return value.holder_id == 0; }));
    cancel.request_id = 1;
    cancel.author = fresh.player;
    cancel.control_epoch = old_epoch;
    const auto before_stale = server.world().checksum();
    const auto stale = server.enqueue(fresh.player, cancel);
    CHECK(stale.status == CommandReplyStatus::Rejected);
    CHECK(stale.control_rejections ==
          std::vector<ResidentControlRejection>({{1, ControlFailure::StaleEpoch}}));
    CHECK(server.world().checksum() == before_stale);
}

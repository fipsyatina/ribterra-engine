void manual_queue_and_control_wire_are_strict() {
    constexpr auto order_id = (std::uint64_t{1} << 40) + 3;
    auto baseline = sample_view();
    baseline.control_epoch = (std::uint64_t{1} << 48) + 9;
    baseline.control_players = {{1, "Controller"}, {2, "Recipient"}};
    baseline.resident_controls = {{7, 1, "Controller", true, false, 19}};
    baseline.manual_orders = {{order_id, 7, ManualOrderKind::Gather, CellCoord{0, 0, WorldLayer::Surface},
                               GatherCompletion::OneDelivery, ManualOrderPhase::Active, PathFailure::None,
                               ManualOrderCompletion::None},
                              {order_id + (std::uint64_t{1} << 32), 7, ManualOrderKind::Move, std::nullopt,
                               GatherCompletion::UntilExhausted, ManualOrderPhase::Queued, PathFailure::None,
                               ManualOrderCompletion::None}};
    const auto id = world_id(15);
    ViewSnapshot view;
    WorldId applied_id{};
    std::string error;
    const auto encoded = encode({1, make_snapshot(baseline, id)});
    auto decoded = decode(encoded, error);
    CHECK(decoded.has_value());
    if (!decoded)
        return;
    CHECK(apply_snapshot(view, applied_id, std::get<Snapshot>(decoded->payload), error));
    CHECK(same_view(view, baseline));

    auto current = baseline;
    ++current.revision;
    current.manual_orders.front().phase = ManualOrderPhase::Complete;
    current.manual_orders.front().completion = ManualOrderCompletion::Delivered;
    current.manual_orders.erase(current.manual_orders.begin() + 1);
    current.resident_controls.front() = {7, 2, "Recipient", false, false, 20};
    const auto delta = make_snapshot(current, id, &baseline);
    CHECK(delta.removed_manual_orders == std::vector<std::uint64_t>{order_id + (std::uint64_t{1} << 32)});
    CHECK(delta.manual_orders.size() == 1 && delta.manual_orders.front().order_id == order_id);
    decoded = decode(encode({2, delta}), error);
    CHECK(decoded.has_value());
    if (decoded)
        CHECK(apply_snapshot(view, applied_id, std::get<Snapshot>(decoded->payload), error));
    CHECK(same_view(view, current));
    auto cancelled = current;
    ++cancelled.revision;
    cancelled.manual_orders.front().phase = ManualOrderPhase::Cancelled;
    cancelled.manual_orders.front().completion = ManualOrderCompletion::Cancelled;
    decoded = decode(encode({3, make_snapshot(cancelled, id, &current)}), error);
    CHECK(decoded.has_value());
    if (decoded)
        CHECK(apply_snapshot(view, applied_id, std::get<Snapshot>(decoded->payload), error));
    CHECK(same_view(view, cancelled));
    CHECK(view.resident_controls == current.resident_controls);
    current = cancelled;
    auto removed = current;
    ++removed.revision;
    removed.manual_orders.clear();
    removed.resident_controls.clear();
    CHECK(apply_snapshot(view, applied_id, make_snapshot(removed, id, &current), error));
    CHECK(same_view(view, removed));

    auto reject = [&](Snapshot invalid) {
        const auto unchanged = view;
        const auto unchanged_id = applied_id;
        CHECK(!apply_snapshot(view, applied_id, invalid, error));
        CHECK(same_view(view, unchanged) && applied_id == unchanged_id);
    };
    auto invalid = make_snapshot(baseline, world_id(16));
    invalid.manual_orders.front().resident_id = 999;
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    invalid.manual_orders.front().target = CellCoord{100, 100, WorldLayer::Surface};
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    invalid.resident_controls.front().holder_name = "Wrong identity";
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    invalid.manual_orders.front().phase = ManualOrderPhase::Failed;
    reject(invalid);
    const auto cancelled_packet = Packet{7, make_snapshot(cancelled, world_id(16))};
    auto wrong_phase = mutated_wire_packet(cancelled_packet, [](auto &packet) {
        packet.payload.AsSnapshot()->manual_orders.front()->phase =
            ribterra::wire::ManualOrderPhase::Complete;
    });
    CHECK(!decode(wrong_phase, error));
    CHECK(error == "Invalid manual order");
    auto wrong_completion = mutated_wire_packet(cancelled_packet, [](auto &packet) {
        auto &order = *packet.payload.AsSnapshot()->manual_orders.front();
        order.kind = ribterra::wire::ManualOrderKind::Move;
        order.gather_completion = ribterra::wire::GatherCompletion::UntilExhausted;
        order.completion = ribterra::wire::ManualOrderCompletion::Arrived;
    });
    CHECK(!decode(wrong_completion, error));
    CHECK(error == "Invalid manual order");
    invalid = make_snapshot(baseline, world_id(16));
    invalid.manual_orders.front().phase = ManualOrderPhase::Complete;
    invalid.manual_orders.front().completion = ManualOrderCompletion::Delivered;
    invalid.manual_orders.front().gather_completion = GatherCompletion::UntilExhausted;
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    invalid.residents.front().pending_path_batch = 77;
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    invalid.residents.front().nation = 2;
    reject(invalid);
    invalid = make_snapshot(baseline, world_id(16));
    while (invalid.manual_orders.size() < 9) {
        auto order = invalid.manual_orders.front();
        order.order_id += invalid.manual_orders.size();
        invalid.manual_orders.push_back(order);
    }
    reject(invalid);
    auto later = removed;
    ++later.revision;
    invalid = make_snapshot(later, id, &removed);
    ++invalid.control_epoch;
    reject(invalid);

    GameCommand command;
    command.request_id = 31;
    command.author = 1;
    command.issued_tick = 17;
    command.type = CommandType::CancelManualOrder;
    command.manual_order_id = order_id;
    command.control_epoch = baseline.control_epoch;
    decoded = decode(encode({3, command}), error);
    CHECK(decoded.has_value());
    if (decoded) {
        const auto &actual = std::get<GameCommand>(decoded->payload);
        CHECK(actual.manual_order_id == order_id && actual.control_epoch == baseline.control_epoch);
    }
    CommandReply reply{31, 17, 1, CommandReplyStatus::Applied, PathFailure::None, "Accepted"};
    reply.residents = {{7, ResidentCommandStatus::Applied, PathFailure::None, order_id},
                       {8, ResidentCommandStatus::Rejected, PathFailure::ReservationConflict, 0}};
    reply.control_rejections = {{8, ControlFailure::HeldByAnother}};
    decoded = decode(encode({4, reply}), error);
    CHECK(decoded.has_value());
    if (decoded)
        CHECK(std::get<CommandReply>(decoded->payload) == reply);
    auto malformed = mutated_wire_packet(
        {5, reply}, [](auto &packet) { packet.payload.AsReply()->resident_order_ids.pop_back(); });
    CHECK(!decode(malformed, error));
    malformed = mutated_wire_packet({6, reply}, [](auto &packet) {
        packet.payload.AsReply()->control_rejections.front() =
            ribterra::wire::ResidentControlRejection(7, ribterra::wire::ControlFailure::HeldByAnother);
    });
    CHECK(!decode(malformed, error));
}

static bool validate_manual(const WorldState &state, std::string &error) {
    const auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    if (!state.next_manual_order_id || !state.manual_order_cursor_id ||
        state.manual_order_cursor_id > state.next_manual_order_id ||
        state.manual_orders.size() > max_manual_orders)
        return fail("Manual order counters are invalid.");
    std::map<int, const Resident *> actors;
    std::map<int, const RouteState *> routes;
    std::map<std::uint64_t, const PathBatchState *> paths;
    std::map<std::uint64_t, const WorkIntentState *> work;
    std::map<std::uint64_t, const LogisticsTaskState *> tasks;
    std::set<int> work_actors, physical_actors;
    for (const auto &r : state.residents)
        actors[r.id] = &r;
    for (const auto &r : state.routes)
        routes[r.resident_id] = &r;
    for (const auto &b : state.path_batches)
        paths[b.path_batch_id] = &b;
    for (const auto &w : state.work_intents) {
        work[w.intent_id] = &w;
        if (active_work(w))
            work_actors.insert(w.resident_id);
    }
    for (const auto &t : state.logistics_tasks) {
        tasks[t.task_id] = &t;
        if (t.phase != LogisticsPhase::Complete && t.phase != LogisticsPhase::Failed && t.resident_id)
            physical_actors.insert(t.resident_id);
    }
    const auto gather_job = [](Job job) {
        return job == Job::Woodcut || job == Job::Mine || job == Job::Forage;
    };
    std::map<int, std::size_t> counts;
    std::map<std::uint64_t, const ManualOrderState *> orders;
    std::uint64_t previous = 0;
    std::size_t terminal = 0;
    for (const auto &o : state.manual_orders) {
        if (!o.order_id || o.order_id <= previous || o.order_id >= state.next_manual_order_id ||
            !actors.contains(o.resident_id) || o.owner_nation != actors.at(o.resident_id)->nation ||
            static_cast<unsigned>(o.request.kind) > static_cast<unsigned>(ManualOrderKind::Legacy) ||
            static_cast<unsigned>(o.request.gather_completion) >
                static_cast<unsigned>(GatherCompletion::OneDelivery) ||
            static_cast<unsigned>(o.phase) > static_cast<unsigned>(ManualOrderPhase::Cancelled) ||
            static_cast<unsigned>(o.failure) > static_cast<unsigned>(PathFailure::ReservationConflict) ||
            static_cast<unsigned>(o.completion) > static_cast<unsigned>(ManualOrderCompletion::Cancelled) ||
            static_cast<unsigned>(o.executor) > static_cast<unsigned>(ManualExecutorKind::Logistics) ||
            (!o.request.target && o.request.kind != ManualOrderKind::Legacy) ||
            (o.request.target && (!inside_world(o.request.target->x, o.request.target->y) ||
                                  !valid_layer(o.request.target->layer))) ||
            (o.request.kind != ManualOrderKind::Gather &&
             o.request.gather_completion != GatherCompletion::UntilExhausted) ||
            (o.request.kind == ManualOrderKind::Legacy && !o.legacy_compatibility) ||
            (o.issuer && (!o.issuer->player_id || !o.issuer->request_id)) ||
            (o.legacy_compatibility && (o.issuer || o.issued_tick)) ||
            (!o.legacy_compatibility && !o.issued_tick) || (o.issued_tick && *o.issued_tick > state.ticks) ||
            (o.activated_tick &&
             (*o.activated_tick > state.ticks || (o.issued_tick && *o.activated_tick < *o.issued_tick))) ||
            o.attempts > max_work_admission_attempts ||
            o.next_attempt_tick >
                state.ticks +
                    std::min<std::uint64_t>(32, std::numeric_limits<std::uint64_t>::max() - state.ticks))
            return fail("Manual order metadata is invalid.");
        previous = o.order_id;
        orders[o.order_id] = &o;
        if (!manual_live(o)) {
            ++terminal;
            const bool cancelled = o.completion == ManualOrderCompletion::Replaced ||
                                   o.completion == ManualOrderCompletion::Stopped ||
                                   o.completion == ManualOrderCompletion::Released ||
                                   o.completion == ManualOrderCompletion::Cancelled;
            const bool complete =
                (o.completion == ManualOrderCompletion::Arrived && o.request.kind == ManualOrderKind::Move) ||
                (o.completion == ManualOrderCompletion::Delivered &&
                 o.request.kind == ManualOrderKind::Gather &&
                 o.request.gather_completion == GatherCompletion::OneDelivery) ||
                (o.completion == ManualOrderCompletion::Exhausted &&
                 o.request.kind == ManualOrderKind::Gather) ||
                (o.completion == ManualOrderCompletion::LegacyFinished && o.legacy_compatibility);
            if (o.executor != ManualExecutorKind::None || o.executor_id || o.next_attempt_tick ||
                (o.phase == ManualOrderPhase::Failed) != (o.failure != PathFailure::None) ||
                (o.phase == ManualOrderPhase::Failed && o.completion != ManualOrderCompletion::None) ||
                (o.phase == ManualOrderPhase::Cancelled && !cancelled) ||
                (o.phase == ManualOrderPhase::Complete && !complete))
                return fail("Terminal manual order is invalid.");
            continue;
        }
        const auto &r = *actors.at(o.resident_id);
        const bool head = counts[o.resident_id]++ == 0;
        if (counts[o.resident_id] > max_resident_manual_orders || !r.profession.manual_override ||
            r.profession.deferred || (r.health <= 0 && !o.legacy_compatibility) ||
            o.failure != PathFailure::None || o.completion != ManualOrderCompletion::None ||
            (o.legacy_compatibility && o.phase != ManualOrderPhase::Active &&
             o.phase != ManualOrderPhase::PathPending) ||
            (!head && (o.phase != ManualOrderPhase::Queued || o.executor != ManualExecutorKind::None ||
                       o.activated_tick || o.attempts)))
            return fail("Manual queue ownership is invalid.");
        if (o.phase == ManualOrderPhase::Queued) {
            if (o.executor != ManualExecutorKind::None || o.executor_id ||
                (head && (r.order_phase == OrderPhase::PathPending || r.job != Job::Idle ||
                          work_actors.contains(r.id) || physical_actors.contains(r.id))))
                return fail("Queued manual order owns an executor.");
            continue;
        }
        if (o.next_attempt_tick || o.executor == ManualExecutorKind::None)
            return fail("Active manual order has no executor.");
        if ((o.phase == ManualOrderPhase::PathPending && o.executor != ManualExecutorKind::Path) ||
            (o.phase == ManualOrderPhase::Active && o.executor == ManualExecutorKind::Path) ||
            (o.phase == ManualOrderPhase::WaitingBoundary && o.executor != ManualExecutorKind::Path &&
             o.executor != ManualExecutorKind::Logistics))
            return fail("Manual phase does not match its executor.");
        if (o.executor == ManualExecutorKind::Path) {
            const auto it = paths.find(o.executor_id);
            if (it == paths.end() || r.pending_path_batch != o.executor_id ||
                r.order_phase != OrderPhase::PathPending ||
                !std::binary_search(it->second->resident_ids.begin(), it->second->resident_ids.end(), r.id))
                return fail("Manual path correlation is invalid.");
            if (it->second->command == PathCommandKind::Manual) {
                const auto &draft = it->second->drafts[static_cast<std::size_t>(
                    std::lower_bound(it->second->resident_ids.begin(), it->second->resident_ids.end(), r.id) -
                    it->second->resident_ids.begin())];
                if (draft.resolved || draft.manual_order_id != o.order_id ||
                    o.request.target != it->second->target)
                    return fail("Manual draft correlation is invalid.");
            } else if (!o.legacy_compatibility && o.phase != ManualOrderPhase::WaitingBoundary)
                return fail("New manual order owns a legacy external path.");
            if (o.phase != ManualOrderPhase::WaitingBoundary && o.request.kind != ManualOrderKind::Legacy &&
                (o.request.target != it->second->target ||
                 (o.request.kind == ManualOrderKind::Move ? it->second->assigned_job != Job::Move
                                                          : !gather_job(it->second->assigned_job))))
                return fail("Manual request does not match its path job.");
        } else if (o.executor == ManualExecutorKind::Work) {
            const auto it = work.find(o.executor_id);
            if (it == work.end() || !active_work(*it->second) || it->second->resident_id != r.id ||
                (o.request.kind != ManualOrderKind::Legacy &&
                 (o.request.kind != ManualOrderKind::Gather ||
                  it->second->kind != WorkIntentKind::GatherContinuation || !gather_job(it->second->job) ||
                  o.request.target != it->second->target)))
                return fail("Manual Work correlation is invalid.");
        } else if (o.executor == ManualExecutorKind::Logistics) {
            const auto it = tasks.find(o.executor_id);
            if (it == tasks.end() || it->second->resident_id != r.id ||
                it->second->phase == LogisticsPhase::Complete || it->second->phase == LogisticsPhase::Failed)
                return fail("Manual cargo correlation is invalid.");
            if (o.phase != ManualOrderPhase::WaitingBoundary && o.request.kind != ManualOrderKind::Legacy &&
                (o.request.kind != ManualOrderKind::Gather ||
                 it->second->kind != LogisticsTaskKind::HaulToStorage ||
                 !gather_job(it->second->resume_job) || o.request.target != it->second->resume_target))
                return fail("Manual request does not match its cargo continuation.");
        } else {
            if (o.executor_id || r.order_phase == OrderPhase::PathPending || r.job == Job::Idle ||
                !routes.contains(r.id) || !routes.at(r.id)->work ||
                (o.request.kind != ManualOrderKind::Legacy &&
                 (o.request.target != routes.at(r.id)->work ||
                  (o.request.kind == ManualOrderKind::Move ? r.job != Job::Move : !gather_job(r.job)))))
                return fail("Manual route correlation is invalid.");
        }
    }
    if (terminal > manual_terminal_history)
        return fail("Manual terminal history exceeds its bound.");
    for (const auto &r : state.residents)
        if (r.profession.manual_override && !counts.contains(r.id))
            return fail("Manual profile has no authoritative queue head.");
    for (const auto &b : state.path_batches)
        if (b.command == PathCommandKind::Manual)
            for (const auto &d : b.drafts) {
                if (!d.manual_order_id || d.manual_order_id >= state.next_manual_order_id ||
                    (!d.resolved && (d.failure != PathFailure::None || !orders.contains(d.manual_order_id))))
                    return fail("Manual draft owner is invalid.");
                if (!d.resolved) {
                    const auto &o = *orders.at(d.manual_order_id);
                    if (o.resident_id != d.resident_id || o.phase != ManualOrderPhase::PathPending ||
                        o.executor != ManualExecutorKind::Path || o.executor_id != b.path_batch_id)
                        return fail("Manual draft owner is not reciprocal.");
                }
            }
    return true;
}

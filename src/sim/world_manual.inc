// Manual orders own execution intent; logistics alone owns physical items.
static bool manual_live(const ManualOrderState &o) {
    return o.phase <= ManualOrderPhase::Active;
}
ManualOrderState *manual_order(std::uint64_t id) {
    auto it = std::lower_bound(manual_orders.begin(), manual_orders.end(), id,
                               [](const auto &o, auto key) { return o.order_id < key; });
    return it != manual_orders.end() && it->order_id == id ? &*it : nullptr;
}
ManualOrderState *manual_head(int resident) {
    const auto it = manual_heads.find(resident);
    return it == manual_heads.end() ? nullptr : manual_order(it->second);
}
void rebuild_manual_heads() {
    manual_heads.clear();
    manual_queues.clear();
    for (const auto &o : manual_orders)
        if (manual_live(o)) {
            manual_heads.try_emplace(o.resident_id, o.order_id);
            manual_queues[o.resident_id].push_back(o.order_id);
        }
}
void manual_profile(int resident) {
    const auto e = find_resident(resident);
    if (e == entt::null)
        return;
    auto &p = registry.get<ProfessionState>(e);
    const bool active = manual_heads.contains(resident) && registry.get<Vital>(e).health > 0;
    if (p.manual_override != active)
        p.next_evaluation_tick = ticks;
    p.manual_override = active;
    if (p.manual_override) {
        p.deferred = false;
        p.wait_reason = ProfessionWaitReason::ManualOrder;
    }
    cache_dirty = true;
}
void finish_manual(ManualOrderState &o, ManualOrderCompletion completion,
                   PathFailure failure = PathFailure::None) {
    if (!manual_live(o))
        return;
    o.phase = failure != PathFailure::None ? ManualOrderPhase::Failed
              : completion == ManualOrderCompletion::Replaced ||
                      completion == ManualOrderCompletion::Stopped ||
                      completion == ManualOrderCompletion::Released ||
                      completion == ManualOrderCompletion::Cancelled
                  ? ManualOrderPhase::Cancelled
                  : ManualOrderPhase::Complete;
    o.completion = completion;
    o.failure = failure;
    o.executor = ManualExecutorKind::None;
    o.executor_id = 0;
    o.next_attempt_tick = 0;
    const auto resident = o.resident_id;
    auto &queue = manual_queues.at(resident);
    std::erase(queue, o.order_id);
    if (queue.empty()) {
        manual_queues.erase(resident);
        manual_heads.erase(resident);
    } else
        manual_heads[resident] = queue.front();
    manual_profile(resident);
    if(!manual_head(resident) && !o.legacy_compatibility &&
       completion!=ManualOrderCompletion::Replaced && completion!=ManualOrderCompletion::Released &&
       completion!=ManualOrderCompletion::Stopped && completion!=ManualOrderCompletion::Cancelled)
        wait_after_manual(resident);
}
void finish_manual_actor(int resident, ManualOrderCompletion completion,
                         PathFailure failure = PathFailure::None) {
    if (!manual_mutation)
        if (auto *o = manual_head(resident);
            o && o->phase != ManualOrderPhase::WaitingBoundary && o->phase != ManualOrderPhase::Queued)
            finish_manual(*o, completion, failure);
}
void prune_manual_history(bool admission = false) {
    std::size_t terminal = 0;
    for (const auto &o : manual_orders)
        terminal += !manual_live(o);
    const auto retain = admission ? std::size_t{} : manual_terminal_history;
    if (terminal > retain) {
        auto remove = terminal - retain;
        std::erase_if(manual_orders, [&](const auto &o) {
            if (remove && !manual_live(o)) {
                --remove;
                return true;
            }
            return false;
        });
    }
}
void cancel_manual_path_actor(int resident) {
    const auto e = find_resident(resident);
    if (e == entt::null)
        return;
    auto &status = registry.get<OrderStatus>(e);
    if (status.phase != OrderPhase::PathPending)
        return;
    const auto pending_batch = status.pending_batch;
    auto it = std::find_if(path_batches.begin(), path_batches.end(),
                           [pending_batch](const auto &b) { return b.path_batch_id == pending_batch; });
    if (it == path_batches.end() || it->command != PathCommandKind::Manual)
        return; // A legacy atomic batch retains the old reservation until its boundary.
    auto draft = std::lower_bound(it->drafts.begin(), it->drafts.end(), resident,
                                  [](const auto &d, int id) { return d.resident_id < id; });
    if (draft != it->drafts.end() && draft->resident_id == resident && !draft->resolved) {
        draft->resolved = true;
        draft->failure = PathFailure::ReservationConflict;
    }
    status = {};
    lookup_batch_id = 0;
}
bool clear_manual(int resident, ManualOrderCompletion reason, bool stop_executor = true) {
    const auto e = find_resident(resident);
    if (e == entt::null)
        return false;
    clear_outdoor_work_accumulator(resident);
    manual_mutation = true;
    cancel_manual_path_actor(resident);
    const auto found = manual_queues.find(resident);
    if (found != manual_queues.end())
        for (auto id : found->second) {
            auto &o = *manual_order(id);
            o.phase = ManualOrderPhase::Cancelled;
            o.completion = reason;
            o.failure = PathFailure::None;
            o.executor = ManualExecutorKind::None;
            o.executor_id = 0;
            o.next_attempt_tick = 0;
        }
    manual_heads.erase(resident);
    manual_queues.erase(resident);
    const bool protected_executor =
        resident_busy(resident) ||
        (registry.get<OrderStatus>(e).phase == OrderPhase::PathPending && !active_work_intent(resident));
    if (stop_executor && !protected_executor)
        stop_actor(resident);
    manual_mutation = false;
    manual_profile(resident);
    return protected_executor;
}
void bind_manual_executor(ManualOrderState &o) {
    const auto e = find_resident(o.resident_id);
    if (const auto it = active_task_by_resident.find(o.resident_id); it != active_task_by_resident.end()) {
        o.executor = ManualExecutorKind::Logistics;
        o.executor_id = it->second;
    } else if (auto *w = active_work_intent(o.resident_id)) {
        o.executor = ManualExecutorKind::Work;
        o.executor_id = w->intent_id;
    } else if (registry.get<OrderStatus>(e).phase == OrderPhase::PathPending) {
        o.executor = ManualExecutorKind::Path;
        o.executor_id = registry.get<OrderStatus>(e).pending_batch;
    } else if (registry.get<Activity>(e).job != Job::Idle) {
        o.executor = ManualExecutorKind::Route;
        o.executor_id = 0;
    } else {
        o.executor = ManualExecutorKind::None;
        o.executor_id = 0;
    }
}
void reconcile_manual() {
    std::vector<std::uint64_t> heads;
    for (const auto &[resident, id] : manual_heads) {
        (void)resident;
        heads.push_back(id);
    }
    for (auto id : heads) {
        auto *o = manual_order(id);
        const auto e = find_resident(o->resident_id);
        if (e == entt::null || registry.get<Vital>(e).health <= 0) {
            clear_manual(o->resident_id, ManualOrderCompletion::Released, false);
            o = manual_order(id);
            o->phase = ManualOrderPhase::Failed;
            o->completion = ManualOrderCompletion::None;
            o->failure = PathFailure::ResidentUnavailable;
            continue;
        }
        if (o->phase == ManualOrderPhase::WaitingBoundary) {
            if (!resident_busy(o->resident_id) &&
                registry.get<OrderStatus>(e).phase != OrderPhase::PathPending) {
                manual_mutation = true;
                stop_actor(o->resident_id);
                manual_mutation = false;
                o->phase = ManualOrderPhase::Queued;
                o->executor = ManualExecutorKind::None;
                o->executor_id = 0;
            } else
                bind_manual_executor(*o);
        } else if (o->phase == ManualOrderPhase::Active || o->phase == ManualOrderPhase::PathPending) {
            bind_manual_executor(*o);
            if (o->executor == ManualExecutorKind::None) {
                auto failure = registry.get<OrderStatus>(e).failure;
                if (failure == PathFailure::None && !o->legacy_compatibility)
                    failure = PathFailure::TargetInvalid;
                finish_manual(*o,
                              failure == PathFailure::None ? ManualOrderCompletion::LegacyFinished
                                                           : ManualOrderCompletion::None,
                              failure);
            } else
                o->phase = o->executor == ManualExecutorKind::Path ? ManualOrderPhase::PathPending
                                                                   : ManualOrderPhase::Active;
        }
        manual_profile(o->resident_id);
    }
    prune_manual_history();
}
std::vector<ResidentCommandOutcome> issue_manual(const std::vector<int> &input,
                                                 const ManualOrderRequest &request, ManualQueueMode mode,
                                                 std::optional<ManualOrderIssuer> issuer) {
    auto ids = input;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<ResidentCommandOutcome> results;
    results.reserve(ids.size());
    const bool request_valid =
        request.kind <= ManualOrderKind::Gather && request.target &&
        inside_world(request.target->x, request.target->y) && valid_layer(request.target->layer) &&
        request.gather_completion <= GatherCompletion::OneDelivery && mode <= ManualQueueMode::Append &&
        (request.kind == ManualOrderKind::Gather ||
         request.gather_completion == GatherCompletion::UntilExhausted) &&
        (!issuer || (issuer->player_id && issuer->request_id)) &&
        (request.kind == ManualOrderKind::Move
             ? walkable(*request.target)
             : tile(*request.target).amount > 0 && (tile(*request.target).terrain == Terrain::Forest ||
                                                    tile(*request.target).terrain == Terrain::Rock ||
                                                    tile(*request.target).terrain == Terrain::MineOre ||
                                                    tile(*request.target).terrain == Terrain::Grass));
    // Reserve once for this group, before any actor is replaced. Vector capacity
    // is not authoritative state; this request is capped by the record bound.
    if (request_valid)
        manual_orders.reserve(manual_orders.size() +
                              std::min(ids.size(), max_manual_orders - manual_orders.size()));
    bool has_terminal_history = std::any_of(manual_orders.begin(), manual_orders.end(),
                                            [](const auto &o) { return !manual_live(o); });
    const auto reclaim_if_full = [&] {
        // A full queue can reject thousands of later actors. Scan it only when
        // this call has terminal records that can actually release capacity.
        if (manual_orders.size() >= max_manual_orders && has_terminal_history) {
            prune_manual_history(true);
            has_terminal_history = false;
        }
    };
    for (int resident : ids) {
        const auto e = find_resident(resident);
        if (!request_valid || e == entt::null || registry.get<Vital>(e).health <= 0) {
            results.push_back({resident, ResidentCommandStatus::Rejected,
                               request_valid ? PathFailure::ResidentUnavailable : PathFailure::TargetInvalid,
                               0});
            continue;
        }
        if ((economy_worker(resident) || expedition_worker(resident)) || (request.kind==ManualOrderKind::Gather && settlement.youth(resident,ticks) && !settlement.youth(resident,ticks)->forced_work)) {
            results.push_back({resident,ResidentCommandStatus::Rejected,PathFailure::ResidentUnavailable,0});continue;
        }
        const auto prior_queue = manual_queues.find(resident);
        const auto count = prior_queue == manual_queues.end() ? 0 : prior_queue->second.size();
        if ((mode == ManualQueueMode::Append && count >= max_resident_manual_orders) ||
            next_manual_order_id == std::numeric_limits<std::uint64_t>::max()) {
            results.push_back({resident, ResidentCommandStatus::Rejected, PathFailure::LimitExceeded, 0});
            continue;
        }
        reclaim_if_full();
        if (manual_orders.size() - (mode == ManualQueueMode::Replace ? count : 0) >= max_manual_orders) {
            results.push_back({resident, ResidentCommandStatus::Rejected, PathFailure::LimitExceeded, 0});
            continue;
        }
        bool boundary = false;
        if (mode == ManualQueueMode::Replace || !count) {
            boundary = clear_manual(resident, ManualOrderCompletion::Replaced);
            has_terminal_history = has_terminal_history || count > 0;
        }
        reclaim_if_full();
        ManualOrderState o;
        o.order_id = next_manual_order_id++;
        o.resident_id = resident;
        o.owner_nation = registry.get<Identity>(e).nation;
        o.request = request;
        o.phase = boundary ? ManualOrderPhase::WaitingBoundary : ManualOrderPhase::Queued;
        o.issuer = issuer;
        o.issued_tick = ticks;
        if (boundary)
            bind_manual_executor(o);
        manual_orders.push_back(o);
        manual_queues[resident].push_back(o.order_id);
        manual_heads.try_emplace(resident, o.order_id);
        manual_profile(resident);
        results.push_back({resident,
                           boundary ? ResidentCommandStatus::Deferred : ResidentCommandStatus::Applied,
                           PathFailure::None, o.order_id});
    }
    if (has_terminal_history)
        prune_manual_history();
    return results;
}
bool can_track_legacy_manual(const std::vector<int> &ids) const {
    if (next_manual_order_id > std::numeric_limits<std::uint64_t>::max() - ids.size())
        return false;
    std::size_t live = 0;
    const std::set<int> replaced(ids.begin(), ids.end());
    for (const auto &[resident, queue] : manual_queues)
        if (!replaced.contains(resident))
            live += queue.size();
    return live + ids.size() <= max_manual_orders;
}
void track_legacy_manual(const std::vector<int> &ids, std::uint64_t batch, CellCoord target,
                         ManualOrderKind kind) {
    auto ordered = ids;
    std::sort(ordered.begin(), ordered.end());
    for (auto resident : ordered) {
        clear_manual(resident, ManualOrderCompletion::Replaced, false);
        if (manual_orders.size() >= max_manual_orders)
            prune_manual_history(true);
        ManualOrderState o;
        o.order_id = next_manual_order_id++;
        o.resident_id = resident;
        o.owner_nation = registry.get<Identity>(find_resident(resident)).nation;
        o.request = {kind, target, GatherCompletion::UntilExhausted};
        o.phase = ManualOrderPhase::PathPending;
        o.legacy_compatibility = true;
        o.executor = ManualExecutorKind::Path;
        o.executor_id = batch;
        manual_orders.push_back(o);
        manual_queues[resident].push_back(o.order_id);
        manual_heads[resident] = o.order_id;
        manual_profile(resident);
    }
    prune_manual_history();
}
void manual_path_actor(PathBatchState &batch, PathRouteDraftState &d, PathFailure failure) {
    if (d.resolved)
        return;
    d.resolved = true;
    d.failure = failure;
    auto *o = manual_order(d.manual_order_id);
    const auto e = find_resident(d.resident_id);
    if (!o || !manual_live(*o) || e == entt::null)
        return;
    auto &status = registry.get<OrderStatus>(e);
    if (status.pending_batch != batch.path_batch_id) {
        finish_manual(*o, ManualOrderCompletion::None, PathFailure::ResidentUnavailable);
        return;
    }
    if (failure != PathFailure::None) {
        const auto &target_tile = tile(batch.target);
        const bool exhausted =
            failure == PathFailure::TargetInvalid && o->request.kind == ManualOrderKind::Gather &&
            target_tile.amount == 0 &&
            (work_terrain_matches(batch.assigned_job, target_tile.terrain) ||
             target_tile.terrain ==
                 (batch.target.layer == WorldLayer::Mine ? Terrain::MineFloor : Terrain::Grass));
        if (exhausted) {
            d.failure = PathFailure::None;
            status = {};
            registry.get<Activity>(e) = {};
            registry.get<Route>(e) = {};
            finish_manual(*o, ManualOrderCompletion::Exhausted);
            return;
        }
        status = {OrderPhase::PathFailed, failure, 0};
        registry.get<Activity>(e) = {};
        registry.get<Route>(e) = {};
        finish_manual(*o, ManualOrderCompletion::None, failure);
        return;
    }
    auto &route = registry.get<Route>(e);
    route = {d.staged, 0, batch.target};
    if (route.points.empty() && (d.start_x != float(d.start.x) || d.start_y != float(d.start.y)))
        route.points.push_back(d.start);
    registry.get<Position>(e).target = batch.target;
    registry.get<Activity>(e) = {batch.assigned_job, 0};
    status = {OrderPhase::Active, PathFailure::None, 0};
    o->phase = ManualOrderPhase::Active;
    o->executor = ManualExecutorKind::Route;
    o->executor_id = 0;
    cache_dirty = true;
}
void process_manual() {
    manual_diagnostics = {};
    reconcile_manual();
    std::vector<std::uint64_t> heads;
    for (const auto &[resident, id] : manual_heads) {
        (void)resident;
        heads.push_back(id);
    }
    std::sort(heads.begin(), heads.end());
    if (heads.empty())
        return;
    auto first = std::lower_bound(heads.begin(), heads.end(), manual_order_cursor_id);
    std::size_t cursor = first == heads.end() ? 0 : static_cast<std::size_t>(first - heads.begin());
    using Key = std::tuple<NationId, ManualOrderKind, WorldLayer, int, int, GatherCompletion>;
    std::map<Key, std::vector<std::uint64_t>> groups;
    for (std::size_t visit = 0; visit < std::min(manual_visits_per_tick, heads.size()); ++visit) {
        auto *o = manual_order(heads[cursor]);
        cursor = (cursor + 1) % heads.size();
        manual_order_cursor_id = heads[cursor];
        ++manual_diagnostics.visits_last_tick;
        if (o->phase != ManualOrderPhase::Queued || ticks < o->next_attempt_tick)
            continue;
        if (!o->request.target || o->request.kind == ManualOrderKind::Legacy) {
            finish_manual(*o, ManualOrderCompletion::None, PathFailure::TargetInvalid);
            continue;
        }
        if (!o->activated_tick)
            o->activated_tick = ticks;
        if (ticks - *o->activated_tick >= work_admission_deadline_ticks ||
            o->attempts >= max_work_admission_attempts) {
            finish_manual(*o, ManualOrderCompletion::None, PathFailure::Timeout);
            continue;
        }
        const auto target = *o->request.target;
        groups[{o->owner_nation, o->request.kind, target.layer, target.x, target.y,
                o->request.gather_completion}]
            .push_back(o->order_id);
    }
    for (const auto &[key, group] : groups) {
        (void)key;
        if (manual_diagnostics.path_admissions_last_tick >= manual_path_admissions_per_tick)
            break;
        const auto request = manual_order(group.front())->request;
        const auto target = *request.target;
        const auto &t = tile(target);
        const Job job = request.kind == ManualOrderKind::Move                         ? Job::Move
                        : t.terrain == Terrain::Forest                                ? Job::Woodcut
                        : t.terrain == Terrain::Rock || t.terrain == Terrain::MineOre ? Job::Mine
                        : t.terrain == Terrain::Grass                                 ? Job::Forage
                                                                                      : Job::Idle;
        if (job == Job::Idle || (job == Job::Move && !walkable(target))) {
            for (auto id : group)
                finish_manual(*manual_order(id), ManualOrderCompletion::None, PathFailure::TargetInvalid);
            continue;
        }
        if (job != Job::Move && t.amount <= 0) {
            for (auto id : group)
                finish_manual(*manual_order(id), ManualOrderCompletion::Exhausted);
            continue;
        }
        std::vector<int> ids;
        for (auto id : group)
            ids.push_back(manual_order(id)->resident_id);
        const auto result = queue_path(PathCommandKind::Manual, ids, target, job != Job::Move, job);
        ++manual_diagnostics.path_admissions_last_tick;
        for (auto id : group) {
            auto &o = *manual_order(id);
            ++o.attempts;
            if (result.accepted) {
                o.phase = ManualOrderPhase::PathPending;
                o.executor = ManualExecutorKind::Path;
                o.executor_id = result.path_batch_id;
                o.next_attempt_tick = 0;
                ++manual_diagnostics.admitted_residents_last_tick;
            } else if (result.failure == PathFailure::LimitExceeded ||
                       result.failure == PathFailure::ReservationConflict) {
                const auto delay = std::uint64_t{1} << std::min<unsigned>(o.attempts, 5);
                o.next_attempt_tick =
                    ticks + std::min(delay, std::numeric_limits<std::uint64_t>::max() - ticks);
            } else
                finish_manual(o, ManualOrderCompletion::None, result.failure);
        }
        if (result.accepted) {
            auto &batch = path_batches.back();
            batch.legacy_atomic = false;
            for (auto &d : batch.drafts)
                d.manual_order_id = manual_heads.at(d.resident_id);
        }
    }
}

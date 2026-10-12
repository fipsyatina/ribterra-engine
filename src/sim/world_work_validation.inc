static bool validate_work(const WorldState &s, std::string &error) {
    auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    if (s.work_intents.size() > max_work_intents || !s.next_work_intent_id || !s.work_intent_cursor_id ||
        s.work_intent_cursor_id > s.next_work_intent_id || s.work_building_cursor_id <= 0 ||
        s.work_building_cursor_id > s.next_building_id)
        return fail("Work intent counters or bounds are invalid.");
    std::map<int, const Resident *> residents;
    std::map<int, const ConstructionOrderState *> orders;
    std::map<std::uint64_t, const PathBatchState *> paths;
    std::set<int> logistics_actors, active_actors, active_sites, blocked_sites;
    std::set<std::uint64_t> correlated;
    for (const auto &r : s.residents)
        residents[r.id] = &r;
    for (const auto &o : s.construction_orders)
        orders[o.building_id] = &o;
    for (const auto &p : s.path_batches)
        paths[p.path_batch_id] = &p;
    for (const auto &t : s.logistics_tasks)
        if (t.resident_id &&
            (t.path_batch_id || (t.phase != LogisticsPhase::Complete && t.phase != LogisticsPhase::Failed)))
            logistics_actors.insert(t.resident_id);
    std::uint64_t previous = 0;
    for (const auto &w : s.work_intents) {
        if (!w.intent_id || w.intent_id <= previous || w.intent_id >= s.next_work_intent_id ||
            static_cast<unsigned>(w.kind) > static_cast<unsigned>(WorkIntentKind::BuildAssignment) ||
            static_cast<unsigned>(w.phase) > static_cast<unsigned>(WorkIntentPhase::Failed) ||
            static_cast<unsigned>(w.completion) > static_cast<unsigned>(WorkCompletionReason::Cancelled) ||
            static_cast<unsigned>(w.failure) > static_cast<unsigned>(PathFailure::ReservationConflict) ||
            !residents.contains(w.resident_id) || !w.owner_nation ||
            residents.at(w.resident_id)->nation != w.owner_nation || !inside_world(w.target.x, w.target.y) ||
            !valid_layer(w.target.layer) || w.created_tick > s.ticks ||
            w.created_tick > std::numeric_limits<std::uint64_t>::max() - work_admission_deadline_ticks ||
            w.admission_deadline_tick != w.created_tick + work_admission_deadline_ticks ||
            w.attempts > max_work_admission_attempts ||
            w.last_attempt_topology_revision > s.topology_revision)
            return fail("Work intent metadata is invalid.");
        previous = w.intent_id;
        if (w.kind == WorkIntentKind::GatherContinuation) {
            if (w.building_id || (w.job != Job::Woodcut && w.job != Job::Mine && w.job != Job::Forage && w.job != Job::Farm) ||
                !work_terrain_matches(w.job, w.expected_terrain))
                return fail("Gather work binding is invalid.");
        } else {
            if (w.job != Job::Build || w.expected_terrain != Terrain::Grass ||
                !orders.contains(w.building_id) || orders.at(w.building_id)->site != w.target ||
                orders.at(w.building_id)->owner_nation != w.owner_nation)
                return fail("Construction work binding is invalid.");
        }
        if (active_work(w)) {
            if (w.failure != PathFailure::None || w.completion != WorkCompletionReason::None ||
                logistics_actors.contains(w.resident_id) || !active_actors.insert(w.resident_id).second)
                return fail("Active work has conflicting ownership or terminal metadata.");
            if (w.kind == WorkIntentKind::BuildAssignment &&
                (orders.at(w.building_id)->phase != ConstructionPhase::Work ||
                 !active_sites.insert(w.building_id).second))
                return fail("Construction has multiple or inactive work assignments.");
            if (w.phase == WorkIntentPhase::Waiting) {
                if (w.path_batch_id || w.next_attempt_tick < w.created_tick ||
                    w.next_attempt_tick > w.admission_deadline_tick ||
                    residents.at(w.resident_id)->pending_path_batch ||
                    residents.at(w.resident_id)->job != Job::Idle)
                    return fail("Waiting work retry or actor state is invalid.");
            } else {
                if (!w.path_batch_id || !w.attempts || w.next_attempt_tick ||
                    !paths.contains(w.path_batch_id) || !correlated.insert(w.path_batch_id).second)
                    return fail("Pending work has no unique path.");
                const auto &p = *paths.at(w.path_batch_id);
                if (p.command != PathCommandKind::Work || p.resident_ids != std::vector<int>{w.resident_id} ||
                    p.target != w.target || p.assigned_job != w.job || !p.to_adjacent ||
                    p.enqueued_tick < w.created_tick || p.enqueued_tick >= w.admission_deadline_tick)
                    return fail("Work path correlation is invalid.");
            }
        } else {
            if (w.path_batch_id || w.next_attempt_tick ||
                (w.phase == WorkIntentPhase::Complete &&
                 (w.failure != PathFailure::None || w.completion == WorkCompletionReason::None)) ||
                (w.phase == WorkIntentPhase::Failed &&
                 (w.failure == PathFailure::None || w.completion != WorkCompletionReason::None)))
                return fail("Terminal work metadata is invalid.");
            if (w.kind == WorkIntentKind::BuildAssignment && w.phase == WorkIntentPhase::Failed &&
                orders.at(w.building_id)->phase == ConstructionPhase::Work &&
                !blocked_sites.insert(w.building_id).second)
                return fail("Construction has duplicate blocked work records.");
        }
    }
    for (const auto &p : s.path_batches)
        if (p.command == PathCommandKind::Work && !correlated.contains(p.path_batch_id))
            return fail("Internal work path has no owning intent.");
    for (const int id : blocked_sites)
        if (active_sites.contains(id))
            return fail("Construction is both active and blocked.");
    return true;
}

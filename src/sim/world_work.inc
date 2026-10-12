// Private, persisted work admission. Physical items remain owned by logistics.
static bool active_work(const WorkIntentState &w) {
    return w.phase == WorkIntentPhase::Waiting || w.phase == WorkIntentPhase::PathPending;
}
WorkIntentState *work_intent(std::uint64_t id) {
    auto it = std::lower_bound(work_intents.begin(), work_intents.end(), id,
                               [](const auto &w, auto key) { return w.intent_id < key; });
    return it != work_intents.end() && it->intent_id == id ? &*it : nullptr;
}
WorkIntentState *active_work_intent(int resident) {
    auto it = active_work_by_resident.find(resident);
    return it == active_work_by_resident.end() ? nullptr : work_intent(it->second);
}
void cancel_work_path(std::uint64_t id) {
    auto it = std::find_if(path_batches.begin(), path_batches.end(),
                           [=](const auto &b) { return b.path_batch_id == id; });
    if (it == path_batches.end())
        return;
    planner_memory_bytes -= batch_memory(*it);
    for (const auto &d : it->drafts)
        planner_staged_cells -= d.staged.size();
    path_batches.erase(it);
    lookup_batch_id = 0;
    lookup_node_count = 0;
    active_node_lookup.clear();
    active_all_starts.clear();
    active_remaining_starts.clear();
}
void finish_work(WorkIntentState &w, WorkCompletionReason completion, PathFailure failure, bool stop = true) {
    const bool owned = active_work(w);
    const auto actor = find_resident(w.resident_id);
    const bool automatic = actor != entt::null && !registry.get<ProfessionState>(actor).manual_override;
    if (w.path_batch_id)
        cancel_work_path(w.path_batch_id);
    w.phase = failure == PathFailure::None ? WorkIntentPhase::Complete : WorkIntentPhase::Failed;
    w.completion = completion;
    w.failure = failure;
    w.path_batch_id = 0;
    w.next_attempt_tick = 0;
    if (owned) {
        if (failure != PathFailure::None)
            ++work_outcomes.failures[static_cast<unsigned>(failure)];
        else if (completion == WorkCompletionReason::Started)
            ++work_outcomes.started;
        else if (completion == WorkCompletionReason::Exhausted)
            ++work_outcomes.exhausted;
        else if (completion == WorkCompletionReason::Superseded)
            ++work_outcomes.superseded;
        else if (completion == WorkCompletionReason::Cancelled)
            ++work_outcomes.cancelled;
        active_work_by_resident.erase(w.resident_id);
        if (w.kind == WorkIntentKind::BuildAssignment)
            active_work_by_building.erase(w.building_id);
        if (failure != PathFailure::None || completion == WorkCompletionReason::Exhausted ||
            completion == WorkCompletionReason::Cancelled)
            finish_manual_actor(
                w.resident_id,
                completion == WorkCompletionReason::Exhausted   ? ManualOrderCompletion::Exhausted
                : completion == WorkCompletionReason::Cancelled ? ManualOrderCompletion::LegacyFinished
                                                                : ManualOrderCompletion::None,
                failure);
        if (completion != WorkCompletionReason::Started)
            retire_manual_override(w.resident_id);
        if (automatic && failure != PathFailure::None)
            registry.get<ProfessionState>(actor).wait_reason = ProfessionWaitReason::PathFailed;
    }
    const auto e = find_resident(w.resident_id);
    if (owned && stop && e != entt::null) {
        registry.get<Activity>(e) = {};
        registry.get<Route>(e) = {};
        registry.get<OrderStatus>(e) = {};
    }
    cache_dirty = true;
}
void supersede_work_for_residents(const std::vector<int> &ids) {
    for (int id : ids) clear_outdoor_work_accumulator(id);
    const std::set<int> selected(ids.begin(), ids.end());
    for (auto &w : work_intents)
        if (selected.contains(w.resident_id))
            finish_work(w, WorkCompletionReason::Superseded, PathFailure::None);
}
void cancel_work_for_building(int id) {
    for (auto &w : work_intents)
        if (w.kind == WorkIntentKind::BuildAssignment && w.building_id == id)
            finish_work(w, WorkCompletionReason::Cancelled, PathFailure::None);
}
static Terrain work_terrain(Job job, WorldLayer layer) {
    return job == Job::Woodcut ? Terrain::Forest
           : job == Job::Mine  ? (layer == WorldLayer::Mine ? Terrain::MineOre : Terrain::Rock)
                               : Terrain::Grass;
}
static bool work_terrain_matches(Job job, Terrain terrain) {
    return (job == Job::Woodcut && terrain == Terrain::Forest) ||
           (job == Job::Mine && (terrain == Terrain::Rock || terrain == Terrain::MineOre)) ||
           ((job == Job::Forage || job == Job::Build || job == Job::Farm) && terrain == Terrain::Grass);
}
// Zero resource on the original terrain or its harvested terrain is normal exhaustion.
// A changed terrain with resources (or an incompatible terrain) is an invalid target.
int work_target_status(const WorkIntentState &w) {
    if (w.kind == WorkIntentKind::BuildAssignment) {
        const auto *o = construction(w.building_id);
        const auto *b = building(w.building_id);
        if (!o || o->phase == ConstructionPhase::Cancelled || o->phase == ConstructionPhase::Complete)
            return 2;
        return o->phase == ConstructionPhase::Work && b && b->nation == w.owner_nation &&
                       o->owner_nation == w.owner_nation && o->site == w.target && b->progress < 1
                   ? 0
                   : -1;
    }
    if (w.job == Job::Farm)
        return farm_at(w.target, w.owner_nation) ? 0 : -1;
    const auto &t = tile(w.target);
    if (t.terrain == w.expected_terrain && t.amount > 0)
        return 0;
    const auto depleted = w.target.layer == WorldLayer::Mine ? Terrain::MineFloor : Terrain::Grass;
    if (!t.amount && (t.terrain == w.expected_terrain || t.terrain == depleted))
        return 1;
    return -1;
}
bool create_work_intent(int resident, WorkIntentKind kind, CellCoord target, Job job, int building_id = 0) {
    const auto e = find_resident(resident);
    if (e == entt::null || active_work_by_resident.contains(resident) ||
        (kind == WorkIntentKind::BuildAssignment && active_work_by_building.contains(building_id)) ||
        work_intents.size() >= max_work_intents ||
        next_work_intent_id == std::numeric_limits<std::uint64_t>::max() ||
        ticks > std::numeric_limits<std::uint64_t>::max() - work_admission_deadline_ticks)
        return false;
    WorkIntentState w;
    w.intent_id = next_work_intent_id++;
    w.kind = kind;
    w.resident_id = resident;
    w.building_id = building_id;
    w.owner_nation = registry.get<Identity>(e).nation;
    w.target = target;
    w.job = job;
    w.expected_terrain = work_terrain(job, target.layer);
    if (work_terrain_matches(job, tile(target).terrain))
        w.expected_terrain = tile(target).terrain;
    w.created_tick = ticks;
    w.next_attempt_tick = ticks;
    w.admission_deadline_tick = ticks + work_admission_deadline_ticks;
    w.last_attempt_topology_revision = topology_revision;
    if (kind == WorkIntentKind::BuildAssignment)
        for (auto &previous : work_intents)
            if (previous.kind == kind && previous.building_id == building_id &&
                previous.phase == WorkIntentPhase::Failed)
                finish_work(previous, WorkCompletionReason::Superseded, PathFailure::None, false);
    work_intents.push_back(w);
    ++work_outcomes.created;
    active_work_by_resident[resident] = w.intent_id;
    if (kind == WorkIntentKind::BuildAssignment)
        active_work_by_building[building_id] = w.intent_id;
    return true;
}
void work_path_result(std::uint64_t batch_id, bool accepted, PathFailure failure) {
    for (auto &w : work_intents) {
        if (w.path_batch_id != batch_id)
            continue;
        // The planner has already removed this batch; never cancel another path here.
        w.path_batch_id = 0;
        w.last_attempt_topology_revision = topology_revision;
        const auto e = find_resident(w.resident_id);
        if (e == entt::null || registry.get<Vital>(e).health <= 0)
            finish_work(w, WorkCompletionReason::None, PathFailure::ResidentUnavailable);
        else if (const auto status = work_target_status(w); status != 0)
            finish_work(w,
                        status == 1   ? WorkCompletionReason::Exhausted
                        : status == 2 ? WorkCompletionReason::Cancelled
                                      : WorkCompletionReason::None,
                        status < 0 ? PathFailure::TargetInvalid : PathFailure::None);
        else if (accepted)
            finish_work(w, WorkCompletionReason::Started, PathFailure::None, false);
        else
            finish_work(w, WorkCompletionReason::None, failure);
        return;
    }
}
std::vector<ConstructionWorkforce> construction_workforce() const {
    std::vector<ConstructionWorkforce> result;
    std::map<int, std::size_t> by_site;
    std::map<int, std::size_t> by_id;
    std::map<int, NationId> owners;
    for (const auto &o : construction_orders) {
        if (o.phase != ConstructionPhase::Work)
            continue;
        by_site[encode_cell(o.site)] = result.size();
        by_id[o.building_id] = result.size();
        owners[o.building_id] = o.owner_nation;
        result.push_back({o.building_id, 0, 0});
    }
    for (auto e : entities()) {
        const auto &id = registry.get<Identity>(e);
        const auto &route = registry.get<Route>(e);
        if (registry.get<Vital>(e).health <= 0 || registry.get<Activity>(e).job != Job::Build ||
            registry.get<OrderStatus>(e).phase == OrderPhase::PathPending || resident_busy(id.id) ||
            !route.work)
            continue;
        auto it = by_site.find(encode_cell(*route.work));
        if (it == by_site.end())
            continue;
        auto &v = result[it->second];
        if (owners.at(v.building_id) != id.nation)
            continue;
        if (at_location(id.id, *route.work, true))
            ++v.working;
        else if (route.cursor < route.points.size())
            ++v.travelling;
    }
    for (const auto &w : work_intents)
        if (w.kind == WorkIntentKind::BuildAssignment && w.phase == WorkIntentPhase::PathPending &&
            by_id.contains(w.building_id))
            ++result[by_id.at(w.building_id)].planning;
    return result;
}
void process_work_intents() {
    work_diagnostics = {};
    // Indexed population/workforce passes avoid a resident scan for each waiting intent.
    std::map<NationId, std::vector<int>> available;
    std::map<NationId, std::size_t> cursor;
    for (auto e : entities()) {
        const auto &id = registry.get<Identity>(e);
        if (legacy_auto_worker(e, Job::Build) && registry.get<Activity>(e).job == Job::Idle &&
            registry.get<OrderStatus>(e).phase != OrderPhase::PathPending && !resident_busy(id.id) &&
            !active_work_by_resident.contains(id.id))
            available[id.nation].push_back(id.id);
    }
    std::map<int, std::uint32_t> workforce;
    for (const auto &v : construction_workforce())
        workforce[v.building_id] = v.working + v.travelling + v.planning;
    std::map<int, WorkIntentState *> latest_build;
    std::set<int> active_build;
    for (auto &w : work_intents)
        if (w.kind == WorkIntentKind::BuildAssignment) {
            latest_build[w.building_id] = &w;
            if (active_work(w))
                active_build.insert(w.building_id);
        }
    // Up to 32 site visits leave at least 96 of the shared 128 visits for intents.
    std::vector<int> sites;
    for (const auto &o : construction_orders)
        if (o.phase == ConstructionPhase::Work)
            sites.push_back(o.building_id);
    std::sort(sites.begin(), sites.end());
    auto first = std::lower_bound(sites.begin(), sites.end(), work_building_cursor_id);
    std::size_t index = first == sites.end() ? 0 : static_cast<std::size_t>(first - sites.begin());
    // Reserve before retaining references/pointers to existing records.
    work_intents.reserve(max_work_intents);
    latest_build.clear();
    for (auto &w : work_intents)
        if (w.kind == WorkIntentKind::BuildAssignment)
            latest_build[w.building_id] = &w;
    for (std::size_t n = 0; n < std::min<std::size_t>(32, sites.size()); ++n) {
        const int id = sites[index];
        index = (index + 1) % sites.size();
        work_building_cursor_id = sites[index];
        ++work_diagnostics.visits_last_tick;
        if (workforce[id] || active_build.contains(id))
            continue;
        auto old = latest_build.find(id);
        if (old != latest_build.end() && old->second->phase == WorkIntentPhase::Failed) {
            const auto &w = *old->second;
            const bool topology_retry =
                (w.failure == PathFailure::Unreachable || w.failure == PathFailure::TopologyChanged) &&
                w.last_attempt_topology_revision != topology_revision;
            if (w.failure != PathFailure::ResidentUnavailable && !topology_retry)
                continue;
        }
        auto *o = construction(id);
        auto &actors = available[o->owner_nation];
        auto &next = cursor[o->owner_nation];
        if (next == actors.size())
            continue;
        if (create_work_intent(actors[next], WorkIntentKind::BuildAssignment, o->site, Job::Build, id)) {
            ++next;
            active_build.insert(id);
            if (old != latest_build.end())
                finish_work(*old->second, WorkCompletionReason::Superseded, PathFailure::None, false);
        }
    }
    const auto count = work_intents.size();
    auto start = std::lower_bound(work_intents.begin(), work_intents.end(), work_intent_cursor_id,
                                  [](const auto &w, auto id) { return w.intent_id < id; });
    index = start == work_intents.end() ? 0 : static_cast<std::size_t>(start - work_intents.begin());
    for (std::size_t n = 0; n < count && work_diagnostics.visits_last_tick < work_intent_visits_per_tick;
         ++n) {
        auto &w = work_intents[index];
        index = (index + 1) % count;
        work_intent_cursor_id = work_intents[index].intent_id;
        ++work_diagnostics.visits_last_tick;
        if (w.phase != WorkIntentPhase::Waiting)
            continue;
        const auto e = find_resident(w.resident_id);
        if (e == entt::null || registry.get<Vital>(e).health <= 0) {
            finish_work(w, WorkCompletionReason::None, PathFailure::ResidentUnavailable);
            continue;
        }
        const int status = work_target_status(w);
        if (status != 0) {
            finish_work(w,
                        status == 1   ? WorkCompletionReason::Exhausted
                        : status == 2 ? WorkCompletionReason::Cancelled
                                      : WorkCompletionReason::None,
                        status < 0 ? PathFailure::TargetInvalid : PathFailure::None);
            continue;
        }
        if (ticks >= w.admission_deadline_tick || w.attempts >= max_work_admission_attempts) {
            finish_work(w, WorkCompletionReason::None,
                        ticks >= w.admission_deadline_tick ? PathFailure::Timeout
                                                           : PathFailure::LimitExceeded);
            continue;
        }
        if (ticks < w.next_attempt_tick ||
            work_diagnostics.admission_attempts_last_tick >= work_admissions_per_tick)
            continue;
        ++work_diagnostics.admission_attempts_last_tick;
        ++w.attempts;
        w.last_attempt_topology_revision = topology_revision;
        auto result = queue_path(PathCommandKind::Work, {w.resident_id}, w.target, true, w.job);
        if (result.accepted) {
            w.phase = WorkIntentPhase::PathPending;
            w.path_batch_id = result.path_batch_id;
            w.next_attempt_tick = 0;
        } else if (result.failure == PathFailure::LimitExceeded ||
                   result.failure == PathFailure::ReservationConflict) {
            const std::uint64_t delay = std::uint64_t{2} << std::min<unsigned>(w.attempts - 1, 4);
            w.next_attempt_tick = ticks + std::min(delay, w.admission_deadline_tick - ticks);
        } else
            finish_work(w, WorkCompletionReason::None, result.failure);
        // Preserve the next unvisited record when admissions are exhausted. Advancing through
        // the remaining visit budget would permanently skip fixed groups under sustained load.
        if (work_diagnostics.admission_attempts_last_tick == work_admissions_per_tick)
            break;
    }
    // Retain blocked sites; ordinary historical records have a bounded diagnostic tail.
    std::size_t terminal = 0;
    auto keep_blocked = [&](const WorkIntentState &w) {
        return w.kind == WorkIntentKind::BuildAssignment && w.phase == WorkIntentPhase::Failed &&
               workforce.contains(w.building_id);
    };
    for (const auto &w : work_intents)
        if (!active_work(w) && !keep_blocked(w))
            ++terminal;
    std::erase_if(work_intents, [&](const auto &w) {
        if (terminal <= 256 || active_work(w) || keep_blocked(w))
            return false;
        --terminal;
        return true;
    });
    for (const auto &w : work_intents) {
        if (active_work(w))
            ++work_diagnostics.active_intents;
        if (w.phase == WorkIntentPhase::Waiting)
            ++work_diagnostics.waiting_intents;
        if (w.phase == WorkIntentPhase::PathPending)
            ++work_diagnostics.pending_paths;
    }
}

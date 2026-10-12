static int job_priority_index(Job job) {
    return job == Job::Woodcut  ? 0
           : job == Job::Mine   ? 1
           : (job == Job::Forage || job == Job::Farm) ? 2
           : job == Job::Build  ? 3
           : job == Job::Haul   ? 4
                                : -1;
}
static Skill job_skill(Job job) {
    return job == Job::Woodcut  ? Skill::Woodcutting
           : job == Job::Mine   ? Skill::Mining
           : (job == Job::Forage || job == Job::Farm) ? Skill::Farming
           : job == Job::Build  ? Skill::Construction
                                : Skill::Hauling;
}
static int &experience(Skills &s, Skill skill) {
    const auto index = static_cast<std::size_t>(skill);
    return index == 0 ? s.wood : index == 1 ? s.mining : index == 2 ? s.farming : s.additional[index - 3];
}
ModifierBreakdown modifier(entt::entity e, Skill skill) const {
    const auto &s = registry.get<Skills>(e);
    const auto index = static_cast<std::size_t>(skill);
    return skill_modifier(experience(const_cast<Skills &>(s), skill), s.practice[index], s.abilities[index],
                          ticks);
}
void useful_work(entt::entity e, Skill skill, std::uint32_t credit) {
    auto &s = registry.get<Skills>(e);
    record_skill_work(experience(s, skill), s.practice[static_cast<std::size_t>(skill)], credit, ticks);
    cache_dirty = true;
}
static bool profession_permits(Profession profession, Job job, bool construction_delivery = false) {
    switch (profession) {
    case Profession::Unassigned:
        return job_priority_index(job) >= 0;
    case Profession::Woodcutter:
        return job == Job::Woodcut;
    case Profession::Miner:
        return job == Job::Mine;
    case Profession::Forager:
        return job == Job::Forage;
    case Profession::Builder:
        return job == Job::Build || (job == Job::Haul && construction_delivery);
    case Profession::Hauler:
        return job == Job::Haul;
    case Profession::Farmer:
        return job == Job::Farm;
    default:
        return false;
    }
}
// Completed player orders keep their position for 30 simulation seconds.
// Reuse the persisted profession timer; the ManualOrder reason marks this grace period.
static constexpr std::uint64_t manual_autonomy_delay_ticks = 30 * 20;
bool waiting_for_player(entt::entity e) const {
    if(e==entt::null)return false;
    const auto&p=registry.get<ProfessionState>(e);
    return !p.manual_override && p.wait_reason==ProfessionWaitReason::ManualOrder && ticks<p.next_evaluation_tick;
}
void wait_after_manual(int id) {
    const auto e=find_resident(id);
    if(e==entt::null||registry.get<Vital>(e).health<=0)return;
    auto&p=registry.get<ProfessionState>(e);
    p.wait_reason=ProfessionWaitReason::ManualOrder;
    p.next_evaluation_tick=ticks+std::min(manual_autonomy_delay_ticks,UINT64_MAX-ticks);
    cache_dirty=true;
}
bool eligible_for_auto_work(entt::entity e, Job job, std::optional<CellCoord> site = {},
                            bool construction_delivery = false) const {
    if(e!=entt::null && (economy_worker(registry.get<Identity>(e).id) || expedition_worker(registry.get<Identity>(e).id)))return false;
    if (waiting_for_player(e) || !work_allowed(e,job,site)) return false;
    // Extraction requires an explicit finite manual order, regardless of profession.
    if (job == Job::Woodcut || job == Job::Mine || job == Job::Forage)
        return false;
    const auto &p = registry.get<ProfessionState>(e);
    const auto index = job_priority_index(job);
    return registry.get<Vital>(e).health > 0 && p.assignment.automation_enabled && !p.manual_override &&
           !p.deferred && index >= 0 && p.assignment.priorities[static_cast<std::size_t>(index)] > 0 &&
           (p.assignment.profession == Profession::Unassigned || p.assignment.worksite.has_value()) &&
           profession_permits(p.assignment.profession, job, construction_delivery) &&
           (!site || !p.assignment.worksite || *site == *p.assignment.worksite);
}
bool legacy_auto_worker(entt::entity e, Job job) const {
    return registry.get<ProfessionState>(e).assignment.profession == Profession::Unassigned &&
           !registry.get<ProfessionState>(e).assignment.worksite && eligible_for_auto_work(e, job);
}
void retire_manual_override(int id) {
    const auto e = find_resident(id);
    if (e == entt::null)
        return;
    auto &p = registry.get<ProfessionState>(e);
    if (!manual_mutation && manual_head(id)) {
        const auto &status = registry.get<OrderStatus>(e);
        if (status.phase == OrderPhase::PathFailed)
            finish_manual_actor(id, ManualOrderCompletion::None, status.failure);
        if (manual_head(id)) {
            manual_profile(id);
            return;
        }
    }
    const bool waiting=waiting_for_player(e);
    p.manual_override = false;
    if(!waiting)p.next_evaluation_tick = ticks;
    if (p.deferred && !resident_busy(id) && registry.get<OrderStatus>(e).phase != OrderPhase::PathPending) {
        p.deferred = false;
        p.wait_reason = registry.get<Vital>(e).health <= 0 ? ProfessionWaitReason::Incapacitated
                        : p.assignment.automation_enabled  ? ProfessionWaitReason::None
                                                           : ProfessionWaitReason::Stopped;
    }
    cache_dirty = true;
}
void begin_manual_override(const std::vector<int> &ids) {
    for (int id : ids) {
        auto &p = registry.get<ProfessionState>(find_resident(id));
        p.manual_override = true;
        p.deferred = false;
        p.wait_reason = ProfessionWaitReason::ManualOrder;
        p.next_evaluation_tick = ticks;
    }
    cache_dirty = true;
}
void stop_actor(int id) {
    clear_outdoor_work_accumulator(id);
    supersede_work_for_residents({id});
    const auto e = find_resident(id);
    registry.get<Activity>(e) = {};
    registry.get<Route>(e) = {};
    registry.get<OrderStatus>(e) = {};
    cache_dirty = true;
}
// Called only after the physical task has released its actor. Returns whether the
// previous gathering action may continue; a deferred policy always replaces it.
bool delivery_boundary(int id, Job resume, CellCoord target) {
    if (auto *o = manual_head(id)) {
        if (o->phase == ManualOrderPhase::WaitingBoundary) {
            o->phase = ManualOrderPhase::Queued;
            o->executor = ManualExecutorKind::None;
            o->executor_id = 0;
            manual_profile(id);
            return false;
        }
        if (o->request.kind == ManualOrderKind::Gather &&
            o->request.gather_completion == GatherCompletion::OneDelivery) {
            finish_manual(*o, ManualOrderCompletion::Delivered);
            return false;
        }
        if (o->request.kind == ManualOrderKind::Legacy && resume == Job::Idle) {
            finish_manual(*o, ManualOrderCompletion::LegacyFinished);
            return false;
        }
    }
    auto &p = registry.get<ProfessionState>(find_resident(id));
    if(!manual_head(id) && !p.deferred && p.wait_reason==ProfessionWaitReason::SelfGathering) {
        p.wait_reason=ProfessionWaitReason::Unassigned;
        p.next_evaluation_tick=ticks+160;
        return false;
    }
    const bool replaced = p.deferred;
    p.deferred = false;
    p.next_evaluation_tick = ticks;
    if (replaced) {
        p.manual_override = false;
        p.wait_reason =
            p.assignment.automation_enabled ? ProfessionWaitReason::None : ProfessionWaitReason::Stopped;
        supersede_work_for_residents({id});
        return false;
    }
    if (resume == Job::Idle)
        return false;
    if (p.manual_override)
        return true;
    return eligible_for_auto_work(find_resident(id), resume, target);
}
std::vector<ResidentCommandOutcome> change_profession(const std::vector<int> &input,
                                                      const ProfessionAssignment *assignment) {
    std::vector<int> ids = input;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<ResidentCommandOutcome> results;
    const bool valid = !assignment || World::valid_profession_assignment(*assignment);
    for (int id : ids) {
        const auto e = find_resident(id);
        if ((economy_worker(id) || expedition_worker(id)) || !valid || e == entt::null || registry.get<Vital>(e).health <= 0 ||
            (assignment && (assignment->profession != Profession::Unassigned || assignment->worksite) && policy(registry.get<Identity>(e).nation).youth(id,ticks) && !policy(registry.get<Identity>(e).nation).youth(id,ticks)->forced_work)) {
            results.push_back({id, ResidentCommandStatus::Rejected,
                               valid ? PathFailure::ResidentUnavailable : PathFailure::TargetInvalid});
            continue;
        }
        auto &p = registry.get<ProfessionState>(e);
        if (manual_head(id))
            clear_manual(id, assignment ? ManualOrderCompletion::Replaced : ManualOrderCompletion::Stopped);
        if (assignment)
            p.assignment = *assignment;
        else
            p.assignment.automation_enabled = false;
        p.manual_override = false;
        p.next_evaluation_tick = ticks;
        const auto &order = registry.get<OrderStatus>(e);
        const bool external_pending = order.phase == OrderPhase::PathPending && !active_work_intent(id);
        p.deferred = resident_busy(id) || external_pending;
        if (p.deferred) {
            p.wait_reason = resident_busy(id) ? ProfessionWaitReason::DeliveryBoundary
                                              : ProfessionWaitReason::PendingOrder;
            results.push_back({id, ResidentCommandStatus::Deferred, PathFailure::None});
        } else {
            stop_actor(id);
            p.wait_reason = !p.assignment.automation_enabled ? ProfessionWaitReason::Stopped
                            : p.assignment.profession == Profession::Unassigned
                                ? ProfessionWaitReason::Unassigned
                                : ProfessionWaitReason::None;
            results.push_back({id, ResidentCommandStatus::Applied, PathFailure::None});
        }
    }
    cache_dirty = true;
    prune_manual_history();
    return results;
}
void refresh_profession_resources() {
    if (profession_resource_index.size() != chunks.size()) {
        profession_resource_index.clear();
        for (const auto &chunk : chunks) {
            ProfessionChunkIndex index;
            index.key = chunk.key;
            index.revision = std::numeric_limits<std::uint64_t>::max();
            profession_resource_index.push_back(std::move(index));
        }
    }
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto &chunk = chunks[i];
        auto &index = profession_resource_index[i];
        if (index.valid && index.revision == chunk.revision)
            continue;
        index.revision = chunk.revision;
        index.valid = true;
        ++profession_diagnostics.chunk_refreshes_last_tick;
        for (auto &list : index.resources)
            list.clear();
        for (int y = 0; y < chunk_size; ++y)
            for (int x = 0; x < chunk_size; ++x) {
                const auto &t = chunk.tiles[static_cast<std::size_t>(y * chunk_size + x)];
                if (!t.explored || t.amount <= 0)
                    continue;
                const auto job = t.terrain == Terrain::Forest                                  ? Job::Woodcut
                                 : t.terrain == Terrain::Rock || t.terrain == Terrain::MineOre ? Job::Mine
                                 : t.terrain == Terrain::Grass                                 ? Job::Forage
                                                                                               : Job::Idle;
                if (job == Job::Idle)
                    continue;
                const CellCoord cell{chunk.key.cx * chunk_size + x, chunk.key.cy * chunk_size + y,
                                     chunk.key.layer};
                index.resources[static_cast<std::size_t>(job_priority_index(job))].push_back(
                    {job, cell, cell, static_cast<std::uint64_t>(encode_cell(cell))});
            }
    }
}
const Building *farm_at(CellCoord cell, NationId nation) const {
    const auto it = std::find_if(buildings.begin(), buildings.end(), [&](const auto &b) {
        return b.kind == BuildingKind::Farm && b.progress == 1 && b.nation == nation &&
               CellCoord{b.x, b.y, b.layer} == cell;
    });
    return it == buildings.end() ? nullptr : &*it;
}
void process_professions() {
    profession_diagnostics = {};
    const auto residents = entities();
    if (residents.empty())
        return;
    // Reconcile boundaries for every actor before any scheduler can claim it.
    for (auto e : residents) {
        auto &p = registry.get<ProfessionState>(e);
        const auto id = registry.get<Identity>(e).id;
        if (registry.get<Vital>(e).health <= 0) {
            if(waiting_for_player(e))p.next_evaluation_tick=ticks;
            p.manual_override = false;
            p.wait_reason = ProfessionWaitReason::Incapacitated;
            continue;
        }
        if (p.deferred && !resident_busy(id) &&
            registry.get<OrderStatus>(e).phase != OrderPhase::PathPending) {
            stop_actor(id);
            p.deferred = false;
            p.next_evaluation_tick = ticks;
        }
        if (p.deferred)
            p.wait_reason = resident_busy(id) ? ProfessionWaitReason::DeliveryBoundary
                                              : ProfessionWaitReason::PendingOrder;
        else if (p.manual_override)
            p.wait_reason = ProfessionWaitReason::ManualOrder;
        else if (!p.assignment.automation_enabled) {
            if(waiting_for_player(e))p.next_evaluation_tick=ticks;
            p.wait_reason = ProfessionWaitReason::Stopped;
        }
    }
    bool indexed = false;
    std::array<std::map<ChunkKey, std::vector<ProfessionCandidate>>, 5> index;
    std::unordered_map<int, std::uint64_t> latest_work;
    std::map<int, int> farm_workers;
    auto build_index = [&] {
        refresh_profession_resources();
        for (const auto &chunk : profession_resource_index)
            for (std::size_t j = 0; j < 3; ++j)
                if (!chunk.resources[j].empty())
                    index[j][chunk.key] = chunk.resources[j];
        std::map<int, std::uint32_t> workers;
        std::map<int, const WorkIntentState *> site_intents;
        for (const auto &w : work_intents) {
            latest_work[w.resident_id] = w.intent_id;
            if (w.kind == WorkIntentKind::BuildAssignment)
                site_intents[w.building_id] = &w;
        }
        for (const auto &v : construction_workforce())
            workers[v.building_id] = v.working + v.travelling + v.planning;
        for (const auto &site : construction_orders) {
            const auto prior = site_intents.find(site.building_id);
            if (prior != site_intents.end() && prior->second->phase == WorkIntentPhase::Failed) {
                const auto &w = *prior->second;
                const bool changed =
                    w.last_attempt_topology_revision != topology_revision &&
                    (w.failure == PathFailure::Unreachable || w.failure == PathFailure::TopologyChanged);
                if (w.failure != PathFailure::ResidentUnavailable && !changed)
                    continue;
            }
            if (site.phase == ConstructionPhase::Work && !workers[site.building_id] &&
                !active_work_by_building.contains(site.building_id))
                index[3][chunk_key(site.site)].push_back(
                    {Job::Build, site.site, site.site, static_cast<std::uint64_t>(site.building_id)});
        }
        // Travelling workers and harvest deliveries retain a place at their farm.
        for (const auto &b : buildings) {
            if (b.kind != BuildingKind::Farm || b.progress != 1 || !tile({b.x,b.y,b.layer}).explored) continue;
            const CellCoord site{b.x,b.y,b.layer};
            for (auto actor : residents) {
                const auto id = registry.get<Identity>(actor).id;
                const auto &route = registry.get<Route>(actor);
                bool assigned = registry.get<Vital>(actor).health > 0 && registry.get<Activity>(actor).job == Job::Farm && route.work == site;
                if (const auto *intent = active_work_intent(id)) assigned |= intent->job == Job::Farm && intent->target == site;
                if (resident_busy(id)) {
                    const auto *delivery = task(active_task_by_resident.at(id));
                    assigned |= delivery && delivery->resume_job == Job::Farm && delivery->resume_target == site;
                }
                farm_workers[b.id] += assigned;
            }
            index[2][chunk_key(site)].push_back({Job::Farm, site, site, static_cast<std::uint64_t>(b.id)});
        }
        for (const auto &t : logistics_tasks) {
            if (t.phase != LogisticsPhase::Queued || t.resident_id)
                continue;
            const auto *source = stack(t.source_stack_id);
            const auto *dest = building(t.destination_building_id);
            auto position = source ? location_cell(source->location) : std::optional<CellCoord>{};
            if (!position || !dest || !tile(*position).explored)
                continue;
            if (t.kind == LogisticsTaskKind::DeliverConstruction) {
                const auto *site = construction(t.destination_building_id);
                if (!site || site->phase != ConstructionPhase::Delivery)
                    continue;
            }
            const CellCoord site{dest->x, dest->y, dest->layer};
            index[4][chunk_key(*position)].push_back({Job::Haul, *position, site, t.task_id});
        }
        indexed = true;
    };
    auto first = std::lower_bound(residents.begin(), residents.end(), profession_cursor_id,
                                  [&](auto e, int id) { return registry.get<Identity>(e).id < id; });
    std::size_t cursor = first == residents.end() ? 0 : static_cast<std::size_t>(first - residents.begin());
    std::size_t admissions = 0;
    for (std::size_t visits = 0; visits < std::min<std::size_t>(profession_visits_per_tick, residents.size());
         ++visits) {
        ++profession_diagnostics.visits_last_tick;
        const auto e = residents[cursor];
        cursor = (cursor + 1) % residents.size();
        profession_cursor_id = registry.get<Identity>(residents[cursor]).id;
        const auto &identity = registry.get<Identity>(e);
        auto &p = registry.get<ProfessionState>(e);
        if ((economy_worker(identity.id) || expedition_worker(identity.id)) || registry.get<Vital>(e).health <= 0 || p.deferred || p.manual_override ||
            !p.assignment.automation_enabled ||
            registry.get<OrderStatus>(e).phase == OrderPhase::PathPending ||
            registry.get<Activity>(e).job != Job::Idle || resident_busy(identity.id) ||
            active_work_by_resident.contains(identity.id) || ticks < p.next_evaluation_tick)
            continue;
        if(p.wait_reason==ProfessionWaitReason::ManualOrder)p.wait_reason=ProfessionWaitReason::None;
        p.next_evaluation_tick =
            ticks + std::min<std::uint64_t>(profession_evaluation_ticks,
                                            std::numeric_limits<std::uint64_t>::max() - ticks);
        if (!p.assignment.worksite ||
            p.assignment.profession == Profession::Woodcutter ||
            p.assignment.profession == Profession::Miner ||
            p.assignment.profession == Profession::Forager) {
            free_activity(e);
            continue;
        }
        if (p.assignment.profession == Profession::Smith ||
            p.assignment.profession == Profession::Medic || p.assignment.profession == Profession::Guard) {
            p.wait_reason = ProfessionWaitReason::NoCompatibleWork;
            continue;
        }
        if (!indexed)
            build_index();
        const auto from = resident_cell(identity.id);
        std::optional<ProfessionCandidate> selected;
        std::optional<std::tuple<int, int, int, int, int, std::uint64_t>> best;
        for (std::size_t j = 0; j < index.size(); ++j) {
            if (j < 3 && p.assignment.profession == Profession::Unassigned)
                continue;
            const Job job = j == 0   ? Job::Woodcut
                            : j == 1 ? Job::Mine
                            : j == 2 ? (p.assignment.profession == Profession::Farmer ? Job::Farm : Job::Forage)
                            : j == 3 ? Job::Build
                                     : Job::Haul;
            if (!eligible_for_auto_work(e, job, {}, job == Job::Haul))
                continue;
            const int priority = p.assignment.priorities[j];
            if (best && -priority > std::get<0>(*best))
                continue;
            std::vector<std::pair<int, const std::vector<ProfessionCandidate> *>> buckets;
            for (const auto &[key, list] : index[j]) {
                const int x0 = key.cx * chunk_size, y0 = key.cy * chunk_size;
                const int distance = std::max({x0 - from.x, 0, from.x - (x0 + chunk_size - 1)}) +
                                     std::max({y0 - from.y, 0, from.y - (y0 + chunk_size - 1)}) +
                                     (from.layer == key.layer ? 0 : 64);
                buckets.push_back({distance, &list});
            }
            std::stable_sort(buckets.begin(), buckets.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[minimum, list] : buckets) {
                if (best && -priority == std::get<0>(*best) && minimum > std::get<1>(*best))
                    break;
                for (const auto &candidate : *list) {
                    ++profession_diagnostics.candidate_checks_last_tick;
                    if (candidate.job != job) continue;
                    if (!eligible_for_auto_work(e, job, candidate.worksite, job == Job::Haul))
                        continue;
                    if (job == Job::Farm) {
                        const auto *farm = farm_at(candidate.worksite, identity.nation);
                        if (!farm || farm_workers[farm->id] >= farm_worker_limit) continue;
                    }
                    if (job == Job::Build) {
                        const auto *site = construction(static_cast<int>(candidate.id));
                        if (!site || site->owner_nation != identity.nation ||
                            active_work_by_building.contains(site->building_id))
                            continue;
                    } else if (job == Job::Haul) {
                        const auto *t = task(candidate.id);
                        const auto *source = t ? stack(t->source_stack_id) : nullptr;
                        if (!t || t->phase != LogisticsPhase::Queued || t->resident_id || !source ||
                            source->owner_nation != identity.nation ||
                            !profession_permits(p.assignment.profession, Job::Haul,
                                                t->kind == LogisticsTaskKind::DeliverConstruction))
                            continue;
                    }
                    const int distance = std::abs(from.x - candidate.cell.x) +
                                         std::abs(from.y - candidate.cell.y) +
                                         (from.layer == candidate.cell.layer ? 0 : 64);
                    const auto score =
                        std::tuple{-priority,        distance,         static_cast<int>(candidate.cell.layer),
                                   candidate.cell.y, candidate.cell.x, candidate.id};
                    if (!best || score < *best) {
                        best = score;
                        selected = candidate;
                    }
                }
            }
        }
        if (!selected) {
            p.wait_reason = p.assignment.worksite ? ProfessionWaitReason::WorksiteUnavailable
                                                  : ProfessionWaitReason::NoCompatibleWork;
            continue;
        }
        // A failed identical route is retried only on a topology change or explicit new command.
        const auto previous_id = latest_work.find(identity.id);
        const auto *previous = previous_id == latest_work.end() ? nullptr : work_intent(previous_id->second);
        if (p.wait_reason == ProfessionWaitReason::PathFailed &&
            (!previous || previous->phase != WorkIntentPhase::Failed))
            continue;
        if (previous && previous->phase == WorkIntentPhase::Failed &&
            previous->target == selected->worksite &&
            (previous->last_attempt_topology_revision == topology_revision ||
             (previous->failure != PathFailure::Unreachable &&
              previous->failure != PathFailure::TopologyChanged))) {
            p.wait_reason = ProfessionWaitReason::PathFailed;
            continue;
        }
        bool accepted = false;
        if (selected->job == Job::Haul) {
            auto *t = task(selected->id);
            supersede_work_for_residents({identity.id});
            t->resident_id = identity.id;
            t->phase = LogisticsPhase::ToPickup;
            active_task_by_resident[identity.id] = t->task_id;
            registry.get<Activity>(e) = {Job::Haul, 0};
            registry.get<Route>(e) = {};
            registry.get<OrderStatus>(e) = {};
            accepted = true;
        } else
            accepted = create_work_intent(identity.id,
                                          selected->job == Job::Build ? WorkIntentKind::BuildAssignment
                                                                      : WorkIntentKind::GatherContinuation,
                                          selected->worksite, selected->job,
                                          selected->job == Job::Build ? static_cast<int>(selected->id) : 0);
        if (accepted && selected->job == Job::Farm) ++farm_workers[static_cast<int>(selected->id)];
        p.wait_reason = accepted ? ProfessionWaitReason::None : ProfessionWaitReason::QueuePressure;
        ++admissions;
        profession_diagnostics.admissions_last_tick = admissions;
        if (admissions == profession_admissions_per_tick)
            break;
    }
    cache_dirty = true;
}

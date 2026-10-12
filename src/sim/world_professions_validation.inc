static bool validate_professions(const WorldState &state, std::string &error, bool legacy = false) {
    auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    int maximum_id = 1;
    std::set<int> physical_actors;
    std::set<int> intent_actors, routed_actors;
    std::map<int, const Resident *> actors;
    std::set<int> queued_actors;
    if (!legacy)
        for (const auto &o : state.manual_orders)
            if (manual_live(o))
                queued_actors.insert(o.resident_id);
    for (const auto &work : state.work_intents)
        if (active_work(work))
            intent_actors.insert(work.resident_id);
    for (const auto &route : state.routes)
        if (route.work)
            routed_actors.insert(route.resident_id);
    for (const auto &task : state.logistics_tasks)
        if (task.resident_id && task.phase != LogisticsPhase::Complete &&
            task.phase != LogisticsPhase::Failed)
            physical_actors.insert(task.resident_id);
    for (const auto &r : state.residents) {
        maximum_id = std::max(maximum_id, r.id);
        actors[r.id] = &r;
        const auto &p = r.profession;
        if (!World::valid_profession_assignment(p.assignment) ||
            static_cast<unsigned>(p.wait_reason) >
                static_cast<unsigned>(ProfessionWaitReason::SelfGathering) ||
            (p.deferred && p.manual_override) ||
            (p.manual_override && !queued_actors.contains(r.id) && !physical_actors.contains(r.id) &&
             !intent_actors.contains(r.id) && r.order_phase != OrderPhase::PathPending &&
             (!routed_actors.contains(r.id) ||
              (r.job != Job::Move && r.job != Job::Woodcut && r.job != Job::Mine && r.job != Job::Forage))) ||
            p.next_evaluation_tick >
                state.ticks +
                    std::min<std::uint64_t>(p.wait_reason==ProfessionWaitReason::ManualOrder ? manual_autonomy_delay_ticks : p.assignment.profession==Profession::Unassigned ? 200 : profession_evaluation_ticks,
                                            std::numeric_limits<std::uint64_t>::max() - state.ticks) ||
            (p.deferred && !physical_actors.contains(r.id) && r.order_phase != OrderPhase::PathPending))
            return fail("Profession state or deferred boundary is invalid.");
        if (r.job == Job::Farm) {
            const auto route = std::find_if(state.routes.begin(), state.routes.end(), [&](const auto &v) { return v.resident_id == r.id; });
            if (route == state.routes.end() || !route->work ||
                !std::any_of(state.buildings.begin(), state.buildings.end(), [&](const auto &b) {
                    return b.kind == BuildingKind::Farm && b.progress == 1 && b.nation == r.nation &&
                           CellCoord{b.x,b.y,b.layer} == *route->work;
                })) return fail("Farmer has no completed farm worksite.");
        }
        for (std::size_t index = 0; index < skill_count; ++index) {
            const int xp = index == 0   ? r.wood_xp
                           : index == 1 ? r.mining_xp
                           : index == 2 ? r.farming_xp
                                        : r.additional_xp[index - 3];
            if (xp < 0 || xp > maximum_skill_experience ||
                !valid_skill_practice(xp, r.skill_practice[index], state.ticks) ||
                r.abilities_bp[index] < -1500 || r.abilities_bp[index] > 1500)
                return fail("Resident skill practice or ability is invalid.");
        }
    }
    if (state.profession_cursor_id <= 0 || state.profession_cursor_id > maximum_id)
        return fail("Profession fairness cursor is invalid.");
    for (const auto &work : state.work_intents) {
        if (!active_work(work))
            continue;
        const auto &p = actors.at(work.resident_id)->profession;
        if (p.manual_override)
            continue;
        const int index = job_priority_index(work.job);
        if (p.deferred || !p.assignment.automation_enabled || index < 0 ||
            !p.assignment.priorities[static_cast<std::size_t>(index)] ||
            !profession_permits(p.assignment.profession, work.job) ||
            (p.assignment.worksite && *p.assignment.worksite != work.target))
            return fail("Automatic work conflicts with its profession assignment.");
    }
    return true;
}

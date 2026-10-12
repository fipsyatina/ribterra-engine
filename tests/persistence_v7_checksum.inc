// Test-only historical projection copied from d3bc78530ab2f03d8116bb9ef4c09ba71ca0232d.
// Do not add format 8 fields: these bytes are checked against the frozen writer sequences.
void hash_u64(std::uint64_t &h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (unsigned char)(v >> (i * 8));
        h *= 1099511628211ULL;
    }
}
void hash_string(std::uint64_t &h, const std::string &v) {
    hash_u64(h, v.size());
    for (unsigned char c : v) {
        h ^= c;
        h *= 1099511628211ULL;
    }
}
std::uint64_t legacy_v7_checksum(const ribterra::WorldState &state) {
    std::uint64_t h = 1469598103934665603ULL;
    hash_u64(h, state.seed);
    hash_u64(h, state.ticks);
    hash_u64(h, state.next_building_id);
    hash_u64(h, state.has_moved);
    hash_u64(h, state.has_gathered);
    hash_u64(h, state.has_built);
    for (auto &c : state.chunks) {
        hash_u64(h, (int)c.key.layer);
        hash_u64(h, c.key.cx);
        hash_u64(h, c.key.cy);
        hash_u64(h, c.revision);
        for (auto &t : c.tiles) {
            hash_u64(h, (int)t.terrain);
            hash_u64(h, t.amount);
            hash_u64(h, t.explored);
        }
    }
    for (auto &r : state.residents) {
        hash_u64(h, r.id);
        hash_string(h, r.name);
        hash_u64(h, std::bit_cast<uint32_t>(r.x));
        hash_u64(h, std::bit_cast<uint32_t>(r.y));
        hash_u64(h, r.target_x);
        hash_u64(h, r.target_y);
        hash_u64(h, static_cast<int>(r.job));
        hash_u64(h, std::bit_cast<uint32_t>(r.progress));
        hash_u64(h, r.health);
        hash_u64(h, std::bit_cast<uint32_t>(r.hunger));
        hash_u64(h, r.wood_xp);
        hash_u64(h, r.mining_xp);
        hash_u64(h, r.farming_xp);
        for (int xp : r.additional_xp)
            hash_u64(h, xp);
        for (const auto &p : r.skill_practice) {
            hash_u64(h, p.experience_remainder);
            hash_u64(h, p.familiarity_debt);
            hash_u64(h, p.last_practice_tick);
        }
        for (auto ability : r.abilities_bp)
            hash_u64(h, ability);
        const auto &profession = r.profession;
        hash_u64(h, static_cast<unsigned>(profession.assignment.profession));
        hash_u64(h, profession.assignment.worksite.has_value());
        if (profession.assignment.worksite) {
            hash_u64(h, profession.assignment.worksite->x);
            hash_u64(h, profession.assignment.worksite->y);
            hash_u64(h, static_cast<unsigned>(profession.assignment.worksite->layer));
        }
        for (auto priority : profession.assignment.priorities)
            hash_u64(h, priority);
        hash_u64(h, profession.assignment.automation_enabled);
        hash_u64(h, profession.manual_override);
        hash_u64(h, profession.deferred);
        hash_u64(h, static_cast<unsigned>(profession.wait_reason));
        hash_u64(h, profession.next_evaluation_tick);
        hash_u64(h, (int)r.layer);
        hash_u64(h, r.nation);
        hash_u64(h, static_cast<unsigned>(r.order_phase));
        hash_u64(h, static_cast<unsigned>(r.path_failure));
        hash_u64(h, r.pending_path_batch);
    }
    for (auto &b : state.buildings) {
        hash_u64(h, b.id);
        hash_u64(h, (int)b.kind);
        hash_u64(h, b.x);
        hash_u64(h, b.y);
        hash_u64(h, (int)b.layer);
        hash_u64(h, b.nation);
        hash_u64(h, std::bit_cast<uint32_t>(b.progress));
    }
    hash_u64(h, state.resources.wood);
    hash_u64(h, state.resources.stone);
    hash_u64(h, state.resources.food);
    for (auto &r : state.routes) {
        hash_u64(h, r.resident_id);
        hash_u64(h, r.cursor);
        hash_u64(h, r.work.has_value());
        if (r.work) {
            hash_u64(h, r.work->x);
            hash_u64(h, r.work->y);
            hash_u64(h, static_cast<int>(r.work->layer));
        }
        for (auto &p : r.points) {
            hash_u64(h, p.x);
            hash_u64(h, p.y);
            hash_u64(h, (int)p.layer);
        }
    }
    for (const auto &portal : state.portals) {
        hash_u64(h, portal.surface.x);
        hash_u64(h, portal.surface.y);
        hash_u64(h, portal.mine.x);
        hash_u64(h, portal.mine.y);
    }
    for (auto &e : state.events)
        hash_string(h, e);
    hash_u64(h, state.next_path_batch_id);
    hash_u64(h, state.topology_revision);
    const auto hash_cell = [&](const CellCoord &cell) {
        hash_u64(h, cell.x);
        hash_u64(h, cell.y);
        hash_u64(h, static_cast<unsigned>(cell.layer));
    };
    hash_u64(h, state.path_batches.size());
    for (const auto &batch : state.path_batches) {
        hash_u64(h, batch.path_batch_id);
        hash_u64(h, static_cast<unsigned>(batch.command));
        hash_u64(h, static_cast<unsigned>(batch.phase));
        hash_u64(h, batch.resident_ids.size());
        for (int id : batch.resident_ids)
            hash_u64(h, id);
        hash_cell(batch.target);
        hash_u64(h, batch.to_adjacent);
        hash_u64(h, static_cast<unsigned>(batch.assigned_job));
        hash_u64(h, batch.build_kind.has_value());
        if (batch.build_kind)
            hash_u64(h, static_cast<unsigned>(*batch.build_kind));
        hash_u64(h, batch.building_id);
        hash_u64(h, batch.reserved_cost.wood);
        hash_u64(h, batch.reserved_cost.stone);
        hash_u64(h, batch.reserved_cost.food);
        hash_u64(h, batch.enqueued_tick);
        hash_u64(h, batch.deadline_tick);
        hash_u64(h, batch.search_topology_revision);
        hash_u64(h, batch.restart_count);
        hash_u64(h, batch.work_units_done);
        hash_u64(h, batch.roots.size());
        for (const auto &cell : batch.roots)
            hash_cell(cell);
        hash_u64(h, batch.search_storage_reserved);
        hash_u64(h, batch.frontier.size());
        for (const auto &cell : batch.frontier)
            hash_cell(cell);
        hash_u64(h, batch.frontier_cursor);
        hash_u64(h, batch.nodes.size());
        for (const auto &node : batch.nodes) {
            hash_cell(node.cell);
            hash_cell(node.next);
        }
        hash_u64(h, batch.drafts.size());
        for (const auto &draft : batch.drafts) {
            hash_u64(h, draft.resident_id);
            hash_cell(draft.start);
            hash_u64(h, std::bit_cast<std::uint32_t>(draft.start_x));
            hash_u64(h, std::bit_cast<std::uint32_t>(draft.start_y));
            hash_u64(h, draft.storage_reserved);
            hash_u64(h, draft.reconstruction_cursor.has_value());
            if (draft.reconstruction_cursor)
                hash_cell(*draft.reconstruction_cursor);
            hash_u64(h, draft.staged.size());
            for (const auto &cell : draft.staged)
                hash_cell(cell);
        }
        hash_u64(h, batch.reconstruction_resident);
        hash_u64(h, batch.stage_cursor);
    }
    hash_u64(h, state.path_results.size());
    for (const auto &outcome : state.path_results) {
        hash_u64(h, outcome.path_batch_id);
        hash_u64(h, static_cast<unsigned>(outcome.command));
        hash_u64(h, outcome.accepted);
        hash_u64(h, static_cast<unsigned>(outcome.failure));
        hash_string(h, outcome.message);
    }
    hash_u64(h, state.next_item_stack_id);
    hash_u64(h, state.next_item_reservation_id);
    hash_u64(h, state.next_logistics_task_id);
    hash_u64(h, state.item_stacks.size());
    for (const auto &s : state.item_stacks) {
        hash_u64(h, s.stack_id);
        hash_u64(h, static_cast<unsigned>(s.item));
        hash_u64(h, s.quantity);
        hash_u64(h, s.owner_nation);
        hash_u64(h, static_cast<unsigned>(s.location.kind));
        hash_cell(s.location.cell);
        hash_u64(h, s.location.entity_id);
    }
    hash_u64(h, state.storages.size());
    for (const auto &s : state.storages) {
        hash_u64(h, s.building_id);
        hash_u64(h, s.capacity);
        hash_u64(h, s.accepted_items);
        hash_u64(h, s.priority);
    }
    hash_u64(h, state.item_reservations.size());
    for (const auto &r : state.item_reservations) {
        hash_u64(h, r.reservation_id);
        hash_u64(h, r.stack_id);
        hash_u64(h, r.task_id);
        hash_u64(h, r.quantity);
        hash_u64(h, r.owner_nation);
    }
    hash_u64(h, state.logistics_tasks.size());
    for (const auto &t : state.logistics_tasks) {
        hash_u64(h, t.task_id);
        hash_u64(h, static_cast<unsigned>(t.kind));
        hash_u64(h, static_cast<unsigned>(t.phase));
        hash_u64(h, static_cast<unsigned>(t.failure));
        hash_u64(h, t.resident_id);
        hash_u64(h, t.destination_building_id);
        hash_u64(h, t.source_stack_id);
        hash_u64(h, t.reservation_id);
        hash_u64(h, t.carry_stack_id);
        hash_u64(h, t.path_batch_id);
        hash_u64(h, static_cast<unsigned>(t.item));
        hash_u64(h, t.quantity);
        hash_u64(h, t.deadline_tick);
        hash_u64(h, static_cast<unsigned>(t.resume_job));
        hash_cell(t.resume_target);
    }
    hash_u64(h, state.construction_orders.size());
    for (const auto &o : state.construction_orders) {
        hash_u64(h, o.building_id);
        hash_u64(h, static_cast<unsigned>(o.kind));
        hash_cell(o.site);
        hash_u64(h, o.owner_nation);
        hash_u64(h, o.required.wood);
        hash_u64(h, o.required.stone);
        hash_u64(h, o.required.food);
        hash_u64(h, o.delivered.wood);
        hash_u64(h, o.delivered.stone);
        hash_u64(h, o.delivered.food);
        hash_u64(h, static_cast<unsigned>(o.phase));
        hash_u64(h, o.originating_path_batch_id);
    }
    hash_u64(h, state.next_work_intent_id);
    hash_u64(h, state.work_intent_cursor_id);
    hash_u64(h, state.work_building_cursor_id);
    hash_u64(h, state.profession_cursor_id);
    hash_u64(h, state.work_intents.size());
    for (const auto &w : state.work_intents) {
        hash_u64(h, w.intent_id);
        hash_u64(h, static_cast<unsigned>(w.kind));
        hash_u64(h, static_cast<unsigned>(w.phase));
        hash_u64(h, static_cast<unsigned>(w.completion));
        hash_u64(h, static_cast<unsigned>(w.failure));
        hash_u64(h, w.resident_id);
        hash_u64(h, w.building_id);
        hash_u64(h, w.owner_nation);
        hash_cell(w.target);
        hash_u64(h, static_cast<unsigned>(w.job));
        hash_u64(h, static_cast<unsigned>(w.expected_terrain));
        hash_u64(h, w.created_tick);
        hash_u64(h, w.next_attempt_tick);
        hash_u64(h, w.admission_deadline_tick);
        hash_u64(h, w.path_batch_id);
        hash_u64(h, w.attempts);
        hash_u64(h, w.last_attempt_topology_revision);
    }
    return h;
}

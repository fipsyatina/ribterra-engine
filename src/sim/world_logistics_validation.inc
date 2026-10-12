static bool validate_logistics(const WorldState &state, std::string &error) {
    auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    if (!state.next_item_stack_id || !state.next_item_reservation_id ||
        !state.next_logistics_task_id || state.item_stacks.size() > max_item_stacks ||
        state.item_reservations.size() > max_item_reservations ||
        state.logistics_tasks.size() > max_logistics_tasks ||
        state.construction_orders.size() > world_size * world_size)
        return fail("Inventory counters or limits are invalid.");
    std::map<int, const Building *> buildings;
    std::map<int, const Resident *> residents;
    std::map<int, const StorageState *> storages;
    std::map<int, const ConstructionOrderState *> orders;
    std::map<std::uint64_t, const ItemStackState *> stacks;
    std::map<std::uint64_t, const ItemReservationState *> reservations;
    std::map<std::uint64_t, const LogisticsTaskState *> tasks;
    std::map<std::uint64_t, const PathBatchState *> paths;
    for (const auto &b : state.buildings)
        buildings[b.id] = &b;
    for (const auto &r : state.residents)
        residents[r.id] = &r;
    for (const auto &p : state.path_batches)
        paths[p.path_batch_id] = &p;
    for (const auto &s : state.storages) {
        if (!storages.emplace(s.building_id, &s).second || !buildings.contains(s.building_id) ||
            !s.capacity || s.accepted_items == 0 || s.accepted_items > 7 || s.priority < -100000 ||
            s.priority > 100000)
            return fail("Storage metadata is invalid.");
        const auto *b = buildings.at(s.building_id);
        if (b->progress != 1 ||
            (b->kind != BuildingKind::Camp && b->kind != BuildingKind::Stockpile && b->kind!=BuildingKind::House && b->kind!=BuildingKind::Farm && b->kind!=BuildingKind::Workshop && b->kind!=BuildingKind::Fishing && b->kind!=BuildingKind::Sawmill && b->kind!=BuildingKind::Mill && b->kind!=BuildingKind::Kitchen && b->kind!=BuildingKind::ClayPit))
            return fail("Storage references an unfinished or incompatible building.");
    }
    for (const auto &order : state.construction_orders) {
        if (order.building_id <= 0 || order.building_id >= state.next_building_id ||
            !orders.emplace(order.building_id, &order).second || order.owner_nation == 0 ||
            static_cast<unsigned>(order.kind) < 1 || !valid_building_kind(order.kind) ||
            !inside_world(order.site.x, order.site.y) || order.site.layer != WorldLayer::Surface ||
            order.required != World::cost(order.kind) ||
            static_cast<unsigned>(order.phase) >
                static_cast<unsigned>(ConstructionPhase::Cancelled) ||
            order.delivered.wood < 0 || order.delivered.stone < 0 || order.delivered.food < 0 ||
            order.delivered.wood > order.required.wood ||
            order.delivered.stone > order.required.stone ||
            order.delivered.food > order.required.food ||
            order.originating_path_batch_id >= state.next_path_batch_id)
            return fail("Construction metadata is invalid.");
        if (order.phase == ConstructionPhase::Planned) {
            if (buildings.contains(order.building_id) ||
                !paths.contains(order.originating_path_batch_id) ||
                paths.at(order.originating_path_batch_id)->command != PathCommandKind::Build ||
                paths.at(order.originating_path_batch_id)->building_id != order.building_id ||
                paths.at(order.originating_path_batch_id)->target != order.site ||
                order.delivered != Resources{0, 0, 0})
                return fail("Planned construction path is invalid.");
        } else if (order.phase == ConstructionPhase::Cancelled) {
            if (buildings.contains(order.building_id))
                return fail("Cancelled construction retains a building.");
        } else {
            if (!buildings.contains(order.building_id))
                return fail("Construction building is missing.");
            const auto &b = *buildings.at(order.building_id);
            if (b.kind != order.kind || b.nation != order.owner_nation ||
                CellCoord{b.x, b.y, b.layer} != order.site ||
                (order.phase == ConstructionPhase::Delivery && b.progress != 0) ||
                ((order.phase == ConstructionPhase::Work ||
                  order.phase == ConstructionPhase::Complete) &&
                 order.delivered != order.required) ||
                (order.phase == ConstructionPhase::Complete) != (b.progress == 1))
                return fail("Construction phase, delivery, or work is inconsistent.");
        }
    }
    for (const auto &b : state.buildings) {
        if (b.progress < 1 && !orders.contains(b.id))
            return fail("Unfinished building has no delivery order.");
        if (b.progress == 1 &&
            (b.kind == BuildingKind::Camp || b.kind == BuildingKind::Stockpile) &&
            !storages.contains(b.id))
            return fail("Completed storage building has no storage definition.");
    }
    std::uint64_t previous = 0;
    std::map<std::pair<NationId, ItemKind>, std::uint64_t> totals;
    std::map<int, std::uint64_t> occupied;
    std::map<int, std::uint64_t> carried_by_resident;
    std::map<int, Resources> site_materials;
    for (const auto &s : state.item_stacks) {
        if (!s.stack_id || s.stack_id <= previous || s.stack_id >= state.next_item_stack_id ||
            !stacks.emplace(s.stack_id, &s).second || !s.quantity || !s.owner_nation ||
            static_cast<unsigned>(s.item) >= economy_item_count || static_cast<unsigned>(s.location.kind) > 3)
            return fail("Item stack metadata is invalid.");
        previous = s.stack_id;
        auto &total = totals[{s.owner_nation, s.item}];
        total += s.quantity;
        if (total > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return fail("Inventory total cannot be represented.");
        if (s.location.kind == ItemLocationKind::Ground) {
            if (s.location.entity_id != 0 || !inside_world(s.location.cell.x, s.location.cell.y) ||
                !valid_layer(s.location.cell.layer))
                return fail("Ground item location is invalid.");
        } else {
            if (s.location.cell != CellCoord{} || s.location.entity_id <= 0)
                return fail("Entity item location is not canonical.");
            if (s.location.kind == ItemLocationKind::Carried) {
                if (!residents.contains(s.location.entity_id) ||
                    residents.at(s.location.entity_id)->nation != s.owner_nation ||
                    s.quantity > resident_carry_capacity ||
                    !carried_by_resident.emplace(s.location.entity_id, s.stack_id).second)
                    return fail("Carried item ownership or capacity is invalid.");
                const auto &carrier = *residents.at(s.location.entity_id);
                if (std::lround(carrier.x) < world_min || std::lround(carrier.x) >= world_max ||
                    std::lround(carrier.y) < world_min || std::lround(carrier.y) >= world_max)
                    return fail("Carrier position cannot resolve to a valid drop cell.");
            } else {
                if (!buildings.contains(s.location.entity_id) ||
                    buildings.at(s.location.entity_id)->nation != s.owner_nation)
                    return fail("Item building ownership is invalid.");
                if (s.location.kind == ItemLocationKind::Storage) {
                    if (!storages.contains(s.location.entity_id) ||
                        !storage_accepts(storages.at(s.location.entity_id)->accepted_items,s.item))
                        return fail("Storage item kind is not accepted.");
                    occupied[s.location.entity_id] += s.quantity;
                    if (occupied[s.location.entity_id] >
                        storages.at(s.location.entity_id)->capacity)
                        return fail("Storage capacity is exceeded.");
                } else {
                    if (!orders.contains(s.location.entity_id) ||
                        orders.at(s.location.entity_id)->phase != ConstructionPhase::Delivery)
                        return fail("Construction material has no delivery phase.");
                    auto [it, inserted] =
                        site_materials.try_emplace(s.location.entity_id, Resources{0, 0, 0});
                    amount(it->second, s.item) += static_cast<int>(s.quantity);
                }
            }
        }
    }
    Resources expected{0, 0, 0};
    for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food})
        amount(expected, item) = static_cast<int>(totals[{1, item}]);
    if (expected != state.resources)
        return fail("Resource projection does not match physical items.");
    previous = 0;
    std::map<std::uint64_t, std::uint64_t> reserved_quantity;
    for (const auto &r : state.item_reservations) {
        if (!r.reservation_id || r.reservation_id <= previous ||
            r.reservation_id >= state.next_item_reservation_id ||
            !reservations.emplace(r.reservation_id, &r).second || !r.task_id || !r.quantity ||
            r.quantity > resident_carry_capacity || !stacks.contains(r.stack_id) ||
            stacks.at(r.stack_id)->owner_nation != r.owner_nation ||
            (stacks.at(r.stack_id)->location.kind != ItemLocationKind::Ground &&
             stacks.at(r.stack_id)->location.kind != ItemLocationKind::Storage))
            return fail("Item reservation metadata is invalid.");
        previous = r.reservation_id;
        reserved_quantity[r.stack_id] += r.quantity;
        if (reserved_quantity[r.stack_id] > stacks.at(r.stack_id)->quantity)
            return fail("Item is reserved more than once.");
    }
    previous = 0;
    std::set<int> assigned;
    std::set<std::uint64_t> correlated_paths, assigned_cargo;
    std::map<int, Resources> remaining_materials;
    for (const auto &t : state.logistics_tasks) {
        if (!t.task_id || t.task_id <= previous || t.task_id >= state.next_logistics_task_id ||
            !tasks.emplace(t.task_id, &t).second || static_cast<unsigned>(t.kind) > 1 ||
            static_cast<unsigned>(t.phase) > 4 || static_cast<unsigned>(t.failure) > 7 ||
            static_cast<unsigned>(t.item) >= economy_item_count || !t.quantity ||
            t.quantity > resident_carry_capacity || !t.deadline_tick ||
            (t.deadline_tick > state.ticks && t.deadline_tick - state.ticks > 10000) ||
            (t.resume_job != Job::Idle && t.resume_job != Job::Woodcut &&
             t.resume_job != Job::Mine && t.resume_job != Job::Forage && t.resume_job != Job::Farm) ||
            !inside_world(t.resume_target.x, t.resume_target.y) ||
            !valid_layer(t.resume_target.layer) ||
            (t.resident_id && !residents.contains(t.resident_id)))
            return fail("Logistics task metadata is invalid.");
        previous = t.task_id;
        const bool terminal =
            t.phase == LogisticsPhase::Complete || t.phase == LogisticsPhase::Failed;
        if ((t.phase == LogisticsPhase::Failed) != (t.failure != LogisticsFailure::None))
            return fail("Logistics failure phase is inconsistent.");
        if (terminal) {
            if (t.reservation_id || t.carry_stack_id)
                return fail("Terminal task retains physical ownership.");
        } else {
            if (t.phase == LogisticsPhase::Queued) {
                if (t.resident_id || t.path_batch_id)
                    return fail("Queued task already owns a resident or path.");
            } else if (!t.resident_id || !assigned.insert(t.resident_id).second)
                return fail("Resident has multiple logistics tasks.");
            if (t.phase == LogisticsPhase::Queued || t.phase == LogisticsPhase::ToPickup) {
                if (!reservations.contains(t.reservation_id) || t.carry_stack_id ||
                    !stacks.contains(t.source_stack_id))
                    return fail("Pickup task has no reserved source.");
                const auto &r = *reservations.at(t.reservation_id);
                const auto &s = *stacks.at(t.source_stack_id);
                if (r.stack_id != t.source_stack_id || r.task_id != t.task_id ||
                    r.quantity != t.quantity || s.item != t.item ||
                    (t.resident_id && residents.at(t.resident_id)->nation != s.owner_nation))
                    return fail("Pickup reservation does not match its task.");
            } else {
                if (t.reservation_id || !stacks.contains(t.carry_stack_id) ||
                    !assigned_cargo.insert(t.carry_stack_id).second)
                    return fail("Delivery task has no unique cargo.");
                const auto &s = *stacks.at(t.carry_stack_id);
                if (s.location.kind != ItemLocationKind::Carried ||
                    s.location.entity_id != t.resident_id || s.quantity != t.quantity ||
                    s.item != t.item)
                    return fail("Delivery task and cargo disagree.");
            }
            if (t.kind == LogisticsTaskKind::HaulToStorage) {
                if (!storages.contains(t.destination_building_id) ||
                    !buildings.contains(t.destination_building_id))
                    return fail("Haul destination is not a storage.");
                const auto nation = t.phase == LogisticsPhase::ToDropoff
                                        ? stacks.at(t.carry_stack_id)->owner_nation
                                        : stacks.at(t.source_stack_id)->owner_nation;
                if (buildings.at(t.destination_building_id)->nation != nation)
                    return fail("Haul destination ownership is invalid.");
            }
        }
        if (t.kind == LogisticsTaskKind::DeliverConstruction) {
            if (!orders.contains(t.destination_building_id))
                return fail("Delivery task has no construction order.");
            const auto &order = *orders.at(t.destination_building_id);
            if (!terminal) {
                if (order.phase != ConstructionPhase::Planned &&
                    order.phase != ConstructionPhase::Delivery)
                    return fail("Delivery task belongs to inactive construction.");
                const auto nation = t.phase == LogisticsPhase::ToDropoff
                                        ? stacks.at(t.carry_stack_id)->owner_nation
                                        : stacks.at(t.source_stack_id)->owner_nation;
                if (nation != order.owner_nation)
                    return fail("Construction task ownership is invalid.");
                auto [it, inserted] =
                    remaining_materials.try_emplace(order.building_id, Resources{0, 0, 0});
                amount(it->second, t.item) += static_cast<int>(t.quantity);
            }
        }
        if (t.path_batch_id) {
            if (!paths.contains(t.path_batch_id) ||
                !correlated_paths.insert(t.path_batch_id).second ||
                paths.at(t.path_batch_id)->command != PathCommandKind::Logistics ||
                paths.at(t.path_batch_id)->resident_ids != std::vector<int>{t.resident_id})
                return fail("Logistics path correlation is invalid.");
            const auto &p = *paths.at(t.path_batch_id);
            if (t.phase == LogisticsPhase::Complete &&
                (t.resume_job == Job::Idle || p.assigned_job != t.resume_job ||
                 p.target != t.resume_target))
                return fail("Gather continuation path is invalid.");
            if (!terminal && p.assigned_job != Job::Haul)
                return fail("Logistics path job is invalid.");
        }
    }
    for (const auto &[id, reservation] : reservations)
        if (!tasks.contains(reservation->task_id) ||
            tasks.at(reservation->task_id)->reservation_id != id)
            return fail("Reservation has no owning task.");
    for (const auto &[resident, cargo] : carried_by_resident)
        if (!assigned_cargo.contains(cargo))
            return fail("Carried stack has no task.");
    for (const auto &p : state.path_batches) {
        if (p.command == PathCommandKind::Logistics && !correlated_paths.contains(p.path_batch_id))
            return fail("Internal logistics path has no task.");
        if (p.command == PathCommandKind::Build &&
            (!orders.contains(p.building_id) ||
             orders.at(p.building_id)->phase != ConstructionPhase::Planned))
            return fail("Build path has no physical reservation order.");
    }
    for (const auto &r : state.residents)
        if (r.job == Job::Haul && !assigned.contains(r.id))
            return fail("Hauling resident has no active task.");
    for (const auto &[id, order] : orders) {
        if (order->phase != ConstructionPhase::Planned &&
            order->phase != ConstructionPhase::Delivery)
            continue;
        const auto physical =
            site_materials.contains(id) ? site_materials.at(id) : Resources{0, 0, 0};
        const auto remaining =
            remaining_materials.contains(id) ? remaining_materials.at(id) : Resources{0, 0, 0};
        if (physical != order->delivered ||
            remaining.wood + physical.wood != order->required.wood ||
            remaining.stone + physical.stone != order->required.stone ||
            remaining.food + physical.food != order->required.food)
            return fail("Construction material conservation is invalid.");
    }
    return true;
}

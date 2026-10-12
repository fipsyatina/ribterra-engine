// Private World::Impl methods. ItemStackState is the sole physical quantity authority.
static int &amount(Resources &r, ItemKind item) {
    return item == ItemKind::Wood ? r.wood : item == ItemKind::Stone ? r.stone : r.food;
}
static int amount(const Resources &r, ItemKind item) {
    return item == ItemKind::Wood ? r.wood : item == ItemKind::Stone ? r.stone : r.food;
}
ItemStackState *stack(std::uint64_t id) {
    auto it = std::lower_bound(item_stacks.begin(), item_stacks.end(), id,
                               [](const auto &value, auto key) { return value.stack_id < key; });
    return it != item_stacks.end() && it->stack_id == id ? &*it : nullptr;
}
const ItemStackState *stack(std::uint64_t id) const {
    return const_cast<Impl *>(this)->stack(id);
}
Building *building(int id) {
    auto it = std::find_if(buildings.begin(), buildings.end(), [=](const auto &b) { return b.id == id; });
    return it == buildings.end() ? nullptr : &*it;
}
ConstructionOrderState *construction(int id) {
    auto it = std::find_if(construction_orders.begin(), construction_orders.end(),
                           [=](const auto &value) { return value.building_id == id; });
    return it == construction_orders.end() ? nullptr : &*it;
}
LogisticsTaskState *task(std::uint64_t id) {
    auto it = std::lower_bound(logistics_tasks.begin(), logistics_tasks.end(), id,
                               [](const auto &value, auto key) { return value.task_id < key; });
    return it != logistics_tasks.end() && it->task_id == id ? &*it : nullptr;
}
std::uint32_t reserved(std::uint64_t id) const {
    std::uint32_t result = 0;
    for (const auto &r : item_reservations)
        if (r.stack_id == id)
            result += r.quantity;
    return result;
}
InventorySummary inventory(NationId nation) const {
    InventorySummary result;
    std::unordered_map<std::uint64_t, std::uint32_t> reservations;
    for (const auto &r : item_reservations)
        reservations[r.stack_id] += r.quantity;
    for (const auto &s : item_stacks) {
        if (s.owner_nation != nation || static_cast<unsigned>(s.item)>=3)
            continue;
        amount(result.total, s.item) += static_cast<int>(s.quantity);
        const bool committed =
            s.location.kind == ItemLocationKind::Carried || s.location.kind == ItemLocationKind::Construction ||
            production_material(s);
        const auto held = committed ? s.quantity : reservations[s.stack_id];
        amount(result.reserved, s.item) += static_cast<int>(held);
        amount(result.available, s.item) += static_cast<int>(s.quantity - held);
        if (s.location.kind == ItemLocationKind::Storage)
            amount(result.stored, s.item) += static_cast<int>(s.quantity);
        if (s.location.kind == ItemLocationKind::Carried)
            amount(result.carried, s.item) += static_cast<int>(s.quantity);
    }
    return result;
}
CellCoord resident_cell(int id) const {
    const auto e = find_resident(id);
    const auto &p = registry.get<Position>(e);
    return {static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)), p.layer};
}
std::optional<CellCoord> location_cell(const ItemLocation &location) {
    if (location.kind == ItemLocationKind::Ground)
        return location.cell;
    if (location.kind == ItemLocationKind::Carried) {
        if (find_resident(location.entity_id) == entt::null)
            return {};
        return resident_cell(location.entity_id);
    }
    if (auto *b = building(location.entity_id))
        return CellCoord{b->x, b->y, b->layer};
    return {};
}
std::uint64_t add_stack(ItemKind item, std::uint32_t quantity, NationId nation, ItemLocation location,
                        bool transferring = false) {
    if (!quantity)
        return 0;
    if (!transferring) {
        std::uint64_t total = quantity;
        for (const auto &s : item_stacks)
            if (s.item == item && s.owner_nation == nation)
                total += s.quantity;
        if (total > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return 0;
    }
    if (location.kind == ItemLocationKind::Ground || location.kind == ItemLocationKind::Storage)
        for (auto &s : item_stacks)
            if (s.item == item && s.owner_nation == nation && s.location == location &&
                s.quantity <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) - quantity) {
                s.quantity += quantity;
                return s.stack_id;
            }
    if (item_stacks.size() >= max_item_stacks ||
        next_item_stack_id == std::numeric_limits<std::uint64_t>::max())
        return 0;
    const auto id = next_item_stack_id++;
    item_stacks.push_back({id, item, quantity, nation, location});
    return id;
}
void erase_empty_stacks() {
    std::erase_if(item_stacks, [](const auto &s) { return s.quantity == 0; });
}
void release_reservation(std::uint64_t id) {
    std::erase_if(item_reservations, [=](const auto &r) { return r.reservation_id == id; });
}
bool resident_busy(int id) const {
    return active_task_by_resident.contains(id);
}
std::uint64_t storage_used(int id) const {
    std::uint64_t used = 0;
    for (const auto &s : item_stacks)
        if (s.location.kind == ItemLocationKind::Storage && s.location.entity_id == id)
            used += s.quantity;
    return used;
}
int choose_storage(CellCoord from, NationId nation, ItemKind item, std::uint32_t quantity) {
    std::optional<std::tuple<int, int, int>> best;
    int selected = 0;
    for (const auto &s : storages) {
        auto *b = building(s.building_id);
        if (!b || (b->kind!=BuildingKind::Camp && b->kind!=BuildingKind::Stockpile) || b->progress < 1 || b->nation != nation ||
            !storage_accepts(s.accepted_items,item) ||
            storage_used(b->id) + quantity > s.capacity)
            continue;
        int distance = std::abs(from.x - b->x) + std::abs(from.y - b->y) + (from.layer != b->layer ? 64 : 0);
        auto key = std::tuple{distance, -s.priority, b->id};
        if (!best || key < *best) {
            best = key;
            selected = b->id;
        }
    }
    return selected;
}
void stop_resident(LogisticsTaskState &t) {
    if (auto e = find_resident(t.resident_id); e != entt::null) {
        registry.get<Activity>(e) = {};
        registry.get<Route>(e) = {};
        auto &order = registry.get<OrderStatus>(e);
        if (order.phase != OrderPhase::PathPending)
            order = {};
    }
    active_task_by_resident.erase(t.resident_id);
    cache_dirty = true;
}
void fail_task(LogisticsTaskState &t, LogisticsFailure failure) {
    if (t.phase == LogisticsPhase::Complete || t.phase == LogisticsPhase::Failed)
        return;
    release_reservation(t.reservation_id);
    t.reservation_id = 0;
    if (auto *cargo = stack(t.carry_stack_id)) {
        cargo->location = {ItemLocationKind::Ground, resident_cell(t.resident_id), 0};
    }
    t.carry_stack_id = 0;
    t.phase = LogisticsPhase::Failed;
    t.failure = failure;
    // A pending planner leg must retire before the resident can receive new commands.
    stop_resident(t);
    finish_manual_actor(t.resident_id, ManualOrderCompletion::None,
                        failure == LogisticsFailure::ResidentUnavailable ? PathFailure::ResidentUnavailable
                        : failure == LogisticsFailure::Timeout           ? PathFailure::Timeout
                        : failure == LogisticsFailure::LimitExceeded     ? PathFailure::LimitExceeded
                        : failure == LogisticsFailure::Unreachable       ? PathFailure::Unreachable
                                                                         : PathFailure::TargetInvalid);
    retire_manual_override(t.resident_id);
}
void cancel_order(int id, LogisticsFailure failure) {
    auto *order = construction(id);
    if (!order || order->phase == ConstructionPhase::Complete || order->phase == ConstructionPhase::Cancelled)
        return;
    CellCoord refund_site=order->site;
    if (order->kind==BuildingKind::Bridge)
        bridge_connected(order->site.x,order->site.y,[&](int x,int y){
            if (!walkable({x,y,order->site.layer})) return false;
            refund_site={x,y,order->site.layer}; return true;
        });
    const bool consumed = order->phase == ConstructionPhase::Work;
    for (const auto e : registry.view<Activity, Route>())
        if (registry.get<Activity>(e).job == Job::Build && registry.get<Route>(e).work == order->site)
            clear_outdoor_work_accumulator(registry.get<Identity>(e).id);
    cancel_work_for_building(id);
    order->phase = ConstructionPhase::Cancelled;
    for (auto &t : logistics_tasks)
        if (t.kind == LogisticsTaskKind::DeliverConstruction && t.destination_building_id == id)
            fail_task(t, failure);
    for (auto &s : item_stacks)
        if (s.location.kind == ItemLocationKind::Construction && s.location.entity_id == id)
            s.location = {ItemLocationKind::Ground, refund_site, 0};
    if (consumed)
        for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food})
            if (amount(order->delivered, item) > 0)
                add_stack(item, static_cast<std::uint32_t>(amount(order->delivered, item)),
                          order->owner_nation, {ItemLocationKind::Ground, refund_site, 0});
    if (building(id)) {
        std::erase_if(buildings, [=](const auto &b) { return b.id == id; });
        ++topology_revision;
    }
    event("Construction cancelled; physical materials remain at their locations.");
}
bool reserve_construction(ConstructionOrderState order) {
    struct Allocation {
        std::uint64_t stack_id;
        ItemKind item;
        std::uint32_t quantity;
    };
    std::vector<Allocation> allocation;
    for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food}) {
        int remaining = amount(order.required, item);
        std::vector<std::pair<std::tuple<int, int, std::uint64_t>, const ItemStackState *>> sources;
        for (const auto &s : item_stacks) {
            if (s.item != item || s.owner_nation != order.owner_nation ||
                (s.location.kind != ItemLocationKind::Ground && s.location.kind != ItemLocationKind::Storage))
                continue;
            if(production_material(s))continue;
            auto cell = location_cell(s.location);
            if (!cell)
                continue;
            int priority = 0;
            for (const auto &storage : storages)
                if (storage.building_id == s.location.entity_id)
                    priority = storage.priority;
            const int distance = std::abs(cell->x - order.site.x) + std::abs(cell->y - order.site.y) +
                                 (cell->layer != order.site.layer ? 64 : 0);
            sources.push_back({{distance, -priority, s.stack_id}, &s});
        }
        std::sort(sources.begin(), sources.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[key, s] : sources) {
            auto available = s->quantity - reserved(s->stack_id);
            while (remaining > 0 && available > 0) {
                auto q =
                    std::min({static_cast<std::uint32_t>(remaining), available, resident_carry_capacity});
                allocation.push_back({s->stack_id, item, q});
                remaining -= static_cast<int>(q);
                available -= q;
            }
        }
        if (remaining)
            return false;
    }
    if (logistics_tasks.size() + allocation.size() > max_logistics_tasks ||
        item_reservations.size() + allocation.size() > max_item_reservations ||
        next_logistics_task_id >= std::numeric_limits<std::uint64_t>::max() - allocation.size() ||
        next_item_reservation_id >= std::numeric_limits<std::uint64_t>::max() - allocation.size() ||
        ticks > std::numeric_limits<std::uint64_t>::max() - 10000)
        return false;
    construction_orders.push_back(order);
    for (const auto &a : allocation) {
        LogisticsTaskState t;
        t.task_id = next_logistics_task_id++;
        t.kind = LogisticsTaskKind::DeliverConstruction;
        t.destination_building_id = order.building_id;
        t.source_stack_id = a.stack_id;
        t.reservation_id = next_item_reservation_id++;
        t.item = a.item;
        t.quantity = a.quantity;
        t.deadline_tick = ticks + 10000;
        item_reservations.push_back(
            {t.reservation_id, a.stack_id, t.task_id, a.quantity, order.owner_nation});
        logistics_tasks.push_back(t);
    }
    return true;
}
bool queue_task_leg(LogisticsTaskState &t, CellCoord target, bool to_adjacent) {
    auto result = queue_path(PathCommandKind::Logistics, {t.resident_id}, target, to_adjacent, Job::Haul);
    if (!result.accepted) {
        // Queue pressure is transient and consumes the existing task deadline.
        if (result.failure != PathFailure::LimitExceeded &&
            result.failure != PathFailure::ReservationConflict)
            fail_task(t, LogisticsFailure::TargetInvalid);
        return false;
    }
    t.path_batch_id = result.path_batch_id;
    return true;
}
void logistics_path_result(std::uint64_t batch_id, bool accepted, PathFailure failure) {
    for (auto &t : logistics_tasks) {
        if (t.path_batch_id != batch_id)
            continue;
        t.path_batch_id = 0;
        if (t.phase == LogisticsPhase::Failed || t.phase == LogisticsPhase::Complete) {
            if (!accepted && t.phase == LogisticsPhase::Complete) {
                t.phase = LogisticsPhase::Failed;
                t.failure = failure == PathFailure::Timeout ? LogisticsFailure::Timeout
                            : failure == PathFailure::ResidentUnavailable
                                ? LogisticsFailure::ResidentUnavailable
                            : failure == PathFailure::LimitExceeded ? LogisticsFailure::LimitExceeded
                            : failure == PathFailure::TargetInvalid ? LogisticsFailure::TargetInvalid
                                                                    : LogisticsFailure::Unreachable;
                t.resume_job = Job::Idle;
            }
            if (!accepted || t.phase == LogisticsPhase::Failed)
                stop_resident(t);
            if (!accepted || t.phase == LogisticsPhase::Failed)
                retire_manual_override(t.resident_id);
            return;
        }
        if (!accepted) {
            fail_task(t, failure == PathFailure::ResidentUnavailable ? LogisticsFailure::ResidentUnavailable
                         : failure == PathFailure::Timeout           ? LogisticsFailure::Timeout
                                                                     : LogisticsFailure::Unreachable);
        }
        return;
    }
}
bool at_location(int id, CellCoord target, bool to_adjacent) const {
    const auto e = find_resident(id);
    const auto &p = registry.get<Position>(e);
    const auto &r = registry.get<Route>(e);
    if (r.cursor != r.points.size() || p.x != std::round(p.x) || p.y != std::round(p.y))
        return false;
    auto cell = resident_cell(id);
    return to_adjacent ? adjacent(cell, target) : cell == target;
}
void complete_task(LogisticsTaskState &t) {
    t.phase = LogisticsPhase::Complete;
    t.carry_stack_id = 0;
    stop_resident(t);
    if (delivery_boundary(t.resident_id, t.resume_job, t.resume_target)) {
        if (!create_work_intent(t.resident_id, WorkIntentKind::GatherContinuation, t.resume_target,
                                t.resume_job)) {
            registry.get<OrderStatus>(find_resident(t.resident_id)) = {OrderPhase::PathFailed,
                                                                       PathFailure::LimitExceeded, 0};
            retire_manual_override(t.resident_id);
        }
    }
    t.resume_job = Job::Idle;
}
void process_logistics() {
    // Bounded scan and at most128 transitions per tick, independent of the path work budget.
    std::size_t transitions = 0;
    std::map<NationId, std::vector<int>> available_residents;
    std::map<NationId, std::size_t> available_cursor;
    for (auto e : entities()) {
        const auto &identity = registry.get<Identity>(e);
        const auto job = registry.get<Activity>(e).job;
        bool delivery_waiter = false;
        if (job == Job::Build) {
            const auto &route = registry.get<Route>(e);
            for (const auto &order : construction_orders)
                if (route.work && order.site == *route.work && order.phase == ConstructionPhase::Delivery) {
                    delivery_waiter = true;
                    break;
                }
        }
        if (legacy_auto_worker(e, Job::Haul) && !resident_busy(identity.id) &&
            !active_work_by_resident.contains(identity.id) &&
            registry.get<OrderStatus>(e).phase != OrderPhase::PathPending &&
            (job == Job::Idle || delivery_waiter))
            available_residents[identity.nation].push_back(identity.id);
    }
    for (auto &t : logistics_tasks) {
        if (t.phase == LogisticsPhase::Complete || t.phase == LogisticsPhase::Failed)
            continue;
        auto *site = t.kind == LogisticsTaskKind::DeliverConstruction
                         ? construction(t.destination_building_id)
                         : nullptr;
        if (site && site->phase == ConstructionPhase::Planned)
            continue;
        if (transitions >= 128)
            break;
        const auto actor = t.resident_id ? find_resident(t.resident_id) : entt::null;
        const bool paused = t.phase != LogisticsPhase::ToDropoff &&
            (actor != entt::null ? !work_allowed(actor,Job::Haul) : (is_night(calendar_tick()) && !settlement.night_shift(calendar_tick())));
        if (paused) { ++t.deadline_tick; continue; }
        if (ticks >= t.deadline_tick) {
            fail_task(t, LogisticsFailure::Timeout);
            ++transitions;
            continue;
        }
        if (t.resident_id) {
            auto e = find_resident(t.resident_id);
            if (e == entt::null || registry.get<Vital>(e).health <= 0) {
                fail_task(t, LogisticsFailure::ResidentUnavailable);
                ++transitions;
                continue;
            }
        }
        if (t.path_batch_id)
            continue;
        if (t.phase == LogisticsPhase::Queued) {
            const auto *source = stack(t.source_stack_id);
            if (!source) {
                fail_task(t, LogisticsFailure::TargetInvalid);
                ++transitions;
                continue;
            }
            auto &available = available_residents[source->owner_nation];
            auto &cursor = available_cursor[source->owner_nation];
            if (cursor < available.size()) {
                t.resident_id = available[cursor++];
                supersede_work_for_residents({t.resident_id});
                active_task_by_resident[t.resident_id] = t.task_id;
            }
            if (!t.resident_id)
                continue;
            registry.get<Route>(find_resident(t.resident_id)) = {};
            registry.get<Activity>(find_resident(t.resident_id)) = {Job::Haul, 0};
            t.phase = LogisticsPhase::ToPickup;
            ++transitions;
            if (transitions >= 128)
                continue;
        }
        if (t.phase == LogisticsPhase::ToPickup) {
            auto *source = stack(t.source_stack_id);
            auto target = source ? location_cell(source->location) : std::optional<CellCoord>{};
            if (!source || !target) {
                fail_task(t, LogisticsFailure::TargetInvalid);
                ++transitions;
                continue;
            }
            const bool adjacent_target =
                source->location.kind != ItemLocationKind::Ground || !walkable(*target);
            if (!at_location(t.resident_id, *target, adjacent_target)) {
                if (registry.get<Route>(find_resident(t.resident_id)).cursor ==
                    registry.get<Route>(find_resident(t.resident_id)).points.size()) {
                    queue_task_leg(t, *target, adjacent_target);
                    ++transitions;
                }
                continue;
            }
            if (source->quantity < t.quantity || item_stacks.size() >= max_item_stacks) {
                fail_task(t, LogisticsFailure::LimitExceeded);
                ++transitions;
                continue;
            }
            const auto nation = source->owner_nation;
            const auto new_id =
                add_stack(t.item, t.quantity, nation, {ItemLocationKind::Carried, {}, t.resident_id}, true);
            if (!new_id) {
                fail_task(t, LogisticsFailure::LimitExceeded);
                ++transitions;
                continue;
            }
            stack(t.source_stack_id)->quantity -= t.quantity;
            release_reservation(t.reservation_id);
            t.reservation_id = 0;
            t.carry_stack_id = new_id;
            t.phase = LogisticsPhase::ToDropoff;
            erase_empty_stacks();
            registry.get<Route>(find_resident(t.resident_id)) = {};
            ++transitions;
            if (transitions >= 128)
                continue;
        }
        if (t.phase != LogisticsPhase::ToDropoff)
            continue;
        auto *destination = building(t.destination_building_id);
        if (!destination) {
            fail_task(t, LogisticsFailure::TargetInvalid);
            ++transitions;
            continue;
        }
        const CellCoord target{destination->x, destination->y, destination->layer};
        if (!at_location(t.resident_id, target, true)) {
            if (registry.get<Route>(find_resident(t.resident_id)).cursor ==
                registry.get<Route>(find_resident(t.resident_id)).points.size()) {
                queue_task_leg(t, target, true);
                ++transitions;
            }
            continue;
        }
        auto *cargo = stack(t.carry_stack_id);
        if (!cargo) {
            fail_task(t, LogisticsFailure::TargetInvalid);
            ++transitions;
            continue;
        }
        if (t.kind == LogisticsTaskKind::HaulToStorage) {
            auto storage = std::find_if(storages.begin(), storages.end(),
                                        [&](const auto &s) { return s.building_id == destination->id; });
            if (storage != storages.end() &&
                !storage_accepts(storage->accepted_items,cargo->item)) {
                fail_task(t, LogisticsFailure::TargetInvalid);
                ++transitions;
                continue;
            }
            if (storage == storages.end() ||
                storage_used(destination->id) + cargo->quantity > storage->capacity) {
                fail_task(t, LogisticsFailure::StorageFull);
                ++transitions;
                continue;
            }
            cargo->location = {ItemLocationKind::Storage, {}, destination->id};
            for (auto &stored : item_stacks)
                if (stored.stack_id != cargo->stack_id && stored.item == cargo->item &&
                    stored.owner_nation == cargo->owner_nation && stored.location == cargo->location &&
                    stored.quantity <=
                        static_cast<std::uint32_t>(std::numeric_limits<int>::max()) - cargo->quantity) {
                    stored.quantity += cargo->quantity;
                    cargo->quantity = 0;
                    break;
                }
        } else {
            cargo->location = {ItemLocationKind::Construction, {}, destination->id};
            amount(site->delivered, t.item) += static_cast<int>(cargo->quantity);
            if (site->delivered == site->required) {
                std::erase_if(item_stacks, [&](const auto &s) {
                    return s.location.kind == ItemLocationKind::Construction &&
                           s.location.entity_id == destination->id;
                });
                site->phase = ConstructionPhase::Work;
            }
        }
        complete_task(t);
        if (site && site->phase == ConstructionPhase::Work &&
            eligible_for_auto_work(find_resident(t.resident_id), Job::Build, target)) {
            auto e = find_resident(t.resident_id);
            registry.get<Activity>(e) = {Job::Build, 0};
            registry.get<Route>(e) = {{}, 0, target};
            registry.get<OrderStatus>(e) = {OrderPhase::Active, PathFailure::None, 0};
        }
        ++transitions;
    }
    // A failed delivery cancels the whole order atomically, freeing every remaining reservation.
    std::map<int, LogisticsFailure> failed_orders;
    for (const auto &t : logistics_tasks)
        if (t.kind == LogisticsTaskKind::DeliverConstruction && t.phase == LogisticsPhase::Failed)
            failed_orders.try_emplace(t.destination_building_id, t.failure);
    for (const auto &[id, failure] : failed_orders) {
        auto *order = construction(id);
        if (order && order->phase == ConstructionPhase::Delivery)
            cancel_order(id, failure);
    }
    // Keep a bounded visible terminal history; never remove records still owning a planner leg.
    std::size_t terminal_count = 0;
    for (const auto &t : logistics_tasks)
        if ((t.phase == LogisticsPhase::Complete || t.phase == LogisticsPhase::Failed) && !t.path_batch_id)
            ++terminal_count;
    std::erase_if(logistics_tasks, [&](const auto &t) {
        if (terminal_count <= 256 || t.path_batch_id ||
            (t.phase != LogisticsPhase::Complete && t.phase != LogisticsPhase::Failed))
            return false;
        --terminal_count;
        return true;
    });
    // Free ground output receives an explicitly reserved haul; competing builders cannot take it
    // twice.
    std::unordered_map<std::uint64_t, std::uint32_t> reserved_by_stack;
    for (const auto &r : item_reservations)
        reserved_by_stack[r.stack_id] += r.quantity;
    for (const auto &s : item_stacks) {
        if (transitions >= 128 || logistics_tasks.size() >= max_logistics_tasks ||
            item_reservations.size() >= max_item_reservations)
            break;
        if (s.location.kind != ItemLocationKind::Ground)
            continue;
        auto available = s.quantity - reserved_by_stack[s.stack_id];
        if (!available || ticks > std::numeric_limits<std::uint64_t>::max() - 10000 ||
            next_logistics_task_id == std::numeric_limits<std::uint64_t>::max() ||
            next_item_reservation_id == std::numeric_limits<std::uint64_t>::max())
            continue;
        auto quantity = std::min(available, resident_carry_capacity);
        int destination = choose_storage(s.location.cell, s.owner_nation, s.item, quantity);
        if (!destination)
            continue;
        LogisticsTaskState t;
        t.task_id = next_logistics_task_id++;
        t.destination_building_id = destination;
        t.source_stack_id = s.stack_id;
        t.reservation_id = next_item_reservation_id++;
        t.item = s.item;
        t.quantity = quantity;
        t.deadline_tick = ticks + 10000;
        item_reservations.push_back({t.reservation_id, s.stack_id, t.task_id, quantity, s.owner_nation});
        reserved_by_stack[s.stack_id] += quantity;
        logistics_tasks.push_back(t);
        ++transitions;
    }
}
bool gather_yield(entt::entity e, ItemKind item, std::uint32_t quantity, Job resume_job,
                  CellCoord resume_target) {
    if (item_stacks.size() >= max_item_stacks || logistics_tasks.size() >= max_logistics_tasks ||
        next_logistics_task_id == std::numeric_limits<std::uint64_t>::max() ||
        ticks > std::numeric_limits<std::uint64_t>::max() - 10000)
        return false;
    const auto &identity = registry.get<Identity>(e);
    const auto cell = resident_cell(identity.id);
    if (!inside_world(cell.x, cell.y))
        return false;
    const int destination = choose_storage(cell, identity.nation, item, quantity);
    if (!destination) {
        // Production still has a physical output even when no storage can accept it.
        const auto ground = add_stack(item, quantity, identity.nation, {ItemLocationKind::Ground, cell, 0});
        if (!ground)
            return false;
        LogisticsTaskState failure;
        failure.task_id = next_logistics_task_id++;
        failure.phase = LogisticsPhase::Failed;
        failure.failure = LogisticsFailure::StorageFull;
        failure.resident_id = identity.id;
        failure.source_stack_id = ground;
        failure.item = item;
        failure.quantity = quantity;
        failure.deadline_tick = ticks + 10000;
        logistics_tasks.push_back(failure);
        return true;
    }
    const auto cargo_id =
        add_stack(item, quantity, identity.nation, {ItemLocationKind::Carried, {}, identity.id});
    if (!cargo_id)
        return false;
    supersede_work_for_residents({identity.id});
    LogisticsTaskState t;
    t.task_id = next_logistics_task_id++;
    t.phase = LogisticsPhase::ToDropoff;
    t.resident_id = identity.id;
    t.destination_building_id = destination;
    t.carry_stack_id = cargo_id;
    t.item = item;
    t.quantity = quantity;
    t.deadline_tick = ticks + 10000;
    t.resume_job = resume_job;
    t.resume_target = resume_target;
    active_task_by_resident[identity.id] = t.task_id;
    logistics_tasks.push_back(t);
    registry.get<Activity>(e) = {Job::Haul, 0};
    registry.get<Route>(e) = {};
    return true;
}
bool consume_food(NationId nation) {
    for (auto &s : item_stacks)
        if (s.owner_nation == nation && s.item == ItemKind::Food &&
            s.location.kind == ItemLocationKind::Storage && s.quantity > reserved(s.stack_id)) {
            --s.quantity;
            return true;
        }
    return false;
}

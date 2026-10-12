#include "ribterra/seasons.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/biomes.hpp"
#include "ribterra/world.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <entt/entity/registry.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ribterra {
namespace {
struct Identity {
    int id{};
    std::string name;
    NationId nation{1};
};
struct Position {
    float x{}, y{};
    CellCoord target;
    WorldLayer layer{WorldLayer::Surface};
};
struct Activity {
    Job job{Job::Idle};
    float progress{};
    std::optional<OutdoorWorkRemainderState> outdoor_work;
};
struct Vital {
    int health{100};
    float hunger{};
};
struct Skills {
    int wood{}, mining{}, farming{};
    std::array<int, skill_count - 3> additional{};
    std::array<SkillPractice, skill_count> practice{};
    std::array<std::int16_t, skill_count> abilities{};
};
struct Route {
    std::vector<CellCoord> points;
    std::size_t cursor{};
    std::optional<CellCoord> work;
};
struct OrderStatus {
    OrderPhase phase{OrderPhase::Idle};
    PathFailure failure{PathFailure::None};
    std::uint64_t pending_batch{};
};
std::uint32_t mix(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}
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
bool adjacent(CellCoord a, CellCoord b) {
    return a.layer == b.layer && std::abs(a.x - b.x) + std::abs(a.y - b.y) == 1;
}
} // namespace
struct World::Impl {
    entt::registry registry;
    std::string server_metadata;
#include "world_settlement.inc"
#include "world_economy.inc"
#include "world_wilderness.inc"
#include "world_nations.inc"
    std::unordered_map<int, entt::entity> resident_by_id;
    std::vector<Chunk> chunks;
    struct TileHash { std::uint64_t revision{}, value{}; bool valid{}; };
    mutable std::vector<TileHash> tile_hashes;
    std::vector<Portal> portals; // New shafts are opened by completed Mine buildings.
    std::vector<Building> buildings;
    mutable Resources resources;
    std::uint64_t next_item_stack_id{1}, next_item_reservation_id{1}, next_logistics_task_id{1};
    std::vector<ItemStackState> item_stacks;
    std::vector<StorageState> storages;
    std::vector<ItemReservationState> item_reservations;
    std::vector<LogisticsTaskState> logistics_tasks;
    std::vector<ConstructionOrderState> construction_orders;
    std::unordered_map<int, std::uint64_t> active_task_by_resident;
    std::uint64_t next_work_intent_id{1}, work_intent_cursor_id{1};
    int work_building_cursor_id{1};
    std::vector<WorkIntentState> work_intents;
    std::unordered_map<int, std::uint64_t> active_work_by_resident;
    std::unordered_map<int, std::uint64_t> active_work_by_building;
    WorkIntentDiagnostics work_diagnostics;
    WorkIntentOutcomes work_outcomes;
    int profession_cursor_id{1};
    ProfessionDiagnostics profession_diagnostics;
    std::uint64_t next_manual_order_id{1}, manual_order_cursor_id{1};
    std::vector<ManualOrderState> manual_orders;
    std::map<int, std::uint64_t> manual_heads;
    std::map<int, std::vector<std::uint64_t>> manual_queues;
    ManualOrderDiagnostics manual_diagnostics;
    bool manual_mutation{};
    struct ProfessionCandidate {
        Job job;
        CellCoord cell, worksite;
        std::uint64_t id{};
    };
    struct ProfessionChunkIndex {
        ChunkKey key;
        std::uint64_t revision{};
        bool valid{};
        std::array<std::vector<ProfessionCandidate>, 3> resources;
    };
    std::vector<ProfessionChunkIndex> profession_resource_index;
    std::vector<std::string> events;
    std::uint32_t seed{};
    std::uint32_t terrain_generator{generator_version};
    std::array<std::uint8_t, 16> world_id{};
    std::uint64_t ticks{};
    ClimateRuntimeState climate;
    std::optional<WeatherScheduleCache> weather_cache;
    static constexpr std::uint64_t normal_food_interval_ticks = 200;
    static constexpr std::uint64_t reduced_food_interval_ticks = 400;
    static constexpr std::uint64_t winter_food_buffer_ticks = 20u * 60u * 20u;
    std::uint32_t initial_food(int consumers) const {
        if (climate.scenario != ClimateScenarioId::FirstWinterStorm) return 100;
        const auto checks = (winter_food_buffer_ticks + normal_food_interval_ticks - 1u) / normal_food_interval_ticks;
        return static_cast<std::uint32_t>(std::max<std::uint64_t>(100u,static_cast<std::uint64_t>(consumers) * checks));
    }
    std::uint32_t initial_camp_capacity(std::uint32_t food) const {
        // Approved scenario-only provisioning rule. The existing population
        // limits bound this to180120 (constructor) /12120 (one new nation).
        return climate.scenario == ClimateScenarioId::FirstWinterStorm ? std::max(10000u,80u + 40u + food) : 10000u;
    }
    int next_building_id{1};
    bool moved{}, gathered{}, built{};
    std::uint64_t next_path_batch_id{1};
    std::uint64_t topology_revision{};
    std::vector<PathBatchState> path_batches;
    std::vector<PathBatchOutcome> path_results;
    PathPlannerDiagnostics path_diagnostics;
    std::unordered_map<int, std::size_t> active_node_lookup;
    std::unordered_set<int> active_all_starts;
    std::unordered_set<int> active_remaining_starts;
    std::uint64_t lookup_batch_id{};
    std::size_t lookup_node_count{};
    std::size_t planner_memory_bytes{};
    std::size_t planner_staged_cells{};
    Resources path_reserved_resources{0, 0, 0};
    mutable std::vector<Resident> resident_cache;
    mutable bool cache_dirty{true};
    Chunk *find_chunk(CellCoord c) {
        auto k = chunk_key(c);
        auto i = std::lower_bound(chunks.begin(), chunks.end(), k,
                                  [](const Chunk &a, const ChunkKey &b) { return a.key < b; });
        return i != chunks.end() && i->key == k ? &*i : nullptr;
    }
    const Chunk *find_chunk(CellCoord c) const { return const_cast<Impl *>(this)->find_chunk(c); }
    Tile *mutable_tile(CellCoord c) {
        auto *q = find_chunk(c);
        return q ? &q->tiles[local_index(c.x, c.y)] : nullptr;
    }
    const Tile &tile(CellCoord c) const {
        static const Tile outside{Terrain::Water, 0, false};
        auto *q = find_chunk(c);
        return q ? q->tiles[local_index(c.x, c.y)] : outside;
    }
    void changed(CellCoord c) {
        if (auto *q = find_chunk(c))
            ++q->revision;
    }
    void event(std::string s) {
        events.push_back(std::move(s));
        if (events.size() > 64)
            events.erase(events.begin());
    }
    std::vector<entt::entity> entities() const {
        std::vector<std::pair<int, entt::entity>> ordered;
        auto view = registry.view<const Identity>();
        for (auto e : view)
            ordered.push_back({view.get<const Identity>(e).id, e});
        std::sort(ordered.begin(), ordered.end());
        std::vector<entt::entity> out;
        for (auto [id, e] : ordered)
            out.push_back(e);
        return out;
    }
    entt::entity find_resident(int id) const {
        const auto found = resident_by_id.find(id);
        return found == resident_by_id.end() ? entt::null : found->second;
    }
    void rebuild_cache() const {
        if (!cache_dirty)
            return;
        resident_cache.clear();
        for (auto e : entities()) {
            auto &i = registry.get<const Identity>(e);
            auto &p = registry.get<const Position>(e);
            auto &a = registry.get<const Activity>(e);
            auto &v = registry.get<const Vital>(e);
            auto &s = registry.get<const Skills>(e);
            auto &o = registry.get<const OrderStatus>(e);
            resident_cache.push_back({i.id,         i.name,
                                      p.x,          p.y,
                                      p.target.x,   p.target.y,
                                      a.job,        a.progress,
                                      v.health,     v.hunger,
                                      s.wood,       s.mining,
                                      s.farming,    p.layer,
                                      i.nation,     o.phase,
                                      o.failure,    o.pending_batch,
                                      s.additional, s.practice,
                                      s.abilities,  registry.get<const ProfessionState>(e)});
        }
        cache_dirty = false;
    }
    bool walkable(CellCoord c) const {
        if (!inside_world(c.x, c.y) || !valid_layer(c.layer))
            return false;
        const auto building = std::find_if(buildings.begin(), buildings.end(), [&](const Building &b) {
            return b.x == c.x && b.y == c.y && b.layer == c.layer;
        });
        if (building != buildings.end()) return building_passage(building->kind, building->progress);
        return tile(c).terrain != Terrain::Water && tile(c).terrain != Terrain::MineOre;
    }
    CalendarTick calendar_tick() const {
        const auto value = make_calendar_tick(ticks, climate.calendar_offset_ticks);
        if (!value) throw std::overflow_error("World calendar addition overflow");
        return *value;
    }
    void clear_outdoor_work_accumulator(int id) noexcept {
        if (const auto e = find_resident(id); e != entt::null)
            registry.get<Activity>(e).outdoor_work.reset();
    }
    static bool same_outdoor_key(const OutdoorWorkRemainderState& a, const OutdoorWorkRemainderState& b) {
        return a.resident_id == b.resident_id && a.job == b.job && a.target == b.target &&
               a.building_id == b.building_id;
    }
    std::optional<OutdoorWorkRemainderState> outdoor_work_key(entt::entity e) const {
        if (e == entt::null || registry.get<const Vital>(e).health <= 0) return {};
        const auto& activity = registry.get<const Activity>(e);
        const auto& route = registry.get<const Route>(e);
        if (!route.work || route.work->layer != WorldLayer::Surface) return {};
        const auto& identity = registry.get<const Identity>(e);
        int building_id{};
        if (activity.job == Job::Build) {
            const auto site = std::find_if(buildings.begin(), buildings.end(), [&](const Building& b) {
                return CellCoord{b.x,b.y,b.layer} == *route.work && b.nation == identity.nation && b.progress < 1;
            });
            if (site == buildings.end()) return {};
            building_id = site->id;
        } else if (activity.job == Job::Woodcut || activity.job == Job::Mine || activity.job == Job::Forage) {
            const auto& work = tile(*route.work);
            if (work.amount <= 0 || !work_terrain_matches(activity.job, work.terrain)) return {};
        } else return {};
        return OutdoorWorkRemainderState{identity.id,activity.job,*route.work,building_id,0};
    }
    bool outdoor_progress_step(entt::entity e) {
        // Only a Surface actor accumulates weather work. Underground activity
        // keeps the existing unmodified simulation behavior and frozen key.
        if (registry.get<Position>(e).layer != WorldLayer::Surface) return true;
        const auto key = outdoor_work_key(e);
        auto& activity = registry.get<Activity>(e);
        if (!key) { activity.outdoor_work.reset(); return true; }
        if (!activity.outdoor_work || !same_outdoor_key(*activity.outdoor_work,*key))
            activity.outdoor_work = *key;
        return outdoor_work_step(activity.outdoor_work->remainder_bp,climate.current.outdoor_work_rate_bp);
    }
    void clear_stale_outdoor_work() {
        for (const auto e : registry.view<Activity>()) {
            auto& activity = registry.get<Activity>(e);
            if (!activity.outdoor_work) continue;
            const auto key = outdoor_work_key(e);
            if (!key || !same_outdoor_key(*activity.outdoor_work,*key)) activity.outdoor_work.reset();
        }
    }
    bool passage(CellCoord c) const {
        const auto building = std::find_if(buildings.begin(), buildings.end(), [&](const Building &b) {
            return b.x == c.x && b.y == c.y && b.layer == c.layer;
        });
        return building != buildings.end() && building_passage(building->kind, building->progress);
    }
    bool can_traverse(CellCoord from, CellCoord to) const {
        if (!adjacent(from, to)) return portal_other(from) && *portal_other(from) == to;
        if (!walkable(to)) return false;
        if (terrain_generator < surface_rules_generator_version || from.layer == WorldLayer::Mine) return true;
        return surface_edge_traversable(tile(from), tile(to), passage(from), passage(to));
    }
    void wear_trail(CellCoord c) {
        // Pre-V02 profiles retain byte-for-byte continuation semantics. Generator 6
        // worlds keep their surface rules and desire paths after the gen7 art rework.
        if (terrain_generator < surface_rules_generator_version) return;
        if (c.layer != WorldLayer::Surface) return;
        auto *ground = mutable_tile(c);
        if (!ground || ground->terrain == Terrain::Water || ground->trail_wear == 255) return;
        const auto next = static_cast<std::uint8_t>(std::min(255, int(ground->trail_wear) + 3));
        if (next != ground->trail_wear) {
            ground->trail_wear = next;
            changed(c);
        }
    }
    bool build_site(BuildingKind kind, CellCoord c) const {
        if (!inside_world(c.x,c.y) || c.layer != WorldLayer::Surface || !tile(c).explored ||
            !building_ground_allowed(kind,tile(c)) ||
            std::any_of(buildings.begin(),buildings.end(),[&](const Building &b){
                return b.x==c.x && b.y==c.y && b.layer==c.layer;
            })) return false;
        if (kind != BuildingKind::Bridge) {
            if (!walkable(c)) return false;
            const auto sample = [&](int x, int y) { return tile({x, y, c.layer}); };
            const auto support = terrain_generator >= stable_foundation_generator_version
                                     ? surface_foundation_support(c.x, c.y, sample)
                                     : surface_buildability(c.x, c.y, sample);
            return support == SurfaceReason::Allowed;
        }
        return bridge_connected(c.x,c.y,[&](int x,int y){
            const CellCoord neighbor{x,y,c.layer};
            return tile(neighbor).explored && walkable(neighbor);
        });
    }
    std::optional<CellCoord> portal_other(CellCoord c) const {
        for (auto &p : portals) {
            if (p.surface == c)
                return p.mine;
            if (p.mine == c)
                return p.surface;
        }
        return {};
    }
    static int encode_cell(CellCoord c) {
        const auto i = *world_cell_index(c.x, c.y);
        return static_cast<int>(
            i + (c.layer == WorldLayer::Mine ? static_cast<std::size_t>(world_size) * world_size : 0));
    }
    static CellCoord decode_cell(int i) {
        const auto layer = i >= world_size * world_size ? WorldLayer::Mine : WorldLayer::Surface;
        return *cell_from_index(static_cast<std::size_t>(i) % (world_size * world_size), layer);
    }
    bool route_step(CellCoord from, CellCoord to) const { return can_traverse(from, to); }
    static std::size_t batch_memory(const PathBatchState &batch) {
        std::size_t bytes =
            (batch.legacy_atomic ? legacy_path_batch_logical_bytes : sizeof(PathBatchState)) +
            batch.resident_ids.size() * sizeof(int) + batch.roots.size() * sizeof(CellCoord) +
            (batch.search_storage_reserved ? max_search_cells * sizeof(CellCoord)
                                           : batch.frontier.size() * sizeof(CellCoord)) +
            (batch.search_storage_reserved ? max_search_cells * sizeof(PathSearchNodeState)
                                           : batch.nodes.size() * sizeof(PathSearchNodeState)) +
            batch.nodes.size() * 40 +
            batch.drafts.size() *
                ((batch.legacy_atomic ? legacy_path_draft_logical_bytes : sizeof(PathRouteDraftState)) + 40);
        for (const auto &draft : batch.drafts)
            bytes += (draft.storage_reserved ? max_path_cells : draft.staged.size()) * sizeof(CellCoord);
        return bytes;
    }
    static std::size_t outcome_memory(const PathBatchOutcome &outcome) {
        return sizeof(PathBatchOutcome) + outcome.message.size();
    }
    void observe_planner_memory() {
        path_diagnostics.peak_planner_bytes =
            std::max(path_diagnostics.peak_planner_bytes, planner_memory_bytes);
        path_diagnostics.peak_staged_route_cells =
            std::max(path_diagnostics.peak_staged_route_cells, planner_staged_cells);
    }
    void rebuild_node_lookup(PathBatchState &batch) {
        if (lookup_batch_id == batch.path_batch_id && lookup_node_count == batch.nodes.size())
            return;
        const bool new_batch = lookup_batch_id != batch.path_batch_id;
        active_node_lookup.clear();
        for (std::size_t i = 0; i < batch.nodes.size(); ++i)
            active_node_lookup.emplace(encode_cell(batch.nodes[i].cell), i);
        if (new_batch) {
            active_all_starts.clear();
            active_remaining_starts.clear();
            for (const auto &draft : batch.drafts) {
                if (!draft.resolved) {
                    active_all_starts.insert(encode_cell(draft.start));
                    active_remaining_starts.insert(encode_cell(draft.start));
                }
            }
            for (const auto &node : batch.nodes)
                active_remaining_starts.erase(encode_cell(node.cell));
        }
        lookup_batch_id = batch.path_batch_id;
        lookup_node_count = batch.nodes.size();
    }
    const PathSearchNodeState *search_node(PathBatchState &batch, CellCoord cell) {
        rebuild_node_lookup(batch);
        const auto found = active_node_lookup.find(encode_cell(cell));
        return found == active_node_lookup.end() ? nullptr : &batch.nodes[found->second];
    }
    bool add_search_node(PathBatchState &batch, CellCoord cell, CellCoord next, bool expand = true) {
        rebuild_node_lookup(batch);
        const int key = encode_cell(cell);
        if (active_node_lookup.contains(key) || batch.nodes.size() >= max_search_cells)
            return false;
        batch.nodes.push_back({cell, next});
        if (expand)
            batch.frontier.push_back(cell);
        planner_memory_bytes += 40;
        observe_planner_memory();
        active_node_lookup.emplace(key, batch.nodes.size() - 1);
        active_remaining_starts.erase(key);
        lookup_node_count = batch.nodes.size();
        return true;
    }
    std::vector<CellCoord> path_roots(CellCoord target, bool to_adjacent) const {
        if (!to_adjacent)
            return walkable(target) ? std::vector<CellCoord>{target} : std::vector<CellCoord>{};
        constexpr int directions[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        std::vector<CellCoord> result;
        for (const auto &direction : directions) {
            CellCoord cell{target.x + direction[0], target.y + direction[1], target.layer};
            if (walkable(cell))
                result.push_back(cell);
        }
        return result;
    }
    CommandResult queue_path(PathCommandKind command, std::vector<int> ids, CellCoord target,
                             bool to_adjacent, Job job, std::optional<BuildingKind> build_kind = {},
                             int building_id = 0, Resources reserved_cost = {0, 0, 0}, bool manual = false) {
        if (ids.size() > max_path_batch_residents || next_path_batch_id == 0 ||
            next_path_batch_id == std::numeric_limits<std::uint64_t>::max())
            return {false, "Path batch limit reached.", 0, false, PathFailure::LimitExceeded};
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
            return {false, "Resident selection contains duplicates.", 0, false,
                    PathFailure::ResidentUnavailable};

        PathBatchState batch;
        batch.path_batch_id = next_path_batch_id;
        batch.command = command;
        batch.legacy_atomic = command != PathCommandKind::Manual;
        batch.resident_ids = ids;
        batch.target = target;
        batch.to_adjacent = to_adjacent;
        batch.assigned_job = job;
        batch.build_kind = build_kind;
        batch.building_id = building_id;
        batch.reserved_cost = reserved_cost;
        batch.enqueued_tick = ticks;
        if (ticks > std::numeric_limits<std::uint64_t>::max() - 1200)
            return {false, "Path deadline cannot be represented.", 0, false, PathFailure::LimitExceeded};
        batch.deadline_tick = ticks + 1200;
        batch.search_topology_revision = topology_revision;
        batch.roots = path_roots(target, to_adjacent);
        batch.drafts.reserve(ids.size());
        std::set<std::uint64_t> superseded_paths;
        std::size_t released_memory = 0;
        for (int id : ids) {
            const auto entity = find_resident(id);
            if (entity == entt::null || registry.get<Vital>(entity).health <= 0)
                return {false, "Unknown or unavailable resident id.", 0, false,
                        PathFailure::ResidentUnavailable};
            const auto &order = registry.get<OrderStatus>(entity);
            if (order.phase == OrderPhase::PathPending) {
                auto *intent = active_work_intent(id);
                if (!manual || !intent || intent->phase != WorkIntentPhase::PathPending ||
                    intent->path_batch_id != order.pending_batch)
                    return {false, "Resident already has a pending path.", 0, false,
                            PathFailure::ReservationConflict};
                const auto pending =
                    std::find_if(path_batches.begin(), path_batches.end(),
                                 [&](const auto &b) { return b.path_batch_id == order.pending_batch; });
                if (pending == path_batches.end() || pending->command != PathCommandKind::Work)
                    return {false, "Resident path correlation is invalid.", 0, false,
                            PathFailure::ReservationConflict};
                if (superseded_paths.insert(pending->path_batch_id).second)
                    released_memory += batch_memory(*pending);
            }
            const auto &position = registry.get<Position>(entity);
            const CellCoord start{static_cast<int>(std::lround(position.x)),
                                  static_cast<int>(std::lround(position.y)), position.layer};
            if (!inside_world(start.x, start.y))
                return {false, "Resident cannot resolve to a valid path cell.", 0, false,
                        PathFailure::ResidentUnavailable};
            batch.drafts.push_back({id, start, position.x, position.y, false, start, {}});
        }
        const auto memory = batch_memory(batch);
        if (path_batches.size() + path_results.size() - superseded_paths.size() >= max_path_batches)
            return {false, "Path queue limit reached.", 0, false, PathFailure::LimitExceeded};
        if (planner_memory_bytes - released_memory + memory > max_planner_bytes)
            return {false, "Path planner memory limit reached.", 0, false, PathFailure::LimitExceeded};
        path_batches.reserve(path_batches.size() + 1);
        if (manual)
            supersede_work_for_residents(ids);
        ++next_path_batch_id;
        path_batches.push_back(std::move(batch));
        planner_memory_bytes += memory;
        path_reserved_resources.wood += reserved_cost.wood;
        path_reserved_resources.stone += reserved_cost.stone;
        path_reserved_resources.food += reserved_cost.food;
        observe_planner_memory();
        for (int id : ids) {
            if (command != PathCommandKind::Replan) clear_outdoor_work_accumulator(id);
            auto &order = registry.get<OrderStatus>(find_resident(id));
            order = {OrderPhase::PathPending, PathFailure::None, path_batches.back().path_batch_id};
        }
        if (manual)
            begin_manual_override(ids);
        cache_dirty = true;
        return {true, "Path queued.", path_batches.back().path_batch_id, true, PathFailure::None};
    }
    void push_outcome(PathBatchOutcome outcome) {
        if (path_results.size() >= max_path_batches)
            throw std::logic_error("Path result bound violated");
        planner_memory_bytes += outcome_memory(outcome);
        observe_planner_memory();
        path_results.push_back(std::move(outcome));
    }
    void finish_failed(PathFailure failure, std::string message) {
        auto batch = std::move(path_batches.front());
        path_batches.erase(path_batches.begin());
        planner_memory_bytes -= batch_memory(batch);
        planner_staged_cells -=
            std::accumulate(batch.drafts.begin(), batch.drafts.end(), std::size_t{},
                            [](std::size_t total, const auto &draft) { return total + draft.staged.size(); });
        path_reserved_resources.wood -= batch.reserved_cost.wood;
        path_reserved_resources.stone -= batch.reserved_cost.stone;
        path_reserved_resources.food -= batch.reserved_cost.food;
        for (int id : batch.resident_ids) {
            if (batch.command == PathCommandKind::Manual)
                break;
            const auto entity = find_resident(id);
            if (entity == entt::null)
                continue;
            auto &order = registry.get<OrderStatus>(entity);
            if (order.pending_batch == batch.path_batch_id) {
                clear_outdoor_work_accumulator(id);
                order = {OrderPhase::PathFailed, failure, 0};
            }
        }
        if (batch.command == PathCommandKind::Manual) {
            for (auto &d : batch.drafts)
                manual_path_actor(batch, d, failure);
        } else if (batch.command == PathCommandKind::Work)
            work_path_result(batch.path_batch_id, false, failure);
        else if (batch.command == PathCommandKind::Logistics)
            logistics_path_result(batch.path_batch_id, false, failure);
        else {
            if (batch.command == PathCommandKind::Build)
                cancel_order(batch.building_id, LogisticsFailure::Unreachable);
            if (batch.command == PathCommandKind::Move || batch.command == PathCommandKind::Gather ||
                batch.command == PathCommandKind::Replan)
                for (int id : batch.resident_ids) {
                    const auto e = find_resident(id);
                    if (e != entt::null && registry.get<ProfessionState>(e).manual_override) {
                        registry.get<Activity>(e) = {};
                        registry.get<Route>(e) = {};
                        retire_manual_override(id);
                    }
                }
            push_outcome({batch.path_batch_id, batch.command, false, failure, std::move(message)});
        }
        lookup_batch_id = 0;
        active_node_lookup.clear();
        active_all_starts.clear();
        active_remaining_starts.clear();
        lookup_node_count = 0;
        cache_dirty = true;
    }
    void finish_success() {
        if (path_batches.front().command == PathCommandKind::Manual) {
            auto batch = std::move(path_batches.front());
            path_batches.erase(path_batches.begin());
            planner_memory_bytes -= batch_memory(batch);
            for (const auto &d : batch.drafts)
                planner_staged_cells -= d.staged.size();
            lookup_batch_id = 0;
            lookup_node_count = 0;
            active_node_lookup.clear();
            active_all_starts.clear();
            active_remaining_starts.clear();
            cache_dirty = true;
            return;
        }
        const auto &pending = path_batches.front();
        const auto terrain = tile(pending.target).terrain;
        const bool gather_job = pending.assigned_job == Job::Woodcut || pending.assigned_job == Job::Mine ||
                                pending.assigned_job == Job::Forage;
        const bool gather_target_valid =
            !gather_job || (tile(pending.target).amount > 0 &&
                            ((pending.assigned_job == Job::Woodcut && terrain == Terrain::Forest) ||
                             (pending.assigned_job == Job::Mine &&
                              (terrain == Terrain::Rock || terrain == Terrain::MineOre)) ||
                             (pending.assigned_job == Job::Forage && terrain == Terrain::Grass)));
        const bool farm_target_valid = pending.assigned_job != Job::Farm ||
            (!pending.resident_ids.empty() && farm_at(pending.target, registry.get<Identity>(find_resident(pending.resident_ids.front())).nation));
        const bool destination_valid = pending.to_adjacent || walkable(pending.target);
        const bool build_valid =
            !pending.build_kind ||
            (build_site(*pending.build_kind, pending.target) &&
             construction(pending.building_id) &&
             construction(pending.building_id)->phase == ConstructionPhase::Planned);
        if (!gather_target_valid || !farm_target_valid || !destination_valid || !build_valid) {
            finish_failed(PathFailure::TargetInvalid, "Path target became invalid.");
            return;
        }
        auto batch = std::move(path_batches.front());
        path_batches.erase(path_batches.begin());
        planner_memory_bytes -= batch_memory(batch);
        planner_staged_cells -=
            std::accumulate(batch.drafts.begin(), batch.drafts.end(), std::size_t{},
                            [](std::size_t total, const auto &draft) { return total + draft.staged.size(); });
        path_reserved_resources.wood -= batch.reserved_cost.wood;
        path_reserved_resources.stone -= batch.reserved_cost.stone;
        path_reserved_resources.food -= batch.reserved_cost.food;
        if (batch.build_kind) {
            construction(batch.building_id)->phase = ConstructionPhase::Delivery;
            buildings.push_back({batch.building_id, *batch.build_kind, batch.target.x, batch.target.y, 0,
                                 batch.target.layer, construction(batch.building_id)->owner_nation});
            ++topology_revision;
        }
        for (auto &draft : batch.drafts) {
            const auto entity = find_resident(draft.resident_id);
            auto &route = registry.get<Route>(entity);
            route = {std::move(draft.staged), 0, batch.target};
            // A cancelled order may leave the actor between cell centres. A zero-edge
            // search still needs a physical final step; otherwise logistics repeatedly
            // replans from the rounded destination without ever reaching it.
            if (route.points.empty() && (draft.start_x != static_cast<float>(draft.start.x) ||
                                         draft.start_y != static_cast<float>(draft.start.y)))
                route.points.push_back(draft.start);
            registry.get<Position>(entity).target = batch.target;
            registry.get<Activity>(entity) = {batch.assigned_job, 0};
            registry.get<OrderStatus>(entity) = {OrderPhase::Active, PathFailure::None, 0};
        }
        if (batch.command == PathCommandKind::Move)
            event("Move order issued.");
        else if (batch.command == PathCommandKind::Gather)
            event("Gather order issued.");
        else if (batch.command == PathCommandKind::Build)
            event(std::string(building_name(*batch.build_kind)) + " construction started.");
        if (batch.command == PathCommandKind::Work)
            work_path_result(batch.path_batch_id, true, PathFailure::None);
        else if (batch.command == PathCommandKind::Logistics)
            logistics_path_result(batch.path_batch_id, true, PathFailure::None);
        else
            push_outcome({batch.path_batch_id, batch.command, true, PathFailure::None, "Path applied."});
        lookup_batch_id = 0;
        active_node_lookup.clear();
        active_all_starts.clear();
        active_remaining_starts.clear();
        lookup_node_count = 0;
        cache_dirty = true;
    }
    bool starts_discovered(PathBatchState &batch) {
        rebuild_node_lookup(batch);
        return active_remaining_starts.empty();
    }
    void restart_search(PathBatchState &batch) {
        planner_memory_bytes -= batch.nodes.size() * 40;
        for (const auto &draft : batch.drafts) {
            planner_staged_cells -= draft.staged.size();
        }
        batch.phase = PathPlannerPhase::Queued;
        batch.search_topology_revision = topology_revision;
        batch.roots = path_roots(batch.target, batch.to_adjacent);
        planner_memory_bytes += batch.roots.size() * sizeof(CellCoord);
        batch.frontier.clear();
        batch.frontier_cursor = 0;
        batch.nodes.clear();
        batch.reconstruction_resident = 0;
        batch.stage_cursor = 0;
        for (auto &draft : batch.drafts) {
            draft.reconstruction_cursor = draft.start;
            draft.staged.clear();
        }
        lookup_batch_id = 0;
        active_node_lookup.clear();
        active_all_starts.clear();
        active_remaining_starts.clear();
        lookup_node_count = 0;
    }
    void process_paths() {
        std::size_t budget = path_work_units_per_tick;
        const auto before = budget;
        while (budget > 0 && !path_batches.empty()) {
            auto &batch = path_batches.front();
            if (ticks > batch.deadline_tick) {
                finish_failed(PathFailure::Timeout, "Path planning timed out.");
                continue;
            }
            if ((batch.phase != PathPlannerPhase::Queued || !batch.nodes.empty()) &&
                batch.search_topology_revision != topology_revision) {
                if (++batch.restart_count > 3) {
                    finish_failed(PathFailure::TopologyChanged, "Path topology changed repeatedly.");
                    continue;
                }
                ++path_diagnostics.topology_restarts;
                restart_search(batch);
                --budget;
                continue;
            }
            if (batch.phase == PathPlannerPhase::Queued) {
                if (batch.nodes.empty() && batch.search_topology_revision != topology_revision) {
                    planner_memory_bytes -= batch.roots.size() * sizeof(CellCoord);
                    batch.roots = path_roots(batch.target, batch.to_adjacent);
                    planner_memory_bytes += batch.roots.size() * sizeof(CellCoord);
                    batch.search_topology_revision = topology_revision;
                }
                if (batch.nodes.size() < batch.roots.size()) {
                    if (!batch.search_storage_reserved) {
                        constexpr auto cell_count = max_search_cells;
                        const auto reservation =
                            cell_count * (sizeof(CellCoord) + sizeof(PathSearchNodeState));
                        if (planner_memory_bytes + reservation > max_planner_bytes) {
                            finish_failed(PathFailure::LimitExceeded, "Path planner memory limit reached.");
                            continue;
                        }
                        batch.frontier.reserve(cell_count);
                        batch.nodes.reserve(cell_count);
                        batch.search_storage_reserved = true;
                        planner_memory_bytes += reservation;
                        observe_planner_memory();
                        ++batch.work_units_done;
                        --budget;
                        continue;
                    }
                    const auto root = batch.roots[batch.nodes.size()];
                    add_search_node(batch, root, root);
                    ++batch.work_units_done;
                    --budget;
                    continue;
                }
                batch.phase = PathPlannerPhase::Search;
                continue;
            }
            if (batch.phase == PathPlannerPhase::Search) {
                if (starts_discovered(batch)) {
                    batch.phase = PathPlannerPhase::Reconstruct;
                    continue;
                }
                if (batch.nodes.size() >= max_search_cells) {
                    if (!batch.legacy_atomic) {
                        // Preserve routes already found for other members of a group.
                        batch.phase = PathPlannerPhase::Reconstruct;
                        continue;
                    }
                    finish_failed(PathFailure::LimitExceeded, "Path search cell limit reached.");
                    continue;
                }
                if (batch.frontier_cursor >= batch.frontier.size()) {
                    if (!batch.legacy_atomic) {
                        batch.phase = PathPlannerPhase::Reconstruct;
                        continue;
                    }
                    finish_failed(PathFailure::Unreachable, "No route to target.");
                    continue;
                }
                const auto current = batch.frontier[batch.frontier_cursor++];
                constexpr int directions[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
                for (const auto &direction : directions) {
                    CellCoord next{current.x + direction[0], current.y + direction[1], current.layer};
                    if (!inside_world(next.x, next.y) ||
                        (batch.legacy_grid && (next.x >= 160 || next.y >= 160)))
                        continue;
                    rebuild_node_lookup(batch);
                    const bool is_start = active_all_starts.contains(encode_cell(next));
                    const bool traversable = can_traverse(next, current);
                    // A resident trapped inside a newly blocked cell may step out,
                    // but an ordinary walkable start must never bypass a cliff edge.
                    const bool escape_blocked_start = is_start && !walkable(next);
                    if (traversable || escape_blocked_start)
                        add_search_node(batch, next, current, traversable && walkable(next));
                }
                if (const auto portal = portal_other(current); portal && walkable(*portal))
                    add_search_node(batch, *portal, current);
                ++batch.work_units_done;
                ++path_diagnostics.total_expansions;
                --budget;
                if (planner_memory_bytes > max_planner_bytes) {
                    finish_failed(PathFailure::LimitExceeded, "Path planner memory limit reached.");
                    continue;
                }
                continue;
            }
            if (batch.phase == PathPlannerPhase::Reconstruct) {
                if (batch.reconstruction_resident >= batch.drafts.size()) {
                    batch.phase = PathPlannerPhase::Stage;
                    continue;
                }
                auto &draft = batch.drafts[batch.reconstruction_resident];
                if (draft.resolved) {
                    ++batch.reconstruction_resident;
                    ++batch.work_units_done;
                    --budget;
                    continue;
                }
                if (!draft.storage_reserved) {
                    const auto reservation = max_path_cells * sizeof(CellCoord);
                    if (planner_memory_bytes + reservation > max_planner_bytes) {
                        finish_failed(PathFailure::LimitExceeded, "Path planner memory limit reached.");
                        continue;
                    }
                    draft.staged.reserve(max_path_cells);
                    draft.storage_reserved = true;
                    planner_memory_bytes += reservation;
                    observe_planner_memory();
                    ++batch.work_units_done;
                    --budget;
                    continue;
                }
                if (!draft.reconstruction_cursor)
                    draft.reconstruction_cursor = draft.start;
                const auto *node = search_node(batch, *draft.reconstruction_cursor);
                if (!node) {
                    if (!batch.legacy_atomic) {
                        PathFailure failure = PathFailure::Unreachable;
                        if (batch.nodes.size() >= max_search_cells) {
                            // An isolated starting cell proves unreachable even when
                            // the remote search hit its budget. Otherwise report the limit.
                            bool has_exit = false;
                            for (const auto &d : std::array<CellCoord, 4>{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}})
                                has_exit = has_exit || walkable({draft.start.x + d.x, draft.start.y + d.y, draft.start.layer});
                            if (const auto exit = portal_other(draft.start); exit && walkable(*exit)) has_exit = true;
                            if (has_exit) failure = PathFailure::LimitExceeded;
                        }
                        manual_path_actor(batch, draft, failure);
                        ++batch.reconstruction_resident;
                        ++batch.work_units_done;
                        --budget;
                        continue;
                    }
                    finish_failed(PathFailure::Unreachable, "Path reconstruction failed.");
                    continue;
                }
                if (node->next == node->cell) {
                    ++batch.reconstruction_resident;
                    continue;
                }
                if (draft.staged.size() >= max_path_cells || planner_staged_cells >= max_staged_route_cells) {
                    if (!batch.legacy_atomic) {
                        manual_path_actor(batch, draft, PathFailure::LimitExceeded);
                        ++batch.reconstruction_resident;
                        ++batch.work_units_done;
                        --budget;
                        continue;
                    }
                    finish_failed(PathFailure::LimitExceeded, "Path length limit reached.");
                    continue;
                }
                draft.staged.push_back(node->next);
                ++planner_staged_cells;
                observe_planner_memory();
                draft.reconstruction_cursor = node->next;
                ++batch.work_units_done;
                ++path_diagnostics.total_reconstructed_cells;
                --budget;
                continue;
            }
            if (!batch.legacy_atomic) {
                if (batch.stage_cursor == batch.drafts.size()) {
                    ++batch.work_units_done;
                    --budget;
                    finish_success();
                    continue;
                }
                auto &draft = batch.drafts[batch.stage_cursor++];
                PathFailure failure = PathFailure::None;
                const auto entity = find_resident(draft.resident_id);
                if (!draft.resolved) {
                    if (entity == entt::null || registry.get<Vital>(entity).health <= 0)
                        failure = PathFailure::ResidentUnavailable;
                    else {
                        const auto &p = registry.get<Position>(entity);
                        if (p.x != draft.start_x || p.y != draft.start_y || p.layer != draft.start.layer ||
                            registry.get<OrderStatus>(entity).pending_batch != batch.path_batch_id)
                            failure = PathFailure::ResidentUnavailable;
                    }
                    if (failure == PathFailure::None &&
                        ((batch.assigned_job == Job::Move && !walkable(batch.target)) ||
                         (batch.assigned_job != Job::Move &&
                          (!work_terrain_matches(batch.assigned_job, tile(batch.target).terrain) ||
                           tile(batch.target).amount <= 0))))
                        failure = PathFailure::TargetInvalid;
                    manual_path_actor(batch, draft, failure);
                }
                ++batch.work_units_done;
                --budget;
                continue;
            }
            if (batch.stage_cursor < batch.drafts.size()) {
                const auto &draft = batch.drafts[batch.stage_cursor++];
                const auto entity = find_resident(draft.resident_id);
                if (entity == entt::null || registry.get<Vital>(entity).health <= 0) {
                    finish_failed(PathFailure::ResidentUnavailable, "Resident became unavailable.");
                    continue;
                }
                const auto &position = registry.get<Position>(entity);
                const auto &order = registry.get<OrderStatus>(entity);
                if (position.x != draft.start_x || position.y != draft.start_y ||
                    position.layer != draft.start.layer || order.pending_batch != batch.path_batch_id) {
                    finish_failed(PathFailure::ResidentUnavailable, "Resident reservation changed.");
                    continue;
                }
                ++batch.work_units_done;
                --budget;
                continue;
            }
            if (budget < batch.drafts.size() + 1)
                break;
            bool reservations_valid = true;
            for (const auto &draft : batch.drafts) {
                const auto entity = find_resident(draft.resident_id);
                if (entity == entt::null || registry.get<Vital>(entity).health <= 0 ||
                    registry.get<OrderStatus>(entity).pending_batch != batch.path_batch_id) {
                    reservations_valid = false;
                    break;
                }
            }
            if (!reservations_valid) {
                finish_failed(PathFailure::ResidentUnavailable, "Resident became unavailable.");
                continue;
            }
            batch.work_units_done += batch.drafts.size() + 1;
            budget -= batch.drafts.size() + 1;
            finish_success();
        }
        path_diagnostics.work_units_last_tick = before - budget;
        path_diagnostics.queued_batches = path_batches.size();
        path_diagnostics.active_batch_id = path_batches.empty() ? 0 : path_batches.front().path_batch_id;
        path_diagnostics.staged_route_cells = planner_staged_cells;
        path_diagnostics.planner_bytes = planner_memory_bytes;
    }
#include "world_logistics.inc"
#include "world_logistics_validation.inc"
#include "world_manual.inc"
#include "world_manual_validation.inc"
#include "world_autonomy.inc"
#include "world_professions.inc"
#include "world_professions_validation.inc"
#include "world_work.inc"
#include "world_work_validation.inc"
    bool certified_start() const {
        return certify_alpine_start([this](int x, int y) {
            return tile({x, y, WorldLayer::Surface});
        }).passed();
    }
    void generate() {
        const WorldGenerationConfig config{seed, terrain_generator, alpine_valley_profile_id,
                                           terrain_rules_version};
        static thread_local std::vector<std::pair<WorldGenerationConfig, std::vector<Chunk>>> cache;
        for (const auto &entry : cache)
            if (entry.first == config) { chunks = entry.second; return; }
        chunks.clear();
        chunks.reserve(world_chunk_count);
        for (auto layer : {WorldLayer::Surface, WorldLayer::Mine})
            for (int cy = floor_div(world_min, chunk_size); cy <= floor_div(world_max - 1, chunk_size); ++cy)
                for (int cx = floor_div(world_min, chunk_size); cx <= floor_div(world_max - 1, chunk_size);
                     ++cx) {
                    Chunk chunk;
                    chunk.key = {layer, cx, cy};
                    for (int ly = 0; ly < chunk_size; ++ly)
                        for (int lx = 0; lx < chunk_size; ++lx) {
                            int x = cx * chunk_size + lx, y = cy * chunk_size + ly;
                            const auto ux = static_cast<std::uint32_t>(x);
                            const auto uy = static_cast<std::uint32_t>(y);
                            const auto noise = mix(seed ^ ux * 73856093U ^ uy * 19349663U);
                            Tile &t = chunk.tiles[(std::size_t)ly * chunk_size + lx];
                            if (layer == WorldLayer::Mine)
                                t = noise % 100 < 18 ? Tile{Terrain::MineOre, 10 + (int)(noise % 21), false}
                                                     : Tile{Terrain::MineFloor, 0, false};
                            else
                                t = surface_tile(x, y, config);
                        }
                    chunks.push_back(std::move(chunk));
                }
        std::sort(chunks.begin(), chunks.end(), [](const Chunk &a, const Chunk &b) { return a.key < b.key; });
        *mutable_tile({28, 24, WorldLayer::Mine}) = {Terrain::MineFloor, 0, false};
        if (!certified_start())
            throw std::runtime_error("AlpineValley generation failed certification for seed " +
                                     std::to_string(seed));
        if (cache.size() == 2) cache.erase(cache.begin());
        cache.emplace_back(config, chunks);
    }
    void reveal(CellCoord c, float radius, NationId nation=1) {
        const int extent = static_cast<int>(std::ceil(radius));
        for (int y = std::max(world_min, c.y - extent); y <= std::min(world_max - 1, c.y + extent); ++y)
            for (int x = std::max(world_min, c.x - extent); x <= std::min(world_max - 1, c.x + extent); ++x)
                if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= radius * radius) {
                    CellCoord p{x, y, c.layer};
                    auto *t = mutable_tile(p);
                    if(t)remember_cell(p,nation);
                    if (t && !t->explored) {
                        t->explored = true;
                        changed(p);
                    }
                }
        for (const auto &portal : portals) {
            auto *surface = mutable_tile(portal.surface);
            auto *mine = mutable_tile(portal.mine);
            if (surface && mine && explored_by(portal.surface,nation) && !explored_by(portal.mine,nation)) {
                remember_cell(portal.mine,nation);
                mine->explored = true;
                changed(portal.mine);
            } else if (surface && mine && explored_by(portal.mine,nation) && !explored_by(portal.surface,nation)) {
                remember_cell(portal.surface,nation);
                surface->explored = true;
                changed(portal.surface);
            }
        }
    }
};
World::World(std::uint32_t seed, int population, std::uint32_t terrain_generator, ClimateConfig climate)
    : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    s.seed = seed;
    if (terrain_generator == 0 || terrain_generator > generator_version)
        throw std::invalid_argument("Invalid terrain generator profile");
    s.terrain_generator = terrain_generator;
    const WeatherContext context{seed,climate};
    s.weather_cache.emplace(context,CalendarTick{scenario_calendar_offset(climate.scenario)});
    s.climate = initialize_climate(context,*s.weather_cache);
    auto token = secure_token(16);
    auto nib = [](char c) { return (std::uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; i < 16; ++i)
        s.world_id[i] = (std::uint8_t)((nib(token[i * 2]) << 4) | nib(token[i * 2 + 1]));
    s.world_id[6] = (s.world_id[6] & 15) | 64;
    s.world_id[8] = (s.world_id[8] & 63) | 128;
    s.generate();
    population = std::clamp(population, 0, 1500);
    s.resources.food = static_cast<int>(s.initial_food(population));
    s.buildings.push_back({s.next_building_id++, BuildingKind::Camp, 24, 24, 1, WorldLayer::Surface, 1});
    s.storages.push_back({1, s.initial_camp_capacity(static_cast<std::uint32_t>(s.resources.food)), 7, 0});
    for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food})
        s.add_stack(item, static_cast<std::uint32_t>(Impl::amount(s.resources, item)), 1,
                    {ItemLocationKind::Storage, {}, 1});
    static constexpr const char *names[] = {"Marta", "Taras", "Oles",   "Iryna", "Bohdan",
                                            "Nadia", "Lev",   "Sofiia", "Marko", "Vira"};
    for (int i = 0; i < population; ++i) {
        auto e = s.registry.create();
        int x = 21 + i % 7, y = 21 + (i / 7) % 7;
        s.registry.emplace<Identity>(e, i + 1, i < 10 ? names[i] : "Settler " + std::to_string(i + 1),
                                     NationId{1});
        s.registry.emplace<Position>(e, (float)x, (float)y, CellCoord{x, y, WorldLayer::Surface},
                                     WorldLayer::Surface);
        s.registry.emplace<Activity>(e);
        s.registry.emplace<Vital>(e);
        s.registry.emplace<Skills>(e);
        s.registry.emplace<ProfessionState>(e);
        s.registry.emplace<Route>(e);
        s.registry.emplace<OrderStatus>(e);
        s.resident_by_id.emplace(i + 1, e);
    }
    s.reveal({24, 24, WorldLayer::Surface}, starting_vision_radius);
    s.event("The expedition founded Ribterra camp.");
}
World::~World() = default;
World::World(World &&) noexcept = default;
World &World::operator=(World &&) noexcept = default;
const Tile &World::tile(int x, int y, WorldLayer l) const {
    return impl_->tile({x, y, l});
}
const std::vector<Chunk> &World::chunks() const {
    return impl_->chunks;
}
const std::vector<Portal> &World::portals() const {
    return impl_->portals;
}
const std::vector<Resident> &World::residents() const {
    impl_->rebuild_cache();
    return impl_->resident_cache;
}
const std::vector<Building> &World::buildings() const {
    return impl_->buildings;
}
const Resources &World::resources() const {
    impl_->resources = impl_->inventory(1).total;
    return impl_->resources;
}
InventorySummary World::inventory(NationId nation) const {
    return impl_->inventory(nation);
}
const std::vector<ItemStackState> &World::item_stacks() const {
    return impl_->item_stacks;
}
const std::vector<StorageState> &World::storages() const {
    return impl_->storages;
}
const std::vector<ItemReservationState> &World::item_reservations() const {
    return impl_->item_reservations;
}
const std::vector<LogisticsTaskState> &World::logistics_tasks() const {
    return impl_->logistics_tasks;
}
const std::vector<ConstructionOrderState> &World::construction_orders() const {
    return impl_->construction_orders;
}
const std::vector<std::string> &World::events() const {
    return impl_->events;
}
std::vector<PathBatchOutcome> World::take_path_results() {
    auto results = std::move(impl_->path_results);
    for (const auto &outcome : results)
        impl_->planner_memory_bytes -= Impl::outcome_memory(outcome);
    impl_->path_results.clear();
    impl_->path_diagnostics.queued_batches = impl_->path_batches.size();
    impl_->path_diagnostics.planner_bytes = impl_->planner_memory_bytes;
    return results;
}
PathPlannerDiagnostics World::path_diagnostics() const {
    auto result = impl_->path_diagnostics;
    result.queued_batches = impl_->path_batches.size();
    result.active_batch_id = impl_->path_batches.empty() ? 0 : impl_->path_batches.front().path_batch_id;
    result.staged_route_cells = impl_->planner_staged_cells;
    result.planner_bytes = impl_->planner_memory_bytes;
    return result;
}
std::uint64_t World::ticks() const {
    return impl_->ticks;
}
std::uint32_t World::seed() const {
    return impl_->seed;
}
std::uint32_t World::terrain_generator() const {
    return impl_->terrain_generator;
}
bool World::walkable(int x, int y, WorldLayer l) const {
    return impl_->walkable({x, y, l});
}
Resources World::cost(BuildingKind k) {
    return valid_building_kind(k) ? building_definition(k).cost : Resources{};
}
CommandResult World::move(const std::vector<int> &ids, int x, int y, WorldLayer l) {
    if (ids.empty())
        return {false, "Select at least one resident."};
    for (int id : ids)
        if (impl_->resident_busy(id))
            return {false, "Resident is carrying out a reserved delivery.", 0, false,
                    PathFailure::ReservationConflict};
    CellCoord target{x, y, l};
    if (!impl_->walkable(target))
        return {false, "Destination is blocked.", 0, false, PathFailure::TargetInvalid};
    if (!impl_->can_track_legacy_manual(ids))
        return {false, "Manual order capacity reached.", 0, false, PathFailure::LimitExceeded};
    auto result =
        impl_->queue_path(PathCommandKind::Move, ids, target, false, Job::Move, {}, 0, {0, 0, 0}, true);
    if (result.accepted)
        impl_->track_legacy_manual(ids, result.path_batch_id, target, ManualOrderKind::Move);
    return result;
}
bool World::valid_profession_assignment(const ProfessionAssignment &assignment) {
    return static_cast<unsigned>(assignment.profession) <= static_cast<unsigned>(Profession::Guard) &&
           (!assignment.worksite || (inside_world(assignment.worksite->x, assignment.worksite->y) &&
                                     valid_layer(assignment.worksite->layer))) &&
           std::all_of(assignment.priorities.begin(), assignment.priorities.end(),
                       [](auto priority) { return priority <= 5; });
}
std::vector<ResidentCommandOutcome> World::assign_profession(const std::vector<int> &ids,
                                                             const ProfessionAssignment &assignment) {
    return impl_->change_profession(ids, &assignment);
}
std::vector<ResidentCommandOutcome> World::stop(const std::vector<int> &ids) {
    return impl_->change_profession(ids, nullptr);
}
std::vector<ResidentCommandOutcome> World::issue_manual_order(const std::vector<int> &ids,
                                                              const ManualOrderRequest &request,
                                                              ManualQueueMode mode,
                                                              std::optional<ManualOrderIssuer> issuer) {
    return impl_->issue_manual(ids, request, mode, issuer);
}
const std::vector<ManualOrderState> &World::manual_orders() const {
    return impl_->manual_orders;
}
ManualOrderDiagnostics World::manual_diagnostics() const {
    auto result = impl_->manual_diagnostics;
    for (const auto &o : impl_->manual_orders)
        Impl::manual_live(o) ? ++result.active_orders : ++result.terminal_orders;
    return result;
}
std::vector<ResidentCommandOutcome> World::release_manual_control(const std::vector<int> &input) {
    auto ids = input;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<ResidentCommandOutcome> results;
    for (auto id : ids) {
        const auto e = impl_->find_resident(id);
        if (e == entt::null) {
            results.push_back({id, ResidentCommandStatus::Rejected, PathFailure::ResidentUnavailable});
            continue;
        }
        const bool boundary = impl_->clear_manual(id, ManualOrderCompletion::Released);
        auto &p = impl_->registry.get<ProfessionState>(e);
        p.deferred = boundary;
        p.next_evaluation_tick = impl_->ticks;
        p.wait_reason = boundary ? (impl_->resident_busy(id) ? ProfessionWaitReason::DeliveryBoundary
                                                             : ProfessionWaitReason::PendingOrder)
                        : p.assignment.automation_enabled ? ProfessionWaitReason::None
                                                          : ProfessionWaitReason::Stopped;
        results.push_back({id, boundary ? ResidentCommandStatus::Deferred : ResidentCommandStatus::Applied,
                           PathFailure::None});
    }
    impl_->prune_manual_history();
    return results;
}
std::vector<ResidentCommandOutcome> World::cancel_manual_order(std::uint64_t id) {
    auto *o = impl_->manual_order(id);
    if (!o || !Impl::manual_live(*o))
        return {{o ? o->resident_id : 0, ResidentCommandStatus::Rejected, PathFailure::TargetInvalid, id}};
    const int resident = o->resident_id;
    const bool head = impl_->manual_head(resident) == o;
    const auto e = impl_->find_resident(resident);
    if (head) {
        impl_->manual_mutation = true;
        impl_->cancel_manual_path_actor(resident);
        if (!impl_->resident_busy(resident) &&
            (impl_->registry.get<OrderStatus>(e).phase != OrderPhase::PathPending ||
             impl_->active_work_intent(resident)))
            impl_->stop_actor(resident);
        impl_->manual_mutation = false;
    }
    const bool boundary = head && (impl_->resident_busy(resident) ||
                                   impl_->registry.get<OrderStatus>(e).phase == OrderPhase::PathPending);
    impl_->finish_manual(*o, ManualOrderCompletion::Cancelled);
    if (boundary) {
        if (auto *next = impl_->manual_head(resident)) {
            next->phase = ManualOrderPhase::WaitingBoundary;
            impl_->bind_manual_executor(*next);
        } else {
            auto &p = impl_->registry.get<ProfessionState>(e);
            p.deferred = true;
            p.wait_reason = impl_->resident_busy(resident) ? ProfessionWaitReason::DeliveryBoundary
                                                           : ProfessionWaitReason::PendingOrder;
        }
    }
    impl_->prune_manual_history();
    return {{resident, boundary ? ResidentCommandStatus::Deferred : ResidentCommandStatus::Applied,
             PathFailure::None, id}};
}
std::optional<ModifierBreakdown> World::skill_breakdown(int id, Skill skill) const {
    const auto e = impl_->find_resident(id);
    if (e == entt::null || static_cast<std::size_t>(skill) >= skill_count)
        return {};
    return impl_->modifier(e, skill);
}
ProfessionDiagnostics World::profession_diagnostics() const {
    return impl_->profession_diagnostics;
}
#include "world_economy_api.inc"
#include "world_wilderness_api.inc"
const SettlementState &World::settlement(NationId nation) const { return impl_->policy(nation); }
bool World::explored(CellCoord c,NationId nation)const{return impl_->explored_by(c,nation);}
#include "world_nations_api.inc"
bool World::work_allowed(int id, Job job, std::optional<CellCoord> site) const {
    return impl_->work_allowed(impl_->find_resident(id),job,site);
}
CommandResult World::decide(SettlementDecision decision,std::uint64_t revision,int id,NationId nation) {
    if(!alpha::forced_work&&decision!=SettlementDecision::Recruit)return {false,"alpha.forced_work_unavailable"};
    auto &s=*impl_;auto &policy=s.policy(nation);
    const auto reject=[](const char *reason){return CommandResult{false,reason,0,false,PathFailure::TargetInvalid};};
    if(revision!=policy.revision || revision==UINT64_MAX) return reject("settlement.stale");
    if(decision==SettlementDecision::Recruit) {
        if(policy.arrival_tick) return reject("settlement.pending");
        if(residents().size()>=1500 || s.ticks>UINT64_MAX-recruitment_ticks-youth_ticks ||
           std::any_of(residents().begin(),residents().end(),[](const auto &r){return r.id>=INT_MAX-1;})) return reject("settlement.limit");
        std::uint64_t food=0;
        for(const auto &stack:s.item_stacks) if(stack.owner_nation==nation && stack.item==ItemKind::Food && stack.location.kind==ItemLocationKind::Storage)
            food+=stack.quantity-s.reserved(stack.stack_id);
        if(food<recruitment_food) return reject("settlement.food_missing");
        for(int n=0;n<recruitment_food;++n) s.consume_food(nation);
        s.erase_empty_stacks();
        policy.arrival_tick=s.ticks+recruitment_ticks;
    } else if(decision==SettlementDecision::NightShift) {
        if(!is_night(s.calendar_tick()) || policy.night_shift(s.calendar_tick())) return reject("settlement.night_unavailable");
        policy.forced_night=calendar_day(s.calendar_tick());
        policy.night_discontent=std::min(100,policy.night_discontent+night_shift_discontent);
    } else if(decision==SettlementDecision::YouthWork) {
        auto youth=std::find_if(policy.young.begin(),policy.young.end(),[&](const auto &y){return y.resident_id==id && s.ticks<y.adult_tick;});
        auto e=s.find_resident(id);
        if(youth==policy.young.end() || youth->forced_work || e==entt::null || s.registry.get<Vital>(e).health<=0)
            return reject("settlement.youth_unavailable");
        youth->forced_work=true;
        policy.youth_discontent=std::min(100,policy.youth_discontent+youth_work_discontent);
    } else return reject("settlement.invalid");
    ++policy.revision;s.cache_dirty=true;
    return {true,"settlement.accepted"};
}
CommandResult World::gather(const std::vector<int> &ids, int x, int y, WorldLayer l) {
    CellCoord target{x, y, l};
    auto &t = impl_->tile(target);
    if (ids.empty())
        return {false, "Select at least one resident."};
    for (int id : ids)
        if (impl_->resident_busy(id))
            return {false, "Resident is carrying out a reserved delivery.", 0, false,
                    PathFailure::ReservationConflict};
    if (!inside_world(x, y) || t.amount <= 0 || (l == WorldLayer::Surface && t.terrain == Terrain::Water))
        return {false, "Nothing can be gathered there.", 0, false, PathFailure::TargetInvalid};
    Job job = t.terrain == Terrain::Forest                                  ? Job::Woodcut
              : t.terrain == Terrain::Rock || t.terrain == Terrain::MineOre ? Job::Mine
                                                                            : Job::Forage;
    if (!impl_->can_track_legacy_manual(ids))
        return {false, "Manual order capacity reached.", 0, false, PathFailure::LimitExceeded};
    auto result = impl_->queue_path(PathCommandKind::Gather, ids, target, true, job, {}, 0, {0, 0, 0}, true);
    if (result.accepted)
        impl_->track_legacy_manual(ids, result.path_batch_id, target, ManualOrderKind::Gather);
    return result;
}
bool World::can_build(BuildingKind k, int x, int y, WorldLayer l,NationId nation) const {
    if(!alpha::buildable(k)||!explored({x,y,l},nation))return false;
    int v = (int)k;
    if (v < (int)BuildingKind::House || !valid_building_kind(k) || l != WorldLayer::Surface ||
        !impl_->build_site(k, {x,y,l}))
        return false;
    if (k == BuildingKind::Fishing && !fishing_shore(x, y, [&](int a, int b) { return tile(a, b, l); }))
        return false;
    if (k == BuildingKind::Mine) {
        std::size_t shafts = impl_->portals.size();
        for (const auto &order : impl_->construction_orders)
            shafts += order.kind == BuildingKind::Mine && order.phase != ConstructionPhase::Complete &&
                      order.phase != ConstructionPhase::Cancelled;
        if (shafts >= 64 || std::any_of(impl_->portals.begin(), impl_->portals.end(), [&](const auto &p) {
                return p.surface.x == x && p.surface.y == y;
            })) return false;
    }
    for (const auto &batch : impl_->path_batches) {
        if (!batch.build_kind)
            continue;
        if (batch.target == CellCoord{x, y, l})
            return false;
    }
    auto c = cost(k);
    auto available = impl_->inventory(nation).available;
    return available.wood >= c.wood && available.stone >= c.stone && available.food >= c.food;
}
CommandResult World::build(BuildingKind k, int x, int y, WorldLayer l,NationId nation) {
    if (!alpha::buildable(k)) return {false,"alpha.building_unavailable",0,false,PathFailure::TargetInvalid};
    if (!can_build(k, x, y, l,nation))
        return {false, "Building site, type, or resources are invalid.", 0, false,
                PathFailure::TargetInvalid};
    const int building_id = impl_->next_building_id;
    const auto first_task = impl_->next_logistics_task_id;
    const auto first_reservation = impl_->next_item_reservation_id;
    if (!impl_->reserve_construction(
            {building_id, k, {x, y, l}, nation, cost(k), {0, 0, 0}, ConstructionPhase::Planned, 0}))
        return {false, "Materials or logistics capacity are unavailable.", 0, false,
                PathFailure::LimitExceeded};
    auto result =
        impl_->queue_path(PathCommandKind::Build, {}, {x, y, l}, true, Job::Build, k, building_id, cost(k));
    if (result.accepted) {
        ++impl_->next_building_id;
        impl_->construction(building_id)->originating_path_batch_id = result.path_batch_id;
    } else {
        std::erase_if(impl_->logistics_tasks, [=](const auto &t) { return t.task_id >= first_task; });
        std::erase_if(impl_->item_reservations,
                      [=](const auto &r) { return r.reservation_id >= first_reservation; });
        impl_->construction_orders.pop_back();
        impl_->next_logistics_task_id = first_task;
        impl_->next_item_reservation_id = first_reservation;
    }
    return result;
}
CommandResult World::cancel_construction(int building_id) {
    auto *order = impl_->construction(building_id);
    if (!order || order->phase == ConstructionPhase::Complete || order->phase == ConstructionPhase::Cancelled)
        return {false, "Construction is not active.", 0, false, PathFailure::TargetInvalid};
    if (order->phase == ConstructionPhase::Work) {
        const auto total = impl_->inventory(order->owner_nation).total;
        std::size_t refunds = 0;
        for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food}) {
            const auto quantity = Impl::amount(order->delivered, item);
            if (!quantity)
                continue;
            ++refunds;
            if (Impl::amount(total, item) > std::numeric_limits<int>::max() - quantity)
                return {false, "Returned material exceeds inventory capacity.", 0, false,
                        PathFailure::LimitExceeded};
        }
        if (impl_->item_stacks.size() + refunds > max_item_stacks ||
            impl_->next_item_stack_id >= std::numeric_limits<std::uint64_t>::max() - refunds)
            return {false, "Returned material exceeds item capacity.", 0, false, PathFailure::LimitExceeded};
    }
    if (order->phase == ConstructionPhase::Planned) {
        auto it = std::find_if(impl_->path_batches.begin(), impl_->path_batches.end(), [&](const auto &b) {
            return b.path_batch_id == order->originating_path_batch_id;
        });
        if (it != impl_->path_batches.end()) {
            const auto id = it->path_batch_id;
            impl_->planner_memory_bytes -= Impl::batch_memory(*it);
            for (const auto &draft : it->drafts) {
                impl_->planner_staged_cells -= draft.staged.size();
                impl_->registry.get<OrderStatus>(impl_->find_resident(draft.resident_id)) = {};
            }
            impl_->path_reserved_resources.wood -= it->reserved_cost.wood;
            impl_->path_reserved_resources.stone -= it->reserved_cost.stone;
            impl_->path_reserved_resources.food -= it->reserved_cost.food;
            impl_->path_batches.erase(it);
            impl_->lookup_batch_id = 0;
            impl_->active_node_lookup.clear();
            impl_->active_all_starts.clear();
            impl_->active_remaining_starts.clear();
            impl_->lookup_node_count = 0;
            impl_->push_outcome(
                {id, PathCommandKind::Build, false, PathFailure::TargetInvalid, "Construction cancelled."});
        }
    }
    impl_->cancel_order(building_id, LogisticsFailure::Cancelled);
    return {true, "Construction cancelled."};
}
void World::tick() {
    auto &s = *impl_;
    if (s.ticks == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("World simulation tick overflow");
    const auto next_tick = s.ticks + 1u;
    // The domain validates the next checked calendar/horizon before changing
    // its runtime/cache. Only then publish the corresponding simulation tick.
    advance_climate_tick(s.climate,next_tick,*s.weather_cache);
    s.ticks = next_tick;
    s.population_tick();
    for(auto &nation:s.economy.nations)if(nation.id!=1)s.population_tick(static_cast<NationId>(nation.id));
    s.process_paths();
    s.process_animals();
    s.process_expeditions();
    s.process_economy();
    s.process_manual();
    s.process_professions();
    s.process_work_intents();
    s.process_logistics();
    for (auto e : s.entities()) {
        auto &a = s.registry.get<Activity>(e);
        auto &v = s.registry.get<Vital>(e);
        auto &p = s.registry.get<Position>(e);
        auto &r = s.registry.get<Route>(e);
        auto &order = s.registry.get<OrderStatus>(e);
        if (v.health <= 0) {
            a = {};
            r = {};
            if (order.phase != OrderPhase::PathPending)
                order = {OrderPhase::Idle, PathFailure::None, 0};
            continue;
        }
        if (order.phase == OrderPhase::PathPending)
            continue;
        if (s.economy_worker(s.registry.get<Identity>(e).id) && a.job==Job::Move && !s.work_allowed(e,Job::Build)) continue;
        if (!s.work_allowed(e, a.job, r.work))
            continue;
        if (r.cursor < r.points.size()) {
            auto next = r.points[r.cursor];
            CellCoord current{(int)std::lround(p.x), (int)std::lround(p.y), p.layer};
            if ((!s.walkable(next) || (next != current && !s.can_traverse(current, next))) && next != current) {
                if (auto *manual = s.manual_head(s.registry.get<Identity>(e).id);
                    manual && !manual->legacy_compatibility &&
                    manual->request.kind == ManualOrderKind::Move) {
                    a = {};
                    r = {};
                    order = {};
                    manual->phase = ManualOrderPhase::Queued;
                    manual->executor = ManualExecutorKind::None;
                    manual->executor_id = 0;
                    continue;
                }
                if (!r.work) {
                    a = {};
                    r = {};
                    order = {OrderPhase::PathFailed, PathFailure::TargetInvalid, 0};
                    s.retire_manual_override(s.registry.get<Identity>(e).id);
                    continue;
                }
                const int id = s.registry.get<Identity>(e).id;
                const bool logistics = s.resident_busy(id);
                if (!logistics && (a.job == Job::Build || a.job == Job::Woodcut || a.job == Job::Mine ||
                                   a.job == Job::Forage || a.job == Job::Farm)) {
                    const auto target = *r.work;
                    const auto job = a.job;
                    int site_id = 0;
                    if (job == Job::Build)
                        for (const auto &site : s.construction_orders)
                            if (site.site == target && site.phase == ConstructionPhase::Work)
                                site_id = site.building_id;
                    s.supersede_work_for_residents({id});
                    a = {};
                    r = {};
                    order = {};
                    if (job == Job::Build && s.active_work_by_building.contains(site_id))
                        continue;
                    if ((job == Job::Build && !site_id) ||
                        !s.create_work_intent(id,
                                              job == Job::Build ? WorkIntentKind::BuildAssignment
                                                                : WorkIntentKind::GatherContinuation,
                                              target, job, site_id)) {
                        order = {OrderPhase::PathFailed, PathFailure::LimitExceeded, 0};
                        s.retire_manual_override(id);
                    }
                    continue;
                }
                bool to_adjacent = a.job != Job::Move;
                if (logistics) {
                    auto *task = s.task(s.active_task_by_resident.at(id));
                    if (task->phase == LogisticsPhase::ToPickup) {
                        auto *source = s.stack(task->source_stack_id);
                        auto target = source ? s.location_cell(source->location) : std::optional<CellCoord>{};
                        to_adjacent = !source || source->location.kind != ItemLocationKind::Ground ||
                                      !target || !s.walkable(*target);
                    }
                }
                auto queued = s.queue_path(logistics ? PathCommandKind::Logistics : PathCommandKind::Replan,
                                           {id}, *r.work, to_adjacent, a.job);
                if (logistics && queued.accepted)
                    s.task(s.active_task_by_resident.at(id))->path_batch_id = queued.path_batch_id;
                if (!queued.accepted) {
                    order = {OrderPhase::PathFailed, queued.failure, 0};
                    if (!logistics) {
                        a = {};
                        r = {};
                        s.retire_manual_override(id);
                    }
                }
                continue;
            }
            next = r.points[r.cursor];
            if (next.layer != p.layer) {
                p.layer = next.layer;
                p.x = (float)next.x;
                p.y = (float)next.y;
                ++r.cursor;
                s.reveal(next, resident_vision_radius,s.registry.get<Identity>(e).nation);
                continue;
            }
            float dx = next.x - p.x, dy = next.y - p.y;
            float speed = .125f;
            if (std::abs(dx) <= speed && std::abs(dy) <= speed) {
                p.x = (float)next.x;
                p.y = (float)next.y;
                ++r.cursor;
                s.wear_trail(next);
                s.reveal(next, resident_vision_radius,s.registry.get<Identity>(e).nation);
            } else if (std::abs(dx) > .001f)
                p.x += dx > 0 ? speed : -speed;
            else
                p.y += dy > 0 ? speed : -speed;
            continue;
        }
        if (a.job == Job::Move) {
            a = {};
            order = {OrderPhase::Idle, PathFailure::None, 0};
            s.moved = true;
            s.finish_manual_actor(s.registry.get<Identity>(e).id, ManualOrderCompletion::Arrived);
            s.retire_manual_override(s.registry.get<Identity>(e).id);
            auto &profession = s.registry.get<ProfessionState>(e);
            if (profession.wait_reason == ProfessionWaitReason::Strolling) {
                // Leave an idle boundary for paid construction and hauling before
                // choosing another walk, even when the last walk outlasted its timer.
                profession.next_evaluation_tick = s.ticks + 40;
                profession.wait_reason = ProfessionWaitReason::Unassigned;
            }
            continue;
        }
        if (a.job == Job::Woodcut || a.job == Job::Mine || a.job == Job::Forage) {
            if (!r.work) {
                a = {};
                order = {OrderPhase::Idle, PathFailure::None, 0};
                s.retire_manual_override(s.registry.get<Identity>(e).id);
                continue;
            }
            auto *work = s.mutable_tile(*r.work);
            if (!work || work->amount <= 0 || !Impl::work_terrain_matches(a.job, work->terrain)) {
                const auto target = *r.work;
                const auto job = a.job;
                const auto id = s.registry.get<Identity>(e).id;
                s.supersede_work_for_residents({id});
                a = {};
                r = {};
                order = {OrderPhase::Idle, PathFailure::None, 0};
                if (s.create_work_intent(id, WorkIntentKind::GatherContinuation, target, job)) {
                    auto *intent = s.active_work_intent(id);
                    const bool exhausted = s.work_target_status(*intent) == 1;
                    s.finish_work(*intent,
                                  exhausted ? WorkCompletionReason::Exhausted : WorkCompletionReason::None,
                                  exhausted ? PathFailure::None : PathFailure::TargetInvalid);
                } else {
                    order = {OrderPhase::PathFailed, PathFailure::LimitExceeded, 0};
                    s.retire_manual_override(id);
                }
                continue;
            }
            if (a.progress < 1) {
                if (!s.outdoor_progress_step(e)) continue;
                a.progress += skill_gather_base_progress() *
                              (static_cast<float>(s.modifier(e, Impl::job_skill(a.job)).speed_bp) / 10000.f);
            }
            if (a.progress >= 1) {
                const auto job = a.job;
                const auto work_cell = *r.work;
                const auto item = job == Job::Woodcut ? ItemKind::Wood
                                  : job == Job::Mine  ? ItemKind::Stone
                                                      : ItemKind::Food;
                if (!s.gather_yield(e, item, job == Job::Woodcut ? 3 : 2, job, work_cell)) {
                    a.progress = 1;
                    continue;
                }
                if (a.job != Job::Haul)
                    a.progress -= 1;
                --work->amount;
                s.changed(work_cell);
                s.useful_work(e, Impl::job_skill(job), skill_gather_work_units());
                if (work->amount == 0) {
                    work->terrain = work_cell.layer == WorldLayer::Mine ? Terrain::MineFloor : Terrain::Grass;
                    ++s.topology_revision;
                }
                s.gathered = true;
                // A saved automatic worker must also stop when storage is full and
                // the last output was placed on the ground instead of carried.
                if (a.job != Job::Haul && !s.manual_head(s.registry.get<Identity>(e).id)) {
                    a = {};
                    r = {};
                    order = {};
                    s.retire_manual_override(s.registry.get<Identity>(e).id);
                }
                if (a.job != Job::Haul)
                    if (auto *manual = s.manual_head(s.registry.get<Identity>(e).id);
                        manual && manual->request.kind == ManualOrderKind::Gather &&
                        manual->request.gather_completion == GatherCompletion::OneDelivery) {
                        a = {};
                        r = {};
                        order = {OrderPhase::PathFailed, PathFailure::TargetInvalid, 0};
                        s.finish_manual(*manual, ManualOrderCompletion::None, PathFailure::TargetInvalid);
                    }
            }
        } else if (a.job == Job::Farm) {
            const auto id = s.registry.get<Identity>(e);
            const auto *farm = r.work ? s.farm_at(*r.work, id.nation) : nullptr;
            if (!farm || !s.eligible_for_auto_work(e, Job::Farm, *r.work)) {
                s.stop_actor(id.id);
                continue;
            }
            const CellCoord site{farm->x, farm->y, farm->layer};
            if (!s.at_location(id.id, site, true)) continue;
            const float before = a.progress;
            a.progress = std::min(1.f, a.progress + (1.f / farm_work_ticks) *
                (static_cast<float>(s.modifier(e, Skill::Farming).speed_bp) / 10000.f));
            if (a.progress > before) s.useful_work(e, Skill::Farming, skill_work_units_per_tick());
            if (a.progress >= 1 && s.gather_yield(e, ItemKind::Food, farm_harvest_food, Job::Farm, site)) {
                // gather_yield owns the physical crop and takes the farmer to storage.
                if (a.job != Job::Haul) a.progress = 0;
                s.gathered = true;
            }
        } else if (a.job == Job::Build) {
            auto b = std::find_if(s.buildings.begin(), s.buildings.end(), [&](auto &x) {
                return r.work && x.x == r.work->x && x.y == r.work->y && x.layer == r.work->layer;
            });
            if (b == s.buildings.end() || b->progress >= 1) {
                a = {};
                order = {OrderPhase::Idle, PathFailure::None, 0};
                continue;
            }
            auto *construction = s.construction(b->id);
            if (construction && construction->phase != ConstructionPhase::Work)
                continue;
            if (!s.at_location(s.registry.get<Identity>(e).id, {b->x, b->y, b->layer}, true))
                continue;
            if (!s.outdoor_progress_step(e)) continue;
            float before = b->progress;
            b->progress = std::min(
                1.f, b->progress +
                         skill_build_base_progress() *
                             (static_cast<float>(s.modifier(e, Skill::Construction).speed_bp) / 10000.f));
            const auto full_credit = std::llround(static_cast<double>(skill_work_units_per_tick()) /
                                                  static_cast<double>(skill_build_base_progress()));
            const auto credit =
                std::llround(static_cast<double>(b->progress) * static_cast<double>(full_credit)) -
                std::llround(static_cast<double>(before) * static_cast<double>(full_credit));
            if (credit > 0)
                s.useful_work(e, Skill::Construction, static_cast<std::uint32_t>(credit));
            a.progress = b->progress;
            if (before < 1 && b->progress >= 1) {
                s.built = true;
                if (construction)
                    construction->phase = ConstructionPhase::Complete;
                if (b->kind == BuildingKind::Bridge || b->kind == BuildingKind::Road)
                    ++s.topology_revision;
                if (b->kind == BuildingKind::Stockpile)
                    s.storages.push_back({b->id, 5000, 7, 0});
                if (b->kind == BuildingKind::Mine) {
                    const CellCoord entrance{b->x, b->y, WorldLayer::Mine};
                    s.portals.push_back({{b->x, b->y, WorldLayer::Surface}, entrance});
                    // Excavation is part of completing the shaft, never of placing its blueprint.
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const CellCoord cell{b->x + dx, b->y + dy, WorldLayer::Mine};
                            if (auto *tile = s.mutable_tile(cell)) {
                                *tile = {Terrain::MineFloor, 0, true};
                                s.changed(cell);
                            }
                        }
                    s.reveal(entrance, entrance_vision_radius,b->nation);
                    ++s.topology_revision;
                }
                s.event(std::string(building_name(b->kind)) + " completed.");
            }
        }
    }
    for (auto e : s.entities()) {
            if(s.ticks % (s.policy(s.registry.get<Identity>(e).nation).reduced_rations?
                Impl::reduced_food_interval_ticks:Impl::normal_food_interval_ticks)!=0)continue;
            auto &v = s.registry.get<Vital>(e);
            if (v.health <= 0)
                continue;
            if (s.consume_food(s.registry.get<Identity>(e).nation)) {
                v.hunger = std::max(0.f, v.hunger - .25f);
            } else {
                v.hunger = std::min(10.f, v.hunger + (winter() ? 1.5f : 1));
                if (v.hunger >= 5)
                    v.health = std::max(0, v.health - 1);
            }
        }
    s.erase_empty_stacks();
    s.reconcile_manual();
    s.clear_stale_outdoor_work();
    s.cache_dirty = true;
}
int World::housing(NationId nation) const {
    int n = 0;
    for (auto &b : impl_->buildings)
        if (b.progress >= 1 && b.nation==nation)
            n += b.kind == BuildingKind::Camp ? 10 : b.kind == BuildingKind::House ? 6 + (impl_->building_level(b.id)==2?4:0) : 0;
    return n;
}
CalendarView World::calendar() const {
    return calendar_view(impl_->calendar_tick());
}
WeatherView World::weather() const {
    return impl_->weather_cache->view(impl_->climate);
}
std::uint32_t World::day() const {
    return calendar_day(impl_->calendar_tick());
}
bool World::winter() const {
    return calendar().season == Season::Winter;
}
int World::tutorial_step() const {
    return !impl_->moved ? 0 : !impl_->gathered ? 1 : !impl_->built ? 2 : 3;
}
const std::vector<WorkIntentState> &World::work_intents() const {
    return impl_->work_intents;
}
WorkIntentDiagnostics World::work_diagnostics() const {
    auto result = impl_->work_diagnostics;
    result.outcomes = impl_->work_outcomes;
    result.active_intents = result.waiting_intents = result.pending_paths = 0;
    for (const auto &w : impl_->work_intents) {
        if (Impl::active_work(w))
            ++result.active_intents;
        if (w.phase == WorkIntentPhase::Waiting)
            ++result.waiting_intents;
        if (w.phase == WorkIntentPhase::PathPending)
            ++result.pending_paths;
    }
    return result;
}
std::vector<ConstructionWorkforce> World::construction_workforce() const {
    return impl_->construction_workforce();
}
bool migrate_legacy_work_intents(WorldState &state, std::string &error) {
    error.clear();
    if (!state.work_intents.empty() || state.next_work_intent_id != 1 || state.work_intent_cursor_id != 1 ||
        state.work_building_cursor_id != 1) {
        error = "Legacy work destination is not empty.";
        return false;
    }
    auto candidate = state;
    std::set<std::uint64_t> correlated;
    for (auto &t : candidate.logistics_tasks) {
        if (t.phase != LogisticsPhase::Complete || !t.path_batch_id)
            continue;
        auto p = std::find_if(candidate.path_batches.begin(), candidate.path_batches.end(),
                              [&](const auto &b) { return b.path_batch_id == t.path_batch_id; });
        auto r = std::find_if(candidate.residents.begin(), candidate.residents.end(),
                              [&](const auto &v) { return v.id == t.resident_id; });
        if (p == candidate.path_batches.end() || r == candidate.residents.end() ||
            !correlated.insert(t.path_batch_id).second || p->command != PathCommandKind::Logistics ||
            p->resident_ids != std::vector<int>{t.resident_id} || p->assigned_job != t.resume_job ||
            p->target != t.resume_target || !p->to_adjacent ||
            (t.resume_job != Job::Woodcut && t.resume_job != Job::Mine && t.resume_job != Job::Forage) ||
            p->enqueued_tick > std::numeric_limits<std::uint64_t>::max() - work_admission_deadline_ticks ||
            candidate.work_intents.size() >= max_work_intents) {
            error = "Legacy continuation correlation is invalid.";
            return false;
        }
        WorkIntentState w;
        w.intent_id = candidate.next_work_intent_id++;
        w.phase = WorkIntentPhase::PathPending;
        w.resident_id = t.resident_id;
        w.owner_nation = r->nation;
        w.target = t.resume_target;
        w.job = t.resume_job;
        w.expected_terrain =
            t.resume_job == Job::Woodcut ? Terrain::Forest
            : t.resume_job == Job::Mine
                ? (t.resume_target.layer == WorldLayer::Mine ? Terrain::MineOre : Terrain::Rock)
                : Terrain::Grass;
        if (w.job == Job::Mine && inside_world(w.target.x, w.target.y) && valid_layer(w.target.layer))
            for (const auto &chunk : candidate.chunks)
                if (chunk.key == chunk_key(w.target)) {
                    const auto terrain = chunk.tiles[local_index(w.target.x, w.target.y)].terrain;
                    if (terrain == Terrain::Rock || terrain == Terrain::MineOre)
                        w.expected_terrain = terrain;
                    break;
                }
        w.created_tick = p->enqueued_tick;
        w.admission_deadline_tick = p->enqueued_tick + work_admission_deadline_ticks;
        w.path_batch_id = p->path_batch_id;
        w.attempts = 1;
        w.last_attempt_topology_revision = p->search_topology_revision;
        candidate.work_intents.push_back(w);
        p->command = PathCommandKind::Work;
        t.path_batch_id = 0;
        t.resume_job = Job::Idle;
    }
    state = std::move(candidate);
    return true;
}
bool migrate_legacy_manual_orders(WorldState &state, std::string &error) {
    if (!state.manual_orders.empty() || state.next_manual_order_id != 1 ||
        state.manual_order_cursor_id != 1) {
        error = "Legacy state contains manual queue data.";
        return false;
    }
    World validator(1, 0);
    if (!validator.import_state_impl(state, error, true))
        return false;
    auto candidate = state;
    std::vector<const Resident *> residents;
    for (const auto &r : candidate.residents)
        residents.push_back(&r);
    std::sort(residents.begin(), residents.end(), [](auto a, auto b) { return a->id < b->id; });
    const auto gather_job = [](Job j) { return j == Job::Woodcut || j == Job::Mine || j == Job::Forage; };
    for (const auto *r : residents) {
        if (!r->profession.manual_override)
            continue; // Neutral legacy execution and Deferred Assign/Stop stay unchanged.
        ManualOrderState o;
        o.order_id = candidate.next_manual_order_id++;
        o.resident_id = r->id;
        o.owner_nation = r->nation;
        o.legacy_compatibility = true;
        o.request.kind = ManualOrderKind::Legacy;
        o.phase = ManualOrderPhase::Active;
        const auto t = std::find_if(
            candidate.logistics_tasks.begin(), candidate.logistics_tasks.end(), [&](const auto &task) {
                return task.resident_id == r->id && task.phase != LogisticsPhase::Complete &&
                       task.phase != LogisticsPhase::Failed;
            });
        const auto w =
            std::find_if(candidate.work_intents.begin(), candidate.work_intents.end(), [&](const auto &work) {
                return work.resident_id == r->id &&
                       (work.phase == WorkIntentPhase::Waiting || work.phase == WorkIntentPhase::PathPending);
            });
        if (t != candidate.logistics_tasks.end()) {
            o.executor = ManualExecutorKind::Logistics;
            o.executor_id = t->task_id;
            if (t->kind == LogisticsTaskKind::HaulToStorage && gather_job(t->resume_job)) {
                o.request.kind = ManualOrderKind::Gather;
                o.request.target = t->resume_target;
            }
        } else if (w != candidate.work_intents.end()) {
            o.executor = ManualExecutorKind::Work;
            o.executor_id = w->intent_id;
            if (w->kind == WorkIntentKind::GatherContinuation) {
                o.request.kind = ManualOrderKind::Gather;
                o.request.target = w->target;
            }
        } else if (r->order_phase == OrderPhase::PathPending) {
            const auto p =
                std::find_if(candidate.path_batches.begin(), candidate.path_batches.end(),
                             [&](const auto &b) { return b.path_batch_id == r->pending_path_batch; });
            o.phase = ManualOrderPhase::PathPending;
            o.executor = ManualExecutorKind::Path;
            o.executor_id = p->path_batch_id;
            if (p->command == PathCommandKind::Move || p->command == PathCommandKind::Gather ||
                (p->command == PathCommandKind::Replan &&
                 (p->assigned_job == Job::Move || gather_job(p->assigned_job)))) {
                o.request.kind =
                    p->assigned_job == Job::Move ? ManualOrderKind::Move : ManualOrderKind::Gather;
                o.request.target = p->target;
            }
        } else {
            const auto route = std::find_if(candidate.routes.begin(), candidate.routes.end(),
                                            [&](const auto &value) { return value.resident_id == r->id; });
            o.executor = ManualExecutorKind::Route;
            if (route != candidate.routes.end() && route->work &&
                (r->job == Job::Move || gather_job(r->job))) {
                o.request.kind = r->job == Job::Move ? ManualOrderKind::Move : ManualOrderKind::Gather;
                o.request.target = route->work;
            }
        }
        candidate.manual_orders.push_back(o);
    }
    state = std::move(candidate);
    return true;
}
WorldState World::export_state() const { return export_state_impl(true); }
WorldState World::export_state_impl(bool include_chunks) const {
    auto &s = *impl_;
    WorldState out;
    out.world_id = s.world_id;
    out.seed = s.seed;
    out.terrain_generator = s.terrain_generator;
    out.ticks = s.ticks;
    out.climate = s.climate;
    if (!out.climate.outdoor_work_remainders.empty())
        throw std::logic_error("Live climate must not duplicate Activity work remainders");
    out.next_building_id = s.next_building_id;
    out.has_moved = s.moved;
    out.has_gathered = s.gathered;
    out.has_built = s.built;
    out.resources = resources();
    if (include_chunks) out.chunks = s.chunks;
    out.residents = residents();
    out.settlement = s.settlement;
    out.economy = s.economy;
    out.server_metadata=s.server_metadata;
    out.buildings = s.buildings;
    std::sort(out.buildings.begin(), out.buildings.end(),
              [](const Building &a, const Building &b) { return a.id < b.id; });
    out.portals = s.portals;
    std::sort(out.portals.begin(), out.portals.end(), [](const Portal &a, const Portal &b) {
        return std::tie(a.surface.x, a.surface.y, a.mine.x, a.mine.y) <
               std::tie(b.surface.x, b.surface.y, b.mine.x, b.mine.y);
    });
    out.events = s.events;
    out.next_path_batch_id = s.next_path_batch_id;
    out.topology_revision = s.topology_revision;
    out.path_batches = s.path_batches;
    out.path_results = s.path_results;
    out.next_item_stack_id = s.next_item_stack_id;
    out.next_item_reservation_id = s.next_item_reservation_id;
    out.next_logistics_task_id = s.next_logistics_task_id;
    out.item_stacks = s.item_stacks;
    out.storages = s.storages;
    out.item_reservations = s.item_reservations;
    out.logistics_tasks = s.logistics_tasks;
    out.construction_orders = s.construction_orders;
    out.next_work_intent_id = s.next_work_intent_id;
    out.work_intent_cursor_id = s.work_intent_cursor_id;
    out.work_building_cursor_id = s.work_building_cursor_id;
    out.work_intents = s.work_intents;
    out.profession_cursor_id = s.profession_cursor_id;
    out.next_manual_order_id = s.next_manual_order_id;
    out.manual_order_cursor_id = s.manual_order_cursor_id;
    out.manual_orders = s.manual_orders;
    for (auto e : s.entities()) {
        auto &i = s.registry.get<Identity>(e);
        auto &r = s.registry.get<Route>(e);
        out.routes.push_back({i.id, r.points, r.cursor, r.work});
        const auto& accumulator = s.registry.get<Activity>(e).outdoor_work;
        if (accumulator) {
            const auto key = s.outdoor_work_key(e);
            if (!key || !Impl::same_outdoor_key(*accumulator,*key) || accumulator->remainder_bp >= 10000u)
                throw std::logic_error("Live outdoor work remainder has a stale key");
            out.climate.outdoor_work_remainders.push_back(*accumulator);
        }
    }
    return out;
}
bool World::migrate_legacy_inventory(WorldState &state, std::string &error) {
    error.clear();
    if (!state.item_stacks.empty() || !state.storages.empty() || !state.item_reservations.empty() ||
        !state.logistics_tasks.empty() || !state.construction_orders.empty() || state.resources.wood < 0 ||
        state.resources.stone < 0 || state.resources.food < 0) {
        error = "Legacy inventory input is not empty or has negative resources.";
        return false;
    }
    Impl migrated;
    migrated.buildings = state.buildings;
    migrated.ticks = state.ticks;
    int camp_id = 0;
    const std::uint64_t total = static_cast<std::uint64_t>(state.resources.wood) +
                                static_cast<std::uint64_t>(state.resources.stone) +
                                static_cast<std::uint64_t>(state.resources.food);
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        error = "Legacy camp capacity cannot be represented.";
        return false;
    }
    for (const auto &b : state.buildings) {
        if (b.id <= 0 || !inside_world(b.x, b.y) || !valid_layer(b.layer) || !b.nation ||
            !valid_building_kind(b.kind) || !std::isfinite(b.progress) || b.progress < 0 ||
            b.progress > 1) {
            error = "Legacy building metadata is invalid.";
            return false;
        }
        if (b.progress == 1 && (b.kind == BuildingKind::Camp || b.kind == BuildingKind::Stockpile)) {
            const auto capacity = b.kind == BuildingKind::Camp
                                      ? std::max<std::uint32_t>(10000, static_cast<std::uint32_t>(total))
                                      : 5000U;
            migrated.storages.push_back({b.id, capacity, 7, 0});
            if (b.kind == BuildingKind::Camp && b.nation == 1 && (camp_id == 0 || b.id < camp_id))
                camp_id = b.id;
        }
        if (b.progress < 1) {
            if (b.kind == BuildingKind::Camp) {
                error = "Legacy unfinished camp cannot be represented.";
                return false;
            }
            migrated.construction_orders.push_back({b.id,
                                                    b.kind,
                                                    {b.x, b.y, b.layer},
                                                    b.nation,
                                                    cost(b.kind),
                                                    cost(b.kind),
                                                    ConstructionPhase::Work,
                                                    0});
        }
    }
    if (total && camp_id == 0) {
        error = "Legacy resources have no owned completed camp.";
        return false;
    }
    for (auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food})
        if (Impl::amount(state.resources, item) > 0)
            migrated.add_stack(item, static_cast<std::uint32_t>(Impl::amount(state.resources, item)), 1,
                               {ItemLocationKind::Storage, {}, camp_id});
    for (const auto &batch : state.path_batches) {
        if (batch.command != PathCommandKind::Build)
            continue;
        if (!inside_world(batch.target.x, batch.target.y) || !valid_layer(batch.target.layer) ||
            !batch.build_kind || batch.reserved_cost != cost(*batch.build_kind) ||
            !migrated.reserve_construction({batch.building_id,
                                            *batch.build_kind,
                                            batch.target,
                                            1,
                                            batch.reserved_cost,
                                            {0, 0, 0},
                                            ConstructionPhase::Planned,
                                            batch.path_batch_id})) {
            error = "Legacy pending construction cannot reserve its physical materials.";
            return false;
        }
    }
    state.next_item_stack_id = migrated.next_item_stack_id;
    state.next_item_reservation_id = migrated.next_item_reservation_id;
    state.next_logistics_task_id = migrated.next_logistics_task_id;
    state.item_stacks = std::move(migrated.item_stacks);
    state.storages = std::move(migrated.storages);
    state.item_reservations = std::move(migrated.item_reservations);
    state.logistics_tasks = std::move(migrated.logistics_tasks);
    state.construction_orders = std::move(migrated.construction_orders);
    return true;
}
bool World::import_state(WorldState state, std::string &error) {
    return import_state_impl(std::move(state), error, false);
}
bool World::import_state_impl(WorldState state, std::string &error, bool legacy_validation, bool enforce_alpha) {
    constexpr int maximum_experience = 1'000'000'000;
    auto fail = [&](std::string m) {
        error = std::move(m);
        return false;
    };
    error.clear();
    if (std::all_of(state.world_id.begin(), state.world_id.end(), [](auto v) { return v == 0; }))
        return fail("World id is invalid.");
    if (state.terrain_generator == 0 || state.terrain_generator > generator_version)
        return fail("Terrain generator profile is invalid.");
    const WeatherContext climate_context{state.seed,{state.climate.algorithm_version,
        state.climate.climate_profile,state.climate.scenario}};
    std::optional<WeatherScheduleCache> climate_cache;
    CalendarTick state_calendar;
    try {
        validate_weather_context(climate_context);
        const auto calendar = make_calendar_tick(state.ticks,state.climate.calendar_offset_ticks);
        if (!calendar) return fail("Climate calendar addition overflow.");
        state_calendar = *calendar;
        climate_cache.emplace(climate_context,state_calendar);
        validate_climate_runtime(state.climate,state.ticks,climate_context,*climate_cache);
        if (state.ticks == 0u && state.climate != initialize_climate(climate_context,*climate_cache))
            return fail("Initial climate cannot contain earned history.");
    } catch (const std::exception& e) {
        return fail(std::string("Invalid climate state: ") + e.what());
    }
    if (state.chunks.size() == 128) {
        for (auto &batch : state.path_batches) batch.legacy_grid = true;
        std::set<ChunkKey> legacy_keys;
        for (const auto &chunk : state.chunks)
            if (!valid_layer(chunk.key.layer) || chunk.key.cx < -3 || chunk.key.cx > 4 ||
                chunk.key.cy < -3 || chunk.key.cy > 4 || !legacy_keys.insert(chunk.key).second)
                return fail("Legacy chunk grid is invalid.");
        World extension(state.seed, 0, state.terrain_generator,climate_context.config);
        for (const auto &chunk : extension.chunks())
            if (!legacy_keys.contains(chunk.key)) state.chunks.push_back(chunk);
    }
    if (state.chunks.size() != world_chunk_count || state.resources.wood < 0 || state.resources.stone < 0 ||
        state.resources.food < 0 || state.ticks == std::numeric_limits<std::uint64_t>::max())
        return fail("World grid or resources are invalid.");
    if(state.settlement.reduced_rations!=state.economy.wilderness.reduced_rations)return fail("Ration policy mismatch.");
    if (!valid_economy(state.economy)) return fail("Invalid economy state.");
    if (!legacy_validation && enforce_alpha) {
        const auto unsupported_policy=[&](const SettlementState& policy) {
            return policy.reduced_rations || (!alpha::forced_work &&
                (policy.night_shift(state_calendar) || std::any_of(policy.young.begin(),policy.young.end(),
                    [&](const auto& y){return y.forced_work&&state.ticks<y.adult_tick;})));
        };
        if (unsupported_policy(state.settlement) || std::any_of(state.economy.nations.begin(),state.economy.nations.end(),
                [&](const auto& n){return unsupported_policy(n.policy);})) return fail("alpha.save_policy");
        if (std::any_of(state.economy.wilderness.expeditions.begin(),state.economy.wilderness.expeditions.end(),expedition_active))
            return fail("alpha.save_expedition");
        if (std::any_of(state.economy.wilderness.animals.begin(),state.economy.wilderness.animals.end(),
                [](const auto& a){return a.behaviour==AnimalBehaviour::Attacking;})) return fail("alpha.save_attack");
        if (std::any_of(state.economy.orders.begin(),state.economy.orders.end(),
                [](const auto& o){return production_active(o)&&!o.upgrade&&!alpha::recipe_available(o.recipe);})) return fail("alpha.save_production");
        if (std::any_of(state.buildings.begin(),state.buildings.end(),[](const auto& b){return b.progress<1&&!alpha::buildable(b.kind);}) ||
            std::any_of(state.path_batches.begin(),state.path_batches.end(),[](const auto& b){return b.build_kind&&!alpha::buildable(*b.build_kind);}) ||
            std::any_of(state.construction_orders.begin(),state.construction_orders.end(),[](const auto& o){return o.phase<ConstructionPhase::Complete&&!alpha::buildable(o.kind);}))
            return fail("alpha.save_building");
    }
    for(const auto&e:state.economy.wilderness.expeditions){
        if(e.night_permission>calendar_day(state_calendar)||e.night_prompt>calendar_day(state_calendar))return fail("Invalid expedition night.");
        for(int id:e.members){
            if(std::none_of(state.residents.begin(),state.residents.end(),[&](const auto&r){return r.id==id;}))return fail("Missing expedition member.");
            if(expedition_active(e))for(const auto&o:state.economy.orders)if(production_active(o)&&o.resident_id==id)return fail("Overlapping work assignment.");
        }
    }
    for(const auto&o:state.economy.orders) {
        if(std::none_of(state.buildings.begin(),state.buildings.end(),[&](const auto&b){return b.id==o.building_id;}) || std::none_of(state.residents.begin(),state.residents.end(),[&](const auto&r){return r.id==o.resident_id;}))return fail("Missing production actor or building.");
    }
    for(const auto&o:state.economy.orders){
        const auto b=std::find_if(state.buildings.begin(),state.buildings.end(),[&](const auto&v){return v.id==o.building_id;});
        const auto r=std::find_if(state.residents.begin(),state.residents.end(),[&](const auto&v){return v.id==o.resident_id;});
        if(b->nation!=r->nation||b->progress!=1)return fail("Invalid production ownership.");
        if(o.upgrade){if(o.requested!=1||(b->kind!=BuildingKind::House&&b->kind!=BuildingKind::Stockpile&&b->kind!=BuildingKind::Workshop))return fail("Invalid upgrade recipe.");}
        else if(int(b->kind)!=recipes[o.recipe].building||o.work>recipes[o.recipe].ticks)return fail("Production recipe does not match building.");
        if(o.logistics_id){const auto t=std::find_if(state.logistics_tasks.begin(),state.logistics_tasks.end(),[&](const auto&v){return v.task_id==o.logistics_id;});
            if(t==state.logistics_tasks.end()||t->resident_id!=o.resident_id||t->kind!=LogisticsTaskKind::HaulToStorage)return fail("Production delivery is invalid.");}
    }
    for(const auto&u:state.economy.upgrades){
        const auto b=std::find_if(state.buildings.begin(),state.buildings.end(),[&](const auto&v){return v.id==u.building_id;});
        if(b==state.buildings.end()||b->progress!=1||(b->kind!=BuildingKind::House&&b->kind!=BuildingKind::Stockpile&&b->kind!=BuildingKind::Workshop))return fail("Invalid upgraded building.");
    }
    for(const auto&n:state.economy.nations)if(!valid_settlement(n.policy,state.ticks,state_calendar))return fail("Invalid nation policy.");
    if (!valid_settlement(state.settlement,state.ticks,state_calendar)) return fail("Invalid settlement state.");
    for(const auto &y:state.settlement.young)
        if(std::none_of(state.residents.begin(),state.residents.end(),[&](const auto &r){return r.id==y.resident_id && r.nation==1;}))
            return fail("Young resident is missing or foreign.");
    if(state.settlement.arrival_tick && (state.residents.size()>=1500 ||
       std::any_of(state.residents.begin(),state.residents.end(),[](const auto &r){return r.id>=INT_MAX-1;})))
        return fail("Pending population exceeds capacity.");
    std::set<ChunkKey> keys;
    for (auto &c : state.chunks) {
        if (!valid_layer(c.key.layer) || !valid_chunk_position(c.key.cx, c.key.cy) ||
            !keys.insert(c.key).second)
            return fail("Chunk key is invalid.");
        for (auto &t : c.tiles)
            if ((int)t.terrain < 0 || (int)t.terrain > 5 || static_cast<unsigned>(t.biome) > 8 || t.amount < 0 || t.amount > 1'000'000 || t.elevation < -32 || t.elevation > 512 ||
                (c.key.layer == WorldLayer::Mine && (t.elevation != 0 || t.trail_wear != 0)))
                return fail("Tile data is invalid.");
    }
    std::set<int> ids;
    std::map<NationId, std::size_t> nation_populations;
    for (auto &r : state.residents)
        if (r.id <= 0 || !ids.insert(r.id).second || !valid_utf8_text(r.name, 128) || r.nation == 0 ||
            !valid_layer(r.layer) || !std::isfinite(r.x) || !std::isfinite(r.y) || r.x < world_min ||
            r.x >= world_max || r.y < world_min || r.y >= world_max ||
            !inside_world(r.target_x, r.target_y) || static_cast<unsigned>(r.job) > static_cast<unsigned>(Job::Farm) ||
            !std::isfinite(r.progress) || r.progress < 0 || r.progress > 1 || r.health < 0 ||
            r.health > 100 || !std::isfinite(r.hunger) || r.hunger < 0 || r.hunger > 10 || r.wood_xp < 0 ||
            r.wood_xp > maximum_experience || r.mining_xp < 0 || r.mining_xp > maximum_experience ||
            r.farming_xp < 0 || r.farming_xp > maximum_experience ||
            static_cast<unsigned>(r.order_phase) > static_cast<unsigned>(OrderPhase::PathFailed) ||
            static_cast<unsigned>(r.path_failure) > static_cast<unsigned>(PathFailure::ReservationConflict) ||
            (r.order_phase == OrderPhase::PathPending) != (r.pending_path_batch != 0) ||
            (r.order_phase == OrderPhase::PathFailed) != (r.path_failure != PathFailure::None))
            return fail("Resident data is invalid.");
        else if (++nation_populations[r.nation] > 1500 || state.residents.size() > 6000)
            return fail("Nation population is invalid.");

    if (state.portals.size() > 64)
        return fail("Portal count is invalid.");
    std::set<std::tuple<int, int>> surface_portals, mine_portals;
    for (const auto &portal : state.portals) {
        if (portal.surface.layer != WorldLayer::Surface || portal.mine.layer != WorldLayer::Mine ||
            !inside_world(portal.surface.x, portal.surface.y) ||
            !inside_world(portal.mine.x, portal.mine.y) ||
            !surface_portals.insert({portal.surface.x, portal.surface.y}).second ||
            !mine_portals.insert({portal.mine.x, portal.mine.y}).second)
            return fail("Portal data is invalid.");
    }

    for (const auto &building : state.buildings) {
        if (building.kind != BuildingKind::Mine) continue;
        const auto passage = std::find_if(state.portals.begin(), state.portals.end(), [&](const auto &p) {
            return p.surface == CellCoord{building.x, building.y, WorldLayer::Surface};
        });
        if ((building.progress >= 1) != (passage != state.portals.end()) ||
            (passage != state.portals.end() && passage->mine != CellCoord{building.x, building.y, WorldLayer::Mine}))
            return fail("Mine completion and passage disagree.");
    }

    if (state.routes.size() != state.residents.size())
        return fail("Route count is invalid.");
    const auto state_tile_at = [&](CellCoord cell) -> const Tile * {
        const auto key = chunk_key(cell);
        const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                        [&](const Chunk &value) { return value.key == key; });
        return chunk == state.chunks.end() ? nullptr : &chunk->tiles[local_index(cell.x, cell.y)];
    };
    const auto state_passage_at = [&](CellCoord cell) {
        return std::any_of(state.buildings.begin(), state.buildings.end(), [&](const Building &building) {
            return building.x == cell.x && building.y == cell.y && building.layer == cell.layer &&
                   building_passage(building.kind, building.progress);
        });
    };
    const auto legal_step = [&](CellCoord first, CellCoord second) {
        if (adjacent(first, second)) {
            if (state.terrain_generator < surface_rules_generator_version || first.layer == WorldLayer::Mine) return true;
            const auto *from = state_tile_at(first);
            const auto *to = state_tile_at(second);
            return from && to && surface_edge_traversable(*from, *to, state_passage_at(first),
                                                          state_passage_at(second));
        }
        return std::any_of(state.portals.begin(), state.portals.end(), [&](const Portal &portal) {
            return (portal.surface == first && portal.mine == second) ||
                   (portal.mine == first && portal.surface == second);
        });
    };
    std::map<int, const Resident *> resident_by_id;
    for (const auto &resident : state.residents)
        resident_by_id.emplace(resident.id, &resident);
    std::set<int> route_ids;
    for (auto &r : state.routes) {
        if (!ids.contains(r.resident_id) || !route_ids.insert(r.resident_id).second ||
            r.cursor > r.points.size())
            return fail("Route metadata is invalid.");
        if (r.work && (!inside_world(r.work->x, r.work->y) || !valid_layer(r.work->layer)))
            return fail("Route work cell is invalid.");
        for (size_t i = 0; i < r.points.size(); ++i) {
            auto c = r.points[i];
            if (!inside_world(c.x, c.y) || !valid_layer(c.layer))
                return fail("Route point is invalid.");
            if (i && !legal_step(r.points[i - 1], c))
                return fail("Route seam is invalid.");
        }
        const Resident &resident = *resident_by_id.at(r.resident_id);
        if (r.points.empty())
            continue;
        if (r.cursor == r.points.size()) {
            const auto last = r.points.back();
            if (resident.layer != last.layer || resident.x != static_cast<float>(last.x) ||
                resident.y != static_cast<float>(last.y))
                return fail("Exhausted route position is invalid.");
        } else if (r.cursor == 0) {
            const auto rounded_x = std::lround(resident.x);
            const auto rounded_y = std::lround(resident.y);
            const CellCoord rounded{static_cast<int>(rounded_x), static_cast<int>(rounded_y), resident.layer};
            if (rounded_x < world_min || rounded_x >= world_max || rounded_y < world_min ||
                rounded_y >= world_max ||
                (rounded != r.points.front() && !legal_step(rounded, r.points.front()))) {
                const auto *from = state_tile_at(rounded);
                const auto *to = state_tile_at(r.points.front());
                const auto nation = std::find_if(state.economy.nations.begin(), state.economy.nations.end(),
                                                 [&](const NationState &item) {
                                                     return item.id == static_cast<int>(resident.nation);
                                                 });
                return fail("Route start is discontinuous for resident " +
                            std::to_string(r.resident_id) + " at " +
                            std::to_string(rounded.x) + "," + std::to_string(rounded.y) +
                            " before " + std::to_string(r.points.front().x) + "," +
                            std::to_string(r.points.front().y) + " (terrain " +
                            (from ? std::to_string(static_cast<int>(from->terrain)) : "missing") +
                            "/" + (to ? std::to_string(static_cast<int>(to->terrain)) : "missing") +
                            ", elevation " + (from ? std::to_string(from->elevation) : "missing") +
                            "/" + (to ? std::to_string(to->elevation) : "missing") +
                            ", nation " + std::to_string(resident.nation) + " at " +
                            (nation != state.economy.nations.end() ? std::to_string(nation->x) : "missing") +
                            "," + (nation != state.economy.nations.end() ? std::to_string(nation->y) : "missing") +
                            ").");
            }
        } else {
            const CellCoord previous = r.points[r.cursor - 1];
            const CellCoord next = r.points[r.cursor];
            if (previous.layer != next.layer) {
                if (!legal_step(previous, next) || resident.layer != previous.layer ||
                    resident.x != static_cast<float>(previous.x) ||
                    resident.y != static_cast<float>(previous.y))
                    return fail("Route portal progress is discontinuous.");
            } else if (resident.layer != previous.layer ||
                       (previous.x == next.x &&
                        (resident.x != static_cast<float>(previous.x) ||
                         resident.y < static_cast<float>(std::min(previous.y, next.y)) ||
                         resident.y > static_cast<float>(std::max(previous.y, next.y)))) ||
                       (previous.y == next.y &&
                        (resident.y != static_cast<float>(previous.y) ||
                         resident.x < static_cast<float>(std::min(previous.x, next.x)) ||
                         resident.x > static_cast<float>(std::max(previous.x, next.x)))) ||
                       (previous.x != next.x && previous.y != next.y)) {
                return fail("Route progress is discontinuous.");
            }
        }
    }
    std::set<int> bids;
    std::set<std::tuple<int, int, WorldLayer>> bcells;
    int maxid = 0;
    if (state.buildings.size() > static_cast<std::size_t>(world_size * world_size))
        return fail("Building count is invalid.");
    for (auto &b : state.buildings) {
        if (b.id <= 0 || !bids.insert(b.id).second || b.nation == 0 || !inside_world(b.x, b.y) ||
            !valid_building_kind(b.kind) || !valid_layer(b.layer) || !std::isfinite(b.progress) ||
            b.progress < 0 || b.progress > 1 || !bcells.insert({b.x, b.y, b.layer}).second)
            return fail("Building data is invalid.");
        if (b.kind==BuildingKind::Bridge) {
            const auto key=chunk_key({b.x,b.y,b.layer});
            const auto chunk=std::find_if(state.chunks.begin(),state.chunks.end(),[&](const auto &c){return c.key==key;});
            if(b.layer!=WorldLayer::Surface || chunk==state.chunks.end() ||
               chunk->tiles[local_index(b.x,b.y)].terrain!=Terrain::Water)
                return fail("Bridge is not on surface water.");
        }
        maxid = std::max(maxid, b.id);
    }
    if (state.next_building_id <= maxid || state.next_building_id == std::numeric_limits<int>::max() ||
        state.events.size() > 64 ||
        std::any_of(state.events.begin(), state.events.end(),
                    [](const std::string &event) { return !valid_utf8_text(event, 512, true); }))
        return fail("Counters or events are invalid.");

    if (state.next_path_batch_id == 0 ||
        state.path_batches.size() + state.path_results.size() > max_path_batches)
        return fail("Path planner counters are invalid.");
    std::set<std::uint64_t> path_ids;
    std::set<int> reserved_residents;
    std::set<std::tuple<int, int, WorldLayer>> reserved_sites;
    std::set<int> reserved_building_ids;
    std::int64_t reserved_wood = 0, reserved_stone = 0, reserved_food = 0;
    std::size_t planner_memory = state.path_results.size() * legacy_path_outcome_logical_bytes;
    std::size_t staged_total = 0;
    std::uint64_t previous_batch_id = 0;
    const auto state_walkable = [&](CellCoord cell) {
        if (!inside_world(cell.x, cell.y) || !valid_layer(cell.layer))
            return false;
        const auto key = chunk_key(cell);
        const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                        [&](const Chunk &value) { return value.key == key; });
        if (chunk == state.chunks.end())
            return false;
        const auto terrain = chunk->tiles[local_index(cell.x, cell.y)].terrain;
        if (bcells.contains({cell.x, cell.y, cell.layer}))
            return std::any_of(state.buildings.begin(),state.buildings.end(),[&](const auto &b){
                return b.x==cell.x && b.y==cell.y && b.layer==cell.layer && building_passage(b.kind,b.progress);
            });
        return terrain != Terrain::Water && terrain != Terrain::MineOre;
    };
    const auto expected_roots = [&](CellCoord target, bool to_adjacent) {
        std::vector<CellCoord> result;
        if (!to_adjacent) {
            if (state_walkable(target))
                result.push_back(target);
            return result;
        }
        constexpr int directions[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        for (const auto &direction : directions) {
            CellCoord cell{target.x + direction[0], target.y + direction[1], target.layer};
            if (state_walkable(cell))
                result.push_back(cell);
        }
        return result;
    };
    for (std::size_t batch_index = 0; batch_index < state.path_batches.size(); ++batch_index) {
        const auto &batch = state.path_batches[batch_index];
        if (batch.path_batch_id == 0 || batch.path_batch_id <= previous_batch_id ||
            batch.path_batch_id >= state.next_path_batch_id || !path_ids.insert(batch.path_batch_id).second ||
            static_cast<unsigned>(batch.command) >
                static_cast<unsigned>(legacy_validation ? PathCommandKind::Work : PathCommandKind::Manual) ||
            batch.legacy_atomic != (batch.command != PathCommandKind::Manual) ||
            static_cast<unsigned>(batch.phase) > static_cast<unsigned>(PathPlannerPhase::Stage) ||
            !inside_world(batch.target.x, batch.target.y) || !valid_layer(batch.target.layer) ||
            batch.resident_ids.size() > max_path_batch_residents ||
            (batch.resident_ids.empty() && batch.command != PathCommandKind::Build) ||
            !std::is_sorted(batch.resident_ids.begin(), batch.resident_ids.end()) ||
            std::adjacent_find(batch.resident_ids.begin(), batch.resident_ids.end()) !=
                batch.resident_ids.end() ||
            batch.drafts.size() != batch.resident_ids.size() || batch.enqueued_tick > state.ticks ||
            batch.enqueued_tick > std::numeric_limits<std::uint64_t>::max() - 1200 ||
            batch.deadline_tick != batch.enqueued_tick + 1200 ||
            batch.search_topology_revision > state.topology_revision || batch.restart_count > 3 ||
            batch.frontier_cursor > batch.frontier.size() ||
            batch.reconstruction_resident > batch.drafts.size() || batch.stage_cursor > batch.drafts.size() ||
            batch.roots.size() > 4 || batch.frontier.size() > max_search_cells ||
            batch.nodes.size() > max_search_cells || batch.reserved_cost.wood < 0 ||
            batch.reserved_cost.stone < 0 || batch.reserved_cost.food < 0 ||
            (batch_index > 0 && batch.phase != PathPlannerPhase::Queued))
            return fail("Path batch metadata is invalid.");
        const bool command_job_valid =
            (batch.command == PathCommandKind::Move && batch.assigned_job == Job::Move &&
             !batch.to_adjacent) ||
            (batch.command == PathCommandKind::Gather &&
             (batch.assigned_job == Job::Woodcut || batch.assigned_job == Job::Mine ||
              batch.assigned_job == Job::Forage) &&
             batch.to_adjacent) ||
            (batch.command == PathCommandKind::Build && batch.assigned_job == Job::Build &&
             batch.to_adjacent) ||
            (batch.command == PathCommandKind::Replan && batch.assigned_job != Job::Idle &&
             batch.to_adjacent == (batch.assigned_job != Job::Move)) ||
            (batch.command == PathCommandKind::Logistics &&
             (batch.assigned_job == Job::Haul ||
              ((batch.assigned_job == Job::Woodcut || batch.assigned_job == Job::Mine ||
                batch.assigned_job == Job::Forage) &&
               batch.to_adjacent))) ||
            (batch.command == PathCommandKind::Work && batch.resident_ids.size() == 1 && batch.to_adjacent &&
             (batch.assigned_job == Job::Woodcut || batch.assigned_job == Job::Mine ||
              batch.assigned_job == Job::Forage || batch.assigned_job == Job::Build || batch.assigned_job == Job::Farm)) ||
            (batch.command == PathCommandKind::Manual &&
             ((batch.assigned_job == Job::Move && !batch.to_adjacent) ||
              ((batch.assigned_job == Job::Woodcut || batch.assigned_job == Job::Mine ||
                batch.assigned_job == Job::Forage) &&
               batch.to_adjacent)));
        if (!command_job_valid)
            return fail("Path command job is invalid.");
        previous_batch_id = batch.path_batch_id;
        planner_memory +=
            (batch.legacy_atomic ? legacy_path_batch_logical_bytes : sizeof(PathBatchState)) +
            batch.resident_ids.size() * sizeof(int) + batch.roots.size() * sizeof(CellCoord) +
            (batch.search_storage_reserved ? max_search_cells * sizeof(CellCoord)
                                           : batch.frontier.size() * sizeof(CellCoord)) +
            (batch.search_storage_reserved ? max_search_cells * sizeof(PathSearchNodeState)
                                           : batch.nodes.size() * sizeof(PathSearchNodeState)) +
            batch.nodes.size() * 40 +
            batch.drafts.size() *
                ((batch.legacy_atomic ? legacy_path_draft_logical_bytes : sizeof(PathRouteDraftState)) + 40);

        if (batch.command == PathCommandKind::Build) {
            if (!batch.build_kind || static_cast<unsigned>(*batch.build_kind) < 1 ||
                !valid_building_kind(*batch.build_kind) || batch.target.layer != WorldLayer::Surface ||
                batch.building_id <= 0 || batch.building_id >= state.next_building_id ||
                batch.reserved_cost != cost(*batch.build_kind) || bids.contains(batch.building_id) ||
                !reserved_building_ids.insert(batch.building_id).second ||
                bcells.contains({batch.target.x, batch.target.y, batch.target.layer}) ||
                !reserved_sites.insert({batch.target.x, batch.target.y, batch.target.layer}).second)
                return fail("Path build reservation is invalid.");
            reserved_wood += batch.reserved_cost.wood;
            reserved_stone += batch.reserved_cost.stone;
            reserved_food += batch.reserved_cost.food;
        } else if (batch.build_kind || batch.building_id != 0 || batch.reserved_cost != Resources{0, 0, 0}) {
            return fail("Non-build path reservation is invalid.");
        }

        std::set<CellCoord, decltype([](const CellCoord &a, const CellCoord &b) {
                     return std::tie(a.layer, a.x, a.y) < std::tie(b.layer, b.x, b.y);
                 })>
            roots;
        for (const auto &root : batch.roots)
            if (!inside_world(root.x, root.y) || !valid_layer(root.layer) || !roots.insert(root).second)
                return fail("Path roots are invalid.");
        if ((!batch.to_adjacent && std::any_of(batch.roots.begin(), batch.roots.end(),
                                               [&](CellCoord root) { return root != batch.target; })) ||
            (batch.to_adjacent &&
             std::any_of(batch.roots.begin(), batch.roots.end(),
                         [&](CellCoord root) { return !adjacent(root, batch.target); })) ||
            (batch.search_topology_revision == state.topology_revision &&
             batch.roots != expected_roots(batch.target, batch.to_adjacent)))
            return fail("Path roots do not match the target.");
        std::set<int> discovered;
        std::unordered_map<int, CellCoord> node_next;
        std::unordered_map<int, std::size_t> node_order;
        for (std::size_t i = 0; i < batch.nodes.size(); ++i) {
            const auto &node = batch.nodes[i];
            if (!inside_world(node.cell.x, node.cell.y) || !valid_layer(node.cell.layer) ||
                !inside_world(node.next.x, node.next.y) || !valid_layer(node.next.layer) ||
                !discovered.insert(Impl::encode_cell(node.cell)).second ||
                (node.cell != node.next &&
                 (!legal_step(node.cell, node.next) || !discovered.contains(Impl::encode_cell(node.next)))) ||
                (node.cell == node.next && !roots.contains(node.cell)))
                return fail("Path search node is invalid.");
            node_next.emplace(Impl::encode_cell(node.cell), node.next);
            node_order.emplace(Impl::encode_cell(node.cell), i);
        }
        std::size_t previous_frontier_node = 0;
        bool first_frontier = true;
        for (const auto &cell : batch.frontier) {
            const auto node = node_order.find(Impl::encode_cell(cell));
            if (node == node_order.end())
                return fail("Path frontier is invalid.");
            const auto index = node->second;
            if ((!first_frontier && index <= previous_frontier_node) ||
                (batch.search_topology_revision == state.topology_revision && !state_walkable(cell)))
                return fail("Path frontier order is invalid.");
            first_frontier = false;
            previous_frontier_node = index;
        }
        if ((batch.phase == PathPlannerPhase::Queued && batch.nodes.size() > batch.roots.size()) ||
            (batch.phase != PathPlannerPhase::Queued && !batch.roots.empty() &&
             !batch.search_storage_reserved) ||
            (batch.phase == PathPlannerPhase::Search && batch.nodes.size() < batch.roots.size()))
            return fail("Path search phase is invalid.");

        for (std::size_t i = 0; i < batch.drafts.size(); ++i) {
            const auto &draft = batch.drafts[i];
            if (draft.resident_id != batch.resident_ids[i] || !ids.contains(draft.resident_id) ||
                (!draft.resolved && !reserved_residents.insert(draft.resident_id).second) ||
                (batch.legacy_atomic &&
                 (draft.manual_order_id || draft.resolved || draft.failure != PathFailure::None)) ||
                static_cast<unsigned>(draft.failure) >
                    static_cast<unsigned>(PathFailure::ReservationConflict) ||
                !inside_world(draft.start.x, draft.start.y) || !valid_layer(draft.start.layer) ||
                !std::isfinite(draft.start_x) || !std::isfinite(draft.start_y) || draft.start_x < world_min ||
                draft.start_x >= world_max || draft.start_y < world_min || draft.start_y >= world_max ||
                draft.staged.size() > max_path_cells ||
                draft.start.x != static_cast<int>(std::lround(draft.start_x)) ||
                draft.start.y != static_cast<int>(std::lround(draft.start_y)) ||
                (draft.reconstruction_cursor &&
                 (!inside_world(draft.reconstruction_cursor->x, draft.reconstruction_cursor->y) ||
                  !valid_layer(draft.reconstruction_cursor->layer))))
                return fail("Path route draft is invalid.");
            const auto *resident = resident_by_id.at(draft.resident_id);
            if (!draft.resolved &&
                (resident->order_phase != OrderPhase::PathPending ||
                 resident->pending_path_batch != batch.path_batch_id || resident->x != draft.start_x ||
                 resident->y != draft.start_y || resident->layer != draft.start.layer))
                return fail("Path resident reservation is invalid.");
            CellCoord previous = draft.start;
            for (const auto &cell : draft.staged) {
                if (!inside_world(cell.x, cell.y) || !valid_layer(cell.layer) || !legal_step(previous, cell))
                    return fail("Staged route is invalid.");
                const auto parent = node_next.find(Impl::encode_cell(previous));
                if (parent == node_next.end() || parent->second != cell)
                    return fail("Staged route does not follow the search tree.");
                previous = cell;
            }
            if (!draft.reconstruction_cursor || *draft.reconstruction_cursor != previous ||
                ((batch.phase == PathPlannerPhase::Queued || batch.phase == PathPlannerPhase::Search) &&
                 (!draft.staged.empty() || *draft.reconstruction_cursor != draft.start)) ||
                (!draft.resolved &&
                 ((batch.phase == PathPlannerPhase::Reconstruct && i < batch.reconstruction_resident) ||
                  batch.phase == PathPlannerPhase::Stage) &&
                 !roots.contains(previous)) ||
                (batch.phase == PathPlannerPhase::Reconstruct && i > batch.reconstruction_resident &&
                 !draft.staged.empty()) ||
                (!draft.resolved &&
                 ((batch.phase == PathPlannerPhase::Reconstruct && i <= batch.reconstruction_resident) ||
                  batch.phase == PathPlannerPhase::Stage) &&
                 !draft.storage_reserved))
                return fail("Path reconstruction phase is invalid.");
            staged_total += draft.staged.size();
            planner_memory +=
                (draft.storage_reserved ? max_path_cells : draft.staged.size()) * sizeof(CellCoord);
        }
        if (((batch.phase == PathPlannerPhase::Queued || batch.phase == PathPlannerPhase::Search) &&
             (batch.reconstruction_resident != 0 || batch.stage_cursor != 0)) ||
            (batch.phase == PathPlannerPhase::Reconstruct && batch.stage_cursor != 0) ||
            (batch.phase == PathPlannerPhase::Stage && batch.reconstruction_resident != batch.drafts.size()))
            return fail("Path phase cursors are invalid.");
    }
    if (reserved_wood > state.resources.wood || reserved_stone > state.resources.stone ||
        reserved_food > state.resources.food || staged_total > max_staged_route_cells ||
        planner_memory > max_planner_bytes)
        return fail("Path planner limits are invalid.");
    for (const auto &resident : state.residents)
        if (resident.order_phase == OrderPhase::PathPending && !reserved_residents.contains(resident.id))
            return fail("Resident references an unknown path batch.");
    for (const auto &outcome : state.path_results) {
        if (outcome.path_batch_id == 0 || outcome.path_batch_id >= state.next_path_batch_id ||
            !path_ids.insert(outcome.path_batch_id).second ||
            static_cast<unsigned>(outcome.command) > static_cast<unsigned>(PathCommandKind::Replan) ||
            static_cast<unsigned>(outcome.failure) >
                static_cast<unsigned>(PathFailure::ReservationConflict) ||
            outcome.accepted != (outcome.failure == PathFailure::None) ||
            !valid_utf8_text(outcome.message, 512, true))
            return fail("Path outcome is invalid.");
        planner_memory += outcome.message.size();
    }
    if (planner_memory > max_planner_bytes)
        return fail("Path planner memory limit is invalid.");
    if (!Impl::validate_logistics(state, error) || !Impl::validate_work(state, error) ||
        !Impl::validate_professions(state, error, legacy_validation))
        return false;
    if (legacy_validation)
        return true;
    if (!Impl::validate_manual(state, error))
        return false;
    World candidate(state.seed, 0, state.terrain_generator,climate_context.config);
    auto &c = *candidate.impl_;
    c.registry.clear();
    c.resident_by_id.clear();
    c.world_id = state.world_id;
    c.ticks = state.ticks;
    c.weather_cache = std::move(climate_cache);
    auto imported_work = std::move(state.climate.outdoor_work_remainders);
    c.climate = std::move(state.climate);
    c.climate.outdoor_work_remainders.clear();
    c.next_building_id = state.next_building_id;
    c.moved = state.has_moved;
    c.gathered = state.has_gathered;
    c.built = state.has_built;
    c.resources = state.resources;
    c.chunks = std::move(state.chunks);
    std::sort(c.chunks.begin(), c.chunks.end(), [](auto &a, auto &b) { return a.key < b.key; });
    c.buildings = std::move(state.buildings);
    std::sort(c.buildings.begin(), c.buildings.end(), [](auto &a, auto &b) { return a.id < b.id; });
    c.portals = std::move(state.portals);
    std::sort(c.portals.begin(), c.portals.end(), [](const Portal &a, const Portal &b) {
        return std::tie(a.surface.x, a.surface.y, a.mine.x, a.mine.y) <
               std::tie(b.surface.x, b.surface.y, b.mine.x, b.mine.y);
    });
    c.events = std::move(state.events);
    c.next_path_batch_id = state.next_path_batch_id;
    c.topology_revision = state.topology_revision;
    c.path_batches = std::move(state.path_batches);
    c.path_results = std::move(state.path_results);
    c.next_item_stack_id = state.next_item_stack_id;
    c.next_item_reservation_id = state.next_item_reservation_id;
    c.next_logistics_task_id = state.next_logistics_task_id;
    c.item_stacks = std::move(state.item_stacks);
    c.storages = std::move(state.storages);
    c.item_reservations = std::move(state.item_reservations);
    c.logistics_tasks = std::move(state.logistics_tasks);
    c.construction_orders = std::move(state.construction_orders);
    c.next_work_intent_id = state.next_work_intent_id;
    c.work_intent_cursor_id = state.work_intent_cursor_id;
    c.work_building_cursor_id = state.work_building_cursor_id;
    c.work_intents = std::move(state.work_intents);
    c.profession_cursor_id = state.profession_cursor_id;
    c.next_manual_order_id = state.next_manual_order_id;
    c.manual_order_cursor_id = state.manual_order_cursor_id;
    c.manual_orders = std::move(state.manual_orders);
    c.rebuild_manual_heads();
    for (const auto &w : c.work_intents)
        if (Impl::active_work(w)) {
            c.active_work_by_resident[w.resident_id] = w.intent_id;
            if (w.kind == WorkIntentKind::BuildAssignment)
                c.active_work_by_building[w.building_id] = w.intent_id;
        }
    c.active_task_by_resident.clear();
    for (const auto &t : c.logistics_tasks)
        if (t.resident_id && t.phase != LogisticsPhase::Complete && t.phase != LogisticsPhase::Failed)
            c.active_task_by_resident[t.resident_id] = t.task_id;
    c.path_diagnostics = {};
    c.planner_memory_bytes = planner_memory;
    c.planner_staged_cells = staged_total;
    c.path_reserved_resources = {static_cast<int>(reserved_wood), static_cast<int>(reserved_stone),
                                 static_cast<int>(reserved_food)};
    c.observe_planner_memory();
    c.lookup_batch_id = 0;
    c.lookup_node_count = 0;
    c.active_node_lookup.clear();
    c.active_all_starts.clear();
    c.active_remaining_starts.clear();
    for (auto &batch : c.path_batches) {
        if (batch.search_storage_reserved) {
            constexpr auto cell_count = max_search_cells;
            batch.frontier.reserve(cell_count);
            batch.nodes.reserve(cell_count);
        }
        for (auto &draft : batch.drafts)
            if (draft.storage_reserved)
                draft.staged.reserve(max_path_cells);
    }
    std::map<int, RouteState> routes;
    for (auto &r : state.routes)
        routes.emplace(r.resident_id, std::move(r));
    std::sort(state.residents.begin(), state.residents.end(),
              [](const Resident &a, const Resident &b) { return a.id < b.id; });
    for (auto &r : state.residents) {
        auto &route = routes.at(r.id);
        auto e = c.registry.create();
        c.registry.emplace<Identity>(e, r.id, std::move(r.name), r.nation);
        c.registry.emplace<Position>(
            e, r.x, r.y, CellCoord{r.target_x, r.target_y, route.work ? route.work->layer : r.layer},
            r.layer);
        c.registry.emplace<Activity>(e, r.job, r.progress);
        c.registry.emplace<Vital>(e, r.health, r.hunger);
        c.registry.emplace<Skills>(e, r.wood_xp, r.mining_xp, r.farming_xp, r.additional_xp, r.skill_practice,
                                   r.abilities_bp);
        c.registry.emplace<ProfessionState>(e, r.profession);
        c.registry.emplace<Route>(e, std::move(route.points), route.cursor, route.work);
        c.registry.emplace<OrderStatus>(e, r.order_phase, r.path_failure, r.pending_path_batch);
        c.resident_by_id.emplace(r.id, e);
    }
    c.settlement = std::move(state.settlement);
    c.economy = std::move(state.economy);
    c.server_metadata=std::move(state.server_metadata);
    for (const auto& accumulator : imported_work) {
        const auto actor = c.find_resident(accumulator.resident_id);
        const auto key = c.outdoor_work_key(actor);
        if (!key || !Impl::same_outdoor_key(accumulator,*key))
            return fail("Outdoor work remainder does not match the live Activity/Route key.");
        c.registry.get<Activity>(actor).outdoor_work = accumulator;
    }
    c.cache_dirty = true;
    if (!c.path_batches.empty())
        c.rebuild_node_lookup(c.path_batches.front());
    *this = std::move(candidate);
    return true;
}
std::uint64_t World::checksum() const {
    auto state = export_state_impl(false);
    std::uint64_t h = 1469598103934665603ULL;
    hash_u64(h, state.seed);
    hash_u64(h, state.ticks);
    const auto& climate = state.climate;
    hash_u64(h,climate.algorithm_version);
    hash_u64(h,static_cast<unsigned>(climate.climate_profile));
    hash_u64(h,static_cast<unsigned>(climate.scenario));
    hash_u64(h,climate.calendar_offset_ticks);
    hash_u64(h,climate.evaluated_simulation_tick);
    const auto& weather = climate.current;
    hash_u64(h,static_cast<unsigned>(weather.kind));
    hash_u64(h,static_cast<unsigned>(weather.precipitation));
    hash_u64(h,static_cast<unsigned>(weather.wind_direction));
    hash_u64(h,static_cast<unsigned>(weather.work_reason));
    hash_u64(h,static_cast<std::uint64_t>(weather.temperature_deci_c));
    hash_u64(h,weather.precipitation_permille);
    hash_u64(h,weather.wind_permille);
    hash_u64(h,weather.outdoor_work_rate_bp);
    hash_u64(h,weather.period_start_calendar_tick);
    hash_u64(h,weather.period_end_calendar_tick);
    hash_u64(h,climate.snow_cover_permille);
    hash_u64(h,static_cast<std::uint64_t>(climate.snow_cover_remainder));
    hash_u64(h,climate.wetness_permille);
    hash_u64(h,static_cast<std::uint64_t>(climate.wetness_remainder));
    hash_u64(h,climate.outdoor_work_remainders.size());
    for (const auto& accumulator : climate.outdoor_work_remainders) {
        hash_u64(h,static_cast<std::uint64_t>(accumulator.resident_id));
        hash_u64(h,static_cast<unsigned>(accumulator.job));
        hash_u64(h,static_cast<std::uint64_t>(accumulator.target.x));
        hash_u64(h,static_cast<std::uint64_t>(accumulator.target.y));
        hash_u64(h,static_cast<unsigned>(accumulator.target.layer));
        hash_u64(h,static_cast<std::uint64_t>(accumulator.building_id));
        hash_u64(h,accumulator.remainder_bp);
    }
    hash_u64(h, state.next_building_id);
    hash_u64(h, state.has_moved);
    hash_u64(h, state.has_gathered);
    hash_u64(h, state.has_built);
    // Chunk revisions invalidate cached tile hashes; import creates a fresh Impl.
    // Repeated checksums scale with changed chunks, not the entire enlarged world.
    auto &cache = impl_->tile_hashes;
    cache.resize(impl_->chunks.size());
    for (std::size_t i = 0; i < impl_->chunks.size(); ++i) {
        const auto &c = impl_->chunks[i];
        auto &entry = cache[i];
        if (!entry.valid || entry.revision != c.revision) {
            std::uint64_t value = 1469598103934665603ULL;
            for (const auto &t : c.tiles) {
                const auto packed = std::uint64_t(static_cast<std::uint16_t>(t.elevation)) << 32 | std::uint64_t(t.amount) << 8 | std::uint64_t(t.biome) << 4 |
                                    std::uint64_t(t.explored) << 3 | std::uint64_t(t.terrain);
                hash_u64(value, packed);
                hash_u64(value, t.trail_wear);
            }
            entry = {c.revision, value, true};
        }
        hash_u64(h, static_cast<unsigned>(c.key.layer));
        hash_u64(h, c.key.cx);
        hash_u64(h, c.key.cy);
        hash_u64(h, c.revision);
        hash_u64(h, entry.value);
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
            hash_u64(h, draft.manual_order_id);
            hash_u64(h, draft.resolved);
            hash_u64(h, static_cast<unsigned>(draft.failure));
        }
        hash_u64(h, batch.reconstruction_resident);
        hash_u64(h, batch.stage_cursor);
    }
    for (const auto &batch : state.path_batches) {
        hash_u64(h, batch.legacy_atomic);
        hash_u64(h, batch.legacy_grid);
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
    hash_u64(h, state.next_manual_order_id);
    hash_u64(h, state.manual_order_cursor_id);
    hash_u64(h, state.manual_orders.size());
    for (const auto &o : state.manual_orders) {
        hash_u64(h, o.order_id);
        hash_u64(h, o.resident_id);
        hash_u64(h, o.owner_nation);
        hash_u64(h, static_cast<unsigned>(o.request.kind));
        hash_u64(h, o.request.target.has_value());
        if (o.request.target)
            hash_cell(*o.request.target);
        hash_u64(h, static_cast<unsigned>(o.request.gather_completion));
        hash_u64(h, static_cast<unsigned>(o.phase));
        hash_u64(h, static_cast<unsigned>(o.failure));
        hash_u64(h, static_cast<unsigned>(o.completion));
        hash_u64(h, o.issuer.has_value());
        if (o.issuer) {
            hash_u64(h, o.issuer->player_id);
            hash_u64(h, o.issuer->request_id);
        }
        hash_u64(h, o.issued_tick.has_value());
        if (o.issued_tick)
            hash_u64(h, *o.issued_tick);
        hash_u64(h, o.legacy_compatibility);
        hash_u64(h, static_cast<unsigned>(o.executor));
        hash_u64(h, o.executor_id);
        hash_u64(h, o.activated_tick.has_value());
        if (o.activated_tick)
            hash_u64(h, *o.activated_tick);
        hash_u64(h, o.next_attempt_tick);
        hash_u64(h, o.attempts);
    }
    if(state.settlement.reduced_rations)hash_u64(h,0x2410);
    if(state.economy!=EconomyState{}) { hash_u64(h,0x230);for(unsigned char ch:encode_economy(state.economy))hash_u64(h,ch); }
    if (state.settlement != SettlementState{}) {
        hash_u64(h,0x212);hash_u64(h,state.settlement.revision);hash_u64(h,state.settlement.arrival_tick);
        hash_u64(h,state.settlement.forced_night);hash_u64(h,state.settlement.night_discontent);
        hash_u64(h,state.settlement.youth_discontent);hash_u64(h,state.settlement.young.size());
        for(const auto &y:state.settlement.young) {hash_u64(h,y.resident_id);hash_u64(h,y.adult_tick);hash_u64(h,y.forced_work);}
    }
    return h;
}
const char *job_name(Job j) {
    static constexpr const char *v[] = {"Idle", "Move", "Woodcut", "Mine", "Forage", "Build", "Haul", "Farm"};
    int i = (int)j;
    return i >= 0 && i < 8 ? v[i] : "Unknown";
}
const char *building_name(BuildingKind k) {
    return valid_building_kind(k) ? building_definition(k).name : "Unknown";
}
} // namespace ribterra

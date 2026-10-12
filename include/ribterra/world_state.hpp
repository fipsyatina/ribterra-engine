#pragma once
#include "ribterra/foundation.hpp"
#include "ribterra/world.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace ribterra {
struct RouteState {
    int resident_id{};
    std::vector<CellCoord> points;
    std::size_t cursor{};
    std::optional<CellCoord> work;
};
struct PathSearchNodeState {
    CellCoord cell;
    CellCoord next;
    friend bool operator==(const PathSearchNodeState &, const PathSearchNodeState &) = default;
};
struct PathRouteDraftState {
    int resident_id{};
    CellCoord start;
    float start_x{}, start_y{};
    bool storage_reserved{};
    std::optional<CellCoord> reconstruction_cursor;
    std::vector<CellCoord> staged;
    // New Manual batches resolve actors independently, retaining the shared BFS.
    std::uint64_t manual_order_id{};
    bool resolved{};
    PathFailure failure{PathFailure::None};
    friend bool operator==(const PathRouteDraftState &, const PathRouteDraftState &) = default;
};
struct PathBatchState {
    std::uint64_t path_batch_id{};
    PathCommandKind command{PathCommandKind::Move};
    PathPlannerPhase phase{PathPlannerPhase::Queued};
    std::vector<int> resident_ids;
    CellCoord target;
    bool to_adjacent{};
    Job assigned_job{Job::Idle};
    std::optional<BuildingKind> build_kind;
    int building_id{};
    Resources reserved_cost{0, 0, 0};
    std::uint64_t enqueued_tick{};
    std::uint64_t deadline_tick{};
    std::uint64_t search_topology_revision{};
    std::uint8_t restart_count{};
    std::uint64_t work_units_done{};
    std::vector<CellCoord> roots;
    bool search_storage_reserved{};
    std::vector<CellCoord> frontier;
    std::size_t frontier_cursor{};
    std::vector<PathSearchNodeState> nodes;
    std::vector<PathRouteDraftState> drafts;
    std::size_t reconstruction_resident{};
    std::size_t stage_cursor{};
    // Existing v7 and legacy API batches retain atomic completion and its budget.
    bool legacy_atomic{true};
    bool legacy_grid{}; // Pending pre-expansion searches retain their original boundary.
    friend bool operator==(const PathBatchState &, const PathBatchState &) = default;
};
struct WorldState {
    std::array<std::uint8_t, 16> world_id{};
    std::uint32_t seed{};
    // Container metadata retained in memory so resaving an old world keeps its
    // legacy terrain/passability profile without regenerating stored chunks.
    std::uint32_t terrain_generator{generator_version};
    std::uint64_t ticks{};
    ClimateRuntimeState climate;
    int next_building_id{1};
    bool has_moved{}, has_gathered{}, has_built{};
    Resources resources;
    SettlementState settlement;
    EconomyState economy;
    std::string server_metadata;
    std::vector<Chunk> chunks;
    std::vector<Resident> residents;
    std::vector<Building> buildings;
    std::vector<RouteState> routes;
    std::vector<Portal> portals;
    std::vector<std::string> events;
    std::uint64_t next_path_batch_id{1};
    std::uint64_t topology_revision{};
    std::vector<PathBatchState> path_batches;
    std::vector<PathBatchOutcome> path_results;
    std::uint64_t next_item_stack_id{1}, next_item_reservation_id{1}, next_logistics_task_id{1};
    std::vector<ItemStackState> item_stacks;
    std::vector<StorageState> storages;
    std::vector<ItemReservationState> item_reservations;
    std::vector<LogisticsTaskState> logistics_tasks;
    std::vector<ConstructionOrderState> construction_orders;
    std::uint64_t next_work_intent_id{1}, work_intent_cursor_id{1};
    int work_building_cursor_id{1};
    std::vector<WorkIntentState> work_intents;
    int profession_cursor_id{1};
    std::uint64_t next_manual_order_id{1}, manual_order_cursor_id{1};
    std::vector<ManualOrderState> manual_orders;
};
} // namespace ribterra

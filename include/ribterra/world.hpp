#pragma once
#include "ribterra/foundation.hpp"
#include "ribterra/settlement.hpp"
#include "ribterra/economy.hpp"
#include "ribterra/skills.hpp"
#include "ribterra/world_grid.hpp"
#include "ribterra/weather.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace ribterra {
struct WorldState;
struct SaveLoadInfo {
    std::filesystem::path source_path, save_path;
    std::uint32_t format{}, rules{}, generator{};
    bool copied{}, normalized_rations{};
    int neutralized_attacks{};
    std::string source_version() const {
        if(!format)return "unknown";
        if (format == 8 && rules == 18) return "0.26.2";
        if (format == 8 && rules == 14) return "0.25.7–0.25.8";
        if (format == 8 && rules == 17) return "0.25.11–0.26.1";
        if (format == 8 && rules >= 15 && rules <= 16) return "0.25." + std::to_string(rules - 6);
        return "format " + std::to_string(format) + ", rules " + std::to_string(rules);
    }
};
using NationId = std::uint32_t;
inline constexpr double tick_seconds = .05;
inline constexpr std::size_t path_work_units_per_tick = 4096;
inline constexpr std::size_t max_path_batches = 512;
inline constexpr std::size_t max_path_batch_residents = 1500;
inline constexpr std::size_t max_path_cells = 2048;
inline constexpr std::size_t max_search_cells = 2ULL * 256 * 256;
inline constexpr std::size_t max_staged_route_cells = 3'072'000;
inline constexpr std::size_t max_planner_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t legacy_path_draft_logical_bytes = 72;
inline constexpr std::size_t legacy_path_batch_logical_bytes = 256;
inline constexpr std::size_t legacy_path_outcome_logical_bytes = 48;
enum class BuildingKind : int { Camp, House, Farm, Stockpile, Mine, Workshop, Temple, Tavern, Cartographer, Research, Smelter, Fishing, Library, Barracks, Bridge, Sawmill, Mill, Kitchen, ClayPit, Road };
inline constexpr unsigned building_kind_count = 20;
constexpr bool valid_building_kind(BuildingKind k) { return static_cast<unsigned>(k) < building_kind_count; }
enum class Job : int { Idle, Move, Woodcut, Mine, Forage, Build, Haul, Farm };
inline constexpr int farm_worker_limit = 2;
inline constexpr int farm_work_ticks = 400;
inline constexpr int farm_harvest_food = 8;
enum class OrderPhase : std::uint8_t { Idle, PathPending, Active, PathFailed };
enum class PathFailure : std::uint8_t {
    None,
    Unreachable,
    Timeout,
    TopologyChanged,
    LimitExceeded,
    ResidentUnavailable,
    TargetInvalid,
    ReservationConflict
};
enum class PathCommandKind : std::uint8_t { Move, Gather, Build, Replan, Logistics, Work, Manual };
enum class PathPlannerPhase : std::uint8_t { Queued, Search, Reconstruct, Stage };
enum class Profession : std::uint8_t {
    Unassigned,
    Woodcutter,
    Miner,
    Forager,
    Builder,
    Hauler,
    Farmer,
    Smith,
    Medic,
    Guard
};
enum class ProfessionWaitReason : std::uint8_t {
    None,
    Unassigned,
    Stopped,
    ManualOrder,
    DeliveryBoundary,
    NoCompatibleWork,
    WorksiteUnavailable,
    QueuePressure,
    PathFailed,
    Incapacitated,
    PendingOrder,
    Strolling,
    Socializing,
    SelfGathering
};
struct ProfessionAssignment {
    Profession profession{Profession::Unassigned};
    std::optional<CellCoord> worksite;
    // Woodcut, Mine, Forage/Farm, Build, Haul; 0 forbids, 1..5 sets priority.
    std::array<std::uint8_t, 5> priorities{1, 1, 1, 1, 1};
    bool automation_enabled{true};
    friend bool operator==(const ProfessionAssignment &, const ProfessionAssignment &) = default;
};
struct ProfessionState {
    ProfessionAssignment assignment;
    bool manual_override{}, deferred{};
    ProfessionWaitReason wait_reason{ProfessionWaitReason::Unassigned};
    std::uint64_t next_evaluation_tick{};
    friend bool operator==(const ProfessionState &, const ProfessionState &) = default;
};
inline bool has_permanent_work(const ProfessionAssignment& assignment) {
    return assignment.automation_enabled &&
           (assignment.profession != Profession::Unassigned || assignment.worksite.has_value());
}
enum class ResidentCommandStatus : std::uint8_t { Applied, Deferred, Rejected };
struct ResidentCommandOutcome {
    int resident_id{};
    ResidentCommandStatus status{ResidentCommandStatus::Rejected};
    PathFailure failure{PathFailure::None};
    std::uint64_t order_id{};
    friend bool operator==(const ResidentCommandOutcome &, const ResidentCommandOutcome &) = default;
};
inline constexpr std::size_t max_manual_orders = 8192;
inline constexpr std::size_t max_resident_manual_orders = 8;
inline constexpr std::size_t manual_terminal_history = 256;
inline constexpr std::size_t manual_visits_per_tick = 1500;
inline constexpr std::size_t manual_path_admissions_per_tick = 32;
enum class ManualOrderKind : std::uint8_t { Move, Gather, Legacy };
enum class ManualQueueMode : std::uint8_t { Replace, Append };
enum class GatherCompletion : std::uint8_t { UntilExhausted, OneDelivery };
enum class ManualOrderPhase : std::uint8_t {
    Queued,
    WaitingBoundary,
    PathPending,
    Active,
    Complete,
    Failed,
    Cancelled
};
enum class ManualOrderCompletion : std::uint8_t {
    None,
    Arrived,
    Delivered,
    Exhausted,
    Replaced,
    Stopped,
    Released,
    LegacyFinished,
    Cancelled
};
enum class ManualExecutorKind : std::uint8_t { None, Path, Route, Work, Logistics };
struct ManualOrderRequest {
    ManualOrderKind kind{ManualOrderKind::Move};
    // Only an opaque legacy executor may have no known target.
    std::optional<CellCoord> target;
    GatherCompletion gather_completion{GatherCompletion::UntilExhausted};
    friend bool operator==(const ManualOrderRequest &, const ManualOrderRequest &) = default;
};
struct ManualOrderIssuer {
    // Provenance only: these values confer no control rights.
    std::uint64_t player_id{}, request_id{};
    friend bool operator==(const ManualOrderIssuer &, const ManualOrderIssuer &) = default;
};
struct ManualOrderState {
    std::uint64_t order_id{};
    int resident_id{};
    NationId owner_nation{1};
    ManualOrderRequest request;
    ManualOrderPhase phase{ManualOrderPhase::Queued};
    PathFailure failure{PathFailure::None};
    ManualOrderCompletion completion{ManualOrderCompletion::None};
    std::optional<ManualOrderIssuer> issuer;
    std::optional<std::uint64_t> issued_tick;
    bool legacy_compatibility{};
    ManualExecutorKind executor{ManualExecutorKind::None};
    std::uint64_t executor_id{};
    std::optional<std::uint64_t> activated_tick;
    std::uint64_t next_attempt_tick{};
    std::uint16_t attempts{};
    friend bool operator==(const ManualOrderState &, const ManualOrderState &) = default;
};
struct ManualOrderDiagnostics {
    std::size_t active_orders{}, terminal_orders{}, visits_last_tick{}, path_admissions_last_tick{},
        admitted_residents_last_tick{};
};
inline constexpr std::size_t profession_visits_per_tick = 1024;
inline constexpr std::size_t profession_admissions_per_tick = 32;
inline constexpr std::uint64_t profession_evaluation_ticks = 10;
struct ProfessionDiagnostics {
    std::size_t visits_last_tick{}, admissions_last_tick{}, candidate_checks_last_tick{},
        chunk_refreshes_last_tick{};
};
struct Resident {
    int id{};
    std::string name;
    float x{}, y{};
    int target_x{}, target_y{};
    Job job{Job::Idle};
    float progress{};
    int health{100};
    float hunger{};
    int wood_xp{}, mining_xp{}, farming_xp{};
    WorldLayer layer{WorldLayer::Surface};
    NationId nation{1};
    OrderPhase order_phase{OrderPhase::Idle};
    PathFailure path_failure{PathFailure::None};
    std::uint64_t pending_path_batch{};
    // XP for the first three skills remains in the legacy named fields above.
    // The five appended entries follow Skill::Construction..Combat, without a
    // second authoritative copy of any experience value.
    std::array<int, skill_count - 3> additional_xp{};
    std::array<SkillPractice, skill_count> skill_practice{};
    std::array<std::int16_t, skill_count> abilities_bp{};
    ProfessionState profession{};
    friend bool operator==(const Resident &, const Resident &) = default;
};
struct Building {
    int id{};
    BuildingKind kind{};
    int x{}, y{};
    float progress{};
    WorldLayer layer{WorldLayer::Surface};
    NationId nation{1};
    friend bool operator==(const Building &, const Building &) = default;
};
struct Resources {
    int wood{80}, stone{40}, food{100};
    friend bool operator==(const Resources &, const Resources &) = default;
};
inline constexpr std::uint32_t resident_carry_capacity = 10;
inline constexpr std::size_t max_item_stacks = 65536;
inline constexpr std::size_t max_logistics_tasks = 4096;
inline constexpr std::size_t max_item_reservations = 8192;
enum class ItemKind : std::uint8_t { Wood, Stone, Food, Planks, Clay, Bricks, Grain, Vegetables, Fish, Meat, Flour, Tools, Firewood, Hide, Spear };
constexpr bool storage_accepts(std::uint8_t mask, ItemKind item) { return static_cast<unsigned>(item)<3 ? (mask & (1U<<static_cast<unsigned>(item)))!=0 : mask==7; }
enum class ItemLocationKind : std::uint8_t { Ground, Storage, Carried, Construction };
enum class LogisticsTaskKind : std::uint8_t { HaulToStorage, DeliverConstruction };
enum class LogisticsPhase : std::uint8_t { Queued, ToPickup, ToDropoff, Complete, Failed };
enum class LogisticsFailure : std::uint8_t {
    None,
    Unreachable,
    Timeout,
    ResidentUnavailable,
    StorageFull,
    Cancelled,
    LimitExceeded,
    TargetInvalid
};
enum class ConstructionPhase : std::uint8_t { Planned, Delivery, Work, Complete, Cancelled };
struct ItemLocation {
    ItemLocationKind kind{ItemLocationKind::Ground};
    CellCoord cell{}; // Ground only; other locations resolve their stable entity id.
    int entity_id{};
    friend bool operator==(const ItemLocation &, const ItemLocation &) = default;
};
struct ItemStackState {
    std::uint64_t stack_id{};
    ItemKind item{ItemKind::Wood};
    std::uint32_t quantity{};
    NationId owner_nation{1};
    ItemLocation location;
    friend bool operator==(const ItemStackState &, const ItemStackState &) = default;
};
struct StorageState {
    int building_id{};
    std::uint32_t capacity{10000};
    std::uint8_t accepted_items{7};
    int priority{};
    friend bool operator==(const StorageState &, const StorageState &) = default;
};
struct ItemReservationState {
    std::uint64_t reservation_id{}, stack_id{}, task_id{};
    std::uint32_t quantity{};
    NationId owner_nation{1};
    friend bool operator==(const ItemReservationState &, const ItemReservationState &) = default;
};
struct LogisticsTaskState {
    std::uint64_t task_id{};
    LogisticsTaskKind kind{LogisticsTaskKind::HaulToStorage};
    LogisticsPhase phase{LogisticsPhase::Queued};
    LogisticsFailure failure{LogisticsFailure::None};
    int resident_id{}, destination_building_id{};
    std::uint64_t source_stack_id{}, reservation_id{}, carry_stack_id{}, path_batch_id{};
    ItemKind item{ItemKind::Wood};
    std::uint32_t quantity{};
    std::uint64_t deadline_tick{};
    Job resume_job{Job::Idle};
    CellCoord resume_target{};
    friend bool operator==(const LogisticsTaskState &, const LogisticsTaskState &) = default;
};
struct ConstructionOrderState {
    int building_id{};
    BuildingKind kind{BuildingKind::House};
    CellCoord site;
    NationId owner_nation{1};
    Resources required{0, 0, 0}, delivered{0, 0, 0};
    ConstructionPhase phase{ConstructionPhase::Planned};
    std::uint64_t originating_path_batch_id{};
    friend bool operator==(const ConstructionOrderState &, const ConstructionOrderState &) = default;
};
inline constexpr std::size_t max_work_intents = 8192;
inline constexpr std::size_t work_intent_visits_per_tick = 128;
inline constexpr std::size_t work_admissions_per_tick = 32;
inline constexpr std::uint16_t max_work_admission_attempts = 64;
inline constexpr std::uint64_t work_admission_deadline_ticks = 1200;
enum class WorkIntentKind : std::uint8_t { GatherContinuation, BuildAssignment };
enum class WorkIntentPhase : std::uint8_t { Waiting, PathPending, Complete, Failed };
enum class WorkCompletionReason : std::uint8_t { None, Started, Exhausted, Superseded, Cancelled };
struct WorkIntentState {
    std::uint64_t intent_id{};
    WorkIntentKind kind{WorkIntentKind::GatherContinuation};
    WorkIntentPhase phase{WorkIntentPhase::Waiting};
    WorkCompletionReason completion{WorkCompletionReason::None};
    PathFailure failure{PathFailure::None};
    int resident_id{}, building_id{};
    NationId owner_nation{1};
    CellCoord target;
    Job job{Job::Idle};
    Terrain expected_terrain{Terrain::Grass};
    std::uint64_t created_tick{}, next_attempt_tick{}, admission_deadline_tick{}, path_batch_id{};
    std::uint16_t attempts{};
    std::uint64_t last_attempt_topology_revision{};
    friend bool operator==(const WorkIntentState &, const WorkIntentState &) = default;
};
struct WorkIntentOutcomes {
    // Diagnostic counters for this World instance; not gameplay or save state.
    std::size_t created{}, started{}, exhausted{}, superseded{}, cancelled{};
    std::array<std::size_t, 8> failures{};
};
struct WorkIntentDiagnostics {
    std::size_t active_intents{}, waiting_intents{}, pending_paths{};
    std::size_t visits_last_tick{}, admission_attempts_last_tick{};
    WorkIntentOutcomes outcomes;
};
struct ConstructionWorkforce {
    int building_id{};
    std::uint32_t working{}, travelling{}, planning{};
};
struct InventorySummary {
    Resources total{0, 0, 0}, available{0, 0, 0}, reserved{0, 0, 0};
    Resources stored{0, 0, 0}, carried{0, 0, 0};
    friend bool operator==(const InventorySummary &, const InventorySummary &) = default;
};
struct CommandResult {
    bool accepted{};
    std::string message;
    std::uint64_t path_batch_id{};
    bool pending{};
    PathFailure failure{PathFailure::None};
};
struct PathBatchOutcome {
    std::uint64_t path_batch_id{};
    PathCommandKind command{PathCommandKind::Move};
    bool accepted{};
    PathFailure failure{PathFailure::None};
    std::string message;
    friend bool operator==(const PathBatchOutcome &, const PathBatchOutcome &) = default;
};
struct PathPlannerDiagnostics {
    std::size_t queued_batches{};
    std::uint64_t active_batch_id{};
    std::size_t work_units_last_tick{};
    std::uint64_t total_expansions{};
    std::uint64_t total_reconstructed_cells{};
    std::uint64_t topology_restarts{};
    std::size_t staged_route_cells{};
    std::size_t planner_bytes{};
    std::size_t peak_staged_route_cells{};
    std::size_t peak_planner_bytes{};
};
class World {
  public:
    explicit World(std::uint32_t seed = 70421, int population = 10,
                   std::uint32_t terrain_generator = generator_version,
                   ClimateConfig climate = {});
    ~World();
    World(World &&) noexcept;
    World &operator=(World &&) noexcept;
    World(const World &) = delete;
    World &operator=(const World &) = delete;
    void tick();
    CommandResult move(const std::vector<int> &, int, int, WorldLayer = WorldLayer::Surface);
    const SettlementState &settlement(NationId nation=1) const;
    bool explored(CellCoord,NationId nation=1) const;
    void create_nations(const std::vector<int>&teams,int slots,int population);
    EconomyState economy(NationId nation=1) const;
    CommandResult wilderness(WildernessAction action,const std::vector<int>& members,int x,int y,int argument,std::uint64_t revision,NationId nation=1);
    void enable_wildlife();
    CommandResult production(int building_id,int resident_id,int recipe,int quantity,std::uint64_t revision,bool upgrade=false,NationId nation=1);
    CommandResult cancel_production(int order_id,std::uint64_t revision,NationId nation=1);
    CommandResult decide(SettlementDecision, std::uint64_t expected_revision, int resident_id = 0,NationId nation=1);
    bool work_allowed(int resident_id, Job job, std::optional<CellCoord> site = {}) const;
    CommandResult gather(const std::vector<int> &, int, int, WorldLayer = WorldLayer::Surface);
    CommandResult build(BuildingKind, int, int, WorldLayer = WorldLayer::Surface,NationId nation=1);
    std::vector<ResidentCommandOutcome> assign_profession(const std::vector<int> &,
                                                          const ProfessionAssignment &);
    std::vector<ResidentCommandOutcome> stop(const std::vector<int> &);
    std::vector<ResidentCommandOutcome> issue_manual_order(const std::vector<int> &,
                                                           const ManualOrderRequest &, ManualQueueMode,
                                                           std::optional<ManualOrderIssuer> = {});
    // Release removes queued manual work at the physical boundary and preserves
    // the existing profession/automation choice. Stop instead disables automation.
    std::vector<ResidentCommandOutcome> release_manual_control(const std::vector<int> &);
    std::vector<ResidentCommandOutcome> cancel_manual_order(std::uint64_t order_id);
    const std::vector<ManualOrderState> &manual_orders() const;
    ManualOrderDiagnostics manual_diagnostics() const;
    std::optional<ModifierBreakdown> skill_breakdown(int resident_id, Skill) const;
    ProfessionDiagnostics profession_diagnostics() const;
    static bool valid_profession_assignment(const ProfessionAssignment &);
    bool can_build(BuildingKind, int, int, WorldLayer = WorldLayer::Surface,NationId nation=1) const;
    bool walkable(int, int, WorldLayer = WorldLayer::Surface) const;
    const Tile &tile(int, int, WorldLayer = WorldLayer::Surface) const;
    const std::vector<Chunk> &chunks() const;
    const std::vector<Portal> &portals() const;
    const std::vector<Resident> &residents() const;
    const std::vector<Building> &buildings() const;
    const Resources &resources() const;
    InventorySummary inventory(NationId = 1) const;
    const std::vector<ItemStackState> &item_stacks() const;
    const std::vector<StorageState> &storages() const;
    const std::vector<ItemReservationState> &item_reservations() const;
    const std::vector<LogisticsTaskState> &logistics_tasks() const;
    const std::vector<ConstructionOrderState> &construction_orders() const;
    const std::vector<WorkIntentState> &work_intents() const;
    WorkIntentDiagnostics work_diagnostics() const;
    std::vector<ConstructionWorkforce> construction_workforce() const;
    CommandResult cancel_construction(int building_id);
    const std::vector<std::string> &events() const;
    std::vector<PathBatchOutcome> take_path_results();
    PathPlannerDiagnostics path_diagnostics() const;
    std::uint64_t ticks() const;
    std::uint32_t seed() const;
    std::uint32_t terrain_generator() const;
    int housing(NationId nation=1) const;
    CalendarView calendar() const;
    WeatherView weather() const;
    std::uint32_t day() const;
    bool winter() const;
    int tutorial_step() const;
    bool save(const std::filesystem::path &, std::string &, std::string_view server_metadata={}) const;
    bool load(const std::filesystem::path &, std::string &);
    const SaveLoadInfo& last_load() const { return last_load_; }
    WorldState export_state() const;
    bool import_state(WorldState, std::string &);
    std::uint64_t checksum() const;
    static Resources cost(BuildingKind);
    static bool migrate_legacy_inventory(WorldState &, std::string &);

  private:
    friend bool migrate_legacy_manual_orders(WorldState &, std::string &);
    bool import_state_impl(WorldState, std::string &, bool legacy_validation, bool enforce_alpha = true);
    SaveLoadInfo last_load_;
    std::filesystem::path migrated_source_, migrated_path_;
    WorldState export_state_impl(bool include_chunks) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
const char *job_name(Job);
const char *building_name(BuildingKind);
bool migrate_legacy_work_intents(WorldState &, std::string &);
bool migrate_legacy_manual_orders(WorldState &, std::string &);
} // namespace ribterra

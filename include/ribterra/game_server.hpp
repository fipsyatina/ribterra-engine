#pragma once
#include "ribterra/foundation.hpp"
#include "ribterra/world.hpp"
#include "ribterra/lobby.hpp"
#include <chrono>
#include <compare>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace ribterra {
using PlayerId = std::uint32_t;
enum class CommandType : std::uint8_t {
    Move,
    Gather,
    Build,
    CancelConstruction,
    AssignProfession,
    Stop,
    CancelManualOrder,
    ReleaseControl,
    TransferControl,
    Recruit,
    NightShift,
    YouthWork,
    Production,
    Upgrade,
    CancelProduction,
    Wilderness,
    Lobby
};
enum class ControlFailure : std::uint8_t {
    None,
    HeldByAnother,
    NotController,
    TargetUnavailable,
    StaleEpoch
};
struct ResidentControlRejection {
    int resident_id{};
    ControlFailure failure{ControlFailure::None};
    bool operator==(const ResidentControlRejection &) const = default;
};
enum class CommandReplyStatus : std::uint8_t { Invalid, Queued, Applied, Rejected };
struct GameCommand {
    std::uint64_t request_id{};
    PlayerId author{};
    std::uint64_t issued_tick{};
    CommandType type{};
    std::vector<int> residents;
    int x{}, y{};
    BuildingKind building{BuildingKind::House};
    WorldLayer layer{WorldLayer::Surface};
    int building_id{};
    ProfessionAssignment assignment{};
    ManualQueueMode manual_queue_mode{ManualQueueMode::Replace};
    GatherCompletion gather_completion{GatherCompletion::UntilExhausted};
    std::uint64_t manual_order_id{};
    PlayerId target_player{};
    std::uint64_t control_epoch{};
    std::uint64_t settlement_revision{};
    bool operator==(const GameCommand &) const = default;
};
struct CommandReply {
    std::uint64_t request_id{}, applied_tick{};
    PlayerId author{};
    CommandReplyStatus status{CommandReplyStatus::Invalid};
    PathFailure failure{PathFailure::None};
    std::string reason;
    std::vector<ResidentCommandOutcome> residents{};
    std::vector<ResidentControlRejection> control_rejections{};
    bool operator==(const CommandReply &) const = default;
};
struct ReplyReceipt {
    std::uint64_t request_id{};
    CommandReplyStatus status{CommandReplyStatus::Invalid};
    bool operator==(const ReplyReceipt &) const = default;
    auto operator<=>(const ReplyReceipt &) const = default;
};
struct ResidentLogisticsView {
    int resident_id{};
    ItemKind item{ItemKind::Wood};
    std::uint32_t quantity{}, capacity{resident_carry_capacity};
    LogisticsPhase phase{LogisticsPhase::Queued};
    LogisticsFailure failure{LogisticsFailure::None};
    bool operator==(const ResidentLogisticsView &) const = default;
};
struct ResidentWorkView {
    int resident_id{};
    WorkIntentKind kind{WorkIntentKind::GatherContinuation};
    WorkIntentPhase phase{WorkIntentPhase::Waiting};
    PathFailure failure{PathFailure::None};
    WorkCompletionReason completion{WorkCompletionReason::None};
    std::uint16_t attempts{};
    bool operator==(const ResidentWorkView &) const = default;
};
struct ResidentSkillView {
    Skill skill{Skill::Woodcutting};
    int experience{};
    ModifierBreakdown modifier;
    bool operator==(const ResidentSkillView &) const = default;
};
struct ResidentProfessionView {
    int resident_id{};
    ProfessionAssignment assignment;
    bool manual_override{}, deferred{};
    ProfessionWaitReason wait_reason{ProfessionWaitReason::Unassigned};
    std::array<ResidentSkillView, skill_count> skills;
    bool operator==(const ResidentProfessionView &) const = default;
};
struct ManualOrderView {
    std::uint64_t order_id{};
    int resident_id{};
    ManualOrderKind kind{ManualOrderKind::Move};
    std::optional<CellCoord> target;
    GatherCompletion gather_completion{GatherCompletion::UntilExhausted};
    ManualOrderPhase phase{ManualOrderPhase::Queued};
    PathFailure failure{PathFailure::None};
    ManualOrderCompletion completion{ManualOrderCompletion::None};
    bool operator==(const ManualOrderView &) const = default;
};
struct ResidentControlView {
    int resident_id{};
    PlayerId holder_id{};
    std::string holder_name;
    bool held_by_recipient{}, disconnected_grace{};
    std::uint64_t revision{};
    bool operator==(const ResidentControlView &) const = default;
};
struct ControlPlayerView {
    PlayerId player_id{};
    std::string name;
    bool operator==(const ControlPlayerView &) const = default;
};
enum class ConstructionWorkerPhase : std::uint8_t { None, Waiting, Travelling, Working, Planning };
struct ConstructionSiteView {
    int building_id{};
    BuildingKind kind{BuildingKind::House};
    CellCoord site;
    Resources required{0, 0, 0}, delivered{0, 0, 0};
    float work{};
    ConstructionPhase phase{ConstructionPhase::Planned};
    LogisticsFailure failure{LogisticsFailure::None};
    ConstructionWorkerPhase worker_phase{ConstructionWorkerPhase::None};
    PathFailure worker_failure{PathFailure::None};
    std::uint32_t assigned_workers{};
    bool operator==(const ConstructionSiteView &) const = default;
};
struct StorageView {
    int building_id{};
    std::uint32_t used{}, capacity{};
    bool operator==(const StorageView &) const = default;
};
struct ViewSnapshot {
    std::uint64_t tick{}, revision{};
    std::uint32_t terrain_generator{generator_version};
    std::vector<Chunk> chunks;
    std::vector<Portal> portals;
    std::vector<Resident> residents;
    std::vector<Building> buildings;
    Resources resources{0, 0, 0};
    LobbyState lobby;
    SettlementState settlement;
    EconomyState economy;
    InventorySummary inventory;
    std::vector<StorageView> storages;
    std::vector<ResidentLogisticsView> resident_logistics;
    std::vector<ResidentWorkView> resident_work;
    std::vector<ResidentProfessionView> resident_professions;
    std::vector<ConstructionSiteView> construction_sites;
    std::vector<ManualOrderView> manual_orders;
    std::vector<ResidentControlView> resident_controls;
    std::vector<ControlPlayerView> control_players;
    std::uint64_t control_epoch{};
    int housing{}, tutorial_step{};
    std::uint32_t day{1};
    bool winter{};
    WeatherView weather;
    const Tile &tile(int x, int y, WorldLayer layer = WorldLayer::Surface) const;
};
struct ChatEntry {
    std::uint64_t sequence{};
    PlayerId author{};
    std::string name, text;
};
struct JoinResult {
    bool accepted{};
    PlayerId player{};
    std::string resume_token, reason;
    std::uint64_t next_request{1};
};
struct ServerConfig {
    Versions versions;
    std::uint32_t seed{70421};
    int population{10};
    bool wildlife_enabled{};
    bool lobby_enabled{};
    std::size_t max_players{4};
    std::uint64_t reconnect_ticks{1200};
    std::size_t command_limit_per_second{40};
    std::function<std::chrono::steady_clock::time_point()> clock{
        [] { return std::chrono::steady_clock::now(); }};
    ClimateConfig climate;
};
class GameServer {
  public:
    explicit GameServer(ServerConfig config = {});
    JoinResult join(std::string name, const Versions &, std::string_view resume_token = {},
                    bool observer = false);
    void disconnect(PlayerId);
    CommandReply enqueue(PlayerId authenticated_player, GameCommand command);
    void tick();
    // Applies ordered command/lease boundaries without advancing simulation time.
    void pump_boundary();
    std::vector<CommandReply> take_replies(PlayerId);
    std::vector<CommandReply> pending_replies(PlayerId) const;
    void acknowledge_reply(PlayerId, ReplyReceipt);
    void acknowledge_replies(PlayerId, std::span<const ReplyReceipt>);
    ViewSnapshot snapshot(PlayerId) const;
    // Lets presentation/transport skip materializing an unchanged projection.
    std::uint64_t revision() const { return revision_; }
    std::optional<ChatEntry> chat(PlayerId, std::string text, std::string &error);
    std::vector<ChatEntry> chat_since(PlayerId recipient, std::uint64_t sequence) const;
    bool mute(PlayerId recipient, PlayerId author, bool muted);
    bool kick(PlayerId actor, PlayerId target, bool ban);
    const World &world() const { return world_; }
    const ServerConfig &config() const { return config_; }
    bool save(const std::filesystem::path &, std::string &error) const;
    bool load(const std::filesystem::path &, std::string &error);
    const SaveLoadInfo& last_load() const { return load_info_; }
    bool connected(PlayerId) const;
    // Trusted local host restoration only. Never include this credential in a
    // player snapshot or expose it through the network protocol.
    std::string local_host_resume_token() const;
    int simulation_speed()const{return lobby_.enabled?lobby_.speed:1;}

  private:
    bool load_in_place(const std::filesystem::path&, std::string&);
    SaveLoadInfo load_info_;
    struct Cached {
        GameCommand command;
        CommandReply reply;
    };
    struct Player {
        std::string name, token;
        bool connected{}, observer{};
        int team{}; bool ready{}, bot{};
        std::uint64_t disconnected_at{}, highest_request{};
        std::optional<std::chrono::steady_clock::time_point> disconnected_clock;
        std::deque<std::chrono::steady_clock::time_point> commands_at, chats_at;
        std::map<std::uint64_t, Cached> completed;
        std::map<std::uint64_t, GameCommand> pending;
        std::deque<CommandReply> replies;
        std::set<PlayerId> muted;
    };
    struct Pending {
        PlayerId player;
        GameCommand command;
        std::vector<std::pair<int, std::uint64_t>> expired_controls;
    };
    struct PathBatchLink {
        PlayerId player{};
        std::uint64_t request_id{};
        PathCommandKind command{PathCommandKind::Move};
    };
    struct ControlLease {
        PlayerId holder{};
        std::uint64_t revision{};
        bool expiry_queued{};
    };
    NationId player_nation(PlayerId id)const{return lobby_.enabled&&players_.contains(id)?NationId(players_.at(id).team):NationId{1};}
    LobbyState lobby_;
    LobbyState lobby_snapshot() const;
    void tick_bots();
    CommandReply lobby_command(PlayerId, const GameCommand &);
    ServerConfig config_;
    World world_;
    std::map<PlayerId, Player> players_;
    std::map<std::uint64_t, PathBatchLink> path_batches_;
    std::set<std::string> banned_tokens_;
    std::deque<Pending> pending_;
    std::deque<ChatEntry> chat_;
    std::map<int, ControlLease> control_leases_;
    PlayerId next_player_{1}, host_{};
    std::uint64_t chat_sequence_{}, revision_{};
    std::uint64_t control_epoch_{1}, control_revision_{};
    CommandReply apply(PlayerId, const GameCommand &);
    void complete(PlayerId, GameCommand, CommandReply);
    void queue_expired_leases();
};
} // namespace ribterra

#pragma once
#include "ribterra/game_server.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <span>
#include <variant>

namespace ribterra::net {
inline constexpr std::size_t max_packet_bytes = 256 * 1024;
inline constexpr std::size_t max_expanded_bytes = 1024 * 1024;
using WorldId = std::array<std::uint8_t, 16>;
struct Hello {
    std::string name;
    Versions versions;
    std::string resume_token, password_proof;
};
struct Challenge {
    std::string nonce;
    bool password_required{};
    Versions versions;
};
struct Welcome {
    PlayerId player{};
    std::string resume_token;
    WorldId world_id{};
    std::uint64_t next_request{1};
};
struct Rejected {
    std::string reason;
};
struct TileChange {
    std::uint32_t index{};
    Tile tile;
};
struct ChunkChange {
    ChunkKey key;
    std::uint64_t revision{};
    std::vector<TileChange> tiles;
};
struct Snapshot {
    WorldId world_id{};
    std::uint64_t revision{}, baseline{}, tick{};
    std::uint32_t terrain_generator{generator_version};
    bool full{};
    std::vector<ChunkChange> chunks;
    std::vector<ChunkKey> removed_chunks;
    std::vector<Portal> portals;
    std::vector<Resident> residents;
    std::vector<Building> buildings;
    std::vector<int> removed_residents, removed_buildings;
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
    std::vector<int> removed_resident_logistics, removed_construction_sites;
    std::vector<int> removed_storages;
    std::vector<int> removed_resident_work;
    std::vector<int> removed_resident_professions;
    std::vector<ManualOrderView> manual_orders;
    std::vector<std::uint64_t> removed_manual_orders;
    std::vector<ResidentControlView> resident_controls;
    std::vector<int> removed_resident_controls;
    std::vector<ControlPlayerView> control_players;
    std::uint64_t control_epoch{};
    int housing{}, tutorial_step{};
    std::uint32_t day{1};
    bool winter{};
    std::uint32_t part_index{}, part_count{1};
    WeatherView weather;
};
struct SnapshotAck {
    std::uint64_t revision{};
    bool resync{};
};
struct ChatSend {
    std::string text;
};
struct Ping {
    std::uint64_t value{};
};
struct ReplyAck {
    std::vector<ReplyReceipt> receipts;
};
using Payload = std::variant<Hello, Challenge, Welcome, Rejected, GameCommand, CommandReply,
                             Snapshot, SnapshotAck, ChatSend, ChatEntry, Ping, ReplyAck>;
struct Packet {
    std::uint64_t sequence{};
    Payload payload;
};
std::vector<std::uint8_t> encode(const Packet &);
std::optional<Packet> decode(std::span<const std::uint8_t>, std::string &error,
                             std::size_t expanded_limit = max_expanded_bytes);
Snapshot make_snapshot(const ViewSnapshot &, WorldId, const ViewSnapshot *baseline = nullptr);
std::vector<Snapshot> split_snapshot(const Snapshot &);
bool apply_snapshot(ViewSnapshot &, WorldId &, const Snapshot &, std::string &error);
enum class AssemblyResult { Pending, Applied, Rejected };
// A partial snapshot never changes the displayed world or acknowledged baseline.
class SnapshotAssembler {
  public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    explicit SnapshotAssembler(Clock clock = [] { return std::chrono::steady_clock::now(); })
        : clock_(std::move(clock)) {}
    AssemblyResult accept(ViewSnapshot &, WorldId &, Snapshot, std::string &error);
    bool expired() const;
    void reset();

  private:
    std::vector<std::optional<Snapshot>> parts_;
    std::chrono::steady_clock::time_point started_{}, last_progress_{};
    std::size_t count_{}, weight_{};
    Clock clock_;
};
std::string password_proof(std::string_view password, const Challenge &, const Hello &);
} // namespace ribterra::net

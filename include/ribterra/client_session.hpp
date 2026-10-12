#pragma once

#include "ribterra/game_server.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ribterra {

// The presentation-facing simulation boundary. It exposes only the same
// visibility-filtered snapshot a network player receives and never a mutable
// World reference.
class ClientSession {
  public:
    explicit ClientSession(std::uint32_t seed = 70421, std::string player_name = "Player", ClimateConfig climate = {});
    ~ClientSession();
    ClientSession(const ClientSession &) = delete;
    ClientSession &operator=(const ClientSession &) = delete;

    bool connect_remote(std::string_view endpoint, std::string password, std::string &error,std::string resume_token={});
    void poll();
    void advance();
    // Monotonic presentation generation, including loads of a different world.
    std::uint64_t revision() const;
    std::string resume_token() const;
    std::string connection_label;
    struct ConnectionAddress { std::string endpoint, label; };
    std::vector<ConnectionAddress> connection_addresses;
    const LobbyState &lobby() const;
    bool can_change_speed() const;
    CommandResult lobby_action(LobbyAction,int value=0);
    bool remote() const;
    bool ready() const;
    // A remote presentation may use lobby data only after an authoritative
    // snapshot has been applied. Entering the world additionally requires the
    // server's lobby phase to be started and visible terrain to be present.
    bool snapshot_ready() const;
    bool world_ready() const;
    bool reconnecting() const;
    bool recovery_incomplete() const;
    bool send_chat(std::string text, std::string &error);
    std::vector<ChatEntry> take_chat();

    CommandResult move(const std::vector<int> &ids, int x, int y,
                       WorldLayer layer = WorldLayer::Surface,
                       ManualQueueMode mode = ManualQueueMode::Replace);
    CommandResult gather(const std::vector<int> &ids, int x, int y,
                         WorldLayer layer = WorldLayer::Surface,
                         ManualQueueMode mode = ManualQueueMode::Replace,
                         GatherCompletion completion = GatherCompletion::OneDelivery);
    CommandResult build(BuildingKind kind, int x, int y, WorldLayer layer = WorldLayer::Surface);
    CommandResult cancel_construction(int building_id);
    CommandResult assign_profession(const std::vector<int> &ids,
                                    const ProfessionAssignment &assignment);
    const SettlementState &settlement() const;
    const EconomyState &economy() const;
    CommandResult wilderness(WildernessAction,const std::vector<int>&,int x,int y,int argument,std::uint64_t revision);
    CommandResult production(int building,int resident,int recipe,int quantity,std::uint64_t revision,bool upgrade=false);
    CommandResult cancel_production(int order,std::uint64_t revision);
    CommandResult production_group(const std::vector<int>& buildings,int recipe,int batches,int worker=0);
    const std::string& production_group_status() const;
    std::string production_feedback(int building_id) const;
    struct ProductionResult { int building_id{}, resident_id{}; std::string reason; };
    const std::vector<ProductionResult>& production_results() const;
    // Empty means available. Used by manual selection and the automatic selector.
    std::string production_worker_reason(int resident_id, bool automatic=false) const;
    CommandResult decide(SettlementDecision, std::uint64_t revision, int resident_id = 0);
    CommandResult stop(const std::vector<int> &ids);
    CommandResult release_control(const std::vector<int> &ids);
    CommandResult transfer_control(const std::vector<int> &ids, PlayerId target);
    CommandResult cancel_manual_order(std::uint64_t order_id);
    PlayerId player_id() const;
    NationId nation_id() const;
    std::uint64_t control_epoch() const;
    static Resources cost(BuildingKind kind);
    bool can_build(BuildingKind kind, int x, int y, WorldLayer layer = WorldLayer::Surface) const;
    bool save(const std::filesystem::path &path, std::string &error) const;
    bool load(const std::filesystem::path &path, std::string &error);
    const SaveLoadInfo& last_load() const;
    std::optional<std::string> take_notice();
    std::optional<CommandReply> take_command_reply();

    const Tile &tile(int x, int y, WorldLayer layer = WorldLayer::Surface) const;
    const std::vector<Chunk> &chunks() const;
    const std::vector<Portal> &portals() const;
    const std::vector<Resident> &residents() const;
    const std::vector<Building> &buildings() const;
    const Resources &resources() const;
    const InventorySummary &inventory() const;
    const std::vector<StorageView> &storages() const;
    const std::vector<ResidentLogisticsView> &resident_logistics() const;
    const std::vector<ResidentWorkView> &resident_work() const;
    const std::vector<ResidentProfessionView> &resident_professions() const;
    const std::vector<ConstructionSiteView> &construction_sites() const;
    const std::vector<ManualOrderView> &manual_orders() const;
    const std::vector<ResidentControlView> &resident_controls() const;
    const std::vector<ControlPlayerView> &control_players() const;
    std::uint64_t ticks() const;
    int housing() const;
    const WeatherView &weather() const;
    std::optional<CalendarView> calendar() const;
    std::uint32_t day() const;
    bool winter() const;
    int tutorial_step() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra

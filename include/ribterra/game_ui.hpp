#pragma once

#include "ribterra/presentation.hpp"
#include "ribterra/render_frame.hpp"
#include "ribterra/world.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;

namespace ribterra {
class AssetCatalog;
class ClientSession;
struct CommandReply;
class Localization;
namespace render {
class Renderer2D;
}

enum class UiActionType {
    NewGame,
    HostGame, JoinGame, LeaveOnline, LobbyCommand,
    ContinueGame,
    Quit,
    Save,
    Load,
    Menu,
    Pause,
    Speed,
    Build,
    Chat,
    Layer,
    Passage,
    CancelConstruction,
    AssignProfession,
    ChooseWorksite,
    Stop,
    GatherMode,
    ReleaseControl,
    TransferControl,
    CancelManualOrder,
    SettlementDecision,
    Production,
    Upgrade,
    CancelProduction,
    BuildingProduction,
    ManageProductionWorker,
    ClearBuildingSelection,
    CancelInteraction,
    FocusMap,
    PresentationSettings
};
struct UiAction {
    UiActionType type{};
    int value{};
    BuildingKind building{BuildingKind::House};
    std::string text;
    ProfessionAssignment assignment{};
    std::vector<int> residents{};
    std::uint64_t order_id{}, control_epoch{}, settlement_revision{};
    std::uint32_t target_player{};
    int recipe{}, quantity{1},x{},y{};
    std::vector<int> buildings;
    render::PresentationRequest presentation{};
};

// RmlUi presentation controller. It emits intent only; gameplay authority
// remains in ClientSession/GameServer and the application handles file actions.
class GameUi {
  public:
    GameUi(render::Renderer2D &renderer, AssetCatalog &assets, Localization &localization,
           std::filesystem::path settings_path, render::Extent size = {},
           bool restore_presentation_settings = true);
    ~GameUi();
    GameUi(const GameUi &) = delete;
    GameUi &operator=(const GameUi &) = delete;

    bool process_event(const SDL_Event &event);
    void set_logical_size(render::Extent size);
    void update(ClientSession *session, std::string_view status = {});
    std::string load_status(const SaveLoadInfo&, std::string_view error = {}) const;
    void update(ClientSession &session, std::string_view status = {}) { update(&session, status); }
    void compose(render::Frame2D &frame);
    std::vector<UiAction> take_actions();
    void show_menu();
    void show_world();
    void toggle_hud();
    void reset_world_state();
    bool world_visible() const;
    bool hud_visible() const;
    bool text_input_focused() const;
    void clear_chat_input();
    // Call after Renderer2D::set_presentation() so the UI can display the
    // actual result and persist only a request the renderer accepted.
    void presentation_applied();
    void set_view_state(const ClientSession *session, const std::vector<int> &selected_ids, bool paused,
                        int speed, WorldLayer layer = WorldLayer::Surface, const std::vector<int> &buildings = {},
                        bool interaction_active = false);
    void show_help();
    void show_profession();
    const std::string &locale() const;
    std::string format_command_reply(const CommandReply &reply) const;
    float scale() const;
    bool visible_for_test(std::string_view element_id) const;
    bool enabled_for_test(std::string_view element_id) const;
    bool activate_for_test(std::string_view element_id);
    bool focus_for_test(std::string_view element_id);
    std::string focused_element_for_test() const;
    std::string element_rml_for_test(std::string_view element_id) const;
    std::string select_option_rml_for_test(std::string_view element_id, int index) const;
    render::Point element_center_for_test(std::string_view element_id) const;
    render::Rect element_bounds_for_test(std::string_view element_id) const;
    float element_text_width_for_test(std::string_view element_id) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra

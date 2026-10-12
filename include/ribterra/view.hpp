#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ribterra/render_frame.hpp"
#include "ribterra/world_grid.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>

#include "ribterra/environment_lighting.hpp"
#include "ribterra/weather_presentation.hpp"

namespace ribterra {

class ClientSession;
class AssetCatalog;
class Localization;
enum class BuildingKind;
enum class GatherCompletion : std::uint8_t;
struct ProfessionAssignment;
namespace render {
class Renderer2D;
}

struct EngineControlInstance {
    std::string sprite_id;
    float x{};
    float y{};
    float geometry_scale{1.f};
    std::uint32_t owner_id{};
    std::uint32_t first_occluder_id{};
    float depth_offset{};
    bool emission_enabled{true};
    // Independent light namespace; enabled sprites with authored local emitters
    // require nonzero IDs. Disabled/non-emitting sprites may leave this zero.
    std::uint32_t first_light_id{};
    // Separate stable namespace for authored ground-contact rows.
    std::uint32_t first_contact_id{};
};

struct EngineAtmosphereSettings {
    bool enabled{true};
    float time_hour{6.f};
    float weather_haze01{.25f};
};

enum class EngineTimeSource { ExplicitDiagnostic, SessionShown };
enum class EngineWeatherSource { ExplicitDiagnostic, SessionAuthority };

struct EngineControlSettings {
    float camera_x{};
    float camera_y{};
    float zoom{1.3f};
    int cell_radius{12};
    render::SurfaceLightingFrame lighting{};
    // This diagnostic route has no external blocker stream: incoming lights
    // must have zero occluder ranges; their other bytes are preserved verbatim.
    render::MoonLightingGpu moon{};
    float exposure{1.f};
    // Explicit cumulative engine slice; older controls stay unchanged.
    bool water_enabled{};
    bool contact_enabled{};
    float water_wind{.25f};
    std::array<float, 2> water_flow{1.f, 0.f};
    // Diagnostic presentation inputs, not simulation/weather authority. The
    // adapter derives morning strength and all colors from this one time value.
    std::optional<EngineAtmosphereSettings> atmosphere;
    // ExplicitDiagnostic V1 owns ambient/sun/moon/atmosphere; only incoming
    // local counts and light rows remain inputs. Requires WaterAtmosphereSlice
    // even with fog disabled, and is exclusive with manual atmosphere.
    std::optional<EngineEnvironmentSettingsV1> environment;
    // SessionShown shares the unit presentation clock and overrides only the
    // environment hour and water/fog phase. Cloud and haze remain explicit
    // unless weather_source selects SessionAuthority below.
    EngineTimeSource time_source{EngineTimeSource::ExplicitDiagnostic};
    // Opt-in authority uses received WeatherView and the same staged shown
    // clock. It owns environment/water wind/wetness, never local lights or flow.
    EngineWeatherSource weather_source{EngineWeatherSource::ExplicitDiagnostic};
    // Bounded diagnostic material input; validated even when authority replaces
    // it. Actual sprite eligibility still comes from its authored wet mask.
    float ground_wetness01{};
};

struct EngineControlStats {
    std::size_t ground_cells{};
    std::size_t instances_drawn{};
    std::size_t instances_hidden{};
    std::size_t sky_triangles{};
    std::size_t local_occluders{};
    std::size_t local_lights{};
    std::size_t water_cells{}, bridge_decks{}, reflection_draws{};
    std::size_t atmosphere_water_patches{};
    std::size_t contact_shadows{};
    std::size_t contact_receivers_skipped{};
    // Published projection witness only. Snow data does not imply snow FX.
    std::optional<EngineWeatherProjectionV1> authoritative_weather;
};

// Native prototype presentation and input layer.  It deliberately owns no
// simulation state: every command is routed through ClientSession.
class GameView {
  public:
    explicit GameView(render::Renderer2D &renderer);
    ~GameView();
    void load_art(const AssetCatalog &assets);
    // Explicit cold installation for normal mixed LegacyCombined composition.
    // Registered paint must match the already-loaded original catalog exactly.
    // Surface uses authoritative weather; unavailable authority, Mine and
    // unregistered/mirrored variants retain the original legacy route.
    void load_material_collection(const AssetCatalog &assets);
    std::size_t registered_material_count() const noexcept;
    bool has_registered_material(std::string_view legacy_key) const;
    void set_logical_size(render::Extent size);
    void compose_cursor(render::Frame2D &frame);
    GameView(const GameView &) = delete;
    GameView &operator=(const GameView &) = delete;

    void handle_event(const SDL_Event &event, ClientSession &session);
    // Observe modifiers even when RmlUi consumes the event itself.
    void observe_input(const SDL_Event &event);
    void compose(const ClientSession &session, double seconds, render::Frame2D &frame,
                 bool legacy_hud = true);
    // Opt-in diagnostic slice: real session terrain plus supplied presentation-only
    // controls inside a fixed bounded cell radius. It is not normal GameView
    // composition, does not represent every world object, and appends no HUD.
    void load_engine_control_art(const AssetCatalog &assets);
    // Startup-only technical tileable normal. No old baked water art is reused.
    void load_engine_water_art();
    EngineControlStats compose_engine_control(
        const ClientSession &session, double seconds, const EngineControlSettings &settings,
        std::span<const EngineControlInstance> instances, render::Frame2D &frame);
    bool selection_drag_active() const;
    bool paused() const;
    int speed() const;
    void set_status(std::string status);
    // Returns display text, with legacy keys resolved once. Do not translate again.
    std::optional<std::string> take_status_text(const Localization &localization);
    void set_paused(bool paused);
    void set_speed(int speed);
    // Photo/HUD mode hides presentation annotations, never world state.
    void set_annotations_visible(bool visible);
    void arm_build(BuildingKind kind);
    void focus_group(const ClientSession&,const std::vector<int>&);
    // Recenters only on an explored cell represented by the currently drawn minimap.
    bool focus_minimap(const ClientSession&,float normalized_x,float normalized_y);
    void arm_worksite(ProfessionAssignment, std::vector<int> residents);
    bool worksite_armed() const;
    void cancel_worksite();
    bool interaction_active() const;
    void cancel_interaction();
    void set_gather_completion(GatherCompletion completion);
    GatherCompletion gather_completion() const;
    std::optional<std::string> take_worksite_error();
    void reset_world_state();
    void set_layer(WorldLayer layer);
    WorldLayer layer() const;
    const std::vector<int> &selected_ids() const;
    const std::vector<int> &selected_building_ids() const;
    void clear_building_selection();
    SDL_FPoint tile_screen(float x, float y) const;
    // The position actually drawn, also used by selection and movement diagnostics.
    std::optional<SDL_FPoint> resident_screen(const ClientSession &, int id) const;
    CellCoord cell_at_screen(float x, float y) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra

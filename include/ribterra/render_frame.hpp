#pragma once

#include "ribterra/surface_lighting.hpp"
#include "ribterra/contact_shadow.hpp"
#include "ribterra/moon_lighting.hpp"
#include "ribterra/sky_shadow.hpp"
#include "ribterra/atmosphere.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace ribterra::render {

struct Point { float x{}, y{}; };
struct Extent { std::uint32_t width{1440}, height{900}; };
struct Rect { float x{}, y{}, w{}, h{}; };
struct Color { std::uint8_t r{}, g{}, b{}, a{255}; };
enum class Pass { Terrain, WorldOrdered, Atmosphere, Fog, Ui };
enum class Blend { Opaque, Alpha, Additive, PremultipliedAlpha };
enum class FrameContent { World, UiOnly };
struct TextureHandle {
    std::uint32_t slot{}, generation{};
    friend bool operator==(const TextureHandle&, const TextureHandle&) = default;
};
struct MaterialHandle {
    std::uint32_t slot{}, generation{};
    friend bool operator==(const MaterialHandle&, const MaterialHandle&) = default;
};
struct alignas(16) SpriteSurfaceGpu {
    std::array<std::array<float, 4>, 5> uv_regions{};
    std::array<float, 4> anchor_export{};
    std::array<float, 4> source_size_pivot{};
    std::array<float, 4> depth_scale{};
    std::array<float, 4> emission{};
    std::array<float, 4> response{};
};
static_assert(sizeof(SpriteSurfaceGpu) == 160);
static_assert(alignof(SpriteSurfaceGpu) == 16);
static_assert(std::is_standard_layout_v<SpriteSurfaceGpu>);
static_assert(std::is_trivially_copyable_v<SpriteSurfaceGpu>);
static_assert(offsetof(SpriteSurfaceGpu, uv_regions) == 0);
static_assert(offsetof(SpriteSurfaceGpu, anchor_export) == 80);
static_assert(offsetof(SpriteSurfaceGpu, source_size_pivot) == 96);
static_assert(offsetof(SpriteSurfaceGpu, depth_scale) == 112);
static_assert(offsetof(SpriteSurfaceGpu, emission) == 128);
static_assert(offsetof(SpriteSurfaceGpu, response) == 144);
// Intentional CPU-wrapper gaps align the embedded uniform and round the tail
// to 16 bytes. This wrapper is never itself uploaded as a GPU constant buffer.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324)
#endif
struct SpriteMaterialDraw {
    MaterialHandle material{};
    SpriteSurfaceGpu surface{};
    // Explicit forward metadata, not a reinterpretation of SpriteSurface's
    // reserved components. Used only by the internal ground/forward graph.
    float reveal{1.f};
    std::uint32_t receiver_mask{1u};
    std::uint32_t receiver_owner{}; // Zero means ownerless, never excludes a blocker.
    // Instance switch/dimmer, applied to material emission intensity on upload.
    // Occupies the CPU wrapper's previous tail padding; GPU uniforms stay unchanged.
    float emission_scale{1.f};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static_assert(alignof(SpriteMaterialDraw) == 16 && sizeof(SpriteMaterialDraw) == 192);
static_assert(offsetof(SpriteMaterialDraw, material) == 0 && offsetof(SpriteMaterialDraw, surface) == 16);
static_assert(offsetof(SpriteMaterialDraw, reveal) == 176 && offsetof(SpriteMaterialDraw, receiver_mask) == 180 &&
              offsetof(SpriteMaterialDraw, receiver_owner) == 184 &&
              offsetof(SpriteMaterialDraw, emission_scale) == 188);
static_assert(std::is_standard_layout_v<SpriteMaterialDraw> && std::is_trivially_copyable_v<SpriteMaterialDraw>);
struct alignas(16) GroundMaterialGpu {
    // Roughness, wetness, shore/snow response, reveal; each in [0,1].
    std::array<float, 4> response{1.f, 0.f, 0.f, 1.f};
    // Receiver bits in the low byte of x; all other components reserved zero.
    std::array<std::uint32_t, 4> flags{1u, 0u, 0u, 0u};
    // Authored ambient/contact visibility, sun visibility; reserved zero.
    std::array<float, 4> visibility{1.f, 1.f, 0.f, 0.f};
};
static_assert(sizeof(GroundMaterialGpu) == 48 && alignof(GroundMaterialGpu) == 16);
static_assert(std::is_standard_layout_v<GroundMaterialGpu> && std::is_trivially_copyable_v<GroundMaterialGpu>);
static_assert(offsetof(GroundMaterialGpu, response) == 0 && offsetof(GroundMaterialGpu, flags) == 16 &&
              offsetof(GroundMaterialGpu, visibility) == 32);
// WaterCoverage V1 is a separate full-graph contract. It never changes the
// discrete Contact V1 world tag, AO, reveal, or the eight local receiver bits.
enum class GroundSurfaceClass : std::uint32_t { Land = 1, Water = 2, BridgeDeck = 3 };
struct alignas(16) WaterGroundGpu {
    // RGBA8 target: water geometric coverage, class/255, shore edges/15,
    // reflection allowed. Non-water writes zero water coverage/shore/allow.
    std::array<float, 4> receiver{};
};
static_assert(sizeof(WaterGroundGpu) == 16 && alignof(WaterGroundGpu) == 16);
static_assert(std::is_standard_layout_v<WaterGroundGpu> && std::is_trivially_copyable_v<WaterGroundGpu>);
struct alignas(16) WaterSurfaceGpu {
    // Absolute world origin XY plus the one shown clock (seconds) and wind[0,1].
    std::array<float, 4> world_origin_time{};
    // Normalized flow XY, reflection strength[0,.34], normal strength[0,1].
    std::array<float, 4> flow_response{1.f, 0.f, .25f, .3f};
    // Linear water tint RGB, roughness[0,1]. No baked illumination.
    std::array<float, 4> tint{.035f, .19f, .25f, .22f};
    // Distortion budget in reference pixels; reference height; reserved zero.
    std::array<float, 4> viewport{1.5f, 900.f, 0.f, 0.f};
};
static_assert(sizeof(WaterSurfaceGpu) == 64 && alignof(WaterSurfaceGpu) == 16);
static_assert(offsetof(WaterSurfaceGpu, flow_response) == 16 &&
              offsetof(WaterSurfaceGpu, tint) == 32 && offsetof(WaterSurfaceGpu, viewport) == 48);
static_assert(std::is_standard_layout_v<WaterSurfaceGpu> && std::is_trivially_copyable_v<WaterSurfaceGpu>);
// Same intentional CPU-wrapper alignment; only surface48 is uploaded.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324)
#endif
struct GroundMaterialDraw {
    TextureHandle albedo{}; // Typed sRGB albedo, never a baked legacy texture.
    GroundMaterialGpu surface{};
    // Contact graph only. All eight local-light receiver bits stay unchanged.
    ContactReceiver contact_receiver{ContactReceiver::Ordinary};
    GroundSurfaceClass water_surface_class{GroundSurfaceClass::Land};
    // Orthogonal shores: N=1, E=2, S=4, W=8. Unknown is never a shore.
    std::uint32_t shore_edges{};
    // Water-only analytic/authored edge coverage, independent of Contact V1.
    // A sample-count-1 interior value does not itself claim antialiasing.
    float geometric_coverage{1.f};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static_assert(alignof(GroundMaterialDraw) == 16 && sizeof(GroundMaterialDraw) == 80);
static_assert(offsetof(GroundMaterialDraw, albedo) == 0 && offsetof(GroundMaterialDraw, surface) == 16 &&
              offsetof(GroundMaterialDraw, contact_receiver) == 64);
static_assert(offsetof(GroundMaterialDraw, water_surface_class) == 68 &&
              offsetof(GroundMaterialDraw, shore_edges) == 72 &&
              offsetof(GroundMaterialDraw, geometric_coverage) == 76);
static_assert(std::is_standard_layout_v<GroundMaterialDraw> && std::is_trivially_copyable_v<GroundMaterialDraw>);
struct alignas(16) ForwardMetadataGpu {
    std::array<float, 4> reveal{1.f, 0.f, 0.f, 0.f};
    std::array<std::uint32_t, 4> receiver{1u, 0u, 0u, 0u}; // mask, owner, reserved zero
};
static_assert(sizeof(ForwardMetadataGpu) == 32 && alignof(ForwardMetadataGpu) == 16);
static_assert(offsetof(ForwardMetadataGpu, receiver) == 16);
struct Vertex { Point position, uv; Color color; };
static_assert(sizeof(Vertex) == 20 && offsetof(Vertex, uv) == 8 && offsetof(Vertex, color) == 16);
struct GeometryDraw {
    Pass pass{Pass::WorldOrdered};
    Blend blend{Blend::Alpha};
    TextureHandle texture{}; // Zero is white; other handles reference renderer resources.
    std::uint32_t first_index{}, index_count{};
    Rect scissor{};
    bool scissor_enabled{};
    // Opt-in opaque world geometry with a companion world-position/normal
    // stream. This is not a claim that the full Dynamic mode is available.
    bool surface_lit{};
    // One-based index into Frame2D::sprite_material_draws. Zero keeps the
    // legacy sprite path.
    std::uint32_t sprite_material_index{};
    // One-based ground draw index; zero keeps the existing rendering path.
    std::uint32_t ground_material_index{};
};
struct TextDraw {
    Pass pass{Pass::Ui};
    std::string text;
    Point position{};
    float size{18};
    Color color{238, 226, 197, 255};
    float max_width{};
};
struct ReflectionDraw {
    // Mirrored geometry uses the original authored atlas UV and source P/N.
    // It is not a framebuffer copy and is never placed in ordinary commands.
    GeometryDraw draw{};
    float strength{.25f};
    float fade{1.7f}; // Distance-fade exponent, not output scene transparency.
};

// Commands retain painter order across geometry and text. A renderer must never
// sort them by material: world depth, panels and overlays depend on this order.
struct Frame2D {
    Extent logical_size{};
    Color clear{0, 0, 0, 255};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::variant<GeometryDraw, TextDraw>> commands;
    std::vector<SurfaceVertex> surface_vertices;
    std::vector<SpriteMaterialDraw> sprite_material_draws;
    std::vector<GroundMaterialDraw> ground_material_draws;
    std::optional<WaterSurfaceGpu> water_surface;
    std::optional<AtmosphereGpu> atmosphere;
    TextureHandle water_normal{}; // Typed linear tileable normal, not sRGB albedo.
    std::vector<ReflectionDraw> reflection_draws;
    std::vector<OccluderGpu> local_occluders;
    std::vector<ContactShadowDraw> contact_shadows;
    std::optional<ContactProjection> contact_projection;
    SurfaceLightingFrame surface_lighting{};
    // Explicit SkyContactSlice extension. The legacy sun/local ABI stays384;
    // an independent moon never aliases its sun or contact/AO visibility.
    std::optional<MoonLightingGpu> moon_lighting;
    // Full opaque geometry, independent of the front-camera caster mask.
    // Fixed-camera diagnostic SkyShadowSlice only; no art-registry acceptance.
    std::vector<SkyShadowTriangle> sky_shadow_triangles;
    // Opt-in production color path, not the completed Dynamic material graph.
    // World is linear RGBA16F; exposure/SDR encode precede the untouched UI.
    // Requires an opaque clear; the SDR composite has opaque scene coverage.
    bool linear_hdr_world{};
    float world_exposure{1.f};
    // Fresh menu/loading frames opt in explicitly; missing world data never
    // changes the selected world graph or repairs an invalid World frame.
    FrameContent content{FrameContent::World};
    void clear_commands() {
        vertices.clear(); indices.clear(); commands.clear(); surface_vertices.clear();
        sprite_material_draws.clear();
        ground_material_draws.clear();
        water_surface.reset(); water_normal = {}; reflection_draws.clear();
        atmosphere.reset();
        local_occluders.clear();
        contact_shadows.clear(); contact_projection.reset();
        surface_lighting = {};
        moon_lighting.reset();
        sky_shadow_triangles.clear();
        linear_hdr_world = false; world_exposure = 1.f;
        content = FrameContent::World;
    }
};

} // namespace ribterra::render

#pragma once

#include "ribterra/presentation.hpp"
#include "ribterra/render_frame.hpp"
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace ribterra::render {

enum class Backend { Direct3D12, Software };
// Dynamic is reserved for a future rendering path, not an alias for Legacy.
enum class LightingMode { Legacy, Dynamic };
enum class LightingUnavailableReason { None, NotImplemented };
// Internal production integration slice. This does not enable LightingMode::Dynamic.
enum class WorldGraph { LegacyCombined, GroundForwardSlice, GroundContactSlice, SkyContactSlice, SkyShadowSlice,
                        WaterReflectionSlice, WaterAtmosphereSlice };
struct SkyShadowStatus {
    bool available{}, d32_supported{};
    std::uint32_t map_size{}, frame_sets{}, candidate_live_textures{};
    // Nominal payload only, not driver VRAM allocation or measured performance.
    std::uint64_t attachment_bytes{}, creation_attempts{}, target_generation{};
    std::array<std::uint64_t, 2> active_set_ids{};
};
struct ContactArtStatus {
    std::uint32_t live_arts{};
    std::uint64_t logical_bytes{}, creation_attempts{};
};
struct GroundForwardStatus {
    bool available{};
    // The seven attachments use four distinct formats, in order:
    // RGBA8 sRGB, RGBA16F, RGBA8 UNORM, RG16F.
    std::array<bool, 4> format_supported{};
    // First seven entries retain their old order; WaterCoverage V1 and the
    // half-resolution reflection are entries 7/8, zero in older graphs.
    std::array<std::uint32_t, 9> target_formats{};
    std::uint32_t width{}, height{}, frame_sets{};
    std::uint64_t attachment_bytes{}, target_generation{}, creation_attempts{};
    std::array<std::uint64_t, 2> active_set_ids{};
    std::uint32_t candidate_live_textures{};
    std::uint32_t reflection_width{}, reflection_height{};
    std::uint64_t coverage_bytes{}, reflection_bytes{};
    std::string reason{"Ground/forward graph is unavailable on this backend"};
};
struct LightingStatus {
    LightingMode requested{LightingMode::Legacy};
    LightingMode effective{LightingMode::Legacy};
    bool dynamic_available{};
    LightingUnavailableReason reason{LightingUnavailableReason::None};
};
struct WorldHdrStatus {
    bool available{}, rgba16f_supported{}, sdr_unorm_swapchain{};
    std::uint32_t swapchain_format{}, width{}, height{}, frame_sets{};
    // Logical attachment footprint (world RGBA16F + final RGBA8 per set),
    // not driver allocation size or total/peak GPU VRAM usage.
    std::uint64_t attachment_bytes{};
    std::string reason{"Linear HDR world is unavailable on this backend"};
};
struct RendererOptions {
    Backend backend{Backend::Direct3D12};
    bool debug{};
    // Deterministic fault injection for the GPU text upload recovery contract.
    // Zero disables it; production callers leave this at its default.
    std::uint32_t test_fail_gpu_text_uploads{};
    // Startup requires support: an unsupported explicit mode rejects construction.
    LightingMode lighting_mode{LightingMode::Legacy};
    PresentationRequest presentation{};
    bool collect_telemetry{};
    // One-shot native contract fault: fail this numbered frame-buffer creation.
    // Vertex/index/surface/occluder buffers are counted; zero disables injection.
    std::uint32_t test_fail_gpu_frame_buffer_creation_at{};
    // One-shot numbered HDR world/final target creation fault; zero disables.
    std::uint32_t test_fail_hdr_target_creation_at{};
    // One-shot numbered sampled-texture candidate creation fault. Zero
    // disables injection; successful and failed candidates are counted.
    std::uint32_t test_fail_sampled_texture_creation_at{};
    WorldGraph world_graph{WorldGraph::LegacyCombined};
    // One-shot numbered creation fault across seven legacy attachments or all
    // nine WaterReflectionSlice attachments per frame set.
    std::uint32_t test_fail_ground_target_creation_at{};
    std::uint32_t test_fail_contact_art_creation_at{};
    // Fail after recording an upload, before submission/publication.
    std::uint32_t test_fail_contact_art_upload_at{};
    std::uint32_t test_fail_contact_art_idle_waits{};
    std::uint32_t test_fail_sky_target_creation_at{};
};
struct TextureOptions {
    // Total levels including the base image. The renderer caps this at four
    // and at the number of levels supported by the texture dimensions.
    std::uint32_t mip_levels{1};
};
enum class SampledTextureFormat { Rgba8UnormSrgb, Rgba8Unorm, R8Unorm, R16Unorm };
struct SampledTextureDesc {
    Extent size{};
    SampledTextureFormat format{SampledTextureFormat::Rgba8Unorm};
};
struct MaterialTextureBinding {
    TextureHandle texture{};
    Rect uv{0.f, 0.f, 1.f, 1.f};
};
enum class MaterialChannel : std::uint32_t {
    Albedo = 0,
    NormalMaterial = 1,
    CameraDepth = 2,
    EmissionMask = 3,
    SurfaceMask = 4,
    CasterMask = 5
};
struct SpriteMaterialDesc {
    std::array<MaterialTextureBinding, 6> channels{};
    std::array<float, 4> emission{0.f, 0.f, 0.f, 0.f};
};
std::uint32_t texture_mip_level_count(Extent size, TextureOptions options = {}) noexcept;
struct DeviceInfo { std::string backend, device, driver; bool software{}; };
struct WaterCoverageReadback {
    Extent size{};
    // Native download stride before rows are repacked into tightly packed RGBA8.
    std::uint32_t gpu_row_pitch{};
    std::uint32_t frame_slot{};
    std::uint64_t target_generation{};
    std::vector<std::uint8_t> rgba8;
};

// Owns rendering resources only. No simulation or command authority crosses
// this boundary. Software is an explicit test backend, never a silent fallback.
class Renderer2D {
public:
    Renderer2D(SDL_Window* window, const std::filesystem::path& assets,
               RendererOptions options = {});
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;
    Point measure_text(std::string_view text, float size);
    // RGBA8 rows are tightly packed. Handles remain valid until explicitly
    // released; reuse increments the generation so stale draws are rejected.
    TextureHandle create_texture(Extent size, std::span<const std::uint8_t> rgba,
                                 TextureOptions options = {});
    // Base-level, tightly packed sampled data. R16_UNORM bytes are little-endian.
    // The software backend explicitly rejects this typed material path.
    TextureHandle create_sampled_texture(SampledTextureDesc desc,
                                         std::span<const std::uint8_t> bytes);
    void release_texture(TextureHandle texture);
    MaterialHandle create_sprite_material(const SpriteMaterialDesc& desc);
    void release_sprite_material(MaterialHandle material);
    // Immutable R8 contact art; caller serializes renderer access. Cold path,
    // never per-frame/unit. Frames store handles, not resolved GPU references.
    ContactArtHandle create_contact_art(const ContactArtDesc& desc,
                                        std::span<const std::uint8_t> bytes);
    // Throws if rendering, stale or idle wait fails; no mutation on failure.
    // Pending handle-only frames become stale before any GPU acquisition.
    void release_contact_art(ContactArtHandle art);
    const ContactArtStatus& contact_art_status() const;
    // Keeps a 900-unit-tall presentation and widens it for displays wider than
    // 16:10. Existing callers remain on the default 1440x900 until they opt in.
    Extent fitted_logical_size() const;
    void set_logical_size(Extent size);
    Extent logical_size() const;
    Point window_to_logical(Point point) const;
    Point logical_to_window(Point point) const;
    void convert_event(SDL_Event& event) const;
    // A requested capture reads the offscreen target and completes before return.
    // An empty path uses the normal asynchronous presentation path.
    void render(const Frame2D& frame, const std::filesystem::path& capture = {});
    // Synchronous diagnostic readback of the last successfully submitted
    // WaterCoverage V1 target. Never falls back to another graph/generation.
    WaterCoverageReadback read_water_coverage_v1();
    const DeviceInfo& device_info() const;
    // Only opaque geometry with explicit world positions/normals. Sprite
    // materials, shadows and the full Dynamic mode are not implied.
    bool surface_lighting_available() const;
    const WorldHdrStatus& world_hdr_status() const;
    const GroundForwardStatus& ground_forward_status() const;
    const SkyShadowStatus& sky_shadow_status() const;
    // Unsupported requests preserve the current rendering path and return false.
    // Inspect status before presenting a setting as enabled. Invalid modes throw.
    bool set_lighting_mode(LightingMode mode);
    const LightingStatus& lighting_status() const;
    bool set_presentation(PresentationRequest request);
    const PresentationStatus& presentation_status() const;
    DisplayInfo display_info() const;
    const RendererFrameTelemetry& frame_telemetry() const;
    void set_telemetry_enabled(bool enabled);
    bool has_submitted_frames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra::render

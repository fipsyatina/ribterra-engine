#pragma once

#include "ribterra/render_frame.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace ribterra::render {

enum class PresentMode { VSync, Immediate, Mailbox };
enum class PresentationUnavailableReason { None, UnsupportedBackend, UnsupportedMode, ApplyFailed };

struct PresentationRequest {
    PresentMode mode{PresentMode::VSync};
    std::uint32_t frame_limit{}; // 0 = uncapped; pacing belongs to the caller.
    std::uint32_t frames_in_flight{2};
    friend bool operator==(const PresentationRequest&, const PresentationRequest&) = default;
};

struct PresentationStatus {
    PresentationRequest requested{};
    PresentationRequest effective{};
    bool available{}; // GPU controls; caller-side frame_limit needs no GPU support.
    bool immediate_supported{};
    bool mailbox_supported{};
    PresentationUnavailableReason reason{PresentationUnavailableReason::None};
    std::string error;
};

struct DisplayInfo {
    Extent drawable{};
    float refresh_hz{}; // 0 = unavailable, never a guessed display refresh.
    float pixel_density{};
    float display_scale{};
};
enum class WorldGraphPass { GroundGBuffer, GroundLighting, WorldForward, Composite, Ui, GroundContact, SunShadow, MoonShadow, Reflection };

// CPU intervals and counters for a completed render call. Submission/queueing
// does not prove scan-out. SDL_GPU has no timestamp-query API in pinned SDL.
struct RendererFrameTelemetry {
    bool measured{};
    bool capture{};
    bool submitted{};
    bool presentation_queued{};
    double validation_ms{};
    double resource_wait_ms{};
    double preparation_ms{};
    double encoding_ms{};
    double swapchain_wait_ms{};
    double submit_ms{};
    double capture_ms{};
    double cpu_total_ms{};
    std::optional<double> gpu_execution_ms;
    std::uint64_t sequence{};
    std::uint64_t vertices{};
    std::uint64_t indices{};
    std::uint64_t draws{};
    std::uint64_t surface_draws{};
    std::uint64_t upload_bytes{};
    std::uint64_t ground_draws{}, material_draws{};
    std::uint64_t occluders{}, occluder_upload_bytes{};
    std::uint32_t frame_slot{}, graph_pass_count{};
    std::array<WorldGraphPass, 9> graph_passes{};
    std::uint64_t contact_casters{}, contact_draws{}, contact_mask_binds{};
    std::uint64_t water_draws{}, reflection_draws{};
};

} // namespace ribterra::render

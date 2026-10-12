#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ribterra::render {
namespace {

constexpr SDL_GPUTextureFormat target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat hdr_format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr SDL_GPUTextureFormat ground_albedo_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
constexpr SDL_GPUTextureFormat world_meta_format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
constexpr auto target_usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
constexpr auto sky_target_usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
constexpr std::uint32_t max_frame_count = 3;
constexpr std::uint32_t ground_frame_count = 2;
constexpr float max_surface_coordinate = 1048576.f;
constexpr float max_surface_radiance = 64.f;
constexpr float max_sprite_coordinate = 8192.f;
// RGBA16F stores components in [-32, 32] with <= 0.0078125 rounding error;
// the resulting three-axis position error remains below the 0.02 contract.
constexpr float max_ground_camera_coordinate = 32.f;
// Six bounded lights (sun + independent moon + four locals), ambient and emission stay below
// RGBA16F's finite maximum even with the wet-surface specular multiplier.
constexpr float max_sprite_specular_strength = .99f;
static_assert(sizeof(Vertex) == 20);
static_assert(offsetof(Vertex, uv) == 8);
static_assert(offsetof(Vertex, color) == 16);
static_assert(sizeof(SpriteSurfaceGpu) == 160);

constexpr bool is_ground_graph(WorldGraph graph) noexcept {
    return graph != WorldGraph::LegacyCombined;
}
constexpr bool has_moon_direct(WorldGraph graph) noexcept {
    return graph == WorldGraph::SkyContactSlice || graph == WorldGraph::SkyShadowSlice ||
           graph == WorldGraph::WaterReflectionSlice || graph == WorldGraph::WaterAtmosphereSlice;
}
constexpr bool has_sky_shadows(WorldGraph graph) noexcept {
    return graph == WorldGraph::SkyShadowSlice || graph == WorldGraph::WaterReflectionSlice ||
           graph == WorldGraph::WaterAtmosphereSlice;
}
constexpr bool has_water_reflection(WorldGraph graph) noexcept {
    return graph == WorldGraph::WaterReflectionSlice || graph == WorldGraph::WaterAtmosphereSlice;
}
constexpr bool has_atmosphere(WorldGraph graph) noexcept { return graph == WorldGraph::WaterAtmosphereSlice; }
constexpr bool is_contact_graph(WorldGraph graph) noexcept {
    return graph == WorldGraph::GroundContactSlice || has_moon_direct(graph);
}

std::uint32_t sampled_texture_bytes_per_pixel(SampledTextureFormat format) {
    switch (format) {
    case SampledTextureFormat::Rgba8UnormSrgb:
    case SampledTextureFormat::Rgba8Unorm: return 4;
    case SampledTextureFormat::R8Unorm: return 1;
    case SampledTextureFormat::R16Unorm: return 2;
    }
    throw std::invalid_argument("Invalid sampled texture format");
}

SDL_GPUTextureFormat sampled_texture_gpu_format(SampledTextureFormat format) {
    switch (format) {
    case SampledTextureFormat::Rgba8UnormSrgb: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    case SampledTextureFormat::Rgba8Unorm: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case SampledTextureFormat::R8Unorm: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case SampledTextureFormat::R16Unorm: return SDL_GPU_TEXTUREFORMAT_R16_UNORM;
    }
    throw std::invalid_argument("Invalid sampled texture format");
}

std::string error(std::string_view action) {
    return std::string(action) + ": " + SDL_GetError();
}

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

float srgb_byte_to_linear(std::uint8_t byte) {
    const float value = byte / 255.f;
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}

struct GpuTextureDeleter {
    SDL_GPUDevice* device{};
    void operator()(SDL_GPUTexture* texture) const noexcept {
        if (texture) SDL_ReleaseGPUTexture(device, texture);
    }
};
using OwnedGpuTexture = std::unique_ptr<SDL_GPUTexture, GpuTextureDeleter>;
struct GpuTransferDeleter {
    SDL_GPUDevice* device{};
    void operator()(SDL_GPUTransferBuffer* transfer) const noexcept {
        if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
    }
};
using OwnedGpuTransfer = std::unique_ptr<SDL_GPUTransferBuffer, GpuTransferDeleter>;
struct GpuFenceDeleter {
    SDL_GPUDevice* device{};
    void operator()(SDL_GPUFence* fence) const noexcept {
        if (fence) SDL_ReleaseGPUFence(device, fence);
    }
};
using OwnedGpuFence = std::unique_ptr<SDL_GPUFence, GpuFenceDeleter>;

void validate_presentation_request(PresentationRequest request) {
    switch (request.mode) {
    case PresentMode::VSync:
    case PresentMode::Immediate:
    case PresentMode::Mailbox:
        break;
    default:
        throw std::invalid_argument("Invalid presentation mode");
    }
    if (request.frames_in_flight < 1 || request.frames_in_flight > max_frame_count)
        throw std::invalid_argument("Frames in flight must be between 1 and 3");
    if (request.frame_limit > 1000)
        throw std::invalid_argument("Frame limit must be between 0 and 1000");
}

SDL_GPUPresentMode gpu_present_mode(PresentMode mode) {
    switch (mode) {
    case PresentMode::VSync: return SDL_GPU_PRESENTMODE_VSYNC;
    case PresentMode::Immediate: return SDL_GPU_PRESENTMODE_IMMEDIATE;
    case PresentMode::Mailbox: return SDL_GPU_PRESENTMODE_MAILBOX;
    }
    throw std::invalid_argument("Invalid presentation mode");
}

std::vector<std::uint8_t> load(const std::filesystem::path& path) {
    std::size_t size{};
    void* data = SDL_LoadFile(utf8(path).c_str(), &size);
    if (!data) throw std::runtime_error(error("Could not load " + utf8(path)));
    std::vector<std::uint8_t> result(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    SDL_free(data);
    return result;
}

struct Layout {
    float x{}, y{}, scale{1};
    int pixel_w{1}, pixel_h{1};
    float window_to_pixel_x{1}, window_to_pixel_y{1};
};

Layout layout(SDL_Window* window, Extent logical) {
    int ww = 1, wh = 1, pw = 1, ph = 1;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    ww = std::max(ww, 1); wh = std::max(wh, 1); pw = std::max(pw, 1); ph = std::max(ph, 1);
    const auto lw = static_cast<float>(std::max(logical.width, 1u));
    const auto lh = static_cast<float>(std::max(logical.height, 1u));
    const float scale = std::min(static_cast<float>(pw) / lw, static_cast<float>(ph) / lh);
    return {(pw - lw * scale) * .5f, (ph - lh * scale) * .5f, scale, pw, ph,
            static_cast<float>(pw) / ww, static_cast<float>(ph) / wh};
}

struct TextKey {
    std::string text;
    int size{};
    friend bool operator==(const TextKey&, const TextKey&) = default;
};
struct TextKeyHash {
    std::size_t operator()(const TextKey& key) const noexcept {
        return std::hash<std::string>{}(key.text) ^ (std::hash<int>{}(key.size) << 1);
    }
};

struct CpuText {
    SDL_Surface* surface{};
    float w{}, h{};
};

struct PreparedDraw {
    Pass pass{Pass::WorldOrdered};
    Blend blend{Blend::Alpha};
    TextureHandle texture{};
    std::uint32_t first{}, count{};
    Rect scissor{};
    bool scissor_enabled{};
    TextKey text_key{};
    bool text{};
    bool surface_lit{};
    std::uint32_t sprite_material_index{};
    std::uint32_t ground_material_index{};
};

struct PreparedFrame {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PreparedDraw> draws;
};

struct alignas(16) ReflectionMetadataGpu {
    std::array<float,4> response{};
    std::array<std::uint32_t,4> receiver{};
};
static_assert(sizeof(ReflectionMetadataGpu)==32 && offsetof(ReflectionMetadataGpu,receiver)==16);

int point_size(float size) {
    if (!std::isfinite(size)) throw std::invalid_argument("Text size must be finite");
    if (size <= 6) return 6;
    if (size >= 192) return 192;
    return static_cast<int>(std::lround(size));
}

bool unit_normal(const std::array<float, 3>& normal) {
    const float length_squared = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];
    return std::isfinite(length_squared) && std::abs(length_squared - 1.f) <= .001f;
}

void validate_surface_lighting(const SurfaceLightingFrame& lighting, bool local_visibility = false,
                               std::span<const OccluderGpu> occluders = {}) {
    const auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    const auto radiance = [&](const auto& values) {
        return finite(values) && std::all_of(values.begin(), values.end(),
            [](float value) { return value >= 0.f && value <= max_surface_radiance; });
    };
    if (!radiance(lighting.ambient) || lighting.ambient[3] != 0.f ||
        !radiance(lighting.sun_color_intensity) || !finite(lighting.sun_direction) ||
        lighting.sun_direction[3] != 0.f ||
        !unit_normal({lighting.sun_direction[0], lighting.sun_direction[1], lighting.sun_direction[2]}))
        throw std::invalid_argument("Surface ambient/sun values must be finite, bounded and normalized");
    if (lighting.counts[0] > surface_light_capacity || lighting.counts[1] || lighting.counts[2] || lighting.counts[3])
        throw std::invalid_argument("Surface light count exceeds the supported four-light contract");
    for (std::uint32_t index = 0; index < lighting.counts[0]; ++index) {
        const auto& light = lighting.lights[index];
        if (!finite(light.anchor_radius) || !finite(light.direction_cone) ||
            !radiance(light.color_intensity) || !finite(light.height_falloff_reveal))
            throw std::invalid_argument("Surface light values must be finite and radiance bounded");
        if (std::any_of(light.anchor_radius.begin(), light.anchor_radius.end(),
                        [](float value) { return std::abs(value) > max_surface_coordinate; }) || light.anchor_radius[3] <= 0.f)
            throw std::invalid_argument("Surface light position/radius is outside the render metric");
        if (!local_visibility && (light.occluder_range_flags[0] || light.occluder_range_flags[1] || light.occluder_range_flags[2] != 1u))
            throw std::invalid_argument("Surface kernel does not yet support occluders or receiver masks");
        if (local_visibility) {
            if (std::any_of(light.anchor_radius.begin(), light.anchor_radius.begin()+3,
                            [](float value) { return std::abs(value) > max_ground_camera_coordinate; }))
                throw std::invalid_argument("Local light anchor exceeds the camera-relative bound");
            const auto first = light.occluder_range_flags[0], count = light.occluder_range_flags[1];
            if (count > local_occluders_per_light)
                throw std::invalid_argument("Local occluder per-light overflow: maximum 32; no blockers discarded");
            if (first > occluders.size() || count > occluders.size() - first || (!count && first))
                throw std::invalid_argument("Local occluder light range is outside the prepared list");
            if (!light.occluder_range_flags[2] || light.occluder_range_flags[2] > 255u)
                throw std::invalid_argument("Local light receiver mask must be a nonzero low byte");
        }
        const float outer = light.direction_cone[3], inner = light.height_falloff_reveal[0];
        const bool point = outer == -1.f && inner == -1.f;
        if (!unit_normal({light.direction_cone[0], light.direction_cone[1], light.direction_cone[2]}) ||
            (!point && (outer < -1.f || inner > 1.f || inner <= outer)) ||
            light.height_falloff_reveal[1] <= 0.f || light.height_falloff_reveal[1] > 16.f ||
            light.height_falloff_reveal[2] < 0.f || light.height_falloff_reveal[2] > 1.f ||
            light.height_falloff_reveal[3] != 0.f)
            throw std::invalid_argument("Surface light direction, cone, falloff or reveal is invalid");
    }
}

void validate_frame_content(const Frame2D& frame) {
    if (frame.content == FrameContent::World) return;
    if (frame.content != FrameContent::UiOnly)
        throw std::invalid_argument("Frame contains an invalid content mode");
    if (frame.linear_hdr_world || frame.world_exposure != 1.f ||
        !frame.sprite_material_draws.empty() || !frame.ground_material_draws.empty() ||
        frame.water_surface || frame.atmosphere || frame.water_normal != TextureHandle{} ||
        !frame.reflection_draws.empty() || !frame.local_occluders.empty() ||
        !frame.contact_shadows.empty() || frame.contact_projection || frame.moon_lighting ||
        !frame.sky_shadow_triangles.empty())
        throw std::invalid_argument("UiOnly frames cannot contain world rendering payloads");

    // Compare semantic fields, including inactive light rows, without relying
    // on wrapper padding. Default metadata is inert on the SDR UI path.
    const SurfaceLightingFrame neutral_lighting{};
    const auto& lighting = frame.surface_lighting;
    if (lighting.ambient != neutral_lighting.ambient ||
        lighting.sun_direction != neutral_lighting.sun_direction ||
        lighting.sun_color_intensity != neutral_lighting.sun_color_intensity ||
        lighting.counts != neutral_lighting.counts)
        throw std::invalid_argument("UiOnly frames require default lighting metadata");
    for (std::size_t index = 0; index < lighting.lights.size(); ++index) {
        const auto& light = lighting.lights[index];
        const auto& neutral = neutral_lighting.lights[index];
        if (light.anchor_radius != neutral.anchor_radius ||
            light.direction_cone != neutral.direction_cone ||
            light.color_intensity != neutral.color_intensity ||
            light.occluder_range_flags != neutral.occluder_range_flags ||
            light.height_falloff_reveal != neutral.height_falloff_reveal)
            throw std::invalid_argument("UiOnly frames require default inactive light rows");
    }
    if (!frame.surface_vertices.empty()) {
        if (frame.surface_vertices.size() != frame.vertices.size())
            throw std::invalid_argument("UiOnly surface companions must be index-aligned");
        const SurfaceVertex neutral{};
        for (const auto& surface : frame.surface_vertices)
            if (surface.world_position != neutral.world_position || surface.normal != neutral.normal)
                throw std::invalid_argument("UiOnly surface companions must be canonical neutral values");
    }
    for (const auto& command : frame.commands) {
        if (std::visit([](const auto& value) { return value.pass; }, command) != Pass::Ui)
            throw std::invalid_argument("UiOnly frames accept only Ui-pass commands");
        if (const auto* draw = std::get_if<GeometryDraw>(&command);
            draw && (draw->surface_lit || draw->sprite_material_index || draw->ground_material_index))
            throw std::invalid_argument("UiOnly geometry cannot reference world materials or lighting");
    }
}

void validate_frame(const Frame2D& frame, bool local_visibility = false) {
    if (!local_visibility && !frame.local_occluders.empty())
        throw std::invalid_argument("Local occluders require GroundForwardSlice");
    if (!frame.logical_size.width || !frame.logical_size.height) throw std::invalid_argument("Logical frame size must be non-zero");
    if (!std::isfinite(frame.world_exposure) || frame.world_exposure <= 0.f || frame.world_exposure > 64.f ||
        (!frame.linear_hdr_world && frame.world_exposure != 1.f))
        throw std::invalid_argument("World exposure requires HDR and must be finite in (0, 64]");
    if (frame.linear_hdr_world && frame.clear.a != 255)
        throw std::invalid_argument("Linear HDR world requires an opaque scene clear");
    if (frame.vertices.size() > std::numeric_limits<std::uint32_t>::max()/sizeof(Vertex) ||
        frame.indices.size() > std::numeric_limits<std::uint32_t>::max()/sizeof(std::uint32_t))
        throw std::length_error("Frame geometry exceeds 32-bit limits");
    const std::uint64_t upload_bytes = std::uint64_t(frame.vertices.size())*sizeof(Vertex) +
        std::uint64_t(frame.indices.size())*sizeof(std::uint32_t) +
        std::uint64_t(frame.surface_vertices.size())*sizeof(SurfaceVertex);
    if (upload_bytes > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Combined frame upload exceeds 32-bit limits");
    for (const auto& vertex : frame.vertices)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
            throw std::invalid_argument("Frame vertices must contain finite coordinates");
    auto previous_pass = Pass::Terrain;
    bool surface_validated = false;
    for (const auto& command : frame.commands) {
        const auto pass = std::visit([](const auto& value) { return value.pass; }, command);
        if (pass < Pass::Terrain || pass > Pass::Ui) throw std::invalid_argument("Render command contains an invalid pass");
        if (pass < previous_pass) throw std::invalid_argument("Render command passes must be nondecreasing");
        previous_pass = pass;
        if (const auto* draw = std::get_if<GeometryDraw>(&command)) {
        if (draw->blend < Blend::Opaque || draw->blend > Blend::PremultipliedAlpha) throw std::invalid_argument("GeometryDraw contains an invalid blend mode");
        if (!std::isfinite(draw->scissor.x) || !std::isfinite(draw->scissor.y) ||
            !std::isfinite(draw->scissor.w) || !std::isfinite(draw->scissor.h) || draw->scissor.w < 0 || draw->scissor.h < 0)
            throw std::invalid_argument("GeometryDraw scissor must be finite and non-negative");
        if (draw->first_index > frame.indices.size() || draw->index_count > frame.indices.size() - draw->first_index)
            throw std::out_of_range("GeometryDraw index range is outside the frame");
        if (draw->index_count % 3) throw std::invalid_argument("GeometryDraw index count must describe triangles");
        if (draw->surface_lit) {
            if (draw->pass > Pass::WorldOrdered || draw->blend != Blend::Opaque)
                throw std::invalid_argument("Surface lighting currently requires opaque world geometry");
            if (frame.surface_vertices.size() != frame.vertices.size())
                throw std::invalid_argument("Surface lighting requires an index-aligned world position/normal stream");
            if (!surface_validated) { validate_surface_lighting(frame.surface_lighting, local_visibility, frame.local_occluders); surface_validated = true; }
        }
        if (draw->sprite_material_index) {
            if (!frame.linear_hdr_world)
                throw std::invalid_argument("Sprite materials require the linear HDR world path");
            if (draw->sprite_material_index > frame.sprite_material_draws.size())
                throw std::out_of_range("GeometryDraw sprite material index is outside the frame");
            if (draw->pass > Pass::WorldOrdered || draw->blend != Blend::Alpha)
                throw std::invalid_argument("Sprite materials require alpha-blended world geometry");
            if (draw->surface_lit)
                throw std::invalid_argument("Sprite material and surface-lit paths are mutually exclusive");
            if (!surface_validated) { validate_surface_lighting(frame.surface_lighting, local_visibility, frame.local_occluders); surface_validated = true; }
        }
        for (std::uint32_t i = 0; i < draw->index_count; ++i)
            if (const auto vertex_index = frame.indices[draw->first_index + i]; vertex_index >= frame.vertices.size())
                throw std::out_of_range("GeometryDraw contains an invalid vertex index");
            else if (draw->surface_lit) {
                const auto& surface = frame.surface_vertices[vertex_index];
                if (!unit_normal(surface.normal) || std::any_of(surface.world_position.begin(), surface.world_position.end(),
                    [](float value) { return !std::isfinite(value) || std::abs(value) > max_surface_coordinate; }))
                    throw std::invalid_argument("Surface vertices require finite positions and unit normals");
                if (frame.vertices[vertex_index].color.a != 255)
                    throw std::invalid_argument("Surface lighting currently requires opaque vertex coverage");
            } else if (draw->sprite_material_index) {
                const auto color = frame.vertices[vertex_index].color;
                if (color.r != 255 || color.g != 255 || color.b != 255)
                    throw std::invalid_argument("Sprite materials require white RGB vertices with alpha coverage");
                const auto uv = frame.vertices[vertex_index].uv;
                if (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f)
                    throw std::invalid_argument("Sprite material vertices require local [0,1] UVs");
            }
    } else {
        const auto& text = std::get<TextDraw>(command);
        if (text.text.size() > 65536) throw std::length_error("TextDraw exceeds 64 KiB");
        if (!std::isfinite(text.position.x) || !std::isfinite(text.position.y) ||
            !std::isfinite(text.max_width) || text.max_width < 0) throw std::invalid_argument("TextDraw coordinates must be finite");
        (void)point_size(text.size);
        }
    }
    const auto finite4 = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    for (const auto& draw : frame.sprite_material_draws) {
        const auto& surface = draw.surface;
        if (!std::isfinite(draw.emission_scale) || draw.emission_scale < 0.f || draw.emission_scale > 1.f)
            throw std::invalid_argument("Sprite emission scale must be finite and within [0, 1]");
        if (!finite4(surface.anchor_export) || !finite4(surface.source_size_pivot) ||
            !finite4(surface.depth_scale) || !finite4(surface.response))
            throw std::invalid_argument("Sprite surface values must be finite");
        if (std::any_of(surface.anchor_export.begin(), surface.anchor_export.begin()+3,
                        [](float value) { return std::abs(value) > max_sprite_coordinate; }) ||
            surface.anchor_export[3] < .001f || surface.anchor_export[3] > 64.f ||
            surface.source_size_pivot[0] > 4096.f || surface.source_size_pivot[1] > 4096.f ||
            surface.source_size_pivot[0] <= 0.f ||
            surface.source_size_pivot[1] <= 0.f || surface.source_size_pivot[2] < 0.f ||
            surface.source_size_pivot[3] < 0.f ||
            surface.source_size_pivot[2] > surface.source_size_pivot[0] ||
            surface.source_size_pivot[3] > surface.source_size_pivot[1] ||
            surface.depth_scale[1] <= surface.depth_scale[0] ||
            std::abs(surface.depth_scale[0]) > max_sprite_coordinate ||
            std::abs(surface.depth_scale[1]) > max_sprite_coordinate ||
            surface.depth_scale[2] <= 0.f || surface.depth_scale[2] > 64.f ||
            surface.depth_scale[3] != 0.f ||
            surface.response[0] < 0.f || surface.response[0] > 1.f ||
            surface.response[1] < 0.f || surface.response[1] > max_sprite_specular_strength ||
            surface.response[2] != 0.f || surface.response[3] != 0.f)
            throw std::invalid_argument("Sprite surface geometry, emission or response is invalid");
        // P is affine in local UV/depth. Bounding the eight corners bounds the
        // entire sampled region before any GPU submission/division overflow.
        for (float x : {0.f, surface.source_size_pivot[0]})
            for (float y : {0.f, surface.source_size_pivot[1]})
                for (float depth : {surface.depth_scale[0], surface.depth_scale[1]}) {
                    auto point = reconstruct_render_xyz(reference_offset_from_source_pixel(
                        {x, y}, {surface.source_size_pivot[2], surface.source_size_pivot[3]},
                        surface.anchor_export[3]), depth);
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        const float value = surface.anchor_export[axis] + point[axis] * surface.depth_scale[2];
                        if (!std::isfinite(value) || std::abs(value) > max_sprite_coordinate)
                            throw std::invalid_argument("Reconstructed sprite position exceeds the render metric bound");
                    }
                }
    }
}

void validate_local_occluders(std::span<const OccluderGpu> occluders) {
    if (occluders.size() > local_occluder_capacity)
        throw std::invalid_argument("Local occluder global overflow: maximum 256; no blockers discarded");
    std::unordered_set<std::uint32_t> stable_ids;
    for (const auto& blocker : occluders) {
        const auto bounded = [](const auto& values) {
            return std::all_of(values.begin(), values.end(), [](float value) {
                return std::isfinite(value) && std::abs(value) <= max_ground_camera_coordinate;
            });
        };
        const auto& center = blocker.center_base;
        const auto& axis_x = blocker.axis_x_extent;
        const auto& axis_y = blocker.axis_y_radius;
        const auto& flags = blocker.shape_flags_id;
        if (!bounded(center) || !bounded(axis_x) || !bounded(axis_y) || center[2] != 0.f ||
            axis_x[3] <= center[3] || flags[1] != 1u || !flags[2] || !flags[3] ||
            !stable_ids.insert(flags[2]).second)
            throw std::invalid_argument("Local occluder bounds/reveal/identity are invalid");
        if (!unit_normal({axis_x[0],axis_x[1],0.f}))
            throw std::invalid_argument("Local occluder primary axis must be normalized XY");
        if (axis_x[3]-center[3] < local_occluder_min_dimension)
            throw std::invalid_argument("Local occluder height must be at least 0.0001");
        float extent_x{}, extent_y{};
        if (flags[0] == static_cast<std::uint32_t>(OccluderShape::Obb)) {
            if (axis_x[2] <= 0.f || axis_y[2] <= 0.f || axis_y[3] != 0.f ||
                !unit_normal({axis_y[0],axis_y[1],0.f}) ||
                std::abs(axis_x[0]*axis_y[0]+axis_x[1]*axis_y[1]) > .001f)
                throw std::invalid_argument("Local OBB requires positive extents and orthonormal XY axes");
            if (axis_x[2] < local_occluder_min_dimension || axis_y[2] < local_occluder_min_dimension)
                throw std::invalid_argument("Local OBB half extents must be at least 0.0001");
            extent_x = std::abs(axis_x[0])*axis_x[2] + std::abs(axis_y[0])*axis_y[2];
            extent_y = std::abs(axis_x[1])*axis_x[2] + std::abs(axis_y[1])*axis_y[2];
        } else if (flags[0] == static_cast<std::uint32_t>(OccluderShape::Capsule)) {
            if (axis_x[2] < 0.f || axis_y[0] != 0.f || axis_y[1] != 0.f ||
                axis_y[2] != 0.f || axis_y[3] <= 0.f)
                throw std::invalid_argument("Local capsule requires nonnegative half length and positive radius");
            if (axis_y[3] < local_occluder_min_dimension)
                throw std::invalid_argument("Local capsule radius must be at least 0.0001");
            extent_x = std::abs(axis_x[0])*axis_x[2] + axis_y[3];
            extent_y = std::abs(axis_x[1])*axis_x[2] + axis_y[3];
        } else {
            throw std::invalid_argument("Local occluder shape is unsupported");
        }
        if (std::abs(center[0])+extent_x > max_ground_camera_coordinate ||
            std::abs(center[1])+extent_y > max_ground_camera_coordinate)
            throw std::invalid_argument("Local occluder footprint exceeds the camera-relative bound");
    }
}

void validate_water_contract(const Frame2D& frame, bool water_graph) {
    const bool has_payload=frame.water_surface.has_value() || frame.water_normal != TextureHandle{} ||
                           !frame.reflection_draws.empty();
    if (!water_graph) {
        if (has_payload)
            throw std::invalid_argument("Water payload requires the explicit WaterReflectionSlice graph");
        return;
    }
    if (!frame.water_surface || frame.water_normal == TextureHandle{})
        throw std::invalid_argument("WaterReflectionSlice requires water constants and a typed normal texture");
    if (frame.reflection_draws.size()>32)
        throw std::invalid_argument("WaterReflectionSlice reflection caster capacity is 32");
    const auto& water=*frame.water_surface;
    const auto finite=[](const auto& values){return std::all_of(values.begin(),values.end(),
        [](float value){return std::isfinite(value);});};
    if (!finite(water.world_origin_time) || !finite(water.flow_response) ||
        !finite(water.tint) || !finite(water.viewport))
        throw std::invalid_argument("Water constants must be finite");
    const float flow_length=water.flow_response[0]*water.flow_response[0]+
                            water.flow_response[1]*water.flow_response[1];
    if (std::abs(water.world_origin_time[0])>8192.f || std::abs(water.world_origin_time[1])>8192.f ||
        water.world_origin_time[2]<0.f || water.world_origin_time[3]<0.f || water.world_origin_time[3]>1.f ||
        std::abs(flow_length-1.f)>.001f || water.flow_response[2]<0.f || water.flow_response[2]>.34f ||
        water.flow_response[3]<0.f || water.flow_response[3]>1.f ||
        std::any_of(water.tint.begin(),water.tint.begin()+3,[](float value){return value<0.f||value>64.f;}) ||
        water.tint[3]<0.f || water.tint[3]>1.f || water.viewport[0]<0.f || water.viewport[0]>1.5f ||
        water.viewport[1]!=900.f || water.viewport[2]!=0.f || water.viewport[3]!=0.f)
        throw std::invalid_argument("Water flow, response, tint or distortion contract is invalid");
    for (const auto& reflection:frame.reflection_draws) {
        const auto& draw=reflection.draw;
        if (!std::isfinite(reflection.strength) || !std::isfinite(reflection.fade) ||
            reflection.strength<0.f || reflection.strength>.34f || reflection.fade<1.f || reflection.fade>16.f)
            throw std::invalid_argument("Reflection strength/fade is outside the bounded contract");
        if (draw.pass!=Pass::WorldOrdered || draw.blend!=Blend::Alpha || draw.texture!=TextureHandle{} ||
            !draw.sprite_material_index || draw.sprite_material_index>frame.sprite_material_draws.size() ||
            draw.ground_material_index || draw.surface_lit || !draw.index_count || draw.index_count%3 ||
            draw.first_index>frame.indices.size() || draw.index_count>frame.indices.size()-draw.first_index)
            throw std::invalid_argument("Reflection draw must be schema-2 mirrored world triangles only");
        if (!std::isfinite(draw.scissor.x) || !std::isfinite(draw.scissor.y) ||
            !std::isfinite(draw.scissor.w) || !std::isfinite(draw.scissor.h) ||
            draw.scissor.w<0.f || draw.scissor.h<0.f)
            throw std::invalid_argument("Reflection scissor must be finite and non-negative");
        for (std::uint32_t i=0;i<draw.index_count;++i) {
            const auto vertex_index=frame.indices[draw.first_index+i];
            if (vertex_index>=frame.vertices.size())
                throw std::out_of_range("Reflection draw contains an invalid vertex index");
            const auto& vertex=frame.vertices[vertex_index];
            if (vertex.color.r!=255 || vertex.color.g!=255 || vertex.color.b!=255 ||
                vertex.uv.x<0.f || vertex.uv.x>1.f || vertex.uv.y<0.f || vertex.uv.y>1.f)
                throw std::invalid_argument("Reflection geometry requires white RGB, alpha coverage and authored local UVs");
        }
    }
}

void validate_atmosphere_contract(const Frame2D& frame, bool atmosphere_graph) {
    if (!atmosphere_graph) {
        if (frame.atmosphere)
            throw std::invalid_argument("Atmosphere payload requires the explicit WaterAtmosphereSlice graph");
        return;
    }
    if (!frame.atmosphere)
        throw std::invalid_argument("WaterAtmosphereSlice requires an explicit atmosphere payload");
    validate_atmosphere_gpu(*frame.atmosphere);
}

void validate_ground_forward_frame(const Frame2D& frame, bool contact_graph = false,
                                   bool water_graph = false) {
    if (!frame.linear_hdr_world)
        throw std::invalid_argument("GroundForwardSlice requires the linear HDR world path");
    if (frame.surface_vertices.size() != frame.vertices.size())
        throw std::invalid_argument("GroundForwardSlice requires an index-aligned position/normal stream");
    validate_local_occluders(frame.local_occluders);
    validate_surface_lighting(frame.surface_lighting, true, frame.local_occluders);
    if (contact_graph) {
        if (!frame.contact_projection)
            throw std::invalid_argument("GroundContactSlice requires an explicit canonical projection");
        validate_contact_projection(*frame.contact_projection);
        validate_contact_shadows(frame.contact_shadows);
    } else if (frame.contact_projection || !frame.contact_shadows.empty()) {
        throw std::invalid_argument("Contact data requires the explicit GroundContactSlice graph");
    }

    const auto finite_unit = [](float value) {
        return std::isfinite(value) && value >= 0.f && value <= 1.f;
    };
    for (const auto& material : frame.ground_material_draws) {
        (void)contact_receiver_tag(material.contact_receiver);
        if (!contact_graph && material.contact_receiver != ContactReceiver::Ordinary)
            throw std::invalid_argument("Contact receiver classification requires GroundContactSlice");
        if (!std::all_of(material.surface.response.begin(), material.surface.response.end(), finite_unit) ||
            material.surface.flags[0] > 255u || material.surface.flags[1] ||
            material.surface.flags[2] || material.surface.flags[3] ||
            !finite_unit(material.surface.visibility[0]) ||
            !finite_unit(material.surface.visibility[1]) ||
            material.surface.visibility[2] != 0.f || material.surface.visibility[3] != 0.f)
            throw std::invalid_argument("Ground material metadata must be finite, bounded and reserved-zero");
        const auto surface_class=material.water_surface_class;
        if (surface_class!=GroundSurfaceClass::Land && surface_class!=GroundSurfaceClass::Water &&
            surface_class!=GroundSurfaceClass::BridgeDeck)
            throw std::invalid_argument("Ground water surface class is invalid");
        if (!std::isfinite(material.geometric_coverage) || material.geometric_coverage<0.f ||
            material.geometric_coverage>1.f || material.shore_edges>15u)
            throw std::invalid_argument("Ground water coverage/shore metadata is invalid");
        if (!water_graph && (surface_class!=GroundSurfaceClass::Land || material.shore_edges ||
                             material.geometric_coverage!=1.f))
            throw std::invalid_argument("Water ground metadata requires WaterReflectionSlice");
        if (water_graph) {
            if ((material.surface.response[3]!=0.f && material.surface.response[3]!=1.f) ||
                (material.geometric_coverage!=0.f && material.geometric_coverage!=1.f))
                throw std::invalid_argument("WaterCoverage V1 requires binary authored reveal and coverage");
            if (surface_class==GroundSurfaceClass::Water) {
                if (material.contact_receiver!=ContactReceiver::Ordinary)
                    throw std::invalid_argument("Water is never a contact-shadow receiver");
            } else if (material.shore_edges || material.geometric_coverage!=0.f) {
                throw std::invalid_argument("Land and bridge coverage/shore channels must be zero in WaterCoverage V1");
            }
        }
    }
    for (const auto& material : frame.sprite_material_draws) {
        if (!finite_unit(material.reveal) || !material.receiver_mask || material.receiver_mask > 255u)
            throw std::invalid_argument("Forward reveal metadata must be finite and bounded");
        if (water_graph && material.reveal!=0.f && material.reveal!=1.f)
            throw std::invalid_argument("WaterReflectionSlice requires binary sprite reveal");
        const auto& surface=material.surface;
        for (float x:{0.f,surface.source_size_pivot[0]})
            for (float y:{0.f,surface.source_size_pivot[1]})
                for (float depth:{surface.depth_scale[0],surface.depth_scale[1]}) {
                    const auto point=reconstruct_render_xyz(reference_offset_from_source_pixel(
                        {x,y},{surface.source_size_pivot[2],surface.source_size_pivot[3]},
                        surface.anchor_export[3]),depth);
                    for (std::size_t axis=0;axis<3;++axis) {
                        const float value=surface.anchor_export[axis]+point[axis]*surface.depth_scale[2];
                        if (!std::isfinite(value) || std::abs(value)>max_ground_camera_coordinate)
                            throw std::invalid_argument("GroundForwardSlice sprite reconstruction exceeds the camera-relative bound");
                    }
                }
    }

    bool has_ground = false;
    for (const auto& command : frame.commands) {
        const auto pass = std::visit([](const auto& value) { return value.pass; }, command);
        if (pass == Pass::Atmosphere || pass == Pass::Fog)
            throw std::invalid_argument("GroundForwardSlice does not accept Atmosphere or Fog compatibility commands");
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw) {
            if (pass != Pass::Ui)
                throw std::invalid_argument("GroundForwardSlice text commands must remain in the UI pass");
            continue;
        }
        if ((draw->ground_material_index != 0) + (draw->sprite_material_index != 0) +
                (draw->surface_lit ? 1 : 0) > 1)
            throw std::invalid_argument("GroundForwardSlice material roles are mutually exclusive");
        if (draw->pass == Pass::Terrain) {
            if (!draw->ground_material_index || draw->sprite_material_index || draw->surface_lit ||
                draw->blend != Blend::Opaque || draw->texture != TextureHandle{})
                throw std::invalid_argument("GroundForwardSlice terrain requires one typed ground material and opaque white geometry");
            if (draw->ground_material_index > frame.ground_material_draws.size())
                throw std::out_of_range("GeometryDraw ground material index is outside the frame");
            has_ground = has_ground || draw->index_count != 0;
        } else if (draw->pass == Pass::WorldOrdered) {
            if (!draw->sprite_material_index || draw->ground_material_index || draw->surface_lit ||
                draw->blend != Blend::Alpha || draw->texture != TextureHandle{})
                throw std::invalid_argument("GroundForwardSlice world geometry requires a schema-2 alpha material");
        } else if (draw->pass == Pass::Ui) {
            if (draw->ground_material_index || draw->sprite_material_index || draw->surface_lit)
                throw std::invalid_argument("GroundForwardSlice UI must use the unchanged UI path");
        } else {
            throw std::invalid_argument("GroundForwardSlice contains an unsupported command pass");
        }

        if (!draw->ground_material_index) continue;
        for (std::uint32_t index = 0; index < draw->index_count; ++index) {
            const auto vertex_index = frame.indices[draw->first_index + index];
            const auto color = frame.vertices[vertex_index].color;
            const auto& surface = frame.surface_vertices[vertex_index];
            if (color.r != 255 || color.g != 255 || color.b != 255 || color.a != 255)
                throw std::invalid_argument("GroundForwardSlice terrain vertices require opaque white tint");
            if (!unit_normal(surface.normal) || std::any_of(
                    surface.world_position.begin(), surface.world_position.end(), [](float value) {
                        return !std::isfinite(value) || std::abs(value) > max_ground_camera_coordinate;
                    }))
                throw std::invalid_argument("GroundForwardSlice terrain requires finite positions and unit normals");
            if (contact_graph) {
                const auto expected = contact_project_logical(surface.world_position, *frame.contact_projection);
                const auto actual = frame.vertices[vertex_index].position;
                constexpr float tolerance = 1.f / 1024.f;
                if (std::abs(actual.x - expected[0]) > tolerance ||
                    std::abs(actual.y - expected[1]) > tolerance)
                    throw std::invalid_argument("Contact ground vertex does not match canonical projection");
                const auto receiver = frame.ground_material_draws[draw->ground_material_index - 1].contact_receiver;
                if (receiver != ContactReceiver::Ordinary && surface.normal[2] < contact_shadow_min_upward_normal)
                    throw std::invalid_argument("Contact receivers require an upward supported normal");
            }
            if (water_graph && frame.ground_material_draws[draw->ground_material_index-1].water_surface_class==
                    GroundSurfaceClass::Water && surface.world_position[2]!=0.f)
                throw std::invalid_argument("WaterReflectionSlice water receivers must lie on render Z zero");
        }
    }
    if (!has_ground)
        throw std::invalid_argument("GroundForwardSlice requires at least one ground draw");
}

template<class Raster>
// Both backend entry points validate before any rendering/resource mutation.
PreparedFrame prepare_validated(const Frame2D& frame, Raster&& raster) {
    PreparedFrame out{frame.vertices, frame.indices, {}};
    out.draws.reserve(frame.commands.size());
    bool previous_command_was_geometry = false;
    for (const auto& command : frame.commands) {
        if (const auto* geometry = std::get_if<GeometryDraw>(&command)) {
            if (geometry->index_count == 0) {
                previous_command_was_geometry = false;
                continue;
            }
            const auto same_scissor = [](const PreparedDraw& previous, const GeometryDraw& next) {
                return previous.scissor_enabled == next.scissor_enabled &&
                    (!next.scissor_enabled ||
                     (previous.scissor.x == next.scissor.x && previous.scissor.y == next.scissor.y &&
                      previous.scissor.w == next.scissor.w && previous.scissor.h == next.scissor.h));
            };
            if (previous_command_was_geometry && !out.draws.empty()) {
                auto& previous = out.draws.back();
                if (!previous.text && previous.pass == geometry->pass && previous.blend == geometry->blend &&
                    previous.texture == geometry->texture && same_scissor(previous, *geometry) &&
                    previous.surface_lit == geometry->surface_lit &&
                    previous.sprite_material_index == geometry->sprite_material_index &&
                    previous.ground_material_index == geometry->ground_material_index &&
                    std::uint64_t(previous.first) + previous.count == geometry->first_index) {
                    previous.count += geometry->index_count;
                    continue;
                }
            }
            out.draws.push_back({geometry->pass, geometry->blend, geometry->texture,
                                 geometry->first_index, geometry->index_count, geometry->scissor,
                                 geometry->scissor_enabled, {}, false, geometry->surface_lit,
                                 geometry->sprite_material_index, geometry->ground_material_index});
            previous_command_was_geometry = true;
            continue;
        }
        previous_command_was_geometry = false;
        const auto& text = std::get<TextDraw>(command);
        if (text.text.empty()) continue;
        if (out.vertices.size() > std::numeric_limits<std::uint32_t>::max()/sizeof(Vertex)-4 ||
            out.indices.size() > std::numeric_limits<std::uint32_t>::max()/sizeof(std::uint32_t)-6)
            throw std::length_error("Text geometry exceeds 32-bit limits");
        TextKey key{text.text, point_size(text.size)};
        const auto extent = raster(key);
        if (extent.x <= 0 || extent.y <= 0) continue;
        float w = extent.x, h = extent.y;
        if (text.max_width > 0 && w > text.max_width) { h *= text.max_width / w; w = text.max_width; }
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        const auto first = static_cast<std::uint32_t>(out.indices.size());
        out.vertices.insert(out.vertices.end(), {
            {{text.position.x, text.position.y}, {0,0}, text.color},
            {{text.position.x+w, text.position.y}, {1,0}, text.color},
            {{text.position.x+w, text.position.y+h}, {1,1}, text.color},
            {{text.position.x, text.position.y+h}, {0,1}, text.color}});
        out.indices.insert(out.indices.end(), {base,base+1,base+2,base,base+2,base+3});
        out.draws.push_back({text.pass, Blend::Alpha, {}, first, 6, {}, false, std::move(key), true});
    }
    return out;
}

    SDL_Surface* raster_text(TTF_Font* font, const TextKey& key) {
    SDL_Surface* rendered = TTF_RenderText_Blended(font, key.text.data(), key.text.size(), {255,255,255,255});
    if (!rendered) throw std::runtime_error(error("Could not rasterize text"));
    SDL_Surface* rgba = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(rendered);
    if (!rgba) throw std::runtime_error(error("Could not convert text surface"));
    return rgba;
}

SDL_Rect pixel_scissor(const Rect& value, const Layout& l, Extent logical) {
    const auto bounded=[](float value,float high){return std::clamp(value,0.f,high);};
    const int x = static_cast<int>(std::floor(bounded(l.x + value.x * l.scale,static_cast<float>(l.pixel_w))));
    const int y = static_cast<int>(std::floor(bounded(l.y + value.y * l.scale,static_cast<float>(l.pixel_h))));
    const int right = static_cast<int>(std::ceil(bounded(l.x + (value.x + value.w) * l.scale,static_cast<float>(l.pixel_w))));
    const int bottom = static_cast<int>(std::ceil(bounded(l.y + (value.y + value.h) * l.scale,static_cast<float>(l.pixel_h))));
    const int lx = static_cast<int>(std::floor(bounded(l.x,static_cast<float>(l.pixel_w))));
    const int ly = static_cast<int>(std::floor(bounded(l.y,static_cast<float>(l.pixel_h))));
    const int lr = static_cast<int>(std::ceil(bounded(l.x + l.scale * logical.width,static_cast<float>(l.pixel_w))));
    const int lb = static_cast<int>(std::ceil(bounded(l.y + l.scale * logical.height,static_cast<float>(l.pixel_h))));
    const int clipped_x = std::clamp(x, lx, lr), clipped_y = std::clamp(y, ly, lb);
    return {clipped_x, clipped_y, std::max(0, std::min(right, lr)-clipped_x), std::max(0, std::min(bottom, lb)-clipped_y)};
}

} // namespace

std::uint32_t texture_mip_level_count(Extent size, TextureOptions options) noexcept {
    constexpr std::uint32_t max_levels=4;
    const auto requested=std::clamp(options.mip_levels,1u,max_levels);
    std::uint32_t available=1;
    for(auto dimension=std::max(size.width,size.height);dimension>1&&available<max_levels;dimension>>=1)
        ++available;
    return std::min(requested,available);
}

struct Renderer2D::Impl {
    SDL_Window* window{};
    std::filesystem::path assets;
    RendererOptions options;
    DeviceInfo info;
    LightingStatus lighting;
    PresentationStatus presentation;
    RendererFrameTelemetry telemetry;
    std::uint64_t telemetry_sequence{};
    Extent logical{};
    std::unordered_map<int, TTF_Font*> fonts;
    bool ttf_initialized{};

    SDL_Renderer* software{};
    struct SoftwareText { SDL_Texture* texture{}; float w{}, h{}; };
    std::unordered_map<TextKey, SoftwareText, TextKeyHash> software_text;

    SDL_GPUDevice* gpu{};
    bool window_claimed{};
    SDL_GPUSampler* sampler{};
    SDL_GPUSampler* mip_sampler{};
    SDL_GPUGraphicsPipeline* pipelines[4]{};
    SDL_GPUGraphicsPipeline* surface_pipeline{};
    SDL_GPUGraphicsPipeline* world_linear_pipelines[4]{};
    SDL_GPUGraphicsPipeline* surface_hdr_pipeline{};
    SDL_GPUGraphicsPipeline* material_sprite_pipeline{};
    SDL_GPUGraphicsPipeline* hdr_composite_pipeline{};
    SDL_GPUGraphicsPipeline* ground_gbuffer_pipeline{};
    SDL_GPUGraphicsPipeline* ground_lighting_pipeline{};
    SDL_GPUGraphicsPipeline* ground_contact_pipeline{};
    SDL_GPUGraphicsPipeline* material_forward_pipeline{};
    SDL_GPUGraphicsPipeline* water_reflection_pipeline{};
    SDL_GPUGraphicsPipeline* sky_depth_pipeline{};
    SDL_GPUSampler* ground_point_sampler{};
    SDL_GPUSampler* water_repeat_sampler{};
    WorldHdrStatus hdr_status;
    GroundForwardStatus ground_status;
    struct HdrTargets {
        std::array<OwnedGpuTexture, max_frame_count> world;
        std::array<OwnedGpuTexture, max_frame_count> final;
        int width{}, height{};
        std::uint32_t count{};
    } hdr_targets;
    struct GroundTargets {
        struct Set {
            OwnedGpuTexture ground_albedo;
            OwnedGpuTexture surface_world;
            OwnedGpuTexture surface_material;
            OwnedGpuTexture surface_flags;
            OwnedGpuTexture water_coverage;
            OwnedGpuTexture reflection;
            OwnedGpuTexture scene;
            OwnedGpuTexture world_meta;
            OwnedGpuTexture final;
        };
        std::array<Set, ground_frame_count> sets;
        int width{}, height{};
    } ground_targets;
    struct SkyTargets {
        struct Set { std::array<OwnedGpuTexture, 2> maps; };
        std::array<Set, ground_frame_count> sets;
    } sky_targets;
    SkyShadowStatus sky_status;
    std::uint64_t hdr_target_creation_attempts{};
    std::uint64_t ground_target_creation_attempts{};
    std::uint64_t sampled_texture_creation_attempts{};
    bool submitted_frames{};
    SDL_GPUTexture* white{};
    SDL_GPUTexture* offscreen{};
    int offscreen_w{}, offscreen_h{};
    struct GpuText { SDL_GPUTexture* texture{}; float w{}, h{}; };
    std::unordered_map<TextKey, GpuText, TextKeyHash> gpu_text;
    struct TextureSlot {
        std::uint32_t generation{1};
        bool alive{};
        bool typed_sampled{};
        bool opaque_alpha{};
        bool mipmapped{};
        SampledTextureFormat format{SampledTextureFormat::Rgba8Unorm};
        SDL_Texture* software{};
        SDL_GPUTexture* gpu{};
        Extent sampled_extent{0, 0};
    };
    std::vector<TextureSlot> textures{{}};
    std::vector<std::uint32_t> free_texture_slots;
    struct MaterialSlot {
        std::uint32_t generation{1};
        bool alive{};
        SpriteMaterialDesc desc{};
    };
    std::vector<MaterialSlot> materials{{}};
    std::vector<std::uint32_t> free_material_slots;
    struct ContactSlot {
        std::uint32_t generation{1};
        bool alive{};
        ContactArtDesc desc{};
        OwnedGpuTexture texture;
    };
    std::vector<ContactSlot> contacts;
    std::vector<std::uint32_t> free_contact_slots;
    ContactArtStatus contact_status;
    bool render_active{}; // Renderer methods are caller-serialized, not thread-safe.
    struct FrameResources {
        SDL_GPUBuffer* vertices{};
        SDL_GPUBuffer* indices{};
        SDL_GPUBuffer* surfaces{};
        SDL_GPUBuffer* occluders{};
        SDL_GPUBuffer* sky_vertices{};
        std::uint32_t vertex_capacity{}, index_capacity{}, surface_capacity{}, occluder_capacity{}, sky_capacity{};
        // Reused only after this owner's fence completes. Text uploads and
        // capture downloads remain separate, short-lived transfer buffers.
        SDL_GPUTransferBuffer* upload{};
        std::uint32_t upload_capacity{};
        SDL_GPUFence* fence{};
        std::vector<SDL_GPUTransferBuffer*> transfers;
    } frames[max_frame_count];
    std::uint32_t frame_index{};
    bool water_readback_valid{};
    std::uint32_t water_readback_slot{};
    std::uint64_t water_readback_generation{};

    struct CommandGuard {
        SDL_GPUCommandBuffer* command{};
        bool can_cancel{true};
        ~CommandGuard(){if(command&&can_cancel)SDL_CancelGPUCommandBuffer(command);}
        void swapchain_acquired(){can_cancel=false;}
        SDL_GPUCommandBuffer* release(){auto* result=command;command=nullptr;return result;}
    };

    Impl(SDL_Window* value, std::filesystem::path asset_path, RendererOptions renderer_options)
        : window(value), assets(std::move(asset_path)), options(renderer_options) {
        if (!window) throw std::invalid_argument("Renderer2D requires a window");
        if (options.world_graph != WorldGraph::LegacyCombined &&
            options.world_graph != WorldGraph::GroundForwardSlice &&
            options.world_graph != WorldGraph::GroundContactSlice &&
            options.world_graph != WorldGraph::SkyContactSlice &&
            options.world_graph != WorldGraph::SkyShadowSlice &&
            options.world_graph != WorldGraph::WaterReflectionSlice &&
            options.world_graph != WorldGraph::WaterAtmosphereSlice)
            throw std::invalid_argument("Invalid world graph");
        if (options.backend == Backend::Software &&
            options.world_graph != WorldGraph::LegacyCombined)
            throw std::invalid_argument("GroundForwardSlice requires the GPU backend");
        validate_presentation_request(options.presentation);
        if (!set_lighting_mode(options.lighting_mode))
            throw std::invalid_argument("Requested lighting mode is not implemented");
        if (!TTF_Init()) throw std::runtime_error(error("Could not initialize SDL_ttf"));
        ttf_initialized = true;
        textures.reserve(4097);
        free_texture_slots.reserve(4096);
        materials.reserve(4097);
        free_material_slots.reserve(4096);
        contacts.reserve(257); contacts.emplace_back();
        free_contact_slots.reserve(256);
        try {
            if (options.backend == Backend::Software) {
                create_software();
                presentation = {options.presentation, {}, false, false, false,
                                PresentationUnavailableReason::None, {}};
                if (!set_presentation(options.presentation))
                    throw std::runtime_error(presentation.error);
            } else {
                create_gpu();
            }
        }
        catch (...) { shutdown(); throw; }
    }

    ~Impl() { shutdown(); }

    bool set_lighting_mode(LightingMode mode) {
        if (mode != LightingMode::Legacy && mode != LightingMode::Dynamic)
            throw std::invalid_argument("Invalid lighting mode");
        // Capability remains false until the dynamic path and its assets exist.
        // No extra GPU resources or shader changes are made by this scaffold.
        lighting = {mode, LightingMode::Legacy, false,
                    mode == LightingMode::Dynamic ? LightingUnavailableReason::NotImplemented
                                                  : LightingUnavailableReason::None};
        return mode == lighting.effective;
    }

    bool present_mode_supported(PresentMode mode) const {
        if (mode == PresentMode::VSync) return true;
        if (mode == PresentMode::Immediate) return presentation.immediate_supported;
        if (mode == PresentMode::Mailbox) return presentation.mailbox_supported;
        return false;
    }

    void release_all_frames() {
        for (auto& frame : frames) release_frame(frame);
        frame_index = 0;
    }

    bool set_presentation(PresentationRequest request) {
        validate_presentation_request(request);
        presentation.requested = request;
        presentation.reason = PresentationUnavailableReason::None;
        presentation.error.clear();
        if (!gpu) {
            presentation.available = false;
            // The application owns FPS pacing; only mode/queue depth need GPU
            // support. Do not partially apply the limit of a failed mixed request.
            if (request.mode == PresentMode::VSync && request.frames_in_flight == 2) {
                presentation.effective = request;
                return true;
            }
            presentation.reason = PresentationUnavailableReason::UnsupportedBackend;
            presentation.error = "Presentation controls are unavailable on the software backend";
            return false;
        }
        if (!present_mode_supported(request.mode)) {
            presentation.reason = PresentationUnavailableReason::UnsupportedMode;
            presentation.error = "Requested GPU presentation mode is unsupported";
            return false;
        }

        const auto previous = presentation.effective;
        if (request == previous) return true;
        if (request.mode == previous.mode && request.frames_in_flight == previous.frames_in_flight) {
            presentation.effective = request;
            return true;
        }
        try {
            release_all_frames();
        } catch (const std::exception& exception) {
            presentation.reason = PresentationUnavailableReason::ApplyFailed;
            presentation.error = exception.what();
            return false;
        }

        const bool mode_changed = request.mode != previous.mode;
        if (mode_changed && !SDL_SetGPUSwapchainParameters(
                gpu, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, gpu_present_mode(request.mode))) {
            presentation.reason = PresentationUnavailableReason::ApplyFailed;
            presentation.error = error("Could not set GPU presentation mode");
            return false;
        }
        if (request.frames_in_flight != previous.frames_in_flight &&
            !SDL_SetGPUAllowedFramesInFlight(gpu, request.frames_in_flight)) {
            const auto apply_error = error("Could not set GPU frames in flight");
            if (!mode_changed || SDL_SetGPUSwapchainParameters(
                    gpu, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, gpu_present_mode(previous.mode))) {
                presentation.effective = previous;
                presentation.error = apply_error;
            } else {
                presentation.effective = previous;
                presentation.effective.mode = request.mode;
                presentation.error = apply_error + "; rollback failed: " + SDL_GetError();
            }
            presentation.reason = PresentationUnavailableReason::ApplyFailed;
            if (mode_changed && hdr_composite_pipeline) refresh_hdr_swapchain_format();
            return false;
        }
        presentation.effective = request;
        if (mode_changed && hdr_composite_pipeline) refresh_hdr_swapchain_format();
        return true;
    }

    void begin_telemetry(bool capture) {
        telemetry = {};
        telemetry.measured = options.collect_telemetry;
        telemetry.capture = capture;
    }

    void finish_telemetry(const RendererFrameTelemetry& measured) {
        telemetry = measured;
        telemetry.sequence = ++telemetry_sequence;
    }

    void shutdown() noexcept {
        for (auto& texture : textures) if (texture.software) { SDL_DestroyTexture(texture.software); texture.software=nullptr; }
        for (auto& [_, item] : software_text) SDL_DestroyTexture(item.texture);
        software_text.clear();
        if (software) SDL_DestroyRenderer(software);
        software = nullptr;
        if (gpu) {
            SDL_WaitForGPUIdle(gpu);
            ground_targets = {};
            sky_targets = {};
            hdr_targets = {};
            for (auto& frame : frames) {
                if (frame.fence) SDL_ReleaseGPUFence(gpu, frame.fence);
                for (auto* transfer : frame.transfers) SDL_ReleaseGPUTransferBuffer(gpu, transfer);
                if (frame.vertices) SDL_ReleaseGPUBuffer(gpu, frame.vertices);
                if (frame.indices) SDL_ReleaseGPUBuffer(gpu, frame.indices);
                if (frame.surfaces) SDL_ReleaseGPUBuffer(gpu, frame.surfaces);
                if (frame.occluders) SDL_ReleaseGPUBuffer(gpu, frame.occluders);
                if (frame.sky_vertices) SDL_ReleaseGPUBuffer(gpu, frame.sky_vertices);
                if (frame.upload) SDL_ReleaseGPUTransferBuffer(gpu, frame.upload);
            }
            for (auto& [_, item] : gpu_text) SDL_ReleaseGPUTexture(gpu, item.texture);
            for (auto& texture : textures) if (texture.gpu) { SDL_ReleaseGPUTexture(gpu,texture.gpu); texture.gpu=nullptr; }
            for (auto& contact : contacts) contact.texture.reset();
            if (offscreen) SDL_ReleaseGPUTexture(gpu, offscreen);
            if (white) SDL_ReleaseGPUTexture(gpu, white);
            for (auto* pipeline : pipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
            if (surface_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, surface_pipeline);
            for (auto* pipeline : world_linear_pipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
            if (surface_hdr_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, surface_hdr_pipeline);
            if (material_sprite_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, material_sprite_pipeline);
            if (hdr_composite_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, hdr_composite_pipeline);
            if (ground_gbuffer_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, ground_gbuffer_pipeline);
            if (ground_lighting_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, ground_lighting_pipeline);
            if (ground_contact_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, ground_contact_pipeline);
            if (material_forward_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, material_forward_pipeline);
            if (water_reflection_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, water_reflection_pipeline);
            if (sky_depth_pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, sky_depth_pipeline);
            if (water_repeat_sampler) SDL_ReleaseGPUSampler(gpu, water_repeat_sampler);
            if (ground_point_sampler) SDL_ReleaseGPUSampler(gpu, ground_point_sampler);
            if (mip_sampler) SDL_ReleaseGPUSampler(gpu, mip_sampler);
            if (sampler) SDL_ReleaseGPUSampler(gpu, sampler);
            if (window_claimed) SDL_ReleaseWindowFromGPUDevice(gpu, window);
            SDL_DestroyGPUDevice(gpu);
            gpu = nullptr;
        }
        for (auto& [_, font] : fonts) TTF_CloseFont(font);
        fonts.clear();
        if (ttf_initialized) { TTF_Quit(); ttf_initialized = false; }
    }

    TTF_Font* font(int size) {
        if (const auto it = fonts.find(size); it != fonts.end()) return it->second;
        const auto path = utf8(assets / "fonts" / "NotoSans-Regular.ttf");
        TTF_Font* result = TTF_OpenFont(path.c_str(), static_cast<float>(size));
        if (!result) throw std::runtime_error(error("Could not open Noto Sans"));
        fonts.emplace(size, result);
        return result;
    }

    Point measure(std::string_view text, float size) {
        int w{}, h{};
        const int pixels = point_size(size);
        if (!TTF_GetStringSize(font(pixels), text.data(), text.size(), &w, &h)) throw std::runtime_error(error("Could not measure text"));
        return {static_cast<float>(w), static_cast<float>(h)};
    }

    const TextureSlot& texture(TextureHandle handle) const {
        if (handle.slot == 0 || handle.slot >= textures.size()) throw std::invalid_argument("Texture handle has no allocated slot");
        const auto& slot=textures[handle.slot];
        if (!slot.alive || slot.generation != handle.generation) throw std::invalid_argument("Texture handle is stale or released");
        return slot;
    }

    const MaterialSlot& material(MaterialHandle handle) const {
        if (handle.slot == 0 || handle.slot >= materials.size())
            throw std::invalid_argument("Material handle has no allocated slot");
        const auto& slot = materials[handle.slot];
        if (!slot.alive || slot.generation != handle.generation)
            throw std::invalid_argument("Material handle is stale or released");
        return slot;
    }

    const ContactSlot& contact_art(ContactArtHandle handle) const {
        if (!handle.slot || handle.slot >= contacts.size())
            throw std::invalid_argument("Contact art has no allocated slot");
        const auto& slot = contacts[handle.slot];
        if (!slot.alive || slot.generation != handle.generation)
            throw std::invalid_argument("Contact art is stale or released");
        return slot;
    }

    ContactArtHandle create_contact_art(const ContactArtDesc& desc, std::span<const std::uint8_t> bytes) {
        if (render_active || !gpu || !is_contact_graph(options.world_graph))
            throw std::invalid_argument("Contact art requires an idle GroundContactSlice renderer");
        validate_contact_art(desc, bytes);
        constexpr std::uint64_t logical_budget = 16u * 1024u * 1024u;
        if (contact_status.live_arts >= 256 || bytes.size() > logical_budget - contact_status.logical_bytes)
            throw std::length_error("Contact art registry exceeds its provisional live budget");
        if (free_contact_slots.empty() && contacts.size() >= 257)
            throw std::length_error("Contact art slots exhausted (wrapped generations are retired)");
        if (!SDL_GPUTextureSupportsFormat(gpu, SDL_GPU_TEXTUREFORMAT_R8_UNORM,
                                         SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER))
            throw std::runtime_error("Contact art R8 sampling is unavailable");
        const auto attempt = ++contact_status.creation_attempts;
        if (attempt == options.test_fail_contact_art_creation_at) {
            options.test_fail_contact_art_creation_at = 0;
            throw std::runtime_error("Injected contact art creation failure");
        }
        const bool fail_upload = attempt == options.test_fail_contact_art_upload_at;
        if (fail_upload) options.test_fail_contact_art_upload_at = 0;
        const SampledTextureDesc sampled{{desc.region.texture_width, desc.region.texture_height},
                                         SampledTextureFormat::R8Unorm};
        OwnedGpuTexture candidate(upload_sampled_texture(sampled, bytes, fail_upload), GpuTextureDeleter{gpu});
        // Capacity is reserved at construction. All fallible upload work precedes publication.
        const auto index = free_contact_slots.empty() ? static_cast<std::uint32_t>(contacts.size()) :
                                                       free_contact_slots.back();
        if (free_contact_slots.empty()) contacts.emplace_back();
        else free_contact_slots.pop_back();
        auto& slot = contacts[index];
        slot.desc = desc; slot.texture = std::move(candidate); slot.alive = true;
        ++contact_status.live_arts; contact_status.logical_bytes += bytes.size();
        return {index, slot.generation};
    }

    void release_contact_art(ContactArtHandle handle) {
        if (render_active) throw std::logic_error("Contact art release requires a cold renderer phase");
        auto& slot = contacts.at(handle.slot); // Full handle validation occurs before any mutation.
        (void)contact_art(handle);
        if (options.test_fail_contact_art_idle_waits) {
            --options.test_fail_contact_art_idle_waits;
            throw std::runtime_error("Injected contact art idle wait failure");
        }
        if (!SDL_WaitForGPUIdle(gpu)) throw std::runtime_error(error("Contact art idle wait failed"));
        contact_status.logical_bytes -= std::uint64_t(slot.desc.region.texture_width) * slot.desc.region.texture_height;
        --contact_status.live_arts; slot.texture.reset(); slot.alive = false;
        // Never wrap: an ancient pending frame must never match a new art (ABA).
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slot.generation; free_contact_slots.push_back(handle.slot);
        }
    }

    void validate_textures(const Frame2D& frame) const {
        for (const auto& command : frame.commands) if (const auto* draw=std::get_if<GeometryDraw>(&command))
            if (draw->texture.slot != 0 || draw->texture.generation != 0) {
                const auto& slot = texture(draw->texture);
                if (!draw->sprite_material_index && slot.typed_sampled)
                    throw std::invalid_argument("Typed sampled textures require a material draw, not a legacy geometry binding");
            }
        for (const auto& draw : frame.sprite_material_draws) {
            const auto& stored = material(draw.material).desc;
            for (const auto& channel : stored.channels) (void)texture(channel.texture);
        }
        for (const auto& draw : frame.ground_material_draws) {
            const auto& albedo = texture(draw.albedo);
            if (!albedo.typed_sampled || albedo.format != SampledTextureFormat::Rgba8UnormSrgb ||
                !albedo.opaque_alpha)
                throw std::invalid_argument("Ground material albedo must be a typed opaque sRGB texture");
        }
        if (frame.water_normal != TextureHandle{}) {
            const auto& normal=texture(frame.water_normal);
            if (!normal.typed_sampled || normal.format!=SampledTextureFormat::Rgba8Unorm)
                throw std::invalid_argument("Water normal must be a typed linear RGBA8 texture");
        }
        for (const auto& draw : frame.contact_shadows) {
            const auto& stored = contact_art(draw.art).desc;
            if (draw.surface.uv_region != contact_mask_uv(stored.region) ||
                (stored.facing != ContactFacing::Omnidirectional && stored.facing != draw.facing))
                throw std::invalid_argument("Contact draw UV/facing does not match authoritative art");
        }
    }

    TextureHandle store_texture(SDL_Texture* software_texture, SDL_GPUTexture* gpu_texture,
                                bool mipmapped, SampledTextureFormat format, bool typed_sampled = false,
                                Extent sampled_extent = {0, 0}, bool opaque_alpha = false) {
        if (!free_texture_slots.empty()) {
            const auto index=free_texture_slots.back(); free_texture_slots.pop_back();
            auto& slot=textures[index]; slot.alive=true;slot.mipmapped=mipmapped;slot.format=format;
            slot.typed_sampled=typed_sampled;
            slot.opaque_alpha=opaque_alpha;
            slot.sampled_extent=sampled_extent;
            slot.software=software_texture;slot.gpu=gpu_texture;
            return {index,slot.generation};
        }
        if (textures.size() >= 4097) throw std::length_error("Renderer texture registry is full");
        textures.push_back({1,true,typed_sampled,opaque_alpha,mipmapped,format,software_texture,gpu_texture,sampled_extent});
        return {static_cast<std::uint32_t>(textures.size()-1),1};
    }

    TextureHandle create_texture(Extent size,std::span<const std::uint8_t> rgba,TextureOptions texture_options) {
        constexpr std::uint32_t max_dimension=16384;
        if (!size.width || !size.height || size.width>max_dimension || size.height>max_dimension)
            throw std::invalid_argument("Texture extent must be between 1 and 16384 pixels");
        const std::uint64_t bytes=static_cast<std::uint64_t>(size.width)*size.height*4u;
        if (bytes>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Texture exceeds the 32-bit upload limit");
        if (rgba.size()!=static_cast<std::size_t>(bytes)) throw std::invalid_argument("Texture data is not tightly packed RGBA8");
        const auto mip_levels=texture_mip_level_count(size,texture_options);
        SDL_Texture* sw{};SDL_GPUTexture* gt{};
        if (software) {
            sw=SDL_CreateTexture(software,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,static_cast<int>(size.width),static_cast<int>(size.height));
            if(!sw)throw std::runtime_error(error("Could not create software texture"));
            if(!SDL_UpdateTexture(sw,nullptr,rgba.data(),static_cast<int>(size.width*4u))){SDL_DestroyTexture(sw);throw std::runtime_error(error("Could not upload software texture"));}
            SDL_SetTextureScaleMode(sw,SDL_SCALEMODE_LINEAR);
        } else {
            SDL_Surface* surface=SDL_CreateSurfaceFrom(static_cast<int>(size.width),static_cast<int>(size.height),SDL_PIXELFORMAT_RGBA32,
                                                       const_cast<std::uint8_t*>(rgba.data()),static_cast<int>(size.width*4u));
            if(!surface)throw std::runtime_error(error("Could not wrap GPU texture upload"));
            try { gt=upload_texture(surface,mip_levels); } catch(...) { SDL_DestroySurface(surface);throw; }
            SDL_DestroySurface(surface);
        }
        try { return store_texture(sw,gt,mip_levels>1,SampledTextureFormat::Rgba8Unorm); }
        catch(...) {if(sw)SDL_DestroyTexture(sw);if(gt)SDL_ReleaseGPUTexture(gpu,gt);throw;}
    }

    TextureHandle create_sampled_texture(SampledTextureDesc desc,
                                         std::span<const std::uint8_t> bytes) {
        constexpr std::uint32_t max_dimension=16384;
        if (software)
            throw std::invalid_argument("Typed sampled textures are unavailable on the software backend");
        if (!desc.size.width || !desc.size.height || desc.size.width>max_dimension ||
            desc.size.height>max_dimension)
            throw std::invalid_argument("Sampled texture extent must be between 1 and 16384 pixels");
        const auto bytes_per_pixel=sampled_texture_bytes_per_pixel(desc.format);
        const std::uint64_t byte_count=std::uint64_t(desc.size.width)*desc.size.height*bytes_per_pixel;
        if (byte_count>std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Sampled texture exceeds the 32-bit upload limit");
        if (bytes.size()!=static_cast<std::size_t>(byte_count))
            throw std::invalid_argument("Sampled texture data has an invalid byte count");
        const auto format=sampled_texture_gpu_format(desc.format);
        if (!SDL_GPUTextureSupportsFormat(gpu,format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER))
            throw std::runtime_error("Sampled texture format does not support SAMPLER usage");
        if (++sampled_texture_creation_attempts == options.test_fail_sampled_texture_creation_at) {
            options.test_fail_sampled_texture_creation_at=0;
            throw std::runtime_error("Injected sampled texture candidate creation failure");
        }
        SDL_GPUTexture* candidate=upload_sampled_texture(desc,bytes);
        const bool opaque_alpha=(desc.format!=SampledTextureFormat::Rgba8UnormSrgb &&
                                 desc.format!=SampledTextureFormat::Rgba8Unorm) ||
            [&]{for(std::size_t index=3;index<bytes.size();index+=4)if(bytes[index]!=255)return false;return true;}();
        try { return store_texture(nullptr,candidate,false,desc.format,true,desc.size,opaque_alpha); }
        catch (...) { SDL_ReleaseGPUTexture(gpu,candidate); throw; }
    }

    void release_texture(TextureHandle handle) {
        auto& slot=const_cast<TextureSlot&>(texture(handle));
        if(slot.software)SDL_DestroyTexture(slot.software);
        if(slot.gpu)SDL_ReleaseGPUTexture(gpu,slot.gpu);
        slot.software=nullptr;slot.gpu=nullptr;slot.alive=false;slot.opaque_alpha=false;slot.mipmapped=false;
        if(++slot.generation==0)++slot.generation;
        free_texture_slots.push_back(handle.slot);
    }

    MaterialHandle create_sprite_material(const SpriteMaterialDesc& desc) {
        if (!hdr_status.available)
            throw std::invalid_argument("Sprite materials require an available linear HDR world path: " +
                                        hdr_status.reason);
        constexpr std::array expected{
            SampledTextureFormat::Rgba8UnormSrgb, SampledTextureFormat::Rgba8Unorm,
            SampledTextureFormat::R16Unorm, SampledTextureFormat::R8Unorm,
            SampledTextureFormat::Rgba8Unorm, SampledTextureFormat::R8Unorm};
        auto validated = desc;
        for (std::size_t index=0; index<desc.channels.size(); ++index) {
            const auto& binding=desc.channels[index];
            const auto& region=binding.uv;
            if (!std::isfinite(region.x) || !std::isfinite(region.y) ||
                !std::isfinite(region.w) || !std::isfinite(region.h) ||
                region.x<0.f || region.y<0.f || region.w<=0.f || region.h<=0.f ||
                region.x+region.w>1.f || region.y+region.h>1.f)
                throw std::invalid_argument("Sprite material UV region must be a finite normalized rectangle");
            const auto& channel_texture = texture(binding.texture);
            if (!channel_texture.typed_sampled || channel_texture.format != expected[index])
                throw std::invalid_argument("Sprite material channel texture format is incompatible");
            const auto pixel_aligned = [](float value) {
                return std::abs(value - std::round(value)) <= .001f;
            };
            const float width = float(channel_texture.sampled_extent.width);
            const float height = float(channel_texture.sampled_extent.height);
            if (std::round(region.w * width) < 1.f || std::round(region.h * height) < 1.f ||
                !pixel_aligned(region.x * width) || !pixel_aligned(region.y * height) ||
                !pixel_aligned(region.w * width) || !pixel_aligned(region.h * height))
                throw std::invalid_argument("Sprite material UV region must cover whole aligned texels");
            // Normalize accepted floating-point roundoff to exact whole-texel
            // geometry, including one-texel regions at the tolerance boundary.
            validated.channels[index].uv = {
                std::round(region.x * width) / width, std::round(region.y * height) / height,
                std::round(region.w * width) / width, std::round(region.h * height) / height};
        }
        if (std::any_of(desc.emission.begin(),desc.emission.end(),[](float value) {
                return !std::isfinite(value) || value<0.f || value>max_surface_radiance;
            }))
            throw std::invalid_argument("Sprite material emission must be finite and bounded");
        if (!free_material_slots.empty()) {
            const auto index=free_material_slots.back();free_material_slots.pop_back();
            auto& slot=materials[index];slot.alive=true;slot.desc=validated;
            return {index,slot.generation};
        }
        if (materials.size()>=4097) throw std::length_error("Renderer material registry is full");
        materials.push_back({1,true,validated});
        return {static_cast<std::uint32_t>(materials.size()-1),1};
    }

    void release_sprite_material(MaterialHandle handle) {
        auto& slot=const_cast<MaterialSlot&>(material(handle));
        slot.alive=false;slot.desc={};
        if (++slot.generation==0) ++slot.generation;
        free_material_slots.push_back(handle.slot);
    }

    void create_software() {
        software = SDL_CreateRenderer(window, "software");
        if (!software) throw std::runtime_error(error("Could not create explicit software renderer"));
        if (!SDL_SetRenderLogicalPresentation(software, logical.width, logical.height, SDL_LOGICAL_PRESENTATION_LETTERBOX))
            throw std::runtime_error(error("Could not configure software logical presentation"));
        SDL_SetRenderDrawBlendMode(software, SDL_BLENDMODE_BLEND);
        info = {"software", SDL_GetRendererName(software) ? SDL_GetRendererName(software) : "software", "SDL_Renderer", true};
    }

    SoftwareText& software_raster(const TextKey& key) {
        if (const auto it = software_text.find(key); it != software_text.end()) return it->second;
        SDL_Surface* surface = raster_text(font(key.size), key);
        SDL_Texture* texture = SDL_CreateTextureFromSurface(software, surface);
        const SoftwareText item{texture, static_cast<float>(surface->w), static_cast<float>(surface->h)};
        SDL_DestroySurface(surface);
        if (!texture) throw std::runtime_error(error("Could not create software text texture"));
        return software_text.emplace(key, item).first->second;
    }

    void render_software(const Frame2D& frame, const std::filesystem::path& capture) {
        if (frame.contact_projection || !frame.contact_shadows.empty())
            throw std::invalid_argument("Contact rendering requires the explicit GPU graph");
        begin_telemetry(!capture.empty());
        RendererFrameTelemetry measured{};
        measured.measured = options.collect_telemetry;
        measured.capture = !capture.empty();
        const auto total_start = measured.measured ? Clock::now() : Clock::time_point{};
        auto phase_start = total_start;
        const bool ground_graph=options.world_graph==WorldGraph::GroundForwardSlice;
        validate_frame(frame, ground_graph);
        if (ground_graph) validate_ground_forward_frame(frame);
        if (ground_graph)
            throw std::invalid_argument("GroundForwardSlice is unavailable on the software backend");
        if (!frame.ground_material_draws.empty() || std::any_of(
                frame.commands.begin(),frame.commands.end(),[](const auto& command){
                    const auto* draw=std::get_if<GeometryDraw>(&command);
                    return draw && draw->ground_material_index;
                }))
            throw std::invalid_argument("Ground materials require GroundForwardSlice");
        if (frame.linear_hdr_world)
            throw std::invalid_argument("Linear HDR world is unavailable on the software backend");
        for (const auto& command : frame.commands)
            if (const auto* draw = std::get_if<GeometryDraw>(&command); draw && draw->surface_lit)
                throw std::invalid_argument("Surface lighting is unavailable on the software backend");
        validate_textures(frame);
        logical = frame.logical_size;
        if (measured.measured) {
            const auto now = Clock::now();
            measured.validation_ms = elapsed_ms(phase_start, now);
            phase_start = now;
        }
        if (software_text.size() > 1024) {
            for (auto& [_, item] : software_text) SDL_DestroyTexture(item.texture);
            software_text.clear();
        }
        SDL_SetRenderLogicalPresentation(software, logical.width, logical.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        if (measured.measured) {
            const auto now = Clock::now();
            measured.resource_wait_ms = elapsed_ms(phase_start, now);
            phase_start = now;
        }
        const auto prepared = prepare_validated(frame, [&](const TextKey& key) { const auto& t=software_raster(key); return Point{t.w,t.h}; });
        if (measured.measured) {
            measured.vertices = prepared.vertices.size();
            measured.indices = prepared.indices.size();
            measured.draws = prepared.draws.size();
            measured.surface_draws = std::count_if(prepared.draws.begin(), prepared.draws.end(),
                                                   [](const auto& draw) { return draw.surface_lit; });
            if (frame.content == FrameContent::UiOnly) {
                measured.graph_pass_count = 1;
                measured.graph_passes[0] = WorldGraphPass::Ui;
            }
            const auto now = Clock::now();
            measured.preparation_ms = elapsed_ms(phase_start, now);
            phase_start = now;
        }
        SDL_SetRenderDrawColor(software, frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a);
        SDL_RenderClear(software);
        std::vector<SDL_Vertex> vertices; vertices.reserve(prepared.vertices.size());
        for (const auto& v : prepared.vertices) vertices.push_back({{v.position.x,v.position.y},
            {v.color.r/255.f,v.color.g/255.f,v.color.b/255.f,v.color.a/255.f},{v.uv.x,v.uv.y}});
        std::vector<int> indices(prepared.indices.begin(), prepared.indices.end());
        for (const auto& draw : prepared.draws) {
            if (draw.scissor_enabled) {
                SDL_Rect rect{static_cast<int>(draw.scissor.x),static_cast<int>(draw.scissor.y),
                              static_cast<int>(draw.scissor.w),static_cast<int>(draw.scissor.h)};
                SDL_SetRenderClipRect(software, &rect);
            } else SDL_SetRenderClipRect(software, nullptr);
            const auto blend=draw.blend==Blend::Additive?SDL_BLENDMODE_ADD:draw.blend==Blend::Opaque?SDL_BLENDMODE_NONE:
                             draw.blend==Blend::PremultipliedAlpha?SDL_BLENDMODE_BLEND_PREMULTIPLIED:SDL_BLENDMODE_BLEND;
            if (!SDL_SetRenderDrawBlendMode(software,blend))
                throw std::runtime_error(error("Software draw blend mode is unsupported"));
            SDL_Texture* texture = draw.text ? software_raster(draw.text_key).texture :
                                   (draw.texture.slot?textures[draw.texture.slot].software:nullptr);
            if(texture && !SDL_SetTextureBlendMode(texture,blend))
                throw std::runtime_error(error("Software texture blend mode is unsupported"));
            if (!SDL_RenderGeometry(software, texture, vertices.data(), static_cast<int>(vertices.size()),
                                    indices.data()+draw.first, static_cast<int>(draw.count)))
                throw std::runtime_error(error("Software geometry draw failed"));
        }
        SDL_SetRenderClipRect(software, nullptr);
        if (measured.measured) {
            const auto now = Clock::now();
            measured.encoding_ms = elapsed_ms(phase_start, now);
            phase_start = now;
        }
        if (!capture.empty()) {
            if (capture.has_parent_path()) std::filesystem::create_directories(capture.parent_path());
            if (!SDL_FlushRenderer(software)) throw std::runtime_error(error("Software capture flush failed"));
            if (!SDL_SetRenderLogicalPresentation(software, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED))
                throw std::runtime_error(error("Could not expose full software framebuffer for capture"));
            SDL_Surface* surface = SDL_RenderReadPixels(software, nullptr);
            const std::string read_error = SDL_GetError();
            const bool restored = SDL_SetRenderLogicalPresentation(software, logical.width, logical.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
            if (!surface) throw std::runtime_error("Software capture read failed: " + read_error);
            if (!restored) { SDL_DestroySurface(surface); throw std::runtime_error(error("Could not restore software logical presentation")); }
            const bool ok = capture.extension() == ".bmp" ? SDL_SaveBMP(surface, utf8(capture).c_str()) : SDL_SavePNG(surface, utf8(capture).c_str());
            SDL_DestroySurface(surface);
            if (!ok) throw std::runtime_error(error("Software capture save failed"));
        }
        if (measured.measured && !capture.empty()) {
            const auto now = Clock::now();
            measured.capture_ms = elapsed_ms(phase_start, now);
            phase_start = now;
        }
        SDL_RenderPresent(software);
        submitted_frames = true;
        measured.submitted = true;
        measured.presentation_queued = true;
        if (measured.measured) {
            const auto now = Clock::now();
            measured.submit_ms = elapsed_ms(phase_start, now);
            measured.cpu_total_ms = elapsed_ms(total_start, now);
        }
        finish_telemetry(measured);
    }

    SDL_GPUShader* shader(const std::filesystem::path& path, SDL_GPUShaderStage stage,
                          std::uint32_t fragment_uniforms = 0, bool fullscreen = false,
                          std::uint32_t fragment_samplers = 1, std::uint32_t fragment_storage_buffers = 0) {
        // Pinned SDL3 also uses fixed-size arrays for this limit when debug is
        // disabled; never rely on its debug-only validation to prevent overflow.
        if (fragment_uniforms > 4u) throw std::invalid_argument("SDL_GPU supports at most four shader uniform buffers per stage");
        const auto bytes = load(path);
        const SDL_GPUShaderCreateInfo create{bytes.size(),bytes.data(),"main",SDL_GPU_SHADERFORMAT_DXIL,stage,
            stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? fragment_samplers : 0u,0,
            stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? fragment_storage_buffers : 0u,
            stage == SDL_GPU_SHADERSTAGE_VERTEX ? (fullscreen ? 0u : 1u) : fragment_uniforms,0};
        SDL_GPUShader* result = SDL_CreateGPUShader(gpu, &create);
        if (!result) throw std::runtime_error(error("Could not create GPU shader " + utf8(path)));
        return result;
    }

    SDL_GPUGraphicsPipeline* pipeline(SDL_GPUShader* vs, SDL_GPUShader* fs, Blend blend, bool surface = false,
                                     SDL_GPUTextureFormat format = target_format, bool fullscreen = false) {
        const SDL_GPUVertexBufferDescription buffers[]{
            {0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0},
            {1,sizeof(SurfaceVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0}};
        const SDL_GPUVertexAttribute attributes[]{
            {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,position)},
            {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,uv)},
            {2,0,SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,offsetof(Vertex,color)},
            {3,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,world_position)},
            {4,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,normal)}};
        SDL_GPUColorTargetDescription target{}; target.format=format;
        if (blend != Blend::Opaque) {
            auto& b=target.blend_state; b.enable_blend=true;
            b.src_color_blendfactor=blend==Blend::PremultipliedAlpha?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            b.dst_color_blendfactor=blend==Blend::Additive?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.color_blend_op=SDL_GPU_BLENDOP_ADD;
            b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            b.dst_alpha_blendfactor=blend==Blend::Additive?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        }
        SDL_GPUGraphicsPipelineCreateInfo create{};
        create.vertex_shader=vs; create.fragment_shader=fs;
        if (!fullscreen) create.vertex_input_state={buffers,surface ? 2u : 1u,attributes,surface ? 5u : 3u};
        create.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        create.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL; create.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        create.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        create.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        create.target_info={&target,1,SDL_GPU_TEXTUREFORMAT_INVALID,false,0,0,0};
        SDL_GPUGraphicsPipeline* result=SDL_CreateGPUGraphicsPipeline(gpu,&create);
        if(!result) throw std::runtime_error(error("Could not create GPU pipeline"));
        return result;
    }

    SDL_GPUGraphicsPipeline* ground_pipeline(SDL_GPUShader* vs, SDL_GPUShader* fs,
                                             std::span<SDL_GPUColorTargetDescription> targets,
                                             bool surface, bool fullscreen) {
        const SDL_GPUVertexBufferDescription buffers[]{
            {0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0},
            {1,sizeof(SurfaceVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0}};
        const SDL_GPUVertexAttribute attributes[]{
            {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,position)},
            {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,uv)},
            {2,0,SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,offsetof(Vertex,color)},
            {3,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,world_position)},
            {4,1,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(SurfaceVertex,normal)}};
        SDL_GPUGraphicsPipelineCreateInfo create{};
        create.vertex_shader=vs;create.fragment_shader=fs;
        if (!fullscreen)
            create.vertex_input_state={buffers,surface ? 2u : 1u,attributes,surface ? 5u : 3u};
        create.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        create.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        create.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        create.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        create.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        create.target_info={targets.data(),static_cast<Uint32>(targets.size()),
                            SDL_GPU_TEXTUREFORMAT_INVALID,false,0,0,0};
        auto* result=SDL_CreateGPUGraphicsPipeline(gpu,&create);
        if (!result) throw std::runtime_error(error("Could not create ground/forward GPU pipeline"));
        return result;
    }

    void create_ground_programs() {
        ground_status.target_formats={
            static_cast<std::uint32_t>(ground_albedo_format),
            static_cast<std::uint32_t>(hdr_format),
            static_cast<std::uint32_t>(target_format),
            static_cast<std::uint32_t>(target_format),
            static_cast<std::uint32_t>(hdr_format),
            static_cast<std::uint32_t>(world_meta_format),
            static_cast<std::uint32_t>(target_format)};
        const bool water_graph=has_water_reflection(options.world_graph);
        if (water_graph) {
            ground_status.target_formats[7]=static_cast<std::uint32_t>(target_format);
            ground_status.target_formats[8]=static_cast<std::uint32_t>(hdr_format);
        }
        constexpr std::array formats{ground_albedo_format,hdr_format,target_format,world_meta_format};
        for (std::size_t index=0;index<formats.size();++index)
            ground_status.format_supported[index]=SDL_GPUTextureSupportsFormat(
                gpu,formats[index],SDL_GPU_TEXTURETYPE_2D,target_usage);
        if (!std::all_of(ground_status.format_supported.begin(),ground_status.format_supported.end(),
                         [](bool supported){return supported;})) {
            ground_status.reason="GroundForwardSlice requires exact COLOR_TARGET|SAMPLER attachment formats";
            return;
        }
        if (!hdr_status.available) {
            ground_status.reason="GroundForwardSlice requires the available linear HDR world path: "+hdr_status.reason;
            return;
        }

        const auto release_shader=[this](SDL_GPUShader* value){if(value)SDL_ReleaseGPUShader(gpu,value);};
        const bool sky_graph = has_moon_direct(options.world_graph);
        const bool shadow_graph = has_sky_shadows(options.world_graph);
        const bool contact_graph = is_contact_graph(options.world_graph);
        const bool atmosphere_graph = has_atmosphere(options.world_graph);
        {
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/(water_graph ? "water_ground_gbuffer.vert.dxil" : contact_graph ? "ground_contact_gbuffer.vert.dxil" : "ground_gbuffer.vert.dxil"),SDL_GPU_SHADERSTAGE_VERTEX),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/(water_graph ? "water_ground_gbuffer.frag.dxil" : contact_graph ? "ground_contact_gbuffer.frag.dxil" : "ground_gbuffer.frag.dxil"),SDL_GPU_SHADERSTAGE_FRAGMENT,water_graph ? 2u : 1u,false,1),release_shader);
            std::array<SDL_GPUColorTargetDescription,5> targets{};
            targets[0].format=ground_albedo_format;targets[1].format=hdr_format;
            targets[2].format=target_format;targets[3].format=target_format;
            targets[4].format=target_format;
            ground_gbuffer_pipeline=ground_pipeline(vs.get(),fs.get(),
                std::span<SDL_GPUColorTargetDescription>(targets.data(),water_graph ? 5u : 4u),true,false);
        }
        {
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_ground_lighting.vert.dxil" : water_graph ? "water_ground_lighting.vert.dxil" : shadow_graph ? "ground_shadow_lighting.vert.dxil" : sky_graph ? "ground_sky_lighting.vert.dxil" : contact_graph ? "ground_contact_lighting.vert.dxil" : "ground_lighting.vert.dxil"),SDL_GPU_SHADERSTAGE_VERTEX,0,true),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_ground_lighting.frag.dxil" : water_graph ? "water_ground_lighting.frag.dxil" : shadow_graph ? "ground_shadow_lighting.frag.dxil" : sky_graph ? "ground_sky_lighting.frag.dxil" : contact_graph ? "ground_contact_lighting.frag.dxil" : "ground_lighting.frag.dxil"),SDL_GPU_SHADERSTAGE_FRAGMENT,water_graph ? 4u : shadow_graph ? 3u : sky_graph ? 2u : 1u,false,water_graph ? 9u : shadow_graph ? 6u : 4u,1),release_shader);
            std::array<SDL_GPUColorTargetDescription,2> targets{};
            targets[0].format=hdr_format;targets[1].format=world_meta_format;
            ground_lighting_pipeline=ground_pipeline(vs.get(),fs.get(),targets,false,true);
        }
        if (contact_graph) {
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/"ground_contact.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX,0,true),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/"ground_contact.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,1,false,3),release_shader);
            std::array<SDL_GPUColorTargetDescription,1> targets{};
            targets[0].format=ground_albedo_format;
            auto& blend=targets[0].blend_state;
            blend.enable_blend=true; blend.enable_color_write_mask=true;
            blend.color_write_mask=SDL_GPU_COLORCOMPONENT_A;
            blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
            blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            ground_contact_pipeline=ground_pipeline(vs.get(),fs.get(),targets,false,true);
        }
        {
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_material_forward.vert.dxil" : shadow_graph ? "material_shadow_forward.vert.dxil" : sky_graph ? "material_sky_forward.vert.dxil" : "material_forward.vert.dxil"),SDL_GPU_SHADERSTAGE_VERTEX),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_material_forward.frag.dxil" : shadow_graph ? "material_shadow_forward.frag.dxil" : sky_graph ? "material_sky_forward.frag.dxil" : "material_forward.frag.dxil"),SDL_GPU_SHADERSTAGE_FRAGMENT,sky_graph ? 4u : 3u,false,shadow_graph ? 7u : 5u,1),release_shader);
            std::array<SDL_GPUColorTargetDescription,2> targets{};
            targets[0].format=hdr_format;targets[1].format=world_meta_format;
            auto& scene_blend=targets[0].blend_state;scene_blend.enable_blend=true;
            scene_blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            scene_blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            scene_blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
            scene_blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            scene_blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            scene_blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            auto& meta_blend=targets[1].blend_state;meta_blend.enable_blend=true;
            meta_blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            meta_blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            meta_blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
            meta_blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            meta_blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            meta_blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            material_forward_pipeline=ground_pipeline(vs.get(),fs.get(),targets,false,false);
        }
        if (water_graph) {
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_water_reflection.vert.dxil" : "water_reflection.vert.dxil"),SDL_GPU_SHADERSTAGE_VERTEX),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/(atmosphere_graph ? "atmosphere_water_reflection.frag.dxil" : "water_reflection.frag.dxil"),SDL_GPU_SHADERSTAGE_FRAGMENT,4,false,7,1),release_shader);
            std::array<SDL_GPUColorTargetDescription,1> targets{};
            targets[0].format=hdr_format;
            auto& blend=targets[0].blend_state;blend.enable_blend=true;
            blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
            blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            water_reflection_pipeline=ground_pipeline(vs.get(),fs.get(),targets,false,false);
        }
        SDL_GPUSamplerCreateInfo point{};
        point.min_filter=SDL_GPU_FILTER_NEAREST;point.mag_filter=SDL_GPU_FILTER_NEAREST;
        point.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        point.address_mode_u=point.address_mode_v=point.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ground_point_sampler=SDL_CreateGPUSampler(gpu,&point);
        if (!ground_point_sampler) throw std::runtime_error(error("Could not create ground point sampler"));
        if (water_graph) {
            SDL_GPUSamplerCreateInfo repeat{};
            repeat.min_filter=SDL_GPU_FILTER_LINEAR;repeat.mag_filter=SDL_GPU_FILTER_LINEAR;
            repeat.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            repeat.address_mode_u=repeat.address_mode_v=repeat.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            water_repeat_sampler=SDL_CreateGPUSampler(gpu,&repeat);
            if (!water_repeat_sampler) throw std::runtime_error(error("Could not create water repeat sampler"));
        }
        if (shadow_graph) {
            sky_status.d32_supported=SDL_GPUTextureSupportsFormat(gpu,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                SDL_GPU_TEXTURETYPE_2D,sky_target_usage);
            if (!sky_status.d32_supported)
                throw std::runtime_error("SkyShadowSlice requires D32_FLOAT DEPTH_STENCIL_TARGET|SAMPLER");
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> vs(
                shader(assets/"shaders"/"sky_shadow_depth.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX),release_shader);
            std::unique_ptr<SDL_GPUShader,decltype(release_shader)> fs(
                shader(assets/"shaders"/"sky_shadow_depth.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,0,false,0),release_shader);
            const SDL_GPUVertexBufferDescription buffer{0,sizeof(SkyShadowVertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
            const SDL_GPUVertexAttribute attribute{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0};
            SDL_GPUGraphicsPipelineCreateInfo create{};
            create.vertex_shader=vs.get();create.fragment_shader=fs.get();
            create.vertex_input_state={&buffer,1,&attribute,1};
            create.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            create.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
            create.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
            create.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            create.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
            create.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS;
            create.depth_stencil_state.enable_depth_test=true;
            create.depth_stencil_state.enable_depth_write=true;
            create.target_info={nullptr,0,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,true,0,0,0};
            sky_depth_pipeline=SDL_CreateGPUGraphicsPipeline(gpu,&create);
            if (!sky_depth_pipeline) throw std::runtime_error(error("Could not create sky depth pipeline"));
            sky_status.available=true;
        }
        ground_status.available=true;
        ground_status.reason.clear();
    }

    void create_gpu() {
        gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,options.debug,"direct3d12");
        if(!gpu) throw std::runtime_error(error("Could not create Direct3D 12 GPU device"));
        if(!SDL_ClaimWindowForGPUDevice(gpu,window)) throw std::runtime_error(error("Could not claim window for GPU device"));
        window_claimed=true;
        presentation = {{}, {}, true,
                        SDL_WindowSupportsGPUPresentMode(gpu,window,SDL_GPU_PRESENTMODE_IMMEDIATE),
                        SDL_WindowSupportsGPUPresentMode(gpu,window,SDL_GPU_PRESENTMODE_MAILBOX),
                        PresentationUnavailableReason::None, {}};
        if(!set_presentation(options.presentation))
            throw std::runtime_error(presentation.error);
        const char* backend=SDL_GetGPUDeviceDriver(gpu);
        if(!backend || std::string_view(backend)!="direct3d12") throw std::runtime_error("SDL_GPU did not create the requested Direct3D 12 backend");
        const auto props=SDL_GetGPUDeviceProperties(gpu);
        const char* device=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,""):"";
        const char* driver_name=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,""):"";
        const char* driver_info=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,""):"";
        if(!driver_info || !*driver_info) driver_info=props?SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,""):"";
        std::string driver=driver_name?driver_name:"";
        if(driver_info&&*driver_info){if(!driver.empty())driver+=" ";driver+=driver_info;}
        info={backend,device?device:"",std::move(driver),false};
        const auto vs=shader(assets/"shaders"/"sprite.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX);
        SDL_GPUShader* fs{};
        try { fs=shader(assets/"shaders"/"sprite.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT); }
        catch(...) { SDL_ReleaseGPUShader(gpu,vs); throw; }
        try { pipelines[0]=pipeline(vs,fs,Blend::Opaque); pipelines[1]=pipeline(vs,fs,Blend::Alpha); pipelines[2]=pipeline(vs,fs,Blend::Additive); pipelines[3]=pipeline(vs,fs,Blend::PremultipliedAlpha); }
        catch(...) { SDL_ReleaseGPUShader(gpu,vs); SDL_ReleaseGPUShader(gpu,fs); throw; }
        SDL_ReleaseGPUShader(gpu,vs); SDL_ReleaseGPUShader(gpu,fs);
        const auto surface_vs = shader(assets/"shaders"/"surface_lit.vert.dxil", SDL_GPU_SHADERSTAGE_VERTEX, true);
        SDL_GPUShader* surface_fs{};
        try {
            surface_fs = shader(assets/"shaders"/"surface_lit.frag.dxil", SDL_GPU_SHADERSTAGE_FRAGMENT, true);
            surface_pipeline = pipeline(surface_vs, surface_fs, Blend::Opaque, true);
        } catch (...) {
            SDL_ReleaseGPUShader(gpu, surface_vs);
            if (surface_fs) SDL_ReleaseGPUShader(gpu, surface_fs);
            throw;
        }
        SDL_ReleaseGPUShader(gpu, surface_vs);
        SDL_ReleaseGPUShader(gpu, surface_fs);
        create_hdr_programs();
        if (options.world_graph != WorldGraph::LegacyCombined) {
            create_ground_programs();
            if (!ground_status.available) throw std::invalid_argument(ground_status.reason);
        }
        SDL_GPUSamplerCreateInfo sampler_info{}; sampler_info.min_filter=SDL_GPU_FILTER_LINEAR; sampler_info.mag_filter=SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler_info.address_mode_u=sampler_info.address_mode_v=sampler_info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler=SDL_CreateGPUSampler(gpu,&sampler_info); if(!sampler) throw std::runtime_error(error("Could not create GPU sampler"));
        sampler_info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_info.max_lod=3.f;
        mip_sampler=SDL_CreateGPUSampler(gpu,&sampler_info); if(!mip_sampler) throw std::runtime_error(error("Could not create mipmapped GPU sampler"));
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixel(SDL_CreateSurface(1,1,SDL_PIXELFORMAT_RGBA32),&SDL_DestroySurface);
        if(!pixel) throw std::runtime_error(error("Could not create white pixel"));
        *static_cast<std::uint32_t*>(pixel->pixels)=0xffffffffu;
        white=upload_texture(pixel.get());
    }

    void refresh_hdr_swapchain_format() {
        const auto swap_format = SDL_GetGPUSwapchainTextureFormat(gpu, window);
        hdr_status.swapchain_format = static_cast<std::uint32_t>(swap_format);
        hdr_status.sdr_unorm_swapchain = swap_format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
                                        swap_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        if (hdr_composite_pipeline) {
            hdr_status.available = hdr_status.sdr_unorm_swapchain;
            hdr_status.reason = hdr_status.available ? "" :
                "HDR explicit sRGB output requires an SDR UNORM swapchain, not an sRGB view";
        }
    }

    void create_hdr_programs() {
        hdr_status.rgba16f_supported = SDL_GPUTextureSupportsFormat(gpu, hdr_format, SDL_GPU_TEXTURETYPE_2D, target_usage);
        refresh_hdr_swapchain_format();
        if (!hdr_status.rgba16f_supported ||
            !SDL_GPUTextureSupportsFormat(gpu, target_format, SDL_GPU_TEXTURETYPE_2D, target_usage)) {
            hdr_status.reason = "HDR world requires RGBA16F and RGBA8 COLOR_TARGET|SAMPLER support";
            return;
        }
        if (!hdr_status.sdr_unorm_swapchain) {
            hdr_status.reason = "HDR explicit sRGB output requires an SDR UNORM swapchain, not an sRGB view";
            return;
        }
        const auto release_shader = [this](SDL_GPUShader* value) { if (value) SDL_ReleaseGPUShader(gpu, value); };
        const auto build = [&](std::string_view name, bool surface, bool fullscreen) {
            std::unique_ptr<SDL_GPUShader, decltype(release_shader)> vs(
                shader(assets/"shaders"/(std::string(name)+".vert.dxil"), SDL_GPU_SHADERSTAGE_VERTEX, 0, fullscreen), release_shader);
            std::unique_ptr<SDL_GPUShader, decltype(release_shader)> fs(
                shader(assets/"shaders"/(std::string(name)+".frag.dxil"), SDL_GPU_SHADERSTAGE_FRAGMENT, 1), release_shader);
            if (name == "world_linear") {
                for (std::uint32_t index=0; index<4; ++index)
                    world_linear_pipelines[index] = pipeline(vs.get(), fs.get(), static_cast<Blend>(index), false, hdr_format);
            } else if (surface) {
                surface_hdr_pipeline = pipeline(vs.get(), fs.get(), Blend::Opaque, true, hdr_format);
            } else {
                hdr_composite_pipeline = pipeline(vs.get(), fs.get(), Blend::Opaque, false, target_format, true);
            }
        };
        build("world_linear", false, false);
        build("surface_hdr", true, false);
        build("hdr_composite", false, true);
        {
            std::unique_ptr<SDL_GPUShader, decltype(release_shader)> vs(
                shader(assets/"shaders"/"material_sprite.vert.dxil",SDL_GPU_SHADERSTAGE_VERTEX),
                release_shader);
            std::unique_ptr<SDL_GPUShader, decltype(release_shader)> fs(
                shader(assets/"shaders"/"material_sprite.frag.dxil",SDL_GPU_SHADERSTAGE_FRAGMENT,
                       2,false,5),release_shader);
            material_sprite_pipeline=pipeline(vs.get(),fs.get(),Blend::Alpha,false,hdr_format);
        }
        hdr_status.available = true;
        hdr_status.reason.clear();
    }

    void ensure_hdr_targets(int width, int height) {
        const auto count = presentation.effective.frames_in_flight;
        if (hdr_targets.width == width && hdr_targets.height == height && hdr_targets.count == count) return;
        HdrTargets candidate;
        candidate.width = width; candidate.height = height; candidate.count = count;
        for (std::uint32_t index=0; index<count; ++index) {
            for (const bool world : {true, false}) {
                const auto format = world ? hdr_format : target_format;
                const auto context = "HDR " + std::string(world ? "world RGBA16F" : "final RGBA8") +
                    " COLOR_TARGET|SAMPLER " + std::to_string(width) + "x" + std::to_string(height) +
                    " frame-set " + std::to_string(index);
                if (!SDL_GPUTextureSupportsFormat(gpu, format, SDL_GPU_TEXTURETYPE_2D, target_usage))
                    throw std::runtime_error(context + " unsupported");
                if (++hdr_target_creation_attempts == options.test_fail_hdr_target_creation_at) {
                    options.test_fail_hdr_target_creation_at = 0;
                    throw std::runtime_error("Injected target creation failure: " + context);
                }
                const SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D, format, target_usage,
                    static_cast<Uint32>(width), static_cast<Uint32>(height), 1, 1, SDL_GPU_SAMPLECOUNT_1, 0};
                OwnedGpuTexture texture(SDL_CreateGPUTexture(gpu, &create), GpuTextureDeleter{gpu});
                if (!texture) throw std::runtime_error(error("Could not create " + context));
                (world ? candidate.world[index] : candidate.final[index]) = std::move(texture);
            }
        }
        if (!SDL_WaitForGPUIdle(gpu)) throw std::runtime_error(error("Could not wait before committing HDR targets"));
        hdr_targets = std::move(candidate); // Previous sets are idle; failure above leaves them untouched.
        hdr_status.width = static_cast<std::uint32_t>(width);
        hdr_status.height = static_cast<std::uint32_t>(height);
        hdr_status.frame_sets = count;
        hdr_status.attachment_bytes = std::uint64_t(width) * std::uint64_t(height) * count * 12u;
    }

    void ensure_ground_targets(int width,int height) {
        if (ground_targets.width==width && ground_targets.height==height) return;
        GroundTargets candidate;
        candidate.width=width;candidate.height=height;
        const bool water_graph=has_water_reflection(options.world_graph);
        const int reflection_width=(width+1)/2,reflection_height=(height+1)/2;
        constexpr std::array formats{ground_albedo_format,hdr_format,target_format,target_format,
                                     hdr_format,world_meta_format,target_format,target_format,hdr_format};
        constexpr std::array names{"ground_albedo","surface_world","surface_material","surface_flags",
                                   "scene_linear","world_meta","final","water_coverage_v1","reflection"};
        ground_status.candidate_live_textures=0;
        try {
            for (std::uint32_t set_index=0;set_index<ground_frame_count;++set_index) {
                auto& set=candidate.sets[set_index];
                std::array<OwnedGpuTexture*,9> targets{
                    &set.ground_albedo,&set.surface_world,&set.surface_material,&set.surface_flags,
                    &set.scene,&set.world_meta,&set.final,&set.water_coverage,&set.reflection};
                const std::size_t target_count=water_graph ? targets.size() : 7u;
                for (std::size_t target_index=0;target_index<target_count;++target_index) {
                    const int target_width=target_index==8 ? reflection_width : width;
                    const int target_height=target_index==8 ? reflection_height : height;
                    const auto context=std::string(names[target_index])+" COLOR_TARGET|SAMPLER "+
                        std::to_string(target_width)+"x"+std::to_string(target_height)+" frame-set "+
                        std::to_string(set_index);
                    ++ground_target_creation_attempts;
                    ground_status.creation_attempts=ground_target_creation_attempts;
                    if (ground_target_creation_attempts==options.test_fail_ground_target_creation_at) {
                        options.test_fail_ground_target_creation_at=0;
                        throw std::runtime_error("Injected ground target creation failure: "+context);
                    }
                    const SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D,formats[target_index],target_usage,
                        static_cast<Uint32>(target_width),static_cast<Uint32>(target_height),1,1,SDL_GPU_SAMPLECOUNT_1,0};
                    OwnedGpuTexture texture(SDL_CreateGPUTexture(gpu,&create),GpuTextureDeleter{gpu});
                    if (!texture) throw std::runtime_error(error("Could not create "+context));
                    *targets[target_index]=std::move(texture);
                    ++ground_status.candidate_live_textures;
                }
            }
            for (std::uint32_t slot=0;slot<ground_frame_count;++slot) if (frames[slot].fence) {
                SDL_GPUFence* fences[]{frames[slot].fence};
                if (!SDL_WaitForGPUFences(gpu,true,fences,1))
                    throw std::runtime_error(error("Could not wait before committing ground targets"));
            }
            ground_targets=std::move(candidate);
            // A committed replacement retires every previous attachment. A
            // failed candidate never reaches this point and preserves the last
            // submitted generation for diagnostic readback.
            if (water_graph) water_readback_valid=false;
        } catch (...) {
            ground_status.candidate_live_textures=0;
            throw;
        }
        ground_status.candidate_live_textures=0;
        ground_status.width=static_cast<std::uint32_t>(width);
        ground_status.height=static_cast<std::uint32_t>(height);
        ground_status.frame_sets=ground_frame_count;
        const std::uint64_t full_pixels=std::uint64_t(width)*std::uint64_t(height);
        const std::uint64_t reflection_pixels=std::uint64_t(reflection_width)*std::uint64_t(reflection_height);
        ground_status.coverage_bytes=water_graph ? full_pixels*4u*ground_frame_count : 0u;
        ground_status.reflection_bytes=water_graph ? reflection_pixels*8u*ground_frame_count : 0u;
        ground_status.attachment_bytes=full_pixels*ground_frame_count*36u+
            ground_status.coverage_bytes+ground_status.reflection_bytes;
        ground_status.reflection_width=water_graph ? static_cast<std::uint32_t>(reflection_width) : 0u;
        ground_status.reflection_height=water_graph ? static_cast<std::uint32_t>(reflection_height) : 0u;
        ++ground_status.target_generation;
        ground_status.active_set_ids={ground_status.target_generation*2u-1u,
                                      ground_status.target_generation*2u};
    }

    void ensure_sky_targets() {
        if (sky_status.target_generation) return;
        SkyTargets candidate;
        sky_status.candidate_live_textures=0;
        try {
            for (auto& set:candidate.sets) for (auto& map:set.maps) {
                ++sky_status.creation_attempts;
                if (sky_status.creation_attempts==options.test_fail_sky_target_creation_at) {
                    options.test_fail_sky_target_creation_at=0;
                    throw std::runtime_error("Injected sky target creation failure");
                }
                const SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                    sky_target_usage,1024,1024,1,1,SDL_GPU_SAMPLECOUNT_1,0};
                map=OwnedGpuTexture(SDL_CreateGPUTexture(gpu,&create),GpuTextureDeleter{gpu});
                if (!map) throw std::runtime_error(error("Could not create sky depth map"));
                ++sky_status.candidate_live_textures;
            }
            // No old maps exist for this immutable-size first generation. Each
            // later write is guarded by its normal frame-resource fence.
            sky_targets=std::move(candidate);
        } catch (...) {
            sky_status.candidate_live_textures=0;
            throw;
        }
        sky_status.candidate_live_textures=0;
        sky_status.map_size=1024;sky_status.frame_sets=ground_frame_count;
        sky_status.attachment_bytes=4ull*1024u*1024u*4u;
        sky_status.target_generation=1;sky_status.active_set_ids={1,2};
    }

    SDL_GPUTexture* upload_texture(SDL_Surface* surface, std::uint32_t mip_levels=1,
                                   SDL_GPUCommandBuffer* existing=nullptr, FrameResources* owner=nullptr) {
        const auto usage=SDL_GPU_TEXTUREUSAGE_SAMPLER |
            (mip_levels>1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0u);
        SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D,target_format,usage,
            static_cast<Uint32>(surface->w),static_cast<Uint32>(surface->h),1,mip_levels,SDL_GPU_SAMPLECOUNT_1,0};
        SDL_GPUTexture* texture=SDL_CreateGPUTexture(gpu,&create); if(!texture) throw std::runtime_error(error("Could not create GPU texture"));
        const auto bytes=static_cast<std::uint32_t>(surface->w*surface->h*4);
        SDL_GPUTransferBufferCreateInfo transfer_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,bytes,0};
        SDL_GPUTransferBuffer* transfer=SDL_CreateGPUTransferBuffer(gpu,&transfer_info);
        if(!transfer){SDL_ReleaseGPUTexture(gpu,texture);throw std::runtime_error(error("Could not create texture upload buffer"));}
        void* mapped=SDL_MapGPUTransferBuffer(gpu,transfer,false); if(!mapped){SDL_ReleaseGPUTransferBuffer(gpu,transfer);SDL_ReleaseGPUTexture(gpu,texture);throw std::runtime_error(error("Could not map texture upload buffer"));}
        for(int y=0;y<surface->h;++y) std::memcpy(static_cast<std::uint8_t*>(mapped)+y*surface->w*4,
            static_cast<std::uint8_t*>(surface->pixels)+y*surface->pitch,surface->w*4);
        SDL_UnmapGPUTransferBuffer(gpu,transfer);
        SDL_GPUCommandBuffer* command=existing?existing:SDL_AcquireGPUCommandBuffer(gpu);
        if(!command){SDL_ReleaseGPUTransferBuffer(gpu,transfer);SDL_ReleaseGPUTexture(gpu,texture);throw std::runtime_error(error("Could not acquire texture upload command buffer"));}
        if(owner){
            try{owner->transfers.push_back(transfer);}
            catch(...){if(!existing)SDL_CancelGPUCommandBuffer(command);SDL_ReleaseGPUTransferBuffer(gpu,transfer);SDL_ReleaseGPUTexture(gpu,texture);throw;}
        }
        auto* copy=SDL_BeginGPUCopyPass(command);
        if(!copy){if(owner)owner->transfers.pop_back();if(!existing)SDL_CancelGPUCommandBuffer(command);SDL_ReleaseGPUTransferBuffer(gpu,transfer);SDL_ReleaseGPUTexture(gpu,texture);throw std::runtime_error(error("Could not begin texture upload"));}
        SDL_GPUTextureTransferInfo source{transfer,0,0,0};
        SDL_GPUTextureRegion destination{texture,0,0,0,0,0,static_cast<Uint32>(surface->w),static_cast<Uint32>(surface->h),1};
        SDL_UploadToGPUTexture(copy,&source,&destination,false); SDL_EndGPUCopyPass(copy);
        if(mip_levels>1)SDL_GenerateMipmapsForGPUTexture(command,texture);
        if(!owner) {
            auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
            if(!fence){SDL_ReleaseGPUTransferBuffer(gpu,transfer);SDL_ReleaseGPUTexture(gpu,texture);throw std::runtime_error(error("Could not submit texture upload"));}
            SDL_GPUFence* fences[]{fence};
            if (!SDL_WaitForGPUFences(gpu,true,fences,1)) {
                const auto message = error("Could not wait for texture upload");
                SDL_ReleaseGPUFence(gpu,fence);
                SDL_ReleaseGPUTransferBuffer(gpu,transfer);
                SDL_ReleaseGPUTexture(gpu,texture);
                throw std::runtime_error(message);
            }
            SDL_ReleaseGPUFence(gpu,fence); SDL_ReleaseGPUTransferBuffer(gpu,transfer);
        }
        return texture;
    }

    SDL_GPUTexture* upload_sampled_texture(SampledTextureDesc desc,
                                          std::span<const std::uint8_t> bytes, bool fail_before_submit = false) {
        const auto format=sampled_texture_gpu_format(desc.format);
        const SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D,format,
            SDL_GPU_TEXTUREUSAGE_SAMPLER,desc.size.width,desc.size.height,1,1,
            SDL_GPU_SAMPLECOUNT_1,0};
        SDL_GPUTexture* texture=SDL_CreateGPUTexture(gpu,&create);
        if (!texture) throw std::runtime_error(error("Could not create sampled GPU texture"));
        SDL_GPUTransferBuffer* transfer{};
        SDL_GPUCommandBuffer* command{};
        SDL_GPUFence* fence{};
        try {
            const SDL_GPUTransferBufferCreateInfo transfer_info{
                SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,static_cast<Uint32>(bytes.size()),0};
            transfer=SDL_CreateGPUTransferBuffer(gpu,&transfer_info);
            if (!transfer) throw std::runtime_error(error("Could not create sampled texture upload buffer"));
            void* mapped=SDL_MapGPUTransferBuffer(gpu,transfer,false);
            if (!mapped) throw std::runtime_error(error("Could not map sampled texture upload buffer"));
            std::memcpy(mapped,bytes.data(),bytes.size());
            SDL_UnmapGPUTransferBuffer(gpu,transfer);
            command=SDL_AcquireGPUCommandBuffer(gpu);
            if (!command) throw std::runtime_error(error("Could not acquire sampled texture upload command buffer"));
            auto* copy=SDL_BeginGPUCopyPass(command);
            if (!copy) throw std::runtime_error(error("Could not begin sampled texture upload"));
            const SDL_GPUTextureTransferInfo source{transfer,0,0,0};
            const SDL_GPUTextureRegion destination{texture,0,0,0,0,0,
                desc.size.width,desc.size.height,1};
            SDL_UploadToGPUTexture(copy,&source,&destination,false);
            SDL_EndGPUCopyPass(copy);
            if (fail_before_submit) throw std::runtime_error("Injected contact art upload failure before submission");
            fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
            command=nullptr;
            if (!fence) throw std::runtime_error(error("Could not submit sampled texture upload"));
            SDL_GPUFence* fences[]{fence};
            if (!SDL_WaitForGPUFences(gpu,true,fences,1))
                throw std::runtime_error(error("Could not wait for sampled texture upload"));
            SDL_ReleaseGPUFence(gpu,fence);fence=nullptr;
            SDL_ReleaseGPUTransferBuffer(gpu,transfer);transfer=nullptr;
            return texture;
        } catch (...) {
            if (command) SDL_CancelGPUCommandBuffer(command);
            if (fence) SDL_ReleaseGPUFence(gpu,fence);
            if (transfer) SDL_ReleaseGPUTransferBuffer(gpu,transfer);
            SDL_ReleaseGPUTexture(gpu,texture);
            throw;
        }
    }

    GpuText& gpu_raster(const TextKey& key, SDL_GPUCommandBuffer* command, FrameResources& owner,
                        std::vector<SDL_GPUTexture*>& pending) {
        if(const auto it=gpu_text.find(key);it!=gpu_text.end())return it->second;
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(raster_text(font(key.size),key),&SDL_DestroySurface);
        const GpuText value{upload_texture(surface.get(),1,command,&owner),static_cast<float>(surface->w),static_cast<float>(surface->h)};
        try{
            const auto [it,inserted]=gpu_text.emplace(key,value);
            if(!inserted){SDL_ReleaseGPUTexture(gpu,value.texture);return it->second;}
            pending.push_back(value.texture);
            if(options.test_fail_gpu_text_uploads){--options.test_fail_gpu_text_uploads;throw std::runtime_error("Injected GPU text upload failure");}
            return it->second;
        }catch(...){
            const auto found=std::find_if(gpu_text.begin(),gpu_text.end(),[&](const auto& item){return item.second.texture==value.texture;});
            if(found==gpu_text.end())SDL_ReleaseGPUTexture(gpu,value.texture);
            throw;
        }
    }

    void ensure_offscreen(int w,int h) {
        if(offscreen&&w==offscreen_w&&h==offscreen_h)return;
        SDL_GPUTextureCreateInfo create{SDL_GPU_TEXTURETYPE_2D,target_format,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,
            static_cast<Uint32>(w),static_cast<Uint32>(h),1,1,SDL_GPU_SAMPLECOUNT_1,0};
        auto* candidate=SDL_CreateGPUTexture(gpu,&create);
        if(!candidate)throw std::runtime_error(error("Could not create offscreen GPU target"));
        if(!SDL_WaitForGPUIdle(gpu)){
            SDL_ReleaseGPUTexture(gpu,candidate);
            throw std::runtime_error(error("Could not wait before resizing GPU target"));
        }
        if(offscreen)SDL_ReleaseGPUTexture(gpu,offscreen);
        offscreen=candidate;
        offscreen_w=w;offscreen_h=h;
    }

    void release_frame(FrameResources& frame) {
        if(frame.fence){SDL_GPUFence* fences[]{frame.fence};if(!SDL_WaitForGPUFences(gpu,true,fences,1))throw std::runtime_error(error("GPU frame wait failed"));SDL_ReleaseGPUFence(gpu,frame.fence);frame.fence=nullptr;}
        for(auto* transfer:frame.transfers)SDL_ReleaseGPUTransferBuffer(gpu,transfer);frame.transfers.clear();
    }

    void ensure_buffers(FrameResources& frame,std::uint32_t vertices,std::uint32_t indices,
                        std::uint32_t surfaces,std::uint32_t occluders,std::uint32_t sky_vertices,std::uint32_t upload_bytes) {
        auto grow=[](std::uint32_t value){std::uint32_t out=4096;while(out<value&&out<=std::numeric_limits<std::uint32_t>::max()/2)out*=2;return std::max(out,value);};
        const std::array requested{vertices,indices,surfaces,occluders,sky_vertices};
        const std::array previous{frame.vertex_capacity,frame.index_capacity,frame.surface_capacity,frame.occluder_capacity,frame.sky_capacity};
        const std::array<SDL_GPUBufferUsageFlags,5> usage{
            SDL_GPU_BUFFERUSAGE_VERTEX,SDL_GPU_BUFFERUSAGE_INDEX,SDL_GPU_BUFFERUSAGE_VERTEX,
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,SDL_GPU_BUFFERUSAGE_VERTEX};
        std::array<SDL_GPUBuffer*,5> candidates{};
        std::array<std::uint32_t,5> capacities{};
        SDL_GPUTransferBuffer* upload_candidate{};
        const auto upload_capacity=upload_bytes>frame.upload_capacity?grow(upload_bytes):frame.upload_capacity;
        // Allocate the complete growth set before replacing any usable resource.
        // A failed index/surface/upload allocation must not poison retry capacity.
        try {
            for(std::size_t i=0;i<candidates.size();++i){
                if(requested[i]<=previous[i])continue;
                capacities[i]=grow(requested[i]);
                if(options.test_fail_gpu_frame_buffer_creation_at&&
                   --options.test_fail_gpu_frame_buffer_creation_at==0)
                    throw std::runtime_error("Injected GPU frame buffer creation failure");
                const SDL_GPUBufferCreateInfo create{usage[i],capacities[i],0};
                candidates[i]=SDL_CreateGPUBuffer(gpu,&create);
                if(!candidates[i])throw std::runtime_error(error("Could not allocate GPU frame buffer"));
            }
            if(upload_bytes>frame.upload_capacity){
                const SDL_GPUTransferBufferCreateInfo create{
                    SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,upload_capacity,0};
                upload_candidate=SDL_CreateGPUTransferBuffer(gpu,&create);
                if(!upload_candidate)throw std::runtime_error(error("Could not allocate frame upload buffer"));
            }
        } catch (...) {
            for(auto* candidate:candidates)if(candidate)SDL_ReleaseGPUBuffer(gpu,candidate);
            if(upload_candidate)SDL_ReleaseGPUTransferBuffer(gpu,upload_candidate);
            throw;
        }
        const std::array<SDL_GPUBuffer**,5> destinations{&frame.vertices,&frame.indices,&frame.surfaces,&frame.occluders,&frame.sky_vertices};
        const std::array<std::uint32_t*,5> capacity_destinations{
            &frame.vertex_capacity,&frame.index_capacity,&frame.surface_capacity,&frame.occluder_capacity,&frame.sky_capacity};
        for(std::size_t i=0;i<candidates.size();++i)if(candidates[i]){
            if(*destinations[i])SDL_ReleaseGPUBuffer(gpu,*destinations[i]);
            *destinations[i]=candidates[i];
            *capacity_destinations[i]=capacities[i];
        }
        if(upload_candidate){
            if(frame.upload)SDL_ReleaseGPUTransferBuffer(gpu,frame.upload);
            frame.upload=upload_candidate;
            frame.upload_capacity=upload_capacity;
        }
        if(!frame.vertices||!frame.indices)throw std::runtime_error(error("Could not allocate GPU frame buffers"));
        if (surfaces && !frame.surfaces) throw std::runtime_error(error("Could not allocate surface vertex buffer"));
        if (occluders && !frame.occluders) throw std::runtime_error(error("Could not allocate local occluder storage buffer"));
        if (sky_vertices && !frame.sky_vertices) throw std::runtime_error(error("Could not allocate sky vertex buffer"));
    }

    void queue_capture(SDL_GPUCommandBuffer* command, FrameResources& owner, SDL_GPUTexture* target, int width, int height) {
        const std::uint32_t pitch=(static_cast<std::uint32_t>(width)*4u+255u)&~255u;
        SDL_GPUTransferBufferCreateInfo ci{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,pitch*static_cast<std::uint32_t>(height),0};
        auto* transfer=SDL_CreateGPUTransferBuffer(gpu,&ci);if(!transfer)throw std::runtime_error(error("Could not create capture buffer"));owner.transfers.push_back(transfer);
        auto* copy=SDL_BeginGPUCopyPass(command);if(!copy)throw std::runtime_error(error("Could not begin capture download"));SDL_GPUTextureRegion src{target,0,0,0,0,0,static_cast<Uint32>(width),static_cast<Uint32>(height),1};
        SDL_GPUTextureTransferInfo dst{transfer,0,pitch/4,static_cast<Uint32>(height)};SDL_DownloadFromGPUTexture(copy,&src,&dst);SDL_EndGPUCopyPass(copy);
    }

    void finish_capture(FrameResources& owner,const std::filesystem::path& path, int width, int height) {
        SDL_GPUFence* fences[]{owner.fence};if(!SDL_WaitForGPUFences(gpu,true,fences,1))throw std::runtime_error(error("GPU capture wait failed"));
        auto* transfer=owner.transfers.back();void* pixels=SDL_MapGPUTransferBuffer(gpu,transfer,false);if(!pixels)throw std::runtime_error(error("Could not map capture buffer"));
        const int pitch=(width*4+255)&~255;SDL_Surface* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,pixels,pitch);
        if(!surface){SDL_UnmapGPUTransferBuffer(gpu,transfer);throw std::runtime_error(error("Could not create capture surface"));}
        if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
        const bool ok=path.extension()==".bmp"?SDL_SaveBMP(surface,utf8(path).c_str()):SDL_SavePNG(surface,utf8(path).c_str());
        SDL_DestroySurface(surface);SDL_UnmapGPUTransferBuffer(gpu,transfer);if(!ok)throw std::runtime_error(error("Could not save GPU capture"));
    }

    WaterCoverageReadback read_water_coverage_v1() {
        if (render_active)
            throw std::logic_error("Water coverage readback requires an idle renderer phase");
        if (!gpu || !has_water_reflection(options.world_graph))
            throw std::invalid_argument("WaterCoverage V1 readback requires the D3D12 WaterReflectionSlice graph");
        if (!water_readback_valid)
            throw std::logic_error("WaterCoverage V1 has no valid successful submission");
        if (water_readback_generation!=ground_status.target_generation ||
            water_readback_slot>=ground_frame_count)
            throw std::logic_error("WaterCoverage V1 readback generation is stale");
        auto* source=ground_targets.sets[water_readback_slot].water_coverage.get();
        const auto width=ground_status.width,height=ground_status.height;
        if (!source || !width || !height || ground_targets.width!=static_cast<int>(width) ||
            ground_targets.height!=static_cast<int>(height))
            throw std::logic_error("WaterCoverage V1 target is stale or unavailable");

        const std::uint64_t tight_pitch64=std::uint64_t(width)*4u;
        const std::uint64_t gpu_pitch64=(tight_pitch64+255u)&~std::uint64_t{255u};
        const std::uint64_t download_bytes64=gpu_pitch64*height;
        const std::uint64_t tight_bytes64=tight_pitch64*height;
        if (download_bytes64>std::numeric_limits<std::uint32_t>::max() ||
            tight_bytes64>std::numeric_limits<std::size_t>::max())
            throw std::length_error("WaterCoverage V1 readback exceeds addressable transfer limits");
        const auto gpu_pitch=static_cast<std::uint32_t>(gpu_pitch64);
        const SDL_GPUTransferBufferCreateInfo transfer_info{
            SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,static_cast<std::uint32_t>(download_bytes64),0};
        OwnedGpuTransfer transfer(SDL_CreateGPUTransferBuffer(gpu,&transfer_info),GpuTransferDeleter{gpu});
        if (!transfer) throw std::runtime_error(error("Could not create WaterCoverage V1 download buffer"));

        auto* command=SDL_AcquireGPUCommandBuffer(gpu);
        if (!command) throw std::runtime_error(error("Could not acquire WaterCoverage V1 readback command"));
        CommandGuard command_guard{command};
        auto* copy=SDL_BeginGPUCopyPass(command);
        if (!copy) throw std::runtime_error(error("Could not begin WaterCoverage V1 readback copy"));
        const SDL_GPUTextureRegion source_region{source,0,0,0,0,0,width,height,1};
        const SDL_GPUTextureTransferInfo destination{
            transfer.get(),0,gpu_pitch/4u,height};
        SDL_DownloadFromGPUTexture(copy,&source_region,&destination);
        SDL_EndGPUCopyPass(copy);
        OwnedGpuFence fence(SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.release()),
                            GpuFenceDeleter{gpu});
        if (!fence) throw std::runtime_error(error("Could not submit WaterCoverage V1 readback"));
        SDL_GPUFence* fences[]{fence.get()};
        if (!SDL_WaitForGPUFences(gpu,true,fences,1))
            throw std::runtime_error(error("Could not wait for WaterCoverage V1 readback"));

        WaterCoverageReadback result{{width,height},gpu_pitch,water_readback_slot,
                                     water_readback_generation,{}};
        result.rgba8.resize(static_cast<std::size_t>(tight_bytes64));
        void* mapped=SDL_MapGPUTransferBuffer(gpu,transfer.get(),false);
        if (!mapped) throw std::runtime_error(error("Could not map WaterCoverage V1 readback"));
        struct UnmapGuard {
            SDL_GPUDevice* device;
            SDL_GPUTransferBuffer* transfer;
            ~UnmapGuard(){SDL_UnmapGPUTransferBuffer(device,transfer);}
        } unmap{gpu,transfer.get()};
        for (std::uint32_t row=0;row<height;++row)
            std::memcpy(result.rgba8.data()+std::size_t(row)*tight_pitch64,
                        static_cast<const std::uint8_t*>(mapped)+std::size_t(row)*gpu_pitch,
                        static_cast<std::size_t>(tight_pitch64));
        return result;
    }

    void encode_ground_graph(SDL_GPUCommandBuffer* command,FrameResources& resources,
                             const Frame2D& frame,const PreparedFrame& prepared,
                             const std::vector<SDL_GPUTexture*>& resolved_textures,
                             const Layout& l,std::uint32_t slot,RendererFrameTelemetry& measured,
                             std::span<const ContactScissor> contact_scissors,
                             const PreparedSkyShadows& sky) {
        auto& targets=ground_targets.sets[slot];
        std::optional<SurfaceLightingAtmosphereGpu> atmosphere_lighting;
        if (has_atmosphere(options.world_graph))
            atmosphere_lighting.emplace(SurfaceLightingAtmosphereGpu{
                frame.surface_lighting, *frame.atmosphere});
        const auto push_surface_lighting = [&] {
            if (atmosphere_lighting)
                SDL_PushGPUFragmentUniformData(command,0,&*atmosphere_lighting,
                                               sizeof(*atmosphere_lighting));
            else
                SDL_PushGPUFragmentUniformData(command,0,&frame.surface_lighting,
                                               sizeof(frame.surface_lighting));
        };
        if (has_sky_shadows(options.world_graph)) {
            const SDL_GPUViewport viewport{0,0,1024,1024,0,1};
            const SDL_Rect scissor{0,0,1024,1024};
            for (std::size_t source=0;source<2;++source) {
                SDL_GPUDepthStencilTargetInfo depth{};
                depth.texture=sky_targets.sets[slot].maps[source].get();
                depth.clear_depth=1.f;depth.load_op=SDL_GPU_LOADOP_CLEAR;
                depth.store_op=SDL_GPU_STOREOP_STORE;
                depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;
                depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
                auto* shadow_pass=SDL_BeginGPURenderPass(command,nullptr,0,&depth);
                if (!shadow_pass) throw std::runtime_error(error("Could not begin sky shadow pass"));
                SDL_SetGPUViewport(shadow_pass,&viewport);SDL_SetGPUScissor(shadow_pass,&scissor);
                SDL_BindGPUGraphicsPipeline(shadow_pass,sky_depth_pipeline);
                if (sky.vertex_count[source]) {
                    const SDL_GPUBufferBinding vertices{resources.sky_vertices,0};
                    SDL_BindGPUVertexBuffers(shadow_pass,0,&vertices,1);
                    SDL_PushGPUVertexUniformData(command,0,&sky.gpu.sources[source],sizeof(ShadowSourceGpu));
                    SDL_DrawGPUPrimitives(shadow_pass,sky.vertex_count[source],1,sky.first_vertex[source],0);
                    if (measured.measured) ++measured.draws;
                }
                SDL_EndGPURenderPass(shadow_pass);
            }
        }
        const SDL_GPUViewport world_viewport{l.x,l.y,frame.logical_size.width*l.scale,
                                             frame.logical_size.height*l.scale,0,1};
        const SDL_Rect world_scissor{static_cast<int>(l.x),static_cast<int>(l.y),
            static_cast<int>(frame.logical_size.width*l.scale),
            static_cast<int>(frame.logical_size.height*l.scale)};
        const std::array<float,4> vertex_uniform{static_cast<float>(frame.logical_size.width),
                                                static_cast<float>(frame.logical_size.height),0,0};
        const SDL_GPUBufferBinding vertex_binding{resources.vertices,0};
        const SDL_GPUBufferBinding surface_binding{resources.surfaces,0};
        const SDL_GPUBufferBinding index_binding{resources.indices,0};

        if (has_water_reflection(options.world_graph)) {
            SDL_GPUColorTargetInfo reflection_target{};
            reflection_target.texture=targets.reflection.get();
            reflection_target.load_op=SDL_GPU_LOADOP_CLEAR;
            reflection_target.store_op=SDL_GPU_STOREOP_STORE;
            reflection_target.clear_color={0,0,0,0};
            auto* reflection_pass=SDL_BeginGPURenderPass(command,&reflection_target,1,nullptr);
            if (!reflection_pass) throw std::runtime_error(error("Could not begin Reflection pass"));
            const float scale_x=float(ground_status.reflection_width)/float(l.pixel_w);
            const float scale_y=float(ground_status.reflection_height)/float(l.pixel_h);
            const SDL_GPUViewport reflection_viewport{
                l.x*scale_x,l.y*scale_y,frame.logical_size.width*l.scale*scale_x,
                frame.logical_size.height*l.scale*scale_y,0,1};
            SDL_SetGPUViewport(reflection_pass,&reflection_viewport);
            SDL_BindGPUGraphicsPipeline(reflection_pass,water_reflection_pipeline);
            SDL_BindGPUFragmentStorageBuffers(reflection_pass,0,&resources.occluders,1);
            SDL_BindGPUVertexBuffers(reflection_pass,0,&vertex_binding,1);
            SDL_BindGPUIndexBuffer(reflection_pass,&index_binding,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_PushGPUVertexUniformData(command,0,vertex_uniform.data(),sizeof(vertex_uniform));
            const SkyMoonLightingGpu sky_moon_gpu{*frame.moon_lighting,sky.gpu};
            for (const auto& reflection:frame.reflection_draws) {
                const auto& draw=reflection.draw;
                const auto& instance=frame.sprite_material_draws[draw.sprite_material_index-1];
                const auto& desc=material(instance.material).desc;
                SpriteSurfaceGpu surface=instance.surface;
                for (std::size_t index=0;index<surface.uv_regions.size();++index) {
                    const auto& uv=desc.channels[index].uv;
                    surface.uv_regions[index]={uv.x,uv.y,uv.w,uv.h};
                }
                surface.emission=desc.emission;surface.emission[3]*=instance.emission_scale;
                const ReflectionMetadataGpu metadata{
                    {reflection.strength,reflection.fade,instance.reveal,0.f},
                    {instance.receiver_mask,instance.receiver_owner,0u,0u}};
                push_surface_lighting();
                SDL_PushGPUFragmentUniformData(command,1,&surface,sizeof(surface));
                SDL_PushGPUFragmentUniformData(command,2,&metadata,sizeof(metadata));
                SDL_PushGPUFragmentUniformData(command,3,&sky_moon_gpu,sizeof(sky_moon_gpu));
                std::array<SDL_GPUTextureSamplerBinding,7> bindings{};
                for (std::size_t index=0;index<5;++index) {
                    const auto& channel=desc.channels[index];
                    bindings[index]={textures[channel.texture.slot].gpu,sampler};
                }
                bindings[5]={sky_targets.sets[slot].maps[0].get(),ground_point_sampler};
                bindings[6]={sky_targets.sets[slot].maps[1].get(),ground_point_sampler};
                SDL_BindGPUFragmentSamplers(reflection_pass,0,bindings.data(),
                                            static_cast<Uint32>(bindings.size()));
                const auto full=draw.scissor_enabled ? pixel_scissor(draw.scissor,l,frame.logical_size) : world_scissor;
                const int left=std::clamp(static_cast<int>(std::floor(full.x*scale_x)),0,
                                          static_cast<int>(ground_status.reflection_width));
                const int top=std::clamp(static_cast<int>(std::floor(full.y*scale_y)),0,
                                         static_cast<int>(ground_status.reflection_height));
                const int right=std::clamp(static_cast<int>(std::ceil((full.x+full.w)*scale_x)),left,
                                           static_cast<int>(ground_status.reflection_width));
                const int bottom=std::clamp(static_cast<int>(std::ceil((full.y+full.h)*scale_y)),top,
                                            static_cast<int>(ground_status.reflection_height));
                const SDL_Rect scissor{left,top,right-left,bottom-top};
                if (!scissor.w || !scissor.h) continue;
                SDL_SetGPUScissor(reflection_pass,&scissor);
                SDL_DrawGPUIndexedPrimitives(reflection_pass,draw.index_count,1,draw.first_index,0,0);
                if (measured.measured) {++measured.reflection_draws;++measured.draws;}
            }
            SDL_EndGPURenderPass(reflection_pass);
        }

        std::array<SDL_GPUColorTargetInfo,5> gbuffer{};
        const std::array gbuffer_textures{targets.ground_albedo.get(),targets.surface_world.get(),
                                          targets.surface_material.get(),targets.surface_flags.get(),
                                          targets.water_coverage.get()};
        const std::size_t gbuffer_count=has_water_reflection(options.world_graph) ? 5u : 4u;
        for (std::size_t index=0;index<gbuffer_count;++index) {
            gbuffer[index].texture=gbuffer_textures[index];gbuffer[index].load_op=SDL_GPU_LOADOP_CLEAR;
            gbuffer[index].store_op=SDL_GPU_STOREOP_STORE;gbuffer[index].clear_color={0,0,0,0};
        }
        auto* pass=SDL_BeginGPURenderPass(command,gbuffer.data(),static_cast<Uint32>(gbuffer_count),nullptr);
        if (!pass) throw std::runtime_error(error("Could not begin GroundGBuffer pass"));
        SDL_SetGPUViewport(pass,&world_viewport);SDL_BindGPUGraphicsPipeline(pass,ground_gbuffer_pipeline);
        SDL_BindGPUVertexBuffers(pass,0,&vertex_binding,1);SDL_BindGPUVertexBuffers(pass,1,&surface_binding,1);
        SDL_BindGPUIndexBuffer(pass,&index_binding,SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(command,0,vertex_uniform.data(),sizeof(vertex_uniform));
        for (const auto& draw:prepared.draws) if (draw.ground_material_index) {
            const auto& material_draw=frame.ground_material_draws[draw.ground_material_index-1];
            auto ground_surface = material_draw.surface;
            if (is_contact_graph(options.world_graph))
                ground_surface.flags[1] = static_cast<std::uint32_t>(material_draw.contact_receiver);
            SDL_PushGPUFragmentUniformData(command,0,&ground_surface,sizeof(ground_surface));
            if (has_water_reflection(options.world_graph)) {
                const bool water=material_draw.water_surface_class==GroundSurfaceClass::Water;
                const WaterGroundGpu receiver{{
                    water ? material_draw.geometric_coverage : 0.f,
                    float(static_cast<std::uint32_t>(material_draw.water_surface_class))/255.f,
                    water ? float(material_draw.shore_edges)/15.f : 0.f,
                    water ? 1.f : 0.f}};
                SDL_PushGPUFragmentUniformData(command,1,&receiver,sizeof(receiver));
                if (measured.measured && water) ++measured.water_draws;
            }
            const SDL_GPUTextureSamplerBinding binding{
                textures[material_draw.albedo.slot].gpu,sampler};
            SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
            const auto scissor=draw.scissor_enabled?pixel_scissor(draw.scissor,l,frame.logical_size):world_scissor;
            SDL_SetGPUScissor(pass,&scissor);
            SDL_DrawGPUIndexedPrimitives(pass,draw.count,1,draw.first,0,0);
        }
        SDL_EndGPURenderPass(pass);

        if (!frame.contact_shadows.empty()) {
            SDL_GPUColorTargetInfo contact_target{};
            contact_target.texture=targets.ground_albedo.get();
            contact_target.load_op=SDL_GPU_LOADOP_LOAD;contact_target.store_op=SDL_GPU_STOREOP_STORE;
            pass=SDL_BeginGPURenderPass(command,&contact_target,1,nullptr);
            if (!pass) throw std::runtime_error(error("Could not begin GroundContact pass"));
            const SDL_GPUViewport viewport{0,0,static_cast<float>(l.pixel_w),static_cast<float>(l.pixel_h),0,1};
            SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,ground_contact_pipeline);
            for (std::size_t index=0;index<frame.contact_shadows.size();++index) {
                const auto& clipped=contact_scissors[index];
                if (!clipped.w || !clipped.h) continue;
                const auto& draw=frame.contact_shadows[index];
                const SDL_Rect scissor{clipped.x,clipped.y,clipped.w,clipped.h};
                SDL_SetGPUScissor(pass,&scissor);
                const std::array<SDL_GPUTextureSamplerBinding,3> bindings{{
                    {targets.surface_world.get(),ground_point_sampler},
                    {targets.surface_flags.get(),ground_point_sampler},
                    {contacts[draw.art.slot].texture.get(),sampler}}};
                SDL_BindGPUFragmentSamplers(pass,0,bindings.data(),static_cast<Uint32>(bindings.size()));
                SDL_PushGPUFragmentUniformData(command,0,&draw.surface,sizeof(draw.surface));
                SDL_DrawGPUPrimitives(pass,3,1,0,0);
                if (measured.measured) {++measured.contact_draws;++measured.contact_mask_binds;++measured.draws;}
            }
            SDL_EndGPURenderPass(pass);
        }

        std::array<SDL_GPUColorTargetInfo,2> lighting_targets{};
        lighting_targets[0].texture=targets.scene.get();lighting_targets[0].load_op=SDL_GPU_LOADOP_CLEAR;
        lighting_targets[0].store_op=SDL_GPU_STOREOP_STORE;
        lighting_targets[0].clear_color={srgb_byte_to_linear(frame.clear.r),srgb_byte_to_linear(frame.clear.g),
                                 srgb_byte_to_linear(frame.clear.b),1.f};
        lighting_targets[1].texture=targets.world_meta.get();lighting_targets[1].load_op=SDL_GPU_LOADOP_CLEAR;
        lighting_targets[1].store_op=SDL_GPU_STOREOP_STORE;lighting_targets[1].clear_color={0,0,0,0};
        pass=SDL_BeginGPURenderPass(command,lighting_targets.data(),static_cast<Uint32>(lighting_targets.size()),nullptr);
        if (!pass) throw std::runtime_error(error("Could not begin GroundLighting pass"));
        // These textures cover the entire drawable, including letterbox bands.
        // Full-target UVs must match physical GBuffer pixels; restricting the
        // fullscreen triangle's viewport to the world rectangle stretches UVs.
        const SDL_GPUViewport lighting_viewport{0,0,static_cast<float>(l.pixel_w),
                                                static_cast<float>(l.pixel_h),0,1};
        SDL_SetGPUViewport(pass,&lighting_viewport);SDL_SetGPUScissor(pass,&world_scissor);
        SDL_BindGPUGraphicsPipeline(pass,ground_lighting_pipeline);
        if (has_water_reflection(options.world_graph)) {
            const std::array<SDL_GPUTextureSamplerBinding,9> bindings{{
                {targets.ground_albedo.get(),ground_point_sampler},
                {targets.surface_world.get(),ground_point_sampler},
                {targets.surface_material.get(),ground_point_sampler},
                {targets.surface_flags.get(),ground_point_sampler},
                {targets.water_coverage.get(),ground_point_sampler},
                {targets.reflection.get(),sampler},
                {textures[frame.water_normal.slot].gpu,water_repeat_sampler},
                {sky_targets.sets[slot].maps[0].get(),ground_point_sampler},
                {sky_targets.sets[slot].maps[1].get(),ground_point_sampler}}};
            SDL_BindGPUFragmentSamplers(pass,0,bindings.data(),static_cast<Uint32>(bindings.size()));
            SDL_PushGPUFragmentUniformData(command,3,&*frame.water_surface,sizeof(WaterSurfaceGpu));
        } else {
            const std::array<SDL_GPUTextureSamplerBinding,4> ground_bindings{{
                {targets.ground_albedo.get(),ground_point_sampler},{targets.surface_world.get(),ground_point_sampler},
                {targets.surface_material.get(),ground_point_sampler},{targets.surface_flags.get(),ground_point_sampler}}};
            SDL_BindGPUFragmentSamplers(pass,0,ground_bindings.data(),static_cast<Uint32>(ground_bindings.size()));
        }
        if (has_sky_shadows(options.world_graph) && !has_water_reflection(options.world_graph)) {
            const std::array<SDL_GPUTextureSamplerBinding,2> maps{{
                {sky_targets.sets[slot].maps[0].get(),ground_point_sampler},
                {sky_targets.sets[slot].maps[1].get(),ground_point_sampler}}};
            SDL_BindGPUFragmentSamplers(pass,4,maps.data(),2);
            SDL_PushGPUFragmentUniformData(command,2,&sky.gpu,sizeof(sky.gpu));
        }
        if (has_water_reflection(options.world_graph))
            SDL_PushGPUFragmentUniformData(command,2,&sky.gpu,sizeof(sky.gpu));
        SDL_BindGPUFragmentStorageBuffers(pass,0,&resources.occluders,1);
        push_surface_lighting();
        if (has_moon_direct(options.world_graph))
            SDL_PushGPUFragmentUniformData(command,1,&*frame.moon_lighting,sizeof(MoonLightingGpu));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);
        SDL_EndGPURenderPass(pass);

        for (auto& target:lighting_targets) target.load_op=SDL_GPU_LOADOP_LOAD;
        pass=SDL_BeginGPURenderPass(command,lighting_targets.data(),static_cast<Uint32>(lighting_targets.size()),nullptr);
        if (!pass) throw std::runtime_error(error("Could not begin WorldForward pass"));
        SDL_SetGPUViewport(pass,&world_viewport);SDL_BindGPUGraphicsPipeline(pass,material_forward_pipeline);
        SDL_BindGPUFragmentStorageBuffers(pass,0,&resources.occluders,1);
        SDL_BindGPUVertexBuffers(pass,0,&vertex_binding,1);
        SDL_BindGPUIndexBuffer(pass,&index_binding,SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(command,0,vertex_uniform.data(),sizeof(vertex_uniform));
        for (const auto& draw:prepared.draws) if (draw.sprite_material_index) {
            const auto& instance=frame.sprite_material_draws[draw.sprite_material_index-1];
            const auto& desc=material(instance.material).desc;
            SpriteSurfaceGpu surface=instance.surface;
            for (std::size_t index=0;index<surface.uv_regions.size();++index) {
                const auto& uv=desc.channels[index].uv;
                surface.uv_regions[index]={uv.x,uv.y,uv.w,uv.h};
            }
            surface.emission=desc.emission;
            surface.emission[3]*=instance.emission_scale;
            const ForwardMetadataGpu metadata{{instance.reveal,0,0,0},
                                               {instance.receiver_mask,instance.receiver_owner,0,0}};
            push_surface_lighting();
            SDL_PushGPUFragmentUniformData(command,1,&surface,sizeof(surface));
            SDL_PushGPUFragmentUniformData(command,2,&metadata,sizeof(metadata));
            if (has_moon_direct(options.world_graph) && !has_sky_shadows(options.world_graph))
                SDL_PushGPUFragmentUniformData(command,3,&*frame.moon_lighting,sizeof(MoonLightingGpu));
            if (has_sky_shadows(options.world_graph)) {
                const std::array<SDL_GPUTextureSamplerBinding,2> maps{{
                    {sky_targets.sets[slot].maps[0].get(),ground_point_sampler},
                    {sky_targets.sets[slot].maps[1].get(),ground_point_sampler}}};
                SDL_BindGPUFragmentSamplers(pass,5,maps.data(),2);
                const SkyMoonLightingGpu sky_moon_gpu{*frame.moon_lighting,sky.gpu};
                SDL_PushGPUFragmentUniformData(command,3,&sky_moon_gpu,sizeof(sky_moon_gpu));
            }
            std::array<SDL_GPUTextureSamplerBinding,5> bindings{};
            for (std::size_t index=0;index<bindings.size();++index) {
                const auto& channel=desc.channels[index];
                bindings[index]={textures[channel.texture.slot].gpu,sampler};
            }
            SDL_BindGPUFragmentSamplers(pass,0,bindings.data(),static_cast<Uint32>(bindings.size()));
            const auto scissor=draw.scissor_enabled?pixel_scissor(draw.scissor,l,frame.logical_size):world_scissor;
            SDL_SetGPUScissor(pass,&scissor);
            SDL_DrawGPUIndexedPrimitives(pass,draw.count,1,draw.first,0,0);
        }
        SDL_EndGPURenderPass(pass);

        SDL_GPUColorTargetInfo composite{};composite.texture=targets.final.get();
        composite.load_op=SDL_GPU_LOADOP_DONT_CARE;composite.store_op=SDL_GPU_STOREOP_STORE;
        pass=SDL_BeginGPURenderPass(command,&composite,1,nullptr);
        if (!pass) throw std::runtime_error(error("Could not begin ground composite pass"));
        const SDL_GPUViewport full_viewport{0,0,static_cast<float>(l.pixel_w),static_cast<float>(l.pixel_h),0,1};
        const SDL_Rect full_scissor{0,0,l.pixel_w,l.pixel_h};
        SDL_SetGPUViewport(pass,&full_viewport);SDL_SetGPUScissor(pass,&full_scissor);
        SDL_BindGPUGraphicsPipeline(pass,hdr_composite_pipeline);
        const SDL_GPUTextureSamplerBinding scene_binding{targets.scene.get(),sampler};
        SDL_BindGPUFragmentSamplers(pass,0,&scene_binding,1);
        const std::array<float,4> exposure{frame.world_exposure,0,0,0};
        SDL_PushGPUFragmentUniformData(command,0,exposure.data(),sizeof(exposure));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);

        SDL_GPUColorTargetInfo ui{};ui.texture=targets.final.get();ui.load_op=SDL_GPU_LOADOP_LOAD;
        ui.store_op=SDL_GPU_STOREOP_STORE;
        pass=SDL_BeginGPURenderPass(command,&ui,1,nullptr);
        if (!pass) throw std::runtime_error(error("Could not begin ground UI pass"));
        SDL_SetGPUViewport(pass,&world_viewport);
        SDL_BindGPUVertexBuffers(pass,0,&vertex_binding,1);
        SDL_BindGPUIndexBuffer(pass,&index_binding,SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(command,0,vertex_uniform.data(),sizeof(vertex_uniform));
        for (const auto& draw:prepared.draws) if (draw.pass==Pass::Ui) {
            const auto pipeline_index=draw.blend==Blend::Opaque?0:draw.blend==Blend::Alpha?1:
                draw.blend==Blend::Additive?2:3;
            SDL_BindGPUGraphicsPipeline(pass,pipelines[pipeline_index]);
            const auto* texture=draw.text?gpu_text.at(draw.text_key).texture:
                (draw.texture.slot?resolved_textures[draw.texture.slot]:white);
            auto* texture_sampler=!draw.text&&draw.texture.slot&&textures[draw.texture.slot].mipmapped?
                mip_sampler:sampler;
            const SDL_GPUTextureSamplerBinding binding{const_cast<SDL_GPUTexture*>(texture),texture_sampler};
            SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
            const auto scissor=draw.scissor_enabled?pixel_scissor(draw.scissor,l,frame.logical_size):world_scissor;
            SDL_SetGPUScissor(pass,&scissor);
            SDL_DrawGPUIndexedPrimitives(pass,draw.count,1,draw.first,0,0);
        }
        SDL_EndGPURenderPass(pass);
        if (measured.measured) measured.draws+=2;
    }

    void render_gpu(const Frame2D& frame,const std::filesystem::path& capture,
                    WorldGraph effective_graph) {
        begin_telemetry(!capture.empty());
        RendererFrameTelemetry measured{};
        measured.measured=options.collect_telemetry;
        measured.capture=!capture.empty();
        const auto total_start=measured.measured?Clock::now():Clock::time_point{};
        auto phase_start=total_start;
        const bool contact_graph=is_contact_graph(effective_graph);
        const bool ground_graph=is_ground_graph(effective_graph);
        const bool water_graph=has_water_reflection(effective_graph);
        validate_frame(frame, ground_graph);
        if (ground_graph) validate_ground_forward_frame(frame, contact_graph, water_graph);
        else if (frame.contact_projection || !frame.contact_shadows.empty())
            throw std::invalid_argument("Contact data requires GroundContactSlice");
        else if (!frame.ground_material_draws.empty() || std::any_of(
                     frame.commands.begin(),frame.commands.end(),[](const auto& command){
                         const auto* draw=std::get_if<GeometryDraw>(&command);
                         return draw && draw->ground_material_index;
                     }))
            throw std::invalid_argument("Ground materials require GroundForwardSlice");
        // Full CPU preparation/validation is before any frame fence/resource/
        // command acquisition. No caller-authored light projection is trusted.
        const auto sky=has_sky_shadows(effective_graph)
            ? prepare_sky_shadows(frame.sky_shadow_triangles,frame.surface_lighting,*frame.moon_lighting)
            : PreparedSkyShadows{};
        validate_textures(frame);
        if (ground_graph && !ground_status.available) throw std::invalid_argument(ground_status.reason);
        if (frame.linear_hdr_world && !hdr_status.available) throw std::invalid_argument(hdr_status.reason);
        if (frame.linear_hdr_world && SDL_GetGPUSwapchainTextureFormat(gpu, window) !=
                static_cast<SDL_GPUTextureFormat>(hdr_status.swapchain_format))
            throw std::runtime_error("HDR swapchain format changed; explicit color contract must be revalidated");
        if(measured.measured){const auto now=Clock::now();measured.validation_ms=elapsed_ms(phase_start,now);phase_start=now;}
        // Some native backends still provide a texture while minimized. Treat
        // the window state explicitly rather than relying on a null swapchain.
        const bool minimized=(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)!=0;
        if(minimized&&capture.empty()){
            if(measured.measured)measured.cpu_total_ms=elapsed_ms(total_start,Clock::now());
            finish_telemetry(measured);
            return;
        }
        logical=frame.logical_size;const auto l=layout(window,logical);
        // Compute all fallible scissor work before resource/command acquisition.
        std::vector<ContactScissor> contact_scissors;
        contact_scissors.reserve(frame.contact_shadows.size());
        for (const auto& draw:frame.contact_shadows)
            contact_scissors.push_back(contact_shadow_scissor(draw.surface,*frame.contact_projection,
                {l.x,l.y,l.scale,static_cast<std::uint32_t>(l.pixel_w),static_cast<std::uint32_t>(l.pixel_h)},
                {0,0,double(frame.logical_size.width),double(frame.logical_size.height)}));
        const auto slot = frame_index%(ground_graph ? ground_frame_count : presentation.effective.frames_in_flight);
        auto& resources=frames[slot];release_frame(resources);
        // Candidate failure is still cancellable before swapchain acquisition.
        if (has_sky_shadows(effective_graph)) ensure_sky_targets();
        if(gpu_text.size()>1024){if(!SDL_WaitForGPUIdle(gpu))throw std::runtime_error(error("Could not prune GPU text cache"));for(auto& [_,item]:gpu_text)SDL_ReleaseGPUTexture(gpu,item.texture);gpu_text.clear();}
        if(measured.measured){const auto now=Clock::now();measured.resource_wait_ms=elapsed_ms(phase_start,now);phase_start=now;}
        auto* command=SDL_AcquireGPUCommandBuffer(gpu);if(!command)throw std::runtime_error(error("Could not acquire GPU command buffer"));
        CommandGuard command_guard{command};
        SDL_GPUTexture* swap{};Uint32 sw{},sh{};
        const auto swap_start=measured.measured?Clock::now():Clock::time_point{};
        if(!minimized&&!SDL_WaitAndAcquireGPUSwapchainTexture(command,window,&swap,&sw,&sh))
            throw std::runtime_error(error("Could not wait for GPU swapchain texture"));
        if(swap)command_guard.swapchain_acquired();
        if(measured.measured)measured.swapchain_wait_ms=elapsed_ms(swap_start,Clock::now());
        if(!swap&&capture.empty()){
            if(measured.measured)measured.cpu_total_ms=elapsed_ms(total_start,Clock::now());
            finish_telemetry(measured);
            return;
        }
        std::vector<SDL_GPUTexture*> pending_textures;
        bool submitted=false;
        try{
        ++frame_index;
        pending_textures.reserve(frame.commands.size());
        const auto resize_start=measured.measured?Clock::now():Clock::time_point{};
        if (ground_graph) ensure_ground_targets(l.pixel_w,l.pixel_h);
        else if (frame.linear_hdr_world) ensure_hdr_targets(l.pixel_w,l.pixel_h);
        else ensure_offscreen(l.pixel_w,l.pixel_h);
        auto* final_target = ground_graph ? ground_targets.sets[slot].final.get() :
            (frame.linear_hdr_world ? hdr_targets.final[slot].get() : offscreen);
        auto* world_target = ground_graph ? ground_targets.sets[slot].scene.get() :
            (frame.linear_hdr_world ? hdr_targets.world[slot].get() : offscreen);
        if(measured.measured)measured.resource_wait_ms+=elapsed_ms(resize_start,Clock::now());
        phase_start=measured.measured?Clock::now():Clock::time_point{};
        std::vector<SDL_GPUTexture*> resolved_textures(textures.size());
        for(const auto& item:frame.commands)if(const auto* draw=std::get_if<GeometryDraw>(&item);draw&&draw->texture.slot)
            resolved_textures[draw->texture.slot]=textures[draw->texture.slot].gpu;
        auto prepared=prepare_validated(frame,[&](const TextKey& key){const auto& t=gpu_raster(key,command,resources,pending_textures);return Point{t.w,t.h};});
        if(measured.measured){
            measured.vertices=prepared.vertices.size();measured.indices=prepared.indices.size();measured.draws=prepared.draws.size();
            measured.surface_draws=std::count_if(prepared.draws.begin(),prepared.draws.end(),[](const auto& draw){return draw.surface_lit;});
            measured.ground_draws=std::count_if(prepared.draws.begin(),prepared.draws.end(),[](const auto& draw){return draw.ground_material_index!=0;});
            measured.material_draws=std::count_if(prepared.draws.begin(),prepared.draws.end(),[](const auto& draw){return draw.sprite_material_index!=0;});
            if (ground_graph) {
                measured.occluders=frame.local_occluders.size();
                measured.frame_slot=slot;measured.graph_pass_count=0;
                const auto record_pass=[&](WorldGraphPass pass){
                    measured.graph_passes[measured.graph_pass_count++]=pass;
                };
                if (has_sky_shadows(effective_graph)) {
                    record_pass(WorldGraphPass::SunShadow);
                    record_pass(WorldGraphPass::MoonShadow);
                }
                if (water_graph) record_pass(WorldGraphPass::Reflection);
                record_pass(WorldGraphPass::GroundGBuffer);
                measured.contact_casters=frame.contact_shadows.size();
                if (!frame.contact_shadows.empty()) record_pass(WorldGraphPass::GroundContact);
                record_pass(WorldGraphPass::GroundLighting);
                record_pass(WorldGraphPass::WorldForward);
                record_pass(WorldGraphPass::Composite);
                record_pass(WorldGraphPass::Ui);
            } else if (frame.content == FrameContent::UiOnly) {
                measured.frame_slot=slot;measured.graph_pass_count=1;
                measured.graph_passes[0]=WorldGraphPass::Ui;
            }
            const auto now=Clock::now();measured.preparation_ms=elapsed_ms(phase_start,now);phase_start=now;
        }
        const auto vertex_bytes=static_cast<std::uint32_t>(prepared.vertices.size()*sizeof(Vertex));const auto index_bytes=static_cast<std::uint32_t>(prepared.indices.size()*sizeof(std::uint32_t));
        const bool surface_active = std::any_of(prepared.draws.begin(), prepared.draws.end(),
            [](const auto& draw) { return draw.surface_lit || draw.ground_material_index; });
        const auto surface_bytes = surface_active ? static_cast<std::uint32_t>(frame.surface_vertices.size()*sizeof(SurfaceVertex)) : 0u;
        // A valid zero-count storage binding still has one initialized element.
        // No shader can read it because validated ranges are empty.
        const auto occluder_bytes = ground_graph ? static_cast<std::uint32_t>(
            std::max<std::size_t>(frame.local_occluders.size(),1u)*sizeof(OccluderGpu)) : 0u;
        const auto sky_bytes=static_cast<std::uint32_t>(sky.vertices.size()*sizeof(SkyShadowVertex));
        const std::uint64_t upload_bytes = std::uint64_t(vertex_bytes) + index_bytes + surface_bytes + occluder_bytes + sky_bytes;
        if (upload_bytes > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Prepared frame upload exceeds 32-bit limits");
        if(measured.measured){measured.upload_bytes=upload_bytes;measured.occluder_upload_bytes=occluder_bytes;}
        ensure_buffers(resources,std::max(vertex_bytes,1u),std::max(index_bytes,1u),surface_bytes,occluder_bytes,
                       sky_bytes,static_cast<std::uint32_t>(upload_bytes));
        if(upload_bytes){auto* transfer=resources.upload;
            void* mapped=SDL_MapGPUTransferBuffer(gpu,transfer,false);if(!mapped)throw std::runtime_error(error("Could not map frame upload buffer"));
            if(vertex_bytes)std::memcpy(mapped,prepared.vertices.data(),vertex_bytes);if(index_bytes)std::memcpy(static_cast<std::uint8_t*>(mapped)+vertex_bytes,prepared.indices.data(),index_bytes);
            if (surface_bytes) std::memcpy(static_cast<std::uint8_t*>(mapped)+vertex_bytes+index_bytes,
                                          frame.surface_vertices.data(),surface_bytes);
            if (occluder_bytes) {
                const OccluderGpu unused{};
                const auto* source=frame.local_occluders.empty()?&unused:frame.local_occluders.data();
                std::memcpy(static_cast<std::uint8_t*>(mapped)+vertex_bytes+index_bytes+surface_bytes,
                            source,occluder_bytes);
            }
            if (sky_bytes) std::memcpy(static_cast<std::uint8_t*>(mapped)+vertex_bytes+index_bytes+surface_bytes+occluder_bytes,
                                       sky.vertices.data(),sky_bytes);
            SDL_UnmapGPUTransferBuffer(gpu,transfer);
            auto* copy=SDL_BeginGPUCopyPass(command);if(!copy)throw std::runtime_error(error("Could not begin frame upload"));if(vertex_bytes){SDL_GPUTransferBufferLocation s{transfer,0};SDL_GPUBufferRegion d{resources.vertices,0,vertex_bytes};SDL_UploadToGPUBuffer(copy,&s,&d,false);}if(index_bytes){SDL_GPUTransferBufferLocation s{transfer,vertex_bytes};SDL_GPUBufferRegion d{resources.indices,0,index_bytes};SDL_UploadToGPUBuffer(copy,&s,&d,false);}
            if (surface_bytes) {
                const SDL_GPUTransferBufferLocation source{transfer,vertex_bytes+index_bytes};
                const SDL_GPUBufferRegion destination{resources.surfaces,0,surface_bytes};
                SDL_UploadToGPUBuffer(copy,&source,&destination,false);
            }
            if (occluder_bytes) {
                const SDL_GPUTransferBufferLocation source{transfer,vertex_bytes+index_bytes+surface_bytes};
                const SDL_GPUBufferRegion destination{resources.occluders,0,occluder_bytes};
                SDL_UploadToGPUBuffer(copy,&source,&destination,false);
            }
            if (sky_bytes) {
                const SDL_GPUTransferBufferLocation source{transfer,vertex_bytes+index_bytes+surface_bytes+occluder_bytes};
                const SDL_GPUBufferRegion destination{resources.sky_vertices,0,sky_bytes};
                SDL_UploadToGPUBuffer(copy,&source,&destination,false);
            }
            SDL_EndGPUCopyPass(copy);}
        if (ground_graph) {
            encode_ground_graph(command,resources,frame,prepared,resolved_textures,l,slot,measured,contact_scissors,sky);
        } else {
        const auto draw_pass = [&](SDL_GPUTexture* target, SDL_GPULoadOp load_op, bool linear_world, bool ui_only) {
        SDL_GPUColorTargetInfo color{};color.texture=target;
        color.clear_color=linear_world ? SDL_FColor{srgb_byte_to_linear(frame.clear.r),srgb_byte_to_linear(frame.clear.g),srgb_byte_to_linear(frame.clear.b),1.f} :
            SDL_FColor{frame.clear.r/255.f,frame.clear.g/255.f,frame.clear.b/255.f,frame.clear.a/255.f};
        color.load_op=load_op;color.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);if(!pass)throw std::runtime_error(error("Could not begin GPU draw pass"));SDL_GPUViewport viewport{l.x,l.y,logical.width*l.scale,logical.height*l.scale,0,1};SDL_SetGPUViewport(pass,&viewport);
        SDL_GPUBufferBinding vb{resources.vertices,0},ib{resources.indices,0};SDL_BindGPUVertexBuffers(pass,0,&vb,1);SDL_BindGPUIndexBuffer(pass,&ib,SDL_GPU_INDEXELEMENTSIZE_32BIT);
        const std::array<float,4> uniform{static_cast<float>(logical.width),static_cast<float>(logical.height),0,0};SDL_PushGPUVertexUniformData(command,0,uniform.data(),sizeof(uniform));
        if (surface_active) {
            const SDL_GPUBufferBinding surface_binding{resources.surfaces,0};
            SDL_BindGPUVertexBuffers(pass,1,&surface_binding,1);
        }
        SDL_GPUGraphicsPipeline* bound_pipeline{};
        const SDL_GPUTexture* bound_texture{};
        SDL_GPUSampler* bound_sampler{};
        SDL_Rect bound_scissor{};
        bool scissor_bound=false;
        for(const auto& draw:prepared.draws){const auto pipeline_index=draw.blend==Blend::Opaque?0:draw.blend==Blend::Alpha?1:draw.blend==Blend::Additive?2:3;
            if (frame.linear_hdr_world && ((draw.pass == Pass::Ui) != ui_only)) continue;
            auto* next_pipeline=draw.sprite_material_index ? material_sprite_pipeline :
                draw.surface_lit ? (linear_world ? surface_hdr_pipeline : surface_pipeline) :
                (linear_world ? world_linear_pipelines[pipeline_index] : pipelines[pipeline_index]);
            const bool pipeline_changed=next_pipeline!=bound_pipeline;
            if(pipeline_changed){SDL_BindGPUGraphicsPipeline(pass,next_pipeline);bound_pipeline=next_pipeline;}
            if (draw.sprite_material_index) {
                const auto& instance=frame.sprite_material_draws[draw.sprite_material_index-1];
                const auto& desc=material(instance.material).desc;
                SpriteSurfaceGpu surface=instance.surface;
                for (std::size_t index=0; index<surface.uv_regions.size(); ++index) {
                    const auto& uv=desc.channels[index].uv;
                    surface.uv_regions[index]={uv.x,uv.y,uv.w,uv.h};
                }
                surface.emission=desc.emission;
                surface.emission[3]*=instance.emission_scale;
                SDL_PushGPUFragmentUniformData(command,0,&frame.surface_lighting,
                                               sizeof(frame.surface_lighting));
                SDL_PushGPUFragmentUniformData(command,1,&surface,sizeof(surface));
                std::array<SDL_GPUTextureSamplerBinding,5> bindings{};
                for (std::size_t index=0; index<bindings.size(); ++index) {
                    const auto& channel=desc.channels[index];
                    bindings[index]={textures[channel.texture.slot].gpu,sampler};
                }
                SDL_BindGPUFragmentSamplers(pass,0,bindings.data(),
                                            static_cast<Uint32>(bindings.size()));
                bound_texture=nullptr;bound_sampler=nullptr;
            } else if (pipeline_changed && draw.surface_lit)
                SDL_PushGPUFragmentUniformData(command,0,&frame.surface_lighting,sizeof(frame.surface_lighting));
            else if (pipeline_changed && linear_world) {
                const std::array<std::uint32_t,4> alpha_mode{draw.blend==Blend::PremultipliedAlpha ? 1u : 0u,0u,0u,0u};
                SDL_PushGPUFragmentUniformData(command,0,alpha_mode.data(),sizeof(alpha_mode));
            }
            if (!draw.sprite_material_index) {
                const auto* texture=draw.text?gpu_text.at(draw.text_key).texture:(draw.texture.slot?resolved_textures[draw.texture.slot]:white);
                auto* texture_sampler=!draw.text&&draw.texture.slot&&textures[draw.texture.slot].mipmapped?mip_sampler:sampler;
                if(pipeline_changed||texture!=bound_texture||texture_sampler!=bound_sampler){
                    const SDL_GPUTextureSamplerBinding binding{const_cast<SDL_GPUTexture*>(texture),texture_sampler};
                    SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
                    bound_texture=texture;bound_sampler=texture_sampler;
                }
            }
            const auto rect=draw.scissor_enabled?pixel_scissor(draw.scissor,l,logical):
                SDL_Rect{static_cast<int>(l.x),static_cast<int>(l.y),static_cast<int>(logical.width*l.scale),static_cast<int>(logical.height*l.scale)};
            if(!scissor_bound||rect.x!=bound_scissor.x||rect.y!=bound_scissor.y||
               rect.w!=bound_scissor.w||rect.h!=bound_scissor.h){
                SDL_SetGPUScissor(pass,&rect);bound_scissor=rect;scissor_bound=true;
            }
            SDL_DrawGPUIndexedPrimitives(pass,draw.count,1,draw.first,0,0);}
        SDL_EndGPURenderPass(pass);
        };
        draw_pass(world_target,SDL_GPU_LOADOP_CLEAR,frame.linear_hdr_world,false);
        if (frame.linear_hdr_world) {
            // World target is no longer active. Final shader alone performs
            // exposure + SDR clamp + sRGB encode; UI never enters this pass.
            SDL_GPUColorTargetInfo color{};color.texture=final_target;
            color.load_op=SDL_GPU_LOADOP_DONT_CARE;color.store_op=SDL_GPU_STOREOP_STORE;
            auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);
            if(!pass)throw std::runtime_error(error("Could not begin HDR composite pass"));
            const SDL_GPUViewport viewport{0,0,static_cast<float>(l.pixel_w),static_cast<float>(l.pixel_h),0,1};
            SDL_SetGPUViewport(pass,&viewport);
            const SDL_Rect scissor{0,0,l.pixel_w,l.pixel_h};SDL_SetGPUScissor(pass,&scissor);
            SDL_BindGPUGraphicsPipeline(pass,hdr_composite_pipeline);
            const SDL_GPUTextureSamplerBinding binding{world_target,sampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
            const std::array<float,4> exposure{frame.world_exposure,0,0,0};
            SDL_PushGPUFragmentUniformData(command,0,exposure.data(),sizeof(exposure));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);
            SDL_EndGPURenderPass(pass);
            if(measured.measured)++measured.draws;
            if(std::any_of(prepared.draws.begin(),prepared.draws.end(),[](const auto& draw){return draw.pass==Pass::Ui;}))
                draw_pass(final_target,SDL_GPU_LOADOP_LOAD,false,true);
        }
        }
        // Both source and SDR swapchain hold encoded UNORM bytes here. This
        // blit is presentation only, never the HDR color conversion.
        if(swap){SDL_GPUBlitInfo blit{};blit.source={final_target,0,0,0,0,static_cast<Uint32>(l.pixel_w),static_cast<Uint32>(l.pixel_h)};blit.destination={swap,0,0,0,0,sw,sh};blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;SDL_BlitGPUTexture(command,&blit);}
        if(measured.measured){const auto now=Clock::now();measured.encoding_ms=elapsed_ms(phase_start,now);phase_start=now;}
        if(!capture.empty()){
            const auto capture_start=measured.measured?Clock::now():Clock::time_point{};
            queue_capture(command,resources,final_target,l.pixel_w,l.pixel_h);
            if(measured.measured)measured.capture_ms+=elapsed_ms(capture_start,Clock::now());
        }
        const auto submit_start=measured.measured?Clock::now():Clock::time_point{};
        resources.fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.release());
        if(!resources.fence){
            if(water_graph&&water_readback_valid&&water_readback_slot==slot)water_readback_valid=false;
            throw std::runtime_error(error("Could not submit GPU frame"));
        }
        submitted=true;
        if(water_graph){
            water_readback_slot=slot;
            water_readback_generation=ground_status.target_generation;
            water_readback_valid=true;
        }
        submitted_frames=true;
        measured.submitted=true;measured.presentation_queued=swap!=nullptr;
        if(measured.measured)measured.submit_ms=elapsed_ms(submit_start,Clock::now());
        if(!capture.empty()){
            const auto capture_start=measured.measured?Clock::now():Clock::time_point{};
            finish_capture(resources,capture,l.pixel_w,l.pixel_h);
            if(measured.measured)measured.capture_ms+=elapsed_ms(capture_start,Clock::now());
        }
        }catch(...){
            if(!submitted&&command_guard.command&&!command_guard.can_cancel){
                auto* recovery=SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.release());
                if(recovery)resources.fence=recovery;
                submitted=recovery!=nullptr;
                if(recovery&&water_graph&&water_readback_valid&&water_readback_slot==slot)
                    water_readback_valid=false;
            }
            if(!submitted)for(auto* texture:pending_textures){
                const auto found=std::find_if(gpu_text.begin(),gpu_text.end(),[&](const auto& item){return item.second.texture==texture;});
                if(found!=gpu_text.end()){SDL_ReleaseGPUTexture(gpu,found->second.texture);gpu_text.erase(found);}
            }
            throw;
        }
        if(measured.measured)measured.cpu_total_ms=elapsed_ms(total_start,Clock::now());
        finish_telemetry(measured);
    }
};

Renderer2D::Renderer2D(SDL_Window* window,const std::filesystem::path& assets,RendererOptions options)
    :impl_(std::make_unique<Impl>(window,assets,options)){}
Renderer2D::~Renderer2D()=default;
Point Renderer2D::measure_text(std::string_view text,float size){return impl_->measure(text,size);}
TextureHandle Renderer2D::create_texture(Extent size,std::span<const std::uint8_t> rgba,TextureOptions options){return impl_->create_texture(size,rgba,options);}
TextureHandle Renderer2D::create_sampled_texture(SampledTextureDesc desc,std::span<const std::uint8_t> bytes){return impl_->create_sampled_texture(desc,bytes);}
void Renderer2D::release_texture(TextureHandle texture){impl_->release_texture(texture);}
MaterialHandle Renderer2D::create_sprite_material(const SpriteMaterialDesc& desc){return impl_->create_sprite_material(desc);}
void Renderer2D::release_sprite_material(MaterialHandle material){impl_->release_sprite_material(material);}
ContactArtHandle Renderer2D::create_contact_art(const ContactArtDesc& desc,std::span<const std::uint8_t> bytes){return impl_->create_contact_art(desc,bytes);}
void Renderer2D::release_contact_art(ContactArtHandle art){impl_->release_contact_art(art);}
const ContactArtStatus& Renderer2D::contact_art_status()const{return impl_->contact_status;}
Extent Renderer2D::fitted_logical_size() const {
    int pixel_w = 1, pixel_h = 1;
    SDL_GetWindowSizeInPixels(impl_->window, &pixel_w, &pixel_h);
    pixel_w = std::max(pixel_w, 1);
    pixel_h = std::max(pixel_h, 1);
    constexpr std::uint32_t logical_height = 900;
    const auto fitted_width = static_cast<std::uint32_t>(std::lround(
        double(logical_height) * double(pixel_w) / double(pixel_h)));
    return {std::max(1440u, fitted_width), logical_height};
}
void Renderer2D::set_logical_size(Extent size) {
    if (!size.width || !size.height)
        throw std::invalid_argument("Logical presentation size must be non-zero");
    impl_->logical = size;
}
Extent Renderer2D::logical_size() const { return impl_->logical; }
Point Renderer2D::window_to_logical(Point point)const{const auto l=layout(impl_->window,impl_->logical);return {(point.x*l.window_to_pixel_x-l.x)/l.scale,(point.y*l.window_to_pixel_y-l.y)/l.scale};}
Point Renderer2D::logical_to_window(Point point)const{const auto l=layout(impl_->window,impl_->logical);return {(l.x+point.x*l.scale)/l.window_to_pixel_x,(l.y+point.y*l.scale)/l.window_to_pixel_y};}
void Renderer2D::convert_event(SDL_Event& event)const{auto convert=[&](float& x,float& y){const auto p=window_to_logical({x,y});x=p.x;y=p.y;};switch(event.type){case SDL_EVENT_MOUSE_MOTION:{const auto l=layout(impl_->window,impl_->logical);convert(event.motion.x,event.motion.y);event.motion.xrel*=l.window_to_pixel_x/l.scale;event.motion.yrel*=l.window_to_pixel_y/l.scale;break;}case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:convert(event.button.x,event.button.y);break;case SDL_EVENT_MOUSE_WHEEL:convert(event.wheel.mouse_x,event.wheel.mouse_y);break;default:break;}}
void Renderer2D::render(const Frame2D& frame,const std::filesystem::path& capture){
    if (impl_->render_active) throw std::logic_error("Renderer2D render is not reentrant");
    validate_frame_content(frame);
    // An explicit UI frame uses the established SDR path locally. The selected
    // graph and its retained world targets remain intact for the next World.
    const auto effective_graph = frame.content == FrameContent::UiOnly
        ? WorldGraph::LegacyCombined : impl_->options.world_graph;
    validate_water_contract(frame,has_water_reflection(effective_graph));
    validate_atmosphere_contract(frame,has_atmosphere(effective_graph));
    const bool sky_graph = has_moon_direct(effective_graph);
    if (sky_graph) {
        if (!frame.moon_lighting)
            throw std::invalid_argument("The selected cumulative sky graph requires explicit independent moon lighting");
        validate_moon_lighting(*frame.moon_lighting);
    } else if (frame.moon_lighting) {
        throw std::invalid_argument("Moon lighting requires an explicit cumulative sky graph");
    }
    if (!has_sky_shadows(effective_graph) && !frame.sky_shadow_triangles.empty())
        throw std::invalid_argument("Sky triangles require the explicit SkyShadowSlice graph");
    struct PhaseGuard { bool& active; explicit PhaseGuard(bool& value):active(value){active=true;} ~PhaseGuard(){active=false;} } guard(impl_->render_active);
    if(impl_->options.backend==Backend::Software)impl_->render_software(frame,capture);else impl_->render_gpu(frame,capture,effective_graph);
}
WaterCoverageReadback Renderer2D::read_water_coverage_v1(){return impl_->read_water_coverage_v1();}
const DeviceInfo& Renderer2D::device_info()const{return impl_->info;}
bool Renderer2D::surface_lighting_available()const{return impl_->gpu && impl_->surface_pipeline;}
const WorldHdrStatus& Renderer2D::world_hdr_status()const{return impl_->hdr_status;}
const GroundForwardStatus& Renderer2D::ground_forward_status()const{return impl_->ground_status;}
const SkyShadowStatus& Renderer2D::sky_shadow_status()const{return impl_->sky_status;}
bool Renderer2D::set_lighting_mode(LightingMode mode){return impl_->set_lighting_mode(mode);}
const LightingStatus& Renderer2D::lighting_status()const{return impl_->lighting;}
bool Renderer2D::set_presentation(PresentationRequest request){return impl_->set_presentation(request);}
const PresentationStatus& Renderer2D::presentation_status()const{return impl_->presentation;}
DisplayInfo Renderer2D::display_info()const{
    int width{},height{};
    SDL_GetWindowSizeInPixels(impl_->window,&width,&height);
    float refresh{};
    if(const auto display=SDL_GetDisplayForWindow(impl_->window))
        if(const auto* mode=SDL_GetCurrentDisplayMode(display))refresh=mode->refresh_rate;
    return {{static_cast<std::uint32_t>(std::max(width,0)),static_cast<std::uint32_t>(std::max(height,0))},
            refresh,SDL_GetWindowPixelDensity(impl_->window),SDL_GetWindowDisplayScale(impl_->window)};
}
const RendererFrameTelemetry& Renderer2D::frame_telemetry()const{return impl_->telemetry;}
void Renderer2D::set_telemetry_enabled(bool enabled){impl_->options.collect_telemetry=enabled;impl_->telemetry={};}
bool Renderer2D::has_submitted_frames()const{return impl_->submitted_frames;}

} // namespace ribterra::render

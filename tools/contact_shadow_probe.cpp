#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unversioned"
#endif

namespace {
using Json = nlohmann::json;
using namespace ribterra::render;

constexpr Extent logical_size{640, 480};
constexpr ContactProjection canonical_projection{{320.f, 240.f}, 1.8f};

[[noreturn]] void fail(std::string_view message) { throw std::runtime_error(std::string(message)); }
void require(bool value, std::string_view message) { if (!value) fail(message); }

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path from_utf8(const char* value) {
    const std::string_view bytes(value);
    std::u8string encoded;
    encoded.reserve(bytes.size());
    for (const unsigned char byte : bytes) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(stream.good(), "Could not open retained probe output");
    const auto size = stream.tellg();
    require(size >= 0, "Could not size retained probe output");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty())
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    require(stream.good() || stream.eof(), "Could not read retained probe output");
    return bytes;
}

void write_json_new(const std::filesystem::path& path, const Json& value) {
    require(!std::filesystem::exists(path), "Probe report path already exists");
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not create probe report");
    stream << value.dump(2) << '\n';
    require(stream.good(), "Could not finish probe report");
}

struct Reporter {
    std::filesystem::path output;
    Json checks = Json::array();
    Json outputs = Json::array();
    std::uint32_t serial{};

    void pass(std::string name, Json detail = Json::object()) {
        checks.push_back({{"name", std::move(name)}, {"pass", true}, {"detail", std::move(detail)}});
    }
    std::filesystem::path fresh(std::string_view stem, std::string_view extension = ".png") {
        const auto path = output / (std::string(stem) + std::string(extension));
        require(!std::filesystem::exists(path), "Probe output would overwrite retained evidence");
        outputs.push_back(path.filename().string());
        return path;
    }
    void checkpoint(std::string_view phase, const Json& device = Json::object()) {
        const auto name = "contact-shadow-progress-" + std::to_string(serial++) + ".json";
        write_json_new(output / name, {{"schema", 1}, {"complete", false}, {"phase", phase},
            {"build", RIBTERRA_PROBE_BUILD}, {"device", device}, {"checks", checks}, {"outputs", outputs}});
        outputs.push_back(name);
    }
};

struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
};
Image load_png(const std::filesystem::path& path) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
        SDL_LoadPNG(utf8(path).c_str()), &SDL_DestroySurface);
    require(source != nullptr, "SDL_LoadPNG could not decode an unmodified capture");
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
        SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), &SDL_DestroySurface);
    require(converted != nullptr, "Could not convert captured PNG to RGBA32");
    Image image{converted->w, converted->h,
        std::vector<std::uint8_t>(static_cast<std::size_t>(converted->w) * converted->h * 4u)};
    for (int y = 0; y < image.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(converted->pixels) + y * converted->pitch;
        std::copy_n(row, static_cast<std::size_t>(image.width) * 4u,
                    image.rgba.begin() + static_cast<std::size_t>(y) * image.width * 4u);
    }
    return image;
}
using Pixel = std::array<std::uint8_t, 4>;
Pixel pixel(const Image& image, int x, int y) {
    require(x >= 0 && y >= 0 && x < image.width && y < image.height, "Pixel sample is outside capture");
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
    return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
}
Pixel logical_pixel(const Image& image, Point logical) {
    const double scale = std::min(double(image.width) / logical_size.width,
                                  double(image.height) / logical_size.height);
    const double x = (image.width - logical_size.width * scale) * .5 + logical.x * scale;
    const double y = (image.height - logical_size.height * scale) * .5 + logical.y * scale;
    return pixel(image, std::clamp(static_cast<int>(std::floor(x)), 0, image.width - 1),
                 std::clamp(static_cast<int>(std::floor(y)), 0, image.height - 1));
}
int brightness(Pixel value) { return int(value[0]) + int(value[1]) + int(value[2]); }
Json pixel_json(Pixel value) { return {value[0], value[1], value[2], value[3]}; }

void pump_events() { SDL_Event event{}; while (SDL_PollEvent(&event)) {} }

struct Window {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> value{nullptr, &SDL_DestroyWindow};
    explicit Window(std::string_view title, int width = 640, int height = 480)
        : value(SDL_CreateWindow(std::string(title).c_str(), width, height,
                                 SDL_WINDOW_HIGH_PIXEL_DENSITY), &SDL_DestroyWindow) {
        require(value != nullptr, "Could not create visible diagnostic window");
        require(SDL_SyncWindow(value.get()), "Could not synchronize diagnostic window");
        pump_events();
    }
};

RendererOptions contact_options() {
    RendererOptions options;
    options.backend = Backend::Direct3D12;
    options.world_graph = WorldGraph::GroundContactSlice;
    options.collect_telemetry = true;
    return options;
}
RendererOptions forward_options() {
    auto options = contact_options();
    options.world_graph = WorldGraph::GroundForwardSlice;
    return options;
}

std::array<float, 3> cross_normal(const std::array<float, 3>& u,
                                  const std::array<float, 3>& v) {
    std::array<float, 3> n{u[1] * v[2] - u[2] * v[1],
                           u[2] * v[0] - u[0] * v[2],
                           u[0] * v[1] - u[1] * v[0]};
    const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (float& value : n) value /= length;
    return n;
}
std::array<float, 3> normalized(std::array<float, 3> value) {
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    for (float& component : value) component /= length;
    return value;
}

struct ReceiverSpec {
    std::array<float, 3> center{};
    std::array<float, 3> u{1.f, 0.f, 0.f};
    std::array<float, 3> v{0.f, 1.f, 0.f};
    float extent_u{.72f}, extent_v{.72f};
    ContactReceiver receiver{ContactReceiver::Flat};
    float reveal{1.f};
};

void append_receiver(Frame2D& frame, TextureHandle albedo, const ReceiverSpec& spec,
                     Color tint = {255, 255, 255, 255}) {
    GroundMaterialDraw material;
    material.albedo = albedo;
    material.contact_receiver = spec.receiver;
    material.surface.response = {1.f, 0.f, 0.f, spec.reveal};
    material.surface.flags = {1u, 0u, 0u, 0u};
    material.surface.visibility = {1.f, 1.f, 0.f, 0.f};
    frame.ground_material_draws.push_back(material);
    const std::uint32_t material_index = static_cast<std::uint32_t>(frame.ground_material_draws.size());
    const auto normal = cross_normal(spec.u, spec.v);
    const std::uint32_t base = static_cast<std::uint32_t>(frame.vertices.size());
    constexpr std::array<std::array<float, 2>, 4> signs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    constexpr std::array<Point, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t i = 0; i < signs.size(); ++i) {
        std::array<float, 3> world{};
        for (std::size_t axis = 0; axis < 3; ++axis)
            world[axis] = spec.center[axis] + signs[i][0] * spec.extent_u * spec.u[axis] +
                          signs[i][1] * spec.extent_v * spec.v[axis];
        const auto projected = contact_project_logical(world, canonical_projection);
        frame.vertices.push_back({{projected[0], projected[1]}, uvs[i], tint});
        frame.surface_vertices.push_back({world, normal});
    }
    const std::uint32_t first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::Terrain; draw.blend = Blend::Opaque;
    draw.first_index = first; draw.index_count = 6; draw.ground_material_index = material_index;
    frame.commands.emplace_back(draw);
}

void append_ui(Frame2D& frame) {
    const std::uint32_t base = static_cast<std::uint32_t>(frame.vertices.size());
    for (const auto& vertex : std::array<Vertex, 4>{{
             {{18, 18}, {0, 0}, {215, 42, 76, 255}}, {{70, 18}, {1, 0}, {215, 42, 76, 255}},
             {{70, 42}, {1, 1}, {215, 42, 76, 255}}, {{18, 42}, {0, 1}, {215, 42, 76, 255}}}}) {
        frame.vertices.push_back(vertex);
        frame.surface_vertices.push_back({});
    }
    const std::uint32_t first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::Ui; draw.blend = Blend::Opaque; draw.first_index = first; draw.index_count = 6;
    frame.commands.emplace_back(draw);
}

struct ScenePoints {
    Point flat{}, slope{}, bridge{}, ordinary{}, hidden{}, different_plane{}, ui{30, 28};
};
Point projected(const std::array<float, 3>& world) {
    const auto result = contact_project_logical(world, canonical_projection);
    return {result[0], result[1]};
}

Frame2D make_scene(TextureHandle albedo, bool contact_graph, ScenePoints& points,
                   bool all_ordinary = false) {
    Frame2D frame;
    frame.logical_size = logical_size;
    frame.clear = {11, 15, 22, 255};
    frame.linear_hdr_world = true;
    frame.world_exposure = 1.f;
    if (contact_graph) frame.contact_projection = canonical_projection;
    frame.surface_lighting = {};
    frame.surface_lighting.ambient = {.82f, .82f, .82f, 0.f};
    frame.surface_lighting.sun_color_intensity = {1.f, 1.f, 1.f, 0.f};

    const ReceiverSpec flat{{-1.f, 1.f, 0.f}};
    const ReceiverSpec slope{{1.f, 1.f, 0.f}, normalized({1.f, 0.f, .25f}),
                              {0.f, 1.f, 0.f}, .7f, .7f,
                              ContactReceiver::Slope};
    // The receiver is deliberately wider than its caster so pivot-only samples
    // stay on identical bridge geometry in both controls.
    const ReceiverSpec bridge{{2.f, -2.f, 1.f}, {1, 0, 0}, {0, 1, 0}, 2.f, 2.f,
                               ContactReceiver::Bridge};
    const ReceiverSpec ordinary{{-2.f, 2.f, 0.f}, {1, 0, 0}, {0, 1, 0}, .65f, .65f,
                                 ContactReceiver::Ordinary};
    const ReceiverSpec hidden{{2.f, 0.f, 0.f}, {1, 0, 0}, {0, 1, 0}, .65f, .65f,
                               ContactReceiver::Flat, .5f};
    const ReceiverSpec different_lower{{0.f, -2.f, 0.f}, {1, 0, 0}, {0, 1, 0}, .6f, .6f,
                                        ContactReceiver::Flat};
    const ReceiverSpec different_upper{{1.f, -1.f, 1.f}, {1, 0, 0}, {0, 1, 0}, .6f, .6f,
                                        ContactReceiver::Bridge};
    const auto append = [&](ReceiverSpec spec) {
        if (!contact_graph || all_ordinary) spec.receiver = ContactReceiver::Ordinary;
        append_receiver(frame, albedo, spec);
    };
    append(flat); append(slope); append(bridge); append(ordinary);
    append(hidden); append(different_lower); append(different_upper);
    append_ui(frame);
    points.flat = projected(flat.center); points.slope = projected(slope.center);
    points.bridge = projected(bridge.center); points.ordinary = projected(ordinary.center);
    points.hidden = projected(hidden.center); points.different_plane = projected(different_upper.center);
    return frame;
}

std::vector<std::uint8_t> gradient_mask() {
    std::vector<std::uint8_t> bytes(8u * 8u);
    for (std::uint32_t y = 1; y < 7; ++y)
        for (std::uint32_t x = 1; x < 7; ++x)
            bytes[y * 8u + x] = static_cast<std::uint8_t>(48u + x * 21u + y * 9u);
    return bytes;
}
std::vector<std::uint8_t> atlas_mask() {
    std::vector<std::uint8_t> bytes(16u * 8u, 255u);
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 4; x < 12; ++x) {
            const bool ring = x == 4 || x == 11 || y == 0 || y == 7;
            bytes[y * 16u + x] = ring ? 0u : static_cast<std::uint8_t>(220u - (x - 5u) * 17u);
        }
    return bytes;
}

ContactShadowDraw contact_draw(ContactArtHandle art, const ContactArtDesc& desc,
                               const ReceiverSpec& receiver, std::uint32_t stable_id,
                               std::array<float, 2> pivot = {.5f, .5f}, float opacity = .8f) {
    ContactShadowDraw draw;
    draw.art = art; draw.stable_id = stable_id; draw.owner_id = stable_id + 1000u;
    draw.reveal = 1.f; draw.facing = desc.facing;
    draw.surface.anchor_opacity = {receiver.center[0], receiver.center[1], receiver.center[2], opacity};
    draw.surface.axis_u_extent = {receiver.u[0], receiver.u[1], receiver.u[2], receiver.extent_u};
    draw.surface.axis_v_extent = {receiver.v[0], receiver.v[1], receiver.v[2], receiver.extent_v};
    draw.surface.uv_region = contact_mask_uv(desc.region);
    draw.surface.pivot_plane = {pivot[0], pivot[1], .03f, 0.f};
    return draw;
}

Json telemetry_json(const RendererFrameTelemetry& value) {
    Json passes = Json::array();
    for (std::uint32_t i = 0; i < value.graph_pass_count; ++i)
        passes.push_back(static_cast<std::uint32_t>(value.graph_passes[i]));
    return {{"submitted", value.submitted}, {"frame_slot", value.frame_slot},
            {"graph_pass_count", value.graph_pass_count}, {"graph_passes", passes},
            {"contact_casters", value.contact_casters}, {"contact_draws", value.contact_draws},
            {"contact_mask_binds", value.contact_mask_binds},
            {"gpu_execution_ms", value.gpu_execution_ms ? Json(*value.gpu_execution_ms) : Json(nullptr)}};
}
Json device_json(const DeviceInfo& value) {
    return {{"backend", value.backend}, {"device", value.device}, {"driver", value.driver},
            {"software", value.software}};
}
Json art_status_json(const ContactArtStatus& value) {
    return {{"live_arts", value.live_arts}, {"logical_bytes", value.logical_bytes},
            {"creation_attempts", value.creation_attempts}};
}

Image capture(Renderer2D& renderer, const Frame2D& frame, Reporter& report, std::string_view name) {
    pump_events();
    const auto path = report.fresh(name);
    renderer.render(frame, path);
    return load_png(path);
}
template<class Exception = std::exception, class Function>
std::string expect_throw(Function&& function, std::string_view message) {
    try { function(); }
    catch (const Exception& error) { return error.what(); }
    fail(message);
}

void require_identity(const RendererFrameTelemetry& telemetry, std::uint64_t count) {
    require(telemetry.submitted, "Expected contact diagnostic frame was not submitted");
    require(telemetry.contact_casters == count && telemetry.contact_draws == count &&
            telemetry.contact_mask_binds == count, "Contact telemetry counts do not match submitted casters");
    require(!telemetry.gpu_execution_ms.has_value(), "Pinned SDL unexpectedly reported unavailable GPU timing");
}

Json run_production(Window& window, const std::filesystem::path& assets, Reporter& report) {
    Renderer2D renderer(window.value.get(), assets, contact_options());
    renderer.set_logical_size(logical_size);
    const std::array<std::uint8_t, 4> white_albedo{184, 171, 148, 255};
    const auto albedo = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, white_albedo);
    const ContactArtDesc gradient_desc{{8, 8, 0, 0, 8, 8}, ContactFacing::Omnidirectional};
    const ContactArtDesc atlas_desc{{16, 8, 4, 0, 8, 8}, ContactFacing::Omnidirectional};
    const auto gradient = gradient_mask(); const auto atlas = atlas_mask();
    auto atlas_zero_neighbor = atlas;
    for (std::uint32_t y = 0; y < atlas_desc.region.texture_height; ++y)
        for (std::uint32_t x = 0; x < atlas_desc.region.texture_width; ++x)
            if (x < atlas_desc.region.x || x >= atlas_desc.region.x + atlas_desc.region.width)
                atlas_zero_neighbor[static_cast<std::size_t>(y) * atlas_desc.region.texture_width + x] = 0;
    const auto gradient_art = renderer.create_contact_art(gradient_desc, gradient);
    const auto atlas_art = renderer.create_contact_art(atlas_desc, atlas);
    const auto atlas_zero_art = renderer.create_contact_art(atlas_desc, atlas_zero_neighbor);
    require(renderer.contact_art_status().live_arts == 3 && renderer.contact_art_status().logical_bytes == 320,
            "Contact art publication ledger is incorrect");
    ScenePoints points;
    auto baseline = make_scene(albedo, true, points);
    require(baseline.ground_material_draws[0].contact_receiver == ContactReceiver::Flat &&
            baseline.ground_material_draws[4].contact_receiver == ContactReceiver::Flat &&
            baseline.ground_material_draws[4].surface.response[3] == .5f &&
            baseline.ground_material_draws[6].contact_receiver == ContactReceiver::Bridge,
            "Paired exclusion scene lost its authoritative receiver/reveal classifications");
    const auto baseline_image = capture(renderer, baseline, report, "ambient-baseline");
    const auto baseline_telemetry = renderer.frame_telemetry();
    require(baseline_telemetry.submitted && baseline_telemetry.contact_casters == 0 &&
            baseline_telemetry.contact_draws == 0 && baseline_telemetry.graph_pass_count == 5,
            "Zero-contact graph did not retain the five-pass route");

    const ReceiverSpec flat{{-1.f, 1.f, 0.f}};
    auto flat_contact = baseline;
    flat_contact.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, flat, 1));
    const auto flat_image = capture(renderer, flat_contact, report, "ambient-flat-contact");
    const auto flat_telemetry = renderer.frame_telemetry();
    require_identity(flat_telemetry, 1);
    require(flat_telemetry.graph_pass_count == 6 &&
            flat_telemetry.graph_passes[1] == WorldGraphPass::GroundContact,
            "Contact frame did not insert exactly the GroundContact pass");
    const auto base_flat = logical_pixel(baseline_image, points.flat);
    const auto dark_flat = logical_pixel(flat_image, points.flat);
    require(brightness(dark_flat) + 12 < brightness(base_flat), "Ambient-only contact did not darken its flat footprint");
    const double initial_scale = std::min(double(baseline_image.width) / logical_size.width,
                                           double(baseline_image.height) / logical_size.height);
    const double initial_x = (baseline_image.width - logical_size.width * initial_scale) * .5;
    const double initial_y = (baseline_image.height - logical_size.height * initial_scale) * .5;
    const auto flat_scissor = contact_shadow_scissor(flat_contact.contact_shadows[0].surface,
        canonical_projection, {initial_x, initial_y, initial_scale,
            static_cast<std::uint32_t>(baseline_image.width), static_cast<std::uint32_t>(baseline_image.height)},
        {0, 0, logical_size.width, logical_size.height});
    std::size_t changed_pixels{};
    for (int y = 0; y < baseline_image.height; ++y)
        for (int x = 0; x < baseline_image.width; ++x) {
            const bool changed = pixel(flat_image, x, y) != pixel(baseline_image, x, y);
            if (!changed) continue;
            ++changed_pixels;
            require(x >= flat_scissor.x && y >= flat_scissor.y &&
                    x < flat_scissor.x + flat_scissor.w && y < flat_scissor.y + flat_scissor.h,
                    "Contact changed a pixel outside its conservative bounded scissor");
        }
    require(changed_pixels != 0, "Contact capture contained no causal pixel changes");
    require(logical_pixel(flat_image, points.ui) == logical_pixel(baseline_image, points.ui),
            "UI rectangle changed under contact");
    require(flat_telemetry.frame_slot != baseline_telemetry.frame_slot,
            "Consecutive contact graph frames did not exercise both telemetry slots");
    report.pass("ambient_only_contact_darkens_flat", {{"baseline", pixel_json(base_flat)},
        {"contact", pixel_json(dark_flat)}});
    report.pass("ui_rectangle_byte_exact_under_contact");
    report.pass("contact_graph_uses_both_slots", {{"baseline", baseline_telemetry.frame_slot},
        {"contact", flat_telemetry.frame_slot}});
    report.pass("ground_contact_pass_inserted", {{"baseline_passes", baseline_telemetry.graph_pass_count},
        {"contact_passes", flat_telemetry.graph_pass_count}});
    report.pass("contact_changes_only_bounded_footprint", {{"changed_pixels", changed_pixels},
        {"scissor", {flat_scissor.x, flat_scissor.y, flat_scissor.w, flat_scissor.h}}});
    report.pass("ambient_contact_reference_samples", {{"baseline", pixel_json(base_flat)},
        {"contact", pixel_json(dark_flat)}, {"slot_baseline", baseline_telemetry.frame_slot},
        {"slot_contact", flat_telemetry.frame_slot}});

    const auto clear_sample = logical_pixel(baseline_image, {5, 80});
    require(base_flat != clear_sample, "Flat receiver oracle landed on frame clear instead of ground");
    auto ordinary_frame = flat_contact;
    ordinary_frame.ground_material_draws[0].contact_receiver = ContactReceiver::Ordinary;
    const auto ordinary_image = capture(renderer, ordinary_frame, report, "ordinary-same-geometry-contact");
    require(logical_pixel(ordinary_image, points.flat) == base_flat,
            "Ordinary receiver changed under a centered same-geometry contact");
    require(brightness(dark_flat) + 12 < brightness(base_flat),
            "Flat paired positive did not darken the identical receiver geometry");
    report.pass("ordinary_vs_flat_same_geometry", {{"baseline", pixel_json(base_flat)},
        {"ordinary", pixel_json(logical_pixel(ordinary_image, points.flat))},
        {"flat", pixel_json(dark_flat)}});

    const ReceiverSpec hidden{{2.f, 0.f, 0.f}, {1, 0, 0}, {0, 1, 0}, .65f, .65f,
                               ContactReceiver::Flat, .5f};
    const auto base_hidden = logical_pixel(baseline_image, points.hidden);
    require(base_hidden != clear_sample, "Reveal oracle landed on frame clear instead of ground");
    auto hidden_excluded = baseline;
    hidden_excluded.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, hidden, 10));
    const auto hidden_excluded_image = capture(renderer, hidden_excluded, report, "reveal-half-excluded");
    require(logical_pixel(hidden_excluded_image, points.hidden) == base_hidden,
            "Reveal 0.5 receiver changed under a centered contact");
    auto revealed_baseline = baseline;
    revealed_baseline.ground_material_draws[4].surface.response[3] = 1.f;
    const auto revealed_baseline_image = capture(renderer, revealed_baseline, report, "reveal-one-baseline");
    auto revealed_positive = revealed_baseline;
    revealed_positive.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, hidden, 11));
    const auto revealed_positive_image = capture(renderer, revealed_positive, report, "reveal-one-contact");
    const auto revealed_base_pixel = logical_pixel(revealed_baseline_image, points.hidden);
    const auto revealed_dark_pixel = logical_pixel(revealed_positive_image, points.hidden);
    require(revealed_base_pixel != clear_sample && brightness(revealed_dark_pixel) + 12 < brightness(revealed_base_pixel),
            "Reveal 1 paired positive did not darken identical receiver geometry");
    report.pass("reveal_half_vs_one_same_geometry", {{"half_baseline", pixel_json(base_hidden)},
        {"half_contact", pixel_json(logical_pixel(hidden_excluded_image, points.hidden))},
        {"one_baseline", pixel_json(revealed_base_pixel)}, {"one_contact", pixel_json(revealed_dark_pixel)}});

    const ReceiverSpec lower_plane{{0.f, -2.f, 0.f}, {1, 0, 0}, {0, 1, 0}, .6f, .6f,
                                   ContactReceiver::Flat};
    const ReceiverSpec upper_plane{{1.f, -1.f, 1.f}, {1, 0, 0}, {0, 1, 0}, .6f, .6f,
                                   ContactReceiver::Bridge};
    const auto base_plane = logical_pixel(baseline_image, points.different_plane);
    require(base_plane != clear_sample, "Plane oracle landed on frame clear instead of stacked receiver");
    auto lower_plane_frame = baseline;
    lower_plane_frame.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, lower_plane, 12));
    const auto lower_plane_image = capture(renderer, lower_plane_frame, report, "lower-plane-caster-excluded");
    require(logical_pixel(lower_plane_image, points.different_plane) == base_plane,
            "Lower-plane caster changed stacked upper receiver");
    auto upper_plane_frame = baseline;
    upper_plane_frame.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, upper_plane, 13));
    const auto upper_plane_image = capture(renderer, upper_plane_frame, report, "upper-plane-caster-contact");
    const auto upper_dark = logical_pixel(upper_plane_image, points.different_plane);
    require(brightness(upper_dark) + 12 < brightness(base_plane),
            "Upper-plane paired positive did not darken stacked identical receiver geometry");
    report.pass("lower_vs_upper_plane_same_stacked_receiver", {{"baseline", pixel_json(base_plane)},
        {"lower_caster", pixel_json(logical_pixel(lower_plane_image, points.different_plane))},
        {"upper_caster", pixel_json(upper_dark)}});

    const ReceiverSpec slope{{1.f, 1.f, 0.f}, normalized({1.f, 0.f, .25f}),
                              {0.f, 1.f, 0.f}, .7f, .7f,
                              ContactReceiver::Slope};
    auto slope_frame = baseline;
    slope_frame.contact_shadows.push_back(contact_draw(atlas_art, atlas_desc, slope, 2));
    const auto slope_image = capture(renderer, slope_frame, report, "ambient-slope-contact");
    require(brightness(logical_pixel(slope_image, points.slope)) + 12 <
            brightness(logical_pixel(baseline_image, points.slope)), "Slope contact did not causally darken");
    auto slope_zero_neighbor = baseline;
    slope_zero_neighbor.contact_shadows.push_back(contact_draw(atlas_zero_art, atlas_desc, slope, 20));
    const auto slope_zero_image = capture(renderer, slope_zero_neighbor, report, "subrect-zero-neighbor-contact");
    require(slope_image.rgba == slope_zero_image.rgba,
            "Subrect sampling bled from different bytes outside the authoritative region");
    require(brightness(logical_pixel(slope_zero_image, points.slope)) + 12 <
            brightness(logical_pixel(baseline_image, points.slope)),
            "Subrect no-bleed comparison was trivial instead of a darkening contact");
    report.pass("subrect_byte255_vs_zero_neighbors_byte_exact");
    report.pass("subrect_no_bleed_both_contacts_nontrivial");

    const ReceiverSpec bridge{{2.f, -2.f, 1.f}, {1, 0, 0}, {0, 1, 0}, .65f, .65f,
                               ContactReceiver::Bridge};
    auto bridge_frame = baseline;
    bridge_frame.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, bridge, 3));
    const auto bridge_image = capture(renderer, bridge_frame, report, "ambient-bridge-contact");
    require(brightness(logical_pixel(bridge_image, points.bridge)) + 12 <
            brightness(logical_pixel(baseline_image, points.bridge)), "Bridge contact did not causally darken");
    report.pass("slope_contact_causal_change");
    report.pass("bridge_contact_causal_change");

    auto shifted = baseline;
    ReceiverSpec shifted_flat = flat; shifted_flat.center[0] += .6f;
    shifted_flat.extent_u = shifted_flat.extent_v = .28f;
    shifted.contact_shadows.push_back(contact_draw(gradient_art, gradient_desc, shifted_flat, 4));
    const auto shifted_image = capture(renderer, shifted, report, "contact-position-shift");
    const auto shifted_point = projected(shifted_flat.center);
    require(logical_pixel(shifted_image, points.flat) == logical_pixel(baseline_image, points.flat),
            "Shifted contact remained at its old position");
    require(brightness(logical_pixel(shifted_image, shifted_point)) <
            brightness(logical_pixel(baseline_image, shifted_point)), "Contact did not follow its authored position");

    auto pivoted = baseline;
    auto pivot_draw = contact_draw(gradient_art, gradient_desc, bridge, 5, {.2f, .8f});
    pivoted.contact_shadows.push_back(pivot_draw);
    const auto pivot_image = capture(renderer, pivoted, report, "contact-offcentre-pivot");
    std::array<float, 3> pivot_only_world{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        pivot_only_world[axis] = bridge.center[axis] + .85f * bridge.u[axis] - .85f * bridge.v[axis];
    const auto pivot_sample = projected(pivot_only_world);
    const auto pivot_baseline = logical_pixel(baseline_image, pivot_sample);
    const auto centered_at_shift = logical_pixel(bridge_image, pivot_sample);
    const auto pivoted_at_shift = logical_pixel(pivot_image, pivot_sample);
    require(centered_at_shift == pivot_baseline,
            "Centred caster unexpectedly covered the pivot-only comparison point");
    require(brightness(pivoted_at_shift) + 12 < brightness(pivot_baseline),
            "Off-centre pivot did not darken its shifted-only footprint");
    report.pass("contact_shift_clears_old_position");
    report.pass("contact_shift_follows_new_position");
    report.pass("offcentre_pivot_places_shifted_only_footprint", {{"baseline", pixel_json(pivot_baseline)},
        {"centered", pixel_json(centered_at_shift)}, {"pivoted", pixel_json(pivoted_at_shift)}});

    auto sun_baseline = baseline;
    sun_baseline.surface_lighting.ambient = {0, 0, 0, 0};
    sun_baseline.surface_lighting.sun_color_intensity = {1, 1, 1, .9f};
    auto sun_contact = sun_baseline; sun_contact.contact_shadows = flat_contact.contact_shadows;
    const auto sun_a = capture(renderer, sun_baseline, report, "sun-only-baseline");
    const auto sun_b = capture(renderer, sun_contact, report, "sun-only-contact");
    require(sun_a.rgba == sun_b.rgba, "Contact changed sun-only lighting with ambient disabled");

    auto local_baseline = baseline;
    local_baseline.surface_lighting.ambient = {0, 0, 0, 0};
    local_baseline.surface_lighting.sun_color_intensity = {1, 1, 1, 0};
    local_baseline.surface_lighting.counts[0] = 1;
    auto& light = local_baseline.surface_lighting.lights[0];
    light.anchor_radius = {-1.f, 1.f, 3.f, 8.f};
    light.color_intensity = {1.f, .8f, .6f, 2.f};
    light.occluder_range_flags = {0, 0, 1, 77};
    light.height_falloff_reveal = {-1.f, 2.f, 1.f, 0.f};
    auto local_contact = local_baseline; local_contact.contact_shadows = flat_contact.contact_shadows;
    const auto local_a = capture(renderer, local_baseline, report, "local-only-baseline");
    const auto local_b = capture(renderer, local_contact, report, "local-only-contact");
    require(local_a.rgba == local_b.rgba, "Contact changed local-only lighting with ambient disabled");
    report.pass("sun_only_unchanged_when_ambient_disabled");
    report.pass("local_only_unchanged_when_ambient_disabled");

    auto crowded = baseline;
    crowded.contact_shadows.reserve(contact_shadow_capacity);
    ReceiverSpec small = flat; small.extent_u = small.extent_v = .2f;
    for (std::size_t i = 0; i < contact_shadow_capacity; ++i)
        crowded.contact_shadows.push_back(contact_draw(i % 2 ? gradient_art : atlas_art,
            i % 2 ? gradient_desc : atlas_desc, small, 100u + static_cast<std::uint32_t>(i),
            {.5f, .5f}, .35f));
    (void)capture(renderer, crowded, report, "contact-128-overlap-two-masks");
    const auto crowded_telemetry = renderer.frame_telemetry();
    require_identity(crowded_telemetry, contact_shadow_capacity);
    report.pass("max_128_overlap_two_masks_accepted");
    report.pass("two_contact_masks_alternated", telemetry_json(crowded_telemetry));
    report.pass("contact_telemetry_caster_count_128");
    report.pass("contact_telemetry_draw_count_128");
    report.pass("contact_telemetry_bind_count_128");
    report.pass("gpu_execution_time_unavailable_not_guessed");

    std::vector<ContactArtHandle> distinct_arts;
    distinct_arts.reserve(contact_shadow_capacity);
    for (std::size_t i = 0; i < contact_shadow_capacity; ++i) {
        std::vector<std::uint8_t> unique_mask(8u * 8u);
        const auto value = static_cast<std::uint8_t>(i + 1u);
        for (std::uint32_t y = 1; y < 7; ++y)
            std::fill(unique_mask.begin() + y * 8u + 1u, unique_mask.begin() + y * 8u + 7u, value);
        const auto handle = renderer.create_contact_art(gradient_desc, unique_mask);
        require(std::find(distinct_arts.begin(), distinct_arts.end(), handle) == distinct_arts.end(),
                "Distinct contact art creation returned a duplicate live handle");
        distinct_arts.push_back(handle);
    }
    auto distinct_frame = baseline;
    distinct_frame.contact_shadows.reserve(contact_shadow_capacity);
    for (std::size_t i = 0; i < distinct_arts.size(); ++i)
        distinct_frame.contact_shadows.push_back(contact_draw(distinct_arts[i], gradient_desc, small,
            1000u + static_cast<std::uint32_t>(i), {.5f, .5f}, .15f));
    const auto distinct_image = capture(renderer, distinct_frame, report, "contact-128-distinct-live-masks");
    const auto distinct_telemetry = renderer.frame_telemetry();
    require_identity(distinct_telemetry, contact_shadow_capacity);
    require(renderer.contact_art_status().live_arts == 3u + contact_shadow_capacity,
            "128 distinct live contact arts were not all retained through rendering");
    auto repeated_frame = baseline;
    repeated_frame.contact_shadows.reserve(contact_shadow_capacity);
    for (std::size_t i = 0; i < contact_shadow_capacity; ++i)
        repeated_frame.contact_shadows.push_back(contact_draw(distinct_arts.front(), gradient_desc, small,
            1200u + static_cast<std::uint32_t>(i), {.5f, .5f}, .15f));
    const auto repeated_image = capture(renderer, repeated_frame, report, "contact-128-repeated-value1-mask");
    const auto repeated_telemetry = renderer.frame_telemetry();
    require_identity(repeated_telemetry, contact_shadow_capacity);
    const auto distinct_baseline_pixel = logical_pixel(baseline_image, points.flat);
    const auto repeated_value1_pixel = logical_pixel(repeated_image, points.flat);
    const auto distinct_values_pixel = logical_pixel(distinct_image, points.flat);
    require(brightness(distinct_values_pixel) + 30 < brightness(distinct_baseline_pixel) &&
            brightness(distinct_values_pixel) + 30 < brightness(repeated_value1_pixel),
            "Distinct mask contents did not materially darken beyond baseline and the matched repeated-value1 control");
    report.pass("created_128_unique_live_art_handles");
    report.pass("rendered_128_distinct_interior_masks", {{"telemetry", telemetry_json(distinct_telemetry)},
        {"baseline_pixel", pixel_json(distinct_baseline_pixel)},
        {"repeated_value1_pixel", pixel_json(repeated_value1_pixel)},
        {"distinct_values_1_to_128_pixel", pixel_json(distinct_values_pixel)}});
    report.pass("matched_128_draw_repeated_value1_control", telemetry_json(repeated_telemetry));
    report.pass("distinct_mask_content_materially_darker_than_controls");

    const auto before_resize = capture(renderer, flat_contact, report, "resize-A-before");
    int original_w{}, original_h{};
    require(SDL_GetWindowSize(window.value.get(), &original_w, &original_h), "Could not read initial window size");
    require(SDL_MinimizeWindow(window.value.get()), "Could not minimize probe window");
    require(SDL_SyncWindow(window.value.get()), "Could not synchronize minimized probe window"); pump_events();
    renderer.render(flat_contact);
    require(!renderer.frame_telemetry().submitted, "Minimized no-capture frame unexpectedly submitted");
    require(SDL_RestoreWindow(window.value.get()), "Could not restore probe window");
    require(SDL_SetWindowSize(window.value.get(), 800, 450), "Could not request letterbox resize");
    require(SDL_SyncWindow(window.value.get()), "Could not synchronize letterbox resize"); pump_events();
    const auto resized = capture(renderer, flat_contact, report, "resize-B-letterbox");
    const auto resized_display = renderer.display_info().drawable;
    require(resized.width == static_cast<int>(resized_display.width) &&
            resized.height == static_cast<int>(resized_display.height),
            "Resized PNG dimensions do not exactly match SDL drawable dimensions");
    require(SDL_SetWindowSize(window.value.get(), original_w, original_h), "Could not restore original window size");
    require(SDL_SyncWindow(window.value.get()), "Could not synchronize original window restore"); pump_events();
    const auto after_resize = capture(renderer, flat_contact, report, "resize-A-after");
    require(before_resize.width == after_resize.width && before_resize.height == after_resize.height &&
            before_resize.rgba == after_resize.rgba, "A-B-A resize did not restore byte-exact pixels");
    require(logical_pixel(resized, points.ui) == logical_pixel(before_resize, points.ui),
            "Letterbox/resize changed UI logical pixel");
    require(brightness(logical_pixel(resized, points.flat)) <
            brightness(logical_pixel(baseline_image, points.flat)), "Letterbox/resize lost contact darkening");
    report.pass("minimized_frame_not_submitted");
    report.pass("resized_capture_matches_SDL_drawable", {{"captured", {resized.width, resized.height}},
        {"drawable", {resized_display.width, resized_display.height}}});
    report.pass("resize_A_B_A_byte_exact");
    report.pass("letterbox_contact_and_ui_stable");
    report.pass("minimize_restore_letterbox_resize_summary", {{"A", {before_resize.width, before_resize.height}},
        {"B", {resized.width, resized.height}}, {"telemetry", telemetry_json(renderer.frame_telemetry())}});

    return {{"device", device_json(renderer.device_info())}, {"art_status", art_status_json(renderer.contact_art_status())},
            {"baseline_telemetry", telemetry_json(baseline_telemetry)},
            {"contact_telemetry", telemetry_json(flat_telemetry)},
            {"samples", {{"flat_baseline", pixel_json(base_flat)}, {"flat_contact", pixel_json(dark_flat)}}}};
}

void run_zero_contact_compatibility(const std::filesystem::path& assets, Reporter& report) {
    const std::array<std::uint8_t, 4> albedo_bytes{184, 171, 148, 255};
    Image forward_image, contact_image;
    std::filesystem::path forward_path, contact_path;
    {
        Window window("GroundForward zero-contact compatibility");
        Renderer2D renderer(window.value.get(), assets, forward_options());
        const auto albedo = renderer.create_sampled_texture({{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo_bytes);
        ScenePoints points; auto frame = make_scene(albedo, false, points);
        forward_path = report.fresh("zero-contact-ground-forward"); renderer.render(frame, forward_path);
        forward_image = load_png(forward_path);
    }
    {
        Window window("GroundContact zero-contact compatibility");
        Renderer2D renderer(window.value.get(), assets, contact_options());
        const auto albedo = renderer.create_sampled_texture({{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo_bytes);
        ScenePoints points; auto frame = make_scene(albedo, true, points, true);
        contact_path = report.fresh("zero-contact-ground-contact"); renderer.render(frame, contact_path);
        contact_image = load_png(contact_path);
    }
    require(forward_image.rgba == contact_image.rgba && read_bytes(forward_path) == read_bytes(contact_path),
            "Zero-contact GroundForward and GroundContact captures are not byte-exact");
    report.pass("zero_contact_groundforward_groundcontact_byte_exact");
    report.pass("zero_contact_ordinary_receiver_compatibility");
}

void run_validation_and_lifetime(const std::filesystem::path& assets, Reporter& report) {
    Window window("Contact stale-generation validation");
    Renderer2D renderer(window.value.get(), assets, contact_options());
    const std::array<std::uint8_t, 4> albedo_bytes{184, 171, 148, 255};
    const auto albedo = renderer.create_sampled_texture({{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo_bytes);
    const ContactArtDesc desc{{8, 8, 0, 0, 8, 8}, ContactFacing::Omnidirectional};
    const auto bytes = gradient_mask();
    const auto art_a = renderer.create_contact_art(desc, bytes);
    ScenePoints points; auto old_frame = make_scene(albedo, true, points);
    const ReceiverSpec flat{{-1.f, 1.f, 0.f}};
    old_frame.contact_shadows.push_back(contact_draw(art_a, desc, flat, 400));
    renderer.release_contact_art(art_a);
    const auto art_b = renderer.create_contact_art(desc, bytes);
    require(art_b.slot == art_a.slot && art_b.generation == art_a.generation + 1,
            "Released contact art slot did not reuse with a fresh generation");
    require(!renderer.has_submitted_frames(), "Stale-handle renderer submitted before the stale-frame check");
    const auto stale = expect_throw<std::invalid_argument>([&] { renderer.render(old_frame); },
        "Stale pending contact frame was accepted");
    require(!renderer.has_submitted_frames() && !renderer.frame_telemetry().submitted,
            "Stale pending frame reached GPU submission");
    auto mixed = old_frame;
    mixed.contact_shadows.push_back(contact_draw(art_b, desc, flat, 401));
    const auto mixed_error = expect_throw<std::invalid_argument>([&] { renderer.render(mixed); },
        "Mixed stale and live contacts were not atomically rejected");
    require(!renderer.has_submitted_frames(), "Mixed stale/live frame partially submitted");

    auto valid = make_scene(albedo, true, points);
    valid.contact_shadows.push_back(contact_draw(art_b, desc, flat, 402));
    auto mismatched_uv = valid; mismatched_uv.contact_shadows[0].surface.uv_region[0] += .01f;
    const auto uv_error = expect_throw<std::invalid_argument>([&] { renderer.render(mismatched_uv); },
        "Mismatched authoritative contact UV was accepted");
    auto bad_projection = valid; bad_projection.contact_projection->zoom = 0.f;
    const auto projection_error = expect_throw<std::invalid_argument>([&] { renderer.render(bad_projection); },
        "Invalid contact projection was accepted");
    auto overflow = make_scene(albedo, true, points);
    for (std::uint32_t i = 0; i < 129; ++i)
        overflow.contact_shadows.push_back(contact_draw(art_b, desc, flat, 500u + i));
    const auto overflow_error = expect_throw<std::invalid_argument>([&] { renderer.render(overflow); },
        "129th contact caster was not rejected");

    const ContactArtDesc north_desc{{8, 8, 0, 0, 8, 8}, ContactFacing::North};
    const auto north = renderer.create_contact_art(north_desc, bytes);
    auto facing = make_scene(albedo, true, points);
    auto facing_draw = contact_draw(north, north_desc, flat, 700);
    facing_draw.facing = ContactFacing::South;
    facing.contact_shadows.push_back(facing_draw);
    const auto facing_error = expect_throw<std::invalid_argument>([&] { renderer.render(facing); },
        "Mismatched authoritative contact facing was accepted");
    require(!renderer.has_submitted_frames(), "Validation-negative renderer submitted a frame");
    report.pass("contact_art_generation_reused_without_ABA", {{"old", {art_a.slot, art_a.generation}},
        {"fresh", {art_b.slot, art_b.generation}}});
    report.pass("stale_frame_rejected_before_first_submission", {{"diagnostic", stale}});
    report.pass("mixed_stale_live_frame_atomically_rejected", {{"diagnostic", mixed_error}});
    report.pass("mismatched_authoritative_uv_rejected", {{"diagnostic", uv_error}});
    report.pass("mismatched_authoritative_facing_rejected", {{"diagnostic", facing_error}});
    report.pass("bad_canonical_projection_rejected", {{"diagnostic", projection_error}});
    report.pass("contact_capacity_129_rejected", {{"diagnostic", overflow_error}});
    report.pass("all_validation_negatives_pre_submission");
}

void run_faults(const std::filesystem::path& assets, Reporter& report) {
    const ContactArtDesc desc{{8, 8, 0, 0, 8, 8}, ContactFacing::Omnidirectional};
    const auto bytes = gradient_mask();
    Json faults = Json::array();
    for (const auto kind : {std::string_view("creation"), std::string_view("upload")}) {
        Window window(std::string("Contact ") + std::string(kind) + " fault");
        auto options = contact_options();
        if (kind == "creation") options.test_fail_contact_art_creation_at = 2;
        else options.test_fail_contact_art_upload_at = 2;
        Renderer2D renderer(window.value.get(), assets, options);
        const std::array<std::uint8_t, 4> albedo_bytes{184, 171, 148, 255};
        const auto albedo = renderer.create_sampled_texture(
            {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo_bytes);
        const auto sentinel = renderer.create_contact_art(desc, bytes);
        ScenePoints points;
        auto baseline = make_scene(albedo, true, points);
        const ReceiverSpec flat{{-1.f, 1.f, 0.f}};
        auto sentinel_frame = baseline;
        sentinel_frame.contact_shadows.push_back(contact_draw(sentinel, desc, flat, 800));
        const auto baseline_image = capture(renderer, baseline, report,
            std::string("fault-") + std::string(kind) + "-sentinel-baseline");
        const auto sentinel_before = capture(renderer, sentinel_frame, report,
            std::string("fault-") + std::string(kind) + "-sentinel-before");
        require(brightness(logical_pixel(sentinel_before, points.flat)) + 12 <
                brightness(logical_pixel(baseline_image, points.flat)),
                "Fault sentinel art was not uploaded and rendered before candidate failure");
        const auto before = renderer.contact_art_status();
        const auto diagnostic = expect_throw<std::runtime_error>([&] { (void)renderer.create_contact_art(desc, bytes); },
            "Injected contact art fault did not fail");
        const auto after = renderer.contact_art_status();
        require(after.live_arts == before.live_arts && after.logical_bytes == before.logical_bytes &&
                after.creation_attempts == before.creation_attempts + 1,
                "Injected contact art creation/upload fault mutated publication state");
        const auto sentinel_after = capture(renderer, sentinel_frame, report,
            std::string("fault-") + std::string(kind) + "-sentinel-after");
        require(sentinel_after.rgba == sentinel_before.rgba,
                "Failed contact art candidate changed already-published sentinel pixels");
        faults.push_back({{"kind", kind}, {"diagnostic", diagnostic}, {"before", art_status_json(before)},
                          {"after", art_status_json(after)}, {"sentinel_pixels_unchanged", true}});
    }
    {
        Window window("Contact idle-wait fault");
        auto options = contact_options(); options.test_fail_contact_art_idle_waits = 1;
        Renderer2D renderer(window.value.get(), assets, options);
        const auto art = renderer.create_contact_art(desc, bytes);
        const auto before = renderer.contact_art_status();
        const auto diagnostic = expect_throw<std::runtime_error>([&] { renderer.release_contact_art(art); },
            "Injected contact art idle wait did not fail");
        require(renderer.contact_art_status().live_arts == before.live_arts &&
                renderer.contact_art_status().logical_bytes == before.logical_bytes &&
                renderer.contact_art_status().creation_attempts == before.creation_attempts,
                "Injected idle-wait fault mutated contact publication state");
        renderer.release_contact_art(art);
        require(renderer.contact_art_status().live_arts == 0 && renderer.contact_art_status().logical_bytes == 0,
                "Contact art did not release after the one-shot idle fault");
        faults.push_back({{"kind", "idle_wait"}, {"diagnostic", diagnostic},
                          {"before", art_status_json(before)}, {"after_retry", art_status_json(renderer.contact_art_status())}});
    }
    report.pass("art_creation_fault_transactional", faults[0]);
    report.pass("art_pre_submit_upload_fault_transactional", faults[1]);
    report.pass("art_idle_wait_fault_transactional", faults[2]);
    report.pass("art_idle_wait_retry_releases");
}

void run_registry_budget(const std::filesystem::path& assets, Reporter& report) {
    Window window("Contact art registry budget");
    Renderer2D renderer(window.value.get(), assets, contact_options());
    constexpr std::uint32_t dimension = 256;
    ContactArtDesc desc{{dimension, dimension, 0, 0, dimension, dimension}, ContactFacing::Omnidirectional};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(dimension) * dimension);
    for (std::uint32_t y = 1; y + 1 < dimension; ++y)
        std::fill(bytes.begin() + static_cast<std::size_t>(y) * dimension + 1,
                  bytes.begin() + static_cast<std::size_t>(y + 1) * dimension - 1, 128u);
    std::vector<ContactArtHandle> handles;
    handles.reserve(contact_art_capacity);
    for (std::size_t i = 0; i < contact_art_capacity; ++i)
        handles.push_back(renderer.create_contact_art(desc, bytes));
    const auto full = renderer.contact_art_status();
    require(full.live_arts == contact_art_capacity && full.logical_bytes == 16u * 1024u * 1024u,
            "Contact registry did not reach its exact 256-art/16-MiB logical boundary");
    const auto diagnostic = expect_throw<std::length_error>([&] { (void)renderer.create_contact_art(desc, bytes); },
        "Contact registry accepted a 257th art beyond 16 MiB");
    require(renderer.contact_art_status().live_arts == full.live_arts &&
            renderer.contact_art_status().logical_bytes == full.logical_bytes &&
            renderer.contact_art_status().creation_attempts == full.creation_attempts,
            "Rejected registry overflow changed its live budget or attempt ledger");
    const auto released = handles.back();
    renderer.release_contact_art(released);
    const auto replacement = renderer.create_contact_art(desc, bytes);
    require(replacement.slot == released.slot && replacement.generation == released.generation + 1,
            "Full registry did not safely reuse a released generation");
    report.pass("registry_accepts_exactly_256_live_arts");
    report.pass("registry_accepts_exactly_16MiB_logical_bytes", art_status_json(full));
    report.pass("registry_rejects_257th_art_without_mutation", {{"diagnostic", diagnostic}});
    report.pass("registry_reuses_released_generation", {{"released", {released.slot, released.generation}},
        {"replacement", {replacement.slot, replacement.generation}}});
    report.pass("registry_boundary_summary", {{"full", art_status_json(full)},
        {"diagnostic", diagnostic}, {"released", {released.slot, released.generation}},
        {"replacement", {replacement.slot, replacement.generation}}});
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path output;
    std::unique_ptr<Reporter> report;
    try {
        require(argc == 3, "Usage: ContactShadowProbe <assets-dir> <NEW-output-dir>");
        const auto assets = from_utf8(argv[1]); output = from_utf8(argv[2]);
        require(std::filesystem::is_directory(assets), "Assets directory does not exist");
        require(!std::filesystem::exists(output), "Output directory already exists; refusing to reuse retained evidence");
        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        require(std::filesystem::create_directories(output), "Output directory was concurrently created");
        report = std::make_unique<Reporter>(); report->output = output;
        report->checkpoint("initialized");

        Window window("Ribterra contact shadow native diagnostic", 640, 480);
        const auto production = run_production(window, assets, *report);
        const auto device = production.at("device");
        report->checkpoint("production-captures-complete", device);
        run_zero_contact_compatibility(assets, *report);
        run_validation_and_lifetime(assets, *report);
        report->checkpoint("validation-and-compatibility-complete", device);
        run_faults(assets, *report);
        run_registry_budget(assets, *report);

        std::uintmax_t captured_bytes{};
        for (const auto& item : report->outputs) {
            const auto name = item.get<std::string>();
            if (std::filesystem::path(name).extension() == ".png")
                captured_bytes += std::filesystem::file_size(output / name);
        }
        require(captured_bytes < 10u * 1024u * 1024u, "Retained PNG captures exceeded the bounded 10 MiB diagnostic budget");
        report->pass("retained_png_budget_under_10MiB", {{"bytes", captured_bytes}});

        const auto final_path = report->fresh("contact-shadow-report", ".json");
        const Json final{{"schema", 1}, {"complete", true}, {"build", RIBTERRA_PROBE_BUILD},
            {"platform", SDL_GetPlatform()}, {"device", device}, {"production", production},
            {"checks", report->checks}, {"check_count", report->checks.size()}, {"outputs", report->outputs},
            {"claims", {"native D3D12 ContactSlice routing", "ambient-only contact attenuation",
                         "typed receiver and plane exclusions", "two-slot and resize stability",
                         "stale generation and transactional art registry behavior"}},
            {"not_claimed", {"raw GBuffer W readback", "desktop scan-out", "artist or concept fidelity",
                             "representative performance", "GPU execution time", "FPS"}}};
        write_json_new(final_path, final);
        std::cout << "Contact shadow native diagnostics passed; retained outputs: " << utf8(output) << '\n';
        return 0;
    } catch (const std::exception& error) {
        if (report && !output.empty() && std::filesystem::is_directory(output)) {
            try {
                const auto failure_path = report->fresh("contact-shadow-failure", ".json");
                write_json_new(failure_path, {{"schema", 1}, {"complete", false}, {"build", RIBTERRA_PROBE_BUILD},
                    {"error", error.what()}, {"checks", report->checks}, {"outputs", report->outputs}});
            } catch (...) {}
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}

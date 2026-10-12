#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"

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
#include <utility>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace {
using namespace ribterra;
using namespace ribterra::render;
using Json = nlohmann::json;

struct Pixel { int r{}, g{}, b{}, a{}; };

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    require(!std::filesystem::exists(path), "Fixture writer refuses to overwrite a file");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not create fixture file");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    stream.close();
    require(stream.good(), "Could not finish fixture file");
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    write_bytes(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::string digest(std::span<const std::uint8_t> bytes) { return sha256(bytes); }
std::string digest(std::string_view text) {
    return digest(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

struct ManifestRow {
    std::string id;
    std::string path;
    std::string hash;
    std::uint64_t bytes{};
};

void write_manifest(const std::filesystem::path& root, const std::vector<ManifestRow>& rows) {
    std::string canonical;
    Json assets = Json::array();
    for (const auto& row : rows) {
        assets.push_back({{"id", row.id}, {"path", row.path}, {"kind", "data"},
                          {"sha256", row.hash}, {"license", "LicenseRef-Test"},
                          {"source", "fixture"}, {"author", "Ribterra tests"},
                          {"notice", "NOTICE.txt"}, {"bytes", row.bytes}});
    }
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    for (const auto& row : sorted) {
        for (const auto& field : std::array<std::string, 8>{
                 row.id, row.path, "data", row.hash, "LicenseRef-Test", "fixture",
                 "Ribterra tests", "NOTICE.txt"})
            canonical += std::to_string(field.size()) + ":" + field;
        canonical += '\n';
    }
    const Json manifest{{"schema", 1}, {"content_sha256", digest(canonical)},
                        {"assets", std::move(assets)}};
    write_text(root / "manifest.json", manifest.dump(2) + "\n");
}

enum class FixtureMutation {
    Valid,
    ValidOccluders,
    ValidCapsule,
    BadCapsuleAxes,
    BadCapsuleLength,
    TinyOccluderHeight,
    TinyOccluderExtent,
    TinyCapsuleRadius,
    BadIdentity,
    MissingChannel,
    WrongFormat,
    BadDepthRange,
    BadHash,
    BadRegion,
    BadOccluderShape,
    BadOccluderAxes,
    BadOccluderBase,
    BadOccluderNonFinite
};

std::filesystem::path make_fixture(const std::filesystem::path& output, std::string_view name,
                                   FixtureMutation mutation) {
    const auto root = output / std::string(name);
    require(!std::filesystem::exists(root), "Fixture directory already exists");
    std::filesystem::create_directories(root / "art/material");

    constexpr std::uint32_t width = 2, height = 2;
    const std::vector<std::uint8_t> albedo{
        128, 128, 128, 255, 128, 128, 128, 255,
        128, 128, 128, 255, 128, 128, 128, 255};
    const std::vector<std::uint8_t> normal{
        128, 128, 255, 255, 128, 128, 255, 255,
        128, 128, 255, 255, 128, 128, 255, 255};
    const std::vector<std::uint8_t> depth{0, 128, 0, 128, 0, 128, 0, 128};
    const std::vector<std::uint8_t> emission(4, 0);
    const std::vector<std::uint8_t> surface{
        0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const std::vector<std::uint8_t> caster(4, 255);
    struct TextureSource {
        const char* id;
        const char* filename;
        const char* format;
        const std::vector<std::uint8_t>* bytes;
    };
    const std::array sources{
        TextureSource{"albedo", "albedo.rtex", "rgba8_unorm_srgb", &albedo},
        TextureSource{"normal", "normal.rtex", "rgba8_unorm", &normal},
        TextureSource{"depth", "depth.rtex", "r16_unorm", &depth},
        TextureSource{"emission", "emission.rtex", "r8_unorm", &emission},
        TextureSource{"surface", "surface.rtex", "rgba8_unorm", &surface},
        TextureSource{"caster", "caster.rtex", "r8_unorm", &caster}};

    Json textures = Json::array();
    std::vector<ManifestRow> rows;
    for (const auto& source : sources) {
        const auto relative = std::string("art/material/") + source.filename;
        write_bytes(root / relative, *source.bytes);
        const auto hash = digest(*source.bytes);
        rows.push_back({std::string("fixture.") + source.id, relative, hash, source.bytes->size()});
        std::string format = source.format;
        if (mutation == FixtureMutation::WrongFormat && std::string_view(source.id) == "normal")
            format = "rgba8_unorm_srgb";
        textures.push_back({{"id", source.id}, {"path", relative}, {"format", format},
                            {"width", width}, {"height", height}, {"layout", "tight_le"},
                            {"mip_levels", 1}, {"bytes", source.bytes->size()},
                            {"sha256", mutation == FixtureMutation::BadHash &&
                                             std::string_view(source.id) == "albedo"
                                         ? std::string(64, '0') : hash}});
    }

    Json channels{
        {"albedo", {{"texture", "albedo"}, {"rect", {0, 0, 2, 2}}}},
        {"normal_material", {{"texture", "normal"}, {"rect", {0, 0, 2, 2}}}},
        {"camera_depth", {{"texture", "depth"}, {"rect", {0, 0, 2, 2}}}},
        {"emission_mask", {{"texture", "emission"}, {"rect", {0, 0, 2, 2}}}},
        {"surface_mask", {{"texture", "surface"}, {"rect", {0, 0, 2, 2}}}},
        {"caster_mask", {{"texture", "caster"}, {"rect", {0, 0, 2, 2}}}}};
    if (mutation == FixtureMutation::MissingChannel) channels.erase("caster_mask");
    if (mutation == FixtureMutation::BadRegion)
        channels["camera_depth"]["rect"] = {0, 0, 3, 2};

    Json material{{"id", "analytic.material"}, {"family", "DynamicSprite25D"},
                  {"alpha", "straight"}, {"camera", "ribterra-affine-v1"},
                  {"revision", mutation == FixtureMutation::BadIdentity ? 0 : 1},
                  {"source_hash", digest("analytic material source")},
                  {"channels", std::move(channels)}, {"emission", {0.f, 0.f, 0.f, 0.f}}};
    Json sprite{{"id", "analytic.sprite"}, {"material", "analytic.material"},
                {"source_size", {2.f, 2.f}}, {"pivot_px", {1.f, 1.f}},
                {"depth_range", mutation == FixtureMutation::BadDepthRange
                                      ? Json::array({1.f, 1.f}) : Json::array({-1.f, 1.f})},
                {"export_pixels_per_reference_pixel", .01f}};
    if (mutation == FixtureMutation::ValidOccluders ||
        mutation == FixtureMutation::ValidCapsule || mutation == FixtureMutation::BadCapsuleAxes ||
        mutation == FixtureMutation::BadCapsuleLength ||
        mutation == FixtureMutation::TinyOccluderHeight || mutation == FixtureMutation::TinyOccluderExtent ||
        mutation == FixtureMutation::TinyCapsuleRadius ||
        mutation == FixtureMutation::BadOccluderShape ||
        mutation == FixtureMutation::BadOccluderAxes ||
        mutation == FixtureMutation::BadOccluderBase ||
        mutation == FixtureMutation::BadOccluderNonFinite) {
        Json primitive{{"shape", mutation == FixtureMutation::BadOccluderShape ? "mesh" : "obb"},
                       {"center_xy", {1.f, 1.f}}, {"axis_x", {1.f, 0.f}},
                       {"axis_y", mutation == FixtureMutation::BadOccluderAxes
                                      ? Json::array({1.f, 0.f}) : Json::array({0.f, 1.f})},
                       {"half_extent_x", 1.f}, {"half_extent_y", 1.f},
                       {"base_z", mutation == FixtureMutation::BadOccluderBase ? 2.4f : 0.f},
                       {"top_z", 2.4f}};
        if (mutation == FixtureMutation::BadOccluderNonFinite)
            primitive["center_xy"][0] = std::numeric_limits<float>::quiet_NaN();
        if (mutation == FixtureMutation::ValidCapsule || mutation == FixtureMutation::BadCapsuleAxes ||
            mutation == FixtureMutation::BadCapsuleLength || mutation == FixtureMutation::TinyCapsuleRadius) {
            primitive = {{"shape","capsule"},{"center_xy",{1.f,1.f}},{"axis_x",{.6f,.8f}},
                         {"axis_y",mutation==FixtureMutation::BadCapsuleAxes?Json::array({0.f,1.f}):Json::array({0.f,0.f})},
                         {"half_length",mutation==FixtureMutation::BadCapsuleLength?-.1f:0.f},
                         {"radius",.25f},{"base_z",0.f},{"top_z",2.4f}};
        }
        if (mutation == FixtureMutation::TinyOccluderHeight) primitive["top_z"] = 1e-6f;
        if (mutation == FixtureMutation::TinyOccluderExtent) primitive["half_extent_x"] = 1e-6f;
        if (mutation == FixtureMutation::TinyCapsuleRadius) primitive["radius"] = 1e-6f;
        sprite["local_occluders"] = Json::array({std::move(primitive)});
    }
    const Json world{{"schema", 2}, {"pages", Json::array()}, {"textures", textures},
                     {"materials", Json::array({material})},
                     {"lit_sprites", Json::array({sprite})}};
    const auto world_text = world.dump(2) + "\n";
    write_text(root / "art/world.json", world_text);
    rows.insert(rows.begin(), {"art.world", "art/world.json", digest(world_text), world_text.size()});
    write_manifest(root, rows);
    return root;
}

std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>
load_capture(const std::filesystem::path& path) {
    auto* surface = SDL_LoadPNG(utf8(path).c_str());
    require(surface != nullptr, std::string("Could not decode renderer capture: ") + SDL_GetError());
    const auto* details = SDL_GetPixelFormatDetails(surface->format);
    require(details && details->bytes_per_pixel == 4,
            "Renderer capture is not final four-byte RGBA8 evidence");
    return {surface, SDL_DestroySurface};
}

Pixel sample(SDL_Surface* surface, SDL_Window* window, const Renderer2D& renderer, Point logical) {
    auto point = renderer.logical_to_window(logical);
    int window_width = 1, window_height = 1;
    SDL_GetWindowSize(window, &window_width, &window_height);
    point.x *= static_cast<float>(surface->w) / std::max(window_width, 1);
    point.y *= static_cast<float>(surface->h) / std::max(window_height, 1);
    Uint8 r{}, g{}, b{}, a{};
    require(SDL_ReadSurfacePixel(surface,
                                 std::clamp(static_cast<int>(point.x), 0, surface->w - 1),
                                 std::clamp(static_cast<int>(point.y), 0, surface->h - 1),
                                 &r, &g, &b, &a),
            "Capture pixel is unavailable");
    return {r, g, b, a};
}

Json pixel_json(Pixel value) { return {value.r, value.g, value.b, value.a}; }

void require_color(Pixel value, Pixel expected, int tolerance, std::string_view message) {
    const auto close = [&](int actual, int target) { return std::abs(actual - target) <= tolerance; };
    require(close(value.r, expected.r) && close(value.g, expected.g) &&
                close(value.b, expected.b) && value.a == expected.a,
            message);
}

void require_gray(Pixel value, int expected, int tolerance, std::string_view message) {
    require_color(value, {expected, expected, expected, 255}, tolerance, message);
}

Pixel capture(Renderer2D& renderer, SDL_Window* window, const Frame2D& frame,
              const std::filesystem::path& output, std::string_view name, Point point,
              Json& samples) {
    const auto path = output / (std::string(name) + ".png");
    require(!std::filesystem::exists(path), "Capture path already exists");
    renderer.render(frame, path);
    auto surface = load_capture(path);
    const auto value = sample(surface.get(), window, renderer, point);
    require(value.a == 255, "Final material capture is not opaque RGBA8");
    samples[std::string(name)] = pixel_json(value);
    return value;
}

Rect atlas_rect(std::uint32_t first, std::uint32_t count = 1) {
    return {static_cast<float>(first) / 8.f, 0.f, static_cast<float>(count) / 8.f, 1.f};
}

struct AnalyticAtlas {
    std::array<TextureHandle, 6> textures{};
    std::vector<std::uint8_t> albedo;
};

AnalyticAtlas create_analytic_atlas(Renderer2D& renderer) {
    AnalyticAtlas atlas;
    atlas.albedo = {
        128,128,128,255, 255,255,255,255, 0,0,0,255, 255,255,255,255,
        128,128,128,255, 255,255,255,255, 255,255,255,128, 0,0,0,255};
    const std::vector<std::uint8_t> normal{
        128,128,255,255, 255,128,255,0, 0,128,255,255, 128,255,255,255,
        128,0,255,255, 128,128,255,128, 128,128,255,255, 128,128,255,255};
    const std::vector<std::uint8_t> depth{
        0,0, 255,255, 0,128, 0,128, 0,128, 0,128, 0,128, 0,128};
    const std::vector<std::uint8_t> emission{0,128,255,0,0,0,0,0};
    const std::vector<std::uint8_t> surface{
        128,17,33,255, 128,17,33,255, 128,17,33,255, 128,17,33,255,
        128,17,33,255, 128,17,33,255, 128,17,33,255, 128,17,33,255};
    const std::vector<std::uint8_t> caster(8, 255);
    atlas.textures[0] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::Rgba8UnormSrgb}, atlas.albedo);
    atlas.textures[1] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::Rgba8Unorm}, normal);
    atlas.textures[2] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::R16Unorm}, depth);
    atlas.textures[3] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::R8Unorm}, emission);
    atlas.textures[4] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::Rgba8Unorm}, surface);
    atlas.textures[5] = renderer.create_sampled_texture({{8,1}, SampledTextureFormat::R8Unorm}, caster);
    return atlas;
}

SpriteMaterialDesc material_desc(const AnalyticAtlas& atlas, Rect albedo,
                                 Rect normal = atlas_rect(0), Rect depth = atlas_rect(2),
                                 Rect emission_mask = atlas_rect(0),
                                 std::array<float, 4> emission = {}) {
    SpriteMaterialDesc desc;
    desc.channels[0] = {atlas.textures[0], albedo};
    desc.channels[1] = {atlas.textures[1], normal};
    desc.channels[2] = {atlas.textures[2], depth};
    desc.channels[3] = {atlas.textures[3], emission_mask};
    desc.channels[4] = {atlas.textures[4], atlas_rect(0)};
    desc.channels[5] = {atlas.textures[5], atlas_rect(0)};
    desc.emission = emission;
    return desc;
}

SpriteSurfaceGpu analytic_surface() {
    SpriteSurfaceGpu surface;
    for (auto& region : surface.uv_regions) region = {7.f, 7.f, 7.f, 7.f};
    surface.anchor_export = {0.f, 0.f, 0.f, 1.f};
    surface.source_size_pivot = {2.f, 2.f, 1.f, 1.f};
    surface.depth_scale = {-1.f, 1.f, 1.f, 0.f};
    surface.emission = {63.f, 63.f, 63.f, 63.f};
    surface.response = {0.f, 0.f, 0.f, 0.f};
    return surface;
}

void append_material_quad(Frame2D& frame, MaterialHandle material, Rect rect,
                          SpriteSurfaceGpu surface = analytic_surface()) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    constexpr Color white{255,255,255,255};
    frame.vertices.insert(frame.vertices.end(), {
        {{rect.x,rect.y},{0,0},white}, {{rect.x+rect.w,rect.y},{1,0},white},
        {{rect.x+rect.w,rect.y+rect.h},{1,1},white}, {{rect.x,rect.y+rect.h},{0,1},white}});
    frame.indices.insert(frame.indices.end(), {first_vertex,first_vertex+1,first_vertex+2,
                                               first_vertex,first_vertex+2,first_vertex+3});
    frame.sprite_material_draws.push_back({material, surface});
    GeometryDraw draw{Pass::WorldOrdered, Blend::Alpha, {}, first_index, 6};
    draw.sprite_material_index = static_cast<std::uint32_t>(frame.sprite_material_draws.size());
    frame.commands.emplace_back(draw);
}

void append_ui_quad(Frame2D& frame, Rect rect, Color color) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    frame.vertices.insert(frame.vertices.end(), {
        {{rect.x,rect.y},{},color}, {{rect.x+rect.w,rect.y},{},color},
        {{rect.x+rect.w,rect.y+rect.h},{},color}, {{rect.x,rect.y+rect.h},{},color}});
    frame.indices.insert(frame.indices.end(), {first_vertex,first_vertex+1,first_vertex+2,
                                               first_vertex,first_vertex+2,first_vertex+3});
    frame.commands.emplace_back(GeometryDraw{Pass::Ui, Blend::Opaque, {}, first_index, 6});
}

Frame2D material_frame() {
    Frame2D frame;
    frame.logical_size = {640,480};
    frame.clear = {0,0,0,255};
    frame.linear_hdr_world = true;
    frame.surface_lighting.ambient = {1.f,1.f,1.f,0.f};
    frame.surface_lighting.sun_color_intensity = {0.f,0.f,0.f,0.f};
    return frame;
}

template<class Operation>
std::string require_rejected(Operation operation, std::string_view expected) {
    try {
        operation();
    } catch (const std::exception& error) {
        require(std::string_view(error.what()) == expected, "Rejection diagnostic changed or wrong failure fired");
        return error.what();
    }
    throw std::runtime_error("Expected operation was accepted");
}

void settle_window(SDL_Window* window) {
    require(SDL_ShowWindow(window), SDL_GetError());
    for (int attempt=0; attempt<100; ++attempt) {
        SDL_PumpEvents();
        int width{}, height{};
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width>0 && height>0 && !(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)) return;
        SDL_Delay(10);
    }
    throw std::runtime_error("Native material probe window did not become drawable");
}

Json schema_transaction_contract(Renderer2D&, const std::filesystem::path& output,
                                 GameArt& art) {
    const auto valid = make_fixture(output, "fixture-valid", FixtureMutation::Valid);
    AssetCatalog valid_assets(valid);
    art.load(valid_assets);
    require(art.contains_lit("analytic.sprite"), "Valid schema-2 lit sprite was not installed");
    require(!art.ready(), "Lit-only catalog must not advertise legacy GameView readiness");
    require(!art.contains("analytic.sprite"), "Lit-only sprite must not pass a legacy contains guard");
    Frame2D compatibility_frame;
    require(art.append_lit_occluders(compatibility_frame, "analytic.sprite", {0.f, 0.f, 0.f},
                                     1.f, 1u, 1u) == 0 &&
                compatibility_frame.local_occluders.empty(),
            "Metadata-absent schema-2 sprite lost optional compatibility");

    const auto valid_occluders = make_fixture(
        output, "fixture-valid-occluders", FixtureMutation::ValidOccluders);
    AssetCatalog valid_occluder_assets(valid_occluders);
    art.load(valid_occluder_assets);
    Frame2D scaled;
    require(art.append_lit_occluders(scaled, "analytic.sprite", {10.f, 20.f, 3.f},
                                     2.f, 7u, 41u) == 1,
            "Authored local OBB was not appended");
    const auto &obb = scaled.local_occluders.front();
    require(obb.center_base == std::array<float, 4>{12.f, 22.f, 0.f, 3.f} &&
                obb.axis_x_extent == std::array<float, 4>{1.f, 0.f, 2.f, 7.8f} &&
                obb.axis_y_radius == std::array<float, 4>{0.f, 1.f, 2.f, 0.f} &&
                obb.shape_flags_id == std::array<std::uint32_t, 4>{1u, 1u, 41u, 7u},
            "Authored local OBB did not preserve anchor, world scale or runtime IDs");
    Frame2D hidden;
    require(art.append_lit_occluders(hidden, "analytic.sprite", {10.f, 20.f, 3.f},
                                     2.f, 7u, 41u, .999f) == 0 &&
                hidden.local_occluders.empty(),
            "Partially revealed instance uploaded a blocker");
    const auto duplicate_size = scaled.local_occluders.size();
    require_rejected([&] { art.append_lit_occluders(
                         scaled, "analytic.sprite", {0.f, 0.f, 0.f}, 1.f, 8u, 41u); },
                     "Duplicate local occluder stable ID");
    require(scaled.local_occluders.size() == duplicate_size,
            "Duplicate stable-ID rejection mutated the frame");
    require_rejected([&] { Frame2D invalid; art.append_lit_occluders(
                         invalid, "analytic.sprite", {0.f, 0.f, 0.f}, 1.f, 0u, 1u); },
                     "Lit occluders require finite anchor/positive scale, owner and stable ID");
    Frame2D full;
    full.local_occluders.resize(local_occluder_capacity);
    require_rejected([&] { art.append_lit_occluders(
                         full, "analytic.sprite", {0.f, 0.f, 0.f}, 1.f, 1u, 1u); },
                     "Lit occluder capacity or stable ID range overflow");
    require(art.append_lit_occluders(full,"analytic.sprite",{0,0,0},1.f,0u,0u,.5f)==0 &&
            full.local_occluders.size()==local_occluder_capacity,
            "Hidden geometry consumed a full budget or required unused runtime IDs");
    require(art.append_lit_occluders(scaled,"analytic.sprite",{0,0,0},1.f,7u,41u,.5f)==0 &&
            scaled.local_occluders.size()==duplicate_size,"Hidden geometry triggered duplicate-ID failure");
    const auto capsule_fixture=make_fixture(output,"fixture-valid-capsule",FixtureMutation::ValidCapsule);
    art.load(AssetCatalog(capsule_fixture));
    Frame2D capsule;
    require(art.append_lit_occluders(capsule,"analytic.sprite",{10,20,3},2.f,7u,42u)==1,
            "Authored zero-half-length capsule was not accepted");
    const auto& capsule_row=capsule.local_occluders.front();
    require(capsule_row.center_base==std::array<float,4>{12,22,0,3} &&
            capsule_row.axis_x_extent==std::array<float,4>{.6f,.8f,0,7.8f} &&
            capsule_row.axis_y_radius==std::array<float,4>{0,0,0,.5f} &&
            capsule_row.shape_flags_id==std::array<std::uint32_t,4>{2,1,42,7},
            "Authored capsule packing disagrees with the production GPU ABI");
    art.load(AssetCatalog(valid_occluders));
    Frame2D tiny_scaled;
    require_rejected([&]{art.append_lit_occluders(tiny_scaled,"analytic.sprite",{0,0,0},1e-6f,7u,1u);},
                     "Lit occluder transformed dimensions must be at least 0.0001");
    require(tiny_scaled.local_occluders.empty(), "Sub-contract scale partially mutated the frame");
    Frame2D receiver = material_frame();
    art.lit_sprite(receiver, "analytic.sprite", {0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 1.f, 0.f, 7u);
    require(receiver.sprite_material_draws.back().receiver_owner == 7u,
            "Lit sprite receiver owner was not forwarded");
    Json results = Json::array();
    const std::array cases{
        std::pair{"bad-identity", FixtureMutation::BadIdentity},
        std::pair{"missing-channel", FixtureMutation::MissingChannel},
        std::pair{"wrong-format", FixtureMutation::WrongFormat},
        std::pair{"bad-depth-range", FixtureMutation::BadDepthRange},
        std::pair{"bad-hash", FixtureMutation::BadHash},
        std::pair{"bad-region", FixtureMutation::BadRegion},
        std::pair{"bad-occluder-shape", FixtureMutation::BadOccluderShape},
        std::pair{"bad-occluder-axes", FixtureMutation::BadOccluderAxes},
        std::pair{"bad-occluder-base", FixtureMutation::BadOccluderBase},
        std::pair{"bad-occluder-nonfinite", FixtureMutation::BadOccluderNonFinite},
        std::pair{"bad-capsule-axis",FixtureMutation::BadCapsuleAxes},
        std::pair{"bad-capsule-length",FixtureMutation::BadCapsuleLength},
        std::pair{"tiny-occluder-height",FixtureMutation::TinyOccluderHeight},
        std::pair{"tiny-occluder-extent",FixtureMutation::TinyOccluderExtent},
        std::pair{"tiny-capsule-radius",FixtureMutation::TinyCapsuleRadius}};
    for (const auto& [name, mutation] : cases) {
        const auto root = make_fixture(output, std::string("fixture-") + name, mutation);
        AssetCatalog candidate(root);
        std::string diagnostic;
        try {
            art.load(candidate);
        } catch (const std::exception& error) {
            diagnostic = error.what();
        }
        require(!diagnostic.empty(), "Invalid schema-2 candidate was accepted");
        require(art.contains_lit("analytic.sprite"),
                "Rejected schema-2 candidate replaced the previous valid art catalog");
        Frame2D retained;
        require(art.append_lit_occluders(retained, "analytic.sprite", {0.f, 0.f, 0.f},
                                         1.f, 9u, 101u) == 1,
                "Rejected schema-2 candidate replaced valid authored occluder metadata");
        results.push_back({{"case", name}, {"rejected", true}, {"diagnostic", diagnostic},
                           {"old_catalog_valid", true}});
    }
    return {{"valid_fixture", utf8(valid)},
            {"valid_occluder_fixture", utf8(valid_occluders)},
            {"optional_metadata_compatibility", true},
            {"scaled_obb_api", true}, {"scaled_capsule_api",true}, {"partial_reveal_excluded", true},
            {"hidden_before_budget_and_ids",true},
            {"runtime_id_guards", true}, {"receiver_owner_forwarded", true},
            {"negative_candidates", std::move(results)}};
}

Json direct_renderer_contract(Renderer2D& renderer, SDL_Window* window,
                              const std::filesystem::path& output, Json& samples) {
    const auto atlas = create_analytic_atlas(renderer);
    auto gray_desc = material_desc(atlas, atlas_rect(0));
    const auto gray = renderer.create_sprite_material(gray_desc);
    gray_desc.channels[0].uv = atlas_rect(7);
    gray_desc.emission = {64.f,64.f,64.f,64.f};
    auto frame = material_frame();
    append_material_quad(frame, gray, {200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-srgb-gray128",{320,240},samples),
                 128,3,"SRGB gray128 did not receive exactly one decode/final encode");
    require_gray(capture(renderer,window,frame,output,"material-albedo-edge-no-bleed",{439,240},samples),
                 128,3,"Albedo atlas edge sampled the adjacent material texel");

    const auto black_white = renderer.create_sprite_material(material_desc(atlas,atlas_rect(2,2)));
    frame = material_frame(); append_material_quad(frame,black_white,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-srgb-black-white-midpoint",{320,240},samples),
                 188,3,"SRGB black/white midpoint was not filtered in linear light");

    const auto gray_white = renderer.create_sprite_material(material_desc(atlas,atlas_rect(4,2)));
    frame = material_frame(); append_material_quad(frame,gray_white,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-srgb-gray-white-midpoint",{320,240},samples),
                 205,3,"SRGB gray/white midpoint did not distinguish decode-before-filter");

    const auto ao_half = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(3),atlas_rect(5)));
    frame = material_frame(); append_material_quad(frame,ao_half,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-linear-ao128",{320,240},samples),
                 188,4,"Linear normal/material AO was gamma-decoded or ignored");
    const auto ao_edge = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(3),atlas_rect(0)));
    frame = material_frame(); append_material_quad(frame,ao_edge,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-normal-ao-edge-no-bleed",{439,240},samples),
                 255,3,"Normal/material atlas edge sampled the adjacent AO texel");

    const auto mask_half = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(7),atlas_rect(0),atlas_rect(2),atlas_rect(1),
                      {1.f,1.f,1.f,1.f}));
    frame = material_frame(); append_material_quad(frame,mask_half,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-linear-emission-mask128",{320,240},samples),
                 188,4,"Linear R8 emission mask was treated as encoded color data");
    require_gray(capture(renderer,window,frame,output,"material-emission-edge-no-bleed",{439,240},samples),
                 188,4,"Emission-mask atlas edge sampled the adjacent material texel");

    const auto alpha_half = renderer.create_sprite_material(material_desc(atlas,atlas_rect(6)));
    frame = material_frame(); append_material_quad(frame,alpha_half,{200,140,240,200});
    require_gray(capture(renderer,window,frame,output,"material-straight-alpha128",{320,240},samples),
                 188,4,"Straight material alpha coverage blended outside linear HDR");

    const auto emission_off = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(7),atlas_rect(0),atlas_rect(2),atlas_rect(2),{}));
    const auto emission_on = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(7),atlas_rect(0),atlas_rect(2),atlas_rect(2),
                      {1.f,1.f,1.f,.5f}));
    frame = material_frame(); append_material_quad(frame,emission_off,{200,140,240,200});
    const auto off = capture(renderer,window,frame,output,"material-emission-off",{320,240},samples);
    frame = material_frame(); append_material_quad(frame,emission_on,{200,140,240,200});
    const auto on = capture(renderer,window,frame,output,"material-emission-on",{320,240},samples);
    require(off.r < 4 && on.r > 180 && on.g > 180 && on.b > 180,
            "Material emission toggle changed no visible radiance over constant albedo");

    const auto normal_px = renderer.create_sprite_material(material_desc(atlas,atlas_rect(3),atlas_rect(1)));
    const auto normal_nx = renderer.create_sprite_material(material_desc(atlas,atlas_rect(3),atlas_rect(2)));
    const auto normal_py = renderer.create_sprite_material(material_desc(atlas,atlas_rect(3),atlas_rect(3)));
    const auto normal_ny = renderer.create_sprite_material(material_desc(atlas,atlas_rect(3),atlas_rect(4)));
    const auto directional = [&](MaterialHandle material, std::array<float,4> direction,
                                 std::string_view name) {
        auto directional_frame = material_frame();
        directional_frame.surface_lighting.ambient = {0,0,0,0};
        directional_frame.surface_lighting.sun_direction = direction;
        directional_frame.surface_lighting.sun_color_intensity = {1,1,1,1};
        append_material_quad(directional_frame,material,{200,140,240,200});
        return capture(renderer,window,directional_frame,output,name,{320,240},samples);
    };
    const auto px_lit=directional(normal_px,{1,0,0,0},"material-normal-plus-x-lit");
    const auto nx_dark=directional(normal_nx,{1,0,0,0},"material-normal-minus-x-dark");
    const auto py_lit=directional(normal_py,{0,1,0,0},"material-normal-plus-y-lit");
    const auto ny_dark=directional(normal_ny,{0,1,0,0},"material-normal-minus-y-dark");
    require(px_lit.r>220 && nx_dark.r<12 && py_lit.r>220 && ny_dark.r<12,
            "Octahedral X/Y normals did not flip directional-sun receiving panels");

    const auto depth_near = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(3),atlas_rect(0),atlas_rect(0)));
    const auto depth_far = renderer.create_sprite_material(
        material_desc(atlas,atlas_rect(3),atlas_rect(0),atlas_rect(1)));
    const auto depth_sample = [&](MaterialHandle material, std::string_view name) {
        auto depth_frame=material_frame();
        depth_frame.surface_lighting.ambient={0,0,0,0};
        depth_frame.surface_lighting.counts[0]=1;
        auto& light=depth_frame.surface_lighting.lights[0];
        light.anchor_radius={-.57735f,-.57735f,1.f,4.f};
        light.direction_cone={0,0,-1,-1};
        light.color_intensity={1,1,1,1};
        light.height_falloff_reveal={-1,2,1,0};
        append_material_quad(depth_frame,material,{200,140,240,200});
        return capture(renderer,window,depth_frame,output,name,{320,240},samples);
    };
    const auto near_pixel=depth_sample(depth_near,"material-depth-near-light");
    const auto far_pixel=depth_sample(depth_far,"material-depth-far-light");
    require(near_pixel.r > far_pixel.r + 45,
            "R16 camera depth did not causally change reconstructed point-light response");

    frame=material_frame(); append_material_quad(frame,gray,{200,140,240,200});
    append_ui_quad(frame,{260,200,120,80},{31,157,219,255});
    require_color(capture(renderer,window,frame,output,"material-world-ui-order",{320,240},samples),
                  {31,157,219,255},2,
                  "Painter order or HDR composite graded byte-authored UI");

    auto missing=material_desc(atlas,atlas_rect(0)); missing.channels[5].texture={};
    require_rejected([&]{(void)renderer.create_sprite_material(missing);},
                     "Texture handle has no allocated slot");
    auto wrong=material_desc(atlas,atlas_rect(0)); wrong.channels[1].texture=atlas.textures[0];
    require_rejected([&]{(void)renderer.create_sprite_material(wrong);},
                     "Sprite material channel texture format is incompatible");
    auto fractional=material_desc(atlas,atlas_rect(0)); fractional.channels[0].uv.x=.01f;
    require_rejected([&]{(void)renderer.create_sprite_material(fractional);},
                     "Sprite material UV region must cover whole aligned texels");
    const std::array<std::uint8_t,4> legacy_bytes{128,128,255,255};
    const auto legacy_data=renderer.create_texture({1,1},legacy_bytes);
    auto legacy_binding=material_desc(atlas,atlas_rect(0));
    legacy_binding.channels[1]={legacy_data,{0,0,1,1}};
    require_rejected([&]{(void)renderer.create_sprite_material(legacy_binding);},
                     "Sprite material channel texture format is incompatible");
    auto legacy_frame=material_frame(); append_material_quad(legacy_frame,gray,{200,140,240,200});
    auto& legacy_draw=std::get<GeometryDraw>(legacy_frame.commands.front());
    legacy_draw.sprite_material_index=0;
    legacy_draw.texture=atlas.textures[0];
    legacy_frame.sprite_material_draws.clear();
    require_rejected([&]{renderer.render(legacy_frame);},
                     "Typed sampled textures require a material draw, not a legacy geometry binding");
    const std::array<std::uint8_t,1> one_mask{255};
    const auto stale_texture=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::R8Unorm},one_mask);
    renderer.release_texture(stale_texture);
    auto stale_texture_desc=material_desc(atlas,atlas_rect(0));
    stale_texture_desc.channels[5].texture=stale_texture;
    require_rejected([&]{(void)renderer.create_sprite_material(stale_texture_desc);},
                     "Texture handle is stale or released");
    const auto stale=renderer.create_sprite_material(material_desc(atlas,atlas_rect(0)));
    auto stale_frame=material_frame(); append_material_quad(stale_frame,stale,{200,140,240,200});
    renderer.release_sprite_material(stale);
    require_rejected([&]{renderer.render(stale_frame);},"Material handle is stale or released");

    const auto albedo_hash=digest(atlas.albedo);
    return {{"typed_texture_creations",7}, {"material_creations",16},
            {"albedo_sha256",albedo_hash},
            {"emission_off_albedo_sha256",albedo_hash},
            {"emission_on_albedo_sha256",albedo_hash},
            {"edge_clamp_controls",{{"albedo",true},{"normal_material_ao",true},
                                     {"emission_mask",true}}},
            {"immutable_uv_and_emission_copy",true}, {"constant_albedo_emission_toggle",true},
            {"missing_channel_rejected",true}, {"wrong_format_rejected",true},
            {"fractional_atlas_region_rejected",true}, {"legacy_data_binding_rejected",true},
            {"typed_legacy_draw_rejected",true},
            {"stale_texture_rejected",true},{"stale_material_rejected",true}};
}

Json game_art_render_contract(Renderer2D& renderer, SDL_Window* window, GameArt& art,
                              const std::filesystem::path& output, Json& samples) {
    auto frame=material_frame();
    art.lit_sprite(frame,"analytic.sprite",{320,240},{0,0,0},1.f);
    require_gray(capture(renderer,window,frame,output,"game-art-schema2-lit-sprite",{320,240},samples),
                 128,4,"GameArt schema-2 bridge did not render its analytic material");
    return {{"contains_lit",art.contains_lit("analytic.sprite")},
            {"analytic_fixture_only",true}};
}

Json fault_rollback_contract(const std::filesystem::path& runtime_assets,
                             const std::filesystem::path& fixture,
                             const std::filesystem::path& output) {
    Json results=Json::array();
    for (const auto candidate_position : {1u,3u,6u}) {
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow(("Ribterra material fault "+std::to_string(candidate_position)).c_str(),
                             640,480,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);
        require(window!=nullptr,SDL_GetError());
        settle_window(window.get());
        RendererOptions options;
        options.backend=Backend::Direct3D12;
        options.test_fail_sampled_texture_creation_at=6u+candidate_position;
        Renderer2D renderer(window.get(),runtime_assets,options);
        AssetCatalog assets(fixture);
        GameArt art(renderer);
        art.load(assets); // Six baseline sampled textures.
        std::string diagnostic;
        try { art.load(assets); }
        catch(const std::runtime_error& error) { diagnostic=error.what(); }
        require(diagnostic=="Injected sampled texture candidate creation failure",
                "Sampled-texture fault did not fire at the exact candidate creation point");
        require(art.contains_lit("analytic.sprite"),
                "Failed sampled-texture candidate replaced the old GameArt catalog");
        auto frame=material_frame(); art.lit_sprite(frame,"analytic.sprite",{320,240},{0,0,0},1.f);
        const auto path=output/("fault-candidate-"+std::to_string(candidate_position)+"-old-catalog.png");
        renderer.render(frame,path);
        auto surface=load_capture(path);
        require_gray(sample(surface.get(),window.get(),renderer,{320,240}),128,4,
                     "Old material failed to render after sampled-texture candidate rollback");
        results.push_back({{"candidate_position",candidate_position},
                           {"absolute_creation_attempt",6u+candidate_position},
                           {"diagnostic",diagnostic},{"old_catalog_rendered",true}});
    }
    return results;
}

int software_contract(const std::filesystem::path& runtime_assets) {
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra material software contract",320,240,SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window!=nullptr,SDL_GetError());
    Renderer2D renderer(window.get(),runtime_assets,{Backend::Software});
    const std::array<std::uint8_t,4> rgba{255,255,255,255};
    require_rejected([&]{(void)renderer.create_sampled_texture(
                         {{1,1},SampledTextureFormat::Rgba8UnormSrgb},rgba);},
                     "Typed sampled textures are unavailable on the software backend");

    Frame2D frame; frame.logical_size={320,240};
    append_material_quad(frame,{}, {40,40,240,160});
    require_rejected([&]{renderer.render(frame);},
                     "Sprite materials require the linear HDR world path");
    frame.linear_hdr_world=true;
    auto& draw=std::get<GeometryDraw>(frame.commands.front());
    draw.pass=Pass::Ui;
    require_rejected([&]{renderer.render(frame);},
                     "Sprite materials require alpha-blended world geometry");
    draw.pass=Pass::WorldOrdered;
    auto invalid=frame;
    invalid.sprite_material_draws.front().surface.response[1]=1.f;
    require_rejected([&]{renderer.render(invalid);},
                     "Sprite surface geometry, emission or response is invalid");
    invalid=frame;
    invalid.sprite_material_draws.front().surface.anchor_export[3]=0.f;
    require_rejected([&]{renderer.render(invalid);},
                     "Sprite surface geometry, emission or response is invalid");
    invalid=frame;
    invalid.sprite_material_draws.front().surface.source_size_pivot={4096,4096,0,0};
    invalid.sprite_material_draws.front().surface.anchor_export[3]=.001f;
    require_rejected([&]{renderer.render(invalid);},
                     "Reconstructed sprite position exceeds the render metric bound");
    require_rejected([&]{renderer.render(frame);},
                     "Linear HDR world is unavailable on the software backend");
    std::cout << "Material bridge software validation/rejection contracts passed; no GPU shader proof.\n";
    return 0;
}

int native_probe(SDL_Window* window, const std::filesystem::path& runtime_assets,
                 const std::filesystem::path& output) {
    RendererOptions options; options.backend=Backend::Direct3D12; options.collect_telemetry=true;
    Renderer2D renderer(window,runtime_assets,options);
    require(!renderer.device_info().software && renderer.device_info().backend=="direct3d12",
            "Material bridge proof requires native Direct3D 12");
    require(renderer.world_hdr_status().available,"Material bridge requires the HDR world path");

    GameArt art(renderer);
    const auto schema=schema_transaction_contract(renderer,output,art);
    Json samples=Json::object();
    const auto direct=direct_renderer_contract(renderer,window,output,samples);
    const auto game_art=game_art_render_contract(renderer,window,art,output,samples);
    const auto valid_fixture=output/"fixture-valid";
    const auto faults=fault_rollback_contract(runtime_assets,valid_fixture,output);
    const auto display=renderer.display_info();

    Json report{{"schema",1},{"build",RIBTERRA_PROBE_BUILD},{"platform",SDL_GetPlatform()},
                {"backend",renderer.device_info().backend},{"device",renderer.device_info().device},
                {"driver",renderer.device_info().driver},{"software",renderer.device_info().software},
                {"window_points",{640,480}},
                {"drawable",{display.drawable.width,display.drawable.height}},
                {"samples",samples},{"schema2",schema},
                {"direct_renderer",direct},{"game_art",game_art},{"fault_rollbacks",faults},
                {"input_counts",{{"analytic_atlas_channels",6},{"schema_negative_candidates",6},
                                  {"fault_candidate_positions",3}}},
                {"claims",{"typed sampled texture formats","hardware SRGB decode before filtering",
                           "linear material data channels","R16 depth affects analytic point light",
                           "schema-2 transactional installation","material candidate rollback"}},
                {"not_claimed",{"concept-ready art kit","complete material graph","shadows",
                                "occlusion","performance","native shader generality beyond controls",
                                "full E13 completion"}},
                {"complete",true}};
    const auto report_path=output/"material-bridge-report.json";
    write_text(report_path,report.dump(2)+"\n");
    std::cout<<"Material bridge native analytic proof passed; artifacts preserved in "
             <<utf8(output)<<'\n';
    return 0;
}

} // namespace

int main(int argc,char** argv) {
    try {
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        struct Quit { ~Quit(){SDL_Quit();} } quit;
        const auto base=std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
        if(argc==2 && std::string_view(argv[1])=="--software-contract")
            return software_contract(base/"assets");
        require(argc==2,"Usage: MaterialBridgeProbe <new-output-directory> | --software-contract");
        const auto output=path_from_utf8(argv[1]);
        require(!std::filesystem::exists(output),"Output directory already exists; refusing overwrite");
        std::filesystem::create_directories(output);
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Material Bridge Probe",640,480,
                             SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);
        require(window!=nullptr,SDL_GetError());
        settle_window(window.get());
        return native_probe(window.get(),base/"assets",output);
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

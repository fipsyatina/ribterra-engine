#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
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

namespace authored_sky_native_probe {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace ribterra;
using namespace ribterra::render;

constexpr Extent logical_size{640, 480};
constexpr ContactProjection projection{{320.f, 240.f}, 1.8f};
constexpr Point ui_sample{30.f, 28.f};
constexpr std::array<std::uint8_t, 4> ui_color{215, 42, 76, 255};
constexpr float diagonal = .7071067811865475f;
constexpr std::array<float, 3> sun_direction{diagonal, 0.f, diagonal};
constexpr std::array<float, 3> moon_direction{-diagonal, 0.f, diagonal};
constexpr std::array<float, 3> base_anchor{-2.f, -3.f, 0.f};
constexpr float caster_scale = 3.f;
constexpr float visual_scale = .5f;
constexpr Rect visible_ground_roi{90.f, 100.f, 470.f, 255.f};
constexpr std::array<float, 3> ground_receiver_center{1.f, 0.f, 0.f};
constexpr float ground_receiver_extent_x = 11.f;
constexpr float ground_receiver_extent_y = 5.f;
constexpr std::uintmax_t max_native_output_bytes = 128ull * 1024ull * 1024ull;
constexpr std::uintmax_t min_free_bytes = 384ull * 1024ull * 1024ull;
constexpr std::uintmax_t projected_capture_bytes = 8ull * 1024ull * 1024ull;
fs::path guarded_output_root;

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

fs::path from_utf8(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::uintmax_t logical_tree_bytes(const fs::path& root) {
    if (!fs::exists(root)) return 0;
    std::uintmax_t total{};
    for (fs::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator)
        if (iterator->is_regular_file()) total += iterator->file_size();
    return total;
}

bool path_is_within(const fs::path& child, const fs::path& parent) {
    std::error_code error;
    const auto relative = fs::relative(fs::weakly_canonical(child),
                                       fs::weakly_canonical(parent), error);
    if (error || relative.empty()) return !error;
    const auto first = *relative.begin();
    return first != "..";
}

void require_output_budget(std::uintmax_t projected_addition, std::string_view operation) {
    if (guarded_output_root.empty()) return;
    const auto current = logical_tree_bytes(guarded_output_root);
    require(projected_addition <= max_native_output_bytes &&
                current <= max_native_output_bytes - projected_addition,
            std::string(operation) + " would exceed the 128 MiB native evidence limit");
    const auto available = fs::space(guarded_output_root).available;
    require(available >= min_free_bytes && available - min_free_bytes >= projected_addition,
            std::string(operation) + " requires 384 MiB free after the projected write");
}

std::string to_utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    const auto size = fs::file_size(path);
    require(size <= std::uintmax_t(std::numeric_limits<std::size_t>::max()) &&
                size <= std::uintmax_t(std::numeric_limits<std::streamsize>::max()),
            "Input file is too large for the native probe");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "Could not open native probe input");
    if (!result.empty())
        require(bool(stream.read(reinterpret_cast<char*>(result.data()),
                                 static_cast<std::streamsize>(result.size()))),
                "Could not read complete native probe input");
    return result;
}

std::string read_text(const fs::path& path) {
    const auto bytes = read_bytes(path);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void write_bytes_new(const fs::path& path, std::span<const std::uint8_t> bytes) {
    require(!fs::exists(path), "Native probe refuses to overwrite retained evidence");
    require_output_budget(bytes.size(), "native evidence write");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not create native probe output");
    if (!bytes.empty())
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    stream.close();
    require(stream.good(), "Could not finish native probe output");
    require_output_budget(0, "completed native evidence write");
}

void write_text_new(const fs::path& path, std::string_view text) {
    write_bytes_new(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

void write_json_new(const fs::path& path, const Json& value) {
    write_text_new(path, value.dump(2) + "\n");
}

std::string digest(std::span<const std::uint8_t> bytes) { return sha256(bytes); }
std::string digest(std::string_view text) {
    return digest(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
std::string file_digest(const fs::path& path) { return digest(read_bytes(path)); }

std::string manifest_content_hash(const Json& assets) {
    std::vector<const Json*> rows;
    rows.reserve(assets.size());
    for (const auto& row : assets) rows.push_back(&row);
    std::sort(rows.begin(), rows.end(), [](const Json* left, const Json* right) {
        return left->at("id").get<std::string>() < right->at("id").get<std::string>();
    });
    std::string canonical;
    for (const auto* row : rows) {
        for (const auto field : {"id", "path", "kind", "sha256", "license", "source",
                                 "author", "notice"}) {
            const auto value = row->at(field).get<std::string>();
            canonical += std::to_string(value.size()) + ":" + value;
        }
        canonical += '\n';
    }
    return digest(canonical);
}

struct KitVariant {
    fs::path root;
    std::string world_hash;
    std::string manifest_hash;
    std::string descriptor_hash;
};

using WorldMutation = std::function<void(Json&)>;

KitVariant make_variant(const fs::path& base, const fs::path& output, std::string_view name,
                        const WorldMutation& mutate_world, int caster_fill = -1) {
    const auto root = output / "derived-kits" / std::string(name);
    require(!fs::exists(root), "Derived native kit already exists");
    require(fs::create_directories(root), "Could not create derived native kit");
    Json manifest = Json::parse(read_text(base / "manifest.json"));
    Json world = Json::parse(read_text(base / "art/world.json"));
    mutate_world(world);
    std::map<std::string, std::vector<std::uint8_t>> overrides;

    if (caster_fill >= 0) {
        const auto texture_id = world.at("materials").at(0).at("channels")
            .at("caster_mask").at("texture").get<std::string>();
        auto texture = std::find_if(world.at("textures").begin(), world.at("textures").end(),
            [&](const Json& row) { return row.at("id") == texture_id; });
        require(texture != world.at("textures").end(), "Caster-mask texture descriptor is missing");
        const auto path = texture->at("path").get<std::string>();
        auto bytes = read_bytes(base / from_utf8(path));
        std::fill(bytes.begin(), bytes.end(), static_cast<std::uint8_t>(caster_fill));
        (*texture)["sha256"] = digest(bytes);
        (*texture)["bytes"] = bytes.size();
        overrides[path] = std::move(bytes);
    }

    const auto& triangles = world.at("lit_sprites").at(0).at("sky_shadow_triangles");
    const auto descriptor_hash = digest(triangles.dump() + "\n");
    Json calibration = Json::parse(read_text(base / "art/material/calibration.json"));
    calibration["sky_shadow_triangle_count"] = triangles.size();
    calibration["sky_shadow_triangles_sha256"] = descriptor_hash;
    const auto calibration_text = calibration.dump(2) + "\n";
    overrides["art/material/calibration.json"] = std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(calibration_text.data()),
        reinterpret_cast<const std::uint8_t*>(calibration_text.data()) + calibration_text.size());

    const auto world_text = world.dump(2) + "\n";
    overrides["art/world.json"] = std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(world_text.data()),
        reinterpret_cast<const std::uint8_t*>(world_text.data()) + world_text.size());
    for (auto& row : manifest.at("assets")) {
        const auto relative_text = row.at("path").get<std::string>();
        const auto destination = root / from_utf8(relative_text);
        const auto replacement = overrides.find(relative_text);
        if (replacement != overrides.end()) {
            write_bytes_new(destination, replacement->second);
            row["sha256"] = digest(replacement->second);
            row["bytes"] = replacement->second.size();
            continue;
        }
        fs::create_directories(destination.parent_path());
        require_output_budget(fs::file_size(base / from_utf8(relative_text)),
                              "derived-kit hardlink or copy");
        std::error_code link_error;
        fs::create_hard_link(base / from_utf8(relative_text), destination, link_error);
        if (link_error) {
            std::error_code copy_error;
            const bool copied = fs::copy_file(base / from_utf8(relative_text), destination,
                                              fs::copy_options::none, copy_error);
            require(copied && !copy_error, "Could not preserve derived native asset");
        }
        require_output_budget(0, "completed derived-kit hardlink or copy");
    }
    manifest["content_sha256"] = manifest_content_hash(manifest.at("assets"));
    const auto manifest_text = manifest.dump(2) + "\n";
    write_text_new(root / "manifest.json", manifest_text);
    AssetCatalog(root).verify();
    require_output_budget(0, "completed derived native kit");
    return {root, digest(world_text), digest(manifest_text), descriptor_hash};
}

struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
};
using Pixel = std::array<std::uint8_t, 4>;

Image load_png(const fs::path& path) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
        SDL_LoadPNG(to_utf8(path).c_str()), SDL_DestroySurface);
    require(source != nullptr, "SDL_LoadPNG could not decode native capture");
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
        SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    require(converted != nullptr, "Could not convert native capture to RGBA32");
    Image result{converted->w, converted->h,
                 std::vector<std::uint8_t>(static_cast<std::size_t>(converted->w) *
                                           converted->h * 4u)};
    for (int y = 0; y < result.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(converted->pixels) + y * converted->pitch;
        std::copy_n(row, static_cast<std::size_t>(result.width) * 4u,
                    result.rgba.begin() + static_cast<std::size_t>(y) * result.width * 4u);
    }
    return result;
}

Pixel pixel(const Image& image, int x, int y) {
    require(x >= 0 && y >= 0 && x < image.width && y < image.height,
            "Native sample is outside capture");
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
    return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2],
            image.rgba[offset + 3]};
}

Pixel logical_pixel(const Image& image, Point logical) {
    const double scale = std::min(double(image.width) / logical_size.width,
                                  double(image.height) / logical_size.height);
    const double left = (image.width - logical_size.width * scale) * .5;
    const double top = (image.height - logical_size.height * scale) * .5;
    const double sample_x = left + logical.x * scale;
    const double sample_y = top + logical.y * scale;
    require(std::isfinite(sample_x) && std::isfinite(sample_y) &&
                sample_x >= 0. && sample_y >= 0. &&
                sample_x < double(image.width) && sample_y < double(image.height),
            "Logical sample is outside capture");
    return pixel(image, static_cast<int>(std::floor(sample_x)),
                 static_cast<int>(std::floor(sample_y)));
}

int brightness(Pixel value) { return int(value[0]) + int(value[1]) + int(value[2]); }
Json pixel_json(Pixel value) { return {value[0], value[1], value[2], value[3]}; }

struct Reporter {
    fs::path output;
    Json checks = Json::array();
    Json captures = Json::array();
    Json variants = Json::array();
    Json diagnostics = Json::object();
    std::uint32_t capture_count{};
    std::uint32_t serial{};

    void pass(std::string_view name, Json evidence = Json::object()) {
        checks.push_back({{"name", name}, {"passed", true}, {"evidence", std::move(evidence)}});
    }
    fs::path capture_path(std::string_view name) {
        require(++capture_count <= 40u, "Authored native probe exceeded 40 PNGs");
        const auto path = output / (std::string(name) + ".png");
        require(!fs::exists(path), "Native capture path already exists");
        require_output_budget(projected_capture_bytes, "native PNG capture");
        return path;
    }
    void checkpoint(std::string_view phase) {
        write_json_new(output / ("authored-sky-native-progress-" + std::to_string(serial++) + ".json"),
                        {{"schema", 1}, {"complete", false}, {"phase", phase},
                         {"build", RIBTERRA_PROBE_BUILD}, {"checks", checks},
                         {"captures", captures}, {"diagnostics", diagnostics},
                         {"png_count", capture_count}});
    }
};

struct Captured {
    Image image;
    std::vector<std::uint8_t> file_bytes;
    RendererFrameTelemetry telemetry;
    std::string name;
};

Json sample_pair_json(const Captured& open, const Captured& shadowed, Point point) {
    return {{"open", pixel_json(logical_pixel(open.image, point))},
            {"shadow", pixel_json(logical_pixel(shadowed.image, point))}};
}

Json visible_receiver_diagnostic(const std::array<float, 3>& receiver, Point projected,
                                 float margin_texels, const Captured& open,
                                 const Captured& shadowed, Point typed_sample) {
    return {{"receiver", receiver}, {"projected", {projected.x, projected.y}},
            {"margin_texels", margin_texels},
            {"ground_exact", sample_pair_json(open, shadowed, projected)},
            {"typed_point", {typed_sample.x, typed_sample.y}},
            {"typed_exact", sample_pair_json(open, shadowed, typed_sample)}};
}

Json telemetry_json(const RendererFrameTelemetry& value) {
    Json passes = Json::array();
    for (std::uint32_t index = 0; index < value.graph_pass_count; ++index)
        passes.push_back(static_cast<std::uint32_t>(value.graph_passes[index]));
    return {{"submitted", value.submitted}, {"capture", value.capture},
            {"frame_slot", value.frame_slot}, {"graph_pass_count", value.graph_pass_count},
            {"graph_passes", passes},
            {"gpu_execution_ms", value.gpu_execution_ms ? Json(*value.gpu_execution_ms) : Json(nullptr)}};
}

Json device_json(const DeviceInfo& value) {
    return {{"backend", value.backend}, {"device", value.device},
            {"driver", value.driver}, {"software", value.software}};
}

void pump_events() { SDL_Event event{}; while (SDL_PollEvent(&event)) {} }

Captured capture(Renderer2D& renderer, const Frame2D& frame, Reporter& report,
                 std::string_view name) {
    pump_events();
    const auto path = report.capture_path(name);
    renderer.render(frame, path);
    Captured result{load_png(path), read_bytes(path), renderer.frame_telemetry(), std::string(name)};
    require(result.telemetry.submitted && result.telemetry.capture,
            "Native capture was not submitted and read back");
    const std::array<WorldGraphPass, 7> expected{
        WorldGraphPass::SunShadow, WorldGraphPass::MoonShadow,
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward, WorldGraphPass::Composite, WorldGraphPass::Ui};
    require(result.telemetry.graph_pass_count == expected.size(),
            "Authored SkyShadowSlice encoded an unexpected pass count");
    for (std::size_t index = 0; index < expected.size(); ++index)
        require(result.telemetry.graph_passes[index] == expected[index],
                "Authored SkyShadowSlice pass order changed");
    require(logical_pixel(result.image, ui_sample) == ui_color,
            "Authored sky pass changed exact UI color");
    report.captures.push_back({{"name", name}, {"file", path.filename().string()},
                               {"sha256", digest(result.file_bytes)},
                               {"telemetry", telemetry_json(result.telemetry)}});
    require_output_budget(0, "completed native PNG capture");
    return result;
}

std::array<float, 3> subtract(const std::array<float, 3>& left,
                              const std::array<float, 3>& right) {
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}
std::array<float, 3> add(const std::array<float, 3>& left,
                         const std::array<float, 3>& right) {
    return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
}
std::array<float, 3> multiply(const std::array<float, 3>& value, float scale) {
    return {value[0] * scale, value[1] * scale, value[2] * scale};
}
float dot(const std::array<float, 3>& left, const std::array<float, 3>& right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}
std::array<float, 3> cross(const std::array<float, 3>& left,
                           const std::array<float, 3>& right) {
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}
std::array<float, 3> normalized(std::array<float, 3> value) {
    const float length = std::sqrt(dot(value, value));
    for (auto& component : value) component /= length;
    return value;
}

struct AuthoredTriangle {
    std::string id;
    std::array<std::array<float, 3>, 3> vertices{};
    std::uint32_t mask{};
};

std::vector<AuthoredTriangle> descriptor_triangles(const Json& world) {
    std::vector<AuthoredTriangle> result;
    for (const auto& row : world.at("lit_sprites").at(0).at("sky_shadow_triangles")) {
        AuthoredTriangle triangle;
        triangle.id = row.at("id").get<std::string>();
        triangle.mask = row.at("source_mask").get<std::uint32_t>();
        for (std::size_t vertex = 0; vertex < triangle.vertices.size(); ++vertex)
            triangle.vertices[vertex] = row.at("vertices").at(vertex)
                .get<std::array<float, 3>>();
        result.push_back(std::move(triangle));
    }
    return result;
}

std::vector<AuthoredTriangle> transformed_triangles(const std::vector<AuthoredTriangle>& local,
                                                    const std::array<float, 3>& anchor,
                                                    float scale,
                                                    const std::array<float, 3>& camera) {
    auto result = local;
    for (auto& triangle : result)
        for (auto& vertex : triangle.vertices)
            vertex = camera_relative_render_xyz(add(anchor, multiply(vertex, scale)), camera);
    return result;
}

bool visible_ground_receiver_point(const std::array<float, 3>& point) {
    if (point[0] < ground_receiver_center[0] - ground_receiver_extent_x ||
        point[0] > ground_receiver_center[0] + ground_receiver_extent_x ||
        point[1] < ground_receiver_center[1] - ground_receiver_extent_y ||
        point[1] > ground_receiver_center[1] + ground_receiver_extent_y ||
        point[2] != ground_receiver_center[2]) return false;
    const auto projected = contact_project_logical(point, projection);
    return projected[0] >= visible_ground_roi.x &&
           projected[0] < visible_ground_roi.x + visible_ground_roi.w &&
           projected[1] >= visible_ground_roi.y &&
           projected[1] < visible_ground_roi.y + visible_ground_roi.h;
}

struct RayHit {
    bool hit{};
    std::string id;
    std::uint32_t mask{};
    float t{}, bary_u{}, bary_v{};
    std::array<float, 3> point{};
    float edge_margin_texels{};
};

bool intersect_triangle(const std::array<float, 3>& origin,
                        const std::array<float, 3>& direction,
                        const AuthoredTriangle& triangle, float& t, float& u, float& v) {
    const auto edge_a = subtract(triangle.vertices[1], triangle.vertices[0]);
    const auto edge_b = subtract(triangle.vertices[2], triangle.vertices[0]);
    const auto p = cross(direction, edge_b);
    const float determinant = dot(edge_a, p);
    if (std::abs(determinant) < 1e-7f) return false;
    const float inverse = 1.f / determinant;
    const auto distance = subtract(origin, triangle.vertices[0]);
    u = dot(distance, p) * inverse;
    if (u < 0.f || u > 1.f) return false;
    const auto q = cross(distance, edge_a);
    v = dot(direction, q) * inverse;
    if (v < 0.f || u + v > 1.f) return false;
    t = dot(edge_b, q) * inverse;
    return t > .001f;
}

struct ShadowBasis {
    std::array<float, 3> right{}, up{};
    float texels_u{}, texels_v{};
};

ShadowBasis shadow_basis(const std::array<float, 3>& direction) {
    const auto forward = multiply(normalized(direction), -1.f);
    const auto right = normalized(cross({0.f, 1.f, 0.f}, forward));
    const auto up = cross(forward, right);
    std::array<float, 2> minimum{std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max()};
    std::array<float, 2> maximum{std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest()};
    for (const float x : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound})
        for (const float y : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound})
            for (const float z : {-sky_shadow_coordinate_bound, sky_shadow_coordinate_bound}) {
                const std::array<float, 3> corner{x, y, z};
                const std::array<float, 2> projected{dot(corner, right), dot(corner, up)};
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    minimum[axis] = std::min(minimum[axis], projected[axis]);
                    maximum[axis] = std::max(maximum[axis], projected[axis]);
                }
            }
    constexpr float interior = sky_shadow_map_resolution - 4.f;
    const float span_u = (maximum[0] - minimum[0]) * sky_shadow_map_resolution / interior;
    const float span_v = (maximum[1] - minimum[1]) * sky_shadow_map_resolution / interior;
    return {right, up, sky_shadow_map_resolution / span_u,
            sky_shadow_map_resolution / span_v};
}

float point_segment_distance(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float length_squared = dx * dx + dy * dy;
    const float amount = length_squared > 0.f
        ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length_squared, 0.f, 1.f)
        : 0.f;
    return std::hypot(px - (ax + amount * dx), py - (ay + amount * dy));
}

float edge_margin_texels(const std::array<float, 3>& point,
                         const AuthoredTriangle& triangle,
                         const ShadowBasis& basis) {
    const float px = dot(point, basis.right) * basis.texels_u;
    const float py = dot(point, basis.up) * basis.texels_v;
    float margin = std::numeric_limits<float>::max();
    for (std::size_t edge = 0; edge < 3; ++edge) {
        const auto& a = triangle.vertices[edge];
        const auto& b = triangle.vertices[(edge + 1u) % 3u];
        margin = std::min(margin, point_segment_distance(
            px, py, dot(a, basis.right) * basis.texels_u,
            dot(a, basis.up) * basis.texels_v,
            dot(b, basis.right) * basis.texels_u,
            dot(b, basis.up) * basis.texels_v));
    }
    return margin;
}

RayHit nearest_hit(const std::vector<AuthoredTriangle>& triangles,
                   const std::array<float, 3>& receiver,
                   const std::array<float, 3>& direction, std::uint32_t source_bit) {
    RayHit result;
    result.t = std::numeric_limits<float>::max();
    const auto basis = shadow_basis(direction);
    for (const auto& triangle : triangles) {
        if ((triangle.mask & source_bit) == 0u) continue;
        float t{}, u{}, v{};
        if (intersect_triangle(receiver, direction, triangle, t, u, v) && t < result.t) {
            result.hit = true; result.id = triangle.id; result.mask = triangle.mask;
            result.t = t; result.bary_u = u; result.bary_v = v;
            result.point = add(receiver, multiply(direction, t));
            result.edge_margin_texels = edge_margin_texels(result.point, triangle, basis);
        }
    }
    return result;
}

Json hit_json(const RayHit& hit) {
    return {{"hit", hit.hit}, {"triangle_id", hit.id}, {"source_mask", hit.mask},
            {"t", hit.t}, {"barycentrics", {1.f - hit.bary_u - hit.bary_v,
                                              hit.bary_u, hit.bary_v}},
            {"intersection", hit.point}, {"edge_margin_texels", hit.edge_margin_texels}};
}

std::array<float, 3> find_interior(const std::vector<AuthoredTriangle>& triangles,
                                   std::array<float, 3> seed,
                                   const std::array<float, 3>& direction,
                                   std::uint32_t source_bit, std::string_view id_fragment) {
    std::array<float, 3> best{};
    float best_margin{};
    const auto consider = [&](const std::array<float, 3>& candidate) {
        if (!visible_ground_receiver_point(candidate)) return;
        const auto hit = nearest_hit(triangles, candidate, direction, source_bit);
        if (hit.hit && hit.id.find(id_fragment) != std::string::npos &&
            hit.edge_margin_texels > best_margin) {
            best = candidate;
            best_margin = hit.edge_margin_texels;
        }
    };
    for (int y = -20; y <= 20; ++y)
        for (int x = -20; x <= 20; ++x) {
            auto candidate = seed;
            candidate[0] += float(x) * .05f;
            candidate[1] += float(y) * .05f;
            consider(candidate);
        }
    if (best_margin < 5.f)
        for (int y = 0; y <= 200; ++y)
            for (int x = 0; x <= 440; ++x)
                consider(std::array<float, 3>{
                    ground_receiver_center[0] - ground_receiver_extent_x + float(x) * .05f,
                    ground_receiver_center[1] - ground_receiver_extent_y + float(y) * .05f,
                    ground_receiver_center[2]});
    require(best_margin >= 5.f, "Could not find a PCF-safe authored triangle interior");
    return best;
}

float outside_margin(const std::vector<AuthoredTriangle>& triangles,
                     const std::array<float, 3>& receiver,
                     const std::array<float, 3>& direction, std::uint32_t source_bit) {
    const auto basis = shadow_basis(direction);
    float margin = std::numeric_limits<float>::max();
    for (const auto& triangle : triangles)
        if ((triangle.mask & source_bit) != 0u)
            margin = std::min(margin, edge_margin_texels(receiver, triangle, basis));
    return margin;
}

std::array<float, 3> find_outside(const std::vector<AuthoredTriangle>& triangles,
                                  std::array<float, 3> seed,
                                  const std::array<float, 3>& direction,
                                  std::uint32_t source_bit) {
    std::array<float, 3> best{};
    float best_margin{};
    const auto consider = [&](const std::array<float, 3>& candidate) {
        if (!visible_ground_receiver_point(candidate) ||
            nearest_hit(triangles, candidate, direction, source_bit).hit) return;
        const float margin = outside_margin(triangles, candidate, direction, source_bit);
        if (margin > best_margin) {
            best = candidate;
            best_margin = margin;
        }
    };
    for (int y = -20; y <= 20; ++y)
        for (int x = -20; x <= 20; ++x) {
            auto candidate = seed;
            candidate[0] += float(x) * .05f;
            candidate[1] += float(y) * .05f;
            consider(candidate);
        }
    if (best_margin < 5.f)
        for (int y = 0; y <= 200; ++y)
            for (int x = 0; x <= 440; ++x)
                consider(std::array<float, 3>{
                    ground_receiver_center[0] - ground_receiver_extent_x + float(x) * .05f,
                    ground_receiver_center[1] - ground_receiver_extent_y + float(y) * .05f,
                    ground_receiver_center[2]});
    require(best_margin >= 5.f, "Could not find a PCF-safe authored triangle exterior");
    return best;
}

std::array<float, 3> cross_normal(const std::array<float, 3>& u,
                                  const std::array<float, 3>& v) {
    return normalized(cross(u, v));
}

struct ReceiverSpec {
    std::array<float, 3> center{};
    std::array<float, 3> u{1.f, 0.f, 0.f};
    std::array<float, 3> v{0.f, 1.f, 0.f};
    float extent_u{1.f}, extent_v{1.f};
};

void append_ground_receiver(Frame2D& frame, TextureHandle albedo, const ReceiverSpec& spec) {
    GroundMaterialDraw material;
    material.albedo = albedo;
    material.surface.response = {1.f, 0.f, 0.f, 1.f};
    material.surface.flags = {1u, 0u, 0u, 0u};
    material.surface.visibility = {1.f, 1.f, 0.f, 0.f};
    frame.ground_material_draws.push_back(material);
    const auto material_index = static_cast<std::uint32_t>(frame.ground_material_draws.size());
    const auto normal = cross_normal(spec.u, spec.v);
    const auto base = static_cast<std::uint32_t>(frame.vertices.size());
    constexpr std::array<std::array<float, 2>, 4> signs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    constexpr std::array<Point, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t index = 0; index < signs.size(); ++index) {
        std::array<float, 3> world{};
        for (std::size_t axis = 0; axis < 3; ++axis)
            world[axis] = spec.center[axis] + signs[index][0] * spec.extent_u * spec.u[axis] +
                          signs[index][1] * spec.extent_v * spec.v[axis];
        const auto screen = contact_project_logical(world, projection);
        frame.vertices.push_back({{screen[0], screen[1]}, uvs[index], {255, 255, 255, 255}});
        frame.surface_vertices.push_back({world, normal});
    }
    const auto first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::Terrain; draw.blend = Blend::Opaque;
    draw.first_index = first; draw.index_count = 6; draw.ground_material_index = material_index;
    frame.commands.emplace_back(draw);
}

MaterialHandle create_flat_material(Renderer2D& renderer, bool emissive) {
    const std::array<std::uint8_t, 4> albedo{144, 144, 144, 255};
    const std::array<std::uint8_t, 4> normal{128, 128, 255, 255};
    const std::array<std::uint8_t, 2> depth{0, 0};
    const std::array<std::uint8_t, 1> emission{static_cast<std::uint8_t>(emissive ? 255 : 0)};
    const std::array<std::uint8_t, 4> surface{0, 0, 0, 0};
    const std::array<std::uint8_t, 1> caster{0};
    SpriteMaterialDesc desc;
    desc.channels[0].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo);
    desc.channels[1].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8Unorm}, normal);
    desc.channels[2].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R16Unorm}, depth);
    desc.channels[3].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R8Unorm}, emission);
    desc.channels[4].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8Unorm}, surface);
    desc.channels[5].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R8Unorm}, caster);
    for (auto& channel : desc.channels) channel.uv = {0.f, 0.f, 1.f, 1.f};
    desc.emission = emissive ? std::array<float, 4>{1.f, .3f, .08f, .8f}
                             : std::array<float, 4>{0.f, 0.f, 0.f, 0.f};
    return renderer.create_sprite_material(desc);
}

void append_typed_receiver(Frame2D& frame, MaterialHandle material, Rect rectangle,
                           const std::array<float, 3>& world, bool emissive) {
    SpriteMaterialDraw typed;
    typed.material = material;
    for (auto& region : typed.surface.uv_regions) region = {0.f, 0.f, 1.f, 1.f};
    typed.surface.anchor_export = {world[0], world[1], world[2], 64.f};
    typed.surface.source_size_pivot = {1.f, 1.f, .5f, .5f};
    typed.surface.depth_scale = {0.f, 1.f, 1.f, 0.f};
    typed.surface.emission = emissive ? std::array<float, 4>{1.f, .3f, .08f, .8f}
                                      : std::array<float, 4>{0.f, 0.f, 0.f, 0.f};
    typed.surface.response = {0.f, 0.f, 0.f, 0.f};
    frame.sprite_material_draws.push_back(typed);
    const auto material_index = static_cast<std::uint32_t>(frame.sprite_material_draws.size());
    const auto base = static_cast<std::uint32_t>(frame.vertices.size());
    const std::array<Vertex, 4> vertices{{
        {{rectangle.x, rectangle.y}, {0, 0}, {255, 255, 255, 255}},
        {{rectangle.x + rectangle.w, rectangle.y}, {1, 0}, {255, 255, 255, 255}},
        {{rectangle.x + rectangle.w, rectangle.y + rectangle.h}, {1, 1}, {255, 255, 255, 255}},
        {{rectangle.x, rectangle.y + rectangle.h}, {0, 1}, {255, 255, 255, 255}}}};
    for (const auto& vertex : vertices) {
        frame.vertices.push_back(vertex); frame.surface_vertices.push_back({});
    }
    const auto first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::WorldOrdered; draw.blend = Blend::Alpha;
    draw.first_index = first; draw.index_count = 6; draw.sprite_material_index = material_index;
    frame.commands.emplace_back(draw);
}

void append_ui(Frame2D& frame) {
    const auto base = static_cast<std::uint32_t>(frame.vertices.size());
    const std::array<Vertex, 4> vertices{{
        {{18, 18}, {0, 0}, {215, 42, 76, 255}}, {{70, 18}, {1, 0}, {215, 42, 76, 255}},
        {{70, 42}, {1, 1}, {215, 42, 76, 255}}, {{18, 42}, {0, 1}, {215, 42, 76, 255}}}};
    for (const auto& vertex : vertices) {
        frame.vertices.push_back(vertex); frame.surface_vertices.push_back({});
    }
    const auto first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::Ui; draw.blend = Blend::Opaque;
    draw.first_index = first; draw.index_count = 6;
    frame.commands.emplace_back(draw);
}

struct ReceiverFixture {
    TextureHandle ground{};
    MaterialHandle flat{}, emissive{};
    std::array<float, 3> sun_point{}, moon_point{};
};

ReceiverFixture create_receivers(Renderer2D& renderer,
                                 const std::vector<AuthoredTriangle>& base_triangles) {
    ReceiverFixture result;
    const std::array<std::uint8_t, 4> ground{144, 144, 144, 255};
    result.ground = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, ground);
    result.flat = create_flat_material(renderer, false);
    result.emissive = create_flat_material(renderer, true);
    const auto transformed = transformed_triangles(base_triangles, base_anchor, caster_scale, {0, 0, 0});
    result.sun_point = find_interior(transformed, add(base_anchor, {4.5f, 3.f, 0.f}),
                                     sun_direction, 1u, "sun");
    result.moon_point = find_interior(transformed, add(base_anchor, {6.f, 3.f, 0.f}),
                                      moon_direction, 2u, "moon");
    return result;
}

enum class LightChoice { Sun, Moon, Both, Disabled };

struct SceneConfig {
    GameArt* caster_art{};
    GameArt* sprite_art{};
    std::string sprite_id;
    std::array<float, 3> anchor = base_anchor;
    std::array<float, 3> camera{};
    Point sprite_ground{100.f, 95.f};
    float sprite_visual_scale{visual_scale};
    LightChoice light{LightChoice::Sun};
    bool append_casters{};
    bool draw_sprite{};
    bool local{};
    bool emission{};
};

Frame2D scene(const ReceiverFixture& fixture, const SceneConfig& config) {
    Frame2D frame;
    frame.logical_size = logical_size;
    frame.clear = {8, 12, 18, 255};
    frame.linear_hdr_world = true;
    frame.world_exposure = .72f;
    frame.contact_projection = projection;
    frame.surface_lighting.ambient = config.light == LightChoice::Disabled
        ? std::array<float, 4>{.15f, .15f, .15f, 0.f}
        : std::array<float, 4>{.04f, .04f, .04f, 0.f};
    frame.surface_lighting.sun_direction = {sun_direction[0], sun_direction[1],
                                            sun_direction[2], 0.f};
    frame.surface_lighting.sun_color_intensity = {1.f, .8f, .55f,
        (config.light == LightChoice::Sun || config.light == LightChoice::Both) ? .8f : 0.f};
    MoonLightingGpu moon;
    moon.direction = {moon_direction[0], moon_direction[1], moon_direction[2], 0.f};
    moon.color_intensity = {.55f, .68f, 1.f,
        (config.light == LightChoice::Moon || config.light == LightChoice::Both) ? .8f : 0.f};
    frame.moon_lighting = moon;
    if (config.local) {
        frame.surface_lighting.counts[0] = 1u;
        frame.surface_lighting.lights[0].anchor_radius = {0.f, 0.f, 3.f, 14.f};
        frame.surface_lighting.lights[0].color_intensity = {1.f, .35f, .12f, 1.2f};
    }
    append_ground_receiver(frame, fixture.ground,
                           {ground_receiver_center, {1, 0, 0}, {0, 1, 0},
                            ground_receiver_extent_x, ground_receiver_extent_y});
    append_typed_receiver(frame, fixture.flat, {65, 365, 100, 72}, fixture.sun_point, false);
    append_typed_receiver(frame, config.emission ? fixture.emissive : fixture.flat,
                          {475, 365, 100, 72}, fixture.moon_point, config.emission);
    if (config.draw_sprite) {
        require(config.sprite_art != nullptr, "Sprite draw requires a loaded GameArt registry");
        require(frame.vertices.size() == frame.surface_vertices.size(),
                "Authored sprite fixture requires aligned vertex/surface streams before append");
        const auto vertex_count_before_sprite = frame.vertices.size();
        const auto surface_count_before_sprite = frame.surface_vertices.size();
        const auto render_anchor = camera_relative_render_xyz(config.anchor, config.camera);
        config.sprite_art->lit_sprite(frame, config.sprite_id, config.sprite_ground,
                                      render_anchor, 1.f, config.sprite_visual_scale);
        require(frame.vertices.size() == vertex_count_before_sprite + 4u,
                "Authored sprite fixture expected lit_sprite to append exactly four vertices");
        require(frame.surface_vertices.size() == surface_count_before_sprite,
                "Authored sprite fixture expected lit_sprite to leave surface companions to its caller");
        frame.surface_vertices.insert(frame.surface_vertices.end(), 4u, {});
    }
    if (config.append_casters) {
        require(config.caster_art != nullptr, "Caster append requires a loaded GameArt registry");
        require(config.caster_art->append_lit_sky_casters(
                    frame, config.sprite_id, config.anchor, caster_scale, config.camera) == 6u,
                "Authored native adapter did not append six triangles");
    }
    append_ui(frame);
    require(frame.vertices.size() == frame.surface_vertices.size(),
            "Authored scene fixture ended with misaligned vertex/surface streams");
    return frame;
}

struct SpriteDisplayMetadata {
    std::array<float, 2> source_size{};
    std::array<float, 2> pivot{};
    float export_scale{};
};

SpriteDisplayMetadata sprite_metadata(const Json& world) {
    const auto& sprite = world.at("lit_sprites").at(0);
    return {sprite.at("source_size").get<std::array<float, 2>>(),
            sprite.at("pivot_px").get<std::array<float, 2>>(),
            sprite.at("export_pixels_per_reference_pixel").get<float>()};
}

Rect sprite_bounds(const SpriteDisplayMetadata& metadata, Point ground, float scale) {
    const float pixel_scale = scale / metadata.export_scale;
    return {ground.x - metadata.pivot[0] * pixel_scale,
            ground.y - metadata.pivot[1] * pixel_scale,
            metadata.source_size[0] * pixel_scale,
            metadata.source_size[1] * pixel_scale};
}

Point sprite_ground_for_top_left(const SpriteDisplayMetadata& metadata, Point top_left,
                                 float scale) {
    const float pixel_scale = scale / metadata.export_scale;
    return {top_left.x + metadata.pivot[0] * pixel_scale,
            top_left.y + metadata.pivot[1] * pixel_scale};
}

bool rects_overlap(Rect left, Rect right) {
    return left.x < right.x + right.w && right.x < left.x + left.w &&
           left.y < right.y + right.h && right.y < left.y + left.h;
}

bool rect_contains(Rect rectangle, Point point) {
    return point.x >= rectangle.x && point.x < rectangle.x + rectangle.w &&
           point.y >= rectangle.y && point.y < rectangle.y + rectangle.h;
}

bool logical_point_visible(Point point) {
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           point.x >= 0.f && point.x < logical_size.width &&
           point.y >= 0.f && point.y < logical_size.height;
}

bool logical_rect_visible(Rect rectangle) {
    return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
           std::isfinite(rectangle.w) && std::isfinite(rectangle.h) &&
           rectangle.x >= 0.f && rectangle.y >= 0.f &&
           rectangle.w > 0.f && rectangle.h > 0.f &&
           rectangle.x + rectangle.w <= logical_size.width &&
           rectangle.y + rectangle.h <= logical_size.height;
}

struct DiffStats {
    std::uint32_t changed{}, darkened{};
    int maximum_darkening{};
    double centroid_x{}, centroid_y{};
};

DiffStats difference(const Image& open, const Image& shadowed, Rect roi) {
    double weighted_x{}, weighted_y{}, total_weight{};
    DiffStats result;
    const int left = static_cast<int>(std::ceil(roi.x));
    const int top = static_cast<int>(std::ceil(roi.y));
    const int right = static_cast<int>(std::floor(roi.x + roi.w));
    const int bottom = static_cast<int>(std::floor(roi.y + roi.h));
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) {
            const auto a = logical_pixel(open, {float(x), float(y)});
            const auto b = logical_pixel(shadowed, {float(x), float(y)});
            if (a != b) ++result.changed;
            const int delta = brightness(a) - brightness(b);
            if (delta > 3) {
                ++result.darkened;
                result.maximum_darkening = std::max(result.maximum_darkening, delta);
                weighted_x += double(x) * delta; weighted_y += double(y) * delta;
                total_weight += delta;
            }
        }
    if (total_weight > 0.) {
        result.centroid_x = weighted_x / total_weight;
        result.centroid_y = weighted_y / total_weight;
    }
    return result;
}

Json diff_json(const DiffStats& value) {
    return {{"changed", value.changed}, {"darkened", value.darkened},
            {"maximum_darkening", value.maximum_darkening},
            {"centroid", {value.centroid_x, value.centroid_y}}};
}

bool roi_equal(const Image& left, const Image& right, Rect roi) {
    const int x0 = static_cast<int>(std::ceil(roi.x));
    const int y0 = static_cast<int>(std::ceil(roi.y));
    const int x1 = static_cast<int>(std::floor(roi.x + roi.w));
    const int y1 = static_cast<int>(std::floor(roi.y + roi.h));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (logical_pixel(left, {float(x), float(y)}) !=
                logical_pixel(right, {float(x), float(y)})) return false;
    return true;
}

void require_shadow(const Captured& open, const Captured& shadowed, Point point,
                    std::string_view label) {
    const int a = brightness(logical_pixel(open.image, point));
    const int b = brightness(logical_pixel(shadowed.image, point));
    require(a > 64 && b + 24 < a, std::string(label) + " was not causally shadowed");
}

Json triangle_rows_json(const Frame2D& frame) {
    Json result = Json::array();
    for (const auto& triangle : frame.sky_shadow_triangles) {
        Json vertices = Json::array();
        for (const auto& vertex : triangle.vertices) vertices.push_back(vertex.position);
        result.push_back({{"vertices", std::move(vertices)}, {"mask", triangle.source_mask},
                          {"reveal", triangle.reveal}});
    }
    return result;
}

Json authored_triangles_json(const std::vector<AuthoredTriangle>& triangles) {
    Json result = Json::array();
    for (const auto& triangle : triangles)
        result.push_back({{"id", triangle.id}, {"vertices", triangle.vertices},
                          {"source_mask", triangle.mask}});
    return result;
}

bool same_caster_rows(const Frame2D& left, const Frame2D& right) {
    return left.sky_shadow_triangles.size() == right.sky_shadow_triangles.size() &&
        (left.sky_shadow_triangles.empty() ||
         std::memcmp(left.sky_shadow_triangles.data(), right.sky_shadow_triangles.data(),
                     left.sky_shadow_triangles.size() * sizeof(SkyShadowTriangle)) == 0);
}

template<class T>
bool same_prefix(const std::vector<T>& left, const std::vector<T>& right, std::size_t count) {
    return left.size() >= count && right.size() >= count &&
           (count == 0u || std::memcmp(left.data(), right.data(), count * sizeof(T)) == 0);
}

bool same_independent_receiver_rows(const Frame2D& left, const Frame2D& right) {
    if (!same_prefix(left.vertices, right.vertices, 12u) ||
        !same_prefix(left.surface_vertices, right.surface_vertices, 12u) ||
        !same_prefix(left.indices, right.indices, 18u) ||
        left.ground_material_draws.empty() || right.ground_material_draws.empty() ||
        left.sprite_material_draws.size() < 2u || right.sprite_material_draws.size() < 2u)
        return false;
    const auto& ground_left = left.ground_material_draws[0];
    const auto& ground_right = right.ground_material_draws[0];
    if (!(ground_left.albedo == ground_right.albedo) ||
        ground_left.surface.response != ground_right.surface.response ||
        ground_left.surface.flags != ground_right.surface.flags ||
        ground_left.surface.visibility != ground_right.surface.visibility ||
        ground_left.contact_receiver != ground_right.contact_receiver)
        return false;
    for (std::size_t index = 0; index < 2u; ++index) {
        const auto& sprite_left = left.sprite_material_draws[index];
        const auto& sprite_right = right.sprite_material_draws[index];
        if (!(sprite_left.material == sprite_right.material) ||
            sprite_left.surface.uv_regions != sprite_right.surface.uv_regions ||
            sprite_left.surface.anchor_export != sprite_right.surface.anchor_export ||
            sprite_left.surface.source_size_pivot != sprite_right.surface.source_size_pivot ||
            sprite_left.surface.depth_scale != sprite_right.surface.depth_scale ||
            sprite_left.surface.emission != sprite_right.surface.emission ||
            sprite_left.surface.response != sprite_right.surface.response ||
            sprite_left.reveal != sprite_right.reveal ||
            sprite_left.receiver_mask != sprite_right.receiver_mask ||
            sprite_left.receiver_owner != sprite_right.receiver_owner)
            return false;
    }
    return true;
}

struct VariantSet {
    KitVariant mask1, mask2, mask3, raster0, raster255, vertex;
};

VariantSet create_variants(const fs::path& exported, Reporter& report) {
    const auto set_mask = [](std::uint32_t mask) {
        return [mask](Json& world) {
            for (auto& sprite : world.at("lit_sprites"))
                for (auto& triangle : sprite.at("sky_shadow_triangles"))
                    triangle["source_mask"] = mask;
        };
    };
    VariantSet result{
        make_variant(exported, report.output, "all-source-mask-1", set_mask(1u)),
        make_variant(exported, report.output, "all-source-mask-2", set_mask(2u)),
        make_variant(exported, report.output, "all-source-mask-3", set_mask(3u)),
        make_variant(exported, report.output, "raster-caster-mask-0", [](Json&) {}, 0),
        make_variant(exported, report.output, "raster-caster-mask-255", [](Json&) {}, 255),
        make_variant(exported, report.output, "one-authored-vertex-moved", [](Json& world) {
            for (auto& sprite : world.at("lit_sprites"))
                sprite.at("sky_shadow_triangles").at(0).at("vertices").at(0).at(1) = -1.25f;
        })};
    const std::array<std::pair<std::string_view, const KitVariant*>, 6> rows{{
        {"mask1", &result.mask1}, {"mask2", &result.mask2}, {"mask3", &result.mask3},
        {"raster0", &result.raster0}, {"raster255", &result.raster255},
        {"vertex", &result.vertex}}};
    for (const auto& [name, variant] : rows)
        report.variants.push_back({{"name", name}, {"root", to_utf8(variant->root)},
                                   {"fixture_kind", "full-export-raster-real-parser-adapter"},
                                   {"logical_bytes", logical_tree_bytes(variant->root)},
                                   {"world_sha256", variant->world_hash},
                                   {"manifest_sha256", variant->manifest_hash},
                                   {"descriptor_sha256", variant->descriptor_hash}});
    report.pass("write-once-derived-kits", {{"count", rows.size()}, {"variants", report.variants}});
    return result;
}

std::unique_ptr<GameArt> load_art(Renderer2D& renderer, const fs::path& root) {
    auto art = std::make_unique<GameArt>(renderer);
    art->load(AssetCatalog(root));
    return art;
}

Json shadow_status_json(const SkyShadowStatus& status) {
    return {{"available", status.available}, {"d32_supported", status.d32_supported},
            {"map_size", status.map_size}, {"frame_sets", status.frame_sets},
            {"attachment_bytes", status.attachment_bytes},
            {"creation_attempts", status.creation_attempts},
            {"target_generation", status.target_generation},
            {"candidate_live_textures", status.candidate_live_textures},
            {"active_set_ids", status.active_set_ids}};
}

Json target_rollback(SDL_Window* window, const fs::path& runtime_assets,
                     const fs::path& exported, const Json& exported_world,
                     Reporter& report) {
    RendererOptions failing;
    failing.backend = Backend::Direct3D12;
    failing.world_graph = WorldGraph::SkyShadowSlice;
    failing.collect_telemetry = true;
    failing.test_fail_sky_target_creation_at = 3u;
    Renderer2D renderer(window, runtime_assets, failing);
    renderer.set_logical_size(logical_size);
    const auto local = descriptor_triangles(exported_world);
    const auto receivers = create_receivers(renderer, local);
    const auto sprite_id = exported_world.at("lit_sprites").at(0).at("id").get<std::string>();
    auto art = load_art(renderer, exported);
    SceneConfig shadow;
    shadow.sprite_id = sprite_id;
    shadow.caster_art = art.get();
    shadow.append_casters = true;
    const auto shadow_frame = scene(receivers, shadow);
    SceneConfig open;
    open.sprite_id = sprite_id;
    const auto open_frame = scene(receivers, open);
    const auto before = renderer.sky_shadow_status();
    require(!renderer.has_submitted_frames() && !renderer.frame_telemetry().submitted,
            "Sky target rollback did not begin before first submission");
    std::string diagnostic;
    const auto rejected_path = report.output / "target-fault-no-capture.png";
    require(!fs::exists(rejected_path), "Target-fault capture path already exists");
    require_output_budget(projected_capture_bytes, "injected target-fault capture");
    try {
        renderer.render(shadow_frame, rejected_path);
    }
    catch (const std::exception& error) { diagnostic = error.what(); }
    const auto failed = renderer.sky_shadow_status();
    require(!diagnostic.empty() && !fs::exists(rejected_path) &&
                failed.creation_attempts == 3u &&
                failed.target_generation == before.target_generation &&
                failed.active_set_ids == before.active_set_ids &&
                failed.candidate_live_textures == 0u &&
                !renderer.has_submitted_frames() && !renderer.frame_telemetry().submitted,
            "Injected lazy sky-target failure did not roll back before submission");
    const auto recovered = capture(renderer, shadow_frame, report, "target-retry-authored-shadow");
    const auto recovered_open = capture(renderer, open_frame, report, "target-retry-authored-open");
    const auto after = renderer.sky_shadow_status();
    const auto sample_array = contact_project_logical(receivers.sun_point, projection);
    const Point sample{sample_array[0], sample_array[1]};
    require_shadow(recovered_open, recovered, sample, "Recovered authored target");
    require(after.available && after.d32_supported && after.frame_sets == 2u &&
                after.map_size == 1024u && after.attachment_bytes == 4ull * 1024ull * 1024ull * 4ull &&
                after.target_generation == 1u && after.creation_attempts == 7u &&
                after.candidate_live_textures == 0u && after.active_set_ids[0] != 0u &&
                after.active_set_ids[0] != after.active_set_ids[1],
            "Same-renderer sky target retry did not publish exactly four depth maps");
    return {{"failure_diagnostic", diagnostic}, {"before", shadow_status_json(before)},
            {"failed", shadow_status_json(failed)}, {"after", shadow_status_json(after)},
            {"failure_submitted", false}, {"rejected_capture_absent", true},
            {"retry_causal_sample", {sample.x, sample.y}},
            {"retry_open", pixel_json(logical_pixel(recovered_open.image, sample))},
            {"retry_shadow", pixel_json(logical_pixel(recovered.image, sample))}};
}

struct MembershipFlip {
    std::array<float, 3> receiver{};
    RayHit before, after;
    float before_outside_margin{}, after_outside_margin{};
};

MembershipFlip find_geometry_flip(const std::vector<AuthoredTriangle>& baseline,
                                  const std::vector<AuthoredTriangle>& changed) {
    MembershipFlip best;
    float best_margin{};
    for (int yi = -50; yi <= 50; ++yi)
        for (int xi = -100; xi <= 120; ++xi) {
            const std::array<float, 3> candidate{float(xi) * .1f, float(yi) * .1f, 0.f};
            const auto point = contact_project_logical(candidate, projection);
            if (!visible_ground_receiver_point(candidate) ||
                point[0] < 110.f || point[0] > 530.f ||
                point[1] < 110.f || point[1] > 335.f)
                continue;
            const auto before = nearest_hit(baseline, candidate, sun_direction, 1u);
            const auto after = nearest_hit(changed, candidate, sun_direction, 1u);
            if (before.hit == after.hit) continue;
            const float before_outside = before.hit ? 0.f :
                outside_margin(baseline, candidate, sun_direction, 1u);
            const float after_outside = after.hit ? 0.f :
                outside_margin(changed, candidate, sun_direction, 1u);
            const float margin = std::min(before.hit ? before.edge_margin_texels : before_outside,
                                          after.hit ? after.edge_margin_texels : after_outside);
            if (margin > best_margin) {
                best = {candidate, before, after, before_outside, after_outside};
                best_margin = margin;
            }
        }
    require(best_margin >= 5.f, "Moved authored vertex produced no five-texel causal membership flip");
    return best;
}


struct MovementOracle {
    std::array<float, 3> old_receiver{}, new_receiver{};
    RayHit old_before, old_after, new_before, new_after;
    float old_after_outside{}, new_before_outside{};
};

MovementOracle find_movement_oracle(const std::vector<AuthoredTriangle>& before,
                                    const std::vector<AuthoredTriangle>& after,
                                    const std::array<float, 3>& expected_delta) {
    MovementOracle best;
    float best_margin{};
    for (int yi = -100; yi <= 100; ++yi)
        for (int xi = -200; xi <= 240; ++xi) {
            const std::array<float, 3> old_point{float(xi) * .05f, float(yi) * .05f, 0.f};
            const auto new_point = add(old_point, expected_delta);
            if (!visible_ground_receiver_point(old_point) ||
                !visible_ground_receiver_point(new_point)) continue;
            const auto old_before = nearest_hit(before, old_point, sun_direction, 1u);
            const auto old_after = nearest_hit(after, old_point, sun_direction, 1u);
            const auto new_before = nearest_hit(before, new_point, sun_direction, 1u);
            const auto new_after = nearest_hit(after, new_point, sun_direction, 1u);
            if (!old_before.hit || old_after.hit || new_before.hit || !new_after.hit) continue;
            const float old_after_outside = outside_margin(after, old_point, sun_direction, 1u);
            const float new_before_outside = outside_margin(before, new_point, sun_direction, 1u);
            const float margin = std::min({old_before.edge_margin_texels, old_after_outside,
                                           new_before_outside, new_after.edge_margin_texels});
            if (margin > best_margin) {
                best = {old_point, new_point, old_before, old_after, new_before, new_after,
                        old_after_outside, new_before_outside};
                best_margin = margin;
            }
        }
    require(best_margin >= 5.f, "No five-texel CPU movement oracle crosses the authored shadow");
    return best;
}

Json movement_json(const MovementOracle& value) {
    return {{"old_receiver", value.old_receiver}, {"new_receiver", value.new_receiver},
            {"old_before", hit_json(value.old_before)}, {"old_after", hit_json(value.old_after)},
            {"new_before", hit_json(value.new_before)}, {"new_after", hit_json(value.new_after)},
            {"old_after_outside_margin_texels", value.old_after_outside},
            {"new_before_outside_margin_texels", value.new_before_outside}};
}

Json production(SDL_Window* window, const fs::path& runtime_assets, const fs::path& exported,
                const Json& exported_world, const VariantSet& variants, Reporter& report) {
    RendererOptions options;
    options.backend = Backend::Direct3D12;
    options.world_graph = WorldGraph::SkyShadowSlice;
    options.collect_telemetry = true;
    Renderer2D renderer(window, runtime_assets, options);
    renderer.set_logical_size(logical_size);
    require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
            "Authored native proof requires Direct3D 12");
    const auto base_local = descriptor_triangles(exported_world);
    const Json mask3_world = Json::parse(read_text(variants.mask3.root / "art/world.json"));
    const auto mask3_local = descriptor_triangles(mask3_world);
    require(base_local.size() == 6u, "Authored native proof requires six exported triangles");
    const auto receivers = create_receivers(renderer, base_local);
    const auto sprite_id = exported_world.at("lit_sprites").at(0).at("id").get<std::string>();
    const auto display = sprite_metadata(exported_world);

    // Every registry is loaded before the first frame submission. GameArt's
    // schema-2 lifetime contract intentionally rejects startup reloads later.
    auto base_art = load_art(renderer, exported);
    auto mask1_art = load_art(renderer, variants.mask1.root);
    auto mask2_art = load_art(renderer, variants.mask2.root);
    auto mask3_art = load_art(renderer, variants.mask3.root);
    auto raster0_art = load_art(renderer, variants.raster0.root);
    auto raster255_art = load_art(renderer, variants.raster255.root);
    auto vertex_art = load_art(renderer, variants.vertex.root);
    require(!renderer.has_submitted_frames(), "GameArt registries were not all loaded before rendering");

    const Rect ground_roi = visible_ground_roi;
    const Point typed_sun{115.f, 401.f};
    const Point typed_moon{525.f, 401.f};
    const auto sun_ground_array = contact_project_logical(receivers.sun_point, projection);
    const auto moon_ground_array = contact_project_logical(receivers.moon_point, projection);
    const Point sun_ground{sun_ground_array[0], sun_ground_array[1]};
    const Point moon_ground{moon_ground_array[0], moon_ground_array[1]};
    const auto transformed_base = transformed_triangles(base_local, base_anchor, caster_scale, {0, 0, 0});
    const auto transformed_mask3 = transformed_triangles(mask3_local, base_anchor,
                                                         caster_scale, {0, 0, 0});
    const auto sun_hit = nearest_hit(transformed_base, receivers.sun_point, sun_direction, 1u);
    const auto moon_hit = nearest_hit(transformed_base, receivers.moon_point, moon_direction, 2u);
    report.diagnostics["visible_samples"]["sun_interior"] =
        {{"receiver", receivers.sun_point}, {"projected", {sun_ground.x, sun_ground.y}},
         {"cpu_hit", hit_json(sun_hit)}};
    report.diagnostics["visible_samples"]["moon_interior"] =
        {{"receiver", receivers.moon_point}, {"projected", {moon_ground.x, moon_ground.y}},
         {"cpu_hit", hit_json(moon_hit)}};
    require(sun_hit.hit && moon_hit.hit && sun_hit.edge_margin_texels >= 5.f &&
                moon_hit.edge_margin_texels >= 5.f,
            "Selected authored receiver points are not PCF-safe CPU hits");

    SceneConfig open_config;
    open_config.sprite_id = sprite_id;
    open_config.light = LightChoice::Sun;
    SceneConfig sun_mask1 = open_config;
    sun_mask1.caster_art = mask1_art.get(); sun_mask1.append_casters = true;
    const auto frame_a = scene(receivers, open_config);
    const auto frame_b = scene(receivers, sun_mask1);
    const auto a0 = capture(renderer, frame_a, report, "matrix-a0-sun-open");
    const auto b1 = capture(renderer, frame_b, report, "matrix-b1-sun-mask1");
    const auto b0 = capture(renderer, frame_b, report, "matrix-b0-sun-mask1-repeat");
    const auto a1 = capture(renderer, frame_a, report, "matrix-a1-sun-open-repeat");
    report.diagnostics["visible_samples"]["sun_interior"] =
        visible_receiver_diagnostic(receivers.sun_point, sun_ground,
                                    sun_hit.edge_margin_texels, a0, b1, typed_sun);
    require(a0.telemetry.frame_slot == 0u && b1.telemetry.frame_slot == 1u &&
                b0.telemetry.frame_slot == 0u && a1.telemetry.frame_slot == 1u,
            "Authored A/B/B/A sequence did not alternate the two frame sets");
    require(a0.file_bytes == a1.file_bytes && b1.file_bytes == b0.file_bytes,
            "Authored same-state captures were not byte-exact across frame slots");
    const auto sun_difference = difference(a0.image, b1.image, ground_roi);
    require(sun_difference.darkened > 20u && sun_difference.maximum_darkening > 24,
            "Authored sun caster did not produce a nonvacuous nonrectangular ground shadow");
    require_shadow(a0, b1, sun_ground, "Authored sun ground interior");
    require_shadow(a0, b1, typed_sun, "Authored sun typed receiver");
    report.pass("01-authored-mask1-ground-typed-and-slots",
                {{"slots", {0, 1, 0, 1}}, {"repeat_exact", true},
                 {"ground_difference", diff_json(sun_difference)},
                 {"ground_sample", pixel_json(logical_pixel(b1.image, sun_ground))},
                 {"typed_sample", pixel_json(logical_pixel(b1.image, typed_sun))},
                 {"cpu_hit", hit_json(sun_hit)}});

    SceneConfig moon_open_config = open_config;
    moon_open_config.light = LightChoice::Moon;
    SceneConfig moon_mask1 = moon_open_config;
    moon_mask1.caster_art = mask1_art.get(); moon_mask1.append_casters = true;
    const auto moon_open = capture(renderer, scene(receivers, moon_open_config), report, "moon-open");
    const auto moon_with_mask1 = capture(renderer, scene(receivers, moon_mask1), report, "moon-mask1-isolated");
    require(moon_open.file_bytes == moon_with_mask1.file_bytes,
            "Sun-only authored mask attenuated the moon source");
    SceneConfig sun_mask2 = open_config;
    sun_mask2.caster_art = mask2_art.get(); sun_mask2.append_casters = true;
    const auto sun_with_mask2 = capture(renderer, scene(receivers, sun_mask2), report, "sun-mask2-isolated");
    require(a0.file_bytes == sun_with_mask2.file_bytes,
            "Moon-only authored mask attenuated the sun source");
    SceneConfig moon_mask2 = moon_open_config;
    moon_mask2.caster_art = mask2_art.get(); moon_mask2.append_casters = true;
    const auto moon_shadow = capture(renderer, scene(receivers, moon_mask2), report, "moon-mask2-shadow");
    const auto moon_difference = difference(moon_open.image, moon_shadow.image, ground_roi);
    report.diagnostics["visible_samples"]["moon_interior"] =
        visible_receiver_diagnostic(receivers.moon_point, moon_ground,
                                    moon_hit.edge_margin_texels, moon_open,
                                    moon_shadow, typed_moon);
    require(moon_difference.darkened > 20u && moon_difference.maximum_darkening > 24,
            "Authored moon caster did not produce a nonvacuous ground shadow");
    require_shadow(moon_open, moon_shadow, moon_ground, "Authored moon ground interior");
    require_shadow(moon_open, moon_shadow, typed_moon, "Authored moon typed receiver");
    require(moon_difference.centroid_x > sun_difference.centroid_x + 30.,
            "Opposite authored sun/moon directions did not separate shadow centroids");
    const auto sun_outside = find_outside(transformed_base,
        add(base_anchor, {2.5f, .8f, 0.f}), sun_direction, 1u);
    const auto moon_outside = find_outside(transformed_base,
        add(base_anchor, {8.5f, .8f, 0.f}), moon_direction, 2u);
    auto outside_receivers = receivers;
    outside_receivers.sun_point = sun_outside;
    outside_receivers.moon_point = moon_outside;
    const auto sun_outside_screen_array = contact_project_logical(sun_outside, projection);
    const auto moon_outside_screen_array = contact_project_logical(moon_outside, projection);
    const Point sun_outside_screen{sun_outside_screen_array[0], sun_outside_screen_array[1]};
    const Point moon_outside_screen{moon_outside_screen_array[0], moon_outside_screen_array[1]};
    const auto sun_outside_open = capture(renderer, scene(outside_receivers, open_config), report,
                                          "sun-outside-open");
    const auto sun_outside_shadow = capture(renderer, scene(outside_receivers, sun_mask1), report,
                                            "sun-outside-mask1");
    const float sun_outside_margin = outside_margin(transformed_base, sun_outside,
                                                     sun_direction, 1u);
    report.diagnostics["visible_samples"]["sun_outside"] =
        visible_receiver_diagnostic(sun_outside, sun_outside_screen,
                                    sun_outside_margin, sun_outside_open,
                                    sun_outside_shadow, typed_sun);
    const auto sun_outside_ground_open = logical_pixel(sun_outside_open.image,
                                                       sun_outside_screen);
    const auto sun_outside_ground_shadow = logical_pixel(sun_outside_shadow.image,
                                                         sun_outside_screen);
    const auto sun_outside_typed_open = logical_pixel(sun_outside_open.image, typed_sun);
    const auto sun_outside_typed_shadow = logical_pixel(sun_outside_shadow.image, typed_sun);
    require(sun_outside_ground_open == sun_outside_ground_shadow &&
                sun_outside_typed_open == sun_outside_typed_shadow,
            "PCF-safe sun exterior changed on ground or typed receiver");
    const auto moon_outside_open = capture(renderer, scene(outside_receivers, moon_open_config), report,
                                           "moon-outside-open");
    const auto moon_outside_shadow = capture(renderer, scene(outside_receivers, moon_mask2), report,
                                             "moon-outside-mask2");
    const float moon_outside_margin = outside_margin(transformed_base, moon_outside,
                                                      moon_direction, 2u);
    report.diagnostics["visible_samples"]["moon_outside"] =
        visible_receiver_diagnostic(moon_outside, moon_outside_screen,
                                    moon_outside_margin, moon_outside_open,
                                    moon_outside_shadow, typed_moon);
    const auto moon_outside_ground_open = logical_pixel(moon_outside_open.image,
                                                        moon_outside_screen);
    const auto moon_outside_ground_shadow = logical_pixel(moon_outside_shadow.image,
                                                          moon_outside_screen);
    const auto moon_outside_typed_open = logical_pixel(moon_outside_open.image, typed_moon);
    const auto moon_outside_typed_shadow = logical_pixel(moon_outside_shadow.image, typed_moon);
    require(moon_outside_ground_open == moon_outside_ground_shadow &&
                moon_outside_typed_open == moon_outside_typed_shadow,
            "PCF-safe moon exterior changed on ground or typed receiver");
    report.pass("09-source-isolation-opposite-directions-ui",
                {{"mask1_moon_exact", true}, {"mask2_sun_exact", true},
                 {"sun_centroid_x", sun_difference.centroid_x},
                 {"moon_centroid_x", moon_difference.centroid_x},
                  {"moon_difference", diff_json(moon_difference)},
                  {"moon_cpu_hit", hit_json(moon_hit)},
                  {"sun_outside", {{"receiver", sun_outside},
                                   {"projected", {sun_outside_screen.x, sun_outside_screen.y}},
                                   {"margin_texels", sun_outside_margin}}},
                  {"moon_outside", {{"receiver", moon_outside},
                                    {"projected", {moon_outside_screen.x, moon_outside_screen.y}},
                                    {"margin_texels", moon_outside_margin}}},
                  {"inside_outside_ground_and_typed", true}, {"ui_exact", true}});

    SceneConfig sun_mask3 = open_config;
    sun_mask3.caster_art = mask3_art.get(); sun_mask3.append_casters = true;
    SceneConfig moon_mask3 = moon_open_config;
    moon_mask3.caster_art = mask3_art.get(); moon_mask3.append_casters = true;
    const auto shared_sun = capture(renderer, scene(receivers, sun_mask3), report, "shared-mask3-sun");
    const auto shared_moon = capture(renderer, scene(receivers, moon_mask3), report, "shared-mask3-moon");
    require(difference(a0.image, shared_sun.image, ground_roi).darkened > 20u &&
                difference(moon_open.image, shared_moon.image, ground_roi).darkened > 20u,
            "Shared authored mask did not affect both independent sky sources");
    report.pass("01-authored-mask3-addresses-both-sources");

    SceneConfig anchor_open = open_config;
    anchor_open.draw_sprite = true; anchor_open.sprite_art = base_art.get();
    anchor_open.sprite_visual_scale = visual_scale;
    const float displayed_width = display.source_size[0] * visual_scale / display.export_scale;
    const float displayed_height = display.source_size[1] * visual_scale / display.export_scale;
    require(visual_scale <= .5f && displayed_width >= 2.f && displayed_height >= 2.f &&
                displayed_width < logical_size.width * .35f &&
                displayed_height + 12.f <= ground_roi.y,
            "Authored sprite display scale is not independently bounded and nonvacuous");
    const Point left_top{12.f, 12.f};
    const Point right_top{logical_size.width - 12.f - displayed_width, 12.f};
    const auto left_ground = sprite_ground_for_top_left(display, left_top, visual_scale);
    const auto right_ground = sprite_ground_for_top_left(display, right_top, visual_scale);
    const auto left_bounds = sprite_bounds(display, left_ground, visual_scale);
    const auto right_bounds = sprite_bounds(display, right_ground, visual_scale);
    require(logical_rect_visible(ground_roi) && logical_rect_visible(left_bounds) &&
                logical_rect_visible(right_bounds),
            "Authored sprite and ground sample rectangles must be fully capture-visible");
    require(!rects_overlap(left_bounds, ground_roi) && !rects_overlap(right_bounds, ground_roi),
            "Metadata-derived sprite bounds overlap the exact ground comparison ROI");
    const std::array<Point, 4> fixed_samples{{sun_ground, moon_ground, typed_sun, typed_moon}};
    for (const auto sample : fixed_samples) {
        require(logical_point_visible(sample), "Authored fixed sample is outside the logical capture");
        require(!rect_contains(left_bounds, sample) && !rect_contains(right_bounds, sample),
                "Metadata-derived sprite bounds overlap a causal receiver sample");
    }
    anchor_open.sprite_ground = left_ground;
    SceneConfig anchor_shadow = anchor_open;
    anchor_shadow.caster_art = mask3_art.get(); anchor_shadow.append_casters = true;
    const std::array<float, 3> anchor_delta{4.f, 0.f, 0.f};
    SceneConfig moved_open = anchor_open; moved_open.anchor = add(base_anchor, anchor_delta);
    SceneConfig moved_shadow = anchor_shadow; moved_shadow.anchor = add(base_anchor, anchor_delta);
    const auto moved_mask3 = transformed_triangles(mask3_local, moved_shadow.anchor,
                                                    caster_scale, moved_shadow.camera);
    const auto anchor_oracle = find_movement_oracle(transformed_mask3, moved_mask3,
                                                     anchor_delta);
    const auto anchor_old_screen_value = contact_project_logical(anchor_oracle.old_receiver, projection);
    const auto anchor_new_screen_value = contact_project_logical(anchor_oracle.new_receiver, projection);
    const Point anchor_old_screen{anchor_old_screen_value[0], anchor_old_screen_value[1]};
    const Point anchor_new_screen{anchor_new_screen_value[0], anchor_new_screen_value[1]};
    require(visible_ground_receiver_point(anchor_oracle.old_receiver) &&
                visible_ground_receiver_point(anchor_oracle.new_receiver) &&
                logical_point_visible(anchor_old_screen) && logical_point_visible(anchor_new_screen),
            "Anchor movement samples must be capture-visible before rendering");
    const auto anchored_open = capture(renderer, scene(receivers, anchor_open), report, "anchor-base-open");
    const auto anchored_shadow = capture(renderer, scene(receivers, anchor_shadow), report, "anchor-base-shadow");
    const auto translated_open = capture(renderer, scene(receivers, moved_open), report, "anchor-moved-open");
    const auto translated_shadow = capture(renderer, scene(receivers, moved_shadow), report, "anchor-moved-shadow");
    const auto anchor_before_frame = scene(receivers, anchor_shadow);
    const auto anchor_after_frame = scene(receivers, moved_shadow);
    require(same_independent_receiver_rows(anchor_before_frame, anchor_after_frame),
            "World-anchor control changed independent ground or typed receiver rows");
    report.diagnostics["visible_samples"]["anchor_movement"] =
        {{"cpu_oracle", movement_json(anchor_oracle)},
         {"old_projected", {anchor_old_screen.x, anchor_old_screen.y}},
         {"new_projected", {anchor_new_screen.x, anchor_new_screen.y}},
         {"old_exact", {{"open", pixel_json(logical_pixel(anchored_open.image, anchor_old_screen))},
                         {"before", pixel_json(logical_pixel(anchored_shadow.image, anchor_old_screen))},
                         {"after", pixel_json(logical_pixel(translated_shadow.image, anchor_old_screen))}}},
         {"new_exact", {{"before", pixel_json(logical_pixel(anchored_shadow.image, anchor_new_screen))},
                         {"open", pixel_json(logical_pixel(translated_open.image, anchor_new_screen))},
                         {"after", pixel_json(logical_pixel(translated_shadow.image, anchor_new_screen))}}}};
    require_shadow(anchored_open, anchored_shadow, anchor_old_screen, "Anchor old footprint");
    require(logical_pixel(translated_open.image, anchor_old_screen) ==
                logical_pixel(translated_shadow.image, anchor_old_screen),
            "Translated authored caster did not release its old CPU-predicted footprint");
    require(logical_pixel(anchored_open.image, anchor_new_screen) ==
                logical_pixel(anchored_shadow.image, anchor_new_screen),
            "Baseline authored caster unexpectedly covered the new CPU-predicted footprint");
    require_shadow(translated_open, translated_shadow, anchor_new_screen, "Anchor new footprint");
    const auto anchored_diff = difference(anchored_open.image, anchored_shadow.image, ground_roi);
    const auto translated_diff = difference(translated_open.image, translated_shadow.image, ground_roi);
    require(anchored_diff.darkened > 20u && translated_diff.darkened > 20u &&
                std::abs(translated_diff.centroid_x - anchored_diff.centroid_x) > 2.,
            "Moving only authored caster world anchor did not move its shadow");
    report.pass("02-world-anchor-moves-shadow-not-sprite-rectangle",
                 {{"sprite_ground", {anchor_open.sprite_ground.x, anchor_open.sprite_ground.y}},
                  {"visual_scale", visual_scale}, {"caster_scale", caster_scale},
                  {"anchor_delta", anchor_delta}, {"before", diff_json(anchored_diff)},
                  {"after", diff_json(translated_diff)}, {"cpu_oracle", movement_json(anchor_oracle)},
                  {"old_screen", {anchor_old_screen.x, anchor_old_screen.y}},
                  {"new_screen", {anchor_new_screen.x, anchor_new_screen.y}},
                  {"exact_samples", {{"old_open", pixel_json(logical_pixel(anchored_open.image, anchor_old_screen))},
                                     {"old_shadow", pixel_json(logical_pixel(anchored_shadow.image, anchor_old_screen))},
                                     {"old_after", pixel_json(logical_pixel(translated_shadow.image, anchor_old_screen))},
                                     {"new_before", pixel_json(logical_pixel(anchored_shadow.image, anchor_new_screen))},
                                     {"new_open", pixel_json(logical_pixel(translated_open.image, anchor_new_screen))},
                                     {"new_shadow", pixel_json(logical_pixel(translated_shadow.image, anchor_new_screen))}}},
                  {"receiver_rows_exact", true}});

    SceneConfig sprite_left = anchor_shadow;
    sprite_left.sprite_ground = left_ground;
    SceneConfig sprite_right = sprite_left;
    sprite_right.sprite_ground = right_ground;
    const auto left_sprite = capture(renderer, scene(receivers, sprite_left), report, "sprite-left-shadow-fixed");
    const auto right_sprite = capture(renderer, scene(receivers, sprite_right), report, "sprite-right-shadow-fixed");
    require(roi_equal(left_sprite.image, right_sprite.image, ground_roi),
            "Moving only the caster-art screen rectangle changed the ground shadow ROI");
    const auto left_frame = scene(receivers, sprite_left);
    const auto right_frame = scene(receivers, sprite_right);
    require(same_caster_rows(left_frame, right_frame) &&
                same_independent_receiver_rows(left_frame, right_frame),
            "Sprite-only move changed caster or independent receiver rows");
    const auto left_sprite_change = difference(right_sprite.image, left_sprite.image, left_bounds);
    const auto right_sprite_change = difference(left_sprite.image, right_sprite.image, right_bounds);
    const auto minimum_sprite_change = std::max<std::uint32_t>(
        4u, static_cast<std::uint32_t>(std::ceil(displayed_width * displayed_height * .25f)));
    require(left_sprite.file_bytes != right_sprite.file_bytes &&
                left_sprite_change.changed >= minimum_sprite_change &&
                right_sprite_change.changed >= minimum_sprite_change,
            "Metadata-derived sprite screen move was visually vacuous");
    report.pass("03-sprite-screen-move-ground-shadow-exact",
                 {{"ground_roi", {ground_roi.x, ground_roi.y, ground_roi.w, ground_roi.h}},
                  {"left_sprite_bounds", {left_bounds.x, left_bounds.y, left_bounds.w, left_bounds.h}},
                  {"right_sprite_bounds", {right_bounds.x, right_bounds.y, right_bounds.w, right_bounds.h}},
                  {"left_sprite_changed_pixels", left_sprite_change.changed},
                  {"right_sprite_changed_pixels", right_sprite_change.changed},
                  {"minimum_sprite_changed_pixels", minimum_sprite_change},
                  {"visual_scale", visual_scale}, {"caster_scale", caster_scale},
                  {"ground_roi_exact", true}, {"caster_rows_exact", true},
                  {"receiver_rows_exact", true}, {"whole_frame_changed", true}});

    const auto no_sprite = capture(renderer, scene(receivers, sun_mask3), report, "authored-shadow-no-sprite");
    require(roi_equal(no_sprite.image, left_sprite.image, ground_roi),
            "Omitting the sprite changed caster-identical ground shadow pixels");
    require(difference(a0.image, no_sprite.image, ground_roi).darkened > 20u,
            "Authored caster without a sprite draw was not causally visible");
    report.pass("06-authored-caster-independent-of-sprite-draw",
                {{"ground_roi_exact_to_sprite_case", true}});

    const auto base_frame = scene(receivers, sun_mask3);
    require(std::all_of(base_frame.sky_shadow_triangles.begin(),
                        base_frame.sky_shadow_triangles.end(),
                        [](const SkyShadowTriangle& row) { return row.reveal == 1.f; }),
            "Authored adapter did not emit exact reveal-one caster rows");
    std::array<float, 3> camera_delta{};
    Frame2D lockstep_frame;
    bool found_lockstep{};
    const std::array<std::array<float, 3>, 8> lockstep_candidates{{
        {{.25f, 0.f, 0.f}}, {{.5f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}},
        {{0.f, .25f, 0.f}}, {{0.f, .5f, 0.f}}, {{0.f, 1.f, 0.f}},
        {{0.f, 0.f, .25f}}, {{0.f, 0.f, .5f}}}};
    for (const auto& candidate : lockstep_candidates) {
        SceneConfig lockstep = sun_mask3;
        lockstep.anchor = add(base_anchor, candidate); lockstep.camera = candidate;
        auto candidate_frame = scene(receivers, lockstep);
        if (same_caster_rows(base_frame, candidate_frame)) {
            camera_delta = candidate;
            lockstep_frame = std::move(candidate_frame);
            found_lockstep = true;
            break;
        }
    }
    require(found_lockstep, "No nonzero camera+anchor delta preserved exact caster rows");
    require(same_caster_rows(base_frame, lockstep_frame),
            "Camera+anchor lockstep did not preserve exact adapted caster rows");
    const auto lockstep_capture = capture(renderer, lockstep_frame, report, "camera-anchor-lockstep");
    require(no_sprite.file_bytes == lockstep_capture.file_bytes,
            "Camera+anchor lockstep changed caster-identical output");
    report.pass("04-camera-anchor-lockstep-byte-exact",
                {{"camera_delta", camera_delta}, {"caster_rows_exact", true},
                 {"capture_exact", true}});

    SceneConfig camera_only = sun_mask3;
    camera_only.camera = {2.f, 0.f, 0.f};
    const auto camera_transformed = transformed_triangles(mask3_local, base_anchor, caster_scale,
                                                           camera_only.camera);
    const auto camera_expected_delta = multiply(camera_only.camera, -1.f);
    const auto camera_oracle = find_movement_oracle(transformed_mask3, camera_transformed,
                                                     camera_expected_delta);
    const auto camera_old_screen_value = contact_project_logical(camera_oracle.old_receiver, projection);
    const auto camera_new_screen_value = contact_project_logical(camera_oracle.new_receiver, projection);
    const Point camera_old_screen{camera_old_screen_value[0], camera_old_screen_value[1]};
    const Point camera_new_screen{camera_new_screen_value[0], camera_new_screen_value[1]};
    require(visible_ground_receiver_point(camera_oracle.old_receiver) &&
                visible_ground_receiver_point(camera_oracle.new_receiver) &&
                logical_point_visible(camera_old_screen) && logical_point_visible(camera_new_screen),
            "Camera movement samples must be capture-visible before rendering");
    const auto camera_frame = scene(receivers, camera_only);
    require(same_independent_receiver_rows(base_frame, camera_frame),
            "Camera-only control changed independent ground or typed receiver rows");
    require(camera_frame.sky_shadow_triangles.size() == base_frame.sky_shadow_triangles.size(),
            "Camera-only control changed triangle count");
    for (std::size_t triangle = 0; triangle < base_frame.sky_shadow_triangles.size(); ++triangle)
        for (std::size_t vertex = 0; vertex < 3; ++vertex)
            require(camera_frame.sky_shadow_triangles[triangle].vertices[vertex].position ==
                        subtract(base_frame.sky_shadow_triangles[triangle].vertices[vertex].position,
                                 camera_only.camera),
                    "Camera-only adapter vertex delta was not exact negative camera translation");
    SceneConfig camera_open_config = open_config;
    camera_open_config.camera = camera_only.camera;
    const auto camera_open_capture = capture(renderer, scene(receivers, camera_open_config), report,
                                             "camera-only-open");
    const auto camera_capture = capture(renderer, camera_frame, report, "camera-only-shadow-moved");
    require(a0.file_bytes == camera_open_capture.file_bytes,
            "Camera-only open control changed fixed receivers without a caster");
    report.diagnostics["visible_samples"]["camera_movement"] =
        {{"cpu_oracle", movement_json(camera_oracle)},
         {"old_projected", {camera_old_screen.x, camera_old_screen.y}},
         {"new_projected", {camera_new_screen.x, camera_new_screen.y}},
         {"old_exact", {{"open", pixel_json(logical_pixel(a0.image, camera_old_screen))},
                         {"before", pixel_json(logical_pixel(no_sprite.image, camera_old_screen))},
                         {"after", pixel_json(logical_pixel(camera_capture.image, camera_old_screen))}}},
         {"new_exact", {{"before", pixel_json(logical_pixel(no_sprite.image, camera_new_screen))},
                         {"open", pixel_json(logical_pixel(camera_open_capture.image, camera_new_screen))},
                         {"after", pixel_json(logical_pixel(camera_capture.image, camera_new_screen))}}}};
    require_shadow(a0, no_sprite, camera_old_screen, "Camera old footprint");
    require(logical_pixel(camera_open_capture.image, camera_old_screen) ==
                logical_pixel(camera_capture.image, camera_old_screen),
            "Camera-only move did not release its old CPU-predicted footprint");
    require(logical_pixel(a0.image, camera_new_screen) ==
                logical_pixel(no_sprite.image, camera_new_screen),
            "Baseline caster unexpectedly covered the camera-only new footprint");
    require_shadow(camera_open_capture, camera_capture, camera_new_screen,
                   "Camera-only new footprint");
    const auto camera_diff = difference(a0.image, camera_capture.image, ground_roi);
    require(camera_diff.darkened > 20u &&
                std::abs(camera_diff.centroid_x - sun_difference.centroid_x) > 2.,
            "Camera-only translation did not move the authored shadow boundary");
    report.pass("05-camera-only-causal-shadow-move",
                 {{"camera_delta", camera_only.camera}, {"vertex_delta_exact", true},
                  {"difference", diff_json(camera_diff)},
                  {"cpu_oracle", movement_json(camera_oracle)},
                  {"old_screen", {camera_old_screen.x, camera_old_screen.y}},
                  {"new_screen", {camera_new_screen.x, camera_new_screen.y}},
                  {"exact_samples", {{"old_open", pixel_json(logical_pixel(a0.image, camera_old_screen))},
                                     {"old_shadow", pixel_json(logical_pixel(no_sprite.image, camera_old_screen))},
                                     {"old_after", pixel_json(logical_pixel(camera_capture.image, camera_old_screen))},
                                     {"new_before", pixel_json(logical_pixel(no_sprite.image, camera_new_screen))},
                                     {"new_open", pixel_json(logical_pixel(camera_open_capture.image, camera_new_screen))},
                                     {"new_shadow", pixel_json(logical_pixel(camera_capture.image, camera_new_screen))}}},
                  {"receiver_rows_exact", true},
                  {"caster_rows", triangle_rows_json(camera_frame)}});

    SceneConfig raster0 = open_config;
    raster0.caster_art = raster0_art.get(); raster0.append_casters = true;
    SceneConfig raster255 = open_config;
    raster255.caster_art = raster255_art.get(); raster255.append_casters = true;
    const auto raster0_frame = scene(receivers, raster0);
    const auto raster255_frame = scene(receivers, raster255);
    require(same_caster_rows(raster0_frame, raster255_frame),
            "Raster-mask variants changed authored caster rows");
    const auto raster0_capture = capture(renderer, raster0_frame, report, "raster-mask-zero");
    const auto raster255_capture = capture(renderer, raster255_frame, report, "raster-mask-255");
    require(raster0_capture.file_bytes == raster255_capture.file_bytes,
            "Raster caster mask bytes changed authored directional shadows");
    report.pass("07-raster-caster-mask-independent",
                {{"caster_rows_exact", true}, {"capture_exact", true}});

    SceneConfig base_geometry = anchor_open;
    base_geometry.caster_art = base_art.get(); base_geometry.append_casters = true;
    SceneConfig moved_geometry = anchor_open;
    moved_geometry.caster_art = vertex_art.get(); moved_geometry.append_casters = true;
    const Json changed_world = Json::parse(read_text(variants.vertex.root / "art/world.json"));
    const auto changed_local = descriptor_triangles(changed_world);
    const auto changed_transformed = transformed_triangles(changed_local, base_anchor, caster_scale, {0, 0, 0});
    const auto flip = find_geometry_flip(transformed_base, changed_transformed);
    const auto flip_screen_array = contact_project_logical(flip.receiver, projection);
    const Point flip_screen{flip_screen_array[0], flip_screen_array[1]};
    const auto mutation_sprite_bounds = sprite_bounds(display, anchor_open.sprite_ground,
                                                       visual_scale);
    require(visible_ground_receiver_point(flip.receiver) && logical_point_visible(flip_screen) &&
                logical_rect_visible(mutation_sprite_bounds),
            "Vertex-mutation samples must be fully capture-visible before rendering");
    const auto base_geometry_capture = capture(renderer, scene(receivers, base_geometry), report,
                                               "geometry-baseline");
    const auto moved_geometry_capture = capture(renderer, scene(receivers, moved_geometry), report,
                                                "geometry-one-vertex-moved");
    report.diagnostics["visible_samples"]["vertex_membership_flip"] =
        {{"receiver", flip.receiver}, {"projected", {flip_screen.x, flip_screen.y}},
         {"before_hit", hit_json(flip.before)}, {"after_hit", hit_json(flip.after)},
         {"before_outside_margin_texels", flip.before_outside_margin},
         {"after_outside_margin_texels", flip.after_outside_margin},
         {"exact", {{"open", pixel_json(logical_pixel(anchored_open.image, flip_screen))},
                     {"before", pixel_json(logical_pixel(base_geometry_capture.image, flip_screen))},
                     {"after", pixel_json(logical_pixel(moved_geometry_capture.image, flip_screen))}}}};
    require(!rect_contains(mutation_sprite_bounds, flip_screen),
            "Vertex-mutation causal sample overlaps the visible sprite rectangle");
    const auto open_flip = logical_pixel(anchored_open.image, flip_screen);
    const auto before_flip = logical_pixel(base_geometry_capture.image, flip_screen);
    const auto after_flip = logical_pixel(moved_geometry_capture.image, flip_screen);
    if (flip.before.hit) {
        require_shadow(anchored_open, base_geometry_capture, flip_screen,
                       "Vertex baseline membership");
        require(after_flip == open_flip,
                "Moved authored vertex did not release its CPU-predicted receiver");
    } else {
        require(before_flip == open_flip,
                "Baseline authored geometry unexpectedly covered the swept receiver");
        require_shadow(anchored_open, moved_geometry_capture, flip_screen,
                       "Vertex moved membership");
    }
    const auto sprite_presence = difference(a0.image, anchored_open.image,
                                            sprite_bounds(display, anchor_open.sprite_ground,
                                                          visual_scale));
    require(sprite_presence.changed >= minimum_sprite_change,
            "Vertex-mutation proof did not retain a nonvacuous visible authored sprite");
    report.pass("08-one-authored-vertex-causal-boundary-change",
                {{"receiver", flip.receiver}, {"screen", {flip_screen.x, flip_screen.y}},
                 {"before_hit", hit_json(flip.before)}, {"after_hit", hit_json(flip.after)},
                 {"before_outside_margin_texels", flip.before_outside_margin},
                 {"after_outside_margin_texels", flip.after_outside_margin},
                 {"open", pixel_json(open_flip)}, {"before", pixel_json(before_flip)},
                 {"after", pixel_json(after_flip)},
                 {"sky_caster_path_has_no_raster_channel_input", true},
                 {"visible_sprite_changed_pixels", sprite_presence.changed},
                 {"sample_outside_sprite_bounds", true},
                 {"visual_scale", visual_scale}, {"caster_scale", caster_scale},
                 {"sprite_ground", {anchor_open.sprite_ground.x, anchor_open.sprite_ground.y}}});

    SceneConfig isolated_open = open_config;
    isolated_open.light = LightChoice::Disabled; isolated_open.local = true; isolated_open.emission = true;
    SceneConfig isolated_caster = isolated_open;
    isolated_caster.caster_art = mask3_art.get(); isolated_caster.append_casters = true;
    const auto isolated_a = capture(renderer, scene(receivers, isolated_open), report, "disabled-sky-local-emission-open");
    const auto isolated_b = capture(renderer, scene(receivers, isolated_caster), report, "disabled-sky-local-emission-caster");
    require(isolated_a.file_bytes == isolated_b.file_bytes,
            "Disabled direct sources let authored casters alter local/emission/UI output");
    require(brightness(logical_pixel(isolated_a.image, sun_ground)) > 48 &&
                brightness(logical_pixel(isolated_a.image, typed_moon)) > 48,
            "Local/emission isolation control was vacuous");
    report.pass("09-disabled-direct-preserves-local-emission-ui",
                {{"capture_exact", true}, {"ui_exact", true}});

    const auto& status = renderer.sky_shadow_status();
    Json authored_ids = Json::array();
    for (const auto& triangle : base_local) authored_ids.push_back(triangle.id);
    Json material_ids = Json::array();
    for (const auto& material : exported_world.at("materials"))
        material_ids.push_back(material.at("id"));
    return {{"device", device_json(renderer.device_info())},
            {"status", shadow_status_json(status)},
            {"authored_sprite_id", sprite_id},
            {"authored_material_ids", material_ids},
            {"authored_local_occluders",
             exported_world.at("lit_sprites").at(0).at("local_occluders")},
            {"authored_local_ids", authored_ids},
            {"authored_local_triangles", authored_triangles_json(base_local)},
            {"base_anchor", base_anchor}, {"caster_scale", caster_scale},
            {"visual_scale", visual_scale}, {"reveal", 1.f},
            {"camera_lockstep_delta", camera_delta},
            {"camera_only_delta", camera_only.camera},
            {"ground_roi", {ground_roi.x, ground_roi.y, ground_roi.w, ground_roi.h}},
            {"sprite_display", {{"source_size", display.source_size}, {"pivot", display.pivot},
                                {"export_pixels_per_reference_pixel", display.export_scale},
                                {"left_bounds", {left_bounds.x, left_bounds.y, left_bounds.w, left_bounds.h}},
                                {"right_bounds", {right_bounds.x, right_bounds.y, right_bounds.w, right_bounds.h}}}},
            {"sun_receiver", receivers.sun_point}, {"moon_receiver", receivers.moon_point},
            {"sun_hit", hit_json(sun_hit)}, {"moon_hit", hit_json(moon_hit)},
            {"sun_outside", {{"receiver", sun_outside},
                             {"projected", {sun_outside_screen.x, sun_outside_screen.y}},
                             {"margin_texels", sun_outside_margin}}},
            {"moon_outside", {{"receiver", moon_outside},
                              {"projected", {moon_outside_screen.x, moon_outside_screen.y}},
                              {"margin_texels", moon_outside_margin}}},
            {"anchor_movement_oracle", movement_json(anchor_oracle)},
            {"camera_movement_oracle", movement_json(camera_oracle)},
            {"vertex_membership_flip", {{"receiver", flip.receiver},
                                         {"before", hit_json(flip.before)},
                                         {"after", hit_json(flip.after)},
                                         {"before_outside_margin_texels", flip.before_outside_margin},
                                         {"after_outside_margin_texels", flip.after_outside_margin}}},
            {"transformed_authored_triangles", triangle_rows_json(base_frame)}};
}

} // namespace authored_sky_native_probe

int main(int argc, char** argv) {
    using namespace authored_sky_native_probe;
    fs::path output;
    std::unique_ptr<Reporter> report;
    try {
        require(argc == 4,
                "Usage: AuthoredSkyNativeProbe <runtime-assets> <exported-kit> <NEW-output>");
        const auto runtime_assets = from_utf8(argv[1]);
        const auto exported = from_utf8(argv[2]);
        output = from_utf8(argv[3]);
        require(fs::is_directory(runtime_assets), "Runtime assets directory is missing");
        require(fs::is_directory(exported), "Exported authored kit directory is missing");
        require(!fs::exists(output), "Authored native probe refuses an existing output root");
        require(!path_is_within(output, runtime_assets) && !path_is_within(output, exported),
                "Native evidence output must be separate from runtime and exported inputs");
        require(fs::create_directories(output), "Could not create authored native output root");
        guarded_output_root = output;
        require_output_budget(0, "new authored native output root");
        report = std::make_unique<Reporter>();
        report->output = output;
        report->checkpoint("output-created-before-derived-kits");

        const AssetCatalog exported_catalog(exported);
        const Json exported_world = Json::parse(read_text(exported / "art/world.json"));
        const auto& descriptor = exported_world.at("lit_sprites").at(0).at("sky_shadow_triangles");
        require(descriptor.is_array() && descriptor.size() == 6u,
                "Exported kit does not contain the six-triangle authored descriptor");
        const auto descriptor_hash = digest(descriptor.dump() + "\n");
        const auto calibration_bytes = exported_catalog.read_path("art/material/calibration.json");
        const Json calibration = Json::parse(calibration_bytes.begin(), calibration_bytes.end());
        require(calibration.at("sky_shadow_triangles_sha256") == descriptor_hash,
                "Exporter calibration hash does not bind the authored descriptor");
        require(calibration.at("master_sha256").get<std::string>().size() == 64u &&
                    calibration.at("exporter_sha256").get<std::string>().size() == 64u,
                "Exporter calibration provenance hashes are missing");
        const auto variants = create_variants(exported, *report);
        require_output_budget(0, "all derived native kits");
        report->checkpoint("derived-kits-complete-before-sdl");

        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Authored Sky Native Probe", 640, 480,
                             SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow);
        require(window != nullptr, "Could not create visible authored native window");
        require(SDL_ShowWindow(window.get()), SDL_GetError());
        require(SDL_SyncWindow(window.get()), "Could not synchronize authored native window");
        pump_events();
        report->checkpoint("visible-window-ready-before-target-fault");
        const auto rollback = target_rollback(window.get(), runtime_assets, exported,
                                              exported_world, *report);
        report->pass("09-sky-target-candidate-rollback", rollback);
        const auto evidence = production(window.get(), runtime_assets, exported,
                                         exported_world, variants, *report);

        std::string source_hash = "unavailable";
        if (fs::is_regular_file(fs::path(__FILE__))) source_hash = file_digest(fs::path(__FILE__));
        std::string executable_hash = "unavailable";
        const auto executable = from_utf8(argv[0]);
        if (fs::is_regular_file(executable)) executable_hash = file_digest(executable);
        const Json final_report{
            {"schema", 1}, {"complete", true}, {"build", RIBTERRA_PROBE_BUILD},
            {"scope", "bounded authored world-triangle adapter into fixed-camera SkyShadowSlice"},
            {"source_sha256", source_hash}, {"executable_sha256", executable_hash},
            {"runtime_shader_manifest_sha256",
             file_digest(runtime_assets / "shaders/manifest.json")},
            {"exported_manifest_sha256", file_digest(exported / "manifest.json")},
            {"exported_world_sha256", file_digest(exported / "art/world.json")},
            {"exported_content_sha256", exported_catalog.content_hash()},
            {"material_master_sha256", calibration.at("master_sha256")},
            {"material_exporter_sha256", calibration.at("exporter_sha256")},
            {"material_source_hash_basis", "exported calibration metadata"},
            {"authored_descriptor_exporter_basis_sha256", descriptor_hash},
            {"native_output_logical_bytes_before_final", logical_tree_bytes(output)},
            {"native_output_limit_bytes", max_native_output_bytes},
            {"minimum_free_bytes", min_free_bytes},
            {"variants", report->variants}, {"target_rollback", rollback},
            {"production", evidence}, {"check_count", report->checks.size()},
            {"checks", report->checks}, {"captures", report->captures},
            {"diagnostics", report->diagnostics},
            {"png_count", report->capture_count},
            {"matrix", {{"1_source_masks_nonrect_ground_typed", "PASS"},
                         {"2_world_anchor_only", "PASS"},
                         {"3_sprite_rectangle_only", "PASS"},
                         {"4_camera_anchor_lockstep", "PASS"},
                         {"5_camera_only", "PASS"},
                         {"6_no_sprite", "PASS"},
                         {"7_raster_mask_independence", "PASS"},
                         {"8_vertex_mutation", "PASS"},
                         {"9_source_slots_ui_rollback", "PASS"}}},
            {"claims", {"real AssetCatalog and GameArt authored-caster path",
                        "native D3D12 SkyShadowSlice PNG causality",
                        "independent ground and typed-material receivers",
                        "source masks, camera/anchor/sprite controls, raster independence",
                        "two frame slots, exact UI and target-candidate rollback"}},
            {"non_claims", {"final category art or concept acceptance", "GameView integration",
                            "camera-following clipmaps or owner filtering", "partial alpha coverage",
                            "full engine or gameplay completion", "performance, GPU timing or 120 FPS",
                            "E01-E17 acceptance"}},
            {"prior_old_graph_note",
             "The frozen prior zero-caster compatibility proof is cited by lineage only and is not rerun here."}};
        write_json_new(output / "authored-sky-native-report.json", final_report);
        std::cout << "Authored sky native probe passed " << report->checks.size()
                  << " checks with " << report->capture_count << " immutable PNGs\n";
        return 0;
    } catch (const std::exception& error) {
        try {
            if (report)
                write_json_new(output / ("authored-sky-native-failure-" +
                                          std::to_string(report->serial++) + ".json"),
                               {{"schema", 1}, {"complete", false},
                                 {"build", RIBTERRA_PROBE_BUILD}, {"error", error.what()},
                                 {"checks", report->checks}, {"captures", report->captures},
                                 {"variants", report->variants},
                                 {"diagnostics", report->diagnostics},
                                {"png_count", report->capture_count}});
        } catch (...) {}
        std::cerr << "Authored sky native probe failed: " << error.what() << '\n';
        return 1;
    }
}

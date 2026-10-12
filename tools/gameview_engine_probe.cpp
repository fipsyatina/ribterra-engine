#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"

#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <png.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace ribterra;
using namespace ribterra::render;
constexpr std::uintmax_t output_limit = 128u * 1024u * 1024u;
constexpr std::uintmax_t minimum_free = 384u * 1024u * 1024u;
constexpr Extent logical_size{1920, 1080};
constexpr std::array<std::array<float,2>,2> control_camera_offsets{{{0.f,0.f},{.25f,-.125f}}};

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
fs::path path_from_utf8(const char* value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
}
std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(stream.good(), "Could not read evidence");
    const auto size = stream.tellg();
    require(size >= 0 && size <= static_cast<std::streamoff>(output_limit), "Evidence exceeds limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "Incomplete evidence read");
    return bytes;
}
Json read_json(const fs::path& path) {
    const auto bytes = read_bytes(path);
    return Json::parse(bytes.begin(), bytes.end());
}
std::string digest(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
    unsigned int length{};
    require(EVP_Digest(bytes.data(), bytes.size(), hash.data(), &length, EVP_sha256(), nullptr) == 1 &&
                length == 32u, "SHA256 failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned int index = 0; index < length; ++index) {
        result.push_back(hex[hash[index] >> 4]);
        result.push_back(hex[hash[index] & 15]);
    }
    return result;
}
void check_budget(const fs::path& output) {
    require(fs::space(output.parent_path()).available >= minimum_free, "Insufficient disk headroom");
    std::uintmax_t total{};
    for (const auto& entry : fs::recursive_directory_iterator(output)) {
        require(!entry.is_symlink(), "Symlink in diagnostic output");
        if (entry.is_regular_file()) {
            const auto bytes = entry.file_size();
            require(bytes <= output_limit - total, "Diagnostic output exceeds 128MiB");
            total += bytes;
        }
    }
}
void write_report(const fs::path& path, const Json& report) {
    require(!fs::exists(path), "Refusing to overwrite diagnostic report");
    std::ofstream stream(path, std::ios::binary);
    stream << report.dump(2) << '\n';
    stream.flush();
    require(stream.good(), "Could not write diagnostic report");
}
std::string geometry_fingerprint(const Frame2D& frame) {
    // Deliberately excludes lighting uniforms and SpriteMaterialDraw::emission_scale:
    // paired source controls must prove radiance-only changes over identical geometry.
    Json rows = Json::array();
    for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
        const auto& vertex = frame.vertices[index];
        const auto& surface = frame.surface_vertices.at(index);
        rows.push_back({vertex.position.x, vertex.position.y, vertex.uv.x, vertex.uv.y,
                        vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a,
                        surface.world_position, surface.normal});
    }
    const auto text = Json{{"vertices", rows}, {"indices", frame.indices},
                           {"ground_count", frame.ground_material_draws.size()},
                           {"sprite_count", frame.sprite_material_draws.size()},
                           {"caster_count", frame.sky_shadow_triangles.size()},
                           {"occluder_count", frame.local_occluders.size()},
                           {"commands", frame.commands.size()}}.dump();
    return digest({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
template<class T>
void append_trivial(std::vector<std::uint8_t>& bytes, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(T));
}
template<class T>
void append_trivial(std::vector<std::uint8_t>& bytes, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (values.empty()) return;
    const auto* first = reinterpret_cast<const std::uint8_t*>(values.data());
    bytes.insert(bytes.end(), first, first + values.size() * sizeof(T));
}
std::string full_frame_fingerprint(const Frame2D& frame) {
    std::vector<std::uint8_t> bytes;
    const auto geometry = geometry_fingerprint(frame);
    bytes.insert(bytes.end(), geometry.begin(), geometry.end());
    append_trivial(bytes, frame.logical_size);
    append_trivial(bytes, frame.clear);
    for (const std::size_t count : {frame.vertices.size(),frame.indices.size(),frame.commands.size(),
            frame.surface_vertices.size(),frame.sprite_material_draws.size(),
            frame.ground_material_draws.size(),frame.local_occluders.size(),
            frame.contact_shadows.size(),frame.sky_shadow_triangles.size()})
        append_trivial(bytes,count);
    for (const auto& command : frame.commands) {
        const auto kind = static_cast<std::uint32_t>(command.index());
        append_trivial(bytes,kind);
        if (const auto* geometry_draw = std::get_if<GeometryDraw>(&command)) {
            append_trivial(bytes,*geometry_draw);
        } else {
            const auto& text_draw = std::get<TextDraw>(command);
            append_trivial(bytes,text_draw.pass);
            append_trivial(bytes,text_draw.position);
            append_trivial(bytes,text_draw.size);
            append_trivial(bytes,text_draw.color);
            append_trivial(bytes,text_draw.max_width);
            const auto text_size = text_draw.text.size();
            append_trivial(bytes,text_size);
            bytes.insert(bytes.end(),text_draw.text.begin(),text_draw.text.end());
        }
    }
    append_trivial(bytes, frame.surface_lighting);
    append_trivial(bytes, frame.sprite_material_draws);
    append_trivial(bytes, frame.ground_material_draws);
    append_trivial(bytes, frame.local_occluders);
    append_trivial(bytes, frame.contact_shadows);
    append_trivial(bytes, frame.sky_shadow_triangles);
    append_trivial(bytes, frame.linear_hdr_world);
    append_trivial(bytes, frame.world_exposure);
    const bool has_contact_projection = frame.contact_projection.has_value();
    append_trivial(bytes,has_contact_projection);
    if (frame.contact_projection) append_trivial(bytes,*frame.contact_projection);
    const bool has_moon = frame.moon_lighting.has_value();
    append_trivial(bytes, has_moon);
    if (frame.moon_lighting) append_trivial(bytes, *frame.moon_lighting);
    return digest(bytes);
}
bool lighting_exact(const SurfaceLightingFrame& left, const SurfaceLightingFrame& right) {
    return std::memcmp(&left,&right,sizeof(SurfaceLightingFrame)) == 0;
}
void append_ui_control(Frame2D& frame) {
    require(frame.vertices.size() == frame.surface_vertices.size(), "Companion stream mismatch before UI");
    const auto base = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first = static_cast<std::uint32_t>(frame.indices.size());
    constexpr Color color{215, 42, 76, 255};
    frame.vertices.insert(frame.vertices.end(), {{{18,18},{0,0},color}, {{70,18},{1,0},color},
                                                {{70,42},{1,1},color}, {{18,42},{0,1},color}});
    frame.surface_vertices.insert(frame.surface_vertices.end(), 4u, {});
    frame.indices.insert(frame.indices.end(), {base,base+1,base+2,base,base+2,base+3});
    GeometryDraw draw;
    draw.pass = Pass::Ui; draw.blend = Blend::Opaque;
    draw.first_index = first; draw.index_count = 6;
    frame.commands.emplace_back(draw);
}
struct Image {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
};
Image load_png(const fs::path& path) {
    png_image raw{};
    raw.version = PNG_IMAGE_VERSION;
    struct Cleanup { png_image& value; ~Cleanup() { png_image_free(&value); } } cleanup{raw};
    require(png_image_begin_read_from_file(&raw, utf8(path).c_str()) != 0, "Could not decode PNG");
    require(raw.width == logical_size.width && raw.height == logical_size.height,
            "Native control capture is not 1920x1080");
    raw.format = PNG_FORMAT_RGBA;
    Image image{raw.width, raw.height, std::vector<std::uint8_t>(PNG_IMAGE_SIZE(raw))};
    require(png_image_finish_read(&raw, nullptr, image.rgba.data(), 0, nullptr) != 0, "PNG read failed");
    return image;
}
std::size_t changed_pixels(const Image& left, const Image& right) {
    require(left.rgba.size() == right.rgba.size(), "Capture dimensions differ");
    std::size_t changed{};
    for (std::size_t index = 0; index < left.rgba.size(); index += 4)
        if (!std::equal(left.rgba.begin() + static_cast<std::ptrdiff_t>(index),
                        left.rgba.begin() + static_cast<std::ptrdiff_t>(index + 4),
                        right.rgba.begin() + static_cast<std::ptrdiff_t>(index))) ++changed;
    return changed;
}
using Pixel = std::array<int, 4>;
Pixel pixel_at(const Image& image, Point point) {
    require(std::isfinite(point.x) && std::isfinite(point.y), "Non-finite pixel sample");
    const int x = static_cast<int>(std::floor(point.x));
    const int y = static_cast<int>(std::floor(point.y));
    require(x >= 0 && y >= 0 && x < static_cast<int>(image.width) &&
                y < static_cast<int>(image.height), "Pixel sample is outside the native capture");
    const auto index = (static_cast<std::size_t>(y) * image.width +
                        static_cast<std::size_t>(x)) * 4u;
    return {image.rgba[index], image.rgba[index + 1u], image.rgba[index + 2u],
            image.rgba[index + 3u]};
}
Json pixel_json(const Pixel& value) { return {value[0], value[1], value[2], value[3]}; }
Point sprite_source_screen(const Frame2D& frame, std::uint32_t owner,
                           const std::array<float, 2>& source_pixel) {
    std::uint32_t material_index{};
    for (std::size_t index = 0; index < frame.sprite_material_draws.size(); ++index)
        if (frame.sprite_material_draws[index].receiver_owner == owner) {
            require(material_index == 0u, "Sprite owner is not unique in control frame");
            material_index = static_cast<std::uint32_t>(index + 1u);
        }
    require(material_index != 0u, "Sprite owner is absent from control frame");
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw || draw->sprite_material_index != material_index) continue;
        require(draw->index_count == 6u, "Control sprite is not a quad");
        const auto first = frame.indices.at(draw->first_index);
        const auto opposite = frame.indices.at(draw->first_index + 2u);
        const auto& metadata = frame.sprite_material_draws.at(material_index - 1u).surface;
        const float width = metadata.source_size_pivot[0];
        const float height = metadata.source_size_pivot[1];
        require(source_pixel[0] >= 0.f && source_pixel[0] < width &&
                    source_pixel[1] >= 0.f && source_pixel[1] < height,
                "Authored source pixel is outside its sprite");
        const float u = (source_pixel[0] + .5f) / width;
        const float v = (source_pixel[1] + .5f) / height;
        const auto& top_left = frame.vertices.at(first).position;
        const auto& bottom_right = frame.vertices.at(opposite).position;
        return {top_left.x + (bottom_right.x - top_left.x) * u,
                top_left.y + (bottom_right.y - top_left.y) * v};
    }
    throw std::runtime_error("Sprite draw is absent from control frame");
}
const SpriteMaterialDraw& sprite_material(const Frame2D& frame, std::uint32_t owner) {
    const SpriteMaterialDraw* found{};
    for (const auto& draw : frame.sprite_material_draws)
        if (draw.receiver_owner == owner) {
            require(found == nullptr, "Sprite owner is not unique in control frame");
            found = &draw;
        }
    require(found != nullptr, "Sprite owner is absent from control frame");
    return *found;
}
std::pair<std::uint32_t, std::uint32_t> occluder_range(const Frame2D& frame,
                                                       std::uint32_t owner) {
    std::uint32_t first{};
    std::uint32_t count{};
    for (std::size_t index = 0; index < frame.local_occluders.size(); ++index) {
        if (frame.local_occluders[index].shape_flags_id[3] != owner) continue;
        if (count == 0u) first = static_cast<std::uint32_t>(index);
        require(index == static_cast<std::size_t>(first + count),
                "Authored owner occluders are not contiguous");
        ++count;
    }
    require(count != 0u, "Authored blocker has no local occluder");
    return {first, count};
}
Point ground_screen(const Frame2D& frame, float render_x, float render_y) {
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw || draw->ground_material_index == 0u) continue;
        require(draw->index_count % 3u == 0u, "Ground draw is not triangular");
        for (std::uint32_t offset = 0; offset < draw->index_count; offset += 3u) {
            std::array<std::uint32_t, 3> indices{
                frame.indices.at(draw->first_index + offset),
                frame.indices.at(draw->first_index + offset + 1u),
                frame.indices.at(draw->first_index + offset + 2u)};
            const auto& a = frame.surface_vertices.at(indices[0]).world_position;
            const auto& b = frame.surface_vertices.at(indices[1]).world_position;
            const auto& c = frame.surface_vertices.at(indices[2]).world_position;
            const float denominator = (b[1] - c[1]) * (a[0] - c[0]) +
                                      (c[0] - b[0]) * (a[1] - c[1]);
            require(std::abs(denominator) > 1.e-7f, "Ground triangle is degenerate");
            const float wa = ((b[1] - c[1]) * (render_x - c[0]) +
                              (c[0] - b[0]) * (render_y - c[1])) / denominator;
            const float wb = ((c[1] - a[1]) * (render_x - c[0]) +
                              (a[0] - c[0]) * (render_y - c[1])) / denominator;
            const float wc = 1.f - wa - wb;
            if (wa < 0.f || wb < 0.f || wc < 0.f) continue;
            const auto& sa = frame.vertices.at(indices[0]).position;
            const auto& sb = frame.vertices.at(indices[1]).position;
            const auto& sc = frame.vertices.at(indices[2]).position;
            return {wa * sa.x + wb * sb.x + wc * sc.x,
                    wa * sa.y + wb * sb.y + wc * sc.y};
        }
    }
    throw std::runtime_error("Requested ground point is outside the actual GameView terrain");
}
std::array<float, 2> first_mask_interior(const fs::path& kit, const Json& world,
                                         std::string_view material_id) {
    const auto material = std::find_if(world.at("materials").begin(), world.at("materials").end(),
        [&](const Json& row) { return row.at("id").get<std::string>() == material_id; });
    require(material != world.at("materials").end(), "House material metadata is absent");
    const auto texture_id = material->at("channels").at("emission_mask").at("texture").get<std::string>();
    const auto texture = std::find_if(world.at("textures").begin(), world.at("textures").end(),
        [&](const Json& row) { return row.at("id") == texture_id; });
    require(texture != world.at("textures").end(), "House emission texture metadata is absent");
    const int width = texture->at("width"), height = texture->at("height");
    const auto bytes = read_bytes(kit / texture->at("path").get<std::string>());
    require(bytes.size() == static_cast<std::size_t>(width * height),
            "House emission-mask payload size changed");
    for (int y = 1; y + 1 < height; ++y)
        for (int x = 1; x + 1 < width; ++x) {
            bool interior = true;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                interior = interior && bytes[static_cast<std::size_t>((y + dy) * width + x + dx)] != 0u;
            if (interior) return {static_cast<float>(x), static_cast<float>(y)};
        }
    throw std::runtime_error("House emission mask has no safe interior source pixel");
}
std::array<float, 2> typed_probe_pixel(const fs::path& kit, const Json& world,
                                       const Json& metadata, std::string_view category_id) {
    const auto category = std::find_if(metadata.at("categories").begin(), metadata.at("categories").end(),
        [&](const Json& row) { return row.at("id").get<std::string>() == category_id; });
    require(category != metadata.at("categories").end() && !category->at("probes").empty(),
            "Typed receiver probe metadata is absent");
    const auto source = category->at("probes").front().at("source_pixel");
    const int x = source.at(0), y = source.at(1);
    const auto material_id = category->at("material_id").get<std::string>();
    const auto material = std::find_if(world.at("materials").begin(), world.at("materials").end(),
        [&](const Json& row) { return row.at("id") == material_id; });
    require(material != world.at("materials").end(), "Typed receiver material metadata is absent");
    for (const std::string channel : {"normal_material", "camera_depth"}) {
        const auto texture_id = material->at("channels").at(channel).at("texture").get<std::string>();
        const auto texture = std::find_if(world.at("textures").begin(), world.at("textures").end(),
            [&](const Json& row) { return row.at("id") == texture_id; });
        require(texture != world.at("textures").end(), "Typed receiver channel texture is absent");
        const int width = texture->at("width"), height = texture->at("height");
        require(x >= 0 && y >= 0 && x < width && y < height, "Typed receiver probe is outside channel blobs");
        const auto bytes = read_bytes(kit / texture->at("path").get<std::string>());
        require(!bytes.empty() && bytes.size() % static_cast<std::size_t>(width * height) == 0u,
                "Typed receiver channel blob layout changed");
        const auto stride = bytes.size() / static_cast<std::size_t>(width * height);
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>((y * width + x) * stride);
        require(std::any_of(begin, begin + static_cast<std::ptrdiff_t>(stride),
                            [](std::uint8_t value) { return value != 0u; }),
                "Typed receiver probe has vacuous normal/depth channel data");
    }
    return {static_cast<float>(x), static_cast<float>(y)};
}
Image capture(Renderer2D& renderer, const Frame2D& source, const fs::path& output,
              std::string_view name, Json& captures) {
    Frame2D frame = source;
    append_ui_control(frame);
    const auto path = output / (std::string(name) + ".png");
    require(!fs::exists(path), "Capture path exists");
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    renderer.render(frame, path);
    const auto telemetry = renderer.frame_telemetry();
    require(telemetry.submitted && telemetry.capture && telemetry.graph_pass_count == 7u,
            "GameView control was not captured through seven GPU passes");
    constexpr std::array<WorldGraphPass,7> expected{
        WorldGraphPass::SunShadow, WorldGraphPass::MoonShadow,
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward, WorldGraphPass::Composite, WorldGraphPass::Ui};
    Json passes = Json::array();
    for (std::size_t index=0; index<expected.size(); ++index) {
        require(telemetry.graph_passes[index] == expected[index], "GPU pass order changed");
        passes.push_back(static_cast<std::uint32_t>(telemetry.graph_passes[index]));
    }
    const auto image = load_png(path);
    const auto ui_index = (30u * image.width + 30u) * 4u;
    require(std::equal(image.rgba.begin() + ui_index, image.rgba.begin() + ui_index + 4,
                       std::array<std::uint8_t,4>{215,42,76,255}.begin()), "UI control changed");
    captures.push_back({{"name", name}, {"file", path.filename().string()},
                        {"sha256", digest(read_bytes(path))}, {"slot", telemetry.frame_slot},
                        {"graph_passes", passes},
                        {"submitted", telemetry.submitted}, {"capture", telemetry.capture},
                        {"gpu_execution_ms", telemetry.gpu_execution_ms ? Json(*telemetry.gpu_execution_ms) : Json(nullptr)}});
    check_budget(output);
    return image;
}
bool supported_cell(const ClientSession& session, int x, int y) {
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx)
        if (!inside_world(x+dx,y+dy) || !session.tile(x+dx,y+dy).explored) return false;
    return session.tile(x,y).terrain != Terrain::Water;
}
bool explored_control_window(const ClientSession& session, float camera_x, float camera_y,
                             int cell_radius) {
    const int center_x = static_cast<int>(std::floor(camera_x+.5f));
    const int center_y = static_cast<int>(std::floor(camera_y+.5f));
    for (int y=center_y-cell_radius; y<=center_y+cell_radius; ++y)
        for (int x=center_x-cell_radius; x<=center_x+cell_radius; ++x)
            if (!inside_world(x,y) || !session.tile(x,y).explored) return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    fs::path output;
    bool output_owned{};
    Json captures = Json::array();
    try {
        require(argc == 4, "Usage: GameViewEngineProbe runtime-assets diagnostic-kit new-output");
        const auto assets = fs::absolute(path_from_utf8(argv[1]));
        const auto kit = fs::absolute(path_from_utf8(argv[2]));
        const auto kit_world = read_json(kit / "art/world.json");
        const auto kit_metadata = read_json(kit / "art/control-kit.json");
        require(kit_world.at("schema") == 2 && kit_metadata.at("schema") == 1,
                "Engine control kit schema changed");
        output = fs::absolute(path_from_utf8(argv[3]));
        require(output.parent_path() == assets.parent_path().parent_path(),
                "Output must be an immediate sibling build child");
        require(!fs::exists(output) && fs::space(output.parent_path()).available >= minimum_free,
                "Output exists or disk headroom is insufficient");
        require(fs::create_directory(output), "Could not create new output");
        output_owned = true;
        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra GameView Engine Control — not final art", 1920, 1080, 0), SDL_DestroyWindow);
        require(window != nullptr && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()),
                "Could not open visible native control window");
        RendererOptions options;
        options.backend = Backend::Direct3D12;
        options.world_graph = WorldGraph::SkyShadowSlice;
        options.collect_telemetry = true;
        Renderer2D renderer(window.get(), assets, options);
        renderer.set_logical_size(logical_size);
        const auto device = renderer.device_info();
        require(!device.software && device.backend == "direct3d12", "Native D3D12 is required");
        ClientSession session(70421);
        require(session.ready() && !session.residents().empty(), "Offline snapshot is unavailable");
        GameView view(renderer);
        view.set_logical_size(logical_size);
        view.load_engine_control_art(AssetCatalog(kit));
        GameArt direct_art(renderer);
        direct_art.load(AssetCatalog(kit));
        EngineControlSettings settings;
        settings.camera_x = session.residents().front().x;
        settings.camera_y = session.residents().front().y;
        settings.cell_radius = 8;
        settings.lighting.ambient = {.08f,.08f,.08f,0.f};
        settings.lighting.sun_direction = {.70710677f,0.f,.70710677f,0.f};
        settings.lighting.sun_color_intensity = {1.f,.8f,.55f,.8f};
        settings.moon.direction = {-.70710677f,0.f,.70710677f,0.f};
        settings.moon.color_intensity = {.55f,.68f,1.f,0.f};
        const std::array<std::string_view,8> ids{"house","stockpile","workshop","tree","rock",
                                                "resident.empty","resident.log","resident.sack"};
        std::vector<EngineControlInstance> instances;
        std::vector<CellCoord> chosen;
        const int cx = static_cast<int>(std::floor(settings.camera_x));
        const int cy = static_cast<int>(std::floor(settings.camera_y));
        for (int y=cy-5; y<=cy+5 && chosen.size()<ids.size(); ++y)
            for (int x=cx-5; x<=cx+5 && chosen.size()<ids.size(); ++x) {
                if (!supported_cell(session,x,y) || std::any_of(chosen.begin(),chosen.end(),
                    [=](const CellCoord& cell){return std::abs(cell.x-x)+std::abs(cell.y-y)<3;})) continue;
                const auto index = chosen.size();
                chosen.push_back({x,y,WorldLayer::Surface});
                instances.push_back({"control."+std::string(ids[index])+".sprite",float(x),float(y),1.f,
                                     static_cast<std::uint32_t>(index+1),
                                     static_cast<std::uint32_t>(index*4+1),.4f,true,
                                     index == 0u ? 1001u : 0u});
            }
        require(instances.size() == ids.size(), "Not enough fully explored control placements");
        Frame2D baseline;
        const auto stats = view.compose_engine_control(session,0.,settings,instances,baseline);
        require(stats.instances_drawn == 8u && stats.sky_triangles > 0 && stats.local_occluders > 0 &&
                    stats.local_lights == 2u && baseline.surface_lighting.counts[0] == 2u &&
                    stats.ground_cells >= 25u, "Real GameView control composition is vacuous");
        require(baseline.vertices.size() == baseline.surface_vertices.size(), "Global stream mismatch");
        auto external_settings = settings;
        external_settings.lighting.counts[0] = 1u;
        auto& external_light = external_settings.lighting.lights[0];
        external_light.anchor_radius = {0.f,0.f,2.f,4.f};
        external_light.direction_cone = {0.f,0.f,-1.f,-1.f};
        external_light.color_intensity = {.25f,.4f,.6f,1.f};
        external_light.occluder_range_flags = {0u,0u,1u,9000u};
        external_light.height_falloff_reveal = {-1.f,2.f,1.f,0.f};
        const auto external_before = external_light;
        Frame2D external_frame;
        const auto external_stats = view.compose_engine_control(
            session,0.,external_settings,instances,external_frame);
        require(external_stats.local_lights == 3u &&
                    external_frame.surface_lighting.counts[0] == 3u &&
                    std::memcmp(&external_frame.surface_lighting.lights[0],&external_before,
                                sizeof(LightGpu)) == 0,
                "Pre-existing external light slot zero changed during authored composition");
        for (std::size_t light_index=1u; light_index<3u; ++light_index) {
            const auto& range = external_frame.surface_lighting.lights[light_index].occluder_range_flags;
            require(range[0] == 0u &&
                        range[1] == static_cast<std::uint32_t>(external_frame.local_occluders.size()) &&
                        range[3] == 1000u + light_index,
                    "Authored emitter range or independent stable identity changed");
        }
        std::size_t picks{};
        for (const auto& cell : chosen) {
            const auto point = view.tile_screen(float(cell.x),float(cell.y));
            require(view.cell_at_screen(point.x,point.y) == cell, "Drawn surface and cell picking disagree");
            ++picks;
        }
        const auto fingerprint = geometry_fingerprint(baseline);
        const auto image = capture(renderer,baseline,output,"gameview-sun",captures);
        const auto repeat = capture(renderer,baseline,output,"gameview-sun-repeat",captures);
        require(image.rgba == repeat.rgba, "GameView control repeat is not pixel-exact");
        auto hidden_instances = instances;
        bool hidden_found{};
        for (int y=world_min; y<world_max && !hidden_found; ++y)
            for (int x=world_min; x<world_max && !hidden_found; ++x)
                if (!session.tile(x,y).explored) {
                    hidden_instances.push_back({"invalid-hidden-control",float(x),float(y),
                        std::numeric_limits<float>::quiet_NaN(),0u,0u,0.f});
                    hidden_found = true;
                }
        require(hidden_found, "No unexplored cell for hidden-object control");
        Frame2D hidden_frame;
        const auto hidden_stats = view.compose_engine_control(session,0.,settings,hidden_instances,hidden_frame);
        require(hidden_stats.instances_hidden == 1u && hidden_stats.instances_drawn == 8u &&
                    hidden_stats.local_lights == 2u &&
                    geometry_fingerprint(hidden_frame) == fingerprint,
                "Hidden control contributed to visible geometry or buffers");
        const auto hidden_image = capture(renderer,hidden_frame,output,"gameview-sun-hidden",captures);
        require(image.rgba == hidden_image.rgba, "Hidden control changed native pixels");
        Frame2D no_casters = baseline;
        no_casters.sky_shadow_triangles.clear();
        const auto unshadowed = capture(renderer,no_casters,output,"gameview-no-sky-casters",captures);
        const auto shadow_difference = changed_pixels(image,unshadowed);
        require(shadow_difference >= 25u, "Real GameView authored shadows are vacuous");
        auto moon_settings = settings;
        moon_settings.lighting.sun_color_intensity[3] = 0.f;
        moon_settings.moon.color_intensity[3] = .8f;
        Frame2D moon_frame;
        const auto moon_stats = view.compose_engine_control(session,0.,moon_settings,instances,moon_frame);
        require(moon_stats.instances_drawn == 8u && geometry_fingerprint(moon_frame) == fingerprint,
                "Light-only control changed production geometry");
        const auto moon_image = capture(renderer,moon_frame,output,"gameview-moon",captures);
        require(changed_pixels(image,moon_image) >= 500u, "Sun/Moon control is vacuous");

        CellCoord radiance_cell{};
        bool radiance_cell_found{};
        for (int y = cy - 4; y <= cy + 1 && !radiance_cell_found; ++y)
            for (int x = cx - 3; x <= cx + 3 && !radiance_cell_found; ++x) {
                bool explored_window = true;
                for (const auto& offset : control_camera_offsets)
                    explored_window = explored_window && explored_control_window(
                        session,static_cast<float>(x)+offset[0],
                        static_cast<float>(y)+1.5f+offset[1],settings.cell_radius);
                const std::array<CellCoord, 4> cells{{{x,y,WorldLayer::Surface},
                    {x-1,y+2,WorldLayer::Surface},{x+1,y+2,WorldLayer::Surface},
                    {x,y+3,WorldLayer::Surface}}};
                if (!explored_window ||
                    !std::all_of(cells.begin(), cells.end(), [&](const CellCoord& cell) {
                        return supported_cell(session,cell.x,cell.y) &&
                               std::abs(int(session.tile(cell.x,cell.y).elevation) -
                                        int(session.tile(x,y).elevation)) <= 1;
                    })) continue;
                radiance_cell = {x,y,WorldLayer::Surface};
                radiance_cell_found = true;
            }
        require(radiance_cell_found, "No deterministic same-height radiance placement is available");
        constexpr std::uint32_t house_owner = 101u, red_blocker_owner = 102u;
        constexpr std::uint32_t blue_blocker_owner = 103u, receiver_owner = 104u;
        const float rx = static_cast<float>(radiance_cell.x);
        const float ry = static_cast<float>(radiance_cell.y);
        std::vector<EngineControlInstance> radiance_instances{
            {"control.house.sprite",rx,ry,1.f,house_owner,1001u,-10.f,true,1001u},
            {"control.resident.empty.sprite",rx,ry+3.f,1.f,receiver_owner,1030u,10.f,false,0u}};
        std::vector<EngineControlInstance> fixture_instances{
            {"control.house.sprite",rx,ry,1.f,house_owner,1001u,-10.f,false,1001u},
            {"control.rock.sprite",rx-.52f,ry+1.9f,1.f,red_blocker_owner,1010u,-2.f,false,0u},
            {"control.rock.sprite",rx+.52f,ry+1.9f,1.f,blue_blocker_owner,1020u,-1.f,false,0u}};
        auto radiance_settings = settings;
        radiance_settings.camera_x = rx;
        radiance_settings.camera_y = ry + 1.5f;
        radiance_settings.lighting = {};
        radiance_settings.lighting.ambient = {.015f,.015f,.015f,0.f};
        radiance_settings.lighting.sun_color_intensity = {0.f,0.f,0.f,0.f};
        radiance_settings.moon.color_intensity = {0.f,0.f,0.f,0.f};
        Frame2D radiance_on_frame;
        const auto radiance_on_stats = view.compose_engine_control(
            session,0.,radiance_settings,radiance_instances,radiance_on_frame);
        require(radiance_on_stats.instances_drawn == radiance_instances.size() &&
                    radiance_on_stats.local_lights == 2u &&
                    radiance_on_frame.surface_lighting.counts[0] == 2u,
                "Two authored house window lights were not composed");
        auto radiance_off_instances = radiance_instances;
        radiance_off_instances.front().emission_enabled = false;
        Frame2D radiance_off_frame;
        const auto radiance_off_stats = view.compose_engine_control(
            session,0.,radiance_settings,radiance_off_instances,radiance_off_frame);
        require(radiance_off_stats.local_lights == 0u &&
                    radiance_off_frame.surface_lighting.counts[0] == 0u &&
                    sprite_material(radiance_on_frame,house_owner).emission_scale == 1.f &&
                    sprite_material(radiance_off_frame,house_owner).emission_scale == 0.f &&
                    geometry_fingerprint(radiance_on_frame) == geometry_fingerprint(radiance_off_frame),
                "Source-off control changed geometry or retained emission/light state");

        const auto emission_pixel = first_mask_interior(
            kit,kit_world,"control.house.material");
        const auto receiver_pixel = typed_probe_pixel(
            kit,kit_world,kit_metadata,"control.resident.empty");
        const auto window_screen = sprite_source_screen(radiance_on_frame,house_owner,emission_pixel);
        const auto receiver_screen = sprite_source_screen(radiance_on_frame,receiver_owner,receiver_pixel);
        const auto ground_point = ground_screen(radiance_on_frame,
            rx-radiance_settings.camera_x,ry+2.65f-radiance_settings.camera_y);
        const auto radiance_on_image = capture(
            renderer,radiance_on_frame,output,"gameview-window-radiance-on",captures);
        const auto radiance_off_image = capture(
            renderer,radiance_off_frame,output,"gameview-window-radiance-off",captures);
        Frame2D disabled_mask_frame = radiance_off_frame;
        disabled_mask_frame.surface_lighting.counts[0] = 1u;
        auto& disabled_mask_light = disabled_mask_frame.surface_lighting.lights[0];
        disabled_mask_light.anchor_radius = {0.f,1.5f,2.f,6.f};
        disabled_mask_light.direction_cone = {0.f,0.f,-1.f,-1.f};
        disabled_mask_light.color_intensity = {1.f,0.f,1.f,16.f};
        disabled_mask_light.occluder_range_flags = {0u,0u,2u,7000u};
        disabled_mask_light.height_falloff_reveal = {-1.f,2.f,1.f,0.f};
        const auto disabled_mask_image = capture(
            renderer,disabled_mask_frame,output,"gameview-disabled-mask-mismatch",captures);
        require(disabled_mask_image.rgba == radiance_off_image.rgba,
                "Disabled source plus non-matching external light changed receiver pixels");
        Frame2D self_only_frame = radiance_on_frame;
        self_only_frame.surface_lighting.counts = {0u,0u,0u,0u};
        const auto self_only_image = capture(
            renderer,self_only_frame,output,"gameview-window-self-only",captures);
        const auto window_on = pixel_at(self_only_image,window_screen);
        const auto window_off = pixel_at(radiance_off_image,window_screen);
        const auto ground_on = pixel_at(radiance_on_image,ground_point);
        const auto ground_off = pixel_at(radiance_off_image,ground_point);
        const auto receiver_on = pixel_at(radiance_on_image,receiver_screen);
        const auto receiver_off = pixel_at(radiance_off_image,receiver_screen);
        require(window_on[0] - window_off[0] >= 8 &&
                    window_on[0] - window_off[0] > window_on[2] - window_off[2],
                "Authored window self-radiance lacks the required warm-channel response");
        require(pixel_at(self_only_image,ground_point) == ground_off &&
                    pixel_at(self_only_image,receiver_screen) == receiver_off,
                "Self-emission-only control changed independent ground or typed receiver pixels");
        require(ground_on[0] - ground_off[0] >= 12,
                "Authored window lights did not illuminate nearby actual GameView ground");
        require(receiver_on[0] - receiver_off[0] >= 12,
                "Authored window lights did not illuminate the typed sprite receiver");

        const auto receiver_category = std::find_if(kit_metadata.at("categories").begin(),
            kit_metadata.at("categories").end(),[](const Json& row) {
                return row.at("id") == "control.resident.empty";
            });
        require(receiver_category != kit_metadata.at("categories").end() &&
                    !receiver_category->at("probes").empty(),
                "Typed receiver metadata is absent");
        const auto receiver_probe = receiver_category->at("probes").front();
        require(receiver_probe.at("normal").size() == 3u &&
                    receiver_probe.at("position").size() == 3u,
                "Typed receiver probe normal/depth metadata changed");
        Frame2D fixture_base_frame;
        const auto fixture_stats = view.compose_engine_control(
            session,0.,radiance_settings,fixture_instances,fixture_base_frame);
        require(fixture_stats.local_lights == 0u &&
                    fixture_stats.instances_drawn == fixture_instances.size(),
                "Explicit fixture base retained automatic sources or lost geometry");
        const auto fixture_ground_screen = ground_screen(fixture_base_frame,
            rx-radiance_settings.camera_x,ry+3.f-radiance_settings.camera_y);
        const auto& house_material = sprite_material(fixture_base_frame,house_owner);
        const auto house_category = std::find_if(kit_metadata.at("categories").begin(),
            kit_metadata.at("categories").end(),[](const Json& row) {
                return row.at("id") == "control.house";
            });
        require(house_category != kit_metadata.at("categories").end(),
                "House control metadata is absent");
        const auto emitters = house_category->at("local_emitters");
        require(emitters.size() == 2u, "House metadata no longer has exactly two emitters");
        for (const auto& emitter : emitters) {
            const float authored_y = emitter.at("anchor_xyz").at(1);
            require(std::abs(authored_y-.93f) <= 1.e-7f && authored_y-.9f >= .002f,
                    "House emitter source or parser clearance contract changed");
        }
        std::array<std::array<float,3>,2> fixture_anchors{};
        for (std::size_t index=0; index<fixture_anchors.size(); ++index) {
            const auto local = emitters.at(index).at("anchor_xyz").get<std::array<float,3>>();
            for (std::size_t axis=0; axis<3; ++axis)
                fixture_anchors[index][axis] = house_material.surface.anchor_export[axis] + local[axis];
        }
        const auto red_range = occluder_range(fixture_base_frame,red_blocker_owner);
        const auto blue_range = occluder_range(fixture_base_frame,blue_blocker_owner);
        const auto fixture_frame = [&](std::size_t source, bool blocked) {
            Frame2D frame = fixture_base_frame;
            frame.surface_lighting.counts = {1u,0u,0u,0u};
            auto& light = frame.surface_lighting.lights[0];
            light.anchor_radius = {fixture_anchors[source][0],fixture_anchors[source][1],
                                   fixture_anchors[source][2],4.5f};
            light.direction_cone = {0.f,0.f,-1.f,-1.f};
            light.color_intensity = source == 0u
                ? std::array<float,4>{1.f,0.f,0.f,5.f}
                : std::array<float,4>{0.f,0.f,1.f,5.f};
            const auto range = source == 0u ? red_range : blue_range;
            light.occluder_range_flags = {blocked?range.first:0u,blocked?range.second:0u,1u,
                                          static_cast<std::uint32_t>(501u+source)};
            light.height_falloff_reveal = {-1.f,2.f,1.f,0.f};
            return frame;
        };
        const auto red_open_image = capture(renderer,fixture_frame(0u,false),output,
                                             "gameview-fixture-red-open",captures);
        const auto red_blocked_image = capture(renderer,fixture_frame(0u,true),output,
                                                "gameview-fixture-red-blocked",captures);
        const auto blue_open_image = capture(renderer,fixture_frame(1u,false),output,
                                              "gameview-fixture-blue-open",captures);
        const auto blue_blocked_image = capture(renderer,fixture_frame(1u,true),output,
                                                 "gameview-fixture-blue-blocked",captures);
        const auto red_open = pixel_at(red_open_image,fixture_ground_screen);
        const auto red_blocked = pixel_at(red_blocked_image,fixture_ground_screen);
        const auto blue_open = pixel_at(blue_open_image,fixture_ground_screen);
        const auto blue_blocked = pixel_at(blue_blocked_image,fixture_ground_screen);
        require(red_open[0] - red_blocked[0] >= 12 &&
                    std::abs(red_open[2] - red_blocked[2]) <= 2,
                "Red blocker did not attenuate only its corresponding red source");
        require(blue_open[2] - blue_blocked[2] >= 12 &&
                    std::abs(blue_open[0] - blue_blocked[0]) <= 2,
                "Blue blocker did not attenuate only its corresponding blue source");

        Json camera_matrix = Json::array();
        constexpr std::array<float,3> supported_zooms{.62f,1.3f,2.f};
        for (const float zoom : supported_zooms) for (const auto& offset : control_camera_offsets) {
            auto matrix_settings = radiance_settings;
            matrix_settings.zoom = zoom;
            matrix_settings.camera_x += offset[0];
            matrix_settings.camera_y += offset[1];
            require(explored_control_window(session,matrix_settings.camera_x,
                        matrix_settings.camera_y,matrix_settings.cell_radius),
                    "Preflight lost a planned camera-matrix explored ground window");
            Frame2D matrix_frame;
            const auto matrix_stats = view.compose_engine_control(
                session,0.,matrix_settings,radiance_instances,matrix_frame);
            require(matrix_stats.local_lights == 2u && matrix_frame.surface_lighting.counts[0] == 2u,
                    "Authored lights disappeared across supported camera controls");
            std::size_t matrix_picks{};
            for (const auto& instance : radiance_instances) {
                const int cell_x = static_cast<int>(std::floor(instance.x+.5f));
                const int cell_y = static_cast<int>(std::floor(instance.y+.5f));
                const auto screen = view.tile_screen(instance.x,instance.y);
                require(view.cell_at_screen(screen.x,screen.y) ==
                            CellCoord{cell_x,cell_y,WorldLayer::Surface},
                        "Camera control changed actual terrain picking");
                ++matrix_picks;
            }
            const auto& matrix_house = sprite_material(matrix_frame,house_owner);
            for (std::size_t index=0; index<2u; ++index) {
                const auto local = emitters.at(index).at("anchor_xyz").get<std::array<float,3>>();
                const auto& light = matrix_frame.surface_lighting.lights[index];
                for (std::size_t axis=0; axis<3; ++axis)
                    require(std::abs(light.anchor_radius[axis] -
                        (matrix_house.surface.anchor_export[axis]+local[axis])) <= 1.e-5f,
                        "Authored light anchor drifted from its sprite across camera controls");
                require(std::abs(light.anchor_radius[0]+matrix_settings.camera_x-
                                 (rx+local[0])) <= 1.e-5f &&
                            std::abs(light.anchor_radius[1]+matrix_settings.camera_y-
                                 (ry+local[1])) <= 1.e-5f,
                        "Authored light world anchor changed under camera pan or zoom");
            }
            auto matrix_off_instances = radiance_instances;
            matrix_off_instances.front().emission_enabled = false;
            Frame2D matrix_off_frame;
            const auto matrix_off_stats = view.compose_engine_control(
                session,0.,matrix_settings,matrix_off_instances,matrix_off_frame);
            require(matrix_off_stats.local_lights == 0u &&
                        geometry_fingerprint(matrix_frame) == geometry_fingerprint(matrix_off_frame),
                    "Emission-only camera control changed geometry");
            camera_matrix.push_back({{"zoom",zoom},{"camera",{matrix_settings.camera_x,matrix_settings.camera_y}},
                                     {"picks",matrix_picks},{"authored_lights",matrix_stats.local_lights},
                                     {"off_lights",matrix_off_stats.local_lights}});
        }
        const Json radiance_report{
            {"placement",{{"house",{rx,ry}},{"red_blocker",{rx-.52f,ry+1.9f}},
                           {"blue_blocker",{rx+.52f,ry+1.9f}},{"typed_receiver",{rx,ry+3.f}}}},
            {"authored_light_count",radiance_on_stats.local_lights},
            {"source_off_light_count",radiance_off_stats.local_lights},
            {"emitter_contract",{{"anchor_y",.93f},{"minimum_exterior_clearance",.002f}}},
            {"emission_scale",{{"on",sprite_material(radiance_on_frame,house_owner).emission_scale},
                                {"off",sprite_material(radiance_off_frame,house_owner).emission_scale}}},
            {"window",{{"screen",{window_screen.x,window_screen.y}},
                        {"self_only",pixel_json(window_on)},
                        {"authored_lights_on",pixel_json(pixel_at(radiance_on_image,window_screen))},
                        {"off",pixel_json(window_off)}}},
            {"ground",{{"screen",{ground_point.x,ground_point.y}},
                        {"on",pixel_json(ground_on)},{"off",pixel_json(ground_off)}}},
            {"typed_receiver",{{"screen",{receiver_screen.x,receiver_screen.y}},
                                {"on",pixel_json(receiver_on)},{"off",pixel_json(receiver_off)}}},
            {"fixture",{{"red_open",pixel_json(red_open)},{"red_blocked",pixel_json(red_blocked)},
                         {"blue_open",pixel_json(blue_open)},{"blue_blocked",pixel_json(blue_blocked)},
                         {"ground_screen",{fixture_ground_screen.x,fixture_ground_screen.y}},
                         {"channel_margin",12},{"unaffected_channel_tolerance",2}}},
            {"camera_matrix",camera_matrix}};

        Frame2D guard_frame;
        (void)view.compose_engine_control(session,0.,settings,instances,guard_frame);
        const auto guard_first_screen = view.tile_screen(float(chosen.front().x),float(chosen.front().y));
        const auto guard_last_screen = view.tile_screen(float(chosen.back().x),float(chosen.back().y));
        const auto guard_first_pick = view.cell_at_screen(guard_first_screen.x,guard_first_screen.y);
        const auto guard_last_pick = view.cell_at_screen(guard_last_screen.x,guard_last_screen.y);
        Json gameview_rejections = Json::array();
        const auto reject_gameview = [&](std::string_view id, std::string_view expected,
                                         const EngineControlSettings& rejected_settings,
                                         const std::vector<EngineControlInstance>& rejected_instances) {
            Frame2D preserved = guard_frame;
            const auto preserved_before = full_frame_fingerprint(preserved);
            const auto lighting_before = preserved.surface_lighting;
            std::string diagnostic;
            try {
                (void)view.compose_engine_control(
                    session,0.,rejected_settings,rejected_instances,preserved);
            } catch(const std::invalid_argument& error) { diagnostic=error.what(); }
            const auto first_screen = view.tile_screen(float(chosen.front().x),float(chosen.front().y));
            const auto last_screen = view.tile_screen(float(chosen.back().x),float(chosen.back().y));
            require(diagnostic == expected && full_frame_fingerprint(preserved) == preserved_before &&
                        lighting_exact(preserved.surface_lighting,lighting_before) &&
                        first_screen.x == guard_first_screen.x && first_screen.y == guard_first_screen.y &&
                        last_screen.x == guard_last_screen.x && last_screen.y == guard_last_screen.y &&
                        view.cell_at_screen(first_screen.x,first_screen.y) == guard_first_pick &&
                        view.cell_at_screen(last_screen.x,last_screen.y) == guard_last_pick,
                    "Rejected GameView composition changed frame lighting, camera or picking");
            gameview_rejections.push_back({{"id",id},{"diagnostic",diagnostic},{"frame_exact",true},
                                            {"camera_pick_exact",true}});
        };
        auto bad_settings = settings;
        bad_settings.cell_radius = 100;
        reject_gameview("cell-radius",
            "Engine control camera, time, radius, zoom or exposure is unsupported",
            bad_settings,instances);
        auto max_instances = instances;
        max_instances.front().x = std::numeric_limits<float>::max();
        reject_gameview("coordinate-flt-max",
            "Engine control instance coordinates are outside the world",settings,max_instances);
        auto nan_instances = instances;
        nan_instances.front().x = std::numeric_limits<float>::quiet_NaN();
        reject_gameview("coordinate-nan",
            "Engine control instance coordinates must be finite",settings,nan_instances);
        auto count_settings = settings;
        count_settings.lighting.counts[0] = render::surface_light_capacity + 1u;
        reject_gameview("preexisting-light-count-over-four",
            "Engine control external light count/reserved fields are invalid",count_settings,instances);
        auto external_range_settings = external_settings;
        external_range_settings.lighting.lights[0].occluder_range_flags[1] = 1u;
        reject_gameview("external-light-nonzero-blocker-range",
            "Engine control external lights require zero blocker ranges",
            external_range_settings,instances);
        auto collision_settings = external_settings;
        collision_settings.lighting.lights[0].occluder_range_flags[3] = 1002u;
        reject_gameview("second-authored-light-identity-collision",
            "Duplicate local emitter stable ID",collision_settings,instances);
        std::vector<EngineControlInstance> blocker_overflow;
        auto overflow_house = instances.front();
        overflow_house.first_light_id = 6001u;
        blocker_overflow.push_back(overflow_house);
        for (std::uint32_t index=0; index<16u; ++index)
            blocker_overflow.push_back({"control.resident.log.sprite",overflow_house.x,
                overflow_house.y,1.f,200u+index,5000u+index*2u,
                static_cast<float>(index)*.001f,false,0u});
        reject_gameview("authored-light-blocker-range-over-32",
            "Engine control authored light blocker range exceeds 32",settings,blocker_overflow);

        Json direct_rejections = Json::array();
        const auto reject_direct = [&](std::string_view id, std::string_view expected,
                                       Frame2D candidate,
                                       const std::array<float,3>& anchor,
                                       std::uint32_t first_light_id,
                                       float scale=1.f) {
            const auto before = candidate.surface_lighting;
            std::string diagnostic;
            try {
                (void)direct_art.append_lit_emitters(candidate,"control.house.sprite",
                    anchor,scale,first_light_id,true);
            } catch(const std::invalid_argument& error) { diagnostic=error.what(); }
            require(diagnostic == expected && lighting_exact(candidate.surface_lighting,before),
                    "Rejected direct emitter append changed the full lighting frame");
            direct_rejections.push_back({{"id",id},{"diagnostic",diagnostic},
                                         {"lighting_exact",true}});
        };
        const std::array<float,3> direct_anchor{0.f,0.f,0.f};
        constexpr std::string_view invalid_identity =
            "Lit emitter anchor/scale, identity or capacity is invalid";
        reject_direct("stable-id-zero",invalid_identity,Frame2D{},direct_anchor,0u);
        reject_direct("stable-id-overflow",invalid_identity,Frame2D{},direct_anchor,
                      std::numeric_limits<std::uint32_t>::max());
        Frame2D direct_collision;
        direct_collision.surface_lighting.counts[0] = 1u;
        direct_collision.surface_lighting.lights[0].occluder_range_flags[3] = 1002u;
        reject_direct("collision-at-second-emitter","Duplicate local emitter stable ID",
                      direct_collision,direct_anchor,1001u);
        Frame2D direct_capacity;
        direct_capacity.surface_lighting.counts[0] = 3u;
        for (std::uint32_t index=0; index<3u; ++index)
            direct_capacity.surface_lighting.lights[index].occluder_range_flags[3] = 20u+index;
        reject_direct("capacity-over-four",invalid_identity,direct_capacity,direct_anchor,1001u);
        reject_direct("scaled-clearance-below-0.002",
            "Lit emitter scaled exterior clearance is below 0.002",
            Frame2D{},direct_anchor,1001u,.05f);
        Frame2D disabled_filtered;
        disabled_filtered.surface_lighting = external_settings.lighting;
        const auto disabled_before = disabled_filtered.surface_lighting;
        const std::array<float,3> invalid_disabled_anchor{
            std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::max(),0.f};
        const auto disabled_appended = direct_art.append_lit_emitters(
            disabled_filtered,"control.house.sprite",invalid_disabled_anchor,1.f,0u,false);
        require(disabled_appended == 0u &&
                    lighting_exact(disabled_filtered.surface_lighting,disabled_before),
                "Disabled invalid emitter identity/anchor was not filtered transactionally");
        direct_rejections.push_back({{"id","disabled-invalid-id-anchor-filtered"},
                                     {"lighting_exact",true}});
        const auto source_hash = digest(read_bytes(fs::path(__FILE__)));
        const auto executable_hash = digest(read_bytes(fs::absolute(path_from_utf8(argv[0]))));
        write_report(output/"gameview-engine-report.json",
            {{"schema",2},{"complete",true},{"scope","bounded opt-in real GameView terrain plus neutral presentation controls"},
             {"source_sha256",source_hash},{"executable_sha256",executable_hash},
             {"shader_manifest_sha256",digest(read_bytes(assets/"shaders/manifest.json"))},
             {"kit_manifest_sha256",digest(read_bytes(kit/"manifest.json"))},
             {"device",{{"backend",device.backend},{"device",device.device},{"driver",device.driver},{"software",device.software}}},
             {"logical_size",{1920,1080}},{"seed",70421},{"camera",{settings.camera_x,settings.camera_y}},
             {"zoom",settings.zoom},{"cell_radius",settings.cell_radius},
             {"ground_cells",stats.ground_cells},{"instances_drawn",stats.instances_drawn},
             {"sky_triangles",stats.sky_triangles},{"local_occluders",stats.local_occluders},
             {"local_lights",stats.local_lights},
             {"cell_picks",picks},{"shadow_changed_pixels",shadow_difference},
             {"repeat_exact",true},{"hidden_control_pixel_exact",true},
             {"external_light_slot_zero_exact",true},
             {"external_light_preservation_scope","pre-existing slots with zero blocker ranges"},
             {"disabled_mask_mismatch_pixel_exact",true},
             {"radiance",radiance_report},
             {"gameview_rejections",gameview_rejections},
             {"direct_emitter_rejections",direct_rejections},
             {"invalid_settings_transactional",true},{"unsafe_coordinate_rejected",true},{"captures",captures},
             {"non_claims",{"normal game integration","all world objects","final/concept art","HUD acceptance",
                            "full engine","full camera/zoom/reveal matrix","contact pixel proof",
                            "native invalid-catalog reload rollback or GPU-upload counter proof",
                            "performance or 120FPS","full suite","E01-E17 acceptance"}}});
        check_budget(output);
        std::cout << "Bounded real GameView control passed; five baseline PNGs plus radiance controls\n";
        return 0;
    } catch(const std::exception& error) {
        if (output_owned && fs::is_directory(output)) {
            try { write_report(output/"gameview-engine-failure.json",{{"schema",1},{"complete",false},
                      {"error",error.what()},{"captures",captures}}); } catch(...) {}
        }
        std::cerr << "GameView engine control failed: " << error.what() << '\n';
        return 1;
    }
}

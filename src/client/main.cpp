#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/frame_timing.hpp"
#include "ribterra/view.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/terrain_surface.hpp"
#include "ribterra/world_projection.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <png.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <array>
#include <iterator>
#include <optional>
#include <span>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace ribterra::render;
constexpr std::uint64_t house_advance_limit = 12000u;
constexpr std::string_view diagnostic_house_key = "building.house.ready";
constexpr float diagnostic_zoom = 1.30f;
void diagnostic_require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
void diagnostic_no_reparse(fs::path path) {
    path = fs::absolute(path).lexically_normal();
    for (;;) {
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            diagnostic_require((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u,
                               "House diagnostic refuses reparse paths");
#else
        diagnostic_require(!fs::is_symlink(fs::symlink_status(path)), "House diagnostic refuses symlink paths");
#endif
        const auto parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
}
bool diagnostic_same_path(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    const auto left = fs::absolute(a).lexically_normal().wstring();
    const auto right = fs::absolute(b).lexically_normal().wstring();
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return fs::absolute(a).lexically_normal() == fs::absolute(b).lexically_normal();
#endif
}
void diagnostic_budget(const fs::path& root, std::uintmax_t pending = 0u) {
    diagnostic_no_reparse(root);
    constexpr std::uintmax_t limit = 128u * 1024u * 1024u;
    diagnostic_require(pending <= limit, "House diagnostic pending output exceeds its budget");
    std::uintmax_t total = pending;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        diagnostic_no_reparse(entry.path());
        if (entry.is_regular_file()) {
            const auto bytes = entry.file_size();
            diagnostic_require(bytes <= limit - total, "House diagnostic output exceeds 128 MiB");
            total += bytes;
        }
    }
    diagnostic_require(fs::space(root).available >= 384u * 1024u * 1024u + pending,
                       "House diagnostic needs 384 MiB free headroom");
}
std::vector<std::uint8_t> diagnostic_bytes(const fs::path& path) {
    diagnostic_no_reparse(path);
    std::ifstream input(path, std::ios::binary);
    diagnostic_require(input.good(), "House diagnostic cannot read a bound file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string diagnostic_json_hash(const Json& value, bool newline = false) {
    const auto text = value.dump() + (newline ? "\n" : "");
    return ribterra::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
void diagnostic_exclusive(const fs::path& path, const std::string& text) {
    diagnostic_budget(path.parent_path(), text.size());
    diagnostic_no_reparse(path);
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    diagnostic_require(file != INVALID_HANDLE_VALUE, "House diagnostic refuses an existing output file");
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    std::size_t offset{};
    while (offset < text.size()) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 1u << 20u));
        DWORD written{};
        diagnostic_require(WriteFile(file, text.data() + offset, count, &written, nullptr) != 0 && written == count,
                           "House diagnostic report write failed; staging is retained");
        offset += written;
    }
    diagnostic_require(FlushFileBuffers(file) != 0, "House diagnostic report flush failed; output is retained");
#else
    throw std::runtime_error("House diagnostic exclusive outputs require Windows D3D12");
#endif
}
void diagnostic_reserve_png(const fs::path& path) {
    diagnostic_no_reparse(path);
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    diagnostic_require(file != INVALID_HANDLE_VALUE, "House diagnostic refuses an existing PNG");
    CloseHandle(file); // Renderer writes only this exclusively reserved, initially empty owned file.
#else
    throw std::runtime_error("House diagnostic exclusive PNGs require Windows D3D12");
#endif
}
Json diagnostic_draw(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass), static_cast<int>(draw.blend), draw.texture.slot,
        draw.texture.generation, draw.first_index, draw.index_count, draw.scissor.x, draw.scissor.y,
        draw.scissor.w, draw.scissor.h, draw.scissor_enabled, draw.surface_lit,
        draw.sprite_material_index, draw.ground_material_index};
}
Json diagnostic_frame(const Frame2D& frame) {
    // This leaf owns the ordinary LegacyCombined route. Reject future extensions
    // rather than omit a field from the exact single-command comparison.
    diagnostic_require(frame.ground_material_draws.empty() && frame.contact_shadows.empty() &&
        frame.local_occluders.empty() && frame.sky_shadow_triangles.empty() && frame.reflection_draws.empty() &&
        !frame.water_surface && !frame.atmosphere && !frame.moon_lighting && !frame.contact_projection,
        "House diagnostic requires ordinary LegacyCombined payloads");
    Json result{{"content", static_cast<int>(frame.content)},
        {"size", {frame.logical_size.width, frame.logical_size.height}},
        {"clear", {frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a}},
        {"hdr", frame.linear_hdr_world}, {"exposure", frame.world_exposure},
        {"indices", frame.indices}, {"vertices", Json::array()}, {"surfaces", Json::array()},
        {"commands", Json::array()}, {"sprites", Json::array()},
        {"lighting", {frame.surface_lighting.ambient, frame.surface_lighting.sun_direction,
            frame.surface_lighting.sun_color_intensity, frame.surface_lighting.counts}},
        {"lights", Json::array()}, {"water_normal", {frame.water_normal.slot, frame.water_normal.generation}},
        {"ground_materials", Json::array()}, {"contacts", Json::array()}, {"occluders", Json::array()},
        {"sky_triangles", Json::array()}, {"reflections", Json::array()},
        {"water_surface", nullptr}, {"atmosphere", nullptr}, {"moon", nullptr}, {"contact_projection", nullptr}};
    for (const auto& v : frame.vertices)
        result["vertices"].push_back({v.position.x,v.position.y,v.uv.x,v.uv.y,v.color.r,v.color.g,v.color.b,v.color.a});
    for (const auto& v : frame.surface_vertices) result["surfaces"].push_back({v.world_position,v.normal});
    for (const auto& command : frame.commands) {
        if (const auto* draw = std::get_if<GeometryDraw>(&command))
            result["commands"].push_back({{"geometry", diagnostic_draw(*draw)}});
        else {
            const auto& text = std::get<TextDraw>(command);
            result["commands"].push_back({{"text", {static_cast<int>(text.pass),text.text,text.position.x,text.position.y,
                text.size,text.color.r,text.color.g,text.color.b,text.color.a,text.max_width}}});
        }
    }
    for (const auto& light : frame.surface_lighting.lights)
        result["lights"].push_back({light.anchor_radius,light.direction_cone,light.color_intensity,
            light.occluder_range_flags,light.height_falloff_reveal});
    for (const auto& draw : frame.sprite_material_draws)
        result["sprites"].push_back({draw.material.slot,draw.material.generation,draw.surface.uv_regions,
            draw.surface.anchor_export,draw.surface.source_size_pivot,draw.surface.depth_scale,
            draw.surface.emission,draw.surface.response,draw.reveal,draw.receiver_mask,draw.receiver_owner,draw.emission_scale});
    return result;
}
Json diagnostic_weather(const ribterra::ClientSession& session) {
    const auto& weather = session.weather();
    return {{"known", weather.known}, {"simulation_tick", session.ticks()},
        {"calendar_tick", weather.calendar_tick}, {"kind", static_cast<int>(weather.current.kind)},
        {"precipitation", static_cast<int>(weather.current.precipitation)},
        {"temperature_deci_c", weather.current.temperature_deci_c},
        {"wind_direction", static_cast<int>(weather.current.wind_direction)},
        {"precipitation_permille", weather.current.precipitation_permille},
        {"wind_permille", weather.current.wind_permille}, {"wetness_permille", weather.wetness_permille},
        {"wetness_remainder", weather.wetness_remainder}, {"snow_cover_permille", weather.snow_cover_permille},
        {"algorithm_version", weather.algorithm_version}, {"evaluated_simulation_tick", weather.evaluated_simulation_tick},
        {"calendar_offset_ticks", weather.calendar_offset_ticks}, {"climate_profile", static_cast<int>(weather.climate_profile)},
        {"scenario", static_cast<int>(weather.scenario)}, {"outdoor_work_rate_bp", weather.current.outdoor_work_rate_bp}};
}
Json diagnostic_reply(const ribterra::CommandReply& reply) {
    return {{"request_id", reply.request_id}, {"applied_tick", reply.applied_tick}, {"author", reply.author},
        {"status", static_cast<int>(reply.status)}, {"failure", static_cast<int>(reply.failure)}, {"reason", reply.reason}};
}
const ribterra::Building& diagnostic_house(const ribterra::ClientSession& session, int id) {
    const auto found = std::find_if(session.buildings().begin(), session.buildings().end(),
        [id](const auto& building) { return building.id == id; });
    diagnostic_require(found != session.buildings().end(), "Admitted house disappeared");
    return *found;
}
int diagnostic_build_house(ribterra::ClientSession& session, Json& report) {
    diagnostic_require(!session.remote() && session.ready() && session.world_ready(), "House diagnostic requires a fresh local world");
    report["stage"] = "legal_site_search";
    report["initial_tick"] = session.ticks();
    report["initial_resources"] = {session.resources().wood,session.resources().stone,session.resources().food};
    report["initial_buildings"] = Json::array();
    const ribterra::Building* camp{};
    for (const auto& building : session.buildings()) {
        report["initial_buildings"].push_back({building.id,static_cast<int>(building.kind),building.x,building.y,building.progress});
        if (building.kind == ribterra::BuildingKind::Camp) camp = &building;
    }
    diagnostic_require(camp && session.buildings().size() == 1u, "House diagnostic did not receive the ordinary Camp-only world");
    const int camp_x = camp->x, camp_y = camp->y;
    report["camp"] = {camp->id,camp_x,camp_y};
    std::optional<std::array<int,2>> site;
    std::size_t searched{};
    for (int y = camp_y - 2; y <= camp_y + 3 && !site; ++y)
        for (int x = camp_x - 3; x <= camp_x + 3 && !site; ++x) {
            ++searched;
            if (session.tile(x,y).explored && session.can_build(ribterra::BuildingKind::House,x,y)) site = std::array<int,2>{x,y};
        }
    report["site_search"] = {{"bounds", {camp_x-3,camp_y-2,camp_x+3,camp_y+3}}, {"examined", searched}};
    diagnostic_require(site.has_value(), "No legal explored house site near the ordinary Camp");
    const auto [x,y] = *site;
    const auto admission = session.build(ribterra::BuildingKind::House,x,y,ribterra::WorldLayer::Surface);
    report["admission"] = {{"site", {x,y}}, {"accepted", admission.accepted}, {"pending", admission.pending},
        {"path_batch_id", admission.path_batch_id}, {"failure", static_cast<int>(admission.failure)}, {"message", admission.message}};
    diagnostic_require(admission.accepted, "Real house command was not admitted");
    report["stage"] = "actual_construction";
    report["replies"] = Json::array();
    bool applied{};
    int house_id{};
    for (std::uint64_t attempts = 1u; attempts <= house_advance_limit; ++attempts) {
        session.advance();
        report["advance_attempts"] = attempts;
        report["latest_tick"] = session.ticks();
        while (auto reply = session.take_command_reply()) {
            report["replies"].push_back(diagnostic_reply(*reply));
            diagnostic_require(reply->request_id == 1u && reply->author == session.player_id(),
                "Fresh house diagnostic received an unrelated authority reply");
            diagnostic_require(reply->status != ribterra::CommandReplyStatus::Rejected && reply->status != ribterra::CommandReplyStatus::Invalid,
                "Real house build reply rejected construction");
            applied = applied || reply->status == ribterra::CommandReplyStatus::Applied;
        }
        const auto found = std::find_if(session.buildings().begin(), session.buildings().end(), [&](const auto& b) {
            return b.kind == ribterra::BuildingKind::House && b.x == x && b.y == y && b.layer == ribterra::WorldLayer::Surface;
        });
        if (found != session.buildings().end()) {
            if (!house_id) house_id = found->id;
            diagnostic_require(house_id == found->id, "Real house identity changed during construction");
            report["house"] = {{"id", found->id}, {"kind", static_cast<int>(found->kind)}, {"cell", {x,y}},
                {"layer", static_cast<int>(found->layer)}, {"progress", found->progress}, {"nation", found->nation}};
            if (found->progress == 1.f && applied) {
                int level = 1;
                for (const auto& upgrade : session.economy().upgrades) if (upgrade.building_id == found->id) level = upgrade.level;
                report["house"]["level"] = level;
                const auto visual = ribterra::resolve_building_visual(found->kind,level,found->progress,
                    session.winter() || ribterra::snowy(session.tile(x,y).biome));
                report["house"]["resolved_key"] = visual.asset_id;
                report["weather"] = diagnostic_weather(session);
                diagnostic_require(level == 1 && visual.asset_id == diagnostic_house_key && session.weather().known,
                    "Completed real house is not the mapped ready Surface variant");
                report["applied_reply"] = true;
                report["completion_resources"] = {session.resources().wood,session.resources().stone,session.resources().food};
                report["stage"] = "completed_real_house";
                return house_id;
            }
        }
    }
    report["applied_reply"] = applied;
    report["weather"] = diagnostic_weather(session);
    report["incomplete_resources"] = {session.resources().wood,session.resources().stone,session.resources().food};
    report["construction_sites"] = Json::array();
    for (const auto& site_view : session.construction_sites())
        report["construction_sites"].push_back({{"id", site_view.building_id}, {"phase", static_cast<int>(site_view.phase)},
            {"delivered", {site_view.delivered.wood,site_view.delivered.stone,site_view.delivered.food}},
            {"required", {site_view.required.wood,site_view.required.stone,site_view.required.food}}});
    throw std::runtime_error("Real house did not complete within 12000 actual advances; incomplete state is retained");
}
struct DiagnosticImage {
    Extent size{};
    std::vector<std::uint8_t> rgba;
};
DiagnosticImage diagnostic_decode_png(const std::vector<std::uint8_t>& encoded) {
    png_image image{}; image.version = PNG_IMAGE_VERSION;
    struct Free { png_image* image; ~Free() { png_image_free(image); } } free{&image};
    diagnostic_require(png_image_begin_read_from_memory(&image,encoded.data(),encoded.size()) != 0,
                       "House diagnostic PNG header decode failed");
    diagnostic_require(image.width > 0u && image.height > 0u && image.width <= 8192u && image.height <= 8192u,
                       "House diagnostic PNG dimensions are outside bounds");
    image.format = PNG_FORMAT_RGBA;
    DiagnosticImage result{{image.width,image.height},std::vector<std::uint8_t>(PNG_IMAGE_SIZE(image))};
    diagnostic_require(png_image_finish_read(&image,nullptr,result.rgba.data(),0,nullptr) != 0,
                       "House diagnostic PNG pixels decode failed");
    return result;
}
const Json& diagnostic_find(const Json& rows, std::string_view field, std::string_view id) {
    const Json* found{};
    for (const auto& row : rows) if (row.at(std::string(field)).get<std::string>() == id) {
        diagnostic_require(!found, "House diagnostic source identity is ambiguous"); found = &row;
    }
    diagnostic_require(found != nullptr, "House diagnostic source identity is absent");
    return *found;
}
struct DiagnosticSource {
    std::array<int,4> crop{};
    std::array<float,2> pivot{};
    float reference_width{}, export_scale{};
    Json item, sprite, material;
    std::vector<std::uint8_t> rgba;
};
DiagnosticSource diagnostic_source(const ribterra::AssetCatalog& original,
                                    const ribterra::AssetCatalog& collection, Json& report) {
    DiagnosticSource result;
    const auto original_bytes = original.read_path("art/world.json");
    const auto metadata_bytes = collection.read("material.collection.metadata");
    const auto world_bytes = collection.read_path("art/world.json");
    const auto world = Json::parse(world_bytes), catalog = Json::parse(original_bytes), metadata = Json::parse(metadata_bytes);
    result.item = diagnostic_find(metadata.at("items"),"legacy_key",diagnostic_house_key);
    const auto& albedo_kind = result.item.at("albedo_source").at("kind");
    const bool textured_house = albedo_kind == "authored-xyz-triangle-textures-v1";
    diagnostic_require(albedo_kind == "authored-xyz-triangle-materials-v1" ||
        (textured_house && collection.read_profile() == ribterra::AssetCatalogReadProfile::MaterialCollectionTextured),
                       "House diagnostic requires the explicit authored neutral house mode");
    const auto& registration = result.item.at("registration");
    const Json* entry{}; std::string page_path;
    for (const auto& page : catalog.at("pages")) for (const auto& row : page.at("sprites"))
        if (row.at("id").get<std::string>() == diagnostic_house_key) {
            diagnostic_require(!entry, "Original ready house is ambiguous"); entry = &row;
            page_path = page.at("path").get<std::string>();
        }
    diagnostic_require(entry != nullptr, "Original ready house is absent");
    result.crop = entry->at("rect").get<std::array<int,4>>();
    result.pivot = entry->at("pivot").get<std::array<float,2>>();
    result.reference_width = entry->value("reference_width",float(result.crop[2]));
    const auto& item = result.item;
    Json source_record{{"master_sha256",item.at("master_sha256")},{"registration",registration},
        {"geometry_provenance",item.at("geometry_provenance")},{"albedo_source",item.at("albedo_source")},
        {"generated_albedo_sha256",item.at("generated_albedo_sha256")}};
    diagnostic_require(diagnostic_json_hash(source_record,true) == item.at("source_hash").get<std::string>(),
                       "House diagnostic canonical source-record hash differs");
    result.sprite = diagnostic_find(world.at("lit_sprites"),"id",item.at("sprite_id").get<std::string>());
    result.material = diagnostic_find(world.at("materials"),"id",item.at("material_id").get<std::string>());
    diagnostic_require(result.sprite.at("material") == item.at("material_id") && result.material.at("source_hash") == item.at("source_hash"),
                       "House diagnostic delivered sprite/material identity differs");
    result.export_scale = result.sprite.at("export_pixels_per_reference_pixel").get<float>();
    const auto page_bytes = original.read_path(page_path);
    auto decoded = diagnostic_decode_png(page_bytes);
    diagnostic_require(result.crop[0] >= 0 && result.crop[1] >= 0 && result.crop[2] > 0 && result.crop[3] > 0 &&
        std::uint64_t(result.crop[0])+std::uint64_t(result.crop[2]) <= decoded.size.width &&
        std::uint64_t(result.crop[1])+std::uint64_t(result.crop[3]) <= decoded.size.height,
        "House diagnostic original crop is outside its PNG");
    std::vector<std::uint8_t> painted(std::size_t(result.crop[2])*static_cast<std::size_t>(result.crop[3])*4u);
    for (int y=0; y<result.crop[3]; ++y) {
        const auto from = (std::size_t(result.crop[1]+y)*decoded.size.width+static_cast<std::size_t>(result.crop[0]))*4u;
        const auto to = std::size_t(y)*static_cast<std::size_t>(result.crop[2])*4u;
        std::copy_n(decoded.rgba.begin()+static_cast<std::ptrdiff_t>(from),std::size_t(result.crop[2])*4u,
                    painted.begin()+static_cast<std::ptrdiff_t>(to));
    }
    const std::array<float,2> expected_pivot_px{result.pivot[0]*float(result.crop[2]),result.pivot[1]*float(result.crop[3])};
    diagnostic_require(registration.at("page_path") == page_path && registration.at("rect") == entry->at("rect") &&
        registration.at("source_size") == Json::array({result.crop[2],result.crop[3]}) &&
        registration.at("pivot_px").get<std::array<double,2>>() ==
            std::array<double,2>{double(expected_pivot_px[0]),double(expected_pivot_px[1])} &&
        registration.at("reference_width").get<double>() == double(result.reference_width) &&
        result.sprite.at("source_size").get<std::array<float,2>>() == std::array<float,2>{float(result.crop[2]),float(result.crop[3])} &&
        result.sprite.at("pivot_px").get<std::array<float,2>>() == expected_pivot_px &&
        registration.at("png_sha256").get<std::string>() == ribterra::sha256(page_bytes) &&
        registration.at("decoded_crop_sha256").get<std::string>() == ribterra::sha256(painted) &&
        registration.at("catalog_entry_sha256").get<std::string>() == diagnostic_json_hash(Json{{"page",{{"path",page_path}}},{"sprite",*entry}},true),
        "House diagnostic original tuple digest differs");
    report["source"] = {{"original_content_hash",original.content_hash()},{"collection_content_hash",collection.content_hash()},
        {"original_world_sha256",ribterra::sha256(original_bytes)},{"metadata_sha256",ribterra::sha256(metadata_bytes)},
        {"world_sha256",ribterra::sha256(world_bytes)},{"item",item},{"source_record",source_record},
        {"original_page",page_path},{"original_entry",*entry},{"sprite",result.sprite},{"material",result.material},
        {"original_crop_rgba_sha256",ribterra::sha256(painted)},{"channels",Json::array()}};
    constexpr std::array<std::string_view,6> channels{"albedo","normal_material","camera_depth","emission_mask","surface_mask","caster_mask"};
    for (const auto name : channels) {
        const auto& channel = result.material.at("channels").at(std::string(name));
        const auto& texture = diagnostic_find(world.at("textures"),"id",channel.at("texture").get<std::string>());
        const auto& record = collection.find(texture.at("id").get<std::string>());
        const auto raw = collection.read(record.id);
        diagnostic_require(record.path == texture.at("path").get<std::string>() && record.sha256 == ribterra::sha256(raw),
                           "House diagnostic channel/manifest binding differs");
        report["source"]["channels"].push_back({{"name",name},{"channel",channel},{"texture",texture},
            {"asset",{{"id",record.id},{"path",record.path},{"bytes",record.bytes},{"sha256",record.sha256}}}});
        if (name == "albedo") result.rgba = raw;
    }
    diagnostic_require(result.rgba.size() == painted.size() && ribterra::sha256(result.rgba) == item.at("generated_albedo_sha256").get<std::string>(),
                       "House diagnostic generated albedo binding/extent differs");
    report["source"]["generated_rgba_sha256"] = ribterra::sha256(result.rgba);
    return result;
}
ribterra::Tile diagnostic_height_sample(const ribterra::ClientSession& session, int x, int y) {
    auto tile = session.tile(x,y,ribterra::WorldLayer::Surface);
    if (!tile.explored) return {};
    if (tile.terrain == ribterra::Terrain::Water) tile.elevation = 0;
    else tile.terrain = ribterra::Terrain::Grass;
    return tile;
}
Rect diagnostic_house_bounds(const DiagnosticSource& source, ribterra::GameView& view,
                             const ribterra::ClientSession& session, int house_id, Json& report) {
    const auto& house = diagnostic_house(session,house_id);
    const auto sample = [&](int x,int y) { return diagnostic_height_sample(session,x,y); };
    const int camp_x = report.at("camp")[1].get<int>(), camp_y = report.at("camp")[2].get<int>();
    const auto camp_screen = view.tile_screen(float(camp_x),float(camp_y));
    const auto east_screen = view.tile_screen(float(camp_x+1),float(camp_y));
    const auto logical = report.at("logical_size").get<std::array<std::uint32_t,2>>();
    const float measured_zoom = (east_screen.x-camp_screen.x)/reference_tile_half_width;
    diagnostic_require(std::isfinite(measured_zoom) && std::abs(measured_zoom-diagnostic_zoom)<=.00001f,
                       "Actual public projection does not match the requested SDL Camp zoom");
    const float camp_height = ribterra::terrain_height(float(camp_x),float(camp_y),sample);
    const auto relative = reconstruct_render_xy({(camp_screen.x-float(logical[0])*.48f)/measured_zoom,
        (camp_screen.y+camp_height*legacy_elevation_screen_scale*measured_zoom-float(logical[1])*.48f)/measured_zoom},0.f);
    const std::array<float,2> actual_camera{float(camp_x)-relative.x,float(camp_y)-relative.y};
    diagnostic_require(std::abs(actual_camera[0]-float(camp_x))<=.001f && std::abs(actual_camera[1]-float(camp_y))<=.001f,
        "Actual public projection does not match the requested SDL Camp camera");
    report["camera"]["effective"] = {{"center",actual_camera},{"zoom",measured_zoom},
        {"camp_tile_screen",{camp_screen.x,camp_screen.y}},{"east_tile_screen",{east_screen.x,east_screen.y}}};
    const auto corners = ribterra::terrain_corners(house.x,house.y,sample);
    const float base = *std::max_element(corners.begin(),corners.end());
    const float height = ribterra::terrain_height(float(house.x),float(house.y),sample);
    const auto tile = view.tile_screen(float(house.x),float(house.y));
    Point ground{tile.x,tile.y-(base-height)*legacy_elevation_screen_scale*diagnostic_zoom+15.f*diagnostic_zoom};
    const auto visual = ribterra::resolve_building_visual(house.kind,1,house.progress,false);
    float width = visual.width*diagnostic_zoom;
    width *= float(source.crop[2])/source.reference_width;
    const float pixels_h = width*(float(source.crop[3])/float(source.crop[2]));
    const Rect bounds{ground.x-width*source.pivot[0],ground.y-pixels_h*source.pivot[1],width,pixels_h};
    report["foundation"] = {{"corners",corners},{"ground_height",height},{"base",base},
        {"tile_screen",{tile.x,tile.y}},{"sprite_ground",{ground.x,ground.y}},{"bounds",{bounds.x,bounds.y,bounds.w,bounds.h}}};
    return bounds;
}
struct DiagnosticQuad { std::size_t command{}; std::array<std::uint32_t,4> vertices{}; };
DiagnosticQuad diagnostic_quad(const Frame2D& frame, const Rect& bounds) {
    std::optional<DiagnosticQuad> found;
    const std::array<Point,4> positions{{{bounds.x,bounds.y},{bounds.x+bounds.w,bounds.y},
        {bounds.x+bounds.w,bounds.y+bounds.h},{bounds.x,bounds.y+bounds.h}}};
    for (std::size_t index=0; index<frame.commands.size(); ++index) {
        const auto* draw = std::get_if<GeometryDraw>(&frame.commands[index]);
        if (!draw || draw->pass != Pass::WorldOrdered || draw->blend != Blend::Alpha || !draw->sprite_material_index ||
            draw->index_count != 6u || draw->texture.slot || draw->surface_lit || draw->ground_material_index || draw->scissor_enabled) continue;
        diagnostic_require(draw->first_index <= frame.indices.size() && frame.indices.size()-draw->first_index >= 6u,
                           "House diagnostic body index range is invalid");
        const auto a = frame.indices.at(draw->first_index);
        const std::array<std::uint32_t,6> indices{a,a+1u,a+2u,a,a+2u,a+3u};
        if (!std::equal(indices.begin(),indices.end(),frame.indices.begin()+draw->first_index)) continue;
        bool same = a <= frame.vertices.size() && frame.vertices.size()-a >= 4u;
        for (std::uint32_t corner=0; same && corner<4u; ++corner) {
            const auto& v = frame.vertices.at(a+corner);
            const float u = corner==1u || corner==2u ? 1.f : 0.f;
            const float vv = corner>=2u ? 1.f : 0.f;
            same = std::abs(v.position.x-positions[corner].x)<=.005f && std::abs(v.position.y-positions[corner].y)<=.005f &&
                v.uv.x==u && v.uv.y==vv && v.color.r==255u && v.color.g==255u && v.color.b==255u && v.color.a==255u;
        }
        if (same) {
            diagnostic_require(!found, "House diagnostic typed body is not unique");
            found = DiagnosticQuad{index,{a,a+1u,a+2u,a+3u}};
        }
    }
    diagnostic_require(found.has_value(), "Specific completed house has no unique typed body command");
    return *found;
}
Json diagnostic_telemetry(const RendererFrameTelemetry& value, const Frame2D& frame) {
    std::size_t typed{};
    for (const auto& command : frame.commands) if (const auto* draw=std::get_if<GeometryDraw>(&command))
        if (draw->sprite_material_index) ++typed;
    diagnostic_require(value.submitted && value.capture && value.material_draws == typed && value.graph_pass_count == 0u &&
        value.ground_draws == 0u && value.contact_draws == 0u && value.water_draws == 0u && value.reflection_draws == 0u,
        "Actual house capture telemetry differs from ordinary LegacyCombined active commands");
    return {{"vertices",value.vertices},{"indices",value.indices},{"draws",value.draws},{"material_draws",value.material_draws},
        {"material_carrier_rows",frame.sprite_material_draws.size()},{"submitted",value.submitted},{"capture",value.capture},
        {"graph_pass_count",value.graph_pass_count},{"graph_passes",Json::array()},
        {"ground_draws",value.ground_draws},{"contacts",value.contact_draws},{"water_draws",value.water_draws},
        {"reflection_draws",value.reflection_draws},{"gpu_execution_ms",nullptr}};
}
void diagnostic_capture(ribterra::render::Renderer2D& renderer, const Frame2D& frame,
                        ribterra::GameView& view, const ribterra::ClientSession& session, int house_id,
                        const DiagnosticSource& source, const fs::path& capture, const fs::path& removed,
                        const fs::path& report_path, Json& report) {
    diagnostic_require(frame.content == FrameContent::World && frame.linear_hdr_world &&
        frame.surface_vertices.size() == frame.vertices.size() && session.ticks() == report.at("latest_tick").get<std::uint64_t>(),
        "House diagnostic World/frame/paused authority contract changed");
    report["logical_size"] = {frame.logical_size.width,frame.logical_size.height};
    const auto bounds = diagnostic_house_bounds(source,view,session,house_id,report);
    diagnostic_require(bounds.x >= 0.f && bounds.y >= 0.f && bounds.x+bounds.w <= float(frame.logical_size.width) &&
        bounds.y+bounds.h <= float(frame.logical_size.height), "Specific completed house is not fully on-screen");
    const auto quad = diagnostic_quad(frame,bounds);
    const auto& draw = std::get<GeometryDraw>(frame.commands.at(quad.command));
    const auto& material = frame.sprite_material_draws.at(draw.sprite_material_index-1u);
    const float expected_scale = ribterra::resolve_building_visual(ribterra::BuildingKind::House,1,1.f,false).width*
        source.export_scale/source.reference_width;
    const auto source_size = source.sprite.at("source_size").get<std::array<float,2>>();
    const auto pivot_px = source.sprite.at("pivot_px").get<std::array<float,2>>();
    diagnostic_require(material.surface.anchor_export[3] == source.export_scale &&
        material.surface.source_size_pivot == std::array<float,4>{source_size[0],source_size[1],pivot_px[0],pivot_px[1]} &&
        std::abs(material.surface.depth_scale[2]-expected_scale)<=.00001f,
        "Actual typed house material tuple differs from the delivered neutral identity");
    Json actual_vertices = Json::array(), actual_indices = Json::array();
    for (const auto index : quad.vertices) {
        const auto& vertex = frame.vertices.at(index);
        actual_vertices.push_back({vertex.position.x,vertex.position.y,vertex.uv.x,vertex.uv.y,
            vertex.color.r,vertex.color.g,vertex.color.b,vertex.color.a});
    }
    for (std::uint32_t index=0u; index<draw.index_count; ++index) actual_indices.push_back(frame.indices.at(draw.first_index+index));
    report["stage"] = "paired_actual_capture";
    auto without_house = frame;
    without_house.commands.erase(without_house.commands.begin()+static_cast<std::ptrdiff_t>(quad.command));
    const auto full_state = diagnostic_frame(frame), removed_state = diagnostic_frame(without_house);
    auto expected_state = full_state;
    expected_state["commands"].erase(expected_state["commands"].begin()+static_cast<std::ptrdiff_t>(quad.command));
    diagnostic_require(expected_state == removed_state, "House diagnostic changed payload beyond one body command");
    report["body"] = {{"command_index",quad.command},{"command",diagnostic_draw(draw)},
        {"vertex_indices",quad.vertices},{"material_index",draw.sprite_material_index},
        {"vertices",actual_vertices},{"indices",actual_indices},
        {"actual_material",{{"handle",{material.material.slot,material.material.generation}},
            {"anchor_export",material.surface.anchor_export},{"source_size_pivot",material.surface.source_size_pivot},
            {"depth_scale",material.surface.depth_scale},{"uv_regions",material.surface.uv_regions},
            {"emission",material.surface.emission},{"response",material.surface.response},
            {"reveal",material.reveal},{"receiver_owner",material.receiver_owner},{"receiver_mask",material.receiver_mask},
            {"emission_scale",material.emission_scale}}},
        {"full_frame_sha256",diagnostic_json_hash(full_state)},{"removed_frame_sha256",diagnostic_json_hash(removed_state)},
        {"full_frame",full_state},{"removed_frame",removed_state},
        {"only_house_command_removed",true},{"all_other_payload_exact",true}};
    const auto pending = std::uintmax_t(frame.logical_size.width)*frame.logical_size.height*8u+128u*1024u;
    diagnostic_budget(capture.parent_path(),pending);
    diagnostic_reserve_png(capture); renderer.render(frame,capture);
    report["full_telemetry"] = diagnostic_telemetry(renderer.frame_telemetry(),frame);
    diagnostic_reserve_png(removed); renderer.render(without_house,removed);
    report["removed_telemetry"] = diagnostic_telemetry(renderer.frame_telemetry(),without_house);
    const auto full_bytes = diagnostic_bytes(capture), removed_bytes = diagnostic_bytes(removed);
    const auto full_image = diagnostic_decode_png(full_bytes), removed_image = diagnostic_decode_png(removed_bytes);
    diagnostic_require(full_image.size.width == frame.logical_size.width && full_image.size.height == frame.logical_size.height &&
        removed_image.size.width == full_image.size.width && removed_image.size.height == full_image.size.height,
        "House diagnostic requires one drawable pixel per logical unit");
    std::size_t qualified{}, changed{}, total_changed{};
    for (std::uint32_t y=0u; y<full_image.size.height; ++y) for (std::uint32_t x=0u; x<full_image.size.width; ++x) {
        const auto offset = (std::size_t(y)*full_image.size.width+x)*4u;
        diagnostic_require(full_image.rgba[offset+3u]==255u && removed_image.rgba[offset+3u]==255u,
                           "House diagnostic captured a nonopaque final pixel");
        const bool different = !std::equal(full_image.rgba.begin()+static_cast<std::ptrdiff_t>(offset),
            full_image.rgba.begin()+static_cast<std::ptrdiff_t>(offset+4u),removed_image.rgba.begin()+static_cast<std::ptrdiff_t>(offset));
        if (different) ++total_changed;
        const double px = double(x)+.5, py = double(y)+.5;
        if (px<double(bounds.x) || py<double(bounds.y) || px>=double(bounds.x)+bounds.w || py>=double(bounds.y)+bounds.h) continue;
        const int sx = static_cast<int>(std::floor((px-double(bounds.x))/bounds.w*double(source.crop[2])));
        const int sy = static_cast<int>(std::floor((py-double(bounds.y))/bounds.h*double(source.crop[3])));
        if (sx<1 || sy<1 || sx>=source.crop[2]-1 || sy>=source.crop[3]-1) continue;
        bool opaque = true;
        for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx)
            opaque = opaque && source.rgba[(std::size_t(sy+dy)*static_cast<std::size_t>(source.crop[2])+static_cast<std::size_t>(sx+dx))*4u+3u] == 255u;
        if (!opaque) continue;
        ++qualified;
        if (different) ++changed; // An unchanged opaque overlap cannot count as visible house contribution.
    }
    report["pixels"] = {{"qualification","pixel-center source UV; 3x3 generated alpha255; changed only by actual same-frame body removal"},
        {"qualified",qualified},{"changed",changed},{"total_changed",total_changed}};
    report["captures"] = {{{"path",capture.filename().generic_string()},{"bytes",full_bytes.size()},{"sha256",ribterra::sha256(full_bytes)}},
        {{"path",removed.filename().generic_string()},{"bytes",removed_bytes.size()},{"sha256",ribterra::sha256(removed_bytes)}}};
    diagnostic_require(qualified > 0u && changed > 0u, "Specific completed neutral house has no qualified actual body contribution");
    report["complete"] = true; report["stage"] = "actual_paired_capture_complete";
    diagnostic_exclusive(report_path,report.dump(2)+"\n");
    diagnostic_budget(capture.parent_path());
}
std::filesystem::path from_utf8(const char *value) {
    if (!value)
        throw std::runtime_error(SDL_GetError());
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value)));
}
std::string to_utf8(const std::filesystem::path &value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}
std::filesystem::path preferences() {
    char *raw = SDL_GetPrefPath("Ribterra", "Ribterra");
    if (!raw)
        throw std::runtime_error(SDL_GetError());
    const auto result = from_utf8(raw);
    SDL_free(raw);
    return result;
}
std::filesystem::path automatic_preferences(const std::filesystem::path &capture) {
    auto parent = capture.empty() ? std::filesystem::temp_directory_path()
                                  : std::filesystem::absolute(capture).parent_path();
    std::filesystem::create_directories(parent);
    const auto prefix = capture.empty() ? std::filesystem::path("ribterra-smoke-profile-")
                                       : capture.filename() += ".test-profile-";
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto name = prefix;
        name += ribterra::secure_token(16);
        auto candidate = parent / name;
        if (std::filesystem::create_directory(candidate))
            return candidate; // Retained intentionally; no automatic cleanup.
    }
    throw std::runtime_error("Could not allocate a unique automatic test profile");
}
std::uint32_t fresh_world_seed() {
    const auto random_hex = ribterra::secure_token(16);
    return static_cast<std::uint32_t>(std::stoul(random_hex.substr(0, 8), nullptr, 16));
}
std::uint32_t unsigned_option(std::string_view value, std::string_view name,
                              std::uint32_t maximum) {
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result > maximum)
        throw std::invalid_argument(std::string(name) + " has an invalid numeric value");
    return result;
}
void demo(ribterra::ClientSession &world) {
    std::vector<int> ids;
    for (const auto &resident : world.residents())
        ids.push_back(resident.id);
    world.move(ids, 26, 25);
    for (int i = 0; i < 100; ++i)
        world.advance();
    for (int y = 19; y < 30; ++y) {
        bool assigned = false;
        for (int x = 19; x < 30; ++x) {
            if (world.tile(x, y).terrain == ribterra::Terrain::Forest && world.gather(ids, x, y).accepted) {
                assigned = true;
                break;
            }
        }
        if (assigned)
            break;
    }
    for (int i = 0; i < 400; ++i)
        world.advance();
    for (const auto kind :
         {ribterra::BuildingKind::House, ribterra::BuildingKind::Farm, ribterra::BuildingKind::Stockpile}) {
        bool placed = false;
        for (int y = 22; y < 28 && !placed; ++y)
            for (int x = 21; x < 28 && !placed; ++x) {
                if (world.can_build(kind, x, y))
                    placed = world.build(kind, x, y).accepted;
            }
        for (int i = 0; i < 400; ++i)
            world.advance();
    }
}
} // namespace

int main(int argc, char **argv) {
    bool host_lobby_capture = false, online_capture = false;
    bool smoke = false, demo_scene = false, software = false, gpu_debug = false, menu_capture = false;
    bool diagnostic_house_capture = false, diagnostic_outputs_ready = false;
    bool material_collection_textured = false;
    std::filesystem::path capture_path, renderer_info_path, material_collection_path, catalog_assets_path;
    std::filesystem::path diagnostic_removed_path, diagnostic_report_path;
    Json house_report;
    std::uint32_t seed = 70421;
    bool seed_explicit = false;
    std::string endpoint, player_name{"Player"};
    int width = 1440, height = 900;
    ribterra::render::PresentationRequest presentation;
    bool presentation_explicit=false;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--smoke")
                smoke = true;
            else if (arg == "--diagnostic-house-capture")
                diagnostic_house_capture = true;
            else if (arg == "--material-collection-textured") {
                if (material_collection_textured) throw std::invalid_argument("Duplicate --material-collection-textured flag");
                material_collection_textured = true;
            }
            else if (arg == "--software")
                software = true;
            else if (arg == "--gpu-debug")
                gpu_debug = true;
            else if (arg == "--present-mode" && i + 1 < argc) {
                presentation_explicit=true;
                const std::string mode = argv[++i];
                if (mode == "vsync") presentation.mode = ribterra::render::PresentMode::VSync;
                else if (mode == "immediate") presentation.mode = ribterra::render::PresentMode::Immediate;
                else if (mode == "mailbox") presentation.mode = ribterra::render::PresentMode::Mailbox;
                else throw std::invalid_argument("Unknown presentation mode");
            } else if (arg == "--fps-limit" && i + 1 < argc){
                presentation_explicit=true;
                presentation.frame_limit = unsigned_option(argv[++i], "--fps-limit", 1000);
            } else if (arg == "--frames-in-flight" && i + 1 < argc){
                presentation_explicit=true;
                presentation.frames_in_flight = unsigned_option(argv[++i], "--frames-in-flight", 3);
            } else if (arg == "--demo")
                demo_scene = true;
            else if (arg == "--host-lobby-capture" && i + 1 < argc) {
                capture_path = from_utf8(argv[++i]);host_lobby_capture = true;
            } else if (arg == "--online-capture" && i + 1 < argc) {
                capture_path = from_utf8(argv[++i]);online_capture = true;menu_capture = true;
            }
            else if (arg == "--capture" && i + 1 < argc)
                capture_path = from_utf8(argv[++i]);
            else if (arg == "--menu-capture" && i + 1 < argc) {
                capture_path = from_utf8(argv[++i]);
                menu_capture = true;
            } else if (arg == "--renderer-info" && i + 1 < argc)
                renderer_info_path = from_utf8(argv[++i]);
            else if (arg == "--material-collection" && i + 1 < argc)
                material_collection_path = from_utf8(argv[++i]);
            else if (arg == "--catalog-assets" && i + 1 < argc)
                catalog_assets_path = from_utf8(argv[++i]);
            else if (arg == "--seed" && i + 1 < argc) {
                seed = static_cast<std::uint32_t>(std::stoul(argv[++i]));
                seed_explicit = true;
            }
            else if (arg == "--connect" && i + 1 < argc)
                endpoint = argv[++i];
            else if (arg == "--name" && i + 1 < argc)
                player_name = argv[++i];
            else if (arg == "--compact") {
                width = 1100;
                height = 700;
            } else
                return 2;
        }
        if (material_collection_textured && (material_collection_path.empty() || software))
            throw std::invalid_argument("--material-collection-textured requires --material-collection and hardware HDR rendering");
        if (diagnostic_house_capture) {
            house_report = {{"schema",1},{"mode","diagnostic-house-capture"},{"complete",false},
                {"source_preimage_sha256","8468889E5062D8207434604D2B59D25270CAA84C91231A3BB93FA92B868D063D"},
                {"stage","argument_validation"},{"advance_limit",house_advance_limit},
                {"classification","bounded real local construction and ordinary main World/UI composition; diagnostic capture"},
                {"world_fixture_imported",false},{"weather_overridden",false},{"resources_created",false},
                {"fps",nullptr},{"gpu_execution_ms",nullptr},{"reference_fidelity","OPEN"},{"full_engine","OPEN"},
                {"open",{"untouched ordinary startup visibility","genuine desktop input/video","eight production masters",
                    "neutral/reference art acceptance","full cumulative FX","120 FPS acceptance","full E01-E17/P01"}}};
            diagnostic_require(!capture_path.empty() && !renderer_info_path.empty() && !material_collection_path.empty() &&
                !catalog_assets_path.empty(), "House diagnostic requires --capture, --renderer-info, --material-collection and --catalog-assets");
            diagnostic_require(!smoke && !demo_scene && !menu_capture && !host_lobby_capture && !online_capture &&
                endpoint.empty() && !software, "House diagnostic forbids demo/menu/smoke/software/remote/host/online modes");
            diagnostic_require(capture_path.is_absolute() && renderer_info_path.is_absolute() &&
                material_collection_path.is_absolute() && catalog_assets_path.is_absolute(), "House diagnostic requires absolute input/output paths");
            capture_path = capture_path.lexically_normal(); renderer_info_path = renderer_info_path.lexically_normal();
            diagnostic_require(capture_path.extension() == ".png" && renderer_info_path.extension() == ".json" &&
                diagnostic_same_path(renderer_info_path.parent_path(),capture_path.parent_path()),
                "House diagnostic requires sibling PNG and JSON renderer-info outputs");
            diagnostic_removed_path = capture_path.parent_path() / (capture_path.stem().wstring()+L".house-removed.png");
            diagnostic_report_path = capture_path.parent_path() / (capture_path.stem().wstring()+L".house-diagnostic.json");
            const std::array<fs::path,4> diagnostic_outputs{capture_path,renderer_info_path,diagnostic_removed_path,diagnostic_report_path};
            for (const auto& path : diagnostic_outputs) {
                diagnostic_no_reparse(path);
                diagnostic_require(!fs::exists(path), "House diagnostic refuses an existing output");
            }
            for (std::size_t a=0u; a<diagnostic_outputs.size(); ++a)
                for (std::size_t b=a+1u; b<diagnostic_outputs.size(); ++b)
                    diagnostic_require(!diagnostic_same_path(diagnostic_outputs[a],diagnostic_outputs[b]),
                        "House diagnostic output identities collide");
            diagnostic_require(fs::is_directory(capture_path.parent_path()), "House diagnostic needs a fresh caller-owned output directory");
            for (const auto& input : {catalog_assets_path,material_collection_path}) {
                diagnostic_no_reparse(input);
                diagnostic_require(fs::is_directory(input), "House diagnostic input root is absent");
                const auto output_root = fs::canonical(capture_path.parent_path());
                const auto input_root = fs::canonical(input);
                const auto output_relative = output_root.lexically_relative(input_root);
                const auto input_relative = input_root.lexically_relative(output_root);
                const auto nested = [](const fs::path& relative) {
                    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
                };
                diagnostic_require(!nested(output_relative) && !nested(input_relative),
                    "House diagnostic output/input roots must be disjoint");
            }
            diagnostic_budget(capture_path.parent_path());
            diagnostic_outputs_ready = true;
            house_report["seed"] = seed;
        }
        const bool automatic = smoke || !capture_path.empty();
        const auto seed_for_fresh_world = [&] {
            return seed_explicit || automatic ? seed : fresh_world_seed();
        };
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        {
            const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (automatic ? SDL_WINDOW_HIDDEN : 0);
            const std::string window_title = "Ribterra " + std::string(ribterra::engine_version);
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow(window_title.c_str(), width, height, flags),
                SDL_DestroyWindow);
            if (!window)
                throw std::runtime_error(SDL_GetError());
            SDL_SetWindowMinimumSize(window.get(), 960, 600);
            const auto base = from_utf8(SDL_GetBasePath());
            ribterra::render::RendererOptions render_options;
            render_options.backend = software ? ribterra::render::Backend::Software : ribterra::render::Backend::Direct3D12;
            render_options.debug = gpu_debug;
            render_options.presentation = presentation;
            render_options.collect_telemetry = diagnostic_house_capture;
            ribterra::render::Renderer2D renderer(window.get(), base / "assets", render_options);
            auto logical_size = renderer.fitted_logical_size();
            renderer.set_logical_size(logical_size);
            const auto write_renderer_info=[&]{
                if(renderer_info_path.empty())return;
                if(diagnostic_house_capture && !house_report.at("complete").get<bool>())return;
                if (renderer_info_path.has_parent_path())
                    std::filesystem::create_directories(renderer_info_path.parent_path());
                const auto &device = renderer.device_info();
                const auto &present = renderer.presentation_status();
                const auto display = renderer.display_info();
                const auto mode_name = [](ribterra::render::PresentMode mode) {
                    switch (mode) {
                    case ribterra::render::PresentMode::VSync: return "vsync";
                    case ribterra::render::PresentMode::Immediate: return "immediate";
                    case ribterra::render::PresentMode::Mailbox: return "mailbox";
                    }
                    throw std::invalid_argument("Unknown presentation mode");
                };
                const auto request_json = [&](ribterra::render::PresentationRequest request) {
                    return nlohmann::json{{"mode", mode_name(request.mode)},
                        {"frame_limit", request.frame_limit}, {"frames_in_flight", request.frames_in_flight}};
                };
                Json information{{"engine_version", ribterra::engine_version},
                                       {"window_title", SDL_GetWindowTitle(window.get())},
                                       {"backend", device.backend},
                                       {"device", device.device},
                                       {"driver", device.driver},
                                       {"software", device.software},
                                       {"drawable", {display.drawable.width, display.drawable.height}},
                                       {"refresh_hz", display.refresh_hz},
                                       {"presentation", {{"requested", request_json(present.requested)},
                                            {"effective", request_json(present.effective)},
                                            {"GPU_controls_available", present.available},
                                            {"immediate_supported", present.immediate_supported},
                                            {"mailbox_supported", present.mailbox_supported},
                                            {"reason", static_cast<int>(present.reason)},
                                            {"error", present.error}}}};
                if (diagnostic_house_capture) {
                    information["diagnostic_house"] = {{"mode","diagnostic-house-capture"},
                        {"report",diagnostic_report_path.filename().generic_string()},{"complete",true},
                        {"report_sha256",ribterra::sha256(diagnostic_bytes(diagnostic_report_path))},
                        {"house_id",house_report.at("house").at("id")},
                        {"body_changed_pixels",house_report.at("pixels").at("changed")},
                        {"untouched_startup_visibility",false}};
                    diagnostic_exclusive(renderer_info_path,information.dump()+"\n");
                } else {
                    std::ofstream info(renderer_info_path);
                    info << information << '\n';
                    if (!info) throw std::runtime_error("Failed to write renderer information");
                }
            };
            write_renderer_info();
            // Hidden captures must neither move the desktop pointer nor read
            // or update the player's ordinary profile. Keep their settings and
            // local host saves beside their own output, including --smoke.
            auto prefs = automatic ? automatic_preferences(capture_path) : preferences();
            std::filesystem::create_directories(prefs);
            const auto save_prefs = prefs / "climate-v1";
            std::filesystem::create_directories(save_prefs);
            auto save_path = save_prefs / "settlement.save";
            {std::ifstream previous(save_prefs/"local-last.txt");std::string name;std::getline(previous,name);
             if(!name.empty()&&std::filesystem::path(name).filename()==std::filesystem::path(name))save_path=save_prefs/name;}
            const auto remember_save=[&] {std::ofstream latest(save_prefs/"local-last.txt");latest<<save_path.filename().string();};
            std::unique_ptr<ribterra::net::NetworkServer> online_host;
            double host_accumulator=0;
            std::string online_profile_key,stored_resume_token;
            nlohmann::json online_profiles=nlohmann::json::object();
            try{std::ifstream profiles(prefs/"online-profiles.json");if(profiles)profiles>>online_profiles;if(!online_profiles.is_object())online_profiles=nlohmann::json::object();}catch(...){online_profiles=nlohmann::json::object();}
            std::unique_ptr<ribterra::ClientSession> world;
            const bool start_world = (automatic && !menu_capture && !host_lobby_capture) || demo_scene || !endpoint.empty();
            if (start_world)
                world = std::make_unique<ribterra::ClientSession>(seed, player_name);
            if (!endpoint.empty()) {
                std::string error;
                if (!world->connect_remote(
                        endpoint, ribterra::environment_variable("RIBTERRA_SERVER_PASSWORD").value_or(""),
                        error))
                    throw std::runtime_error("Connection failed: " + error);
            } else if (demo_scene)
                demo(*world);
            ribterra::GameView view(renderer);
            view.set_logical_size(logical_size);
            const auto catalog_root = catalog_assets_path.empty() ? base / "assets" : catalog_assets_path;
            ribterra::AssetCatalog assets(catalog_root);
            view.load_art(assets);
            std::unique_ptr<ribterra::AssetCatalog> material_assets;
            if (!material_collection_path.empty()) {
                material_assets = std::make_unique<ribterra::AssetCatalog>(material_collection_path,
                    material_collection_textured ? ribterra::AssetCatalogReadProfile::MaterialCollectionTextured
                                                 : ribterra::AssetCatalogReadProfile::MaterialCollection);
                view.load_material_collection(*material_assets);
            }
            std::optional<DiagnosticSource> house_source;
            int completed_house_id{};
            if (diagnostic_house_capture) {
                diagnostic_require(world && material_assets && renderer.device_info().backend == "direct3d12" &&
                    !renderer.device_info().software && view.has_registered_material(diagnostic_house_key),
                    "House diagnostic requires an accepted registered neutral house on hardware D3D12");
                house_report["stage"] = "bound_neutral_source";
                house_source = diagnostic_source(assets,*material_assets,house_report);
                completed_house_id = diagnostic_build_house(*world,house_report);
                view.set_layer(ribterra::WorldLayer::Surface);
                SDL_Event camera{}; camera.type = SDL_EVENT_KEY_DOWN; camera.key.key = SDLK_C;
                view.handle_event(camera,*world);
                SDL_Event pause{}; pause.type = SDL_EVENT_KEY_DOWN; pause.key.key = SDLK_SPACE;
                view.handle_event(pause,*world);
                diagnostic_require(view.paused() && view.layer() == ribterra::WorldLayer::Surface,
                    "Existing SDL camera/pause controls did not settle the diagnostic view");
                const auto camp = house_report.at("camp");
                house_report["camera"] = {{"requested",{{"SDL_key","C"},{"center",{camp[1],camp[2]}},{"zoom",diagnostic_zoom}}},
                    {"paused_via_SDL_SPACE",view.paused()},{"selected_buildings",view.selected_building_ids()},
                    {"effective_projection","source-derived actual tile queries and unique body quad below"}};
                house_report["backend"] = renderer.device_info().backend;
                house_report["device"] = renderer.device_info().device;
                house_report["driver"] = renderer.device_info().driver;
            }
            if (demo_scene && world && !world->residents().empty())
                view.focus_group(*world, {world->residents().front().id});
            ribterra::Localization localization(catalog_root / "locales");
            ribterra::GameUi ui(renderer, assets, localization, prefs / "ui-settings.json", logical_size,
                               !presentation_explicit);
            if (start_world && (!world->remote() || world->world_ready()))
                ui.show_world();
            if (automatic) {
                SDL_Event pointer{};
                pointer.type = SDL_EVENT_MOUSE_MOTION;
                pointer.motion.x = logical_size.width * .5f;
                pointer.motion.y = logical_size.height * .5f;
                ui.process_event(pointer);
                if (world) view.handle_event(pointer, *world);
            }
            if(host_lobby_capture||online_capture){ui.activate_for_test("open-online");ui.update(nullptr);}
            if(host_lobby_capture)ui.activate_for_test("online-host");
            ribterra::render::Frame2D frame;
            bool running = true;
            int frames = 0;
            int remote_ready_frames = 0;
            double accumulator = 0.0;
            const auto frequency = static_cast<double>(SDL_GetPerformanceFrequency());
            auto previous = SDL_GetPerformanceCounter();
            const auto start = previous;
            const auto automatic_started = SDL_GetTicks();
            ribterra::render::FrameLimiter frame_limiter;
            auto active_frame_limit = presentation.frame_limit;
            frame_limiter.set_limit(active_frame_limit);
            frame_limiter.reset(SDL_GetTicksNS());
            std::filesystem::path requested_capture;
            std::string ui_status;
            bool was_reconnecting = false,online_centered=false;
            bool awaiting_remote_world = world && world->remote() && !world->world_ready();
            const auto sync_presentation = [&] {
                const auto fitted = renderer.fitted_logical_size();
                if (fitted.width == logical_size.width && fitted.height == logical_size.height)
                    return;
                logical_size = fitted;
                renderer.set_logical_size(logical_size);
                view.set_logical_size(logical_size);
                ui.set_logical_size(logical_size);
            };
            while (running) {
                sync_presentation();
                SDL_Event event;
                if(online_host)online_host->poll();
                if (world) {
                    world->poll();
                    if (world->remote() && ui.world_visible() && !world->world_ready()) {
                        awaiting_remote_world = true;
                        online_centered = false;
                    }
                    if (awaiting_remote_world && world->world_ready()) {
                        ui.show_world();
                        awaiting_remote_world = false;
                    }
                    if(world->remote()&&world->ready()&&world->lobby().started&&!online_centered&&!world->residents().empty()){view.focus_group(*world,{world->residents().front().id});online_centered=true;}
                    if(world->remote()&&world->ready()&&!online_profile_key.empty()){
                        const auto token=world->resume_token();if(!token.empty()&&token!=stored_resume_token){stored_resume_token=token;online_profiles[online_profile_key]=token;std::ofstream profiles(prefs/"online-profiles.json");profiles<<online_profiles.dump();}
                    }
                    while (auto reply = world->take_command_reply())
                        ui_status = ui.format_command_reply(*reply);
                    if (auto notice = world->take_notice())
                        ui_status = localization.text_or_literal(*notice);
                    if (world->recovery_incomplete())
                        ui_status = localization.text("network.recovery_incomplete");
                    else if (world->reconnecting())
                        ui_status = localization.text("network.reconnecting");
                    else if (was_reconnecting && world->remote())
                        ui_status = localization.text(world->ready() ? "network.resumed" : "network.reconnect_failed");
                    was_reconnecting = world->reconnecting();
                }
                while (SDL_PollEvent(&event)) {
                    sync_presentation();
                    if (event.type == SDL_EVENT_QUIT) {
                        running = false;
                        continue;
                    }
                    renderer.convert_event(event);
                    view.observe_input(event);
                    if(world && view.selection_drag_active() &&
                       (event.type==SDL_EVENT_MOUSE_MOTION ||
                        (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT))) {
                        view.handle_event(event,*world);continue;
                    }
                    if (ui.process_event(event))
                        continue;
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        std::string error;
                        if (event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN) {
                            if (event.key.key == SDLK_PAGEDOWN && (!world || world->portals().empty())) {
                                ui_status = localization.text("status.mine_required");
                                continue;
                            }
                            view.set_layer(event.key.key == SDLK_PAGEUP ? ribterra::WorldLayer::Surface
                                                                        : ribterra::WorldLayer::Mine);
                            ui_status = localization.text(
                                event.key.key == SDLK_PAGEUP ? "status.layer_surface" : "status.layer_mine");
                            continue;
                        }
                        if (event.key.key == SDLK_F5) {
                            if (world)
                                ui_status =
                                    (online_host ? online_host->save() : world->save(save_path, error))
                                        ? (online_host?localization.text("status.saved"):(remember_save(),localization.text("status.saved")))
                                        : localization.format("status.save_failed", {{"reason", error}});
                            continue;
                        }
                        if (event.key.key == SDLK_F9) {
                            if(world&&world->remote()){ui_status=localization.text("save.remote");continue;}
                            if (world && world->load(save_path, error)) {
                                view.reset_world_state();
                                ui.reset_world_state();
                                accumulator = 0;
                                previous = SDL_GetPerformanceCounter();
                                save_path=world->last_load().save_path;remember_save();
                                ui_status = ui.load_status(world->last_load());
                            } else if (world)
                                ui_status = ui.load_status(world->last_load(),error);
                            continue;
                        }
                        if (event.key.key == SDLK_F12) {
                            requested_capture =
                                prefs / ("screenshot-" + std::to_string(SDL_GetTicks()) + ".png");
                            continue;
                        }
                        if (event.key.key == SDLK_F1) {
                            ui.show_help();
                            continue;
                        }
                    }
                    if (world) {
                        const bool was_worksite = view.worksite_armed();
                        view.handle_event(event, *world);
                        if (auto error = view.take_worksite_error())
                            ui_status = localization.format("status.order_rejected", {{"reason", *error}});
                        else if (was_worksite && !view.worksite_armed())
                            ui_status.clear();
                    }
                }
                bool replaced_world = false;
                for (auto &action : ui.take_actions()) {
                    // All actions in this batch captured the previous world.
                    // A replacement ends that epoch, including already queued IDs.
                    if (replaced_world)
                        break;
                    std::string error;
                    switch (action.type) {
                    case ribterra::UiActionType::PresentationSettings:
                        renderer.set_presentation(action.presentation);
                        ui.presentation_applied();
                        write_renderer_info();
                        break;
                    case ribterra::UiActionType::HostGame:
                    case ribterra::UiActionType::JoinGame: {
                        try {
                            const auto settings=nlohmann::json::parse(action.text);
                            const bool fresh_host = action.type == ribterra::UiActionType::HostGame && !action.value;
                            const auto session_seed = fresh_host ? seed_for_fresh_world() : seed;
                            auto candidate=std::make_unique<ribterra::ClientSession>(session_seed,settings.at("name").get<std::string>());
                            std::unique_ptr<ribterra::net::NetworkServer> host;
                            std::string address=settings.at("address"), password=settings.at("password");
                            if(action.type==ribterra::UiActionType::HostGame){
                                if(online_host)throw std::runtime_error("An online world is already hosted. Leave it first.");
                                ribterra::net::HostConfig config;config.game.seed=session_seed;config.game.max_players=std::stoul(settings.at("capacity").get<std::string>());config.game.lobby_enabled=true;config.game.wildlife_enabled=true;
                                config.password=password;config.transport.bind_address="0.0.0.0";
                                const auto colon=address.rfind(':');if(colon!=std::string::npos){const auto port=std::stoul(address.substr(colon+1));if(!port||port>65535)throw std::runtime_error("Invalid port");config.transport.port=static_cast<std::uint16_t>(port);}
                                if(action.value){std::ifstream previous_world(save_prefs/"online-last.txt");std::string filename;std::getline(previous_world,filename);if(filename.empty()||std::filesystem::path(filename).filename()!=std::filesystem::path(filename))throw std::runtime_error("No saved online world.");config.save_path=save_prefs/filename;}
                                else config.save_path=save_prefs/("online-"+ribterra::secure_token(16)+".save");
                                if(host_lobby_capture)config.transport.port=29881;
                                host=std::make_unique<ribterra::net::NetworkServer>(config);
                                if(!host->listen())throw std::runtime_error(host->last_error());
                                address="127.0.0.1:"+std::to_string(host->port());
                                std::ofstream latest(save_prefs/"online-last.txt");latest<<host->save_path().filename().string();
                            }
                            online_profile_key=address+"|"+settings.at("name").get<std::string>();stored_resume_token.clear();
                            std::string resume;if(action.type==ribterra::UiActionType::JoinGame||action.value)resume=online_profiles.value(online_profile_key,std::string{});
                            // A locally continued save belongs to its recorded host,
                            // even if the form name or listening port has changed.
                            if (host && action.value)
                                resume = host->game().local_host_resume_token();
                            candidate->connection_label=address;
                            if (host) {
                                for (const auto &adapter : ribterra::local_ipv4_interfaces()) {
                                    const auto invite_endpoint = adapter.address + ":" + std::to_string(host->port());
                                    candidate->connection_addresses.push_back(
                                        {invite_endpoint, adapter.adapter_name + " — " + invite_endpoint});
                                }
                                if (!candidate->connection_addresses.empty())
                                    candidate->connection_label = candidate->connection_addresses.front().endpoint;
                            }
                            if(!candidate->connect_remote(address,password,error,resume))throw std::runtime_error(error);
                            world=std::move(candidate);online_host=std::move(host);host_accumulator=0;online_centered=false;
                            awaiting_remote_world=true;
                            view.reset_world_state();ui.reset_world_state();replaced_world=true;
                            ui_status=online_host&&action.value?ui.load_status(online_host->game().last_load()):std::string{};
                        }catch(const std::exception&e){ui_status=e.what();}
                        break;
                    }
                    case ribterra::UiActionType::LeaveOnline:
                        world.reset();online_host.reset();online_profile_key.clear();awaiting_remote_world=false;ui.reset_world_state();ui.show_menu();view.reset_world_state();replaced_world=true;break;
                    case ribterra::UiActionType::LobbyCommand:
                        if(world){auto result=world->lobby_action(ribterra::LobbyAction(action.value),action.quantity);if(!result.accepted)ui_status=result.message;}break;
                    case ribterra::UiActionType::NewGame:
                        save_path=save_prefs/("settlement-"+ribterra::secure_token(16).substr(0,12)+".save");
                        online_host.reset();
                        world = std::make_unique<ribterra::ClientSession>(seed_for_fresh_world(), player_name);
                        awaiting_remote_world = false;
                        view.reset_world_state();
                        ui.reset_world_state();
                        replaced_world = true;
                        accumulator = 0;
                        previous = SDL_GetPerformanceCounter();
                        ui_status.clear();
                        view.set_paused(false);
                        view.set_layer(ribterra::WorldLayer::Surface);
                        ui.show_world();
                        break;
                    case ribterra::UiActionType::ContinueGame: {
                        view.cancel_worksite();
                        if (world) {
                            ui.show_world();
                            break;
                        }
                        auto candidate = std::make_unique<ribterra::ClientSession>(seed, player_name);
                        if (!candidate->load(save_path, error))
                            ui_status = ui.load_status(candidate->last_load(),error);
                        else {
                            online_host.reset();
                            world = std::move(candidate);
                            view.reset_world_state();
                            ui.reset_world_state();
                            replaced_world = true;
                            accumulator = 0;
                            previous = SDL_GetPerformanceCounter();
                            save_path=world->last_load().save_path;remember_save();
                            ui_status = ui.load_status(world->last_load());
                            ui.show_world();
                        }
                    } break;
                    case ribterra::UiActionType::Quit:
                        running = false;
                        break;
                    case ribterra::UiActionType::Save:
                        if (world)
                            ui_status = (online_host ? online_host->save() : world->save(save_path, error))
                                            ? (online_host?localization.text("status.saved"):(remember_save(),localization.text("status.saved")))
                                            : localization.format("status.save_failed", {{"reason", error}});
                        break;
                    case ribterra::UiActionType::Load: {
                        view.cancel_worksite();
                        auto candidate = std::make_unique<ribterra::ClientSession>(seed, player_name);
                        if (world && world->remote()) {
                            ui_status = localization.text("save.remote");
                        } else if (candidate->load(save_path, error)) {
                            online_host.reset();
                            world = std::move(candidate);
                            view.reset_world_state();
                            ui.reset_world_state();
                            replaced_world = true;
                            accumulator = 0;
                            previous = SDL_GetPerformanceCounter();
                            save_path=world->last_load().save_path;remember_save();
                            ui_status = ui.load_status(world->last_load());
                            ui.show_world();
                        } else ui_status = ui.load_status(candidate->last_load(),error);
                        break;
                    }
                    case ribterra::UiActionType::Pause:
                        if (world) {
                            if (world->remote())
                                {auto result=world->lobby_action(ribterra::LobbyAction::Pause,world->lobby().paused?0:1);if(!result.accepted)ui_status=result.message;}
                            else
                                view.set_paused(!view.paused());
                        }
                        break;
                    case ribterra::UiActionType::Speed:
                        if (world) {
                            if (world->remote())
                                {auto result=world->lobby_action(ribterra::LobbyAction::Speed,action.value);if(!result.accepted)ui_status=result.message;}
                            else {
                                view.set_speed(action.value);
                                view.set_paused(false);
                            }
                        }
                        break;
                    case ribterra::UiActionType::Build:
                        if (world)
                            view.arm_build(action.building);
                        break;
                    case ribterra::UiActionType::CancelConstruction:
                        if (world) {
                            const auto result = world->cancel_construction(action.value);
                            if (!result.accepted)
                                ui_status = localization.format("status.order_rejected",
                                                                {{"reason", localization.text_or_literal(result.message)}});
                        }
                        break;
                    case ribterra::UiActionType::AssignProfession:
                    case ribterra::UiActionType::Stop:
                        view.cancel_worksite();
                        ui_status.clear();
                        if (world) {
                            const auto result =
                                action.type == ribterra::UiActionType::Stop
                                    ? world->stop(action.residents)
                                    : world->assign_profession(action.residents, action.assignment);
                            if (!result.accepted)
                                ui_status = localization.format("status.order_rejected",
                                                                {{"reason", localization.text_or_literal(result.message)}});
                        }
                        break;
                    case ribterra::UiActionType::ChooseWorksite:
                        if (world && !action.residents.empty()) {
                            view.arm_worksite(action.assignment, action.residents);
                            ui_status = localization.text("profession.choose_hint");
                        }
                        break;
                    case ribterra::UiActionType::ClearBuildingSelection:
                        view.clear_building_selection(); break;
                    case ribterra::UiActionType::FocusMap:
                        if(world)view.focus_minimap(*world,action.x/10000.f,action.y/10000.f);
                        break;
                    case ribterra::UiActionType::CancelInteraction:
                        view.cancel_interaction();
                        ui_status.clear();
                        break;
                    case ribterra::UiActionType::BuildingProduction:
                        if(world){auto result=world->production_group(action.buildings,action.recipe,action.quantity,action.residents.empty()?0:action.residents.front());if(!result.accepted)ui_status=localization.text_or_literal(result.message);} break;
                    case ribterra::UiActionType::ManageProductionWorker:
                        if(world){view.focus_group(*world,{action.value});
                            ui.set_view_state(world.get(),view.selected_ids(),view.paused(),view.speed(),view.layer(),view.selected_building_ids());
                            ui.show_profession();}
                        break;
                    case ribterra::UiActionType::Production:
                    case ribterra::UiActionType::Upgrade:
                    case ribterra::UiActionType::CancelProduction:
                        if(world){
                            auto result=action.type==ribterra::UiActionType::CancelProduction?world->cancel_production(action.value,action.settlement_revision):world->production(action.value,action.residents.empty()?0:action.residents.front(),action.recipe,action.quantity,action.settlement_revision,action.type==ribterra::UiActionType::Upgrade);
                            if(!result.accepted)ui_status=localization.text_or_literal(result.message);
                        }
                        break;
                    case ribterra::UiActionType::SettlementDecision:
                        if(world) {
                            const auto result=world->decide(static_cast<ribterra::SettlementDecision>(action.value),action.settlement_revision,
                                                            action.residents.empty()?0:action.residents.front());
                            if(!result.accepted)ui_status=localization.text_or_literal(result.message);
                        }
                        break;
                    case ribterra::UiActionType::GatherMode:
                        view.set_gather_completion(action.value == 1
                                                       ? ribterra::GatherCompletion::OneDelivery
                                                       : ribterra::GatherCompletion::UntilExhausted);
                        break;
                    case ribterra::UiActionType::CancelManualOrder:
                    case ribterra::UiActionType::ReleaseControl:
                    case ribterra::UiActionType::TransferControl:
                        if (world) {
                            if (action.control_epoch != world->control_epoch()) {
                                ui_status = localization.text("manual.control_failure.4");
                                break;
                            }
                            view.cancel_worksite();
                            const auto result =
                                action.type == ribterra::UiActionType::CancelManualOrder
                                    ? world->cancel_manual_order(action.order_id)
                                : action.type == ribterra::UiActionType::ReleaseControl
                                    ? world->release_control(action.residents)
                                    : world->transfer_control(action.residents, action.target_player);
                            if (!result.accepted)
                                ui_status = localization.format("status.order_rejected",
                                                                {{"reason", localization.text_or_literal(result.message)}});
                        }
                        break;
                    case ribterra::UiActionType::Chat:
                        if (world) {
                            if (!world->send_chat(action.text, error))
                                ui_status = localization.format("status.chat_failed", {{"reason", localization.text_or_literal(error)}});
                            else
                                ui.clear_chat_input();
                        }
                        break;
                    case ribterra::UiActionType::Menu:
                        view.cancel_worksite();
                        break;
                    case ribterra::UiActionType::Layer:
                        if (action.value == 1 && (!world || world->portals().empty())) {
                            ui_status = localization.text("status.mine_required");
                            break;
                        }
                        view.set_layer(action.value == 0 ? ribterra::WorldLayer::Surface
                                                         : ribterra::WorldLayer::Mine);
                        ui_status = localization.text(action.value == 0 ? "status.layer_surface"
                                                                        : "status.layer_mine");
                        break;
                    case ribterra::UiActionType::Passage:
                        if (!world || view.selected_ids().empty()) {
                            ui_status = localization.text("status.passage_no_selection");
                            break;
                        } else {
                            const auto first =
                                std::find_if(world->residents().begin(), world->residents().end(),
                                             [&](const auto &resident) {
                                                 return resident.id == view.selected_ids().front();
                                             });
                            if (first == world->residents().end() || world->portals().empty()) {
                                ui_status = localization.text("status.passage_unavailable");
                                break;
                            }
                            const ribterra::Portal *chosen = nullptr;
                            float best_distance = std::numeric_limits<float>::max();
                            for (const auto &portal : world->portals()) {
                                const auto &entrance = first->layer == ribterra::WorldLayer::Surface
                                                           ? portal.surface
                                                           : portal.mine;
                                const float distance =
                                    std::abs(first->x - entrance.x) + std::abs(first->y - entrance.y);
                                if (distance < best_distance) {
                                    chosen = &portal;
                                    best_distance = distance;
                                }
                            }
                            const auto &destination = first->layer == ribterra::WorldLayer::Surface
                                                          ? chosen->mine
                                                          : chosen->surface;
                            const auto result = world->move(view.selected_ids(), destination.x, destination.y,
                                                            destination.layer);
                            if (!result.accepted)
                                ui_status = localization.format("status.passage_failed",
                                                                {{"reason", localization.text_or_literal(result.message)}});
                            else {
                                view.set_layer(destination.layer);
                                ui_status = localization.text("status.passage_ordered");
                            }
                        }
                        break;
                    }
                }
                const auto now = SDL_GetPerformanceCounter();
                const double delta = std::min(0.25, static_cast<double>(now - previous) / frequency);
                previous = now;
                if(online_host){host_accumulator+=delta;int steps=0;while(host_accumulator>=ribterra::tick_seconds&&steps++<20){online_host->tick();host_accumulator-=ribterra::tick_seconds;}}
                if(world&&world->remote()&&world->lobby().enabled){view.set_paused(world->lobby().paused);view.set_speed(world->lobby().speed);}
                if (world && ui.world_visible() && !world->remote() && !view.paused()) {
                    accumulator += delta * view.speed();
                    int catch_up = 0;
                    while (accumulator >= ribterra::tick_seconds && catch_up < 20) {
                        world->advance();
                        accumulator -= ribterra::tick_seconds;
                        ++catch_up;
                    }
                } else
                    accumulator = 0;
                view.set_annotations_visible(ui.hud_visible());
                if (world && ui.world_visible())
                    view.compose(*world, static_cast<double>(now - start) / frequency, frame, false);
                else {
                    frame.logical_size = logical_size;
                    frame.clear = {0, 0, 0, 255};
                    frame.clear_commands();
                }
                if(world&&!world->remote()&&!world->economy().wilderness.enabled)world->wilderness(ribterra::WildernessAction::Populate,{},0,0,0,world->economy().wilderness.revision);
                ui.set_view_state(world.get(), view.selected_ids(), view.paused(), view.speed(),
                                  view.layer(), view.selected_building_ids(), view.interaction_active());
                if (auto notice = view.take_status_text(localization)) ui_status = *notice;
                ui.update(world.get(), ui_status);
                ui.compose(frame);
                view.compose_cursor(frame);
                std::filesystem::path frame_capture;
                if (!requested_capture.empty())
                    frame_capture = requested_capture;
                if (automatic && !host_lobby_capture && ((!world || !world->remote()) || menu_capture) && frames == 2 &&
                    !capture_path.empty())
                    frame_capture = capture_path;
                if (automatic && world && world->remote() && !menu_capture) {
                    remote_ready_frames = world->ready() ? remote_ready_frames + 1 : 0;
                    if (remote_ready_frames == 3 && !capture_path.empty())
                        frame_capture = capture_path;
                    if (remote_ready_frames < 3 && SDL_GetTicks() - automatic_started >= 15000)
                        throw std::runtime_error("Remote session was not ready within 15 seconds");
                }
                if(host_lobby_capture&&SDL_GetTicks()-automatic_started>=15000&&(!world||!world->ready()))throw std::runtime_error("Host form did not create a ready lobby: "+ui_status);
                if (diagnostic_house_capture && !frame_capture.empty()) {
                    diagnostic_require(house_source.has_value() && frame_capture == capture_path,
                        "Unexpected capture request in the house diagnostic");
                    diagnostic_capture(renderer,frame,view,*world,completed_house_id,*house_source,
                        capture_path,diagnostic_removed_path,diagnostic_report_path,house_report);
                    write_renderer_info();
                } else renderer.render(frame, frame_capture);
                if (!requested_capture.empty()) {
                    ui_status =
                        localization.format("status.capture_saved", {{"path", to_utf8(requested_capture)}});
                    requested_capture.clear();
                }
                ++frames;
                if (automatic && (((!host_lobby_capture&&(!world || !world->remote() || menu_capture)) && frames >= 3) ||
                                  (world && world->remote() && !menu_capture && remote_ready_frames >= 3)))
                    running = false;
                const auto effective_limit = renderer.presentation_status().effective.frame_limit;
                if (effective_limit != active_frame_limit) {
                    active_frame_limit = effective_limit;
                    frame_limiter.set_limit(active_frame_limit);
                    frame_limiter.reset(SDL_GetTicksNS());
                }
                if (active_frame_limit != 0) {
                    const auto delay = frame_limiter.delay_ns(SDL_GetTicksNS());
                    if (delay != 0) SDL_DelayPrecise(delay);
                } else if ((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0)
                    SDL_Delay(10);
                else if (automatic && world && world->remote())
                    SDL_Delay(1);
            }
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        if (diagnostic_house_capture && diagnostic_outputs_ready) {
            try {
                if (!fs::exists(diagnostic_report_path)) {
                    house_report["complete"] = false;
                    house_report["error"] = error.what();
                    diagnostic_exclusive(diagnostic_report_path,house_report.dump(2)+"\n");
                }
            } catch (const std::exception& report_error) { SDL_Log("House diagnostic failure report: %s",report_error.what()); }
        }
        SDL_Log("Ribterra: %s", error.what());
        if (!smoke && capture_path.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ribterra", error.what(), nullptr);
        SDL_Quit();
        return 1;
    }
}

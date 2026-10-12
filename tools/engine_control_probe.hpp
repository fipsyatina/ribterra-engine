#pragma once

#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace ribterra;
using namespace ribterra::render;
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr Extent extent{1920,1080};
constexpr std::uintmax_t budget = 128u * 1024u * 1024u;
inline void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
inline fs::path argument(const char* value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value)));
}
inline void check_budget(const fs::path& root) {
    std::uintmax_t bytes{};
    for (const auto& file : fs::recursive_directory_iterator(root)) {
        require(!file.is_symlink(), "Evidence symlink is forbidden");
        if (file.is_regular_file()) bytes += file.file_size();
    }
    require(bytes <= budget && fs::space(root).available >= 384u * 1024u * 1024u,
            "Native output storage budget exceeded");
}
inline void save_report(const fs::path& path, const Json& value) {
    require(!fs::exists(path), "Refusing to overwrite native report");
    std::ofstream out(path, std::ios::binary);
    out << value.dump(2) << '\n';
    out.flush();
    require(out.good(), "Native report write failed");
}
inline void save_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    require(!fs::exists(path), "Refusing to overwrite raw GPU readback");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    require(out.good(), "Raw GPU readback write failed");
}
inline Json carrier_counts(const WaterCoverageReadback& carrier) {
    require(carrier.size.width > 0u && carrier.size.height > 0u &&
            carrier.rgba8.size() == std::size_t(carrier.size.width) * carrier.size.height * 4u,
            "Raw WaterCoverage dimensions/packed bytes are invalid");
    require(carrier.gpu_row_pitch >= carrier.size.width * 4u && carrier.gpu_row_pitch % 256u == 0u,
            "Raw WaterCoverage GPU pitch is not aligned");
    std::size_t clear{}, land{}, water{}, deck{}, shore{};
    for (std::size_t offset = 0; offset < carrier.rgba8.size(); offset += 4u) {
        const auto* value = carrier.rgba8.data() + offset;
        if (value[0] == 0u && value[1] == 0u && value[2] == 0u && value[3] == 0u) ++clear;
        else if (value[0] == 0u && value[1] == 1u && value[2] == 0u && value[3] == 0u) ++land;
        else if (value[0] == 0u && value[1] == 3u && value[2] == 0u && value[3] == 0u) ++deck;
        else if (value[0] == 255u && value[1] == 2u && value[2] % 17u == 0u && value[3] == 255u) {
            ++water;
            if (value[2] != 0u) ++shore;
        } else require(false, "Raw WaterCoverage contains an invalid class/coverage/shore/allow tuple");
    }
    require(clear > 0u && land > 0u && water > 0u && deck > 0u && shore > 0u,
            "Raw WaterCoverage classification check is vacuous");
    return {{"width",carrier.size.width},{"height",carrier.size.height},
        {"gpu_row_pitch",carrier.gpu_row_pitch},{"packed_bytes",carrier.rgba8.size()},
        {"frame_slot",carrier.frame_slot},{"target_generation",carrier.target_generation},
        {"clear_pixels",clear},{"land_pixels",land},{"water_pixels",water},
        {"bridge_deck_pixels",deck},{"shore_water_pixels",shore},{"invalid_tuples",0},
        {"coverage_scope","exact binary carrier; intermediate AA coverage not implemented"}};
}
inline Json stable_targets(const GroundForwardStatus& status) {
    return {{"available",status.available},{"format_supported",status.format_supported},
        {"target_formats",status.target_formats},{"width",status.width},{"height",status.height},
        {"frame_sets",status.frame_sets},{"attachment_bytes",status.attachment_bytes},
        {"target_generation",status.target_generation},{"active_set_ids",status.active_set_ids},
        {"reflection_width",status.reflection_width},{"reflection_height",status.reflection_height},
        {"coverage_bytes",status.coverage_bytes},{"reflection_bytes",status.reflection_bytes},
        {"reason",status.reason}};
}
inline Json resize_window(SDL_Window* window, Renderer2D& renderer, Extent requested) {
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    require(SDL_SetWindowSize(window,static_cast<int>(requested.width),static_cast<int>(requested.height)) &&
            SDL_SyncWindow(window),"Native window resize failed");
    SDL_PumpEvents();
    while (SDL_PollEvent(&event)) {}
    int logical_width{},logical_height{},pixel_width{},pixel_height{};
    require(SDL_GetWindowSize(window,&logical_width,&logical_height) &&
            SDL_GetWindowSizeInPixels(window,&pixel_width,&pixel_height),"Native size query failed");
    const auto display=renderer.display_info();
    require(logical_width==static_cast<int>(requested.width) && logical_height==static_cast<int>(requested.height) &&
            pixel_width==static_cast<int>(requested.width) && pixel_height==static_cast<int>(requested.height) &&
            display.drawable.width==requested.width && display.drawable.height==requested.height,
            "Native window did not honor requested drawable size");
    return {{"logical_width",logical_width},{"logical_height",logical_height},
        {"drawable_width",pixel_width},{"drawable_height",pixel_height},
        {"pixel_density",display.pixel_density},{"display_scale",display.display_scale}};
}
inline void create_fixture(const fs::path& file) {
    World world(70421, 4);
    auto state = world.export_state();
    state.resources = {250,250,250};
    for (auto& stack : state.item_stacks) stack.quantity = 250;
    for (auto& chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            const int x = chunk.key.cx * chunk_size + static_cast<int>(index % chunk_size);
            const int y = chunk.key.cy * chunk_size + static_cast<int>(index / chunk_size);
            if (x < 15 || x > 40 || y < 13 || y > 37) continue;
            // Current-schema diagnostic world only, not the new procedural generator.
            const int center = y == 25 ? 28 : 28 + (y < 22 ? -1 : y > 29 ? 1 : 0);
            const bool water = x >= center - 1 && x <= center + 1;
            chunk.tiles[index] = water ? Tile{Terrain::Water,0,true,Biome::Lake}
                : Tile{Terrain::Grass,0,true,Biome::Meadow,static_cast<std::int16_t>(x < 27 ? 8 : 12)};
        }
    }
    std::string error;
    require(world.import_state(std::move(state),error),error);
    for (int x = 27; x <= 29; ++x) {
        require(world.build(BuildingKind::Bridge,x,25).accepted,"Fixture bridge build rejected");
        for (int tick = 0; tick < 3000 && !world.walkable(x,25); ++tick) world.tick();
        require(world.walkable(x,25) && world.tile(x,25).terrain == Terrain::Water,
                "Fixture bridge failed to complete without replacing water");
        require(world.construction_orders().back().phase == ConstructionPhase::Complete,
                "Fixture bridge completion phase is wrong");
    }
    state = world.export_state();
    require(state.economy.nations.empty(),"Fixture requires nation-1 tile visibility fallback");
    for (auto& chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            const int x = chunk.key.cx * chunk_size + static_cast<int>(index % chunk_size);
            const int y = chunk.key.cy * chunk_size + static_cast<int>(index / chunk_size);
            // Hide a continuation inside the camera window. Existing camp and
            // residents stay revealed; no tile terrain/elevation is regenerated.
            if (x >= 31 && y >= 30) chunk.tiles[index].explored = false;
        }
    }
    ++state.topology_revision;
    require(world.import_state(std::move(state),error),error);
    require(!fs::exists(file),"Fixture path already exists");
    require(world.save(file,error),error);
}
struct Image { std::vector<std::uint8_t> rgba; };
inline Image read_image(const fs::path& path) {
    png_image raw{};
    raw.version = PNG_IMAGE_VERSION;
    struct Free { png_image* image; ~Free() { png_image_free(image); } } free{&raw};
    const auto text = path.u8string();
    require(png_image_begin_read_from_file(&raw,reinterpret_cast<const char*>(text.c_str())) != 0,
            "Native PNG read failed");
    require(raw.width == extent.width && raw.height == extent.height,"Wrong native resolution");
    raw.format = PNG_FORMAT_RGBA;
    Image image{std::vector<std::uint8_t>(PNG_IMAGE_SIZE(raw))};
    require(png_image_finish_read(&raw,nullptr,image.rgba.data(),0,nullptr) != 0,"PNG decode failed");
    for (std::size_t pixel = 3; pixel < image.rgba.size(); pixel += 4u)
        require(image.rgba[pixel] == 255u,"Final scene/UI alpha is not opaque");
    return image;
}
inline void append_ui(Frame2D& frame) {
    const auto first = static_cast<std::uint32_t>(frame.vertices.size());
    const auto index = static_cast<std::uint32_t>(frame.indices.size());
    for (const Point point : std::array<Point,4>{{{12,12},{92,12},{92,52},{12,52}}}) {
        frame.vertices.push_back({point,{0,0},{31,189,78,255}});
        frame.surface_vertices.push_back({});
    }
    for (const auto offset : std::array<std::uint32_t,6>{0,1,2,0,2,3}) frame.indices.push_back(first+offset);
    GeometryDraw draw;
    draw.pass = Pass::Ui; draw.blend = Blend::Opaque; draw.first_index=index; draw.index_count=6;
    frame.commands.emplace_back(draw);
}
inline Image capture(Renderer2D& renderer, const Frame2D& source, const fs::path& root,
              std::string_view name, Json& records) {
    Frame2D frame = source;
    append_ui(frame);
    const auto path = root/(std::string(name)+".png");
    require(!fs::exists(path),"Refusing capture overwrite");
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    renderer.render(frame,path);
    const auto telemetry = renderer.frame_telemetry();
    require(telemetry.submitted && telemetry.capture && telemetry.graph_pass_count == 8u,
            "Water probe did not execute cumulative eight-pass graph without contact input");
    constexpr std::array<WorldGraphPass,8> order{WorldGraphPass::SunShadow,WorldGraphPass::MoonShadow,
        WorldGraphPass::Reflection,WorldGraphPass::GroundGBuffer,WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward,WorldGraphPass::Composite,WorldGraphPass::Ui};
    require(std::equal(order.begin(),order.end(),telemetry.graph_passes.begin()),"Wrong water graph order");
    records.push_back({{"file",path.filename().string()},{"bytes",fs::file_size(path)},
        {"water_draws",telemetry.water_draws},{"reflection_draws",telemetry.reflection_draws},
        {"graph_passes",telemetry.graph_pass_count},{"gpu_execution_ms",nullptr}});
    check_budget(root);
    return read_image(path);
}
// Diagnostic raster classifier, not GPU carrier readback or AA acceptance.
// Last ground receiver wins, matching native painter order. The one-physical-
// pixel boundary margin accommodates raster top-left/float differences.
inline std::vector<std::uint8_t> water_mask(const Frame2D& frame) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(extent.width)*extent.height);
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw || draw->ground_material_index == 0u) continue;
        const auto& material = frame.ground_material_draws.at(draw->ground_material_index-1u);
        const bool water = material.water_surface_class == GroundSurfaceClass::Water &&
                           material.surface.response[3] == 1.f;
        for (std::uint32_t offset = 0; offset < draw->index_count; offset += 3u) {
            std::array<Point,3> p{};
            for (std::uint32_t corner=0; corner<3u; ++corner)
                p[corner]=frame.vertices.at(frame.indices.at(draw->first_index+offset+corner)).position;
            const float denominator=(p[1].y-p[2].y)*(p[0].x-p[2].x)+(p[2].x-p[1].x)*(p[0].y-p[2].y);
            require(std::abs(denominator)>1.e-6f,"Degenerate receiver triangle");
            const int min_x=std::max(0,static_cast<int>(std::floor(std::min({p[0].x,p[1].x,p[2].x}))));
            const int max_x=std::min(1919,static_cast<int>(std::ceil(std::max({p[0].x,p[1].x,p[2].x}))));
            const int min_y=std::max(0,static_cast<int>(std::floor(std::min({p[0].y,p[1].y,p[2].y}))));
            const int max_y=std::min(1079,static_cast<int>(std::ceil(std::max({p[0].y,p[1].y,p[2].y}))));
            for (int y=min_y;y<=max_y;++y) for(int x=min_x;x<=max_x;++x) {
                const float px=static_cast<float>(x)+.5f,py=static_cast<float>(y)+.5f;
                const float a=((p[1].y-p[2].y)*(px-p[2].x)+(p[2].x-p[1].x)*(py-p[2].y))/denominator;
                const float b=((p[2].y-p[0].y)*(px-p[2].x)+(p[0].x-p[2].x)*(py-p[2].y))/denominator;
                if(a>=0.f&&b>=0.f&&a+b<=1.f) mask[static_cast<std::size_t>(y)*extent.width+x]=water?1u:0u;
            }
        }
    }
    return mask;
}
inline std::size_t water_only_difference(const Image& a,const Image& b,const Frame2D& frame) {
    const auto mask=water_mask(frame);
    std::size_t changed{};
    for(int y=0;y<1080;++y) for(int x=0;x<1920;++x) {
        const auto pixel=static_cast<std::size_t>(y)*extent.width+x;
        if(std::equal(a.rgba.begin()+static_cast<std::ptrdiff_t>(pixel*4u),
                      a.rgba.begin()+static_cast<std::ptrdiff_t>(pixel*4u+4u),
                      b.rgba.begin()+static_cast<std::ptrdiff_t>(pixel*4u))) continue;
        ++changed;
        bool near_water{};
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(x+dx>=0&&x+dx<1920&&y+dy>=0&&y+dy<1080)
                near_water=near_water||mask[static_cast<std::size_t>(y+dy)*extent.width+x+dx]!=0u;
        require(near_water,"Water/reflection changed a bank, deck or unknown pixel beyond 1px edge");
    }
    return changed;
}
} // namespace


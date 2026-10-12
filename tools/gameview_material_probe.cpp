#include "engine_control_probe.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/unit_animation.hpp"
#include "ribterra/unit_motion.hpp"
#include "ribterra/world_projection.hpp"
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {
constexpr std::size_t capture_limit = 20u;
constexpr int house_id = 2;
constexpr int resident_id = 1;
constexpr float house_width = 76.f * 2.1f;
constexpr float initial_zoom = 1.30f;
constexpr std::array<std::string_view,5> world_saves{
    "material-day.save","material-night.save","material-wet.save",
    "material-variants.save","material-winter.save"};

void check_no_reparse(fs::path path) {
    path=fs::absolute(path).lexically_normal();
    for (;;) {
        if (fs::exists(path)) {
            const auto attributes=GetFileAttributesW(path.c_str());
            require(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)==0u,
                "Reparse point is forbidden in input/output path");
        }
        const auto parent=path.parent_path(); if (parent==path || parent.empty()) break; path=parent;
    }
}

std::vector<std::uint8_t> file_bytes(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    require(input.good(),"Cannot read retained input: "+path.filename().string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::string json_hash(const Json& value) {
    const auto text = value.dump();
    return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}
std::string source_record_hash(const Json& value) {
    const auto text=value.dump()+"\n";
    return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}
Json file_row(const fs::path& path, const fs::path& root) {
    const auto bytes = file_bytes(path);
    return {{"path",fs::relative(path,root).generic_string()},{"bytes",bytes.size()},{"sha256",sha256(bytes)}};
}
Json tree_rows(const fs::path& root) {
    check_no_reparse(root);
    std::vector<fs::path> names;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        require(!entry.is_symlink(),"Symlink in retained fixture tree");
        check_no_reparse(entry.path());
        if (entry.is_regular_file()) names.push_back(entry.path());
    }
    std::sort(names.begin(),names.end()); Json rows=Json::array();
    for (const auto& path : names) rows.push_back(file_row(path,root));
    return rows;
}
void write_json(const fs::path& path, const Json& value) {
    require(!fs::exists(path),"Refusing output overwrite");
    std::ofstream output(path,std::ios::binary);
    output << value.dump() << '\n'; output.flush();
    require(output.good(),"Retained JSON write failed");
}

Json draw_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass),static_cast<int>(draw.blend),draw.texture.slot,
        draw.texture.generation,draw.first_index,draw.index_count,draw.scissor.x,draw.scissor.y,
        draw.scissor.w,draw.scissor.h,draw.scissor_enabled,draw.surface_lit,
        draw.sprite_material_index,draw.ground_material_index};
}
Json frame_state(const Frame2D& frame) {
    require(frame.ground_material_draws.empty() && frame.contact_shadows.empty() &&
        frame.local_occluders.empty() && frame.sky_shadow_triangles.empty() &&
        frame.reflection_draws.empty() && !frame.water_surface && !frame.atmosphere &&
        !frame.moon_lighting && !frame.contact_projection,
        "Normal LegacyCombined fixture unexpectedly contains cumulative payloads");
    Json result{{"content",static_cast<int>(frame.content)},
        {"size",{frame.logical_size.width,frame.logical_size.height}},
        {"clear",{frame.clear.r,frame.clear.g,frame.clear.b,frame.clear.a}},
        {"hdr",frame.linear_hdr_world},{"exposure",frame.world_exposure},
        {"indices",frame.indices},{"vertices",Json::array()},{"surfaces",Json::array()},
        {"commands",Json::array()},{"sprites",Json::array()},
        {"lighting",{frame.surface_lighting.ambient,frame.surface_lighting.sun_direction,
            frame.surface_lighting.sun_color_intensity,frame.surface_lighting.counts}},
        {"lights",Json::array()},{"water_normal",{frame.water_normal.slot,frame.water_normal.generation}}};
    for (const auto& v : frame.vertices)
        result["vertices"].push_back({v.position.x,v.position.y,v.uv.x,v.uv.y,v.color.r,v.color.g,v.color.b,v.color.a});
    for (const auto& v : frame.surface_vertices) result["surfaces"].push_back({v.world_position,v.normal});
    for (const auto& command : frame.commands) {
        if (const auto* draw=std::get_if<GeometryDraw>(&command)) result["commands"].push_back({{"geometry",draw_row(*draw)}});
        else {
            const auto& text=std::get<TextDraw>(command);
            result["commands"].push_back({{"text",{static_cast<int>(text.pass),text.text,text.position.x,text.position.y,
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
Json independently_loaded_legacy_state(const Frame2D& frame) {
    require(frame.sprite_material_draws.empty(),"Legacy equivalence comparator received typed material");
    auto result=frame_state(frame);
    // The two views cold-load the same bound original catalog independently.
    // Their GPU texture handles differ; retain the observed binding equivalence
    // classes in first-use order, never serialize process/resource identities.
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> bindings;
    for (auto& row : result["commands"]) if (row.contains("geometry")) {
        auto& draw=row["geometry"]; const auto slot=draw[2].get<std::uint32_t>();
        const auto generation=draw[3].get<std::uint32_t>();
        if (slot) {
            const auto key=std::pair{slot,generation};
            if (!bindings.contains(key)) bindings.emplace(key,static_cast<std::uint32_t>(bindings.size()+1u));
            draw[2]=bindings.at(key); draw[3]=1u;
        }
    }
    return result;
}
Json frame_contract(const Frame2D& frame) {
    const SurfaceVertex neutral{}; std::size_t neutral_count{},mismatch{},typed{},plain{},texts{};
    Json surfaces=Json::array(),roles=Json::array();
    for (const auto& surface : frame.surface_vertices) {
        surfaces.push_back({surface.world_position,surface.normal});
        if (surface.world_position==neutral.world_position && surface.normal==neutral.normal) ++neutral_count;
        else ++mismatch;
    }
    for (std::size_t ci=0u;ci<frame.commands.size();++ci) {
        const auto* draw=std::get_if<GeometryDraw>(&frame.commands[ci]); if (!draw) { ++texts; continue; }
        if (!draw->sprite_material_index) { ++plain; continue; } ++typed;
        std::set<std::uint32_t> references; Json colors=Json::array();
        for (std::uint32_t i=0u;i<draw->index_count;++i) references.insert(frame.indices.at(draw->first_index+i));
        for (const auto index : references) {
            const auto& color=frame.vertices.at(index).color;
            colors.push_back({index,color.r,color.g,color.b,color.a});
        }
        roles.push_back({{"command",ci},{"draw",draw_row(*draw)},{"referenced_vertices",std::move(colors)}});
    }
    return {{"content",static_cast<int>(frame.content)},{"hdr",frame.linear_hdr_world},
        {"vertex_count",frame.vertices.size()},{"index_count",frame.indices.size()},
        {"command_count",frame.commands.size()},{"sprite_material_count",frame.sprite_material_draws.size()},
        {"companion_count",frame.surface_vertices.size()},{"neutral_companion_count",neutral_count},
        {"non_neutral_companion_count",mismatch},{"companion_sha256",json_hash(surfaces)},
        {"typed_command_count",typed},{"plain_geometry_command_count",plain},{"text_command_count",texts},
        {"typed_roles",std::move(roles)}};
}
Json telemetry_row(const RendererFrameTelemetry& value) {
    Json passes=Json::array();
    for (std::uint32_t i=0; i<value.graph_pass_count; ++i) passes.push_back(static_cast<int>(value.graph_passes[i]));
    return {{"submitted",value.submitted},{"capture",value.capture},{"vertices",value.vertices},
        {"indices",value.indices},{"draws",value.draws},{"surface_draws",value.surface_draws},
        {"material_draws",value.material_draws},{"ground_draws",value.ground_draws},
        {"occluders",value.occluders},{"occluder_upload_bytes",value.occluder_upload_bytes},
        {"contact_casters",value.contact_casters},{"contact_draws",value.contact_draws},
        {"contact_mask_binds",value.contact_mask_binds},{"water_draws",value.water_draws},
        {"reflection_draws",value.reflection_draws},{"upload_bytes",value.upload_bytes},
        {"frame_slot",value.frame_slot},{"graph_pass_count",value.graph_pass_count},
        {"graph_passes",std::move(passes)},{"gpu_execution_ms",nullptr}};
}
void render_check(Renderer2D& renderer, const Frame2D& frame, const fs::path& path={}) {
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    renderer.render(frame,path); const auto& t=renderer.frame_telemetry();
    require(t.measured && t.submitted && !t.gpu_execution_ms,"Actual normal submission telemetry absent");
    require(t.ground_draws==0u && t.occluders==0u && t.occluder_upload_bytes==0u &&
        t.contact_casters==0u && t.contact_draws==0u && t.contact_mask_binds==0u &&
        t.water_draws==0u && t.reflection_draws==0u,"LegacyCombined executed cumulative work");
    if (frame.content==FrameContent::UiOnly)
        require(t.graph_pass_count==1u && t.graph_passes[0]==WorldGraphPass::Ui &&
            t.material_draws==0u && t.surface_draws==0u,"UiOnly reported world work");
    else require(t.graph_pass_count==0u && t.material_draws==frame.sprite_material_draws.size(),
        "LegacyCombined typed draw telemetry disagrees with normal frame");
}
Image capture_frame(Renderer2D& renderer, const Frame2D& frame, const fs::path& root,
                    std::string_view name, Json& rows) {
    require(rows.size()<capture_limit,"Material capture budget exceeded");
    const auto path=root/(std::string(name)+".png"); require(!fs::exists(path),"Refusing capture overwrite");
    render_check(renderer,frame,path); require(renderer.frame_telemetry().capture,"Actual capture flag absent");
    rows.push_back({{"id",name},{"file",path.filename().string()},{"width",extent.width},{"height",extent.height},
        {"bytes",fs::file_size(path)},{"sha256",sha256(file_bytes(path))},
        {"frame_sha256",json_hash(frame_state(frame))},{"frame_contract",frame_contract(frame)},
        {"telemetry",telemetry_row(renderer.frame_telemetry())}});
    check_budget(root); return read_image(path);
}
std::size_t difference(const Image& a, const Image& b, std::optional<Rect> bounds={}) {
    require(a.rgba.size()==b.rgba.size(),"PNG extents disagree"); std::size_t changed{};
    for (std::uint32_t y=0u; y<extent.height; ++y) for (std::uint32_t x=0u; x<extent.width; ++x) {
        if (bounds && (float(x)+.5f<bounds->x || float(x)+.5f>=bounds->x+bounds->w ||
            float(y)+.5f<bounds->y || float(y)+.5f>=bounds->y+bounds->h)) continue;
        const auto p=(std::size_t(y)*extent.width+x)*4u;
        if (!std::equal(a.rgba.begin()+static_cast<std::ptrdiff_t>(p),a.rgba.begin()+static_cast<std::ptrdiff_t>(p+4u),
            b.rgba.begin()+static_cast<std::ptrdiff_t>(p))) ++changed;
    }
    return changed;
}
Json weather_row(const WeatherView& value) {
    return {{"known",value.known},{"evaluated_tick",value.evaluated_simulation_tick},
        {"calendar_tick",value.calendar_tick},{"offset",value.calendar_offset_ticks},
        {"wet_permille",value.wetness_permille},{"wet_remainder",value.wetness_remainder},
        {"snow_permille",value.snow_cover_permille},{"kind",static_cast<int>(value.current.kind)},
        {"precipitation",static_cast<int>(value.current.precipitation)},{"wind",value.current.wind_permille},
        {"temperature_deci_c",value.current.temperature_deci_c}};
}

void create_worlds(const fs::path& root, Json& rows) {
    World world(70421,2); auto base=world.export_state();
    require(base.next_building_id==house_id && base.residents.size()==2u &&
        base.routes.size()==2u && base.settlement.young.empty() && base.path_batches.empty(),
        "Initial normal fixture authority layout changed");
    for (auto& chunk : base.chunks) for (std::size_t i=0; i<chunk.tiles.size(); ++i) {
        const int x=chunk.key.cx*chunk_size+static_cast<int>(i%chunk_size);
        const int y=chunk.key.cy*chunk_size+static_cast<int>(i/chunk_size);
        if (x<14 || x>42 || y<12 || y>40) continue;
        chunk.tiles[i]=chunk.key.layer==WorldLayer::Surface
            ? Tile{Terrain::Grass,0,true,Biome::Meadow,0}
            : Tile{Terrain::Grass,0,true,Biome::Mountain,0};
    }
    base.residents[0].x=21.f; base.residents[0].y=27.f;
    base.residents[1].x=30.f; base.residents[1].y=28.f;
    for (auto& resident : base.residents) {
        resident.target_x=static_cast<int>(resident.x); resident.target_y=static_cast<int>(resident.y);
        resident.job=Job::Idle; resident.progress=0.f; resident.order_phase=OrderPhase::Idle;
        resident.pending_path_batch=0u; resident.path_failure=PathFailure::None;
    }
    for (auto& route : base.routes) { route.points.clear(); route.cursor=0u; route.work.reset(); }
    base.buildings.push_back({base.next_building_id++,BuildingKind::House,26,22,1.f,WorldLayer::Surface,1u});
    base.buildings.push_back({base.next_building_id++,BuildingKind::Workshop,20,23,1.f,WorldLayer::Surface,1u});
    ++base.topology_revision;
    for (std::size_t i=0; i<world_saves.size(); ++i) {
        auto state=base; state.ticks=i==1u?21600u:7200u;
        if (i==4u) {
            const ClimateConfig config{weather_algorithm_version,ClimateProfileId::TemperateValley,
                ClimateScenarioId::FirstWinterStorm};
            World winter(70421,0,generator_version,config); state.climate=winter.export_state().climate;
        }
        // Authored host fixture clock/cover, validated and published by real
        // World import/save and ClientSession load. This is not earned history.
        const WeatherContext context{state.seed,{state.climate.algorithm_version,
            state.climate.climate_profile,state.climate.scenario}};
        const auto calendar=make_calendar_tick(state.ticks,state.climate.calendar_offset_ticks);
        require(calendar.has_value(),"Normal fixture calendar overflow");
        WeatherScheduleCache cache(context,*calendar); state.climate.evaluated_simulation_tick=state.ticks;
        state.climate.current=cache.sample(*calendar);
        state.climate.wetness_permille=i==2u?600u:0u; state.climate.wetness_remainder=i==2u?14400:0;
        state.climate.snow_cover_permille=0u; state.climate.snow_cover_remainder=0;
        if (i==3u) {
            state.economy.upgrades.push_back({house_id,2});
            for (int stage=1; stage<=3; ++stage) {
                const int id=state.next_building_id++,x=16+stage*3,y=29;
                const float progress=stage==1?.1f:stage==2?.5f:.9f;
                state.buildings.push_back({id,BuildingKind::House,x,y,progress,WorldLayer::Surface,1u});
                const auto required=ClientSession::cost(BuildingKind::House);
                state.construction_orders.push_back({id,BuildingKind::House,{x,y,WorldLayer::Surface},
                    1u,required,required,ConstructionPhase::Work,0u});
            }
        }
        validate_climate_runtime(state.climate,state.ticks,context,cache);
        std::string error; const bool imported=world.import_state(std::move(state),error);
        require(imported,"Normal fixture import "+std::string(world_saves[i])+": "+error);
        const auto path=root/world_saves[i]; require(!fs::exists(path),"Fixture save exists");
        const bool saved=world.save(path,error); require(saved,"Normal fixture save: "+error);
        rows.push_back({{"file",world_saves[i]},{"tick",world.ticks()},
            {"calendar_tick",world.calendar().tick.value},{"season",static_cast<int>(world.calendar().season)},
            {"checksum",world.checksum()},{"authored_fixture",true},{"earned_history",false}});
    }
}

struct SourceSprite {
    std::string key,page; Json entry; std::array<int,4> crop{};
    std::array<float,2> pivot{}; float reference_width{}; Extent page_size{};
    std::vector<std::uint8_t> rgba;
    Extent mask_size{}; std::vector<std::uint8_t> pooled_alpha,pooled_coverage;
};
SourceSprite source_sprite(const AssetCatalog& catalog, const Json& world, std::string_view key) {
    SourceSprite result; result.key=key;
    for (const auto& page : world.at("pages")) for (const auto& entry : page.at("sprites"))
        if (entry.at("id").get<std::string>()==key) { require(result.page.empty(),"Ambiguous original source sprite");
            result.page=page.at("path").get<std::string>(); result.entry=entry; }
    require(!result.page.empty(),"Original source sprite is absent: "+std::string(key));
    result.crop=result.entry.at("rect").get<std::array<int,4>>();
    result.pivot=result.entry.at("pivot").get<std::array<float,2>>();
    result.reference_width=result.entry.value("reference_width",float(result.crop[2]));
    const auto encoded=catalog.read_path(result.page); png_image image{}; image.version=PNG_IMAGE_VERSION;
    struct Free { png_image* p; ~Free(){png_image_free(p);} } free{&image};
    require(png_image_begin_read_from_memory(&image,encoded.data(),encoded.size())!=0,"Original PNG decode header failed");
    result.page_size={image.width,image.height}; image.format=PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
    require(png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr)!=0,"Original PNG decode failed");
    result.rgba.resize(std::size_t(result.crop[2])*static_cast<std::size_t>(result.crop[3])*4u);
    for (int y=0; y<result.crop[3]; ++y) {
        const auto from=(std::size_t(result.crop[1]+y)*image.width+static_cast<std::size_t>(result.crop[0]))*4u;
        const auto to=std::size_t(y)*static_cast<std::size_t>(result.crop[2])*4u;
        std::copy_n(pixels.begin()+static_cast<std::ptrdiff_t>(from),std::size_t(result.crop[2])*4u,
            result.rgba.begin()+static_cast<std::ptrdiff_t>(to));
    }
    // Independent crop-based oracle for the documented conservative CPU hit
    // mask. This is max-pooled coverage, not full-resolution pixel picking.
    const float scale=std::min(1.f,192.f/float(std::max(result.crop[2],result.crop[3])));
    result.mask_size={static_cast<std::uint32_t>(std::max(1L,std::lround(float(result.crop[2])*scale))),
        static_cast<std::uint32_t>(std::max(1L,std::lround(float(result.crop[3])*scale)))};
    const auto mask_bytes=std::size_t(result.mask_size.width)*result.mask_size.height;
    result.pooled_alpha.resize(mask_bytes); result.pooled_coverage.resize(mask_bytes);
    for (std::uint32_t my=0u;my<result.mask_size.height;++my) for (std::uint32_t mx=0u;mx<result.mask_size.width;++mx) {
        const auto x0=static_cast<int>(std::uint64_t(mx)*static_cast<std::uint32_t>(result.crop[2])/result.mask_size.width);
        const auto x1=static_cast<int>(std::uint64_t(mx+1u)*static_cast<std::uint32_t>(result.crop[2])/result.mask_size.width);
        const auto y0=static_cast<int>(std::uint64_t(my)*static_cast<std::uint32_t>(result.crop[3])/result.mask_size.height);
        const auto y1=static_cast<int>(std::uint64_t(my+1u)*static_cast<std::uint32_t>(result.crop[3])/result.mask_size.height);
        std::uint8_t maximum{};
        for (int y=y0;y<std::max(y0+1,y1);++y) for (int x=x0;x<std::max(x0+1,x1);++x)
            maximum=std::max(maximum,result.rgba[(std::size_t(y)*static_cast<std::size_t>(result.crop[2])+static_cast<std::size_t>(x))*4u+3u]);
        const auto index=std::size_t(my)*result.mask_size.width+mx;
        result.pooled_alpha[index]=maximum; result.pooled_coverage[index]=maximum>=64u?255u:0u;
    }
    return result;
}
Rect source_bounds(const SourceSprite& source, Point ground, float width) {
    width*=float(source.crop[2])/source.reference_width;
    const float height=width*(float(source.crop[3])/float(source.crop[2]));
    return {ground.x-width*source.pivot[0],ground.y-height*source.pivot[1],width,height};
}
Json rectangle(const Rect& r) { return {r.x,r.y,r.w,r.h}; }
bool close(float a,float b) { return std::abs(a-b)<=.005f; }
struct Quad {
    std::size_t command{}; std::uint32_t first_index{}; std::array<std::uint32_t,4> vertices{};
    std::uint32_t material{};
};
Quad find_quad(const Frame2D& frame, const SourceSprite& source, const Rect& bounds, bool typed) {
    std::vector<Quad> matches;
    const std::array<Point,4> expected{{{bounds.x,bounds.y},{bounds.x+bounds.w,bounds.y},
        {bounds.x+bounds.w,bounds.y+bounds.h},{bounds.x,bounds.y+bounds.h}}};
    for (std::size_t ci=0u; ci<frame.commands.size(); ++ci) {
        const auto* draw=std::get_if<GeometryDraw>(&frame.commands[ci]);
        if (!draw || draw->pass!=Pass::WorldOrdered || bool(draw->sprite_material_index)!=typed) continue;
        for (std::uint32_t offset=0u; offset+6u<=draw->index_count; offset+=6u) {
            const auto first=draw->first_index+offset; const auto a=frame.indices.at(first);
            const std::array<std::uint32_t,6> pattern{a,a+1u,a+2u,a,a+2u,a+3u};
            if (!std::equal(pattern.begin(),pattern.end(),frame.indices.begin()+first)) continue;
            bool equal=true;
            for (std::uint32_t corner=0u; corner<4u; ++corner) {
                const auto& v=frame.vertices.at(a+corner);
                equal=equal && close(v.position.x,expected[corner].x) && close(v.position.y,expected[corner].y);
                const float u=typed ? ((corner==1u || corner==2u)?1.f:0.f)
                    : (float(source.crop[0])+((corner==1u || corner==2u)?float(source.crop[2]):0.f))/float(source.page_size.width);
                const float vv=typed ? (corner>=2u?1.f:0.f)
                    : (float(source.crop[1])+(corner>=2u?float(source.crop[3]):0.f))/float(source.page_size.height);
                equal=equal && close(v.uv.x,u) && close(v.uv.y,vv);
            }
            if (equal) matches.push_back({ci,first,{a,a+1u,a+2u,a+3u},draw->sprite_material_index});
        }
    }
    require(matches.size()==1u,"Expected unique actual body quad for "+source.key+" (found "+std::to_string(matches.size())+")");
    return matches.front();
}
Json quad_row(const Frame2D& frame, const Quad& quad) {
    Json vertices=Json::array();
    for (const auto index : quad.vertices) {
        const auto& v=frame.vertices.at(index);
        vertices.push_back({v.position.x,v.position.y,v.uv.x,v.uv.y,v.color.r,v.color.g,v.color.b,v.color.a});
    }
    Json result{{"command",quad.command},{"first_index",quad.first_index},{"vertices",std::move(vertices)},
        {"material_index",quad.material},{"draw",draw_row(std::get<GeometryDraw>(frame.commands.at(quad.command)))}};
    if (quad.material) {
        const auto& m=frame.sprite_material_draws.at(quad.material-1u);
        result["surface"]={{"anchor_export",m.surface.anchor_export},{"source_size_pivot",m.surface.source_size_pivot},
            {"depth_scale",m.surface.depth_scale},{"response",m.surface.response},
            {"receiver_owner",m.receiver_owner},{"emission_scale",m.emission_scale}};
    }
    return result;
}
void check_typed(const Frame2D& frame, std::size_t expected) {
    require(frame.content==FrameContent::World && frame.linear_hdr_world &&
        frame.surface_vertices.size()==frame.vertices.size() && frame.sprite_material_draws.size()==expected,
        "Normal mapped material count/HDR/companion alignment differs");
    const SurfaceVertex neutral{}; std::size_t typed{}; std::size_t plain{};
    for (const auto& v : frame.surface_vertices)
        require(v.world_position==neutral.world_position && v.normal==neutral.normal,"Normal companion is not neutral");
    for (const auto& command : frame.commands) if (const auto* draw=std::get_if<GeometryDraw>(&command)) {
        if (draw->sprite_material_index) {
            ++typed; require(draw->pass==Pass::WorldOrdered && draw->blend==Blend::Alpha &&
                draw->index_count==6u && !draw->texture.slot && !draw->surface_lit && !draw->ground_material_index,
                "Typed body command crossed a legacy role");
            for (std::uint32_t i=0u;i<draw->index_count;++i) {
                const auto& color=frame.vertices.at(frame.indices.at(draw->first_index+i)).color;
                require(color.r==255u && color.g==255u && color.b==255u,"Typed body received legacy RGB grading");
            }
        } else ++plain;
    }
    require(typed==expected && plain>0u,"Normal mixed roles are vacuous");
    (void)frame_state(frame);
}
Frame2D compose(GameView& view, const ClientSession& session, double seconds=10.) {
    Frame2D frame; view.compose(session,seconds,frame,false); (void)frame_state(frame); return frame;
}
void load_session(ClientSession& session, GameView& view, GameView& baseline, const fs::path& path) {
    std::string error; const bool loaded=session.load(path,error);
    require(loaded,"Actual session load "+path.filename().string()+": "+error);
    require(session.ready() && session.world_ready() && session.weather().known && !session.last_load().copied,
        "Current fixture did not publish ready weather authority");
    view.reset_world_state(); baseline.reset_world_state();
    for (auto* candidate : std::array<GameView*,2>{&view,&baseline}) {
        candidate->set_paused(true); candidate->set_annotations_visible(false); candidate->set_layer(WorldLayer::Surface);
    }
}
void click(GameView& view, ClientSession& session, Point point) {
    SDL_Event event{}; event.type=SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button=SDL_BUTTON_LEFT;
    event.button.x=point.x; event.button.y=point.y; view.handle_event(event,session);
    event.type=SDL_EVENT_MOUSE_BUTTON_UP; view.handle_event(event,session);
}
bool picked(const GameView& view, bool building, int id) {
    const auto& ids=building?view.selected_building_ids():view.selected_ids();
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
std::array<int,3> alpha_at(const SourceSprite& source, const Rect& bounds, Point point) {
    const int x=static_cast<int>(std::floor((point.x-bounds.x)*float(source.crop[2])/bounds.w));
    const int y=static_cast<int>(std::floor((point.y-bounds.y)*float(source.crop[3])/bounds.h));
    if (x<0 || y<0 || x>=source.crop[2] || y>=source.crop[3]) return {x,y,-1};
    return {x,y,source.rgba.at((std::size_t(y)*static_cast<std::size_t>(source.crop[2])+static_cast<std::size_t>(x))*4u+3u)};
}
std::array<int,3> mask_at(const SourceSprite& source,const Rect& bounds,Point point) {
    if (point.x<bounds.x || point.y<bounds.y || point.x>=bounds.x+bounds.w || point.y>=bounds.y+bounds.h) return {-1,-1,-1};
    const auto x=std::min(source.mask_size.width-1u,static_cast<std::uint32_t>((point.x-bounds.x)/bounds.w*float(source.mask_size.width)));
    const auto y=std::min(source.mask_size.height-1u,static_cast<std::uint32_t>((point.y-bounds.y)/bounds.h*float(source.mask_size.height)));
    return {static_cast<int>(x),static_cast<int>(y),source.pooled_alpha.at(std::size_t(y)*source.mask_size.width+x)};
}
Point sample_point(const SourceSprite& source,const Rect& bounds,bool covered) {
    std::optional<Point> best; float distance=std::numeric_limits<float>::max();
    for (int y=static_cast<int>(std::ceil(bounds.y+.5f)); y<static_cast<int>(std::floor(bounds.y+bounds.h-.5f));++y)
        for (int x=static_cast<int>(std::ceil(bounds.x+.5f)); x<static_cast<int>(std::floor(bounds.x+bounds.w-.5f));++x) {
            const Point p{float(x)+.5f,float(y)+.5f}; if (x<1 || y<1 || x>=1919 || y>=1079) continue;
            const auto sample=mask_at(source,bounds,p); if ((sample[2]>=64)!=covered) continue;
            const float mx=(p.x-bounds.x)/bounds.w*float(source.mask_size.width);
            const float my=(p.y-bounds.y)/bounds.h*float(source.mask_size.height);
            if (mx-std::floor(mx)<.05f || mx-std::floor(mx)>.95f || my-std::floor(my)<.05f || my-std::floor(my)>.95f) continue;
            const float d=std::hypot(p.x-(bounds.x+bounds.w*.5f),p.y-(bounds.y+bounds.h*.5f));
            if (d<distance) { best=p; distance=d; }
        }
    require(best.has_value(),"Original alpha sample is vacuous: "+source.key); return *best;
}
Json query_pair(GameView& view, GameView& baseline, ClientSession& session,
                const SourceSprite& source, Point ground, float width, bool building, int id,
                const Frame2D& mixed_frame, const Frame2D& baseline_frame) {
    const auto bounds=source_bounds(source,ground,width);
    const auto legacy=find_quad(baseline_frame,source,bounds,false);
    const auto typed=find_quad(mixed_frame,source,bounds,true);
    const auto& surface=mixed_frame.sprite_material_draws.at(typed.material-1u).surface;
    const float zoom=width/(building?house_width:39.f);
    const std::array<float,4> source_size_pivot{float(source.crop[2]),float(source.crop[3]),
        source.pivot[0]*float(source.crop[2]),source.pivot[1]*float(source.crop[3])};
    const float expected_scale=(width/zoom)*4.f/source.reference_width;
    require(surface.source_size_pivot==source_size_pivot && surface.anchor_export[3]==4.f &&
        close(surface.depth_scale[2],expected_scale),"Actual registered material size/pivot/export scale differs from original");
    const std::array<float,3> expected_anchor{building?2.f:-3.f,building?-2.f:3.f,
        -(building?15.f:6.f)/reference_tile_height};
    for (std::size_t i=0u;i<expected_anchor.size();++i)
        require(close(surface.anchor_export[i],expected_anchor[i]),"Actual registered render anchor moved from original ground");
    Json samples=Json::array();
    for (const bool covered : std::array<bool,2>{true,false}) {
        const auto point=sample_point(source,bounds,covered); const auto raw=alpha_at(source,bounds,point);
        const auto sample=mask_at(source,bounds,point);
        click(baseline,session,point); click(view,session,point);
        require(picked(view,building,id)==covered && picked(baseline,building,id)==covered,
            "Actual SDL alpha picking differs for "+source.key);
        samples.push_back({{"point",{point.x,point.y}},{"raw_source_pixel",{raw[0],raw[1]}},{"raw_alpha",raw[2]},
            {"mask_cell",{sample[0],sample[1]}},{"max_alpha",sample[2]},{"expected_pick",covered},{"baseline_pick",picked(baseline,building,id)},
            {"mixed_pick",picked(view,building,id)}});
    }
    const auto& typed_right=mixed_frame.vertices.at(typed.vertices[1]).position;
    const Point boundary{std::max(bounds.x+bounds.w,typed_right.x),bounds.y+bounds.h*.5f};
    click(baseline,session,boundary); click(view,session,boundary);
    require(!picked(view,building,id) && !picked(baseline,building,id),"Actual half-open sprite edge was picked");
    click(baseline,session,{1.f,1000.f}); click(view,session,{1.f,1000.f});
    const auto origin=view.tile_screen(24.f,24.f); const auto oracle_origin=baseline.tile_screen(24.f,24.f);
    require(origin.x==oracle_origin.x && origin.y==oracle_origin.y,"Picking changed one camera");
    return {{"key",source.key},{"entity_id",id},{"building",building},{"ground",{ground.x,ground.y}},
        {"requested_width",width},{"reference_width",source.reference_width},{"crop",source.crop},
        {"pivot",source.pivot},{"crop_rgba_sha256",sha256(source.rgba)},{"bounds",rectangle(bounds)},
        {"mask_size",{source.mask_size.width,source.mask_size.height}},
        {"pooled_coverage_sha256",sha256(source.pooled_coverage)},{"pooled_alpha_sha256",sha256(source.pooled_alpha)},
        {"pick_scope","conservative max-pooled mask, longest side <=192, threshold64, half-open bounds"},
        {"geometry_scale",expected_scale},{"expected_render_anchor",expected_anchor},
        {"baseline",quad_row(baseline_frame,legacy)},{"mixed",quad_row(mixed_frame,typed)},
        {"samples",std::move(samples)},{"half_open_point",{boundary.x,boundary.y}},
        {"half_open_baseline_pick",false},{"half_open_mixed_pick",false},
        {"camera_origin",{origin.x,origin.y}}};
}

void rebind_manifest(const fs::path& root,Json manifest) {
    std::string canonical;
    auto& rows=manifest.at("assets");
    std::sort(rows.begin(),rows.end(),[](const Json& a,const Json& b){return a.at("id").get<std::string>()<b.at("id").get<std::string>();});
    for (auto& row : rows) {
        const auto bytes=file_bytes(root/row.at("path").get<std::string>());
        row["sha256"]=sha256(bytes); row["bytes"]=bytes.size();
        for (const char* key : std::array<const char*,8>{"id","path","kind","sha256","license","source","author","notice"}) {
            const auto value=row.at(key).get<std::string>(); canonical+=std::to_string(value.size())+":"+value;
        }
        canonical+='\n';
    }
    manifest["content_sha256"]=sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
    write_json(root/"manifest.json",manifest);
}
void bind_registration(Json& metadata,Json& world,std::size_t index) {
    auto& item=metadata.at("items").at(index);
    const auto source=source_record_hash(Json{{"master_sha256",item.at("master_sha256")},
        {"registration",item.at("registration")},{"geometry_provenance",item.at("geometry_provenance")}});
    item["source_hash"]=source; world.at("materials").at(index)["source_hash"]=source;
}
Json invalid_catalogs(GameView& view,ClientSession& session,const fs::path& valid,const fs::path& output) {
    Json rows=Json::array(); const auto before=compose(view,session); const auto before_hash=json_hash(frame_state(before));
    const auto camera=view.tile_screen(24.f,24.f); const auto save_digest=sha256(file_bytes(output/world_saves[0]));
    const auto selected_before=view.selected_building_ids(); const auto residents_before=view.selected_ids();
    require(selected_before==std::vector<int>{house_id},"Cold rejection selection control is vacuous");
    const std::array<std::string_view,6> ids{"schema","reference_width","pivot","decoded_crop_hash","legacy_key","albedo_byte"};
    for (const auto id : ids) {
        const auto root=output/("invalid-"+std::string(id)); require(!fs::exists(root),"Invalid fixture root exists");
        auto metadata=Json::parse(file_bytes(valid/"art/material-collection.json"));
        auto world=Json::parse(file_bytes(valid/"art/world.json")); std::string field,expected_error;
        std::string changed_path; std::vector<std::uint8_t> changed_bytes;
        if (id=="schema") { world["schema"]=3; field="world.schema";
            expected_error="Normal material collection schema/counts are inconsistent"; }
        else if (id=="reference_width") { metadata["items"][1]["registration"]["reference_width"]=139.;
            bind_registration(metadata,world,1u); field="items[1].registration.reference_width";
            expected_error="Registered source crop/pivot/reference width/catalog hash differs from original"; }
        else if (id=="pivot") { metadata["items"][0]["registration"]["pivot_px"][0]=265.5;
            bind_registration(metadata,world,0u); field="items[0].registration.pivot_px[0]";
            expected_error="Registered source crop/pivot/reference width/catalog hash differs from original"; }
        else if (id=="decoded_crop_hash") { metadata["items"][0]["registration"]["decoded_crop_sha256"]=std::string(64u,'0');
            bind_registration(metadata,world,0u); field="items[0].registration.decoded_crop_sha256";
            expected_error="Registered decoded paint hash differs from original crop"; }
        else if (id=="legacy_key") { metadata["items"][0]["legacy_key"]="building.house.ready.unknown";
            field="items[0].legacy_key"; expected_error="Normal material mapping requires a unique exact supported original legacy key"; }
        else {
            auto& texture=world["textures"][0]; changed_path=texture.at("path").get<std::string>();
            changed_bytes=file_bytes(valid/changed_path); changed_bytes[0]^=1u;
            texture["sha256"]=sha256(changed_bytes); field="textures[0].payload_byte[0]";
            expected_error="Registered albedo must preserve the exact original cropped RGBA";
        }
        fs::create_directory(root);
        for (const auto& file : fs::recursive_directory_iterator(valid)) {
            require(!file.is_symlink(),"Invalid fixture source contains symlink"); check_no_reparse(file.path());
            const auto relative=fs::relative(file.path(),valid); const auto name=relative.generic_string();
            const auto to=root/relative;
            if (file.is_directory()) fs::create_directory(to);
            else if (file.is_regular_file() && name!="manifest.json" && name!="art/world.json" &&
                name!="art/material-collection.json" && name!=changed_path) {
                require(!fs::exists(to),"Invalid fixture copy collision"); fs::copy_file(file.path(),to);
            }
        }
        if (!changed_path.empty()) save_bytes(root/changed_path,changed_bytes);
        write_json(root/"art/material-collection.json",metadata); write_json(root/"art/world.json",world);
        rebind_manifest(root,Json::parse(file_bytes(valid/"manifest.json")));
        AssetCatalog catalog(root); catalog.verify(); bool rejected{}; std::string message;
        try { view.load_material_collection(catalog); } catch (const std::exception& error) { rejected=true; message=error.what(); }
        require(rejected && message==expected_error,"Invalid collection did not reach intended loader rejection: "+std::string(id)+": "+message);
        const auto after=compose(view,session); const auto after_camera=view.tile_screen(24.f,24.f);
        require(view.registered_material_count()==2u && view.has_registered_material("building.house.ready") &&
            view.has_registered_material("resident.man.walk.2.1") && frame_state(after)==frame_state(before) &&
            camera.x==after_camera.x && camera.y==after_camera.y &&
            view.selected_building_ids()==selected_before && view.selected_ids()==residents_before &&
            sha256(file_bytes(output/world_saves[0]))==save_digest,
            "Invalid collection changed accepted frame/map/camera/save: "+std::string(id));
        rows.push_back({{"id",id},{"root",root.filename().string()},{"primary_mutation",field},
            {"derived_rebindings","source_hash/material.source_hash when registration changes; texture hash when pixels change; manifest bytes/hash/content_sha256"},
            {"error",message},{"expected_error",expected_error},{"before_frame_sha256",before_hash},{"after_frame_sha256",json_hash(frame_state(after))},
            {"registered_count",view.registered_material_count()},{"camera_before",{camera.x,camera.y}},
            {"camera_after",{after_camera.x,after_camera.y}},{"selected_buildings_before",selected_before},
            {"selected_buildings_after",view.selected_building_ids()},
            {"selected_residents_before",residents_before},{"selected_residents_after",view.selected_ids()},
            {"files",tree_rows(root)}});
        check_budget(output);
    }
    return rows;
}

Frame2D fresh_menu() {
    Frame2D frame; frame.logical_size=extent; frame.clear={20u,30u,40u,255u}; frame.content=FrameContent::UiOnly; return frame;
}
Json rml_counts(const Frame2D& frame,std::size_t first=0u) {
    std::size_t draws{},textured{},premultiplied{}; std::set<std::pair<std::uint32_t,std::uint32_t>> textures;
    for (std::size_t i=first;i<frame.commands.size();++i) {
        const auto* draw=std::get_if<GeometryDraw>(&frame.commands[i]);
        require(draw && draw->pass==Pass::Ui && !draw->surface_lit && !draw->sprite_material_index &&
            !draw->ground_material_index,"RmlUi crossed world command role");
        ++draws; if (draw->texture.slot) { ++textured; textures.emplace(draw->texture.slot,draw->texture.generation); }
        if (draw->blend==Blend::PremultipliedAlpha) ++premultiplied;
    }
    require(draws>0u && textured>0u && textures.size()>=2u && premultiplied>0u,"Actual RmlUi font/image draw is vacuous");
    return {{"vertices",frame.vertices.size()},{"indices",frame.indices.size()},{"commands",frame.commands.size()},
        {"surface_vertices",frame.surface_vertices.size()},{"ui_draws",draws},{"textured_draws",textured},
        {"distinct_textures",textures.size()},{"premultiplied_draws",premultiplied}};
}
void check_prefix(const Frame2D& original,const Frame2D& composed) {
    require(composed.vertices.size()>original.vertices.size() && composed.indices.size()>original.indices.size() &&
        composed.commands.size()>original.commands.size() && composed.surface_vertices.size()==composed.vertices.size(),
        "Normal typed HUD suffix is vacuous or misaligned");
    auto prefix=composed; prefix.vertices.resize(original.vertices.size()); prefix.indices.resize(original.indices.size());
    prefix.commands.resize(original.commands.size()); prefix.surface_vertices.resize(original.surface_vertices.size());
    require(frame_state(prefix)==frame_state(original),"RmlUi changed normal World prefix or material payload");
    const SurfaceVertex neutral{};
    for (std::size_t i=original.vertices.size();i<composed.surface_vertices.size();++i)
        require(composed.surface_vertices[i].world_position==neutral.world_position && composed.surface_vertices[i].normal==neutral.normal,
            "RmlUi suffix companion is not neutral");
    for (std::size_t i=original.indices.size();i<composed.indices.size();++i)
        require(composed.indices[i]>=original.vertices.size(),"UI suffix index points into World prefix");
}

Json legacy_variant(const AssetCatalog& assets,const Json& original,GameView& view,const ClientSession& session,
                    const Frame2D& frame,std::string_view key,int id,float x,float y,float width,float zoom) {
    const auto source=source_sprite(assets,original,key); const auto tile=view.tile_screen(x,y);
    const Point ground{tile.x,tile.y+15.f*zoom}; const auto bounds=source_bounds(source,ground,width*zoom);
    const auto quad=find_quad(frame,source,bounds,false);
    require(!view.has_registered_material(key),"Variant unexpectedly aliases registered representative");
    return {{"key",key},{"entity_id",id},{"world",{x,y}},{"ground",{ground.x,ground.y}},
        {"requested_width",width*zoom},{"bounds",rectangle(bounds)},{"quad",quad_row(frame,quad)},
        {"registered",false},{"tick",session.ticks()}};
}
} // namespace

int main(int argc,char** argv) {
    fs::path output; bool owned{}; Json report{{"schema",1},{"complete",false}};
    const auto started=std::chrono::steady_clock::now();
    try {
        require(argc==6,"Usage: GameViewMaterialProbe runtime-assets original-assets material-collection ui-kit fresh-output");
        const auto runtime=fs::absolute(argument(argv[1])), original_path=fs::absolute(argument(argv[2]));
        const auto collection_path=fs::absolute(argument(argv[3])),ui_path=fs::absolute(argument(argv[4]));
        output=fs::absolute(argument(argv[5]));
        require(output.parent_path()==runtime.parent_path().parent_path() && !fs::exists(output),
            "Material output must be a fresh immediate build child");
        for (const auto& path : std::array<fs::path,5>{runtime,original_path,collection_path,ui_path,output}) check_no_reparse(path);
        require(fs::space(output.parent_path()).available>=384u*1024u*1024u,"Insufficient evidence headroom");
        require(fs::create_directory(output),"Cannot create new material evidence root"); owned=true;
        Json data{{"fixtures",Json::array()},{"captures",Json::array()},{"zoom_cases",Json::array()},
            {"variants",Json::array()},{"animation_cases",Json::array()},{"authority_cases",Json::array()},
            {"world_saves",world_saves},{"capture_limit",capture_limit}};
        create_worlds(output,data["fixtures"]); const auto save_before=tree_rows(output);
        require(SDL_Init(SDL_INIT_VIDEO),"SDL initialization failed");
        struct Quit { ~Quit(){SDL_Quit();} } quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra normal registered material diagnostic",1920,1080,0),SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()),"Native window failed");
        RendererOptions options; options.world_graph=WorldGraph::LegacyCombined; options.collect_telemetry=true;
        Renderer2D renderer(window.get(),runtime,options); renderer.set_logical_size(extent);
        const auto& hardware=renderer.device_info();
        require(!hardware.software && hardware.backend=="direct3d12","Actual hardware D3D12 required");
        data["hardware"]={{"backend",hardware.backend},{"gpu",hardware.device},{"driver",hardware.driver},
            {"software",hardware.software},{"graph","LegacyCombined"},{"resolution",{extent.width,extent.height}},
            {"fps",nullptr},{"gpu_execution_ms",nullptr}};
        AssetCatalog originals(original_path),collection(collection_path),ui_assets(ui_path);
        originals.verify(); collection.verify(); ui_assets.verify();
        const auto original=Json::parse(originals.read_path("art/world.json"));
        require(original.at("schema")==1 && original.at("pages").size()==67u,"Original full schema1 page closure changed");
        std::size_t records{}; for (const auto& page : original.at("pages")) records+=page.at("sprites").size();
        require(records==987u,"Original legacy record coverage changed");
        data["inputs"]={{"original_pages",67},{"original_records",records},{"original_content_hash",originals.content_hash()},
            {"collection_content_hash",collection.content_hash()},{"ui_content_hash",ui_assets.content_hash()},
            {"original_world_sha256",sha256(originals.read_path("art/world.json"))},
            {"collection_metadata_sha256",sha256(collection.read("material.collection.metadata"))},
            {"collection_world_sha256",sha256(collection.read("art.world"))}};
        data["registrations"]=Json::array();
        const auto collection_metadata=Json::parse(collection.read("material.collection.metadata"));
        for (const auto& item : collection_metadata.at("items")) {
            const auto canonical=source_record_hash(Json{{"master_sha256",item.at("master_sha256")},
                {"registration",item.at("registration")},{"geometry_provenance",item.at("geometry_provenance")}});
            require(item.at("source_hash")==canonical,"Accepted source_record hash disagrees with exporter metadata");
            data["registrations"].push_back({{"legacy_key",item.at("legacy_key")},{"sprite_id",item.at("sprite_id")},
                {"material_id",item.at("material_id")},{"source_hash",item.at("source_hash")},
                {"source_record_sha256",canonical},{"catalog_entry_sha256",item.at("registration").at("catalog_entry_sha256")}});
        }
        GameView view(renderer),baseline(renderer);
        for (auto* candidate : std::array<GameView*,2>{&view,&baseline}) {
            candidate->set_logical_size(extent); candidate->load_art(originals);
        }
        view.load_material_collection(collection);
        require(view.registered_material_count()==2u && baseline.registered_material_count()==0u &&
            view.has_registered_material("building.house.ready") && view.has_registered_material("resident.man.walk.2.1") &&
            !view.has_registered_material("resident.man.walk.2.0") && !view.has_registered_material("building.house.winter"),
            "Cold exact material registry differs");
        ClientSession session(70421); load_session(session,view,baseline,output/world_saves[0]);
        const auto house=source_sprite(originals,original,"building.house.ready");
        const auto man=source_sprite(originals,original,"resident.man.walk.2.1");
        const auto clean_before=compose(view,session); check_typed(clean_before,2u);
        const auto cold_house_tile=view.tile_screen(26.f,22.f);
        const Point cold_house_ground{cold_house_tile.x,cold_house_tile.y+15.f*initial_zoom};
        click(view,session,sample_point(house,source_bounds(house,cold_house_ground,house_width*initial_zoom),true));
        auto before=compose(view,session); check_typed(before,2u);
        data["cold_load_rejections"]=invalid_catalogs(view,session,collection_path,output);
        require(!renderer.has_submitted_frames(),"Cold validation submitted an actual frame");
        data["cold_install"]={{"registered_count",2},{"before_any_submission",true},
            {"baseline_frame_sha256",json_hash(frame_state(compose(baseline,session)))},
            {"mixed_frame_sha256",json_hash(frame_state(compose(view,session)))}};
        Localization locale(ui_path/"locales","uk"); const auto settings_path=output/"material-settings-unwritten.json";
        GameUi ui(renderer,ui_assets,locale,settings_path,extent,false);
        ui.show_menu(); ui.update(&session); auto menu=fresh_menu(); ui.compose(menu);
        require(menu.surface_vertices.empty() && ui.locale()=="uk" && ui.visible_for_test("menu-card") &&
            ui.element_text_width_for_test("app-title")>20.f && ui.element_bounds_for_test("menu-landscape").w>100.f,
            "Real pre-World menu is vacuous");
        auto menu_counts=rml_counts(menu);
        const auto menu_image=capture_frame(renderer,menu,output,"material-menu-first",data["captures"]);
        data["ui"]["menu_first"]=std::move(menu_counts);
        bool late_rejected{}; std::string late_error;
        try { view.load_material_collection(collection); } catch (const std::logic_error& error) { late_rejected=true; late_error=error.what(); }
        const auto after=compose(view,session);
        require(late_rejected && frame_state(after)==frame_state(before) && view.registered_material_count()==2u,
            "Actual post-menu cold load did not preserve accepted normal state");
        data["post_submit_rejection"]={{"id","after_actual_rml_menu"},{"error",late_error},
            {"before_frame_sha256",json_hash(frame_state(before))},{"after_frame_sha256",json_hash(frame_state(after))},
            {"selected_buildings_before",std::vector<int>{house_id}},{"selected_buildings_after",view.selected_building_ids()},
            {"registered_count",2},{"submitted",renderer.has_submitted_frames()}};
        require(view.selected_building_ids()==std::vector<int>{house_id},"Post-menu rejection changed picks");
        view.clear_building_selection();
        require(frame_state(compose(view,session))==frame_state(clean_before),"Clearing cold-load selection did not restore clean World");

        float zoom=initial_zoom; Image day_legacy,day_mixed; Frame2D day_legacy_frame;
        const std::array<float,3> wheel{0.f,-100.f,100.f};
        const std::array<std::string_view,3> names{"default","min","max"};
        for (std::size_t i=0;i<names.size();++i) {
            if (i) { SDL_Event event{}; event.type=SDL_EVENT_MOUSE_WHEEL; event.wheel.y=wheel[i];
                view.handle_event(event,session); baseline.handle_event(event,session); zoom=std::clamp(zoom+wheel[i]*.09f,.62f,3.f); }
            const auto legacy=compose(baseline,session),mixed=compose(view,session); check_typed(mixed,2u);
            require(!legacy.linear_hdr_world && legacy.sprite_material_draws.empty(),"Legacy baseline was promoted");
            const auto house_tile=view.tile_screen(26.f,22.f); const auto feet=view.resident_screen(session,resident_id);
            require(feet.has_value(),"Actual resident position query absent");
            const Point house_ground{house_tile.x,house_tile.y+15.f*zoom},man_ground{feet->x,feet->y+6.f*zoom};
            auto house_query=query_pair(view,baseline,session,house,house_ground,house_width*zoom,true,house_id,mixed,legacy);
            auto man_query=query_pair(view,baseline,session,man,man_ground,39.f*zoom,false,resident_id,mixed,legacy);
            require(frame_state(compose(view,session))==frame_state(mixed),"SDL query transaction left a selection/World mutation");
            const auto li=capture_frame(renderer,legacy,output,"material-legacy-"+std::string(names[i]),data["captures"]);
            const auto mi=capture_frame(renderer,mixed,output,"material-mixed-"+std::string(names[i]),data["captures"]);
            const auto house_change=difference(li,mi,source_bounds(house,house_ground,house_width*zoom));
            const auto man_change=difference(li,mi,source_bounds(man,man_ground,39.f*zoom));
            require(house_change>0u && man_change>0u,"Mapped authored quad native difference is vacuous");
            data["zoom_cases"].push_back({{"id",names[i]},{"zoom",zoom},{"wheel_delta",wheel[i]},
                {"house",std::move(house_query)},{"resident",std::move(man_query)},
                {"whole_changed_pixels",difference(li,mi)},{"house_bounds_changed_pixels",house_change},
                {"resident_bounds_changed_pixels",man_change},{"typed_material_draws",2}});
            if (!i) { day_legacy=li; day_mixed=mi; day_legacy_frame=legacy; }
        }
        // Return both views to the authored default zoom using the same ordinary
        // wheel rule, not a diagnostic camera setter.
        SDL_Event restore{}; restore.type=SDL_EVENT_MOUSE_WHEEL; restore.wheel.y=(initial_zoom-zoom)/.09f;
        view.handle_event(restore,session); baseline.handle_event(restore,session); zoom=std::clamp(zoom+restore.wheel.y*.09f,.62f,3.f);
        require(close(zoom,initial_zoom),"Ordinary wheel failed to restore zoom");
        load_session(session,view,baseline,output/world_saves[0]); (void)compose(view,session);
        // Reuse image comparison only after confirming the restored pixel-space
        // geometry is identical, independent of harmless wheel float rounding.
        const auto restored_house=view.tile_screen(26.f,22.f);
        const auto restored_bounds=source_bounds(house,{restored_house.x,restored_house.y+15.f*zoom},house_width*zoom);
        require(close(restored_bounds.w,house_width*initial_zoom),"Restored House scale differs");
        for (const auto index : std::array<std::size_t,2>{1u,2u}) {
            load_session(session,view,baseline,output/world_saves[index]);
            const auto legacy=compose(baseline,session),mixed=compose(view,session); check_typed(mixed,2u);
            const auto projection=project_engine_weather(session.weather(),session.ticks(),double(session.ticks()),false);
            const auto expected=project_engine_environment(projection.environment,{});
            require(mixed.surface_lighting.ambient==expected.lighting.ambient &&
                mixed.surface_lighting.sun_direction==expected.lighting.sun_direction &&
                mixed.surface_lighting.sun_color_intensity==expected.lighting.sun_color_intensity,
                "Normal lighting disagrees with actual shown weather authority");
            for (const auto& material : mixed.sprite_material_draws)
                require(material.surface.response[0]==projection.wetness01,"Normal wetness disagrees with authority");
            const auto name=index==1u?"night":"wet";
            Image li;
            if (index==1u) li=capture_frame(renderer,legacy,output,std::string("material-legacy-")+name,data["captures"]);
            else {
                require(frame_state(legacy)==frame_state(day_legacy_frame),"Authority wet-only fixture changed Legacy frame");
                render_check(renderer,legacy); li=day_legacy;
            }
            const auto mi=capture_frame(renderer,mixed,output,std::string("material-mixed-")+name,data["captures"]);
            require(difference(mi,day_mixed)>0u,"Normal night/wet native control is vacuous");
            if (index==2u) require(li.rgba==day_legacy.rgba,"Authority wet-only fixture changed Legacy baseline pixels");
            data["authority_cases"].push_back({{"id",name},{"weather",weather_row(session.weather())},
                {"hour",projection.environment.time_hour},{"wetness",projection.wetness01},
                {"ambient",mixed.surface_lighting.ambient},{"sun_direction",mixed.surface_lighting.sun_direction},
                {"sun_color_intensity",mixed.surface_lighting.sun_color_intensity},
                {"legacy_vs_mixed_changed_pixels",difference(li,mi)},
                {"mixed_vs_day_changed_pixels",difference(mi,day_mixed)},
                {"legacy_vs_day_changed_pixels",difference(li,day_legacy)},
                {"legacy_frame_sha256",json_hash(frame_state(legacy))},
                {"day_legacy_frame_sha256",json_hash(frame_state(day_legacy_frame))},
                {"legacy_native_capture",index==1u}});
        }
        load_session(session,view,baseline,output/world_saves[0]);
        const auto clean=compose(view,session); check_typed(clean,2u);
        ui.show_world(); ui.set_view_state(&session,{},true,1); ui.update(session);
        auto hud=clean; ui.compose(hud); check_prefix(clean,hud);
        require(ui.world_visible() && ui.hud_visible() && ui.element_text_width_for_test("hud-title")>20.f &&
            ui.element_rml_for_test("wood")!="0","Actual normal HUD state/text is vacuous");
        const auto hud_image=capture_frame(renderer,hud,output,"material-world-hud",data["captures"]);
        const auto hud_change=difference(day_mixed,hud_image); require(hud_change>1000u,"Real HUD native suffix is vacuous");
        data["ui"]["hud"]=rml_counts(hud,clean.commands.size()); data["ui"]["hud"]["changed_pixels"]=hud_change;
        data["ui"]["hud"]["world_prefix_sha256"]=json_hash(frame_state(clean));
        const auto house_tile=view.tile_screen(26.f,22.f); const Point house_ground{house_tile.x,house_tile.y+15.f*zoom};
        const auto selected_point=sample_point(house,source_bounds(house,house_ground,house_width*zoom),true);
        click(view,session,selected_point); require(picked(view,true,house_id),"Actual annotation house selection failed");
        view.set_annotations_visible(true); const auto selected=compose(view,session); check_typed(selected,2u);
        require(selected.vertices.size()>clean.vertices.size() && selected.commands.size()>clean.commands.size(),
            "Selected outline/annotation addition is vacuous");
        const auto selected_image=capture_frame(renderer,selected,output,"material-world-selected",data["captures"]);
        const auto selected_change=difference(day_mixed,selected_image); require(selected_change>0u,"Outline/annotations native contribution absent");
        data["ui"]["selection"]={{"selected_buildings",view.selected_building_ids()},
            {"point",{selected_point.x,selected_point.y}},{"clean_vertices",clean.vertices.size()},
            {"selected_vertices",selected.vertices.size()},{"clean_commands",clean.commands.size()},
            {"selected_commands",selected.commands.size()},{"typed_material_draws",2},{"changed_pixels",selected_change},
            {"outline_scope","original legacy paint outline; typed geometry outline remains OPEN"}};
        view.clear_building_selection(); view.set_annotations_visible(false);
        require(frame_state(compose(view,session))==frame_state(clean),"Clearing annotations/selection changed normal body frame");
        ui.show_menu(); ui.update(&session); auto interleave=fresh_menu(); ui.compose(interleave);
        const auto interleave_image=capture_frame(renderer,interleave,output,"material-menu-interleave",data["captures"]);
        require(interleave_image.rgba==menu_image.rgba,"Unchanged real menu did not repeat exactly");
        const auto world_after=compose(view,session);
        require(frame_state(world_after)==frame_state(clean),"World->UI->World changed normal material state");
        const auto world_after_image=capture_frame(renderer,world_after,output,"material-world-restored",data["captures"]);
        require(world_after_image.rgba==day_mixed.rgba,"World->UI->World pixels changed");
        data["ui"]["interleave"]={{"menu_pixels_equal",true},{"world_pixels_equal",true},
            {"before_frame_sha256",json_hash(frame_state(clean))},{"after_frame_sha256",json_hash(frame_state(world_after))}};

        load_session(session,view,baseline,output/world_saves[3]); auto variants=compose(view,session); check_typed(variants,1u);
        data["variants"].push_back(legacy_variant(originals,original,view,session,variants,
            "building.house.level2",house_id,26.f,22.f,house_width,zoom));
        for (int stage=1;stage<=3;++stage)
            data["variants"].push_back(legacy_variant(originals,original,view,session,variants,
                "building.house.stage"+std::to_string(stage),3+stage,float(16+stage*3),29.f,house_width,zoom));
        capture_frame(renderer,variants,output,"material-variants",data["captures"]);
        load_session(session,view,baseline,output/world_saves[4]); auto winter=compose(view,session); check_typed(winter,1u);
        require(session.winter() && session.calendar()->season==Season::Winter,"Actual winter fixture calendar is not winter");
        data["variants"].push_back(legacy_variant(originals,original,view,session,winter,
            "building.house.winter",house_id,26.f,22.f,house_width,zoom));
        capture_frame(renderer,winter,output,"material-winter",data["captures"]);

        load_session(session,view,baseline,output/world_saves[0]); view.set_paused(false); baseline.set_paused(false);
        MotionClock clock; UnitMotion motion; UnitAnimation animation; double seconds=20.;
        clock.update(session.ticks(),seconds,1,false,false);
        const auto initial=session.residents().front(); require(initial.id==resident_id,"Real motion actor changed");
        motion.add({initial.x,initial.y,initial.layer},session.ticks()); animation.update(initial.x,initial.y,clock.tick());
        (void)compose(view,session,seconds); (void)compose(baseline,session,seconds);
        const auto commanded=session.move({resident_id},21,35); require(commanded.accepted,"Actual move command failed");
        std::set<std::pair<int,int>> observed; bool phase_fallback{},direction_fallback{}; Frame2D last_animation;
        for (int step=1;step<=120;++step) {
            session.advance(); seconds=20.+double(step)*tick_seconds;
            clock.update(session.ticks(),seconds,1,false,false);
            const auto actor=std::find_if(session.residents().begin(),session.residents().end(),
                [](const Resident& r){return r.id==resident_id;}); require(actor!=session.residents().end(),"Motion actor disappeared");
            if (motion.add({actor->x,actor->y,actor->layer},session.ticks())) animation={};
            const auto p=motion.sample(clock.tick()); animation.update(p.x,p.y,clock.tick());
            const auto key=resident_sprite(resident_id,ResidentLoad::Empty,animation.facing,animation.frame());
            auto mixed=compose(view,session,seconds); const auto legacy=compose(baseline,session,seconds);
            const bool mapped=key=="resident.man.walk.2.1"; check_typed(mixed,mapped?2u:1u);
            const auto source=source_sprite(originals,original,key); const auto feet=view.resident_screen(session,resident_id);
            require(feet.has_value(),"Displayed motion query absent"); const Point ground{feet->x,feet->y+6.f*zoom};
            const auto bounds=source_bounds(source,ground,39.f*zoom);
            const auto q=find_quad(mixed,source,bounds,mapped); (void)find_quad(legacy,source,bounds,false);
            if (animation.moving && observed.emplace(animation.facing,animation.frame()).second) {
                const bool phase=animation.frame()!=1,direction=animation.facing!=2;
                phase_fallback=phase_fallback||phase; direction_fallback=direction_fallback||direction;
                data["animation_cases"].push_back({{"key",key},{"facing",animation.facing},{"phase",animation.frame()},
                    {"moving",animation.moving},{"mapped",mapped},{"step",step},{"authority_tick",session.ticks()},
                    {"shown_tick",clock.tick()},{"authority_position",{actor->x,actor->y}},
                    {"displayed_position",{p.x,p.y}},{"ground",{ground.x,ground.y}},
                    {"bounds",rectangle(bounds)},{"quad",quad_row(mixed,q)}});
                render_check(renderer,mixed); last_animation=mixed;
            }
        }
        require(phase_fallback && direction_fallback && observed.size()>=4u && !last_animation.vertices.empty(),
            "Actual animation phase/direction fallback is vacuous");
        capture_frame(renderer,last_animation,output,"material-animation-fallback",data["captures"]);

        load_session(session,view,baseline,output/world_saves[0]); view.set_layer(WorldLayer::Mine); baseline.set_layer(WorldLayer::Mine);
        const auto mine=compose(view,session),mine_oracle=compose(baseline,session);
        require(!mine.linear_hdr_world && mine.sprite_material_draws.empty() &&
            independently_loaded_legacy_state(mine)==independently_loaded_legacy_state(mine_oracle),
            "Mine did not preserve the explicit original fallback");
        const auto mine_image=capture_frame(renderer,mine,output,"material-mine-fallback",data["captures"]);
        const auto mine_legacy_image=capture_frame(renderer,mine_oracle,output,"material-mine-legacy",data["captures"]);
        require(mine_image.rgba==mine_legacy_image.rgba,"Actual Mine fallback pixels differ from original baseline");
        data["authority_cases"].push_back({{"id","mine"},{"hdr",false},{"typed_material_draws",0},
            {"mixed_frame_sha256",json_hash(independently_loaded_legacy_state(mine))},
            {"baseline_frame_sha256",json_hash(independently_loaded_legacy_state(mine_oracle))},
            {"pixels_equal",true},{"vertices",mine.vertices.size()},{"commands",mine.commands.size()}});
        ClientSession pending(70421); std::string connect_error;
        const bool connected=pending.connect_remote("127.0.0.1:1","",connect_error);
        require(connected,"Actual pending-transport setup: "+connect_error);
        require(pending.remote() && !pending.ready() && !pending.world_ready() && !pending.weather().known,
            "Pending remote did not mask authority");
        view.reset_world_state(); baseline.reset_world_state(); view.set_layer(WorldLayer::Surface); baseline.set_layer(WorldLayer::Surface);
        const auto unavailable=compose(view,pending),unavailable_oracle=compose(baseline,pending);
        require(!unavailable.linear_hdr_world && unavailable.sprite_material_draws.empty() &&
            independently_loaded_legacy_state(unavailable)==independently_loaded_legacy_state(unavailable_oracle),
            "Unknown authority did not preserve explicit original fallback");
        capture_frame(renderer,unavailable,output,"material-authority-unavailable",data["captures"]);
        data["authority_cases"].push_back({{"id","unavailable"},{"remote",true},{"ready",false},{"world_ready",false},
            {"weather_known",false},{"hdr",false},{"typed_material_draws",0},
            {"mixed_frame_sha256",json_hash(independently_loaded_legacy_state(unavailable))},
            {"baseline_frame_sha256",json_hash(independently_loaded_legacy_state(unavailable_oracle))},
            {"scope","actual pending GNS connection; recovery_incomplete/reconnecting routes remain OPEN"}});
        require(data["captures"].size()==capture_limit && !fs::exists(settings_path) && ui.take_actions().empty(),
            "Capture/settings/action inventory changed");
        for (const auto& row : save_before) {
            const auto path=output/row.at("path").get<std::string>();
            require(file_row(path,output)==row,"Actual native composition modified a fixture save");
        }
        data["saved_worlds_preserved"]=true;
        data["open_delivery"]={"Full E01-E17/P01 engine acceptance","full normal main startup and package/release",
            "production neutral albedo and all eight authored material masters","reference fidelity",
            "new house draft export/native/production acceptance","Cumulative graph and independent Moon/weather/water FX",
            "typed geometry outline equivalence","all resident directions/phases/cargo assets",
            "remote reconnect/recovery_incomplete normal-view route","physical two-PC/co-op",
            "FPS and GPU execution timing","full suite on this current cohort"};
        data["scope"]="Actual normal GameView::compose LegacyCombined diagnostic painted subset; two-plane authored proxy fixtures, not production/reference-neutral art";
        report["complete"]=true; report["deterministic"]=std::move(data);
        report["measured"]={{"total_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
        save_report(output/"material-report.json",report); check_budget(output);
        std::cout << "Normal registered material diagnostic complete; engine/production/reference/FPS remain OPEN\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (owned) try { report["complete"]=false; report["error"]=error.what(); save_report(output/"material-report.json",report); } catch (...) {}
        return 1;
    }
}

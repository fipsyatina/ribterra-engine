#include "engine_control_probe.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/unit_animation.hpp"
#include "ribterra/unit_motion.hpp"
#include "ribterra/world_projection.hpp"
#include <chrono>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

namespace {
constexpr std::size_t capture_limit = 20u;
constexpr int house_id = 2;
constexpr int resident_id = 1;
constexpr float house_width = 76.f * 2.1f;
constexpr float initial_zoom = 1.30f;
constexpr std::array<std::string_view,6> world_saves{
    "neutral-material-day.save","neutral-material-night.save","neutral-material-wet.save",
    "neutral-material-variants.save","neutral-material-winter.save","neutral-material-two-houses.save"};
constexpr std::string_view house_key="building.house.ready", man_key="resident.man.walk.2.1";
std::map<fs::path,fs::path> retained_junctions;

void junction_exact(const fs::path& path,const fs::path& target) {
    const auto attributes=GetFileAttributesW(path.c_str());
    require(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0u,
        "Owned retained junction lost its reparse identity");
    require(fs::canonical(path)==fs::canonical(target),"Owned retained junction target changed");
}

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
    for (auto iterator=fs::recursive_directory_iterator(root);iterator!=fs::recursive_directory_iterator();++iterator) {
        const auto& entry=*iterator;
        if (const auto alias=retained_junctions.find(entry.path());alias!=retained_junctions.end()) {
            junction_exact(alias->first,alias->second); iterator.disable_recursion_pending(); continue;
        }
        require(!entry.is_symlink(),"Symlink in retained fixture tree"); check_no_reparse(entry.path());
        if (entry.is_regular_file()) names.push_back(entry.path());
    }
    std::sort(names.begin(),names.end()); Json rows=Json::array();
    for (const auto& path : names) rows.push_back(file_row(path,root));
    return rows;
}
void neutral_budget(const fs::path& root,std::uintmax_t pending=0u) {
    std::uintmax_t bytes=pending;
    for (const auto& row : tree_rows(root)) bytes+=row.at("bytes").get<std::uintmax_t>();
    require(bytes<=budget && fs::space(root).available>=384u*1024u*1024u+pending,
        "Neutral native output storage/headroom budget exceeded");
}
void save_neutral_report(const fs::path& path,const Json& value) {
    check_no_reparse(path);
    require(!fs::exists(path),"Refusing to overwrite native report");
    // Keep the complete proof objects. Pretty indentation alone can exceed the
    // retained-output budget when both actual World frame serializers are large.
    const auto encoded=value.dump()+"\n";
    neutral_budget(path.parent_path(),encoded.size());
    {
        const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file!=INVALID_HANDLE_VALUE,"Cannot exclusively create native report; existing output is retained");
        struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{file};
        std::size_t offset{};
        while(offset<encoded.size()) {
            const auto count=static_cast<DWORD>(std::min<std::size_t>(encoded.size()-offset,1u<<20u));
            DWORD written{};
            require(WriteFile(file,encoded.data()+offset,count,&written,nullptr)!=0 && written==count,
                "Native report write failed; partial output is retained");
            offset+=written;
        }
        require(FlushFileBuffers(file)!=0,"Native report flush failed; output is retained");
    }
    neutral_budget(path.parent_path());
}
void preserve_neutral_failure_report(const fs::path& root,const Json& report) {
    const auto path=root/"neutral-material-report.json";
    if(fs::exists(path)) return; // Never replace a completed or partially written report.
    try { save_neutral_report(path,report); }
    catch(const std::exception& write_error) {
        std::cerr << "Full native failure report could not be written: " << write_error.what() << '\n';
        if(fs::exists(path)) return;
        // A bounded failure summary cannot masquerade as a complete proof report.
        // Original run/write errors remain in stderr; all existing artifacts stay.
        const Json minimal{{"schema",1},{"profile",report.value("profile",std::string("unknown"))},
            {"complete",false},{"error","Native run failed; full diagnostic report could not be written. See retained stderr."},
            {"report_scope","minimal failure diagnostic; complete proof report unavailable"}};
        try { save_neutral_report(path,minimal); }
        catch(const std::exception& minimal_error) {
            std::cerr << "Minimal native failure report could not be written: " << minimal_error.what() << '\n';
        }
    }
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
    else {
        const auto typed_commands=static_cast<std::size_t>(std::count_if(frame.commands.begin(),frame.commands.end(),
            [](const auto& command) {
                const auto* draw=std::get_if<GeometryDraw>(&command);
                return draw && draw->index_count!=0u && draw->sprite_material_index!=0u;
            }));
        require(t.graph_pass_count==0u && t.material_draws==typed_commands,
            "LegacyCombined typed draw telemetry disagrees with active normal commands");
    }
}
Image capture_frame(Renderer2D& renderer, const Frame2D& frame, const fs::path& root,
                    std::string_view name, Json& rows) {
    require(rows.size()<capture_limit,"Material capture budget exceeded");
    const auto path=root/(std::string(name)+".png"); require(!fs::exists(path),"Refusing capture overwrite");
    neutral_budget(root,9u*1024u*1024u);
    render_check(renderer,frame,path); require(renderer.frame_telemetry().capture,"Actual capture flag absent");
    rows.push_back({{"id",name},{"file",path.filename().string()},{"width",extent.width},{"height",extent.height},
        {"bytes",fs::file_size(path)},{"sha256",sha256(file_bytes(path))},
        {"frame_sha256",json_hash(frame_state(frame))},{"frame_contract",frame_contract(frame)},
        {"telemetry",telemetry_row(renderer.frame_telemetry())}});
    neutral_budget(root); return read_image(path);
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

void create_worlds(const fs::path& root, Json& rows,bool cold) {
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
    for (std::size_t i=0; i<(cold?1u:world_saves.size()); ++i) {
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
        if (i==5u) {
            state.buildings.push_back({state.next_building_id++,BuildingKind::House,32,25,1.f,WorldLayer::Surface,1u});
            state.buildings.push_back({state.next_building_id++,BuildingKind::Workshop,29,24,1.f,WorldLayer::Surface,1u});
            ++state.topology_revision;
        }
        validate_climate_runtime(state.climate,state.ticks,context,cache);
        std::string error; const bool imported=world.import_state(std::move(state),error);
        require(imported,"Normal fixture import "+std::string(world_saves[i])+": "+error);
        const auto path=root/world_saves[i]; require(!fs::exists(path),"Fixture save exists");
        neutral_budget(root,16u*1024u*1024u);
        const bool saved=world.save(path,error); require(saved,"Normal fixture save: "+error);
        rows.push_back({{"file",world_saves[i]},{"tick",world.ticks()},
            {"calendar_tick",world.calendar().tick.value},{"season",static_cast<int>(world.calendar().season)},
            {"checksum",world.checksum()},{"authored_fixture",true},{"earned_history",false}});
        neutral_budget(root);
    }
}

struct SourceSprite {
    std::string key,page; Json entry; std::array<int,4> crop{};
    std::array<float,2> pivot{}; float reference_width{}; Extent page_size{};
    std::vector<std::uint8_t> rgba;
    Extent mask_size{}; std::vector<std::uint8_t> pooled_alpha,pooled_coverage;
};
void pool_source(SourceSprite& result) {
    const float scale=std::min(1.f,192.f/float(std::max(result.crop[2],result.crop[3])));
    result.mask_size={static_cast<std::uint32_t>(std::max(1L,std::lround(float(result.crop[2])*scale))),
        static_cast<std::uint32_t>(std::max(1L,std::lround(float(result.crop[3])*scale)))};
    const auto count=std::size_t(result.mask_size.width)*result.mask_size.height;
    result.pooled_alpha.assign(count,0u); result.pooled_coverage.assign(count,0u);
    for (std::uint32_t my=0;my<result.mask_size.height;++my) for (std::uint32_t mx=0;mx<result.mask_size.width;++mx) {
        const int x0=static_cast<int>(std::uint64_t(mx)*static_cast<std::uint32_t>(result.crop[2])/result.mask_size.width);
        const int x1=static_cast<int>(std::uint64_t(mx+1u)*static_cast<std::uint32_t>(result.crop[2])/result.mask_size.width);
        const int y0=static_cast<int>(std::uint64_t(my)*static_cast<std::uint32_t>(result.crop[3])/result.mask_size.height);
        const int y1=static_cast<int>(std::uint64_t(my+1u)*static_cast<std::uint32_t>(result.crop[3])/result.mask_size.height);
        std::uint8_t maximum{};
        for (int y=y0;y<std::max(y0+1,y1);++y) for (int x=x0;x<std::max(x0+1,x1);++x)
            maximum=std::max(maximum,result.rgba[(std::size_t(y)*static_cast<std::size_t>(result.crop[2])+static_cast<std::size_t>(x))*4u+3u]);
        const auto p=std::size_t(my)*result.mask_size.width+mx;
        result.pooled_alpha[p]=maximum; result.pooled_coverage[p]=maximum>=64u?255u:0u;
    }
}
std::size_t item_index(const Json& metadata,std::string_view key) {
    std::optional<std::size_t> found;
    for (std::size_t i=0;i<metadata.at("items").size();++i)
        if(metadata.at("items")[i].at("legacy_key").get<std::string>()==key) {
            require(!found,"Ambiguous material legacy key"); found=i;
        }
    require(found.has_value(),"Missing material legacy key: "+std::string(key)); return *found;
}
Json source_record(const Json& item) {
    Json result{{"master_sha256",item.at("master_sha256")},{"registration",item.at("registration")},
        {"geometry_provenance",item.at("geometry_provenance")}};
    if(item.contains("albedo_source")) {
        result["albedo_source"]=item.at("albedo_source");
        result["generated_albedo_sha256"]=item.at("generated_albedo_sha256");
    }
    return result;
}
SourceSprite delivered_source(SourceSprite original,const AssetCatalog& collection,const Json& metadata,const Json& world) {
    const auto& item=metadata.at("items").at(item_index(metadata,original.key));
    const Json* material{};
    for(const auto& row:world.at("materials")) if(row.at("id")==item.at("material_id")) material=&row;
    require(material,"Delivered material missing"); const auto texture_id=material->at("channels").at("albedo").at("texture");
    const Json* texture{}; for(const auto& row:world.at("textures")) if(row.at("id")==texture_id) texture=&row;
    require(texture && texture->at("format")=="rgba8_unorm_srgb","Delivered raw albedo binding missing");
    original.rgba=collection.read_path(texture->at("path").get<std::string>());
    require(original.rgba.size()==std::size_t(original.crop[2])*static_cast<std::size_t>(original.crop[3])*4u,
        "Delivered raw albedo extent differs"); pool_source(original); return original;
}
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
void load_session(ClientSession& session, GameView& view, GameView& baseline, const fs::path& path,
                  bool annotations=false) {
    std::string error; const bool loaded=session.load(path,error);
    require(loaded,"Actual session load "+path.filename().string()+": "+error);
    require(session.ready() && session.world_ready() && session.weather().known && !session.last_load().copied,
        "Current fixture did not publish ready weather authority");
    view.reset_world_state(); baseline.reset_world_state();
    for (auto* candidate : std::array<GameView*,2>{&view,&baseline}) {
        candidate->set_paused(true); candidate->set_annotations_visible(annotations); candidate->set_layer(WorldLayer::Surface);
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
Point sample_point(const SourceSprite& source,const Rect& bounds,bool covered,
                   const SourceSprite* foreground=nullptr,Rect foreground_bounds={}) {
    std::optional<Point> best; float distance=std::numeric_limits<float>::max();
    for (int y=static_cast<int>(std::ceil(bounds.y+.5f)); y<static_cast<int>(std::floor(bounds.y+bounds.h-.5f));++y)
        for (int x=static_cast<int>(std::ceil(bounds.x+.5f)); x<static_cast<int>(std::floor(bounds.x+bounds.w-.5f));++x) {
            const Point p{float(x)+.5f,float(y)+.5f}; if (x<1 || y<1 || x>=1919 || y>=1079) continue;
            const auto sample=mask_at(source,bounds,p); if ((sample[2]>=64)!=covered) continue;
            if(foreground && mask_at(*foreground,foreground_bounds,p)[2]>=64) continue;
            const float mx=(p.x-bounds.x)/bounds.w*float(source.mask_size.width);
            const float my=(p.y-bounds.y)/bounds.h*float(source.mask_size.height);
            if (mx-std::floor(mx)<.05f || mx-std::floor(mx)>.95f || my-std::floor(my)<.05f || my-std::floor(my)>.95f) continue;
            const float d=std::hypot(p.x-(bounds.x+bounds.w*.5f),p.y-(bounds.y+bounds.h*.5f));
            if (d<distance) { best=p; distance=d; }
        }
    require(best.has_value(),"Original alpha sample is vacuous: "+source.key); return *best;
}
Json query_pair(GameView& view, GameView& baseline, ClientSession& session,
                const SourceSprite& source,const SourceSprite& original_source, Point ground, float width, bool building, int id,
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
        const bool baseline_expected=mask_at(original_source,bounds,point)[2]>=64;
        require(picked(view,building,id)==covered && picked(baseline,building,id)==baseline_expected,
            "Actual SDL alpha picking differs for "+source.key);
        samples.push_back({{"point",{point.x,point.y}},{"raw_source_pixel",{raw[0],raw[1]}},{"raw_alpha",raw[2]},
            {"mask_cell",{sample[0],sample[1]}},{"max_alpha",sample[2]},{"expected_pick",covered},{"baseline_expected_pick",baseline_expected},
            {"baseline_pick",picked(baseline,building,id)},
            {"mixed_pick",picked(view,building,id)}});
    }
    Json disagreement=nullptr;
    if(source.rgba!=original_source.rgba) {
        require(source.mask_size.width==original_source.mask_size.width && source.mask_size.height==original_source.mask_size.height,
            "Generated/original mask extents differ");
        std::size_t disagreement_cells{}; for(std::size_t p=0;p<source.pooled_coverage.size();++p)
            disagreement_cells+=source.pooled_coverage[p]!=original_source.pooled_coverage[p];
        require(disagreement_cells>0u,"Generated vs original pooled silhouette disagreement is vacuous");
        std::optional<Point> witness;float best=std::numeric_limits<float>::max();
        for(std::uint32_t y=0;y<source.mask_size.height;++y)
            for(std::uint32_t x=0;x<source.mask_size.width;++x) {
                const auto cell=std::size_t(y)*source.mask_size.width+x;
                if(source.pooled_coverage[cell]==original_source.pooled_coverage[cell]) continue;
                const Point point{bounds.x+(float(x)+.5f)*bounds.w/float(source.mask_size.width),
                    bounds.y+(float(y)+.5f)*bounds.h/float(source.mask_size.height)};
                if(point.x<1.f || point.y<1.f || point.x>=1919.f || point.y>=1079.f) continue;
                const auto a=mask_at(source,bounds,point),b=mask_at(original_source,bounds,point);
                if((a[2]>=64)==(b[2]>=64)) continue;
                const float mx=(point.x-bounds.x)/bounds.w*float(source.mask_size.width);
                const float my=(point.y-bounds.y)/bounds.h*float(source.mask_size.height);
                if(mx-std::floor(mx)<.1f || mx-std::floor(mx)>.9f || my-std::floor(my)<.1f || my-std::floor(my)>.9f) continue;
                const float distance=std::hypot(point.x-(bounds.x+bounds.w*.5f),point.y-(bounds.y+bounds.h*.5f));
                if(distance<best){best=distance;witness=point;}
            }
        require(witness.has_value(),"No interior screen sample for changed neutral pooled silhouette");
        const auto a=mask_at(source,bounds,*witness),b=mask_at(original_source,bounds,*witness);
        click(view,session,*witness); click(baseline,session,*witness);
        require(picked(view,building,id)==(a[2]>=64) && picked(baseline,building,id)==(b[2]>=64),
            "Actual pick still follows the original painted halo at neutral disagreement sample");
        disagreement={{"point",{witness->x,witness->y}},{"generated_mask",a},{"original_mask",b},
            {"generated_pick",picked(view,building,id)},{"original_pick",picked(baseline,building,id)},
            {"different_pooled_cells",disagreement_cells},{"sample_scope","continuous CPU mask-cell center; not a final-PNG alpha sample"}};
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
        {"samples",std::move(samples)},{"silhouette_disagreement",std::move(disagreement)},
        {"original_rgba_sha256",sha256(original_source.rgba)},{"original_pooled_coverage_sha256",sha256(original_source.pooled_coverage)},
        {"half_open_point",{boundary.x,boundary.y}},
        {"half_open_baseline_pick",false},{"half_open_mixed_pick",false},
        {"camera_origin",{origin.x,origin.y}}};
}

void bind_registration(Json& metadata,Json& world,std::size_t index) {
    auto& item=metadata.at("items").at(index);
    const auto hash=source_record_hash(source_record(item)); item["source_hash"]=hash;
    bool found{}; for(auto& material:world.at("materials")) if(material.at("id")==item.at("material_id")) {
        material["source_hash"]=hash; found=true;
    }
    require(found,"Mutated material association missing");
}
Json outline_row(const Frame2D& frame,const SourceSprite& source,Point ground,float width,float radius) {
    const auto bounds=source_bounds(source,ground,width);
    constexpr std::array<Point,8> directions{{{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}}};
    std::optional<std::size_t> command; Json quads=Json::array(); std::uint32_t first{};
    TextureHandle texture{}; std::set<std::uint32_t> unique;
    for(std::size_t offset=0;offset<directions.size();++offset) {
        const Rect shifted{bounds.x+directions[offset].x*radius,bounds.y+directions[offset].y*radius,bounds.w,bounds.h};
        const std::array<Point,4> points{{{shifted.x,shifted.y},{shifted.x+shifted.w,shifted.y},
            {shifted.x+shifted.w,shifted.y+shifted.h},{shifted.x,shifted.y+shifted.h}}};
        std::size_t matches{}; Json row;
        for(std::size_t ci=0;ci<frame.commands.size();++ci) {
            const auto* draw=std::get_if<GeometryDraw>(&frame.commands[ci]);
            if(!draw || draw->pass!=Pass::WorldOrdered || draw->blend!=Blend::Alpha || !draw->texture.slot ||
                draw->surface_lit || draw->sprite_material_index || draw->ground_material_index || draw->scissor_enabled) continue;
            for(std::uint32_t j=0;j+6u<=draw->index_count;j+=6u) {
                const auto at=draw->first_index+j,a=frame.indices.at(at);
                const std::array<std::uint32_t,6> indices{a,a+1u,a+2u,a,a+2u,a+3u};
                if(!std::equal(indices.begin(),indices.end(),frame.indices.begin()+at)) continue;
                bool equal=true; Json vertices=Json::array();
                for(std::uint32_t k=0;k<4u;++k) {
                    const auto& v=frame.vertices.at(a+k);
                    equal=equal && close(v.position.x,points[k].x) && close(v.position.y,points[k].y) &&
                        v.uv.x==((k==1u || k==2u)?1.f:0.f) && v.uv.y==(k>=2u?1.f:0.f) &&
                        v.color.r==69u && v.color.g==210u && v.color.b==247u && v.color.a==235u;
                    vertices.push_back({v.position.x,v.position.y,v.uv.x,v.uv.y,v.color.r,v.color.g,v.color.b,v.color.a});
                }
                if(!equal) continue;
                ++matches;
                if(!command) {command=ci; first=at; texture=draw->texture;}
                require(*command==ci && draw->texture==texture && at==first+offset*6u,
                    "Outline offsets are not one contiguous cached command");
                for(std::uint32_t k=0;k<4u;++k) unique.insert(a+k);
                row={{"offset",{directions[offset].x,directions[offset].y}},{"first_index",at},
                    {"indices",indices},{"vertices",std::move(vertices)}};
            }
        }
        if(matches!=1u) {
            Json candidates=Json::array();
            for(std::size_t ci=0;ci<frame.commands.size() && candidates.size()<3u;++ci) {
                const auto* draw=std::get_if<GeometryDraw>(&frame.commands[ci]);
                if(!draw || draw->pass!=Pass::WorldOrdered || draw->index_count!=48u ||
                    draw->surface_lit || draw->sprite_material_index || draw->ground_material_index) continue;
                Json corners=Json::array();
                const auto first_vertex=frame.indices.at(draw->first_index);
                for(std::uint32_t k=0;k<4u;++k) {
                    const auto& vertex=frame.vertices.at(first_vertex+k);
                    corners.push_back({vertex.position.x,vertex.position.y,vertex.uv.x,vertex.uv.y,
                        vertex.color.r,vertex.color.g,vertex.color.b,vertex.color.a});
                }
                candidates.push_back({{"command",ci},{"draw",draw_row(*draw)},{"first_quad",std::move(corners)}});
            }
            Json expected=Json::array();for(const auto& point:points)expected.push_back({point.x,point.y});
            throw std::runtime_error("Expected unique source-derived typed outline offset "+std::to_string(offset)+
                "; matches="+std::to_string(matches)+"; expected_points="+expected.dump()+
                "; candidate_48_index_commands="+candidates.dump());
        }
        quads.push_back(std::move(row));
    }
    require(command.has_value() && unique.size()==32u,"Typed outline32-vertex witness is vacuous");
    const auto& draw=std::get<GeometryDraw>(frame.commands.at(*command));
    require(draw.index_count==48u && frame.surface_vertices.size()==frame.vertices.size(),
        "Typed outline48-index command or neutral companions differ");
    return {{"command",*command},{"draw",draw_row(draw)},{"texture",{texture.slot,texture.generation}},
        {"radius",radius},{"ground",{ground.x,ground.y}},{"bounds",rectangle(bounds)},
        {"vertex_count",unique.size()},{"index_count",48},{"quads",std::move(quads)}};
}
Json art_counters(const GameArt& art) {
    return {{"hit_mask_bytes",art.hit_mask_bytes()},{"hit_mask_count",art.hit_mask_count()},
        {"outline_texture_bytes",art.outline_texture_bytes()},{"outline_sprite_count",art.outline_sprite_count()}};
}
Json direct_controls(Renderer2D& renderer,const fs::path& runtime,const AssetCatalog& originals,
                     const AssetCatalog& collection,const Json& metadata,const SourceSprite& house,const SourceSprite& man) {
    GameArt legacy(renderer); legacy.load(originals); const GameArtCacheLimits maximum;
    require(legacy.hit_mask_bytes()<maximum.hit_mask_bytes && legacy.outline_texture_bytes()<maximum.outline_texture_bytes,
        "Legacy retained counters leave no typed capacity");
    const GameArtCacheLimits remaining{maximum.hit_mask_bytes-legacy.hit_mask_bytes(),
        maximum.outline_texture_bytes-legacy.outline_texture_bytes()};
    GameArt typed(renderer); typed.load(collection,remaining);
    const auto id=metadata.at("items").at(item_index(metadata,house_key)).at("sprite_id").get<std::string>();
    const Point ground{960.f,540.f}; const float scale=house_width*4.f/house.reference_width;
    Frame2D base; base.logical_size=extent; base.linear_hdr_world=true;
    typed.lit_sprite(base,id,ground,{0.f,0.f,0.f},initial_zoom,scale);
    base.surface_vertices.resize(base.vertices.size());
    const auto original_frame=frame_state(base),original_counters=art_counters(typed); Json rows=Json::array();
    const auto body_exact=[&]{
        Frame2D after;after.logical_size=extent;after.linear_hdr_world=true;
        typed.lit_sprite(after,id,ground,{0.f,0.f,0.f},initial_zoom,scale);after.surface_vertices.resize(after.vertices.size());
        require(frame_state(after)==original_frame,"Rejected load changed accepted typed material/bounds payload");
    };
    const auto reject=[&](std::string_view name,const std::function<void(Frame2D&)>& action,std::string_view expected) {
        auto candidate=base; const auto prior=frame_state(candidate); bool failed{}; std::string message;
        try {action(candidate);} catch(const std::exception& error) {failed=true;message=error.what();}
        require(failed && message.find(expected)!=std::string::npos && frame_state(candidate)==prior &&
            art_counters(typed)==original_counters,"Direct outline rejection was not atomic/intended: "+std::string(name)+": "+message);
        rows.push_back({{"id",name},{"error",message},{"diagnostic_contains",expected},{"frame_exact",true},{"cache_exact",true}});
    };
    const Color invisible{69,210,247,0};
    {
        auto candidate=base;bool failed{};std::string message;
        try{typed.outline_lit_sprite(candidate,"missing",ground,initial_zoom,scale,0.f,Pass::WorldOrdered,invisible);}
        catch(const std::out_of_range& error){failed=true;message=error.what();}
        require(failed && frame_state(candidate)==original_frame && art_counters(typed)==original_counters,
            "Unknown lit outline ID did not reject before zero no-op");
        rows.push_back({{"id","unknown-id-alpha-zero"},{"error",message},{"exception_type","out_of_range"},{"frame_exact",true},{"cache_exact",true}});
    }
    reject("invalid-pass-alpha-zero",[&](Frame2D& f){typed.outline_lit_sprite(f,id,ground,initial_zoom,scale,1.f,Pass::Ui,invisible);},"world-ordered World");
    // Content/prefix mutation creates the input candidate; compare that exact
    // candidate before/after the API, rather than repairing it after failure.
    const auto malformed=[&](std::string_view label,Frame2D candidate,std::string_view reason) {
        const auto prior=frame_state(candidate);bool failed{};std::string message;
        try{typed.outline_lit_sprite(candidate,id,ground,initial_zoom,scale,0.f,Pass::WorldOrdered,invisible);}
        catch(const std::exception& error){failed=true;message=error.what();}
        require(failed && message.find(reason)!=std::string::npos && frame_state(candidate)==prior &&
            art_counters(typed)==original_counters,"Malformed direct outline input changed frame/cache");
        rows.push_back({{"id",label},{"error",message},{"diagnostic_contains",reason},{"frame_exact",true},{"cache_exact",true}});
    };
    auto bad_content=base;bad_content.content=FrameContent::UiOnly;malformed("invalid-content-alpha-zero",bad_content,"world-ordered World");
    for(const auto& [name,radius]:std::array<std::pair<std::string_view,float>,3>{{{"radius-negative",-1.f},
        {"radius-nan",std::numeric_limits<float>::quiet_NaN()},{"radius-infinite",std::numeric_limits<float>::infinity()}}})
        reject(name,[&,radius](Frame2D& f){typed.outline_lit_sprite(f,id,ground,initial_zoom,scale,radius,Pass::WorldOrdered,invisible);},"finite nonnegative");
    for(const auto& [name,zoom,geometry]:std::array<std::tuple<std::string_view,float,float>,3>{{{"zoom-zero",0.f,scale},
        {"zoom-nan",std::numeric_limits<float>::quiet_NaN(),scale},{"scale-zero",initial_zoom,0.f}}})
        reject(name,[&,zoom,geometry](Frame2D& f){typed.outline_lit_sprite(f,id,ground,zoom,geometry,0.f,Pass::WorldOrdered,invisible);},"Lit sprite projection");
    reject("ground-nan",[&](Frame2D& f){typed.outline_lit_sprite(f,id,{std::numeric_limits<float>::quiet_NaN(),ground.y},
        initial_zoom,scale,0.f,Pass::WorldOrdered,invisible);},"Lit sprite projection");
    auto short_prefix=base;short_prefix.surface_vertices.pop_back();malformed("prefix-short",short_prefix,"vertex-aligned");
    auto long_prefix=base;long_prefix.surface_vertices.push_back({});malformed("prefix-long",long_prefix,"vertex-aligned");
    auto authored=base;authored.surface_vertices.clear();std::get<GeometryDraw>(authored.commands.at(0)).surface_lit=true;
    malformed("authored-prefix-not-synthesized",authored,"cannot synthesize");
    for(const auto& [name,radius,tint]:std::array<std::tuple<std::string_view,float,Color>,2>{{{"alpha-zero",1.f,invisible},
        {"radius-zero",0.f,{69,210,247,235}}}}) {
        auto candidate=base; typed.outline_lit_sprite(candidate,id,ground,initial_zoom,scale,radius,Pass::WorldOrdered,tint);
        require(frame_state(candidate)==original_frame && art_counters(typed)==original_counters,"Valid outline no-op mutated payload/cache");
        rows.push_back({{"id",name},{"frame_exact",true},{"cache_exact",true}});
    }
    auto outlined=base;typed.outline_lit_sprite(outlined,id,ground,initial_zoom,scale,1.56f,Pass::WorldOrdered,{69,210,247,235});
    const auto first_outline=outline_row(outlined,house,ground,house_width*initial_zoom,1.56f);
    const auto cached=art_counters(typed);auto repeat=base;
    typed.outline_lit_sprite(repeat,id,ground,initial_zoom,scale,1.56f,Pass::WorldOrdered,{69,210,247,235});
    require(frame_state(repeat)==frame_state(outlined) && art_counters(typed)==cached && typed.outline_sprite_count()==1u,
        "Repeated direct outline changed handle/cache/payload");
    require(legacy.hit_mask_bytes()+typed.hit_mask_bytes()<=maximum.hit_mask_bytes &&
        legacy.outline_texture_bytes()+typed.outline_texture_bytes()<=maximum.outline_texture_bytes,"Combined retained budgets exceeded");
    bool cap_rejected{};std::string cap_error;
    try{typed.load(collection,{1u,remaining.outline_texture_bytes});}catch(const std::exception& error){cap_rejected=true;cap_error=error.what();}
    const auto bounds=typed.lit_sprite_bounds(id,ground,initial_zoom,scale);
    require(cap_rejected && cap_error.find("hit-mask")!=std::string::npos && art_counters(typed)==cached &&
        close(bounds.w,house_width*initial_zoom),"Exhausted remaining mask budget changed accepted typed catalog");
    body_exact();
    GameArt no_outline(renderer);no_outline.load(collection,{remaining.hit_mask_bytes,0u});
    auto candidate=base;const auto counters=art_counters(no_outline);bool outline_rejected{};
    try{no_outline.outline_lit_sprite(candidate,id,ground,initial_zoom,scale,1.f);}
    catch(const std::runtime_error& error){outline_rejected=std::string(error.what())=="Lit outline texture budget exceeded";}
    require(outline_rejected && frame_state(candidate)==original_frame && art_counters(no_outline)==counters,
        "Zero lazy outline cap did not preserve frame/cache");
    Json invalid_limits=Json::array();
    for(const auto& [label,limits]:std::array<std::pair<std::string_view,GameArtCacheLimits>,2>{{
        {"hit-limit-above-maximum",{maximum.hit_mask_bytes+1u,remaining.outline_texture_bytes}},
        {"outline-limit-above-maximum",{remaining.hit_mask_bytes,maximum.outline_texture_bytes+1u}}}}) {
        bool rejected{};std::string error;
        try{typed.load(collection,limits);}catch(const std::invalid_argument& failure){rejected=true;error=failure.what();}
        require(rejected && error=="Game art cache caps exceed retained memory limits" && art_counters(typed)==cached,
            "Invalid retained cache limit changed accepted typed art");
        body_exact();
        invalid_limits.push_back({{"id",label},{"error",error},{"cache_exact",true}});
    }
    Frame2D mirror;mirror.logical_size=extent;legacy.sprite(mirror,man_key,ground,39.f,Pass::WorldOrdered,{255,255,255,255},true);
    const auto& mirror_draw=std::get<GeometryDraw>(mirror.commands.at(0));
    require(!mirror_draw.sprite_material_index && mirror.vertices.size()==4u &&
        mirror.vertices[0].uv.x>mirror.vertices[1].uv.x,"Direct original mirrored UV fallback is vacuous");
    const auto mirror_bounds=source_bounds(man,ground,39.f);Json mirror_samples=Json::array();
    for(const bool covered:std::array<bool,2>{true,false}) {
        const auto source=sample_point(man,mirror_bounds,covered);
        const Point reflected{mirror_bounds.x+mirror_bounds.w-(source.x-mirror_bounds.x),source.y};
        const bool actual=legacy.hit_test_sprite(man_key,ground,39.f,reflected,true);
        require(actual==covered,"Direct original mirrored hit mask differs from reflected source pool");
        mirror_samples.push_back({{"source_point",{source.x,source.y}},{"reflected_point",{reflected.x,reflected.y}},
            {"source_mask",mask_at(man,mirror_bounds,source)},{"expected_pick",covered},{"actual_pick",actual}});
    }
    Json injected;
    {
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra sampled failure diagnostic",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),"Fault window creation failed");RendererOptions options;options.world_graph=WorldGraph::LegacyCombined;
        options.test_fail_sampled_texture_creation_at=1u;
        Renderer2D fault_renderer(window.get(),runtime,options);GameArt fault(fault_renderer);fault.load(originals);
        const auto prior=art_counters(fault);Frame2D initial;initial.logical_size=extent;
        fault.sprite(initial,house_key,ground,house_width);const auto state=frame_state(initial);
        bool rejected{};std::string error;try{fault.load(collection);}catch(const std::runtime_error& failure){rejected=true;error=failure.what();}
        Frame2D after;after.logical_size=extent;fault.sprite(after,house_key,ground,house_width);
        require(rejected && error=="Injected sampled texture candidate creation failure" && art_counters(fault)==prior &&
            frame_state(after)==state && fault.contains(house_key) && !fault.contains_lit(id),"Injected sampled failure lost original art");
        injected={{"error",error},{"legacy_catalog_exact",true},{"frame_exact",true},{"cache_exact",true},
            {"scope","Renderer-injected sampled candidate failure; physical GPU OOM/plain outline fault OPEN"}};
    }
    return {{"api_controls",std::move(rows)},{"legacy",art_counters(legacy)},{"typed",cached},
        {"remaining_hit_mask_bytes",remaining.hit_mask_bytes},{"remaining_outline_bytes",remaining.outline_texture_bytes},
        {"first_outline",first_outline},{"repeat_frame_exact",true},{"repeat_cache_exact",true},
        {"remaining_hit_cap_rejected",true},{"remaining_hit_cap_error",cap_error},{"zero_outline_cap_rejected",true},
        {"invalid_limits",invalid_limits},{"legacy_mirror",{{"quad",quad_row(mirror,{0u,0u,{0u,1u,2u,3u},0u})},
            {"bounds",rectangle(mirror_bounds)},{"samples",mirror_samples},{"pool_sha256",sha256(man.pooled_coverage)},
            {"scope","Direct original GameArt API; full canonical catalog normal mirrored route untriggered"}}},
        {"sampled_candidate_failure",std::move(injected)},{"maximum_hit_mask_bytes",maximum.hit_mask_bytes},
        {"maximum_outline_bytes",maximum.outline_texture_bytes},
        {"memory_scope","Published retained caches; candidate coexistence/transient decode separate"}};
}

using Payload=std::map<std::string,std::vector<std::uint8_t>>;
Payload fixture_payload(const fs::path& root) {
    Payload result;for(const auto& row:tree_rows(root)) {
        const auto name=row.at("path").get<std::string>();result.emplace(name,file_bytes(root/name));
    }return result;
}
void payload_json(Payload& files,std::string_view path,const Json& value) {
    const auto text=value.dump()+"\n";files[std::string(path)]={text.begin(),text.end()};
}
void payload_manifest(Payload& files,Json manifest,const std::map<std::string,std::size_t>& declared={}) {
    auto& rows=manifest.at("assets");std::sort(rows.begin(),rows.end(),[](const Json& a,const Json& b){return a.at("id").get<std::string>()<b.at("id").get<std::string>();});
    std::string canonical;
    for(auto& row:rows) {
        const auto& bytes=files.at(row.at("path").get<std::string>());row["sha256"]=sha256(bytes);row["bytes"]=bytes.size();
        if(const auto at=declared.find(row.at("id").get<std::string>());at!=declared.end()) row["bytes"]=at->second;
        for(const char* key:std::array<const char*,8>{"id","path","kind","sha256","license","source","author","notice"}) {
            const auto value=row.at(key).get<std::string>();canonical+=std::to_string(value.size())+":"+value;
        }canonical+='\n';
    }
    manifest["content_sha256"]=sha256(std::span(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
    payload_json(files,"manifest.json",manifest);
}
void flush_fixture(const fs::path& root,const fs::path& output,const Payload& files) {
    std::uintmax_t total{};for(const auto& [name,bytes]:files){(void)name;total+=bytes.size();}
    neutral_budget(output,total);require(!fs::exists(root) && fs::create_directory(root),"Cold fixture root already exists");
    for(const auto& [name,bytes]:files) {
        const auto destination=root/fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
        fs::create_directories(destination.parent_path());save_bytes(destination,bytes);
    }
}
void create_retained_junction(const fs::path& path,const fs::path& target) {
    require(!fs::exists(path) && fs::create_directory(path),"Retained junction path exists");
    const auto absolute=fs::canonical(target).wstring();const auto substitute=L"\\??\\"+absolute;
    require(substitute.size()+absolute.size()+2u<8000u,"Retained junction target is too long");
    const auto data_bytes=static_cast<WORD>((substitute.size()+1u+absolute.size()+1u)*sizeof(wchar_t));
    std::vector<std::uint8_t> buffer(16u+data_bytes);
    const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;const WORD length=static_cast<WORD>(8u+data_bytes);
    const WORD substitute_offset=0u,substitute_length=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
    const WORD print_offset=static_cast<WORD>((substitute.size()+1u)*sizeof(wchar_t));
    const WORD print_length=static_cast<WORD>(absolute.size()*sizeof(wchar_t));
    std::memcpy(buffer.data(),&tag,4u);std::memcpy(buffer.data()+4u,&length,2u);
    std::memcpy(buffer.data()+8u,&substitute_offset,2u);std::memcpy(buffer.data()+10u,&substitute_length,2u);
    std::memcpy(buffer.data()+12u,&print_offset,2u);std::memcpy(buffer.data()+14u,&print_length,2u);
    std::memcpy(buffer.data()+16u,substitute.c_str(),(substitute.size()+1u)*sizeof(wchar_t));
    std::memcpy(buffer.data()+16u+print_offset,absolute.c_str(),(absolute.size()+1u)*sizeof(wchar_t));
    const HANDLE handle=CreateFileW(path.c_str(),GENERIC_WRITE,0u,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Cannot open owned junction directory");DWORD returned{};
    const bool created=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),
        nullptr,0u,&returned,nullptr)!=FALSE;const bool closed=CloseHandle(handle)!=FALSE;
    require(created && closed,"Owned retained junction creation failed");
    retained_junctions.emplace(path,target);junction_exact(path,target);
}
Json cold_controls(GameView& view,ClientSession& session,const fs::path& valid,const fs::path& output) {
    const auto before=frame_state(compose(view,session));const auto camera=view.tile_screen(24.f,24.f);
    const auto selected=view.selected_building_ids(),residents=view.selected_ids();
    require(selected==std::vector<int>{house_id},"Cold rejection pick control is vacuous");
    const auto saves=sha256(file_bytes(output/world_saves[0]));Json rows=Json::array();
    const auto preserve=[&] {
        const auto next=view.tile_screen(24.f,24.f);
        require(frame_state(compose(view,session))==before && view.registered_material_count()==2u &&
            view.selected_building_ids()==selected && view.selected_ids()==residents &&
            camera.x==next.x && camera.y==next.y && sha256(file_bytes(output/world_saves[0]))==saves,
            "Cold failure changed accepted map/frame/picks/camera/save");
    };
    const auto reject=[&](std::string_view id,const std::function<void()>& action,std::string_view reason,Json detail=Json::object()) {
        bool failed{};std::string error;try{action();}catch(const std::exception& failure){failed=true;error=failure.what();}
        require(failed && error.find(reason)!=std::string::npos,"Cold case missed intended rejection: "+std::string(id)+": "+error);
        preserve();detail["id"]=id;detail["error"]=error;detail["diagnostic_contains"]=reason;
        detail["frame_camera_pick_save_exact"]=true;detail["before_frame_sha256"]=json_hash(before);
        detail["after_frame_sha256"]=json_hash(frame_state(compose(view,session)));rows.push_back(std::move(detail));
    };
    reject("default-profile-authored-mode",[&]{AssetCatalog catalog(valid);view.load_material_collection(catalog);},"MaterialCollection read profile");
    reject("unknown-read-profile",[&]{AssetCatalog catalog(valid,static_cast<AssetCatalogReadProfile>(77));(void)catalog;},"Unknown asset catalog read profile");
    const auto baseline=fixture_payload(valid);const auto parsed=Json::parse(baseline.at("art/material-collection.json"));
    const auto hi=item_index(parsed,house_key),mi=item_index(parsed,man_key);
    const std::array<std::string_view,20> cases{"schema","reference_width","pivot","decoded_crop_hash","legacy_key","albedo_byte",
        "neutral-null","neutral-extra","palette-scalar","authored-provenance","coverage","generated-sha",
        "palette-declared-oversize","palette-actual-oversize","palette-reread-oversize","notice-reparse",
        "root-reparse","palette-internal-reparse","palette-schema","palette-renamed-id"};
    for(const auto id:cases) {
        const auto root=output/("invalid-"+std::string(id));
        if(id=="root-reparse") {
            create_retained_junction(root,valid);
            reject(id,[&]{AssetCatalog catalog(root,AssetCatalogReadProfile::MaterialCollection);view.load_material_collection(catalog);},
                "Material catalog rejects",{{"root",root.filename().string()},{"reparse_target",valid.generic_string()}});
            continue;
        }
        auto files=baseline;auto metadata=parsed;auto world=Json::parse(files.at("art/world.json"));
        auto manifest=Json::parse(files.at("manifest.json"));auto& house=metadata["items"][hi];auto& man=metadata["items"][mi];
        const auto palette_path=house.at("albedo_source").at("palette_path").get<std::string>();
        auto palette=Json::parse(files.at(palette_path));std::string reason,mutation;std::map<std::string,std::size_t> declared;
        bool changed_palette{},changed_source{};std::string junction_name;fs::path junction_target;
        const auto palette_id=house.at("namespace").get<std::string>()+".palette";
        if(id=="schema"){world["schema"]=3;reason="Normal material collection schema/counts";mutation="world.schema";}
        else if(id=="reference_width"){man["registration"]["reference_width"]=139.;changed_source=true;reason="Registered source crop/pivot/reference width/catalog hash";mutation="resident.registration.reference_width";}
        else if(id=="pivot"){man["registration"]["pivot_px"][0]=1.;changed_source=true;reason="Registered source crop/pivot/reference width/catalog hash";mutation="resident.registration.pivot_px";}
        else if(id=="decoded_crop_hash"){man["registration"]["decoded_crop_sha256"]=std::string(64u,'0');changed_source=true;reason="Registered decoded paint hash";mutation="resident.registration.decoded_crop_sha256";}
        else if(id=="legacy_key"){man["legacy_key"]="resident.man.walk.unknown";reason="unique exact supported original legacy key";mutation="resident.legacy_key";}
        else if(id=="albedo_byte"){
            const auto material_id=man.at("material_id");Json* texture{};Json albedo;
            for(auto& material:world["materials"]) if(material.at("id")==material_id) albedo=material.at("channels").at("albedo").at("texture");
            for(auto& row:world["textures"]) if(row.at("id")==albedo) texture=&row;
            require(texture,"Painted resident texture missing");auto& bytes=files.at(texture->at("path").get<std::string>());
            bytes[0]^=1u;(*texture)["sha256"]=sha256(bytes);reason="Registered albedo must preserve the exact original cropped RGBA";mutation="resident.albedo.byte0";
        }
        else if(id=="neutral-null"){house["albedo_source"]=nullptr;changed_source=true;reason="Neutral material object has missing or extra fields";mutation="house.albedo_source=null";}
        else if(id=="neutral-extra"){house["albedo_source"]["extra"]=true;changed_source=true;reason="Neutral material object has missing or extra fields";mutation="house.albedo_source.extra";}
        else if(id=="palette-scalar"){palette["materials"][0]["roughness"]=true;changed_palette=true;reason="Neutral material scalar";mutation="palette.materials[0].roughness";}
        else if(id=="authored-provenance"){house["albedo_source"]["provenance"]["author"]="Different author";changed_source=true;reason="palette manifest/provenance binding";mutation="house.albedo_source.provenance.author";}
        else if(id=="coverage"){house["coverage"]["original_alpha_samples"]=0;reason="coverage declarations differ";mutation="house.coverage.original_alpha_samples";}
        else if(id=="generated-sha"){house["generated_albedo_sha256"]=std::string(64u,'0');changed_source=true;reason="Generated neutral albedo digest";mutation="house.generated_albedo_sha256";}
        else if(id=="palette-schema"){palette["schema"]=2;changed_palette=true;reason="Neutral material integer is outside its bounded range";mutation="palette.schema";}
        else if(id=="palette-renamed-id"){for(auto& row:manifest["assets"]) if(row.at("id")==palette_id) row["id"]=house.at("namespace").get<std::string>()+".renamed";
            reason="Unknown asset ID: "+palette_id;mutation="manifest.palette.id";}
        else if(id=="notice-reparse"){
            const auto old_notice=house.at("albedo_source").at("provenance").at("notice_path").get<std::string>();
            const auto alias_notice="notice-alias/"+fs::path(old_notice).filename().generic_string();
            house["albedo_source"]["provenance"]["notice_path"]=alias_notice;palette["draft_notice"]["path"]=alias_notice;
            for(auto& row:manifest["assets"]) if(row.at("notice")==old_notice) row["notice"]=alias_notice;
            changed_palette=true;junction_name="notice-alias";junction_target=(valid/old_notice).parent_path();
            reason="Neutral material read rejects absent/reparse";mutation="authored notice raw-root alias";
        }
        else if(id=="palette-internal-reparse"){
            const auto alias_palette="palette-alias/"+fs::path(palette_path).filename().generic_string();
            house["albedo_source"]["palette_path"]=alias_palette;
            for(auto& row:manifest["assets"]) if(row.at("id")==palette_id) row["path"]=alias_palette;
            files[alias_palette]=files.at(palette_path);changed_source=true;junction_name="palette-alias";
            junction_target=(valid/palette_path).parent_path();reason="Material catalog rejects";mutation="palette raw-record alias";
        }
        else if(id=="palette-declared-oversize" || id=="palette-actual-oversize") {
            files[palette_path].resize(512u*1024u+1u,static_cast<std::uint8_t>(' '));
            if(id=="palette-actual-oversize") declared[palette_id]=512u*1024u;
            reason=id=="palette-declared-oversize"?"Material palette declared bytes exceed 512 KiB":"Material palette exceeds 512 KiB";
            mutation=id=="palette-declared-oversize"?"actual+declared palette bytes524289":"actual palette524289/declared524288";
        }
        else if(id=="palette-reread-oversize"){reason="Material palette exceeds 512 KiB";mutation="post-constructor palette growth";}
        else require(false,"Unhandled cold case");
        if(changed_palette){payload_json(files,palette_path,palette);house["albedo_source"]["palette_sha256"]=sha256(files.at(palette_path));changed_source=true;}
        if(changed_source){bind_registration(metadata,world,hi);bind_registration(metadata,world,mi);}
        payload_json(files,"art/material-collection.json",metadata);payload_json(files,"art/world.json",world);
        payload_manifest(files,manifest,declared);
        // Bind the bytes before the owned junction is made. Its actual target is
        // the unchanged accepted input, never another mutable failed fixture.
        if(id=="palette-internal-reparse") files.erase(house.at("albedo_source").at("palette_path").get<std::string>());
        flush_fixture(root,output,files);
        if(!junction_name.empty()) create_retained_junction(root/junction_name,junction_target);
        if(id=="palette-reread-oversize") {
            AssetCatalog catalog(root,AssetCatalogReadProfile::MaterialCollection);auto copied=catalog;
            require(copied.read_profile()==AssetCatalogReadProfile::MaterialCollection,"Copied catalog lost bounded profile");
            const auto path=root/palette_path,retained=root/"retained-before-palette.json";
            require(!fs::exists(retained),"Retained pre-growth palette exists");
            fs::rename(path,retained);std::vector<std::uint8_t> oversized(512u*1024u+1u,static_cast<std::uint8_t>(' '));
            neutral_budget(output,oversized.size());save_bytes(path,oversized);
            for(const auto operation:std::array<std::string_view,3>{"verify","read","read_path"})
                reject(std::string(id)+"-"+std::string(operation),[&]{
                    if(operation=="verify") copied.verify();else if(operation=="read") (void)copied.read(palette_id);
                    else (void)copied.read_path(palette_path);
                },reason,{{"root",root.filename().string()},{"mutation",mutation},{"operation",operation},
                    {"initial_constructor_passed",true},{"copied_profile_preserved",true},{"files",tree_rows(root)}});
        }else reject(id,[&]{AssetCatalog catalog(root,AssetCatalogReadProfile::MaterialCollection);catalog.verify();view.load_material_collection(catalog);},
            reason,{{"root",root.filename().string()},{"mutation",mutation},{"files",tree_rows(root)},{"constructor_inside_try",true}});
        neutral_budget(output);
    }
    return {{"rejections",std::move(rows)},{"variant_catalog_count",cases.size()},{"captures",0},
        {"registered_count",view.registered_material_count()},{"saved_world_exact",true}};
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
        require(argc==8,"Usage: GameViewNeutralMaterialProbe positive|cold runtime-assets original-assets material-collection painted-collection ui-kit fresh-output");
        const std::string profile=argv[1];require(profile=="positive" || profile=="cold","Unknown neutral native profile");
        const bool cold=profile=="cold";report["profile"]=profile;
        const auto runtime=fs::absolute(argument(argv[2])), original_path=fs::absolute(argument(argv[3]));
        const auto collection_path=fs::absolute(argument(argv[4])),painted_path=fs::absolute(argument(argv[5]));
        const auto ui_path=fs::absolute(argument(argv[6]));output=fs::absolute(argument(argv[7]));
        require(output.parent_path()==runtime.parent_path().parent_path() && !fs::exists(output),
            "Material output must be a fresh immediate build child");
        for (const auto& path : std::array<fs::path,6>{runtime,original_path,collection_path,painted_path,ui_path,output}) check_no_reparse(path);
        require(fs::space(output.parent_path()).available>=384u*1024u*1024u,"Insufficient evidence headroom");
        require(fs::create_directory(output),"Cannot create new material evidence root"); owned=true;
        Json data{{"fixtures",Json::array()},{"captures",Json::array()},{"zoom_cases",Json::array()},
            {"variants",Json::array()},{"animation_cases",Json::array()},{"authority_cases",Json::array()},
            {"world_saves",cold?Json::array({world_saves[0]}):Json(world_saves)},{"capture_limit",cold?0u:capture_limit}};
        Json protected_before{{"runtime",tree_rows(runtime)},{"original",tree_rows(original_path)},
            {"collection",tree_rows(collection_path)},{"painted",tree_rows(painted_path)},{"ui",tree_rows(ui_path)}};
        create_worlds(output,data["fixtures"],cold);const auto save_before=tree_rows(output);
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
        AssetCatalog originals(original_path),collection(collection_path,AssetCatalogReadProfile::MaterialCollection),painted(painted_path),ui_assets(ui_path);
        originals.verify(); collection.verify(); painted.verify(); ui_assets.verify();
        const auto original=Json::parse(originals.read_path("art/world.json"));
        require(original.at("schema")==1 && original.at("pages").size()==67u,"Original full schema1 page closure changed");
        std::size_t records{}; for (const auto& page : original.at("pages")) records+=page.at("sprites").size();
        require(records==987u,"Original legacy record coverage changed");
        data["inputs"]={{"original_pages",67},{"original_records",records},{"original_content_hash",originals.content_hash()},
            {"collection_content_hash",collection.content_hash()},{"painted_content_hash",painted.content_hash()},{"ui_content_hash",ui_assets.content_hash()},
            {"read_profiles",{{"original",static_cast<int>(originals.read_profile())},{"neutral",static_cast<int>(collection.read_profile())},
                {"painted",static_cast<int>(painted.read_profile())},{"ui",static_cast<int>(ui_assets.read_profile())}}},
            {"original_world_sha256",sha256(originals.read_path("art/world.json"))},
            {"collection_metadata_sha256",sha256(collection.read("material.collection.metadata"))},
            {"collection_world_sha256",sha256(collection.read("art.world"))}};
        data["registrations"]=Json::array();
        const auto collection_metadata=Json::parse(collection.read("material.collection.metadata"));
        for (const auto& item : collection_metadata.at("items")) {
            const auto canonical=source_record_hash(source_record(item));
            require(item.at("source_hash")==canonical,"Accepted source_record hash disagrees with exporter metadata");
            data["registrations"].push_back({{"legacy_key",item.at("legacy_key")},{"sprite_id",item.at("sprite_id")},
                {"material_id",item.at("material_id")},{"source_hash",item.at("source_hash")},
                {"source_record_sha256",canonical},{"source_record",source_record(item)},
                {"catalog_entry_sha256",item.at("registration").at("catalog_entry_sha256")}});
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
        const auto original_house=source_sprite(originals,original,house_key);
        const auto collection_world=Json::parse(collection.read("art.world"));
        const auto house=delivered_source(original_house,collection,collection_metadata,collection_world);
        const auto man=source_sprite(originals,original,"resident.man.walk.2.1");
        const auto& house_item=collection_metadata.at("items").at(item_index(collection_metadata,house_key));
        require(house_item.at("albedo_source").at("kind")=="authored-xyz-triangle-materials-v1" &&
            !collection_metadata.at("items").at(item_index(collection_metadata,man_key)).contains("albedo_source"),
            "Neutral-house/absent-painted-resident mixed input modes differ");
        data["house_source"]={{"legacy_key",house_key},{"original_rgba_sha256",sha256(original_house.rgba)},
            {"generated_rgba_sha256",sha256(house.rgba)},{"coverage",house_item.at("coverage")},
            {"original_pool_sha256",sha256(original_house.pooled_coverage)},{"generated_pool_sha256",sha256(house.pooled_coverage)}};
        const auto clean_before=compose(view,session); check_typed(clean_before,2u);
        const auto cold_house_tile=view.tile_screen(26.f,22.f);
        const Point cold_house_ground{cold_house_tile.x,cold_house_tile.y+15.f*initial_zoom};
        click(view,session,sample_point(house,source_bounds(house,cold_house_ground,house_width*initial_zoom),true));
        auto before=compose(view,session); check_typed(before,2u);
        if(cold) {
            data["cold"]=cold_controls(view,session,collection_path,output);
            require(!renderer.has_submitted_frames() && data["captures"].empty(),"Cold rejections submitted a frame/capture");
            const auto primary_before=frame_state(compose(view,session));
            const auto primary_camera=view.tile_screen(24.f,24.f);
            const auto primary_selected=view.selected_building_ids();
            const auto painted_metadata=Json::parse(painted.read("material.collection.metadata"));
            const auto painted_world=Json::parse(painted.read("art.world"));
            require(painted.read_profile()==AssetCatalogReadProfile::Default && painted_metadata.at("items").size()==2u &&
                painted_metadata.at("paint_colour")=="legacy-painted-neutral-albedo-acceptance-pending",
                "Current all-painted Default compatibility input differs");
            auto painted_copy=painted;require(painted_copy.read_profile()==AssetCatalogReadProfile::Default,
                "Copied painted catalog lost Default profile");
            Json painted_registrations=Json::array(),painted_channels=Json::array();
            for(const auto& item:painted_metadata.at("items")) {
                require(!item.contains("albedo_source") && !item.contains("generated_albedo_sha256"),
                    "Default compatibility input contains new authored-mode fields");
                const auto record=source_record(item);
                const auto canonical=source_record_hash(record);
                require(record.size()==3u && item.at("source_hash")==canonical,
                    "Current painted compatibility source3 hash differs");
                painted_registrations.push_back({{"legacy_key",item.at("legacy_key")},{"material_id",item.at("material_id")},
                    {"sprite_id",item.at("sprite_id")},{"source_record",record},{"source_record_sha256",canonical}});
            }
            for(const auto& texture:painted_world.at("textures")) {
                const auto raw=painted.read_path(texture.at("path").get<std::string>());
                painted_channels.push_back({{"id",texture.at("id")},{"path",texture.at("path")},
                    {"bytes",raw.size()},{"sha256",sha256(raw)},
                    {"prefix",std::vector<std::uint8_t>(raw.begin(),raw.begin()+static_cast<std::ptrdiff_t>(std::min(raw.size(),std::size_t(16u))))}});
            }
            const auto painted_house=delivered_source(original_house,painted,painted_metadata,painted_world);
            const auto painted_man=delivered_source(man,painted,painted_metadata,painted_world);
            require(painted_house.rgba==original_house.rgba && painted_man.rgba==man.rgba &&
                painted_house.pooled_coverage==original_house.pooled_coverage && painted_man.pooled_coverage==man.pooled_coverage,
                "Default painted body or pooled alpha changed from original");
            GameView painted_view(renderer);painted_view.set_logical_size(extent);painted_view.load_art(originals);
            painted_view.load_material_collection(painted);
            require(painted_view.registered_material_count()==2u && painted_view.has_registered_material(house_key) &&
                painted_view.has_registered_material(man_key),"Default painted install did not publish both exact keys");
            painted_view.reset_world_state();painted_view.set_paused(true);painted_view.set_annotations_visible(false);
            painted_view.set_layer(WorldLayer::Surface);
            const auto painted_frame=compose(painted_view,session),painted_legacy=compose(baseline,session);
            check_typed(painted_frame,2u);
            const auto painted_tile=painted_view.tile_screen(26.f,22.f);
            const auto painted_feet=painted_view.resident_screen(session,resident_id);
            require(painted_feet.has_value(),"Default painted resident position absent");
            const Point painted_ground{painted_tile.x,painted_tile.y+15.f*initial_zoom};
            const Point painted_man_ground{painted_feet->x,painted_feet->y+6.f*initial_zoom};
            auto painted_house_query=query_pair(painted_view,baseline,session,painted_house,original_house,
                painted_ground,house_width*initial_zoom,true,house_id,painted_frame,painted_legacy);
            auto painted_man_query=query_pair(painted_view,baseline,session,painted_man,man,
                painted_man_ground,39.f*initial_zoom,false,resident_id,painted_frame,painted_legacy);
            require(frame_state(compose(painted_view,session))==frame_state(painted_frame),
                "Default painted picks left a World/selection mutation");
            render_check(renderer,painted_frame);
            const auto primary_after=frame_state(compose(view,session));
            const auto camera_after=view.tile_screen(24.f,24.f);
            require(primary_before==primary_after && primary_camera.x==camera_after.x && primary_camera.y==camera_after.y &&
                primary_selected==view.selected_building_ids() && view.registered_material_count()==2u,
                "Default compatibility control mutated primary accepted neutral view");
            data["painted_default_compatibility"]={{"id","current_exporter_absent_mode_default"},
                {"read_profile",static_cast<int>(painted.read_profile())},{"copied_profile_exact",true},
                {"registered_count",painted_view.registered_material_count()},{"source_record_fields",3},
                {"registrations",std::move(painted_registrations)},{"raw_channels",std::move(painted_channels)},
                {"house_rgba_sha256",sha256(painted_house.rgba)},{"resident_rgba_sha256",sha256(painted_man.rgba)},
                {"house_pool_sha256",sha256(painted_house.pooled_coverage)},{"resident_pool_sha256",sha256(painted_man.pooled_coverage)},
                {"house",std::move(painted_house_query)},{"resident",std::move(painted_man_query)},
                {"frame_sha256",json_hash(frame_state(painted_frame))},{"frame_contract",frame_contract(painted_frame)},
                {"telemetry",telemetry_row(renderer.frame_telemetry())},{"submitted_after_all_cold_rejections",true},
                {"primary_before_frame_sha256",json_hash(primary_before)},{"primary_after_frame_sha256",json_hash(primary_after)},
                {"primary_camera",{primary_camera.x,primary_camera.y}},{"primary_selected",primary_selected},
                {"primary_frame_camera_pick_registry_exact",true},{"captures",0},
                {"scope","current-exporter absent-mode Default cold install, exact original RGBA/pooled picking and actual normal LegacyCombined submission; no PNG or neutral claim"}};
            data["protected_before"]=protected_before;
            data["protected_after"]={{"runtime",tree_rows(runtime)},{"original",tree_rows(original_path)},
                {"collection",tree_rows(collection_path)},{"painted",tree_rows(painted_path)},{"ui",tree_rows(ui_path)}};
            require(data["protected_before"]==data["protected_after"],"Cold native changed accepted inputs");
            data["retained_junctions"]=Json::array();
            for(const auto& [alias,target]:retained_junctions) data["retained_junctions"].push_back(
                {{"path",alias.lexically_relative(output).generic_string()},
                 {"target",target.generic_string()},{"owned",true},{"target_exact",true},{"followed_in_inventory",false}});
            data["output_inventory_excluding_report"]=tree_rows(output);
            data["scope"]="Cold MaterialCollection actual constructor/read/View rejections before submission, then current all-painted Default compatibility normal submission without PNG; no physical allocation-fault claim";
            data["open_delivery"]={"Positive GPU proof in separate profile","Physical GPU OOM and bad_alloc","Production/reference acceptance",
                "Full E01-E17/P01","Normal main startup","FPS/GPU timing"};
            report["complete"]=true;report["deterministic"]=std::move(data);
            report["measured"]={{"total_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
            save_neutral_report(output/"neutral-material-report.json",report);return 0;
        }
        data["direct"]=direct_controls(renderer,runtime,originals,collection,collection_metadata,house,man);
        require(!renderer.has_submitted_frames(),"Cold validation submitted an actual frame");
        data["cold_install"]={{"registered_count",2},{"before_any_submission",true},
            {"baseline_frame_sha256",json_hash(frame_state(compose(baseline,session)))},
            {"mixed_frame_sha256",json_hash(frame_state(compose(view,session)))}};
        Localization locale(ui_path/"locales","uk"); const auto settings_path=output/"neutral-settings-unwritten.json";
        GameUi ui(renderer,ui_assets,locale,settings_path,extent,false);
        ui.show_menu(); ui.update(&session); auto menu=fresh_menu(); ui.compose(menu);
        require(menu.surface_vertices.empty() && ui.locale()=="uk" && ui.visible_for_test("menu-card") &&
            ui.element_text_width_for_test("app-title")>20.f && ui.element_bounds_for_test("menu-landscape").w>100.f,
            "Real pre-World menu is vacuous");
        auto menu_counts=rml_counts(menu);
        const auto menu_image=capture_frame(renderer,menu,output,"neutral-menu-first",data["captures"]);
        std::set<std::array<std::uint8_t,3>> menu_colors;
        for(std::size_t i=0;i<menu_image.rgba.size();i+=4u)
            menu_colors.insert({menu_image.rgba[i],menu_image.rgba[i+1u],menu_image.rgba[i+2u]});
        require(menu_colors.size()>100u,"Actual RmlUi menu pixel contribution is vacuous");
        data["ui"]["menu_first"]=std::move(menu_counts);
        data["ui"]["menu_first"]["unique_rgb"]=menu_colors.size();
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

        // Real selection outlines are intentionally gated by annotations. Keep
        // both unselected/selected and original/mixed captures in that same
        // public state, including the real minimap/annotation geometry.
        view.set_annotations_visible(true);baseline.set_annotations_visible(true);
        float zoom=initial_zoom;Image day_legacy,day_mixed;Frame2D day_legacy_frame;Json cached_outline_texture;
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
            auto house_query=query_pair(view,baseline,session,house,original_house,house_ground,house_width*zoom,true,house_id,mixed,legacy);
            auto man_query=query_pair(view,baseline,session,man,man,man_ground,39.f*zoom,false,resident_id,mixed,legacy);
            require(frame_state(compose(view,session))==frame_state(mixed),"SDL query transaction left a selection/World mutation");
            const auto mi=capture_frame(renderer,mixed,output,"neutral-body-"+std::string(names[i]),data["captures"]);
            if(!i) {
                const auto bounds=source_bounds(house,house_ground,house_width*zoom);
                const auto quad=find_quad(mixed,house,bounds,true);
                const auto& draw=std::get<GeometryDraw>(mixed.commands.at(quad.command));
                require(draw.index_count==6u && draw.first_index==quad.first_index && draw.sprite_material_index==quad.material,
                    "House visibility control would remove more than its identified body quad");
                auto without_house=mixed;
                without_house.commands.erase(without_house.commands.begin()+static_cast<std::ptrdiff_t>(quad.command));
                const auto source_state=frame_state(mixed),removed_state=frame_state(without_house);
                auto expected_state=source_state;
                expected_state["commands"].erase(expected_state["commands"].begin()+static_cast<std::ptrdiff_t>(quad.command));
                require(removed_state==expected_state && without_house.sprite_material_draws.size()==2u,
                    "House visibility control changed payload beyond its single command");
                const auto removed_image=capture_frame(renderer,without_house,output,"neutral-body-default-house-removed",data["captures"]);
                std::size_t qualified{},changed{};
                for(std::uint32_t y=0u;y<extent.height;++y) for(std::uint32_t x=0u;x<extent.width;++x) {
                    const double px=double(x)+.5,py=double(y)+.5;
                    if(px<double(bounds.x) || py<double(bounds.y) ||
                        px>=double(bounds.x)+double(bounds.w) || py>=double(bounds.y)+double(bounds.h)) continue;
                    const int sx=static_cast<int>(std::floor((px-double(bounds.x))/double(bounds.w)*double(house.crop[2])));
                    const int sy=static_cast<int>(std::floor((py-double(bounds.y))/double(bounds.h)*double(house.crop[3])));
                    if(sx<1 || sy<1 || sx>=house.crop[2]-1 || sy>=house.crop[3]-1) continue;
                    bool opaque=true;
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                        const auto sample=(std::size_t(sy+dy)*static_cast<std::size_t>(house.crop[2])+static_cast<std::size_t>(sx+dx))*4u+3u;
                        opaque=opaque && house.rgba.at(sample)==255u;
                    }
                    if(!opaque) continue;
                    ++qualified;
                    const auto p=(std::size_t(y)*extent.width+x)*4u;
                    if(!std::equal(mi.rgba.begin()+static_cast<std::ptrdiff_t>(p),mi.rgba.begin()+static_cast<std::ptrdiff_t>(p+4u),
                        removed_image.rgba.begin()+static_cast<std::ptrdiff_t>(p))) ++changed;
                }
                require(qualified>0u && changed>0u,"Same-frame generated house body has no qualified native pixel contribution");
                Json removed_indices=Json::array();
                for(std::uint32_t n=0u;n<draw.index_count;++n) removed_indices.push_back(mixed.indices.at(draw.first_index+n));
                data["body_visibility"]={{"id","same_frame_house_body_removal"},
                    {"source_capture","neutral-body-default.png"},{"removed_capture","neutral-body-default-house-removed.png"},
                    {"source_frame",source_state},{"removed_frame",removed_state},
                    {"source_frame_sha256",json_hash(source_state)},{"removed_frame_sha256",json_hash(removed_state)},
                    {"removed_command_index",quad.command},{"removed_command",draw_row(draw)},
                    {"removed_first_index",draw.first_index},{"removed_indices",std::move(removed_indices)},
                    {"removed_vertex_indices",quad.vertices},{"body",quad_row(mixed,quad)},{"bounds",rectangle(bounds)},
                    {"generated_rgba_sha256",sha256(house.rgba)},
                    {"qualification","pixel-center local UV; binary64 floor to generated source pixel; all 3x3 alpha samples255 inside bound"},
                    {"qualified_pixels",qualified},{"changed_pixels",changed},
                    {"changed_pixels_in_bounds",difference(mi,removed_image,bounds)},
                    {"total_changed_pixels",difference(mi,removed_image)},
                    {"only_house_command_removed",true},{"carrier_payload_exact",true}};
            }
            if(!i) day_legacy=capture_frame(renderer,legacy,output,"neutral-original-default",data["captures"]);
            else render_check(renderer,legacy);
            click(view,session,sample_point(house,source_bounds(house,house_ground,house_width*zoom),true));
            require(picked(view,true,house_id),"Actual generated-alpha house selection failed");
            const auto outlined=compose(view,session);check_typed(outlined,2u);
            const float radius=std::clamp(1.2f*zoom,1.2f,2.4f);
            auto witness=outline_row(outlined,house,house_ground,house_width*zoom,radius);
            if(!i) cached_outline_texture=witness["texture"];
            require(witness["texture"]==cached_outline_texture,"Normal outline cache handle changed across zooms");
            const auto oi=capture_frame(renderer,outlined,output,"neutral-outline-"+std::string(names[i]),data["captures"]);
            const auto changed=difference(mi,oi);require(changed>0u,"Rendered typed outline contribution is vacuous");
            view.clear_building_selection();require(frame_state(compose(view,session))==frame_state(mixed),
                "Clearing typed outline changed normal body");
            data["zoom_cases"].push_back({{"id",names[i]},{"zoom",zoom},{"wheel_delta",wheel[i]},
                {"house",std::move(house_query)},{"resident",std::move(man_query)},
                {"outline",std::move(witness)},{"body_vs_outline_changed_pixels",changed},
                {"cache_handle_equal",true},{"typed_material_draws",2},{"annotations_visible",true}});
            if (!i) {
                require(difference(day_legacy,mi,source_bounds(house,house_ground,house_width*zoom))>0u &&
                    difference(day_legacy,mi,source_bounds(man,man_ground,39.f*zoom))>0u,"Default mapped native difference is vacuous");
                day_mixed=mi;day_legacy_frame=legacy;
            }
        }
        // Return both views to the authored default zoom using the same ordinary
        // wheel rule, not a diagnostic camera setter.
        SDL_Event restore{}; restore.type=SDL_EVENT_MOUSE_WHEEL; restore.wheel.y=(initial_zoom-zoom)/.09f;
        view.handle_event(restore,session); baseline.handle_event(restore,session); zoom=std::clamp(zoom+restore.wheel.y*.09f,.62f,3.f);
        require(close(zoom,initial_zoom),"Ordinary wheel failed to restore zoom");
        load_session(session,view,baseline,output/world_saves[0],true); (void)compose(view,session);
        // Reuse image comparison only after confirming the restored pixel-space
        // geometry is identical, independent of harmless wheel float rounding.
        const auto restored_house=view.tile_screen(26.f,22.f);
        const auto restored_bounds=source_bounds(house,{restored_house.x,restored_house.y+15.f*zoom},house_width*zoom);
        require(close(restored_bounds.w,house_width*initial_zoom),"Restored House scale differs");
        for (const auto index : std::array<std::size_t,2>{1u,2u}) {
            load_session(session,view,baseline,output/world_saves[index],true);
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
            if (index==1u) render_check(renderer,legacy);
            else {
                require(frame_state(legacy)==frame_state(day_legacy_frame),"Authority wet-only fixture changed Legacy frame");
                render_check(renderer,legacy);
            }
            const auto mi=capture_frame(renderer,mixed,output,std::string("neutral-mixed-")+name,data["captures"]);
            require(difference(mi,day_mixed)>0u,"Normal night/wet native control is vacuous");
            data["authority_cases"].push_back({{"id",name},{"weather",weather_row(session.weather())},
                {"hour",projection.environment.time_hour},{"wetness",projection.wetness01},
                {"ambient",mixed.surface_lighting.ambient},{"sun_direction",mixed.surface_lighting.sun_direction},
                {"sun_color_intensity",mixed.surface_lighting.sun_color_intensity},
                {"legacy_vs_mixed_changed_pixels",index==2u?Json(difference(day_legacy,mi)):Json(nullptr)},
                {"mixed_vs_day_changed_pixels",difference(mi,day_mixed)},
                {"legacy_vs_day_changed_pixels",index==2u?Json(0u):Json(nullptr)},
                {"legacy_frame_sha256",json_hash(frame_state(legacy))},
                {"day_legacy_frame_sha256",json_hash(frame_state(day_legacy_frame))},
                {"legacy_native_capture",false},{"legacy_wet_frame_exact",index==2u}});
        }
        load_session(session,view,baseline,output/world_saves[0],true);
        const auto clean=compose(view,session); check_typed(clean,2u);
        ui.show_world(); ui.set_view_state(&session,{},true,1); ui.update(session);
        auto hud=clean; ui.compose(hud); check_prefix(clean,hud);
        require(ui.world_visible() && ui.hud_visible() && ui.element_text_width_for_test("hud-title")>20.f &&
            ui.element_rml_for_test("wood")!="0","Actual normal HUD state/text is vacuous");
        const auto hud_image=capture_frame(renderer,hud,output,"neutral-world-hud",data["captures"]);
        const auto hud_change=difference(day_mixed,hud_image); require(hud_change>1000u,"Real HUD native suffix is vacuous");
        data["ui"]["hud"]=rml_counts(hud,clean.commands.size()); data["ui"]["hud"]["changed_pixels"]=hud_change;
        data["ui"]["hud"]["world_prefix_sha256"]=json_hash(frame_state(clean));
        load_session(session,view,baseline,output/world_saves[5],true);
        const auto two_clean=compose(view,session);check_typed(two_clean,3u);
        const auto foreground_building=std::find_if(session.buildings().begin(),session.buildings().end(),
            [](const Building& building){return building.id==5;});
        require(foreground_building!=session.buildings().end() && foreground_building->kind==BuildingKind::Workshop &&
            foreground_building->x==29 && foreground_building->y==24 && foreground_building->progress==1.f &&
            foreground_building->layer==WorldLayer::Surface && foreground_building->x+foreground_building->y>26+22,
            "Two-house fixture foreground workshop identity/depth changed");
        const auto foreground_visual=resolve_building_visual(foreground_building->kind,1,foreground_building->progress,false);
        const auto foreground_source=source_sprite(originals,original,foreground_visual.asset_id);
        const auto foreground_tile=view.tile_screen(float(foreground_building->x),float(foreground_building->y));
        const Point foreground_ground{foreground_tile.x,foreground_tile.y+15.f*zoom};
        const auto foreground_bounds=source_bounds(foreground_source,foreground_ground,foreground_visual.width*zoom);
        Json selection_occlusion{{"foreground_id",foreground_building->id},{"foreground_key",foreground_source.key},
            {"foreground_depth",foreground_building->x+foreground_building->y},{"first_house_depth",26+22},
            {"foreground_ground",{foreground_ground.x,foreground_ground.y}},{"foreground_bounds",rectangle(foreground_bounds)},
            {"requested_width",foreground_visual.width*zoom},{"crop",foreground_source.crop},{"pivot",foreground_source.pivot},
            {"reference_width",foreground_source.reference_width},
            {"crop_rgba_sha256",sha256(foreground_source.rgba)},
            {"mask_size",{foreground_source.mask_size.width,foreground_source.mask_size.height}},
            {"pooled_alpha_sha256",sha256(foreground_source.pooled_alpha)},
            {"qualification","generated house pooled alpha>=64; competing front workshop pooled alpha<64"}};
        Json selected_points=Json::array(),outlines=Json::array(),bodies=Json::array();
        for(const auto& [id,x,y]:std::array<std::tuple<int,float,float>,2>{{{house_id,26.f,22.f},{4,32.f,25.f}}}) {
            const auto tile=view.tile_screen(x,y);const Point ground{tile.x,tile.y+15.f*zoom};
            const auto bounds=source_bounds(house,ground,house_width*zoom);
            const auto point=sample_point(house,bounds,true,id==house_id?&foreground_source:nullptr,foreground_bounds);
            if(id==house_id) {
                const auto occluded=sample_point(house,bounds,true);
                const auto original_mask=mask_at(foreground_source,foreground_bounds,occluded);
                const auto safe_house=mask_at(house,bounds,point),safe_foreground=mask_at(foreground_source,foreground_bounds,point);
                require(original_mask[2]>=64 && safe_house[2]>=64 && safe_foreground[2]<64,
                    "Two-house source-mask occlusion qualification is vacuous");
                selection_occlusion["occluded_point"]={occluded.x,occluded.y};
                selection_occlusion["occluded_foreground_mask"]=original_mask;
                selection_occlusion["safe_point"]={point.x,point.y};
                selection_occlusion["safe_house_mask"]=safe_house;
                selection_occlusion["safe_foreground_mask"]=safe_foreground;
            }
            SDL_Event shift{};
            // Explicit release before the first click excludes desktop modifier
            // state; the second uses the real additive Shift input path.
            shift.type=id==house_id?SDL_EVENT_KEY_UP:SDL_EVENT_KEY_DOWN;
            shift.key.key=SDLK_LSHIFT;shift.key.mod=id==house_id?SDL_KMOD_NONE:SDL_KMOD_LSHIFT;view.handle_event(shift,session);
            click(view,session,point);
            if(id!=house_id){shift.type=SDL_EVENT_KEY_UP;shift.key.mod=SDL_KMOD_NONE;view.handle_event(shift,session);}
            selected_points.push_back({{"id",id},{"point",{point.x,point.y}},{"ground",{ground.x,ground.y}},
                {"selected_ids_after_click",view.selected_building_ids()}});
        }
        require(view.selected_building_ids()==std::vector<int>{house_id,4},"Actual two mapped house selections failed; selected="+
            Json(view.selected_building_ids()).dump()+"; points="+selected_points.dump()+"; occlusion="+selection_occlusion.dump());
        const auto selected=compose(view,session);check_typed(selected,3u);
        for(const auto& row:selected_points) {
            const auto ground=row.at("ground").get<std::array<float,2>>();const Point point{ground[0],ground[1]};
            auto witness=outline_row(selected,house,point,house_width*zoom,std::clamp(1.2f*zoom,1.2f,2.4f));
            require(witness["texture"]==cached_outline_texture,"Same mapped material allocated another normal outline texture");
            const auto body=find_quad(selected,house,source_bounds(house,point,house_width*zoom),true);
            witness["entity_id"]=row.at("id");outlines.push_back(std::move(witness));bodies.push_back(quad_row(selected,body));
        }
        const auto first_body_command=bodies[0].at("command").get<std::size_t>();
        const auto second_outline_command=outlines[1].at("command").get<std::size_t>();std::size_t intervening_plain{};
        for(std::size_t ci=first_body_command+1u;ci<second_outline_command;++ci)
            if(const auto* draw=std::get_if<GeometryDraw>(&selected.commands.at(ci));draw && !draw->sprite_material_index &&
                draw->pass==Pass::WorldOrdered && draw->index_count>0u) ++intervening_plain;
        require(second_outline_command>first_body_command && intervening_plain>0u &&
            selected.vertices.size()==two_clean.vertices.size()+64u &&
            selected.indices.size()==two_clean.indices.size()+96u,"Two selected outline/body/plain-geometry interleave is vacuous");
        const auto repeated_selected=compose(view,session);
        require(frame_state(repeated_selected)==frame_state(selected),"Repeated two-house normal composition changed frame/cache handle");
        const auto selected_image=capture_frame(renderer,selected,output,"neutral-two-houses-selected",data["captures"]);
        auto removed_outlines=selected;
        std::vector<std::size_t> removed_commands;
        for(const auto& row:outlines) removed_commands.push_back(row.at("command").get<std::size_t>());
        std::sort(removed_commands.rbegin(),removed_commands.rend());
        for(const auto ci:removed_commands) removed_outlines.commands.erase(removed_outlines.commands.begin()+static_cast<std::ptrdiff_t>(ci));
        // Actual second submission exercises the exact same World minus only
        // outline commands. Its structural fingerprint is retained; pixel
        // causality is independently proven by each single-house zoom capture.
        render_check(renderer,removed_outlines);
        data["two_selected"]={{"selected_buildings",view.selected_building_ids()},{"points",selected_points},
            {"selection_occlusion",selection_occlusion},
            {"outlines",outlines},{"bodies",bodies},{"intervening_plain_commands",intervening_plain},
            {"clean_frame_contract",frame_contract(two_clean)},{"selected_frame_contract",frame_contract(selected)},
            {"repeat_frame_exact",true},{"cache_handle_equal",true},{"added_vertices",64},{"added_indices",96},
            {"annotations_visible",true},
            {"outline_removed_frame_sha256",json_hash(frame_state(removed_outlines))},
            {"capture_rgba_sha256",sha256(selected_image.rgba)}};
        load_session(session,view,baseline,output/world_saves[0],true);
        view.clear_building_selection();
        require(frame_state(compose(view,session))==frame_state(clean),"Clearing selection changed annotated normal body frame");
        ui.show_menu(); ui.update(&session); auto interleave=fresh_menu(); ui.compose(interleave);
        render_check(renderer,interleave);const auto interleave_counts=rml_counts(interleave);
        const auto world_after=compose(view,session);
        require(frame_state(world_after)==frame_state(clean),"World->UI->World changed normal material state");
        const auto world_after_image=capture_frame(renderer,world_after,output,"neutral-world-restored",data["captures"]);
        require(world_after_image.rgba==day_mixed.rgba,"World->UI->World pixels changed");
        data["ui"]["interleave"]={{"menu_actual_submission",true},{"menu_pixel_repeat_measured",false},{"menu_roles",interleave_counts},
            {"world_pixels_equal",true},
            {"before_frame_sha256",json_hash(frame_state(clean))},{"after_frame_sha256",json_hash(frame_state(world_after))}};

        load_session(session,view,baseline,output/world_saves[3]); auto variants=compose(view,session); check_typed(variants,1u);
        data["variants"].push_back(legacy_variant(originals,original,view,session,variants,
            "building.house.level2",house_id,26.f,22.f,house_width,zoom));
        for (int stage=1;stage<=3;++stage)
            data["variants"].push_back(legacy_variant(originals,original,view,session,variants,
                "building.house.stage"+std::to_string(stage),3+stage,float(16+stage*3),29.f,house_width,zoom));
        capture_frame(renderer,variants,output,"neutral-variants",data["captures"]);
        load_session(session,view,baseline,output/world_saves[4]); auto winter=compose(view,session); check_typed(winter,1u);
        require(session.winter() && session.calendar()->season==Season::Winter,"Actual winter fixture calendar is not winter");
        data["variants"].push_back(legacy_variant(originals,original,view,session,winter,
            "building.house.winter",house_id,26.f,22.f,house_width,zoom));
        capture_frame(renderer,winter,output,"neutral-winter",data["captures"]);

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
        capture_frame(renderer,last_animation,output,"neutral-animation-fallback",data["captures"]);

        load_session(session,view,baseline,output/world_saves[0]); view.set_layer(WorldLayer::Mine); baseline.set_layer(WorldLayer::Mine);
        const auto mine=compose(view,session),mine_oracle=compose(baseline,session);
        require(!mine.linear_hdr_world && mine.sprite_material_draws.empty() &&
            independently_loaded_legacy_state(mine)==independently_loaded_legacy_state(mine_oracle),
            "Mine did not preserve the explicit original fallback");
        const auto mine_image=capture_frame(renderer,mine,output,"neutral-mine-fallback",data["captures"]);
        const auto mine_legacy_image=capture_frame(renderer,mine_oracle,output,"neutral-mine-legacy",data["captures"]);
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
        capture_frame(renderer,unavailable,output,"neutral-authority-unavailable",data["captures"]);
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
        data["protected_before"]=protected_before;
        data["protected_after"]={{"runtime",tree_rows(runtime)},{"original",tree_rows(original_path)},
            {"collection",tree_rows(collection_path)},{"painted",tree_rows(painted_path)},{"ui",tree_rows(ui_path)}};
        require(data["protected_before"]==data["protected_after"],"Positive native changed accepted input bytes");
        data["output_inventory_excluding_report"]=tree_rows(output);
        data["open_delivery"]={"Full E01-E17/P01 engine acceptance","full normal main startup and package/release",
            "production neutral albedo and all eight authored material masters","reference fidelity",
            "physical material/artist acceptance of provisional flat house palette","Cumulative graph and independent Moon/weather/water FX",
            "full-resolution alpha picking and dilation outline","all resident directions/phases/cargo assets",
            "remote reconnect/recovery_incomplete normal-view route","physical two-PC/co-op",
            "FPS and GPU execution timing","full suite on this current cohort","physical GPU OOM/bad_alloc/plain-outline injection",
            "normal GameView mirrored fallback untriggered with canonical full987 catalog"};
        data["scope"]="Actual normal GameView::compose LegacyCombined provisional XYZ palette house plus unchanged diagnostic painted resident; art/reference/whole-engine acceptance OPEN";
        report["complete"]=true; report["deterministic"]=std::move(data);
        report["measured"]={{"total_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
        save_neutral_report(output/"neutral-material-report.json",report);
        std::cout << "Normal registered material diagnostic complete; engine/production/reference/FPS remain OPEN\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (owned) try {
            report["complete"]=false; report["error"]=error.what();
            preserve_neutral_failure_report(output,report);
        } catch(const std::exception& report_error) {
            std::cerr << "Native failure report could not be preserved: " << report_error.what() << '\n';
        }
        return 1;
    }
}

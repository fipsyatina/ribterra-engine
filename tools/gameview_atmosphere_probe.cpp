#include "engine_control_probe.hpp"
#include <limits>

namespace {
inline std::size_t changed_pixels(const Image& first, const Image& second) {
    std::size_t result{};
    for (std::size_t pixel = 0; pixel < first.rgba.size(); pixel += 4u)
        if (!std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(pixel),
                        first.rgba.begin() + static_cast<std::ptrdiff_t>(pixel + 4u),
                        second.rgba.begin() + static_cast<std::ptrdiff_t>(pixel))) ++result;
    return result;
}
inline Json atmosphere_tuple(const AtmosphereGpu& payload) {
    return {{"origin_time",payload.origin_time},{"depth",payload.depth},{"density",payload.density},
        {"far_color_noise",payload.far_color_noise},{"morning_color",payload.morning_color},
        {"coastal_color",payload.coastal_color},{"wind_shore",payload.wind_shore},
        {"counts_version",payload.counts_version},{"water_aabbs",payload.water_aabbs}};
}
inline void verify_patches(const ClientSession& session, const AtmosphereGpu& payload) {
    for (std::size_t patch = 0; patch < payload.counts_version[0]; ++patch) {
        const auto& bounds = payload.water_aabbs[patch];
        for (int y = static_cast<int>(bounds[1] + .5f); y < static_cast<int>(bounds[3] + .5f); ++y)
            for (int x = static_cast<int>(bounds[0] + .5f); x < static_cast<int>(bounds[2] + .5f); ++x)
                require(session.tile(x,y).explored && session.tile(x,y).terrain == Terrain::Water,
                        "Atmosphere rectangle spans land or unknown water");
    }
}
inline void verify_ui(const Image& first, const Image& second) {
    for (std::size_t y = 14; y < 50; ++y) for (std::size_t x = 14; x < 90; ++x) {
        const auto pixel = (y * extent.width + x) * 4u;
        require(std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(pixel),
            first.rgba.begin() + static_cast<std::ptrdiff_t>(pixel + 4u),
            second.rgba.begin() + static_cast<std::ptrdiff_t>(pixel)),"Atmosphere altered the opaque UI");
    }
}
inline void create_patch_fixture(const fs::path& file, int patch_count) {
    World world(70421,4);
    auto state = world.export_state();
    for (auto& chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            const int x = chunk.key.cx * chunk_size + static_cast<int>(index % chunk_size);
            const int y = chunk.key.cy * chunk_size + static_cast<int>(index / chunk_size);
            // Flat diagnostic window plus the complete one-cell normal halo.
            // Keep steep procedural neighbors out of this capacity-only test.
            if (x < 14 || x > 42 || y < 11 || y > 39) continue;
            const int column = (x - 20) / 2, row = (y - 18) / 2;
            const bool patch = x >= 20 && (x-20)%2 == 0 && column < 11 &&
                y >= 18 && (y-18)%2 == 0 && row < 3 && row*11 + column < patch_count;
            chunk.tiles[index] = patch ? Tile{Terrain::Water,0,true,Biome::Lake}
                                      : Tile{Terrain::Grass,0,true,Biome::Meadow,0};
        }
    }
    ++state.topology_revision;
    std::string error;
    require(world.import_state(std::move(state),error),error);
    require(!fs::exists(file) && world.save(file,error),error);
}
struct Panel {
    std::array<float,3> center;
};
inline Frame2D panels(const Frame2D& source, std::span<const Panel> controls) {
    auto frame = source;
    frame.vertices.clear();frame.surface_vertices.clear();frame.indices.clear();frame.commands.clear();
    frame.sprite_material_draws.clear();frame.reflection_draws.clear();frame.local_occluders.clear();
    frame.sky_shadow_triangles.clear();frame.contact_shadows.clear();
    frame.surface_lighting = {};frame.surface_lighting.ambient = {.12f,.12f,.12f,0.f};
    frame.moon_lighting = MoonLightingGpu{};
    frame.ground_material_draws.resize(1u);
    frame.contact_projection = ContactProjection{{960.f,540.f},1.3f};
    constexpr std::array<Point,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    constexpr std::array<Point,4> offsets{{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
    for (const auto& control : controls) {
        const auto first = static_cast<std::uint32_t>(frame.vertices.size());
        const auto index = static_cast<std::uint32_t>(frame.indices.size());
        for (std::size_t corner = 0; corner < 4u; ++corner) {
            const std::array<float,3> position{control.center[0]+offsets[corner].x,
                control.center[1]+offsets[corner].y,control.center[2]};
            const auto screen = contact_project_logical(position,*frame.contact_projection);
            frame.vertices.push_back({{screen[0],screen[1]},uv[corner],{255,255,255,255}});
            frame.surface_vertices.push_back({position,{0.f,0.f,1.f}});
        }
        for (const auto offset : std::array<std::uint32_t,6>{0,1,2,0,2,3}) frame.indices.push_back(first+offset);
        GeometryDraw draw;draw.pass=Pass::Terrain;draw.blend=Blend::Opaque;
        draw.first_index=index;draw.index_count=6u;draw.ground_material_index=1u;
        frame.commands.emplace_back(draw);
    }
    return frame;
}
inline double center_change(const Image& first, const Image& second, const Frame2D& frame, const Panel& panel) {
    const auto point=contact_project_logical(panel.center,*frame.contact_projection);
    const int center_x=static_cast<int>(std::floor(point[0])),center_y=static_cast<int>(std::floor(point[1]));
    double change{};
    for (int y=center_y-2;y<=center_y+2;++y) for (int x=center_x-2;x<=center_x+2;++x) {
        require(x>=0&&x<1920&&y>=0&&y<1080,"Causal panel is outside viewport");
        const auto pixel=(static_cast<std::size_t>(y)*extent.width+static_cast<std::size_t>(x))*4u;
        for (std::size_t channel=0;channel<3u;++channel)
            change+=std::abs(static_cast<int>(first.rgba[pixel+channel])-static_cast<int>(second.rgba[pixel+channel]));
    }
    return change/75.;
}
} // namespace

int main(int argc,char** argv) {
    fs::path output;
    bool owned{};
    Json captures=Json::array();
    try {
        require(argc==4,"Usage: GameViewWaterProbe runtime-assets diagnostic-kit new-output (atmosphere probe)");
        const auto assets=fs::absolute(argument(argv[1])),kit=fs::absolute(argument(argv[2]));
        output=fs::absolute(argument(argv[3]));
        require(output.parent_path()==assets.parent_path().parent_path(),"Output must be sibling build child");
        require(!fs::exists(output),"Native output already exists");
        require(fs::space(output.parent_path()).available>=384u*1024u*1024u,"Insufficient disk headroom");
        require(fs::create_directory(output),"Could not create native output");owned=true;
        create_fixture(output/"water-world-current.save");
        create_patch_fixture(output/"atmosphere-32-current.save",32);
        create_patch_fixture(output/"atmosphere-33-current.save",33);
        require(SDL_Init(SDL_INIT_VIDEO),"SDL initialization failed");
        struct Quit { ~Quit(){SDL_Quit();} } quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Atmosphere Engine Control - not final art",1920,1080,0),SDL_DestroyWindow);
        require(window&&SDL_ShowWindow(window.get())&&SDL_SyncWindow(window.get()),"Native window failed");
        ClientSession session(70421);std::string error;
        require(session.load(output/"water-world-current.save",error)&&!session.last_load().copied,error);
        EngineControlSettings settings;settings.camera_x=28.f;settings.camera_y=25.f;
        settings.cell_radius=8;settings.water_enabled=true;
        settings.lighting.ambient={.16f,.2f,.24f,0.f};
        settings.lighting.sun_direction={.70710677f,0.f,.70710677f,0.f};
        settings.lighting.sun_color_intensity={1.f,.86f,.62f,.8f};
        settings.moon.direction={-.70710677f,0.f,.70710677f,0.f};
        settings.moon.color_intensity={.55f,.68f,1.f,0.f};
        const std::array<EngineControlInstance,2> controls{{
            {"control.house.sprite",26.f,22.f,1.f,101u,101u,0.f,true,1001u},
            {"control.resident.empty.sprite",26.f,24.f,1.f,102u,111u,0.f,false,0u}}};
        Image old_image;WaterCoverageReadback old_carrier;
        {
            RendererOptions old_options;old_options.world_graph=WorldGraph::WaterReflectionSlice;
            old_options.collect_telemetry=true;
            Renderer2D old_renderer(window.get(),assets,old_options);old_renderer.set_logical_size(extent);
            GameView old_view(old_renderer);old_view.set_logical_size(extent);
            old_view.load_engine_control_art(AssetCatalog(kit));old_view.load_engine_water_art();
            Frame2D old_frame;(void)old_view.compose_engine_control(session,10.,settings,controls,old_frame);
            old_image=capture(old_renderer,old_frame,output,"atmosphere-old-water",captures);
            old_carrier=old_renderer.read_water_coverage_v1();
            save_bytes(output/"atmosphere-old-water.rgba8",old_carrier.rgba8);
        }
        RendererOptions options;options.world_graph=WorldGraph::WaterAtmosphereSlice;options.collect_telemetry=true;
        Renderer2D renderer(window.get(),assets,options);renderer.set_logical_size(extent);
        require(!renderer.device_info().software,"Hardware D3D12 is required");
        GameView view(renderer);view.set_logical_size(extent);
        view.load_engine_control_art(AssetCatalog(kit));view.load_engine_water_art();
        settings.atmosphere=EngineAtmosphereSettings{false,6.f,.25f};
        Frame2D disabled;const auto stats=view.compose_engine_control(session,10.,settings,controls,disabled);
        require(stats.atmosphere_water_patches==0u,"Disabled atmosphere allocated coastal regions");
        verify_patches(session,*disabled.atmosphere);
        const auto off=capture(renderer,disabled,output,"atmosphere-disabled",captures);
        require(off.rgba==old_image.rgba,"Disabled atmosphere changed the old water graph pixels");
        const auto new_carrier=renderer.read_water_coverage_v1();
        require(new_carrier.size.width==old_carrier.size.width&&new_carrier.size.height==old_carrier.size.height&&
                new_carrier.gpu_row_pitch==old_carrier.gpu_row_pitch&&new_carrier.rgba8==old_carrier.rgba8,
                "Disabled atmosphere changed raw water carrier dimensions/pitch/bytes");
        save_bytes(output/"atmosphere-disabled.rgba8",new_carrier.rgba8);
        settings.atmosphere->enabled=true;
        Frame2D morning;const auto morning_stats=view.compose_engine_control(session,10.,settings,controls,morning);
        require(morning_stats.atmosphere_water_patches>0u&&morning_stats.atmosphere_water_patches<32u,
                "Enabled water patch fixture is vacuous");
        verify_patches(session,*morning.atmosphere);
        const auto take=[&](const Frame2D& frame,std::string_view name) {
            const auto result=capture(renderer,frame,output,name,captures);
            require(renderer.read_water_coverage_v1().rgba8==old_carrier.rgba8,"Atmosphere changed carrier/deck/shore/reveal bytes");
            verify_ui(off,result);
            return result;
        };
        const auto dawn=take(morning,"atmosphere-morning");
        require(changed_pixels(off,dawn)>500u,"Native atmosphere is vacuous");
        const auto repeated=take(morning,"atmosphere-paused-repeat");
        require(dawn.rgba==repeated.rgba,"Frozen shown clock is not exact");
        Frame2D advanced;(void)view.compose_engine_control(session,11.,settings,controls,advanced);
        require(advanced.atmosphere->origin_time[2]==11.f,"GameView did not project advanced shown clock");
        advanced.water_surface->world_origin_time[2]=10.f; // Isolate atmosphere drift, not water phase.
        const auto drift=take(advanced,"atmosphere-drift-next");
        require(changed_pixels(dawn,drift)>10u,"World-anchored shown-clock noise drift is vacuous");
        Json time_cases=Json::array();
        for (const float hour:{12.f,22.f}) {
            auto input=settings;input.atmosphere->time_hour=hour;
            Frame2D frame;(void)view.compose_engine_control(session,10.,input,controls,frame);
            const auto image=take(frame,hour==12.f?"atmosphere-noon":"atmosphere-night");
            const auto changes=changed_pixels(dawn,image);
            require(changes>100u&&frame.atmosphere->origin_time[3]==0.f,"Diagnostic time projection is vacuous");
            time_cases.push_back({{"hour",hour},{"weather_haze01",input.atmosphere->weather_haze01},
                {"changed_pixels_from_morning",changes},{"gpu",atmosphere_tuple(*frame.atmosphere)}});
        }
        auto clear_input=settings;clear_input.atmosphere->time_hour=12.f;clear_input.atmosphere->weather_haze01=0.f;
        Frame2D clear;(void)view.compose_engine_control(session,10.,clear_input,controls,clear);
        const auto clear_image=take(clear,"atmosphere-weather-clear");
        auto haze_input=clear_input;haze_input.atmosphere->weather_haze01=1.f;
        Frame2D haze;(void)view.compose_engine_control(session,10.,haze_input,controls,haze);
        const auto haze_image=take(haze,"atmosphere-weather-haze");
        require(changed_pixels(clear_image,haze_image)>100u,"Diagnostic weather projection is vacuous");
        auto no_reflection=morning;no_reflection.reflection_draws.clear();
        const auto reflection_off=take(no_reflection,"atmosphere-reflection-off");
        const auto reflected=water_only_difference(dawn,reflection_off,morning);
        require(reflected>25u,"Source-lit reflection disappeared under atmosphere");
        auto hidden=morning;hidden.sprite_material_draws.front().reveal=0.f;
        require(hidden.sprite_material_draws.front().emission_scale>0.f,"Hidden-emission control is vacuous");
        hidden.reflection_draws.clear();
        const auto hidden_on=take(hidden,"atmosphere-hidden-source-on");
        hidden.sprite_material_draws.front().emission_scale=0.f;
        const auto hidden_off=take(hidden,"atmosphere-hidden-source-off");
        require(hidden_on.rgba==hidden_off.rgba,"Hidden sprite emission leaked under atmosphere");
        const std::array<EngineControlInstance,3> frontier_controls{{controls[0],controls[1],
            {"unsupported-hidden-control",32.f,32.f,1.f,0u,0u,0.f,true,0u}}};
        Frame2D frontier;const auto frontier_stats=view.compose_engine_control(session,10.,settings,frontier_controls,frontier);
        require(frontier_stats.instances_hidden==1u&&frontier_stats.instances_drawn==2u,
                "Unknown-world source control is not actually hidden");
        const auto frontier_image=take(frontier,"atmosphere-unknown-source");
        require(frontier_image.rgba==dawn.rgba&&frontier.atmosphere->water_aabbs==morning.atmosphere->water_aabbs,
                "Unknown source affected the scene or revealed-water regions");
        std::size_t camera_cases{};
        for (const float zoom:{.62f,1.3f,3.f}) {
            auto input=settings;input.zoom=zoom;input.camera_x+=.25f;input.camera_y-=.125f;
            Frame2D frame;const auto current=view.compose_engine_control(session,10.,input,controls,frame);
            require(current.water_cells>10u&&current.reflection_draws>0u,"Camera control became vacuous");
            verify_patches(session,*frame.atmosphere);
            require(frame.atmosphere->origin_time[0]==input.camera_x&&frame.atmosphere->origin_time[1]==input.camera_y&&
                    frame.atmosphere->origin_time[2]==10.f,"Absolute origin/shown clock was not preserved");
            (void)capture(renderer,frame,output,"atmosphere-camera-"+std::to_string(camera_cases),captures);
            ++camera_cases;
        }
        // Small actual projected terrain controls, equal material/height where
        // appropriate. Only field under test changes; no screen-space veil.
        const std::array<Panel,3> depth_panels{{{{3.f,3.f,0.f}},{{0.f,0.f,0.f}},{{-3.f,-3.f,0.f}}}};
        auto depth_frame=panels(disabled,depth_panels);
        depth_frame.atmosphere->counts_version[2]=0u;
        const auto depth_off=capture(renderer,depth_frame,output,"atmosphere-depth-off",captures);
        depth_frame.atmosphere->counts_version[2]=1u;depth_frame.atmosphere->depth={-6.f,6.f,.4f,.2f};
        depth_frame.atmosphere->density[0]=0.f;depth_frame.atmosphere->density[1]=0.f;
        depth_frame.atmosphere->far_color_noise={1.f,1.f,1.f,0.f};
        const auto depth_on=capture(renderer,depth_frame,output,"atmosphere-depth-on",captures);
        std::array<double,3> depth_changes{};
        for(std::size_t index=0;index<3u;++index)depth_changes[index]=center_change(depth_off,depth_on,depth_frame,depth_panels[index]);
        require(depth_changes[0]<depth_changes[1]&&depth_changes[1]<depth_changes[2],"View-depth haze is not monotonic");
        const std::array<Panel,2> height_panels{{{{-2.f,2.f,0.f}},{{2.f,-2.f,3.f}}}};
        auto height_frame=panels(disabled,height_panels);height_frame.atmosphere->counts_version[2]=0u;
        const auto height_off=capture(renderer,height_frame,output,"atmosphere-height-off",captures);
        height_frame.atmosphere->counts_version[2]=1u;height_frame.atmosphere->origin_time[3]=1.f;
        height_frame.atmosphere->depth={16.f,24.f,.5f,.2f};height_frame.atmosphere->density={1.f,0.f,0.f,1.25f};
        height_frame.atmosphere->far_color_noise[3]=0.f;height_frame.atmosphere->morning_color={1.f,1.f,1.f,0.f};
        const auto height_on=capture(renderer,height_frame,output,"atmosphere-height-on",captures);
        const double low=center_change(height_off,height_on,height_frame,height_panels[0]);
        const double high=center_change(height_off,height_on,height_frame,height_panels[1]);
        require(low>high&&high>=0.&&low>3.,"Low-height morning haze is not stronger than roof height");
        const std::array<Panel,3> coast_panels{{{{-4.f,4.f,0.f}},{{0.f,0.f,0.f}},{{4.f,-4.f,0.f}}}};
        auto coast_frame=panels(disabled,coast_panels);coast_frame.atmosphere->counts_version[2]=0u;
        const auto coast_off=capture(renderer,coast_frame,output,"atmosphere-coast-off",captures);
        auto& coastal=*coast_frame.atmosphere;coastal.counts_version={1u,1u,1u,0u};
        coastal.water_aabbs={};coastal.water_aabbs[0]={23.5f,28.5f,24.5f,29.5f};
        coastal.depth={16.f,24.f,.5f,.2f};coastal.density={0.f,1.f,0.f,1.25f};
        coastal.wind_shore[3]=8.f;coastal.far_color_noise[3]=0.f;coastal.coastal_color={1.f,1.f,1.f,0.f};
        const auto coast_on=capture(renderer,coast_frame,output,"atmosphere-coast-on",captures);
        std::array<double,3> coast_changes{};
        for(std::size_t index=0;index<3u;++index)coast_changes[index]=center_change(coast_off,coast_on,coast_frame,coast_panels[index]);
        require(coast_changes[0]>coast_changes[1]&&coast_changes[1]>coast_changes[2],"Coastal distance gate is not monotonic");
        ClientSession patches32(70421),patches33(70421);
        require(patches32.load(output/"atmosphere-32-current.save",error),error);
        require(patches33.load(output/"atmosphere-33-current.save",error),error);
        auto capacity_input=settings;capacity_input.cell_radius=12;
        Frame2D saturated;const auto saturated_stats=view.compose_engine_control(patches32,10.,capacity_input,{},saturated);
        require(saturated_stats.atmosphere_water_patches==32u,"32-patch capacity test is vacuous");
        verify_patches(patches32,*saturated.atmosphere);
        const auto saturated_image=capture(renderer,saturated,output,"atmosphere-max-32-patches",captures);
        const auto before=saturated.vertices;const auto before_patches=saturated.atmosphere->water_aabbs;
        const auto before_pick=view.cell_at_screen(960.f,540.f);
        const auto before_screen=view.tile_screen(28.f,25.f);
        bool overflow{};
        try{(void)view.compose_engine_control(patches33,10.,capacity_input,{},saturated);}
        catch(const std::invalid_argument&){overflow=true;}
        require(overflow&&saturated.vertices.size()==before.size()&&saturated.atmosphere->water_aabbs==before_patches&&
            view.cell_at_screen(960.f,540.f)==before_pick&&view.tile_screen(28.f,25.f).x==before_screen.x&&
            view.tile_screen(28.f,25.f).y==before_screen.y,"33rd patch did not reject frame/picks/camera transactionally");
        const auto after_overflow=capture(renderer,saturated,output,"atmosphere-after-overflow",captures);
        require(after_overflow.rgba==saturated_image.rgba,"33rd patch damaged the previously published frame");
        auto disabled_capacity=capacity_input;disabled_capacity.atmosphere->enabled=false;
        Frame2D capacity_off;const auto capacity_off_stats=view.compose_engine_control(patches33,10.,disabled_capacity,{},capacity_off);
        require(capacity_off_stats.atmosphere_water_patches==0u&&capacity_off_stats.water_cells==33u,
                "Disabled atmosphere wrongly applied the active coastal capacity");
        (void)capture(renderer,capacity_off,output,"atmosphere-disabled-33-patches",captures);
        std::size_t rejected{};
        for(int test=0;test<7;++test) {
            auto bad=morning;
            switch(test) {
            case 0:bad.atmosphere.reset();break;
            case 1:bad.atmosphere->depth[3]=.21f;break;
            case 2:bad.atmosphere->origin_time[2]=std::numeric_limits<float>::quiet_NaN();break;
            case 3:bad.atmosphere->counts_version[0]=33u;break;
            case 4:bad.atmosphere->counts_version[3]=1u;break;
            case 5:bad.atmosphere->water_aabbs[bad.atmosphere->counts_version[0]]={1.f,1.f,2.f,2.f};break;
            case 6:bad.atmosphere->wind_shore[0]=2.f;break;
            }
            bool refused{};try{renderer.render(bad);}catch(const std::invalid_argument&){refused=true;}
            require(refused,"Invalid atmosphere input reached GPU rendering");++rejected;
        }
        const auto recovered=take(morning,"atmosphere-after-rejects");
        require(recovered.rgba==dawn.rgba,"Invalid atmosphere inputs damaged the next valid frame");
        const auto& device=renderer.device_info();const auto& target=renderer.ground_forward_status();
        save_report(output/"atmosphere-native-result.json",{{"complete",true},{"scope","Diagnostic AnalyticAtmosphere V1, not full E10/engine/game acceptance"},
            {"backend",device.backend},{"gpu",device.device},{"driver",device.driver},
            {"resolution",{1920,1080}},{"graph","WaterAtmosphereSlice"},{"captures",captures},
            {"disabled_exact_old_graph",true},{"carrier_exact_for_same_camera",true},{"opaque_ui_exact",true},
            {"raw_carriers",{{"old",carrier_counts(old_carrier)},{"new_disabled",carrier_counts(new_carrier)}}},
            {"paused_exact",true},{"drift_changed_pixels",changed_pixels(dawn,drift)},
            {"morning_changed_pixels",changed_pixels(off,dawn)},{"time_cases",time_cases},
            {"morning_input",{{"time_hour",6.f},{"weather_haze01",.25f},{"gpu",atmosphere_tuple(*morning.atmosphere)}}},
            {"weather_changed_pixels",changed_pixels(clear_image,haze_image)},{"reflection_pixels",reflected},
            {"hidden_surface_emission_exact",true},{"unknown_source_non_submission_exact",true},{"camera_cases",camera_cases},
            {"depth_changes_near_mid_far",depth_changes},{"height_changes_low_high",{low,high}},
            {"coastal_changes_inside_near_outside",coast_changes},{"water_patch_count",morning_stats.atmosphere_water_patches},
            {"saturated_patch_count",saturated_stats.atmosphere_water_patches},{"overflow_rejected",overflow},
            {"invalid_inputs_rejected",rejected},{"targets",stable_targets(target)},
            {"atmosphere_bytes",sizeof(AtmosphereGpu)},{"lighting_wrapper_bytes",sizeof(SurfaceLightingAtmosphereGpu)},
            {"fps",nullptr},{"gpu_execution_ms",nullptr},{"full_engine","OPEN"},{"E10","OPEN"},
            {"limitations",{"Diagnostic time/weather only; host authority not integrated","No video/DPI/input/performance evidence",
                "Analytic path replaces proposed fog quads/cache; old cache rubric not tested","Binary WaterCoverage only, AA OPEN",
                "Technical controls are not final concept assets"}}});
        check_budget(output);std::cout<<"GameView atmosphere native checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        if(owned&&!fs::exists(output/"atmosphere-native-result.json")) {
            try{save_report(output/"atmosphere-native-result.json",{{"complete",false},{"error",error.what()},{"captures",captures}});}
            catch(const std::exception& reporting){std::cerr<<reporting.what()<<'\n';}
        }
        return 1;
    }
}

#include "engine_control_probe.hpp"
#include <cstring>
#include <limits>

namespace {
inline std::size_t changed(const Image& first, const Image& second) {
    require(first.rgba.size()==second.rgba.size(),"Image sizes differ");
    std::size_t count{};
    for (std::size_t pixel=0;pixel<first.rgba.size();pixel+=4u)
        if (!std::equal(first.rgba.begin()+static_cast<std::ptrdiff_t>(pixel),
            first.rgba.begin()+static_cast<std::ptrdiff_t>(pixel+4u),
            second.rgba.begin()+static_cast<std::ptrdiff_t>(pixel))) ++count;
    return count;
}
inline void ui_exact(const Image& first,const Image& second) {
    for (std::size_t y=14;y<50;++y) for (std::size_t x=14;x<90;++x) {
        const auto pixel=(y*extent.width+x)*4u;
        require(std::equal(first.rgba.begin()+static_cast<std::ptrdiff_t>(pixel),
            first.rgba.begin()+static_cast<std::ptrdiff_t>(pixel+4u),
            second.rgba.begin()+static_cast<std::ptrdiff_t>(pixel)),"Environment changed opaque UI");
    }
}
inline Json sky_tuple(const Frame2D& frame) {
    return {{"ambient",frame.surface_lighting.ambient},
        {"sun_direction",frame.surface_lighting.sun_direction},
        {"sun_color_intensity",frame.surface_lighting.sun_color_intensity},
        {"moon_direction",frame.moon_lighting->direction},
        {"moon_color_intensity",frame.moon_lighting->color_intensity},
        {"atmosphere_origin_time",frame.atmosphere->origin_time},
        {"atmosphere_depth",frame.atmosphere->depth},
        {"atmosphere_counts_version",frame.atmosphere->counts_version}};
}
inline void same_locals(const Frame2D& first,const Frame2D& second) {
    require(first.surface_lighting.counts==second.surface_lighting.counts &&
        std::memcmp(first.surface_lighting.lights.data(),second.surface_lighting.lights.data(),
                    sizeof(first.surface_lighting.lights))==0,"Environment changed local-light rows");
    require(first.sprite_material_draws.size()==second.sprite_material_draws.size(),"Sprite count changed");
    for (std::size_t index=0;index<first.sprite_material_draws.size();++index)
        require(first.sprite_material_draws[index].emission_scale==second.sprite_material_draws[index].emission_scale &&
            first.sprite_material_draws[index].surface.emission==second.sprite_material_draws[index].surface.emission,
            "Environment changed source emission metadata");
}
struct ShadowMask { std::size_t pixels{};double x{},y{}; };
inline ShadowMask shadow_mask(const Image& on,const Image& off,const WaterCoverageReadback& carrier) {
    ShadowMask result;
    for (std::size_t pixel=0;pixel<on.rgba.size();pixel+=4u) {
        if (carrier.rgba8[pixel+1u]!=1u) continue; // Exact land carrier only.
        int delta{};
        for (std::size_t channel=0;channel<3u;++channel)
            delta+=static_cast<int>(off.rgba[pixel+channel])-static_cast<int>(on.rgba[pixel+channel]);
        if (delta<3) continue;
        ++result.pixels;const auto index=pixel/4u;
        result.x+=static_cast<double>(index%extent.width);result.y+=static_cast<double>(index/extent.width);
    }
    require(result.pixels>10u,"Directional shadow difference mask is empty");
    result.x/=static_cast<double>(result.pixels);result.y/=static_cast<double>(result.pixels);
    return result;
}
inline double land_energy(const Image& image,const WaterCoverageReadback& carrier) {
    double total{};std::size_t count{};
    for (std::size_t pixel=0;pixel<image.rgba.size();pixel+=4u) {
        if (carrier.rgba8[pixel+1u]!=1u) continue;
        for (std::size_t channel=0;channel<3u;++channel) total+=image.rgba[pixel+channel];
        ++count;
    }
    require(count>0u,"Land energy measurement is empty");return total/(3.*static_cast<double>(count));
}
inline void cpu_checks() {
    SurfaceLightingFrame local;
    local.counts[0]=1u;local.lights[0].occluder_range_flags[3]=9001u;
    EngineEnvironmentSettingsV1 input;
    input.time_hour=std::nextafter(24.f,0.f);
    const auto end=project_engine_environment(input,local);input.time_hour=0.f;
    const auto start=project_engine_environment(input,local);
    for (std::size_t channel=0;channel<4u;++channel) {
        require(std::abs(end.lighting.sun_direction[channel]-start.lighting.sun_direction[channel])<1.e-5f &&
            std::abs(end.moon.direction[channel]-start.moon.direction[channel])<1.e-5f &&
            std::abs(end.lighting.ambient[channel]-start.lighting.ambient[channel])<1.e-5f &&
            std::abs(end.lighting.sun_color_intensity[channel]-start.lighting.sun_color_intensity[channel])<1.e-5f &&
            std::abs(end.moon.color_intensity[channel]-start.moon.color_intensity[channel])<1.e-5f,
            "Environment V1 midnight wrap is discontinuous");
    }
    for (float hour:std::array<float,6>{5.9f,6.f,6.1f,17.9f,18.f,18.1f}) {
        input.time_hour=hour;const auto projected=project_engine_environment(input,local);
        require(projected.lighting.counts==local.counts &&
            std::memcmp(projected.lighting.lights.data(),local.lights.data(),sizeof(local.lights))==0,
            "Pure environment projection changed incoming locals");
        if (hour==6.f||hour==18.f)
            require(projected.lighting.sun_color_intensity[3]==0.f&&projected.moon.color_intensity[3]==0.f,
                    "Horizon float residue activated a source");
    }
    for (int index=0;index<96;++index) {
        input.time_hour=static_cast<float>(index)*.25f;
        const auto projected=project_engine_environment(input,local);
        const auto sun_z=projected.lighting.sun_direction[2],moon_z=projected.moon.direction[2];
        require(sun_z>=.342f&&moon_z>=.342f,"Environment elevation fell below supported shadow floor");
        require(projected.lighting.sun_color_intensity[3]==0.f||projected.moon.color_intensity[3]==0.f,
                "Opposing V1 sky sources overlap unexpectedly");
    }
}
} // namespace

int main(int argc,char** argv) {
    fs::path output;bool owned{};Json captures=Json::array();
    try {
        require(argc==4,"Usage: GameViewWaterProbe runtime-assets kit fresh-output (environment probe)");
        const auto assets=fs::absolute(argument(argv[1])),kit=fs::absolute(argument(argv[2]));
        output=fs::absolute(argument(argv[3]));
        require(output.parent_path()==assets.parent_path().parent_path()&&!fs::exists(output),
                "Environment output must be a new sibling build child");
        require(fs::space(output.parent_path()).available>=384u*1024u*1024u,"Insufficient headroom");
        require(fs::create_directory(output),"Could not create environment output");owned=true;
        cpu_checks();create_fixture(output/"environment-world-current.save");
        require(SDL_Init(SDL_INIT_VIDEO),"SDL initialization failed");
        struct Quit { ~Quit(){SDL_Quit();} } quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Environment Engine Control - not final art",1920,1080,0),SDL_DestroyWindow);
        require(window&&SDL_ShowWindow(window.get())&&SDL_SyncWindow(window.get()),"Native window failed");
        RendererOptions options;options.world_graph=WorldGraph::WaterAtmosphereSlice;options.collect_telemetry=true;
        Renderer2D renderer(window.get(),assets,options);renderer.set_logical_size(extent);
        require(!renderer.device_info().software,"Hardware D3D12 is required");
        GameView view(renderer);view.set_logical_size(extent);
        view.load_engine_control_art(AssetCatalog(kit));view.load_engine_water_art();
        ClientSession session(70421);std::string error;
        require(session.load(output/"environment-world-current.save",error)&&!session.last_load().copied,error);
        EngineControlSettings settings;settings.camera_x=28.f;settings.camera_y=25.f;
        settings.cell_radius=8;settings.water_enabled=true;settings.environment=EngineEnvironmentSettingsV1{};
        // Active zero-radiance external row tests byte preservation through real
        // composition without contaminating the sky-only difference masks.
        settings.lighting.counts[0]=1u;settings.lighting.lights[0].color_intensity[3]=0.f;
        settings.lighting.lights[0].occluder_range_flags[3]=9001u;
        const std::array<EngineControlInstance,2> controls{{
            {"control.house.sprite",26.f,22.f,1.f,101u,101u,0.f,true,1001u},
            {"control.resident.empty.sprite",26.f,24.f,1.f,102u,111u,0.f,false,0u}}};
        Frame2D noon;(void)view.compose_engine_control(session,10.,settings,controls,noon);
        const auto noon_image=capture(renderer,noon,output,"environment-noon",captures);
        const auto carrier=renderer.read_water_coverage_v1();
        save_bytes(output/"environment-carrier.rgba8",carrier.rgba8);const auto counts=carrier_counts(carrier);
        const auto checked_capture=[&](const Frame2D& frame,std::string_view name) {
            const auto image=capture(renderer,frame,output,name,captures);
            ui_exact(noon_image,image);
            require(renderer.read_water_coverage_v1().rgba8==carrier.rgba8,"Environment changed raw water carrier");
            return image;
        };
        const auto compose=[&](float hour,float cloud,float haze,bool fog) {
            auto configured=settings;configured.environment=EngineEnvironmentSettingsV1{1u,hour,cloud,haze,fog};
            Frame2D frame;(void)view.compose_engine_control(session,10.,configured,controls,frame);
            require(frame.atmosphere&&frame.moon_lighting,"Resolved environment missing GPU tuple");
            require(frame.atmosphere->counts_version[2]==static_cast<std::uint32_t>(fog) &&
                (fog||frame.atmosphere->counts_version[0]==0u),"Atmosphere enable flag/capacity disagrees with input");
            require(frame.surface_lighting.lights[0].occluder_range_flags==settings.lighting.lights[0].occluder_range_flags &&
                std::memcmp(&frame.surface_lighting.lights[0],&settings.lighting.lights[0],sizeof(LightGpu))==0,
                "Incoming active local row was overwritten");
            same_locals(noon,frame);return frame;
        };
        const auto paused=checked_capture(noon,"environment-paused-repeat");
        require(paused.rgba==noon_image.rgba,"Paused environment is not exact");
        const auto resolved=project_engine_environment(*settings.environment,settings.lighting);
        auto manual=settings;manual.environment.reset();manual.lighting=resolved.lighting;manual.moon=resolved.moon;
        manual.atmosphere=EngineAtmosphereSettings{true,12.f,0.f};
        Frame2D manual_frame;(void)view.compose_engine_control(session,10.,manual,controls,manual_frame);
        require(checked_capture(manual_frame,"environment-manual-equivalent").rgba==noon_image.rgba,
                "Resolved environment differs from equivalent manual path");
        Json time_cases=Json::array();
        for (const float hour:std::array<float,7>{0.f,6.f,9.f,15.f,18.f,21.f,3.f}) {
            const auto frame=compose(hour,0.f,0.f,true);
            const auto image=checked_capture(frame,"environment-hour-"+std::to_string(static_cast<int>(hour)));
            require(changed(noon_image,image)>100u,"Time did not change native lighting");
            time_cases.push_back({{"hour",hour},{"gpu",sky_tuple(frame)},
                {"changed_pixels_from_noon",changed(noon_image,image)}});
        }
        auto cloudy=compose(12.f,1.f,0.f,true);
        const auto cloud_image=checked_capture(cloudy,"environment-cloudy-noon");
        const auto cloud_pixels=changed(noon_image,cloud_image);
        require(cloud_pixels>100u&&cloudy.surface_lighting.sun_color_intensity[3]<noon.surface_lighting.sun_color_intensity[3],
                "Cloud coverage did not weaken sunlight");
        for (std::size_t channel=0;channel<3u;++channel)
            require(cloudy.surface_lighting.ambient[channel]<noon.surface_lighting.ambient[channel],
                    "Cloud coverage did not weaken daytime ambient");
        const auto clear_haze=compose(12.f,.5f,0.f,true),dense_haze=compose(12.f,.5f,1.f,true);
        require(std::memcmp(&clear_haze.surface_lighting,&dense_haze.surface_lighting,sizeof(SurfaceLightingFrame))==0 &&
            std::memcmp(&*clear_haze.moon_lighting,&*dense_haze.moon_lighting,sizeof(MoonLightingGpu))==0,
                "Haze-only input changed light sources");
        const auto haze_a=checked_capture(clear_haze,"environment-haze-clear");
        const auto haze_b=checked_capture(dense_haze,"environment-haze-dense");
        const auto haze_pixels=changed(haze_a,haze_b);require(haze_pixels>100u,"Haze input did not change atmosphere");
        Json shadow_cases=Json::array();
        for (const auto hours:std::array<std::array<float,2>,2>{{{9.f,15.f},{21.f,3.f}}}) {
            std::array<ShadowMask,2> masks{};
            for (std::size_t index=0;index<2u;++index) {
                auto frame=compose(hours[index],0.f,0.f,false);
                const auto name="environment-shadow-"+std::to_string(static_cast<int>(hours[index]));
                const auto on=checked_capture(frame,name+"-on");frame.sky_shadow_triangles.clear();
                const auto off=checked_capture(frame,name+"-off");masks[index]=shadow_mask(on,off,carrier);
            }
            const auto distance=std::hypot(masks[0].x-masks[1].x,masks[0].y-masks[1].y);
            require(distance>3.,"Direction change did not relocate the source-specific shadow");
            shadow_cases.push_back({{"hours",hours},{"counts",{masks[0].pixels,masks[1].pixels}},
                {"centroids",{{masks[0].x,masks[0].y},{masks[1].x,masks[1].y}}},{"relocation_pixels",distance}});
        }
        Json horizons=Json::array();
        std::size_t horizon_index{};
        for (const float hour:std::array<float,6>{5.9f,6.f,6.1f,17.9f,18.f,18.1f}) {
            const auto frame=compose(hour,0.f,0.f,false);
            const auto name="environment-horizon-"+std::to_string(horizon_index++);
            const auto image=checked_capture(frame,name+"-on");
            auto sky_off=frame;sky_off.surface_lighting.sun_color_intensity[3]=0.f;
            sky_off.moon_lighting->color_intensity[3]=0.f;
            const auto off_image=checked_capture(sky_off,name+"-off");
            const auto sky_delta=land_energy(image,carrier)-land_energy(off_image,carrier);
            if(hour==6.f||hour==18.f)
                require(image.rgba==off_image.rgba&&sky_delta==0.,"Horizon zero source changed radiance");
            else require(sky_delta>0.&&changed(image,off_image)>10u,"Near-horizon sky contribution is empty");
            horizons.push_back({{"hour",hour},{"land_sdr_mean_sky_delta",sky_delta},
                {"changed_sky_pixels",changed(image,off_image)},{"gpu",sky_tuple(frame)}});
        }
        auto night=compose(0.f,0.f,0.f,false);const auto night_image=checked_capture(night,"environment-night-source-on");
        auto moon_off=night;moon_off.moon_lighting->color_intensity[3]=0.f;
        require(changed(night_image,checked_capture(moon_off,"environment-night-moon-off"))>100u,
                "Independent moon contribution is empty");
        auto sun_off=noon;sun_off.surface_lighting.sun_color_intensity[3]=0.f;
        require(changed(noon_image,checked_capture(sun_off,"environment-noon-sun-off"))>100u,
                "Independent sun contribution is empty");
        auto local_off=night;
        for (std::uint32_t index=1u;index<local_off.surface_lighting.counts[0];++index)
            local_off.surface_lighting.lights[index].color_intensity[3]=0.f;
        const auto local_pixels=changed(night_image,checked_capture(local_off,"environment-night-local-off"));
        require(local_pixels>100u,"Independent authored local-light contribution is empty");
        auto ambient_on=local_off;ambient_on.moon_lighting->color_intensity[3]=0.f;
        ambient_on.surface_lighting.sun_color_intensity[3]=0.f;
        for (auto& material:ambient_on.sprite_material_draws) material.emission_scale=0.f;
        const auto ambient_image=checked_capture(ambient_on,"environment-night-ambient-on");
        auto ambient_off=ambient_on;ambient_off.surface_lighting.ambient={0.f,0.f,0.f,0.f};
        const auto ambient_pixels=changed(ambient_image,checked_capture(ambient_off,"environment-night-ambient-off"));
        require(ambient_pixels>100u,"Independent ambient contribution is empty");
        // Isolate emission over black, below SDR clamp. Differencing two
        // saturated night pixels would not reliably measure the linear hue.
        auto emission_on=ambient_off;emission_on.sprite_material_draws[0].emission_scale=.125f;
        const auto emission_image=checked_capture(emission_on,"environment-night-emission-on-isolated");
        auto emission_off=emission_on;emission_off.sprite_material_draws[0].emission_scale=0.f;
        const auto no_emission=checked_capture(emission_off,"environment-night-emission-off");
        std::size_t warm_pixels{};
        for (std::size_t pixel=0;pixel<emission_image.rgba.size();pixel+=4u) {
            const int red=static_cast<int>(emission_image.rgba[pixel])-no_emission.rgba[pixel];
            const int green=static_cast<int>(emission_image.rgba[pixel+1u])-no_emission.rgba[pixel+1u];
            const int blue=static_cast<int>(emission_image.rgba[pixel+2u])-no_emission.rgba[pixel+2u];
            if (red>3&&red>green&&green>blue) ++warm_pixels;
        }
        require(warm_pixels>10u,"Window emission-only night delta is not warm");
        std::vector<EngineControlSettings> invalid;
        const auto add_bad=[&](const auto& edit) {auto bad=settings;edit(bad);invalid.push_back(bad);};
        add_bad([](auto& bad){bad.environment->version=2u;});
        for (const float hour:std::array<float,3>{-1.f,24.f,std::numeric_limits<float>::quiet_NaN()})
            add_bad([&](auto& bad){bad.environment->time_hour=hour;});
        for (const float value:std::array<float,3>{-.1f,1.1f,std::numeric_limits<float>::quiet_NaN()}) {
            add_bad([&](auto& bad){bad.environment->cloud01=value;});
            add_bad([&](auto& bad){bad.environment->haze01=value;});
        }
        add_bad([](auto& bad){bad.atmosphere=EngineAtmosphereSettings{};});
        add_bad([](auto& bad){bad.water_enabled=false;});
        Frame2D published;(void)view.compose_engine_control(session,10.,settings,controls,published);
        const auto old_screen=view.tile_screen(26.f,24.f);const auto old_pick=view.cell_at_screen(old_screen.x,old_screen.y);
        for (const auto& bad:invalid) {
            bool rejected{};try{(void)view.compose_engine_control(session,10.,bad,controls,published);}
            catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"Invalid environment input was accepted");
            const auto screen=view.tile_screen(26.f,24.f);
            const auto pick=view.cell_at_screen(old_screen.x,old_screen.y);
            require(screen.x==old_screen.x&&screen.y==old_screen.y&&pick==old_pick &&
                published.surface_lighting.ambient==noon.surface_lighting.ambient &&
                published.vertices.size()==noon.vertices.size(),"Rejected environment published partial state");
        }
        require(checked_capture(published,"environment-after-invalid").rgba==noon_image.rgba,
                "Invalid-input recovery changed the previous frame");
        save_report(output/"environment-native-result.json",{{"complete",true},
            {"scope","Diagnostic Environment V1 adapter/native causality, not full E06/engine acceptance"},
            {"backend",renderer.device_info().backend},{"gpu",renderer.device_info().device},
            {"driver",renderer.device_info().driver},{"resolution",{1920,1080}},
            {"graph","WaterAtmosphereSlice"},{"captures",captures},{"time_cases",time_cases},
            {"shadow_cases",shadow_cases},{"horizons",horizons},{"cloud_changed_pixels",cloud_pixels},
            {"haze_changed_pixels",haze_pixels},{"warm_emission_pixels",warm_pixels},
            {"warm_emission_fixture_scale",.125f},{"warm_emission_fixture_lighting","all non-emission radiance zero"},
            {"authored_local_changed_pixels",local_pixels},{"ambient_changed_pixels",ambient_pixels},
            {"raw_carrier",counts},{"raw_carrier_exact",true},{"opaque_ui_exact",true},
            {"locals_emission_metadata_exact",true},{"paused_exact",true},{"manual_equivalence_exact",true},
            {"invalid_inputs_rejected",invalid.size()},{"midnight_cpu_tolerance",1.e-5},
            {"targets",stable_targets(renderer.ground_forward_status())},{"fps",nullptr},
            {"gpu_execution_ms",nullptr},{"E06","OPEN"},{"full_engine","OPEN"},
            {"limitations",{"Diagnostic hour/cloud/haze only, not host authority",
                "Technical controls, not concept art/all world categories",
                "No video/native input/DPI/performance or raw HDR evidence",
                "Invalid checks preserve published frame/picks/camera, not all terrain cache internals"}}});
        check_budget(output);std::cout<<"GameView environment native checks passed\n";return 0;
    } catch(const std::exception& error) {
        if(owned)try{save_report(output/"environment-native-result.json",{{"complete",false},
            {"error",error.what()},{"captures",captures},{"E06","OPEN"},{"full_engine","OPEN"}});}catch(...){}
        std::cerr<<"GameView environment probe failed: "<<error.what()<<'\n';return 1;
    }
}

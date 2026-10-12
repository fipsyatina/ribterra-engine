#include "engine_control_probe.hpp"

int main(int argc,char** argv) {
    fs::path output;
    bool owned{};
    Json captures=Json::array();
    try {
        require(argc==4,"Usage: GameViewWaterProbe runtime-assets diagnostic-kit new-output");
        const auto assets=fs::absolute(argument(argv[1])),kit=fs::absolute(argument(argv[2]));
        output=fs::absolute(argument(argv[3]));
        require(output.parent_path()==assets.parent_path().parent_path(),"Output must be sibling build child");
        require(!fs::exists(output),"Native output already exists");
        require(fs::space(output.parent_path()).available>=384u*1024u*1024u,"Insufficient disk headroom");
        require(fs::create_directory(output),"Could not create native output"); owned=true;
        create_fixture(output/"water-world-current.save");
        require(SDL_Init(SDL_INIT_VIDEO),"SDL initialization failed");
        struct Quit { ~Quit(){SDL_Quit();} } quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Water Engine Control - not final art",1920,1080,0),SDL_DestroyWindow);
        require(window!=nullptr&&SDL_ShowWindow(window.get())&&SDL_SyncWindow(window.get()),"Native window failed");
        RendererOptions options;options.backend=Backend::Direct3D12;
        options.world_graph=WorldGraph::WaterReflectionSlice;options.collect_telemetry=true;
        // Initial allocation is 18 targets. Fail the last candidate of the first resize.
        options.test_fail_ground_target_creation_at=36u;
        Renderer2D renderer(window.get(),assets,options);renderer.set_logical_size(extent);
        require(!renderer.device_info().software,"Hardware D3D12 is required");
        bool unreadable_before_submit{};
        try { (void)renderer.read_water_coverage_v1(); }
        catch (const std::logic_error&) { unreadable_before_submit=true; }
        require(unreadable_before_submit,"Raw WaterCoverage was readable before a successful frame");
        ClientSession session(70421);std::string error;
        require(session.load(output/"water-world-current.save",error),error);
        require(!session.last_load().copied,"Current fixture unexpectedly migrated");
        GameView view(renderer);view.set_logical_size(extent);
        view.load_engine_control_art(AssetCatalog(kit));view.load_engine_water_art();
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
        Frame2D frame;const auto stats=view.compose_engine_control(session,10.,settings,controls,frame);
        require(stats.water_cells>10u&&stats.bridge_decks==3u&&stats.reflection_draws==2u,
                "Water/three-section bridge/reflection fixture is vacuous");
        // Every section is E-W and narrow, including the middle over water.
        for(const auto& command:frame.commands) {
            const auto* draw=std::get_if<GeometryDraw>(&command);
            if(!draw||draw->ground_material_index==0u||frame.ground_material_draws[draw->ground_material_index-1u].water_surface_class!=GroundSurfaceClass::BridgeDeck)continue;
            const auto& a=frame.surface_vertices[frame.indices[draw->first_index]].world_position;
            const auto& b=frame.surface_vertices[frame.indices[draw->first_index+2u]].world_position;
            require(std::abs((b[0]-a[0])-1.f)<1.e-5f&&std::abs((b[1]-a[1])-.44f)<1.e-5f,
                    "Bridge sections do not form a continuous E-W narrow deck");
        }
        const auto baseline=capture(renderer,frame,output,"water-baseline",captures);
        const auto repeat=capture(renderer,frame,output,"water-same-clock",captures);
        require(baseline.rgba==repeat.rgba,"Same shown clock is not repeatable");
        auto no_reflection=frame;no_reflection.reflection_draws.clear();
        const auto reflection_off=capture(renderer,no_reflection,output,"water-reflection-off",captures);
        const auto reflection_difference=water_only_difference(baseline,reflection_off,frame);
        require(reflection_difference>25u,"Native reflection contribution is vacuous");
        auto advanced=frame;advanced.water_surface->world_origin_time[2]+=1.f;
        const auto next=capture(renderer,advanced,output,"water-clock-next",captures);
        const auto clock_difference=water_only_difference(baseline,next,frame);
        require(clock_difference>100u,"Native world-clock water flow is vacuous");
        auto no_shore=frame;
        for(auto& material:no_shore.ground_material_draws)material.shore_edges=0;
        const auto shore_off=capture(renderer,no_shore,output,"water-shore-off",captures);
        const auto shore_difference=water_only_difference(baseline,shore_off,frame);
        require(shore_difference>25u,"Native water-side shore foam is vacuous");
        std::size_t camera_cases{};
        for(const float zoom:{.62f,1.3f,2.f})for(const std::array<float,2> pan:
                std::array<std::array<float,2>,2>{{{0.f,0.f},{.25f,-.125f}}}) {
            auto camera=settings;camera.zoom=zoom;camera.camera_x+=pan[0];camera.camera_y+=pan[1];
            Frame2D reprojection;const auto reprojection_stats=view.compose_engine_control(session,10.,camera,controls,reprojection);
            require(reprojection_stats.bridge_decks==3u&&reprojection_stats.reflection_draws==2u,
                    "Camera/zoom culled mandatory bridge/reflection controls");
            const auto name="water-camera-"+std::to_string(camera_cases++);
            (void)capture(renderer,reprojection,output,name,captures);
        }
        auto hidden=frame;
        for(auto& material:hidden.ground_material_draws)
            if(material.water_surface_class==GroundSurfaceClass::Water)material.surface.response[3]=0.f;
        const auto hidden_on=capture(renderer,hidden,output,"water-hidden-receiver",captures);
        hidden.reflection_draws.clear();
        const auto hidden_off=capture(renderer,hidden,output,"water-hidden-receiver-no-reflection",captures);
        require(hidden_on.rgba==hidden_off.rgba,"Unrevealed receiver retained reflection");
        Json rejects=Json::array();
        const auto reject=[&](const Frame2D& invalid,std::string_view name) {
            bool rejected{};std::string reason;
            try { renderer.render(invalid); }
            catch(const std::invalid_argument& exception){rejected=true;reason=exception.what();}
            catch(const std::out_of_range& exception){rejected=true;reason=exception.what();}
            require(rejected,"Invalid water frame was not rejected");
            rejects.push_back({{"case",name},{"reason",reason}});
        };
        auto invalid=frame;invalid.water_surface->flow_response[0]=2.f;reject(invalid,"non-unit-flow");
        invalid=frame;invalid.water_surface->viewport[0]=2.f;reject(invalid,"excess-distortion");
        invalid=frame;invalid.ground_material_draws.front().surface.response[3]=.5f;reject(invalid,"fractional-ground-reveal");
        invalid=frame;invalid.sprite_material_draws.front().reveal=.5f;reject(invalid,"fractional-caster-reveal");
        invalid=frame;invalid.ground_material_draws[1].geometric_coverage=.5f;reject(invalid,"unsupported-authored-AA");
        invalid=frame;invalid.reflection_draws.front().draw.sprite_material_index=999999u;reject(invalid,"invalid-reflection-material-index");
        const auto after_reject=capture(renderer,frame,output,"water-after-reject",captures);
        require(after_reject.rgba==baseline.rgba,"Rejected input changed subsequent native result");
        const auto before_targets=renderer.ground_forward_status();
        require(before_targets.creation_attempts==18u && before_targets.target_generation==1u,
                "Initial Water target allocation count/generation is wrong");
        const auto before_telemetry=renderer.frame_telemetry();
        const auto raw_before=renderer.read_water_coverage_v1();
        const auto raw_before_counts=carrier_counts(raw_before);
        require(raw_before.frame_slot==before_telemetry.frame_slot &&
                raw_before.target_generation==before_targets.target_generation &&
                raw_before.size.width==before_targets.width && raw_before.size.height==before_targets.height,
                "Initial raw carrier is not bound to the last successful submitted target");
        const auto after_read=renderer.frame_telemetry();
        require(after_read.sequence==before_telemetry.sequence &&
                after_read.submitted==before_telemetry.submitted &&
                after_read.frame_slot==before_telemetry.frame_slot &&
                after_read.graph_passes==before_telemetry.graph_passes &&
                after_read.cpu_total_ms==before_telemetry.cpu_total_ms &&
                stable_targets(renderer.ground_forward_status())==stable_targets(before_targets),
                "Raw readback changed render telemetry or active targets");
        constexpr Extent odd_extent{1279,719};
        const auto odd_window=resize_window(window.get(),renderer,odd_extent);
        bool resize_rejected{};std::string resize_reason;
        try { renderer.render(frame); }
        catch(const std::runtime_error& exception) {
            resize_rejected=true;resize_reason=exception.what();
        }
        const auto failed_targets=renderer.ground_forward_status();
        require(resize_rejected && resize_reason.find("Injected ground target creation failure")!=std::string::npos &&
                stable_targets(failed_targets)==stable_targets(before_targets) &&
                failed_targets.candidate_live_textures==0u && failed_targets.creation_attempts==36u &&
                !renderer.frame_telemetry().submitted,"Odd-size target failure did not roll back atomically");
        const auto raw_rollback=renderer.read_water_coverage_v1();
        require(raw_rollback.rgba8==raw_before.rgba8 && raw_rollback.frame_slot==raw_before.frame_slot &&
                raw_rollback.target_generation==raw_before.target_generation &&
                raw_rollback.size.width==raw_before.size.width && raw_rollback.size.height==raw_before.size.height &&
                !renderer.frame_telemetry().submitted,"Failed resize lost or rewrote prior raw carrier");
        renderer.render(frame);
        const auto odd_targets=renderer.ground_forward_status();
        const std::uint64_t full_pixels=std::uint64_t(odd_extent.width)*odd_extent.height;
        const std::uint64_t half_pixels=std::uint64_t((odd_extent.width+1u)/2u)*((odd_extent.height+1u)/2u);
        require(renderer.frame_telemetry().submitted && odd_targets.width==odd_extent.width &&
                odd_targets.height==odd_extent.height && odd_targets.reflection_width==(odd_extent.width+1u)/2u &&
                odd_targets.reflection_height==(odd_extent.height+1u)/2u && odd_targets.coverage_bytes==full_pixels*8u &&
                odd_targets.reflection_bytes==half_pixels*16u &&
                odd_targets.attachment_bytes==full_pixels*80u+half_pixels*16u &&
                odd_targets.target_generation==before_targets.target_generation+1u &&
                odd_targets.active_set_ids!=before_targets.active_set_ids &&
                odd_targets.creation_attempts==54u && odd_targets.candidate_live_textures==0u,
                "Odd-size Water allocation retry or ceiling-half dimensions are wrong");
        const auto raw_odd=renderer.read_water_coverage_v1();
        const auto raw_odd_counts=carrier_counts(raw_odd);
        require(raw_odd.size.width==odd_extent.width && raw_odd.size.height==odd_extent.height &&
                raw_odd.target_generation==odd_targets.target_generation &&
                raw_odd.frame_slot==renderer.frame_telemetry().frame_slot,
                "Raw carrier came from a stale target generation after resize");
        save_bytes(output/"water-coverage-odd.rgba8",raw_odd.rgba8);
        check_budget(output);
        const auto restored_window=resize_window(window.get(),renderer,extent);
        const auto restored=capture(renderer,frame,output,"water-after-resize",captures);
        require(restored.rgba==baseline.rgba && renderer.ground_forward_status().creation_attempts==72u &&
                renderer.ground_forward_status().target_generation==before_targets.target_generation+2u,
                "Restoring native size changed the baseline or target generation");
        const auto& ledger=renderer.ground_forward_status();
        require(ledger.attachment_bytes==174182400u&&ledger.reflection_width==960u&&
                ledger.reflection_height==540u,"Native water attachment ledger is wrong");
        const auto device=renderer.device_info();
        save_report(output/"native-water-report.json",{{"schema",1},{"accepted_scope","bounded-control-only"},
            {"device",{{"backend",device.backend},{"device",device.device},{"driver",device.driver}}},
            {"water_cells",stats.water_cells},{"bridge_decks",stats.bridge_decks},{"reflection_draws",stats.reflection_draws},
            {"reflection_changed_pixels",reflection_difference},{"clock_changed_pixels",clock_difference},
            {"shore_changed_pixels",shore_difference},{"camera_gpu_cases",camera_cases},
            {"opaque_final_alpha",true},{"same_clock_exact",true},{"receiver_reveal_gate",true},
            {"rejected_inputs",rejects},{"after_reject_exact",true},
            {"clip_check","diagnostic CPU raster classifier with 1 physical pixel boundary margin, not raw GBuffer readback"},
            {"nominal_attachment_bytes",ledger.attachment_bytes},{"captures",captures},
            {"raw_coverage",{{"before_submit_rejected",true},{"initial",raw_before_counts},
                {"odd",raw_odd_counts},{"odd_file","water-coverage-odd.rgba8"},
                {"failed_resize_raw_exact",true},{"readback_preserves_render_telemetry",true}}},
            {"resize",{{"failure_reason",resize_reason},{"failure_creation_attempt",36},
                {"odd_window",odd_window},{"restored_window",restored_window},
                {"old_targets_preserved",true},{"candidate_live_after_failure",0},
                {"retry",stable_targets(odd_targets)},{"retry_creation_attempts",54},
                {"restored",stable_targets(ledger)},{"restored_creation_attempts",72},
                {"restored_baseline_exact",true},{"memory_scope","nominal target payload, not total VRAM or measured peak"}}},
            {"open",{"intermediate AA coverage","normal startup/input/video","full weather/combined engine","concept art","120 FPS","full suite"}}});
        check_budget(output);
        std::cout<<"Bounded native water checkpoint passed; full engine remains OPEN\n";
        return 0;
    }catch(const std::exception& exception) {
        std::cerr<<exception.what()<<'\n';
        if(owned&&!fs::exists(output/"native-water-failure.json"))
            save_report(output/"native-water-failure.json",{{"error",exception.what()},{"captures",captures},{"accepted",false}});
        return 1;
    }
}

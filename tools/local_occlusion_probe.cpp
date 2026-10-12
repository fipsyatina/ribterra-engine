#define main ground_forward_probe_entry
#include "ground_forward_probe.cpp"
#undef main

namespace {
struct ReceiverControl {
    std::string id;
    Vec3 world{},normal{};
    Point screen{};
    std::uint32_t owner{};
};
struct CaptureSample {std::array<int,4> receiver{},ui{};RendererFrameTelemetry telemetry{};};

Vec3 add3(Vec3 a,Vec3 b){for(std::size_t i=0;i<3;++i)a[i]+=b[i];return a;}
Vec3 times3(Vec3 a,float scale){for(auto& value:a)value*=scale;return a;}
Vec3 centroid(const std::array<Vec3,3>& triangle){Vec3 out{};for(const auto& point:triangle)for(std::size_t i=0;i<3;++i)out[i]+=point[i]/3.f;return out;}
Point material_sample(const Kit& kit,std::size_t probe,Point ground,float zoom=1.25f){
    const auto& row=kit.calibration.at("probes").at(probe);const Point source{row.at("source_pixel").at(0),row.at("source_pixel").at(1)};
    const Point pivot{kit.calibration.at("pivot_px").at(0),kit.calibration.at("pivot_px").at(1)};
    const float eppp=kit.calibration.at("export_pixels_per_reference_pixel");const auto offset=screen_offset_from_source_pixel(source,pivot,eppp,zoom);
    return {ground.x+offset.x,ground.y+offset.y};
}
Vec3 tangent_for(Vec3 normal){Vec3 tangent=cross(normal,{0,0,1});if(dot(tangent,tangent)<.04f)tangent={1,0,0};return normalized(tangent);}

OccluderGpu blocker_between(Vec3 receiver,Vec3 light,OccluderShape shape,std::uint32_t stable,std::uint32_t owner=900u){
    const Vec3 middle=times3(add3(receiver,light),.5f);OccluderGpu row;
    row.center_base={middle[0],middle[1],0.f,middle[2]-.45f};row.axis_x_extent={1,0,shape==OccluderShape::Obb?.38f:0.f,middle[2]+.45f};
    row.axis_y_radius=shape==OccluderShape::Obb?std::array<float,4>{0,1,.38f,0}:std::array<float,4>{0,0,0,.38f};
    row.shape_flags_id={static_cast<std::uint32_t>(shape),1u,stable,owner};return row;
}
void move_off_axis(OccluderGpu& row,Vec3 receiver,Vec3 light){
    Vec3 direction=subtract(light,receiver);Vec3 side=normalized(Vec3{-direction[1],direction[0],0});row.center_base[0]+=side[0]*2.f;row.center_base[1]+=side[1]*2.f;
}

Frame2D receiver_frame(Renderer2D& renderer,GameArt& art,const Kit& kit,TextureHandle ground,
                       const ReceiverControl& target,std::vector<OccluderGpu> blockers,
                       bool red_range,bool blue_range,std::uint32_t red_mask=1u,std::uint32_t blue_mask=1u){
    ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);
    frame.sprite_material_draws.back().receiver_owner=target.owner;frame.sprite_material_draws.back().receiver_mask=1u;
    frame.surface_lighting.ambient={.02f,.02f,.02f,0};frame.surface_lighting.sun_color_intensity={0,0,0,0};frame.surface_lighting.counts[0]=2;
    const auto tangent=tangent_for(target.normal);const auto normal=times3(target.normal,1.5f);
    const Vec3 red_anchor=add3(add3(target.world,normal),times3(tangent,.8f));
    const Vec3 blue_anchor=add3(add3(target.world,normal),times3(tangent,-.8f));
    auto& red=frame.surface_lighting.lights[0];red.anchor_radius={red_anchor[0],red_anchor[1],red_anchor[2],4.f};red.color_intensity={1,0,0,5.f};
    red.occluder_range_flags={0u,red_range?1u:0u,red_mask,0u};
    auto& blue=frame.surface_lighting.lights[1];blue.anchor_radius={blue_anchor[0],blue_anchor[1],blue_anchor[2],4.f};blue.color_intensity={0,0,1,5.f};
    blue.occluder_range_flags={blue_range&&red_range?1u:0u,blue_range?1u:0u,blue_mask,0u};frame.local_occluders=std::move(blockers);
    return frame;
}

std::pair<Vec3,Vec3> light_anchors(const ReceiverControl& target){
    const auto tangent=tangent_for(target.normal),normal=times3(target.normal,1.5f);
    return {add3(add3(target.world,normal),times3(tangent,.8f)),add3(add3(target.world,normal),times3(tangent,-.8f))};
}
CaptureSample capture(Renderer2D& renderer,Frame2D frame,const std::filesystem::path& path,Point receiver){
    renderer.render(frame,path);const auto measured=renderer.frame_telemetry();require(measured.graph_pass_count==5&&measured.submitted,"Occlusion capture bypassed production graph");
    require(measured.occluders==frame.local_occluders.size()&&measured.occluder_upload_bytes==std::max<std::size_t>(frame.local_occluders.size(),1u)*sizeof(OccluderGpu),"Occluder telemetry/upload ledger mismatch");
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_LoadPNG(utf8(path).c_str()),SDL_DestroySurface);require(image!=nullptr,"Could not decode occlusion capture");
    require(logical_pixel(image.get(),{48,48})==std::array<int,4>{37,211,149,255},"Local visibility control changed UI bytes");
    return {logical_pixel(image.get(),receiver),logical_pixel(image.get(),{48,48}),measured};
}
Json sample_json(const CaptureSample& sample){return {{"receiver",sample.receiver},{"ui",sample.ui},{"slot",sample.telemetry.frame_slot},
    {"occluders",sample.telemetry.occluders},{"occluder_upload_bytes",sample.telemetry.occluder_upload_bytes}};}

Json causal_matrix(Renderer2D& renderer,GameArt& art,const Kit& kit,TextureHandle ground,const std::filesystem::path& output){
    constexpr Point cabin_ground{950,565};constexpr Point origin{490,390};constexpr float zoom=1.45f;
    const auto slope_world=Vec3{.3f*kit.slope_a[0][0]+.3f*kit.slope_a[1][0]+.4f*kit.slope_a[2][0],
        .3f*kit.slope_a[0][1]+.3f*kit.slope_a[1][1]+.4f*kit.slope_a[2][1],.3f*kit.slope_a[0][2]+.3f*kit.slope_a[1][2]+.4f*kit.slope_a[2][2]};
    Vec3 flat_world=slope_world;flat_world[0]+=6.f;flat_world[2]=0;const auto slope_projected=project_render_xyz(slope_world),flat_projected=project_render_xyz(flat_world);
    const auto& facade_probe=kit.calibration.at("probes").at(0);const auto& roof_probe=kit.calibration.at("probes").at(8);
    const std::array receivers{
        ReceiverControl{"ground-slope",slope_world,kit.slope_normal,{origin.x+slope_projected.x*zoom,origin.y+slope_projected.y*zoom},0},
        ReceiverControl{"ground-flat",flat_world,{0,0,1},{origin.x+flat_projected.x*zoom,origin.y+flat_projected.y*zoom},0},
        ReceiverControl{"cabin-facade",facade_probe.at("position").get<Vec3>(),facade_probe.at("normal").get<Vec3>(),material_sample(kit,0,cabin_ground),77},
        ReceiverControl{"cabin-roof",roof_probe.at("position").get<Vec3>(),roof_probe.at("normal").get<Vec3>(),material_sample(kit,8,cabin_ground),77}};
    Json cases=Json::array();constexpr std::array<int,4> ui_expected{37,211,149,255};
    for(std::size_t index=0;index<receivers.size();++index){const auto& target=receivers[index];const auto [red_anchor,blue_anchor]=light_anchors(target);
        const auto base=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{},false,false),output/(target.id+"-base.png"),target.screen);
        auto red_obb=blocker_between(target.world,red_anchor,OccluderShape::Obb,100u+static_cast<std::uint32_t>(index)*10u);
        const auto red_blocked=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{red_obb},true,false),output/(target.id+"-red-obb.png"),target.screen);
        auto blue_capsule=blocker_between(target.world,blue_anchor,OccluderShape::Capsule,101u+static_cast<std::uint32_t>(index)*10u);
        const auto blue_blocked=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{blue_capsule},false,true),output/(target.id+"-blue-capsule.png"),target.screen);
        move_off_axis(red_obb,target.world,red_anchor);move_off_axis(blue_capsule,target.world,blue_anchor);
        const auto moved=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{red_obb,blue_capsule},true,true),output/(target.id+"-moved.png"),target.screen);
        red_obb=blocker_between(target.world,red_anchor,OccluderShape::Obb,100u+static_cast<std::uint32_t>(index)*10u);
        blue_capsule=blocker_between(target.world,blue_anchor,OccluderShape::Capsule,101u+static_cast<std::uint32_t>(index)*10u);
        const auto red_repeat=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{red_obb},true,false),output/(target.id+"-red-repeat.png"),target.screen);
        const auto blue_repeat=capture(renderer,receiver_frame(renderer,art,kit,ground,target,{blue_capsule},false,true),output/(target.id+"-blue-repeat.png"),target.screen);
        require(red_blocked.telemetry.frame_slot!=red_repeat.telemetry.frame_slot&&
                blue_blocked.telemetry.frame_slot!=blue_repeat.telemetry.frame_slot&&
                read_bytes(output/(target.id+"-red-obb.png"))==read_bytes(output/(target.id+"-red-repeat.png"))&&
                read_bytes(output/(target.id+"-blue-capsule.png"))==read_bytes(output/(target.id+"-blue-repeat.png")),
                "Local occlusion state was not byte-exact across both fenced slots");
        require(base.receiver[0]-red_blocked.receiver[0]>=12&&std::abs(base.receiver[2]-red_blocked.receiver[2])<=2,"OBB did not independently block the red light");
        require(base.receiver[2]-blue_blocked.receiver[2]>=12&&std::abs(base.receiver[0]-blue_blocked.receiver[0])<=2,"Capsule did not independently block the blue light");
        require(channels_near(base.receiver,moved.receiver,2),"Moving both blockers off-axis did not restore the receiver");
        for(const auto* sample:{&base,&red_blocked,&blue_blocked,&moved})require(sample->ui==ui_expected,"Local occlusion changed UI reference color");
        cases.push_back({{"receiver",target.id},{"world",target.world},{"normal",target.normal},{"red_anchor",red_anchor},{"blue_anchor",blue_anchor},
            {"base",sample_json(base)},{"red_obb",sample_json(red_blocked)},{"blue_capsule",sample_json(blue_blocked)},{"moved",sample_json(moved)}});
    }
    return {{"receivers",cases},{"channel_margin",12},{"unaffected_channel_tolerance",2},{"moved_tolerance",2},{"cross_slot_byte_exact",true}};
}

Json owner_and_controls(Renderer2D& renderer,GameArt& art,const Kit& kit,TextureHandle ground,const std::filesystem::path& output){
    constexpr Point cabin_ground{950,565};const auto& roof_probe=kit.calibration.at("probes").at(8);
    ReceiverControl roof{"cabin-roof-owner",roof_probe.at("position").get<Vec3>(),roof_probe.at("normal").get<Vec3>(),material_sample(kit,8,cabin_ground),77};
    const auto [red_anchor,blue_anchor]=light_anchors(roof);(void)blue_anchor;
    const auto authored=[&](std::uint32_t blocker_owner,float reveal,bool mixed){
        ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);frame.sprite_material_draws.back().receiver_owner=77;
        frame.surface_lighting.ambient={.02f,.02f,.02f,0};frame.surface_lighting.sun_color_intensity={0,0,0,0};frame.surface_lighting.counts[0]=1;
        auto& light=frame.surface_lighting.lights[0];light.anchor_radius={red_anchor[0],red_anchor[1],red_anchor[2],4};light.color_intensity={1,0,0,5};
        const auto count=art.append_lit_occluders(frame,"calibration.off",{0,0,0},1.f,blocker_owner,700u,reveal);
        if(mixed)art.append_lit_occluders(frame,"calibration.off",{0,0,0},1.f,88u,701u,1.f);
        light.occluder_range_flags={0u,static_cast<std::uint32_t>(frame.local_occluders.size()),1u,0u};return std::pair{std::move(frame),count};};
    auto [hidden_frame,hidden_count]=authored(77,.5f,false);require(hidden_count==0&&hidden_frame.local_occluders.empty(),"Partially revealed authored blocker was not omitted");
    const auto hidden=capture(renderer,std::move(hidden_frame),output/"owner-hidden.png",roof.screen);
    auto [own_frame,own_count]=authored(77,1.f,false);require(own_count==1,"Fresh source kit did not append its authored OBB");const auto authored_row=own_frame.local_occluders.front();
    const auto own=capture(renderer,std::move(own_frame),output/"owner-own.png",roof.screen);
    auto [foreign_frame,foreign_count]=authored(88,1.f,false);require(foreign_count==1,"Foreign authored blocker missing");const auto foreign=capture(renderer,std::move(foreign_frame),output/"owner-foreign.png",roof.screen);
    auto [mixed_frame,mixed_count]=authored(77,1.f,true);require(mixed_count==1&&mixed_frame.local_occluders.size()==2,"Mixed owner fixture did not retain both prisms");
    const auto mixed=capture(renderer,std::move(mixed_frame),output/"owner-mixed.png",roof.screen);
    require(channels_near(hidden.receiver,own.receiver,2),"Matching owner did not exclude only its own authored prism");
    require(own.receiver[0]-foreign.receiver[0]>=12&&own.receiver[0]-mixed.receiver[0]>=12,"Foreign authored prism did not remain blocking beside an excluded own prism");

    const auto sprite_mask=[&](std::uint32_t receiver_mask){
        ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);
        frame.sprite_material_draws.back().receiver_owner=77;frame.sprite_material_draws.back().receiver_mask=receiver_mask;
        frame.surface_lighting.ambient={.02f,.02f,.02f,0};frame.surface_lighting.sun_color_intensity={0,0,0,0};frame.surface_lighting.counts[0]=1;
        auto& light=frame.surface_lighting.lights[0];light.anchor_radius={red_anchor[0],red_anchor[1],red_anchor[2],4};light.color_intensity={1,0,0,5};
        light.occluder_range_flags={0u,0u,2u,0u};return frame;};
    const auto sprite_mask_miss=capture(renderer,sprite_mask(1u),output/"sprite-mask-miss.png",roof.screen);
    const auto sprite_mask_match=capture(renderer,sprite_mask(2u),output/"sprite-mask-match.png",roof.screen);
    require(sprite_mask_match.receiver[0]-sprite_mask_miss.receiver[0]>=12&&
            std::abs(sprite_mask_match.receiver[2]-sprite_mask_miss.receiver[2])<=2,
            "Sprite material receiver mask did not independently gate its matching local light");

    ReceiverControl slope; slope.id="ground-controls";slope.world={.3f*kit.slope_a[0][0]+.3f*kit.slope_a[1][0]+.4f*kit.slope_a[2][0],
        .3f*kit.slope_a[0][1]+.3f*kit.slope_a[1][1]+.4f*kit.slope_a[2][1],.3f*kit.slope_a[0][2]+.3f*kit.slope_a[1][2]+.4f*kit.slope_a[2][2]};
    slope.normal=kit.slope_normal;const auto projected=project_render_xyz(slope.world);slope.screen={490+projected.x*1.45f,390+projected.y*1.45f};
    const auto [slope_red,slope_blue]=light_anchors(slope);const auto baseline=capture(renderer,receiver_frame(renderer,art,kit,ground,slope,{},false,false),output/"control-base.png",slope.screen);
    auto high=blocker_between(slope.world,slope_red,OccluderShape::Obb,800);high.center_base[3]=10;high.axis_x_extent[3]=11;
    const auto outside_z=capture(renderer,receiver_frame(renderer,art,kit,ground,slope,{high},true,false),output/"control-outside-z.png",slope.screen);
    auto offaxis=blocker_between(slope.world,slope_blue,OccluderShape::Capsule,801);move_off_axis(offaxis,slope.world,slope_blue);
    const auto capsule_offaxis=capture(renderer,receiver_frame(renderer,art,kit,ground,slope,{offaxis},false,true),output/"control-capsule-offaxis.png",slope.screen);
    const Vec3 rotated_anchor{slope.world[0]+.6f*.8f,slope.world[1]+.8f*.8f,slope.world[2]+1.5f};
    const auto rotated_frame=[&](std::vector<OccluderGpu> blockers){const bool has_blocker=!blockers.empty();auto frame=receiver_frame(renderer,art,kit,ground,slope,std::move(blockers),false,has_blocker);
        frame.surface_lighting.lights[1].anchor_radius={rotated_anchor[0],rotated_anchor[1],rotated_anchor[2],4.f};return frame;};
    const auto rotated_baseline=capture(renderer,rotated_frame({}),output/"control-capsule-rotated-base.png",slope.screen);
    auto rotated=blocker_between(slope.world,rotated_anchor,OccluderShape::Capsule,802);rotated.axis_x_extent[0]=.6f;rotated.axis_x_extent[1]=.8f;rotated.axis_x_extent[2]=.8f;
    const auto capsule_rotated=capture(renderer,rotated_frame({rotated}),output/"control-capsule-rotated.png",slope.screen);
    auto perpendicular_inside=rotated;perpendicular_inside.shape_flags_id[2]=803;perpendicular_inside.center_base[0]-=.8f*.2f;perpendicular_inside.center_base[1]+=.6f*.2f;
    const auto capsule_perpendicular_inside=capture(renderer,rotated_frame({perpendicular_inside}),output/"control-capsule-perpendicular-inside.png",slope.screen);
    auto perpendicular_outside=rotated;perpendicular_outside.shape_flags_id[2]=804;perpendicular_outside.center_base[0]-=.8f*.6f;perpendicular_outside.center_base[1]+=.6f*.6f;
    const auto capsule_perpendicular_outside=capture(renderer,rotated_frame({perpendicular_outside}),output/"control-capsule-perpendicular-outside.png",slope.screen);
    auto endpoint=rotated;endpoint.shape_flags_id[2]=805;endpoint.center_base[0]+=.6f*.8f;endpoint.center_base[1]+=.8f*.8f;
    const auto capsule_endpoint=capture(renderer,rotated_frame({endpoint}),output/"control-capsule-endpoint.png",slope.screen);
    auto capsule_high=rotated;capsule_high.shape_flags_id[2]=806;capsule_high.center_base[3]=10;capsule_high.axis_x_extent[3]=11;
    const auto capsule_offheight=capture(renderer,rotated_frame({capsule_high}),output/"control-capsule-offheight.png",slope.screen);
    const auto mask=capture(renderer,receiver_frame(renderer,art,kit,ground,slope,{},false,false,2u,1u),output/"control-mask.png",slope.screen);
    require(channels_near(baseline.receiver,outside_z.receiver,2)&&channels_near(baseline.receiver,capsule_offaxis.receiver,2)&&
            channels_near(rotated_baseline.receiver,capsule_perpendicular_outside.receiver,2)&&
            channels_near(rotated_baseline.receiver,capsule_offheight.receiver,2),"Outside-Z/off-axis/perpendicular/outside-height blocker changed receiver visibility");
    require(rotated_baseline.receiver[2]-capsule_rotated.receiver[2]>=12&&
            rotated_baseline.receiver[2]-capsule_perpendicular_inside.receiver[2]>=12&&
            rotated_baseline.receiver[2]-capsule_endpoint.receiver[2]>=12,
            "Rotated finite capsule body/end-cap did not block the blue light");
    require(baseline.receiver[0]-mask.receiver[0]>=12&&std::abs(baseline.receiver[2]-mask.receiver[2])<=2,"Receiver mask did not independently gate the red light");
    return {{"authored_source_prism",{{"center_base",authored_row.center_base},{"axis_x_extent",authored_row.axis_x_extent},{"axis_y_radius",authored_row.axis_y_radius},{"shape_flags_id",authored_row.shape_flags_id}}},
        {"owner",{{"hidden",sample_json(hidden)},{"own",sample_json(own)},{"foreign",sample_json(foreign)},{"mixed",sample_json(mixed)}}},
        {"sprite_receiver_mask",{{"light_mask",2},{"miss_receiver_mask",1},{"match_receiver_mask",2},
         {"miss",sample_json(sprite_mask_miss)},{"match",sample_json(sprite_mask_match)},{"channel_margin",12}}},
        {"controls",{{"base",sample_json(baseline)},{"outside_z",sample_json(outside_z)},{"capsule_offaxis",sample_json(capsule_offaxis)},
         {"capsule_rotated_base",sample_json(rotated_baseline)},{"capsule_rotated",sample_json(capsule_rotated)},
         {"capsule_perpendicular_inside",sample_json(capsule_perpendicular_inside)},{"capsule_perpendicular_outside",sample_json(capsule_perpendicular_outside)},
         {"capsule_endpoint",sample_json(capsule_endpoint)},{"capsule_offheight",sample_json(capsule_offheight)},
         {"rotated_axis",{.6f,.8f}},{"perpendicular",{-.8f,.6f}},{"inside_offset",.2f},{"outside_offset",.6f},{"radius",.38f},{"half_length",.8f},{"mask",sample_json(mask)}}}};
}

Frame2D minimal_validation_frame(){
    Frame2D frame;frame.logical_size=logical;frame.clear={0,0,0,255};frame.linear_hdr_world=true;frame.ground_material_draws.push_back({});
    append_ground_triangle(frame,{{Vec3{-1,-1,0},Vec3{1,-1,0},Vec3{0,1,0}}},{0,0,1},{320,180},40,1);frame.surface_lighting.sun_color_intensity={0,0,0,0};return frame;
}
void reject_exact(Renderer2D& renderer,const Frame2D& frame,std::string_view expected){
    std::string diagnostic;try{renderer.render(frame);}catch(const std::invalid_argument& error){diagnostic=error.what();}
    require(diagnostic==expected,"Unexpected CPU validation diagnostic");require(!renderer.has_submitted_frames(),"Invalid occluder frame reached GPU submission");
}
Json cpu_rejections(SDL_Window* window,const std::filesystem::path& assets){
    RendererOptions options;options.backend=Backend::Direct3D12;options.world_graph=WorldGraph::GroundForwardSlice;Renderer2D renderer(window,assets,options);Json rows=Json::array();
    const auto valid=[](std::uint32_t id){OccluderGpu row;row.shape_flags_id[2]=id;return row;};
    auto check=[&](std::string_view id,Frame2D frame,std::string_view message){reject_exact(renderer,frame,message);rows.push_back({{"id",id},{"diagnostic",message}});};
    auto frame=minimal_validation_frame();for(std::uint32_t i=1;i<=33;++i)frame.local_occluders.push_back(valid(i));frame.surface_lighting.counts[0]=1;frame.surface_lighting.lights[0].occluder_range_flags={0,33,1,0};
    check("per-light-33",frame,"Local occluder per-light overflow: maximum 32; no blockers discarded");
    frame=minimal_validation_frame();for(std::uint32_t i=1;i<=257;++i)frame.local_occluders.push_back(valid(i));check("global-257",frame,"Local occluder global overflow: maximum 256; no blockers discarded");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.surface_lighting.counts[0]=1;frame.surface_lighting.lights[0].occluder_range_flags={1,1,1,0};check("range",frame,"Local occluder light range is outside the prepared list");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].center_base[0]=NAN;check("nan",frame,"Local occluder bounds/reveal/identity are invalid");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].axis_x_extent[0]=2;check("axes",frame,"Local occluder primary axis must be normalized XY");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].axis_x_extent[3]=5.e-5f;check("minimum-height",frame,"Local occluder height must be at least 0.0001");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].axis_x_extent[2]=5.e-5f;check("minimum-obb-extent",frame,"Local OBB half extents must be at least 0.0001");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].shape_flags_id[0]=static_cast<std::uint32_t>(OccluderShape::Capsule);frame.local_occluders[0].axis_x_extent[2]=0;frame.local_occluders[0].axis_y_radius={0,0,0,5.e-5f};check("minimum-capsule-radius",frame,"Local capsule radius must be at least 0.0001");
    frame=minimal_validation_frame();frame.surface_lighting.counts[0]=1;frame.surface_lighting.lights[0].anchor_radius[0]=33.f;check("light-anchor-bound",frame,"Local light anchor exceeds the camera-relative bound");
    frame=minimal_validation_frame();frame.local_occluders={valid(1),valid(1)};check("duplicate-id",frame,"Local occluder bounds/reveal/identity are invalid");
    frame=minimal_validation_frame();frame.local_occluders.push_back(valid(1));frame.local_occluders[0].shape_flags_id[1]=0;check("hidden",frame,"Local occluder bounds/reveal/identity are invalid");
    return {{"gpu_submissions",0},{"cases",rows}};
}
Json legacy_rejection(SDL_Window* window,const std::filesystem::path& assets){
    Renderer2D renderer(window,assets,{Backend::Software});auto frame=minimal_validation_frame();frame.local_occluders.push_back({});
    std::string diagnostic;try{renderer.render(frame);}catch(const std::invalid_argument& error){diagnostic=error.what();}
    require(diagnostic=="Local occluders require GroundForwardSlice","Legacy local-occluder rejection changed");return {{"diagnostic",diagnostic},{"gpu_submission",false}};
}

Json buffer_fault(SDL_Window* window,const std::filesystem::path& assets,const Kit& kit,const std::filesystem::path& output){
    Json initial;
    {
        RendererOptions options;options.backend=Backend::Direct3D12;options.world_graph=WorldGraph::GroundForwardSlice;options.collect_telemetry=true;options.test_fail_gpu_frame_buffer_creation_at=4;
        Renderer2D renderer(window,assets,options);GameArt art(renderer);art.load(kit.assets);const std::array<std::uint8_t,4> albedo{82,96,86,255};
        const auto ground=renderer.create_sampled_texture({{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo);ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);
        frame.local_occluders.push_back(blocker_between({0,0,0},{0,0,2},OccluderShape::Obb,900));frame.surface_lighting.counts[0]=1;frame.surface_lighting.lights[0].occluder_range_flags={0,1,1,0};
        std::string diagnostic;try{renderer.render(frame);}catch(const std::runtime_error& error){diagnostic=error.what();}require(diagnostic=="Injected GPU frame buffer creation failure","Occluder buffer fault did not fire at allocation four");
        std::array<std::vector<std::uint8_t>,2> bytes;for(std::size_t i=0;i<2;++i){const auto path=output/("buffer-initial-recovery-slot"+std::to_string(i)+".png");renderer.render(frame,path);
            bytes[renderer.frame_telemetry().frame_slot]=read_bytes(path);require(renderer.frame_telemetry().occluders==1&&renderer.frame_telemetry().occluder_upload_bytes==64,"Recovered initial occluder telemetry mismatch");}
        require(bytes[0]==bytes[1],"Initial occluder storage recovery was not byte-exact across both slots");initial={{"allocation_fault",4},{"diagnostic",diagnostic},{"same_renderer_recovered",true},{"cross_slot_byte_exact",true}};
    }
    RendererOptions options;options.backend=Backend::Direct3D12;options.world_graph=WorldGraph::GroundForwardSlice;options.collect_telemetry=true;options.test_fail_gpu_frame_buffer_creation_at=9;
    Renderer2D renderer(window,assets,options);GameArt art(renderer);art.load(kit.assets);const std::array<std::uint8_t,4> albedo{82,96,86,255};
    const auto ground=renderer.create_sampled_texture({{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo);ScenePoints points;auto small=make_scene(renderer,art,kit,ground,false,false,points);
    small.local_occluders.push_back(blocker_between({0,0,0},{0,0,2},OccluderShape::Obb,1000));small.surface_lighting.counts[0]=1;small.surface_lighting.lights[0].occluder_range_flags={0,1,1,0};
    std::array<std::vector<std::uint8_t>,2> primed;for(std::size_t i=0;i<2;++i){const auto path=output/("buffer-growth-prime-slot"+std::to_string(i)+".png");renderer.render(small,path);primed[renderer.frame_telemetry().frame_slot]=read_bytes(path);}
    require(primed[0]==primed[1],"Growth fixture did not prime byte-identical slot contents");auto large=small;
    for(std::uint32_t id=1001;id<1065;++id){OccluderGpu extra;extra.center_base={20,20,0,0};extra.axis_x_extent={1,0,.25f,1};extra.axis_y_radius={0,1,.25f,0};extra.shape_flags_id={1,1,id,999};large.local_occluders.push_back(extra);}
    require(large.local_occluders.size()==65&&large.local_occluders.size()*sizeof(OccluderGpu)==4160,"Occluder-only growth fixture size changed");
    const auto command_count=large.commands.size(),vertex_count=large.vertices.size(),index_count=large.indices.size(),surface_count=large.surface_vertices.size(),occluder_count=large.local_occluders.size();
    std::string diagnostic;try{renderer.render(large);}catch(const std::runtime_error& error){diagnostic=error.what();}
    const auto failed_telemetry=renderer.frame_telemetry();
    require(diagnostic=="Injected GPU frame buffer creation failure"&&!failed_telemetry.submitted&&
            failed_telemetry.graph_pass_count==0&&failed_telemetry.draws==0&&failed_telemetry.vertices==0&&failed_telemetry.occluders==0&&
            large.commands.size()==command_count&&large.vertices.size()==vertex_count&&large.indices.size()==index_count&&
            large.surface_vertices.size()==surface_count&&large.local_occluders.size()==occluder_count,
            "Occluder-only growth fault encoded a rendered frame or mutated the source command payload");
    std::array<std::vector<std::uint8_t>,2> retained;for(std::size_t i=0;i<2;++i){const auto path=output/("buffer-growth-retained-render"+std::to_string(i)+".png");renderer.render(small,path);
        retained[renderer.frame_telemetry().frame_slot]=read_bytes(path);require(renderer.frame_telemetry().occluder_upload_bytes==64,"Retained small frame upload ledger changed");}
    require(retained==primed,"Failed occluder-only growth changed old buffer pixels in one or both slots");
    std::array<std::vector<std::uint8_t>,2> grown;for(std::size_t i=0;i<2;++i){const auto path=output/("buffer-growth-retry-render"+std::to_string(i)+".png");renderer.render(large,path);
        grown[renderer.frame_telemetry().frame_slot]=read_bytes(path);require(renderer.frame_telemetry().occluders==65&&renderer.frame_telemetry().occluder_upload_bytes==4160,"Successful occluder growth telemetry mismatch");}
    require(grown[0]==grown[1],"Successful occluder-only growth differed across frame slots");
    return {{"initial_allocation",initial},{"growth",{{"allocation_fault",9},{"small_occluders",1},{"small_bytes",64},{"large_occluders",65},{"large_bytes",4160},
        {"diagnostic",diagnostic},{"failed_rendered_frame_submitted",false},{"failed_rendered_frame_telemetry_empty",true},{"source_command_payload_unchanged",true},
        {"old_slots_byte_exact_after_failure",true},{"large_retry_cross_slot_byte_exact",true}}}};
}
} // namespace

int main(int argc,char** argv){
    try{
        require(argc==3,"Usage: LocalOcclusionProbe <fresh-kit-root> <NEW-output-root>");const auto kit_root=from_utf8(argv[1]),output=from_utf8(argv[2]);
        require(!std::filesystem::exists(output),"Output already exists; retained evidence is never reused or overwritten");Kit kit(kit_root);
        require(std::filesystem::create_directories(output),"Output was concurrently created; refusing to reuse it");require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed");struct Quit{~Quit(){SDL_Quit();}} quit;
        const auto* base=SDL_GetBasePath();require(base,"Runtime base path unavailable");const auto assets=from_utf8(base)/"assets";
        Json production;{
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Ribterra local occlusion production probe",logical.width,logical.height,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);require(window!=nullptr,"Production window unavailable");
            RendererOptions options;options.backend=Backend::Direct3D12;options.world_graph=WorldGraph::GroundForwardSlice;options.collect_telemetry=true;Renderer2D renderer(window.get(),assets,options);renderer.set_logical_size(logical);
            GameArt art(renderer);art.load(kit.assets);const std::array<std::uint8_t,4> albedo{82,96,86,255};const auto ground=renderer.create_sampled_texture({{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo);
            const auto matrix=causal_matrix(renderer,art,kit,ground,output);const auto owner=owner_and_controls(renderer,art,kit,ground,output);
            const auto before=renderer.ground_forward_status();require(SDL_SetWindowSize(window.get(),960,540),"Occlusion resize request failed");SDL_SyncWindow(window.get());
            ScenePoints points;auto frame=make_scene(renderer,art,kit,ground,false,false,points);frame.local_occluders.push_back(blocker_between({0,0,0},{0,0,2},OccluderShape::Obb,990));
            const auto resized=capture(renderer,std::move(frame),output/"occlusion-resize.png",points.slope);const auto after=renderer.ground_forward_status();
            require(after.target_generation==before.target_generation+1&&after.active_set_ids!=before.active_set_ids&&after.candidate_live_textures==0,"Occlusion resize did not replace both target sets transactionally");
            production={{"backend",renderer.device_info().backend},{"device",renderer.device_info().device},{"driver",renderer.device_info().driver},{"matrix",matrix},{"owner_and_controls",owner},
                {"resize",{{"before",status_json(before)},{"after",status_json(after)},{"sample",sample_json(resized)}}}};
        }
        Json cpu;{
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Occlusion CPU validation",320,180,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);require(window!=nullptr,"CPU validation window unavailable");cpu=cpu_rejections(window.get(),assets);
        }
        Json legacy;{
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Occlusion legacy rejection",320,180,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);require(window!=nullptr,"Legacy validation window unavailable");legacy=legacy_rejection(window.get(),assets);
        }
        Json fault;{
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Occlusion buffer fault",640,360,SDL_WINDOW_HIGH_PIXEL_DENSITY),SDL_DestroyWindow);require(window!=nullptr,"Fault window unavailable");fault=buffer_fault(window.get(),assets,kit,output);
        }
        const auto executable=from_utf8(base)/"LocalOcclusionProbe.exe";write_json(output/"local-occlusion-report.json",{{"schema",1},{"complete",true},{"build",RIBTERRA_PROBE_BUILD},
            {"probe_executable_sha256",sha256(read_bytes(executable))},{"source_kit",{{"content_sha256",kit.assets.content_hash()},{"manifest_sha256",sha256(read_bytes(kit_root/"manifest.json"))},
             {"world_sha256",sha256(read_bytes(kit_root/"art/world.json"))},{"editable_master_sha256",sha256(read_bytes(kit.master_path))}}},
            {"production_shader_manifest_sha256",sha256(read_bytes(assets/"shaders/manifest.json"))},{"production",production},{"cpu_rejections",cpu},{"legacy_rejection",legacy},{"buffer_fault",fault},
            {"claims",{"bounded local visibility","authored OBB and analytic capsule blocking","receiver-owner self exclusion","receiver-mask routing","same-renderer occluder-buffer recovery"}},
            {"not_claimed",{"sun shadows","contact shadows","complete E01-E17 acceptance","full GameView integration","occlusion acceleration","GPU timing","FPS","desktop scan-out"}}});
        std::cout<<"Production local occlusion, ownership, controls, and recovery passed; evidence retained in "<<utf8(output)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

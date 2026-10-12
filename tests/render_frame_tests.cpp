#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace ribterra::render;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void quad(Frame2D& frame, Rect rect, Color color, bool clip = false) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    for (auto p : {Point{rect.x, rect.y}, Point{rect.x + rect.w, rect.y},
                   Point{rect.x + rect.w, rect.y + rect.h}, Point{rect.x, rect.y + rect.h}})
        frame.vertices.push_back({p, {}, color});
    for (auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(first_vertex + index);
    GeometryDraw draw{}; draw.first_index = first_index; draw.index_count = 6;
    draw.scissor_enabled = clip; draw.scissor = {300, 200, 50, 50};
    frame.commands.emplace_back(draw);
}
void textured_quad(Frame2D& frame, Rect rect, TextureHandle texture, Blend blend, Point uv_offset = {}) {
    const auto first_vertex=static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index=static_cast<std::uint32_t>(frame.indices.size());
    for(auto vertex:{Vertex{{rect.x,rect.y},{uv_offset.x,uv_offset.y},{255,255,255,255}},
                     Vertex{{rect.x+rect.w,rect.y},{1+uv_offset.x,uv_offset.y},{255,255,255,255}},
                     Vertex{{rect.x+rect.w,rect.y+rect.h},{1+uv_offset.x,1+uv_offset.y},{255,255,255,255}},
                     Vertex{{rect.x,rect.y+rect.h},{uv_offset.x,1+uv_offset.y},{255,255,255,255}}})frame.vertices.push_back(vertex);
    for(auto index:{0u,1u,2u,0u,2u,3u})frame.indices.push_back(first_vertex+index);
    GeometryDraw draw{};draw.texture=texture;draw.blend=blend;draw.first_index=first_index;draw.index_count=6;
    frame.commands.emplace_back(draw);
}
void pixel(SDL_Surface* surface, SDL_Window* window, const Renderer2D& renderer, Point logical, Color expected) {
    auto point = renderer.logical_to_window(logical);
    int width{}, height{}; SDL_GetWindowSize(window, &width, &height);
    point.x *= static_cast<float>(surface->w) / width;
    point.y *= static_cast<float>(surface->h) / height;
    Uint8 r{}, g{}, b{}, a{};
    check(SDL_ReadSurfacePixel(surface, static_cast<int>(point.x), static_cast<int>(point.y), &r, &g, &b, &a), "capture pixel is unavailable");
    const auto close = [](int lhs, int rhs) { return std::abs(lhs - rhs) <= 2; };
    if (!(close(r, expected.r) && close(g, expected.g) && close(b, expected.b) && close(a, expected.a))) {
        throw std::runtime_error("pixel contract at " + std::to_string(logical.x) + "," + std::to_string(logical.y) +
            " expected " + std::to_string(expected.r) + "," + std::to_string(expected.g) + "," + std::to_string(expected.b) + "," + std::to_string(expected.a) +
            " got " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a));
    }
}
void gpu_buffer_growth_recovery(SDL_Window* window,const std::filesystem::path& base) {
    check(SDL_ShowWindow(window),"GPU buffer recovery window could not be shown");
    SDL_PumpEvents();
    RendererOptions options;
    options.presentation.frames_in_flight=1;
    options.collect_telemetry=true;
    // Initial vertex/index creations consume 1 and 2; fail the index growth
    // after a new, larger vertex buffer has already been staged (creation 3).
    options.test_fail_gpu_frame_buffer_creation_at=4;
    Renderer2D renderer(window,base/"assets",options);
    Frame2D small;
    quad(small,{20,20,64,64},{210,40,70,255});
    renderer.render(small,base/"gpu-buffer-initial.png");
    check(renderer.frame_telemetry().presentation_queued,
          "GPU buffer recovery fixture did not acquire a visible swapchain");
    auto large=small;
    for(int i=0;i<256;++i)quad(large,{20,20,64,64},{210,40,70,255});
    bool injected=false;
    const auto recovered_path=base/"gpu-buffer-growth-recovered.png";
    try{renderer.render(large,recovered_path);}
    catch(const std::runtime_error& error){
        injected=std::string_view(error.what()).find("Injected GPU frame buffer creation failure")!=std::string_view::npos;
    }
    check(injected,"GPU buffer growth fault did not exercise staged-allocation rollback");
    const auto check_capture=[&](const std::filesystem::path& path){
        const auto name=path.u8string();
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> captured(
            SDL_LoadPNG(reinterpret_cast<const char*>(name.c_str())),SDL_DestroySurface);
        check(captured!=nullptr,"GPU buffer recovery capture could not be decoded");
        pixel(captured.get(),window,renderer,{32,32},{210,40,70,255});
    };
    // The original capacity and contents must remain usable after failed growth.
    const auto retained_path=base/"gpu-buffer-retained.png";
    renderer.render(small,retained_path);
    check_capture(retained_path);
    // The same growth must then retry successfully, not retain poisoned capacity.
    renderer.render(large,recovered_path);
    check_capture(recovered_path);
}
void ground_forward_contract(SDL_Window* window,const std::filesystem::path& base) {
    check(SDL_ShowWindow(window),"Ground-forward window could not be shown");
    SDL_PumpEvents();
    {
    RendererOptions options;
    options.world_graph=WorldGraph::GroundForwardSlice;
    options.collect_telemetry=true;
    Renderer2D renderer(window,base/"assets",options);
    check(renderer.ground_forward_status().available,
          "GroundForwardSlice programs or exact formats are unavailable");
    const std::vector<std::uint8_t> opaque{180,150,110,255};
    const std::vector<std::uint8_t> cutout{180,150,110,254};
    const auto albedo=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::Rgba8UnormSrgb},opaque);
    const auto cutout_albedo=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::Rgba8UnormSrgb},cutout);
    const std::array<std::uint8_t,4> normal_bytes{128,128,255,255};
    const std::array<std::uint8_t,2> depth_bytes{0,0};
    const std::array<std::uint8_t,1> zero_byte{0};
    const std::array<std::uint8_t,4> surface_bytes{128,128,255,255};
    const auto normal=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::Rgba8Unorm},normal_bytes);
    const auto depth=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::R16Unorm},depth_bytes);
    const auto emission=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::R8Unorm},zero_byte);
    const auto surface=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::Rgba8Unorm},surface_bytes);
    const auto caster=renderer.create_sampled_texture(
        {{1,1},SampledTextureFormat::R8Unorm},zero_byte);
    SpriteMaterialDesc sprite_desc;
    sprite_desc.channels={MaterialTextureBinding{albedo,{}},MaterialTextureBinding{normal,{}},
        MaterialTextureBinding{depth,{}},MaterialTextureBinding{emission,{}},
        MaterialTextureBinding{surface,{}},MaterialTextureBinding{caster,{}}};
    for(auto& channel:sprite_desc.channels)channel.uv={0,0,1,1};
    const auto sprite_material=renderer.create_sprite_material(sprite_desc);
    Frame2D frame;
    frame.logical_size={1440,900};frame.linear_hdr_world=true;
    quad(frame,{0,0,1440,900},{255,255,255,255});
    auto& ground=std::get<GeometryDraw>(frame.commands.front());
    ground.pass=Pass::Terrain;ground.blend=Blend::Opaque;ground.ground_material_index=1;
    frame.surface_vertices.resize(frame.vertices.size());
    for(std::size_t index=0;index<frame.surface_vertices.size();++index)
        frame.surface_vertices[index].world_position={frame.vertices[index].position.x/100.f,
                                                       frame.vertices[index].position.y/100.f,0.f};
    frame.ground_material_draws.push_back({albedo,{}});
    renderer.render(frame);
    const auto initial=renderer.ground_forward_status();
    check(initial.width>0&&initial.height>0&&initial.frame_sets==2&&
              initial.attachment_bytes==std::uint64_t(initial.width)*initial.height*72u&&
              initial.creation_attempts==14&&initial.candidate_live_textures==0&&
              initial.active_set_ids[0]!=0&&initial.active_set_ids[0]!=initial.active_set_ids[1],
          "GroundForwardSlice target ownership/status contract failed");
    check(renderer.frame_telemetry().frame_slot==0&&renderer.frame_telemetry().graph_pass_count==5&&
              renderer.frame_telemetry().ground_draws==1&&renderer.frame_telemetry().material_draws==0&&
              renderer.frame_telemetry().graph_passes==std::array{
                  WorldGraphPass::GroundGBuffer,WorldGraphPass::GroundLighting,
                  WorldGraphPass::WorldForward,WorldGraphPass::Composite,WorldGraphPass::Ui,
                  WorldGraphPass{},WorldGraphPass{},WorldGraphPass{}},
          "GroundForwardSlice pass telemetry contract failed");
    renderer.render(frame);
    check(renderer.frame_telemetry().frame_slot==1,
          "GroundForwardSlice did not alternate its two frame sets");
    auto boundary=frame;
    boundary.surface_vertices.front().world_position[0]=32.f;
    quad(boundary,{10,10,1,1},{255,255,255,255});
    auto& sprite=std::get<GeometryDraw>(boundary.commands.back());
    sprite.pass=Pass::WorldOrdered;sprite.blend=Blend::Alpha;sprite.sprite_material_index=1;
    boundary.surface_vertices.resize(boundary.vertices.size());
    SpriteMaterialDraw boundary_sprite;
    boundary_sprite.material=sprite_material;
    boundary_sprite.surface.anchor_export={32.f,0.f,0.f,64.f};
    boundary_sprite.surface.source_size_pivot={1.e-6f,1.e-6f,1.e-6f,1.e-6f};
    boundary_sprite.surface.depth_scale={-1.e-6f,0.f,1.f,0.f};
    boundary.sprite_material_draws.push_back(boundary_sprite);
    renderer.render(boundary);
    check(renderer.frame_telemetry().material_draws==1,
          "GroundForwardSlice rejected boundary-valid WorldForward metadata reconstruction");

    const auto reject=[&](auto mutate,const char* message){
        auto invalid=frame;mutate(invalid);bool failed=false;
        try{renderer.render(invalid);}catch(const std::invalid_argument&){failed=true;}
        check(failed,message);
    };
    reject([](auto& value){value.linear_hdr_world=false;},
           "GroundForwardSlice accepted a non-HDR frame");
    reject([](auto& value){std::get<GeometryDraw>(value.commands.front()).ground_material_index=0;},
           "GroundForwardSlice accepted legacy terrain");
    reject([](auto& value){std::get<GeometryDraw>(value.commands.front()).index_count=0;},
           "GroundForwardSlice accepted an empty command as its required ground draw");
    reject([](auto& value){std::get<GeometryDraw>(value.commands.front()).surface_lit=true;},
           "GroundForwardSlice accepted mixed ground/surface roles");
    reject([](auto& value){value.ground_material_draws.front().surface.response[0]=
                               std::numeric_limits<float>::quiet_NaN();},
           "GroundForwardSlice accepted non-finite ground metadata");
    reject([](auto& value){value.surface_vertices.front().world_position[0]=33.f;},
           "GroundForwardSlice accepted ground geometry outside the camera-relative bound");
    reject([](auto& value){value.surface_vertices.front().world_position[0]=
                               std::numeric_limits<float>::quiet_NaN();},
           "GroundForwardSlice accepted non-finite ground geometry");
    reject([](auto& value){
        SpriteMaterialDraw metadata{};
        metadata.surface.anchor_export[3]=1.f;
        metadata.surface.source_size_pivot={1.f,1.f,.5f,.5f};
        metadata.surface.depth_scale={0.f,1.f,1.f,0.f};
        metadata.surface.response={1.f,0.f,0.f,0.f};
        metadata.reveal=std::numeric_limits<float>::quiet_NaN();
        value.sprite_material_draws.push_back(metadata);
    },"GroundForwardSlice accepted non-finite forward reveal metadata");
    reject([](auto& value){
        SpriteMaterialDraw metadata{};
        metadata.surface.anchor_export={33.f,0.f,0.f,64.f};
        metadata.surface.source_size_pivot={1.e-6f,1.e-6f,1.e-6f,1.e-6f};
        metadata.surface.depth_scale={-1.e-6f,0.f,1.f,0.f};
        value.sprite_material_draws.push_back(metadata);
    },"GroundForwardSlice accepted WorldForward reconstruction outside the camera-relative bound");
    reject([](auto& value){
        SpriteMaterialDraw metadata{};
        metadata.surface.anchor_export={std::numeric_limits<float>::quiet_NaN(),0.f,0.f,64.f};
        metadata.surface.source_size_pivot={1.e-6f,1.e-6f,1.e-6f,1.e-6f};
        metadata.surface.depth_scale={-1.e-6f,0.f,1.f,0.f};
        value.sprite_material_draws.push_back(metadata);
    },"GroundForwardSlice accepted non-finite WorldForward reconstruction metadata");
    reject([&](auto& value){value.ground_material_draws.front().albedo=cutout_albedo;},
           "GroundForwardSlice accepted cutout alpha as opaque ground albedo");
    check(renderer.ground_forward_status().active_set_ids==initial.active_set_ids&&
              renderer.ground_forward_status().candidate_live_textures==0,
          "Rejected ground frames mutated active target identity");
    (void)albedo;(void)cutout_albedo;(void)normal;(void)depth;(void)emission;
    (void)surface;(void)caster;(void)sprite_material;
    }
    std::uint32_t resize_index=0;
    for(const auto fault:{15u,21u,28u}){
        check(SDL_SetWindowSize(window,900+static_cast<int>(resize_index)*40,
                               600+static_cast<int>(resize_index)*30),
              "Ground-forward initial resize failed");
        SDL_PumpEvents();
        RendererOptions options;
        options.world_graph=WorldGraph::GroundForwardSlice;
        options.test_fail_ground_target_creation_at=fault;
        Renderer2D renderer(window,base/"assets",options);
        const std::vector<std::uint8_t> opaque{180,150,110,255};
        const auto albedo=renderer.create_sampled_texture(
            {{1,1},SampledTextureFormat::Rgba8UnormSrgb},opaque);
        Frame2D frame;
        frame.linear_hdr_world=true;
        quad(frame,{0,0,1440,900},{255,255,255,255});
        auto& ground=std::get<GeometryDraw>(frame.commands.front());
        ground.pass=Pass::Terrain;ground.blend=Blend::Opaque;ground.ground_material_index=1;
        frame.surface_vertices.resize(frame.vertices.size());
        frame.ground_material_draws.push_back({albedo,{}});
        renderer.render(frame);
        const auto before=renderer.ground_forward_status();
        check(SDL_SetWindowSize(window,1100+static_cast<int>(resize_index)*40,
                               700+static_cast<int>(resize_index)*30),
              "Ground-forward fault resize failed");
        SDL_PumpEvents();
        bool injected=false;
        try{renderer.render(frame);}catch(const std::runtime_error& error){
            injected=std::string_view(error.what()).find("Injected ground target creation failure")!=
                     std::string_view::npos;
        }
        check(injected,"Ground-forward resize fault injection did not fire");
        const auto failed=renderer.ground_forward_status();
        check(failed.width==before.width&&failed.height==before.height&&
                  failed.target_generation==before.target_generation&&
                  failed.active_set_ids==before.active_set_ids&&failed.candidate_live_textures==0,
              "Failed ground resize changed active target state");
        renderer.render(frame);
        const auto recovered=renderer.ground_forward_status();
        check(recovered.target_generation==before.target_generation+1&&
                  recovered.active_set_ids!=before.active_set_ids&&
                  recovered.candidate_live_textures==0,
              "Ground resize did not recover transactionally after injected failure");
        (void)albedo; // Renderer shutdown waits before releasing the submitted frame.
        ++resize_index;
    }
    check(SDL_SetWindowSize(window,1440,900),"Ground-forward window restore failed");
    SDL_PumpEvents();
}
}

int main(int argc, char** argv) {
    const bool gpu = argc > 1 && std::string(argv[1]) == "--gpu";
    try {
        Frame2D reset_frame;
        reset_frame.moon_lighting = MoonLightingGpu{};
        reset_frame.contact_projection = ContactProjection{};
        reset_frame.sky_shadow_triangles.push_back({});
        reset_frame.clear_commands();
        check(!reset_frame.moon_lighting && !reset_frame.contact_projection && reset_frame.sky_shadow_triangles.empty(),
              "Cleared frame retained independent moon/contact/sky geometry state");
        check(texture_mip_level_count({4096,4096})==1,"textures must default to the base mip level");
        check(texture_mip_level_count({4096,4096},{99})==4,"texture mip levels were not capped at four");
        check(texture_mip_level_count({1,1},{4})==1,"1px texture exposed unavailable mip levels");
        check(texture_mip_level_count({2,1},{4})==2,"2px texture mip count differs from its dimensions");
        check(texture_mip_level_count({3,1},{4})==2,"non-power-of-two texture mip count was rounded up");
        check(texture_mip_level_count({4,1},{4})==3,"4px texture mip count differs from its dimensions");
        check(texture_mip_level_count({8,1},{4})==4,"opt-in texture did not expose four mip levels");
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window* window = SDL_CreateWindow("Ribterra renderer contract", 1440, 900, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        check(window != nullptr, "window creation failed");
        {
            const auto base = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
            if(gpu){
                check(SDL_ShowWindow(window),"GPU recovery window could not be shown");
                SDL_PumpEvents();
                Renderer2D recovery_renderer(window,base/"assets",{Backend::Direct3D12,false,3});
                Frame2D recovery_frame;
                recovery_frame.commands.emplace_back(TextDraw{Pass::Ui,"GPU text rollback",{20,20},18,{255,255,255,255},0});
                // Hidden/minimized windows deliberately skip normal GPU work.
                // A requested offscreen capture explicitly exercises the upload path.
                const auto recovery_capture=base/"gpu-text-recovery.png";
                for(int attempt=0;attempt<2;++attempt){
                    // Acquired swapchains must be submitted even on recovery;
                    // an already uploaded text key can legitimately remain cached.
                    std::get<TextDraw>(recovery_frame.commands.front()).text=
                        "GPU text recovery " + std::to_string(attempt);
                    bool injected=false;
                    try{recovery_renderer.render(recovery_frame,recovery_capture);}catch(const std::runtime_error& error){injected=std::string_view(error.what()).find("Injected GPU text upload failure")!=std::string_view::npos;}
                    check(injected,"GPU text upload fault did not roll back for a retry");
                }
                // One injection remains armed: these must be genuine cache hits,
                // not silently successful re-uploads after the counter reaches 0.
                for(int retained=0;retained<2;++retained){
                    std::get<TextDraw>(recovery_frame.commands.front()).text=
                        "GPU text recovery " + std::to_string(retained);
                    recovery_renderer.render(recovery_frame,base/("gpu-text-retained-"+std::to_string(retained)+".png"));
                }
                std::get<TextDraw>(recovery_frame.commands.front()).text="GPU text recovery fresh";
                bool final_injected=false;
                try{recovery_renderer.render(recovery_frame,recovery_capture);}
                catch(const std::runtime_error& error){final_injected=std::string_view(error.what()).find("Injected GPU text upload failure")!=std::string_view::npos;}
                check(final_injected,"Retained GPU text unexpectedly consumed a new upload");
                recovery_renderer.render(recovery_frame,recovery_capture);
                const auto recovered_name=recovery_capture.u8string();
                SDL_Surface* recovered=SDL_LoadPNG(reinterpret_cast<const char*>(recovered_name.c_str()));
                check(recovered!=nullptr,"GPU recovery capture could not be decoded");
                bool glyph_visible=false;
                for(int y=20;y<70&&!glyph_visible;++y)for(int x=20;x<350&&!glyph_visible;++x){
                    Uint8 red{},green{},blue{},alpha{};
                    check(SDL_ReadSurfacePixel(recovered,x,y,&red,&green,&blue,&alpha),"GPU recovery glyph sample failed");
                    glyph_visible=red>32&&green>32&&blue>32;
                }
                SDL_DestroySurface(recovered);
                check(glyph_visible,"Text texture was uninitialized after GPU upload recovery");
                check(SDL_HideWindow(window),"GPU contract window could not be hidden again");
            }
            if(gpu){
                gpu_buffer_growth_recovery(window,base);
                check(SDL_HideWindow(window),"GPU buffer contract window could not be hidden again");
            }
            if(gpu){
                ground_forward_contract(window,base);
                check(SDL_HideWindow(window),"Ground-forward contract window could not be hidden again");
            }
            RendererOptions unsupported_options;
            unsupported_options.backend = gpu ? Backend::Direct3D12 : Backend::Software;
            unsupported_options.lighting_mode = LightingMode::Dynamic;
            bool unsupported_start_rejected=false;
            try{Renderer2D unsupported(window, base / "assets", unsupported_options);}
            catch(const std::invalid_argument&){unsupported_start_rejected=true;}
            check(unsupported_start_rejected,"explicit unsupported startup lighting silently downgraded");
            RendererOptions software_ground_options;
            software_ground_options.backend=Backend::Software;
            software_ground_options.world_graph=WorldGraph::GroundForwardSlice;
            bool software_ground_rejected=false;
            try{Renderer2D unsupported(window,base/"assets",software_ground_options);}
            catch(const std::invalid_argument&){software_ground_rejected=true;}
            check(software_ground_rejected,
                  "GroundForwardSlice startup was accepted on the software backend");
            Renderer2D renderer(window, base / "assets", {gpu ? Backend::Direct3D12 : Backend::Software, false});
            check(renderer.device_info().backend == (gpu ? "direct3d12" : "software"), "requested renderer backend was not selected");
            check(renderer.lighting_status().requested == LightingMode::Legacy &&
                  renderer.lighting_status().effective == LightingMode::Legacy &&
                  !renderer.lighting_status().dynamic_available,
                  "default lighting must preserve the existing renderer");
            check(!renderer.set_lighting_mode(LightingMode::Dynamic),
                  "unimplemented dynamic lighting was reported as enabled");
            check(renderer.lighting_status().requested == LightingMode::Dynamic &&
                  renderer.lighting_status().effective == LightingMode::Legacy &&
                  renderer.lighting_status().reason == LightingUnavailableReason::NotImplemented,
                  "unsupported lighting request must expose its effective mode and reason");
            bool invalid_lighting_rejected=false;
            try{renderer.set_lighting_mode(static_cast<LightingMode>(999));}
            catch(const std::invalid_argument&){invalid_lighting_rejected=true;}
            check(invalid_lighting_rejected && renderer.lighting_status().requested == LightingMode::Dynamic,
                  "invalid lighting request changed the last valid state");
            for (const auto [window_size, expected] : {
                     std::pair{Extent{1280, 720}, Extent{1600, 900}},
                     std::pair{Extent{1920, 1080}, Extent{1600, 900}},
                     std::pair{Extent{1600, 1000}, Extent{1440, 900}},
                     std::pair{Extent{2560, 1440}, Extent{1600, 900}}}) {
                check(SDL_SetWindowSize(window, static_cast<int>(window_size.width),
                                        static_cast<int>(window_size.height)),
                      "presentation resize failed");
                SDL_PumpEvents();
                const auto fitted = renderer.fitted_logical_size();
                check(fitted.width == expected.width && fitted.height == expected.height,
                      "fitted logical presentation differs from the drawable aspect");
                renderer.set_logical_size(fitted);
                for (const auto point : {Point{0, 0},
                                         Point{fitted.width * .5f, fitted.height * .5f},
                                         Point{float(fitted.width), float(fitted.height)}}) {
                    const auto roundtrip = renderer.window_to_logical(renderer.logical_to_window(point));
                    check(std::abs(roundtrip.x - point.x) < .1f &&
                              std::abs(roundtrip.y - point.y) < .1f,
                          "fitted presentation input did not roundtrip");
                }
                int window_w = 0, window_h = 0;
                SDL_GetWindowSize(window, &window_w, &window_h);
                const auto origin = renderer.logical_to_window({0, 0});
                const auto edge = renderer.logical_to_window(
                    {float(fitted.width), float(fitted.height)});
                check(std::abs(origin.x) < .1f && std::abs(origin.y) < .1f &&
                          std::abs(edge.x - window_w) < .1f && std::abs(edge.y - window_h) < .1f,
                      "supported presentation retained a letterbox inset");
            }
            check(SDL_SetWindowSize(window, 1440, 900), "presentation restore failed");
            SDL_PumpEvents();
            renderer.set_logical_size({1440, 900});
            const std::vector<std::uint8_t> checker{
                255,0,0,255, 255,0,0,255, 0,255,0,255, 0,255,0,255,
                255,0,0,255, 255,0,0,255, 0,255,0,255, 0,255,0,255,
                0,0,255,255, 0,0,255,255, 255,255,0,255, 255,255,0,255,
                0,0,255,255, 0,0,255,255, 255,255,0,255, 255,255,0,255};
            const auto checker_texture=renderer.create_texture({4,4},checker);
            constexpr std::uint32_t minify_size=128;
            std::vector<std::uint8_t> minify_checker(minify_size*minify_size*4u);
            for(std::uint32_t y=0;y<minify_size;++y)for(std::uint32_t x=0;x<minify_size;++x){
                const auto i=(y*minify_size+x)*4u;
                const auto value=static_cast<std::uint8_t>(((x+y)&1u)?255:0);
                minify_checker[i]=minify_checker[i+1]=minify_checker[i+2]=value;
                minify_checker[i+3]=255;
            }
            const auto minify_texture=renderer.create_texture({minify_size,minify_size},minify_checker,{4});
            const std::vector<std::uint8_t> premultiplied_pixel{128,0,0,128};
            const auto premultiplied_texture=renderer.create_texture({1,1},premultiplied_pixel);
            check(checker_texture.slot>=1&&checker_texture.generation>=1,"texture registry returned a reserved handle");
            Frame2D frame;
            quad(frame, {100, 100, 200, 200}, {255, 0, 0, 255});
            quad(frame, {150, 150, 200, 200}, {0, 255, 0, 128});
            quad(frame, {275, 175, 150, 150}, {0, 0, 255, 255}, true);
            textured_quad(frame,{900,100,160,160},checker_texture,Blend::Opaque);
            constexpr float half_texel=.5f/minify_size;
            // This case tests the GPU's clamp sampler and mip chain. Its UVs
            // intentionally extend past 1; SDL software geometry requires
            // in-range UVs and must not receive this GPU-only draw. All common
            // alpha/clip/painter-order assertions still run on both backends.
            if(gpu)textured_quad(frame,{850,300,32,32},minify_texture,Blend::Opaque,{half_texel,half_texel});
            quad(frame,{1100,100,100,100},{255,255,255,255});
            textured_quad(frame,{1100,100,100,100},premultiplied_texture,Blend::PremultipliedAlpha);
            frame.commands.emplace_back(TextDraw{Pass::Ui, "Рібтерра — GPU", {500, 100}, 18, {255, 255, 255, 255}, 0});
            quad(frame, {490, 90, 400, 70}, {0, 0, 0, 255});
            std::get<GeometryDraw>(frame.commands.back()).pass = Pass::Ui;
            const auto text_extent = renderer.measure_text("Рібтерра", 18);
            check(text_extent.x > 20 && text_extent.y > 10, "Unicode text measurement failed");
            for (const auto extent : {Extent{1440, 900}, Extent{1100, 700}}) {
                // The first capture proves a rejected Dynamic request still renders
                // correctly; the second proves returning to Legacy clears the reason.
                if(extent.width == 1100){
                    check(renderer.set_lighting_mode(LightingMode::Legacy) &&
                          renderer.lighting_status().reason == LightingUnavailableReason::None,
                          "returning to Legacy did not clear the unavailable reason");
                }
                check(SDL_SetWindowSize(window, static_cast<int>(extent.width), static_cast<int>(extent.height)), "resize failed");
                SDL_PumpEvents();
                const auto path = base / (std::string(gpu ? "gpu-contract-" : "software-contract-") + std::to_string(extent.width) + ".png");
                renderer.render(frame, path);
                auto utf8 = path.u8string();
                SDL_Surface* surface = SDL_LoadPNG(reinterpret_cast<const char*>(utf8.c_str()));
                check(surface != nullptr, "capture could not be decoded");
                int pixel_w{}, pixel_h{}; SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
                check(surface->w == pixel_w && surface->h == pixel_h, "capture has stale pre-resize pixel dimensions");
                pixel(surface, window, renderer, {120, 120}, {255, 0, 0, 255});
                pixel(surface, window, renderer, {175, 175}, {127, 128, 0, 255});
                pixel(surface, window, renderer, {325, 225}, {0, 0, 255, 255});
                pixel(surface, window, renderer, {400, 225}, {0, 0, 0, 255});
                pixel(surface, window, renderer, {905, 105}, {255, 0, 0, 255});
                pixel(surface, window, renderer, {1055, 105}, {0, 255, 0, 255});
                pixel(surface, window, renderer, {905, 255}, {0, 0, 255, 255});
                pixel(surface, window, renderer, {1055, 255}, {255, 255, 0, 255});
                if(gpu)pixel(surface, window, renderer, {866, 316}, {128, 128, 128, 255});
                pixel(surface, window, renderer, {1150, 150}, {255, 127, 127, 255});
                pixel(surface, window, renderer, {515, 110}, {0, 0, 0, 255});
                SDL_DestroySurface(surface);
                for (const auto point : {Point{0, 0}, Point{720, 450}, Point{1440, 900}}) {
                    auto actual = renderer.window_to_logical(renderer.logical_to_window(point));
                    check(std::abs(actual.x - point.x) < .1f && std::abs(actual.y - point.y) < .1f, "letterbox input mapping failed after resize");
                }
            }
            auto reject = [&](auto mutate) {
                auto invalid = frame; mutate(invalid);
                bool failed = false;
                try { renderer.render(invalid); } catch (const std::exception&) { failed = true; }
                check(failed, "invalid frame reached the render backend");
                renderer.render(frame); // Rejection must leave the renderer usable.
            };
            reject([](auto& bad) { bad.indices[0] = 999999; });
            reject([](auto& bad) { std::get<GeometryDraw>(bad.commands[0]).index_count = 5; });
            reject([](auto& bad) { std::get<GeometryDraw>(bad.commands[0]).blend = static_cast<Blend>(999); });
            reject([](auto& bad) { std::get<GeometryDraw>(bad.commands.back()).pass = Pass::Terrain; });
            reject([](auto& bad) { bad.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN(); });
            auto stale_frame=Frame2D{};
            textured_quad(stale_frame,{0,0,10,10},checker_texture,Blend::Opaque);
            renderer.release_texture(checker_texture);
            bool stale_rejected=false;
            try{renderer.render(stale_frame);}catch(const std::invalid_argument&){stale_rejected=true;}
            check(stale_rejected,"released texture handle was accepted");
            const std::vector<std::uint8_t> white{255,255,255,255};
            const auto replacement=renderer.create_texture({1,1},white);
            check(replacement.slot==checker_texture.slot&&replacement.generation!=checker_texture.generation,
                  "reused texture slot did not advance its generation");
            bool double_release_rejected=false;
            try{renderer.release_texture(checker_texture);}catch(const std::invalid_argument&){double_release_rejected=true;}
            check(double_release_rejected,"stale texture release was accepted");
            bool invalid_bytes_rejected=false;
            try{renderer.create_texture({2,2},white);}catch(const std::invalid_argument&){invalid_bytes_rejected=true;}
            check(invalid_bytes_rejected,"invalid RGBA8 byte count was accepted");
            bool invalid_extent_rejected=false;
            try{renderer.create_texture({0,1},{});}catch(const std::invalid_argument&){invalid_extent_rejected=true;}
            check(invalid_extent_rejected,"zero texture extent was accepted");
            renderer.release_texture(premultiplied_texture);
            renderer.release_texture(minify_texture);
            Frame2D clean_frame;renderer.render(clean_frame);
            (void)replacement; // Intentionally left live to verify renderer-owned cleanup.
            std::cout << "Renderer contract passed: " << renderer.device_info().backend << " / " << renderer.device_info().device << '\n';
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << ": " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace ribterra::render;

void check(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

std::uint32_t append_quad_data(Frame2D& frame, Rect rect, Color color = {255, 255, 255, 255}) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto point : {Point{rect.x, rect.y}, Point{rect.x + rect.w, rect.y},
                             Point{rect.x + rect.w, rect.y + rect.h}, Point{rect.x, rect.y + rect.h}})
        frame.vertices.push_back({point, {}, color});
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u})
        frame.indices.push_back(first_vertex + index);
    return first_index;
}

void append_quad(Frame2D& frame, GeometryDraw draw, Rect rect) {
    draw.first_index = append_quad_data(frame, rect);
    draw.index_count = 6;
    frame.commands.emplace_back(draw);
}

void expect_draws(Renderer2D& renderer, const Frame2D& frame, std::uint64_t expected,
                  std::string_view message) {
    renderer.render(frame);
    const auto& telemetry = renderer.frame_telemetry();
    check(telemetry.measured, "batch telemetry was not measured");
    check(telemetry.draws == expected, message);
}
}

int main(int argc,char** argv) {
    const bool gpu=argc>1&&std::string_view(argv[1])=="--gpu";
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window* window = SDL_CreateWindow("Ribterra render batching", 320, 240, SDL_WINDOW_HIDDEN);
        check(window != nullptr, "window creation failed");
        {
            const auto base = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
            RendererOptions options;
            options.backend = Backend::Software;
            options.collect_telemetry = true;
            Renderer2D renderer(window, base / "assets", options);

            Frame2D adjacent;
            append_quad(adjacent, {}, {0, 0, 20, 20});
            append_quad(adjacent, {}, {20, 0, 20, 20});
            expect_draws(renderer, adjacent, 1, "adjacent compatible draws were not batched");
            check(renderer.frame_telemetry().indices == 12,
                  "batching changed the prepared index stream");

            Frame2D pass_barrier;
            GeometryDraw world{};
            world.pass = Pass::WorldOrdered;
            GeometryDraw atmosphere = world;
            atmosphere.pass = Pass::Atmosphere;
            append_quad(pass_barrier, world, {0, 0, 20, 20});
            append_quad(pass_barrier, atmosphere, {20, 0, 20, 20});
            expect_draws(renderer, pass_barrier, 2, "batching crossed a render-pass barrier");

            Frame2D blend_barrier;
            GeometryDraw alpha{};
            GeometryDraw additive = alpha;
            additive.blend = Blend::Additive;
            append_quad(blend_barrier, alpha, {0, 0, 20, 20});
            append_quad(blend_barrier, additive, {20, 0, 20, 20});
            expect_draws(renderer, blend_barrier, 2, "batching crossed a blend barrier");

            const std::vector<std::uint8_t> white{255, 255, 255, 255};
            const auto texture_a = renderer.create_texture({1, 1}, white);
            const auto texture_b = renderer.create_texture({1, 1}, white);
            Frame2D texture_barrier;
            GeometryDraw textured_a{};
            textured_a.texture = texture_a;
            GeometryDraw textured_b = textured_a;
            textured_b.texture = texture_b;
            append_quad(texture_barrier, textured_a, {0, 0, 20, 20});
            append_quad(texture_barrier, textured_b, {20, 0, 20, 20});
            expect_draws(renderer, texture_barrier, 2, "batching crossed a texture-handle barrier");

            Frame2D scissor_barrier;
            GeometryDraw clipped{};
            clipped.scissor_enabled = true;
            clipped.scissor = {0, 0, 30, 30};
            Frame2D matching_scissor;
            append_quad(matching_scissor, clipped, {0, 0, 20, 20});
            append_quad(matching_scissor, clipped, {20, 0, 20, 20});
            expect_draws(renderer, matching_scissor, 1,
                         "identical enabled scissors prevented compatible batching");

            GeometryDraw other_clip = clipped;
            other_clip.scissor = {1, 0, 30, 30};
            GeometryDraw unclipped = clipped;
            unclipped.scissor_enabled = false;
            append_quad(scissor_barrier, clipped, {0, 0, 20, 20});
            append_quad(scissor_barrier, other_clip, {20, 0, 20, 20});
            append_quad(scissor_barrier, unclipped, {40, 0, 20, 20});
            expect_draws(renderer, scissor_barrier, 3, "batching crossed a scissor barrier");

            Frame2D noncontiguous;
            GeometryDraw first{};
            first.first_index = append_quad_data(noncontiguous, {0, 0, 20, 20});
            first.index_count = 6;
            noncontiguous.commands.emplace_back(first);
            (void)append_quad_data(noncontiguous, {100, 100, 1, 1});
            GeometryDraw second{};
            second.first_index = append_quad_data(noncontiguous, {20, 0, 20, 20});
            second.index_count = 6;
            noncontiguous.commands.emplace_back(second);
            expect_draws(renderer, noncontiguous, 2, "batching crossed a noncontiguous index range");

            Frame2D text_barrier;
            GeometryDraw ui{};
            ui.pass = Pass::Ui;
            append_quad(text_barrier, ui, {0, 0, 20, 20});
            text_barrier.commands.emplace_back(
                TextDraw{Pass::Ui, "batch barrier", {2, 2}, 12, {255, 255, 255, 255}, 0});
            append_quad(text_barrier, ui, {20, 0, 20, 20});
            expect_draws(renderer, text_barrier, 3, "batching crossed a text barrier");

            renderer.release_texture(texture_a);
            renderer.release_texture(texture_b);
        }
        if(gpu){
            check(argc==3,"Usage: RenderBatchTests --gpu <new-output-directory>");
            const auto output=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(argv[2])));
            check(!std::filesystem::exists(output),"Refusing to overwrite native batch evidence");
            std::filesystem::create_directories(output);
            const auto base=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
            RendererOptions options;
            options.collect_telemetry=true;
            Renderer2D renderer(window,base/"assets",options);
            Frame2D surfaces;
            surfaces.logical_size={320,240};
            GeometryDraw lit{};
            lit.pass=Pass::Terrain;
            lit.blend=Blend::Opaque;
            lit.surface_lit=true;
            auto legacy=lit;
            legacy.surface_lit=false;
            append_quad(surfaces,lit,{0,0,40,40});
            append_quad(surfaces,legacy,{40,0,40,40});
            append_quad(surfaces,lit,{80,0,40,40});
            append_quad(surfaces,lit,{120,0,40,40});
            surfaces.surface_vertices.resize(surfaces.vertices.size());
            renderer.render(surfaces,output/"surface-batch-barriers.png");
            check(renderer.frame_telemetry().draws==3&&renderer.frame_telemetry().surface_draws==2,
                  "Native batching crossed lit/legacy barriers or failed to merge compatible lit draws");
        }
        if(gpu){
            check(SDL_ShowWindow(window),"Ground batching window could not be shown");
            SDL_PumpEvents();
            const auto base=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
            RendererOptions options;
            options.world_graph=WorldGraph::GroundForwardSlice;
            options.collect_telemetry=true;
            Renderer2D renderer(window,base/"assets",options);
            const std::vector<std::uint8_t> albedo_bytes{190,160,110,255};
            const auto albedo=renderer.create_sampled_texture(
                {{1,1},SampledTextureFormat::Rgba8UnormSrgb},albedo_bytes);
            const auto make_frame=[&](std::uint32_t second_material){
                Frame2D frame;
                frame.logical_size={320,240};frame.linear_hdr_world=true;
                GeometryDraw ground{};ground.pass=Pass::Terrain;ground.blend=Blend::Opaque;
                ground.ground_material_index=1;
                append_quad(frame,ground,{0,0,160,240});
                ground.ground_material_index=second_material;
                append_quad(frame,ground,{160,0,160,240});
                frame.surface_vertices.resize(frame.vertices.size());
                for(std::size_t index=0;index<frame.surface_vertices.size();++index)
                    frame.surface_vertices[index].world_position={frame.vertices[index].position.x/100.f,
                                                                   frame.vertices[index].position.y/100.f,0.f};
                frame.ground_material_draws={{albedo,{}},{albedo,{}}};
                return frame;
            };
            renderer.render(make_frame(1));
            check(renderer.frame_telemetry().ground_draws==1,
                  "Adjacent ground draws with the same material index were not batched");
            renderer.render(make_frame(2));
            check(renderer.frame_telemetry().ground_draws==2,
                  "Ground batching crossed a ground-material-index barrier");
            (void)albedo; // Renderer shutdown waits before releasing the submitted frame.
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Renderer batching contract passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << ": " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

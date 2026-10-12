#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace {
using namespace ribterra::render;
using Clock = std::chrono::steady_clock;

struct Pixel {
    int r{};
    int g{};
    int b{};
    int a{};
};

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void add_surface_quad(Frame2D& frame, const std::array<Point, 4>& screen,
                      Color color,
                      const std::array<std::array<float, 3>, 4>& world_positions,
                      std::array<float, 3> normal = {0.f, 0.f, 1.f},
                      Pass pass = Pass::Terrain, Blend blend = Blend::Opaque,
                      bool surface_lit = true) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    for (std::size_t index = 0; index < screen.size(); ++index) {
        frame.vertices.push_back({screen[index], {}, color});
        frame.surface_vertices.push_back({world_positions[index], normal});
    }
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u})
        frame.indices.push_back(first_vertex + index);
    GeometryDraw draw{};
    draw.pass = pass;
    draw.blend = blend;
    draw.first_index = first_index;
    draw.index_count = 6;
    draw.surface_lit = surface_lit;
    frame.commands.emplace_back(draw);
}

void add_quad(Frame2D& frame, Rect screen, Color color,
              std::array<float, 3> world_position,
              std::array<float, 3> normal = {0.f, 0.f, 1.f},
              Pass pass = Pass::Terrain, Blend blend = Blend::Opaque,
              bool surface_lit = true) {
    const std::array<Point, 4> points{{
        {screen.x, screen.y}, {screen.x + screen.w, screen.y},
        {screen.x + screen.w, screen.y + screen.h}, {screen.x, screen.y + screen.h}}};
    add_surface_quad(frame, points, color,
                     {world_position, world_position, world_position, world_position},
                     normal, pass, blend, surface_lit);
}

Frame2D receiver_frame(Rect screen = {220.f, 160.f, 200.f, 160.f},
                       std::array<float, 3> world = {0.f, 0.f, 0.f},
                       Color color = {255, 255, 255, 255}) {
    Frame2D frame;
    frame.logical_size = {640, 480};
    frame.clear = {8, 10, 14, 255};
    frame.surface_lighting.ambient = {0.f, 0.f, 0.f, 0.f};
    frame.surface_lighting.sun_color_intensity = {1.f, 1.f, 1.f, 0.f};
    add_quad(frame, screen, color, world);
    return frame;
}

LightGpu point_light(std::array<float, 3> position, float radius,
                     std::array<float, 3> color, float intensity) {
    LightGpu light;
    light.anchor_radius = {position[0], position[1], position[2], radius};
    light.direction_cone = {0.f, 0.f, -1.f, -1.f};
    light.color_intensity = {color[0], color[1], color[2], intensity};
    light.occluder_range_flags = {0u, 0u, 1u, 0u};
    light.height_falloff_reveal = {-1.f, 2.f, 1.f, 0.f};
    return light;
}

LightGpu spot_light(std::array<float, 3> position,
                    std::array<float, 3> direction, float radius,
                    std::array<float, 3> color, float intensity,
                    float outer_cosine, float inner_cosine) {
    auto light = point_light(position, radius, color, intensity);
    light.direction_cone = {direction[0], direction[1], direction[2], outer_cosine};
    light.height_falloff_reveal[0] = inner_cosine;
    return light;
}

std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>
load_capture(const std::filesystem::path& path) {
    auto* surface = SDL_LoadPNG(utf8(path).c_str());
    require(surface != nullptr, std::string("Could not decode capture: ") + SDL_GetError());
    return {surface, SDL_DestroySurface};
}

Pixel sample(SDL_Surface* surface, SDL_Window* window, const Renderer2D& renderer,
             Point logical) {
    auto point = renderer.logical_to_window(logical);
    int window_width = 1;
    int window_height = 1;
    SDL_GetWindowSize(window, &window_width, &window_height);
    point.x *= static_cast<float>(surface->w) / static_cast<float>(std::max(window_width, 1));
    point.y *= static_cast<float>(surface->h) / static_cast<float>(std::max(window_height, 1));
    Uint8 r{}, g{}, b{}, a{};
    require(SDL_ReadSurfacePixel(surface, static_cast<int>(std::lround(point.x)),
                                 static_cast<int>(std::lround(point.y)),
                                 &r, &g, &b, &a),
            "Capture sample is unavailable");
    return {r, g, b, a};
}

int brightness(Pixel pixel) { return pixel.r + pixel.g + pixel.b; }

void require_close(Pixel lhs, Pixel rhs, int tolerance, std::string_view message) {
    const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
    require(close(lhs.r, rhs.r) && close(lhs.g, rhs.g) && close(lhs.b, rhs.b) &&
                close(lhs.a, rhs.a), message);
}

nlohmann::json pixel_json(Pixel value) {
    return {value.r, value.g, value.b, value.a};
}

Pixel capture(Renderer2D& renderer, SDL_Window* window, const Frame2D& frame,
              const std::filesystem::path& output, std::string_view name,
              Point at, nlohmann::json& samples) {
    const auto file = output / (std::string(name) + ".png");
    renderer.render(frame, file);
    auto surface = load_capture(file);
    const auto result = sample(surface.get(), window, renderer, at);
    samples[std::string(name)] = pixel_json(result);
    return result;
}

template<class Mutate>
void expect_rejected(Renderer2D& renderer, const Frame2D& valid,
                     Mutate mutate, std::string_view contract) {
    auto invalid = valid;
    mutate(invalid);
    bool rejected = false;
    try {
        renderer.render(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, std::string("Renderer accepted invalid surface contract: ") +
                          std::string(contract));
}

void verify_mode_contract(Renderer2D& renderer, bool expect_surface_lighting) {
    auto reset = receiver_frame();
    reset.surface_lighting.counts[0] = 1;
    reset.clear_commands();
    const SurfaceLightingFrame defaults;
    require(reset.vertices.empty() && reset.indices.empty() && reset.commands.empty() &&
                reset.surface_vertices.empty() &&
                reset.surface_lighting.counts == defaults.counts &&
                reset.surface_lighting.ambient == defaults.ambient,
            "Clearing frame commands retained surface lighting data");
    require(renderer.surface_lighting_available() == expect_surface_lighting,
            "Surface lighting capability differs from the selected backend");
    require(renderer.lighting_status().effective == LightingMode::Legacy &&
                !renderer.lighting_status().dynamic_available,
            "Bounded surface lighting must not claim full Dynamic mode");
    require(!renderer.set_lighting_mode(LightingMode::Dynamic),
            "Full Dynamic mode was reported as implemented");
    require(renderer.lighting_status().effective == LightingMode::Legacy &&
                renderer.lighting_status().reason == LightingUnavailableReason::NotImplemented,
            "Unavailable Dynamic mode did not preserve Legacy rendering");
}

int software_contract(const std::filesystem::path& assets) {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra surface lighting software contract", 320, 240,
                         SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    Renderer2D renderer(window.get(), assets, {Backend::Software});
    verify_mode_contract(renderer, false);
    auto frame = receiver_frame({80.f, 60.f, 160.f, 120.f});
    bool rejected = false;
    try {
        renderer.render(frame);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Software backend silently accepted surface-lit geometry");
    std::cout << "Surface lighting software capability and explicit rejection contracts passed.\n";
    return 0;
}

void validate_native_contracts(Renderer2D& renderer) {
    const auto valid = receiver_frame();
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_vertices[0].world_position[0] =
            std::numeric_limits<float>::quiet_NaN();
    }, "non-finite world position");
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_vertices[0].normal = {0.f, 0.f, 2.f};
    }, "non-unit normal");
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_vertices.clear();
    }, "missing companion stream");
    expect_rejected(renderer, valid, [](auto& frame) {
        std::get<GeometryDraw>(frame.commands[0]).blend = Blend::Alpha;
    }, "alpha surface draw");
    expect_rejected(renderer, valid, [](auto& frame) {
        std::get<GeometryDraw>(frame.commands[0]).pass = Pass::Ui;
    }, "UI surface draw");
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_lighting.counts[0] = surface_light_capacity + 1;
    }, "more than four local lights");
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_lighting.counts[0] = 1;
        frame.surface_lighting.lights[0] = point_light({0.f, 0.f, 2.f}, 0.f,
                                                       {1.f, 1.f, 1.f}, 1.f);
    }, "non-positive radius");
    expect_rejected(renderer, valid, [](auto& frame) {
        frame.surface_lighting.counts[0] = 1;
        frame.surface_lighting.lights[0] = point_light({0.f, 0.f, 2.f}, 4.f,
                                                       {1.f, 1.f, 1.f}, 1.f);
        frame.surface_lighting.lights[0].occluder_range_flags[0] = 1;
        frame.surface_lighting.lights[0].occluder_range_flags[1] = 1;
    }, "unsupported occluder range");
}

void add_unlit_ui_patch(Frame2D& frame) {
    add_quad(frame, {20.f, 20.f, 80.f, 48.f}, {31, 157, 219, 255},
             {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, Pass::Ui, Blend::Opaque, false);
}

void create_geometry_calibration(Renderer2D& renderer,
                                 const std::filesystem::path& output) {
    const auto project = [](std::array<float, 3> world) {
        return Point{320.f + (world[0] - world[1]) * 48.f,
                     340.f + (world[0] + world[1]) * 22.f - world[2] * 58.f};
    };
    const auto projected_quad = [&](Frame2D& frame,
                                    const std::array<std::array<float, 3>, 4>& world,
                                    Color color, std::array<float, 3> normal,
                                    Pass pass) {
        std::array<Point, 4> screen{};
        std::transform(world.begin(), world.end(), screen.begin(), project);
        add_surface_quad(frame, screen, color, world, normal, pass);
    };
    Frame2D frame;
    frame.logical_size = {640, 480};
    frame.clear = {12, 18, 25, 255};
    frame.surface_lighting.ambient = {.12f, .14f, .18f, 0.f};
    frame.surface_lighting.sun_direction = {.36f, -.48f, .8f, 0.f};
    frame.surface_lighting.sun_color_intensity = {1.f, .82f, .62f, 1.1f};
    frame.surface_lighting.counts[0] = 1;
    frame.surface_lighting.lights[0] =
        point_light({0.f, 3.f, 3.f}, 7.f, {1.f, .22f, .06f}, 2.2f);
    projected_quad(frame,
        {{{-3.5f, -2.5f, 0.f}, {3.5f, -2.5f, 0.f},
          {3.5f, 2.5f, 0.f}, {-3.5f, 2.5f, 0.f}}},
        {83, 112, 68, 255}, {0.f, 0.f, 1.f}, Pass::Terrain);
    // The fixed camera sees +X/+Y faces. A single sloped calibration roof
    // follows z = 2.8 - .4*y; walls meet that same plane, without a fake gable.
    projected_quad(frame,
        {{{1.7f, -1.5f, 0.f}, {1.7f, 1.2f, 0.f},
          {1.7f, 1.2f, 2.32f}, {1.7f, -1.5f, 3.4f}}},
        {139, 96, 61, 255}, {1.f, 0.f, 0.f}, Pass::WorldOrdered);
    projected_quad(frame,
        {{{-1.7f, 1.2f, 0.f}, {1.7f, 1.2f, 0.f},
          {1.7f, 1.2f, 2.32f}, {-1.7f, 1.2f, 2.32f}}},
        {178, 133, 82, 255}, {0.f, 1.f, 0.f}, Pass::WorldOrdered);
    projected_quad(frame,
        {{{-2.f, 1.5f, 2.2f}, {2.f, 1.5f, 2.2f},
          {2.f, -1.8f, 3.52f}, {-2.f, -1.8f, 3.52f}}},
        {116, 54, 39, 255}, {0.f, .37139068f, .92847669f}, Pass::WorldOrdered);
    frame.commands.emplace_back(TextDraw{Pass::Ui,
        "GEOMETRY CALIBRATION - NOT ACCEPTED GAME ART", {58.f, 40.f}, 18.f,
        {244, 232, 205, 255}, 0.f});
    renderer.render(frame, output / "geometry-calibration-not-game-art.png");
}

int native_probe(SDL_Window* window, const std::filesystem::path& assets,
                 const std::filesystem::path& output) {
    const auto started = Clock::now();
    Renderer2D renderer(window, assets, {Backend::Direct3D12});
    verify_mode_contract(renderer, true);
    Frame2D empty;
    GeometryDraw no_op;
    no_op.pass = Pass::Terrain;
    no_op.blend = Blend::Opaque;
    no_op.surface_lit = true;
    empty.commands.emplace_back(no_op);
    renderer.render(empty);
    validate_native_contracts(renderer);
    nlohmann::json samples = nlohmann::json::object();

    auto off = receiver_frame();
    add_unlit_ui_patch(off);
    const auto dark = capture(renderer, window, off, output, "light-off", {320.f, 240.f}, samples);
    const auto ui_off = capture(renderer, window, off, output, "ui-light-off", {60.f, 44.f}, samples);

    auto ambient = off;
    ambient.surface_lighting.ambient = {.28f, .28f, .28f, 0.f};
    const auto ambient_pixel = capture(renderer, window, ambient, output, "ambient-on",
                                       {320.f, 240.f}, samples);
    const auto ui_on = capture(renderer, window, ambient, output, "ui-light-on",
                              {60.f, 44.f}, samples);
    require(brightness(ambient_pixel) > brightness(dark) + 120,
            "Ambient light did not brighten the receiver");
    require_close(ui_off, ui_on, 1, "Surface lighting changed legacy UI geometry");
    Frame2D legacy;
    legacy.logical_size = {640, 480};
    add_unlit_ui_patch(legacy);
    renderer.render(legacy);
    const auto resumed = capture(renderer, window, ambient, output, "lit-after-legacy",
                                  {320.f, 240.f}, samples);
    require_close(ambient_pixel, resumed, 1, "Legacy draw corrupted the next lit frame");

    auto linear = receiver_frame({220.f, 160.f, 200.f, 160.f}, {0.f, 0.f, 0.f},
                                 {128, 128, 128, 255});
    linear.surface_lighting.ambient = {.25f, .25f, .25f, 0.f};
    const auto linear_pixel = capture(renderer, window, linear, output, "linear-srgb",
                                      {320.f, 240.f}, samples);
    require(std::abs(linear_pixel.r - 66) <= 8 && std::abs(linear_pixel.g - 66) <= 8 &&
                std::abs(linear_pixel.b - 66) <= 8,
            "Lighting was not applied in linear color space with sRGB output encoding");

    Frame2D per_fragment;
    per_fragment.logical_size = {640, 480};
    per_fragment.clear = {8, 10, 14, 255};
    per_fragment.surface_lighting.ambient = {.01f, .01f, .01f, 0.f};
    per_fragment.surface_lighting.sun_color_intensity = {1.f, 1.f, 1.f, 0.f};
    per_fragment.surface_lighting.counts[0] = 1;
    per_fragment.surface_lighting.lights[0] =
        point_light({0.f, 0.f, 1.f}, 1.6f, {1.f, 1.f, 1.f}, 3.f);
    add_surface_quad(per_fragment,
        {{{100.f, 80.f}, {540.f, 80.f}, {540.f, 400.f}, {100.f, 400.f}}},
        {255, 255, 255, 255},
        {{{-4.f, -3.f, -.4f}, {4.f, -3.f, .4f},
          {4.f, 3.f, .4f}, {-4.f, 3.f, -.4f}}});
    const auto fragment_center = capture(renderer, window, per_fragment, output,
                                         "per-fragment-local-light", {320.f, 240.f}, samples);
    auto fragment_surface = load_capture(output / "per-fragment-local-light.png");
    const auto fragment_edge = sample(fragment_surface.get(), window, renderer,
                                      {125.f, 240.f});
    samples["per-fragment-local-light-edge"] = pixel_json(fragment_edge);
    require(brightness(fragment_center) > brightness(fragment_edge) + 300,
            "Local light was not evaluated per fragment from interpolated world position");

    auto sun = receiver_frame({70.f, 150.f, 220.f, 180.f});
    add_quad(sun, {350.f, 150.f, 220.f, 180.f}, {255, 255, 255, 255},
             {0.f, 0.f, 0.f}, {0.f, 0.f, -1.f});
    sun.surface_lighting.ambient = {.04f, .04f, .04f, 0.f};
    sun.surface_lighting.sun_direction = {0.f, 0.f, 1.f, 0.f};
    sun.surface_lighting.sun_color_intensity = {1.f, .8f, .5f, 1.f};
    const auto sun_front = capture(renderer, window, sun, output, "sun-opposing-normals",
                                   {180.f, 240.f}, samples);
    auto sun_surface = load_capture(output / "sun-opposing-normals.png");
    const auto sun_back = sample(sun_surface.get(), window, renderer, {460.f, 240.f});
    samples["sun-opposing-normals-back"] = pixel_json(sun_back);
    require(brightness(sun_front) > brightness(sun_back) + 180,
            "Opposing normals did not produce opposing sun response");

    auto source_swap = receiver_frame({70.f, 150.f, 220.f, 180.f},
                                      {0.f, 0.f, 0.f});
    for (auto& vertex : source_swap.surface_vertices)
        vertex.normal = {-1.f, 0.f, 0.f};
    add_quad(source_swap, {350.f, 150.f, 220.f, 180.f}, {255, 255, 255, 255},
             {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f});
    source_swap.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    source_swap.surface_lighting.counts[0] = 1;
    source_swap.surface_lighting.lights[0] =
        point_light({-2.f, 0.f, 0.f}, 4.f, {1.f, .8f, .5f}, 2.f);
    const auto source_left_left = capture(renderer, window, source_swap, output,
                                          "same-light-source-left", {180.f, 240.f}, samples);
    auto source_left_surface = load_capture(output / "same-light-source-left.png");
    const auto source_left_right = sample(source_left_surface.get(), window, renderer,
                                          {460.f, 240.f});
    samples["same-light-source-left-opposing-normal"] = pixel_json(source_left_right);
    require(brightness(source_left_left) > brightness(source_left_right) + 240,
            "Left light source did not select the left-facing normal");
    source_swap.surface_lighting.lights[0].anchor_radius[0] = 2.f;
    const auto source_right_left = capture(renderer, window, source_swap, output,
                                           "same-light-source-right", {180.f, 240.f}, samples);
    auto source_right_surface = load_capture(output / "same-light-source-right.png");
    const auto source_right_right = sample(source_right_surface.get(), window, renderer,
                                           {460.f, 240.f});
    samples["same-light-source-right-opposing-normal"] = pixel_json(source_right_right);
    require(brightness(source_right_right) > brightness(source_right_left) + 240,
            "Moving the same light right did not swap the opposing-normal response");

    auto point = receiver_frame({65.f, 145.f, 220.f, 190.f}, {-2.f, 0.f, 0.f});
    add_quad(point, {355.f, 145.f, 220.f, 190.f}, {255, 255, 255, 255},
             {2.f, 0.f, 0.f});
    point.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    point.surface_lighting.counts[0] = 2;
    point.surface_lighting.lights[0] = point_light({-2.f, 0.f, 2.f}, 4.f, {1.f, 0.f, 0.f}, 2.f);
    point.surface_lighting.lights[1] = point_light({2.f, 0.f, 2.f}, 4.f, {0.f, 0.f, 1.f}, 2.f);
    const auto point_left = capture(renderer, window, point, output, "two-point-color-separation",
                                    {175.f, 240.f}, samples);
    auto point_surface = load_capture(output / "two-point-color-separation.png");
    const auto point_right = sample(point_surface.get(), window, renderer, {465.f, 240.f});
    samples["two-point-color-separation-right"] = pixel_json(point_right);
    require(point_left.r > point_left.b + 80 && point_right.b > point_right.r + 80,
            "Local point lights did not preserve left/right color and position");

    auto cutoff = receiver_frame({65.f, 145.f, 220.f, 190.f}, {0.f, 0.f, 0.f});
    add_quad(cutoff, {355.f, 145.f, 220.f, 190.f}, {255, 255, 255, 255},
             {5.f, 0.f, 0.f});
    cutoff.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    cutoff.surface_lighting.counts[0] = 1;
    cutoff.surface_lighting.lights[0] = point_light({0.f, 0.f, 2.f}, 3.f, {1.f, 1.f, 1.f}, 2.f);
    const auto radius_inside = capture(renderer, window, cutoff, output, "radius-cutoff",
                                       {175.f, 240.f}, samples);
    auto cutoff_surface = load_capture(output / "radius-cutoff.png");
    const auto radius_outside = sample(cutoff_surface.get(), window, renderer, {465.f, 240.f});
    samples["radius-cutoff-outside"] = pixel_json(radius_outside);
    require(brightness(radius_inside) > brightness(radius_outside) + 180,
            "Point-light radius did not cut off an outside receiver");

    auto spot = receiver_frame({65.f, 145.f, 220.f, 190.f}, {0.f, 0.f, 0.f});
    add_quad(spot, {355.f, 145.f, 220.f, 190.f}, {255, 255, 255, 255},
             {3.f, 0.f, 0.f});
    spot.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    spot.surface_lighting.counts[0] = 1;
    spot.surface_lighting.lights[0] = spot_light({0.f, 0.f, 4.f}, {0.f, 0.f, -1.f},
                                                7.f, {1.f, .7f, .2f}, 2.f, .82f, .94f);
    const auto spot_inside = capture(renderer, window, spot, output, "spot-inside-outside",
                                     {175.f, 240.f}, samples);
    auto spot_surface = load_capture(output / "spot-inside-outside.png");
    const auto spot_outside = sample(spot_surface.get(), window, renderer, {465.f, 240.f});
    samples["spot-inside-outside-outside"] = pixel_json(spot_outside);
    require(brightness(spot_inside) > brightness(spot_outside) + 150,
            "Spot cone did not distinguish inside and outside receivers");

    auto movement = receiver_frame();
    movement.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    movement.surface_lighting.counts[0] = 1;
    movement.surface_lighting.lights[0] = point_light({0.f, 0.f, 2.f}, 4.f,
                                                      {.2f, 1.f, .3f}, 2.f);
    const auto movement_near = capture(renderer, window, movement, output, "receiver-near",
                                       {320.f, 240.f}, samples);
    for (auto& vertex : movement.surface_vertices) vertex.world_position[0] += 6.f;
    const auto movement_far = capture(renderer, window, movement, output, "receiver-moved",
                                      {320.f, 240.f}, samples);
    require(brightness(movement_near) > brightness(movement_far) + 160,
            "Changing receiver world position did not change local lighting");

    auto invariant = receiver_frame({220.f, 160.f, 200.f, 160.f}, {1.f, 2.f, 0.f});
    invariant.surface_lighting.ambient = {.03f, .03f, .03f, 0.f};
    invariant.surface_lighting.counts[0] = 1;
    invariant.surface_lighting.lights[0] = point_light({1.f, 2.f, 2.f}, 5.f,
                                                       {.3f, .6f, 1.f}, 1.6f);
    const auto original = capture(renderer, window, invariant, output, "camera-original",
                                  {320.f, 240.f}, samples);
    auto pan_zoom = receiver_frame({280.f, 200.f, 80.f, 80.f}, {5.f, 5.f, 0.f});
    pan_zoom.surface_lighting = invariant.surface_lighting;
    pan_zoom.surface_lighting.lights[0].anchor_radius[0] += 4.f;
    pan_zoom.surface_lighting.lights[0].anchor_radius[1] += 3.f;
    const auto transformed = capture(renderer, window, pan_zoom, output, "camera-pan-zoom",
                                     {320.f, 240.f}, samples);
    require_close(original, transformed, 2,
                  "Equivalent camera pan/zoom changed center-sample world lighting");

    create_geometry_calibration(renderer, output);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                Clock::now() - started).count();
    const auto& device = renderer.device_info();
    nlohmann::json report{
        {"build", RIBTERRA_PROBE_BUILD},
        {"platform", SDL_GetPlatform()},
        {"logical_cpu_cores", SDL_GetNumLogicalCPUCores()},
        {"system_ram_mib", SDL_GetSystemRAM()},
        {"backend", device.backend}, {"device", device.device},
        {"driver", device.driver}, {"software", device.software},
        {"surface_lighting_available", renderer.surface_lighting_available()},
        {"dynamic_mode_available", renderer.lighting_status().dynamic_available},
        {"surface_light_capacity", surface_light_capacity},
        {"surface_lighting_frame_bytes", sizeof(SurfaceLightingFrame)},
        {"elapsed_ms", elapsed_ms}, {"samples", samples},
        {"fixture", "geometry calibration, not accepted game art"},
        {"claims", {"ambient", "sun diffuse", "point", "spot", "linear color",
                     "per-fragment world-position interpolation", "same-light source movement",
                     "receiver movement", "camera pan/zoom invariance", "legacy UI isolation"}},
        {"not_claimed", {"full Dynamic mode", "shadows", "emission", "GameView integration"}}
    };
    std::ofstream stream(output / "surface-lighting-report.json", std::ios::binary);
    require(stream.good(), "Could not create surface lighting report");
    stream << report.dump(2) << '\n';
    require(stream.good(), "Could not finish surface lighting report");
    std::cout << "Surface lighting native proof passed; captures preserved in "
              << utf8(output) << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool software = argc == 2 && std::string_view(argv[1]) == "--software-contract";
        require(software || argc == 2,
                "Usage: SurfaceLightingProbe <new-output-directory> | --software-contract");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto base = path_from_utf8(SDL_GetBasePath());
        if (software) {
            const auto result = software_contract(base / "assets");
            SDL_Quit();
            return result;
        }
        const auto output = std::filesystem::absolute(path_from_utf8(argv[1]));
        require(!std::filesystem::exists(output),
                "Output path already exists; refusing to overwrite or delete preserved evidence");
        require(std::filesystem::create_directories(output),
                "Could not create the new output directory");
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra surface lighting proof", 640, 480,
                             SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        const auto result = native_probe(window.get(), base / "assets", output);
        window.reset();
        SDL_Quit();
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

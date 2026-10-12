#include "ribterra/presentation.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace {
using namespace ribterra::render;
using Json = nlohmann::json;

struct Pixel {
    int r{}, g{}, b{}, a{};
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

Json pixel_json(Pixel value) { return {value.r, value.g, value.b, value.a}; }

Json status_json(const WorldHdrStatus& value) {
    return {{"available", value.available},
            {"rgba16f_supported", value.rgba16f_supported},
            {"sdr_unorm_swapchain", value.sdr_unorm_swapchain},
            {"swapchain_format", value.swapchain_format},
            {"width", value.width}, {"height", value.height},
            {"frame_sets", value.frame_sets},
            {"attachment_bytes", value.attachment_bytes},
            {"reason", value.reason}};
}

std::string_view mode_name(PresentMode mode) {
    switch (mode) {
    case PresentMode::VSync: return "vsync";
    case PresentMode::Immediate: return "immediate";
    case PresentMode::Mailbox: return "mailbox";
    }
    throw std::invalid_argument("Unknown presentation mode");
}

Json presentation_status_json(const PresentationStatus& value) {
    const auto request_json = [](PresentationRequest request) {
        return Json{{"mode", mode_name(request.mode)}, {"frame_limit", request.frame_limit},
                    {"frames_in_flight", request.frames_in_flight}};
    };
    return {{"requested", request_json(value.requested)},
            {"effective", request_json(value.effective)},
            {"available", value.available},
            {"immediate_supported", value.immediate_supported},
            {"mailbox_supported", value.mailbox_supported},
            {"reason", static_cast<int>(value.reason)},
            {"error", value.error}};
}

void add_quad(Frame2D& frame, Rect rect, Color color,
              Pass pass = Pass::WorldOrdered, Blend blend = Blend::Opaque,
              bool surface_lit = false,
              std::array<float, 3> world_position = {0.f, 0.f, 0.f},
              std::array<float, 3> normal = {0.f, 0.f, 1.f}) {
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    for (const Point point : std::array<Point, 4>{{
             {rect.x, rect.y}, {rect.x + rect.w, rect.y},
             {rect.x + rect.w, rect.y + rect.h}, {rect.x, rect.y + rect.h}}}) {
        frame.vertices.push_back({point, {}, color});
        frame.surface_vertices.push_back({world_position, normal});
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

Frame2D hdr_frame(float exposure = 1.f) {
    Frame2D frame;
    frame.logical_size = {640, 480};
    frame.clear = {0, 0, 0, 255};
    frame.linear_hdr_world = true;
    frame.world_exposure = exposure;
    return frame;
}

std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>
load_capture(const std::filesystem::path& path) {
    auto* surface = SDL_LoadPNG(utf8(path).c_str());
    require(surface != nullptr, std::string("Could not decode renderer capture: ") + SDL_GetError());
    const auto* details = SDL_GetPixelFormatDetails(surface->format);
    require(details && details->bytes_per_pixel == 4,
            "Renderer capture is not final four-byte RGBA8 evidence");
    return {surface, SDL_DestroySurface};
}

Pixel surface_pixel(SDL_Surface* surface, int x, int y) {
    Uint8 r{}, g{}, b{}, a{};
    require(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a),
            "Capture pixel is unavailable");
    return {r, g, b, a};
}

Pixel sample(SDL_Surface* surface, SDL_Window* window, const Renderer2D& renderer,
             Point logical) {
    auto point = renderer.logical_to_window(logical);
    int window_width = 1;
    int window_height = 1;
    SDL_GetWindowSize(window, &window_width, &window_height);
    point.x *= static_cast<float>(surface->w) / static_cast<float>(std::max(window_width, 1));
    point.y *= static_cast<float>(surface->h) / static_cast<float>(std::max(window_height, 1));
    return surface_pixel(surface, std::clamp(static_cast<int>(std::lround(point.x)), 0, surface->w - 1),
                         std::clamp(static_cast<int>(std::lround(point.y)), 0, surface->h - 1));
}

Pixel capture(Renderer2D& renderer, SDL_Window* window, const Frame2D& frame,
              const std::filesystem::path& output, std::string_view name,
              Point point, Json& samples) {
    const auto file = output / (std::string(name) + ".png");
    require(!std::filesystem::exists(file), "Capture path already exists");
    renderer.render(frame, file);
    auto surface = load_capture(file);
    const auto value = sample(surface.get(), window, renderer, point);
    require(value.a == 255, "Final RGBA8 capture sample is not opaque");
    samples[std::string(name)] = pixel_json(value);
    return value;
}

void require_channel(Pixel value, int expected, int tolerance, std::string_view message) {
    const auto close = [&](int channel) { return std::abs(channel - expected) <= tolerance; };
    require(close(value.r) && close(value.g) && close(value.b), message);
}

void require_color(Pixel value, Pixel expected, int tolerance, std::string_view message) {
    const auto close = [&](int actual, int target) { return std::abs(actual - target) <= tolerance; };
    require(close(value.r, expected.r) && close(value.g, expected.g) &&
                close(value.b, expected.b) && value.a == expected.a,
            message);
}

void require_invalid(Renderer2D& renderer, Frame2D frame, std::string_view message) {
    bool rejected = false;
    try {
        renderer.render(frame);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

Extent drawable(SDL_Window* window) {
    int width{}, height{};
    require(SDL_GetWindowSizeInPixels(window, &width, &height), SDL_GetError());
    require(width > 0 && height > 0, "Window drawable is empty");
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

void settle_window(SDL_Window* window, int requested_width, int requested_height) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        SDL_PumpEvents();
        int width{}, height{};
        SDL_GetWindowSize(window, &width, &height);
        if (width == requested_width && height == requested_height &&
            !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
            return;
        SDL_Delay(10);
    }
    throw std::runtime_error("Window size/restore event did not settle");
}

void validate_status(const WorldHdrStatus& status, Extent size, std::uint32_t frame_sets) {
    require(status.available && status.rgba16f_supported && status.sdr_unorm_swapchain,
            "Native renderer did not expose the required HDR-world/SDR-final formats");
    require(status.swapchain_format != 0 && status.width == size.width &&
                status.height == size.height && status.frame_sets == frame_sets,
            "HDR status does not describe the active drawable/frame sets");
    const auto expected_bytes = std::uint64_t(size.width) * size.height * frame_sets * (8u + 4u);
    require(status.attachment_bytes == expected_bytes,
            "HDR attachment footprint does not match RGBA16F world plus RGBA8 final targets");
    require(status.reason.empty(), "Available HDR path reported an unavailable reason");
}

std::array<int, 4> capture_rect(SDL_Surface* surface, SDL_Window* window,
                                const Renderer2D& renderer, Rect logical) {
    auto top_left = renderer.logical_to_window({logical.x, logical.y});
    auto bottom_right = renderer.logical_to_window({logical.x + logical.w, logical.y + logical.h});
    int window_width = 1;
    int window_height = 1;
    SDL_GetWindowSize(window, &window_width, &window_height);
    const auto scale_x = static_cast<float>(surface->w) / std::max(window_width, 1);
    const auto scale_y = static_cast<float>(surface->h) / std::max(window_height, 1);
    const int x = std::clamp(static_cast<int>(std::floor(top_left.x * scale_x)), 0, surface->w - 1);
    const int y = std::clamp(static_cast<int>(std::floor(top_left.y * scale_y)), 0, surface->h - 1);
    const int right = std::clamp(static_cast<int>(std::ceil(bottom_right.x * scale_x)), x + 1, surface->w);
    const int bottom = std::clamp(static_cast<int>(std::ceil(bottom_right.y * scale_y)), y + 1, surface->h);
    return {x, y, right - x, bottom - y};
}

bool region_equal(SDL_Surface* first, SDL_Surface* second, SDL_Window* window,
                  const Renderer2D& renderer, Rect logical) {
    require(first->w == second->w && first->h == second->h, "Capture dimensions changed");
    const auto [x, y, w, h] = capture_rect(first, window, renderer, logical);
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) {
            const auto a = surface_pixel(first, px, py);
            const auto b = surface_pixel(second, px, py);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) return false;
        }
    return true;
}

std::size_t non_black_pixels(SDL_Surface* surface, SDL_Window* window,
                             const Renderer2D& renderer, Rect logical) {
    const auto [x, y, w, h] = capture_rect(surface, window, renderer, logical);
    std::size_t count{};
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px) {
            const auto value = surface_pixel(surface, px, py);
            count += value.r != 0 || value.g != 0 || value.b != 0;
        }
    return count;
}

LightGpu point_light() {
    LightGpu light;
    light.anchor_radius = {0.f, 0.f, 2.f, 5.f};
    light.direction_cone = {0.f, 0.f, -1.f, -1.f};
    light.color_intensity = {1.f, .35f, .1f, 2.f};
    light.occluder_range_flags = {0u, 0u, 1u, 0u};
    light.height_falloff_reveal = {-1.f, 2.f, 1.f, 0.f};
    return light;
}

int software_contract(const std::filesystem::path& assets) {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra HDR software contract", 320, 240, SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    Renderer2D renderer(window.get(), assets, {Backend::Software});
    const auto status = renderer.world_hdr_status();
    require(!status.available && !status.rgba16f_supported && !status.reason.empty(),
            "Software backend claimed production HDR world support");
    auto frame = hdr_frame();
    add_quad(frame, {40.f, 40.f, 240.f, 160.f}, {255, 255, 255, 255});
    require_invalid(renderer, frame, "Software backend silently accepted HDR world rendering");
    Frame2D reset = frame;
    reset.clear_commands();
    require(!reset.linear_hdr_world && reset.world_exposure == 1.f,
            "clear_commands retained the HDR opt-in or exposure");
    std::cout << "HDR color software capability and explicit rejection contracts passed.\n";
    return 0;
}

void invalid_contracts(Renderer2D& renderer) {
    auto frame = hdr_frame();
    add_quad(frame, {200.f, 140.f, 240.f, 200.f}, {128, 128, 128, 255});
    for (const float exposure : {std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(), -.5f, 0.f, 65.f}) {
        auto invalid = frame;
        invalid.world_exposure = exposure;
        require_invalid(renderer, invalid, "Renderer accepted invalid HDR world exposure");
    }
    auto legacy = frame;
    legacy.linear_hdr_world = false;
    legacy.world_exposure = 2.f;
    require_invalid(renderer, legacy, "Legacy world silently accepted non-unity HDR exposure");
    auto translucent_clear = frame;
    translucent_clear.clear.a = 128;
    require_invalid(renderer, translucent_clear, "HDR world silently accepted a non-opaque scene clear");

    auto wrong_order = hdr_frame();
    add_quad(wrong_order, {20.f, 20.f, 80.f, 60.f}, {255, 255, 255, 255}, Pass::Ui);
    add_quad(wrong_order, {200.f, 140.f, 240.f, 200.f}, {255, 0, 0, 255}, Pass::WorldOrdered);
    require_invalid(renderer, wrong_order, "HDR frame accepted a world draw after the UI pass");
}

void numeric_contracts(Renderer2D& renderer, SDL_Window* window,
                       const std::filesystem::path& output, Json& samples) {
    const Rect center{200.f, 140.f, 240.f, 200.f};
    auto colored_clear = hdr_frame();
    colored_clear.clear = {128, 64, 200, 255};
    require_color(capture(renderer, window, colored_clear, output, "colored-hdr-clear-unity",
                          {20.f, 20.f}, samples),
                  {128, 64, 200, 255}, 3,
                  "Opaque colored HDR clear did not round-trip at unity exposure");

    auto unity = hdr_frame();
    add_quad(unity, center, {128, 128, 128, 255});
    require_channel(capture(renderer, window, unity, output, "srgb-gray128-unity", {320.f, 240.f}, samples),
                    128, 3, "sRGB gray 128 did not round-trip at unity exposure");

    auto straight = hdr_frame();
    add_quad(straight, center, {255, 255, 255, 128}, Pass::WorldOrdered, Blend::Alpha);
    const auto straight_sample =
        capture(renderer, window, straight, output, "straight-white-alpha128", {320.f, 240.f}, samples);
    require_channel(straight_sample, 188, 4,
                    "Straight-alpha white blended in encoded space or clipped before SDR encode");
    require(std::abs(straight_sample.r - 128) > 30, "Straight-alpha proof is indistinguishable from encoded blending");

    auto premultiplied = hdr_frame();
    add_quad(premultiplied, center, {128, 128, 128, 128}, Pass::WorldOrdered,
             Blend::PremultipliedAlpha);
    require_channel(capture(renderer, window, premultiplied, output, "premult-white-alpha128",
                            {320.f, 240.f}, samples),
                    188, 4, "Encoded premultiplied white was not converted to linear premultiplied color");

    auto zero_alpha = hdr_frame();
    add_quad(zero_alpha, center, {255, 255, 255, 0}, Pass::WorldOrdered,
             Blend::PremultipliedAlpha);
    require_color(capture(renderer, window, zero_alpha, output, "premult-zero-alpha",
                          {320.f, 240.f}, samples),
                  {0, 0, 0, 255}, 0,
                  "Zero-alpha encoded premultiplied draw changed opaque black");

    auto additive = hdr_frame();
    add_quad(additive, center, {128, 128, 128, 255});
    add_quad(additive, center, {255, 255, 255, 64}, Pass::WorldOrdered, Blend::Additive);
    require_channel(capture(renderer, window, additive, output, "additive-white-alpha64-gray128",
                            {320.f, 240.f}, samples),
                    182, 4, "Additive HDR blend did not sum source alpha in linear space");

    auto ambient = hdr_frame(.125f);
    ambient.surface_lighting.ambient = {4.f, 4.f, 4.f, 0.f};
    ambient.surface_lighting.sun_color_intensity = {0.f, 0.f, 0.f, 0.f};
    add_quad(ambient, center, {255, 255, 255, 255}, Pass::WorldOrdered, Blend::Opaque, true);
    require_channel(capture(renderer, window, ambient, output, "hdr-ambient4-exposure0125",
                            {320.f, 240.f}, samples),
                    188, 5, "HDR surface lighting clipped before exposure and SDR encoding");

    auto ordered = hdr_frame(.25f);
    ordered.surface_lighting.ambient = {2.f, 2.f, 2.f, 0.f};
    ordered.surface_lighting.sun_color_intensity = {0.f, 0.f, 0.f, 0.f};
    add_quad(ordered, center, {255, 0, 0, 255}, Pass::WorldOrdered, Blend::Opaque, true);
    add_quad(ordered, center, {0, 255, 0, 255}, Pass::WorldOrdered, Blend::Opaque, false);
    add_quad(ordered, center, {0, 0, 255, 255}, Pass::WorldOrdered, Blend::Opaque, true);
    const auto order_sample = capture(renderer, window, ordered, output, "lit-legacy-lit-order",
                                      {320.f, 240.f}, samples);
    require(order_sample.b > 180 && order_sample.r < 8 && order_sample.g < 8,
            "Surface-lit and legacy world draws lost painter order");

    auto kernel_dark = hdr_frame(.25f);
    kernel_dark.surface_lighting.ambient = {.02f, .02f, .02f, 0.f};
    kernel_dark.surface_lighting.sun_color_intensity = {0.f, 0.f, 0.f, 0.f};
    add_quad(kernel_dark, center, {255, 255, 255, 255}, Pass::WorldOrdered, Blend::Opaque, true);
    const auto dark = capture(renderer, window, kernel_dark, output, "surface-kernel-dark",
                              {320.f, 240.f}, samples);
    auto kernel_lit = kernel_dark;
    kernel_lit.surface_lighting.sun_direction = {0.f, 0.f, 1.f, 0.f};
    kernel_lit.surface_lighting.sun_color_intensity = {.4f, .5f, 1.f, 1.25f};
    kernel_lit.surface_lighting.counts[0] = 1;
    kernel_lit.surface_lighting.lights[0] = point_light();
    const auto lit = capture(renderer, window, kernel_lit, output, "surface-kernel-sun-local",
                             {320.f, 240.f}, samples);
    require(lit.r + lit.g + lit.b > dark.r + dark.g + dark.b + 100,
            "Actual sun/local surface kernel did not feed the HDR world path");
}

void exposure_ui_contract(Renderer2D& renderer, SDL_Window* window,
                          const std::filesystem::path& output, Json& samples) {
    auto frame = hdr_frame(1.f);
    add_quad(frame, {250.f, 250.f, 140.f, 120.f}, {200, 200, 200, 255});
    add_quad(frame, {20.f, 20.f, 120.f, 48.f}, {31, 157, 219, 255}, Pass::Ui);
    frame.commands.emplace_back(TextDraw{Pass::Ui, "HDR UI", {20.f, 112.f}, 32.f,
                                         {244, 232, 205, 255}, 0.f});
    const auto first_path = output / "exposure1-ui.png";
    renderer.render(frame, first_path);
    auto first = load_capture(first_path);
    samples["exposure1-world"] = pixel_json(sample(first.get(), window, renderer, {320.f, 300.f}));
    samples["exposure1-ui"] = pixel_json(sample(first.get(), window, renderer, {60.f, 40.f}));
    frame.world_exposure = .25f;
    const auto second_path = output / "exposure025-ui.png";
    renderer.render(frame, second_path);
    auto second = load_capture(second_path);
    const auto world_first = sample(first.get(), window, renderer, {320.f, 300.f});
    const auto world_second = sample(second.get(), window, renderer, {320.f, 300.f});
    require(world_first.a == 255 && world_second.a == 255,
            "Exposure captures did not preserve opaque final alpha");
    require(world_first.r > world_second.r + 50, "World exposure did not change world color");
    require(region_equal(first.get(), second.get(), window, renderer, {15.f, 15.f, 135.f, 60.f}),
            "World exposure changed the byte-authored UI quad");
    require(region_equal(first.get(), second.get(), window, renderer, {10.f, 75.f, 180.f, 70.f}) &&
                non_black_pixels(first.get(), window, renderer, {10.f, 75.f, 180.f, 70.f}) > 10,
            "World exposure changed UI text or the text evidence was empty");
    const auto ui_sample = sample(second.get(), window, renderer, {60.f, 40.f});
    require(std::abs(ui_sample.r - 31) <= 1 && std::abs(ui_sample.g - 157) <= 1 &&
                std::abs(ui_sample.b - 219) <= 1,
            "HDR composite transformed UI byte color");
    samples["exposure025-world"] = pixel_json(world_second);
    samples["exposure025-ui"] = pixel_json(ui_sample);
}

void minimal_frame_contracts(Renderer2D& renderer, SDL_Window* window,
                             const std::filesystem::path& output, Json& samples) {
    auto empty = hdr_frame();
    const auto empty_sample =
        capture(renderer, window, empty, output, "empty-hdr-frame", {320.f, 240.f}, samples);
    require(empty_sample.r == 0 && empty_sample.g == 0 && empty_sample.b == 0 && empty_sample.a == 255,
            "Empty HDR frame did not preserve opaque black clear");
    auto world_only = hdr_frame();
    add_quad(world_only, {180.f, 130.f, 280.f, 220.f}, {90, 140, 210, 255});
    const auto world_sample = capture(renderer, window, world_only, output, "world-only-hdr-frame",
                                      {320.f, 240.f}, samples);
    require(std::abs(world_sample.r - 90) <= 3 && std::abs(world_sample.g - 140) <= 3 &&
                std::abs(world_sample.b - 210) <= 3 && world_sample.a == 255,
            "World-only HDR frame did not preserve expected unity-exposure color");
    auto ui_only = hdr_frame();
    add_quad(ui_only, {180.f, 130.f, 280.f, 220.f}, {90, 140, 210, 255}, Pass::Ui);
    const auto ui_sample = capture(renderer, window, ui_only, output, "ui-only-hdr-frame",
                                   {320.f, 240.f}, samples);
    require(std::abs(ui_sample.r - 90) <= 1 && std::abs(ui_sample.g - 140) <= 1 &&
                std::abs(ui_sample.b - 210) <= 1 && ui_sample.a == 255,
            "UI-only HDR frame changed byte-authored UI color or alpha");
}

Json asynchronous_owner_contract(Renderer2D& renderer) {
    auto frame = hdr_frame();
    add_quad(frame, {180.f, 130.f, 280.f, 220.f}, {110, 160, 210, 255});
    renderer.set_telemetry_enabled(true);
    std::uint64_t previous_sequence{};
    constexpr int frame_count = 40;
    for (int index = 0; index < frame_count; ++index) {
        renderer.render(frame);
        const auto& telemetry = renderer.frame_telemetry();
        require(telemetry.measured && !telemetry.capture && telemetry.submitted &&
                    telemetry.presentation_queued && telemetry.sequence > previous_sequence,
                "Asynchronous HDR frame did not retain valid GPU ownership/presentation telemetry");
        previous_sequence = telemetry.sequence;
    }
    return {{"frames", frame_count}, {"last_sequence", previous_sequence},
            {"capture_used", false}, {"all_submitted", true}, {"all_presentations_queued", true}};
}

Json frame_count_contract(Renderer2D& renderer, SDL_Window* window,
                          const std::filesystem::path& output) {
    Json results = Json::array();
    auto frame = hdr_frame();
    add_quad(frame, {180.f, 130.f, 280.f, 220.f}, {150, 110, 210, 255});
    for (const auto count : {1u, 2u, 3u}) {
        const bool applied = renderer.set_presentation({PresentMode::VSync, 0, count});
        require(applied, renderer.presentation_status().error);
        const auto capture_path = output / ("frame-sets-" + std::to_string(count) + ".png");
        renderer.render(frame, capture_path);
        auto surface = load_capture(capture_path);
        require_color(sample(surface.get(), window, renderer, {320.f, 240.f}),
                      {150, 110, 210, 255}, 3,
                      "Frame-set capture did not preserve its known HDR unity color");
        validate_status(renderer.world_hdr_status(), drawable(window), count);
        results.push_back({{"requested_frame_sets", count},
                           {"status", status_json(renderer.world_hdr_status())}});
    }
    const bool frame_sets_restored = renderer.set_presentation({PresentMode::VSync, 0, 2});
    require(frame_sets_restored, renderer.presentation_status().error);
    const auto restored_path = output / "frame-sets-restored-2.png";
    renderer.render(frame, restored_path);
    auto restored_surface = load_capture(restored_path);
    require_color(sample(restored_surface.get(), window, renderer, {320.f, 240.f}),
                  {150, 110, 210, 255}, 3,
                  "Restored two-frame-set capture did not preserve its known HDR unity color");
    validate_status(renderer.world_hdr_status(), drawable(window), 2);
    return results;
}

Json presentation_mode_contract(Renderer2D& renderer, SDL_Window* window,
                                const std::filesystem::path& output) {
    Json results = Json::array();
    auto frame = hdr_frame();
    add_quad(frame, {180.f, 130.f, 280.f, 220.f}, {96, 170, 220, 255});
    const auto capabilities = renderer.presentation_status();
    require(capabilities.available, "Native renderer did not expose GPU presentation controls");
    for (const auto mode : {PresentMode::VSync, PresentMode::Immediate, PresentMode::Mailbox}) {
        const auto before = renderer.presentation_status();
        require(before.available && before.immediate_supported == capabilities.immediate_supported &&
                    before.mailbox_supported == capabilities.mailbox_supported,
                "Presentation capability flags changed while exercising supported modes");
        const bool supported = mode == PresentMode::VSync ||
            (mode == PresentMode::Immediate && before.immediate_supported) ||
            (mode == PresentMode::Mailbox && before.mailbox_supported);
        Json result{{"mode", mode_name(mode)}, {"supported", supported},
                    {"exercised", false}, {"fps_limit_claimed", false}};
        if (!supported) {
            result["skip_reason"] = "SDL GPU window reports presentation mode unsupported";
            result["presentation_status"] = presentation_status_json(before);
            results.push_back(std::move(result));
            continue;
        }

        const PresentationRequest request{mode, 0, 2};
        const bool applied_mode = renderer.set_presentation(request);
        require(applied_mode, renderer.presentation_status().error);
        const auto& applied = renderer.presentation_status();
        require(applied.available && applied.requested == request && applied.effective == request &&
                    applied.immediate_supported == capabilities.immediate_supported &&
                    applied.mailbox_supported == capabilities.mailbox_supported &&
                    applied.reason == PresentationUnavailableReason::None && applied.error.empty(),
                "Supported presentation mode did not become the effective renderer mode");
        renderer.render(frame);
        require(renderer.frame_telemetry().measured && renderer.frame_telemetry().submitted &&
                    renderer.frame_telemetry().presentation_queued,
                "Supported presentation mode did not submit and queue an actual window frame");
        const auto capture_path = output / ("presentation-" + std::string(mode_name(mode)) + ".png");
        renderer.render(frame, capture_path);
        auto surface = load_capture(capture_path);
        require_color(sample(surface.get(), window, renderer, {320.f, 240.f}),
                      {96, 170, 220, 255}, 3,
                      "Presentation-mode capture did not preserve known HDR unity color");
        validate_status(renderer.world_hdr_status(), drawable(window), 2);
        result["exercised"] = true;
        result["presentation_status"] = presentation_status_json(renderer.presentation_status());
        result["hdr_status"] = status_json(renderer.world_hdr_status());
        results.push_back(std::move(result));
    }

    const PresentationRequest restored{PresentMode::VSync, 0, 2};
    const bool restored_mode = renderer.set_presentation(restored);
    require(restored_mode, renderer.presentation_status().error);
    require(renderer.presentation_status().effective == restored,
            "Could not restore VSync with two frames in flight after presentation-mode proof");
    return {{"modes", std::move(results)},
            {"restored", presentation_status_json(renderer.presentation_status())},
            {"fps_limit_claimed", false}};
}

void resize_minimize_contract(Renderer2D& renderer, SDL_Window* window,
                              const std::filesystem::path& output, Json& report) {
    auto frame = hdr_frame();
    add_quad(frame, {180.f, 130.f, 280.f, 220.f}, {128, 180, 220, 255});
    require(SDL_SetWindowSize(window, 800, 600), SDL_GetError());
    settle_window(window, 800, 600);
    renderer.render(frame, output / "resized-800x600.png");
    const auto resized = drawable(window);
    validate_status(renderer.world_hdr_status(), resized, 2);
    require(SDL_MinimizeWindow(window), SDL_GetError());
    for (int attempt = 0; attempt < 100 && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED); ++attempt) {
        SDL_PumpEvents(); SDL_Delay(10);
    }
    require(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED, "Window did not minimize");
    renderer.render(frame);
    require(renderer.frame_telemetry().measured && !renderer.frame_telemetry().capture &&
                !renderer.frame_telemetry().submitted && !renderer.frame_telemetry().presentation_queued,
            "Minimized no-capture render submitted or queued unused GPU work");
    renderer.render(frame, output / "minimized-offscreen.png");
    require(renderer.frame_telemetry().measured && renderer.frame_telemetry().capture &&
                renderer.frame_telemetry().submitted && !renderer.frame_telemetry().presentation_queued,
            "Minimized explicit capture did not submit offscreen work without presentation");
    require(SDL_RestoreWindow(window), SDL_GetError());
    settle_window(window, 800, 600);
    renderer.render(frame, output / "restored-800x600.png");
    validate_status(renderer.world_hdr_status(), drawable(window), 2);
    report["resized_status"] = status_json(renderer.world_hdr_status());
}

void fault_rollback_contract(std::uint32_t injection, const std::filesystem::path& assets,
                             const std::filesystem::path& output, Json& results) {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow(("Ribterra HDR fault " + std::to_string(injection)).c_str(),
                         640, 480, SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    settle_window(window.get(), 640, 480);
    RendererOptions options;
    options.backend = Backend::Direct3D12;
    options.test_fail_hdr_target_creation_at = injection;
    Renderer2D renderer(window.get(), assets, options);
    auto frame = hdr_frame();
    add_quad(frame, {180.f, 130.f, 280.f, 220.f}, {180, 120, 80, 255});
    renderer.render(frame);
    const auto before = renderer.world_hdr_status();
    validate_status(before, drawable(window.get()), 2);
    require(SDL_SetWindowSize(window.get(), 800, 600), SDL_GetError());
    settle_window(window.get(), 800, 600);
    const auto failed_size = drawable(window.get());
    const auto failed_kind = injection == 5 ? "world RGBA16F" : "final RGBA8";
    const auto failed_set = injection == 8 ? 1u : 0u;
    const auto expected_failure = "Injected target creation failure: HDR " + std::string(failed_kind) +
        " COLOR_TARGET|SAMPLER " + std::to_string(failed_size.width) + "x" +
        std::to_string(failed_size.height) + " frame-set " + std::to_string(failed_set);
    bool failed = false;
    try {
        renderer.render(frame);
    } catch (const std::runtime_error& error) {
        if (error.what() != expected_failure) throw;
        failed = true;
    }
    require(failed, "Injected HDR target creation failure did not fire");
    const auto after_failure = renderer.world_hdr_status();
    require(after_failure.width == before.width && after_failure.height == before.height &&
                after_failure.frame_sets == before.frame_sets &&
                after_failure.attachment_bytes == before.attachment_bytes,
            "Failed HDR resize did not preserve the prior complete target set");
    require(SDL_SetWindowSize(window.get(), 640, 480), SDL_GetError());
    settle_window(window.get(), 640, 480);
    for (const auto slot : {0u, 1u}) {
        const auto old_path = output / ("fault-" + std::to_string(injection) +
                                        "-retained-slot-" + std::to_string(slot) + ".png");
        renderer.render(frame, old_path);
        auto old_surface = load_capture(old_path);
        require_color(sample(old_surface.get(), window.get(), renderer, {320.f, 240.f}),
                      {180, 120, 80, 255}, 3,
                      "Retained HDR frame slot did not preserve known quad color");
        require_color(sample(old_surface.get(), window.get(), renderer, {20.f, 20.f}),
                      {0, 0, 0, 255}, 0,
                      "Retained HDR frame slot did not preserve opaque black clear");
        validate_status(renderer.world_hdr_status(), drawable(window.get()), 2);
    }
    require(SDL_SetWindowSize(window.get(), 800, 600), SDL_GetError());
    settle_window(window.get(), 800, 600);
    const auto retry_path = output / ("fault-" + std::to_string(injection) + "-retry.png");
    renderer.render(frame, retry_path);
    auto retry_surface = load_capture(retry_path);
    require_color(sample(retry_surface.get(), window.get(), renderer, {320.f, 240.f}),
                  {180, 120, 80, 255}, 3,
                  "Retried HDR target set did not preserve known quad color");
    require_color(sample(retry_surface.get(), window.get(), renderer, {20.f, 20.f}),
                  {0, 0, 0, 255}, 0,
                  "Retried HDR target set did not preserve opaque black clear");
    validate_status(renderer.world_hdr_status(), drawable(window.get()), 2);
    results.push_back({{"injection", injection}, {"before", status_json(before)},
                       {"after_failure", status_json(after_failure)},
                       {"after_retry", status_json(renderer.world_hdr_status())}});
}

int native_probe(SDL_Window* window, const std::filesystem::path& assets,
                 const std::filesystem::path& output) {
    RendererOptions options;
    options.backend = Backend::Direct3D12;
    options.collect_telemetry = true;
    Renderer2D renderer(window, assets, options);
    require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
            "HDR color proof requires native Direct3D 12");
    require(!renderer.lighting_status().dynamic_available,
            "HDR color proof must not claim full Dynamic lighting");
    auto initialize = hdr_frame();
    renderer.render(initialize);
    validate_status(renderer.world_hdr_status(), drawable(window), 2);
    invalid_contracts(renderer);
    const auto asynchronous_owner = asynchronous_owner_contract(renderer);
    Json samples = Json::object();
    numeric_contracts(renderer, window, output, samples);
    exposure_ui_contract(renderer, window, output, samples);
    minimal_frame_contracts(renderer, window, output, samples);
    const auto frame_sets = frame_count_contract(renderer, window, output);
    const auto presentation_modes = presentation_mode_contract(renderer, window, output);
    Json report{{"schema", 1}, {"build", RIBTERRA_PROBE_BUILD},
                {"platform", SDL_GetPlatform()},
                {"backend", renderer.device_info().backend},
                {"device", renderer.device_info().device},
                {"driver", renderer.device_info().driver},
                {"software", renderer.device_info().software},
                {"drawable", {drawable(window).width, drawable(window).height}},
                {"dynamic_mode_available", renderer.lighting_status().dynamic_available},
                {"initial_hdr_status", status_json(renderer.world_hdr_status())},
                {"samples", samples}, {"asynchronous_gpu_ownership", asynchronous_owner},
                {"frame_set_contracts", frame_sets}, {"presentation_modes", presentation_modes},
                {"fault_rollbacks", Json::array()},
                {"capture_format", "renderer final RGBA8 PNG readback"},
                {"claims", {"linear HDR world", "SDR encode", "world alpha in linear space",
                             "encoded premultiplied alpha conversion", "exposure", "UI isolation",
                             "surface kernel feeds HDR", "painter order", "resize rollback"}},
                {"not_claimed", {"full Dynamic lighting", "concept fidelity", "performance",
                                 "display scan-out", "HDR monitor output"}}};
    resize_minimize_contract(renderer, window, output, report);
    for (const auto injection : {5u, 6u, 8u})
        fault_rollback_contract(injection, assets, output, report["fault_rollbacks"]);
    report["final_hdr_status"] = status_json(renderer.world_hdr_status());
    report["complete"] = true;
    const auto report_path = output / "hdr-color-report.json";
    std::ofstream stream(report_path, std::ios::binary);
    require(stream.good(), "Could not create HDR color report");
    stream << report.dump(2) << '\n';
    stream.flush();
    stream.close();
    require(stream.good(), "Could not finish HDR color report");
    std::cout << "HDR color native numeric/capture proof passed; artifacts preserved in "
              << utf8(output) << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool software = argc == 2 && std::string_view(argv[1]) == "--software-contract";
        require(software || argc == 2,
                "Usage: HdrColorProbe <new-output-directory> | --software-contract");
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
            SDL_CreateWindow("Ribterra HDR color proof", 640, 480,
                             SDL_WINDOW_HIGH_PIXEL_DENSITY), SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        settle_window(window.get(), 640, 480);
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

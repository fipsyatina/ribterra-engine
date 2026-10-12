#define main contact_shadow_probe_entry
#include "contact_shadow_probe.cpp"
#undef main

#include "ribterra/moon_lighting.hpp"

#include <bit>
#include <tuple>

namespace sky_probe {
using namespace ribterra::render;

constexpr Point ui_sample{30.f, 28.f};
constexpr Pixel ui_color{215, 42, 76, 255};

struct SkyReporter {
    std::filesystem::path output;
    Json checks = Json::array();
    Json captures = Json::array();
    std::uint32_t capture_count{};
    std::uint32_t serial{};

    void pass(std::string name, Json detail = Json::object()) {
        checks.push_back({{"name", std::move(name)}, {"pass", true}, {"detail", std::move(detail)}});
    }
    std::filesystem::path fresh(std::string_view stem) {
        const auto path = output / (std::string(stem) + ".png");
        require(!std::filesystem::exists(path), "Sky probe output would overwrite retained evidence");
        require(++capture_count <= 40, "Sky probe exceeded its bounded 40-PNG evidence budget");
        return path;
    }
    void checkpoint(std::string_view phase, const Json& device = Json::object()) {
        const auto name = "sky-direct-progress-" + std::to_string(serial++) + ".json";
        write_json_new(output / name, {{"schema", 1}, {"complete", false}, {"phase", phase},
            {"build", RIBTERRA_PROBE_BUILD}, {"device", device}, {"checks", checks},
            {"captures", captures}, {"png_count", capture_count}});
    }
};

struct Captured {
    Image image;
    std::vector<std::uint8_t> file_bytes;
    RendererFrameTelemetry telemetry;
    std::string name;
};

RendererOptions sky_options() {
    auto options = contact_options();
    options.world_graph = WorldGraph::SkyContactSlice;
    return options;
}

Captured capture(Renderer2D& renderer, const Frame2D& frame, SkyReporter& report,
                 std::string_view name) {
    pump_events();
    const auto path = report.fresh(name);
    renderer.render(frame, path);
    Captured result{load_png(path), read_bytes(path), renderer.frame_telemetry(), std::string(name)};
    require(result.telemetry.submitted && result.telemetry.capture,
            "Sky diagnostic capture was not submitted and read back");
    report.captures.push_back({{"name", result.name}, {"file", path.filename().string()},
        {"width", result.image.width}, {"height", result.image.height},
        {"bytes", result.file_bytes.size()}, {"telemetry", telemetry_json(result.telemetry)}});
    return result;
}

std::array<std::uint8_t, 4> encoded_normal(std::array<float, 3> normal) {
    normal = normalized(normal);
    const float denominator = std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]);
    const auto encode = [denominator](float value) {
        return static_cast<std::uint8_t>(std::lround((value / denominator * .5f + .5f) * 255.f));
    };
    return {encode(normal[0]), encode(normal[1]), 255u, 255u};
}

struct Fixture {
    TextureHandle ground{};
    MaterialHandle plus{}, minus{}, flat{}, emissive{};
    ContactArtHandle contact{};
};

MaterialHandle make_material(Renderer2D& renderer, std::array<float, 3> normal,
                             bool emissive = false) {
    const std::array<std::uint8_t, 4> albedo{144, 144, 144, 255};
    const auto normal_bytes = encoded_normal(normal);
    const std::array<std::uint8_t, 2> depth{0, 0};
    const std::array<std::uint8_t, 1> emission{static_cast<std::uint8_t>(emissive ? 255 : 0)};
    const std::array<std::uint8_t, 4> surface{0, 0, 0, 0};
    const std::array<std::uint8_t, 1> caster{0};
    SpriteMaterialDesc desc;
    desc.channels[0].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, albedo);
    desc.channels[1].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8Unorm}, normal_bytes);
    desc.channels[2].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R16Unorm}, depth);
    desc.channels[3].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R8Unorm}, emission);
    desc.channels[4].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8Unorm}, surface);
    desc.channels[5].texture = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::R8Unorm}, caster);
    for (auto& channel : desc.channels) channel.uv = {0.f, 0.f, 1.f, 1.f};
    desc.emission = emissive ? std::array<float, 4>{1.f, .32f, .08f, .75f}
                             : std::array<float, 4>{0.f, 0.f, 0.f, 0.f};
    return renderer.create_sprite_material(desc);
}

Fixture make_fixture(Renderer2D& renderer) {
    Fixture fixture;
    const std::array<std::uint8_t, 4> ground{144, 144, 144, 255};
    fixture.ground = renderer.create_sampled_texture(
        {{1, 1}, SampledTextureFormat::Rgba8UnormSrgb}, ground);
    constexpr std::array<float, 3> plus{.8f, 0.f, .6f};
    constexpr std::array<float, 3> minus{-.8f, 0.f, .6f};
    fixture.plus = make_material(renderer, plus);
    fixture.minus = make_material(renderer, minus);
    fixture.flat = make_material(renderer, {0.f, 0.f, 1.f});
    fixture.emissive = make_material(renderer, {0.f, 0.f, 1.f}, true);
    std::array<std::uint8_t, 25> mask{};
    for (std::uint32_t y = 1; y < 4; ++y)
        for (std::uint32_t x = 1; x < 4; ++x) mask[y * 5u + x] = 255;
    fixture.contact = renderer.create_contact_art(
        {{5, 5, 0, 0, 5, 5}, ContactFacing::Omnidirectional}, mask);
    return fixture;
}

struct Samples {
    Point ground_plus{}, ground_minus{}, ground_flat{};
    Point sprite_plus{105.f, 365.f}, sprite_minus{445.f, 365.f}, sprite_flat{275.f, 365.f};
};

void append_sprite(Frame2D& frame, MaterialHandle material, Rect rectangle,
                   std::array<float, 3> world, bool emission = false) {
    SpriteMaterialDraw typed;
    typed.material = material;
    for (auto& region : typed.surface.uv_regions) region = {0.f, 0.f, 1.f, 1.f};
    typed.surface.anchor_export = {world[0], world[1], world[2], 64.f};
    typed.surface.source_size_pivot = {1.f, 1.f, .5f, .5f};
    typed.surface.depth_scale = {0.f, 1.f, 1.f, 0.f};
    typed.surface.emission = emission ? std::array<float, 4>{1.f, .32f, .08f, .75f}
                                      : std::array<float, 4>{0.f, 0.f, 0.f, 0.f};
    typed.surface.response = {0.f, 0.f, 0.f, 0.f};
    frame.sprite_material_draws.push_back(typed);
    const auto material_index = static_cast<std::uint32_t>(frame.sprite_material_draws.size());
    const std::uint32_t base = static_cast<std::uint32_t>(frame.vertices.size());
    for (const auto& vertex : std::array<Vertex, 4>{{
             {{rectangle.x, rectangle.y}, {0, 0}, {255, 255, 255, 255}},
             {{rectangle.x + rectangle.w, rectangle.y}, {1, 0}, {255, 255, 255, 255}},
             {{rectangle.x + rectangle.w, rectangle.y + rectangle.h}, {1, 1}, {255, 255, 255, 255}},
             {{rectangle.x, rectangle.y + rectangle.h}, {0, 1}, {255, 255, 255, 255}}}}) {
        frame.vertices.push_back(vertex); frame.surface_vertices.push_back({});
    }
    const std::uint32_t first = static_cast<std::uint32_t>(frame.indices.size());
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) frame.indices.push_back(base + index);
    GeometryDraw draw;
    draw.pass = Pass::WorldOrdered; draw.blend = Blend::Alpha;
    draw.first_index = first; draw.index_count = 6; draw.sprite_material_index = material_index;
    frame.commands.emplace_back(draw);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // CPU-only wrapper embeds the strictly aligned MoonLightingGpu ABI.
#endif
struct LightState {
    std::array<float, 3> sun_direction{0.f, 0.f, 1.f};
    std::array<float, 3> sun_color{1.f, .8f, .55f};
    float sun_intensity{};
    MoonLightingGpu moon{};
    float ambient{};
    float exposure{.72f};
    bool flat_sun_visible{true};
    bool local{};
    bool emission{};
    bool contact{};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

Frame2D scene(const Fixture& fixture, const LightState& state, Samples& samples) {
    Frame2D frame;
    frame.logical_size = logical_size; frame.clear = {8, 12, 18, 255};
    frame.linear_hdr_world = true; frame.world_exposure = state.exposure;
    frame.contact_projection = canonical_projection; frame.moon_lighting = state.moon;
    frame.surface_lighting.ambient = {state.ambient, state.ambient, state.ambient, 0.f};
    frame.surface_lighting.sun_direction = {state.sun_direction[0], state.sun_direction[1],
                                            state.sun_direction[2], 0.f};
    frame.surface_lighting.sun_color_intensity = {state.sun_color[0], state.sun_color[1],
                                                  state.sun_color[2], state.sun_intensity};
    if (state.local) {
        frame.surface_lighting.counts[0] = 1;
        auto& light = frame.surface_lighting.lights[0];
        light.anchor_radius = {0.f, 0.f, 2.f, 8.f};
        light.color_intensity = {1.f, .35f, .12f, .8f};
    }
    const ReceiverSpec plus{{-2.f, -1.f, 0.f}, {0.f, 1.f, 0.f}, {-.6f, 0.f, .8f}};
    const ReceiverSpec minus{{2.f, -1.f, 0.f}, {0.f, 1.f, 0.f}, {-.6f, 0.f, -.8f}};
    const ReceiverSpec flat{{0.f, 2.f, 0.f}};
    append_receiver(frame, fixture.ground, plus);
    append_receiver(frame, fixture.ground, minus);
    append_receiver(frame, fixture.ground, flat);
    for (auto& material : frame.ground_material_draws) material.surface.response = {1.f, 0.f, 0.f, 1.f};
    frame.ground_material_draws[2].surface.visibility[1] = state.flat_sun_visible ? 1.f : 0.f;
    samples.ground_plus = projected(plus.center); samples.ground_minus = projected(minus.center);
    samples.ground_flat = projected(flat.center);
    append_sprite(frame, fixture.plus, {75, 335, 60, 60}, {-2.f, 1.f, 0.f});
    append_sprite(frame, fixture.minus, {415, 335, 60, 60}, {2.f, 1.f, 0.f});
    append_sprite(frame, state.emission ? fixture.emissive : fixture.flat,
                  {245, 335, 60, 60}, {0.f, 1.f, 0.f}, state.emission);
    if (state.contact) {
        const ContactArtDesc desc{{5, 5, 0, 0, 5, 5}, ContactFacing::Omnidirectional};
        frame.contact_shadows.push_back(contact_draw(fixture.contact, desc, plus, 901u));
    }
    append_ui(frame);
    return frame;
}

Json sample_json(const Captured& capture_value, const Samples& points) {
    return {{"ground_plus", pixel_json(logical_pixel(capture_value.image, points.ground_plus))},
        {"ground_minus", pixel_json(logical_pixel(capture_value.image, points.ground_minus))},
        {"ground_flat", pixel_json(logical_pixel(capture_value.image, points.ground_flat))},
        {"sprite_plus", pixel_json(logical_pixel(capture_value.image, points.sprite_plus))},
        {"sprite_minus", pixel_json(logical_pixel(capture_value.image, points.sprite_minus))},
        {"sprite_flat", pixel_json(logical_pixel(capture_value.image, points.sprite_flat))},
        {"ui", pixel_json(logical_pixel(capture_value.image, ui_sample))}};
}

int lightness(const Captured& value, Point point_value) {
    return brightness(logical_pixel(value.image, point_value));
}
void require_ui(const Captured& value) {
    require(logical_pixel(value.image, ui_sample) == ui_color, "Sky source changed exact UI color");
}
void require_graph(const Captured& value, bool populated_contact) {
    const std::array<WorldGraphPass, 6> populated{
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundContact,
        WorldGraphPass::GroundLighting, WorldGraphPass::WorldForward,
        WorldGraphPass::Composite, WorldGraphPass::Ui};
    const std::array<WorldGraphPass, 5> empty{
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward, WorldGraphPass::Composite, WorldGraphPass::Ui};
    if (populated_contact) {
        require(value.telemetry.graph_pass_count == populated.size(),
                "Populated SkyContactSlice did not encode six passes");
        for (std::size_t index = 0; index < populated.size(); ++index)
            require(value.telemetry.graph_passes[index] == populated[index],
                    "Populated SkyContactSlice pass order changed");
    } else {
        require(value.telemetry.graph_pass_count == empty.size(),
                "Empty SkyContactSlice did not skip contact and encode five passes");
        for (std::size_t index = 0; index < empty.size(); ++index)
            require(value.telemetry.graph_passes[index] == empty[index],
                    "Empty SkyContactSlice pass order changed");
    }
}

float srgb_linear(std::uint8_t byte) {
    const float encoded = float(byte) / 255.f;
    return encoded <= .04045f ? encoded / 12.92f : std::pow((encoded + .055f) / 1.055f, 2.4f);
}
void require_additive(const Captured& dark, const Captured& sun, const Captured& moon,
                      const Captured& combined, Point point_value, std::string_view label) {
    const auto d = logical_pixel(dark.image, point_value);
    const auto s = logical_pixel(sun.image, point_value);
    const auto m = logical_pixel(moon.image, point_value);
    const auto c = logical_pixel(combined.image, point_value);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float expected = srgb_linear(s[channel]) + srgb_linear(m[channel]) - srgb_linear(d[channel]);
        require(std::abs(srgb_linear(c[channel]) - expected) <= .025f,
                std::string(label) + " sun+moon output was not additive in linear light");
    }
}

Json production(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    Renderer2D renderer(window.value.get(), assets, sky_options());
    renderer.set_logical_size(logical_size);
    const auto fixture = make_fixture(renderer);
    Samples points;
    constexpr std::array<float, 3> plus{.8f, 0.f, .6f};
    constexpr std::array<float, 3> minus{-.8f, 0.f, .6f};

    LightState sun; sun.sun_direction = plus; sun.sun_intensity = .8f;
    sun.moon.direction = {minus[0], minus[1], minus[2], 0.f};
    sun.moon.color_intensity[3] = 0.f;
    LightState moon = sun; moon.sun_intensity = 0.f;
    moon.moon.color_intensity = {.55f, .68f, 1.f, .8f};
    const auto a0 = capture(renderer, scene(fixture, sun, points), report, "sun-a0");
    const auto b1 = capture(renderer, scene(fixture, moon, points), report, "moon-b1");
    const auto b0 = capture(renderer, scene(fixture, moon, points), report, "moon-b0");
    const auto a1 = capture(renderer, scene(fixture, sun, points), report, "sun-a1");
    require(a0.telemetry.frame_slot == 0 && b1.telemetry.frame_slot == 1 &&
            b0.telemetry.frame_slot == 0 && a1.telemetry.frame_slot == 1,
            "Two-slot A/B/B/A sky state ordering changed");
    require(a0.image.rgba == a1.image.rgba && a0.file_bytes == a1.file_bytes,
            "Sun state was not byte-exact after cross-slot reuse");
    require(b1.image.rgba == b0.image.rgba && b1.file_bytes == b0.file_bytes,
            "Moon state was not byte-exact after cross-slot reuse");
    require(lightness(a0, points.ground_plus) > lightness(a0, points.ground_minus) + 24 &&
            lightness(b1, points.ground_minus) > lightness(b1, points.ground_plus) + 24,
            "Ground normals did not causally reverse independent sun/moon response");
    require(lightness(a0, points.sprite_plus) > lightness(a0, points.sprite_minus) + 24 &&
            lightness(b1, points.sprite_minus) > lightness(b1, points.sprite_plus) + 24,
            "Six-channel typed sprite normals did not reverse independent sun/moon response");
    for (const auto* capture_value : {&a0, &b1, &b0, &a1}) require_ui(*capture_value);
    report.pass("production-two-slot-opposite-normal-causality", {{"sun", sample_json(a0, points)},
        {"moon", sample_json(b1, points)}, {"slots", {0, 1, 0, 1}},
        {"fixture_scope", "six 1x1 typed channels prove only the causal direct path; no art/category acceptance"}});

    LightState moon_plus = moon;
    moon_plus.moon.direction = {plus[0], plus[1], plus[2], 0.f};
    const auto moved = capture(renderer, scene(fixture, moon_plus, points), report, "moon-direction-plus-x");
    require(lightness(moved, points.ground_plus) > lightness(moved, points.ground_minus) + 24 &&
            lightness(moved, points.sprite_plus) > lightness(moved, points.sprite_minus) + 24,
            "Moving moon direction from -X to +X did not move direct response");
    require_ui(moved);
    report.pass("moon-direction-opposite-x", sample_json(moved, points));

    LightState invisible_sun = sun; invisible_sun.sun_direction = {0.f, 0.f, 1.f};
    invisible_sun.flat_sun_visible = false;
    LightState visible_moon = invisible_sun; visible_moon.sun_intensity = 0.f;
    visible_moon.moon.direction = {0.f, 0.f, 1.f, 0.f};
    visible_moon.moon.color_intensity = {.55f, .68f, 1.f, .8f};
    const auto blocked = capture(renderer, scene(fixture, invisible_sun, points), report, "sun-visibility-zero");
    const auto unblocked = capture(renderer, scene(fixture, visible_moon, points), report, "moon-ignores-sun-visibility");
    LightState visible_sun = invisible_sun; visible_sun.flat_sun_visible = true;
    LightState moon_visibility_one = visible_moon; moon_visibility_one.flat_sun_visible = true;
    const auto sun_visibility_one = capture(renderer, scene(fixture, visible_sun, points), report,
                                             "sun-visibility-one");
    const auto moon_visibility_one_capture = capture(renderer, scene(fixture, moon_visibility_one, points), report,
                                                      "moon-with-sun-visibility-one");
    require(lightness(sun_visibility_one, points.ground_flat) >
            lightness(blocked, points.ground_flat) + 32,
            "Ground sun visibility one-to-zero control did not remove positive sun light");
    require(lightness(unblocked, points.ground_flat) > lightness(blocked, points.ground_flat) + 32,
            "Ground sun visibility zero incorrectly hid independent moon light");
    require(unblocked.image.rgba == moon_visibility_one_capture.image.rgba &&
            unblocked.file_bytes == moon_visibility_one_capture.file_bytes,
            "Changing sun visibility changed moon-only output");
    report.pass("source-specific-ground-visibility", {{"sun_zero", sample_json(blocked, points)},
        {"sun_one", sample_json(sun_visibility_one, points)},
        {"moon_sun_visibility_zero", sample_json(unblocked, points)},
        {"moon_sun_visibility_one", sample_json(moon_visibility_one_capture, points)},
        {"moon_visibility_pair_byte_exact", true}});

    LightState ambient; ambient.ambient = .75f;
    LightState ambient_contact = ambient; ambient_contact.contact = true;
    const auto ambient_open = capture(renderer, scene(fixture, ambient, points), report, "ambient-no-contact");
    const auto ambient_shadow = capture(renderer, scene(fixture, ambient_contact, points), report, "ambient-contact");
    require(lightness(ambient_open, points.ground_plus) > lightness(ambient_shadow, points.ground_plus) + 24,
            "Authored contact did not reduce ambient at the receiver");
    LightState direct = sun; direct.sun_direction = plus;
    LightState direct_contact = direct; direct_contact.contact = true;
    const auto direct_open = capture(renderer, scene(fixture, direct, points), report, "sky-no-contact");
    const auto direct_shadow = capture(renderer, scene(fixture, direct_contact, points), report, "sky-contact");
    require_graph(direct_open, false); require_graph(direct_shadow, true);
    require(direct_open.image.rgba == direct_shadow.image.rgba,
            "Contact altered direct sky output instead of ambient/AO only");
    LightState moon_direct = moon;
    moon_direct.moon.direction = {plus[0], plus[1], plus[2], 0.f};
    LightState moon_direct_contact = moon_direct; moon_direct_contact.contact = true;
    const auto moon_direct_open = capture(renderer, scene(fixture, moon_direct, points), report,
                                          "moon-no-contact");
    const auto moon_direct_shadow = capture(renderer, scene(fixture, moon_direct_contact, points), report,
                                            "moon-contact");
    require(lightness(moon_direct_open, points.ground_plus) > 32,
            "Moon-only contact control was vacuous at the masked receiver");
    require(moon_direct_open.image.rgba == moon_direct_shadow.image.rgba,
            "Contact altered independent moon output instead of ambient/AO only");
    report.pass("contact-is-ambient-only", {{"ambient_open", sample_json(ambient_open, points)},
        {"ambient_contact", sample_json(ambient_shadow, points)},
        {"sun_byte_exact", direct_open.image.rgba == direct_shadow.image.rgba},
        {"moon_open", sample_json(moon_direct_open, points)},
        {"moon_byte_exact", moon_direct_open.image.rgba == moon_direct_shadow.image.rgba}});

    LightState dark; dark.exposure = .45f;
    LightState sun_only = dark; sun_only.sun_direction = {0.f, 0.f, 1.f}; sun_only.sun_intensity = .34f;
    LightState moon_only = dark; moon_only.moon.direction = {0.f, 0.f, 1.f, 0.f};
    moon_only.moon.color_intensity = {.55f, .68f, 1.f, .28f};
    LightState twilight = sun_only; twilight.moon = moon_only.moon;
    const auto dark_capture = capture(renderer, scene(fixture, dark, points), report, "twilight-dark");
    const auto sun_capture = capture(renderer, scene(fixture, sun_only, points), report, "twilight-sun");
    const auto moon_capture = capture(renderer, scene(fixture, moon_only, points), report, "twilight-moon");
    const auto combined = capture(renderer, scene(fixture, twilight, points), report, "twilight-combined");
    require_additive(dark_capture, sun_capture, moon_capture, combined, points.ground_flat, "Ground");
    require_additive(dark_capture, sun_capture, moon_capture, combined, points.sprite_flat, "Typed sprite");
    for (const auto* value : {&dark_capture, &sun_capture, &moon_capture, &combined}) require_ui(*value);
    report.pass("twilight-linear-additivity", {{"tolerance", .025},
        {"dark", sample_json(dark_capture, points)}, {"sun", sample_json(sun_capture, points)},
        {"moon", sample_json(moon_capture, points)}, {"combined", sample_json(combined, points)}});

    LightState isolation_dark;
    LightState local_only = isolation_dark; local_only.local = true;
    LightState emission_only = isolation_dark; emission_only.emission = true;
    const auto isolation_dark_capture = capture(renderer, scene(fixture, isolation_dark, points), report,
                                                 "zero-sky-dark-control");
    const auto local_capture = capture(renderer, scene(fixture, local_only, points), report,
                                       "zero-sky-local-only");
    const auto emission_capture = capture(renderer, scene(fixture, emission_only, points), report,
                                          "zero-sky-emission-only");
    for (const auto* value : {&isolation_dark_capture, &local_capture, &emission_capture}) require_ui(*value);
    require(lightness(local_capture, points.ground_flat) >
            lightness(isolation_dark_capture, points.ground_flat) + 16,
            "Same-exposure local-only ground control was vacuous");
    require(lightness(emission_capture, points.sprite_flat) >
            lightness(isolation_dark_capture, points.sprite_flat) + 16,
            "Same-exposure emission-only typed-sprite control was vacuous");
    report.pass("zero-sky-preserves-local-emission-ui", {{"dark", sample_json(isolation_dark_capture, points)},
        {"local_only", sample_json(local_capture, points)},
        {"emission_only", sample_json(emission_capture, points)},
        {"shared_exposure", isolation_dark.exposure}, {"minimum_brightness_delta", 16},
        {"ui_exact", true}});

    SDL_SetWindowSize(window.value.get(), 800, 600); require(SDL_SyncWindow(window.value.get()), "Resize failed");
    const auto resized = capture(renderer, scene(fixture, sun, points), report, "sun-resized-800x600");
    SDL_SetWindowSize(window.value.get(), 640, 480); require(SDL_SyncWindow(window.value.get()), "Restore failed");
    const auto restored = capture(renderer, scene(fixture, sun, points), report, "sun-restored-640x480");
    require(restored.image.rgba == a0.image.rgba && restored.file_bytes == a0.file_bytes,
            "Sky graph did not recover byte-exact pixels after resize/restore");
    require_ui(resized); require_ui(restored);
    report.pass("resize-restore", {{"resized", {resized.image.width, resized.image.height}},
        {"restored", {restored.image.width, restored.image.height}},
        {"restored_byte_exact", true}});

    return {{"device", device_json(renderer.device_info())}, {"samples", sample_json(local_capture, points)},
        {"capture_count", report.capture_count}, {"ground_status_frame_sets", renderer.ground_forward_status().frame_sets}};
}

Frame2D compatibility_scene(const Fixture& fixture, bool sky, Samples& points) {
    LightState state; state.local = true; state.emission = true;
    auto frame = scene(fixture, state, points);
    if (!sky) frame.moon_lighting.reset();
    return frame;
}

Json compatibility(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    Captured old_capture;
    Samples old_points;
    {
        Renderer2D renderer(window.value.get(), assets, contact_options());
        renderer.set_logical_size(logical_size);
        const auto fixture = make_fixture(renderer);
        old_capture = capture(renderer, compatibility_scene(fixture, false, old_points), report,
                              "ground-contact-empty-moon-baseline");
    }
    Captured sky_capture;
    Samples sky_points;
    {
        Renderer2D renderer(window.value.get(), assets, sky_options());
        renderer.set_logical_size(logical_size);
        const auto fixture = make_fixture(renderer);
        sky_capture = capture(renderer, compatibility_scene(fixture, true, sky_points), report,
                              "sky-contact-disabled-moon");
    }
    require(old_capture.image.rgba == sky_capture.image.rgba &&
            old_capture.file_bytes == sky_capture.file_bytes,
            "Disabled moon changed GroundContact local/emission/UI compatibility output");
    require_ui(old_capture); require_ui(sky_capture);
    report.pass("disabled-moon-old-contact-compatibility", {{"byte_exact", true},
        {"local", pixel_json(logical_pixel(sky_capture.image, sky_points.ground_flat))},
        {"emission", pixel_json(logical_pixel(sky_capture.image, sky_points.sprite_flat))},
        {"ui", pixel_json(logical_pixel(sky_capture.image, ui_sample))}});
    return {{"byte_exact", true}, {"old_slot", old_capture.telemetry.frame_slot},
            {"sky_slot", sky_capture.telemetry.frame_slot}};
}

template<class Mutator>
void reject_sky_frame(Renderer2D& renderer, const Fixture& fixture, Mutator&& mutate,
                      std::string_view label, Json& negatives) {
    Samples points;
    LightState state;
    auto frame = scene(fixture, state, points);
    mutate(frame);
    const auto message = expect_throw<std::invalid_argument>([&] { renderer.render(frame); },
        std::string(label) + " was not rejected before rendering");
    require(!renderer.has_submitted_frames(), std::string(label) + " reached GPU submission");
    negatives.push_back({{"case", label}, {"diagnostic", message}, {"submitted", false}});
}

Json invalid_frames(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    Json negatives = Json::array();
    {
        Renderer2D renderer(window.value.get(), assets, sky_options());
        renderer.set_logical_size(logical_size);
        const auto fixture = make_fixture(renderer);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting.reset(); },
                         "missing moon", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.contact_projection.reset(); },
                         "missing contact projection", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting->direction[0] =
            std::numeric_limits<float>::quiet_NaN(); }, "nonfinite moon", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting->direction[3] = 1.f; },
                         "reserved moon direction", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting->direction = {0,0,0,0}; },
                         "nonnormal moon direction", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting->color_intensity[0] = 1.01f; },
                         "moon RGB range", negatives);
        reject_sky_frame(renderer, fixture, [](Frame2D& frame) { frame.moon_lighting->color_intensity[3] = 64.01f; },
                         "moon intensity range", negatives);
        auto stale = fixture.flat;
        renderer.release_sprite_material(stale);
        reject_sky_frame(renderer, fixture, [stale](Frame2D& frame) {
            frame.sprite_material_draws[2].material = stale;
        }, "stale typed material", negatives);
    }
    for (const auto graph : {WorldGraph::LegacyCombined, WorldGraph::GroundForwardSlice,
                             WorldGraph::GroundContactSlice}) {
        RendererOptions options = sky_options(); options.world_graph = graph;
        Renderer2D renderer(window.value.get(), assets, options);
        Frame2D frame; frame.logical_size = logical_size; frame.moon_lighting = MoonLightingGpu{};
        if (graph == WorldGraph::GroundContactSlice) frame.contact_projection = canonical_projection;
        const auto message = expect_throw<std::invalid_argument>([&] { renderer.render(frame); },
            "Non-sky graph accepted moon payload");
        require(!renderer.has_submitted_frames(), "Legacy graph moon rejection submitted a frame");
        negatives.push_back({{"case", "moon payload on non-sky graph"},
            {"graph", static_cast<std::uint32_t>(graph)}, {"diagnostic", message}, {"submitted", false}});
    }
    report.pass("pre-gpu-negative-contracts", {{"count", negatives.size()}, {"cases", negatives}});
    return negatives;
}
} // namespace sky_probe

#ifndef RIBTERRA_SKY_DIRECT_PROBE_ENTRY
#define RIBTERRA_SKY_DIRECT_PROBE_ENTRY main
#endif
int RIBTERRA_SKY_DIRECT_PROBE_ENTRY(int argc, char** argv) {
    using namespace sky_probe;
    std::filesystem::path output;
    std::unique_ptr<SkyReporter> report;
    try {
        require(argc == 3, "Usage: SkyDirectProbe <assets-dir> <NEW-output-dir>");
        const auto assets = from_utf8(argv[1]); output = from_utf8(argv[2]);
        require(std::filesystem::is_directory(assets), "Sky probe assets directory is missing");
        require(!std::filesystem::exists(output), "Sky probe refuses an existing output root");
        require(std::filesystem::create_directories(output), "Could not create fresh sky probe output root");
        report = std::make_unique<SkyReporter>(); report->output = output;
        report->checkpoint("output-created-before-window");
        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        Window window("Ribterra independent sun/moon diagnostic");
        report->checkpoint("visible-window-created");
        const auto invalid = invalid_frames(window, assets, *report);
        report->checkpoint("pre-gpu-negatives-complete");
        const auto compatibility_result = compatibility(window, assets, *report);
        report->checkpoint("disabled-moon-compatibility-complete");
        const auto production_result = production(window, assets, *report);
        const Json final_report{{"schema", 1}, {"complete", true}, {"build", RIBTERRA_PROBE_BUILD},
            {"scope", "bounded native SkyContactSlice direct sun/moon/contact diagnostic"},
            {"claims", {"independent directional sun/moon causal pixels on ground and a direct six-channel typed fixture",
                         "source-specific sun visibility", "contact affects ambient only",
                         "disabled-moon GroundContact compatibility", "two-slot repeat and resize recovery"}},
            {"non_claims", {"final art or time curve", "weather/calendar simulation",
                             "authored directional silhouettes", "art category acceptance",
                             "sun/contact completeness", "full GameView", "performance or FPS",
                             "video or motion acceptance"}},
            {"default_moon_note", "RGB 0.55/0.68/1 with intensity zero is a diagnostic default, not final art/time behavior"},
            {"checks", report->checks}, {"captures", report->captures},
            {"png_count", report->capture_count}, {"invalid_frames", invalid},
            {"compatibility", compatibility_result}, {"production", production_result}};
        write_json_new(output / "sky-direct-report.json", final_report);
        std::cout << "Sky direct native probe: " << report->checks.size() << " checks, "
                  << report->capture_count << " immutable PNG captures\n";
        return 0;
    } catch (const std::exception& error) {
        try {
            if (report) write_json_new(output / ("sky-direct-failure-" + std::to_string(report->serial++) + ".json"),
                {{"schema", 1}, {"complete", false}, {"error", error.what()},
                 {"checks", report->checks}, {"captures", report->captures},
                 {"png_count", report->capture_count}});
        } catch (...) {}
        std::cerr << "Sky direct native probe failed: " << error.what() << '\n';
        return 1;
    }
}

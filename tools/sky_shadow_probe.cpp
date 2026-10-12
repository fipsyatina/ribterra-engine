#define RIBTERRA_SKY_DIRECT_PROBE_ENTRY sky_direct_probe_entry
#include "sky_direct_probe.cpp"
#undef RIBTERRA_SKY_DIRECT_PROBE_ENTRY

#include <limits>

namespace sky_shadow_probe {
using namespace ribterra::render;
using sky_probe::Captured;
using sky_probe::Fixture;
using sky_probe::SkyReporter;

constexpr float diagonal = .7071067811865475f;
constexpr std::array<float, 3> sun_direction{diagonal, 0.f, diagonal};
constexpr std::array<float, 3> moon_direction{-diagonal, 0.f, diagonal};

RendererOptions shadow_options() {
    auto options = sky_probe::sky_options();
    options.world_graph = WorldGraph::SkyShadowSlice;
    return options;
}

Json shadow_status_json(const SkyShadowStatus& status) {
    return {{"available", status.available}, {"d32_supported", status.d32_supported},
            {"map_size", status.map_size}, {"frame_sets", status.frame_sets},
            {"attachment_bytes", status.attachment_bytes},
            {"creation_attempts", status.creation_attempts},
            {"target_generation", status.target_generation},
            {"candidate_live_textures", status.candidate_live_textures},
            {"active_set_ids", status.active_set_ids}};
}

struct ShadowState {
    float sun_intensity{};
    float moon_intensity{};
    float ambient{};
    bool local{};
    bool emission{};
    bool contact{};
    bool flip_sun_direction{};
    float exposure{.72f};
};

struct ShadowPoints {
    Point ground_sun{}, ground_moved{}, ground_moon{};
    Point material_sun{100.f, 370.f};
    Point material_moved{320.f, 370.f};
    Point material_moon{540.f, 370.f};
};

SkyShadowTriangle triangle(std::array<float, 3> a, std::array<float, 3> b,
                           std::array<float, 3> c, std::uint32_t mask = 3u,
                           float reveal = 1.f) {
    SkyShadowTriangle result;
    result.vertices[0].position = a;
    result.vertices[1].position = b;
    result.vertices[2].position = c;
    result.source_mask = mask;
    result.reveal = reveal;
    return result;
}

std::vector<SkyShadowTriangle> caster_quad(std::uint32_t mask = 3u, float center_x = 0.f,
                                           float z = 2.f, bool reverse = false) {
    const std::array<float, 3> a{center_x - 1.f, -1.f, z};
    const std::array<float, 3> b{center_x + 1.f, -1.f, z};
    const std::array<float, 3> c{center_x + 1.f, 1.f, z};
    const std::array<float, 3> d{center_x - 1.f, 1.f, z};
    if (reverse)
        return {triangle(a, c, b, mask), triangle(a, d, c, mask)};
    return {triangle(a, b, c, mask), triangle(a, c, d, mask)};
}

Frame2D shadow_scene(const Fixture& fixture, const ShadowState& state,
                     const std::vector<SkyShadowTriangle>& casters, ShadowPoints& points) {
    Frame2D frame;
    frame.logical_size = logical_size;
    frame.clear = {8, 12, 18, 255};
    frame.linear_hdr_world = true;
    frame.world_exposure = state.exposure;
    frame.contact_projection = canonical_projection;
    frame.surface_lighting.ambient = {state.ambient, state.ambient, state.ambient, 0.f};
    const auto& active_sun = state.flip_sun_direction ? moon_direction : sun_direction;
    frame.surface_lighting.sun_direction = {active_sun[0], active_sun[1], active_sun[2], 0.f};
    frame.surface_lighting.sun_color_intensity = {1.f, .78f, .55f, state.sun_intensity};
    MoonLightingGpu moon;
    moon.direction = {moon_direction[0], moon_direction[1], moon_direction[2], 0.f};
    moon.color_intensity = {.55f, .68f, 1.f, state.moon_intensity};
    frame.moon_lighting = moon;
    if (state.local) {
        frame.surface_lighting.counts[0] = 1;
        frame.surface_lighting.lights[0].anchor_radius = {0.f, 0.f, 2.f, 8.f};
        frame.surface_lighting.lights[0].color_intensity = {1.f, .35f, .12f, .8f};
    }

    const ReceiverSpec left{{-2.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, .52f, .52f};
    const ReceiverSpec middle{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, .52f, .52f};
    const ReceiverSpec right{{2.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, .52f, .52f};
    append_receiver(frame, fixture.ground, left);
    append_receiver(frame, fixture.ground, middle);
    append_receiver(frame, fixture.ground, right);
    points.ground_sun = projected(left.center);
    points.ground_moved = projected(middle.center);
    points.ground_moon = projected(right.center);

    sky_probe::append_sprite(frame, fixture.flat, {70.f, 340.f, 60.f, 60.f}, left.center);
    sky_probe::append_sprite(frame, state.emission ? fixture.emissive : fixture.flat,
                             {290.f, 340.f, 60.f, 60.f}, middle.center, state.emission);
    sky_probe::append_sprite(frame, fixture.flat, {510.f, 340.f, 60.f, 60.f}, right.center);
    if (state.contact) {
        const ContactArtDesc desc{{5, 5, 0, 0, 5, 5}, ContactFacing::Omnidirectional};
        frame.contact_shadows.push_back(contact_draw(fixture.contact, desc, middle, 1901u));
    }
    frame.sky_shadow_triangles = casters;
    append_ui(frame);
    return frame;
}

Json samples_json(const Captured& value, const ShadowPoints& points) {
    return {{"ground_sun", pixel_json(logical_pixel(value.image, points.ground_sun))},
            {"ground_moved", pixel_json(logical_pixel(value.image, points.ground_moved))},
            {"ground_moon", pixel_json(logical_pixel(value.image, points.ground_moon))},
            {"material_sun", pixel_json(logical_pixel(value.image, points.material_sun))},
            {"material_moved", pixel_json(logical_pixel(value.image, points.material_moved))},
            {"material_moon", pixel_json(logical_pixel(value.image, points.material_moon))},
            {"ui", pixel_json(logical_pixel(value.image, sky_probe::ui_sample))}};
}

int shadow_lightness(const Captured& value, Point point) {
    return brightness(logical_pixel(value.image, point));
}

void require_darkened(const Captured& open, const Captured& shadowed, Point point,
                      std::string_view label) {
    const int open_value = shadow_lightness(open, point);
    const int shadowed_value = shadow_lightness(shadowed, point);
    require(open_value > 48, std::string(label) + " unshadowed control was vacuous");
    require(shadowed_value + 24 < open_value,
            std::string(label) + " did not causally darken by the required margin");
}

void require_exact(const Captured& first, const Captured& second, std::string_view label) {
    require(first.image.rgba == second.image.rgba && first.file_bytes == second.file_bytes,
            std::string(label) + " was not byte-exact");
}

void require_shadow_graph(const Captured& value, bool contact) {
    const std::array<WorldGraphPass, 8> populated{
        WorldGraphPass::SunShadow, WorldGraphPass::MoonShadow,
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundContact,
        WorldGraphPass::GroundLighting, WorldGraphPass::WorldForward,
        WorldGraphPass::Composite, WorldGraphPass::Ui};
    const std::array<WorldGraphPass, 7> empty{
        WorldGraphPass::SunShadow, WorldGraphPass::MoonShadow,
        WorldGraphPass::GroundGBuffer, WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward, WorldGraphPass::Composite, WorldGraphPass::Ui};
    const std::size_t expected = contact ? populated.size() : empty.size();
    require(value.telemetry.graph_pass_count == expected,
            "SkyShadowSlice graph pass count changed");
    for (std::size_t index = 0; index < expected; ++index) {
        const auto pass = contact ? populated[index] : empty[index];
        require(value.telemetry.graph_passes[index] == pass,
                "SkyShadowSlice graph pass order changed");
    }
}

Captured take(Renderer2D& renderer, const Frame2D& frame, SkyReporter& report,
              std::string_view name) {
    auto value = sky_probe::capture(renderer, frame, report, name);
    sky_probe::require_ui(value);
    return value;
}

Json compatibility(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    const ShadowState state{.sun_intensity = .72f, .moon_intensity = .64f,
                            .ambient = .2f, .local = true, .emission = true,
                            .contact = true};
    Captured old_graph;
    ShadowPoints old_points;
    {
        Renderer2D renderer(window.value.get(), assets, sky_probe::sky_options());
        renderer.set_logical_size(logical_size);
        const auto fixture = sky_probe::make_fixture(renderer);
        old_graph = take(renderer, shadow_scene(fixture, state, {}, old_points), report,
                         "sky-contact-zero-caster");
    }
    Captured shadow_graph;
    ShadowPoints shadow_points;
    Json status;
    {
        Renderer2D renderer(window.value.get(), assets, shadow_options());
        renderer.set_logical_size(logical_size);
        const auto fixture = sky_probe::make_fixture(renderer);
        shadow_graph = take(renderer, shadow_scene(fixture, state, {}, shadow_points), report,
                            "sky-shadow-zero-caster");
        require_shadow_graph(shadow_graph, true);
        const auto& current = renderer.sky_shadow_status();
        require(current.available && current.d32_supported && current.map_size == 1024u &&
                current.frame_sets == 2u && current.attachment_bytes == 16ull * 1024u * 1024u &&
                current.creation_attempts == 4u && current.target_generation == 1u &&
                current.candidate_live_textures == 0u && current.active_set_ids[0] != 0u &&
                current.active_set_ids[0] != current.active_set_ids[1],
                "Sky shadow target status disagrees with the fixed two-set D32 contract");
        status = shadow_status_json(current);
    }
    require_exact(old_graph, shadow_graph,
                  "Zero-caster SkyContact-to-SkyShadow compatibility image");
    report.pass("zero-caster-old-graph-compatibility",
                {{"byte_exact", true}, {"status", status},
                 {"samples", samples_json(shadow_graph, shadow_points)}});
    return {{"byte_exact", true}, {"status", status}};
}

template<class Mutator>
void reject_shadow_frame(Renderer2D& renderer, const Fixture& fixture, Mutator&& mutate,
                         std::string_view label, Json& negatives) {
    ShadowPoints points;
    ShadowState state;
    auto frame = shadow_scene(fixture, state, {}, points);
    mutate(frame);
    const auto diagnostic = expect_throw<std::invalid_argument>([&] { renderer.render(frame); },
        std::string(label) + " was not rejected");
    require(!renderer.has_submitted_frames() && !renderer.frame_telemetry().submitted,
            std::string(label) + " reached GPU submission");
    negatives.push_back({{"case", label}, {"diagnostic", diagnostic}, {"submitted", false}});
}

void boundary_checkpoint(SkyReporter& report, std::string_view phase) {
    const auto name = "sky-shadow-boundary-" + std::to_string(report.serial++) + ".json";
    write_json_new(report.output / name,
        {{"schema", 1}, {"complete", false}, {"phase", phase},
         {"build", RIBTERRA_PROBE_BUILD}, {"checks", report.checks},
         {"captures", report.captures}, {"png_count", report.capture_count}});
}

Json invalid_frames(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    Json negatives = Json::array();
    boundary_checkpoint(report, "invalid-before-shadow-renderer-constructor");
    {
        Renderer2D renderer(window.value.get(), assets, shadow_options());
        boundary_checkpoint(report, "invalid-after-shadow-renderer-constructor");
        renderer.set_logical_size(logical_size);
        boundary_checkpoint(report, "invalid-before-shadow-fixture");
        const auto fixture = sky_probe::make_fixture(renderer);
        boundary_checkpoint(report, "invalid-after-shadow-fixture");
        auto valid = caster_quad(1u).front();
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.vertices[0].position[0] = 32.01f;
            frame.sky_shadow_triangles = {valid};
        }, "out-of-cube reveal-positive triangle", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.reveal = 0.f;
            valid.vertices[0].position[0] = 32.01f;
            frame.sky_shadow_triangles = {valid};
        }, "out-of-cube reveal-zero triangle", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.source_mask = 0u;
            frame.sky_shadow_triangles = {valid};
        }, "zero source mask", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.source_mask = 4u;
            frame.sky_shadow_triangles = {valid};
        }, "unknown source mask", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.vertices[1].position[2] = std::numeric_limits<float>::quiet_NaN();
            frame.sky_shadow_triangles = {valid};
        }, "nonfinite vertex", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.reveal = std::numeric_limits<float>::quiet_NaN();
            frame.sky_shadow_triangles = {valid};
        }, "nonfinite reveal", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) mutable {
            valid.reveal = 1.001f;
            frame.sky_shadow_triangles = {valid};
        }, "reveal above one", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) {
            frame.sky_shadow_triangles.assign(sky_shadow_triangle_capacity + 1u, valid);
            for (auto& item : frame.sky_shadow_triangles) item.reveal = 0.f;
        }, "raw triangle overflow", negatives);
        reject_shadow_frame(renderer, fixture, [valid](Frame2D& frame) {
            frame.sky_shadow_triangles.assign(sky_shadow_active_triangle_capacity + 1u, valid);
        }, "active source overflow", negatives);
        boundary_checkpoint(report, "invalid-before-shadow-renderer-teardown");
    }
    boundary_checkpoint(report, "invalid-after-shadow-renderer-teardown");

    boundary_checkpoint(report, "invalid-before-old-renderer-constructor");
    {
        Renderer2D old_renderer(window.value.get(), assets, sky_probe::sky_options());
        boundary_checkpoint(report, "invalid-after-old-renderer-constructor");
        old_renderer.set_logical_size(logical_size);
        boundary_checkpoint(report, "invalid-before-old-fixture");
        const auto old_fixture = sky_probe::make_fixture(old_renderer);
        boundary_checkpoint(report, "invalid-after-old-fixture");
        ShadowPoints points;
        auto frame = shadow_scene(old_fixture, ShadowState{}, caster_quad(3u), points);
        const auto diagnostic = expect_throw<std::invalid_argument>(
            [&] { old_renderer.render(frame); }, "SkyContactSlice accepted sky shadow geometry");
        require(!old_renderer.has_submitted_frames(),
                "Old graph shadow-payload rejection reached GPU submission");
        negatives.push_back({{"case", "shadow payload on old SkyContactSlice"},
                             {"diagnostic", diagnostic}, {"submitted", false}});
        boundary_checkpoint(report, "invalid-before-old-renderer-teardown");
    }
    boundary_checkpoint(report, "invalid-after-old-renderer-teardown");
    report.pass("pre-command-negative-contracts",
                {{"count", negatives.size()}, {"cases", negatives}});
    return negatives;
}

Json allocation_failure(Window& window, const std::filesystem::path& assets,
                        SkyReporter& report) {
    auto options = shadow_options();
    options.test_fail_sky_target_creation_at = 3u;
    Renderer2D renderer(window.value.get(), assets, options);
    renderer.set_logical_size(logical_size);
    const auto fixture = sky_probe::make_fixture(renderer);
    ShadowPoints points;
    ShadowState state;
    state.sun_intensity = .8f;
    const auto frame = shadow_scene(fixture, state, caster_quad(1u), points);
    const auto before = renderer.sky_shadow_status();
    const auto diagnostic = expect_throw<std::runtime_error>([&] { renderer.render(frame); },
        "Injected sky target creation fault did not reject the frame");
    const auto failed = renderer.sky_shadow_status();
    require(diagnostic == "Injected sky target creation failure" &&
            failed.creation_attempts == 3u && failed.target_generation == before.target_generation &&
            failed.active_set_ids == before.active_set_ids && failed.candidate_live_textures == 0u &&
            !renderer.has_submitted_frames(),
            "Failed sky target candidate mutated public state or reached frame submission");
    const auto recovered = take(renderer, frame, report, "sky-target-retry");
    require_darkened(take(renderer, shadow_scene(fixture, state, {}, points), report,
                          "sky-target-retry-open"),
                     recovered, points.ground_sun, "Recovered target set");
    const auto after = renderer.sky_shadow_status();
    require(after.available && after.target_generation == 1u &&
            after.active_set_ids != before.active_set_ids &&
            after.candidate_live_textures == 0u && after.creation_attempts == 7u,
            "One-shot sky target fault did not retry into one committed four-map generation");
    report.pass("target-candidate-failure-atomic-retry",
                {{"before", shadow_status_json(before)}, {"diagnostic", diagnostic},
                 {"after_failure", shadow_status_json(failed)},
                 {"after_retry", shadow_status_json(after)}});
    return {{"diagnostic", diagnostic}, {"after_retry", shadow_status_json(after)}};
}

Json nearest_depth(Renderer2D& renderer, const Fixture& fixture, SkyReporter& report) {
    const auto make = [&](const std::vector<SkyShadowTriangle>& casters) {
        Frame2D frame;
        frame.logical_size = logical_size;
        frame.clear = {8, 12, 18, 255};
        frame.linear_hdr_world = true;
        frame.world_exposure = .72f;
        frame.contact_projection = canonical_projection;
        frame.surface_lighting.sun_direction = {sun_direction[0], sun_direction[1],
                                                sun_direction[2], 0.f};
        frame.surface_lighting.sun_color_intensity = {1.f, .78f, .55f, .8f};
        MoonLightingGpu moon;
        moon.direction = {moon_direction[0], moon_direction[1], moon_direction[2], 0.f};
        moon.color_intensity[3] = 0.f;
        frame.moon_lighting = moon;
        // SkyShadowSlice is a ground graph even when this oracle samples only
        // the typed receiver. Keep a real, visible ground draw well outside the
        // caster's Y footprint so it satisfies the graph contract without
        // participating in the nearest-depth comparison.
        const ReceiverSpec ground_control{{0.f, -4.f, 0.f}, {1.f, 0.f, 0.f},
                                          {0.f, 1.f, 0.f}, .4f, .4f};
        append_receiver(frame, fixture.ground, ground_control);
        sky_probe::append_sprite(frame, fixture.flat, {290.f, 340.f, 60.f, 60.f}, {1.f, 0.f, 3.f});
        frame.sky_shadow_triangles = casters;
        append_ui(frame);
        return frame;
    };
    const auto low = caster_quad(1u, 0.f, 2.f);
    const auto high = caster_quad(1u, 2.f, 4.f);
    auto both = low;
    both.insert(both.end(), high.begin(), high.end());
    const Point sample{320.f, 370.f};
    const auto low_only = take(renderer, make(low), report, "nearest-low-behind-receiver");
    const auto high_only = take(renderer, make(high), report, "nearest-high-before-receiver");
    const auto overlapping = take(renderer, make(both), report, "nearest-overlapping-depths");
    require_darkened(low_only, high_only, sample, "Source-nearest blocker");
    require_exact(high_only, overlapping,
                  "Overlapping nearer/farther caster depth ownership");
    report.pass("nearest-depth-overlap-causality",
                {{"low_only", pixel_json(logical_pixel(low_only.image, sample))},
                 {"high_only", pixel_json(logical_pixel(high_only.image, sample))},
                 {"both_match_high_byte_exact", true}});
    return {{"both_match_high_byte_exact", true}};
}

Json slope_causality(Renderer2D& renderer, const Fixture& fixture, SkyReporter& report) {
    const auto make = [&](const std::vector<SkyShadowTriangle>& casters) {
        Frame2D frame;
        frame.logical_size = logical_size;
        frame.clear = {8, 12, 18, 255};
        frame.linear_hdr_world = true;
        frame.world_exposure = .72f;
        frame.contact_projection = canonical_projection;
        frame.surface_lighting.sun_direction = {sun_direction[0], sun_direction[1],
                                                sun_direction[2], 0.f};
        frame.surface_lighting.sun_color_intensity = {1.f, .78f, .55f, .8f};
        MoonLightingGpu moon;
        moon.direction = {moon_direction[0], moon_direction[1], moon_direction[2], 0.f};
        moon.color_intensity[3] = 0.f;
        frame.moon_lighting = moon;
        const ReceiverSpec slope{{-2.f, 0.f, 0.f}, normalized({.6f, 0.f, -.8f}),
                                 {0.f, 1.f, 0.f}, .52f, .52f,
                                 ContactReceiver::Slope};
        append_receiver(frame, fixture.ground, slope);
        frame.sky_shadow_triangles = casters;
        append_ui(frame);
        return frame;
    };
    const Point sample = projected({-2.f, 0.f, 0.f});
    const auto open = take(renderer, make({}), report, "tilted-slope-open");
    const auto shadowed = take(renderer, make(caster_quad(1u)), report,
                               "tilted-slope-sun-shadow");
    require_darkened(open, shadowed, sample, "Tilted slope ground receiver");
    report.pass("tilted-slope-ground-causality",
                {{"normal", {.8f, 0.f, .6f}},
                 {"open", pixel_json(logical_pixel(open.image, sample))},
                 {"shadowed", pixel_json(logical_pixel(shadowed.image, sample))},
                 {"scope", "diagnostic slope only; not closed authored art coverage"}});
    return {{"open", pixel_json(logical_pixel(open.image, sample))},
            {"shadowed", pixel_json(logical_pixel(shadowed.image, sample))}};
}

Json production(Window& window, const std::filesystem::path& assets, SkyReporter& report) {
    Renderer2D renderer(window.value.get(), assets, shadow_options());
    renderer.set_logical_size(logical_size);
    const auto fixture = sky_probe::make_fixture(renderer);
    ShadowPoints points;
    ShadowState sun;
    sun.sun_intensity = .8f;
    const auto sun_caster = caster_quad(1u);
    const auto a0 = take(renderer, shadow_scene(fixture, sun, {}, points), report, "slot-a-open-0");
    const auto b1 = take(renderer, shadow_scene(fixture, sun, sun_caster, points), report,
                         "slot-b-sun-shadow-1");
    const auto b0 = take(renderer, shadow_scene(fixture, sun, sun_caster, points), report,
                         "slot-b-sun-shadow-0");
    const auto a1 = take(renderer, shadow_scene(fixture, sun, {}, points), report, "slot-a-open-1");
    require(a0.telemetry.frame_slot == 0u && b1.telemetry.frame_slot == 1u &&
            b0.telemetry.frame_slot == 0u && a1.telemetry.frame_slot == 1u,
            "Sky shadow frame sets did not follow open/shadow/shadow/open slots 0/1/0/1");
    require_exact(a0, a1, "Open state across both frame sets");
    require_exact(b1, b0, "Sun-shadow state across both frame sets");
    require_darkened(a0, b1, points.ground_sun, "Sun ground receiver");
    require_darkened(a0, b1, points.material_sun, "Sun typed-material receiver");
    require(logical_pixel(a0.image, points.ground_moon) ==
            logical_pixel(b1.image, points.ground_moon),
            "Sun caster changed the opposite ground control");
    require_shadow_graph(a0, false);
    require_shadow_graph(b1, false);
    report.pass("two-slot-sun-ground-material-causality",
                {{"states", {"open", "shadow", "shadow", "open"}},
                 {"actual_slots", {a0.telemetry.frame_slot, b1.telemetry.frame_slot,
                                    b0.telemetry.frame_slot, a1.telemetry.frame_slot}},
                 {"open", samples_json(a0, points)},
                 {"shadowed", samples_json(b1, points)}, {"repeats_byte_exact", true}});

    ShadowState moon;
    moon.moon_intensity = .8f;
    const auto moon_open = take(renderer, shadow_scene(fixture, moon, {}, points), report,
                                "moon-open");
    const auto moon_ignores_sun = take(renderer,
        shadow_scene(fixture, moon, caster_quad(1u), points), report, "moon-ignores-mask1");
    const auto moon_shadow = take(renderer,
        shadow_scene(fixture, moon, caster_quad(2u), points), report, "moon-mask2-shadow");
    const auto moon_shadow_other_slot = take(renderer,
        shadow_scene(fixture, moon, caster_quad(2u), points), report,
        "moon-mask2-shadow-other-slot");
    require(moon_open.telemetry.frame_slot == 0u &&
            moon_ignores_sun.telemetry.frame_slot == 1u &&
            moon_shadow.telemetry.frame_slot == 0u &&
            moon_shadow_other_slot.telemetry.frame_slot == 1u,
            "Moon source-mask controls did not span slots 0/1/0/1");
    require_exact(moon_open, moon_ignores_sun, "Moon-only source-mask-one isolation");
    require_exact(moon_shadow, moon_shadow_other_slot,
                  "Moon mask-two shadow state across both frame sets");
    require_darkened(moon_open, moon_shadow, points.ground_moon, "Moon ground receiver");
    require_darkened(moon_open, moon_shadow, points.material_moon,
                     "Moon typed-material receiver");
    const auto sun_ignores_moon = take(renderer,
        shadow_scene(fixture, sun, caster_quad(2u), points), report, "sun-ignores-mask2");
    require_exact(a0, sun_ignores_moon, "Sun-only source-mask-two isolation");
    report.pass("independent-source-masks-and-opposite-directions",
                {{"sun_mask2_byte_exact", true}, {"moon_mask1_byte_exact", true},
                 {"moon_mask2_cross_slot_byte_exact", true},
                 {"moon_actual_slots", {moon_open.telemetry.frame_slot,
                                         moon_ignores_sun.telemetry.frame_slot,
                                         moon_shadow.telemetry.frame_slot,
                                         moon_shadow_other_slot.telemetry.frame_slot}},
                 {"moon_open", samples_json(moon_open, points)},
                 {"moon_shadow", samples_json(moon_shadow, points)}});

    ShadowState both;
    both.sun_intensity = .8f;
    both.moon_intensity = .8f;
    const auto both_open = take(renderer, shadow_scene(fixture, both, {}, points), report,
                                "both-open");
    const auto both_shadow = take(renderer,
        shadow_scene(fixture, both, caster_quad(3u), points), report, "both-mask3-shadow");
    require_darkened(both_open, both_shadow, points.ground_sun, "Shared-mask sun ground");
    require_darkened(both_open, both_shadow, points.material_sun, "Shared-mask sun material");
    require_darkened(both_open, both_shadow, points.ground_moon, "Shared-mask moon ground");
    require_darkened(both_open, both_shadow, points.material_moon, "Shared-mask moon material");
    report.pass("shared-mask-both-sources", {{"open", samples_json(both_open, points)},
        {"shadowed", samples_json(both_shadow, points)}});

    const auto reversed = take(renderer,
        shadow_scene(fixture, sun, caster_quad(1u, 0.f, 2.f, true), points), report,
        "sun-reversed-winding");
    require_exact(b1, reversed, "Two-sided reversed winding");
    const auto moved = take(renderer,
        shadow_scene(fixture, sun, caster_quad(1u, 2.f, 2.f), points), report,
        "sun-caster-moved-plus2");
    require_darkened(a0, moved, points.ground_moved, "Moved caster ground receiver");
    require_darkened(a0, moved, points.material_moved, "Moved caster material receiver");
    require(shadow_lightness(moved, points.ground_sun) >
                shadow_lightness(b1, points.ground_sun) + 24 &&
            shadow_lightness(moved, points.material_sun) >
                shadow_lightness(b1, points.material_sun) + 24,
            "Moving caster did not release its previous ground and material receivers");
    report.pass("two-sided-and-moved-caster",
                {{"reverse_byte_exact", true}, {"moved", samples_json(moved, points)}});

    ShadowState flipped_sun = sun;
    flipped_sun.flip_sun_direction = true;
    const auto direction_flipped = take(renderer,
        shadow_scene(fixture, flipped_sun, sun_caster, points), report,
        "same-caster-sun-direction-negative-x");
    require_darkened(a0, direction_flipped, points.ground_moon,
                     "Direction-flipped sun ground receiver");
    require_darkened(a0, direction_flipped, points.material_moon,
                     "Direction-flipped sun material receiver");
    require(shadow_lightness(direction_flipped, points.ground_sun) >
                shadow_lightness(b1, points.ground_sun) + 24 &&
            shadow_lightness(direction_flipped, points.material_sun) >
                shadow_lightness(b1, points.material_sun) + 24,
            "Flipping sun direction did not release its previous receivers");
    report.pass("same-caster-direction-relocates-shadow",
                {{"moon_intensity", flipped_sun.moon_intensity},
                 {"sun_direction", moon_direction},
                 {"samples", samples_json(direction_flipped, points)}});

    const auto nearest = nearest_depth(renderer, fixture, report);
    const auto slope = slope_causality(renderer, fixture, report);

    ShadowState dark;
    ShadowState isolated = dark;
    isolated.local = true;
    isolated.emission = true;
    isolated.contact = true;
    const auto dark_capture = take(renderer, shadow_scene(fixture, dark, {}, points), report,
                                   "disabled-direct-dark");
    const auto isolated_open = take(renderer, shadow_scene(fixture, isolated, {}, points), report,
                                    "disabled-direct-local-emission-contact");
    const auto isolated_caster = take(renderer,
        shadow_scene(fixture, isolated, caster_quad(3u), points), report,
        "disabled-direct-local-emission-contact-caster");
    require(shadow_lightness(isolated_open, points.ground_moved) >
            shadow_lightness(dark_capture, points.ground_moved) + 16,
            "Disabled-direct local-light control was vacuous");
    require(shadow_lightness(isolated_open, points.material_moved) >
            shadow_lightness(dark_capture, points.material_moved) + 16,
            "Disabled-direct emission control was vacuous");
    require_exact(isolated_open, isolated_caster,
                  "Disabled-direct local/emission/contact/UI isolation");
    require_shadow_graph(isolated_open, true);
    report.pass("disabled-direct-preserves-contact-local-emission-ui",
                {{"dark", samples_json(dark_capture, points)},
                 {"isolated", samples_json(isolated_open, points)},
                 {"caster_pair_byte_exact", true}});

    std::vector<SkyShadowTriangle> capacity;
    capacity.reserve(sky_shadow_triangle_capacity);
    const auto active = triangle({-2.f, -2.f, 2.f}, {2.f, -2.f, 2.f},
                                 {0.f, 2.f, 2.f}, 1u);
    capacity.assign(sky_shadow_active_triangle_capacity, active);
    auto hidden = active;
    hidden.reveal = 0.f;
    capacity.insert(capacity.end(), sky_shadow_triangle_capacity - capacity.size(), hidden);
    const auto capacity_capture = take(renderer,
        shadow_scene(fixture, sun, capacity, points), report, "capacity-1024-active-3072-hidden");
    require_darkened(a0, capacity_capture, points.ground_sun,
                     "Maximum active triangle control");
    report.pass("raw-and-reveal-filtered-capacity-boundary",
                {{"raw", capacity.size()}, {"active_sun", sky_shadow_active_triangle_capacity},
                 {"hidden", sky_shadow_triangle_capacity - sky_shadow_active_triangle_capacity}});

    return {{"device", device_json(renderer.device_info())},
            {"status", shadow_status_json(renderer.sky_shadow_status())},
            {"nearest_depth", nearest}, {"slope", slope},
            {"capture_count", report.capture_count}};
}
} // namespace sky_shadow_probe

int main(int argc, char** argv) {
    using namespace sky_shadow_probe;
    std::filesystem::path output;
    std::unique_ptr<SkyReporter> report;
    try {
        require(argc == 3, "Usage: SkyShadowProbe <assets-dir> <NEW-output-dir>");
        const auto assets = from_utf8(argv[1]);
        output = from_utf8(argv[2]);
        require(std::filesystem::is_directory(assets), "Sky shadow assets directory is missing");
        require(!std::filesystem::exists(output),
                "Sky shadow probe refuses an existing output root");
        require(std::filesystem::create_directories(output),
                "Could not create fresh sky shadow output root");
        report = std::make_unique<SkyReporter>();
        report->output = output;
        write_json_new(output / "sky-shadow-progress-0.json",
            {{"schema", 1}, {"complete", false}, {"phase", "output-created-before-window"},
             {"build", RIBTERRA_PROBE_BUILD}});
        sky_shadow_probe::boundary_checkpoint(*report, "before-sdl-init");
        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        sky_shadow_probe::boundary_checkpoint(*report, "after-sdl-init-before-window");
        Window window("Ribterra independent sun/moon shadow diagnostic");
        sky_shadow_probe::boundary_checkpoint(*report, "after-window-before-invalid-frames");
        const auto invalid = sky_shadow_probe::invalid_frames(window, assets, *report);
        write_json_new(output / "sky-shadow-progress-1.json",
            {{"schema", 1}, {"complete", false}, {"phase", "pre-command-negatives-complete"},
             {"build", RIBTERRA_PROBE_BUILD}, {"checks", report->checks}});
        const auto failure = sky_shadow_probe::allocation_failure(window, assets, *report);
        const auto compatibility_result = sky_shadow_probe::compatibility(window, assets, *report);
        const auto production_result = sky_shadow_probe::production(window, assets, *report);
        const Json final_report{
            {"schema", 1}, {"complete", true}, {"build", RIBTERRA_PROBE_BUILD},
            {"scope", "bounded native SkyShadowSlice D32 sun/moon shadow diagnostic"},
            {"claims", {"two independent fixed-camera directional depth maps",
                         "causal ground and six-channel typed-material receiver pixels",
                         "source masks, opposite directions, two-sided winding and nearest depth",
                         "two-slot repeat, pre-command rejection and target-candidate rollback",
                         "zero-caster SkyContact compatibility"}},
            {"non_claims", {"final art or authored geometry adapter", "camera-following clipmaps",
                             "owner/self-shadow filtering",
                             "partial reveal or alpha coverage; reveal is filter-only",
                             "time, weather or calendar simulation", "full GameView or gameplay",
                             "performance, GPU timing or 120 FPS", "E01-E17 acceptance"}},
            {"attachment_bytes_note",
             "16 MiB is nominal four-map D32 payload, not driver allocation or peak GPU VRAM"},
            {"checks", report->checks}, {"captures", report->captures},
            {"png_count", report->capture_count}, {"invalid_frames", invalid},
            {"allocation_failure", failure}, {"compatibility", compatibility_result},
            {"production", production_result}};
        write_json_new(output / "sky-shadow-report.json", final_report);
        std::cout << "Sky shadow native probe: " << report->checks.size() << " checks, "
                  << report->capture_count << " immutable PNG captures\n";
        return 0;
    } catch (const std::exception& error) {
        try {
            if (report)
                write_json_new(output / "sky-shadow-failure.json",
                    {{"schema", 1}, {"complete", false}, {"build", RIBTERRA_PROBE_BUILD},
                     {"error", error.what()}, {"checks", report->checks},
                     {"captures", report->captures}, {"png_count", report->capture_count}});
        } catch (...) {}
        std::cerr << "Sky shadow native probe failed: " << error.what() << '\n';
        return 1;
    }
}

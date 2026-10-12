#include "engine_control_probe.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/world_projection.hpp"
#include <bit>
#include <limits>
#include <span>
#include <utility>

namespace {
constexpr std::string_view world_save = "opacity-world-current.save";
constexpr std::string_view sprite_id = "control.house.sprite";
constexpr float half_opacity = 128.f / 255.f;
constexpr std::size_t capture_limit = 20u;
constexpr double fp16_relative_allowance = 3. / 1024.;
constexpr double fp16_absolute_allowance = 1. / 65536.;

void create_opacity_world(const fs::path& file) {
    World world(70421, 0);
    auto state = world.export_state();
    require(state.residents.empty() && state.routes.empty() && state.path_batches.empty(),
            "Opacity fixture must have no actors or path searches");
    for (auto& chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            const int x = chunk.key.cx * chunk_size + static_cast<int>(index % chunk_size);
            const int y = chunk.key.cy * chunk_size + static_cast<int>(index / chunk_size);
            if (x < 15 || x > 40 || y < 13 || y > 37) continue;
            const int center = y == 25 ? 28 : 28 + (y < 22 ? -1 : y > 29 ? 1 : 0);
            chunk.tiles[index] = x >= center - 1 && x <= center + 1
                ? Tile{Terrain::Water, 0, true, Biome::Lake}
                : Tile{Terrain::Grass, 0, true, Biome::Meadow,
                       static_cast<std::int16_t>(x < 27 ? 8 : 12)};
            if (x >= 31 && y >= 30) chunk.tiles[index].explored = false;
        }
    }
    for (int x = 27; x <= 29; ++x)
        state.buildings.push_back({state.next_building_id++, BuildingKind::Bridge,
                                   x, 25, 1.f, WorldLayer::Surface, 1u});
    ++state.topology_revision;
    std::string error;
    const bool imported = world.import_state(std::move(state), error);
    require(imported, "Opacity fixture import: " + error);
    for (int x = 27; x <= 29; ++x)
        require(world.walkable(x, 25) && world.tile(x, 25).terrain == Terrain::Water,
                "Authored completed bridge lost its actual water tile");
    require(!fs::exists(file), "Refusing opacity world save overwrite");
    const bool saved = world.save(file, error);
    require(saved, "Opacity fixture save: " + error);
}

Json geometry_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass), static_cast<int>(draw.blend), draw.texture.slot,
        draw.texture.generation, draw.first_index, draw.index_count, draw.scissor.x,
        draw.scissor.y, draw.scissor.w, draw.scissor.h, draw.scissor_enabled,
        draw.surface_lit, draw.sprite_material_index, draw.ground_material_index};
}

Json frame_state(const Frame2D& frame) {
    Json result{{"size", {frame.logical_size.width, frame.logical_size.height}},
        {"clear", {frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a}},
        {"indices", frame.indices}, {"hdr", frame.linear_hdr_world},
        {"exposure", frame.world_exposure},
        {"normal", {frame.water_normal.slot, frame.water_normal.generation}},
        {"lighting", {frame.surface_lighting.ambient, frame.surface_lighting.sun_direction,
            frame.surface_lighting.sun_color_intensity, frame.surface_lighting.counts}}};
    for (const auto& light : frame.surface_lighting.lights)
        result["lights"].push_back({light.anchor_radius, light.direction_cone,
            light.color_intensity, light.occluder_range_flags, light.height_falloff_reveal});
    for (const auto& vertex : frame.vertices)
        result["vertices"].push_back({vertex.position.x, vertex.position.y, vertex.uv.x,
            vertex.uv.y, vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a});
    for (const auto& vertex : frame.surface_vertices)
        result["surface_vertices"].push_back({vertex.world_position, vertex.normal});
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        require(draw != nullptr, "Unexpected text command in opacity diagnostic scene");
        result["commands"].push_back(geometry_row(*draw));
    }
    for (const auto& draw : frame.sprite_material_draws)
        result["sprites"].push_back({draw.material.slot, draw.material.generation,
            draw.surface.uv_regions, draw.surface.anchor_export, draw.surface.source_size_pivot,
            draw.surface.depth_scale, draw.surface.emission, draw.surface.response, draw.reveal,
            draw.receiver_mask, draw.receiver_owner, draw.emission_scale});
    for (const auto& draw : frame.ground_material_draws)
        result["ground"].push_back({draw.albedo.slot, draw.albedo.generation,
            draw.surface.response, draw.surface.flags, draw.surface.visibility,
            static_cast<int>(draw.contact_receiver), static_cast<int>(draw.water_surface_class),
            draw.shore_edges, draw.geometric_coverage});
    for (const auto& draw : frame.reflection_draws)
        result["reflections"].push_back({geometry_row(draw.draw), draw.strength, draw.fade});
    if (frame.water_surface) {
        const auto& w = *frame.water_surface;
        result["water"] = {w.world_origin_time, w.flow_response, w.tint, w.viewport};
    }
    if (frame.atmosphere) {
        const auto& a = *frame.atmosphere;
        result["atmosphere"] = {a.origin_time, a.depth, a.density, a.far_color_noise,
            a.morning_color, a.coastal_color, a.wind_shore, a.counts_version, a.water_aabbs};
    }
    if (frame.moon_lighting)
        result["moon"] = {frame.moon_lighting->direction, frame.moon_lighting->color_intensity};
    if (frame.contact_projection)
        result["projection"] = {frame.contact_projection->origin_xy, frame.contact_projection->zoom};
    require(frame.local_occluders.empty() && frame.contact_shadows.empty() && frame.sky_shadow_triangles.empty(),
            "Opacity probe unexpectedly added lighting/blocker/contact/sky side effects");
    return result;
}

void preserve_nonfinite_bits(Json& value) {
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number)) value = Json{{"nonfinite_bits", std::bit_cast<std::uint64_t>(number)}};
    } else if (value.is_array() || value.is_object()) {
        for (auto& item : value) preserve_nonfinite_bits(item);
    }
}

Json rejection_frame_state(const Frame2D& frame) {
    auto result = frame_state(frame);
    preserve_nonfinite_bits(result);
    return result;
}

void marker_present(const Image& image) {
    constexpr std::array<std::uint8_t, 4> marker{31u, 189u, 78u, 255u};
    for (std::size_t y = 14; y < 50; ++y) for (std::size_t x = 14; x < 90; ++x) {
        const auto p = (y * extent.width + x) * 4u;
        require(std::equal(marker.begin(), marker.end(),
            image.rgba.begin() + static_cast<std::ptrdiff_t>(p)), "Known opaque UI RGBA changed");
    }
}

std::size_t changed_pixels(const Image& first, const Image& second) {
    require(first.rgba.size() == second.rgba.size(), "Different opacity capture dimensions");
    std::size_t count{};
    for (std::size_t p = 0; p < first.rgba.size(); p += 4u)
        if (!std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                        first.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                        second.rgba.begin() + static_cast<std::ptrdiff_t>(p))) ++count;
    return count;
}

void alpha_vertices(const Frame2D& frame, std::uint8_t expected) {
    std::size_t vertices{};
    const auto check = [&](const GeometryDraw& draw) {
        if (!draw.sprite_material_index) return;
        for (std::uint32_t index = 0; index < draw.index_count; ++index) {
            const auto& color = frame.vertices.at(frame.indices.at(draw.first_index + index)).color;
            require(color.r == 255u && color.g == 255u && color.b == 255u && color.a == expected,
                    "Typed opacity did not preserve white RGB and the expected quantized alpha");
            ++vertices;
        }
    };
    for (const auto& command : frame.commands)
        if (const auto* draw = std::get_if<GeometryDraw>(&command)) check(*draw);
    for (const auto& reflection : frame.reflection_draws) check(reflection.draw);
    require(vertices == 6u && frame.sprite_material_draws.size() == 1u,
            "Opacity controls must contain exactly one typed house quad");
}

void same_metadata(const Frame2D& first, const Frame2D& second) {
    auto a = first, b = second;
    for (auto& vertex : a.vertices) vertex.color.a = 255u;
    for (auto& vertex : b.vertices) vertex.color.a = 255u;
    require(frame_state(a) == frame_state(b),
            "Opacity changed geometry, material/reveal/light metadata instead of vertex alpha");
}

double inverse_srgb(double encoded) {
    return encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
}
struct Interval { double first{}, last{}; };
Interval inferred_linear_interval(std::uint8_t byte) {
    const double value = static_cast<double>(byte);
    return {inverse_srgb(std::max(0., value - .5) / 255.),
            inverse_srgb(std::min(255., value + .5) / 255.)};
}

Json intermediate_oracle(const Image& absent, const Image& half, const Image& one,
                         const WaterCoverageReadback& carrier, bool reflection,
                         const fs::path& output, std::string_view group, bool write_mask = true) {
    require(absent.rgba.size() == half.rgba.size() && half.rgba.size() == one.rgba.size(),
            "Intermediate opacity image dimensions differ");
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(extent.width) * extent.height);
    std::size_t qualified_pixels{}, qualified_components{}, intermediate_components{}, excluded_saturated{};
    double max_gap{}, max_allowance{}, max_endpoint_delta{};
    constexpr double alpha = 128. / 255.;
    for (std::size_t p = 0; p < one.rgba.size(); p += 4u) {
        // Only pixels changed by the one real house/reflection qualify. There
        // are no other world sprites, authored emitters or shadow casters.
        if (reflection && (carrier.rgba8[p] != 255u || carrier.rgba8[p + 1u] != 2u)) continue;
        bool qualified{};
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            const auto zero = absent.rgba[p + channel], middle = half.rgba[p + channel], full = one.rgba[p + channel];
            if (zero == 255u || middle == 255u || full == 255u) { ++excluded_saturated; continue; }
            if (std::abs(static_cast<int>(full) - zero) < 8) continue;
            const auto z = inferred_linear_interval(zero), h = inferred_linear_interval(middle);
            const auto o = inferred_linear_interval(full);
            const Interval expected{(1. - alpha) * z.first + alpha * o.first,
                                    (1. - alpha) * z.last + alpha * o.last};
            // Existing scene/reflection targets store FP16. This stated bound
            // covers up to three half-precision roundings, plus subnormal loss.
            // It is an inferred SDR interval test, never a direct HDR readback.
            const double allowance = fp16_relative_allowance * std::max(z.last, o.last) + fp16_absolute_allowance;
            const double gap = std::max({0., h.first - expected.last, expected.first - h.last});
            require(gap <= allowance, "Inferred linear opacity interval failed in group " + std::string(group) +
                    " pixel " + std::to_string(p / 4u) + " channel " + std::to_string(channel));
            require(middle >= std::min(zero, full) && middle <= std::max(zero, full),
                    "Intermediate SDR channel lies outside its fixed endpoint range");
            if (middle > std::min(zero, full) && middle < std::max(zero, full)) ++intermediate_components;
            ++qualified_components; qualified = true;
            max_gap = std::max(max_gap, gap); max_allowance = std::max(max_allowance, allowance);
            max_endpoint_delta = std::max(max_endpoint_delta,
                std::abs(inverse_srgb(static_cast<double>(full) / 255.) -
                         inverse_srgb(static_cast<double>(zero) / 255.)));
        }
        if (qualified) { mask[p / 4u] = 1u; ++qualified_pixels; }
    }
    require(qualified_pixels >= 16u && qualified_components >= 32u && intermediate_components >= 16u,
            "Removal-qualified intermediate opacity oracle is vacuous in group " + std::string(group));
    if (write_mask) save_bytes(output / ("opacity-" + std::string(group) + "-qualified.mask8"), mask);
    return {{"kind", "inverse-sRGB final-SDR quantization intervals; inferred linear, not raw HDR"},
        {"alpha_numerator", 128}, {"alpha_denominator", 255}, {"minimum_endpoint_byte_delta", 8},
        {"quantization_half_lsb", .5}, {"fp16_relative_allowance", fp16_relative_allowance},
        {"fp16_absolute_allowance", fp16_absolute_allowance}, {"qualified_pixels", qualified_pixels},
        {"qualified_components", qualified_components}, {"strictly_intermediate_components", intermediate_components},
        {"excluded_saturated_components", excluded_saturated}, {"max_interval_gap", max_gap},
        {"max_fp16_allowance", max_allowance}, {"max_inferred_endpoint_delta", max_endpoint_delta},
        {"intervals_passed", true}, {"mask", "opacity-" + std::string(group) + "-qualified.mask8"}};
}
} // namespace

int main(int argc, char** argv) {
    fs::path output; bool owned{};
    Json captures = Json::array(), groups = Json::array(), cpu_controls = Json::array();
    Json api_rejections = Json::array(), renderer_rejections = Json::array();
    try {
        require(argc == 4, "Usage: GameViewWaterProbe runtime-assets kit fresh-output (opacity)");
        const auto assets = fs::absolute(argument(argv[1])), kit = fs::absolute(argument(argv[2]));
        output = fs::absolute(argument(argv[3]));
        require(output.parent_path() == assets.parent_path().parent_path() && !fs::exists(output),
                "Opacity output must be a fresh sibling build child");
        require(fs::space(output.parent_path()).available >= 384u * 1024u * 1024u,
                "Insufficient opacity evidence headroom");
        require(fs::create_directory(output), "Could not create opacity output"); owned = true;
        create_opacity_world(output / world_save);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Typed Opacity - diagnostic controls", 1920, 1080, 0), SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()), "Native opacity window failed");
        RendererOptions options; options.world_graph = WorldGraph::WaterAtmosphereSlice;
        options.collect_telemetry = true;
        Renderer2D renderer(window.get(), assets, options); renderer.set_logical_size(extent);
        require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
                "Hardware D3D12 is required");
        GameView view(renderer); view.set_logical_size(extent);
        const AssetCatalog catalog(kit);
        view.load_engine_control_art(catalog); view.load_engine_water_art();
        GameArt art(renderer); art.load(catalog);
        ClientSession session(70421); std::string error;
        const bool loaded = session.load(output / world_save, error);
        require(loaded, "Opacity session load: " + error);
        require(!session.last_load().copied, "Opacity fixture unexpectedly migrated");
        view.reset_world_state();
        EngineControlSettings settings; settings.camera_x = 28.f; settings.camera_y = 25.f;
        settings.cell_radius = 8; settings.water_enabled = true; settings.exposure = 1.f;
        settings.lighting.ambient = {.2f, .2f, .2f, 0.f};
        settings.atmosphere = EngineAtmosphereSettings{false, 12.f, 0.f};
        Frame2D terrain;
        const auto stats = view.compose_engine_control(session, 10., settings, {}, terrain);
        require(stats.instances_drawn == 0u && stats.bridge_decks == 3u && stats.water_cells > 0u &&
                terrain.sprite_material_draws.empty() && terrain.reflection_draws.empty(),
                "Opacity base must contain real terrain/water/decks and no world sprites");
        require(terrain.atmosphere && terrain.atmosphere->counts_version[0] == 0u &&
                terrain.atmosphere->counts_version[2] == 0u && terrain.contact_projection,
                "Opacity base must have fog disabled and the real projection");
        terrain.water_surface->flow_response[3] = 0.f; // Fixed normal/no distortion control.
        terrain.water_surface->viewport[0] = 0.f;
        const std::array<float, 3> anchor{-2.f, -3.f, render_z_from_legacy_elevation(8.f)};
        const auto projected = contact_project_logical(anchor, *terrain.contact_projection);
        const Point ground{projected[0], projected[1]};
        const auto append = [&](Frame2D frame, float opacity, float emission, bool default_opacity, bool reflection) {
            const auto first = frame.vertices.size();
            if (default_opacity)
                art.lit_sprite(frame, sprite_id, ground, anchor, settings.zoom, 1.f, 0.f, 101u, emission);
            else art.lit_sprite(frame, sprite_id, ground, anchor, settings.zoom, 1.f, 0.f, 101u, emission, opacity);
            frame.surface_vertices.resize(frame.vertices.size());
            if (reflection && frame.vertices.size() != first) {
                require(frame.vertices.size() == first + 4u && !frame.commands.empty(), "Reflection source is not one house quad");
                auto draw = std::get<GeometryDraw>(frame.commands.back()); frame.commands.pop_back();
                const auto plane = contact_project_logical({anchor[0], anchor[1], 0.f}, *frame.contact_projection);
                const float height = std::abs(frame.vertices[first + 2u].position.y - frame.vertices[first].position.y);
                for (std::size_t index = first; index < first + 4u; ++index)
                    frame.vertices[index].position.y = 2.f * plane[1] - frame.vertices[index].position.y + .06f * height;
                frame.reflection_draws.push_back({draw, .3f, 1.7f});
            }
            return frame;
        };
        const auto one = append(terrain, 1.f, 0.f, false, false);
        const auto default_one = append(terrain, 1.f, 0.f, true, false);
        require(frame_state(one) == frame_state(default_one), "Default opacity differs from explicit one");
        const auto zero = append(terrain, 0.f, 0.f, false, false);
        const auto tiny_zero = append(terrain, .001f, 0.f, false, false);
        require(frame_state(zero) == frame_state(terrain) && frame_state(tiny_zero) == frame_state(terrain),
                "Zero quantized opacity consumed geometry/material/command appends");
        alpha_vertices(one, 255u); alpha_vertices(append(terrain, half_opacity, 0.f, false, false), 128u);
        alpha_vertices(append(terrain, .5f, 0.f, false, false), 128u);
        alpha_vertices(append(terrain, .505f, 0.f, false, false), 129u);
        cpu_controls.push_back({{"id", "default_explicit_one_frame_exact"}, {"passed", true}});
        cpu_controls.push_back({{"id", "zero_and_quantized_zero_no_appends"}, {"passed", true}, {"tiny_input", .001f}});
        cpu_controls.push_back({{"id", "white_rgb_quantized_alpha_128_129_255"}, {"passed", true},
            {"half_input_rounds_to_128", true}});

        const auto api_reject = [&](std::string_view id, const auto& operation, bool hdr = true) {
            auto frame = one; frame.linear_hdr_world = hdr;
            const auto before = frame_state(frame); bool rejected{};
            try { operation(frame); } catch (const std::exception&) { rejected = true; }
            require(rejected && frame_state(frame) == before && !renderer.has_submitted_frames(),
                    "API opacity rejection changed a pending frame: " + std::string(id));
            api_rejections.push_back({{"id", id}, {"passed", true}, {"frame_exact", true}, {"submitted", false}});
        };
        const std::array<std::pair<std::string_view, float>, 5> bad_opacity{{
            {"opacity_negative", -.1f}, {"opacity_above_one", 1.1f},
            {"opacity_nan", std::numeric_limits<float>::quiet_NaN()},
            {"opacity_pos_inf", std::numeric_limits<float>::infinity()},
            {"opacity_neg_inf", -std::numeric_limits<float>::infinity()}}};
        for (const auto& [id, opacity] : bad_opacity)
            api_reject(id, [&](auto& frame) { art.lit_sprite(frame, sprite_id, ground, anchor, settings.zoom,
                                                          1.f, 0.f, 101u, 0.f, opacity); });
        api_reject("zero_unknown_asset", [&](auto& frame) { art.lit_sprite(frame, "missing.opacity.sprite", ground,
                                                             anchor, settings.zoom, 1.f, 0.f, 101u, 0.f, 0.f); });
        api_reject("zero_invalid_zoom", [&](auto& frame) { art.lit_sprite(frame, sprite_id, ground, anchor,
                                                           0.f, 1.f, 0.f, 101u, 0.f, 0.f); });
        api_reject("zero_invalid_emission", [&](auto& frame) { art.lit_sprite(frame, sprite_id, ground, anchor,
                                                               settings.zoom, 1.f, 0.f, 101u, 1.1f, 0.f); });
        api_reject("zero_non_hdr", [&](auto& frame) {
            art.lit_sprite(frame, sprite_id, ground, anchor, settings.zoom, 1.f, 0.f, 101u, 0.f, 0.f);
        }, false);
        api_reject("zero_anchor_nan", [&](auto& frame) { auto bad = anchor; bad[0] = std::numeric_limits<float>::quiet_NaN();
            art.lit_sprite(frame, sprite_id, ground, bad, settings.zoom, 1.f, 0.f, 101u, 0.f, 0.f); });

        const auto reflected_one = append(terrain, 1.f, .125f, false, true);
        const auto renderer_reject = [&](std::string_view id, Frame2D candidate, const auto& edit) {
            edit(candidate); const auto before = rejection_frame_state(candidate); bool rejected{};
            const auto attempts = renderer.ground_forward_status().creation_attempts;
            try { renderer.render(candidate); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && rejection_frame_state(candidate) == before && !renderer.has_submitted_frames() &&
                    renderer.ground_forward_status().creation_attempts == attempts,
                    "Typed renderer rejection submitted/mutated a frame or targets: " + std::string(id));
            renderer_rejections.push_back({{"id", id}, {"passed", true}, {"frame_exact", true},
                {"submitted", false}, {"target_creation_unchanged", true}});
        };
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            const auto name = std::array<std::string_view, 3>{"red", "green", "blue"}[channel];
            const auto edit_rgb = [channel](auto& frame) {
                auto& color = frame.vertices.back().color;
                if (channel == 0u) color.r = 254u; else if (channel == 1u) color.g = 254u; else color.b = 254u;
            };
            renderer_reject("sprite_rgb_" + std::string(name), one, edit_rgb);
            renderer_reject("reflection_rgb_" + std::string(name), reflected_one, edit_rgb);
        }
        renderer_reject("sprite_emission_nan", one, [](auto& frame) { frame.sprite_material_draws[0].emission_scale = std::numeric_limits<float>::quiet_NaN(); });
        renderer_reject("sprite_anchor_nan", one, [](auto& frame) { frame.sprite_material_draws[0].surface.anchor_export[0] = std::numeric_limits<float>::quiet_NaN(); });
        renderer_reject("sprite_reserved_response", one, [](auto& frame) { frame.sprite_material_draws[0].surface.response[2] = 1.f; });
        renderer_reject("sprite_reveal_fractional", one, [](auto& frame) { frame.sprite_material_draws[0].reveal = .5f; });
        renderer_reject("sprite_receiver_zero", one, [](auto& frame) { frame.sprite_material_draws[0].receiver_mask = 0u; });
        renderer_reject("reflection_strength_nan", reflected_one, [](auto& frame) { frame.reflection_draws[0].strength = std::numeric_limits<float>::quiet_NaN(); });
        renderer_reject("reflection_fade_below_one", reflected_one, [](auto& frame) { frame.reflection_draws[0].fade = .5f; });
        renderer_reject("reflection_uv_out_of_bounds", reflected_one, [](auto& frame) { frame.vertices.back().uv.x = 1.1f; });
        renderer_reject("ground_alpha_nonopaque", one, [](auto& frame) { frame.vertices.front().color.a = 128u; });
        require(api_rejections.size() == 10u && renderer_rejections.size() == 15u,
                "Opacity rejection matrix count changed");

        const auto initial = capture(renderer, terrain, output, "opacity-coverage-absent", captures);
        marker_present(initial);
        const auto carrier = renderer.read_water_coverage_v1();
        const auto counts = carrier_counts(carrier);
        save_bytes(output / "opacity-carrier.rgba8", carrier.rgba8);
        const auto checked_capture = [&](const Frame2D& frame, std::string_view name) {
            require(captures.size() < capture_limit, "Opacity PNG bound exceeded");
            const auto image = capture(renderer, frame, output, name, captures); marker_present(image);
            require(renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,
                    "Sprite opacity changed raw WaterCoverage classification");
            return image;
        };
        Frame2D repeat_frame; Image repeat_image;
        for (const auto group : std::array<std::string_view, 3>{"coverage", "emission", "reflection"}) {
            auto base = terrain;
            const bool reflection = group == "reflection";
            const float emission = group == "coverage" ? 0.f : .125f;
            if (group == "emission") base.surface_lighting.ambient = {};
            auto full = append(base, 1.f, emission, false, reflection);
            auto default_full = append(base, 1.f, emission, true, reflection);
            auto empty = append(base, 0.f, emission, false, reflection);
            auto middle = append(base, half_opacity, emission, false, reflection);
            auto geometry_zero = full;
            for (std::size_t index = base.vertices.size(); index < geometry_zero.vertices.size(); ++index)
                geometry_zero.vertices[index].color.a = 0u;
            require(frame_state(base) == frame_state(empty) && frame_state(full) == frame_state(default_full),
                    "Opacity group API zero/default frame contracts differ: " + std::string(group));
            alpha_vertices(full, 255u); alpha_vertices(middle, 128u); alpha_vertices(geometry_zero, 0u);
            same_metadata(full, middle); same_metadata(full, geometry_zero);
            const auto prefix = "opacity-" + std::string(group);
            const auto absent_image = group == "coverage" ? initial : checked_capture(base, prefix + "-absent");
            const auto zero_image = checked_capture(empty, prefix + "-zero-api");
            const auto geometry_zero_image = checked_capture(geometry_zero, prefix + "-zero-geometry");
            const auto half_image = checked_capture(middle, prefix + "-half");
            const auto full_image = checked_capture(full, prefix + "-one");
            const auto default_image = checked_capture(default_full, prefix + "-default");
            require(absent_image.rgba == zero_image.rgba && absent_image.rgba == geometry_zero_image.rgba,
                    "Native zero opacity leaked coverage/emission/reflection in group " + std::string(group));
            require(full_image.rgba == default_image.rgba,
                    "Native default opacity differs from explicit one in group " + std::string(group));
            const auto half_zero_delta = changed_pixels(half_image, absent_image);
            const auto half_one_delta = changed_pixels(half_image, full_image);
            require(half_zero_delta > 16u && half_one_delta > 16u,
                    "Intermediate native opacity response is empty in group " + std::string(group));
            if (reflection) {
                std::size_t water_changes{};
                for (std::size_t p = 0; p < full_image.rgba.size(); p += 4u) {
                    if (std::equal(full_image.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                                   full_image.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                                   absent_image.rgba.begin() + static_cast<std::ptrdiff_t>(p))) continue;
                    require(carrier.rgba8[p] == 255u && carrier.rgba8[p + 1u] == 2u,
                            "Reflection-only opacity changed a non-water pixel");
                    ++water_changes;
                }
                require(water_changes > 16u, "Actual reflection contribution is empty");
            }
            const auto oracle = intermediate_oracle(absent_image, half_image, full_image, carrier,
                                                     reflection, output, group);
            const auto reject_endpoint_substitution = [&](const Image& endpoint) {
                bool rejected{};
                try { static_cast<void>(intermediate_oracle(absent_image, endpoint, full_image,
                    carrier, reflection, output, group, false)); }
                catch (const std::runtime_error& failure) {
                    rejected = std::string_view(failure.what()).starts_with("Inferred linear opacity interval failed");
                }
                require(rejected, "Intermediate interval oracle accepted an endpoint substitution in group " + std::string(group));
            };
            reject_endpoint_substitution(absent_image); reject_endpoint_substitution(full_image);
            groups.push_back({{"id", group}, {"passed", true}, {"category", "house"},
                {"zero_api_frame_exact", true}, {"zero_api_png_equal", true}, {"zero_geometry_png_equal", true},
                {"default_one_frame_equal", true}, {"default_one_png_equal", true}, {"metadata_except_alpha_exact", true},
                {"half_alpha_byte", 128}, {"one_alpha_byte", 255}, {"fixed_exposure", 1.f},
                {"ambient", base.surface_lighting.ambient}, {"emission_scale", emission}, {"fog_enabled", false},
                {"reflection_only", reflection}, {"half_changed_from_zero", half_zero_delta},
                {"half_changed_from_one", half_one_delta}, {"interval_oracle", oracle},
                {"zero_endpoint_substitution_rejected", true}, {"one_endpoint_substitution_rejected", true}});
            repeat_frame = full; repeat_image = full_image;
        }
        require(checked_capture(repeat_frame, "opacity-exact-repeat").rgba == repeat_image.rgba,
                "Fixed-time opacity native repeat is not exact");
        require(captures.size() == 19u, "Opacity native capture plan changed");
        save_report(output / "opacity-native-result.json", {{"complete", true},
            {"scope", "Typed GameArt white-RGB vertex alpha with native coverage/emission/reflection controls"},
            {"categories", {"house"}}, {"groups", groups}, {"cpu_controls", cpu_controls},
            {"api_rejections", api_rejections}, {"renderer_rejections", renderer_rejections},
            {"captures", captures}, {"capture_limit", capture_limit}, {"world_saves", {world_save}},
            {"backend", renderer.device_info().backend}, {"gpu", renderer.device_info().device},
            {"driver", renderer.device_info().driver}, {"resolution", {1920, 1080}},
            {"graph", "WaterAtmosphereSlice"}, {"raw_carrier", counts}, {"raw_carrier_exact", true},
            {"opaque_ui_exact", true}, {"known_ui_rgba", {31, 189, 78, 255}}, {"repeat_exact", true},
            {"raw_hdr_readback", false}, {"inferred_sdr_intervals", true}, {"new_bindings", false},
            {"weather_explicit", true}, {"remote_verified", false}, {"normal_startup", "OPEN"},
            {"targets", stable_targets(renderer.ground_forward_status())}, {"fps", nullptr},
            {"gpu_execution_ms", nullptr}, {"full_engine", "OPEN"}, {"reference_fidelity", "NOT_ACCEPTED"},
            {"limitations", {"Final PNG uses exposure/clamp/sRGB; no half encoded-RGB or direct HDR claim",
                "Numeric proof is category-removal-qualified unsaturated final-SDR intervals with stated FP16 allowance",
                "One neutral diagnostic house; completed bridge fixture records are authored, not simulated construction",
                "No GameView general fade, blocker/light/contact attenuation or particle/weather authority integration",
                "No normal startup, physical input/video/DPI or performance evidence"}}});
        check_budget(output); std::cout << "GameView opacity native checks passed\n"; return 0;
    } catch (const std::exception& error) {
        if (owned) try { save_report(output / "opacity-native-result.json", {{"complete", false},
            {"error", error.what()}, {"captures", captures}, {"groups", groups}, {"cpu_controls", cpu_controls},
            {"api_rejections", api_rejections}, {"renderer_rejections", renderer_rejections},
            {"world_saves", {world_save}}, {"raw_hdr_readback", false}, {"inferred_sdr_intervals", true},
            {"fps", nullptr}, {"gpu_execution_ms", nullptr}, {"full_engine", "OPEN"},
            {"reference_fidelity", "NOT_ACCEPTED"}}); } catch (...) {}
        std::cerr << "GameView opacity probe failed: " << error.what() << '\n'; return 1;
    }
}

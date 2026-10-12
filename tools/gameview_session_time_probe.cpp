#include "engine_control_probe.hpp"
#include "ribterra/seasons.hpp"
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace {
constexpr std::size_t capture_limit = 20u;
constexpr std::array<std::string_view, 5> world_saves{
    "session-time-world-06.save", "session-time-world-12.save",
    "session-time-world-18.save", "session-time-world-00.save", "session-time-world-epoch.save"};
constexpr std::array<float, 4> calendar_hours{6.f, 12.f, 18.f, 0.f};
const std::array<EngineControlInstance, 2> controls{{
    {"control.house.sprite", 26.f, 22.f, 1.f, 101u, 101u, 0.f, true, 1001u},
    {"control.resident.empty.sprite", 26.f, 24.f, 1.f, 102u, 111u, 0.f, false, 0u}}};

void load_current(ClientSession& session, GameView& view, const fs::path& path) {
    std::string error;
    const bool loaded = session.load(path, error);
    require(loaded, "Session load: " + error);
    require(!session.last_load().copied, "Session time fixture unexpectedly migrated");
    // Match the successful local load action, including its explicit view reset.
    view.reset_world_state();
}

void create_time_worlds(const fs::path& output) {
    World world(70421, 0);
    auto state = world.export_state();
    require(state.residents.empty() && state.routes.empty() && state.path_batches.empty(),
            "Time fixture must have no resident routes or pending path searches");
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
    // Authored completed Bridge records exercise actual semantic decks. This
    // fixture does not claim to have executed construction or resident gameplay.
    for (int x = 27; x <= 29; ++x)
        state.buildings.push_back({state.next_building_id++, BuildingKind::Bridge,
                                   x, 25, 1.f, WorldLayer::Surface, 1u});
    ++state.topology_revision;
    for (std::size_t index = 0; index < world_saves.size(); ++index) {
        auto candidate = state;
        candidate.ticks = index < calendar_hours.size()
            ? static_cast<std::uint64_t>(index) * ticks_per_day / 4u : 10u;
        // Explicit diagnostic time jump, without replaying cover accumulation.
        const WeatherContext context{candidate.seed, {candidate.climate.algorithm_version,
            candidate.climate.climate_profile, candidate.climate.scenario}};
        const auto calendar = make_calendar_tick(candidate.ticks, candidate.climate.calendar_offset_ticks);
        require(calendar.has_value(), "Calendar fixture tick overflow");
        WeatherScheduleCache cache(context, *calendar);
        candidate.climate.evaluated_simulation_tick = candidate.ticks;
        candidate.climate.current = cache.sample(*calendar);
        validate_climate_runtime(candidate.climate, candidate.ticks, context, cache);
        std::string error;
        const bool imported = world.import_state(std::move(candidate), error);
        require(imported, "Calendar fixture import " + std::to_string(index) + ": " + error);
        require(clock_time(world.calendar().tick) == std::array<std::string_view, 5>{
                    "06:00", "12:00", "18:00", "00:00", "06:00"}[index],
                "Fixture disagrees with the production calendar");
        for (int x = 27; x <= 29; ++x)
            require(world.walkable(x, 25) && world.tile(x, 25).terrain == Terrain::Water,
                    "Authored completed deck lost its actual water tile");
        const auto file = output / world_saves[index];
        require(!fs::exists(file), "Refusing calendar save overwrite");
        const bool saved = world.save(file, error);
        require(saved, "Calendar fixture save " + std::to_string(index) + ": " + error);
    }
}

Json geometry_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass), static_cast<int>(draw.blend), draw.texture.slot,
        draw.texture.generation, draw.first_index, draw.index_count, draw.scissor.x,
        draw.scissor.y, draw.scissor.w, draw.scissor.h, draw.scissor_enabled,
        draw.surface_lit, draw.sprite_material_index, draw.ground_material_index};
}

Json time_payload(const Frame2D& frame) {
    require(frame.water_surface && frame.atmosphere && frame.moon_lighting,
            "Resolved session time GPU payload is incomplete");
    const auto& water = *frame.water_surface;
    const auto& atmosphere = *frame.atmosphere;
    Json result{{"water", {water.world_origin_time, water.flow_response, water.tint, water.viewport}},
        {"atmosphere", {atmosphere.origin_time, atmosphere.depth, atmosphere.density,
            atmosphere.far_color_noise, atmosphere.morning_color, atmosphere.coastal_color,
            atmosphere.wind_shore, atmosphere.counts_version, atmosphere.water_aabbs}},
        {"ambient", frame.surface_lighting.ambient},
        {"sun_direction", frame.surface_lighting.sun_direction},
        {"sun_color_intensity", frame.surface_lighting.sun_color_intensity},
        {"moon", {frame.moon_lighting->direction, frame.moon_lighting->color_intensity}},
        {"counts", frame.surface_lighting.counts}};
    for (const auto& light : frame.surface_lighting.lights)
        result["lights"].push_back({light.anchor_radius, light.direction_cone,
            light.color_intensity, light.occluder_range_flags, light.height_falloff_reveal});
    return result;
}

// Full field-wise candidate comparison avoids CPU-wrapper padding and includes
// the geometry/material channels beyond the session-time uniform itself.
Json frame_state(const Frame2D& frame) {
    Json result{{"time", time_payload(frame)},
        {"size", {frame.logical_size.width, frame.logical_size.height}},
        {"clear", {frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a}},
        {"indices", frame.indices}, {"hdr", frame.linear_hdr_world},
        {"exposure", frame.world_exposure},
        {"water_normal", {frame.water_normal.slot, frame.water_normal.generation}}};
    require(frame.contact_shadows.empty(),
            "Time probe unexpectedly enabled contacts");
    if (frame.contact_projection)
        result["projection"] = {frame.contact_projection->origin_xy, frame.contact_projection->zoom};
    for (const auto& vertex : frame.vertices)
        result["vertices"].push_back({vertex.position.x, vertex.position.y, vertex.uv.x,
            vertex.uv.y, vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a});
    for (const auto& vertex : frame.surface_vertices)
        result["surface_vertices"].push_back({vertex.world_position, vertex.normal});
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        require(draw != nullptr, "Unexpected text command in diagnostic time world");
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
    for (const auto& draw : frame.local_occluders)
        result["occluders"].push_back({draw.center_base, draw.axis_x_extent,
                                     draw.axis_y_radius, draw.shape_flags_id});
    for (const auto& draw : frame.sky_shadow_triangles)
        result["sky"].push_back({draw.vertices[0].position, draw.vertices[1].position,
                                draw.vertices[2].position, draw.source_mask, draw.reveal});
    for (const auto& draw : frame.reflection_draws)
        result["reflections"].push_back({geometry_row(draw.draw), draw.strength, draw.fade});
    return result;
}

void same_locals_and_emission(const Frame2D& first, const Frame2D& second) {
    require(first.surface_lighting.counts == second.surface_lighting.counts &&
        std::memcmp(first.surface_lighting.lights.data(), second.surface_lighting.lights.data(),
                    sizeof(first.surface_lighting.lights)) == 0,
        "Session time changed local light rows or counts");
    require(first.sprite_material_draws.size() == second.sprite_material_draws.size(),
            "Session time changed the sprite material count");
    for (std::size_t index = 0; index < first.sprite_material_draws.size(); ++index) {
        const auto& a = first.sprite_material_draws[index];
        const auto& b = second.sprite_material_draws[index];
        require(std::memcmp(&a.surface, &b.surface, sizeof(SpriteSurfaceGpu)) == 0 &&
                a.emission_scale == b.emission_scale && a.reveal == b.reveal &&
                a.receiver_mask == b.receiver_mask && a.receiver_owner == b.receiver_owner,
                "Session time changed authored surface/emission metadata");
    }
}

void shown_matches(const Frame2D& frame, double tick, std::string_view label) {
    require(frame.water_surface && frame.atmosphere && frame.moon_lighting,
            std::string(label) + ": missing time payload");
    const float seconds = static_cast<float>(tick * tick_seconds);
    require(frame.water_surface->world_origin_time[2] == seconds &&
            frame.atmosphere->origin_time[2] == seconds,
            std::string(label) + ": water/atmosphere does not use the expected shown tick");
    const auto projected = project_engine_environment(
        EngineEnvironmentSettingsV1{1u, engine_time_hour(tick), .25f, .25f, true},
        frame.surface_lighting);
    require(frame.surface_lighting.ambient == projected.lighting.ambient &&
        frame.surface_lighting.sun_direction == projected.lighting.sun_direction &&
        frame.surface_lighting.sun_color_intensity == projected.lighting.sun_color_intensity &&
        frame.moon_lighting->direction == projected.moon.direction &&
        frame.moon_lighting->color_intensity == projected.moon.color_intensity,
        std::string(label) + ": environment calendar disagrees with shown tick");
}

void marker_present(const Image& image) {
    constexpr std::array<std::uint8_t, 4> marker{31u, 189u, 78u, 255u};
    for (std::size_t y = 14; y < 50; ++y) for (std::size_t x = 14; x < 90; ++x) {
        const auto p = (y * extent.width + x) * 4u;
        require(std::equal(marker.begin(), marker.end(),
            image.rgba.begin() + static_cast<std::ptrdiff_t>(p)),
            "Native opaque UI does not have the known marker RGBA");
    }
}

std::size_t changed_pixels(const Image& first, const Image& second) {
    require(first.rgba.size() == second.rgba.size(), "Different native image dimensions");
    std::size_t count{};
    for (std::size_t p = 0; p < first.rgba.size(); p += 4u)
        if (!std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                        first.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                        second.rgba.begin() + static_cast<std::ptrdiff_t>(p))) ++count;
    return count;
}

struct LocalDriver {
    double accumulator{};
    std::uint64_t steps{};
    void update(ClientSession& session, const GameView& view, double delta) {
        require(!session.remote() && delta >= 0. && delta <= .25,
                "Local driver requires a bounded production frame delta");
        if (!view.paused()) {
            accumulator += delta * view.speed();
            int catch_up{};
            while (accumulator >= tick_seconds && catch_up < 20) {
                session.advance(); accumulator -= tick_seconds; ++catch_up; ++steps;
            }
        } else accumulator = 0.;
    }
};

void cpu_calendar_checks() {
    require(EngineControlSettings{}.time_source == EngineTimeSource::ExplicitDiagnostic,
            "Default time mode broke diagnostic compatibility");
    require(engine_time_hour(0.) == 6.f && engine_time_hour(7200.) == 12.f &&
            engine_time_hour(14400.) == 18.f && engine_time_hour(21600.) == 0.f &&
            engine_time_hour(28800.) == 6.f, "Session hour disagrees with the production calendar");
    const float before_midnight = engine_time_hour(std::nextafter(21600., 0.));
    require(before_midnight >= 0.f && before_midnight < 24.f && before_midnight > 23.99f,
            "Immediately-before-midnight hour converted to invalid float24");
    for (const double tick : std::array<double, 3>{-1.,
             std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        bool rejected{};
        try { (void)engine_time_hour(tick); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid shown tick was accepted by calendar projection");
    }
}
} // namespace

int main(int argc, char** argv) {
    fs::path output; bool owned{};
    Json captures = Json::array(), calendar_cases = Json::array();
    Json clock_controls = Json::array(), transactional_rejections = Json::array();
    try {
        require(argc == 4, "Usage: GameViewWaterProbe runtime-assets kit fresh-output (session time)");
        const auto assets = fs::absolute(argument(argv[1])), kit = fs::absolute(argument(argv[2]));
        output = fs::absolute(argument(argv[3]));
        require(output.parent_path() == assets.parent_path().parent_path() && !fs::exists(output),
                "Session time output must be a new sibling build child");
        require(fs::space(output.parent_path()).available >= 384u * 1024u * 1024u,
                "Insufficient session-time evidence headroom");
        require(fs::create_directory(output), "Could not create session-time output"); owned = true;
        cpu_calendar_checks(); create_time_worlds(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Session Shown Time - diagnostic controls", 1920, 1080, 0),
            SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()),
                "Native session-time window failed");
        RendererOptions options; options.world_graph = WorldGraph::WaterAtmosphereSlice;
        options.collect_telemetry = true;
        Renderer2D renderer(window.get(), assets, options); renderer.set_logical_size(extent);
        require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
                "Hardware D3D12 is required");
        GameView view(renderer), oracle(renderer);
        for (auto* candidate : std::array<GameView*, 2>{&view, &oracle}) {
            candidate->set_logical_size(extent);
            candidate->load_engine_control_art(AssetCatalog(kit)); candidate->load_engine_water_art();
        }
        EngineControlSettings settings; settings.camera_x = 28.f; settings.camera_y = 25.f;
        settings.cell_radius = 8; settings.water_enabled = true;
        settings.environment = EngineEnvironmentSettingsV1{1u, 12.f, .25f, .25f, true};
        settings.time_source = EngineTimeSource::SessionShown;
        settings.lighting.counts[0] = 1u;
        settings.lighting.lights[0].color_intensity[3] = 0.f;
        settings.lighting.lights[0].occluder_range_flags[3] = 9001u;
        ClientSession session(70421);
        const auto compose = [&](GameView& target, const ClientSession& current, double seconds,
                                 const EngineControlSettings& configured) {
            Frame2D frame;
            const auto stats = target.compose_engine_control(current, seconds, configured, controls, frame);
            require(stats.instances_drawn == controls.size() && stats.bridge_decks == 3u &&
                    stats.water_cells > 0u && frame.contact_shadows.empty(),
                    "Time scene is vacuous or unexpectedly uses contact input");
            require(frame.surface_lighting.counts[0] == 3u &&
                std::memcmp(&frame.surface_lighting.lights[0], &settings.lighting.lights[0], sizeof(LightGpu)) == 0,
                "Time adapter overwrote incoming local light or omitted authored house light");
            return frame;
        };
        load_current(session, view, output / world_saves[0]);
        auto morning = compose(view, session, 10., settings);
        shown_matches(morning, 0., "initial06");
        const auto morning_image = capture(renderer, morning, output, "time-calendar-06-session", captures);
        marker_present(morning_image);
        const auto carrier = renderer.read_water_coverage_v1();
        save_bytes(output / "session-time-carrier.rgba8", carrier.rgba8);
        const auto counts = carrier_counts(carrier);
        const auto checked_capture = [&](const Frame2D& frame, std::string_view name) {
            require(captures.size() < capture_limit, "Session time PNG bound exceeded");
            const auto image = capture(renderer, frame, output, name, captures);
            marker_present(image);
            require(renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,
                    "Session time changed raw water carrier classification");
            same_locals_and_emission(morning, frame);
            return image;
        };
        for (std::size_t index = 0; index < calendar_hours.size(); ++index) {
            load_current(session, view, output / world_saves[index]);
            const auto tick = session.ticks();
            auto shown = compose(view, session, 10., settings);
            shown_matches(shown, static_cast<double>(tick), "calendar landmark");
            auto explicit_settings = settings;
            explicit_settings.time_source = EngineTimeSource::ExplicitDiagnostic;
            explicit_settings.environment->time_hour = calendar_hours[index];
            const auto diagnostic = compose(view, session, static_cast<double>(tick) * tick_seconds,
                                            explicit_settings);
            require(frame_state(shown) == frame_state(diagnostic),
                    "Calendar SessionShown differs from equivalent ExplicitDiagnostic frame");
            const auto label = std::array<std::string_view, 4>{"06", "12", "18", "00"}[index];
            const auto shown_image = index == 0u ? morning_image :
                checked_capture(shown, "time-calendar-" + std::string(label) + "-session");
            const auto explicit_image = checked_capture(diagnostic,
                "time-calendar-" + std::string(label) + "-explicit");
            require(shown_image.rgba == explicit_image.rgba, "Calendar equivalent native pixels differ");
            const auto from_morning = changed_pixels(morning_image, shown_image);
            if (index != 0u) require(from_morning > 100u, "Calendar native response is empty");
            calendar_cases.push_back({{"hour", calendar_hours[index]}, {"authority_tick", tick},
                {"shown_seconds", shown.water_surface->world_origin_time[2]},
                {"payload_equal", true}, {"png_equal", true},
                {"changed_pixels_from_06", from_morning}, {"payload", time_payload(shown)}});
        }

        // Authority updates arrive at compose boundaries; the existing local
        // presentation clock deliberately trails by50ms, fills the interval,
        // and never extrapolates beyond the newest authority tick.
        load_current(session, view, output / world_saves[0]);
        view.set_paused(false); view.set_speed(1);
        (void)compose(view, session, 10., settings);
        session.advance();
        auto lag = compose(view, session, 10.05, settings);
        shown_matches(lag, 0., "one-tick authority arrival lag");
        require(session.ticks() == 1u, "Actual ClientSession did not advance exactly one tick");
        auto half_tick = compose(view, session, 10.075, settings);
        shown_matches(half_tick, .5, "half-tick presentation interpolation");
        auto caught = compose(view, session, 10.125, settings);
        shown_matches(caught, 1., "presentation catch-up");
        const auto caught_payload = time_payload(caught);
        for (int index = 1; index <= 8; ++index) {
            auto held = compose(view, session, 10.125 + index * .125, settings);
            require(time_payload(held) == caught_payload && session.ticks() == 1u,
                    "Starved local presentation advanced beyond authority");
        }
        clock_controls.push_back({{"id", "authority_lag_interpolation_catchup_starvation"},
            {"passed", true}, {"authority_ticks", 1}, {"interpolated_tick", .5},
            {"starvation_compositions", 8}});

        for (const int speed : std::array<int, 3>{1, 2, 4}) {
            load_current(session, view, output / world_saves[0]);
            view.set_speed(speed); view.set_paused(false);
            (void)compose(view, session, 20., settings);
            LocalDriver driver;
            float previous_phase{};
            Frame2D endpoint;
            for (int index = 1; index <= 10; ++index) {
                driver.update(session, view, .1);
                endpoint = compose(view, session, 20. + index * .1, settings);
                const float phase = endpoint.water_surface->world_origin_time[2];
                require(phase >= previous_phase && phase <= static_cast<float>(static_cast<double>(session.ticks()) * tick_seconds) &&
                        phase == endpoint.atmosphere->origin_time[2],
                        "Main-policy clock is nonmonotonic, extrapolated or misaligned");
                previous_phase = phase;
            }
            require(driver.steps == static_cast<std::uint64_t>(20 * speed) && session.ticks() == driver.steps,
                    "Main-policy actual tick count disagrees with selected speed");
            shown_matches(endpoint, static_cast<double>(driver.steps - static_cast<std::uint64_t>(speed)),
                          "speed endpoint with local50ms presentation lag");
            const auto image = checked_capture(endpoint, "time-speed-" + std::to_string(speed));
            const auto delta = changed_pixels(morning_image, image);
            require(delta > 0u, "Actual session advancement changed no native pixels");
            auto settled = compose(view, session, 21.125, settings);
            shown_matches(settled, static_cast<double>(session.ticks()), "speed endpoint catch-up");
            const auto settled_payload = time_payload(settled);
            view.set_paused(true);
            driver.accumulator = .025;
            driver.update(session, view, .25);
            auto paused = compose(view, session, 22.125, settings);
            require(time_payload(paused) == settled_payload && driver.accumulator == 0. &&
                    session.ticks() == static_cast<std::uint64_t>(20 * speed),
                    "Selected-speed pause changed authority, payload or accumulator");
            view.set_paused(false);
            driver.update(session, view, .1);
            (void)compose(view, session, 22.225, settings);
            auto resumed_at_speed = compose(view, session, 22.35, settings);
            require(session.ticks() == static_cast<std::uint64_t>(22 * speed),
                    "Selected-speed resume replayed paused wall time");
            shown_matches(resumed_at_speed, static_cast<double>(session.ticks()),
                          "selected-speed pause/resume catch-up");
            clock_controls.push_back({{"id", "main_policy_speed_" + std::to_string(speed)},
                {"passed", true}, {"speed", speed}, {"driver_frames", 10},
                {"authority_ticks", 20 * speed}, {"steps", 20 * speed},
                {"endpoint_shown_tick", 19 * speed}, {"pause_payload_exact", true},
                {"resume_authority_ticks", session.ticks()},
                {"native_changed_pixels", delta}});
        }

        const auto paused_ticks = session.ticks();
        view.set_paused(true);
        const auto paused_base = compose(view, session, 23., settings);
        const auto paused_image = checked_capture(paused_base, "time-paused-base");
        LocalDriver paused_driver; paused_driver.accumulator = .025;
        for (int index = 1; index <= 8; ++index) {
            paused_driver.update(session, view, .25);
            auto paused = compose(view, session, 23. + index * .25, settings);
            require(frame_state(paused) == frame_state(paused_base),
                    "Paused wall-time composition changed the published frame");
        }
        require(session.ticks() == paused_ticks && paused_driver.steps == 0u && paused_driver.accumulator == 0.,
                "Paused main-policy driver advanced authority or kept its accumulator");
        const auto paused_repeat = compose(view, session, 25., settings);
        require(checked_capture(paused_repeat, "time-paused-repeat").rgba == paused_image.rgba,
                "Paused native time output is not exact");
        view.set_paused(false); view.set_speed(1);
        LocalDriver resume_driver;
        resume_driver.update(session, view, .1);
        (void)compose(view, session, 25.1, settings);
        const auto resumed = compose(view, session, 25.225, settings);
        shown_matches(resumed, static_cast<double>(paused_ticks + 2u), "resume has no paused-wall catch-up");
        const auto resumed_delta = changed_pixels(paused_image,
            checked_capture(resumed, "time-resumed"));
        require(resumed_delta > 0u, "Resume changed no native pixels");
        clock_controls.push_back({{"id", "pause_resume_main_policy"}, {"passed", true},
            {"paused_authority_ticks", paused_ticks}, {"paused_driver_steps", 0},
            {"paused_compositions", 8}, {"paused_native_exact", true},
            {"resumed_authority_ticks", session.ticks()}, {"resumed_native_changed_pixels", resumed_delta}});

        const auto repeated_epoch_reload = [&](double start) {
            load_current(session, view, output / world_saves[0]);
            view.set_paused(false); view.set_speed(1);
            (void)compose(view, session, start, settings);
            for (int tick = 0; tick < 10; ++tick) session.advance();
            const auto lagged = compose(view, session, start + .1, settings);
            shown_matches(lagged, 2.2, "repeated-epoch load control starts with shown lag");
            const auto epoch = session.control_epoch();
            std::string error;
            const bool loaded = session.load(output / world_saves[4], error);
            require(loaded, "Repeated-epoch load: " + error);
            require(!session.last_load().copied && session.control_epoch() == epoch && session.ticks() == 10u,
                    "Repeated-epoch load must preserve authority tick/epoch without migration");
            view.reset_world_state();
            auto reset_frame = compose(view, session, start + .11, settings);
            shown_matches(reset_frame, 10., "production load reset at unchanged tick/epoch");
            return reset_frame;
        };
        const auto reset = repeated_epoch_reload(30.);
        const auto reset_image = checked_capture(reset, "time-load-reset");
        const auto reset_repeat = repeated_epoch_reload(31.);
        require(frame_state(reset) == frame_state(reset_repeat) &&
                checked_capture(reset_repeat, "time-load-reset-repeat").rgba == reset_image.rgba,
                "Production reset/load sequence did not reset a repeated-epoch session");
        clock_controls.push_back({{"id", "production_load_reset_repeated_epoch"}, {"passed", true},
            {"epoch_repeated", true}, {"authority_tick_before_after", 10},
            {"before_shown_tick", 2.2}, {"after_shown_tick", 10}, {"native_exact", true}});

        ClientSession other(70421);
        load_current(other, oracle, output / world_saves[4]);
        for (int tick = 0; tick < 4; ++tick) { session.advance(); other.advance(); }
        auto pointer_lag = compose(view, session, 31.21, settings);
        require(pointer_lag.water_surface->world_origin_time[2] < static_cast<float>(14 * tick_seconds),
                "Pointer replacement oracle must begin with presentation lag");
        auto pointer_reset = compose(view, other, 31.22, settings);
        shown_matches(pointer_reset, 14., "session pointer replacement at equal authority tick/epoch");
        other.advance();
        (void)compose(view, other, 32.05, settings);
        auto wall_rollback = compose(view, other, 31., settings);
        shown_matches(wall_rollback, 15., "wall-clock rollback reset");
        const auto old_epoch = other.control_epoch();
        std::string error;
        const bool loaded = other.load(output / world_saves[4], error);
        require(loaded && !other.last_load().copied, "Authority rollback load: " + error);
        require(other.control_epoch() == old_epoch, "Authority rollback fixture epoch unexpectedly changed");
        auto tick_rollback = compose(view, other, 33., settings);
        shown_matches(tick_rollback, 10., "authority tick rollback reset");
        clock_controls.push_back({{"id", "pointer_wall_and_authority_rollback"}, {"passed", true},
            {"authority_rollback_epoch_repeated", true}, {"pointer_shown_tick", 14},
            {"pointer_authority_tick_equal", true}, {"pointer_epoch_equal", true},
            {"wall_rollback_shown_tick", 15}, {"authority_rollback_shown_tick", 10}});

        // A new local session actually changes epoch1 to2 on its first plain
        // world load; subsequent same-save loads need the explicit reset above.
        ClientSession fresh(70421);
        Frame2D initial;
        (void)view.compose_engine_control(fresh, 34., settings, {}, initial);
        const auto initial_epoch = fresh.control_epoch();
        for (int tick = 0; tick < 10; ++tick) fresh.advance();
        (void)view.compose_engine_control(fresh, 34.1, settings, {}, initial);
        require(initial.water_surface->world_origin_time[2] > 0.f &&
                initial.water_surface->world_origin_time[2] < static_cast<float>(10 * tick_seconds),
                "Changed-epoch control must start with a real presentation lag");
        const auto before_epoch_seconds = initial.water_surface->world_origin_time[2];
        const bool epoch_loaded = fresh.load(output / world_saves[4], error);
        require(epoch_loaded && !fresh.last_load().copied, "Changed-epoch load: " + error);
        require(fresh.control_epoch() != initial_epoch, "Fresh session load did not change epoch");
        require(fresh.ticks() == 10u, "Changed-epoch load unexpectedly changed authority tick");
        auto epoch_reset = compose(view, fresh, 34.11, settings);
        shown_matches(epoch_reset, 10., "actual changed control epoch without tick/wall rollback or gap");
        clock_controls.push_back({{"id", "actual_changed_epoch"}, {"passed", true},
            {"old_epoch", initial_epoch}, {"new_epoch", fresh.control_epoch()},
            {"authority_tick_before_after", 10}, {"before_shown_seconds", before_epoch_seconds}, {"shown_tick", 10}});

        struct InvalidCase { std::string id; EngineControlSettings settings;
                             std::array<EngineControlInstance, 2> instances; double seconds{10.25}; };
        std::vector<InvalidCase> invalid;
        const auto add_bad = [&](std::string id, const auto& edit) {
            InvalidCase value{std::move(id), settings, controls}; edit(value); invalid.push_back(value);
        };
        add_bad("enum", [](auto& bad) { bad.settings.time_source = static_cast<EngineTimeSource>(99); });
        add_bad("missing_environment", [](auto& bad) { bad.settings.environment.reset(); });
        add_bad("manual_atmosphere_conflict", [](auto& bad) { bad.settings.atmosphere = EngineAtmosphereSettings{}; });
        add_bad("water_disabled", [](auto& bad) { bad.settings.water_enabled = false; });
        add_bad("environment_version", [](auto& bad) { bad.settings.environment->version = 2u; });
        const std::array<float, 3> bad_weather{-.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()};
        const std::array<std::string_view, 3> weather_names{"below_zero", "above_one", "nan"};
        for (std::size_t index = 0; index < bad_weather.size(); ++index) {
            const float value = bad_weather[index];
            add_bad("cloud_" + std::string(weather_names[index]),
                    [&](auto& bad) { bad.settings.environment->cloud01 = value; });
            add_bad("haze_" + std::string(weather_names[index]),
                    [&](auto& bad) { bad.settings.environment->haze01 = value; });
        }
        add_bad("camera_nan", [](auto& bad) { bad.settings.camera_x = std::numeric_limits<float>::quiet_NaN(); });
        add_bad("camera_window", [](auto& bad) { bad.settings.camera_x = static_cast<float>(world_min); });
        add_bad("unknown_sprite", [](auto& bad) { bad.instances[1].sprite_id = "missing.session.time.sprite"; });
        add_bad("zero_owner", [](auto& bad) { bad.instances[1].owner_id = 0u; });
        add_bad("duplicate_occluder", [](auto& bad) { bad.instances[1].first_occluder_id = bad.instances[0].first_occluder_id; });
        add_bad("instance_nan", [](auto& bad) { bad.instances[1].x = std::numeric_limits<float>::quiet_NaN(); });
        add_bad("wall_nan", [](auto& bad) { bad.seconds = std::numeric_limits<double>::quiet_NaN(); });
        Frame2D retained;
        for (const auto& bad : invalid) {
            load_current(session, view, output / world_saves[0]);
            oracle.reset_world_state(); view.set_speed(1); oracle.set_speed(1);
            view.set_paused(false); oracle.set_paused(false);
            retained = compose(view, session, 10., settings);
            (void)compose(oracle, session, 10., settings);
            const auto retained_state = frame_state(retained);
            const auto old_screen = view.tile_screen(26.f, 24.f);
            const auto old_pick = view.cell_at_screen(old_screen.x, old_screen.y);
            for (int tick = 0; tick < 4; ++tick) session.advance();
            bool rejected{};
            try { (void)view.compose_engine_control(session, bad.seconds, bad.settings, bad.instances, retained); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid session-time candidate accepted: " + bad.id);
            const auto screen = view.tile_screen(26.f, 24.f);
            require(frame_state(retained) == retained_state && screen.x == old_screen.x &&
                    screen.y == old_screen.y && view.cell_at_screen(old_screen.x, old_screen.y) == old_pick,
                    "Invalid session-time candidate published frame/camera/picks: " + bad.id);
            const auto clean = compose(oracle, session, 10.1, settings);
            const auto recovered = compose(view, session, 10.1, settings);
            require(time_payload(clean) == time_payload(recovered),
                    "Rejected candidate advanced shared presentation clock: " + bad.id);
            // Initial tick0 -> actual tick4, local delay50ms, dt100ms, maximum
            // gradual correction2.2ticks. A rejected10.25 update would instead
            // make10.1 a rollback and snap to4, so the oracle is nonvacuous.
            shown_matches(recovered, 2.2, "transaction recovery " + bad.id);
            require(recovered.water_surface->world_origin_time[2] < static_cast<float>(4 * tick_seconds),
                    "Clock transaction oracle accidentally became an authority snap");
            same_locals_and_emission(clean, recovered);
            transactional_rejections.push_back({{"id", bad.id}, {"passed", true},
                {"frame_camera_pick_clock_exact", true}, {"authority_tick", 4},
                {"recovery_shown_seconds", recovered.water_surface->world_origin_time[2]}});
        }
        require(checked_capture(retained, "time-after-invalid-retained").rgba == morning_image.rgba,
                "Retained invalid-input native frame changed");
        require(captures.size() == 17u, "Session-time capture plan count changed");
        save_report(output / "session-time-native-result.json", {{"complete", true},
            {"scope", "Local ClientSession shown-time adapter/native causality; explicit diagnostic weather"},
            {"time_source", "SessionShown"}, {"categories", {"house", "resident.empty"}},
            {"controls", controls.size()}, {"backend", renderer.device_info().backend},
            {"gpu", renderer.device_info().device}, {"driver", renderer.device_info().driver},
            {"resolution", {1920, 1080}}, {"graph", "WaterAtmosphereSlice"},
            {"captures", captures}, {"capture_limit", capture_limit}, {"world_saves", world_saves},
            {"calendar_cases", calendar_cases}, {"clock_controls", clock_controls},
            {"transactional_rejections", transactional_rejections}, {"raw_carrier", counts},
            {"raw_carrier_exact", true}, {"opaque_ui_exact", true}, {"known_ui_rgba", {31, 189, 78, 255}},
            {"locals_emission_metadata_exact", true}, {"explicit_diagnostic_default", true},
            {"explicit_weather", true}, {"remote_verified", false},
            {"targets", stable_targets(renderer.ground_forward_status())}, {"fps", nullptr},
            {"gpu_execution_ms", nullptr}, {"E06", "OPEN"}, {"full_engine", "OPEN"},
            {"reference_fidelity", "NOT_ACCEPTED"},
            {"limitations", {"Local embedded session; no packet/host/weather authority proof",
                "Diagnostic house/resident and authored zero-population calendar fixture",
                "Completed Bridge records authored in fixture; construction was not executed",
                "Same-pointer same-save reload requires production reset_world_state call",
                "No normal-game startup integration, physical input/video/DPI or performance evidence"}}});
        check_budget(output); std::cout << "GameView session time native checks passed\n"; return 0;
    } catch (const std::exception& error) {
        if (owned) try {
            save_report(output / "session-time-native-result.json", {{"complete", false},
                {"error", error.what()}, {"captures", captures}, {"calendar_cases", calendar_cases},
                {"clock_controls", clock_controls}, {"transactional_rejections", transactional_rejections},
                {"world_saves", world_saves}, {"explicit_weather", true}, {"remote_verified", false},
                {"fps", nullptr}, {"gpu_execution_ms", nullptr}, {"E06", "OPEN"},
                {"full_engine", "OPEN"}, {"reference_fidelity", "NOT_ACCEPTED"}});
        } catch (...) {}
        std::cerr << "GameView session time probe failed: " << error.what() << '\n'; return 1;
    }
}

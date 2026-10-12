#include "engine_control_probe.hpp"
#include "ribterra/network_session.hpp"
#include <chrono>
#include <cstring>
#include <iterator>
#include <limits>
#include <thread>

namespace {
constexpr std::size_t capture_limit = 20u;
constexpr std::array<std::string_view, 3> world_saves{
    "weather-world-dry.save", "weather-world-wet.save", "weather-world-reset.save"};
const std::array<EngineControlInstance, 2> controls{{
    {"control.house.sprite", 26.f, 22.f, 1.f, 101u, 101u, 0.f, true, 1001u},
    {"control.resident.empty.sprite", 26.f, 24.f, 1.f, 102u, 111u, 0.f, false, 0u}}};

void load_current(ClientSession& session, GameView& view, const fs::path& path) {
    std::string error;
    require(session.load(path, error), "Session load: " + error);
    require(!session.last_load().copied, "Weather fixture unexpectedly migrated");
    view.reset_world_state();
}

void create_weather_worlds(const fs::path& output) {
    World world(70421, 0);
    auto state = world.export_state();
    require(state.residents.empty() && state.routes.empty() && state.path_batches.empty(),
            "Weather fixture must have no actors/routes/pending paths");
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
    for (std::size_t index = 0; index < world_saves.size(); ++index) {
        auto candidate = state;
        candidate.ticks = index == 2u ? 7210u : 7200u;
        candidate.climate.wetness_permille = index == 0u ? 0u : 600u;
        candidate.climate.wetness_remainder = index == 0u ? 0 : 14400;
        // Host-only authored time/cover fixture; no skipped history is replayed.
        const WeatherContext context{candidate.seed, {candidate.climate.algorithm_version,
            candidate.climate.climate_profile, candidate.climate.scenario}};
        const auto calendar = make_calendar_tick(candidate.ticks, candidate.climate.calendar_offset_ticks);
        require(calendar.has_value(), "Fixture calendar overflow");
        WeatherScheduleCache cache(context, *calendar);
        candidate.climate.evaluated_simulation_tick = candidate.ticks;
        candidate.climate.current = cache.sample(*calendar);
        validate_climate_runtime(candidate.climate, candidate.ticks, context, cache);
        std::string error;
        require(world.import_state(std::move(candidate), error), "Weather fixture import: " + error);
        for (int x = 27; x <= 29; ++x)
            require(world.walkable(x, 25) && world.tile(x, 25).terrain == Terrain::Water,
                    "Authored deck lost actual water terrain");
        const auto path = output / world_saves[index];
        require(!fs::exists(path), "Refusing fixture save overwrite");
        require(world.save(path, error), "Weather fixture save: " + error);
    }
}

Json projection_row(const EngineWeatherProjectionV1& value) {
    return {{"version", value.version}, {"source_simulation_tick", value.source_simulation_tick},
        {"source_calendar_tick", value.source_calendar_tick}, {"shown_calendar_tick", value.shown_calendar_tick},
        {"environment", {value.environment.version, value.environment.time_hour,
            value.environment.cloud01, value.environment.haze01, value.environment.atmosphere_enabled}},
        {"kind", static_cast<int>(value.kind)}, {"precipitation", static_cast<int>(value.precipitation)},
        {"temperature_deci_c", value.temperature_deci_c}, {"wind_direction", static_cast<int>(value.wind_direction)},
        {"precipitation01", value.precipitation01}, {"wind01", value.wind01},
        {"wetness01", value.wetness01}, {"snow_cover01", value.snow_cover01}};
}

Json weather_row(const WeatherView& value) {
    const auto& current = value.current;
    Json row{{"known",value.known}, {"algorithm_version",value.algorithm_version},
        {"profile",static_cast<int>(value.climate_profile)}, {"scenario",static_cast<int>(value.scenario)},
        {"offset",value.calendar_offset_ticks}, {"evaluated_tick",value.evaluated_simulation_tick},
        {"calendar_tick",value.calendar_tick}, {"snow_permille",value.snow_cover_permille},
        {"wet_permille",value.wetness_permille}, {"wet_remainder",value.wetness_remainder},
        {"next_storm",{value.next_storm_start_calendar_tick,value.next_storm_end_calendar_tick}},
        {"current",{{"kind",static_cast<int>(current.kind)}, {"precipitation",static_cast<int>(current.precipitation)},
            {"wind_direction",static_cast<int>(current.wind_direction)}, {"work_reason",static_cast<int>(current.work_reason)},
            {"temperature_deci_c",current.temperature_deci_c}, {"precipitation_permille",current.precipitation_permille},
            {"wind_permille",current.wind_permille}, {"outdoor_work_rate_bp",current.outdoor_work_rate_bp},
            {"period",{current.period_start_calendar_tick,current.period_end_calendar_tick}}}}};
    for (const auto& day : value.forecast) {
        Json card{{"day",day.calendar_day}, {"temperature",{day.minimum_temperature_deci_c,day.maximum_temperature_deci_c}},
            {"dominant_kind",static_cast<int>(day.dominant_kind)}, {"precipitation_summary",static_cast<int>(day.precipitation_summary)},
            {"has_storm",day.has_storm}, {"interval_count",day.interval_count}, {"intervals",Json::array()}};
        for (const auto& interval : day.intervals)
            card["intervals"].push_back({interval.start_tick_in_day,interval.end_tick_in_day,static_cast<int>(interval.kind)});
        row["forecast"].push_back(std::move(card));
    }
    return row;
}

Json geometry_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass), static_cast<int>(draw.blend), draw.texture.slot,
        draw.texture.generation, draw.first_index, draw.index_count, draw.scissor.x,
        draw.scissor.y, draw.scissor.w, draw.scissor.h, draw.scissor_enabled,
        draw.surface_lit, draw.sprite_material_index, draw.ground_material_index};
}

Json time_payload(const Frame2D& frame) {
    require(frame.water_surface && frame.atmosphere && frame.moon_lighting, "Incomplete weather payload");
    const auto& water = *frame.water_surface;
    const auto& atmosphere = *frame.atmosphere;
    Json result{{"water", {water.world_origin_time, water.flow_response, water.tint, water.viewport}},
        {"atmosphere", {atmosphere.origin_time, atmosphere.depth, atmosphere.density,
            atmosphere.far_color_noise, atmosphere.morning_color, atmosphere.coastal_color,
            atmosphere.wind_shore, atmosphere.counts_version, atmosphere.water_aabbs}},
        {"ambient", frame.surface_lighting.ambient}, {"sun_direction", frame.surface_lighting.sun_direction},
        {"sun_color_intensity", frame.surface_lighting.sun_color_intensity},
        {"moon", {frame.moon_lighting->direction, frame.moon_lighting->color_intensity}},
        {"counts", frame.surface_lighting.counts}};
    for (const auto& light : frame.surface_lighting.lights)
        result["lights"].push_back({light.anchor_radius, light.direction_cone,
            light.color_intensity, light.occluder_range_flags, light.height_falloff_reveal});
    return result;
}

// Compare semantic fields, rather than wrapper padding; handles are included in
// in-process equality but never persisted as repeat-stable evidence.
Json frame_state(const Frame2D& frame) {
    Json result{{"time", time_payload(frame)}, {"size", {frame.logical_size.width, frame.logical_size.height}},
        {"clear", {frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a}}, {"indices", frame.indices},
        {"hdr", frame.linear_hdr_world}, {"exposure", frame.world_exposure},
        {"water_normal", {frame.water_normal.slot, frame.water_normal.generation}}};
    require(frame.contact_shadows.empty(), "Weather probe unexpectedly enabled contacts");
    if (frame.contact_projection)
        result["projection"] = {frame.contact_projection->origin_xy, frame.contact_projection->zoom};
    for (const auto& v : frame.vertices)
        result["vertices"].push_back({v.position.x, v.position.y, v.uv.x, v.uv.y,
                                      v.color.r, v.color.g, v.color.b, v.color.a});
    for (const auto& v : frame.surface_vertices)
        result["surface_vertices"].push_back({v.world_position, v.normal});
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        require(draw != nullptr, "Unexpected diagnostic text command");
        result["commands"].push_back(geometry_row(*draw));
    }
    for (const auto& draw : frame.sprite_material_draws)
        result["sprites"].push_back({draw.material.slot, draw.material.generation, draw.surface.uv_regions,
            draw.surface.anchor_export, draw.surface.source_size_pivot, draw.surface.depth_scale,
            draw.surface.emission, draw.surface.response, draw.reveal, draw.receiver_mask,
            draw.receiver_owner, draw.emission_scale});
    for (const auto& draw : frame.ground_material_draws)
        result["ground"].push_back({draw.albedo.slot, draw.albedo.generation, draw.surface.response,
            draw.surface.flags, draw.surface.visibility, static_cast<int>(draw.contact_receiver),
            static_cast<int>(draw.water_surface_class), draw.shore_edges, draw.geometric_coverage});
    for (const auto& draw : frame.local_occluders)
        result["occluders"].push_back({draw.center_base, draw.axis_x_extent, draw.axis_y_radius, draw.shape_flags_id});
    for (const auto& draw : frame.sky_shadow_triangles)
        result["sky"].push_back({draw.vertices[0].position, draw.vertices[1].position,
                               draw.vertices[2].position, draw.source_mask, draw.reveal});
    for (const auto& draw : frame.reflection_draws)
        result["reflections"].push_back({geometry_row(draw.draw), draw.strength, draw.fade});
    return result;
}

void source_invariant(const Frame2D& a, const Frame2D& b) {
    require(a.surface_lighting.counts == b.surface_lighting.counts &&
        std::memcmp(a.surface_lighting.lights.data(), b.surface_lighting.lights.data(),
                    sizeof(a.surface_lighting.lights)) == 0, "Weather changed local source bytes");
    require(a.sprite_material_draws.size() == b.sprite_material_draws.size(), "Sprite count changed");
    for (std::size_t index = 0; index < a.sprite_material_draws.size(); ++index) {
        auto first = a.sprite_material_draws[index].surface;
        auto second = b.sprite_material_draws[index].surface;
        first.response[0] = second.response[0] = 0.f;
        require(std::memcmp(&first, &second, sizeof(first)) == 0 &&
                a.sprite_material_draws[index].emission_scale == b.sprite_material_draws[index].emission_scale,
                "Weather changed authored emission/source metadata");
    }
}

void set_material_wetness(Frame2D& frame, float ground, float sprite) {
    for (auto& draw : frame.ground_material_draws)
        draw.surface.response[1] = draw.water_surface_class == GroundSurfaceClass::Water ? 1.f : ground;
    for (auto& draw : frame.sprite_material_draws) draw.surface.response[0] = sprite;
}

void shown_matches(const Frame2D& frame, const EngineControlStats& stats, const ClientSession& session, double tick) {
    require(stats.authoritative_weather.has_value(), "Authority stats projection is missing");
    auto expected = project_engine_weather(session.weather(), session.ticks(), tick);
    require(std::abs(stats.authoritative_weather->shown_calendar_tick-expected.shown_calendar_tick) < 1.e-7,
            "Published shown tick differs");
    expected.shown_calendar_tick = stats.authoritative_weather->shown_calendar_tick;
    require(projection_row(*stats.authoritative_weather) == projection_row(expected), "Published projection differs");
    require(frame.water_surface && frame.atmosphere &&
        frame.water_surface->world_origin_time[2] == static_cast<float>(tick * tick_seconds) &&
        frame.atmosphere->origin_time[2] == static_cast<float>(tick * tick_seconds) &&
        frame.water_surface->world_origin_time[3] == expected.wind01,
        "Water/fog phase or normalized wind differs from shown authority");
    for (const auto& draw : frame.ground_material_draws)
        require(draw.surface.response[1] == (draw.water_surface_class == GroundSurfaceClass::Water
                ? 1.f : expected.wetness01), "Ground/water wetness differs");
    for (const auto& draw : frame.sprite_material_draws)
        require(draw.surface.response[0] == expected.wetness01, "Sprite scalar wetness differs");
}

struct LocalDriver {
    double accumulator{};
    std::uint64_t steps{};
    void update(ClientSession& session, const GameView& view, double delta) {
        require(!session.remote() && delta >= 0. && delta <= .25, "Invalid local frame delta");
        if (view.paused()) { accumulator = 0.; return; }
        accumulator += delta * view.speed();
        int catch_up{};
        while (accumulator >= tick_seconds && catch_up < 20) {
            session.advance(); accumulator -= tick_seconds; ++catch_up; ++steps;
        }
    }
};

WeatherView structural_view(WeatherKind kind) {
    World world(70421, 0);
    auto value = world.weather();
    value.evaluated_simulation_tick = value.calendar_tick = 7200u;
    const bool event = kind == WeatherKind::Precipitation || kind == WeatherKind::Storm;
    value.current = {kind, event ? PrecipitationKind::Snow : PrecipitationKind::None,
        WindDirection::NE, WeatherWorkReason::None, static_cast<std::int16_t>(event ? -50 : 50),
        static_cast<std::uint16_t>(event ? 800u : 0u), 300u, 10000u, 0u, ticks_per_day};
    const auto modifier = weather_work_modifier(kind, value.current.precipitation, value.current.temperature_deci_c);
    value.current.outdoor_work_rate_bp = modifier.rate_bp; value.current.work_reason = modifier.reason;
    value.snow_cover_permille = 650u; value.wetness_permille = 600u; value.wetness_remainder = 14400;
    value.next_storm_start_calendar_tick = 0u;
    value.next_storm_end_calendar_tick = kind == WeatherKind::Storm ? ticks_per_day : 0u;
    for (std::size_t index = 0; index < value.forecast.size(); ++index) {
        auto& day = value.forecast[index]; day = {};
        day.calendar_day = static_cast<std::uint32_t>(index + 1u);
        day.minimum_temperature_deci_c = day.maximum_temperature_deci_c = value.current.temperature_deci_c;
        day.dominant_kind = kind;
        day.precipitation_summary = event ? PrecipitationSummary::Snow : PrecipitationSummary::None;
        day.has_storm = kind == WeatherKind::Storm;
        day.intervals[0] = {0u, static_cast<std::uint32_t>(ticks_per_day), kind}; day.interval_count = 1u;
    }
    validate_weather_view(value, 7200u);
    return value;
}

Json component_checks() {
    Json result{{"scope", "Seedless legal structural views; not a generated World schedule"},
                {"table", Json::array()}, {"signed_wet", Json::array()}, {"invalid_controls", Json::array()}};
    constexpr std::array<float, 4> cloud{0.f, .60f, .85f, 1.f}, haze{.05f, .12f, .18f, .35f};
    for (std::size_t index = 0; index < cloud.size(); ++index) {
        const auto view = structural_view(static_cast<WeatherKind>(index));
        const auto projection = project_engine_weather(view, 7200u, 7200.);
        require(projection.environment.cloud01 == cloud[index] && projection.environment.haze01 == haze[index] &&
                projection.environment.time_hour == 12.f && projection.wind01 == .3f &&
                projection.snow_cover01 == .65f && projection.precipitation01 == (index >= 2u ? .8f : 0.f),
                "Literal weather projection table differs");
        const auto disabled = project_engine_weather(view, 7200u, 7200., false);
        require(!disabled.environment.atmosphere_enabled, "Atmosphere flag was not preserved");
        result["table"].push_back(projection_row(projection));
    }
    for (const std::int64_t remainder : std::array<std::int64_t, 2>{-14400, 14400}) {
        auto view = structural_view(WeatherKind::Clear); view.wetness_remainder = remainder;
        const auto projected = project_engine_weather(view, 7200u, 7200.);
        const float expected = remainder < 0 ? .5995f : .6005f;
        require(std::abs(projected.wetness01 - expected) <= 1.e-7f, "Signed authoritative wetness normalization differs");
        result["signed_wet"].push_back({{"permille", 600}, {"remainder", remainder}, {"normalized", projected.wetness01}});
    }
    const auto reject = [&](std::string_view id, const WeatherView& view, std::uint64_t tick, double shown) {
        bool rejected{};
        try { (void)project_engine_weather(view, tick, shown); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Component invalid accepted: " + std::string(id));
        result["invalid_controls"].push_back({{"id", id}, {"rejected", true}});
    };
    auto value = structural_view(WeatherKind::Clear), unknown = value; unknown.known = false;
    reject("CPU-unknown", unknown, 7200u, 7200.);
    reject("CPU-tick-mismatch", value, 7201u, 7200.);
    reject("CPU-shown-negative", value, 7200u, -1.);
    reject("CPU-shown-nan", value, 7200u, std::numeric_limits<double>::quiet_NaN());
    reject("CPU-shown-infinity", value, 7200u, std::numeric_limits<double>::infinity());
    reject("CPU-shown-horizon", value, 7200u,
        static_cast<double>((static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 4u) * ticks_per_day));
    return result;
}

void marker_present(const Image& value) {
    constexpr std::array<std::uint8_t, 4> marker{31u, 189u, 78u, 255u};
    for (std::size_t y = 14u; y < 50u; ++y) for (std::size_t x = 14u; x < 90u; ++x)
        require(std::equal(marker.begin(), marker.end(), value.rgba.begin() +
            static_cast<std::ptrdiff_t>((y * extent.width + x) * 4u)), "Opaque native UI marker changed");
}

std::size_t changed_pixels(const Image& a, const Image& b) {
    require(a.rgba.size() == b.rgba.size(), "Different image dimensions");
    std::size_t count{};
    for (std::size_t pixel = 0; pixel < a.rgba.size(); pixel += 4u)
        if (!std::equal(a.rgba.begin() + static_cast<std::ptrdiff_t>(pixel),
                       a.rgba.begin() + static_cast<std::ptrdiff_t>(pixel + 4u),
                       b.rgba.begin() + static_cast<std::ptrdiff_t>(pixel))) ++count;
    return count;
}

// Final Composite clamps/exposes linear RGB then encodes sRGB into UNORM8.
// Disjoint inverse quantization intervals prove a visible linear response; this
// is not a raw HDR readback or an assumption about its exact scalar magnitude.
double inverse_srgb(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
bool disjoint_linear_bins(std::uint8_t a, std::uint8_t b) {
    const auto interval = [](std::uint8_t byte) {
        return std::array<double, 2>{inverse_srgb(std::max(0., (static_cast<double>(byte) - .5) / 255.)),
            byte == 255u ? std::numeric_limits<double>::infinity()
                         : inverse_srgb((static_cast<double>(byte) + .5) / 255.)};
    };
    const auto first = interval(a), second = interval(b);
    return first[1] < second[0] || second[1] < first[0];
}

struct QualifiedMasks {
    std::vector<std::uint8_t> classes;
    std::size_t ground{}, sprite{}, windows{};
    std::uint32_t texture_width{}, texture_height{};
};

QualifiedMasks qualify_pixels(const AssetCatalog& kit, const Frame2D& frame,
                              const std::vector<std::uint8_t>& carrier) {
    const auto descriptor_bytes = kit.read("art.world");
    const auto descriptor = Json::parse(descriptor_bytes.begin(), descriptor_bytes.end());
    std::uint32_t width{}, height{};
    for (const auto& texture : descriptor.at("textures"))
        if (texture.at("id") == "control.house.texture.albedo") {
            width = texture.at("width").get<std::uint32_t>(); height = texture.at("height").get<std::uint32_t>();
        }
    require(width >= 5u && height >= 5u && width <= 2048u && height <= 2048u, "Invalid house texture extent");
    bool material_found{};
    for (const auto& material : descriptor.at("materials")) {
        if (material.at("id") != "control.house.material") continue;
        material_found = true;
        for (const std::string_view channel : std::array<std::string_view,5>{
                 "albedo","normal_material","camera_depth","emission_mask","surface_mask"}) {
            const auto& binding = material.at("channels").at(std::string(channel));
            require(binding.at("texture") == "control.house.texture."+std::string(channel) &&
                    binding.at("rect") == Json::array({0,0,width,height}),
                    "House registry channel no longer binds a complete authored texture");
        }
    }
    require(material_found, "House typed material descriptor is absent");
    const auto albedo = kit.read("control.house.texture.albedo"), wet = kit.read("control.house.texture.surface_mask"),
               emission = kit.read("control.house.texture.emission_mask");
    const auto texels = static_cast<std::size_t>(width) * height;
    require(albedo.size() == texels * 4u && wet.size() == texels * 4u && emission.size() == texels,
            "House raw channel byte count differs");
    QualifiedMasks result{std::vector<std::uint8_t>(static_cast<std::size_t>(extent.width) * extent.height),
                          0u, 0u, 0u, width, height};
    std::vector<std::uint8_t> sprite_bounds(result.classes.size());
    // Triangle interpolation matches the affine sprite UV. Every qualifying
    // source sample has an additional texel margin around the bilinear footprint.
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw || !draw->sprite_material_index) continue;
        const auto& material = frame.sprite_material_draws.at(draw->sprite_material_index - 1u);
        const bool house = material.receiver_owner == 101u;
        // Renderer resolves the registry descriptor into GPU UV/emission fields
        // on upload. CPU instance placeholders must remain canonical zero here.
        if (house) for (const auto& region : material.surface.uv_regions)
            require(region == std::array<float, 4>{}, "House instance unexpectedly overrides registry UV fields");
        for (std::uint32_t offset = 0; offset < draw->index_count; offset += 3u) {
            std::array<Vertex, 3> vertices{};
            for (std::uint32_t corner = 0; corner < 3u; ++corner)
                vertices[corner] = frame.vertices.at(frame.indices.at(draw->first_index + offset + corner));
            const auto a = vertices[0].position, b = vertices[1].position, c = vertices[2].position;
            const float denominator = (b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
            require(std::abs(denominator) > 1.e-6f, "Degenerate sprite triangle");
            const int min_x = std::max(0, static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))));
            const int max_x = std::min(1919, static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))));
            const int min_y = std::max(0, static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))));
            const int max_y = std::min(1079, static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))));
            for (int y = min_y; y <= max_y; ++y) for (int x = min_x; x <= max_x; ++x) {
                const float px = static_cast<float>(x)+.5f, py = static_cast<float>(y)+.5f;
                const float u = ((b.y-c.y)*(px-c.x)+(c.x-b.x)*(py-c.y))/denominator;
                const float v = ((c.y-a.y)*(px-c.x)+(a.x-c.x)*(py-c.y))/denominator;
                if (u < 0.f || v < 0.f || u+v > 1.f) continue;
                const auto screen = static_cast<std::size_t>(y) * extent.width + static_cast<std::size_t>(x);
                sprite_bounds[screen] = 1u; result.classes[screen] = 0u;
                if (!house) continue;
                const float uv_x = u*vertices[0].uv.x+v*vertices[1].uv.x+(1.f-u-v)*vertices[2].uv.x;
                const float uv_y = u*vertices[0].uv.y+v*vertices[1].uv.y+(1.f-u-v)*vertices[2].uv.y;
                const int source_x = static_cast<int>(std::floor(uv_x*static_cast<float>(width)-.5f));
                const int source_y = static_cast<int>(std::floor(uv_y*static_cast<float>(height)-.5f));
                if (source_x < 1 || source_y < 1 || source_x+2 >= static_cast<int>(width) ||
                    source_y+2 >= static_cast<int>(height)) continue;
                bool exterior = true, window = true;
                for (int dy = -1; dy <= 2; ++dy) for (int dx = -1; dx <= 2; ++dx) {
                    const auto sample = static_cast<std::size_t>(source_y+dy)*width+static_cast<std::size_t>(source_x+dx);
                    exterior &= albedo[sample*4u+3u] == 255u && wet[sample*4u] >= 128u && emission[sample] == 0u;
                    window &= albedo[sample*4u+3u] == 255u && wet[sample*4u] == 0u && emission[sample] >= 250u;
                }
                if (exterior) result.classes[screen] = 2u;
                if (window) result.classes[screen] = 3u;
            }
        }
    }
    for (int y = 2; y < 1078; ++y) for (int x = 2; x < 1918; ++x) {
        const auto pixel = static_cast<std::size_t>(y)*extent.width+static_cast<std::size_t>(x);
        if (sprite_bounds[pixel]) continue;
        bool eligible = true;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const auto sample = static_cast<std::size_t>(y+dy)*extent.width+static_cast<std::size_t>(x+dx);
            const auto* tuple = carrier.data()+sample*4u;
            eligible &= !sprite_bounds[sample] && tuple[0] == 0u && (tuple[1] == 1u || tuple[1] == 3u);
        }
        if (eligible) result.classes[pixel] = 1u;
    }
    for (const auto value : result.classes) {
        result.ground += value == 1u; result.sprite += value == 2u; result.windows += value == 3u;
    }
    require(result.ground > 1000u && result.sprite >= 32u && result.windows >= 8u, "Wet UV/classification controls are vacuous");
    return result;
}

Json causal_witness(const QualifiedMasks& masks, const Image& dry, const Image& ground,
                    const Image& sprite, const Image& both) {
    std::size_t ground_changed{}, sprite_changed{}, bins{}, windows{}, nonsaturated_windows{};
    for (std::size_t pixel = 0; pixel < masks.classes.size(); ++pixel) {
        const auto offset = pixel * 4u;
        const auto equal = [&](const Image& a, const Image& b) {
            return std::equal(a.rgba.begin()+static_cast<std::ptrdiff_t>(offset),
                a.rgba.begin()+static_cast<std::ptrdiff_t>(offset+4u),
                b.rgba.begin()+static_cast<std::ptrdiff_t>(offset));
        };
        if (masks.classes[pixel] == 1u) ground_changed += !equal(dry, ground);
        if (masks.classes[pixel] == 2u) {
            sprite_changed += !equal(dry, sprite);
            for (std::size_t channel = 0; channel < 3u; ++channel)
                bins += disjoint_linear_bins(dry.rgba[offset+channel], sprite.rgba[offset+channel]);
        }
        if (masks.classes[pixel] == 3u) {
            require(equal(dry, ground) && equal(dry, sprite) && equal(dry, both), "Dry emissive window changed under wetness");
            ++windows;
            nonsaturated_windows += dry.rgba[offset] < 250u || dry.rgba[offset+1u] < 250u || dry.rgba[offset+2u] < 250u;
        }
    }
    require(ground_changed >= 100u && sprite_changed >= 16u && bins >= 32u && nonsaturated_windows >= 8u,
            "Native causal wetness/linear quantization witness is empty or saturated");
    return {{"wetness", .8f}, {"ground_qualified", masks.ground}, {"ground_changed", ground_changed},
        {"sprite_qualified", masks.sprite}, {"sprite_changed", sprite_changed}, {"disjoint_linear_bins", bins},
        {"dry_window_qualified", windows}, {"dry_window_nonsaturated", nonsaturated_windows}, {"dry_window_exact", true},
        {"texture_size", {masks.texture_width, masks.texture_height}}, {"uv_regions", {0,0,1,1}},
        {"source_footprint", "4x4 texels around bilinear footprint; alpha255, exterior red>=128/emission0, window red0/emission>=250"},
        {"oracle", "Final UNORM8 inverse-sRGB quantization intervals; not raw HDR"},
        {"ground_dry_roughness", 1.f}, {"ground_wet_roughness", .6f},
        {"ground_dry_specular", .05f}, {"ground_wet_specular", .09f}};
}

template<class Predicate> void poll_until(net::NetworkServer* host, ClientSession& client, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {
        if (host) host->poll();
        client.poll();
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Bounded loopback authority transition timed out");
}

std::vector<std::uint8_t> fixture_bytes(const fs::path& path) {
    const auto bytes = fs::file_size(path);
    require(bytes > 0u && bytes < budget, "Fixture preservation byte count is invalid");
    std::ifstream file(path,std::ios::binary);
    require(file.good(), "Fixture preservation read failed");
    std::vector<std::uint8_t> result{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(result.size() == bytes && !file.bad(), "Fixture preservation read was incomplete");
    return result;
}
} // namespace

int main(int argc, char** argv) {
    fs::path output; bool owned{};
    Json report{{"schema", 1}, {"complete", false}};
    const auto started = std::chrono::steady_clock::now();
    try {
        require(argc == 4, "Usage: GameViewWaterProbe runtime-assets kit fresh-output (weather)");
        const auto assets = fs::absolute(argument(argv[1])), kit_path = fs::absolute(argument(argv[2]));
        output = fs::absolute(argument(argv[3]));
        require(output.parent_path() == assets.parent_path().parent_path() && !fs::exists(output),
                "Weather output must be a fresh sibling build child");
        require(fs::space(output.parent_path()).available >= 384u*1024u*1024u, "Insufficient evidence headroom");
        require(fs::create_directory(output), "Could not create weather output"); owned = true;
        Json deterministic{{"controls", Json::array()}, {"invalid_controls", Json::array()},
            {"clock_controls", Json::array()}, {"captures", Json::array()}, {"world_saves", world_saves},
            {"capture_limit",capture_limit}, {"categories",{"house","resident.empty"}},
            {"defaults_explicit",EngineControlSettings{}.weather_source == EngineWeatherSource::ExplicitDiagnostic &&
                                  EngineControlSettings{}.time_source == EngineTimeSource::ExplicitDiagnostic},
            {"fixture", {{"seed", 70421}, {"ticks", {7200,7200,7210}}, {"wet_permille", {0,600,600}},
                {"wet_remainder", {0,14400,14400}}, {"actors", 0}, {"cover_history_replayed", false},
                {"geometry", "Authored revealed banks, hidden continuation and three completed Bridge records"}}}};
        deterministic["component"] = component_checks(); create_weather_worlds(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra authoritative weather controls", 1920, 1080, 0), SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()), "Native window failed");
        RendererOptions options; options.world_graph = WorldGraph::WaterAtmosphereSlice; options.collect_telemetry = true;
        Renderer2D renderer(window.get(), assets, options); renderer.set_logical_size(extent);
        require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12", "Hardware D3D12 required");
        GameView view(renderer), oracle(renderer);
        AssetCatalog kit(kit_path);
        for (auto* candidate : std::array<GameView*, 2>{&view, &oracle}) {
            candidate->set_logical_size(extent); candidate->load_engine_control_art(kit); candidate->load_engine_water_art();
        }
        EngineControlSettings settings; settings.camera_x = 28.f; settings.camera_y = 25.f; settings.cell_radius = 8;
        settings.water_enabled = true; settings.environment = EngineEnvironmentSettingsV1{1u,12.f,.25f,.25f,true};
        settings.time_source = EngineTimeSource::SessionShown; settings.weather_source = EngineWeatherSource::SessionAuthority;
        settings.lighting.counts[0] = 1u; settings.lighting.lights[0].color_intensity[3] = 0.f;
        settings.lighting.lights[0].occluder_range_flags[3] = 9001u;
        ClientSession session(70421);
        const auto compose = [&](GameView& target, const ClientSession& current, double seconds, const EngineControlSettings& configured) {
            Frame2D frame;
            const auto stats = target.compose_engine_control(current, seconds, configured, controls, frame);
            require(stats.instances_drawn == 2u && stats.bridge_decks == 3u && stats.water_cells > 0u &&
                stats.local_lights == 3u && frame.surface_lighting.counts[0] == 3u && frame.contact_shadows.empty(),
                "Weather scene/source controls are vacuous");
            require(std::memcmp(&frame.surface_lighting.lights[0], &settings.lighting.lights[0], sizeof(LightGpu)) == 0,
                    "Incoming local source bytes changed");
            return std::pair{std::move(frame), stats};
        };
        const auto positive = [&](std::string_view id) { deterministic["controls"].push_back({{"id", id}, {"passed", true}}); };
        positive("CPU-projection-table"); positive("CPU-signed-wet");
        std::vector<std::uint8_t> carrier;
        const auto checked_capture = [&](const Frame2D& frame, std::string_view name) {
            require(deterministic["captures"].size() < capture_limit, "PNG plan exceeded");
            const auto image = capture(renderer, frame, output, name, deterministic["captures"]); marker_present(image);
            const auto actual = renderer.read_water_coverage_v1();
            if (carrier.empty()) {
                carrier = actual.rgba8; save_bytes(output/"weather-carrier.rgba8", carrier);
                auto counts = carrier_counts(actual); counts.erase("frame_slot"); counts.erase("target_generation");
                deterministic["carrier"] = std::move(counts);
            } else require(actual.rgba8 == carrier, "Weather changed exact raw receiver carrier");
            return image;
        };
        Frame2D dry_frame, wet_frame;
        Image dry_image;
        for (std::size_t index = 0; index < 2u; ++index) {
            load_current(session, view, output/world_saves[index]);
            auto [authority, stats] = compose(view, session, 10., settings);
            shown_matches(authority, stats, session, 7200.);
            auto explicit_settings = settings; explicit_settings.weather_source = EngineWeatherSource::ExplicitDiagnostic;
            explicit_settings.environment = stats.authoritative_weather->environment;
            explicit_settings.water_wind = stats.authoritative_weather->wind01;
            explicit_settings.ground_wetness01 = stats.authoritative_weather->wetness01;
            auto [diagnostic, diagnostic_stats] = compose(view, session, 10., explicit_settings);
            require(!diagnostic_stats.authoritative_weather && frame_state(authority) == frame_state(diagnostic),
                    "Authority differs from exact explicit projected frame");
            const auto label = index == 0u ? "dry" : "wet";
            const auto authority_image = checked_capture(authority, "authority-"+std::string(label));
            require(checked_capture(diagnostic, "explicit-"+std::string(label)).rgba == authority_image.rgba,
                    "Authority/explicit-equivalent native pixels differ");
            deterministic["authority"].push_back({{"id", label}, {"projection", projection_row(*stats.authoritative_weather)},
                {"source_weather",weather_row(session.weather())},
                {"frame_equal", true}, {"pixels_equal", true}, {"payload", time_payload(authority)}});
            if (index == 0u) { dry_frame = authority; dry_image = authority_image; }
            else wet_frame = authority;
            positive(index == 0u ? "AUTH-dry-equivalent" : "AUTH-wet-equivalent");
        }
        source_invariant(dry_frame, wet_frame);
        auto stripped_dry = dry_frame, stripped_wet = wet_frame;
        set_material_wetness(stripped_dry, 0.f, 0.f); set_material_wetness(stripped_wet, 0.f, 0.f);
        require(frame_state(stripped_dry) == frame_state(stripped_wet), "Wet-only fixture changed other semantic fields");
        deterministic["source_invariance"] = {{"local_bytes_exact", true}, {"emission_metadata_exact", true},
            {"wet_only_other_fields_exact", true}, {"incoming_local_rows", 1}, {"authored_local_rows", 2}};
        positive("SOURCE-local-emission-invariant");

        auto causal_settings = settings;
        causal_settings.weather_source = EngineWeatherSource::ExplicitDiagnostic;
        causal_settings.time_source = EngineTimeSource::ExplicitDiagnostic; causal_settings.environment.reset();
        causal_settings.atmosphere = EngineAtmosphereSettings{false, 12.f, 0.f};
        causal_settings.lighting.ambient = {.12f,.12f,.12f,0.f};
        const double slope = .65, normal_y = slope/std::sqrt(1.+slope*slope), normal_z = 1./std::sqrt(1.+slope*slope);
        const double eye = 1./std::sqrt(3.), dot = eye*(normal_y+normal_z);
        causal_settings.lighting.sun_direction = {static_cast<float>(-eye), static_cast<float>(2.*dot*normal_y-eye),
            static_cast<float>(2.*dot*normal_z-eye), 0.f};
        causal_settings.lighting.sun_color_intensity = {1.f,1.f,1.f,1.f}; causal_settings.water_wind = .3f;
        auto [causal_dry, causal_stats] = compose(view, session, 10., causal_settings);
        require(!causal_stats.authoritative_weather, "Diagnostic causal frame fabricated authority stats");
        const auto causal_screen = view.tile_screen(26.f,24.f);
        const auto causal_pick = view.cell_at_screen(causal_screen.x,causal_screen.y);
        auto causal_wet_settings = causal_settings; causal_wet_settings.ground_wetness01 = .8f;
        auto [causal_both, causal_wet_stats] = compose(view, session, 10., causal_wet_settings);
        require(!causal_wet_stats.authoritative_weather, "Explicit wetness fabricated authority stats");
        const auto wet_screen = view.tile_screen(26.f,24.f);
        require(causal_screen.x == wet_screen.x && causal_screen.y == wet_screen.y &&
                view.cell_at_screen(causal_screen.x,causal_screen.y) == causal_pick,
                "Diagnostic wetness changed camera or actual surface pick");
        auto causal_ground = causal_both, causal_sprite = causal_both;
        set_material_wetness(causal_ground, .8f, 0.f); set_material_wetness(causal_sprite, 0.f, .8f);
        // Only existing typed material scalar fields vary; unchanged command,
        // geometry, phase, lighting and sources are proved by normalization.
        for (const auto* candidate : std::array<const Frame2D*,3>{&causal_ground,&causal_sprite,&causal_both}) {
            auto normalized = *candidate; set_material_wetness(normalized, 0.f, 0.f);
            require(frame_state(normalized) == frame_state(causal_dry), "Causal control changed non-wet semantic fields");
            source_invariant(causal_dry, *candidate);
        }
        const auto all_off = checked_capture(causal_dry, "wet-all-off");
        const auto both_on = checked_capture(causal_both, "wet-both-on");
        const auto sprite_only = checked_capture(causal_sprite, "wet-ground-off");
        const auto ground_only = checked_capture(causal_ground, "wet-sprite-off");
        const auto qualified = qualify_pixels(kit, causal_dry, carrier);
        save_bytes(output/"weather-wet-qualified.mask8", qualified.classes);
        deterministic["causal"] = causal_witness(qualified, all_off, ground_only, sprite_only, both_on);
        deterministic["causal"]["sun_direction"] = causal_settings.lighting.sun_direction;
        deterministic["causal"]["geometry_camera_picks_exact"] = true;
        deterministic["causal"]["water_wetness_one"] = true;
        positive("GPU-ground-wet"); positive("GPU-sprite-wet"); positive("GPU-window-dry");

        struct InvalidCase { std::string id; EngineControlSettings settings;
            std::array<EngineControlInstance,2> instances; double seconds{10.25}; };
        std::vector<InvalidCase> invalid;
        const auto add_bad = [&](std::string id, const auto& edit) {
            InvalidCase bad{std::move(id),settings,controls}; edit(bad); invalid.push_back(std::move(bad));
        };
        add_bad("SOURCE-invalid-enum", [](auto& b) { b.settings.weather_source = static_cast<EngineWeatherSource>(99); });
        add_bad("SOURCE-clock-conflict", [](auto& b) { b.settings.time_source = EngineTimeSource::ExplicitDiagnostic; });
        add_bad("SOURCE-manual-atmosphere", [](auto& b) { b.settings.atmosphere = EngineAtmosphereSettings{}; });
        add_bad("SOURCE-environment-version", [](auto& b) { b.settings.environment->version = 2u; });
        add_bad("SOURCE-cloud-nan", [](auto& b) { b.settings.environment->cloud01 = std::numeric_limits<float>::quiet_NaN(); });
        add_bad("SOURCE-cloud-below", [](auto& b) { b.settings.environment->cloud01 = -.1f; });
        add_bad("SOURCE-haze-above", [](auto& b) { b.settings.environment->haze01 = 1.1f; });
        add_bad("SOURCE-ground-nan", [](auto& b) { b.settings.ground_wetness01 = std::numeric_limits<float>::quiet_NaN(); });
        add_bad("SOURCE-ground-below", [](auto& b) { b.settings.ground_wetness01 = -.1f; });
        add_bad("SOURCE-ground-above", [](auto& b) { b.settings.ground_wetness01 = 1.1f; });
        add_bad("SOURCE-wind-nan", [](auto& b) { b.settings.water_wind = std::numeric_limits<float>::quiet_NaN(); });
        add_bad("SOURCE-wind-below", [](auto& b) { b.settings.water_wind = -.1f; });
        add_bad("SOURCE-wind-above", [](auto& b) { b.settings.water_wind = 1.1f; });
        add_bad("SOURCE-missing-env", [](auto& b) { b.settings.environment.reset(); });
        add_bad("SOURCE-water-disabled", [](auto& b) { b.settings.water_enabled = false; });
        add_bad("SOURCE-time-enum", [](auto& b) { b.settings.time_source = static_cast<EngineTimeSource>(99); });
        add_bad("SOURCE-wall-nan", [](auto& b) { b.seconds = std::numeric_limits<double>::quiet_NaN(); });
        add_bad("SOURCE-unknown-sprite", [](auto& b) { b.instances[1].sprite_id = "missing.weather.sprite"; });
        add_bad("SOURCE-zero-owner", [](auto& b) { b.instances[1].owner_id = 0u; });
        Frame2D retained;
        const auto reject_transaction = [&](const InvalidCase& bad, const ClientSession* source) {
            load_current(session, view, output/world_saves[0]); oracle.reset_world_state();
            view.set_speed(1); oracle.set_speed(1); view.set_paused(false); oracle.set_paused(false);
            retained = compose(view, session, 10., settings).first;
            (void)compose(oracle, session, 10., settings);
            const auto before = frame_state(retained);
            const auto screen = view.tile_screen(26.f,24.f);
            const auto pick = view.cell_at_screen(screen.x,screen.y);
            require(pick == CellCoord{26,24,WorldLayer::Surface}, "Transactional pick control is empty");
            for (int tick = 0; tick < 4; ++tick) session.advance();
            bool rejected{};
            try { (void)view.compose_engine_control(source ? *source : session, bad.seconds, bad.settings, bad.instances, retained); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid authority candidate accepted: "+bad.id);
            const auto after_screen = view.tile_screen(26.f,24.f);
            require(frame_state(retained) == before && screen.x == after_screen.x && screen.y == after_screen.y &&
                    view.cell_at_screen(screen.x,screen.y) == pick, "Failed candidate published frame/camera/picks: "+bad.id);
            const auto [clean, clean_stats] = compose(oracle, session, 10.1, settings);
            const auto [recovered, recovered_stats] = compose(view, session, 10.1, settings);
            require(time_payload(clean) == time_payload(recovered) &&
                    projection_row(*clean_stats.authoritative_weather) == projection_row(*recovered_stats.authoritative_weather),
                    "Failed candidate advanced presentation clock: "+bad.id);
            shown_matches(recovered, recovered_stats, session, 7202.2);
            require(recovered_stats.authoritative_weather->shown_calendar_tick < static_cast<double>(session.ticks()),
                    "Transaction clock oracle accidentally snapped to authority");
            deterministic["invalid_controls"].push_back({{"id",bad.id}, {"rejected",true}, {"unchanged",true},
                {"frame_camera_pick_clock_exact",true}, {"authority_tick",session.ticks()},
                {"recovery_shown_tick",recovered_stats.authoritative_weather->shown_calendar_tick}});
        };
        for (const auto& bad : invalid) reject_transaction(bad,nullptr);
        require(checked_capture(retained,"after-invalid").rgba == dry_image.rgba, "Retained invalid frame changed native pixels");

        for (const int speed : std::array<int,3>{1,2,4}) {
            load_current(session, view, output/world_saves[1]); view.set_speed(speed); view.set_paused(false);
            (void)compose(view,session,20.,settings); LocalDriver driver;
            double previous = 7200.;
            std::pair<Frame2D,EngineControlStats> endpoint;
            for (int frame = 1; frame <= 10; ++frame) {
                driver.update(session,view,.1); endpoint = compose(view,session,20.+frame*.1,settings);
                const double shown = endpoint.second.authoritative_weather->shown_calendar_tick;
                require(shown >= previous && shown <= static_cast<double>(session.ticks()), "Shown authority extrapolated");
                shown_matches(endpoint.first, endpoint.second, session, shown); previous = shown;
            }
            require(driver.steps == static_cast<std::uint64_t>(20*speed) && session.ticks() == 7200u+driver.steps,
                    "Actual selected-speed driver count differs");
            shown_matches(endpoint.first,endpoint.second,session,7200.+19.*speed);
            auto [settled, settled_stats] = compose(view,session,21.125,settings);
            shown_matches(settled,settled_stats,session,static_cast<double>(session.ticks()));
            view.set_paused(true); driver.accumulator = .025; driver.update(session,view,.25);
            auto [held, held_stats] = compose(view,session,22.125,settings);
            require(frame_state(held) == frame_state(settled) && projection_row(*held_stats.authoritative_weather) ==
                    projection_row(*settled_stats.authoritative_weather) && driver.accumulator == 0., "Pause changed clock/weather");
            view.set_paused(false); driver.update(session,view,.1); (void)compose(view,session,22.225,settings);
            auto [resumed, resumed_stats] = compose(view,session,22.35,settings);
            require(session.ticks() == 7200u+static_cast<std::uint64_t>(22*speed), "Resume replayed paused time");
            shown_matches(resumed,resumed_stats,session,static_cast<double>(session.ticks()));
            deterministic["clock_controls"].push_back({{"id","speed-"+std::to_string(speed)}, {"speed",speed},
                {"driver_frames",10}, {"steps",20*speed}, {"authority_tick",7200+20*speed},
                {"shown_tick",7200+19*speed}, {"resume_tick",session.ticks()}, {"pause_exact",true}, {"passed",true}});
        }
        positive("TIME-local-1-2-4");
        view.set_paused(true);
        auto [paused, paused_stats] = compose(view,session,23.,settings);
        const auto paused_image = checked_capture(paused,"paused-base");
        LocalDriver paused_driver; paused_driver.accumulator = .025;
        for (int frame = 1; frame <= 8; ++frame) {
            paused_driver.update(session,view,.25); const auto [held, held_stats] = compose(view,session,23.+frame*.25,settings);
            require(frame_state(held) == frame_state(paused) && projection_row(*held_stats.authoritative_weather) ==
                projection_row(*paused_stats.authoritative_weather), "Paused candidate changed");
        }
        require(paused_driver.steps == 0u && paused_driver.accumulator == 0., "Pause advanced local authority");
        require(checked_capture(compose(view,session,25.,settings).first,"paused-repeat").rgba == paused_image.rgba,
                "Paused native pixels changed");
        const auto before_resume = session.ticks(); view.set_paused(false); view.set_speed(1);
        LocalDriver resume; resume.update(session,view,.1); (void)compose(view,session,25.1,settings);
        auto [resumed, resumed_stats] = compose(view,session,25.225,settings);
        require(session.ticks() == before_resume+2u, "Pause resume replayed wall time");
        shown_matches(resumed,resumed_stats,session,static_cast<double>(session.ticks()));
        const auto resume_delta = changed_pixels(paused_image,checked_capture(resumed,"resumed"));
        require(resume_delta > 0u, "Actual resume changed no native pixels");
        load_current(session,view,output/world_saves[1]); (void)compose(view,session,30.,settings);
        for (int tick = 0; tick < 10; ++tick) session.advance();
        auto [lagged, lagged_stats] = compose(view,session,30.1,settings); shown_matches(lagged,lagged_stats,session,7202.2);
        const auto epoch = session.control_epoch(); load_current(session,view,output/world_saves[2]);
        require(session.ticks() == 7210u && session.control_epoch() == epoch, "Load reset fixture must repeat tick/epoch");
        auto [reset, reset_stats] = compose(view,session,30.11,settings); shown_matches(reset,reset_stats,session,7210.);
        (void)checked_capture(reset,"load-reset");
        deterministic["clock_controls"].push_back({{"id","pause-load-reset"}, {"passed",true}, {"paused_compositions",8},
            {"paused_steps",0}, {"paused_pixels_exact",true}, {"resumed_tick",before_resume+2u},
            {"resumed_changed_pixels",resume_delta}, {"repeated_epoch",true}, {"lagged_shown_tick",7202.2},
            {"reset_shown_tick",7210}, {"reset_source_tick",7210}});
        positive("TIME-pause-load-reset");

        std::array<std::vector<std::uint8_t>,3> original_saves;
        for (std::size_t index = 0; index < world_saves.size(); ++index)
            original_saves[index] = fixture_bytes(output/world_saves[index]);
        net::HostConfig config; config.game.seed = 70421u; config.game.lobby_enabled = false;
        config.save_path = output/world_saves[1]; config.autosave_interval = std::chrono::hours(24);
        std::unique_ptr<net::NetworkServer> host;
        for (std::uint16_t port = 30060u; port < 30080u && !host; ++port) {
            config.transport.port = port;
            auto attempt = std::make_unique<net::NetworkServer>(config);
            if (attempt->listen()) host = std::move(attempt);
        }
        require(host != nullptr, "No bounded loopback listening port was available");
        ClientSession remote(70421u,"Weather remote"); std::string error;
        require(remote.connect_remote("127.0.0.1:"+std::to_string(host->port()),"",error), "Loopback connect: "+error);
        require(!remote.ready() && !remote.weather().known, "Pending loopback must mask unknown weather");
        reject_transaction(InvalidCase{"SOURCE-unready",settings,controls},&remote);
        poll_until(host.get(),remote,[&] { return remote.ready() && remote.world_ready() && remote.weather().known; });
        require(remote.remote() && remote.weather() == host->game().world().weather() && remote.ticks() == 7200u,
                "Client did not actually receive matching host weather");
        view.reset_world_state();
        auto [remote_frame, remote_stats] = compose(view,remote,40.,settings);
        shown_matches(remote_frame,remote_stats,remote,7200.);
        auto remote_explicit = settings; remote_explicit.weather_source = EngineWeatherSource::ExplicitDiagnostic;
        remote_explicit.environment = remote_stats.authoritative_weather->environment;
        remote_explicit.water_wind = remote_stats.authoritative_weather->wind01;
        remote_explicit.ground_wetness01 = remote_stats.authoritative_weather->wetness01;
        require(frame_state(compose(view,remote,40.,remote_explicit).first) == frame_state(remote_frame),
                "Received authority frame differs from explicit projection");
        (void)checked_capture(remote_frame,"remote-authority");
        deterministic["remote"] = {{"received_weather_equal_host",true}, {"ready",true}, {"world_ready",true},
            {"source_tick",remote.ticks()}, {"projection",projection_row(*remote_stats.authoritative_weather)},
            {"source_weather",weather_row(remote.weather())},
            {"explicit_frame_equal",true}, {"transport","Actual loopback GNS full snapshot; no client injection"}};
        // NetworkServer::shutdown also saves. Actual host lifetime loss closes
        // the RAII NetTransport, exercising disconnect without resaving inputs.
        host.reset();
        poll_until(nullptr,remote,[&] { return !remote.ready() && remote.reconnecting() && !remote.weather().known; });
        reject_transaction(InvalidCase{"SOURCE-reconnecting",settings,controls},&remote);
        for (std::size_t index = 0; index < world_saves.size(); ++index) {
            require(fixture_bytes(output/world_saves[index]) == original_saves[index], "Remote phase changed original fixture bytes");
            auto backup = output/world_saves[index]; backup += ".bak";
            require(!fs::exists(backup), "Remote phase created an undeclared fixture backup");
        }
        deterministic["remote"]["disconnect_masks_weather"] = true;
        deterministic["remote"]["reconnecting_rejected"] = true;
        deterministic["remote"]["disconnect_scope"] = "Actual host destruction closes GNS transport; explicit NetworkServer shutdown/save not invoked";
        deterministic["remote"]["source_save_bytes_unchanged"] = true;
        deterministic["remote"]["source_save_backups_absent"] = true;
        positive("REMOTE-loopback-authority"); positive("CARRIER-geometry-picks-invariant");
        require(deterministic["controls"].size() == 12u && deterministic["invalid_controls"].size() == 21u &&
                deterministic["captures"].size() == 14u, "Declared control/capture counts changed");
        deterministic["raw_files"] = {"weather-carrier.rgba8","weather-wet-qualified.mask8"};
        deterministic["raw_carrier_exact"] = true; deterministic["opaque_ui_exact"] = true;
        deterministic["open"] = {"normal startup", "coherent production art", "precipitation FX", "snow materials and FX",
            "footprints", "full suite", "physical two-PC", "reference visuals", "video", "FPS",
            "GameView incomplete-recovery transition; getter mask verified separately",
            "contact/shadow validation; existing component evidence only",
            "fine-grained remote interpolation; existing component evidence only"};
        report["deterministic"] = std::move(deterministic);
        report["backend"] = renderer.device_info().backend; report["gpu"] = renderer.device_info().device;
        report["driver"] = renderer.device_info().driver; report["resolution"] = {1920,1080};
        report["graph"] = "WaterAtmosphereSlice"; report["fps"] = nullptr; report["gpu_execution_ms"] = nullptr;
        report["measured"] = {{"total_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
        check_budget(output);
        // Reserve the exact serialized report bytes before publishing complete.
        report["complete"] = true;
        const auto report_bytes = report.dump(2).size()+1u;
        std::uintmax_t existing_bytes{};
        for (const auto& file : fs::recursive_directory_iterator(output))
            if (file.is_regular_file()) existing_bytes += file.file_size();
        require(existing_bytes+report_bytes <= budget, "Final weather report exceeds output budget");
        save_report(output/"weather-report.json",report);
        std::cout << "gameview weather checks passed\n"; return 0;
    } catch (const std::exception& error) {
        report["complete"] = false; report["error"] = error.what();
        if (owned && !fs::exists(output/"weather-report.json")) {
            try { save_report(output/"weather-report.json",report); } catch (const std::exception&) {}
        }
        std::cerr << "gameview weather checks failed: " << error.what() << '\n'; return 1;
    }
}

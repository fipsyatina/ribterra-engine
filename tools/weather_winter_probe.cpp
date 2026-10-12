#include "ribterra/world_state.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace ribterra;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr std::uintmax_t output_limit = 128u * 1024u * 1024u;
constexpr std::uintmax_t free_space_required = 384u * 1024u * 1024u;
constexpr std::uint64_t trace_ticks = 24000;
constexpr std::uint64_t ration_interval = 200;
constexpr std::uint64_t digest_basis = 1469598103934665603ULL;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void passed(Json& controls, const char* id) {
    controls.push_back({{"id", id}, {"passed", true}});
}
ClimateConfig winter_config() {
    ClimateConfig config;
    config.scenario = ClimateScenarioId::FirstWinterStorm;
    return config;
}
Json sample_json(const WeatherSample& sample) {
    return {{"kind", static_cast<unsigned>(sample.kind)},
        {"phase", static_cast<unsigned>(sample.precipitation)},
        {"temperature_deci_c", sample.temperature_deci_c},
        {"precipitation_permille", sample.precipitation_permille},
        {"wind_direction", static_cast<unsigned>(sample.wind_direction)},
        {"wind_permille", sample.wind_permille},
        {"outdoor_work_rate_bp", sample.outdoor_work_rate_bp},
        {"work_reason", static_cast<unsigned>(sample.work_reason)},
        {"period_start", sample.period_start_calendar_tick},
        {"period_end", sample.period_end_calendar_tick}};
}
Json weather_json(const WeatherView& view) {
    Json cards = Json::array();
    for (const auto& card : view.forecast) {
        Json intervals = Json::array();
        for (std::size_t i = 0; i < card.interval_count; ++i) {
            const auto& interval = card.intervals[i];
            intervals.push_back({interval.start_tick_in_day, interval.end_tick_in_day,
                static_cast<unsigned>(interval.kind)});
        }
        cards.push_back({{"day", card.calendar_day}, {"minimum_deci_c", card.minimum_temperature_deci_c},
            {"maximum_deci_c", card.maximum_temperature_deci_c},
            {"dominant_kind", static_cast<unsigned>(card.dominant_kind)},
            {"precipitation_summary", static_cast<unsigned>(card.precipitation_summary)},
            {"has_storm", card.has_storm}, {"intervals", intervals}});
    }
    return {{"known", view.known}, {"algorithm", view.algorithm_version},
        {"profile", static_cast<unsigned>(view.climate_profile)},
        {"scenario", static_cast<unsigned>(view.scenario)},
        {"offset", view.calendar_offset_ticks}, {"simulation_tick", view.evaluated_simulation_tick},
        {"calendar_tick", view.calendar_tick}, {"current", sample_json(view.current)},
        {"snow_cover_permille", view.snow_cover_permille}, {"wetness_permille", view.wetness_permille},
        {"wetness_remainder", view.wetness_remainder},
        {"next_storm_start", view.next_storm_start_calendar_tick},
        {"next_storm_end", view.next_storm_end_calendar_tick}, {"forecast", cards}};
}
Json climate_json(const ClimateRuntimeState& climate) {
    Json work = Json::array();
    for (const auto& row : climate.outdoor_work_remainders) {
        work.push_back({{"resident", row.resident_id}, {"job", static_cast<int>(row.job)},
            {"target", {row.target.x, row.target.y, static_cast<unsigned>(row.target.layer)}},
            {"building", row.building_id}, {"remainder", row.remainder_bp}});
    }
    return {{"algorithm", climate.algorithm_version}, {"profile", static_cast<unsigned>(climate.climate_profile)},
        {"scenario", static_cast<unsigned>(climate.scenario)}, {"offset", climate.calendar_offset_ticks},
        {"simulation_tick", climate.evaluated_simulation_tick}, {"current", sample_json(climate.current)},
        {"snow_cover_permille", climate.snow_cover_permille}, {"snow_cover_remainder", climate.snow_cover_remainder},
        {"wetness_permille", climate.wetness_permille}, {"wetness_remainder", climate.wetness_remainder},
        {"work_rows", work}};
}
// Portable little-endian word folding, without std::hash or object padding.
void digest_word(std::uint64_t& digest, std::uint64_t word) {
    for (unsigned i = 0; i < 8; ++i) {
        digest ^= (word >> (i * 8u)) & 255u;
        digest *= 1099511628211ULL;
    }
}
void digest_sample(std::uint64_t& digest, std::uint64_t tick, const WeatherSample& sample) {
    for (const auto word : std::array<std::uint64_t, 11>{tick,
        static_cast<std::uint64_t>(sample.kind), static_cast<std::uint64_t>(sample.precipitation),
        static_cast<std::uint64_t>(static_cast<std::int64_t>(sample.temperature_deci_c)),
        sample.precipitation_permille, static_cast<std::uint64_t>(sample.wind_direction), sample.wind_permille,
        sample.outdoor_work_rate_bp, static_cast<std::uint64_t>(sample.work_reason),
        sample.period_start_calendar_tick, sample.period_end_calendar_tick}) digest_word(digest, word);
}
struct StormResult {
    std::uint32_t seed{};
    std::uint64_t offset{}, start{}, end{}, count{}, snow_count{}, rain_count{}, digest{digest_basis};
    std::int16_t minimum{600}, maximum{-800};
    std::optional<std::uint64_t> rejection_tick;
    WeatherSample rejection_sample;
    Json boundaries;
};
StormResult evaluate_seed(std::uint32_t seed) {
    const WeatherContext context{seed, winter_config()};
    const auto offset = scenario_calendar_offset(context.config.scenario);
    WeatherScheduleCache cache(context, CalendarTick{offset});
    const auto initial = initialize_climate(context, cache);
    const auto view = cache.view(initial);
    const auto start = view.next_storm_start_calendar_tick, end = view.next_storm_end_calendar_tick;
    check(start >= offset + 9600u && start <= offset + 14400u && end > start &&
        end - start >= 4800u && end - start <= 7200u, "Forced first-storm bounds differ");
    StormResult result; result.seed = seed; result.offset = offset; result.start = start; result.end = end;
    for (auto tick = start; tick < end; ++tick) {
        const auto sample = cache.sample(CalendarTick{tick});
        check(sample.kind == WeatherKind::Storm && sample.period_start_calendar_tick == start &&
            sample.period_end_calendar_tick == end, "Forced storm period changed during exhaustive scan");
        ++result.count;
        result.minimum = std::min(result.minimum, sample.temperature_deci_c);
        result.maximum = std::max(result.maximum, sample.temperature_deci_c);
        if (sample.precipitation == PrecipitationKind::Snow) ++result.snow_count;
        if (sample.precipitation == PrecipitationKind::Rain) ++result.rain_count;
        if (!result.rejection_tick &&
            (sample.temperature_deci_c > 0 || sample.precipitation != PrecipitationKind::Snow)) {
            result.rejection_tick = tick; result.rejection_sample = sample;
        }
        digest_sample(result.digest, tick, sample);
    }
    check(result.count == end - start, "Exhaustive storm sample count differs");
    result.boundaries = Json::array();
    for (const auto tick : {start - 1u, start, end - 1u, end})
        result.boundaries.push_back({{"calendar_tick", tick}, {"simulation_tick", tick - offset},
            {"sample", sample_json(cache.sample(CalendarTick{tick}))}});
    check(cache.sample(CalendarTick{start - 1u}).kind != WeatherKind::Storm &&
        cache.sample(CalendarTick{end}).kind != WeatherKind::Storm, "First-storm boundary is not exclusive");
    return result;
}
Json storm_json(const StormResult& storm) {
    return {{"seed", storm.seed}, {"algorithm", weather_algorithm_version}, {"profile", 1}, {"scenario", 1},
        {"offset", storm.offset}, {"start_calendar_tick", storm.start}, {"end_calendar_tick", storm.end},
        {"start_simulation_tick", storm.start - storm.offset}, {"end_simulation_tick", storm.end - storm.offset},
        {"duration", storm.end - storm.start}, {"storm_tick_count", storm.count},
        {"snow_tick_count", storm.snow_count}, {"rain_tick_count", storm.rain_count},
        {"minimum_temperature_deci_c", storm.minimum}, {"maximum_temperature_deci_c", storm.maximum},
        {"sample_digest", storm.digest}, {"boundaries", storm.boundaries}};
}
StormResult search_seed(Json& deterministic, Json& controls) {
    const auto negative = evaluate_seed(70421u);
    check(negative.rejection_tick && negative.rain_count > 0u &&
        negative.rejection_sample.temperature_deci_c > 0 &&
        negative.rejection_sample.precipitation == PrecipitationKind::Rain,
        "Known seed70421 unexpectedly passed all-Snow acceptance");
    deterministic["invalid_controls"] = Json::array({{{"id", "SEED-70421-rain"}, {"rejected", true},
        {"first_rejection_tick", *negative.rejection_tick},
        {"sample", sample_json(negative.rejection_sample)}, {"full_storm", storm_json(negative)}}});
    auto& search = deterministic["search"];
    search = {{"first_seed", 0}, {"last_seed", 4095}, {"tried_count", 0},
        {"rejected_candidates", Json::array()}, {"bound_scope", "diagnostic search cap, not a balance rule"}};
    for (std::uint32_t seed = 0; seed <= 4095u; ++seed) {
        const auto candidate = evaluate_seed(seed);
        search["tried_count"] = seed + 1u;
        if (candidate.rejection_tick) {
            search["rejected_candidates"].push_back({{"seed", seed},
                {"first_rejection_tick", *candidate.rejection_tick},
                {"sample", sample_json(candidate.rejection_sample)},
                {"maximum_temperature_deci_c", candidate.maximum}});
            continue;
        }
        check(candidate.maximum <= 0 && candidate.snow_count == candidate.count && candidate.rain_count == 0,
            "Selected seed is not exhaustively all-Snow");
        search["selected"] = storm_json(candidate);
        search["certified_first_storm_snow"] = true;
        search["shipping_startup_selected"] = false;
        passed(controls, "SEED-all-snow");
        return candidate;
    }
    throw std::runtime_error("No all-Snow FirstWinterStorm seed within diagnostic seeds0..4095");
}
void import(World& world, WorldState state) {
    std::string error;
    const bool accepted = world.import_state(std::move(state), error);
    check(accepted, "Actual checkpoint import: " + error);
}
Json population_json(const World& world) {
    Json population = Json::array();
    for (const auto& resident : world.residents())
        population.push_back({{"id", resident.id}, {"health", resident.health}, {"hunger", resident.hunger},
            {"job", static_cast<int>(resident.job)},
            {"automation_enabled", resident.profession.assignment.automation_enabled}});
    return population;
}
void check_population(const World& world) {
    check(world.residents().size() == 10u, "Actual winter population changed");
    for (const auto& resident : world.residents()) {
        check(resident.health == 100 && resident.hunger == 0.f,
            "Actual ration horizon lost health or accumulated hunger");
        check(resident.job == Job::Idle && !resident.profession.assignment.automation_enabled,
            "Actual stopped actor resumed production");
    }
}
Json snapshot_json(const World& world) {
    const auto state = world.export_state();
    return {{"simulation_tick", world.ticks()}, {"checksum", world.checksum()},
        {"stored_food", world.inventory().stored.food}, {"population", population_json(world)},
        {"climate", climate_json(state.climate)}, {"weather", weather_json(world.weather())}};
}
Json save_boundary(const fs::path& output, const char* name, World& world,
                   const StormResult& selected, Json& controls) {
    const auto path = output / name;
    for (const auto suffix : {"", ".tmp", ".bak", ".bak.tmp"})
        check(!fs::exists(fs::path(path.string() + suffix)), "Boundary save output must be new");
    const auto source = world.export_state();
    check(source.ticks == selected.start - selected.offset - 1u ||
        source.ticks == selected.start - selected.offset ||
        source.ticks == selected.end - selected.offset - 1u, "Save was not reached at a real required boundary");
    const auto checksum = world.checksum();
    const auto weather = world.weather();
    std::string error;
    const bool saved = world.save(path, error); check(saved, "Actual winter save: " + error);
    World loaded(1, 0);
    const bool restored = loaded.load(path, error); check(restored, "Actual winter load: " + error);
    check(loaded.checksum() == checksum && loaded.weather() == weather &&
        loaded.export_state().climate == source.climate, "Boundary load lost private climate/current/forecast");
    World direct(1, 0); import(direct, source);
    WeatherScheduleCache cache({selected.seed, winter_config()}, world.calendar().tick);
    auto expected = source.climate;
    Json checksums = Json::array();
    Json next;
    for (std::uint64_t i = 1; i <= 200u; ++i) {
        direct.tick(); loaded.tick();
        advance_climate_tick(expected, source.ticks + i, cache);
        check(direct.checksum() == loaded.checksum() && direct.weather() == loaded.weather() &&
            loaded.weather() == cache.view(expected), "Actual winter boundary continuation diverged");
        checksums.push_back(loaded.checksum());
        if (i == 1u) next = {{"simulation_tick", loaded.ticks()},
            {"sample", sample_json(loaded.weather().current)}, {"checksum", loaded.checksum()}};
    }
    check(direct.export_state().climate == loaded.export_state().climate &&
        loaded.export_state().climate == expected, "Private climate remainder continuation diverged");
    const auto tag = source.ticks == selected.start - selected.offset - 1u ? "before" :
        source.ticks == selected.start - selected.offset ? "start" : "end";
    if (std::string(tag) == "before" || std::string(tag) == "start")
        check(next["sample"]["kind"] == static_cast<unsigned>(WeatherKind::Storm) &&
            next["sample"]["phase"] == static_cast<unsigned>(PrecipitationKind::Snow),
            "Loaded next tick did not enter/remain in Snow storm");
    else check(next["sample"]["kind"] != static_cast<unsigned>(WeatherKind::Storm),
        "Loaded next tick did not leave end-exclusive storm");
    passed(controls, std::string(tag) == "before" ? "SAVE-before200" :
        std::string(tag) == "start" ? "SAVE-start200" : "SAVE-end200");
    return {{"boundary", tag}, {"filename", name}, {"source_tick", source.ticks}, {"source_checksum", checksum},
        {"source_climate", climate_json(source.climate)}, {"source_weather", weather_json(weather)},
        {"loaded_exact", true}, {"next_tick", next}, {"continuation_ticks", 200}, {"pair_world_ticks", 400},
        {"checksums", checksums}, {"checksum_same", true}, {"final_climate", climate_json(expected)},
        {"final_checksum", loaded.checksum()}, {"main_trace_not_advanced", world.ticks() == source.ticks}};
}
void world_trace(const fs::path& output, const StormResult& selected, Json& deterministic, Json& controls) {
    World world(selected.seed, 10, generator_version, winter_config());
    const auto initial = world.export_state();
    check(world.ticks() == 0 && world.calendar().tick.value == 864000u && world.day() == 31u &&
        world.winter() && clock_time(world.calendar().tick) == "06:00", "Winter origin replayed simulation");
    check(initial.climate.snow_cover_permille == 650u && initial.climate.snow_cover_remainder == 0 &&
        initial.climate.wetness_permille == 0u && initial.climate.wetness_remainder == 0 &&
        initial.climate.outdoor_work_remainders.empty(), "Winter origin has earned climate history");
    passed(controls, "ORIGIN-no-replay");
    const auto inventory = world.inventory();
    check(inventory.stored.food == 1200 && inventory.total.food == 1200 && inventory.available.food == 1200 &&
        inventory.reserved.food == 0 && inventory.carried.food == 0 &&
        initial.resources.food == 1200 && initial.storages.size() == 1u &&
        initial.storages.front().capacity == 10000u && !initial.settlement.reduced_rations,
        "Winter physical food/capacity/ration origin differs");
    World standard(selected.seed, 10);
    check(standard.inventory().stored.food == 100, "Standard start received scenario provisioning");
    passed(controls, "STOCK-physical1200");
    auto& trace = deterministic["world"];
    trace = {{"actual_ticks", 0}, {"simulation_minutes", 20}, {"consumer_count", 10},
        {"initial_food", inventory.stored.food}, {"camp_capacity", initial.storages.front().capacity},
        {"standard_initial_food", 100}, {"ration_interval_ticks", ration_interval},
        {"initial", snapshot_json(world)}, {"checkpoints", Json::array()}, {"ration_events", Json::array()}};
    std::vector<int> residents;
    for (const auto& resident : world.residents()) residents.push_back(resident.id);
    const auto commands = world.stop(residents);
    check(commands.size() == 10u && std::all_of(commands.begin(), commands.end(), [](const auto& result) {
        return result.status != ResidentCommandStatus::Rejected;
    }), "Public stop did not accept ten actors");
    check_population(world);
    trace["actor_control"] = {{"public_stop_calls", 1}, {"accepted_actor_results", 10},
        {"state_import_mutations", 0}, {"after_stop", population_json(world)}};
    const WeatherContext context{selected.seed, winter_config()};
    WeatherScheduleCache cache(context, world.calendar().tick);
    auto expected = initialize_climate(context, cache);
    check(world.weather() == cache.view(expected), "Initial canonical host weather differs");
    std::uint64_t actual_storm_ticks{}, actual_digest = digest_basis;
    std::int16_t minimum = 600, maximum = -800;
    auto& persistence = deterministic["persistence"]; persistence = Json::array();
    const auto start = selected.start - selected.offset, end = selected.end - selected.offset;
    for (std::uint64_t tick = 1; tick <= trace_ticks; ++tick) {
        const auto food_before = world.inventory().stored.food;
        world.tick(); advance_climate_tick(expected, tick, cache);
        const auto weather = world.weather();
        check(world.ticks() == tick && weather == cache.view(expected), "Actual World current/forecast/cover lost authority");
        check_population(world);
        const auto food_after = world.inventory().stored.food;
        if (tick % ration_interval == 0u) {
            check(food_before >= 10 && food_after == food_before - 10,
                "Actual ration check did not consume ten physical food");
            trace["ration_events"].push_back({{"tick", tick}, {"food_before", food_before},
                {"food_after", food_after}, {"consumers", 10}, {"population", population_json(world)}});
        } else check(food_after == food_before, "Physical food changed outside normal ration check");
        const auto physical = world.inventory();
        check(physical.total.food == food_after && physical.available.food == food_after &&
            physical.reserved.food == 0 && physical.carried.food == 0,
            "Ration horizon created unexpected food output");
        if (tick >= start && tick < end) {
            const auto& sample = weather.current;
            check(sample.kind == WeatherKind::Storm && sample.precipitation == PrecipitationKind::Snow &&
                sample.temperature_deci_c <= 0 && sample.period_start_calendar_tick == selected.start &&
                sample.period_end_calendar_tick == selected.end, "Actual selected first storm contains non-Snow tick");
            ++actual_storm_ticks; minimum = std::min(minimum, sample.temperature_deci_c);
            maximum = std::max(maximum, sample.temperature_deci_c);
            digest_sample(actual_digest, weather.calendar_tick, sample);
        } else check(weather.current.kind != WeatherKind::Storm, "Unexpected first-day extra Storm period");
        if (tick == start - 1u || tick == start || tick == end - 1u) {
            trace["checkpoints"].push_back(snapshot_json(world));
            const auto filename = tick == start - 1u ? "winter-before.save" :
                tick == start ? "winter-start.save" : "winter-end.save";
            persistence.push_back(save_boundary(output, filename, world, selected, controls));
        } else if (tick == end || tick == trace_ticks)
            trace["checkpoints"].push_back(snapshot_json(world));
        trace["actual_ticks"] = tick;
    }
    check(actual_storm_ticks == selected.count && actual_digest == selected.digest &&
        minimum == selected.minimum && maximum == selected.maximum,
        "Actual all-Snow storm trace differs from exhaustive seed certificate");
    check(world.export_state().climate == expected, "Actual final private climate differs from canonical trace");
    check(world.inventory().stored.food == 0 && trace["ration_events"].size() == 120u,
        "Actual twenty-minute ration horizon did not consume physical1200 food");
    check(persistence.size() == 3u, "Three real reached save boundaries missing");
    trace["final_food"] = world.inventory().stored.food;
    trace["final"] = snapshot_json(world);
    trace["ration_checks"] = trace["ration_events"].size();
    trace["actual_storm_tick_count"] = actual_storm_ticks;
    trace["actual_storm_sample_digest"] = actual_digest;
    trace["minimum_storm_temperature_deci_c"] = minimum;
    trace["maximum_storm_temperature_deci_c"] = maximum;
    trace["canonical_comparisons"] = trace_ticks;
    passed(controls, "WORLD-canonical24000"); passed(controls, "RATIONS-120");
}
std::uintmax_t output_bytes(const fs::path& output) {
    std::uintmax_t total{};
    for (const auto& entry : fs::directory_iterator(output)) {
        check(entry.is_regular_file() && !entry.is_symlink(), "Unexpected output entry");
        total += entry.file_size();
    }
    check(total <= output_limit, "Winter output exceeds128MiB"); return total;
}
void publish(const fs::path& output, const Json& report) {
    const auto path = output / "weather-winter-report.json";
    check(!fs::exists(path), "Refuse existing winter report");
    const auto text = report.dump(2) + "\n";
    check(output_bytes(output) + text.size() <= output_limit, "Winter report exceeds output budget");
    std::ofstream stream(path, std::ios::binary); check(bool(stream), "Winter report open failed");
    stream << text; stream.close(); check(bool(stream), "Winter report flush failed");
}
} // namespace

int main(int argc, char** argv) {
    Json report = {{"schema", 1}, {"complete", false}};
    fs::path output; bool owns = false; const auto begin = Clock::now();
    try {
        check(argc == 2, "Usage: WeatherWinterProbe output-directory"); output = fs::absolute(argv[1]);
        check(!fs::exists(output) && fs::is_directory(output.parent_path()), "Winter output must be fresh");
        check(fs::space(output.parent_path()).available >= free_space_required, "At least384MiB free required");
        check(fs::create_directory(output), "Fresh winter output creation failed"); owns = true;
        auto& deterministic = report["deterministic"];
        deterministic["controls"] = Json::array(); auto& controls = deterministic["controls"];
        const auto search_begin = Clock::now();
        const auto selected = search_seed(deterministic, controls);
        report["measured"]["search_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - search_begin).count();
        const auto trace_begin = Clock::now(); world_trace(output, selected, deterministic, controls);
        report["measured"]["world_trace_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - trace_begin).count();
        check(controls.size() == 8u && deterministic["invalid_controls"].size() == 1u, "Winter control coverage missing");
        deterministic["scope"] = "24000 actual World ticks:20 simulation minutes at1x; no20 wall-clock play proof";
        report["versions"] = {{"save", 9}, {"protocol", protocol_version}, {"rules", rules_version}, {"generator", generator_version}};
        report["open_delivery"] = {"shipping startup seed selection", "renderer/HUD winter delivery",
            "weather FX/materials/footprints and references", "full simulation one-percent overhead/GPU/FPS",
            "full suite and two-PC/cross-network testing", "full engine completion"};
        report["measured"]["output_bytes_before_report"] = output_bytes(output);
        report["measured"]["total_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        report["complete"] = true; publish(output, report);
        std::cout << "weather winter checks passed\n"; return 0;
    } catch (const std::exception& error) {
        report["complete"] = false; report["error"] = error.what(); std::cerr << error.what() << '\n';
        if (owns) try { publish(output, report); }
        catch (const std::exception& publication) { std::cerr << publication.what() << '\n'; }
        return 1;
    }
}

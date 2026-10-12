#include "ribterra/client_session.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/world_state.hpp"
#include "network_frame_generated.h"
#include "network_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <nlohmann/json.hpp>
#include <zstd.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace ribterra;
using namespace ribterra::net;
namespace fb = ribterra::wire;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::uint8_t>;
enum class WireOmission { None, ForecastVector, ForecastCard };

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
template<class Operation>
void checked(const std::string& stage, Operation operation, const std::string& error) {
    const bool ok = operation();
    require(ok, stage + ": " + error);
}
void budget(const std::filesystem::path& output) {
    require(std::filesystem::space(output).available >= 384u * 1024u * 1024u,
            "Transport free space below 384MiB");
    std::uintmax_t used{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) {
        require(!entry.is_symlink(), "Unexpected symlink in transport output");
        if (entry.is_regular_file()) used += entry.file_size();
    }
    require(used <= 128u * 1024u * 1024u, "Transport output exceeds 128MiB");
}
void write_new(const std::filesystem::path& path, const Bytes& bytes) {
    require(!std::filesystem::exists(path), "Refuse existing output: " + path.filename().string());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(bool(stream), "Could not write: " + path.filename().string());
}
ClimateConfig winter_config() {
    ClimateConfig result;
    result.scenario = ClimateScenarioId::FirstWinterStorm;
    return result;
}
WorldId diagnostic_id() {
    WorldId result{};
    result.front() = 42;
    result.back() = 73;
    return result;
}
Bytes fingerprint(const ViewSnapshot& view, const WorldId& id) {
    return encode(Packet{0, make_snapshot(view, id)});
}
bool same_publication(const ViewSnapshot& view, const WorldId& id, const Bytes& before,
                      const WorldId& before_id, const WeatherView& weather) {
    return id == before_id && view.weather == weather && fingerprint(view, id) == before;
}

// Actual object-API mutation bypasses only the trusted sender's validation.
// The outer frame, expanded length and SHA256 are rebuilt correctly, so these
// controls exercise the receiver's required-field and weather validation.
template<class Mutate>
Bytes mutate_packet(const Packet& packet, Mutate mutate, WireOmission omission = WireOmission::None) {
    const auto encoded = encode(packet);
    const auto* frame = fb::GetFrame(encoded.data());
    Bytes expanded(frame->expanded_size());
    if (frame->compression() == fb::Compression::Zstd) {
        const auto size = ZSTD_decompress(expanded.data(), expanded.size(), frame->data()->data(), frame->data()->size());
        require(!ZSTD_isError(size) && size == expanded.size(), "Could not expand diagnostic packet");
    } else {
        require(frame->compression() == fb::Compression::None && frame->data()->size() == expanded.size(),
                "Unexpected diagnostic frame");
        std::copy(frame->data()->begin(), frame->data()->end(), expanded.begin());
    }
    std::unique_ptr<fb::PacketT> native(fb::GetPacket(expanded.data())->UnPack());
    require(native && native->payload.AsSnapshot(), "Diagnostic payload is not Snapshot");
    mutate(*native->payload.AsSnapshot());
    flatbuffers::FlatBufferBuilder inner;
    fb::FinishPacketBuffer(inner, fb::Packet::Pack(inner, native.get()));
    // Required vectors are always emitted by generated object packing, and
    // packing a null table element dereferences it. Remove only these encoded
    // references after valid packing, before rebuilding the outer checksum.
    if (omission != WireOmission::None) {
        const auto* weather = fb::GetPacket(inner.GetBufferPointer())->payload_as_Snapshot()->weather();
        require(weather && weather->forecast() && weather->forecast()->size() == forecast_days,
                "Required-field fixture was not initially valid");
        if (omission == WireOmission::ForecastVector) {
            const auto* table = reinterpret_cast<const std::uint8_t*>(weather);
            const auto* vtable = table - flatbuffers::ReadScalar<flatbuffers::soffset_t>(table);
            require(flatbuffers::ReadScalar<flatbuffers::voffset_t>(vtable) > fb::WeatherView::VT_FORECAST,
                    "Forecast vtable field missing from valid fixture");
            flatbuffers::WriteScalar<flatbuffers::voffset_t>(
                const_cast<std::uint8_t*>(vtable) + fb::WeatherView::VT_FORECAST, 0);
        } else {
            flatbuffers::WriteScalar<flatbuffers::uoffset_t>(
                const_cast<std::uint8_t*>(weather->forecast()->Data()), 0);
        }
    }
    fb::FrameT replacement;
    replacement.protocol = protocol_version;
    replacement.expanded_size = static_cast<std::uint32_t>(inner.GetSize());
    replacement.data.assign(inner.GetBufferPointer(), inner.GetBufferPointer() + inner.GetSize());
    replacement.content_sha256 = sha256(replacement.data);
    flatbuffers::FlatBufferBuilder outer;
    fb::FinishFrameBuffer(outer, fb::Frame::Pack(outer, &replacement));
    require(outer.GetSize() <= max_packet_bytes, "Diagnostic packet is unexpectedly large");
    return {outer.GetBufferPointer(), outer.GetBufferPointer() + outer.GetSize()};
}

Json wire_controls(const std::filesystem::path& output) {
    ServerConfig config;
    config.population = 0;
    config.climate = winter_config();
    GameServer server(config);
    const auto joined = server.join("Wire fixture", {});
    require(joined.accepted, joined.reason);
    const auto source = server.snapshot(joined.player);
    const auto fixed_id = diagnostic_id();
    auto full = make_snapshot(source, fixed_id);
    ViewSnapshot live;
    WorldId live_id{};
    std::string error;
    checked("Initial diagnostic baseline", [&] { return apply_snapshot(live, live_id, full, error); }, error);
    const auto before = fingerprint(live, live_id);
    const auto before_id = live_id;
    const auto before_weather = live.weather;
    Json wire = Json::array(), apply = Json::array(), positive = Json::array(), assembly = Json::array();
    std::filesystem::create_directory(output / "wire");
    const auto reject_wire = [&](const std::string& id, const std::function<void(fb::SnapshotT&)>& mutate,
                                 WireOmission omission = WireOmission::None) {
        const auto bytes = mutate_packet(Packet{1, full}, mutate, omission);
        write_new(output / "wire" / (id + ".rbnf"), bytes);
        const auto decoded = decode(bytes, error);
        require(!decoded && !error.empty(), "Malformed weather decoded: " + id);
        require(same_publication(live, live_id, before, before_id, before_weather), "Decode rejection changed publication: " + id);
        const auto* outer = fb::GetFrame(bytes.data());
        flatbuffers::Verifier verify(outer->data()->data(), outer->data()->size(), 32, 20000);
        const bool inner_valid = fb::VerifyPacketBuffer(verify);
        const bool required_missing = id == "missing_weather" || id == "missing_current" ||
            omission != WireOmission::None;
        require(inner_valid != required_missing, "Wire fixture failed at the wrong structural layer: " + id);
        wire.push_back({{"id", id}, {"decoder_rejected", true}, {"view_world_weather_exact", true},
            {"inner_flatbuffer_valid", inner_valid}, {"error", error}});
    };
    reject_wire("missing_weather", [](auto& s) { s.weather.reset(); });
    reject_wire("missing_current", [](auto& s) { s.weather->current.reset(); });
    reject_wire("missing_forecast", [](auto&) {}, WireOmission::ForecastVector);
    reject_wire("missing_forecast_card", [](auto&) {}, WireOmission::ForecastCard);
    reject_wire("forecast_count_0", [](auto& s) { s.weather->forecast.clear(); });
    reject_wire("forecast_count_3", [](auto& s) { s.weather->forecast.resize(3); });
    reject_wire("forecast_count_5", [](auto& s) { s.weather->forecast.push_back(std::make_unique<fb::WeatherDayForecastT>(*s.weather->forecast.back())); });
    reject_wire("interval_count_0", [](auto& s) { s.weather->forecast[0]->intervals.clear(); });
    reject_wire("interval_count_9", [](auto& s) { s.weather->forecast[0]->intervals.resize(9); });
    reject_wire("unknown_weather", [](auto& s) { s.weather->known = false; });
    reject_wire("algorithm_0", [](auto& s) { s.weather->algorithm_version = 0; });
    reject_wire("algorithm_2", [](auto& s) { s.weather->algorithm_version = 2; });
    reject_wire("profile_0", [](auto& s) { s.weather->climate_profile = static_cast<fb::ClimateProfileId>(0); });
    reject_wire("profile_2", [](auto& s) { s.weather->climate_profile = static_cast<fb::ClimateProfileId>(2); });
    reject_wire("scenario_2", [](auto& s) { s.weather->scenario = static_cast<fb::ClimateScenarioId>(2); });
    reject_wire("kind_enum", [](auto& s) { s.weather->current->kind = static_cast<fb::WeatherKind>(255); });
    reject_wire("precipitation_enum", [](auto& s) { s.weather->current->precipitation = static_cast<fb::PrecipitationKind>(255); });
    reject_wire("wind_enum", [](auto& s) { s.weather->current->wind_direction = static_cast<fb::WindDirection>(255); });
    reject_wire("work_reason_enum", [](auto& s) { s.weather->current->work_reason = static_cast<fb::WeatherWorkReason>(255); });
    reject_wire("forecast_kind_enum", [](auto& s) { s.weather->forecast[0]->dominant_kind = static_cast<fb::WeatherKind>(255); });
    reject_wire("forecast_precipitation_enum", [](auto& s) { s.weather->forecast[0]->precipitation_summary = static_cast<fb::PrecipitationSummary>(255); });
    reject_wire("interval_kind_enum", [](auto& s) { const auto& p = s.weather->forecast[0]->intervals[0]; s.weather->forecast[0]->intervals[0] = fb::WeatherInterval(p.start_tick_in_day(), p.end_tick_in_day(), static_cast<fb::WeatherKind>(255)); });
    reject_wire("evaluated_tick_mismatch", [](auto& s) { ++s.weather->evaluated_simulation_tick; });
    reject_wire("calendar_tick_mismatch", [](auto& s) { ++s.weather->calendar_tick; });
    reject_wire("offset_mismatch", [](auto& s) { s.weather->calendar_offset_ticks = 1; });
    reject_wire("snapshot_day_mismatch", [](auto& s) { ++s.day; });
    reject_wire("snapshot_winter_mismatch", [](auto& s) { s.winter = !s.winter; });
    reject_wire("forecast_day_mismatch", [](auto& s) { ++s.weather->forecast[0]->calendar_day; });
    reject_wire("current_period_empty", [](auto& s) { s.weather->current->period_end_calendar_tick = s.weather->current->period_start_calendar_tick; });
    reject_wire("current_period_not_covering", [](auto& s) { s.weather->current->period_start_calendar_tick = s.weather->calendar_tick + 1; });
    reject_wire("temperature_below", [](auto& s) { s.weather->current->temperature_deci_c = -801; });
    reject_wire("temperature_above", [](auto& s) { s.weather->current->temperature_deci_c = 601; });
    reject_wire("precipitation_range", [](auto& s) { s.weather->current->precipitation_permille = 1001; });
    reject_wire("wind_range", [](auto& s) { s.weather->current->wind_permille = 1001; });
    reject_wire("work_rate_below", [](auto& s) { s.weather->current->outdoor_work_rate_bp = 7499; });
    reject_wire("work_rate_above", [](auto& s) { s.weather->current->outdoor_work_rate_bp = 10001; });
    reject_wire("work_reason_mismatch", [](auto& s) { s.weather->current->work_reason = fb::WeatherWorkReason::Storm; });
    reject_wire("precipitation_kind_mismatch", [](auto& s) { s.weather->current->precipitation = fb::PrecipitationKind::Rain; });
    reject_wire("snow_range", [](auto& s) { s.weather->snow_cover_permille = 1001; });
    reject_wire("wetness_range", [](auto& s) { s.weather->wetness_permille = 1001; });
    reject_wire("wet_remainder_positive_bound", [](auto& s) { s.weather->wetness_remainder = 28800; });
    reject_wire("wet_remainder_negative_bound", [](auto& s) { s.weather->wetness_remainder = -28800; });
    reject_wire("wet_remainder_outward", [](auto& s) { s.weather->wetness_permille = 0; s.weather->wetness_remainder = -1; });
    reject_wire("forecast_minimum_range", [](auto& s) { s.weather->forecast[0]->minimum_temperature_deci_c = -801; });
    reject_wire("forecast_maximum_range", [](auto& s) { s.weather->forecast[0]->maximum_temperature_deci_c = 601; });
    reject_wire("forecast_minimum_above_maximum", [](auto& s) { s.weather->forecast[0]->minimum_temperature_deci_c = 600; s.weather->forecast[0]->maximum_temperature_deci_c = -800; });
    reject_wire("interval_gap", [](auto& s) { const auto& p = s.weather->forecast[0]->intervals[0]; s.weather->forecast[0]->intervals[0] = fb::WeatherInterval(1, p.end_tick_in_day(), p.kind()); });
    reject_wire("interval_empty", [](auto& s) { const auto& p = s.weather->forecast[0]->intervals[0]; s.weather->forecast[0]->intervals[0] = fb::WeatherInterval(0, 0, p.kind()); });
    reject_wire("interval_end_range", [](auto& s) { const auto& p = s.weather->forecast[0]->intervals[0]; s.weather->forecast[0]->intervals[0] = fb::WeatherInterval(0, static_cast<std::uint32_t>(ticks_per_day + 1), p.kind()); });
    reject_wire("interval_order", [](auto& s) { require(s.weather->forecast[0]->intervals.size() >= 2, "First storm fixture has no interval pair"); std::swap(s.weather->forecast[0]->intervals[0], s.weather->forecast[0]->intervals[1]); });
    reject_wire("adjacent_identical_intervals", [](auto& s) { auto& day = *s.weather->forecast[3]; day.intervals = {fb::WeatherInterval(0, 14400, fb::WeatherKind::Clear), fb::WeatherInterval(14400, 28800, fb::WeatherKind::Clear)}; day.dominant_kind = fb::WeatherKind::Clear; day.precipitation_summary = fb::PrecipitationSummary::None; day.has_storm = false; });
    reject_wire("forecast_summary_mismatch", [](auto& s) { s.weather->forecast[0]->precipitation_summary = fb::PrecipitationSummary::None; });
    reject_wire("forecast_storm_mismatch", [](auto& s) { s.weather->forecast[0]->has_storm = !s.weather->forecast[0]->has_storm; });
    reject_wire("forecast_dominant_mismatch", [](auto& s) { const auto kind = s.weather->forecast[0]->dominant_kind; s.weather->forecast[0]->dominant_kind = kind == fb::WeatherKind::Clear ? fb::WeatherKind::Cloudy : fb::WeatherKind::Clear; });
    reject_wire("storm_pair_omitted", [](auto& s) { s.weather->next_storm_start_calendar_tick = 0; s.weather->next_storm_end_calendar_tick = 0; });
    reject_wire("storm_pair_empty", [](auto& s) { s.weather->next_storm_end_calendar_tick = s.weather->next_storm_start_calendar_tick; });
    reject_wire("storm_pair_wrong_start", [](auto& s) { ++s.weather->next_storm_start_calendar_tick; });
    reject_wire("storm_pair_horizon", [](auto& s) { s.weather->next_storm_end_calendar_tick = s.weather->calendar_offset_ticks + 5 * ticks_per_day; });

    const auto positive_wire = [&](const std::string& id, const std::function<void(fb::SnapshotT&)>& mutate) {
        const auto bytes = mutate_packet(Packet{2, full}, mutate);
        write_new(output / "wire" / (id + ".rbnf"), bytes);
        const auto packet = decode(bytes, error);
        require(packet.has_value(), "Positive wire failed: " + id + ": " + error);
        ViewSnapshot target;
        WorldId target_id{};
        checked("Positive wire apply " + id, [&] { return apply_snapshot(target, target_id, std::get<Snapshot>(packet->payload), error); }, error);
        require(target_id == fixed_id && target.weather.known, "Positive wire did not publish: " + id);
        positive.push_back({{"id", id}, {"passed", true}});
    };
    positive_wire("valid_unchanged_object_frame", [](auto&) {});
    positive_wire("valid_cover_lower_bounds", [](auto& s) { s.weather->snow_cover_permille = 0; s.weather->wetness_permille = 0; s.weather->wetness_remainder = 0; });
    positive_wire("valid_cover_upper_bounds", [](auto& s) { s.weather->snow_cover_permille = 1000; s.weather->wetness_permille = 1000; s.weather->wetness_remainder = 0; });
    positive_wire("valid_signed_remainder_positive", [](auto& s) { s.weather->wetness_permille = 500; s.weather->wetness_remainder = 28799; });
    positive_wire("valid_signed_remainder_negative", [](auto& s) { s.weather->wetness_permille = 500; s.weather->wetness_remainder = -28799; });
    positive_wire("valid_interval_count_1", [](auto& s) { auto& day = *s.weather->forecast[3]; day.intervals = {fb::WeatherInterval(0, 28800, fb::WeatherKind::Clear)}; day.dominant_kind = fb::WeatherKind::Clear; day.precipitation_summary = fb::PrecipitationSummary::None; day.has_storm = false; });
    positive_wire("valid_interval_count_8", [](auto& s) { auto& day = *s.weather->forecast[3]; day.intervals.clear(); for (std::uint32_t i = 0; i < 8; ++i) day.intervals.emplace_back(i * 3600, (i + 1) * 3600, i % 2 == 0 ? fb::WeatherKind::Clear : fb::WeatherKind::Cloudy); day.dominant_kind = fb::WeatherKind::Cloudy; day.precipitation_summary = fb::PrecipitationSummary::None; day.has_storm = false; });

    const auto reject_apply = [&](const std::string& id, Snapshot candidate) {
        require(!apply_snapshot(live, live_id, candidate, error) && !error.empty(), "Bad apply accepted: " + id);
        require(same_publication(live, live_id, before, before_id, before_weather), "Bad apply changed publication: " + id);
        apply.push_back({{"id", id}, {"view_world_weather_exact", true}, {"error", error}});
    };
    auto bad = full; bad.revision = full.revision + 1; bad.weather.known = false; reject_apply("unknown_weather", bad);
    bad = full; ++bad.weather.evaluated_simulation_tick; reject_apply("evaluated_tick_mismatch", bad);
    bad = full; ++bad.weather.calendar_tick; reject_apply("calendar_tick_mismatch", bad);
    bad = full; ++bad.day; reject_apply("day_mismatch", bad);
    bad = full; bad.winter = !bad.winter; reject_apply("winter_mismatch", bad);
    bad = full; bad.weather.snow_cover_permille = 1001; reject_apply("snow_range", bad);
    bad = full; bad.weather.wetness_permille = 1001; reject_apply("wetness_range", bad);
    bad = full; bad.weather.wetness_remainder = 28800; reject_apply("wet_remainder_range", bad);
    bad = full;
    require(bad.weather.forecast[0].interval_count < max_forecast_intervals_per_day, "No inactive interval slot");
    bad.weather.forecast[0].intervals[bad.weather.forecast[0].interval_count] = {1, 2, WeatherKind::Clear};
    reject_apply("inactive_interval_nondefault", bad);

    ServerConfig standard_config = config; standard_config.climate = {};
    GameServer standard_server(standard_config);
    const auto standard_join = standard_server.join("Standard fixture", {});
    require(standard_join.accepted, standard_join.reason);
    auto replacement_view = standard_server.snapshot(standard_join.player);
    replacement_view.revision = full.revision + 1;
    auto identity_delta = make_snapshot(replacement_view, fixed_id);
    identity_delta.full = false; identity_delta.baseline = live.revision;
    validate_weather_view(identity_delta.weather, identity_delta.tick);
    const auto decoded_identity = decode(encode(Packet{3, identity_delta}), error);
    require(decoded_identity.has_value(), "Valid changed climate delta failed structural decode");
    reject_apply("delta_climate_identity", std::get<Snapshot>(decoded_identity->payload));
    positive.push_back({{"id", "legal_identity_delta_decodes"}, {"passed", true}});
    bool encoder_rejected{};
    try { (void)make_snapshot(replacement_view, fixed_id, &live); }
    catch (const std::runtime_error&) { encoder_rejected = true; }
    require(encoder_rejected, "Sender accepted climate identity delta");
    positive.push_back({{"id", "sender_identity_delta_rejected"}, {"passed", true}});
    bad = full; bad.full = false; bad.baseline = live.revision; ++bad.revision; bad.world_id.back() ^= 1; reject_apply("delta_wrong_world", bad);
    bad = full; bad.full = false; bad.baseline = live.revision + 1; ++bad.revision; reject_apply("delta_baseline_mismatch", bad);
    auto full_replacement = make_snapshot(replacement_view, fixed_id);
    ViewSnapshot replacement_target = live; WorldId replacement_id = live_id;
    checked("Full climate identity replacement", [&] { return apply_snapshot(replacement_target, replacement_id, full_replacement, error); }, error);
    require(replacement_target.weather == replacement_view.weather && replacement_id == fixed_id,
            "Full replacement did not change climate identity");
    positive.push_back({{"id", "full_identity_replacement"}, {"passed", true}});

    auto bulk = source; ++bulk.revision;
    for (const auto& chunk : server.world().export_state().chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        if (std::none_of(bulk.chunks.begin(), bulk.chunks.end(), [&](const auto& existing) { return existing.key == chunk.key; })) {
            auto fixture = chunk;
            for (auto& tile : fixture.tiles) tile.explored = true;
            bulk.chunks.push_back(std::move(fixture));
        }
        if (bulk.chunks.size() >= 40) break;
    }
    // Publication canonically sorts chunks. Author the expected complete
    // fixture in that same order before encoding/splitting, so byte equality
    // still checks the entire baseline rather than insertion-order artifacts.
    const auto chunk_less = [](const auto& left, const auto& right) { return left.key < right.key; };
    std::sort(bulk.chunks.begin(), bulk.chunks.end(), chunk_less);
    require(std::is_sorted(bulk.chunks.begin(), bulk.chunks.end(), chunk_less),
            "Complete multipart fixture is not canonically ordered");
    const auto parts = split_snapshot(make_snapshot(bulk, fixed_id));
    require(parts.size() > 1, "Assembly fixture is not multipart");
    const auto reject_assembly = [&](const std::string& id, bool duplicate, bool malformed, bool expire) {
        auto now = Clock::time_point{};
        SnapshotAssembler assembler([&] { return now; });
        require(assembler.accept(live, live_id, parts[0], error) == AssemblyResult::Pending, "Assembly start failed: " + id);
        require(same_publication(live, live_id, before, before_id, before_weather), "Pending assembly published: " + id);
        auto next = duplicate ? parts[0] : parts[1];
        if (malformed) next.weather.forecast[0].interval_count = 0;
        else if (!expire) { ++next.weather.snow_cover_permille; validate_weather_view(next.weather, next.tick); }
        if (expire) now += std::chrono::seconds(11);
        require(assembler.accept(live, live_id, next, error) == AssemblyResult::Rejected && !error.empty(),
                "Bad assembly accepted: " + id);
        require(same_publication(live, live_id, before, before_id, before_weather), "Bad assembly changed publication: " + id);
        if (expire) require(error == "Snapshot assembly timed out", "Timeout control failed for another reason");
        assembly.push_back({{"id", id}, {"passed", true}, {"view_world_weather_exact", true}, {"error", error}});
    };
    reject_assembly("malformed_part", false, true, false);
    reject_assembly("legal_header_weather_mismatch", false, false, false);
    reject_assembly("conflicting_repeat_metadata", true, false, false);
    reject_assembly("accept_after_timeout", false, false, true);
    auto now = Clock::time_point{};
    SnapshotAssembler assembler([&] { return now; });
    require(assembler.accept(live, live_id, parts[0], error) == AssemblyResult::Pending, error);
    require(assembler.accept(live, live_id, parts[0], error) == AssemblyResult::Pending &&
            same_publication(live, live_id, before, before_id, before_weather), "Identical part repeat published");
    positive.push_back({{"id", "identical_part_repeat_pending"}, {"passed", true}});
    now += std::chrono::seconds(11);
    require(assembler.accept(live, live_id, parts[1], error) == AssemblyResult::Rejected, "Assembly timeout was not reached");
    assembler.reset();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto packet = decode(encode(Packet{static_cast<std::uint64_t>(i + 10), parts[i]}), error);
        require(packet.has_value(), "Complete multipart decode: " + error);
        const auto result = assembler.accept(live, live_id, std::get<Snapshot>(packet->payload), error);
        require(result == (i + 1 == parts.size() ? AssemblyResult::Applied : AssemblyResult::Pending),
                "Complete multipart apply part " + std::to_string(i) + ": " + error);
    }
    require(live_id == fixed_id && live.weather == bulk.weather && fingerprint(live, live_id) == fingerprint(bulk, fixed_id),
            "Reset after timeout did not publish the full exact baseline");
    positive.push_back({{"id", "complete_baseline_after_timeout_reset"}, {"passed", true}});
    budget(output);
    return {{"wire_rejections", wire}, {"apply_rejections", apply}, {"positive_controls", positive},
        {"assembly_controls", assembly}, {"multipart_count", parts.size()},
        {"wire_rejection_count", wire.size()}, {"apply_rejection_count", apply.size()}};
}

template<class Service, class Predicate>
void wait_until(const std::string& stage, Service service, Predicate predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < deadline) {
        service();
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Bounded transport stage timed out: " + stage);
}
void unknown(const ClientSession& client, const std::string& stage) {
    require(!client.weather().known && !client.calendar() && client.day() == 0 && !client.winter(),
            "Client exposed weather while unavailable: " + stage);
}
void exact_weather(const ClientSession& client, const World& host, const std::string& stage) {
    require(client.ready() && client.world_ready() && !client.reconnecting() && !client.recovery_incomplete() &&
            client.weather() == host.weather() && client.calendar() == host.calendar() &&
            client.ticks() == host.ticks() && client.day() == host.day() && client.winter() == host.winter(),
            "Client differs from actual host: " + stage);
}
std::unique_ptr<NetworkServer> listen(HostConfig config, std::uint16_t exact_port = 0) {
    const auto first = exact_port ? exact_port : std::uint16_t{30020};
    const auto last = exact_port ? static_cast<std::uint32_t>(exact_port) + 1u : 30040u;
    for (std::uint32_t port = first; port < last; ++port) {
        config.transport.port = static_cast<std::uint16_t>(port);
        auto server = std::make_unique<NetworkServer>(config);
        if (server->listen()) return server;
    }
    throw std::runtime_error("No available bounded loopback host port");
}
Json session_controls(const std::filesystem::path& output) {
    // Explicit host-only calendar-boundary fixture. Cover remains authored
    // initial cover; skipped climate or gameplay history is not claimed.
    World fixture(70421, 0, generator_version, winter_config());
    auto state = fixture.export_state();
    state.ticks = ticks_per_day - 2;
    state.climate.evaluated_simulation_tick = state.ticks;
    const WeatherContext context{state.seed, winter_config()};
    const auto calendar = make_calendar_tick(state.ticks, state.climate.calendar_offset_ticks);
    require(calendar.has_value(), "Boundary fixture calendar overflow");
    WeatherScheduleCache host_fixture_cache(context, *calendar);
    state.climate.current = host_fixture_cache.sample(*calendar);
    validate_climate_runtime(state.climate, state.ticks, context, host_fixture_cache);
    std::string error;
    checked("Boundary fixture import", [&] { return fixture.import_state(std::move(state), error); }, error);
    const auto save = output / "transport-boundary.save";
    checked("Boundary fixture initial save", [&] { return fixture.save(save, error); }, error);
    HostConfig config;
    config.game.population = 0;
    config.game.lobby_enabled = true;
    config.game.max_players = 4;
    config.game.climate = winter_config();
    config.transport.bind_address = "127.0.0.1";
    config.save_path = save;
    config.autosave_interval = std::chrono::hours(24);
    auto host = listen(config);
    const auto port = host->port();
    const auto address = "127.0.0.1:" + std::to_string(port);
    const auto host_id = host->game().world().export_state().world_id;
    ClientSession first(1, "Weather first"), second(1, "Weather late");
    ClientConfig audit_config;
    audit_config.name = "Weather audit participant";
    audit_config.auto_reconnect = true;
    NetworkClient audit(audit_config);
    bool second_connected{}, audit_connected{};
    const auto service = [&] {
        if (host) host->poll();
        first.poll();
        if (second_connected) second.poll();
        if (audit_connected) audit.poll();
    };
    Json controls = Json::array();
    const auto record = [&](const std::string& id) {
        controls.push_back({{"id", id}, {"passed", true}, {"host_tick", host->game().world().ticks()},
            {"calendar_day", host->game().world().day()}});
    };
    checked("First actual remote connection", [&] { return first.connect_remote(address, "", error); }, error);
    unknown(first, "pending initial connection");
    wait_until("first complete waiting lobby", service, [&] { return first.ready() && first.snapshot_ready(); });
    require(!first.world_ready(), "Waiting lobby is playable");
    unknown(first, "waiting lobby baseline");
    record("pending_and_waiting_lobby_unknown");
    const auto command = [&](ClientSession& client, LobbyAction action, int value) {
        while (client.take_command_reply()) {}
        const auto queued = client.lobby_action(action, value);
        require(queued.accepted && queued.pending, "Actual lobby command was not queued");
        bool applied{};
        wait_until("actual lobby command", service, [&] {
            while (const auto reply = client.take_command_reply()) {
                if (reply->status == CommandReplyStatus::Rejected || reply->status == CommandReplyStatus::Invalid)
                    throw std::runtime_error("Actual lobby command rejected: " + reply->reason);
                applied |= reply->status == CommandReplyStatus::Applied;
            }
            return applied;
        });
    };
    command(first, LobbyAction::Ready, 1);
    command(first, LobbyAction::Start, 0);
    wait_until("first started baseline", service, [&] { return first.world_ready() && first.weather().known; });
    exact_weather(first, host->game().world(), "first start");
    require(host->game().world().ticks() == ticks_per_day - 2 && host->game().world().day() == 31,
            "Host boundary fixture changed before explicit ticks");
    const auto advance = [&](int speed, int count, const std::string& stage) {
        const auto before_tick = host->game().world().ticks();
        for (int i = 0; i < count; ++i) host->tick();
        const auto expected_tick = before_tick + static_cast<std::uint64_t>(speed * count);
        require(host->game().world().ticks() == expected_tick, "Host speed authority: " + stage);
        wait_until(stage, service, [&] {
            const auto weather = host->game().world().weather();
            return first.weather() == weather && (!second_connected || second.weather() == weather) &&
                (!audit_connected || (audit.ready() && audit.view().weather == weather));
        });
        exact_weather(first, host->game().world(), stage);
        if (second_connected) exact_weather(second, host->game().world(), stage);
        record(stage);
    };
    advance(1, 4, "normal_advance_calendar_rollover");
    require(host->game().world().day() == 32 && first.day() == 32 &&
            first.calendar()->clock_minute == host->game().world().calendar().clock_minute,
            "Calendar rollover was not host authoritative");
    checked("Second actual remote connection", [&] { return second.connect_remote(address, "", error); }, error);
    second_connected = true;
    unknown(second, "pending late join");
    wait_until("actual second late join", service, [&] { return second.world_ready() && second.weather().known; });
    exact_weather(first, host->game().world(), "late join first");
    exact_weather(second, host->game().world(), "late join second");
    record("two_client_sessions_late_join");
    const bool audit_connect = audit.connect(address);
    require(audit_connect, "Third actual participant connection: " + audit.last_error());
    audit_connected = true;
    wait_until("third ordinary participant", service, [&] { return audit.ready(); });
    require(audit.world_id() == host_id && audit.view().weather == host->game().world().weather(),
            "Actual audit participant WorldID/weather differs");
    record("third_participant_received_world_id");
    for (const int speed : {1, 2, 4}) {
        command(first, LobbyAction::Speed, speed);
        wait_until("speed snapshot", service, [&] { return first.lobby().speed == speed && second.lobby().speed == speed; });
        advance(speed, 4, "speed_" + std::to_string(speed));
    }
    command(first, LobbyAction::Pause, 1);
    wait_until("pause snapshot", service, [&] { return first.lobby().paused && second.lobby().paused; });
    const auto paused_weather = host->game().world().weather();
    advance(0, 4, "paused_host_ticks_do_not_advance");
    require(host->game().world().weather() == paused_weather, "Pause advanced weather");
    command(first, LobbyAction::Pause, 0);
    wait_until("resume snapshot", service, [&] { return !first.lobby().paused && !second.lobby().paused; });
    advance(4, 4, "resume_speed_4");
    const auto first_player = first.player_id(), second_player = second.player_id(), audit_player = audit.player();
    const auto first_token = first.resume_token(), second_token = second.resume_token(), audit_token = audit.resume_token();
    require(!first_token.empty() && !second_token.empty() && !audit_token.empty(), "Missing real resume credentials");
    const auto before_restart_tick = host->game().world().ticks();
    const auto before_epoch = first.control_epoch();
    host->shutdown();
    require(host->last_error().empty(), "First shutdown/save failed: " + host->last_error());
    host.reset();
    wait_until("actual disconnect and retry state", service, [&] {
        return !first.ready() && first.reconnecting() && !second.ready() && second.reconnecting() &&
            !audit.ready() && audit.reconnecting();
    });
    unknown(first, "reconnecting first"); unknown(second, "reconnecting second");
    host = listen(config, port);
    require(host->game().world().export_state().world_id == host_id && host->game().world().ticks() == before_restart_tick,
            "Restarted host lost saved WorldID/tick");
    wait_until("actual automatic resume after restart", service, [&] {
        return first.world_ready() && second.world_ready() && first.weather().known && second.weather().known && audit.ready();
    });
    exact_weather(first, host->game().world(), "first resumed"); exact_weather(second, host->game().world(), "second resumed");
    require(first.player_id() == first_player && second.player_id() == second_player && audit.player() == audit_player &&
            first.resume_token() == first_token && second.resume_token() == second_token && audit.resume_token() == audit_token &&
            audit.world_id() == host_id && audit.view().weather == host->game().world().weather() &&
            first.control_epoch() == before_epoch + 1,
            "Automatic resume identity/WorldID/control epoch differs");
    record("actual_disconnect_auto_resume_identity_and_weather");
    require(first.lobby().paused, "Reloaded running lobby should pause");
    // This command is delivered through real GNS, but the server is never
    // polled before shutdown. It therefore has no authoritative reply. The
    // saved epoch transition must expose incomplete recovery, never old intent.
    const auto uncertain = first.lobby_action(LobbyAction::Pause, 0);
    require(uncertain.accepted && uncertain.pending && host->game().snapshot(first_player).lobby.paused,
            "Unconfirmed command fixture was not isolated before shutdown");
    const auto recovery_epoch = first.control_epoch();
    host->shutdown();
    require(host->last_error().empty(), "Second shutdown/save failed: " + host->last_error());
    host.reset();
    wait_until("second real disconnect", service, [&] {
        return !first.ready() && first.reconnecting() && !second.ready() && second.reconnecting() &&
            !audit.ready() && audit.reconnecting();
    });
    unknown(first, "uncertain reconnect"); unknown(second, "second uncertain reconnect");
    host = listen(config, port);
    wait_until("complete resumed baseline with incomplete recovery", service, [&] {
        return first.ready() && first.snapshot_ready() && first.recovery_incomplete() && second.weather().known && audit.ready();
    });
    require(first.control_epoch() == recovery_epoch + 1 && first.player_id() == first_player &&
            host->game().world().export_state().world_id == host_id && audit.world_id() == host_id,
            "Recovery epoch or WorldID differs");
    unknown(first, "actual recovery incomplete on complete baseline");
    exact_weather(second, host->game().world(), "unaffected recovery second");
    record("actual_incomplete_recovery_masks_weather");
    command(first, LobbyAction::Pause, 0);
    wait_until("explicit intent clears incomplete recovery", service, [&] { return !first.recovery_incomplete() && first.weather().known && !first.lobby().paused; });
    exact_weather(first, host->game().world(), "explicit recovery clear");
    exact_weather(second, host->game().world(), "explicit recovery clear second");
    require(audit.world_id() == host_id && audit.view().weather == host->game().world().weather(), "Final audit authority differs");
    record("explicit_command_clears_incomplete_recovery");
    const auto final_tick = host->game().world().ticks();
    host->shutdown();
    require(host->last_error().empty(), "Final shutdown/save failed: " + host->last_error());
    host.reset();
    budget(output);
    return {{"controls", controls}, {"final_host_tick", final_tick}, {"world_id_equal", true},
        {"resume_tokens_equal", true}, {"client_generation", false},
        {"world_saves", {"transport-boundary.save", "transport-boundary.save.bak"}},
        {"host_fixture", "explicit canonical sample at 28798; initial cover retained; skipped history not simulated"},
        {"participants", "two ClientSessions and one ordinary audit NetworkClient"}};
}
} // namespace

int main(int argc, char** argv) {
    Json report{{"schema", 1}, {"complete", false}};
    std::filesystem::path output;
    bool owns{};
    try {
        require(argc == 2, "Usage: WeatherTransportProbe output-directory");
        output = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(output), "Transport output directory must be fresh");
        std::filesystem::create_directory(output); owns = true; budget(output);
        auto deterministic = wire_controls(output);
        deterministic["session"] = session_controls(output);
        deterministic["scopes"] = {"actual object-API malformed frames; seedless structural weather validation",
            "actual GNS localhost; frozen host except explicit tick calls", "host-only declarative calendar fixture",
            "no client weather cache or RNG; no plaintext resume credentials",
            "partial assembly transaction checked through SnapshotAssembler; no injected ClientSession state"};
        deterministic["open_delivery"] = {"two-PC/WAN", "production renderer/HUD and weather FX", "reference acceptance",
            "winter seed certification", "full simulation performance", "full engine", "backup rotation failure runtime"};
        report["deterministic"] = std::move(deterministic);
        report["measured"] = Json::object();
        report["versions"] = {{"protocol", protocol_version}, {"rules", rules_version}, {"generator", generator_version}, {"save", 9}};
        report["complete"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what(); std::cerr << error.what() << '\n';
    }
    if (owns) {
        const auto text = report.dump(2) + "\n";
        write_new(output / "weather-transport-report.json", {text.begin(), text.end()});
    }
    if (report["complete"] == true) { std::cout << "weather transport checks passed\n"; return 0; }
    return 1;
}

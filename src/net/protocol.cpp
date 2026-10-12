#include "ribterra/protocol.hpp"
#include "network_frame_generated.h"
#include "network_generated.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <openssl/hmac.h>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <zstd.h>

namespace ribterra::net {
namespace {
std::unique_ptr<ribterra::wire::SettlementT> pack_settlement(const SettlementState &s) {
    auto p=std::make_unique<ribterra::wire::SettlementT>();
    p->revision=s.revision;p->arrival_tick=s.arrival_tick;p->forced_night=s.forced_night;
    p->reduced_rations=s.reduced_rations;p->night_discontent=s.night_discontent;p->youth_discontent=s.youth_discontent;
    for(const auto &y:s.young) p->young.emplace_back(y.resident_id,y.adult_tick,y.forced_work);
    return p;
}
SettlementState unpack_settlement(const ribterra::wire::Settlement *p) {
    if(!p) return {};
    if(p->young() && p->young()->size()>1500) throw std::runtime_error("Too many young residents");
    SettlementState s{p->revision(),p->arrival_tick(),p->forced_night(),p->night_discontent(),p->youth_discontent(),{},p->reduced_rations()};
    if(p->young()) for(const auto *y:*p->young()) s.young.push_back({y->resident_id(),y->adult_tick(),y->forced_work()});
    return s;
}

namespace fb = ribterra::wire;
static_assert(sizeof(fb::Tile) == 12, "Expanded worlds require compact wire tiles");
bool hex64(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool coordinate(int value) { return value >= world_min && value < world_max; }
bool layer_valid(WorldLayer layer) { return layer == WorldLayer::Surface || layer == WorldLayer::Mine; }
bool key_valid(const ChunkKey &key) {
    return layer_valid(key.layer) && valid_chunk_position(key.cx, key.cy);
}
bool reply_status_valid(CommandReplyStatus status) {
    return status == CommandReplyStatus::Queued || status == CommandReplyStatus::Applied ||
           status == CommandReplyStatus::Rejected;
}
bool path_failure_valid(PathFailure failure) {
    return static_cast<unsigned>(failure) <= static_cast<unsigned>(PathFailure::ReservationConflict);
}
bool logistics_failure_valid(LogisticsFailure failure) {
    return static_cast<unsigned>(failure) <= static_cast<unsigned>(LogisticsFailure::TargetInvalid);
}
bool resources_nonnegative(const Resources &value) {
    return value.wood >= 0 && value.stone >= 0 && value.food >= 0;
}
bool resources_at_most(const Resources &value, const Resources &limit) {
    return value.wood <= limit.wood && value.stone <= limit.stone && value.food <= limit.food;
}
bool resources_sum_at_most(const Resources &a, const Resources &b, const Resources &limit) {
    return std::int64_t(a.wood) + b.wood <= limit.wood && std::int64_t(a.stone) + b.stone <= limit.stone &&
           std::int64_t(a.food) + b.food <= limit.food;
}
bool resources_sum_equals(const Resources &a, const Resources &b, const Resources &total) {
    return std::int64_t(a.wood) + b.wood == total.wood && std::int64_t(a.stone) + b.stone == total.stone &&
           std::int64_t(a.food) + b.food == total.food;
}
std::unique_ptr<fb::Resources> pack_resources(const Resources &r) {
    return std::make_unique<fb::Resources>(r.wood, r.stone, r.food);
}
Resources unpack_resources(const fb::Resources *r) { return {r->wood(), r->stone(), r->food()}; }
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool climate_identity_equal(const WeatherView &a, const WeatherView &b) {
    return a.algorithm_version == b.algorithm_version && a.climate_profile == b.climate_profile &&
           a.scenario == b.scenario && a.calendar_offset_ticks == b.calendar_offset_ticks;
}
std::unique_ptr<fb::WeatherViewT> pack_weather(const WeatherView &value) {
    auto out = std::make_unique<fb::WeatherViewT>();
    out->known = value.known;
    out->algorithm_version = value.algorithm_version;
    out->climate_profile = static_cast<fb::ClimateProfileId>(value.climate_profile);
    out->scenario = static_cast<fb::ClimateScenarioId>(value.scenario);
    out->calendar_offset_ticks = value.calendar_offset_ticks;
    out->evaluated_simulation_tick = value.evaluated_simulation_tick;
    out->calendar_tick = value.calendar_tick;
    out->current = std::make_unique<fb::WeatherSampleT>();
    auto &sample = *out->current;
    sample.kind = static_cast<fb::WeatherKind>(value.current.kind);
    sample.precipitation = static_cast<fb::PrecipitationKind>(value.current.precipitation);
    sample.wind_direction = static_cast<fb::WindDirection>(value.current.wind_direction);
    sample.work_reason = static_cast<fb::WeatherWorkReason>(value.current.work_reason);
    sample.temperature_deci_c = value.current.temperature_deci_c;
    sample.precipitation_permille = value.current.precipitation_permille;
    sample.wind_permille = value.current.wind_permille;
    sample.outdoor_work_rate_bp = value.current.outdoor_work_rate_bp;
    sample.period_start_calendar_tick = value.current.period_start_calendar_tick;
    sample.period_end_calendar_tick = value.current.period_end_calendar_tick;
    out->snow_cover_permille = value.snow_cover_permille;
    out->wetness_permille = value.wetness_permille;
    out->wetness_remainder = value.wetness_remainder;
    out->next_storm_start_calendar_tick = value.next_storm_start_calendar_tick;
    out->next_storm_end_calendar_tick = value.next_storm_end_calendar_tick;
    out->forecast.reserve(value.forecast.size());
    for (const auto &card : value.forecast) {
        auto row = std::make_unique<fb::WeatherDayForecastT>();
        row->calendar_day = card.calendar_day;
        row->minimum_temperature_deci_c = card.minimum_temperature_deci_c;
        row->maximum_temperature_deci_c = card.maximum_temperature_deci_c;
        row->dominant_kind = static_cast<fb::WeatherKind>(card.dominant_kind);
        row->precipitation_summary = static_cast<fb::PrecipitationSummary>(card.precipitation_summary);
        row->has_storm = card.has_storm;
        row->intervals.reserve(card.interval_count);
        for (std::size_t i = 0; i < card.interval_count; ++i) {
            const auto &interval = card.intervals[i];
            row->intervals.emplace_back(interval.start_tick_in_day, interval.end_tick_in_day,
                                        static_cast<fb::WeatherKind>(interval.kind));
        }
        out->forecast.push_back(std::move(row));
    }
    return out;
}
WeatherView unpack_weather(const fb::WeatherView *value, std::uint64_t tick) {
    require(value && value->current() && value->forecast() && value->forecast()->size() == forecast_days,
            "Required four-card weather metadata missing");
    for (const auto *card : *value->forecast())
        require(card && card->intervals() && !card->intervals()->empty() &&
                    card->intervals()->size() <= max_forecast_intervals_per_day,
                "Weather interval count outside bounds");
    WeatherView out;
    out.known = value->known();
    out.algorithm_version = value->algorithm_version();
    out.climate_profile = static_cast<ClimateProfileId>(value->climate_profile());
    out.scenario = static_cast<ClimateScenarioId>(value->scenario());
    out.calendar_offset_ticks = value->calendar_offset_ticks();
    out.evaluated_simulation_tick = value->evaluated_simulation_tick();
    out.calendar_tick = value->calendar_tick();
    const auto *sample = value->current();
    out.current = {static_cast<WeatherKind>(sample->kind()),
                   static_cast<PrecipitationKind>(sample->precipitation()),
                   static_cast<WindDirection>(sample->wind_direction()),
                   static_cast<WeatherWorkReason>(sample->work_reason()), sample->temperature_deci_c(),
                   sample->precipitation_permille(), sample->wind_permille(), sample->outdoor_work_rate_bp(),
                   sample->period_start_calendar_tick(), sample->period_end_calendar_tick()};
    out.snow_cover_permille = value->snow_cover_permille();
    out.wetness_permille = value->wetness_permille();
    out.wetness_remainder = value->wetness_remainder();
    out.next_storm_start_calendar_tick = value->next_storm_start_calendar_tick();
    out.next_storm_end_calendar_tick = value->next_storm_end_calendar_tick();
    for (std::size_t i = 0; i < out.forecast.size(); ++i) {
        const auto *card = value->forecast()->Get(static_cast<flatbuffers::uoffset_t>(i));
        auto &row = out.forecast[i];
        row.calendar_day = card->calendar_day();
        row.minimum_temperature_deci_c = card->minimum_temperature_deci_c();
        row.maximum_temperature_deci_c = card->maximum_temperature_deci_c();
        row.dominant_kind = static_cast<WeatherKind>(card->dominant_kind());
        row.precipitation_summary = static_cast<PrecipitationSummary>(card->precipitation_summary());
        row.has_storm = card->has_storm();
        row.interval_count = static_cast<std::uint8_t>(card->intervals()->size());
        for (std::size_t p = 0; p < row.interval_count; ++p) {
            const auto *interval = card->intervals()->Get(static_cast<flatbuffers::uoffset_t>(p));
            row.intervals[p] = {interval->start_tick_in_day(), interval->end_tick_in_day(),
                                static_cast<WeatherKind>(interval->kind())};
        }
    }
    validate_weather_view(out, tick);
    return out;
}
std::unique_ptr<fb::ProfessionAssignmentT> pack_assignment(const ProfessionAssignment &value) {
    auto result = std::make_unique<fb::ProfessionAssignmentT>();
    result->profession = static_cast<fb::Profession>(value.profession);
    if (value.worksite)
        result->worksite = std::make_unique<fb::LayerCell>(value.worksite->x, value.worksite->y,
                                                           static_cast<std::uint8_t>(value.worksite->layer));
    result->priorities.assign(value.priorities.begin(), value.priorities.end());
    result->automation_enabled = value.automation_enabled;
    return result;
}
ProfessionAssignment unpack_assignment(const fb::ProfessionAssignment *value) {
    require(value && value->priorities()->size() == 5, "Invalid profession assignment size");
    ProfessionAssignment result;
    result.profession = static_cast<Profession>(value->profession());
    if (const auto site = value->worksite())
        result.worksite = CellCoord{site->x(), site->y(), static_cast<WorldLayer>(site->layer())};
    std::copy(value->priorities()->begin(), value->priorities()->end(), result.priorities.begin());
    result.automation_enabled = value->automation_enabled();
    require(World::valid_profession_assignment(result), "Invalid profession assignment");
    return result;
}
void validate_resident_outcomes(const CommandReply &reply) {
    require(reply.residents.size() <= max_path_batch_residents &&
                (reply.status != CommandReplyStatus::Queued || reply.residents.empty()),
            "Invalid resident command outcome count");
    std::set<int> ids;
    bool accepted = false;
    for (const auto &row : reply.residents) {
        require(row.resident_id > 0 && ids.insert(row.resident_id).second &&
                    static_cast<unsigned>(row.status) <=
                        static_cast<unsigned>(ResidentCommandStatus::Rejected) &&
                    path_failure_valid(row.failure) && row.order_id != UINT64_MAX &&
                    ((row.status == ResidentCommandStatus::Rejected) == (row.failure != PathFailure::None)),
                "Invalid resident command outcome");
        accepted |= row.status != ResidentCommandStatus::Rejected;
    }
    require(reply.residents.empty() || accepted == (reply.status == CommandReplyStatus::Applied),
            "Resident command outcomes disagree with reply");
    require(reply.control_rejections.size() <= reply.residents.size(), "Control rejection bounds");
    std::set<int> rejected;
    for (const auto &row : reply.control_rejections) {
        require(row.failure != ControlFailure::None &&
                    static_cast<unsigned>(row.failure) <= static_cast<unsigned>(ControlFailure::StaleEpoch) &&
                    rejected.insert(row.resident_id).second &&
                    std::any_of(reply.residents.begin(), reply.residents.end(),
                                [&](const auto &outcome) {
                                    return outcome.resident_id == row.resident_id &&
                                           outcome.status == ResidentCommandStatus::Rejected;
                                }),
                "Invalid control rejection");
    }
}
void validate_profession(const ResidentProfessionView &profile) {
    require(profile.resident_id > 0 && World::valid_profession_assignment(profile.assignment) &&
                static_cast<unsigned>(profile.wait_reason) <=
                    static_cast<unsigned>(ProfessionWaitReason::SelfGathering),
            "Invalid resident profession");
    for (std::size_t i = 0; i < profile.skills.size(); ++i) {
        const auto &skill = profile.skills[i];
        const auto &m = skill.modifier;
        require(static_cast<std::size_t>(skill.skill) == i && skill.experience >= 0 &&
                    skill.experience <= maximum_skill_experience && m.skill_sum_bp >= -1500 &&
                    m.skill_sum_bp <= 1500 && m.ability_sum_bp >= -1500 && m.ability_sum_bp <= 1500 &&
                    m.familiarity_bp >= -200 && m.familiarity_bp <= 0 &&
                    std::int64_t(m.skill_sum_bp) + m.ability_sum_bp + m.familiarity_bp == m.uncapped_bp &&
                    m.applied_bp == std::clamp(m.uncapped_bp, -1500, 1500) &&
                    m.speed_bp == 10000 + m.applied_bp && m.capped == (m.applied_bp != m.uncapped_bp),
                "Invalid resident skill modifier");
    }
}
std::unique_ptr<fb::VersionsT> pack_versions(const Versions &value) {
    auto out = std::make_unique<fb::VersionsT>();
    out->protocol = value.protocol;
    out->rules = value.rules;
    out->content = value.content;
    return out;
}
Versions unpack_versions(const fb::Versions *value) {
    require(value && hex64(value->content()->string_view()), "Invalid version metadata");
    return {value->protocol(), value->rules(), value->content()->str()};
}
std::unique_ptr<fb::ResidentT> pack_resident(const Resident &r) {
    auto out = std::make_unique<fb::ResidentT>();
    out->id = r.id;
    out->name = r.name;
    out->x = r.x;
    out->y = r.y;
    out->target_x = r.target_x;
    out->target_y = r.target_y;
    out->job = static_cast<std::uint8_t>(r.job);
    out->progress = r.progress;
    out->health = r.health;
    out->hunger = r.hunger;
    out->wood_xp = r.wood_xp;
    out->mining_xp = r.mining_xp;
    out->farming_xp = r.farming_xp;
    out->layer = static_cast<std::uint8_t>(r.layer);
    out->nation = r.nation;
    out->order_phase = static_cast<fb::OrderPhase>(r.order_phase);
    out->path_failure = static_cast<fb::PathFailure>(r.path_failure);
    out->pending_path_batch = r.pending_path_batch;
    return out;
}
Resident unpack_resident(const fb::Resident *r) {
    return {r->id(),
            r->name()->str(),
            r->x(),
            r->y(),
            r->target_x(),
            r->target_y(),
            static_cast<Job>(r->job()),
            r->progress(),
            r->health(),
            r->hunger(),
            r->wood_xp(),
            r->mining_xp(),
            r->farming_xp(),
            static_cast<WorldLayer>(r->layer()),
            r->nation(),
            static_cast<OrderPhase>(r->order_phase()),
            static_cast<PathFailure>(r->path_failure()),
            r->pending_path_batch()};
}
bool resident_equal(const Resident &a, const Resident &b) {
    return std::tie(a.id, a.name, a.x, a.y, a.target_x, a.target_y, a.job, a.progress, a.health, a.hunger,
                    a.wood_xp, a.mining_xp, a.farming_xp, a.layer, a.nation, a.order_phase, a.path_failure,
                    a.pending_path_batch) == std::tie(b.id, b.name, b.x, b.y, b.target_x, b.target_y, b.job,
                                                      b.progress, b.health, b.hunger, b.wood_xp, b.mining_xp,
                                                      b.farming_xp, b.layer, b.nation, b.order_phase,
                                                      b.path_failure, b.pending_path_batch);
}
bool building_equal(const Building &a, const Building &b) {
    return std::tie(a.id, a.kind, a.x, a.y, a.progress, a.layer, a.nation) ==
           std::tie(b.id, b.kind, b.x, b.y, b.progress, b.layer, b.nation);
}
void validate_snapshot(const Snapshot &s) {
    validate_weather_view(s.weather, s.tick);
    const auto calendar = calendar_view(CalendarTick{s.weather.calendar_tick});
    require(s.day == calendar.day && s.winter == (calendar.season == Season::Winter),
            "Snapshot day/season differs from authoritative weather");
    require(std::any_of(s.world_id.begin(), s.world_id.end(), [](auto b) { return b != 0; }),
            "Empty world identity");
    require(s.part_count >= 1 && s.part_count <= 256 && s.part_index < s.part_count,
            "Invalid snapshot part metadata");
    require(s.terrain_generator >= 1 && s.terrain_generator <= generator_version,
            "Invalid snapshot terrain generator");
    require(s.chunks.size() <= world_chunk_count && s.removed_chunks.size() <= world_chunk_count && s.residents.size() <= 6000 &&
                s.buildings.size() <= world_size * world_size,
            "Snapshot collection limit");
    require(s.removed_residents.size() <= 6000 && s.removed_buildings.size() <= world_size * world_size,
            "Removal collection limit");
    require(s.resident_logistics.size() <= 6000 && s.removed_resident_logistics.size() <= 6000 &&
                s.construction_sites.size() <= world_size * world_size &&
                s.removed_construction_sites.size() <= world_size * world_size &&
                s.resident_work.size() <= 6000 && s.removed_resident_work.size() <= 6000 &&
                s.resident_professions.size() <= 6000 && s.removed_resident_professions.size() <= 6000,
            "Logistics collection limit");
    require(!s.full || (!s.baseline && s.removed_chunks.empty() && s.removed_residents.empty() &&
                        s.removed_buildings.empty() && s.removed_resident_logistics.empty() &&
                        s.removed_construction_sites.empty() && s.removed_resident_work.empty() &&
                        s.removed_resident_professions.empty() && s.removed_storages.empty()),
            "Invalid full snapshot");
    require(s.full || s.revision > s.baseline, "Invalid delta revision");
    require(s.resources.wood >= 0 && s.resources.stone >= 0 && s.resources.food >= 0 && s.housing >= 0 &&
                s.day >= 1 && s.tutorial_step >= 0 && s.tutorial_step <= 3,
            "Invalid snapshot counters");
    require(s.inventory.total == s.resources && resources_nonnegative(s.inventory.total) &&
                resources_nonnegative(s.inventory.available) && resources_nonnegative(s.inventory.reserved) &&
                resources_nonnegative(s.inventory.stored) && resources_nonnegative(s.inventory.carried) &&
                resources_sum_equals(s.inventory.available, s.inventory.reserved, s.inventory.total) &&
                resources_sum_at_most(s.inventory.stored, s.inventory.carried, s.inventory.total),
            "Invalid inventory counters");
    require(s.storages.size() <= world_size * world_size &&
                s.removed_storages.size() <= world_size * world_size,
            "Storage collection limit");
    std::set<int> storage_ids;
    for (const auto &storage : s.storages)
        require(storage.building_id > 0 && storage.capacity > 0 && storage.used <= storage.capacity &&
                    storage_ids.insert(storage.building_id).second,
                "Invalid or duplicate storage");
    for (const auto id : s.removed_storages)
        require(id > 0 && storage_ids.insert(id).second, "Invalid storage removal");
    std::set<ChunkKey> keys;
    for (const auto &chunk : s.chunks) {
        require(key_valid(chunk.key) && keys.insert(chunk.key).second && chunk.tiles.size() <= 1024,
                "Invalid or duplicate chunk");
        std::set<std::uint32_t> tiles;
        for (const auto &change : chunk.tiles) {
            require(change.index < 1024 && tiles.insert(change.index).second,
                    "Invalid or duplicate tile index");
            require(static_cast<unsigned>(change.tile.terrain) <= static_cast<unsigned>(Terrain::MineOre) &&
                        change.tile.amount >= 0 && change.tile.amount <= 1000000,
                    "Invalid tile");
            require(static_cast<unsigned>(change.tile.biome) <= 8, "Invalid biome");
            require(change.tile.elevation >= -32 && change.tile.elevation <= 512 &&
                        (chunk.key.layer != WorldLayer::Mine ||
                         (change.tile.elevation == 0 && change.tile.trail_wear == 0)), "Invalid elevation");
            require(change.tile.explored ||
                        (change.tile.terrain == Terrain::Grass && change.tile.amount == 0 &&
                         change.tile.biome == Biome::Meadow && change.tile.elevation == 0 &&
                         change.tile.trail_wear == 0),
                    "Hidden tile contains private data");
        }
    }
    for (const auto &key : s.removed_chunks)
        require(key_valid(key) && keys.insert(key).second, "Invalid chunk removal");
    require(s.portals.size() <= 64, "Portal collection limit");
    std::set<std::pair<int, int>> surface_portals, mine_portals;
    for (const auto &portal : s.portals)
        require(portal.surface.layer == WorldLayer::Surface && portal.mine.layer == WorldLayer::Mine &&
                    coordinate(portal.surface.x) && coordinate(portal.surface.y) &&
                    coordinate(portal.mine.x) && coordinate(portal.mine.y) &&
                    surface_portals.emplace(portal.surface.x, portal.surface.y).second &&
                    mine_portals.emplace(portal.mine.x, portal.mine.y).second,
                "Invalid portal");
    std::set<int> residents, buildings;
    for (const auto &r : s.residents) {
        require(r.id > 0 && residents.insert(r.id).second && valid_utf8_text(r.name, 128),
                "Invalid resident identity");
        require(std::isfinite(r.x) && std::isfinite(r.y) && r.x >= world_min && r.y >= world_min &&
                    r.x < world_max && r.y < world_max && coordinate(r.target_x) && coordinate(r.target_y) &&
                    layer_valid(r.layer) && r.nation > 0,
                "Invalid resident position");
        require(static_cast<unsigned>(r.job) <= static_cast<unsigned>(Job::Farm) &&
                    std::isfinite(r.progress) && r.progress >= 0 && r.progress <= 1 &&
                    std::isfinite(r.hunger) && r.hunger >= 0 && r.hunger <= 10 && r.health >= 0 &&
                    r.health <= 100,
                "Invalid resident state");
        require(r.wood_xp >= 0 && r.mining_xp >= 0 && r.farming_xp >= 0, "Invalid experience");
        require(static_cast<unsigned>(r.order_phase) <= static_cast<unsigned>(OrderPhase::PathFailed) &&
                    path_failure_valid(r.path_failure) && r.pending_path_batch == 0 &&
                    (r.order_phase != OrderPhase::PathFailed || r.path_failure != PathFailure::None),
                "Invalid resident path state");
    }
    for (const auto &b : s.buildings)
        require(b.id > 0 && buildings.insert(b.id).second && coordinate(b.x) && coordinate(b.y) &&
                    valid_building_kind(b.kind) && std::isfinite(b.progress) && b.progress >= 0 &&
                    b.progress <= 1 && layer_valid(b.layer) && b.nation > 0,
                "Invalid building");
    std::set<int> removed;
    for (int id : s.removed_residents)
        require(id > 0 && !residents.contains(id) && removed.insert(id).second, "Invalid resident removal");
    removed.clear();
    for (int id : s.removed_buildings)
        require(id > 0 && !buildings.contains(id) && removed.insert(id).second, "Invalid building removal");
    std::set<int> carriers, sites;
    for (const auto &r : s.resident_logistics) {
        require(r.resident_id > 0 && carriers.insert(r.resident_id).second &&
                    static_cast<unsigned>(r.item) < economy_item_count &&
                    r.capacity == resident_carry_capacity && r.quantity <= r.capacity &&
                    static_cast<unsigned>(r.phase) <= static_cast<unsigned>(LogisticsPhase::Failed) &&
                    logistics_failure_valid(r.failure) &&
                    ((r.phase == LogisticsPhase::Failed) == (r.failure != LogisticsFailure::None)),
                "Invalid resident logistics");
    }
    for (const auto &site : s.construction_sites) {
        require(site.building_id > 0 && sites.insert(site.building_id).second &&
                    static_cast<unsigned>(site.kind) >= static_cast<unsigned>(BuildingKind::House) &&
                    valid_building_kind(site.kind) &&
                    coordinate(site.site.x) && coordinate(site.site.y) && layer_valid(site.site.layer) &&
                    site.required == World::cost(site.kind) && resources_nonnegative(site.delivered) &&
                    resources_at_most(site.delivered, site.required) && std::isfinite(site.work) &&
                    site.work >= 0 && site.work <= 1 &&
                    static_cast<unsigned>(site.phase) <=
                        static_cast<unsigned>(ConstructionPhase::Cancelled) &&
                    logistics_failure_valid(site.failure) &&
                    ((site.phase != ConstructionPhase::Work && site.phase != ConstructionPhase::Complete) ||
                     site.delivered == site.required),
                "Invalid construction delivery");
        require(
            static_cast<unsigned>(site.worker_phase) <=
                    static_cast<unsigned>(ConstructionWorkerPhase::Planning) &&
                path_failure_valid(site.worker_failure) && site.assigned_workers <= 6000 &&
                ((site.phase == ConstructionPhase::Work) ==
                 (site.worker_phase != ConstructionWorkerPhase::None)) &&
                ((site.worker_phase == ConstructionWorkerPhase::Planning ||
                  site.worker_phase == ConstructionWorkerPhase::Travelling ||
                  site.worker_phase == ConstructionWorkerPhase::Working) == (site.assigned_workers != 0)) &&
                (site.worker_failure == PathFailure::None ||
                 site.worker_phase == ConstructionWorkerPhase::Waiting),
            "Invalid construction workforce");
    }
    std::set<int> work_residents;
    for (const auto &work : s.resident_work) {
        require(work.resident_id > 0 && work_residents.insert(work.resident_id).second &&
                    static_cast<unsigned>(work.kind) <=
                        static_cast<unsigned>(WorkIntentKind::BuildAssignment) &&
                    static_cast<unsigned>(work.phase) <= static_cast<unsigned>(WorkIntentPhase::Failed) &&
                    path_failure_valid(work.failure) &&
                    static_cast<unsigned>(work.completion) <=
                        static_cast<unsigned>(WorkCompletionReason::Cancelled) &&
                    work.attempts <= max_work_admission_attempts &&
                    ((work.phase == WorkIntentPhase::Failed) == (work.failure != PathFailure::None)) &&
                    ((work.phase == WorkIntentPhase::Complete) ==
                     (work.completion != WorkCompletionReason::None)) &&
                    (work.phase != WorkIntentPhase::PathPending || work.attempts != 0) &&
                    (work.completion != WorkCompletionReason::Exhausted ||
                     work.kind == WorkIntentKind::GatherContinuation),
                "Invalid resident work intent");
    }
    removed.clear();
    for (int id : s.removed_resident_logistics)
        require(id > 0 && !carriers.contains(id) && removed.insert(id).second,
                "Invalid resident logistics removal");
    removed.clear();
    for (int id : s.removed_construction_sites)
        require(id > 0 && !sites.contains(id) && removed.insert(id).second,
                "Invalid construction site removal");
    removed.clear();
    for (int id : s.removed_resident_work)
        require(id > 0 && !work_residents.contains(id) && removed.insert(id).second,
                "Invalid resident work removal");
    std::set<int> profiles;
    for (const auto &profile : s.resident_professions) {
        validate_profession(profile);
        require(profiles.insert(profile.resident_id).second, "Duplicate resident profession");
    }
    removed.clear();
    for (int id : s.removed_resident_professions)
        require(id > 0 && !profiles.contains(id) && removed.insert(id).second,
                "Invalid resident profession removal");
    require(s.manual_orders.size() <= max_manual_orders &&
                s.removed_manual_orders.size() <= max_manual_orders && s.resident_controls.size() <= 6000 &&
                s.removed_resident_controls.size() <= 6000 && s.control_players.size() <= 4,
            "Manual/control snapshot bounds");
    require(!s.full || (s.removed_manual_orders.empty() && s.removed_resident_controls.empty()),
            "Full snapshot contains manual/control removals");
    require(valid_economy(s.economy), "Invalid economy snapshot");
    require(valid_settlement(s.settlement, s.tick, calendar.tick), "Invalid settlement snapshot");
    require(s.control_epoch ||
                (s.manual_orders.empty() && s.resident_controls.empty() && s.control_players.empty()),
            "Missing control epoch");
    std::set<std::uint64_t> orders, removed_orders;
    for (const auto &order : s.manual_orders) {
        const bool cancelled = order.completion == ManualOrderCompletion::Replaced ||
                               order.completion == ManualOrderCompletion::Stopped ||
                               order.completion == ManualOrderCompletion::Released ||
                               order.completion == ManualOrderCompletion::Cancelled;
        const bool completed =
            (order.completion == ManualOrderCompletion::Arrived && order.kind == ManualOrderKind::Move) ||
            (order.completion == ManualOrderCompletion::Delivered && order.kind == ManualOrderKind::Gather &&
             order.gather_completion == GatherCompletion::OneDelivery) ||
            (order.completion == ManualOrderCompletion::Exhausted && order.kind == ManualOrderKind::Gather) ||
            // Legacy compatibility/provenance remains private to SimCore.
            order.completion == ManualOrderCompletion::LegacyFinished;
        require(
            order.order_id && order.order_id != UINT64_MAX && order.resident_id > 0 &&
                orders.insert(order.order_id).second &&
                static_cast<unsigned>(order.kind) <= static_cast<unsigned>(ManualOrderKind::Legacy) &&
                static_cast<unsigned>(order.gather_completion) <=
                    static_cast<unsigned>(GatherCompletion::OneDelivery) &&
                static_cast<unsigned>(order.phase) <= static_cast<unsigned>(ManualOrderPhase::Cancelled) &&
                static_cast<unsigned>(order.completion) <=
                    static_cast<unsigned>(ManualOrderCompletion::Cancelled) &&
                path_failure_valid(order.failure) &&
                ((order.phase == ManualOrderPhase::Failed) == (order.failure != PathFailure::None)) &&
                ((order.phase == ManualOrderPhase::Cancelled) == cancelled) &&
                ((order.phase == ManualOrderPhase::Complete) == completed) &&
                (order.phase == ManualOrderPhase::Cancelled || order.phase == ManualOrderPhase::Complete ||
                 order.completion == ManualOrderCompletion::None) &&
                (order.kind == ManualOrderKind::Gather ||
                 order.gather_completion == GatherCompletion::UntilExhausted) &&
                (!order.target || (coordinate(order.target->x) && coordinate(order.target->y) &&
                                   layer_valid(order.target->layer))),
            "Invalid manual order");
    }
    for (auto order : s.removed_manual_orders)
        require(order && !orders.contains(order) && removed_orders.insert(order).second,
                "Invalid manual order removal");
    std::set<int> controls;
    for (const auto &control : s.resident_controls)
        require(control.resident_id > 0 && controls.insert(control.resident_id).second &&
                    (control.holder_id ? (valid_utf8_text(control.holder_name, 64) && control.revision != 0)
                                       : (control.holder_name.empty() && !control.held_by_recipient &&
                                          !control.disconnected_grace)),
                "Invalid resident control");
    removed.clear();
    for (int id : s.removed_resident_controls)
        require(id > 0 && !controls.contains(id) && removed.insert(id).second,
                "Invalid resident control removal");
    std::set<PlayerId> players;
    for (const auto &player : s.control_players)
        require(player.player_id && players.insert(player.player_id).second &&
                    valid_utf8_text(player.name, 64),
                "Invalid control player");
}
} // namespace

std::vector<std::uint8_t> encode(const Packet &value) {
    fb::PacketT p;
    p.protocol = protocol_version;
    p.sequence = value.sequence;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Hello>) {
                fb::HelloT x;
                x.name = v.name;
                x.versions = pack_versions(v.versions);
                x.resume_token = v.resume_token;
                x.password_proof = v.password_proof;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, Challenge>) {
                fb::ChallengeT x;
                x.nonce = v.nonce;
                x.password_required = v.password_required;
                x.versions = pack_versions(v.versions);
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, Welcome>) {
                fb::WelcomeT x;
                x.player = v.player;
                x.resume_token = v.resume_token;
                x.world_id.assign(v.world_id.begin(), v.world_id.end());
                x.next_request = v.next_request;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, Rejected>) {
                fb::RejectedT x;
                x.reason = v.reason;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, GameCommand>) {
                fb::CommandT x;
                x.request_id = v.request_id;
                x.author = v.author;
                x.issued_tick = v.issued_tick;
                x.kind = static_cast<fb::CommandType>(v.type);
                x.residents = v.residents;
                x.x = v.x;
                x.y = v.y;
                x.building = static_cast<std::uint8_t>(v.building);
                x.layer = static_cast<std::uint8_t>(v.layer);
                x.building_id = v.building_id;
                x.manual_queue_mode = static_cast<fb::ManualQueueMode>(v.manual_queue_mode);
                x.gather_completion = static_cast<fb::GatherCompletion>(v.gather_completion);
                x.manual_order_id = v.manual_order_id;
                x.target_player = v.target_player;
                x.control_epoch = v.control_epoch;
                x.settlement_revision = v.settlement_revision;
                if (v.type == CommandType::AssignProfession || v.assignment != ProfessionAssignment{})
                    x.assignment = pack_assignment(v.assignment);
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, CommandReply>) {
                validate_resident_outcomes(v);
                require(v.request_id != 0 && v.author != 0 && reply_status_valid(v.status) &&
                            path_failure_valid(v.failure) &&
                            (v.status != CommandReplyStatus::Queued ||
                             (v.applied_tick == 0 && v.failure == PathFailure::None)) &&
                            (v.status != CommandReplyStatus::Applied || v.failure == PathFailure::None) &&
                            valid_utf8_text(v.reason, 512),
                        "Invalid reply");
                fb::ReplyT x;
                x.request_id = v.request_id;
                x.applied_tick = v.applied_tick;
                x.author = v.author;
                x.accepted = v.status == CommandReplyStatus::Applied;
                x.reason = v.reason;
                x.status = static_cast<fb::CommandReplyStatus>(v.status);
                x.failure = static_cast<fb::PathFailure>(v.failure);
                for (const auto &row : v.residents) {
                    x.residents.emplace_back(row.resident_id,
                                             static_cast<fb::ResidentCommandStatus>(row.status),
                                             static_cast<fb::PathFailure>(row.failure));
                    x.resident_order_ids.push_back(row.order_id);
                }
                for (const auto &row : v.control_rejections)
                    x.control_rejections.emplace_back(row.resident_id,
                                                      static_cast<fb::ControlFailure>(row.failure));
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, Snapshot>) {
                validate_snapshot(v);
                fb::SnapshotT x;
                x.world_id.assign(v.world_id.begin(), v.world_id.end());
                x.revision = v.revision;
                x.baseline = v.baseline;
                x.tick = v.tick;
                x.full = v.full;
                x.terrain_generator = v.terrain_generator;
                for (const auto &c : v.chunks) {
                    auto patch = std::make_unique<fb::ChunkPatchT>();
                    patch->key = std::make_unique<fb::ChunkKey>(c.key.cx, c.key.cy,
                                                                static_cast<std::uint8_t>(c.key.layer));
                    patch->revision = c.revision;
                    int previous_height = 0;
                    for (const auto &t : c.tiles) {
                        // Smooth neighboring heights compress better as exact deltas.
                        patch->tiles.emplace_back(static_cast<std::uint16_t>(t.index),
                                                  static_cast<std::int16_t>(t.tile.elevation - previous_height),
                                                  t.tile.amount, static_cast<std::uint8_t>(t.tile.terrain),
                                                  t.tile.explored, static_cast<std::uint8_t>(t.tile.biome),
                                                  t.tile.trail_wear);
                        previous_height = t.tile.elevation;
                    }
                    x.chunks.push_back(std::move(patch));
                }
                for (const auto &key : v.removed_chunks)
                    x.removed_chunks.emplace_back(key.cx, key.cy, static_cast<std::uint8_t>(key.layer));
                for (const auto &r : v.residents)
                    x.residents.push_back(pack_resident(r));
                for (const auto &b : v.buildings) {
                    auto building = std::make_unique<fb::LayeredBuildingT>();
                    building->id = b.id;
                    building->x = b.x;
                    building->y = b.y;
                    building->progress = b.progress;
                    building->kind = static_cast<std::uint8_t>(b.kind);
                    building->layer = static_cast<std::uint8_t>(b.layer);
                    building->nation = b.nation;
                    x.layered_buildings.push_back(std::move(building));
                }
                x.part_index = v.part_index;
                x.part_count = v.part_count;
                for (const auto &portal : v.portals) {
                    auto item = std::make_unique<fb::PortalT>();
                    item->surface = std::make_unique<fb::LayerCell>(
                        portal.surface.x, portal.surface.y, static_cast<std::uint8_t>(portal.surface.layer));
                    item->mine = std::make_unique<fb::LayerCell>(
                        portal.mine.x, portal.mine.y, static_cast<std::uint8_t>(portal.mine.layer));
                    x.portals.push_back(std::move(item));
                }
                x.removed_residents = v.removed_residents;
                x.removed_buildings = v.removed_buildings;
                x.resources =
                    std::make_unique<fb::Resources>(v.resources.wood, v.resources.stone, v.resources.food);
                x.inventory = std::make_unique<fb::InventorySummaryT>();
                x.inventory->total = pack_resources(v.inventory.total);
                x.inventory->available = pack_resources(v.inventory.available);
                x.inventory->reserved = pack_resources(v.inventory.reserved);
                x.inventory->stored = pack_resources(v.inventory.stored);
                x.inventory->carried = pack_resources(v.inventory.carried);
                for (const auto &storage : v.storages) {
                    auto item = std::make_unique<fb::StorageViewT>();
                    item->building_id = storage.building_id;
                    item->used = storage.used;
                    item->capacity = storage.capacity;
                    x.storages.push_back(std::move(item));
                }
                x.removed_storages = v.removed_storages;
                for (const auto &r : v.resident_logistics) {
                    auto item = std::make_unique<fb::ResidentLogisticsT>();
                    item->resident_id = r.resident_id;
                    item->item = static_cast<fb::ItemKind>(r.item);
                    item->quantity = r.quantity;
                    item->capacity = r.capacity;
                    item->phase = static_cast<fb::LogisticsPhase>(r.phase);
                    item->failure = static_cast<fb::LogisticsFailure>(r.failure);
                    x.resident_logistics.push_back(std::move(item));
                }
                for (const auto &site : v.construction_sites) {
                    auto item = std::make_unique<fb::ConstructionSiteT>();
                    item->building_id = site.building_id;
                    item->kind = static_cast<std::uint8_t>(site.kind);
                    item->site = std::make_unique<fb::LayerCell>(site.site.x, site.site.y,
                                                                 static_cast<std::uint8_t>(site.site.layer));
                    item->required = pack_resources(site.required);
                    item->delivered = pack_resources(site.delivered);
                    item->work = site.work;
                    item->phase = static_cast<fb::ConstructionPhase>(site.phase);
                    item->failure = static_cast<fb::LogisticsFailure>(site.failure);
                    item->worker_phase = static_cast<fb::ConstructionWorkerPhase>(site.worker_phase);
                    item->worker_failure = static_cast<fb::PathFailure>(site.worker_failure);
                    item->assigned_workers = site.assigned_workers;
                    x.construction_sites.push_back(std::move(item));
                }
                for (const auto &work : v.resident_work) {
                    auto item = std::make_unique<fb::ResidentWorkT>();
                    item->resident_id = work.resident_id;
                    item->kind = static_cast<fb::WorkIntentKind>(work.kind);
                    item->phase = static_cast<fb::WorkIntentPhase>(work.phase);
                    item->failure = static_cast<fb::PathFailure>(work.failure);
                    item->completion = static_cast<fb::WorkCompletionReason>(work.completion);
                    item->attempts = work.attempts;
                    x.resident_work.push_back(std::move(item));
                }
                for (const auto &profile : v.resident_professions) {
                    auto item = std::make_unique<fb::ResidentProfessionT>();
                    item->resident_id = profile.resident_id;
                    item->assignment = pack_assignment(profile.assignment);
                    item->manual_override = profile.manual_override;
                    item->deferred = profile.deferred;
                    item->wait_reason = static_cast<fb::ProfessionWaitReason>(profile.wait_reason);
                    for (const auto &skill : profile.skills) {
                        auto entry = std::make_unique<fb::ResidentSkillT>();
                        entry->skill = static_cast<fb::Skill>(skill.skill);
                        entry->experience = skill.experience;
                        const auto &m = skill.modifier;
                        entry->modifier = std::make_unique<fb::ModifierBreakdown>(
                            m.skill_sum_bp, m.ability_sum_bp, m.familiarity_bp, m.uncapped_bp, m.applied_bp,
                            m.speed_bp, m.capped);
                        item->skills.push_back(std::move(entry));
                    }
                    x.resident_professions.push_back(std::move(item));
                }
                x.removed_resident_professions = v.removed_resident_professions;
                for (const auto &order : v.manual_orders) {
                    auto item = std::make_unique<fb::ManualOrderT>();
                    item->order_id = order.order_id;
                    item->resident_id = order.resident_id;
                    item->kind = static_cast<fb::ManualOrderKind>(order.kind);
                    if (order.target)
                        item->target = std::make_unique<fb::LayerCell>(
                            order.target->x, order.target->y, static_cast<std::uint8_t>(order.target->layer));
                    item->gather_completion = static_cast<fb::GatherCompletion>(order.gather_completion);
                    item->phase = static_cast<fb::ManualOrderPhase>(order.phase);
                    item->failure = static_cast<fb::PathFailure>(order.failure);
                    item->completion = static_cast<fb::ManualOrderCompletion>(order.completion);
                    x.manual_orders.push_back(std::move(item));
                }
                for (const auto &control : v.resident_controls) {
                    auto item = std::make_unique<fb::ResidentControlT>();
                    item->resident_id = control.resident_id;
                    item->holder_id = control.holder_id;
                    item->holder_name = control.holder_name;
                    item->held_by_recipient = control.held_by_recipient;
                    item->disconnected_grace = control.disconnected_grace;
                    item->revision = control.revision;
                    x.resident_controls.push_back(std::move(item));
                }
                for (const auto &player : v.control_players) {
                    auto item = std::make_unique<fb::ControlPlayerT>();
                    item->player_id = player.player_id;
                    item->name = player.name;
                    x.control_players.push_back(std::move(item));
                }
                x.removed_manual_orders = v.removed_manual_orders;
                x.removed_resident_controls = v.removed_resident_controls;
                x.control_epoch = v.control_epoch;
                x.removed_resident_logistics = v.removed_resident_logistics;
                x.removed_construction_sites = v.removed_construction_sites;
                x.removed_resident_work = v.removed_resident_work;
                x.housing = v.housing;
                x.day = v.day;
                x.tutorial_step = v.tutorial_step;
                x.winter = v.winter;
                x.weather = pack_weather(v.weather);
                x.settlement = pack_settlement(v.settlement);
                x.economy=encode_economy(v.economy);
                x.lobby=encode_lobby(v.lobby);
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, SnapshotAck>) {
                fb::SnapshotAckT x;
                x.revision = v.revision;
                x.resync = v.resync;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, ChatSend>) {
                fb::ChatSendT x;
                x.text = v.text;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, ChatEntry>) {
                fb::ChatT x;
                x.sequence = v.sequence;
                x.author = v.author;
                x.name = v.name;
                x.text = v.text;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, Ping>) {
                fb::PingT x;
                x.value = v.value;
                p.payload.Set(std::move(x));
            } else if constexpr (std::is_same_v<T, ReplyAck>) {
                require(!v.receipts.empty() && v.receipts.size() <= 256, "Reply acknowledgement batch limit");
                fb::ReplyAckT x;
                std::set<ReplyReceipt> receipts;
                for (const auto &receipt : v.receipts) {
                    require(receipt.request_id != 0 && reply_status_valid(receipt.status) &&
                                receipts.insert(receipt).second,
                            "Invalid reply acknowledgement");
                    x.receipts.emplace_back(receipt.request_id,
                                            static_cast<fb::CommandReplyStatus>(receipt.status));
                }
                p.payload.Set(std::move(x));
            }
        },
        value.payload);
    flatbuffers::FlatBufferBuilder inner;
    fb::FinishPacketBuffer(inner, fb::Packet::Pack(inner, &p));
    require(inner.GetSize() <= max_expanded_bytes, "Expanded packet too large");
    fb::FrameT frame;
    frame.protocol = protocol_version;
    frame.expanded_size = static_cast<std::uint32_t>(inner.GetSize());
    frame.data.assign(inner.GetBufferPointer(), inner.GetBufferPointer() + inner.GetSize());
    frame.content_sha256 = sha256(frame.data);
    if (frame.data.size() > 4096) {
        std::vector<std::uint8_t> compressed(ZSTD_compressBound(frame.data.size()));
        auto size =
            ZSTD_compress(compressed.data(), compressed.size(), frame.data.data(), frame.data.size(), 3);
        if (!ZSTD_isError(size) && size < frame.data.size()) {
            compressed.resize(size);
            frame.data = std::move(compressed);
            frame.compression = fb::Compression::Zstd;
        }
    }
    flatbuffers::FlatBufferBuilder outer;
    fb::FinishFrameBuffer(outer, fb::Frame::Pack(outer, &frame));
    require(outer.GetSize() <= max_packet_bytes, "Wire packet too large");
    return {outer.GetBufferPointer(), outer.GetBufferPointer() + outer.GetSize()};
}

std::optional<Packet> decode(std::span<const std::uint8_t> bytes, std::string &error,
                             std::size_t expanded_limit) {
    error.clear();
    try {
        require(bytes.size() >= 16 && bytes.size() <= max_packet_bytes, "Wire packet bounds");
        flatbuffers::Verifier outer(bytes.data(), bytes.size(), 32, 20000);
        require(fb::VerifyFrameBuffer(outer), "Malformed network frame");
        auto frame = fb::GetFrame(bytes.data());
        require(frame->protocol() == protocol_version, "Frame protocol mismatch");
        require(frame->expanded_size() >= 16 &&
                    frame->expanded_size() <= std::min(expanded_limit, max_expanded_bytes),
                "Expanded packet bounds");
        std::vector<std::uint8_t> expanded(frame->expanded_size());
        if (frame->compression() == fb::Compression::None) {
            require(frame->data()->size() == expanded.size(), "Invalid frame length");
            std::copy(frame->data()->begin(), frame->data()->end(), expanded.begin());
        } else if (frame->compression() == fb::Compression::Zstd) {
            auto size = ZSTD_decompress(expanded.data(), expanded.size(), frame->data()->data(),
                                        frame->data()->size());
            require(!ZSTD_isError(size) && size == expanded.size(), "Invalid compressed frame");
        } else
            require(false, "Unknown compression");
        require(hex64(frame->content_sha256()->string_view()) &&
                    constant_time_equal(frame->content_sha256()->string_view(), sha256(expanded)),
                "Frame content checksum mismatch");
        flatbuffers::Verifier verifier(expanded.data(), expanded.size(), 32, 20000);
        require(fb::VerifyPacketBuffer(verifier), "Malformed network packet");
        auto p = fb::GetPacket(expanded.data());
        require(p->protocol() == protocol_version, "Packet protocol mismatch");
        Packet out;
        out.sequence = p->sequence();
        switch (p->payload_type()) {
        case fb::Payload::Hello: {
            auto v = p->payload_as_Hello();
            Hello x;
            x.name = v->name()->str();
            x.versions = unpack_versions(v->versions());
            if (v->resume_token())
                x.resume_token = v->resume_token()->str();
            if (v->password_proof())
                x.password_proof = v->password_proof()->str();
            require(valid_utf8_text(x.name, 64) && (x.resume_token.empty() || hex64(x.resume_token)) &&
                        (x.password_proof.empty() || hex64(x.password_proof)),
                    "Invalid hello");
            out.payload = std::move(x);
            break;
        }
        case fb::Payload::Challenge: {
            auto v = p->payload_as_Challenge();
            require(hex64(v->nonce()->string_view()), "Invalid nonce");
            out.payload =
                Challenge{v->nonce()->str(), v->password_required(), unpack_versions(v->versions())};
            break;
        }
        case fb::Payload::Welcome: {
            auto v = p->payload_as_Welcome();
            require(v->player() != 0 && v->next_request() != 0 && hex64(v->resume_token()->string_view()) &&
                        v->world_id()->size() == 16,
                    "Invalid welcome");
            Welcome x{v->player(), v->resume_token()->str()};
            x.next_request = v->next_request();
            std::copy(v->world_id()->begin(), v->world_id()->end(), x.world_id.begin());
            out.payload = std::move(x);
            break;
        }
        case fb::Payload::Rejected: {
            auto v = p->payload_as_Rejected();
            require(valid_utf8_text(v->reason()->string_view(), 512), "Invalid rejection");
            out.payload = Rejected{v->reason()->str()};
            break;
        }
        case fb::Payload::Command: {
            auto v = p->payload_as_Command();
            require(v->request_id() != 0 && v->request_id() != UINT64_MAX && v->author() != 0 &&
                        static_cast<unsigned>(v->kind()) <=
                            static_cast<unsigned>(CommandType::Lobby) &&
                        v->building() < building_kind_count && coordinate(v->x()) && coordinate(v->y()) && v->layer() <= 1 &&
                        (!v->residents() || v->residents()->size() <= 1500),
                    "Invalid command");
            GameCommand x{v->request_id(),
                          v->author(),
                          v->issued_tick(),
                          static_cast<CommandType>(v->kind()),
                          {},
                          v->x(),
                          v->y(),
                          static_cast<BuildingKind>(v->building())};
            if (v->residents())
                x.residents.assign(v->residents()->begin(), v->residents()->end());
            x.layer = static_cast<WorldLayer>(v->layer());
            x.building_id = v->building_id();
            x.manual_queue_mode = static_cast<ManualQueueMode>(v->manual_queue_mode());
            x.gather_completion = static_cast<GatherCompletion>(v->gather_completion());
            x.manual_order_id = v->manual_order_id();
            x.target_player = v->target_player();
            x.control_epoch = v->control_epoch();
            x.settlement_revision = v->settlement_revision();
            require(static_cast<unsigned>(x.manual_queue_mode) <=
                            static_cast<unsigned>(ManualQueueMode::Append) &&
                        static_cast<unsigned>(x.gather_completion) <=
                            static_cast<unsigned>(GatherCompletion::OneDelivery) &&
                        x.manual_order_id != UINT64_MAX &&
                        ((x.type == CommandType::CancelManualOrder) == (x.manual_order_id != 0)) &&
                        ((x.type == CommandType::TransferControl) == (x.target_player != 0)) &&
                        (x.type != CommandType::CancelManualOrder || x.residents.empty()) &&
                        (x.type == CommandType::Move || x.type == CommandType::Gather ||
                         x.manual_queue_mode == ManualQueueMode::Replace) &&
                        (x.type == CommandType::Gather ||
                         x.gather_completion == GatherCompletion::UntilExhausted),
                    "Invalid manual control command");
            if (v->assignment())
                x.assignment = unpack_assignment(v->assignment());
            require((x.type == CommandType::AssignProfession) == (v->assignment() != nullptr),
                    "Unexpected profession assignment");
            require((x.type != CommandType::AssignProfession && x.type != CommandType::Stop) ||
                        (!x.residents.empty() && x.x == 0 && x.y == 0 && x.layer == WorldLayer::Surface &&
                         x.building == BuildingKind::House),
                    "Invalid profession command");
            require((x.type == CommandType::CancelConstruction && x.building_id > 0 && x.residents.empty()) ||
                        (x.type >= CommandType::Production && x.type<=CommandType::CancelProduction && x.building_id > 0) || ((x.type==CommandType::Wilderness || x.type==CommandType::Lobby) && x.building_id>=0) ||
                        (x.type != CommandType::CancelConstruction && x.type < CommandType::Production && x.building_id == 0),
                    "Invalid construction command");
            std::set<int> ids;
            for (int id : x.residents)
                require(id > 0 && ids.insert(id).second, "Invalid or duplicate resident ID");
            out.payload = std::move(x);
            break;
        }
        case fb::Payload::Reply: {
            auto v = p->payload_as_Reply();
            const auto status = static_cast<CommandReplyStatus>(v->status());
            const auto failure = static_cast<PathFailure>(v->failure());
            require(v->request_id() != 0 && v->author() != 0 && reply_status_valid(status) &&
                        path_failure_valid(failure) &&
                        v->accepted() == (status == CommandReplyStatus::Applied) &&
                        (status != CommandReplyStatus::Queued ||
                         (v->applied_tick() == 0 && failure == PathFailure::None)) &&
                        (status != CommandReplyStatus::Applied || failure == PathFailure::None) &&
                        v->reason()->size() <= 512 &&
                        (v->reason()->size() == 0 || valid_utf8_text(v->reason()->string_view(), 512)),
                    "Invalid reply");
            CommandReply reply{v->request_id(), v->applied_tick(), v->author(),
                               status,          failure,           v->reason()->str()};
            require(!v->residents() || v->residents()->size() <= max_path_batch_residents,
                    "Resident command outcome bounds");
            if (v->residents())
                for (const auto row : *v->residents())
                    reply.residents.push_back({row->resident_id(),
                                               static_cast<ResidentCommandStatus>(row->status()),
                                               static_cast<PathFailure>(row->failure())});
            require((v->resident_order_ids() ? v->resident_order_ids()->size() : 0) == reply.residents.size(),
                    "Resident order identity bounds");
            for (std::size_t i = 0; i < reply.residents.size(); ++i)
                reply.residents[i].order_id =
                    v->resident_order_ids()->Get(static_cast<flatbuffers::uoffset_t>(i));
            require(!v->control_rejections() || v->control_rejections()->size() <= max_path_batch_residents,
                    "Control rejection bounds");
            if (v->control_rejections())
                for (const auto row : *v->control_rejections())
                    reply.control_rejections.push_back(
                        {row->resident_id(), static_cast<ControlFailure>(row->failure())});
            validate_resident_outcomes(reply);
            out.payload = std::move(reply);
            break;
        }
        case fb::Payload::Snapshot: {
            auto v = p->payload_as_Snapshot();
            require(
                v->world_id()->size() == 16 && v->tiles()->size() == 0 && v->residents()->size() <= 6000 &&
                    v->buildings()->size() == 0 && (!v->chunks() || v->chunks()->size() <= world_chunk_count) &&
                    (!v->removed_chunks() || v->removed_chunks()->size() <= world_chunk_count) &&
                    (!v->layered_buildings() || v->layered_buildings()->size() <= world_size * world_size) &&
                    (!v->resident_logistics() || v->resident_logistics()->size() <= 6000) &&
                    (!v->construction_sites() ||
                     v->construction_sites()->size() <= world_size * world_size) &&
                    (!v->removed_resident_logistics() || v->removed_resident_logistics()->size() <= 6000) &&
                    (!v->removed_construction_sites() ||
                     v->removed_construction_sites()->size() <= world_size * world_size) &&
                    (!v->resident_work() || v->resident_work()->size() <= 6000) &&
                    (!v->removed_resident_work() || v->removed_resident_work()->size() <= 6000) &&
                    (!v->resident_professions() || v->resident_professions()->size() <= 6000) &&
                    (!v->removed_resident_professions() || v->removed_resident_professions()->size() <= 6000),
                "Snapshot bounds");
            Snapshot x;
            std::copy(v->world_id()->begin(), v->world_id()->end(), x.world_id.begin());
            x.revision = v->revision();
            x.baseline = v->baseline();
            x.tick = v->tick();
            x.weather = unpack_weather(v->weather(), x.tick);
            x.full = v->full();
            x.terrain_generator = v->terrain_generator();
            if (v->chunks())
                for (auto c : *v->chunks()) {
                    require(c->tiles()->size() <= 1024, "Chunk tile bounds");
                    ChunkChange change{
                        {static_cast<WorldLayer>(c->key()->layer()), c->key()->cx(), c->key()->cy()},
                        c->revision(), {}};
                    int height = 0;
                    for (auto t : *c->tiles()) {
                        height += t->elevation_delta();
                        require(height >= -32 && height <= 512, "Invalid elevation delta");
                        change.tiles.push_back({t->index(),
                            {static_cast<Terrain>(t->terrain()), t->amount(), t->explored(),
                             static_cast<Biome>(t->biome()), static_cast<std::int16_t>(height),
                             t->trail_wear()}});
                    }
                    x.chunks.push_back(std::move(change));
                }
            if (v->removed_chunks())
                for (auto key : *v->removed_chunks())
                    x.removed_chunks.push_back({static_cast<WorldLayer>(key->layer()), key->cx(), key->cy()});
            for (auto r : *v->residents())
                x.residents.push_back(unpack_resident(r));
            if (v->layered_buildings())
                for (auto b : *v->layered_buildings())
                    x.buildings.push_back({b->id(), static_cast<BuildingKind>(b->kind()), b->x(), b->y(),
                                           b->progress(), static_cast<WorldLayer>(b->layer()), b->nation()});
            x.part_index = v->part_index();
            x.part_count = v->part_count();
            require(!v->portals() || v->portals()->size() <= 64, "Portal bounds");
            if (v->portals())
                for (auto portal : *v->portals())
                    x.portals.push_back({{portal->surface()->x(), portal->surface()->y(),
                                          static_cast<WorldLayer>(portal->surface()->layer())},
                                         {portal->mine()->x(), portal->mine()->y(),
                                          static_cast<WorldLayer>(portal->mine()->layer())}});
            if (v->removed_residents())
                x.removed_residents.assign(v->removed_residents()->begin(), v->removed_residents()->end());
            if (v->removed_buildings())
                x.removed_buildings.assign(v->removed_buildings()->begin(), v->removed_buildings()->end());
            x.resources = {v->resources()->wood(), v->resources()->stone(), v->resources()->food()};
            const auto inventory = v->inventory();
            x.inventory = {unpack_resources(inventory->total()), unpack_resources(inventory->available()),
                           unpack_resources(inventory->reserved()), unpack_resources(inventory->stored()),
                           unpack_resources(inventory->carried())};
            if (v->storages())
                for (const auto storage : *v->storages())
                    x.storages.push_back(
                        {storage->building_id(), storage->used(), storage->capacity()});
            if (v->removed_storages())
                x.removed_storages.assign(v->removed_storages()->begin(),
                                          v->removed_storages()->end());
            if (v->resident_logistics())
                for (const auto r : *v->resident_logistics())
                    x.resident_logistics.push_back({r->resident_id(), static_cast<ItemKind>(r->item()),
                                                    r->quantity(), r->capacity(),
                                                    static_cast<LogisticsPhase>(r->phase()),
                                                    static_cast<LogisticsFailure>(r->failure())});
            if (v->construction_sites())
                for (const auto site : *v->construction_sites())
                    x.construction_sites.push_back(
                        {site->building_id(),
                         static_cast<BuildingKind>(site->kind()),
                         {site->site()->x(), site->site()->y(),
                          static_cast<WorldLayer>(site->site()->layer())},
                         unpack_resources(site->required()),
                         unpack_resources(site->delivered()),
                         site->work(),
                         static_cast<ConstructionPhase>(site->phase()),
                         static_cast<LogisticsFailure>(site->failure()),
                         static_cast<ConstructionWorkerPhase>(site->worker_phase()),
                         static_cast<PathFailure>(site->worker_failure()),
                         site->assigned_workers()});
            if (v->resident_work())
                for (const auto work : *v->resident_work())
                    x.resident_work.push_back({work->resident_id(), static_cast<WorkIntentKind>(work->kind()),
                                               static_cast<WorkIntentPhase>(work->phase()),
                                               static_cast<PathFailure>(work->failure()),
                                               static_cast<WorkCompletionReason>(work->completion()),
                                               work->attempts()});
            if (v->removed_resident_logistics())
                x.removed_resident_logistics.assign(v->removed_resident_logistics()->begin(),
                                                    v->removed_resident_logistics()->end());
            if (v->removed_construction_sites())
                x.removed_construction_sites.assign(v->removed_construction_sites()->begin(),
                                                    v->removed_construction_sites()->end());
            if (v->removed_resident_work())
                x.removed_resident_work.assign(v->removed_resident_work()->begin(),
                                               v->removed_resident_work()->end());
            if (v->resident_professions())
                for (const auto profile : *v->resident_professions()) {
                    require(profile->skills()->size() == skill_count, "Resident skill count");
                    ResidentProfessionView item;
                    item.resident_id = profile->resident_id();
                    item.assignment = unpack_assignment(profile->assignment());
                    item.manual_override = profile->manual_override();
                    item.deferred = profile->deferred();
                    item.wait_reason = static_cast<ProfessionWaitReason>(profile->wait_reason());
                    for (std::size_t i = 0; i < skill_count; ++i) {
                        const auto skill = profile->skills()->Get(static_cast<flatbuffers::uoffset_t>(i));
                        const auto m = skill->modifier();
                        item.skills[i] = {static_cast<Skill>(skill->skill()),
                                          skill->experience(),
                                          {m->skill_sum_bp(), m->ability_sum_bp(), m->familiarity_bp(),
                                           m->uncapped_bp(), m->applied_bp(), m->speed_bp(), m->capped()}};
                    }
                    x.resident_professions.push_back(std::move(item));
                }
            if (v->removed_resident_professions())
                x.removed_resident_professions.assign(v->removed_resident_professions()->begin(),
                                                      v->removed_resident_professions()->end());
            require((!v->manual_orders() || v->manual_orders()->size() <= max_manual_orders) &&
                        (!v->removed_manual_orders() ||
                         v->removed_manual_orders()->size() <= max_manual_orders) &&
                        (!v->resident_controls() || v->resident_controls()->size() <= 6000) &&
                        (!v->removed_resident_controls() || v->removed_resident_controls()->size() <= 6000) &&
                        (!v->control_players() || v->control_players()->size() <= 4),
                    "Manual/control collection bounds");
            if (v->manual_orders())
                for (const auto order : *v->manual_orders()) {
                    std::optional<CellCoord> target;
                    if (const auto t = order->target())
                        target = CellCoord{t->x(), t->y(), static_cast<WorldLayer>(t->layer())};
                    x.manual_orders.push_back({order->order_id(), order->resident_id(),
                                               static_cast<ManualOrderKind>(order->kind()), target,
                                               static_cast<GatherCompletion>(order->gather_completion()),
                                               static_cast<ManualOrderPhase>(order->phase()),
                                               static_cast<PathFailure>(order->failure()),
                                               static_cast<ManualOrderCompletion>(order->completion())});
                }
            if (v->resident_controls())
                for (const auto control : *v->resident_controls()) {
                    require(control->holder_name()->size() <= 64, "Controller name bounds");
                    x.resident_controls.push_back(
                        {control->resident_id(), control->holder_id(), control->holder_name()->str(),
                         control->held_by_recipient(), control->disconnected_grace(), control->revision()});
                }
            if (v->control_players())
                for (const auto player : *v->control_players()) {
                    require(player->name()->size() <= 64, "Control player name bounds");
                    x.control_players.push_back({player->player_id(), player->name()->str()});
                }
            if (v->removed_manual_orders())
                x.removed_manual_orders.assign(v->removed_manual_orders()->begin(),
                                               v->removed_manual_orders()->end());
            if (v->removed_resident_controls())
                x.removed_resident_controls.assign(v->removed_resident_controls()->begin(),
                                                   v->removed_resident_controls()->end());
            x.control_epoch = v->control_epoch();
            x.housing = v->housing();
            x.day = v->day();
            x.tutorial_step = v->tutorial_step();
            x.winter = v->winter();
            x.settlement = unpack_settlement(v->settlement());
            x.economy=decode_economy(v->economy()?v->economy()->str():"");
            x.lobby=decode_lobby(v->lobby()?v->lobby()->str():"");
            validate_snapshot(x);
            out.payload = std::move(x);
            break;
        }
        case fb::Payload::SnapshotAck: {
            auto v = p->payload_as_SnapshotAck();
            out.payload = SnapshotAck{v->revision(), v->resync()};
            break;
        }
        case fb::Payload::ChatSend: {
            auto v = p->payload_as_ChatSend();
            require(valid_utf8_text(v->text()->string_view(), 1024, true), "Invalid chat text");
            out.payload = ChatSend{v->text()->str()};
            break;
        }
        case fb::Payload::Chat: {
            auto v = p->payload_as_Chat();
            require(v->author() != 0 && valid_utf8_text(v->name()->string_view(), 64) &&
                        valid_utf8_text(v->text()->string_view(), 1024, true),
                    "Invalid chat entry");
            out.payload = ChatEntry{v->sequence(), v->author(), v->name()->str(), v->text()->str()};
            break;
        }
        case fb::Payload::Ping:
            out.payload = Ping{p->payload_as_Ping()->value()};
            break;
        case fb::Payload::ReplyAck: {
            auto v = p->payload_as_ReplyAck();
            ReplyAck x;
            require(v->request_id() == 0 && (!v->requests() || v->requests()->size() == 0) && v->receipts() &&
                        !v->receipts()->empty() && v->receipts()->size() <= 256,
                    "Reply acknowledgement batch limit");
            std::set<ReplyReceipt> receipts;
            for (auto receipt : *v->receipts()) {
                ReplyReceipt value{receipt->request_id(), static_cast<CommandReplyStatus>(receipt->status())};
                require(value.request_id != 0 && reply_status_valid(value.status) &&
                            receipts.insert(value).second,
                        "Invalid reply acknowledgement");
                x.receipts.push_back(value);
            }
            out.payload = std::move(x);
            break;
        }
        default:
            require(false, "Unknown packet payload");
        }
        return out;
    } catch (const std::exception &exception) {
        error = exception.what();
        return {};
    }
}

Snapshot make_snapshot(const ViewSnapshot &view, WorldId id, const ViewSnapshot *baseline) {
    require(!baseline || climate_identity_equal(baseline->weather, view.weather),
            "Climate identity changed without full snapshot");
    require(!baseline || baseline->terrain_generator == view.terrain_generator,
            "Terrain generator changed without full snapshot");
    Snapshot s;
    s.world_id = id;
    s.revision = view.revision;
    s.tick = view.tick;
    s.weather = view.weather;
    s.terrain_generator = view.terrain_generator;
    s.full = baseline == nullptr;
    s.baseline = baseline ? baseline->revision : 0;
    std::set<ChunkKey> present;
    for (const auto &chunk : view.chunks) {
        require(key_valid(chunk.key) && present.insert(chunk.key).second, "Invalid view chunk");
        const Chunk *old = nullptr;
        if (baseline) {
            auto found = std::find_if(baseline->chunks.begin(), baseline->chunks.end(),
                                      [&](const auto &c) { return c.key == chunk.key; });
            if (found != baseline->chunks.end())
                old = &*found;
        }
        ChunkChange change{chunk.key, chunk.revision, {}};
        for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
            const auto &tile = chunk.tiles[i];
            require(tile.explored || (tile.terrain == Terrain::Grass && tile.amount == 0 &&
                    tile.biome == Biome::Meadow && tile.elevation == 0 && tile.trail_wear == 0),
                    "Hidden tile contains private data");
            if (!old || old->tiles[i] != tile)
                change.tiles.push_back({static_cast<std::uint32_t>(i), tile});
        }
        if (!change.tiles.empty())
            s.chunks.push_back(std::move(change));
    }
    if (baseline)
        for (const auto &chunk : baseline->chunks)
            if (!present.contains(chunk.key))
                s.removed_chunks.push_back(chunk.key);
    s.portals = view.portals;
    const auto changes = [](const auto &current, const auto *prior, auto &changed, auto &removed, auto equal,
                            auto key) {
        using Entity = typename std::decay_t<decltype(current)>::value_type;
        using Key = std::decay_t<decltype(key(std::declval<Entity>()))>;
        std::unordered_map<Key, const Entity *> previous;
        std::unordered_set<Key> present;
        if (prior)
            for (const auto &entity : *prior)
                previous.emplace(key(entity), &entity);
        previous.reserve(prior ? prior->size() : 0);
        present.reserve(current.size());
        for (const auto &entity : current) {
            present.insert(key(entity));
            auto found = previous.find(key(entity));
            if (found == previous.end() || !equal(entity, *found->second))
                changed.push_back(entity);
        }
        if (prior)
            for (const auto &entity : *prior)
                if (!present.contains(key(entity)))
                    removed.push_back(key(entity));
    };
    const auto entity_id = [](const auto &entity) { return entity.id; };
    changes(view.residents, baseline ? &baseline->residents : nullptr, s.residents, s.removed_residents,
            resident_equal, entity_id);
    changes(view.buildings, baseline ? &baseline->buildings : nullptr, s.buildings, s.removed_buildings,
            building_equal, entity_id);
    const auto equal = [](const auto &a, const auto &b) { return a == b; };
    changes(view.resident_logistics, baseline ? &baseline->resident_logistics : nullptr, s.resident_logistics,
            s.removed_resident_logistics, equal, [](const auto &r) { return r.resident_id; });
    changes(view.resident_work, baseline ? &baseline->resident_work : nullptr, s.resident_work,
            s.removed_resident_work, equal, [](const auto &r) { return r.resident_id; });
    changes(view.resident_professions, baseline ? &baseline->resident_professions : nullptr,
            s.resident_professions, s.removed_resident_professions, equal,
            [](const auto &r) { return r.resident_id; });
    changes(view.construction_sites, baseline ? &baseline->construction_sites : nullptr, s.construction_sites,
            s.removed_construction_sites, equal, [](const auto &site) { return site.building_id; });
    changes(view.storages, baseline ? &baseline->storages : nullptr, s.storages,
            s.removed_storages, equal, [](const auto &storage) { return storage.building_id; });
    changes(view.manual_orders, baseline ? &baseline->manual_orders : nullptr, s.manual_orders,
            s.removed_manual_orders, equal, [](const auto &order) { return order.order_id; });
    changes(view.resident_controls, baseline ? &baseline->resident_controls : nullptr, s.resident_controls,
            s.removed_resident_controls, equal, [](const auto &control) { return control.resident_id; });
    s.control_players = view.control_players;
    s.control_epoch = view.control_epoch;
    s.resources = view.resources;
    s.inventory = view.inventory;
    s.housing = view.housing;
    s.day = view.day;
    s.tutorial_step = view.tutorial_step;
    s.winter = view.winter;
    s.settlement = view.settlement;
    s.economy=view.economy;
    s.lobby=view.lobby;
    validate_snapshot(s);
    return s;
}
bool apply_snapshot(ViewSnapshot &view, WorldId &id, const Snapshot &s, std::string &error) {
    error.clear();
    try {
        validate_snapshot(s);
        require(s.part_count == 1, "Multipart snapshot requires complete assembly");
        require(!s.full || s.world_id != id || (s.revision >= view.revision && s.tick >= view.tick),
                "Stale full snapshot");
        require(s.full || (s.world_id == id && s.baseline == view.revision && s.tick >= view.tick),
                "Snapshot baseline mismatch; resync required");
        require(s.full || s.terrain_generator == view.terrain_generator,
                "Terrain generator changed without full snapshot");
        require(s.full || s.control_epoch == view.control_epoch,
                "Control epoch changed without full snapshot");
        require(s.full || climate_identity_equal(s.weather, view.weather),
                "Climate identity changed without full snapshot");
        ViewSnapshot next = s.full ? ViewSnapshot{} : view;
        std::erase_if(next.chunks, [&](const auto &c) {
            return std::find(s.removed_chunks.begin(), s.removed_chunks.end(), c.key) !=
                   s.removed_chunks.end();
        });
        for (const auto &change : s.chunks) {
            auto found = std::find_if(next.chunks.begin(), next.chunks.end(),
                                      [&](const auto &c) { return c.key == change.key; });
            if (found == next.chunks.end()) {
                next.chunks.push_back(Chunk{change.key});
                found = std::prev(next.chunks.end());
            }
            found->revision = change.revision;
            for (const auto &tile : change.tiles)
                found->tiles[tile.index] = tile.tile;
        }
        require(next.chunks.size() <= world_chunk_count, "Merged chunk limit");
        std::sort(next.chunks.begin(), next.chunks.end(),
                  [](const auto &a, const auto &b) { return a.key < b.key; });
        next.portals = s.portals;
        const auto update = [](auto &values, const auto &changed, const auto &removed, auto key) {
            std::erase_if(values, [&](const auto &v) {
                return std::find(removed.begin(), removed.end(), key(v)) != removed.end();
            });
            for (const auto &v : changed) {
                auto found = std::find_if(values.begin(), values.end(),
                                          [&](const auto &p) { return key(p) == key(v); });
                if (found == values.end())
                    values.push_back(v);
                else
                    *found = v;
            }
            std::sort(values.begin(), values.end(),
                      [&](const auto &a, const auto &b) { return key(a) < key(b); });
        };
        const auto entity_id = [](const auto &entity) { return entity.id; };
        update(next.residents, s.residents, s.removed_residents, entity_id);
        update(next.buildings, s.buildings, s.removed_buildings, entity_id);
        update(next.resident_logistics, s.resident_logistics, s.removed_resident_logistics,
               [](const auto &r) { return r.resident_id; });
        update(next.resident_work, s.resident_work, s.removed_resident_work,
               [](const auto &r) { return r.resident_id; });
        update(next.resident_professions, s.resident_professions, s.removed_resident_professions,
               [](const auto &r) { return r.resident_id; });
        update(next.construction_sites, s.construction_sites, s.removed_construction_sites,
               [](const auto &site) { return site.building_id; });
        update(next.storages, s.storages, s.removed_storages,
               [](const auto &storage) { return storage.building_id; });
        update(next.manual_orders, s.manual_orders, s.removed_manual_orders,
               [](const auto &order) { return order.order_id; });
        update(next.resident_controls, s.resident_controls, s.removed_resident_controls,
               [](const auto &control) { return control.resident_id; });
        next.control_players = s.control_players;
        next.control_epoch = s.control_epoch;
        require(next.manual_orders.size() <= max_manual_orders && next.resident_controls.size() <= 6000,
                "Merged manual/control size exceeded");
        require(next.residents.size() <= 6000 && next.buildings.size() <= world_size * world_size,
                "Merged snapshot size exceeded");
        require(next.resident_logistics.size() <= 6000 && next.resident_work.size() <= 6000 &&
                    next.resident_professions.size() <= 6000 &&
                    next.construction_sites.size() <= world_size * world_size &&
                    next.storages.size() <= world_size * world_size,
                "Merged logistics size exceeded");
        std::set<int> own_residents;
        for (const auto &resident : next.residents)
            if (resident.nation == 1 || s.lobby.enabled)
                own_residents.insert(resident.id);
        for (const auto &resident : next.resident_logistics)
            require(own_residents.contains(resident.resident_id), "Logistics for private resident");
        for (const auto &work : next.resident_work)
            require(own_residents.contains(work.resident_id), "Work for private resident");
        std::map<int, std::size_t> active_orders;
        for (const auto &order : next.manual_orders) {
            require(own_residents.contains(order.resident_id), "Manual order for private resident");
            if (order.target)
                require(next.tile(order.target->x, order.target->y, order.target->layer).explored,
                        "Manual order on private tile");
            if (order.phase != ManualOrderPhase::Complete && order.phase != ManualOrderPhase::Failed &&
                order.phase != ManualOrderPhase::Cancelled)
                require(++active_orders[order.resident_id] <= max_resident_manual_orders,
                        "Resident manual queue size exceeded");
        }
        for (const auto &control : next.resident_controls) {
            require(own_residents.contains(control.resident_id), "Control for private resident");
            if (control.holder_id && !control.disconnected_grace)
                require(std::any_of(next.control_players.begin(), next.control_players.end(),
                                    [&](const auto &player) {
                                        return player.player_id == control.holder_id &&
                                               player.name == control.holder_name;
                                    }),
                        "Controller is not an eligible player");
        }
        for (const auto &profile : next.resident_professions) {
            require(own_residents.contains(profile.resident_id), "Profession for private resident");
            if (profile.assignment.worksite) {
                const auto &site = *profile.assignment.worksite;
                require(next.tile(site.x, site.y, site.layer).explored, "Profession on private tile");
            }
        }
        for (const auto &site : next.construction_sites)
            require(next.tile(site.site.x, site.site.y, site.site.layer).explored,
                    "Construction on private tile");
        for (const auto &storage : next.storages) {
            const auto building = std::find_if(next.buildings.begin(), next.buildings.end(),
                                               [&](const auto &candidate) {
                                                   return candidate.id == storage.building_id;
                                               });
            require(building != next.buildings.end() && building->progress == 1 &&
                        (building->kind == BuildingKind::Camp ||
                         building->kind == BuildingKind::Stockpile),
                    "Storage for private or invalid building");
        }
        for (const auto &b : next.buildings)
            require(next.tile(b.x, b.y, b.layer).explored, "Building on private tile");
        for (const auto &portal : next.portals)
            require(next.tile(portal.surface.x, portal.surface.y, portal.surface.layer).explored &&
                        next.tile(portal.mine.x, portal.mine.y, portal.mine.layer).explored,
                    "Portal on private tile");
        next.tick = s.tick;
        next.revision = s.revision;
        next.terrain_generator = s.terrain_generator;
        next.resources = s.resources;
        next.inventory = s.inventory;
        next.housing = s.housing;
        next.day = s.day;
        next.tutorial_step = s.tutorial_step;
        next.winter = s.winter;
        next.weather = s.weather;
        for(const auto &y:s.settlement.young)
            require(own_residents.contains(y.resident_id),"Youth policy for private resident");
        next.settlement = s.settlement;
        next.economy=s.economy;
        next.lobby=s.lobby;
        view = std::move(next);
        id = s.world_id;
        return true;
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
}
std::vector<Snapshot> split_snapshot(const Snapshot &source) {
    validate_snapshot(source);
    require(source.part_count == 1, "Cannot split an existing snapshot part");
    Snapshot metadata = source;
    metadata.chunks.clear();
    metadata.removed_chunks.clear();
    metadata.residents.clear();
    metadata.buildings.clear();
    metadata.removed_residents.clear();
    metadata.removed_buildings.clear();
    metadata.resident_logistics.clear();
    metadata.storages.clear();
    metadata.resident_work.clear();
    metadata.resident_professions.clear();
    metadata.construction_sites.clear();
    metadata.removed_resident_logistics.clear();
    metadata.removed_construction_sites.clear();
    metadata.removed_resident_work.clear();
    metadata.removed_resident_professions.clear();
    metadata.removed_storages.clear();
    metadata.manual_orders.clear();
    metadata.removed_manual_orders.clear();
    metadata.resident_controls.clear();
    metadata.removed_resident_controls.clear();
    std::vector<Snapshot> parts{metadata};
    const std::size_t metadata_weight = 4096 + encode_economy(metadata.economy).size() + metadata.settlement.young.size()*24;
    std::size_t weight = metadata_weight;
    auto room = [&](std::size_t amount) {
        require(amount <= 160 * 1024 - 4096, "Snapshot item too large");
        if (weight + amount > 160 * 1024) {
            parts.push_back(metadata);
            weight = metadata_weight;
        }
        weight += amount;
    };
    for (const auto &chunk : source.chunks) {
        room(64 + chunk.tiles.size() * 20);
        parts.back().chunks.push_back(chunk);
    }
    for (const auto &key : source.removed_chunks) {
        room(16);
        parts.back().removed_chunks.push_back(key);
    }
    for (const auto &resident : source.residents) {
        room(160 + resident.name.size());
        parts.back().residents.push_back(resident);
    }
    for (const auto &building : source.buildings) {
        room(64);
        parts.back().buildings.push_back(building);
    }
    for (int id : source.removed_residents) {
        room(4);
        parts.back().removed_residents.push_back(id);
    }
    for (int id : source.removed_buildings) {
        room(4);
        parts.back().removed_buildings.push_back(id);
    }
    for (const auto &resident : source.resident_logistics) {
        room(80);
        parts.back().resident_logistics.push_back(resident);
    }
    for (const auto &storage : source.storages) {
        room(16);
        parts.back().storages.push_back(storage);
    }
    for (const auto &site : source.construction_sites) {
        room(160);
        parts.back().construction_sites.push_back(site);
    }
    for (const auto &work : source.resident_work) {
        room(64);
        parts.back().resident_work.push_back(work);
    }
    for (const auto &profile : source.resident_professions) {
        room(128 + skill_count * 80);
        parts.back().resident_professions.push_back(profile);
    }
    for (int id : source.removed_resident_logistics) {
        room(4);
        parts.back().removed_resident_logistics.push_back(id);
    }
    for (int id : source.removed_construction_sites) {
        room(4);
        parts.back().removed_construction_sites.push_back(id);
    }
    for (int id : source.removed_resident_work) {
        room(4);
        parts.back().removed_resident_work.push_back(id);
    }
    for (int id : source.removed_resident_professions) {
        room(4);
        parts.back().removed_resident_professions.push_back(id);
    }
    for (int id : source.removed_storages) {
        room(4);
        parts.back().removed_storages.push_back(id);
    }
    for (const auto &order : source.manual_orders) {
        room(112);
        parts.back().manual_orders.push_back(order);
    }
    for (auto order : source.removed_manual_orders) {
        room(8);
        parts.back().removed_manual_orders.push_back(order);
    }
    for (const auto &control : source.resident_controls) {
        room(128 + control.holder_name.size());
        parts.back().resident_controls.push_back(control);
    }
    for (int id : source.removed_resident_controls) {
        room(4);
        parts.back().removed_resident_controls.push_back(id);
    }
    require(parts.size() <= 256, "Snapshot part count limit");
    for (std::size_t i = 0; i < parts.size(); ++i) {
        parts[i].part_index = static_cast<std::uint32_t>(i);
        parts[i].part_count = static_cast<std::uint32_t>(parts.size());
    }
    return parts;
}

void SnapshotAssembler::reset() {
    parts_.clear();
    count_ = weight_ = 0;
    started_ = last_progress_ = {};
}
bool SnapshotAssembler::expired() const {
    return !parts_.empty() && (clock_() - last_progress_ > std::chrono::seconds(10) ||
                               clock_() - started_ > std::chrono::seconds(30));
}
AssemblyResult SnapshotAssembler::accept(ViewSnapshot &view, WorldId &id, Snapshot part, std::string &error) {
    error.clear();
    try {
        validate_snapshot(part);
        if (part.part_count == 1) {
            reset();
            return apply_snapshot(view, id, part, error) ? AssemblyResult::Applied : AssemblyResult::Rejected;
        }
        require(!expired(), "Snapshot assembly timed out");
        if (parts_.empty()) {
            parts_.resize(part.part_count);
            started_ = last_progress_ = clock_();
        }
        const Snapshot *header = nullptr;
        for (const auto &item : parts_)
            if (item) {
                header = &*item;
                break;
            }
        // A requested full replacement may follow the remaining queued parts
        // of an obsolete delta in the reliable snapshot lane.
        if (header && part.full && !header->full && part.world_id == header->world_id &&
            part.revision >= header->revision) {
            reset();
            parts_.resize(part.part_count);
            started_ = last_progress_ = clock_();
            header = nullptr;
        }
        require(parts_.size() == part.part_count, "Snapshot part count changed");
        if (header)
            require(header->world_id == part.world_id && header->revision == part.revision &&
                        header->baseline == part.baseline && header->tick == part.tick &&
                        header->full == part.full &&
                        header->terrain_generator == part.terrain_generator &&
                        header->resources.wood == part.resources.wood &&
                        header->resources.stone == part.resources.stone &&
                        header->resources.food == part.resources.food && header->housing == part.housing &&
                        header->day == part.day && header->tutorial_step == part.tutorial_step &&
                        header->winter == part.winter && header->lobby == part.lobby && header->economy == part.economy && header->settlement == part.settlement && header->portals == part.portals &&
                        header->weather == part.weather &&
                        header->inventory == part.inventory && header->control_epoch == part.control_epoch &&
                        header->control_players == part.control_players,
                    "Inconsistent snapshot parts");
        auto &slot = parts_[part.part_index];
        if (slot) {
            require(encode({0, *slot}) == encode({0, part}), "Conflicting duplicate snapshot part");
            return AssemblyResult::Pending;
        }
        weight_ += part.chunks.size() * 1024 * sizeof(Tile) + part.residents.size() * 512 +
                   part.buildings.size() * 64 + part.removed_buildings.size() * sizeof(int) +
                   part.removed_residents.size() * sizeof(int) + part.resident_logistics.size() * 80 +
                   part.storages.size() * 16 + part.removed_storages.size() * sizeof(int) +
                   part.construction_sites.size() * 160 + part.resident_work.size() * 64 +
                   part.resident_professions.size() * (128 + skill_count * 80) +
                   part.manual_orders.size() * 112 + part.removed_manual_orders.size() * 8 +
                   part.resident_controls.size() * 192 + part.removed_resident_controls.size() * 4 +
                   part.control_players.size() * 96 + part.settlement.young.size()*24 + encode_economy(part.economy).size() +
                   (part.removed_resident_logistics.size() + part.removed_construction_sites.size() +
                    part.removed_resident_work.size() + part.removed_resident_professions.size()) *
                       sizeof(int);
        require(weight_ <= 24 * 1024 * 1024, "Snapshot assembly memory limit");
        slot = std::move(part);
        ++count_;
        last_progress_ = clock_();
        if (count_ != parts_.size())
            return AssemblyResult::Pending;
        Snapshot joined = std::move(*parts_[0]);
        joined.part_index = 0;
        joined.part_count = 1;
        const auto append = [](auto &out, auto &input) {
            out.insert(out.end(), std::make_move_iterator(input.begin()),
                       std::make_move_iterator(input.end()));
        };
        for (std::size_t i = 1; i < parts_.size(); ++i) {
            auto &p = *parts_[i];
            append(joined.chunks, p.chunks);
            append(joined.removed_chunks, p.removed_chunks);
            append(joined.residents, p.residents);
            append(joined.buildings, p.buildings);
            append(joined.removed_residents, p.removed_residents);
            append(joined.removed_buildings, p.removed_buildings);
            append(joined.resident_logistics, p.resident_logistics);
            append(joined.storages, p.storages);
            append(joined.resident_work, p.resident_work);
            append(joined.resident_professions, p.resident_professions);
            append(joined.construction_sites, p.construction_sites);
            append(joined.removed_resident_logistics, p.removed_resident_logistics);
            append(joined.removed_construction_sites, p.removed_construction_sites);
            append(joined.removed_resident_work, p.removed_resident_work);
            append(joined.removed_resident_professions, p.removed_resident_professions);
            append(joined.removed_storages, p.removed_storages);
            append(joined.manual_orders, p.manual_orders);
            append(joined.removed_manual_orders, p.removed_manual_orders);
            append(joined.resident_controls, p.resident_controls);
            append(joined.removed_resident_controls, p.removed_resident_controls);
        }
        reset();
        return apply_snapshot(view, id, joined, error) ? AssemblyResult::Applied : AssemblyResult::Rejected;
    } catch (const std::exception &e) {
        error = e.what();
        reset();
        return AssemblyResult::Rejected;
    }
}
std::string password_proof(std::string_view password, const Challenge &challenge, const Hello &hello) {
    require(password.size() <= 256, "Password is too long");
    // Length-prefix every field so identity and version fields cannot be resegmented.
    std::string input = "Ribterra join proof v1";
    for (const auto &field : std::vector<std::string>{
             challenge.nonce, hello.name, hello.resume_token, std::to_string(hello.versions.protocol),
             std::to_string(hello.versions.rules), hello.versions.content})
        input += std::to_string(field.size()) + ":" + field;
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int size{};
    require(HMAC(EVP_sha256(), password.data(), static_cast<int>(password.size()),
                 reinterpret_cast<const unsigned char *>(input.data()), input.size(), digest,
                 &size) != nullptr,
            "Password proof failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (unsigned i = 0; i < size; ++i) {
        out += hex[digest[i] >> 4];
        out += hex[digest[i] & 15];
    }
    return out;
}
} // namespace ribterra::net

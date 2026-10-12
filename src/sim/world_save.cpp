#include "ribterra/atomic_file.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include "ribterra/weather.hpp"
#include "world_save_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ribterra {
namespace {

constexpr std::array<std::uint8_t, 8> container_magic{'R', 'I', 'B', 'S', 'V', '2', '\r', '\n'};
constexpr std::uint16_t header_version = 2;
constexpr std::uint16_t flag_zstd = 1;
constexpr std::uint32_t format_version = 9;
constexpr std::size_t header_size = 96;
constexpr std::uint64_t max_compressed_size = 96ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t max_decompressed_size = 96ULL * 1024ULL * 1024ULL;
enum class SaveKind { Current };
struct SaveHeader {
    std::uint64_t decompressed_size{}, compressed_size{}, checksum{};
    std::array<std::uint8_t, 16> world_id{};
};

template <typename T> void append_le(std::vector<std::uint8_t> &bytes, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes.push_back(static_cast<std::uint8_t>(bits >> (i * 8U)));
}

template <typename T>
bool read_le(std::span<const std::uint8_t> bytes, std::size_t &offset, T &value) {
    if (bytes.size() - std::min(bytes.size(), offset) < sizeof(T))
        return false;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bits |= static_cast<Unsigned>(bytes[offset++]) << (i * 8U);
    value = static_cast<T>(bits);
    return true;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> bytes,
                    std::uint64_t hash = 1469598103934665603ULL) {
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t container_checksum(std::span<const std::uint8_t> header,
                                 std::span<const std::uint8_t> payload) {
    std::uint64_t hash = fnv1a(header.first(40));
    hash = fnv1a(header.subspan(48, 48), hash);
    return fnv1a(payload, hash);
}

bool preflight_header(std::span<const std::uint8_t> bytes, std::uintmax_t file_size,
                      SaveHeader& result, std::string& error) {
    if (bytes.size() >= 10u && std::memcmp(bytes.data(), "RIBTERRA 1", 10) == 0) {
        error = "save.climate_schema_required";
        return false;
    }
    if (bytes.size() < header_size ||
        !std::equal(container_magic.begin(), container_magic.end(), bytes.begin())) {
        error = "save.header_invalid";
        return false;
    }
    std::size_t offset = container_magic.size();
    std::uint16_t header{}, flags{};
    std::uint32_t format{}, rules{}, generator{};
    if (!read_le(bytes, offset, header) || !read_le(bytes, offset, flags) ||
        !read_le(bytes, offset, format) || !read_le(bytes, offset, rules) ||
        !read_le(bytes, offset, generator) || !read_le(bytes, offset, result.decompressed_size) ||
        !read_le(bytes, offset, result.compressed_size) || !read_le(bytes, offset, result.checksum)) {
        error = "save.header_invalid";
        return false;
    }
    // Old containers never reach ZSTD, FlatBuffers, World import or migration.
    if (format != format_version) {
        error = "save.climate_schema_required";
        return false;
    }
    if (header != header_version || flags != flag_zstd) {
        error = "save.header_invalid";
        return false;
    }
    if (rules != rules_version || generator != generator_version) {
        error = "save.climate_contract_invalid";
        return false;
    }
    if (result.decompressed_size == 0u || result.decompressed_size > max_decompressed_size ||
        result.compressed_size == 0u || result.compressed_size > max_compressed_size ||
        file_size < header_size || result.compressed_size != file_size - header_size) {
        error = "save.size_invalid";
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), result.world_id.size(), result.world_id.begin());
    offset += result.world_id.size();
    const auto content = default_content_hash();
    if (content.size() != 64u) {
        error = "save.content_contract_invalid";
        return false;
    }
    const auto nibble = [](char value) {
        return value >= '0' && value <= '9' ? value - '0'
             : value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    };
    for (std::size_t i = 0; i < 32u; ++i) {
        const auto high = nibble(content[i * 2u]), low = nibble(content[i * 2u + 1u]);
        if (high < 0 || low < 0 || bytes[offset + i] != static_cast<std::uint8_t>(high * 16 + low)) {
            error = "save.content_contract_invalid";
            return false;
        }
    }
    return true;
}

bool validate_save_climate(const WorldState& state, std::string& error) {
    try {
        const auto& climate = state.climate;
        const WeatherContext context{state.seed,
            {climate.algorithm_version, climate.climate_profile, climate.scenario}};
        const auto calendar = make_calendar_tick(state.ticks, climate.calendar_offset_ticks);
        if (!calendar) {
            error = "save.climate_invalid";
            return false;
        }
        const WeatherScheduleCache cache(context, *calendar);
        validate_climate_runtime(climate, state.ticks, context, cache);
        return true;
    } catch (const std::exception&) {
        error = "save.climate_invalid";
        return false;
    }
}

std::vector<std::uint8_t> build_flatbuffer(const WorldState &state) {
    flatbuffers::FlatBufferBuilder builder(128 * 1024);
    const auto world_id = builder.CreateVector(state.world_id.data(), state.world_id.size());
    const auto metadata =
        save::CreateMetadata(builder, world_id, state.seed, state.ticks, state.next_building_id,
                             state.has_moved, state.has_gathered, state.has_built);
    const auto resources = save::CreateResources(builder, state.resources.wood,
                                                 state.resources.stone, state.resources.food);

    const auto legacy_tiles = builder.CreateVector(std::vector<flatbuffers::Offset<save::Tile>>{});

    std::vector<const Chunk *> ordered_chunks;
    ordered_chunks.reserve(state.chunks.size());
    for (const auto &chunk : state.chunks)
        ordered_chunks.push_back(&chunk);
    std::sort(ordered_chunks.begin(), ordered_chunks.end(), [](const auto *a, const auto *b) {
        if (a->key.layer != b->key.layer)
            return a->key.layer < b->key.layer;
        if (a->key.cy != b->key.cy)
            return a->key.cy < b->key.cy;
        return a->key.cx < b->key.cx;
    });
    std::vector<flatbuffers::Offset<save::Chunk>> chunks;
    chunks.reserve(ordered_chunks.size());
    for (const auto *chunk : ordered_chunks) {
        std::vector<flatbuffers::Offset<save::Tile>> tiles;
        tiles.reserve(chunk->tiles.size());
        for (const auto &tile : chunk->tiles) {
            tiles.push_back(save::CreateTile(builder, static_cast<save::Terrain>(tile.terrain),
                                             tile.amount, tile.explored, static_cast<std::uint8_t>(tile.biome),
                                             tile.elevation, tile.trail_wear));
        }
        chunks.push_back(save::CreateChunk(builder, static_cast<save::Layer>(chunk->key.layer),
                                           static_cast<std::int16_t>(chunk->key.cx),
                                           static_cast<std::int16_t>(chunk->key.cy),
                                           chunk->revision, builder.CreateVector(tiles)));
    }

    std::vector<flatbuffers::Offset<save::Resident>> residents;
    residents.reserve(state.residents.size());
    auto ordered_residents = state.residents;
    std::sort(ordered_residents.begin(), ordered_residents.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (const Resident &resident : ordered_residents) {
        const auto name = builder.CreateString(resident.name);
        std::vector<flatbuffers::Offset<save::SkillPractice>> practice;
        practice.reserve(skill_count);
        for (const auto &value : resident.skill_practice)
            practice.push_back(save::CreateSkillPractice(builder, value.experience_remainder,
                                                         value.familiarity_debt, value.last_practice_tick));
        const auto &p = resident.profession;
        std::optional<save::Cell> worksite;
        if (p.assignment.worksite)
            worksite.emplace(static_cast<std::int16_t>(p.assignment.worksite->x),
                             static_cast<std::int16_t>(p.assignment.worksite->y),
                             static_cast<save::Layer>(p.assignment.worksite->layer));
        const auto profession = save::CreateProfessionState(
            builder, static_cast<save::Profession>(p.assignment.profession),
            worksite ? &*worksite : nullptr,
            builder.CreateVector(p.assignment.priorities.data(), p.assignment.priorities.size()),
            p.assignment.automation_enabled, p.manual_override, p.deferred,
            static_cast<save::ProfessionWaitReason>(p.wait_reason), p.next_evaluation_tick);
        residents.push_back(save::CreateResident(
            builder, resident.id, name, resident.x, resident.y, resident.target_x,
            resident.target_y, static_cast<save::Job>(resident.job), resident.progress,
            resident.health, resident.hunger, resident.wood_xp, resident.mining_xp,
            resident.farming_xp, static_cast<save::Layer>(resident.layer), resident.nation,
            static_cast<save::OrderPhase>(resident.order_phase),
            static_cast<save::PathFailure>(resident.path_failure), resident.pending_path_batch,
            builder.CreateVector(resident.additional_xp.data(), resident.additional_xp.size()),
            builder.CreateVector(practice),
            builder.CreateVector(resident.abilities_bp.data(), resident.abilities_bp.size()), profession));
    }

    std::vector<flatbuffers::Offset<save::Building>> buildings;
    buildings.reserve(state.buildings.size());
    auto ordered_buildings = state.buildings;
    std::sort(ordered_buildings.begin(), ordered_buildings.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (const Building &building : ordered_buildings) {
        buildings.push_back(save::CreateBuilding(
            builder, building.id, static_cast<save::BuildingKind>(building.kind), building.x,
            building.y, building.progress, static_cast<save::Layer>(building.layer),
            building.nation));
    }

    std::vector<flatbuffers::Offset<save::Route>> routes;
    auto ordered_routes = state.routes;
    std::sort(ordered_routes.begin(), ordered_routes.end(),
              [](const auto &a, const auto &b) { return a.resident_id < b.resident_id; });
    routes.reserve(ordered_routes.size());
    for (const RouteState &route : ordered_routes) {
        std::vector<save::Cell> cells;
        cells.reserve(route.points.size());
        for (const auto &point : route.points) {
            cells.emplace_back(static_cast<std::int16_t>(point.x),
                               static_cast<std::int16_t>(point.y),
                               static_cast<save::Layer>(point.layer));
        }
        std::optional<save::Cell> work;
        if (route.work) {
            work.emplace(static_cast<std::int16_t>(route.work->x),
                         static_cast<std::int16_t>(route.work->y),
                         static_cast<save::Layer>(route.work->layer));
        }
        routes.push_back(save::CreateRoute(
            builder, {}, static_cast<std::uint32_t>(route.cursor), -1, -1, route.resident_id,
            builder.CreateVectorOfStructs(cells), work ? &*work : nullptr));
    }

    std::vector<flatbuffers::Offset<save::Portal>> portals;
    auto ordered_portals = state.portals;
    std::sort(ordered_portals.begin(), ordered_portals.end(), [](const auto &a, const auto &b) {
        return std::tie(a.surface.layer, a.surface.y, a.surface.x, a.mine.layer, a.mine.y,
                        a.mine.x) < std::tie(b.surface.layer, b.surface.y, b.surface.x,
                                             b.mine.layer, b.mine.y, b.mine.x);
    });
    portals.reserve(ordered_portals.size());
    for (const auto &portal : ordered_portals) {
        const save::Cell surface(static_cast<std::int16_t>(portal.surface.x),
                                 static_cast<std::int16_t>(portal.surface.y),
                                 static_cast<save::Layer>(portal.surface.layer));
        const save::Cell mine(static_cast<std::int16_t>(portal.mine.x),
                              static_cast<std::int16_t>(portal.mine.y),
                              static_cast<save::Layer>(portal.mine.layer));
        portals.push_back(save::CreatePortal(builder, &surface, &mine));
    }

    std::vector<flatbuffers::Offset<flatbuffers::String>> events;
    events.reserve(state.events.size());
    for (const std::string &event : state.events)
        events.push_back(builder.CreateString(event));

    const auto save_cell = [](const CellCoord &cell) {
        return save::Cell(static_cast<std::int16_t>(cell.x), static_cast<std::int16_t>(cell.y),
                          static_cast<save::Layer>(cell.layer));
    };
    std::vector<flatbuffers::Offset<save::PathBatch>> path_batches;
    path_batches.reserve(state.path_batches.size());
    for (const PathBatchState &batch : state.path_batches) {
        std::vector<save::Cell> roots;
        roots.reserve(batch.roots.size());
        for (const CellCoord &cell : batch.roots) roots.push_back(save_cell(cell));
        std::vector<save::Cell> frontier;
        frontier.reserve(batch.frontier.size());
        for (const CellCoord &cell : batch.frontier) frontier.push_back(save_cell(cell));
        std::vector<flatbuffers::Offset<save::PathSearchNode>> nodes;
        nodes.reserve(batch.nodes.size());
        for (const PathSearchNodeState &node : batch.nodes) {
            const save::Cell cell = save_cell(node.cell);
            const save::Cell next = save_cell(node.next);
            nodes.push_back(save::CreatePathSearchNode(builder, &cell, &next));
        }
        std::vector<flatbuffers::Offset<save::PathRouteDraft>> drafts;
        drafts.reserve(batch.drafts.size());
        for (const PathRouteDraftState &draft : batch.drafts) {
            const save::Cell start = save_cell(draft.start);
            std::optional<save::Cell> cursor;
            if (draft.reconstruction_cursor) cursor = save_cell(*draft.reconstruction_cursor);
            std::vector<save::Cell> staged;
            staged.reserve(draft.staged.size());
            for (const CellCoord &cell : draft.staged) staged.push_back(save_cell(cell));
            drafts.push_back(save::CreatePathRouteDraft(
                builder, draft.resident_id, &start, draft.start_x, draft.start_y, draft.storage_reserved,
                cursor ? &*cursor : nullptr, builder.CreateVectorOfStructs(staged), draft.manual_order_id,
                draft.resolved, static_cast<save::PathFailure>(draft.failure)));
        }
        const save::Cell target = save_cell(batch.target);
        const auto reserved = save::CreateResources(builder, batch.reserved_cost.wood,
                                                     batch.reserved_cost.stone,
                                                     batch.reserved_cost.food);
        flatbuffers::Optional<save::BuildingKind> build_kind = flatbuffers::nullopt;
        if (batch.build_kind) build_kind = static_cast<save::BuildingKind>(*batch.build_kind);
        path_batches.push_back(save::CreatePathBatch(
            builder, batch.path_batch_id, static_cast<save::PathCommandKind>(batch.command),
            static_cast<save::PathPlannerPhase>(batch.phase), builder.CreateVector(batch.resident_ids),
            &target, batch.to_adjacent, static_cast<save::Job>(batch.assigned_job), build_kind,
            batch.building_id, reserved, batch.enqueued_tick, batch.deadline_tick,
            batch.search_topology_revision, batch.restart_count, batch.work_units_done,
            builder.CreateVectorOfStructs(roots), batch.search_storage_reserved,
            builder.CreateVectorOfStructs(frontier), batch.frontier_cursor, builder.CreateVector(nodes),
            builder.CreateVector(drafts), batch.reconstruction_resident, batch.stage_cursor,
            batch.legacy_atomic, batch.legacy_grid));
    }

    std::vector<flatbuffers::Offset<save::PathBatchOutcome>> path_results;
    path_results.reserve(state.path_results.size());
    for (const PathBatchOutcome &outcome : state.path_results) {
        path_results.push_back(save::CreatePathBatchOutcome(
            builder, outcome.path_batch_id, static_cast<save::PathCommandKind>(outcome.command),
            outcome.accepted, static_cast<save::PathFailure>(outcome.failure),
            builder.CreateString(outcome.message)));
    }

    std::vector<flatbuffers::Offset<save::ItemStack>> item_stacks;
    item_stacks.reserve(state.item_stacks.size());
    for (const auto &stack : state.item_stacks) {
        const auto cell = save_cell(stack.location.cell);
        item_stacks.push_back(save::CreateItemStack(
            builder, stack.stack_id, static_cast<save::ItemKind>(stack.item), stack.quantity,
            stack.owner_nation, static_cast<save::ItemLocationKind>(stack.location.kind), &cell,
            stack.location.entity_id));
    }
    std::vector<flatbuffers::Offset<save::Storage>> storages;
    storages.reserve(state.storages.size());
    for (const auto &storage : state.storages)
        storages.push_back(save::CreateStorage(builder, storage.building_id, storage.capacity,
                                               storage.accepted_items, storage.priority));
    std::vector<flatbuffers::Offset<save::ItemReservation>> reservations;
    reservations.reserve(state.item_reservations.size());
    for (const auto &reservation : state.item_reservations)
        reservations.push_back(save::CreateItemReservation(
            builder, reservation.reservation_id, reservation.stack_id, reservation.task_id,
            reservation.quantity, reservation.owner_nation));
    std::vector<flatbuffers::Offset<save::LogisticsTask>> logistics_tasks;
    logistics_tasks.reserve(state.logistics_tasks.size());
    for (const auto &task : state.logistics_tasks) {
        const auto resume_target = save_cell(task.resume_target);
        logistics_tasks.push_back(save::CreateLogisticsTask(
            builder, task.task_id, static_cast<save::LogisticsTaskKind>(task.kind),
            static_cast<save::LogisticsPhase>(task.phase),
            static_cast<save::LogisticsFailure>(task.failure), task.resident_id,
            task.destination_building_id, task.source_stack_id, task.reservation_id,
            task.carry_stack_id, task.path_batch_id, static_cast<save::ItemKind>(task.item),
            task.quantity, task.deadline_tick, static_cast<save::Job>(task.resume_job),
            &resume_target));
    }
    std::vector<flatbuffers::Offset<save::ConstructionOrder>> construction_orders;
    construction_orders.reserve(state.construction_orders.size());
    for (const auto &order : state.construction_orders) {
        const auto site = save_cell(order.site);
        const auto required = save::CreateResources(builder, order.required.wood,
                                                    order.required.stone, order.required.food);
        const auto delivered = save::CreateResources(builder, order.delivered.wood,
                                                     order.delivered.stone, order.delivered.food);
        construction_orders.push_back(save::CreateConstructionOrder(
            builder, order.building_id, static_cast<save::BuildingKind>(order.kind), &site,
            order.owner_nation, required, delivered, static_cast<save::ConstructionPhase>(order.phase),
            order.originating_path_batch_id));
    }
    std::vector<flatbuffers::Offset<save::WorkIntent>> work_intents;
    work_intents.reserve(state.work_intents.size());
    for (const auto &intent : state.work_intents) {
        const auto target = save_cell(intent.target);
        work_intents.push_back(save::CreateWorkIntent(
            builder, intent.intent_id, static_cast<save::WorkIntentKind>(intent.kind),
            static_cast<save::WorkIntentPhase>(intent.phase),
            static_cast<save::WorkCompletionReason>(intent.completion),
            static_cast<save::PathFailure>(intent.failure), intent.resident_id, intent.building_id,
            intent.owner_nation, &target, static_cast<save::Job>(intent.job),
            static_cast<save::Terrain>(intent.expected_terrain), intent.created_tick,
            intent.next_attempt_tick, intent.admission_deadline_tick, intent.path_batch_id,
            intent.attempts, intent.last_attempt_topology_revision));
    }
    std::vector<flatbuffers::Offset<save::ManualOrder>> manual_orders;
    manual_orders.reserve(state.manual_orders.size());
    for (const auto &order : state.manual_orders) {
        std::optional<save::Cell> target;
        if (order.request.target)
            target = save_cell(*order.request.target);
        flatbuffers::Offset<save::ManualOrderIssuer> issuer;
        if (order.issuer)
            issuer =
                save::CreateManualOrderIssuer(builder, order.issuer->player_id, order.issuer->request_id);
        flatbuffers::Optional<std::uint64_t> issued_tick = flatbuffers::nullopt;
        flatbuffers::Optional<std::uint64_t> activated_tick = flatbuffers::nullopt;
        if (order.issued_tick)
            issued_tick = *order.issued_tick;
        if (order.activated_tick)
            activated_tick = *order.activated_tick;
        manual_orders.push_back(save::CreateManualOrder(
            builder, order.order_id, order.resident_id, order.owner_nation,
            static_cast<save::ManualOrderKind>(order.request.kind), target ? &*target : nullptr,
            static_cast<save::GatherCompletion>(order.request.gather_completion),
            static_cast<save::ManualOrderPhase>(order.phase), static_cast<save::PathFailure>(order.failure),
            static_cast<save::ManualOrderCompletion>(order.completion), issuer, issued_tick,
            order.legacy_compatibility, static_cast<save::ManualExecutorKind>(order.executor),
            order.executor_id, activated_tick, order.next_attempt_tick, order.attempts));
    }
    std::vector<save::YoungResident> young;
    for(const auto &y:state.settlement.young) young.emplace_back(y.resident_id,y.adult_tick,y.forced_work);
    const auto society = save::CreateSettlement(builder,state.settlement.revision,state.settlement.arrival_tick,
        state.settlement.forced_night,state.settlement.night_discontent,state.settlement.youth_discontent,
        builder.CreateVectorOfStructs(young),state.settlement.reduced_rations);
    const auto economy = builder.CreateString(encode_economy(state.economy));
    const auto& climate = state.climate;
    const auto& sample = climate.current;
    const auto current_weather = save::CreateWeatherSample(
        builder, static_cast<save::WeatherKind>(sample.kind),
        static_cast<save::PrecipitationKind>(sample.precipitation),
        static_cast<save::WindDirection>(sample.wind_direction),
        static_cast<save::WeatherWorkReason>(sample.work_reason), sample.temperature_deci_c,
        sample.precipitation_permille, sample.wind_permille, sample.outdoor_work_rate_bp,
        sample.period_start_calendar_tick, sample.period_end_calendar_tick);
    std::vector<flatbuffers::Offset<save::OutdoorWorkRemainder>> weather_work;
    weather_work.reserve(climate.outdoor_work_remainders.size());
    for (const auto& record : climate.outdoor_work_remainders) {
        const auto target = save_cell(record.target);
        weather_work.push_back(save::CreateOutdoorWorkRemainder(
            builder, record.resident_id, static_cast<save::Job>(record.job), &target,
            record.building_id, record.remainder_bp));
    }
    const auto climate_runtime = save::CreateClimateRuntime(
        builder, climate.algorithm_version, static_cast<save::ClimateProfileId>(climate.climate_profile),
        static_cast<save::ClimateScenarioId>(climate.scenario), climate.calendar_offset_ticks,
        climate.evaluated_simulation_tick, current_weather, climate.snow_cover_permille,
        climate.snow_cover_remainder, climate.wetness_permille, climate.wetness_remainder,
        builder.CreateVector(weather_work));
    const auto root = save::CreateWorldSave(
        builder, metadata, resources, legacy_tiles, builder.CreateVector(residents),
        builder.CreateVector(buildings), builder.CreateVector(routes), builder.CreateVector(events),
        builder.CreateVector(chunks), builder.CreateVector(portals), state.next_path_batch_id,
        state.topology_revision, builder.CreateVector(path_batches), builder.CreateVector(path_results),
        state.next_item_stack_id, state.next_item_reservation_id, state.next_logistics_task_id,
        builder.CreateVector(item_stacks), builder.CreateVector(storages), builder.CreateVector(reservations),
        builder.CreateVector(logistics_tasks), builder.CreateVector(construction_orders),
        state.next_work_intent_id, state.work_intent_cursor_id, state.work_building_cursor_id,
        builder.CreateVector(work_intents), state.profession_cursor_id, state.next_manual_order_id,
        state.manual_order_cursor_id, builder.CreateVector(manual_orders), society, economy, builder.CreateString(state.server_metadata), climate_runtime);
    save::FinishWorldSaveBuffer(builder, root);
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

bool verify_flatbuffer(std::span<const std::uint8_t> bytes, const save::WorldSave *&root,
                       std::string &error) {
    flatbuffers::Verifier verifier(bytes.data(), bytes.size(), 64, 2'000'000);
    if (!save::VerifyWorldSaveBuffer(verifier)) {
        error = "save.climate_payload_invalid";
        return false;
    }
    root = save::GetWorldSave(bytes.data());
    return true;
}

bool has_inventory_fields(const save::WorldSave &root) {
    return root.item_stacks() || root.storages() || root.item_reservations() ||
           root.logistics_tasks() || root.construction_orders() ||
           root.next_item_stack_id() != 1 || root.next_item_reservation_id() != 1 ||
           root.next_logistics_task_id() != 1;
}

bool has_work_intent_fields(const save::WorldSave &root) {
    return root.work_intents() || root.next_work_intent_id() != 1 ||
           root.work_intent_cursor_id() != 1 || root.work_building_cursor_id() != 1;
}

bool has_profession_fields(const save::WorldSave &root) {
    if (flatbuffers::IsFieldPresent(&root, save::WorldSave::VT_PROFESSION_CURSOR_ID))
        return true;
    return std::any_of(root.residents()->begin(), root.residents()->end(), [](const auto *resident) {
        return resident && (resident->additional_xp() || resident->skill_practice() ||
                            resident->abilities_bp() || resident->profession());
    });
}

bool has_manual_order_fields(const save::WorldSave &root) {
    if (root.manual_orders() ||
        flatbuffers::IsFieldPresent(&root, save::WorldSave::VT_NEXT_MANUAL_ORDER_ID) ||
        flatbuffers::IsFieldPresent(&root, save::WorldSave::VT_MANUAL_ORDER_CURSOR_ID))
        return true;
    if (root.path_batches())
        for (const auto *batch : *root.path_batches()) {
            if (!batch)
                continue;
            if (batch->command() > save::PathCommandKind::Work ||
                flatbuffers::IsFieldPresent(batch, save::PathBatch::VT_LEGACY_ATOMIC))
                return true;
            if (batch->drafts())
                for (const auto *draft : *batch->drafts()) {
                    if (draft &&
                        (flatbuffers::IsFieldPresent(draft, save::PathRouteDraft::VT_MANUAL_ORDER_ID) ||
                         flatbuffers::IsFieldPresent(draft, save::PathRouteDraft::VT_RESOLVED) ||
                         flatbuffers::IsFieldPresent(draft, save::PathRouteDraft::VT_FAILURE)))
                        return true;
                }
        }
    if (root.path_results())
        for (const auto *outcome : *root.path_results())
            if (outcome && outcome->command() > save::PathCommandKind::Work)
                return true;
    return false;
}

bool parse_manual_orders(const save::WorldSave &root, WorldState &state, std::string &error) {
    if (!root.manual_orders() || root.manual_orders()->size() > max_manual_orders ||
        root.next_manual_order_id() == 0 || root.manual_order_cursor_id() == 0) {
        error = "Manual order collection bounds are invalid.";
        return false;
    }
    state.next_manual_order_id = root.next_manual_order_id();
    state.manual_order_cursor_id = root.manual_order_cursor_id();
    state.manual_orders.reserve(root.manual_orders()->size());
    for (const auto *source : *root.manual_orders()) {
        if (!source || source->kind() > save::ManualOrderKind::Legacy ||
            source->gather_completion() > save::GatherCompletion::OneDelivery ||
            source->phase() > save::ManualOrderPhase::Cancelled ||
            source->completion() > save::ManualOrderCompletion::Cancelled ||
            source->executor() > save::ManualExecutorKind::Logistics ||
            source->failure() < save::PathFailure::None ||
            source->failure() > save::PathFailure::ReservationConflict ||
            (source->kind() != save::ManualOrderKind::Legacy && !source->target())) {
            error = "Manual order data is invalid.";
            return false;
        }
        ManualOrderState order;
        order.order_id = source->order_id();
        order.resident_id = source->resident_id();
        order.owner_nation = source->owner_nation();
        order.request.kind = static_cast<ManualOrderKind>(source->kind());
        if (const auto *target = source->target())
            order.request.target =
                CellCoord{target->x(), target->y(), static_cast<WorldLayer>(target->layer())};
        order.request.gather_completion = static_cast<GatherCompletion>(source->gather_completion());
        order.phase = static_cast<ManualOrderPhase>(source->phase());
        order.failure = static_cast<PathFailure>(source->failure());
        order.completion = static_cast<ManualOrderCompletion>(source->completion());
        if (const auto *issuer = source->issuer())
            order.issuer = ManualOrderIssuer{issuer->player_id(), issuer->request_id()};
        if (source->issued_tick())
            order.issued_tick = source->issued_tick().value();
        order.legacy_compatibility = source->legacy_compatibility();
        order.executor = static_cast<ManualExecutorKind>(source->executor());
        order.executor_id = source->executor_id();
        if (source->activated_tick())
            order.activated_tick = source->activated_tick().value();
        order.next_attempt_tick = source->next_attempt_tick();
        order.attempts = source->attempts();
        state.manual_orders.push_back(std::move(order));
    }
    return true;
}

bool parse_profession(const save::Resident &source, Resident &resident, std::uint64_t ticks,
                      std::string &error) {
    const auto *p = source.profession();
    if (!source.additional_xp() || source.additional_xp()->size() != skill_count - 3 ||
        !source.skill_practice() || source.skill_practice()->size() != skill_count ||
        !source.abilities_bp() || source.abilities_bp()->size() != skill_count ||
        !p || !p->priorities() || p->priorities()->size() != 5) {
        error = "Resident profession collection bounds are invalid.";
        return false;
    }
    std::copy(source.additional_xp()->begin(), source.additional_xp()->end(), resident.additional_xp.begin());
    std::copy(source.abilities_bp()->begin(), source.abilities_bp()->end(), resident.abilities_bp.begin());
    if (std::any_of(resident.additional_xp.begin(), resident.additional_xp.end(), [](int xp) {
            return xp < 0 || xp > maximum_skill_experience;
        }) || std::any_of(resident.abilities_bp.begin(), resident.abilities_bp.end(), [](int bp) {
            return bp < -1500 || bp > 1500;
        })) {
        error = "Resident skill values are invalid.";
        return false;
    }
    for (std::size_t i = 0; i < skill_count; ++i) {
        const auto *practice = source.skill_practice()->Get(static_cast<flatbuffers::uoffset_t>(i));
        if (!practice || practice->experience_remainder() >= 4000 ||
            practice->familiarity_debt() > 30000 || practice->last_practice_tick() > ticks) {
            error = "Resident skill practice is invalid.";
            return false;
        }
        resident.skill_practice[i] = {practice->experience_remainder(), practice->familiarity_debt(),
                                      practice->last_practice_tick()};
    }
    auto &destination = resident.profession;
    destination.assignment.profession = static_cast<Profession>(p->profession());
    if (p->worksite())
        destination.assignment.worksite = CellCoord{p->worksite()->x(), p->worksite()->y(),
                                                     static_cast<WorldLayer>(p->worksite()->layer())};
    std::copy(p->priorities()->begin(), p->priorities()->end(), destination.assignment.priorities.begin());
    destination.assignment.automation_enabled = p->automation_enabled();
    destination.manual_override = p->manual_override();
    destination.deferred = p->deferred();
    destination.wait_reason = static_cast<ProfessionWaitReason>(p->wait_reason());
    destination.next_evaluation_tick = p->next_evaluation_tick();
    return true;
}

bool parse_work_intents(const save::WorldSave &root, WorldState &state, std::string &error) {
    if (!root.work_intents() || root.work_intents()->size() > max_work_intents) {
        error = "Work intent collection bounds are invalid.";
        return false;
    }
    state.next_work_intent_id = root.next_work_intent_id();
    state.work_intent_cursor_id = root.work_intent_cursor_id();
    state.work_building_cursor_id = root.work_building_cursor_id();
    state.work_intents.reserve(root.work_intents()->size());
    for (const auto *intent : *root.work_intents()) {
        if (!intent || !intent->target()) {
            error = "Work intent data is missing.";
            return false;
        }
        const auto &target = *intent->target();
        state.work_intents.push_back({
            intent->intent_id(), static_cast<WorkIntentKind>(intent->kind()),
            static_cast<WorkIntentPhase>(intent->phase()),
            static_cast<WorkCompletionReason>(intent->completion()),
            static_cast<PathFailure>(intent->failure()), intent->resident_id(), intent->building_id(),
            intent->owner_nation(), {target.x(), target.y(), static_cast<WorldLayer>(target.layer())},
            static_cast<Job>(intent->job()), static_cast<Terrain>(intent->expected_terrain()),
            intent->created_tick(), intent->next_attempt_tick(), intent->admission_deadline_tick(),
            intent->path_batch_id(), intent->attempts(), intent->last_attempt_topology_revision()});
    }
    return true;
}

bool parse_inventory(const save::WorldSave &root, WorldState &state, std::string &error) {
    if (!root.item_stacks() || !root.storages() || !root.item_reservations() ||
        !root.logistics_tasks() || !root.construction_orders() ||
        root.item_stacks()->size() > max_item_stacks ||
        root.item_reservations()->size() > max_item_reservations ||
        root.logistics_tasks()->size() > max_logistics_tasks ||
        root.storages()->size() > root.buildings()->size() ||
        root.construction_orders()->size() >
            static_cast<std::size_t>(world_size * world_size) + max_path_batches) {
        error = "Inventory collection bounds are invalid.";
        return false;
    }
    const auto load_cell = [](const save::Cell &cell) {
        return CellCoord{cell.x(), cell.y(), static_cast<WorldLayer>(cell.layer())};
    };
    state.next_item_stack_id = root.next_item_stack_id();
    state.next_item_reservation_id = root.next_item_reservation_id();
    state.next_logistics_task_id = root.next_logistics_task_id();
    state.item_stacks.reserve(root.item_stacks()->size());
    for (const auto *stack : *root.item_stacks()) {
        if (!stack || !stack->cell()) {
            error = "Item stack data is missing.";
            return false;
        }
        state.item_stacks.push_back({
            stack->stack_id(), static_cast<ItemKind>(stack->item()), stack->quantity(),
            stack->owner_nation(), {static_cast<ItemLocationKind>(stack->location_kind()),
                                    load_cell(*stack->cell()), stack->entity_id()}});
    }
    state.storages.reserve(root.storages()->size());
    for (const auto *storage : *root.storages()) {
        if (!storage) { error = "Storage data is missing."; return false; }
        state.storages.push_back({storage->building_id(), storage->capacity(),
                                  storage->accepted_items(), storage->priority()});
    }
    state.item_reservations.reserve(root.item_reservations()->size());
    for (const auto *reservation : *root.item_reservations()) {
        if (!reservation) { error = "Item reservation data is missing."; return false; }
        state.item_reservations.push_back({reservation->reservation_id(), reservation->stack_id(),
                                           reservation->task_id(), reservation->quantity(),
                                           reservation->owner_nation()});
    }
    state.logistics_tasks.reserve(root.logistics_tasks()->size());
    for (const auto *task : *root.logistics_tasks()) {
        if (!task || !task->resume_target()) {
            error = "Logistics task data is missing.";
            return false;
        }
        state.logistics_tasks.push_back({
            task->task_id(), static_cast<LogisticsTaskKind>(task->kind()),
            static_cast<LogisticsPhase>(task->phase()),
            static_cast<LogisticsFailure>(task->failure()), task->resident_id(),
            task->destination_building_id(), task->source_stack_id(), task->reservation_id(),
            task->carry_stack_id(), task->path_batch_id(), static_cast<ItemKind>(task->item()),
            task->quantity(), task->deadline_tick(), static_cast<Job>(task->resume_job()),
            load_cell(*task->resume_target())});
    }
    state.construction_orders.reserve(root.construction_orders()->size());
    for (const auto *order : *root.construction_orders()) {
        if (!order || !order->site() || !order->required() || !order->delivered()) {
            error = "Construction order data is missing.";
            return false;
        }
        state.construction_orders.push_back({
            order->building_id(), static_cast<BuildingKind>(order->kind()), load_cell(*order->site()),
            order->owner_nation(),
            {order->required()->wood(), order->required()->stone(), order->required()->food()},
            {order->delivered()->wood(), order->delivered()->stone(), order->delivered()->food()},
            static_cast<ConstructionPhase>(order->phase()), order->originating_path_batch_id()});
    }
    return true;
}

bool parse_climate(const save::WorldSave& root, WorldState& state, std::string& error) {
    const auto* climate = root.climate();
    if (!climate || !climate->current() || !climate->outdoor_work_remainders()) {
        error = "save.climate_missing";
        return false;
    }
    const auto* rows = climate->outdoor_work_remainders();
    if (rows->size() > max_outdoor_work_records) {
        error = "save.climate_work_bounds";
        return false;
    }
    auto& destination = state.climate;
    destination.algorithm_version = climate->algorithm_version();
    destination.climate_profile = static_cast<ClimateProfileId>(climate->climate_profile());
    destination.scenario = static_cast<ClimateScenarioId>(climate->scenario());
    destination.calendar_offset_ticks = climate->calendar_offset_ticks();
    destination.evaluated_simulation_tick = climate->evaluated_simulation_tick();
    destination.snow_cover_permille = climate->snow_cover_permille();
    destination.snow_cover_remainder = climate->snow_cover_remainder();
    destination.wetness_permille = climate->wetness_permille();
    destination.wetness_remainder = climate->wetness_remainder();
    const auto& current = *climate->current();
    destination.current = {
        static_cast<WeatherKind>(current.kind()), static_cast<PrecipitationKind>(current.precipitation()),
        static_cast<WindDirection>(current.wind_direction()), static_cast<WeatherWorkReason>(current.work_reason()),
        current.temperature_deci_c(), current.precipitation_permille(), current.wind_permille(),
        current.outdoor_work_rate_bp(), current.period_start_calendar_tick(), current.period_end_calendar_tick()};
    // Bound and validate every key before allocating the destination vector.
    int previous{};
    for (const auto* row : *rows) {
        const auto job = row ? static_cast<Job>(row->job()) : Job::Idle;
        const auto* target = row ? row->target() : nullptr;
        if (!row || !target || row->resident_id() <= previous || row->remainder_bp() >= 10000u ||
            (job != Job::Woodcut && job != Job::Mine && job != Job::Forage && job != Job::Build) ||
            target->layer() != save::Layer::Surface || !inside_world(target->x(), target->y()) ||
            (job == Job::Build ? row->building_id() <= 0 : row->building_id() != 0)) {
            error = "save.climate_work_binding_invalid";
            return false;
        }
        previous = row->resident_id();
    }
    destination.outdoor_work_remainders.reserve(rows->size());
    for (const auto* row : *rows) {
        const auto& target = *row->target();
        destination.outdoor_work_remainders.push_back({
            row->resident_id(), static_cast<Job>(row->job()),
            CellCoord{target.x(), target.y(), static_cast<WorldLayer>(target.layer())},
            row->building_id(), row->remainder_bp()});
    }
    return validate_save_climate(state, error);
}

bool parse_flatbuffer_current(std::span<const std::uint8_t> bytes, WorldState &state, bool current,
                               bool physical_inventory, bool work_intents, bool professions,
                               bool manual_orders, std::string &error) {
    const save::WorldSave *root = nullptr;
    if (!verify_flatbuffer(bytes, root, error))
        return false;
    if(root->server_metadata()){if(root->server_metadata()->size()>65536){error="Server metadata too large.";return false;}state.server_metadata=root->server_metadata()->str();}
    try { state.economy=decode_economy(root->economy()?root->economy()->str():""); }
    catch(const std::exception&){error="Invalid economy payload.";return false;}
    if (const auto *p=root->settlement()) {
        if(!manual_orders || (p->young() && p->young()->size()>1500)) {error="Invalid settlement payload.";return false;}
        state.settlement={p->revision(),p->arrival_tick(),p->forced_night(),p->night_discontent(),p->youth_discontent(),{},p->reduced_rations()};
        if(p->young()) for(const auto *y:*p->young()) state.settlement.young.push_back({y->resident_id(),y->adult_tick(),y->forced_work()});
    }
    if (!professions && has_profession_fields(*root)) {
        error = "Legacy save contains profession fields.";
        return false;
    }
    if (!manual_orders && has_manual_order_fields(*root)) {
        error = "Legacy save contains manual order fields.";
        return false;
    }
    if (manual_orders && !parse_manual_orders(*root, state, error))
        return false;
    if (professions) {
        state.profession_cursor_id = root->profession_cursor_id();
        if (state.profession_cursor_id <= 0) {
            error = "Profession cursor is invalid.";
            return false;
        }
    }
    if (!work_intents && has_work_intent_fields(*root)) {
        error = "Legacy save contains work intent fields.";
        return false;
    }
    if (!physical_inventory && has_inventory_fields(*root)) {
        error = "Legacy save contains inventory fields.";
        return false;
    }
    if (!root->chunks() || !root->portals() || !root->tiles()->empty() ||
        (root->chunks()->size() != 128 && root->chunks()->size() != world_chunk_count) || root->residents()->size() > 6000 ||
        root->buildings()->size() > static_cast<std::size_t>(world_size * world_size) ||
        root->routes()->size() != root->residents()->size() || root->portals()->size() > 64 ||
        root->events()->size() > 64) {
        error = "Save grid authority is invalid.";
        return false;
    }
    if (work_intents && !parse_work_intents(*root, state, error)) return false;
    if (current) {
        if (!root->path_batches() || !root->path_results() ||
            root->path_batches()->size() > max_path_batches ||
            root->path_results()->size() >
                max_path_batches - root->path_batches()->size()) {
            error = "Planner collection bounds are invalid.";
            return false;
        }
    } else if (root->path_batches() || root->path_results() || root->next_path_batch_id() != 1 ||
               root->topology_revision() != 0) {
        error = "Version 3 planner fields are invalid.";
        return false;
    }
    const save::Metadata *metadata = root->metadata();
    if (metadata->world_id()->size() != state.world_id.size()) {
        error = "World id length is invalid.";
        return false;
    }
    std::copy(metadata->world_id()->begin(), metadata->world_id()->end(), state.world_id.begin());
    state.seed = metadata->seed();
    state.ticks = metadata->ticks();
    state.next_building_id = metadata->next_building_id();
    state.has_moved = metadata->has_moved();
    state.has_gathered = metadata->has_gathered();
    state.has_built = metadata->has_built();
    if (!parse_climate(*root, state, error)) return false;
    state.resources = {root->resources()->wood(), root->resources()->stone(),
                       root->resources()->food()};

    state.chunks.reserve(root->chunks()->size());
    for (const save::Chunk *chunk : *root->chunks()) {
        if (!chunk || !chunk->tiles() || chunk->tiles()->size() != chunk_size * chunk_size) {
            error = "Chunk tile count is invalid.";
            return false;
        }
        Chunk result;
        result.key = {static_cast<WorldLayer>(chunk->layer()), chunk->cx(), chunk->cy()};
        result.revision = chunk->revision();
        std::size_t index = 0;
        for (const save::Tile *tile : *chunk->tiles()) {
            if (!tile) {
                error = "Chunk tile is missing.";
                return false;
            }
            result.tiles[index++] = {static_cast<Terrain>(tile->terrain()), tile->amount(),
                                     tile->explored(), static_cast<Biome>(tile->biome()), tile->elevation(),
                                     tile->trail_wear()};
        }
        state.chunks.push_back(std::move(result));
    }
    state.residents.reserve(root->residents()->size());
    for (const save::Resident *resident : *root->residents()) {
        if (!resident || !resident->name() || resident->name()->size() > 128) {
            error = "Resident data is missing.";
            return false;
        }
        if (!physical_inventory && resident->job() > save::Job::Build) {
            error = "Legacy resident job is invalid.";
            return false;
        }
        if (!current && (resident->order_phase() != save::OrderPhase::Idle ||
                         resident->path_failure() != save::PathFailure::None ||
                         resident->pending_path_batch() != 0)) {
            error = "Version 3 resident planner fields are invalid.";
            return false;
        }
        state.residents.push_back({resident->id(), resident->name()->str(), resident->x(),
                                   resident->y(), resident->target_x(), resident->target_y(),
                                   static_cast<Job>(resident->job()), resident->progress(),
                                   resident->health(), resident->hunger(), resident->wood_xp(),
                                   resident->mining_xp(), resident->farming_xp(),
                                   static_cast<WorldLayer>(resident->layer()), resident->nation(),
                                   current ? static_cast<OrderPhase>(resident->order_phase()) : OrderPhase::Idle,
                                   current ? static_cast<PathFailure>(resident->path_failure()) : PathFailure::None,
                                   current ? resident->pending_path_batch() : 0});
        if (professions && !parse_profession(*resident, state.residents.back(), state.ticks, error))
            return false;
    }
    state.buildings.reserve(root->buildings()->size());
    for (const save::Building *building : *root->buildings()) {
        if (!building) {
            error = "Building data is missing.";
            return false;
        }
        state.buildings.push_back({building->id(), static_cast<BuildingKind>(building->kind()),
                                   building->x(), building->y(), building->progress(),
                                   static_cast<WorldLayer>(building->layer()), building->nation()});
    }
    state.routes.reserve(root->routes()->size());
    for (const save::Route *route : *root->routes()) {
        if (!route || !route->cells() || route->cells()->size() > max_path_cells ||
            (route->points() && !route->points()->empty()) || route->work_x() != -1 ||
            route->work_y() != -1) {
            error = "Route authority is invalid.";
            return false;
        }
        RouteState result;
        result.resident_id = route->resident_id();
        result.cursor = route->cursor();
        result.points.reserve(route->cells()->size());
        for (const save::Cell *point : *route->cells()) {
            result.points.push_back(
                {point->x(), point->y(), static_cast<WorldLayer>(point->layer())});
        }
        if (const save::Cell *work = route->work()) {
            result.work = CellCoord{work->x(), work->y(), static_cast<WorldLayer>(work->layer())};
        }
        state.routes.push_back(std::move(result));
    }
    if (!current) {
        std::map<int, bool> route_is_active;
        for (const auto &route : state.routes)
            route_is_active[route.resident_id] = route.cursor < route.points.size();
        for (auto &resident : state.residents) {
            if (resident.job != Job::Idle || route_is_active[resident.id])
                resident.order_phase = OrderPhase::Active;
        }
    }
    state.portals.reserve(root->portals()->size());
    for (const save::Portal *portal : *root->portals()) {
        if (!portal || !portal->first() || !portal->second()) {
            error = "Portal data is missing.";
            return false;
        }
        const save::Cell *first = portal->first();
        const save::Cell *second = portal->second();
        state.portals.push_back(
            {{first->x(), first->y(), static_cast<WorldLayer>(first->layer())},
             {second->x(), second->y(), static_cast<WorldLayer>(second->layer())}});
    }
    state.events.reserve(root->events()->size());
    for (const flatbuffers::String *event : *root->events()) {
        if (!event || event->size() > 512) {
            error = "Event data is missing.";
            return false;
        }
        state.events.push_back(event->str());
    }
    if (!current) return true;

    std::size_t planner_bytes = root->path_results()->size() * legacy_path_outcome_logical_bytes;
    if (planner_bytes > max_planner_bytes) {
        error = "Planner aggregate size is invalid.";
        return false;
    }
    const auto add_planner_bytes = [&](std::size_t count, std::size_t unit) {
        if (count > (max_planner_bytes - planner_bytes) / unit) return false;
        planner_bytes += count * unit;
        return true;
    };
    constexpr std::size_t search_cell_capacity =
        max_search_cells;
    std::size_t staged_cells = 0;
    for (const save::PathBatch *batch : *root->path_batches()) {
        if (batch && !work_intents && batch->command() > save::PathCommandKind::Logistics) {
            error = "Legacy planner work command is invalid.";
            return false;
        }
        if (batch && !physical_inventory &&
            (batch->command() > save::PathCommandKind::Replan ||
             batch->assigned_job() > save::Job::Build)) {
            error = "Legacy planner command is invalid.";
            return false;
        }
        if (!batch || !batch->resident_ids() || !batch->target() || !batch->reserved_cost() ||
            !batch->roots() || !batch->frontier() || !batch->nodes() || !batch->drafts() ||
            (batch->resident_ids()->empty() && batch->command() != save::PathCommandKind::Build) ||
            batch->resident_ids()->size() > max_path_batch_residents || batch->roots()->size() > 4 ||
            batch->frontier()->size() > search_cell_capacity ||
            batch->nodes()->size() > search_cell_capacity ||
            batch->drafts()->size() > max_path_batch_residents ||
            batch->frontier_cursor() > batch->frontier()->size() ||
            batch->reconstruction_resident() > batch->drafts()->size() ||
            batch->stage_cursor() > batch->drafts()->size() ||
            !add_planner_bytes(1, batch->legacy_atomic() ? legacy_path_batch_logical_bytes
                                                         : sizeof(PathBatchState)) ||
            !add_planner_bytes(batch->resident_ids()->size(), sizeof(int)) ||
            !add_planner_bytes(batch->roots()->size(), sizeof(CellCoord)) ||
            !add_planner_bytes(batch->search_storage_reserved() ? search_cell_capacity
                                                                : batch->frontier()->size(),
                               sizeof(CellCoord)) ||
            !add_planner_bytes(batch->search_storage_reserved() ? search_cell_capacity
                                                                : batch->nodes()->size(),
                               sizeof(PathSearchNodeState)) ||
            !add_planner_bytes(batch->nodes()->size(), 40) ||
            !add_planner_bytes(
                batch->drafts()->size(),
                (batch->legacy_atomic() ? legacy_path_draft_logical_bytes : sizeof(PathRouteDraftState)) +
                    40)) {
            error = "Planner batch bounds are invalid.";
            return false;
        }
        for (const save::PathSearchNode *node : *batch->nodes()) {
            if (!node || !node->cell() || !node->next()) {
                error = "Planner search node is missing.";
                return false;
            }
        }
        for (const save::PathRouteDraft *draft : *batch->drafts()) {
            if (!draft || !draft->start() || !draft->staged() || draft->staged()->size() > max_path_cells ||
                staged_cells > max_staged_route_cells - draft->staged()->size() ||
                !add_planner_bytes(draft->storage_reserved() ? max_path_cells
                                                             : draft->staged()->size(),
                                   sizeof(CellCoord))) {
                error = "Planner route draft bounds are invalid.";
                return false;
            }
            staged_cells += draft->staged()->size();
        }
    }
    for (const save::PathBatchOutcome *outcome : *root->path_results()) {
        if (outcome && !work_intents && outcome->command() > save::PathCommandKind::Logistics) {
            error = "Legacy planner work outcome is invalid.";
            return false;
        }
        if (outcome && !physical_inventory && outcome->command() > save::PathCommandKind::Replan) {
            error = "Legacy planner outcome is invalid.";
            return false;
        }
        if (!outcome || !outcome->message() || outcome->message()->size() > 512 ||
            !add_planner_bytes(outcome->message()->size(), sizeof(char))) {
            error = "Planner result bounds are invalid.";
            return false;
        }
    }

    state.next_path_batch_id = root->next_path_batch_id();
    state.topology_revision = root->topology_revision();
    state.path_batches.reserve(root->path_batches()->size());
    const auto load_cell = [](const save::Cell *cell) {
        return CellCoord{cell->x(), cell->y(), static_cast<WorldLayer>(cell->layer())};
    };
    for (const save::PathBatch *source : *root->path_batches()) {
        PathBatchState batch;
        batch.path_batch_id = source->path_batch_id();
        batch.command = static_cast<PathCommandKind>(source->command());
        batch.phase = static_cast<PathPlannerPhase>(source->phase());
        batch.resident_ids.assign(source->resident_ids()->begin(), source->resident_ids()->end());
        batch.target = load_cell(source->target());
        batch.to_adjacent = source->to_adjacent();
        batch.assigned_job = static_cast<Job>(source->assigned_job());
        if (source->build_kind().has_value()) {
            batch.build_kind = static_cast<BuildingKind>(source->build_kind().value());
        }
        batch.building_id = source->building_id();
        batch.reserved_cost = {source->reserved_cost()->wood(), source->reserved_cost()->stone(),
                               source->reserved_cost()->food()};
        batch.enqueued_tick = source->enqueued_tick();
        batch.deadline_tick = source->deadline_tick();
        batch.search_topology_revision = source->search_topology_revision();
        batch.restart_count = source->restart_count();
        batch.work_units_done = source->work_units_done();
        batch.roots.reserve(source->roots()->size());
        for (const save::Cell *cell : *source->roots()) batch.roots.push_back(load_cell(cell));
        batch.search_storage_reserved = source->search_storage_reserved();
        batch.frontier.reserve(source->frontier()->size());
        for (const save::Cell *cell : *source->frontier()) batch.frontier.push_back(load_cell(cell));
        batch.frontier_cursor = static_cast<std::size_t>(source->frontier_cursor());
        batch.nodes.reserve(source->nodes()->size());
        for (const save::PathSearchNode *node : *source->nodes()) {
            batch.nodes.push_back({load_cell(node->cell()), load_cell(node->next())});
        }
        batch.drafts.reserve(source->drafts()->size());
        for (const save::PathRouteDraft *source_draft : *source->drafts()) {
            PathRouteDraftState draft;
            draft.resident_id = source_draft->resident_id();
            draft.start = load_cell(source_draft->start());
            draft.start_x = source_draft->start_x();
            draft.start_y = source_draft->start_y();
            draft.storage_reserved = source_draft->storage_reserved();
            draft.manual_order_id = source_draft->manual_order_id();
            draft.resolved = source_draft->resolved();
            draft.failure = static_cast<PathFailure>(source_draft->failure());
            if (source_draft->reconstruction_cursor()) {
                draft.reconstruction_cursor = load_cell(source_draft->reconstruction_cursor());
            }
            draft.staged.reserve(source_draft->staged()->size());
            for (const save::Cell *cell : *source_draft->staged()) draft.staged.push_back(load_cell(cell));
            batch.drafts.push_back(std::move(draft));
        }
        batch.reconstruction_resident = static_cast<std::size_t>(source->reconstruction_resident());
        batch.stage_cursor = static_cast<std::size_t>(source->stage_cursor());
        batch.legacy_atomic = source->legacy_atomic();
        batch.legacy_grid = source->legacy_grid();
        state.path_batches.push_back(std::move(batch));
    }
    state.path_results.reserve(root->path_results()->size());
    for (const save::PathBatchOutcome *source : *root->path_results()) {
        state.path_results.push_back({source->path_batch_id(), static_cast<PathCommandKind>(source->command()),
                                      source->accepted(), static_cast<PathFailure>(source->failure()),
                                      source->message()->str()});
    }
    return !physical_inventory || parse_inventory(*root, state, error);
}

bool encode_current(const WorldState &state, std::vector<std::uint8_t> &file, std::string &error) {
    if (!validate_save_climate(state, error)) return false;
    const std::vector<std::uint8_t> flatbuffer = build_flatbuffer(state);
    if (flatbuffer.empty() || flatbuffer.size() > max_decompressed_size) {
        error = "Save state exceeds the decompressed size limit.";
        return false;
    }
    std::vector<std::uint8_t> compressed(ZSTD_compressBound(flatbuffer.size()));
    const std::size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(),
                                                      flatbuffer.data(), flatbuffer.size(), 3);
    if (ZSTD_isError(compressed_size)) {
        error = std::string("zstd compression failed: ") + ZSTD_getErrorName(compressed_size);
        return false;
    }
    if (compressed_size == 0 || compressed_size > max_compressed_size) {
        error = "Save state exceeds the compressed size limit.";
        return false;
    }
    compressed.resize(compressed_size);

    file.clear();
    file.reserve(header_size + compressed.size());
    file.insert(file.end(), container_magic.begin(), container_magic.end());
    append_le(file, header_version);
    append_le(file, flag_zstd);
    append_le(file, format_version);
    append_le(file, rules_version);
    append_le(file, state.terrain_generator);
    append_le(file, static_cast<std::uint64_t>(flatbuffer.size()));
    append_le(file, static_cast<std::uint64_t>(compressed.size()));
    append_le(file, std::uint64_t{0});
    file.insert(file.end(), state.world_id.begin(), state.world_id.end());
    const std::string content_hex = default_content_hash();
    for (std::size_t i = 0; i < 32; ++i) {
        const auto nibble = [](char c) -> std::uint8_t {
            return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        };
        file.push_back(static_cast<std::uint8_t>((nibble(content_hex[i * 2]) << 4U) |
                                                 nibble(content_hex[i * 2 + 1])));
    }
    const std::uint64_t checksum =
        container_checksum(std::span<const std::uint8_t>(file).first(header_size), compressed);
    for (std::size_t i = 0; i < sizeof(checksum); ++i)
        file[40 + i] = static_cast<std::uint8_t>(checksum >> (i * 8U));
    file.insert(file.end(), compressed.begin(), compressed.end());
    return true;
}

bool decode_container(std::span<const std::uint8_t> file, WorldState& state, SaveKind& kind,
                      std::string& error) {
    SaveHeader header;
    if (!preflight_header(file, file.size(), header, error)) return false;
    kind = SaveKind::Current;
    const auto payload = file.subspan(header_size);
    if (container_checksum(file.first(header_size), payload) != header.checksum) {
        error = "Save checksum mismatch.";
        return false;
    }
    std::vector<std::uint8_t> flatbuffer(static_cast<std::size_t>(header.decompressed_size));
    const auto result = ZSTD_decompress(flatbuffer.data(), flatbuffer.size(), payload.data(), payload.size());
    if (ZSTD_isError(result) || result != flatbuffer.size()) {
        error = "zstd payload is invalid.";
        return false;
    }
    if (!parse_flatbuffer_current(flatbuffer, state, true, true, true, true, true, error))
        return false;
    if (state.world_id != header.world_id) {
        error = "World id metadata mismatch.";
        return false;
    }
    state.terrain_generator = generator_version;
    return true;
}

bool read_file(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes,
               std::string &error) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Cannot inspect save file.";
        return false;
    }
    if (size == 0 || size > max_decompressed_size + header_size) {
        error = "Save file size limit exceeded.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Cannot open save file.";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
        error = "Cannot read complete save file.";
        return false;
    }
    return true;
}

bool decode_any(std::span<const std::uint8_t> bytes, WorldState& state, SaveKind& kind,
                std::string& error) {
    return decode_container(bytes, state, kind, error);
}

bool inspect_entry(const std::filesystem::path& path, bool& exists, bool regular,
                   std::string& error) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        exists = false;
        return true;
    }
    if (ec) {
        error = "save.path_inspection_failed";
        return false;
    }
    exists = true;
    if (regular && (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))) {
        error = "save.path_invalid";
        return false;
    }
    return true;
}

bool preflight_existing_save(const std::filesystem::path& path, bool& exists,
                             std::string& error) {
    if (!inspect_entry(path, exists, true, error)) return false;
    if (!exists) return true;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0u || size > max_compressed_size + header_size) {
        error = "save.size_invalid";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, header_size> bytes{};
    const auto count = static_cast<std::size_t>(std::min<std::uintmax_t>(size, header_size));
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count))) {
        error = "save.header_read_failed";
        return false;
    }
    SaveHeader header;
    return preflight_header(std::span<const std::uint8_t>(bytes).first(count), size, header, error);
}

bool fresh_stage(const std::filesystem::path& path, std::string& error) {
    bool exists{};
    if (!inspect_entry(path, exists, false, error)) return false;
    if (exists) {
        error = "save.staging_collision";
        return false;
    }
    return true;
}

bool write_file_exclusive(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                          std::string& error) {
    int file = -1;
#ifdef _WIN32
    const auto opened = _wsopen_s(&file, path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                                 _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (opened != 0) {
        error = opened == EEXIST ? "save.staging_collision" : "save.staging_open_failed";
        return false;
    }
#else
    file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (file < 0) {
        error = errno == EEXIST ? "save.staging_collision" : "save.staging_open_failed";
        return false;
    }
#endif
    struct Close {
        int value;
        ~Close() {
#ifdef _WIN32
            _close(value);
#else
            ::close(value);
#endif
        }
    } descriptor_guard{file};
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = std::min<std::size_t>(bytes.size() - offset, 1024u * 1024u);
#ifdef _WIN32
        const auto written = _write(file, bytes.data() + offset, static_cast<unsigned int>(count));
#else
        const auto written = ::write(file, bytes.data() + offset, count);
#endif
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            error = "save.staging_write_failed";
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
#ifdef _WIN32
    const auto flushed = _commit(file);
#else
    const auto flushed = ::fsync(file);
#endif
    if (flushed != 0) {
        error = "save.staging_flush_failed";
        return false;
    }
    return true;
}

bool valid_existing_save(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes,
                         std::string& error) {
    if (!read_file(path, bytes, error)) return false;
    WorldState state;
    SaveKind kind = SaveKind::Current;
    if (!decode_any(bytes, state, kind, error)) return false;
    World validator(state.seed, 0);
    return validator.import_state(std::move(state), error);
}

} // namespace

bool World::save(const std::filesystem::path& path, std::string& error,
                 std::string_view server_metadata) const {
    error.clear();
    if (path.empty()) { error = "save.path_invalid"; return false; }
    auto backup = path; backup += ".bak";
    auto temporary = path; temporary += ".tmp";
    auto backup_temporary = backup; backup_temporary += ".tmp";
    bool primary_exists{}, backup_exists{};
    // Incompatible primary AND backup files are preserved before directories,
    // encoding, staging creation or any attempt to replace a destination.
    if (!preflight_existing_save(path, primary_exists, error) ||
        !preflight_existing_save(backup, backup_exists, error) ||
        !fresh_stage(temporary, error) || !fresh_stage(backup_temporary, error)) return false;
    std::vector<std::uint8_t> previous_bytes, previous_backup_bytes;
    if (primary_exists && !valid_existing_save(path, previous_bytes, error)) return false;
    if (backup_exists && !valid_existing_save(backup, previous_backup_bytes, error)) return false;

    auto state = export_state();
    if (!server_metadata.empty()) state.server_metadata = server_metadata;
    if (!validate_save_climate(state, error)) return false;
    World state_validator(state.seed, 0);
    if (!state_validator.import_state(state, error)) return false;
    std::vector<std::uint8_t> encoded;
    if (!encode_current(state, encoded, error)) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Cannot create save directory."; return false; }
    if (!write_file_exclusive(temporary, encoded, error)) return false;

    // A failed stage is retained. Successful publication consumes only the
    // explicitly created staging files by atomic promotion.
    std::vector<std::uint8_t> verification_bytes;
    WorldState verification_state;
    SaveKind kind = SaveKind::Current;
    World validator(seed(), 0);
    if (!read_file(temporary, verification_bytes, error) ||
        !decode_any(verification_bytes, verification_state, kind, error) ||
        kind != SaveKind::Current || !validator.import_state(std::move(verification_state), error) ||
        validator.checksum() != checksum()) {
        if (error.empty()) error = "Temporary save did not round-trip exactly.";
        return false;
    }

    bool still_exists{}, still_backup_exists{};
    if (!preflight_existing_save(path, still_exists, error) ||
        !preflight_existing_save(backup, still_backup_exists, error)) return false;
    if (still_exists != primary_exists || still_backup_exists != backup_exists) {
        error = "save.target_changed";
        return false;
    }
    if (backup_exists) {
        std::vector<std::uint8_t> current_backup_bytes;
        if (!read_file(backup, current_backup_bytes, error)) return false;
        if (current_backup_bytes != previous_backup_bytes) { error = "save.target_changed"; return false; }
    }
    if (primary_exists) {
        std::vector<std::uint8_t> current_bytes;
        if (!read_file(path, current_bytes, error)) return false;
        if (current_bytes != previous_bytes) { error = "save.target_changed"; return false; }
        // copy_options::none is exclusive: a raced or retained backup stage
        // never gets truncated. Validate the copied bytes before promotion.
        std::filesystem::copy_file(path, backup_temporary, std::filesystem::copy_options::none, ec);
        if (ec) { error = "save.backup_stage_failed"; return false; }
        std::vector<std::uint8_t> backup_bytes;
        if (!valid_existing_save(backup_temporary, backup_bytes, error)) return false;
        if (backup_bytes != previous_bytes) { error = "save.target_changed"; return false; }
        if (!preflight_existing_save(backup, still_backup_exists, error) ||
            still_backup_exists != backup_exists) {
            if (error.empty()) error = "save.target_changed";
            return false;
        }
        if (backup_exists) {
            std::vector<std::uint8_t> current_backup_bytes;
            if (!read_file(backup, current_backup_bytes, error)) return false;
            if (current_backup_bytes != previous_backup_bytes) { error = "save.target_changed"; return false; }
        }
    }
    if (!preflight_existing_save(path, still_exists, error) || still_exists != primary_exists) {
        if (error.empty()) error = "save.target_changed";
        return false;
    }
    if (primary_exists) {
        std::vector<std::uint8_t> current_bytes;
        if (!read_file(path, current_bytes, error)) return false;
        if (current_bytes != previous_bytes) { error = "save.target_changed"; return false; }
    }
    const auto primary_saved = primary_exists ? atomic_replace_file(temporary, path, error)
                                              : atomic_create_file(temporary, path, error);
    if (!primary_saved) return false;
    if (!primary_exists) return true;

    // Keep the original backup untouched until the new primary is durable.
    // A rotation failure retains the new primary, old backup and backup stage;
    // it never rolls back publication or deletes any retained file.
    const auto rotation_failed = [&error]() {
        error = "save.primary_saved_backup_rotation_failed: " + error;
        return false;
    };
    if (!preflight_existing_save(backup, still_backup_exists, error)) return rotation_failed();
    if (still_backup_exists != backup_exists) {
        error = "save.target_changed";
        return rotation_failed();
    }
    if (backup_exists) {
        std::vector<std::uint8_t> current_backup_bytes;
        if (!read_file(backup, current_backup_bytes, error)) return rotation_failed();
        if (current_backup_bytes != previous_backup_bytes) {
            error = "save.target_changed";
            return rotation_failed();
        }
    }
    const auto backup_rotated = backup_exists ? atomic_replace_file(backup_temporary, backup, error)
                                             : atomic_create_file(backup_temporary, backup, error);
    return backup_rotated ? true : rotation_failed();
}

bool World::load(const std::filesystem::path& path, std::string& error) {
    error.clear();
    if (path.empty()) { error = "save.path_invalid"; return false; }
    last_load_ = {};
    last_load_.source_path = std::filesystem::absolute(path).lexically_normal();
    last_load_.save_path = last_load_.source_path;
    std::filesystem::path source = path;
    bool primary_exists{};
    if (!inspect_entry(path, primary_exists, true, error)) return false;
    if (!primary_exists) source += ".bak";
    bool source_exists{};
    if (!preflight_existing_save(source, source_exists, error)) return false;
    if (!source_exists) { error = "Cannot open save file or recovery backup."; return false; }
    SaveLoadInfo info = last_load_;
    info.format = format_version; info.rules = rules_version; info.generator = generator_version;
    last_load_ = info;
    std::vector<std::uint8_t> bytes;
    if (!read_file(source, bytes, error)) return false;
    WorldState state;
    SaveKind kind = SaveKind::Current;
    if (!decode_any(bytes, state, kind, error)) return false;
    World candidate(state.seed, 0);
    if (!candidate.import_state(std::move(state), error)) return false;
    // No legacy decode, migration, normalization or filesystem writeback route.
    *this = std::move(candidate);
    last_load_ = std::move(info);
    return true;
}

} // namespace ribterra

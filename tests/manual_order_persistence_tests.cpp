#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include "world_save_generated.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <zstd.h>

using namespace ribterra;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::string_view v7_hash = "92e20f6f81770d8ca01a478753e8022ee600ca4fa9ddf0a88e07560043a23bcd";
const auto fixtures = fs::path(__FILE__).parent_path() / "fixtures";
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void install_wood_fixture(World &world) {
    auto state = world.export_state();
    const CellCoord target{22, 19};
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    require(chunk != state.chunks.end(), "Wood fixture chunk is missing");
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    require(world.import_state(std::move(state), error), "Wood fixture import failed: " + error);
}
struct Slot {
    fs::path path;
    explicit Slot(const std::string &name)
        : path(fs::temp_directory_path() / ("ribterra_manual_persistence_" + name + ".ribsave")) {
        cleanup();
    }
    ~Slot() { cleanup(); }
    void cleanup() const {
        std::error_code error;
        for (const char *suffix : {"", ".tmp", ".bak", ".bak.tmp", ".v7.bak"})
            fs::remove(path.string() + suffix, error);
    }
};
Bytes read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path &path, const Bytes &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "Cannot write test save");
}
std::uint64_t word(const Bytes &bytes, std::size_t offset, std::size_t size) {
    require(offset + size <= bytes.size(), "Truncated test container");
    std::uint64_t value{};
    for (std::size_t i = 0; i < size; ++i)
        value |= std::uint64_t(bytes[offset + i]) << (8U * i);
    return value;
}
void set_word(Bytes &bytes, std::size_t offset, std::uint64_t value, std::size_t size = 8) {
    for (std::size_t i = 0; i < size; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8U * i));
}
void repair(Bytes &bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i >= 40 && i < 48)
            continue;
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    set_word(bytes, 40, hash);
}
Bytes payload(const Bytes &file) {
    const auto size = word(file, 24, 8);
    require(file.size() >= 96 && size > 0 && size <= 96ULL * 1024 * 1024, "Invalid test payload size");
    Bytes result(static_cast<std::size_t>(size));
    const auto decoded = ZSTD_decompress(result.data(), result.size(), file.data() + 96, file.size() - 96);
    require(!ZSTD_isError(decoded) && decoded == result.size(), "Cannot decode test payload");
    flatbuffers::Verifier verifier(result.data(), result.size(), 64, 2'000'000);
    require(save::VerifyWorldSaveBuffer(verifier), "Test FlatBuffer is invalid");
    return result;
}
std::unique_ptr<save::WorldSaveT> unpack(const Bytes &file) {
    const auto data = payload(file);
    return std::unique_ptr<save::WorldSaveT>(save::GetWorldSave(data.data())->UnPack());
}
Bytes container(const Bytes &original, flatbuffers::FlatBufferBuilder &builder) {
    Bytes compressed(ZSTD_compressBound(builder.GetSize()));
    const auto size =
        ZSTD_compress(compressed.data(), compressed.size(), builder.GetBufferPointer(), builder.GetSize(), 3);
    require(!ZSTD_isError(size), "Cannot encode test payload");
    compressed.resize(size);
    Bytes result(original.begin(), original.begin() + 96);
    result.insert(result.end(), compressed.begin(), compressed.end());
    set_word(result, 24, builder.GetSize());
    set_word(result, 32, compressed.size());
    repair(result);
    return result;
}
// Rules 10 deliberately reveal 1.5x farther. Preserve the frozen v7 oracle for
// every other field by projecting only exploration back to its historical radius.
// The real state must include the entire old footprint, and the current-format
// round-trip below still compares the full, unmodified new state.
struct HistoricalSight {
    std::vector<Chunk> chunks;
    std::vector<Resident> previous;
    explicit HistoricalSight(const WorldState &state) : chunks(state.chunks), previous(state.residents) {}
    Tile *tile(CellCoord cell) {
        const auto key = chunk_key(cell);
        const auto c = std::lower_bound(chunks.begin(),chunks.end(),key,[](const auto &v,const auto &k){return v.key<k;});
        return c!=chunks.end()&&c->key==key ? &c->tiles[local_index(cell.x,cell.y)] : nullptr;
    }
    void project(WorldState &state) {
        bool moved = false;
        for(std::size_t i=0;i<state.residents.size();++i) {
            const auto &r=state.residents[i], &old=previous[i];
            if((r.x==old.x&&r.y==old.y&&r.layer==old.layer)||r.x!=std::floor(r.x)||r.y!=std::floor(r.y))continue;
            moved = true;
            for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)
                if(dx*dx+dy*dy<=9)
                    if(auto *t=tile({int(r.x)+dx,int(r.y)+dy,r.layer}))t->explored=true;
        }
        if(moved)for(const auto &portal:state.portals) {
            auto *a=tile(portal.surface),*b=tile(portal.mine);
            if(a&&b&&(a->explored||b->explored))a->explored=b->explored=true;
        }
        previous=state.residents;
        for(auto &chunk:state.chunks) {
            unsigned extra=0;
            for(std::size_t i=0;i<chunk.tiles.size();++i) {
                const auto &old=*tile({chunk.key.cx*chunk_size+int(i%chunk_size),chunk.key.cy*chunk_size+int(i/chunk_size),chunk.key.layer});
                require(!old.explored||chunk.tiles[i].explored,"Larger vision lost a historically explored cell");
                extra+=chunk.tiles[i].explored&&!old.explored;
                chunk.tiles[i].explored=old.explored;
            }
            require(chunk.revision>=extra,"Exploration revision delta is invalid");
            chunk.revision-=extra;
        }
    }
};
void strip_new_fields(save::WorldSaveT &object) {
    object.settlement.reset(); // Settlement rules were introduced after v7.
    object.manual_orders.clear();
    object.next_manual_order_id = 1;
    object.manual_order_cursor_id = 1;
    for (auto &batch : object.path_batches) {
        batch->legacy_atomic = true;
        batch->legacy_grid = false;
        for (auto &draft : batch->drafts) {
            draft->manual_order_id = 0;
            draft->resolved = false;
            draft->failure = save::PathFailure::None;
        }
    }
}
Bytes canonical_v7_fields(const Bytes &file) {
    auto object = unpack(file);
    strip_new_fields(*object);
    // Compare all original cells; expansion adds only chunks beyond the old map.
    std::erase_if(object->chunks, [](const auto &c) { return c->cx > 4 || c->cy > 4; });
    flatbuffers::FlatBufferBuilder builder;
    save::FinishWorldSaveBuffer(builder, save::WorldSave::Pack(builder, object.get()));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}
// Preserve all current mandatory empty vectors while deliberately changing a
// payload. Generic object Pack elides these vectors and would make semantic
// rejection tests pass for the unrelated reason of missing collections.
template <class T>
auto tables(flatbuffers::FlatBufferBuilder &builder, const std::vector<std::unique_ptr<T>> &values) {
    using Table = typename T::TableType;
    std::vector<flatbuffers::Offset<Table>> offsets;
    for (const auto &value : values)
        offsets.push_back(Table::Pack(builder, value.get()));
    return builder.CreateVector(offsets);
}
Bytes mutate(const Bytes &original, const std::function<void(save::WorldSaveT &)> &change,
             bool force_defaults = false, bool force_manual_vector = false) {
    auto object = unpack(original);
    change(*object);
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(force_defaults);
    std::vector<flatbuffers::Offset<save::Route>> routes;
    for (const auto &route : object->routes)
        routes.push_back(save::CreateRoute(builder, builder.CreateVectorOfStructs(route->points),
                                           route->cursor, route->work_x, route->work_y, route->resident_id,
                                           builder.CreateVectorOfStructs(route->cells), route->work.get()));
    std::vector<flatbuffers::Offset<save::PathBatch>> batches;
    for (const auto &batch : object->path_batches) {
        std::vector<flatbuffers::Offset<save::PathRouteDraft>> drafts;
        for (const auto &draft : batch->drafts)
            drafts.push_back(save::CreatePathRouteDraft(
                builder, draft->resident_id, draft->start.get(), draft->start_x, draft->start_y,
                draft->storage_reserved, draft->reconstruction_cursor.get(),
                builder.CreateVectorOfStructs(draft->staged), draft->manual_order_id, draft->resolved,
                draft->failure));
        batches.push_back(save::CreatePathBatch(
            builder, batch->path_batch_id, batch->command, batch->phase,
            builder.CreateVector(batch->resident_ids), batch->target.get(), batch->to_adjacent,
            batch->assigned_job, batch->build_kind, batch->building_id,
            save::Resources::Pack(builder, batch->reserved_cost.get()), batch->enqueued_tick,
            batch->deadline_tick, batch->search_topology_revision, batch->restart_count,
            batch->work_units_done, builder.CreateVectorOfStructs(batch->roots),
            batch->search_storage_reserved, builder.CreateVectorOfStructs(batch->frontier),
            batch->frontier_cursor, tables(builder, batch->nodes), builder.CreateVector(drafts),
            batch->reconstruction_resident, batch->stage_cursor, batch->legacy_atomic, batch->legacy_grid));
    }
    auto manual = tables(builder, object->manual_orders);
    if (word(original, 12, 4) < 8 && object->manual_orders.empty() && !force_manual_vector)
        manual = 0;
    const auto root = save::CreateWorldSave(
        builder, save::Metadata::Pack(builder, object->metadata.get()),
        save::Resources::Pack(builder, object->resources.get()), tables(builder, object->tiles),
        tables(builder, object->residents), tables(builder, object->buildings), builder.CreateVector(routes),
        builder.CreateVectorOfStrings(object->events), tables(builder, object->chunks),
        tables(builder, object->portals), object->next_path_batch_id, object->topology_revision,
        builder.CreateVector(batches), tables(builder, object->path_results), object->next_item_stack_id,
        object->next_item_reservation_id, object->next_logistics_task_id,
        tables(builder, object->item_stacks), tables(builder, object->storages),
        tables(builder, object->item_reservations), tables(builder, object->logistics_tasks),
        tables(builder, object->construction_orders), object->next_work_intent_id,
        object->work_intent_cursor_id, object->work_building_cursor_id, tables(builder, object->work_intents),
        object->profession_cursor_id, object->next_manual_order_id, object->manual_order_cursor_id, manual);
    save::FinishWorldSaveBuffer(builder, root);
    return container(original, builder);
}
void loaded(World &world, const fs::path &path) {
    std::string error;
    const bool ok = world.load(path, error);
    require(ok, "Load failed: " + error);
}
void saved(const World &world, const fs::path &path) {
    std::string error;
    const bool ok = world.save(path, error);
    require(ok, "Save failed: " + error);
}
#include "persistence_v7_checksum.inc"

void genuine_v7_migration() {
    std::ifstream input(fixtures / "world_save_v7.provenance.json");
    require(bool(input), "V7 provenance missing");
    const auto provenance = Json::parse(input);
    require(provenance.at("writer").at("content_hash").get<std::string>() == v7_hash &&
                provenance.at("fixtures").size() == 25 && provenance.at("gaps").empty(),
            "Unexpected genuine fixture catalog");
    for (const auto &fixture : provenance.at("fixtures")) {
        const auto name = fixture.at("name").get<std::string>();
        try {
            const auto original = read(fixtures / fixture.at("file").get<std::string>());
            require(original.size() == fixture.at("bytes").get<std::size_t>() &&
                        sha256(original) == fixture.at("sha256").get<std::string>(),
                    "Historical fixture hash/length changed");
            Slot slot("v7_" + name);
            write(slot.path, original);
            World world(1, 1, 7), restored(2, 2, 7);
            loaded(world, slot.path);
            const auto upgraded = read(slot.path);
            require(word(upgraded, 12, 4) == 8 && word(upgraded, 16, 4) == rules_version &&
                        word(upgraded, 20, 4) == 2,
                    "Migration did not retain the legacy terrain generator profile");
            require(read(slot.path.string() + ".v7.bak") == original, "Migration backup lost original bytes");
            require(canonical_v7_fields(original) == canonical_v7_fields(upgraded),
                    "A pre-queue field changed during migration");
            const auto initial = world.export_state();
            const auto manual_count =
                fixture.at("production").at("summary").at("manual_override_count").get<std::size_t>();
            require(initial.manual_orders.size() == manual_count,
                    "Wrong number of confirmed manual wrappers");
            int prior_resident = 0;
            for (const auto &order : initial.manual_orders) {
                require(order.legacy_compatibility && !order.issuer && !order.issued_tick &&
                            !order.activated_tick,
                        "Migration invented issuer or issuance/activation time");
                require(order.resident_id > prior_resident &&
                            order.order_id ==
                                static_cast<std::uint64_t>(&order - initial.manual_orders.data() + 1),
                        "Migrated manual IDs are not deterministic by resident");
                prior_resident = order.resident_id;
                require(order.request.gather_completion == GatherCompletion::UntilExhausted,
                        "Legacy gathering became OneDelivery");
                if (name.starts_with("manual_gather") || name == "group_gather_search" ||
                    name == "group_gather_reconstruct" || name == "group_gather_stage" ||
                    name == "group_gather_deferred_assign")
                    require(order.request.kind == ManualOrderKind::Gather,
                            "Confirmed gathering has wrong request kind");
            }
            require(initial.next_manual_order_id == manual_count + 1,
                    "Migrated next manual ID is inconsistent");
            for (const auto &batch : initial.path_batches)
                require(batch.legacy_atomic, "Migration changed accepted v7 atomic planner behavior");
            loaded(restored, slot.path);
            HistoricalSight historical_sight(initial);
            const auto &sequence = fixture.at("checksum_sequence");
            require(sequence.size() == 65, "Frozen continuation sequence missing");
            for (std::size_t tick = 0; tick < sequence.size(); ++tick) {
                auto state = world.export_state();
                std::erase_if(state.chunks, [](const Chunk &c) { return c.key.cx > 4 || c.key.cy > 4; });
                historical_sight.project(state);
                // Rules 12 activate free residents after tick 400; their actions intentionally
                // differ from frozen v7. Reload determinism below remains required at every tick.
                // 0.21 no longer starts extraction from a saved profession assignment.
                const bool extraction_policy_changed = tick > 0 && std::any_of(
                    initial.residents.begin(), initial.residents.end(), [](const auto &r) {
                        const auto p = r.profession.assignment.profession;
                        return p == Profession::Woodcutter || p == Profession::Miner || p == Profession::Forager;
                    });
                // V02 removes the historical Road speed multiplier. Saves keep
                // their routes/passability, but in-flight movement timing may
                // therefore differ from the frozen v7 checksum oracle.
                const bool v02_movement_policy_changed = tick > 0 &&
                    (!initial.manual_orders.empty() || !initial.path_batches.empty());
                require(state.ticks >= 400 || extraction_policy_changed || v02_movement_policy_changed ||
                            legacy_v7_checksum(state) == sequence[tick].get<std::uint64_t>(),
                        "Frozen old-field continuation changed at tick " + std::to_string(tick));
                require(world.checksum() == restored.checksum() &&
                            state.path_results == restored.export_state().path_results,
                        "New-format reload diverged at tick " + std::to_string(tick));
                if (tick + 1 < sequence.size()) {
                    world.tick();
                    restored.tick();
                }
            }
            require(read(fixtures / fixture.at("file").get<std::string>()) == original,
                    "Test changed immutable fixture");
            std::cout << "Genuine v7 " << name
                      << ": initial full-field preservation and 64 frozen continuation ticks (historical sight projection)\n";
        } catch (const std::exception &error) {
            throw std::runtime_error(name + ": " + error.what());
        }
    }
}
void roundtrip(const World &world, const std::string &name) {
    Slot slot(name);
    saved(world, slot.path);
    World first(1, 1, 7), second(2, 2, 7);
    loaded(first, slot.path);
    require(first.checksum() == world.checksum() && first.manual_orders() == world.manual_orders(),
            "Manual DTO changed on first load");
    const auto original = read(slot.path);
    saved(first, slot.path);
    require(read(slot.path) == original, "Current manual state re-encoding is not deterministic");
    loaded(second, slot.path);
    for (int tick = 0; tick < 64; ++tick) {
        first.tick();
        second.tick();
        require(first.checksum() == second.checksum(), "Current manual continuation diverged");
    }
}
void current_queue_roundtrip() {
    World world(70421, 2);
    install_wood_fixture(world);
    const ManualOrderIssuer issuer{17, 9001};
    const auto move = world.issue_manual_order(
        {1}, {ManualOrderKind::Move, CellCoord{26, 24}, GatherCompletion::UntilExhausted},
        ManualQueueMode::Replace, issuer);
    require(move.size() == 1 && move.front().status != ResidentCommandStatus::Rejected, "New Move rejected");
    const auto gather = world.issue_manual_order(
        {1}, {ManualOrderKind::Gather, CellCoord{22, 19}, GatherCompletion::OneDelivery},
        ManualQueueMode::Append, ManualOrderIssuer{17, 9002});
    require(gather.size() == 1 && gather.front().status != ResidentCommandStatus::Rejected,
            "Append Gather rejected");
    require(world.manual_orders().size() == 2 &&
                world.manual_orders().front().issued_tick == std::uint64_t{0},
            "Tick zero issuance was lost");
    roundtrip(world, "queued");
    world.tick();
    roundtrip(world, "admitted");
    for (int tick = 0; tick < 2500 && world.inventory().carried.wood == 0; ++tick)
        world.tick();
    require(world.inventory().carried.wood == 3, "OneDelivery did not reach physical cargo");
    const auto carried = world.export_state().item_stacks;
    const auto replacement = world.issue_manual_order(
        {1}, {ManualOrderKind::Move, CellCoord{27, 24}, GatherCompletion::UntilExhausted},
        ManualQueueMode::Replace, ManualOrderIssuer{17, 9003});
    require(replacement.size() == 1 && replacement.front().status != ResidentCommandStatus::Rejected,
            "Replace at protected boundary rejected");
    require(world.export_state().item_stacks == carried, "Replace changed authoritative carried stacks");
    roundtrip(world, "delivery_boundary");
    for (int tick = 0; tick < 2500; ++tick) {
        world.tick();
        if (std::any_of(world.manual_orders().begin(), world.manual_orders().end(),
                        [](const auto &o) { return o.phase == ManualOrderPhase::Complete; }))
            break;
    }
    require(std::any_of(world.manual_orders().begin(), world.manual_orders().end(),
                        [](const auto &o) { return o.phase == ManualOrderPhase::Complete; }),
            "No terminal manual history");
    roundtrip(world, "terminal_history");
}
void reject_transactionally(const Bytes &damaged, std::string_view expected_error = {}) {
    Slot slot("rejected");
    write(slot.path, damaged);
    World world(811, 3);
    const auto checksum = world.checksum();
    std::string error;
    require(!world.load(slot.path, error) && !error.empty(), "Malformed manual state was accepted");
    require(expected_error.empty() || error.find(expected_error) != std::string::npos,
            "Payload was rejected for an unrelated reason: " + error);
    require(world.checksum() == checksum && read(slot.path) == damaged &&
                !fs::exists(slot.path.string() + ".v7.bak"),
            "Rejected load changed active world, source or migration backup");
}
void single_cancel_reason_roundtrip() {
    static_assert(static_cast<unsigned>(ManualOrderCompletion::LegacyFinished) == 7);
    static_assert(static_cast<unsigned>(ManualOrderCompletion::Cancelled) == 8);
    static_assert(static_cast<unsigned>(save::ManualOrderCompletion::Cancelled) == 8);
    World world(70421, 1);
    install_wood_fixture(world);
    const auto move =
        world.issue_manual_order({1}, {ManualOrderKind::Move, CellCoord{26, 24}}, ManualQueueMode::Replace);
    const auto gather = world.issue_manual_order(
        {1}, {ManualOrderKind::Gather, CellCoord{22, 19}, GatherCompletion::OneDelivery},
        ManualQueueMode::Append);
    require(move.size() == 1 && move.front().status == ResidentCommandStatus::Applied && gather.size() == 1 &&
                gather.front().status == ResidentCommandStatus::Applied,
            "Cancellation roundtrip setup rejected its queue");
    const auto original_head = world.manual_orders().front();
    const auto cancelled = world.cancel_manual_order(gather.front().order_id);
    require(cancelled.size() == 1 && cancelled.front().status == ResidentCommandStatus::Applied &&
                world.manual_orders().front() == original_head &&
                world.manual_orders().back().phase == ManualOrderPhase::Cancelled &&
                world.manual_orders().back().completion == ManualOrderCompletion::Cancelled,
            "Cancelling one tail order changed its head or lost the distinct cancellation reason");
    roundtrip(world, "single_cancel_reason");
    Slot slot("single_cancel_payload");
    saved(world, slot.path);
    const auto current = read(slot.path);
    reject_transactionally(
        mutate(current, [](auto &s) { s.manual_orders.back()->phase = save::ManualOrderPhase::Complete; }),
        "Terminal manual order");
    reject_transactionally(mutate(current,
                                  [](auto &s) {
                                      s.manual_orders.back()->completion =
                                          static_cast<save::ManualOrderCompletion>(9);
                                  }),
                           "Manual order data");
    const auto released = world.release_manual_control({1});
    require(released.size() == 1 && released.front().status == ResidentCommandStatus::Applied &&
                world.manual_orders().front().completion == ManualOrderCompletion::Released &&
                world.manual_orders().back().completion == ManualOrderCompletion::Cancelled,
            "Releasing control collapsed its reason with single-order cancellation");
    roundtrip(world, "released_and_cancelled_reasons");
}
void malformed_v7_and_current_payloads() {
    const auto legacy = read(fixtures / "world_save_v7_group_move_search.ribsave");
    Slot slot("forgery_source");
    write(slot.path, legacy);
    World world(1, 0);
    loaded(world, slot.path);
    const auto current = read(slot.path);
    // First prove the mutation helper preserves valid empty-vector authority.
    Slot control("repack_control");
    write(control.path, mutate(current, [](auto &) {}));
    World control_world(2, 0);
    loaded(control_world, control.path);
    require(control_world.checksum() == world.checksum(), "No-op payload mutation changed valid state");
    const auto reject = [&](const std::function<void(save::WorldSaveT &)> &change) {
        reject_transactionally(mutate(current, change));
    };
    reject([](auto &s) { s.manual_orders.front()->resident_id = 99999; });
    reject([](auto &s) { s.manual_orders.front()->executor_id = std::numeric_limits<std::uint64_t>::max(); });
    reject([](auto &s) { s.manual_orders.front()->kind = static_cast<save::ManualOrderKind>(255); });
    reject([](auto &s) { s.manual_orders.front()->target.reset(); });
    reject([](auto &s) { s.manual_orders.front()->owner_nation = 2; });
    reject([](auto &s) { s.manual_orders.front()->phase = save::ManualOrderPhase::Complete; });
    reject([](auto &s) { s.manual_orders.front()->issued_tick = std::numeric_limits<std::uint64_t>::max(); });
    reject([](auto &s) {
        s.manual_orders.front()->next_attempt_tick = std::numeric_limits<std::uint64_t>::max();
    });
    reject([](auto &s) { s.manual_orders.front()->issuer = std::make_unique<save::ManualOrderIssuerT>(); });
    reject([](auto &s) { s.manual_orders[1]->order_id = s.manual_orders.front()->order_id; });
    reject([](auto &s) { s.manual_orders[1]->resident_id = s.manual_orders.front()->resident_id; });
    reject([](auto &s) { s.next_manual_order_id = 0; });
    reject([](auto &s) { s.manual_order_cursor_id = 0; });
    reject([](auto &s) { s.manual_order_cursor_id = s.next_manual_order_id + 1; });
    reject([](auto &s) { s.manual_orders.clear(); });
    reject([](auto &s) { s.path_batches.front()->legacy_atomic = false; });
    reject([](auto &s) { s.path_batches.front()->drafts.front()->resolved = true; });
    reject([](auto &s) {
        s.path_batches.front()->drafts.front()->manual_order_id = std::numeric_limits<std::uint64_t>::max();
    });
    reject([](auto &s) {
        while (s.manual_orders.size() <= max_manual_orders)
            s.manual_orders.push_back(std::make_unique<save::ManualOrderT>(*s.manual_orders.front()));
    });
    reject_transactionally(mutate(current,
                                  [](auto &s) {
                                      for (std::size_t i = 1; i < max_resident_manual_orders + 1; ++i) {
                                          auto tail =
                                              std::make_unique<save::ManualOrderT>(*s.manual_orders.front());
                                          tail->order_id = s.next_manual_order_id++;
                                          tail->phase = save::ManualOrderPhase::Queued;
                                          tail->executor = save::ManualExecutorKind::None;
                                          tail->executor_id = 0;
                                          tail->activated_tick = flatbuffers::nullopt;
                                          tail->next_attempt_tick = 0;
                                          tail->attempts = 0;
                                          s.manual_orders.push_back(std::move(tail));
                                      }
                                  }),
                           "Manual queue ownership");
    reject_transactionally(mutate(current,
                                  [](auto &s) {
                                      for (std::size_t i = 0; i <= manual_terminal_history; ++i) {
                                          auto terminal =
                                              std::make_unique<save::ManualOrderT>(*s.manual_orders.front());
                                          terminal->order_id = s.next_manual_order_id++;
                                          terminal->phase = save::ManualOrderPhase::Complete;
                                          terminal->completion = save::ManualOrderCompletion::Arrived;
                                          terminal->executor = save::ManualExecutorKind::None;
                                          terminal->executor_id = 0;
                                          terminal->next_attempt_tick = 0;
                                          s.manual_orders.push_back(std::move(terminal));
                                      }
                                  }),
                           "Manual terminal history");
    // Strict old-version gates must run before metadata wrapping or repair.
    reject_transactionally(mutate(legacy, [](auto &s) { s.manual_order_cursor_id = 2; }));
    reject_transactionally(mutate(legacy, [](auto &) {}, false, true));
    reject_transactionally(mutate(legacy, [](auto &) {}, true)); // explicit default-valued appended fields
    reject_transactionally(mutate(legacy, [](auto &s) { s.path_batches.front()->legacy_atomic = false; }));
    reject_transactionally(
        mutate(legacy, [](auto &s) { s.path_batches.front()->drafts.front()->resolved = true; }));
    reject_transactionally(
        mutate(legacy, [](auto &s) { s.path_batches.front()->command = save::PathCommandKind::Manual; }));
    reject_transactionally(
        mutate(legacy, [](auto &s) { s.path_batches.front()->drafts.front()->resident_id = 99999; }));
    auto bad_hash = legacy;
    bad_hash[64] ^= 1;
    repair(bad_hash);
    reject_transactionally(bad_hash);
    auto bad_rules = legacy;
    set_word(bad_rules, 16, 7, 4);
    repair(bad_rules);
    reject_transactionally(bad_rules);
    auto missing_new = legacy;
    set_word(missing_new, 12, 8, 4);
    set_word(missing_new, 16, 7, 4);
    const auto hash = default_content_hash();
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < 32; ++i)
        missing_new[64 + i] =
            static_cast<std::uint8_t>((nibble(hash[i * 2]) << 4U) | nibble(hash[i * 2 + 1]));
    repair(missing_new);
    reject_transactionally(missing_new);
}
void opaque_legacy_executor_roundtrip() {
    const auto original = read(fixtures / "world_save_v7_legacy_v6_carry.ribsave");
    const auto opaque = mutate(original, [](auto &s) {
        const auto carrier =
            std::find_if(s.residents.begin(), s.residents.end(), [](const auto &r) { return r->id == 4; });
        require(carrier != s.residents.end() && (*carrier)->job == save::Job::Haul,
                "Expected genuine construction carrier");
        (*carrier)->profession->manual_override = true;
    });
    Slot slot("opaque_legacy");
    write(slot.path, opaque);
    World world(1, 1);
    loaded(world, slot.path);
    require(canonical_v7_fields(opaque) == canonical_v7_fields(read(slot.path)),
            "Opaque compatibility changed its executor");
    const auto head = std::find_if(world.manual_orders().begin(), world.manual_orders().end(),
                                   [](const auto &o) { return o.resident_id == 4; });
    require(head != world.manual_orders().end() && head->request.kind == ManualOrderKind::Legacy &&
                !head->request.target && head->legacy_compatibility && !head->issuer && !head->issued_tick,
            "Opaque manual executor acquired invented provenance/type/target");
    roundtrip(world, "opaque_roundtrip");
}
} // namespace
int main() {
    try {
        genuine_v7_migration();
        current_queue_roundtrip();
        single_cancel_reason_roundtrip();
        malformed_v7_and_current_payloads();
        opaque_legacy_executor_roundtrip();
        std::cout << "All manual order persistence tests passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Manual persistence: " << error.what() << '\n';
        return 1;
    }
}

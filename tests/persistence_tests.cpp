#include "ribterra/foundation.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include "world_save_generated.h"
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n";             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct TempSave {
    explicit TempSave(std::string name)
        : path(std::filesystem::temp_directory_path() / std::move(name)) {
        cleanup();
    }
    ~TempSave() { cleanup(); }
    void cleanup() const {
        std::error_code ec;
        for (const char *suffix : {"", ".tmp", ".bak", ".bak.tmp", ".legacy.bak", ".v2.bak", ".v3.bak",
                                   ".v4.bak", ".v5.bak", ".v6.bak", ".v7.bak"})
            std::filesystem::remove(path.string() + suffix, ec);
    }
    std::filesystem::path path;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void set_header_u64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

void repair_container_checksum(std::vector<std::uint8_t> &bytes) {
    std::uint64_t checksum = 1469598103934665603ULL;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i >= 40 && i < 48) continue;
        checksum ^= bytes[i];
        checksum *= 1099511628211ULL;
    }
    set_header_u64(bytes, 40, checksum);
}

// The generated object packer drops optional empty vectors. Current saves require
// their presence, so fixtures preserve them before testing semantic validation.
::flatbuffers::Offset<ribterra::save::WorldSave> pack_inventory_fixture(::flatbuffers::FlatBufferBuilder &_fbb, const ribterra::save::WorldSaveT* _o) {
  using namespace ribterra::save;
  const ::flatbuffers::rehasher_function_t* _rehasher=nullptr;
  (void)_o;
  struct _VectorArgs { ::flatbuffers::FlatBufferBuilder *__fbb; const WorldSaveT* __o; const ::flatbuffers::rehasher_function_t *__rehasher; } _va = { &_fbb, _o, _rehasher}; (void)_va;
  auto _metadata = _o->metadata ? CreateMetadata(_fbb, _o->metadata.get(), _rehasher) : 0;
  auto _resources = _o->resources ? CreateResources(_fbb, _o->resources.get(), _rehasher) : 0;
  auto _tiles = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Tile>> (_o->tiles.size(), [](size_t i, _VectorArgs *__va) { return CreateTile(*__va->__fbb, __va->__o->tiles[i].get(), __va->__rehasher); }, &_va );
  auto _residents = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Resident>> (_o->residents.size(), [](size_t i, _VectorArgs *__va) { return CreateResident(*__va->__fbb, __va->__o->residents[i].get(), __va->__rehasher); }, &_va );
  auto _buildings = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Building>> (_o->buildings.size(), [](size_t i, _VectorArgs *__va) { return CreateBuilding(*__va->__fbb, __va->__o->buildings[i].get(), __va->__rehasher); }, &_va );
  auto _routes = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Route>> (_o->routes.size(), [](size_t i, _VectorArgs *__va) { const auto& r=*__va->__o->routes[i]; return CreateRoute(*__va->__fbb, __va->__fbb->CreateVectorOfStructs(r.points),r.cursor,r.work_x,r.work_y,r.resident_id,__va->__fbb->CreateVectorOfStructs(r.cells),r.work.get()); }, &_va );
  auto _events = _fbb.CreateVectorOfStrings(_o->events);
  auto _chunks = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Chunk>> (_o->chunks.size(), [](size_t i, _VectorArgs *__va) { return CreateChunk(*__va->__fbb, __va->__o->chunks[i].get(), __va->__rehasher); }, &_va );
  auto _portals = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Portal>> (_o->portals.size(), [](size_t i, _VectorArgs *__va) { return CreatePortal(*__va->__fbb, __va->__o->portals[i].get(), __va->__rehasher); }, &_va );
  auto _next_path_batch_id = _o->next_path_batch_id;
  auto _topology_revision = _o->topology_revision;
  auto _path_batches = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::PathBatch>> (_o->path_batches.size(), [](size_t i, _VectorArgs *__va) { return CreatePathBatch(*__va->__fbb, __va->__o->path_batches[i].get(), __va->__rehasher); }, &_va );
  auto _path_results = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::PathBatchOutcome>> (_o->path_results.size(), [](size_t i, _VectorArgs *__va) { return CreatePathBatchOutcome(*__va->__fbb, __va->__o->path_results[i].get(), __va->__rehasher); }, &_va );
  auto _next_item_stack_id = _o->next_item_stack_id;
  auto _next_item_reservation_id = _o->next_item_reservation_id;
  auto _next_logistics_task_id = _o->next_logistics_task_id;
  auto _item_stacks = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::ItemStack>> (_o->item_stacks.size(), [](size_t i, _VectorArgs *__va) { return CreateItemStack(*__va->__fbb, __va->__o->item_stacks[i].get(), __va->__rehasher); }, &_va );
  auto _storages = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::Storage>> (_o->storages.size(), [](size_t i, _VectorArgs *__va) { return CreateStorage(*__va->__fbb, __va->__o->storages[i].get(), __va->__rehasher); }, &_va );
  auto _item_reservations = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::ItemReservation>> (_o->item_reservations.size(), [](size_t i, _VectorArgs *__va) { return CreateItemReservation(*__va->__fbb, __va->__o->item_reservations[i].get(), __va->__rehasher); }, &_va );
  auto _logistics_tasks = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::LogisticsTask>> (_o->logistics_tasks.size(), [](size_t i, _VectorArgs *__va) { return CreateLogisticsTask(*__va->__fbb, __va->__o->logistics_tasks[i].get(), __va->__rehasher); }, &_va );
  auto _construction_orders = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::ConstructionOrder>> (_o->construction_orders.size(), [](size_t i, _VectorArgs *__va) { return CreateConstructionOrder(*__va->__fbb, __va->__o->construction_orders[i].get(), __va->__rehasher); }, &_va );
  auto _next_work_intent_id = _o->next_work_intent_id;
  auto _work_intent_cursor_id = _o->work_intent_cursor_id;
  auto _work_building_cursor_id = _o->work_building_cursor_id;
  auto _work_intents = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::WorkIntent>> (_o->work_intents.size(), [](size_t i, _VectorArgs *__va) { return CreateWorkIntent(*__va->__fbb, __va->__o->work_intents[i].get(), __va->__rehasher); }, &_va );
  auto _profession_cursor_id = _o->profession_cursor_id;
  auto _next_manual_order_id = _o->next_manual_order_id;
  auto _manual_order_cursor_id = _o->manual_order_cursor_id;
  auto _manual_orders = _fbb.CreateVector<::flatbuffers::Offset<ribterra::save::ManualOrder>> (_o->manual_orders.size(), [](size_t i, _VectorArgs *__va) { return CreateManualOrder(*__va->__fbb, __va->__o->manual_orders[i].get(), __va->__rehasher); }, &_va );
  auto _settlement = _o->settlement ? CreateSettlement(_fbb, _o->settlement.get(), _rehasher) : 0;
  auto _economy = _o->economy.empty() ? 0 : _fbb.CreateString(_o->economy);
  auto _server_metadata = _o->server_metadata.empty() ? 0 : _fbb.CreateString(_o->server_metadata);
  return ribterra::save::CreateWorldSave(
      _fbb,
      _metadata,
      _resources,
      _tiles,
      _residents,
      _buildings,
      _routes,
      _events,
      _chunks,
      _portals,
      _next_path_batch_id,
      _topology_revision,
      _path_batches,
      _path_results,
      _next_item_stack_id,
      _next_item_reservation_id,
      _next_logistics_task_id,
      _item_stacks,
      _storages,
      _item_reservations,
      _logistics_tasks,
      _construction_orders,
      _next_work_intent_id,
      _work_intent_cursor_id,
      _work_building_cursor_id,
      _work_intents,
      _profession_cursor_id,
      _next_manual_order_id,
      _manual_order_cursor_id,
      _manual_orders,
      _settlement,
      _economy,
      _server_metadata);
}

std::vector<std::uint8_t> mutate_inventory_payload(
    const std::vector<std::uint8_t> &original,
    const std::function<void(ribterra::save::WorldSaveT &)> &mutate) {
    std::uint64_t size = 0;
    for (std::size_t i = 0; i < 8; ++i)
        size |= static_cast<std::uint64_t>(original[24 + i]) << (i * 8U);
    std::vector<std::uint8_t> uncompressed(static_cast<std::size_t>(size));
    const auto decoded = ZSTD_decompress(uncompressed.data(), uncompressed.size(),
                                         original.data() + 96, original.size() - 96);
    CHECK(!ZSTD_isError(decoded) && decoded == uncompressed.size());
    if (ZSTD_isError(decoded)) return {};
    std::unique_ptr<ribterra::save::WorldSaveT> object(
        ribterra::save::GetWorldSave(uncompressed.data())->UnPack());
    mutate(*object);
    flatbuffers::FlatBufferBuilder builder;
    ribterra::save::FinishWorldSaveBuffer(builder,
                                         pack_inventory_fixture(builder, object.get()));
    std::vector<std::uint8_t> compressed(ZSTD_compressBound(builder.GetSize()));
    const auto encoded = ZSTD_compress(compressed.data(), compressed.size(),
                                       builder.GetBufferPointer(), builder.GetSize(), 3);
    CHECK(!ZSTD_isError(encoded));
    if (ZSTD_isError(encoded)) return {};
    compressed.resize(encoded);
    std::vector<std::uint8_t> result(original.begin(), original.begin() + 96);
    result.insert(result.end(), compressed.begin(), compressed.end());
    set_header_u64(result, 24, builder.GetSize());
    set_header_u64(result, 32, compressed.size());
    repair_container_checksum(result);
    return result;
}

std::string captured_legacy_fixture() {
    std::ostringstream fixture;
    fixture << "RIBTERRA 1\n17 42 2 1 1 0\n80 40 100\n2304\n";
    for (int i = 0; i < 48 * 48; ++i)
        fixture << "0 0 1\n";
    fixture << "1\n1 \"Legacy\" 24 24 24 24 1 0 100 0 0 0 0\n";
    fixture << "1\n1 0 24 24 1\n1\n0 -1 -1 1 25 24\n1\n\"Legacy fixture\"\nEND\n";
    return fixture.str();
}

void exact_roundtrip_and_resume() {
    TempSave first("ribterra_persistence_roundtrip.ribsave"),
        second("ribterra_persistence_deterministic.ribsave");
    ribterra::World original(222, 5);
    ribterra::World distinct_identity(222, 5);
    CHECK(original.export_state().world_id != distinct_identity.export_state().world_id);
    CHECK(original.checksum() == distinct_identity.checksum());
    CHECK(original.move({1, 2}, 30, 24).accepted);
    for (int i = 0; i < 23; ++i)
        original.tick();
    std::string error;
    CHECK(original.save(first.path, error));
    CHECK(read_bytes(first.path).size() < 8 * 1024 * 1024);
    ribterra::World loaded(1, 1);
    CHECK(loaded.load(first.path, error));
    CHECK(original.checksum() == loaded.checksum());
    CHECK(loaded.save(second.path, error));
    CHECK(read_bytes(first.path) == read_bytes(second.path));
    for (int i = 0; i < 500; ++i) {
        original.tick();
        loaded.tick();
    }
    CHECK(original.checksum() == loaded.checksum());
}

void truncation_mutation_and_transactionality() {
    TempSave good("ribterra_persistence_good.ribsave"),
        damaged("ribterra_persistence_damaged.ribsave");
    ribterra::World source(400, 12);
    for (int i = 0; i < 60; ++i)
        source.tick();
    std::string error;
    CHECK(source.save(good.path, error));
    const auto bytes = read_bytes(good.path);
    CHECK(bytes.size() > 96);
    const std::array<std::size_t, 8> truncations{0, 1, 7, 8, 39, 95, 96, bytes.size() - 1};
    for (const std::size_t size : truncations) {
        write_bytes(damaged.path, std::span<const std::uint8_t>(bytes).first(size));
        ribterra::World target(77, 3);
        const auto before = target.checksum();
        CHECK(!target.load(damaged.path, error));
        CHECK(!error.empty());
        CHECK(target.checksum() == before);
    }
    for (std::size_t offset = 96; offset < bytes.size();
         offset += std::max<std::size_t>(1, (bytes.size() - 96) / 17)) {
        auto mutation = bytes;
        mutation[offset] ^= static_cast<std::uint8_t>(1U << (offset % 8U));
        write_bytes(damaged.path, mutation);
        ribterra::World target(78, 3);
        const auto before = target.checksum();
        CHECK(!target.load(damaged.path, error));
        CHECK(target.checksum() == before);
    }
}

void limits_and_versions() {
    TempSave good("ribterra_persistence_versions.ribsave"),
        damaged("ribterra_persistence_versions_bad.ribsave");
    ribterra::World source(501, 2);
    std::string error;
    CHECK(source.save(good.path, error));
    const auto clean = read_bytes(good.path);
    auto check_rejected = [&](std::vector<std::uint8_t> bytes) {
        write_bytes(damaged.path, bytes);
        ribterra::World target(9, 1);
        const auto before = target.checksum();
        CHECK(!target.load(damaged.path, error));
        CHECK(!error.empty());
        CHECK(target.checksum() == before);
    };
    auto header = clean;
    header[8] = 99;
    check_rejected(std::move(header));
    auto format = clean;
    format[12] = 99;
    check_rejected(std::move(format));
    auto future_rules = clean;
    future_rules[16] = static_cast<std::uint8_t>(ribterra::rules_version + 1);
    check_rejected(std::move(future_rules));
    auto old_rules = clean;
    old_rules[16] = static_cast<std::uint8_t>(ribterra::rules_version - 4);
    check_rejected(std::move(old_rules));
    auto oversize_decompressed = clean;
    for (std::size_t i = 24; i < 32; ++i)
        oversize_decompressed[i] = 0xff;
    check_rejected(std::move(oversize_decompressed));
    auto oversize_compressed = clean;
    for (std::size_t i = 32; i < 40; ++i)
        oversize_compressed[i] = 0xff;
    check_rejected(std::move(oversize_compressed));
}

void backup_rotation_and_gap_recovery() {
    TempSave slot("ribterra_persistence_backup.ribsave");
    std::string error;
    ribterra::World first(601, 2);
    CHECK(first.save(slot.path, error));
    const auto first_checksum = first.checksum();
    first.tick();
    const auto second_checksum = first.checksum();
    CHECK(first.save(slot.path, error));
    CHECK(std::filesystem::exists(slot.path.string() + ".bak"));
    std::error_code ec;
    std::filesystem::remove(slot.path, ec);
    CHECK(!ec);
    ribterra::World recovered(1, 1);
    CHECK(recovered.load(slot.path, error));
    CHECK(recovered.checksum() == first_checksum);
    CHECK(recovered.checksum() != second_checksum);

    // Recovery is only for an interrupted replacement gap. A corrupt primary
    // must be reported and must never silently roll state back to the backup.
    write_bytes(slot.path, std::array<std::uint8_t, 4>{1, 2, 3, 4});
    const auto before = recovered.checksum();
    CHECK(!recovered.load(slot.path, error));
    CHECK(recovered.checksum() == before);
    const auto retained_backup = read_bytes(slot.path.string() + ".bak");
    ribterra::World replacement(602, 2);
    CHECK(replacement.save(slot.path, error));
    CHECK(read_bytes(slot.path.string() + ".bak") == retained_backup);
}

void legacy_import_is_one_way_and_backed_up() {
    TempSave slot("ribterra_persistence_legacy.ribsave");
    const std::string fixture = captured_legacy_fixture();
    write_bytes(slot.path,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t *>(fixture.data()), fixture.size()));
    ribterra::World loaded(1, 1);
    std::string error;
    CHECK(loaded.load(slot.path, error));
    CHECK(loaded.seed() == 17);
    CHECK(loaded.ticks() == 42);
    CHECK(loaded.tutorial_step() == 2);
    CHECK(loaded.residents().size() == 1);
    CHECK(loaded.residents().front().order_phase == ribterra::OrderPhase::Active);
    CHECK(loaded.residents().front().job == ribterra::Job::Move);
    CHECK(loaded.buildings().size() == 1);
    CHECK(loaded.events().size() == 1);
    CHECK(read_bytes(slot.path.string() + ".legacy.bak") ==
          std::vector<std::uint8_t>(fixture.begin(), fixture.end()));
    const auto upgraded = read_bytes(slot.path);
    CHECK(upgraded.size() >= 8);
    CHECK(std::string(upgraded.begin(), upgraded.begin() + 6) == "RIBSV2");
    ribterra::World reloaded(2, 2);
    CHECK(reloaded.load(slot.path, error));
    CHECK(reloaded.checksum() == loaded.checksum());
}

void captured_v2_import_is_one_way_and_resumable() {
    TempSave slot("ribterra_persistence_v2.ribsave");
    const auto fixture_path = std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                              "world_save_v2_current.ribsave";
    const auto fixture = read_bytes(fixture_path);
    CHECK(fixture.size() == 14071);
    CHECK(ribterra::sha256(fixture) ==
          "a84c8db20201f00590cb3e5b312ac5b0a05371b78f8036313a2e7f5c166d360c");
    write_bytes(slot.path, fixture);

    ribterra::World migrated(1, 1);
    std::string error;
    CHECK(migrated.load(slot.path, error));
    CHECK(migrated.seed() == 424242);
    CHECK(migrated.ticks() == 37);
    CHECK(migrated.residents().size() == 6);
    CHECK(migrated.buildings().size() == 1);
    CHECK(migrated.resources().wood == 80);
    CHECK(migrated.resources().stone == 40);
    CHECK(migrated.resources().food == 100);
    CHECK(migrated.tutorial_step() == 0);
    CHECK(migrated.events().size() == 1);
    CHECK(migrated.export_state().next_building_id == 2);
    CHECK((migrated.export_state().world_id ==
           std::array<std::uint8_t, 16>{0x8d, 0xc7, 0x34, 0x78, 0xca, 0xe8, 0x4d, 0xd0, 0x81, 0x1e,
                                        0xfd, 0xfb, 0x31, 0x81, 0x34, 0x82}));
    CHECK(read_bytes(slot.path.string() + ".v2.bak") == fixture);
    const auto upgraded = read_bytes(slot.path);
    CHECK(upgraded.size() >= 16);
    CHECK(upgraded[12] == 8 && upgraded[13] == 0 && upgraded[14] == 0 && upgraded[15] == 0);

    ribterra::World reloaded(2, 2);
    CHECK(reloaded.load(slot.path, error));
    CHECK(reloaded.checksum() == migrated.checksum());
    for (int i = 0; i < 250; ++i) {
        migrated.tick();
        reloaded.tick();
    }
    CHECK(reloaded.checksum() == migrated.checksum());
}

void captured_v3_import_is_one_way_and_resumable() {
    TempSave slot("ribterra_persistence_v3_migration.ribsave");
    const auto fixture_path = std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                              "world_save_v3_current.ribsave";
    const auto fixture = read_bytes(fixture_path);
    CHECK(fixture.size() == 519560);
    CHECK(ribterra::sha256(fixture) ==
          "b2fa49877b647cbb346a5f6773be86ff8bbe585c003a001b35904e7a7e4e5412");
    write_bytes(slot.path, fixture);

    ribterra::World migrated(1, 1);
    std::string error;
    CHECK(migrated.load(slot.path, error));
    const auto migrated_state = migrated.export_state();
    CHECK(migrated.seed() == 606060);
    CHECK(migrated.ticks() == 43);
    CHECK(migrated.residents().size() == 7);
    CHECK(migrated.buildings().size() == 1);
    CHECK(migrated_state.next_building_id == 2);
    CHECK(migrated_state.chunks.size() == ribterra::world_chunk_count);
    CHECK(migrated_state.portals.size() == 1);
    CHECK(migrated_state.next_path_batch_id == 1);
    CHECK(migrated_state.topology_revision == 0);
    CHECK(migrated_state.path_batches.empty());
    CHECK(migrated_state.path_results.empty());
    CHECK(std::all_of(migrated_state.residents.begin(), migrated_state.residents.end(),
                      [](const auto &resident) {
                          return resident.order_phase == ribterra::OrderPhase::Idle &&
                                 resident.path_failure == ribterra::PathFailure::None &&
                                 resident.pending_path_batch == 0;
                      }));
    CHECK((migrated_state.world_id == std::array<std::uint8_t, 16>{
        0xdd, 0x8c, 0x1d, 0x14, 0x91, 0xf7, 0x47, 0x94, 0xa6, 0x3f, 0x78, 0xc3, 0xdb, 0x61, 0x66, 0xa8}));
    CHECK(read_bytes(slot.path.string() + ".v3.bak") == fixture);
    const auto upgraded = read_bytes(slot.path);
    CHECK(upgraded.size() >= 16);
    CHECK(upgraded[12] == 8 && upgraded[13] == 0 && upgraded[14] == 0 && upgraded[15] == 0);

    ribterra::World reloaded(2, 2);
    CHECK(reloaded.load(slot.path, error));
    CHECK(reloaded.checksum() == migrated.checksum());
    for (int i = 0; i < 250; ++i) { migrated.tick(); reloaded.tick(); }
    CHECK(reloaded.checksum() == migrated.checksum());
}

void captured_v4_builds_migrate_without_double_payment() {
    using namespace ribterra;
    TempSave slot("ribterra_persistence_v4_builds.ribsave");
    const auto fixture = read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                    "world_save_v4_builds.ribsave");
    CHECK(fixture.size() == 526972);
    CHECK(sha256(fixture) ==
          "394268f0e8e3efe8a31b22db99821ad1ccfce200939088ecb5697f465e35923a");
    write_bytes(slot.path, fixture);
    World migrated(1, 1);
    std::string error;
    const bool loaded = migrated.load(slot.path, error);
    CHECK(loaded);
    if (!loaded) { std::cerr << error << '\n'; return; }
    const auto state = migrated.export_state();
    CHECK(state.seed == 707070 && state.ticks == 1);
    CHECK(state.residents.size() == 10 && state.buildings.size() == 2);
    CHECK(state.next_building_id == 4 && state.next_path_batch_id == 3);
    CHECK(state.path_batches.size() == 1);
    if (!state.path_batches.empty()) {
        CHECK(state.path_batches.front().path_batch_id == 2);
        CHECK(state.path_batches.front().building_id == 3);
        CHECK(state.path_batches.front().phase == PathPlannerPhase::Queued);
        CHECK((state.path_batches.front().reserved_cost == Resources{20, 10, 5}));
    }
    CHECK((state.world_id == std::array<std::uint8_t, 16>{
        131, 67, 150, 37, 251, 120, 76, 0, 167, 148, 68, 100, 70, 119, 180, 234}));
    CHECK(state.storages.size() == 1 && state.item_stacks.size() == 3);
    CHECK(std::all_of(state.item_stacks.begin(), state.item_stacks.end(), [](const auto &stack) {
        return stack.location.kind == ItemLocationKind::Storage && stack.location.entity_id == 1;
    }));
    CHECK((migrated.inventory().total == Resources{50, 25, 100}));
    CHECK((migrated.inventory().reserved == Resources{20, 10, 5}));
    CHECK((migrated.inventory().available == Resources{30, 15, 95}));
    const auto paid = std::find_if(state.construction_orders.begin(), state.construction_orders.end(),
                                   [](const auto &order) { return order.building_id == 2; });
    const auto pending = std::find_if(state.construction_orders.begin(), state.construction_orders.end(),
                                      [](const auto &order) { return order.building_id == 3; });
    CHECK(paid != state.construction_orders.end());
    CHECK(pending != state.construction_orders.end());
    if (paid != state.construction_orders.end()) {
        CHECK(paid->phase == ConstructionPhase::Work);
        CHECK((paid->required == Resources{30, 15, 0}));
        CHECK(paid->delivered == paid->required);
    }
    if (pending != state.construction_orders.end()) {
        CHECK(pending->phase == ConstructionPhase::Planned);
        CHECK((pending->required == Resources{20, 10, 5}));
        CHECK((pending->delivered == Resources{0, 0, 0}));
        CHECK(pending->originating_path_batch_id == 2);
    }
    CHECK(read_bytes(slot.path.string() + ".v4.bak") == fixture);
    const auto upgraded = read_bytes(slot.path);
    CHECK(upgraded.size() >= 16 && upgraded[12] == 8);
    World resumed(2, 2);
    CHECK(resumed.load(slot.path, error));
    CHECK(resumed.checksum() == migrated.checksum());
    for (int i = 0; i < 900; ++i) {
        migrated.tick();
        resumed.tick();
        CHECK(resumed.checksum() == migrated.checksum());
    }
    CHECK(migrated.item_reservations().empty());
    CHECK(std::all_of(migrated.buildings().begin(), migrated.buildings().end(),
                       [](const auto &building) { return building.progress >= 1.0f; }));
}

void captured_v5_work_states_preserve_inventory_and_pending_search() {
    using namespace ribterra;
    struct Fixture {
        const char *name;
        std::size_t bytes;
        const char *sha;
        std::uint64_t ticks;
    };
    const std::array<Fixture, 3> fixtures{{
        {"world_save_v5_carry", 521945,
         "cca238944c6a1a5b88504004fbe0996de6ba0a8e6abec353a988e0e17e31af22", 57},
        {"world_save_v5_continuation", 521649,
         "7a38f70b18accc6565c4ee101b7736838911c24186862fe7ac1667b4746e76df", 115},
        {"world_save_v5_work", 521391,
         "c2b32ca4d21dcf2495a0fb62e37b83990170980e567a538df0a0650fd986db88", 91}
    }};
    for (std::size_t index = 0; index < fixtures.size(); ++index) {
        const auto &fixture = fixtures[index];
        const auto bytes = read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                     (std::string(fixture.name) + ".ribsave"));
        CHECK(bytes.size() == fixture.bytes && sha256(bytes) == fixture.sha);
        TempSave slot(std::string("ribterra_migration_") + fixture.name + ".ribsave");
        write_bytes(slot.path, bytes);
        World migrated(1, 1);
        std::string error;
        const bool loaded = migrated.load(slot.path, error);
        CHECK(loaded);
        if (!loaded) { std::cerr << fixture.name << ": " << error << '\n'; continue; }
        const auto state = migrated.export_state();
        CHECK(state.seed == 70421 && state.ticks == fixture.ticks && state.residents.size() == 10);
        CHECK(read_bytes(slot.path.string() + ".v5.bak") == bytes);
        const auto upgraded = read_bytes(slot.path);
        CHECK(upgraded.size() >= 16 && upgraded[12] == 8);
        if (index == 0) {
            CHECK((state.resources == Resources{83, 40, 100}));
            CHECK(state.work_intents.empty());
            CHECK(state.logistics_tasks.size() == 1);
            CHECK(std::any_of(state.item_stacks.begin(), state.item_stacks.end(), [](const auto &stack) {
                return stack.stack_id == 4 && stack.quantity == 3 && stack.item == ItemKind::Wood &&
                       stack.location.kind == ItemLocationKind::Carried && stack.location.entity_id == 1;
            }));
            if (!state.logistics_tasks.empty()) {
                CHECK(state.logistics_tasks.front().phase == LogisticsPhase::ToDropoff);
                CHECK(state.logistics_tasks.front().resume_job == Job::Woodcut);
                CHECK(state.logistics_tasks.front().carry_stack_id == 4);
            }
        } else if (index == 1) {
            CHECK((state.resources == Resources{83, 40, 100}));
            CHECK(state.next_path_batch_id == 4);
            CHECK(state.path_batches.size() == 1 && state.work_intents.size() == 1);
            CHECK(state.logistics_tasks.size() == 1 && state.logistics_tasks.front().path_batch_id == 0);
            if (!state.path_batches.empty() && !state.work_intents.empty()) {
                const auto &batch = state.path_batches.front();
                const auto &intent = state.work_intents.front();
                CHECK(batch.path_batch_id == 3 && batch.command == PathCommandKind::Work);
                CHECK(batch.phase == PathPlannerPhase::Queued && batch.work_units_done == 0);
                CHECK(batch.enqueued_tick == 115 && batch.deadline_tick == 1315);
                CHECK((batch.target == CellCoord{22, 19, WorldLayer::Surface}));
                CHECK(intent.phase == WorkIntentPhase::PathPending && intent.path_batch_id == 3);
                CHECK(intent.kind == WorkIntentKind::GatherContinuation && intent.job == Job::Woodcut);
                CHECK(intent.resident_id == 1 && intent.owner_nation == 1 && intent.target == batch.target);
            }
        } else {
            CHECK((state.resources == Resources{50, 25, 100}));
            CHECK(state.construction_orders.size() == 1);
            if (!state.construction_orders.empty()) {
                const auto &order = state.construction_orders.front();
                CHECK(order.phase == ConstructionPhase::Work && order.kind == BuildingKind::House);
                CHECK((order.delivered == Resources{30, 15, 0}));
                CHECK(order.delivered == order.required);
            }
        }
        World resumed(2, 2);
        CHECK(resumed.load(slot.path, error));
        CHECK(resumed.checksum() == migrated.checksum());
        for (int tick = 0; tick < 250; ++tick) {
            migrated.tick(); resumed.tick();
            CHECK(resumed.checksum() == migrated.checksum());
        }
    }
}

void failed_legacy_continuation_is_not_reanimated() {
    using namespace ribterra;
    World world(70421, 1);
    auto legacy = world.export_state();
    legacy.work_intents.clear();
    legacy.next_work_intent_id = legacy.work_intent_cursor_id = 1;
    legacy.work_building_cursor_id = 1;
    LogisticsTaskState failed;
    failed.task_id = 1; failed.kind = LogisticsTaskKind::HaulToStorage;
    failed.phase = LogisticsPhase::Failed; failed.failure = LogisticsFailure::Unreachable;
    failed.resident_id = 1; failed.destination_building_id = 1;
    failed.quantity = 3; failed.item = ItemKind::Wood; failed.deadline_tick = 10000;
    failed.resume_job = Job::Woodcut; failed.resume_target = {22, 19, WorldLayer::Surface};
    legacy.logistics_tasks = {failed}; legacy.next_logistics_task_id = 2;
    legacy.residents.front().order_phase = OrderPhase::PathFailed;
    legacy.residents.front().path_failure = PathFailure::Unreachable;
    std::string error;
    CHECK(migrate_legacy_work_intents(legacy, error));
    CHECK(legacy.work_intents.empty());
    CHECK(legacy.path_batches.empty());
    CHECK(legacy.logistics_tasks.front().phase == LogisticsPhase::Failed);
    CHECK(legacy.residents.front().order_phase == OrderPhase::PathFailed);
    CHECK(world.import_state(legacy, error));
    for (int i = 0; i < 40; ++i) world.tick();
    CHECK(world.work_intents().empty());
    CHECK(world.residents().front().job == Job::Idle);
}

void waiting_work_intent_roundtrip_retries_after_queue_drain() {
    using namespace ribterra;
    TempSave legacy("ribterra_work_waiting_legacy.ribsave"), slot("ribterra_work_waiting.ribsave");
    const auto fixture = read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                    "world_save_v5_continuation.ribsave");
    write_bytes(legacy.path, fixture);
    World source(1, 1);
    std::string error;
    const bool loaded = source.load(legacy.path, error);
    CHECK(loaded);
    if (!loaded) { std::cerr << error << '\n'; return; }
    auto state = source.export_state();
    CHECK(state.work_intents.size() == 1);
    if (state.work_intents.empty()) return;
    auto &intent = state.work_intents.front();
    intent.phase = WorkIntentPhase::Waiting;
    intent.path_batch_id = 0;
    intent.attempts = 0;
    intent.created_tick = intent.next_attempt_tick = state.ticks;
    intent.admission_deadline_tick = state.ticks + work_admission_deadline_ticks;
    intent.last_attempt_topology_revision = state.topology_revision;
    for (auto &resident : state.residents)
        if (resident.id == intent.resident_id) {
            resident.order_phase = OrderPhase::Idle;
            resident.pending_path_batch = 0;
        }
    state.path_batches.clear();
    state.path_results.clear();
    for (std::size_t i = 0; i < max_path_batches; ++i)
        state.path_results.push_back({state.next_path_batch_id++, PathCommandKind::Move,
                                      true, PathFailure::None, "Completed test command"});
    const bool imported = source.import_state(state, error);
    CHECK(imported);
    if (!imported) { std::cerr << error << '\n'; return; }
    source.tick();
    CHECK(source.work_intents().front().phase == WorkIntentPhase::Waiting);
    CHECK(source.work_intents().front().attempts > 0);
    const bool saved = source.save(slot.path, error);
    CHECK(saved);
    if (!saved) { std::cerr << error << '\n'; return; }
    World resumed(2, 2);
    CHECK(resumed.load(slot.path, error));
    CHECK(resumed.work_intents() == source.work_intents());
    CHECK(resumed.export_state().work_intent_cursor_id == source.export_state().work_intent_cursor_id);
    CHECK(resumed.export_state().work_building_cursor_id == source.export_state().work_building_cursor_id);
    CHECK(resumed.take_path_results() == source.take_path_results());
    bool admitted = false;
    for (int tick = 0; tick < 160; ++tick) {
        source.tick(); resumed.tick();
        CHECK(resumed.checksum() == source.checksum());
        admitted = admitted || std::any_of(source.work_intents().begin(), source.work_intents().end(),
            [](const auto &work) { return work.phase == WorkIntentPhase::PathPending ||
                                         work.completion == WorkCompletionReason::Started; });
    }
    CHECK(admitted);
}

void forged_work_intent_payloads_reject_transactionally() {
    using namespace ribterra;
    TempSave slot("ribterra_work_forgery_source.ribsave"), damaged("ribterra_work_forgery_bad.ribsave");
    write_bytes(slot.path, read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                     "world_save_v5_continuation.ribsave"));
    World source(1, 1);
    std::string error;
    const bool loaded = source.load(slot.path, error);
    CHECK(loaded);
    if (!loaded) { std::cerr << error << '\n'; return; }
    const auto original = read_bytes(slot.path);
    auto reject = [&](const std::function<void(save::WorldSaveT &)> &mutate) {
        const auto bytes = mutate_inventory_payload(original, mutate);
        write_bytes(damaged.path, bytes);
        World target(2, 2);
        const auto before = target.checksum();
        CHECK(!target.load(damaged.path, error));
        CHECK(!error.empty() && target.checksum() == before);
        CHECK(read_bytes(damaged.path) == bytes);
    };
    reject([](auto &state) { state.work_intents.front()->resident_id = 90000; });
    reject([](auto &state) { state.work_intents.front()->owner_nation = 99; });
    reject([](auto &state) { state.work_intents.front()->path_batch_id = state.next_path_batch_id; });
    reject([](auto &state) { state.work_intents.front()->phase = save::WorkIntentPhase::Waiting; });
    reject([](auto &state) { state.work_intents.front()->expected_terrain = save::Terrain::Water; });
    reject([](auto &state) { state.work_intents.front()->kind = save::WorkIntentKind::BuildAssignment; });
    reject([](auto &state) { state.work_intents.front()->created_tick = state.metadata->ticks + 1; });
    reject([](auto &state) { state.work_intents.front()->attempts = 65535; });
    reject([](auto &state) { state.work_intents.front()->last_attempt_topology_revision = state.topology_revision + 1; });
    reject([](auto &state) { state.next_work_intent_id = 1; });
    reject([](auto &state) { state.work_intent_cursor_id = 0; });
    reject([](auto &state) {
        while (state.work_intents.size() <= max_work_intents)
            state.work_intents.push_back(std::make_unique<save::WorkIntentT>(*state.work_intents.front()));
    });
    auto masquerade = original;
    masquerade[20] = 2;
    masquerade[12] = 5; masquerade[16] = 4;
    constexpr std::string_view old_content =
        "52e02f82fa53d54c22b9b3384f91d05a3ef01ef5373ce30c4e3698f52d7abf53";
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < 32; ++i)
        masquerade[64 + i] = static_cast<std::uint8_t>((nibble(old_content[i * 2]) << 4U) |
                                                      nibble(old_content[i * 2 + 1]));
    repair_container_checksum(masquerade);
    write_bytes(damaged.path, masquerade);
    const auto before = source.checksum();
    CHECK(!source.load(damaged.path, error));
    CHECK(error == "Invalid settlement payload.");
    CHECK(source.checksum() == before);
    CHECK(!std::filesystem::exists(damaged.path.string() + ".v5.bak"));
}

void logistics_phases_roundtrip_exactly_once() {
    using namespace ribterra;
    World source(70421, 10);
    CellCoord site{};
    bool found = false;
    for (int y = 20; y <= 30 && !found; ++y)
        for (int x = 20; x <= 30 && !found; ++x)
            if (source.can_build(BuildingKind::House, x, y)) {
                site = {x, y, WorldLayer::Surface}; found = true;
            }
    CHECK(found);
    if (!found) return;
    const auto command = source.build(BuildingKind::House, site.x, site.y);
    CHECK(command.accepted && command.pending);
    std::array<bool, 5> seen{};
    auto verify_resume = [&](std::size_t phase) {
        TempSave slot("ribterra_persistence_logistics_" + std::to_string(phase) + ".ribsave");
        TempSave copy("ribterra_persistence_logistics_copy_" + std::to_string(phase) + ".ribsave");
        std::string error;
        const auto initial = source.export_state();
        World uninterrupted(1, 1), resumed(2, 2);
        CHECK(uninterrupted.import_state(initial, error));
        const bool saved = source.save(slot.path, error);
        CHECK(saved);
        if (!saved) { std::cerr << error << '\n'; return; }
        const bool loaded = resumed.load(slot.path, error);
        CHECK(loaded);
        if (!loaded) { std::cerr << error << '\n'; return; }
        CHECK(resumed.checksum() == source.checksum());
        CHECK(resumed.save(copy.path, error));
        CHECK(read_bytes(slot.path) == read_bytes(copy.path));
        CHECK(resumed.export_state().item_stacks == initial.item_stacks);
        CHECK(resumed.export_state().item_reservations == initial.item_reservations);
        CHECK(resumed.export_state().logistics_tasks == initial.logistics_tasks);
        CHECK(resumed.export_state().construction_orders == initial.construction_orders);
        for (int i = 0; i < 700; ++i) {
            uninterrupted.tick(); resumed.tick();
            CHECK(uninterrupted.checksum() == resumed.checksum());
        }
        CHECK(resumed.item_reservations().empty());
    };
    for (int tick = 0; tick < 700; ++tick) {
        const auto state = source.export_state();
        const bool reserved = !state.item_reservations.empty();
        const bool pickup_route = std::any_of(state.logistics_tasks.begin(), state.logistics_tasks.end(),
            [](const auto &task) { return task.phase == LogisticsPhase::ToPickup && task.path_batch_id; });
        const bool carried = std::any_of(state.item_stacks.begin(), state.item_stacks.end(),
            [](const auto &stack) { return stack.location.kind == ItemLocationKind::Carried; });
        const bool partial_delivery = std::any_of(state.construction_orders.begin(), state.construction_orders.end(),
            [](const auto &order) { return order.delivered != Resources{0,0,0} && order.delivered != order.required; });
        const bool work = std::any_of(state.construction_orders.begin(), state.construction_orders.end(),
            [](const auto &order) { return order.phase == ConstructionPhase::Work; });
        const std::array<bool, 5> phases{reserved, pickup_route, carried, partial_delivery, work};
        for (std::size_t phase = 0; phase < phases.size(); ++phase)
            if (phases[phase] && !seen[phase]) { seen[phase] = true; verify_resume(phase); }
        if (std::all_of(seen.begin(), seen.end(), [](bool value) { return value; })) break;
        source.tick();
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
}

void v3_layers_nations_revisions_and_optional_work_roundtrip() {
    TempSave slot("ribterra_persistence_v3_fields.ribsave");
    ribterra::World source(811, 1);
    auto state = source.export_state();
    CHECK(state.routes.size() == 1);
    state.chunks.front().revision = 0x123456789abcdef0ULL;
    state.residents.front().layer = ribterra::WorldLayer::Mine;
    state.residents.front().nation = 9;
    state.buildings.front().layer = ribterra::WorldLayer::Mine;
    state.buildings.front().nation = 9;
    for (auto &stack : state.item_stacks) stack.owner_nation = 9;
    state.resources = {0, 0, 0};
    state.routes.front().work = ribterra::CellCoord{-1, -1, ribterra::WorldLayer::Mine};
    std::string error;
    CHECK(source.import_state(std::move(state), error));
    CHECK(source.save(slot.path, error));

    ribterra::World loaded(1, 1);
    CHECK(loaded.load(slot.path, error));
    auto restored = loaded.export_state();
    CHECK(restored.chunks.front().revision == 0x123456789abcdef0ULL);
    CHECK(restored.residents.front().layer == ribterra::WorldLayer::Mine);
    CHECK(restored.residents.front().nation == 9);
    CHECK(restored.buildings.front().layer == ribterra::WorldLayer::Mine);
    CHECK(restored.buildings.front().nation == 9);
    CHECK(
        (restored.routes.front().work == ribterra::CellCoord{-1, -1, ribterra::WorldLayer::Mine}));
    CHECK(restored.portals == source.export_state().portals);

    restored.routes.front().work.reset();
    CHECK(loaded.import_state(std::move(restored), error));
    CHECK(loaded.save(slot.path, error));
    ribterra::World null_work(1, 1);
    CHECK(null_work.load(slot.path, error));
    CHECK(!null_work.export_state().routes.front().work.has_value());
}

void pending_path_search_roundtrip_and_resume() {
    using namespace ribterra;
    TempSave slot("ribterra_persistence_pending_path.ribsave");
    World original(1661, 20);
    std::string error;
    auto legacy_profile = original.export_state();
    legacy_profile.terrain_generator = 5;
    CHECK(original.import_state(std::move(legacy_profile), error));
    CellCoord target{100, 100, WorldLayer::Surface};
    bool found = false;
    for (int y = 100; y < 116 && !found; ++y) {
        for (int x = 100; x < 116; ++x) {
            if (original.walkable(x, y, WorldLayer::Surface)) {
                target = {x, y, WorldLayer::Surface};
                found = true;
                break;
            }
        }
    }
    CHECK(found);
    std::vector<int> residents;
    for (int id = 1; id <= 20; ++id) residents.push_back(id);
    const auto queued = original.move(residents, target.x, target.y, target.layer);
    CHECK(queued.accepted && queued.pending && queued.path_batch_id != 0);

    const auto queued_state = original.export_state();
    CHECK(queued_state.path_batches.front().phase == PathPlannerPhase::Queued);
    TempSave queued_slot("ribterra_persistence_path_queued.ribsave");
    CHECK(original.save(queued_slot.path, error));
    World queued_loaded(1, 0);
    CHECK(queued_loaded.load(queued_slot.path, error));
    CHECK(queued_loaded.export_state().path_batches == queued_state.path_batches);

    bool captured_reconstruction = false;
    bool captured_search = false;
    WorldState search_state;
    for (int tick = 0; tick < 100 && !captured_reconstruction; ++tick) {
        original.tick();
        const auto state = original.export_state();
        if (!captured_search && !state.path_batches.empty() &&
            state.path_batches.front().phase == PathPlannerPhase::Search) {
            search_state = state;
            captured_search = true;
        }
        captured_reconstruction = !state.path_batches.empty() &&
                                  state.path_batches.front().phase == PathPlannerPhase::Reconstruct &&
                                  !state.path_batches.front().nodes.empty() &&
                                  std::any_of(state.path_batches.front().drafts.begin(),
                                              state.path_batches.front().drafts.end(),
                                              [](const auto &draft) { return !draft.staged.empty(); });
    }
    CHECK(captured_search);
    CHECK(captured_reconstruction);
    const auto pending = original.export_state();
    CHECK(!pending.path_batches.empty());
    CHECK(pending.path_batches.front().work_units_done != 0);
    CHECK(!pending.path_batches.front().frontier.empty());

    World search_original(1, 0);
    CHECK(search_original.import_state(search_state, error));
    TempSave search_slot("ribterra_persistence_path_search.ribsave");
    CHECK(search_original.save(search_slot.path, error));
    World search_loaded(1, 0);
    CHECK(search_loaded.load(search_slot.path, error));
    CHECK(search_loaded.export_state().path_batches == search_state.path_batches);
    search_original.tick();
    search_loaded.tick();
    CHECK(search_original.checksum() == search_loaded.checksum());

    CHECK(original.save(slot.path, error));
    World loaded(1, 0);
    CHECK(loaded.load(slot.path, error));
    const auto restored = loaded.export_state();
    CHECK(restored.next_path_batch_id == pending.next_path_batch_id);
    CHECK(restored.topology_revision == pending.topology_revision);
    CHECK(restored.path_batches == pending.path_batches);
    CHECK(restored.path_results == pending.path_results);
    CHECK(restored.residents == pending.residents);

    // Materialize the remaining drafts from the real search tree so Stage itself has a
    // deterministic save boundary; a normal small batch can enter and finish Stage in one tick.
    auto staged_state = pending;
    auto &staged_batch = staged_state.path_batches.front();
    bool valid_tree = true;
    for (auto &draft : staged_batch.drafts) {
        draft.staged.clear();
        CellCoord cursor = draft.start;
        bool reached_root = false;
        while (draft.staged.size() < max_path_cells) {
            const auto node = std::find_if(
                staged_batch.nodes.begin(), staged_batch.nodes.end(),
                [&](const PathSearchNodeState &candidate) { return candidate.cell == cursor; });
            if (node == staged_batch.nodes.end()) {
                valid_tree = false;
                break;
            }
            if (node->next == node->cell) {
                reached_root = true;
                break;
            }
            draft.staged.push_back(node->next);
            cursor = node->next;
        }
        valid_tree = valid_tree && reached_root;
        draft.reconstruction_cursor = cursor;
        draft.storage_reserved = true;
        if (!valid_tree)
            break;
    }
    CHECK(valid_tree);
    staged_batch.phase = PathPlannerPhase::Stage;
    staged_batch.reconstruction_resident = staged_batch.drafts.size();
    staged_batch.stage_cursor = staged_batch.drafts.size() / 2;
    World staged_original(1, 0);
    CHECK(staged_original.import_state(staged_state, error));
    TempSave staged_slot("ribterra_persistence_path_stage.ribsave");
    CHECK(staged_original.save(staged_slot.path, error));
    World staged_loaded(1, 0);
    CHECK(staged_loaded.load(staged_slot.path, error));
    CHECK(staged_loaded.export_state().path_batches == staged_state.path_batches);
    for (int tick = 0; tick < 10 && !staged_original.export_state().path_batches.empty(); ++tick) {
        staged_original.tick();
        staged_loaded.tick();
        CHECK(staged_original.checksum() == staged_loaded.checksum());
    }
    CHECK(staged_original.export_state().path_batches.empty());
    CHECK(staged_loaded.export_state().path_batches.empty());

    for (int tick = 0; tick < 300 && !original.export_state().path_batches.empty(); ++tick) {
        original.tick();
        loaded.tick();
        CHECK(original.checksum() == loaded.checksum());
    }
    CHECK(original.export_state().path_batches.empty());
    CHECK(loaded.export_state().path_batches.empty());
    CHECK(!original.export_state().path_results.empty());
    CHECK(original.export_state().path_results == loaded.export_state().path_results);
    TempSave outcome_slot("ribterra_persistence_path_outcome.ribsave");
    CHECK(original.save(outcome_slot.path, error));
    World outcome_loaded(1, 0);
    CHECK(outcome_loaded.load(outcome_slot.path, error));
    CHECK(outcome_loaded.export_state().path_results == original.export_state().path_results);

    auto bad_parent = pending;
    CHECK(bad_parent.path_batches.front().nodes.size() > 1);
    bad_parent.path_batches.front().nodes[1].next = {world_max - 1, world_max - 1,
                                                     WorldLayer::Mine};
    const auto before = loaded.checksum();
    CHECK(!loaded.import_state(std::move(bad_parent), error));
    CHECK(loaded.checksum() == before);
    auto bad_draft = pending;
    bad_draft.path_batches.front().drafts.front().start_x += 0.5F;
    CHECK(!loaded.import_state(std::move(bad_draft), error));
    CHECK(loaded.checksum() == before);

    World queued_twice(1662, 2);
    CHECK(queued_twice.move({1}, 30, 24).accepted);
    CHECK(queued_twice.move({2}, 30, 24).accepted);
    auto multiple_active = queued_twice.export_state();
    CHECK(multiple_active.path_batches.size() == 2);
    multiple_active.path_batches[1].phase = PathPlannerPhase::Search;
    CHECK(!loaded.import_state(std::move(multiple_active), error));
    CHECK(loaded.checksum() == before);
}

void forged_inventory_payloads_reject_before_commit() {
    using namespace ribterra;
    TempSave good("ribterra_persistence_inventory_valid.ribsave");
    TempSave bad("ribterra_persistence_inventory_forged.ribsave");
    World source(70421, 10);
    bool accepted = false;
    for (int y = 20; y <= 30 && !accepted; ++y)
        for (int x = 20; x <= 30 && !accepted; ++x)
            if (source.can_build(BuildingKind::House, x, y))
                accepted = source.build(BuildingKind::House, x, y).accepted;
    CHECK(accepted);
    std::string error;
    const bool saved = source.save(good.path, error);
    CHECK(saved);
    if (!saved) { std::cerr << error << '\n'; return; }
    const auto original = read_bytes(good.path);
    CHECK(!source.item_reservations().empty() && !source.logistics_tasks().empty());
    if (source.item_reservations().empty() || source.logistics_tasks().empty()) return;
    auto reject = [&](const std::function<void(save::WorldSaveT &)> &mutate) {
        const auto bytes = mutate_inventory_payload(original, mutate);
        CHECK(!bytes.empty());
        write_bytes(bad.path, bytes);
        World target(9292, 2);
        const auto before = target.checksum();
        CHECK(!target.load(bad.path, error));
        CHECK(!error.empty());
        CHECK(target.checksum() == before);
        CHECK(read_bytes(bad.path) == bytes);
        CHECK(!std::filesystem::exists(bad.path.string() + ".v4.bak"));
    };
    reject([](auto &state) { state.item_stacks.front()->stack_id = state.next_item_stack_id; });
    reject([](auto &state) { state.item_stacks.front()->owner_nation = 99; });
    reject([](auto &state) { state.storages.front()->building_id = 90000; });
    reject([](auto &state) { state.item_reservations.front()->stack_id = state.next_item_stack_id; });
    reject([](auto &state) { state.item_reservations.front()->quantity = 0xffffffffU; });
    reject([](auto &state) { state.item_reservations.front()->task_id = state.next_logistics_task_id; });
    reject([](auto &state) {
        state.logistics_tasks.front()->phase = save::LogisticsPhase::ToDropoff;
        state.logistics_tasks.front()->carry_stack_id = state.item_stacks.front()->stack_id;
    });
    reject([](auto &state) { state.construction_orders.front()->delivered->wood = 90000; });
    reject([](auto &state) { ++state.resources->wood; });
    reject([](auto &state) { state.next_item_stack_id = 1; });
    reject([](auto &state) {
        while (state.logistics_tasks.size() <= max_logistics_tasks)
            state.logistics_tasks.push_back(std::make_unique<save::LogisticsTaskT>(
                *state.logistics_tasks.front()));
    });

    // A correctly checksummed current body must not be smuggled through the legacy migrator.
    auto masquerade = original;
    masquerade[20] = 2;
    masquerade[12] = 4;
    masquerade[16] = 3;
    constexpr std::string_view old_content =
        "de55d4ba8a19c142b2abf3a0f7492cc195ec601fc02171b6c370df382a8cb401";
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < 32; ++i)
        masquerade[64 + i] = static_cast<std::uint8_t>(
            (nibble(old_content[i * 2]) << 4U) | nibble(old_content[i * 2 + 1]));
    repair_container_checksum(masquerade);
    write_bytes(bad.path, masquerade);
    World target(93, 1);
    const auto before = target.checksum();
    CHECK(!target.load(bad.path, error));
    CHECK(error == "Invalid settlement payload.");
    CHECK(target.checksum() == before);
}

void semantic_import_rejects_without_mutation() {
    ribterra::World world(700, 4);
    const auto checksum = world.checksum();
    std::string error;
    auto state = world.export_state();
    state.residents.front().id = state.residents.back().id;
    CHECK(!world.import_state(std::move(state), error));
    CHECK(world.checksum() == checksum);
    state = world.export_state();
    state.routes.pop_back();
    CHECK(!world.import_state(std::move(state), error));
    CHECK(world.checksum() == checksum);
    state = world.export_state();
    state.chunks.clear();
    CHECK(!world.import_state(std::move(state), error));
    CHECK(world.checksum() == checksum);
}
#include "persistence_professions.inc"
void alpha_save_rejection_preserves_source() {
    using namespace ribterra;
    TempSave safe("ribterra-alpha-safe.save"), unsafe("ribterra-alpha-unsafe.save");
    World source(70421,1);
    auto state=source.export_state();
    state.buildings.push_back({state.next_building_id++,BuildingKind::Workshop,29,29,1,WorldLayer::Surface,1});
    state.buildings.push_back({state.next_building_id++,BuildingKind::Temple,31,29,1,WorldLayer::Surface,1});
    std::string error;CHECK(source.import_state(state,error));CHECK(source.save(safe.path,error));
    const auto original=read_bytes(safe.path);
    for(int scenario=0;scenario<5;++scenario){
        auto bad=mutate_inventory_payload(original,[&](save::WorldSaveT& saved){
            auto economy=decode_economy(saved.economy);
            if(scenario==0){economy.wilderness.enabled=true;economy.wilderness.next_animal=2;economy.wilderness.animals={{1,1,AnimalSpecies::Wolf,AnimalBehaviour::Attacking,24,24,24,24,36,1}};}
            if(scenario==1){Expedition e;e.id=1;e.members={1};e.x=24;e.y=24;economy.wilderness.next_expedition=2;economy.wilderness.expeditions={e};}
            if(scenario==2){economy.wilderness.reduced_rations=true;saved.settlement->reduced_rations=true;}
            if(scenario==3){economy.next_order_id=2;economy.orders.push_back({1,2,1,9});}
            if(scenario==4)saved.buildings.back()->progress=.5f;
            saved.economy=encode_economy(economy);
        });
        // Current alpha saves must still reject forged unsupported states.
        // Supported pre-alpha migration is covered by the frozen 0.25.7 fixtures.
        bad[16]=17;
        bad[20]=5;
        repair_container_checksum(bad);write_bytes(unsafe.path,bad);
        World target(15,2);const auto checksum=target.checksum();
        CHECK(!target.load(unsafe.path,error));if(!error.starts_with("alpha.save_"))std::cerr<<"Alpha scenario "<<scenario<<": "<<error<<"\n";CHECK(error.starts_with("alpha.save_"));
        CHECK(target.checksum()==checksum);CHECK(read_bytes(unsafe.path)==bad);
        CHECK(!std::filesystem::exists(unsafe.path.string()+".bak"));
    }
}

} // namespace

int main(int argc,char**) {
    alpha_save_rejection_preserves_source();
    if(argc>1)return failures?1:0;
    exact_roundtrip_and_resume();
    truncation_mutation_and_transactionality();
    limits_and_versions();
    backup_rotation_and_gap_recovery();
    legacy_import_is_one_way_and_backed_up();
    captured_v2_import_is_one_way_and_resumable();
    captured_v3_import_is_one_way_and_resumable();
    captured_v4_builds_migrate_without_double_payment();
    captured_v5_work_states_preserve_inventory_and_pending_search();
    captured_v6_states_preserve_every_legacy_field();
    profession_and_skill_state_roundtrip();
    forged_profession_payloads_reject_transactionally();
    failed_legacy_continuation_is_not_reanimated();
    waiting_work_intent_roundtrip_retries_after_queue_drain();
    forged_work_intent_payloads_reject_transactionally();
    logistics_phases_roundtrip_exactly_once();
    v3_layers_nations_revisions_and_optional_work_roundtrip();
    pending_path_search_roundtrip_and_resume();
    forged_inventory_payloads_reject_before_commit();
    semantic_import_rejects_without_mutation();
    if (failures != 0) {
        std::cerr << failures << " persistence test(s) failed.\n";
        return 1;
    }
    std::cout << "All persistence tests passed.\n";
    return 0;
}

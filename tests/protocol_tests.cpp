#include "network_frame_generated.h"
#include "network_generated.h"
#include "ribterra/protocol.hpp"
#include "weather_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <zstd.h>

namespace {
int failures = 0;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr "\n";                       \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

using namespace ribterra;
using namespace ribterra::net;

WorldId world_id(std::uint8_t marker) {
    WorldId id{};
    id.front() = marker;
    id.back() = static_cast<std::uint8_t>(marker ^ 0x5a);
    return id;
}

ViewSnapshot sample_view() {
    ViewSnapshot view;
    view.tick = 17;
    view.revision = 4;
    view.terrain_generator = 6;
    view.chunks.push_back(Chunk{{WorldLayer::Surface, 0, 0}});
    view.chunks[0].revision = 0x7c14b9d2u;
    view.chunks[0].tiles[0] = {Terrain::Forest, 91, true};
    view.chunks[0].tiles[1] = {Terrain::Rock, 42, true};
    view.chunks[0].tiles[local_index(1, 1)] = {Terrain::Grass, 0, true};
    view.residents.push_back({7, "Олена", 1.25F, 1.75F, 2, 2, Job::Move, 0.5F, 93, 5.5F, 3, 4, 5});
    view.buildings.push_back({9, BuildingKind::House, 1, 1, 0.75F});
    view.buildings.push_back({10, BuildingKind::Camp, 0, 0, 1.0F});
    view.resources = {31, 22, 13};
    view.inventory = {view.resources, {21, 22, 13}, {10, 0, 0}, {25, 22, 13}, {6, 0, 0}};
    view.storages.push_back({10, 60, 10000});
    view.resident_logistics.push_back(
        {7, ItemKind::Wood, 6, resident_carry_capacity, LogisticsPhase::ToDropoff, LogisticsFailure::None});
    view.resident_work.push_back({7, WorkIntentKind::GatherContinuation, WorkIntentPhase::Waiting,
                                  PathFailure::None, WorkCompletionReason::None, 2});
    ResidentProfessionView profile;
    profile.resident_id = 7;
    profile.assignment.profession = Profession::Woodcutter;
    profile.assignment.worksite = CellCoord{0, 0, WorldLayer::Surface};
    profile.wait_reason = ProfessionWaitReason::DeliveryBoundary;
    profile.deferred = true;
    for (std::size_t i = 0; i < skill_count; ++i)
        profile.skills[i] = {
            static_cast<Skill>(i), static_cast<int>(i + 3), {1200, 500, -200, 1500, 1500, 11500, false}};
    view.resident_professions.push_back(profile);
    view.construction_sites.push_back({9,
                                       BuildingKind::House,
                                       {1, 1, WorldLayer::Surface},
                                       World::cost(BuildingKind::House),
                                       World::cost(BuildingKind::House),
                                       0.75F,
                                       ConstructionPhase::Work,
                                       LogisticsFailure::None,
                                       ConstructionWorkerPhase::Waiting,
                                       PathFailure::None,
                                       0});
    view.housing = 16;
    view.tutorial_step = 2;
    test::bind_fixture_weather(view, 70421, {}, view.tick);
    return view;
}

bool same_view(const ViewSnapshot &a, const ViewSnapshot &b) {
    if (a.tick != b.tick || a.revision != b.revision ||
        a.terrain_generator != b.terrain_generator || a.chunks != b.chunks || a.portals != b.portals ||
        a.residents.size() != b.residents.size() || a.buildings.size() != b.buildings.size() ||
        a.resources.wood != b.resources.wood || a.resources.stone != b.resources.stone ||
        a.resources.food != b.resources.food || a.housing != b.housing || a.day != b.day ||
        a.tutorial_step != b.tutorial_step || a.winter != b.winter || a.weather != b.weather ||
        a.inventory != b.inventory ||
        a.storages != b.storages ||
        a.resident_logistics != b.resident_logistics || a.resident_work != b.resident_work ||
        a.construction_sites != b.construction_sites || a.resident_professions != b.resident_professions ||
        a.manual_orders != b.manual_orders || a.resident_controls != b.resident_controls ||
        a.control_players != b.control_players || a.control_epoch != b.control_epoch)
        return false;
    for (std::size_t i = 0; i < a.residents.size(); ++i) {
        const auto &x = a.residents[i];
        const auto &y = b.residents[i];
        if (x.id != y.id || x.name != y.name || x.x != y.x || x.y != y.y || x.target_x != y.target_x ||
            x.target_y != y.target_y || x.job != y.job || x.progress != y.progress || x.health != y.health ||
            x.hunger != y.hunger || x.wood_xp != y.wood_xp || x.mining_xp != y.mining_xp ||
            x.farming_xp != y.farming_xp || x.layer != y.layer || x.nation != y.nation ||
            x.order_phase != y.order_phase || x.path_failure != y.path_failure ||
            x.pending_path_batch != y.pending_path_batch)
            return false;
    }
    for (std::size_t i = 0; i < a.buildings.size(); ++i) {
        const auto &x = a.buildings[i];
        const auto &y = b.buildings[i];
        if (x.id != y.id || x.kind != y.kind || x.x != y.x || x.y != y.y || x.progress != y.progress ||
            x.layer != y.layer || x.nation != y.nation)
            return false;
    }
    return true;
}

void roundtrip(const Packet &packet) {
    std::string error;
    const auto bytes = encode(packet);
    const auto decoded = decode(bytes, error);
    CHECK(decoded.has_value());
    CHECK(error.empty());
    if (!decoded)
        return;
    CHECK(decoded->sequence == packet.sequence);
    CHECK(decoded->payload.index() == packet.payload.index());
    CHECK(encode(*decoded) == bytes);
}
// Bypass the trusted encoder's semantic checks, but preserve valid FlatBuffers
// and the frame checksum so hostile field tests exercise the receiver itself.
template <typename Mutate>
std::vector<std::uint8_t> mutated_wire_packet(const Packet &packet, Mutate mutate) {
    const auto encoded = encode(packet);
    const auto frame = ribterra::wire::GetFrame(encoded.data());
    std::vector<std::uint8_t> expanded(frame->expanded_size());
    if (frame->compression() == ribterra::wire::Compression::Zstd) {
        const auto size =
            ZSTD_decompress(expanded.data(), expanded.size(), frame->data()->data(), frame->data()->size());
        if (ZSTD_isError(size) || size != expanded.size())
            throw std::runtime_error("Cannot expand test packet");
    } else
        std::copy(frame->data()->begin(), frame->data()->end(), expanded.begin());
    std::unique_ptr<ribterra::wire::PacketT> native(ribterra::wire::GetPacket(expanded.data())->UnPack());
    mutate(*native);
    flatbuffers::FlatBufferBuilder inner;
    ribterra::wire::FinishPacketBuffer(inner, ribterra::wire::Packet::Pack(inner, native.get()));
    ribterra::wire::FrameT replacement;
    replacement.protocol = protocol_version;
    replacement.expanded_size = static_cast<std::uint32_t>(inner.GetSize());
    replacement.data.assign(inner.GetBufferPointer(), inner.GetBufferPointer() + inner.GetSize());
    replacement.content_sha256 = sha256(replacement.data);
    flatbuffers::FlatBufferBuilder outer;
    ribterra::wire::FinishFrameBuffer(outer, ribterra::wire::Frame::Pack(outer, &replacement));
    return {outer.GetBufferPointer(), outer.GetBufferPointer() + outer.GetSize()};
}

void every_payload_roundtrips() {
    const Versions versions;
    const auto id = world_id(7);
    const auto view = sample_view();
    const auto snapshot = make_snapshot(view, id);
    const std::string token(64, 'a');
    const std::string nonce(64, 'b');
    const std::string proof(64, 'c');
    std::uint64_t sequence = 1;
    roundtrip({sequence++, Hello{"Ірина", versions, token, proof}});
    roundtrip({sequence++, Challenge{nonce, true, versions}});
    roundtrip({sequence++, Welcome{3, token, id, 42}});
    roundtrip({sequence++, ReplyAck{{{11, CommandReplyStatus::Queued},
                                     {11, CommandReplyStatus::Applied},
                                     {12, CommandReplyStatus::Rejected}}}});
    roundtrip({sequence++, Rejected{"version mismatch"}});
    roundtrip({sequence++, GameCommand{11, 3, 17, CommandType::Move, {1, 2}, 3, 4, BuildingKind::House}});
    roundtrip({sequence++, GameCommand{12,
                                       3,
                                       17,
                                       CommandType::CancelConstruction,
                                       {},
                                       0,
                                       0,
                                       BuildingKind::House,
                                       WorldLayer::Surface,
                                       9}});
    roundtrip({sequence++, CommandReply{11, 0, 3, CommandReplyStatus::Queued, PathFailure::None, "Queued"}});
    roundtrip(
        {sequence++, CommandReply{11, 18, 3, CommandReplyStatus::Applied, PathFailure::None, "Applied"}});
    roundtrip({sequence++, CommandReply{12, 18, 3, CommandReplyStatus::Rejected, PathFailure::Unreachable,
                                        "Unreachable"}});
    GameCommand assign{13, 3, 17, CommandType::AssignProfession, {1, 2, 99}};
    assign.assignment.profession = Profession::Miner;
    assign.assignment.priorities = {0, 5, 0, 0, 1};
    assign.assignment.worksite = CellCoord{-9, 12, WorldLayer::Mine};
    roundtrip({sequence++, assign});
    roundtrip({sequence++, GameCommand{14, 3, 17, CommandType::Stop, {1, 2}}});
    roundtrip({sequence++,
               CommandReply{13,
                            18,
                            3,
                            CommandReplyStatus::Applied,
                            PathFailure::None,
                            "Partial resident results",
                            {{1, ResidentCommandStatus::Applied, PathFailure::None},
                             {2, ResidentCommandStatus::Deferred, PathFailure::None},
                             {99, ResidentCommandStatus::Rejected, PathFailure::ResidentUnavailable}}}});
    roundtrip({sequence++, snapshot});
    roundtrip({sequence++, SnapshotAck{4, false}});
    roundtrip({sequence++, ChatSend{"Вітаю!"}});
    roundtrip({sequence++, ChatEntry{8, 3, "Ірина", "Привіт"}});
    roundtrip({sequence++, Ping{0x123456789abcdef0ULL}});
}

void malformed_and_semantically_invalid_packets_fail() {
    std::string error;
    CHECK(!decode({}, error).has_value());
    std::vector<std::uint8_t> oversized(max_packet_bytes + 1, 0);
    CHECK(!decode(oversized, error).has_value());

    auto good = encode({1, Ping{9}});
    good.resize(good.size() - 1);
    CHECK(!decode(good, error).has_value());

    auto invalid_enum =
        encode({2, GameCommand{1, 1, 0, static_cast<CommandType>(255), {}, 1, 1, BuildingKind::House}});
    CHECK(!decode(invalid_enum, error).has_value());
    auto duplicate_ids = encode(
        {2, GameCommand{1, 1, 0, CommandType::Move, std::vector<int>(1500, 1), 1, 1, BuildingKind::House}});
    CHECK(!decode(duplicate_ids, error).has_value());
    auto long_chat = encode({3, ChatSend{std::string(1025, 'x')}});
    CHECK(!decode(long_chat, error).has_value());
    auto long_name = encode({4, Hello{std::string(65, 'x'), Versions{}, {}, {}}});
    CHECK(!decode(long_name, error).has_value());
    bool invalid_reply_threw = false;
    try {
        (void)encode({5, CommandReply{9, 1, 1, CommandReplyStatus::Invalid, PathFailure::None, "invalid"}});
    } catch (const std::runtime_error &) {
        invalid_reply_threw = true;
    }
    CHECK(invalid_reply_threw);
    bool invalid_ack_threw = false;
    try {
        (void)encode({5, ReplyAck{{{9, CommandReplyStatus::Queued}, {9, CommandReplyStatus::Queued}}}});
    } catch (const std::runtime_error &) {
        invalid_ack_threw = true;
    }
    CHECK(invalid_ack_threw);

    auto view = sample_view();
    view.residents.front().x = std::numeric_limits<float>::quiet_NaN();
    bool threw = false;
    try {
        (void)encode({5, make_snapshot(view, world_id(1))});
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
    view = sample_view();
    view.chunks[0].tiles[2] = {Terrain::Rock, 25, false};
    threw = false;
    try {
        (void)encode({6, make_snapshot(view, world_id(1))});
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);

    view = sample_view();
    for (auto &tile : view.chunks[0].tiles)
        tile = {Terrain::Forest, 1000000, true};
    auto compressed = encode({7, make_snapshot(view, world_id(2))});
    CHECK(compressed.size() < max_packet_bytes);
    CHECK(decode(compressed, error).has_value());
    CHECK(!decode(compressed, error, 4096).has_value());
    CHECK(error == "Expanded packet bounds");
    compressed[compressed.size() / 2] ^= 0x5a;
    CHECK(!decode(compressed, error).has_value());
}

void full_and_delta_are_transactional() {
    const auto id = world_id(3);
    GameServer server;
    const auto joined = server.join("projection-client", server.config().versions);
    CHECK(joined.accepted);
    auto baseline = server.snapshot(joined.player);
    ViewSnapshot applied;
    WorldId applied_id{};
    std::string error;
    const auto full = make_snapshot(baseline, id);
    CHECK(apply_snapshot(applied, applied_id, full, error));
    CHECK(applied_id == id && same_view(applied, baseline));

    server.tick();
    auto current = server.snapshot(joined.player);
    const auto delta = make_snapshot(current, id, &baseline);
    CHECK(!delta.full && delta.baseline == baseline.revision);
    CHECK(apply_snapshot(applied, applied_id, delta, error));
    CHECK(same_view(applied, current));

    auto after_removals = current;
    ++after_removals.tick;
    test::bind_fixture_weather(after_removals, 70421, {}, after_removals.tick);
    ++after_removals.revision;
    const int removed_resident = after_removals.residents.front().id;
    const int removed_building = after_removals.buildings.front().id;
    after_removals.residents.erase(after_removals.residents.begin());
    after_removals.resident_professions.erase(after_removals.resident_professions.begin());
    after_removals.resident_controls.erase(after_removals.resident_controls.begin());
    after_removals.buildings.erase(after_removals.buildings.begin());
    std::erase_if(after_removals.storages,
                  [&](const auto &storage) { return storage.building_id == removed_building; });
    const auto removal_delta = make_snapshot(after_removals, id, &current);
    CHECK(removal_delta.removed_residents == std::vector<int>{removed_resident});
    CHECK(removal_delta.removed_buildings == std::vector<int>{removed_building});
    CHECK(removal_delta.removed_storages == std::vector<int>{removed_building});
    CHECK(apply_snapshot(applied, applied_id, removal_delta, error));
    CHECK(same_view(applied, after_removals));

    auto stale_view = current;
    auto stale_id = id;
    auto wrong_baseline = removal_delta;
    ++wrong_baseline.baseline;
    CHECK(!apply_snapshot(stale_view, stale_id, wrong_baseline, error));
    CHECK(stale_id == id && same_view(stale_view, current));

    GameServer same_seed_other_world;
    const auto other_join =
        same_seed_other_world.join("other-world", same_seed_other_world.config().versions);
    CHECK(other_join.accepted);
    const auto same_seed_baseline = same_seed_other_world.snapshot(other_join.player);
    auto other_world_delta = make_snapshot(current, world_id(4), &same_seed_baseline);
    CHECK(!apply_snapshot(stale_view, stale_id, other_world_delta, error));
    CHECK(stale_id == id && same_view(stale_view, current));
}

void active_production_keeps_public_storage_snapshot_valid() {
    auto view = sample_view();
    view.chunks.front().tiles[local_index(2, 1)] = {Terrain::Grass, 0, true};
    view.buildings.push_back({11, BuildingKind::Sawmill, 2, 1, 1.F});
    view.economy.revision = 1;
    view.economy.next_order_id = 2;
    view.economy.orders.push_back(
        {1, 11, 7, 3, 2, 0, 0, ProductionPhase::Materials, 0, false, {}});

    ViewSnapshot applied;
    WorldId applied_id{};
    std::string error;
    CHECK(apply_snapshot(applied, applied_id, make_snapshot(view, world_id(21)), error));
    CHECK(same_view(applied, view));
    CHECK(applied.economy == view.economy);

    auto leaks_internal_storage = make_snapshot(view, world_id(22));
    leaks_internal_storage.storages.push_back({11, 0, 100});
    CHECK(!apply_snapshot(applied, applied_id, leaks_internal_storage, error));
    CHECK(error == "Storage for private or invalid building" && same_view(applied, view));
    CHECK(applied.economy == view.economy && applied_id == world_id(21));
}

void logistics_deltas_and_invalid_references_are_transactional() {
    const auto id = world_id(11);
    const auto baseline = sample_view();
    ViewSnapshot applied;
    WorldId applied_id{};
    std::string error;
    CHECK(apply_snapshot(applied, applied_id, make_snapshot(baseline, id), error));
    auto current = baseline;
    ++current.tick;
    test::bind_fixture_weather(current, 70421, {}, current.tick);
    ++current.revision;
    current.resident_logistics.front().quantity = 4;
    current.inventory.carried.wood = 4;
    current.inventory.stored.wood = 27;
    current.storages.front().used = 62;
    current.construction_sites.front().work = .9F;
    current.construction_sites.front().worker_phase = ConstructionWorkerPhase::Planning;
    current.construction_sites.front().assigned_workers = 1;
    current.resident_work.front().phase = WorkIntentPhase::PathPending;
    ++current.resident_work.front().attempts;
    const auto delta = make_snapshot(current, id, &baseline);
    CHECK(delta.residents.empty() && delta.buildings.empty());
    CHECK(delta.resident_logistics.size() == 1 && delta.construction_sites.size() == 1 &&
          (delta.storages == std::vector<StorageView>{{10, 62, 10000}}));
    CHECK(delta.resident_work.size() == 1);
    roundtrip({40, delta});
    CHECK(apply_snapshot(applied, applied_id, delta, error));
    CHECK(same_view(applied, current));
    auto removed = current;
    ++removed.tick;
    test::bind_fixture_weather(removed, 70421, {}, removed.tick);
    ++removed.revision;
    removed.resident_logistics.clear();
    removed.resident_work.clear();
    removed.construction_sites.clear();
    removed.inventory.stored.wood += removed.inventory.carried.wood;
    removed.inventory.carried.wood = 0;
    const auto removals = make_snapshot(removed, id, &current);
    CHECK(removals.removed_resident_logistics == std::vector<int>{7});
    CHECK(removals.removed_construction_sites == std::vector<int>{9});
    CHECK(removals.removed_resident_work == std::vector<int>{7});
    CHECK(apply_snapshot(applied, applied_id, removals, error));
    CHECK(same_view(applied, removed));
    const auto stable = applied;
    auto hidden = baseline;
    hidden.resident_logistics.front().resident_id = 999;
    CHECK(!apply_snapshot(applied, applied_id, make_snapshot(hidden, world_id(12)), error));
    CHECK(error == "Logistics for private resident" && same_view(applied, stable));
    hidden = baseline;
    hidden.residents.front().nation = 2;
    CHECK(!apply_snapshot(applied, applied_id, make_snapshot(hidden, world_id(12)), error));
    CHECK(error == "Logistics for private resident" && same_view(applied, stable));
    hidden = baseline;
    hidden.construction_sites.front().site = {150, 150, WorldLayer::Mine};
    CHECK(!apply_snapshot(applied, applied_id, make_snapshot(hidden, world_id(12)), error));
    CHECK(error == "Construction on private tile" && same_view(applied, stable));
    hidden = baseline;
    hidden.resident_work.front().resident_id = 999;
    CHECK(!apply_snapshot(applied, applied_id, make_snapshot(hidden, world_id(12)), error));
    CHECK(error == "Work for private resident" && same_view(applied, stable));
    hidden = baseline;
    hidden.resident_logistics.clear();
    hidden.residents.front().nation = 2;
    CHECK(!apply_snapshot(applied, applied_id, make_snapshot(hidden, world_id(12)), error));
    CHECK(error == "Work for private resident" && same_view(applied, stable));
    const auto rejects = [&](auto mutate) {
        auto invalid = make_snapshot(baseline, id);
        mutate(invalid);
        CHECK(!apply_snapshot(applied, applied_id, invalid, error));
        CHECK(same_view(applied, stable));
    };
    rejects([](auto &s) { ++s.inventory.available.wood; });
    rejects([](auto &s) { s.inventory.reserved.stone = -1; });
    rejects([](auto &s) { s.inventory.carried.food = std::numeric_limits<int>::max(); });
    rejects([](auto &s) { s.storages.front().used = s.storages.front().capacity + 1; });
    rejects([](auto &s) { s.storages.push_back(s.storages.front()); });
    rejects([](auto &s) { s.resident_logistics.front().quantity = resident_carry_capacity + 1; });
    rejects([](auto &s) { s.resident_logistics.front().failure = LogisticsFailure::StorageFull; });
    rejects([](auto &s) { s.construction_sites.front().delivered.stone = 999; });
    rejects([](auto &s) { s.construction_sites.front().work = std::numeric_limits<float>::quiet_NaN(); });
    rejects([](auto &s) { s.resident_logistics.push_back(s.resident_logistics.front()); });
    rejects([](auto &s) { s.construction_sites.push_back(s.construction_sites.front()); });
    rejects([](auto &s) { s.resident_work.push_back(s.resident_work.front()); });
    rejects([](auto &s) { s.resident_work.front().attempts = max_work_admission_attempts + 1; });
    rejects([](auto &s) { s.resident_work.front().failure = PathFailure::Unreachable; });
    rejects([](auto &s) { s.resident_work.front().phase = WorkIntentPhase::Complete; });
    rejects([](auto &s) { s.resident_work.front().phase = WorkIntentPhase::Failed; });
    rejects([](auto &s) { s.resident_work.front().completion = WorkCompletionReason::Exhausted; });
    rejects([](auto &s) { s.resident_work.front().kind = static_cast<WorkIntentKind>(255); });
    rejects([](auto &s) { s.resident_work.front().phase = static_cast<WorkIntentPhase>(255); });
    rejects([](auto &s) {
        s.resident_work.front().phase = WorkIntentPhase::PathPending;
        s.resident_work.front().attempts = 0;
    });
    rejects([](auto &s) { s.construction_sites.front().worker_phase = ConstructionWorkerPhase::None; });
    rejects([](auto &s) { s.construction_sites.front().assigned_workers = 1; });
    rejects([](auto &s) {
        s.construction_sites.front().worker_phase = ConstructionWorkerPhase::Working;
        s.construction_sites.front().assigned_workers = 1;
        s.construction_sites.front().worker_failure = PathFailure::Unreachable;
    });

    // A retry is neither a fatal path failure nor successful cargo delivery. Resource
    // exhaustion is a separate normal terminal, with no failure code.
    auto work_terminal = baseline;
    work_terminal.resident_work.front().phase = WorkIntentPhase::Complete;
    work_terminal.resident_work.front().completion = WorkCompletionReason::Exhausted;
    auto exhausted = make_snapshot(work_terminal, world_id(13));
    roundtrip({43, exhausted});
    CHECK(apply_snapshot(applied, applied_id, exhausted, error));
    CHECK(applied.resident_work.front().failure == PathFailure::None);
    work_terminal.resident_work.front().phase = WorkIntentPhase::Failed;
    work_terminal.resident_work.front().completion = WorkCompletionReason::None;
    work_terminal.resident_work.front().failure = PathFailure::Timeout;
    work_terminal.construction_sites.front().worker_failure = PathFailure::Unreachable;
    roundtrip({44, make_snapshot(work_terminal, world_id(14))});

    auto missing_identity = GameCommand{
        12, 3, 17, CommandType::CancelConstruction, {}, 0, 0, BuildingKind::House, WorldLayer::Surface, 0};
    CHECK(!decode(encode({41, missing_identity}), error));
    missing_identity.building_id = 9;
    missing_identity.residents = {7};
    CHECK(!decode(encode({42, missing_identity}), error));
}

void profession_deltas_outcomes_and_private_data_are_strict() {
    const auto baseline = sample_view();
    const auto id = world_id(21);
    ViewSnapshot applied;
    WorldId applied_id{};
    std::string error;
    CHECK(apply_snapshot(applied, applied_id, make_snapshot(baseline, id), error));
    auto current = baseline;
    ++current.tick;
    test::bind_fixture_weather(current, 70421, {}, current.tick);
    ++current.revision;
    auto &profile = current.resident_professions.front();
    profile.assignment.profession = Profession::Miner;
    profile.assignment.priorities = {0, 5, 0, 0, 1};
    profile.manual_override = true;
    profile.skills[1].experience += 10;
    profile.skills[1].modifier = {1400, 500, -100, 1800, 1500, 11500, true};
    auto delta = make_snapshot(current, id, &baseline);
    CHECK(delta.residents.empty() && delta.resident_professions.size() == 1);
    roundtrip({1, delta});
    CHECK(apply_snapshot(applied, applied_id, delta, error) && same_view(applied, current));
    auto removed = current;
    ++removed.tick;
    test::bind_fixture_weather(removed, 70421, {}, removed.tick);
    ++removed.revision;
    removed.resident_professions.clear();
    auto removals = make_snapshot(removed, id, &current);
    CHECK(removals.removed_resident_professions == std::vector<int>{7});
    CHECK(apply_snapshot(applied, applied_id, removals, error) && same_view(applied, removed));
    const auto stable = applied;
    const auto rejects = [&](auto mutate) {
        auto invalid = make_snapshot(baseline, world_id(22));
        mutate(invalid);
        CHECK(!apply_snapshot(applied, applied_id, invalid, error));
        CHECK(same_view(applied, stable));
    };
    rejects([](auto &s) { s.resident_professions.push_back(s.resident_professions.front()); });
    rejects([](auto &s) { s.resident_professions.front().resident_id = 999; });
    rejects([](auto &s) {
        s.resident_logistics.clear();
        s.resident_work.clear();
        s.residents.front().nation = 2;
    });
    rejects([](auto &s) {
        s.resident_professions.front().assignment.worksite = CellCoord{99, 99, WorldLayer::Mine};
    });
    rejects([](auto &s) { s.resident_professions.front().assignment.priorities[0] = 6; });
    rejects(
        [](auto &s) { s.resident_professions.front().assignment.profession = static_cast<Profession>(255); });
    rejects(
        [](auto &s) { s.resident_professions.front().wait_reason = static_cast<ProfessionWaitReason>(255); });
    rejects([](auto &s) { s.resident_professions.front().skills[1].skill = Skill::Woodcutting; });
    rejects([](auto &s) { s.resident_professions.front().skills[1].experience = -1; });
    rejects(
        [](auto &s) { s.resident_professions.front().skills[1].experience = maximum_skill_experience + 1; });
    rejects([](auto &s) { ++s.resident_professions.front().skills[1].modifier.speed_bp; });
    rejects([](auto &s) { ++s.resident_professions.front().skills[1].modifier.applied_bp; });
    rejects([](auto &s) { s.resident_professions.front().skills[1].modifier.capped = true; });
    rejects([](auto &s) { s.resident_professions.front().skills[1].modifier.skill_sum_bp = INT32_MAX; });

    GameCommand command{1, 1, 0, CommandType::AssignProfession, {1}};
    command.assignment.profession = Profession::Miner;
    auto invalid_command = command;
    invalid_command.assignment.priorities[0] = 6;
    CHECK(!decode(encode({1, invalid_command}), error));
    invalid_command = command;
    invalid_command.assignment.worksite = CellCoord{world_max, 1, WorldLayer::Surface};
    CHECK(!decode(encode({1, invalid_command}), error));
    invalid_command = command;
    invalid_command.type = CommandType::Stop;
    CHECK(!decode(encode({1, invalid_command}), error));
    invalid_command = command;
    invalid_command.x = 1;
    CHECK(!decode(encode({1, invalid_command}), error));
    const auto reply_rejects = [&](CommandReply reply) {
        bool rejected = false;
        try {
            (void)encode({1, reply});
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        CHECK(rejected);
    };
    CommandReply reply{1,
                       1,
                       1,
                       CommandReplyStatus::Applied,
                       PathFailure::None,
                       "Applied",
                       {{1, ResidentCommandStatus::Applied, PathFailure::None}}};
    auto invalid_reply = reply;
    invalid_reply.residents.push_back(invalid_reply.residents.front());
    reply_rejects(invalid_reply);
    invalid_reply = reply;
    invalid_reply.residents.front().failure = PathFailure::Unreachable;
    reply_rejects(invalid_reply);
    invalid_reply = reply;
    invalid_reply.status = CommandReplyStatus::Rejected;
    reply_rejects(invalid_reply);
    invalid_reply = reply;
    invalid_reply.residents.front().status = static_cast<ResidentCommandStatus>(255);
    reply_rejects(invalid_reply);
    invalid_reply = reply;
    invalid_reply.residents.assign(max_path_batch_residents + 1, reply.residents.front());
    reply_rejects(invalid_reply);
    const Packet full{1, make_snapshot(baseline, id)};
    CHECK(!decode(
        mutated_wire_packet(
            full, [](auto &p) { p.payload.AsSnapshot()->resident_professions.front()->skills.pop_back(); }),
        error));
    CHECK(error == "Resident skill count");
    CHECK(!decode(mutated_wire_packet(
                      full,
                      [](auto &p) {
                          p.payload.AsSnapshot()->resident_professions.front()->skills.push_back(
                              std::make_unique<ribterra::wire::ResidentSkillT>());
                          auto &extra = p.payload.AsSnapshot()->resident_professions.front()->skills.back();
                          extra->modifier = std::make_unique<ribterra::wire::ModifierBreakdown>();
                      }),
                  error));
    CHECK(error == "Resident skill count");
    CHECK(!decode(
        mutated_wire_packet(
            full,
            [](auto &p) {
                p.payload.AsSnapshot()->resident_professions.front()->assignment->priorities.pop_back();
            }),
        error));
    CHECK(error == "Invalid profession assignment size");
    CHECK(!decode(mutated_wire_packet({1, reply},
                                      [](auto &p) {
                                          auto &rows = p.payload.AsReply()->residents;
                                          rows.push_back(rows.front());
                                          auto &order_ids = p.payload.AsReply()->resident_order_ids;
                                          order_ids.push_back(order_ids.front());
                                      }),
                  error));
    CHECK(error == "Invalid resident command outcome");
    CHECK(
        !decode(mutated_wire_packet({1, command}, [](auto &p) { p.payload.AsCommand()->assignment.reset(); }),
                error));
    CHECK(error == "Unexpected profession assignment");
}

void password_proof_binds_handshake_fields() {
    Hello hello{"Марко", Versions{}, std::string(64, 'a'), {}};
    Challenge challenge{std::string(64, 'b'), true, Versions{}};
    const auto proof = password_proof("correct horse", challenge, hello);
    CHECK(proof.size() == 64);
    CHECK(proof == password_proof("correct horse", challenge, hello));
    CHECK(proof != password_proof("wrong horse", challenge, hello));
    auto changed_challenge = challenge;
    changed_challenge.nonce[0] = 'c';
    CHECK(proof != password_proof("correct horse", changed_challenge, hello));
    auto changed_hello = hello;
    changed_hello.name += "x";
    CHECK(proof != password_proof("correct horse", challenge, changed_hello));
    changed_hello = hello;
    ++changed_hello.versions.protocol;
    CHECK(proof != password_proof("correct horse", challenge, changed_hello));
    changed_hello = hello;
    changed_hello.resume_token[0] = 'd';
    CHECK(proof != password_proof("correct horse", challenge, changed_hello));
}

void terrain_generator_wire_contract() {
    const Packet snapshot{1, make_snapshot(sample_view(), world_id(31))};
    std::string error;
    for (std::uint32_t generator = 1; generator <= 7; ++generator) {
        const auto legacy = mutated_wire_packet(snapshot, [&](auto &packet) {
            packet.payload.AsSnapshot()->terrain_generator = generator;
        });
        const auto decoded = decode(legacy, error);
        CHECK(decoded.has_value());
        CHECK(error.empty());
        if (decoded) {
            const auto *payload = std::get_if<Snapshot>(&decoded->payload);
            CHECK(payload && payload->terrain_generator == generator);
        }
    }
    const auto current = mutated_wire_packet(snapshot, [](auto &packet) {
        packet.payload.AsSnapshot()->terrain_generator = generator_version;
    });
    const auto decoded_current = decode(current, error);
    CHECK(decoded_current.has_value());
    CHECK(error.empty());
    if (decoded_current) {
        const auto *payload = std::get_if<Snapshot>(&decoded_current->payload);
        CHECK(payload && payload->terrain_generator == generator_version);
    }

    // Packing a scalar equal to the schema default omits it from the table.
    // The receiver must reject that missing capability instead of assuming a
    // historical or current terrain profile.
    const auto omitted = mutated_wire_packet(snapshot, [](auto &packet) {
        packet.payload.AsSnapshot()->terrain_generator = 0;
    });
    CHECK(!decode(omitted, error));
    CHECK(error == "Invalid snapshot terrain generator");

    const auto future = mutated_wire_packet(snapshot, [](auto &packet) {
        packet.payload.AsSnapshot()->terrain_generator = generator_version + 1;
    });
    CHECK(!decode(future, error));
    CHECK(error == "Invalid snapshot terrain generator");
}

void multipart_chunks_are_bounded_and_transactional() {
    ViewSnapshot large = sample_view();
    large.chunks.clear();
    std::uint32_t random = 424242;
    for (auto layer : {WorldLayer::Surface, WorldLayer::Mine})
        for (int cx = -3; cx < 5; ++cx)
            for (int cy = -3; cy < 5; ++cy) {
                Chunk chunk{{layer, cx, cy}};
                for (auto &tile : chunk.tiles) {
                    random = random * 1664525U + 1013904223U;
                    tile = {layer == WorldLayer::Mine ? Terrain::MineOre : Terrain::Forest,
                            static_cast<int>(random % 1000000), true};
                }
                large.chunks.push_back(std::move(chunk));
            }
    large.residents.clear();
    large.resident_logistics.clear();
    large.resident_work.clear();
    large.resident_professions.clear();
    large.manual_orders.clear();
    large.resident_controls.clear();
    large.control_epoch = (std::uint64_t{1} << 40) + 7;
    large.control_players = {{1, "Controller"}};
    const auto sample_profile = sample_view().resident_professions.front();
    for (int resident_id = 1; resident_id <= 6000; ++resident_id) {
        large.residents.push_back({resident_id, "Carrier", 1.F, 1.F, 1, 1});
        large.resident_logistics.push_back({resident_id, ItemKind::Wood, 1, resident_carry_capacity,
                                            LogisticsPhase::ToDropoff, LogisticsFailure::None});
        large.resident_work.push_back({resident_id, WorkIntentKind::GatherContinuation,
                                       WorkIntentPhase::Waiting, PathFailure::None,
                                       WorkCompletionReason::None, 2});
        auto profile = sample_profile;
        profile.resident_id = resident_id;
        large.resident_professions.push_back(std::move(profile));
        large.manual_orders.push_back({(std::uint64_t{1} << 40) + resident_id, resident_id,
                                       ManualOrderKind::Move, CellCoord{1, 1, WorldLayer::Surface},
                                       GatherCompletion::UntilExhausted, ManualOrderPhase::WaitingBoundary,
                                       PathFailure::None, ManualOrderCompletion::None});
        large.resident_controls.push_back(
            {resident_id, 1, "Controller", true, false, static_cast<std::uint64_t>(resident_id)});
    }
    large.resources.wood = 10000;
    large.inventory.total.wood = 10000;
    large.inventory.available.wood = 4000;
    large.inventory.reserved.wood = 6000;
    large.inventory.stored.wood = 4000;
    large.inventory.carried.wood = 6000;
    auto full = make_snapshot(large, world_id(9));
    auto parts = split_snapshot(full);
    CHECK(parts.size() > 1 && parts.size() <= 128);
    std::vector<Snapshot> decoded_parts;
    std::string error;
    for (const auto &part : parts) {
        auto bytes = encode({1, part});
        CHECK(bytes.size() <= max_packet_bytes);
        auto packet = decode(bytes, error);
        CHECK(packet.has_value());
        if (packet)
            decoded_parts.push_back(std::get<Snapshot>(std::move(packet->payload)));
    }
    ViewSnapshot view = sample_view();
    const auto previous = view;
    WorldId id = world_id(3);
    const auto old_id = id;
    SnapshotAssembler assembler;
    CHECK(!apply_snapshot(view, id, parts.front(), error));
    CHECK(same_view(view, previous) && id == old_id);
    for (std::size_t i = decoded_parts.size(); i-- > 0;) {
        auto result = assembler.accept(view, id, decoded_parts[i], error);
        if (i) {
            CHECK(result == AssemblyResult::Pending);
            CHECK(same_view(view, previous) && id == old_id);
            if (i == decoded_parts.size() - 1)
                CHECK(assembler.accept(view, id, decoded_parts[i], error) == AssemblyResult::Pending);
        } else
            CHECK(result == AssemblyResult::Applied);
    }
    CHECK(same_view(view, large) && id == world_id(9));
    const auto complete = view;
    auto stale = full;
    stale.revision = large.revision - 1;
    CHECK(!apply_snapshot(view, id, stale, error));
    CHECK(same_view(view, complete));
    // Authority chunk revisions are internal and never create a wire change.
    auto revision_only = large;
    ++revision_only.revision;
    for (auto &chunk : revision_only.chunks)
        ++chunk.revision;
    CHECK(make_snapshot(revision_only, id, &large).chunks.empty());
    // A full resync supersedes remaining parts of an obsolete delta, including
    // a different part count, before either partial view is exposed.
    auto obsolete = parts.front();
    obsolete.full = false;
    obsolete.baseline = large.revision;
    obsolete.revision = large.revision + 1;
    obsolete.part_count += 1;
    CHECK(assembler.accept(view, id, obsolete, error) == AssemblyResult::Pending);
    auto replacement = parts;
    for (auto &part : replacement)
        part.revision = obsolete.revision;
    for (std::size_t i = 0; i < replacement.size(); ++i)
        CHECK(assembler.accept(view, id, replacement[i], error) ==
              (i + 1 == replacement.size() ? AssemblyResult::Applied : AssemblyResult::Pending));
    view = complete;
    auto bad = parts.front();
    CHECK(assembler.accept(view, id, bad, error) == AssemblyResult::Pending);
    ++bad.resources.wood;
    CHECK(assembler.accept(view, id, bad, error) == AssemblyResult::Rejected);
    CHECK(same_view(view, complete));
    SnapshotAssembler profile_mismatch;
    CHECK(profile_mismatch.accept(view, id, parts.front(), error) == AssemblyResult::Pending);
    auto mismatched_profile = parts[1];
    mismatched_profile.terrain_generator = 5;
    CHECK(profile_mismatch.accept(view, id, mismatched_profile, error) == AssemblyResult::Rejected);
    CHECK(error == "Inconsistent snapshot parts" && same_view(view, complete));
    bad = parts.front();
    bad.part_count = 257;
    CHECK(assembler.accept(view, id, bad, error) == AssemblyResult::Rejected);
    bad = parts.front();
    CHECK(assembler.accept(view, id, bad, error) == AssemblyResult::Pending);
    ++bad.chunks.front().tiles.front().tile.amount;
    CHECK(assembler.accept(view, id, bad, error) == AssemblyResult::Rejected);
    CHECK(same_view(view, complete));
    auto clock = std::chrono::steady_clock::time_point{};
    SnapshotAssembler timed([&] { return clock; });
    CHECK(timed.accept(view, id, parts.front(), error) == AssemblyResult::Pending);
    clock += std::chrono::seconds(11);
    CHECK(timed.expired());
    CHECK(timed.accept(view, id, parts.back(), error) == AssemblyResult::Rejected);
    CHECK(same_view(view, complete));
    timed.reset();
    CHECK(!timed.expired());
    auto duplicate = parts;
    duplicate.back().chunks.push_back(duplicate.front().chunks.front());
    for (std::size_t i = 0; i < duplicate.size(); ++i) {
        const auto result = assembler.accept(view, id, duplicate[i], error);
        CHECK(result == (i + 1 == duplicate.size() ? AssemblyResult::Rejected : AssemblyResult::Pending));
    }
    CHECK(same_view(view, complete));
    // Individually bounded parts cannot accumulate unbounded decoded state.
    Snapshot bomb = parts.front();
    bomb.part_count = 256;
    bomb.chunks.assign(full.chunks.begin(), full.chunks.begin() + 16);
    bool rejected = false;
    for (std::uint32_t i = 0; i < 256 && !rejected; ++i) {
        bomb.part_index = i;
        rejected = assembler.accept(view, id, bomb, error) == AssemblyResult::Rejected;
    }
    CHECK(rejected && error == "Snapshot assembly memory limit");
    CHECK(same_view(view, complete));
    // Paired exits are authorized independently and endpoints cannot be ambiguous.
    auto private_portal = sample_view();
    private_portal.portals.push_back({{1, 1, WorldLayer::Surface}, {100, 100, WorldLayer::Mine}});
    CHECK(!apply_snapshot(view, id, make_snapshot(private_portal, world_id(1)), error));
    CHECK(same_view(view, complete));
    private_portal.portals.push_back({{1, 1, WorldLayer::Surface}, {101, 101, WorldLayer::Mine}});
    bool portal_rejected = false;
    try {
        (void)make_snapshot(private_portal, world_id(1));
    } catch (const std::runtime_error &) {
        portal_rejected = true;
    }
    CHECK(portal_rejected);
}
#include "protocol_control_tests.inc"
} // namespace

int main() {
    const auto older = mutated_wire_packet({1, make_snapshot(sample_view(), world_id(1))},
                                           [](auto &packet) { packet.protocol = protocol_version - 1; });
    std::string old_error;
    CHECK(!decode(older, old_error));
    CHECK(old_error == "Packet protocol mismatch");
    const auto bad_height = mutated_wire_packet({1, make_snapshot(sample_view(), world_id(1))},
        [](auto &packet) {
            packet.payload.AsSnapshot()->chunks.front()->tiles.front() =
                ribterra::wire::Tile(0, 32000, 0, 0, true, 0, 0);
        });
    CHECK(!decode(bad_height, old_error));
    CHECK(old_error == "Invalid elevation delta");
    every_payload_roundtrips();
    malformed_and_semantically_invalid_packets_fail();
    full_and_delta_are_transactional();
    active_production_keeps_public_storage_snapshot_valid();
    manual_queue_and_control_wire_are_strict();
    logistics_deltas_and_invalid_references_are_transactional();
    profession_deltas_outcomes_and_private_data_are_strict();
    terrain_generator_wire_contract();
    password_proof_binds_handshake_fields();
    multipart_chunks_are_bounded_and_transactional();
    if (failures) {
        std::cerr << failures << " protocol test(s) failed.\n";
        return 1;
    }
    std::cout << "Protocol roundtrip, validation, snapshot delta and password-proof tests passed.\n";
    return 0;
}

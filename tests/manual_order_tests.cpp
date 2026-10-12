#include "ribterra/world_state.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>

namespace {
using namespace ribterra;
int failures{};
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << __LINE__ << ": " #x "\n";                                                           \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)
bool live(const ManualOrderState &o) {
    return o.phase <= ManualOrderPhase::Active;
}
const ManualOrderState *order(const World &w, std::uint64_t id) {
    const auto &orders = w.manual_orders();
    auto it = std::find_if(orders.begin(), orders.end(), [=](const auto &o) { return o.order_id == id; });
    return it == orders.end() ? nullptr : &*it;
}
ManualOrderRequest move_to(int x, int y) {
    return {ManualOrderKind::Move, CellCoord{x, y}};
}
ManualOrderRequest gather_at(int x, int y, GatherCompletion mode = GatherCompletion::UntilExhausted) {
    return {ManualOrderKind::Gather, CellCoord{x, y}, mode};
}
void install_wood_fixture(World &world) {
    auto state = world.export_state();
    const CellCoord target{22, 19};
    auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                              [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    CHECK(chunk != state.chunks.end());
    if (chunk == state.chunks.end())
        return;
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    CHECK(world.import_state(std::move(state), error));
}
bool roundtrip(const World &w) {
    World copy(1, 0);
    std::string error;
    const bool ok = copy.import_state(w.export_state(), error);
    if (!ok)
        std::cerr << "roundtrip tick " << w.ticks() << ": " << error << '\n';
    CHECK(ok);
    if (ok)
        CHECK(copy.checksum() == w.checksum());
    return ok;
}
template <class Predicate> bool until(World &w, Predicate done, int limit = 2500, bool validate = false) {
    for (int i = 0; i < limit && !done(); ++i) {
        w.tick();
        CHECK(w.path_diagnostics().work_units_last_tick <= path_work_units_per_tick);
        CHECK(w.manual_diagnostics().visits_last_tick <= manual_visits_per_tick);
        CHECK(w.manual_diagnostics().path_admissions_last_tick <= manual_path_admissions_per_tick);
        if (validate && !roundtrip(w))
            return false;
    }
    return done();
}
void physical_queue_and_profession_priority() {
    World w(70421, 1);
    install_wood_fixture(w);
    ProfessionAssignment miner;
    miner.profession = Profession::Miner;
    miner.worksite = CellCoord{20, 19};
    CHECK(w.assign_profession({1}, miner).front().status == ResidentCommandStatus::Applied);
    const auto a = w.issue_manual_order({1}, move_to(23, 21), ManualQueueMode::Replace).front().order_id;
    const auto b =
        w.issue_manual_order({1}, gather_at(22, 19, GatherCompletion::OneDelivery), ManualQueueMode::Append)
            .front()
            .order_id;
    const auto c = w.issue_manual_order({1}, move_to(27, 25), ManualQueueMode::Append).front().order_id;
    CHECK(a && b > a && c > b);
    CHECK(roundtrip(w));
    CHECK(until(w, [&] { return order(w, c) && !live(*order(w, c)); }, 1200, true));
    CHECK(order(w, a)->completion == ManualOrderCompletion::Arrived);
    CHECK(order(w, b)->completion == ManualOrderCompletion::Delivered);
    CHECK(order(w, c)->completion == ManualOrderCompletion::Arrived);
    CHECK(w.residents().front().x == 27 && w.residents().front().y == 25);
    CHECK(w.residents().front().wood_xp == 1 && w.residents().front().mining_xp == 0);
    CHECK(w.inventory().stored.wood == 83 && w.inventory().carried.wood == 0);
    CHECK(w.take_path_results().empty());
    for (int tick = 0; tick < 500; ++tick) w.tick();
    CHECK(w.residents().front().mining_xp == 0);
}
Tile &tile(WorldState &s, CellCoord c) {
    auto it =
        std::find_if(s.chunks.begin(), s.chunks.end(), [&](const auto &q) { return q.key == chunk_key(c); });
    return it->tiles[local_index(c.x, c.y)];
}
void partial_shared_group_and_isolated_actor() {
    World w(70421, 3);
    auto state = w.export_state();
    state.residents[1].x = 45;
    state.residents[1].y = 45;
    state.residents[1].target_x = 45;
    state.residents[1].target_y = 45;
    for (auto c : {CellCoord{44, 45}, CellCoord{46, 45}, CellCoord{45, 44}, CellCoord{45, 46}})
        tile(state, c) = {Terrain::Water, 0, true};
    tile(state, {45, 45}) = {Terrain::Grass, 0, true};
    state.residents[2].health = 0;
    std::string error;
    CHECK(w.import_state(state, error));
    const auto result = w.issue_manual_order({1, 2, 3, 999}, move_to(27, 25), ManualQueueMode::Replace);
    CHECK(result.size() == 4);
    CHECK(result[0].status == ResidentCommandStatus::Applied &&
          result[1].status == ResidentCommandStatus::Applied);
    CHECK(result[2].status == ResidentCommandStatus::Rejected &&
          result[2].failure == PathFailure::ResidentUnavailable);
    CHECK(result[3].status == ResidentCommandStatus::Rejected && !result[3].order_id);
    w.tick();
    CHECK(w.export_state().path_batches.size() == 1);
    CHECK(w.export_state().path_batches.front().resident_ids.size() == 2);
    CHECK(!w.export_state().path_batches.front().legacy_atomic);
    CHECK(until(
        w, [&] { return !live(*order(w, result[0].order_id)) && !live(*order(w, result[1].order_id)); }, 1200,
        true));
    CHECK(order(w, result[0].order_id)->completion == ManualOrderCompletion::Arrived);
    CHECK(order(w, result[1].order_id)->failure == PathFailure::Unreachable);
    CHECK(w.residents()[1].x == 45 && w.residents()[1].y == 45);
    CHECK(w.take_path_results().empty());
}
void actor_cancellation_does_not_cancel_shared_peers() {
    // This scheduler fixture intentionally uses the historical topology whose
    // reconstruction/stage boundary crosses a per-tick planning budget.
    World w(70421, 1500, 7);
    std::vector<int> ids(1500);
    std::iota(ids.begin(), ids.end(), 1);
    const auto result = w.issue_manual_order(ids, move_to(90, 90), ManualQueueMode::Replace);
    CHECK(result.size() == 1500);
    w.tick();
    w.tick();
    CHECK(w.export_state().path_batches.size() == 1);
    CHECK(w.export_state().path_batches.front().resident_ids.size() == 1500);
    CHECK(w.cancel_manual_order(result.front().order_id).front().status == ResidentCommandStatus::Applied);
    CHECK(order(w, result.front().order_id)->completion == ManualOrderCompletion::Cancelled);
    CHECK(roundtrip(w));
    bool observed_partial_commit = false;
    for (int i = 0; i < 200 && w.path_diagnostics().queued_batches; ++i) {
        w.tick();
        const auto active = std::count_if(w.residents().begin(), w.residents().end(),
                                          [](const auto &r) { return r.job == Job::Move; });
        if (active > 0 && active < 1499)
            observed_partial_commit = true;
        CHECK(w.path_diagnostics().work_units_last_tick <= path_work_units_per_tick);
        CHECK(roundtrip(w));
    }
    CHECK(observed_partial_commit);
    CHECK(w.path_diagnostics().queued_batches == 0);
    CHECK(w.residents().front().job == Job::Idle);
    CHECK(std::count_if(w.residents().begin(), w.residents().end(),
                        [](const auto &r) { return r.job == Job::Move; }) == 1499);
    CHECK(w.take_path_results().empty());
}
void replacement_stop_release_preserve_cargo() {
    for (int action = 0; action < 4; ++action) {
        World w(70421, 1);
        install_wood_fixture(w);
        const auto old =
            w.issue_manual_order({1}, gather_at(22, 19), ManualQueueMode::Replace).front().order_id;
        CHECK(until(w, [&] { return w.inventory().carried.wood == 3; }));
        const auto total = w.inventory().total.wood;
        std::uint64_t replacement = 0;
        if (action == 0) {
            const auto result = w.issue_manual_order({1}, move_to(27, 25), ManualQueueMode::Replace).front();
            CHECK(result.status == ResidentCommandStatus::Deferred);
            replacement = result.order_id;
            CHECK(order(w, replacement)->phase == ManualOrderPhase::WaitingBoundary);
        } else {
            const auto result = action == 1   ? w.stop({1})
                                : action == 2 ? w.release_manual_control({1})
                                              : w.cancel_manual_order(old);
            CHECK(result.front().status == ResidentCommandStatus::Deferred);
            CHECK(w.residents().front().profession.assignment.automation_enabled == (action != 1));
        }
        CHECK(order(w, old)->phase == ManualOrderPhase::Cancelled);
        CHECK(order(w, old)->completion == (action == 0   ? ManualOrderCompletion::Replaced
                                            : action == 1 ? ManualOrderCompletion::Stopped
                                            : action == 2 ? ManualOrderCompletion::Released
                                                          : ManualOrderCompletion::Cancelled));
        CHECK(roundtrip(w));
        CHECK(until(w, [&] { return w.inventory().carried.wood == 0; }, 1000, true));
        CHECK(w.inventory().stored.wood == total && w.residents().front().wood_xp == 1);
        if (replacement)
            CHECK(until(w, [&] { return !live(*order(w, replacement)); }, 500, true));
        for (int i = 0; i < 30; ++i)
            w.tick();
        CHECK(w.residents().front().wood_xp == 1);
        CHECK(roundtrip(w));
    }
}
void capacity_and_transactional_validation() {
    World w(70421, 1);
    for (int i = 0; i < 8; ++i)
        CHECK(w.issue_manual_order({1}, move_to(26, 25), ManualQueueMode::Append).front().status ==
              ResidentCommandStatus::Applied);
    const auto before = w.checksum();
    // The Camp cell is blocked and the cleared starting area has no resource.
    CHECK(w.issue_manual_order({1}, move_to(24, 24), ManualQueueMode::Replace).front().failure ==
          PathFailure::TargetInvalid);
    CHECK(w.checksum() == before);
    CHECK(w.issue_manual_order({1}, gather_at(26, 25), ManualQueueMode::Replace).front().failure ==
          PathFailure::TargetInvalid);
    CHECK(w.checksum() == before);
    CHECK(w.issue_manual_order({1}, move_to(26, 25), ManualQueueMode::Append).front().failure ==
          PathFailure::LimitExceeded);
    CHECK(w.checksum() == before);
    auto invalid = w.export_state();
    invalid.manual_orders[1].order_id = invalid.manual_orders[0].order_id;
    std::string error;
    CHECK(!w.import_state(invalid, error));
    CHECK(w.checksum() == before);
    invalid = w.export_state();
    invalid.manual_orders.clear();
    CHECK(!w.import_state(invalid, error));
    CHECK(w.checksum() == before);
    invalid = w.export_state();
    invalid.next_manual_order_id = std::numeric_limits<std::uint64_t>::max();
    CHECK(w.import_state(invalid, error));
    const auto full = w.checksum();
    CHECK(w.issue_manual_order({1}, move_to(26, 25), ManualQueueMode::Replace).front().failure ==
          PathFailure::LimitExceeded);
    CHECK(w.checksum() == full);
    const auto cancelled_tail = w.manual_orders()[3].order_id;
    CHECK(w.cancel_manual_order(cancelled_tail).front().status == ResidentCommandStatus::Applied);
    CHECK(order(w, cancelled_tail)->completion == ManualOrderCompletion::Cancelled);
    CHECK(w.manual_diagnostics().active_orders == 7);
    CHECK(roundtrip(w));
}
void cancel_pending_gather_continuation_preserves_delivery_and_tail() {
    for (bool append_tail : {false, true}) {
        World w(70421, 1);
        install_wood_fixture(w);
        const auto head =
            w.issue_manual_order({1}, gather_at(22, 19), ManualQueueMode::Replace).front().order_id;
        const auto tail =
            append_tail ? w.issue_manual_order({1}, move_to(27, 25), ManualQueueMode::Append).front().order_id
                        : 0;
        CHECK(until(w, [&] {
            return std::any_of(w.work_intents().begin(), w.work_intents().end(), [](const auto &intent) {
                return intent.resident_id == 1 && intent.kind == WorkIntentKind::GatherContinuation &&
                       intent.phase == WorkIntentPhase::PathPending;
            });
        }));
        CHECK(order(w, head)->executor == ManualExecutorKind::Work);
        CHECK(w.inventory().carried.wood == 0 && w.residents().front().wood_xp == 1);
        const auto stored = w.inventory().stored.wood;
        CHECK(stored == 83);
        CHECK(roundtrip(w));
        const auto result = w.cancel_manual_order(head).front();
        CHECK(result.status == ResidentCommandStatus::Applied);
        CHECK(order(w, head)->phase == ManualOrderPhase::Cancelled &&
              order(w, head)->completion == ManualOrderCompletion::Cancelled);
        CHECK(w.path_diagnostics().queued_batches == 0);
        CHECK(std::none_of(w.work_intents().begin(), w.work_intents().end(), [](const auto &intent) {
            return intent.resident_id == 1 &&
                   (intent.phase == WorkIntentPhase::Waiting || intent.phase == WorkIntentPhase::PathPending);
        }));
        if (tail) {
            CHECK(order(w, tail)->phase == ManualOrderPhase::Queued);
            CHECK(order(w, tail)->executor == ManualExecutorKind::None);
        } else
            CHECK(!w.residents().front().profession.deferred);
        CHECK(roundtrip(w));
        if (tail) {
            CHECK(until(w, [&] { return !live(*order(w, tail)); }, 500, true));
            CHECK(order(w, tail)->completion == ManualOrderCompletion::Arrived);
        }
        for (int tick = 0; tick < 150; ++tick)
            w.tick();
        CHECK(w.residents().front().job == Job::Idle && w.residents().front().wood_xp == 1);
        CHECK(w.inventory().stored.wood == stored && w.inventory().carried.wood == 0);
        CHECK(w.take_path_results().empty());
        CHECK(roundtrip(w));
    }
}
void group_capacity_replacement_and_counter_exhaustion() {
    World w(70421, 1500);
    std::vector<int> ids(1500);
    std::iota(ids.begin(), ids.end(), 1);
    std::size_t accepted = 0;
    for (int step = 0; step < 6; ++step) {
        const auto results = w.issue_manual_order(ids, move_to(27, 25), ManualQueueMode::Append);
        CHECK(results.size() == ids.size());
        for (const auto &result : results) {
            if (accepted < max_manual_orders) {
                CHECK(result.status == ResidentCommandStatus::Applied);
                CHECK(result.order_id == ++accepted);
            } else
                CHECK(result.status == ResidentCommandStatus::Rejected &&
                      result.failure == PathFailure::LimitExceeded && result.order_id == 0);
        }
    }
    CHECK(accepted == max_manual_orders);
    CHECK(w.manual_diagnostics().active_orders == max_manual_orders);
    const auto full = w.checksum();
    const auto rejected = w.issue_manual_order(ids, move_to(27, 25), ManualQueueMode::Append);
    CHECK(std::all_of(rejected.begin(), rejected.end(), [](const auto &r) {
        return r.status == ResidentCommandStatus::Rejected && r.failure == PathFailure::LimitExceeded &&
               r.order_id == 0;
    }));
    CHECK(w.checksum() == full);
    const auto invalid = w.issue_manual_order(ids, move_to(24, 24), ManualQueueMode::Replace);
    CHECK(std::all_of(invalid.begin(), invalid.end(), [](const auto &r) {
        return r.status == ResidentCommandStatus::Rejected && r.failure == PathFailure::TargetInvalid &&
               r.order_id == 0;
    }));
    CHECK(w.checksum() == full);
    const auto replaced = w.issue_manual_order(ids, move_to(26, 25), ManualQueueMode::Replace);
    for (std::size_t i = 0; i < replaced.size(); ++i) {
        CHECK(replaced[i].status == ResidentCommandStatus::Applied);
        CHECK(replaced[i].order_id == max_manual_orders + i + 1);
    }
    std::vector<int> live_counts(1501);
    for (const auto &o : w.manual_orders())
        if (live(o))
            ++live_counts[static_cast<std::size_t>(o.resident_id)];
    CHECK(std::all_of(live_counts.begin() + 1, live_counts.end(), [](int count) { return count == 1; }));
    CHECK(w.manual_diagnostics().terminal_orders <= manual_terminal_history);
    CHECK(roundtrip(w));

    auto state = w.export_state();
    state.next_manual_order_id = std::numeric_limits<std::uint64_t>::max() - 2;
    std::string error;
    CHECK(w.import_state(state, error));
    const auto limited = w.issue_manual_order({3, 1, 2, 1}, move_to(27, 25), ManualQueueMode::Replace);
    CHECK(limited.size() == 3);
    CHECK(limited[0].resident_id == 1 && limited[0].status == ResidentCommandStatus::Applied &&
          limited[0].order_id == std::numeric_limits<std::uint64_t>::max() - 2);
    CHECK(limited[1].resident_id == 2 && limited[1].status == ResidentCommandStatus::Applied &&
          limited[1].order_id == std::numeric_limits<std::uint64_t>::max() - 1);
    CHECK(limited[2].resident_id == 3 && limited[2].status == ResidentCommandStatus::Rejected &&
          limited[2].failure == PathFailure::LimitExceeded && limited[2].order_id == 0);
    CHECK(order(w, replaced[2].order_id) && live(*order(w, replaced[2].order_id)));
    CHECK(roundtrip(w));
}
void exhausted_target_and_midstep_resume() {
    World w(70421, 1);
    install_wood_fixture(w);
    const auto id = w.issue_manual_order({1}, gather_at(22, 19), ManualQueueMode::Replace).front().order_id;
    CHECK(until(w, [&] { return w.inventory().carried.wood == 3; }));
    World copy(2, 0);
    std::string error;
    CHECK(copy.import_state(w.export_state(), error));
    for (int i = 0; i < 150; ++i) {
        w.tick();
        copy.tick();
        CHECK(w.checksum() == copy.checksum());
    }
    CHECK(order(w, id) != nullptr);
    auto state = w.export_state();
    tile(state, {22, 19}) = {Terrain::Grass, 0, true};
    ++state.topology_revision;
    CHECK(w.import_state(state, error));
    CHECK(until(w, [&] { return !live(*order(w, id)); }, 1000, true));
    CHECK(order(w, id)->completion == ManualOrderCompletion::Exhausted);
    CHECK(order(w, id)->failure == PathFailure::None);
}
void forged_phase_and_executor_bindings_are_transactional() {
    World w(70421, 1);
    CHECK(w.move({1}, 90, 90).accepted);
    const auto original = w.export_state();
    const auto checksum = w.checksum();
    std::string error;
    auto reject = [&](WorldState state) {
        CHECK(!w.import_state(std::move(state), error));
        CHECK(w.checksum() == checksum);
    };
    auto invalid = original;
    invalid.manual_orders.front().request.kind = ManualOrderKind::Legacy;
    invalid.manual_orders.front().request.target.reset();
    invalid.manual_orders.front().phase = ManualOrderPhase::Queued;
    invalid.manual_orders.front().executor = ManualExecutorKind::None;
    invalid.manual_orders.front().executor_id = 0;
    reject(invalid);
    invalid.manual_orders.front().phase = ManualOrderPhase::WaitingBoundary;
    invalid.manual_orders.front().executor = ManualExecutorKind::Path;
    invalid.manual_orders.front().executor_id = original.path_batches.front().path_batch_id;
    reject(invalid);
    invalid = original;
    invalid.manual_orders.front().phase = ManualOrderPhase::Queued;
    invalid.manual_orders.front().executor = ManualExecutorKind::None;
    invalid.manual_orders.front().executor_id = 0;
    reject(invalid);
    invalid = original;
    invalid.manual_orders.front().phase = ManualOrderPhase::Active;
    reject(invalid);

    World gathering(70421, 1);
    install_wood_fixture(gathering);
    gathering.issue_manual_order({1}, gather_at(22, 19), ManualQueueMode::Replace);
    CHECK(until(gathering, [&] { return gathering.residents().front().job == Job::Woodcut; }));
    invalid = gathering.export_state();
    invalid.manual_orders.front().request.kind = ManualOrderKind::Move;
    CHECK(!gathering.import_state(invalid, error));
    CHECK(until(gathering, [&] { return gathering.inventory().carried.wood == 3; }));
    invalid = gathering.export_state();
    invalid.manual_orders.front().request.kind = ManualOrderKind::Move;
    CHECK(!gathering.import_state(invalid, error));
    CHECK(until(gathering,
                [&] { return gathering.manual_orders().front().executor == ManualExecutorKind::Work; }));
    invalid = gathering.export_state();
    invalid.manual_orders.front().request.kind = ManualOrderKind::Move;
    CHECK(!gathering.import_state(invalid, error));
    CHECK(roundtrip(gathering));

    World completed(70421, 1);
    const auto id =
        completed.issue_manual_order({1}, move_to(23, 21), ManualQueueMode::Replace).front().order_id;
    CHECK(until(completed, [&] { return !live(*order(completed, id)); }));
    invalid = completed.export_state();
    invalid.manual_orders.front().completion = ManualOrderCompletion::Released;
    CHECK(!completed.import_state(invalid, error));
    invalid = completed.export_state();
    invalid.manual_orders.front().phase = ManualOrderPhase::Cancelled;
    CHECK(!completed.import_state(invalid, error));
    invalid = completed.export_state();
    invalid.manual_orders.front().completion = ManualOrderCompletion::Cancelled;
    CHECK(!completed.import_state(invalid, error));
    CHECK(roundtrip(completed));
}
} // namespace
int main() {
    physical_queue_and_profession_priority();
    partial_shared_group_and_isolated_actor();
    actor_cancellation_does_not_cancel_shared_peers();
    replacement_stop_release_preserve_cargo();
    capacity_and_transactional_validation();
    cancel_pending_gather_continuation_preserves_delivery_and_tail();
    group_capacity_replacement_and_counter_exhaustion();
    exhausted_target_and_midstep_resume();
    forged_phase_and_executor_bindings_are_transactional();
    return failures ? 1 : 0;
}

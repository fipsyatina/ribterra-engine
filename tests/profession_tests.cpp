#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

namespace {
using namespace ribterra;
int failures = 0;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << __LINE__ << ": " #expr "\n";                                                        \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)
Tile &tile(WorldState &s, CellCoord cell) {
    auto it = std::find_if(s.chunks.begin(), s.chunks.end(),
                           [&](const auto &c) { return c.key == chunk_key(cell); });
    return it->tiles[local_index(cell.x, cell.y)];
}
void import(World &w, WorldState state) {
    std::string error;
    const bool accepted = w.import_state(std::move(state), error);
    if (!accepted)
        std::cerr << "import: " << error << '\n';
    CHECK(accepted);
}
World fixture(int population = 1) {
    World w(70421, population);
    auto s = w.export_state();
    for (int y = 19; y <= 30; ++y)
        for (int x = 18; x <= 33; ++x)
            tile(s, {x, y}) = {Terrain::Grass, 0, true};
    tile(s, {21, 24}) = {Terrain::Forest, 3, true};
    tile(s, {21, 26}) = {Terrain::Rock, 3, true};
    for (auto &r : s.residents) {
        r.x = 22;
        r.y = 24;
        r.target_x = 22;
        r.target_y = 24;
    }
    import(w, std::move(s));
    return w;
}
ProfessionAssignment assigned(Profession profession, CellCoord site) {
    ProfessionAssignment p;
    p.profession = profession;
    p.worksite = site;
    return p;
}
template <class F> bool until(World &w, F condition, int maximum = 2000) {
    for (int i = 0; i < maximum; ++i) {
        if (condition())
            return true;
        w.tick();
        w.take_path_results();
    }
    return condition();
}
void switching_preserves_independent_skills_and_cargo() {
    auto w = fixture();
    const auto result = w.assign_profession({1}, assigned(Profession::Woodcutter, {21, 24}));
    CHECK(result.size() == 1 && result.front().status == ResidentCommandStatus::Applied);
    CHECK(w.residents().front().wood_xp == 0);
    CHECK(w.gather({1}, 21, 24).accepted);
    CHECK(until(w, [&] { return w.inventory().carried.wood > 0; }));
    const auto wood = w.residents().front().wood_xp;
    CHECK(wood == 1);
    const auto reassigned = w.assign_profession({1}, assigned(Profession::Miner, {21, 26}));
    CHECK(reassigned.front().status == ResidentCommandStatus::Deferred);
    CHECK(w.residents().front().profession.deferred);
    CHECK(!w.move({1}, 26, 24).accepted);
    World copy(1, 0);
    import(copy, w.export_state());
    for (int i = 0; i < 240; ++i) {
        w.tick();
        copy.tick();
        w.take_path_results();
        copy.take_path_results();
        CHECK(copy.checksum() == w.checksum());
    }
    CHECK(w.inventory().stored.wood == 83);
    CHECK(w.residents().front().wood_xp == wood);
    CHECK(w.residents().front().mining_xp == 0);
    CHECK(w.gather({1}, 21, 26).accepted);
    CHECK(until(w, [&] { return w.inventory().stored.stone > 40; }));
    const auto mining = w.residents().front().mining_xp;
    CHECK(w.assign_profession({1}, assigned(Profession::Woodcutter, {21, 24})).front().status !=
          ResidentCommandStatus::Rejected);
    CHECK(until(w, [&] { return w.inventory().carried.stone == 0; }));
    CHECK(w.gather({1}, 21, 24).accepted);
    CHECK(until(w, [&] { return w.residents().front().wood_xp > wood; }));
    CHECK(w.residents().front().mining_xp == mining);
}
void stop_finishes_cargo_without_old_continuation() {
    auto w = fixture();
    w.assign_profession({1}, assigned(Profession::Woodcutter, {21, 24}));
    CHECK(w.gather({1}, 21, 24).accepted);
    CHECK(until(w, [&] { return w.inventory().carried.wood > 0; }));
    CHECK(w.stop({1}).front().status == ResidentCommandStatus::Deferred);
    CHECK(until(w, [&] { return w.inventory().carried.wood == 0; }));
    for (int i = 0; i < 120; ++i)
        w.tick();
    CHECK(w.residents().front().job == Job::Idle);
    CHECK(w.residents().front().wood_xp == 1);
    CHECK(w.residents().front().profession.wait_reason == ProfessionWaitReason::Stopped);
    CHECK(!w.residents().front().profession.assignment.automation_enabled);
    CHECK(w.inventory().stored.wood == 83 && w.work_diagnostics().active_intents == 0);
}
void move_returns_to_assignment_and_partial_stop_preserves_other_actor() {
    auto w = fixture();
    w.assign_profession({1}, assigned(Profession::Woodcutter, {21, 24}));
    CHECK(w.move({1}, 27, 24).accepted);
    CHECK(w.residents().front().profession.manual_override);
    bool arrived = false;
    CHECK(until(w, [&] {
        const auto &r = w.residents().front();
        arrived = arrived || (r.x == 27 && r.y == 24);
        return arrived && !r.profession.manual_override;
    }));
    CHECK(arrived && !w.residents().front().profession.manual_override);
    auto group = fixture(2);
    const auto move = group.move({1, 2}, 30, 24);
    CHECK(move.accepted);
    const auto outcomes = group.stop({999, 1});
    CHECK(outcomes.size() == 2);
    CHECK(outcomes[0].resident_id == 1 && outcomes[0].status == ResidentCommandStatus::Deferred);
    CHECK(outcomes[1].resident_id == 999 && outcomes[1].status == ResidentCommandStatus::Rejected);
    CHECK(group.residents().front().profession.wait_reason == ProfessionWaitReason::PendingOrder);
    World copy(1, 0);
    import(copy, group.export_state());
    for (int i = 0; i < 100; ++i) {
        group.tick();
        copy.tick();
        CHECK(group.checksum() == copy.checksum());
    }
    CHECK(group.residents()[0].x == 22 && group.residents()[0].y == 24);
    CHECK(group.residents()[1].x == 30 && group.residents()[1].y == 24);
    CHECK(group.residents()[0].profession.wait_reason == ProfessionWaitReason::Stopped);
    const auto replies = group.take_path_results();
    CHECK(replies.size() == 1 && replies.front().path_batch_id == move.path_batch_id &&
          replies.front().accepted);
}
void stopped_last_delivery_does_not_become_builder() {
    auto w = fixture(2);
    CHECK(w.build(BuildingKind::House, 26, 26).accepted);
    CHECK(until(w, [&] {
        if (w.construction_orders().front().phase != ConstructionPhase::Delivery)
            return false;
        int active = 0;
        bool carrying = false;
        for (const auto &t : w.logistics_tasks())
            if (t.phase != LogisticsPhase::Complete && t.phase != LogisticsPhase::Failed) {
                ++active;
                carrying = t.phase == LogisticsPhase::ToDropoff;
            }
        return active == 1 && carrying;
    }));
    w.stop({1, 2});
    CHECK(until(w, [&] { return w.construction_orders().front().phase == ConstructionPhase::Work; }));
    for (int i = 0; i < 50; ++i)
        w.tick();
    CHECK(w.buildings().back().progress == 0);
    CHECK(std::all_of(w.residents().begin(), w.residents().end(),
                      [](const auto &r) { return r.job == Job::Idle; }));
    const auto inventory = w.resources();
    w.assign_profession({2}, assigned(Profession::Builder, {26, 26}));
    CHECK(until(w, [&] { return w.buildings().back().progress == 1; }));
    CHECK(w.resources().wood == inventory.wood && w.resources().stone == inventory.stone);
    CHECK(w.residents()[1].additional_xp[0] == 2);
    CHECK(
        w.residents()[1].skill_practice[static_cast<std::size_t>(Skill::Construction)].experience_remainder ==
        2000);
}
void authoritative_modifiers_and_fractional_build_credit() {
    auto w = fixture();
    auto s = w.export_state();
    auto &r = s.residents.front();
    r.job = Job::Woodcut;
    r.order_phase = OrderPhase::Active;
    s.routes.front().work = CellCoord{21, 24};
    r.target_x = 21;
    r.wood_xp = 2880;
    r.abilities_bp[0] = 1500;
    import(w, std::move(s));
    const auto breakdown = *w.skill_breakdown(1, Skill::Woodcutting);
    CHECK(breakdown.speed_bp == 11500 && breakdown.capped);
    w.tick();
    CHECK(std::abs(w.residents().front().progress - skill_gather_base_progress() * 1.15f) < 0.000001f);
    auto builder = fixture(2);
    s = builder.export_state();
    s.buildings.push_back({2, BuildingKind::House, 26, 26, .995f, WorldLayer::Surface, 1});
    s.next_building_id = 3;
    ConstructionOrderState site;
    site.building_id = 2;
    site.kind = BuildingKind::House;
    site.site = {26, 26};
    site.required = site.delivered = {30, 15, 0};
    site.phase = ConstructionPhase::Work;
    s.construction_orders.push_back(site);
    for (std::size_t i = 0; i < 2; ++i) {
        auto &actor = s.residents[i];
        actor.x = 25;
        actor.y = 26;
        actor.target_x = 26;
        actor.target_y = 26;
        actor.job = Job::Build;
        actor.order_phase = OrderPhase::Active;
        s.routes[i].work = site.site;
    }
    import(builder, std::move(s));
    builder.tick();
    CHECK(builder.buildings().back().progress == 1);
    unsigned credit = 0;
    for (const auto &actor : builder.residents())
        credit += actor.skill_practice[3].experience_remainder;
    CHECK(credit == 50);
}
void no_candidate_disabled_jobs_and_fairness() {
    auto w = fixture();
    w.assign_profession({1}, assigned(Profession::Farmer, {21, 24}));
    w.tick();
    CHECK(w.residents().front().profession.wait_reason == ProfessionWaitReason::WorksiteUnavailable);
    auto disabled = assigned(Profession::Woodcutter, {21, 24});
    disabled.priorities[0] = 0;
    w.assign_profession({1}, disabled);
    for (int i = 0; i < 50; ++i)
        w.tick();
    CHECK(w.residents().front().wood_xp == 0 && w.residents().front().job == Job::Idle);
    auto crowd = fixture(256);
    std::vector<int> ids(256);
    std::iota(ids.begin(), ids.end(), 1);
    crowd.assign_profession(ids, assigned(Profession::Miner, {21, 26}));
    World copy(1, 0);
    import(copy, crowd.export_state());
    for (std::uint64_t i = 1; i <= 8; ++i) {
        crowd.tick();
        copy.tick();
        CHECK(crowd.checksum() == copy.checksum());
        CHECK(crowd.work_diagnostics().outcomes.created <= i * 32);
    }
    CHECK(crowd.work_diagnostics().outcomes.created == 0);
}
void new_state_validation_is_transactional() {
    auto w = fixture();
    const auto state = w.export_state();
    const auto checksum = w.checksum();
    auto reject = [&](auto mutate) {
        auto bad = state;
        mutate(bad);
        std::string error;
        CHECK(!w.import_state(std::move(bad), error));
        CHECK(!error.empty() && w.checksum() == checksum);
    };
    reject([](auto &s) { s.residents.front().additional_xp[0] = -1; });
    reject([](auto &s) { s.residents.front().skill_practice[0].last_practice_tick = s.ticks + 1; });
    reject([](auto &s) { s.residents.front().abilities_bp[2] = 1501; });
    reject([](auto &s) { s.residents.front().profession.assignment.priorities[0] = 6; });
    reject([](auto &s) { s.residents.front().profession.deferred = true; });
    reject([](auto &s) { s.residents.front().profession.manual_override = true; });
    reject([](auto &s) { s.profession_cursor_id = 0; });
}
void deferred_failure_and_manual_capacity_boundaries_roundtrip() {
    for (bool death : {false, true}) {
        auto w = fixture();
        w.assign_profession({1}, assigned(Profession::Woodcutter, {21, 24}));
        CHECK(w.gather({1}, 21, 24).accepted);
        CHECK(until(w, [&] { return w.inventory().carried.wood > 0; }));
        w.assign_profession({1}, assigned(Profession::Miner, {21, 26}));
        auto state = w.export_state();
        const auto delivery_id = state.logistics_tasks.back().task_id;
        if (death)
            state.residents.front().health = 0;
        else {
            auto &storage = state.storages.front();
            // The existing inventory remains valid; only the in-flight output
            // no longer fits when it reaches this destination.
            storage.capacity = 0;
            for (const auto &stack : state.item_stacks)
                if (stack.location.kind == ItemLocationKind::Storage &&
                    stack.location.entity_id == storage.building_id)
                    storage.capacity += stack.quantity;
        }
        import(w, std::move(state));
        World resumed(1, 0);
        import(resumed, w.export_state());
        for (int tick = 0; tick < 120; ++tick) {
            w.tick();
            resumed.tick();
            CHECK(w.checksum() == resumed.checksum());
            World roundtrip(1, 0);
            import(roundtrip, w.export_state());
            CHECK(roundtrip.checksum() == w.checksum());
        }
        CHECK(!w.residents().front().profession.deferred);
        CHECK(w.inventory().total.wood == 83);
        const auto delivery = std::find_if(w.logistics_tasks().begin(), w.logistics_tasks().end(),
                                           [&](const auto &task) { return task.task_id == delivery_id; });
        CHECK(delivery != w.logistics_tasks().end());
        if (delivery != w.logistics_tasks().end()) {
            CHECK(delivery->phase == LogisticsPhase::Failed);
            CHECK(delivery->failure ==
                  (death ? LogisticsFailure::ResidentUnavailable : LogisticsFailure::StorageFull));
        }
    }
    auto manual = fixture();
    CHECK(manual.gather({1}, 21, 24).accepted);
    CHECK(until(manual, [&] { return manual.inventory().carried.wood > 0; }));
    auto state = manual.export_state();
    state.next_work_intent_id = std::numeric_limits<std::uint64_t>::max();
    import(manual, std::move(state));
    CHECK(until(manual, [&] { return manual.inventory().carried.wood == 0; }));
    CHECK(!manual.residents().front().profession.manual_override);
    CHECK(manual.logistics_tasks().back().phase == LogisticsPhase::Complete);
    CHECK(manual.residents().front().path_failure == PathFailure::LimitExceeded);
    World copy(1, 0);
    import(copy, manual.export_state());
    CHECK(copy.checksum() == manual.checksum());
}
} // namespace
int main() {
    switching_preserves_independent_skills_and_cargo();
    stop_finishes_cargo_without_old_continuation();
    move_returns_to_assignment_and_partial_stop_preserves_other_actor();
    stopped_last_delivery_does_not_become_builder();
    authoritative_modifiers_and_fractional_build_credit();
    no_candidate_disabled_jobs_and_fairness();
    new_state_validation_is_transactional();
    deferred_failure_and_manual_capacity_boundaries_roundtrip();
    return failures ? 1 : 0;
}

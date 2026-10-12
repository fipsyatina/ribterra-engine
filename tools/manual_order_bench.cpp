// Controlled authority benchmark, not a genuine save-migration fixture.
// Flat work area, co-located actors, enlarged food/storage and one shared Wood seam.
// At 6000 actors the second command deliberately exceeds the global 8192-order cap.
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
using namespace ribterra;
using Clock = std::chrono::steady_clock;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
World fixture(int population) {
    World source(70421, std::min(population, 1500));
    auto s = source.export_state();
    const auto residents = s.residents;
    const auto routes = s.routes;
    const int nations = population == 6000 ? 4 : 1;
    for (int nation = 2; nation <= nations; ++nation) {
        for (auto r : residents) {
            r.id += (nation - 1) * 1500;
            r.nation = static_cast<NationId>(nation);
            s.residents.push_back(r);
        }
        for (auto route : routes) {
            route.resident_id += (nation - 1) * 1500;
            s.routes.push_back(route);
        }
        const int x = nation == 2 ? 24 : 26;
        const int y = nation == 3 ? 24 : 26;
        s.buildings.push_back(
            {nation, BuildingKind::Camp, x, y, 1, WorldLayer::Surface, static_cast<NationId>(nation)});
        s.storages.push_back({nation, 1000000, 7, 0});
    }
    s.next_building_id = nations + 1;
    for (auto &chunk : s.chunks)
        if (chunk.key.layer == WorldLayer::Surface)
            for (int y = 0; y < chunk_size; ++y)
                for (int x = 0; x < chunk_size; ++x) {
                    const int wx = chunk.key.cx * chunk_size + x, wy = chunk.key.cy * chunk_size + y;
                    if (wx >= 18 && wx <= 40 && wy >= 18 && wy <= 32)
                        chunk.tiles[static_cast<std::size_t>(y * chunk_size + x)] = {Terrain::Grass, 0, true};
                    if (wx == 32 && wy == 24)
                        chunk.tiles[static_cast<std::size_t>(y * chunk_size + x)] = {Terrain::Forest, 6000,
                                                                                     true};
                }
    for (auto &r : s.residents) {
        r.x = 21;
        r.y = 21;
        r.target_x = 21;
        r.target_y = 21;
    }
    for (auto &store : s.storages)
        store.capacity = 1000000;
    s.item_stacks.clear();
    s.next_item_stack_id = 1;
    for (int nation = 1; nation <= nations; ++nation)
        for (const auto item : {ItemKind::Wood, ItemKind::Stone, ItemKind::Food})
            s.item_stacks.push_back({s.next_item_stack_id++,
                                     item,
                                     item == ItemKind::Food ? 100000U : 80U,
                                     static_cast<NationId>(nation),
                                     {ItemLocationKind::Storage, {}, nation}});
    s.resources = {80, 80, 100000};
    World result(1, 0);
    std::string error;
    if (!result.import_state(std::move(s), error))
        throw std::runtime_error(error);
    return result;
}
double percentile(const std::vector<double> &sorted, double fraction) {
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
    return sorted[std::min(sorted.size() - 1, std::max<std::size_t>(1, rank) - 1)];
}
void run(int population) {
    auto w = fixture(population);
    const int nations = population == 6000 ? 4 : 1;
    std::set<int> gather_actors, moved;
    std::size_t accepted = 0, rejected = 0;
    double command_ms = 0;
    const auto issue = [&](ManualOrderRequest request, ManualQueueMode mode, bool gather) {
        for (int n = 0; n < nations; ++n) {
            std::vector<int> ids(static_cast<std::size_t>(population / nations));
            std::iota(ids.begin(), ids.end(), n * 1500 + 1);
            const auto start = Clock::now();
            const auto outcomes = w.issue_manual_order(ids, request, mode);
            command_ms += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            for (const auto &o : outcomes) {
                if (o.status == ResidentCommandStatus::Rejected) {
                    require(gather && o.failure == PathFailure::LimitExceeded,
                            "Unexpected command rejection");
                    ++rejected;
                } else {
                    require(o.order_id != 0, "Accepted order has no stable ID");
                    ++accepted;
                    if (gather)
                        gather_actors.insert(o.resident_id);
                }
            }
        }
    };
    issue({ManualOrderKind::Move, CellCoord{30, 24}}, ManualQueueMode::Replace, false);
    issue({ManualOrderKind::Gather, CellCoord{32, 24}, GatherCompletion::OneDelivery},
          ManualQueueMode::Append, true);
    std::vector<double> samples;
    std::size_t visits = 0, admissions = 0, pathwork = 0, bytes = 0, queued = 0;
    bool complete = false;
    for (int tick = 0; tick < 10000; ++tick) {
        const auto start = Clock::now();
        w.tick();
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        const auto md = w.manual_diagnostics();
        const auto pd = w.path_diagnostics();
        visits = std::max(visits, md.visits_last_tick);
        admissions = std::max(admissions, md.path_admissions_last_tick);
        pathwork = std::max(pathwork, pd.work_units_last_tick);
        bytes = std::max(bytes, pd.peak_planner_bytes);
        queued = std::max(queued, pd.queued_batches);
        require(md.visits_last_tick <= manual_visits_per_tick &&
                    md.path_admissions_last_tick <= manual_path_admissions_per_tick &&
                    pd.work_units_last_tick <= path_work_units_per_tick,
                "Scheduler budget exceeded");
        for (const auto &r : w.residents()) {
            require(r.health > 0, "Benchmark actor died");
            require(r.wood_xp <= 1, "Finite Gather produced more than one output");
            if (r.x == 30 && r.y == 24)
                moved.insert(r.id);
        }
        if (md.active_orders == 0 && pd.queued_batches == 0) {
            complete = true;
            break;
        }
    }
    std::size_t gathered = 0, failed = 0;
    for (const auto &r : w.residents()) {
        const int expected = gather_actors.contains(r.id) ? 1 : 0;
        require(r.wood_xp == expected, "An accepted finite Gather did not produce its exact useful output");
        gathered += r.wood_xp == 1;
        failed += r.order_phase == OrderPhase::PathFailed;
        require(r.mining_xp == 0 && r.farming_xp == 0, "Unexpected automatic useful work");
    }
    int stored = 0, carried = 0, total = 0;
    for (int nation = 1; nation <= nations; ++nation) {
        const auto inv = w.inventory(static_cast<NationId>(nation));
        stored += inv.stored.wood;
        carried += inv.carried.wood;
        total += inv.total.wood;
    }
    const auto expected_wood = static_cast<int>(80 * nations + gathered * 3);
    require(complete && moved.size() == static_cast<std::size_t>(population) && !failed && carried == 0 &&
                stored == expected_wood && total == stored,
            "Queue did not settle with exact physical arrivals and delivery");
    require(w.take_path_results().empty(), "Internal Manual or logistics outcomes leaked");
    require(accepted == static_cast<std::size_t>(population) + gathered,
            "Accepted orders do not match physical completions");
    const auto mean =
        std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    std::cout << population << ',' << nations << ',' << w.ticks() << ',' << accepted << ',' << rejected << ','
              << moved.size() << ',' << gathered << ',' << failed << ',' << stored << ',' << carried << ','
              << w.manual_diagnostics().active_orders << ',' << w.path_diagnostics().queued_batches << ','
              << command_ms << ',' << percentile(samples, .5) << ',' << percentile(samples, .95) << ','
              << percentile(samples, .99) << ',' << mean << ',' << samples.back() << ',' << visits << ','
              << admissions << ',' << pathwork << ',' << bytes << ',' << queued << ",true\n";
}
} // namespace
int main() {
    try {
        std::cout << std::fixed << std::setprecision(4)
                  << "population,nations,ticks,accepted_orders,rejected_capacity_orders,physical_move_"
                     "arrivals,delivered_gather_outputs,final_failed_residents,stored_wood,carried_wood,"
                     "active_orders,pending_paths,command_ms,p50_tick_ms,p95_tick_ms,p99_tick_ms,mean_tick_"
                     "ms,max_tick_ms,peak_manual_visits,peak_batch_admissions,peak_path_work,peak_planner_"
                     "bytes,peak_pending_batches,complete\n";
        for (int population : {10, 100, 500, 1500, 6000})
            run(population);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

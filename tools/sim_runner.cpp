#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct TickStats {
    double p50_us{};
    double p95_us{};
    double p99_us{};
    double mean_us{};
    double max_us{};
};
struct PlanningStats {
    TickStats ticks;
    std::uint64_t completion_tick{};
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t peak_work_units{};
    std::size_t peak_planner_bytes{};
    std::size_t peak_staged_cells{};
};

std::uint64_t number(const char *text, const char *option) {
    char *end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        std::cerr << "Invalid value for " << option << ": " << text << '\n';
        std::exit(2);
    }
    return value;
}

double percentile(const std::vector<double> &sorted, double fraction) {
    if (sorted.empty())
        return 0;
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
    return sorted[std::min(sorted.size() - 1, std::max<std::size_t>(1, rank) - 1)];
}

TickStats measure_ticks(ribterra::World &world, std::uint64_t tick_count) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(tick_count));
    for (std::uint64_t tick = 0; tick < tick_count; ++tick) {
        const auto start = Clock::now();
        world.tick();
        const auto stop = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    }
    const double mean = samples.empty() ? 0
                                        : std::accumulate(samples.begin(), samples.end(), 0.0) /
                                              static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    return {percentile(samples, .50), percentile(samples, .95), percentile(samples, .99), mean,
            samples.empty() ? 0 : samples.back()};
}

PlanningStats measure_planning(ribterra::World &world, const std::vector<std::uint64_t> &batch_ids,
                               std::uint64_t maximum_ticks = 1201) {
    std::vector<double> samples;
    PlanningStats result;
    for (std::uint64_t tick = 1;
         tick <= maximum_ticks && result.accepted + result.rejected < batch_ids.size(); ++tick) {
        const auto start = Clock::now();
        world.tick();
        const auto stop = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        const auto diagnostics = world.path_diagnostics();
        result.peak_work_units = std::max(result.peak_work_units, diagnostics.work_units_last_tick);
        result.peak_planner_bytes = std::max(result.peak_planner_bytes, diagnostics.peak_planner_bytes);
        result.peak_staged_cells = std::max(result.peak_staged_cells, diagnostics.peak_staged_route_cells);
        for (const auto &outcome : world.take_path_results()) {
            if (std::find(batch_ids.begin(), batch_ids.end(), outcome.path_batch_id) == batch_ids.end())
                continue;
            if (outcome.accepted)
                ++result.accepted;
            else
                ++result.rejected;
            result.completion_tick = tick;
        }
    }
    const double mean = samples.empty() ? 0
                                        : std::accumulate(samples.begin(), samples.end(), 0.0) /
                                              static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    result.ticks = {percentile(samples, .50), percentile(samples, .95), percentile(samples, .99), mean,
                    samples.empty() ? 0 : samples.back()};
    return result;
}

std::vector<int> resident_ids(const ribterra::World &world) {
    std::vector<int> ids;
    ids.reserve(world.residents().size());
    for (const auto &resident : world.residents())
        ids.push_back(resident.id);
    return ids;
}

double command_move(ribterra::World &world, const std::vector<int> &ids, ribterra::CellCoord destination,
                    std::vector<std::uint64_t> *batch_ids = nullptr) {
    const auto start = Clock::now();
    for (std::size_t offset = 0; offset < ids.size(); offset += ribterra::max_path_batch_residents) {
        const auto end = std::min(ids.size(), offset + ribterra::max_path_batch_residents);
        const std::vector<int> batch(ids.begin() + static_cast<std::ptrdiff_t>(offset),
                                     ids.begin() + static_cast<std::ptrdiff_t>(end));
        const auto result = world.move(batch, destination.x, destination.y, destination.layer);
        if (!result.accepted)
            throw std::runtime_error("Benchmark movement command rejected: " + result.message);
        if (batch_ids)
            batch_ids->push_back(result.path_batch_id);
    }
    const auto stop = Clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

ribterra::World four_nation_world(std::uint32_t seed) {
    ribterra::World source(seed, 1500);
    auto state = source.export_state();
    const auto residents = state.residents;
    const auto routes = state.routes;
    state.residents.reserve(6000);
    state.routes.reserve(6000);
    for (ribterra::NationId nation = 2; nation <= 4; ++nation) {
        const int id_offset = static_cast<int>((nation - 1) * 1500);
        for (auto resident : residents) {
            resident.id += id_offset;
            resident.nation = nation;
            resident.name += " N" + std::to_string(nation);
            state.residents.push_back(std::move(resident));
        }
        for (auto route : routes) {
            route.resident_id += id_offset;
            state.routes.push_back(std::move(route));
        }
    }
    ribterra::World result(seed, 0);
    std::string error;
    if (!result.import_state(std::move(state), error))
        throw std::runtime_error("Could not construct four-nation benchmark: " + error);
    return result;
}

ribterra::CellCoord distant_goal(const ribterra::World &world, ribterra::WorldLayer layer) {
    for (int y = ribterra::world_max - 1; y >= ribterra::world_min; --y)
        for (int x = ribterra::world_max - 1; x >= ribterra::world_min; --x)
            if (world.walkable(x, y, layer))
                return {x, y, layer};
    throw std::runtime_error("Benchmark world has no distant walkable goal");
}

void benchmark(std::uint32_t seed) {
    std::cout << "scenario,population,p95_tick_us,p99_tick_us,mean_tick_us,command_ms\n";
    for (int population : {10, 100, 500, 1500}) {
        ribterra::World idle(seed, population);
        for (int i = 0; i < 20; ++i)
            idle.tick();
        const auto idle_stats = measure_ticks(idle, 10000);
        std::cout << "idle," << population << ',' << std::fixed << std::setprecision(3) << idle_stats.p95_us
                  << ',' << idle_stats.p99_us << ',' << idle_stats.mean_us << ",0.000\n";

        ribterra::World moving(seed, population);
        const auto command_ms =
            command_move(moving, resident_ids(moving), {32, 24, ribterra::WorldLayer::Surface});
        const auto moving_stats = measure_ticks(moving, 200);
        std::cout << "moving," << population << ',' << moving_stats.p95_us << ',' << moving_stats.p99_us
                  << ',' << moving_stats.mean_us << ',' << command_ms << '\n';
    }
}

void print_chunk_row(const char *scenario, int population, int nations, std::uint32_t seed,
                     std::uint64_t tick_count, const TickStats &stats, double command_ms) {
    std::cout << scenario << ',' << population << ',' << nations << ',' << seed << ',' << tick_count << ','
              << std::fixed << std::setprecision(3) << stats.p50_us << ',' << stats.p95_us << ','
              << stats.p99_us << ',' << stats.mean_us << ',' << command_ms << '\n';
}

void print_planner_row(const char *scenario, int population, int nations, std::uint32_t seed,
                       double queueing_ms, const PlanningStats &stats) {
    std::cout << scenario << ',' << population << ',' << nations << ',' << seed << ','
              << stats.completion_tick << ',' << std::fixed << std::setprecision(3) << stats.ticks.p50_us
              << ',' << stats.ticks.p95_us << ',' << stats.ticks.p99_us << ',' << stats.ticks.mean_us << ','
              << stats.ticks.max_us << ',' << queueing_ms << ',' << stats.accepted << ',' << stats.rejected
              << ',' << stats.peak_work_units << ',' << stats.peak_planner_bytes << ','
              << stats.peak_staged_cells << '\n';
}

void benchmark_chunks(std::uint32_t seed, std::uint64_t tick_count) {
    if (tick_count == 0)
        throw std::invalid_argument("--benchmark-chunks requires at least one measured tick");
    std::cout << "# command_ms is the separately measured initial route-command cost\n";
    std::cout << "scenario,population,nations,seed,ticks,p50_tick_us,p95_tick_us,p99_tick_us,"
                 "mean_tick_us,command_ms\n";
    for (int population : {10, 100, 500, 1500}) {
        ribterra::World idle(seed, population);
        for (int warmup = 0; warmup < 10; ++warmup)
            idle.tick();
        print_chunk_row("idle-chunks", population, 1, seed, tick_count, measure_ticks(idle, tick_count), 0);

        ribterra::World moving(seed, population);
        const auto command_ms =
            command_move(moving, resident_ids(moving), {32, 24, ribterra::WorldLayer::Surface});
        print_chunk_row("moving-chunk-seam", population, 1, seed, tick_count,
                        measure_ticks(moving, tick_count), command_ms);
    }

    auto idle_nations = four_nation_world(seed);
    for (int warmup = 0; warmup < 10; ++warmup)
        idle_nations.tick();
    print_chunk_row("idle-four-nations", 6000, 4, seed, tick_count, measure_ticks(idle_nations, tick_count),
                    0);

    auto moving_nations = four_nation_world(seed);
    if (moving_nations.portals().empty())
        throw std::runtime_error("Four-nation benchmark has no portal");
    const auto command_ms =
        command_move(moving_nations, resident_ids(moving_nations), moving_nations.portals().front().mine);
    print_chunk_row("moving-portal", 6000, 4, seed, tick_count, measure_ticks(moving_nations, tick_count),
                    command_ms);

    std::cout << "# bounded path planner: queueing_ms excludes planner ticks; completion_tick=0 means no "
                 "terminal outcome within 1201 ticks\n";
    std::cout << "planner_scenario,population,nations,seed,completion_tick,p50_tick_us,p95_tick_us,"
                 "p99_tick_us,mean_tick_us,max_tick_us,queueing_ms,accepted,rejected,peak_work_units,"
                 "peak_planner_bytes,peak_staged_cells\n";
    for (int population : {10, 100, 500, 1500}) {
        ribterra::World moving(seed, population);
        std::vector<std::uint64_t> batch_ids;
        const auto queueing_ms = command_move(moving, resident_ids(moving),
                                              distant_goal(moving, ribterra::WorldLayer::Mine), &batch_ids);
        print_planner_row("planner-portal", population, 1, seed, queueing_ms,
                          measure_planning(moving, batch_ids));
    }
    auto four_nations = four_nation_world(seed);
    const auto four_ids = resident_ids(four_nations);
    std::vector<std::uint64_t> batch_ids;
    const auto queueing_ms = command_move(four_nations, four_ids,
                                          distant_goal(four_nations, ribterra::WorldLayer::Mine), &batch_ids);
    print_planner_row("planner-four-nations", 6000, 4, seed, queueing_ms,
                      measure_planning(four_nations, batch_ids));
}

void benchmark_logistics(std::uint32_t seed, std::uint64_t maximum_ticks) {
    using namespace ribterra;
    std::cout << "# All residents begin useful Mine work at the portal; one shared ore seam "
                 "contains exactly one yield per resident.\n"
                 "# Physical cargo must return through the portal to each owner's Camp; the "
                 "fixture does not model crowd collision.\n"
                 "scenario,population,nations,ticks,p50_us,p95_us,p99_us,max_us,workers_with_xp,"
                 "delivered_stone,completed_tasks,failed_tasks,failed_unreachable,failed_timeout,"
                 "failed_resident_unavailable,failed_storage_full,failed_cancelled,failed_limit_"
                 "exceeded,failed_target_invalid,observed_continuation_failures,pending_paths,peak_"
                 "active_tasks,peak_carried_"
                 "units,alive,complete,work_started,work_exhausted,work_superseded,work_failed,"
                 "work_unreachable,work_timeout,work_topology_changed,work_limit_exceeded,"
                 "work_resident_unavailable,work_target_invalid,work_reservation_conflict,"
                 "work_admission_attempts,peak_waiting_work,max_work_visits,max_work_admissions,"
                 "work_created,work_cancelled\n";
    for (int population : {10, 100, 500, 1500, 6000}) {
        const int nations = population == 6000 ? 4 : 1;
        auto world = population == 6000 ? four_nation_world(seed) : World(seed, population);
        auto state = world.export_state();
        auto set_tile = [&](CellCoord cell, Tile value) {
            const auto key = chunk_key(cell);
            auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                      [&](const auto &c) { return c.key == key; });
            chunk->tiles[local_index(cell.x, cell.y)] = value;
        };
        for (int y = 20; y <= 32; ++y)
            for (int x = 20; x <= 32; ++x) {
                set_tile({x, y, WorldLayer::Surface}, {Terrain::Grass, 0, true});
                set_tile({x, y, WorldLayer::Mine}, {Terrain::MineFloor, 0, true});
            }
        for (int nation = 2; nation <= nations; ++nation) {
            const int id = state.next_building_id++;
            state.buildings.push_back({id, BuildingKind::Camp, 24, 23 + nation, 1, WorldLayer::Surface,
                                       static_cast<NationId>(nation)});
            state.storages.push_back({id, 10000, 7, 0});
        }
        const CellCoord seam{29, 24, WorldLayer::Mine};
        set_tile(seam, {Terrain::MineOre, population, true});
        for (auto &resident : state.residents) {
            resident.x = 28;
            resident.y = 24;
            resident.layer = WorldLayer::Mine;
            resident.target_x = seam.x;
            resident.target_y = seam.y;
            resident.job = Job::Mine;
            resident.progress = 0;
            resident.order_phase = OrderPhase::Active;
        }
        for (auto &route : state.routes) {
            route.points.clear();
            route.cursor = 0;
            route.work = seam;
        }
        std::string error;
        if (!world.import_state(std::move(state), error))
            throw std::runtime_error("Logistics benchmark fixture rejected: " + error);
        std::vector<double> samples;
        struct TaskObservation {
            LogisticsPhase phase{LogisticsPhase::Queued};
            LogisticsFailure failure{LogisticsFailure::None};
            bool observed_delivery{};
        };
        std::unordered_map<std::uint64_t, TaskObservation> task_observations;
        std::size_t peak_active = 0, peak_carried = 0;
        std::size_t peak_waiting_work = 0, max_work_visits = 0, max_work_admissions = 0;
        std::size_t work_attempts = 0;
        bool complete = false;
        std::uint64_t tick = 0;
        int delivered = 0;
        for (; tick < maximum_ticks; ++tick) {
            const auto start = Clock::now();
            world.tick();
            const auto stop = Clock::now();
            samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
            world.take_path_results();
            const auto work = world.work_diagnostics();
            work_attempts += work.admission_attempts_last_tick;
            peak_waiting_work = std::max(peak_waiting_work, work.waiting_intents);
            max_work_visits = std::max(max_work_visits, work.visits_last_tick);
            max_work_admissions = std::max(max_work_admissions, work.admission_attempts_last_tick);
            std::size_t active = 0, carried = 0, pending_paths = 0;
            for (const auto &task : world.logistics_tasks()) {
                auto &observation = task_observations[task.task_id];
                observation.phase = task.phase;
                observation.failure = task.failure;
                if (task.phase == LogisticsPhase::Complete)
                    observation.observed_delivery = true;
                if (task.path_batch_id)
                    ++pending_paths;
                if (task.phase != LogisticsPhase::Complete && task.phase != LogisticsPhase::Failed)
                    ++active;
            }
            delivered = -40;
            for (int nation = 1; nation <= nations; ++nation) {
                const auto inventory = world.inventory(static_cast<NationId>(nation));
                delivered += inventory.stored.stone;
                carried += static_cast<std::size_t>(inventory.carried.stone);
            }
            peak_active = std::max(peak_active, active);
            peak_carried = std::max(peak_carried, carried);
            if (delivered == population * 2 && carried == 0 && active == 0 && pending_paths == 0 &&
                world.path_diagnostics().queued_batches == 0 && work.active_intents == 0 &&
                std::all_of(world.residents().begin(), world.residents().end(),
                            [](const auto &r) { return r.job == Job::Idle; })) {
                complete = true;
                ++tick;
                break;
            }
        }
        std::sort(samples.begin(), samples.end());
        const auto workers = std::count_if(world.residents().begin(), world.residents().end(),
                                           [](const auto &r) { return r.mining_xp > 0; });
        const auto alive = std::count_if(world.residents().begin(), world.residents().end(),
                                         [](const auto &r) { return r.health > 0; });
        std::size_t completed_tasks = 0, failed_tasks = 0, continuation_failures = 0;
        std::array<std::size_t, 8> failure_counts{};
        // Count terminal transitions when they occur, before the bounded history is pruned.
        // Sampling retained records once per tick misses callbacks retired in that same tick.
        const auto outcomes = world.work_diagnostics().outcomes;
        const auto &work_failures = outcomes.failures;
        const auto work_failed = std::accumulate(work_failures.begin(), work_failures.end(), std::size_t{});
        for (const auto &[id, observation] : task_observations) {
            if (observation.phase == LogisticsPhase::Complete)
                ++completed_tasks;
            else if (observation.phase == LogisticsPhase::Failed) {
                ++failed_tasks;
                ++failure_counts[static_cast<unsigned>(observation.failure)];
                if (observation.observed_delivery)
                    ++continuation_failures;
            }
        }
        std::cout << "mine-portal-haul," << population << ',' << nations << ',' << tick << ',' << std::fixed
                  << std::setprecision(3) << percentile(samples, .50) << ',' << percentile(samples, .95)
                  << ',' << percentile(samples, .99) << ',' << (samples.empty() ? 0 : samples.back()) << ','
                  << workers << ',' << delivered << ',' << completed_tasks << ',' << failed_tasks;
        for (std::size_t reason = 1; reason < failure_counts.size(); ++reason)
            std::cout << ',' << failure_counts[reason];
        std::cout << ',' << continuation_failures << ',' << world.path_diagnostics().queued_batches << ','
                  << peak_active << ',' << peak_carried << ',' << alive << ',' << (complete ? 1 : 0) << ','
                  << outcomes.started << ',' << outcomes.exhausted << ',' << outcomes.superseded << ','
                  << work_failed;
        for (std::size_t reason = 1; reason < work_failures.size(); ++reason)
            std::cout << ',' << work_failures[reason];
        std::cout << ',' << work_attempts << ',' << peak_waiting_work << ',' << max_work_visits << ','
                  << max_work_admissions << ',' << outcomes.created << ',' << outcomes.cancelled << '\n';
        if (outcomes.created !=
            outcomes.started + outcomes.exhausted + outcomes.superseded + outcomes.cancelled + work_failed)
            throw std::runtime_error("Work intent terminal accounting did not cover every created intent");
        if (!complete || workers != population || alive != population)
            throw std::runtime_error("Logistics benchmark did not complete every resident's useful "
                                     "work and physical delivery");
    }
}

void benchmark_professions(std::uint32_t seed, std::uint64_t maximum_ticks) {
    using namespace ribterra;
    if (!maximum_ticks || maximum_ticks > 100000)
        throw std::invalid_argument("--benchmark-professions requires --ticks in 1..100000");
    std::cout << "# Controlled flat fixture: all actors start together near two shared resource seams and "
                 "owner Camps with enlarged storage and sufficient provisioned Food. No crowd collision.\n"
                 "# Only terrain, initial positions, Camps and stocks are fixtures. Normal Assign/Stop "
                 "commands drive every job, route, item and XP transition.\n"
                 "# Each actor receives Stop after one useful harvest per stage, preserving in-flight "
                 "delivery; the barriers prevent early actors consuming the shared seam before late actors.\n"
                 "# Stages Woodcutter->Miner->Woodcutter retain independent XP. Empty-source reassignment "
                 "and Stop follow; this is not a full Utility AI, geography or crowd performance gate.\n"
                 "scenario,population,nations,seed,stage_ticks,total_ticks,p50_us,p95_us,p99_us,max_us,"
                 "command_ms,stop_ms,alive,actors_with_wood_work,actors_with_mining_work,min_wood_xp,"
                 "min_mining_xp,zero_wood_xp,zero_mining_xp,wood_yields,mining_yields,generated_wood,"
                 "generated_stone,carried_units,ground_units,pending_paths,active_work,max_profession_visits,"
                 "max_profession_admissions,max_candidate_checks,max_chunk_refreshes,max_path_work,"
                 "peak_planner_bytes,peak_staged_cells,max_work_visits,max_work_admissions,"
                 "final_wait_none,final_wait_unassigned,final_wait_stopped,final_wait_manual,final_wait_"
                 "delivery,final_wait_no_compatible,"
                 "final_wait_worksite,final_wait_queue,final_wait_path,final_wait_incapacitated,final_wait_"
                 "pending,complete\n";
    struct Learning {
        int xp{};
        std::uint32_t remainder{};
        bool operator==(const Learning &) const = default;
    };
    std::array<Learning, 3> learning{};
    {
        int xp = 0;
        SkillPractice practice;
        for (std::size_t count = 1; count < learning.size(); ++count) {
            record_skill_work(xp, practice, skill_gather_work_units(), count);
            learning[count] = {xp, practice.experience_remainder};
        }
    }
    const auto harvest_count = [&](const Resident &resident, Skill skill) {
        const auto index = static_cast<std::size_t>(skill);
        const Learning value{skill == Skill::Woodcutting ? resident.wood_xp : resident.mining_xp,
                             resident.skill_practice[index].experience_remainder};
        const auto found = std::find(learning.begin(), learning.end(), value);
        if (found == learning.end())
            throw std::runtime_error("Profession benchmark observed unaccounted or duplicate harvest XP");
        return static_cast<int>(found - learning.begin());
    };
    for (int population : {10, 100, 500, 1500, 6000}) {
        const int nations = population == 6000 ? 4 : 1;
        const int per_nation = population / nations;
        auto world = population == 6000 ? four_nation_world(seed) : World(seed, population);
        auto state = world.export_state();
        for (auto &chunk : state.chunks)
            for (auto &tile : chunk.tiles)
                tile = {chunk.key.layer == WorldLayer::Surface ? Terrain::Grass : Terrain::MineFloor, 0,
                        false};
        const auto set_tile = [&](CellCoord cell, Tile value) {
            auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                      [&](const auto &c) { return c.key == chunk_key(cell); });
            if (chunk == state.chunks.end())
                throw std::runtime_error("Missing profession fixture chunk");
            chunk->tiles[local_index(cell.x, cell.y)] = value;
        };
        for (int y = 20; y <= 32; ++y)
            for (int x = 20; x <= 32; ++x) {
                set_tile({x, y, WorldLayer::Surface}, {Terrain::Grass, 0, true});
                set_tile({x, y, WorldLayer::Mine}, {Terrain::MineFloor, 0, true});
            }
        const CellCoord forest{30, 24, WorldLayer::Surface}, rock{30, 26, WorldLayer::Surface};
        set_tile(forest, {Terrain::Forest, population * 2, true});
        set_tile(rock, {Terrain::Rock, population, true});
        for (auto &resident : state.residents) {
            resident.x = 28;
            resident.y = 24;
            resident.target_x = 28;
            resident.target_y = 24;
            // Constructor actors are Idle with empty routes and neutral policy.
            if (resident.job != Job::Idle || resident.profession != ProfessionState{})
                throw std::runtime_error("Profession fixture must start with neutral actors");
        }
        std::array<int, 5> camps{};
        for (const auto &building : state.buildings)
            if (building.kind == BuildingKind::Camp)
                camps[building.nation] = building.id;
        for (int nation = 2; nation <= nations; ++nation) {
            const int id = state.next_building_id++;
            state.buildings.push_back({id, BuildingKind::Camp, 24, 23 + nation, 1, WorldLayer::Surface,
                                       static_cast<NationId>(nation)});
            state.storages.push_back({id, 1'000'000, 7, 0});
            camps[static_cast<std::size_t>(nation)] = id;
        }
        for (auto &storage : state.storages)
            storage.capacity = 1'000'000;
        state.item_stacks.clear();
        const auto food_per_nation =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(per_nation) * (maximum_ticks / 200 + 2));
        for (int nation = 1; nation <= nations; ++nation)
            state.item_stacks.push_back(
                {state.next_item_stack_id++,
                 ItemKind::Food,
                 food_per_nation,
                 static_cast<NationId>(nation),
                 {ItemLocationKind::Storage, {}, camps[static_cast<std::size_t>(nation)]}});
        state.resources = {0, 0, static_cast<int>(food_per_nation)};
        std::string error;
        if (!world.import_state(std::move(state), error))
            throw std::runtime_error("Profession benchmark fixture rejected: " + error);
        const auto ids = resident_ids(world);
        const auto command = [&](const std::vector<int> &selected, const ProfessionAssignment *assignment) {
            const auto started = Clock::now();
            for (std::size_t offset = 0; offset < selected.size(); offset += max_path_batch_residents) {
                const auto end = std::min(selected.size(), offset + max_path_batch_residents);
                const std::vector<int> batch(selected.begin() + static_cast<std::ptrdiff_t>(offset),
                                             selected.begin() + static_cast<std::ptrdiff_t>(end));
                const auto results =
                    assignment ? world.assign_profession(batch, *assignment) : world.stop(batch);
                if (results.size() != batch.size() ||
                    std::any_of(results.begin(), results.end(), [](const auto &result) {
                        return result.status == ResidentCommandStatus::Rejected;
                    }))
                    throw std::runtime_error("Profession benchmark Assign/Stop was rejected");
            }
            return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        };
        const auto physical_totals = [&] {
            std::array<std::array<std::int64_t, 3>, 5> physical{};
            for (const auto &stack : world.item_stacks()) {
                if (stack.owner_nation == 0 || stack.owner_nation > static_cast<NationId>(nations))
                    throw std::runtime_error("Profession output has an unexpected owner");
                physical[stack.owner_nation][static_cast<std::size_t>(stack.item)] += stack.quantity;
            }
            return physical;
        };
        const auto check_conservation = [&] {
            std::array<std::array<std::int64_t, 2>, 5> earned{};
            for (const auto &resident : world.residents()) {
                if (resident.health != 100)
                    throw std::runtime_error("Provisioned profession actor lost health");
                earned[resident.nation][0] += harvest_count(resident, Skill::Woodcutting) * 3;
                earned[resident.nation][1] += harvest_count(resident, Skill::Mining) * 2;
            }
            const auto physical = physical_totals();
            for (int nation = 1; nation <= nations; ++nation) {
                const auto owner = static_cast<std::size_t>(nation);
                const auto food = std::int64_t(food_per_nation) -
                                  static_cast<std::int64_t>(world.ticks() / 200) * per_nation;
                if (physical[owner][0] != earned[owner][0] || physical[owner][1] != earned[owner][1] ||
                    physical[owner][2] != food)
                    throw std::runtime_error(
                        "Profession XP/item/food conservation or owner isolation failed");
            }
        };
        const std::array<std::array<int, 2>, 5> stage_begin{{{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 1}}};
        const std::array<std::array<int, 2>, 5> stage_end{{{1, 0}, {1, 1}, {2, 1}, {2, 1}, {2, 1}}};
        const auto check_stage_learning = [&](const auto &minimum, const auto &maximum) {
            for (const auto &resident : world.residents()) {
                const std::array counts{harvest_count(resident, Skill::Woodcutting),
                                        harvest_count(resident, Skill::Mining)};
                for (std::size_t skill = 0; skill < counts.size(); ++skill)
                    if (counts[skill] < minimum[skill] || counts[skill] > maximum[skill])
                        throw std::runtime_error(
                            "Profession benchmark observed work outside its assigned stage");
            }
        };
        for (int stage = 0; stage < 5; ++stage) {
            const auto index = static_cast<std::size_t>(stage);
            check_stage_learning(stage_begin[index], stage_begin[index]);
            const bool mining = stage == 1;
            ProfessionAssignment assignment;
            assignment.profession = mining ? Profession::Miner : Profession::Woodcutter;
            assignment.worksite = mining ? rock : forest;
            assignment.priorities = {0, 0, 0, 0, 0};
            assignment.priorities[mining ? 1 : 0] = 5;
            const double command_ms = command(ids, stage == 4 ? nullptr : &assignment);
            for (const auto &resident : world.residents())
                if ((stage < 4 && resident.profession.assignment != assignment) ||
                    (stage == 4 && resident.profession.assignment.automation_enabled))
                    throw std::runtime_error(
                        "Accepted profession command did not change the authoritative policy");
            double stop_ms = 0;
            std::vector<bool> stopped(static_cast<std::size_t>(population + 1), false);
            std::vector<double> samples;
            ProfessionDiagnostics maxima;
            std::size_t max_path_work = 0, peak_planner_bytes = 0, peak_staged_cells = 0;
            std::size_t max_work_visits = 0, max_work_admissions = 0;
            bool complete = false;
            const auto start_tick = world.ticks();
            while (world.ticks() < maximum_ticks) {
                const auto started = Clock::now();
                world.tick();
                samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - started).count());
                world.take_path_results();
                const auto selection = world.profession_diagnostics();
                const auto path = world.path_diagnostics();
                const auto work = world.work_diagnostics();
                maxima.visits_last_tick = std::max(maxima.visits_last_tick, selection.visits_last_tick);
                maxima.admissions_last_tick =
                    std::max(maxima.admissions_last_tick, selection.admissions_last_tick);
                maxima.candidate_checks_last_tick =
                    std::max(maxima.candidate_checks_last_tick, selection.candidate_checks_last_tick);
                maxima.chunk_refreshes_last_tick =
                    std::max(maxima.chunk_refreshes_last_tick, selection.chunk_refreshes_last_tick);
                max_path_work = std::max(max_path_work, path.work_units_last_tick);
                peak_planner_bytes = std::max(peak_planner_bytes, path.peak_planner_bytes);
                peak_staged_cells = std::max(peak_staged_cells, path.peak_staged_route_cells);
                max_work_visits = std::max(max_work_visits, work.visits_last_tick);
                max_work_admissions = std::max(max_work_admissions, work.admission_attempts_last_tick);
                if (selection.visits_last_tick > profession_visits_per_tick ||
                    selection.admissions_last_tick > profession_admissions_per_tick ||
                    selection.candidate_checks_last_tick > selection.visits_last_tick * 2 ||
                    selection.chunk_refreshes_last_tick > world.chunks().size() ||
                    path.work_units_last_tick > path_work_units_per_tick ||
                    path.peak_planner_bytes > max_planner_bytes ||
                    path.peak_staged_route_cells > max_staged_route_cells ||
                    work.visits_last_tick > work_intent_visits_per_tick ||
                    work.admission_attempts_last_tick > work_admissions_per_tick)
                    throw std::runtime_error("Profession benchmark exceeded a selector/work/path budget");
                check_conservation();
                check_stage_learning(stage_begin[index], stage_end[index]);
                std::vector<int> stop_ids;
                if (stage < 3)
                    for (const auto &resident : world.residents()) {
                        const int expected = stage == 2 ? 2 : 1;
                        const int count =
                            harvest_count(resident, mining ? Skill::Mining : Skill::Woodcutting);
                        if (count > expected)
                            throw std::runtime_error(
                                "Profession actor performed extra work before its Stop barrier");
                        if (!stopped[static_cast<std::size_t>(resident.id)] && count == expected) {
                            stopped[static_cast<std::size_t>(resident.id)] = true;
                            stop_ids.push_back(resident.id);
                        }
                    }
                if (!stop_ids.empty())
                    stop_ms += command(stop_ids, nullptr);
                check_conservation();
                const bool settled =
                    world.path_diagnostics().queued_batches == 0 &&
                    world.work_diagnostics().active_intents == 0 &&
                    std::none_of(world.logistics_tasks().begin(), world.logistics_tasks().end(),
                                 [](const auto &task) {
                                     return task.phase != LogisticsPhase::Complete &&
                                            task.phase != LogisticsPhase::Failed;
                                 }) &&
                    std::none_of(world.item_stacks().begin(), world.item_stacks().end(),
                                 [](const auto &stack) {
                                     return stack.location.kind == ItemLocationKind::Carried ||
                                            stack.location.kind == ItemLocationKind::Ground;
                                 }) &&
                    std::all_of(world.residents().begin(), world.residents().end(), [](const auto &resident) {
                        return resident.job == Job::Idle && !resident.profession.deferred;
                    });
                if (stage < 3) {
                    complete = settled && std::all_of(stopped.begin() + 1, stopped.end(),
                                                      [](bool value) { return value; });
                } else if (world.ticks() - start_tick >= 20) {
                    const auto reason = stage == 3 ? ProfessionWaitReason::WorksiteUnavailable
                                                   : ProfessionWaitReason::Stopped;
                    complete = settled && std::all_of(world.residents().begin(), world.residents().end(),
                                                      [&](const auto &resident) {
                                                          return resident.profession.wait_reason == reason;
                                                      });
                }
                if (complete)
                    break;
            }
            if (complete)
                check_stage_learning(stage_end[index], stage_end[index]);
            std::sort(samples.begin(), samples.end());
            std::size_t wood_workers = 0, mining_workers = 0, zero_wood = 0, zero_mining = 0, alive = 0;
            int min_wood = maximum_skill_experience, min_mining = maximum_skill_experience;
            std::int64_t wood_yields = 0, mining_yields = 0, generated_wood = 0, generated_stone = 0;
            std::uint64_t carried = 0, ground = 0;
            std::array<std::size_t, static_cast<std::size_t>(ProfessionWaitReason::PendingOrder) + 1>
                waiting{};
            for (const auto &resident : world.residents()) {
                const int wood = harvest_count(resident, Skill::Woodcutting),
                          stone = harvest_count(resident, Skill::Mining);
                wood_workers += wood > 0;
                mining_workers += stone > 0;
                wood_yields += wood;
                mining_yields += stone;
                zero_wood += resident.wood_xp == 0;
                zero_mining += resident.mining_xp == 0;
                min_wood = std::min(min_wood, resident.wood_xp);
                min_mining = std::min(min_mining, resident.mining_xp);
                alive += resident.health > 0;
                ++waiting[static_cast<std::size_t>(resident.profession.wait_reason)];
            }
            for (const auto &stack : world.item_stacks()) {
                if (stack.item == ItemKind::Wood)
                    generated_wood += stack.quantity;
                if (stack.item == ItemKind::Stone)
                    generated_stone += stack.quantity;
                if (stack.location.kind == ItemLocationKind::Carried)
                    carried += stack.quantity;
                if (stack.location.kind == ItemLocationKind::Ground)
                    ground += stack.quantity;
            }
            const std::array names{"woodcutter-first", "miner-reassigned", "woodcutter-return",
                                   "woodcut-exhausted", "stopped"};
            std::cout << names[static_cast<std::size_t>(stage)] << ',' << population << ',' << nations << ','
                      << seed << ',' << world.ticks() - start_tick << ',' << world.ticks() << ','
                      << std::fixed << std::setprecision(3) << percentile(samples, .5) << ','
                      << percentile(samples, .95) << ',' << percentile(samples, .99) << ','
                      << (samples.empty() ? 0 : samples.back()) << ',' << command_ms << ',' << stop_ms << ','
                      << alive << ',' << wood_workers << ',' << mining_workers << ',' << min_wood << ','
                      << min_mining << ',' << zero_wood << ',' << zero_mining << ',' << wood_yields << ','
                      << mining_yields << ',' << generated_wood << ',' << generated_stone << ',' << carried
                      << ',' << ground << ',' << world.path_diagnostics().queued_batches << ','
                      << world.work_diagnostics().active_intents << ',' << maxima.visits_last_tick << ','
                      << maxima.admissions_last_tick << ',' << maxima.candidate_checks_last_tick << ','
                      << maxima.chunk_refreshes_last_tick << ',' << max_path_work << ',' << peak_planner_bytes
                      << ',' << peak_staged_cells << ',' << max_work_visits << ',' << max_work_admissions;
            for (auto count : waiting)
                std::cout << ',' << count;
            std::cout << ',' << complete << '\n';
            if (!complete || alive != static_cast<std::size_t>(population))
                throw std::runtime_error(
                    "Profession benchmark stage did not finish all actors and deliveries");
            if ((stage == 1 && world.tile(rock.x, rock.y).amount != 0) ||
                (stage >= 2 && world.tile(forest.x, forest.y).amount != 0))
                throw std::runtime_error("Profession benchmark did not exhaust its exact resource stock");
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    std::uint64_t ticks = 600;
    int population = 10;
    std::uint32_t seed = 70421;
    bool bench = false;
    bool bench_chunks = false;
    bool bench_logistics = false;
    bool bench_professions = false;
    bool ticks_explicit = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--benchmark")
            bench = true;
        else if (arg == "--benchmark-chunks")
            bench_chunks = true;
        else if (arg == "--benchmark-logistics")
            bench_logistics = true;
        else if (arg == "--benchmark-professions")
            bench_professions = true;
        else if ((arg == "--ticks" || arg == "--population" || arg == "--seed") && i + 1 < argc) {
            const auto value = number(argv[++i], arg.c_str());
            if (arg == "--ticks") {
                ticks = value;
                ticks_explicit = true;
            } else if (arg == "--population")
                population = static_cast<int>(std::min<std::uint64_t>(value, 1500));
            else
                seed = static_cast<std::uint32_t>(value);
        } else {
            std::cerr
                << "Usage: sim_runner [--ticks N] [--population N] [--seed N] "
                   "[--benchmark | --benchmark-chunks | --benchmark-logistics | --benchmark-professions]\n";
            return 2;
        }
    }
    if (static_cast<int>(bench) + static_cast<int>(bench_chunks) + static_cast<int>(bench_logistics) +
            static_cast<int>(bench_professions) >
        1) {
        std::cerr << "Choose only one benchmark mode.\n";
        return 2;
    }
    try {
        if (bench) {
            benchmark(seed);
            return 0;
        }
        if (bench_chunks) {
            benchmark_chunks(seed, ticks_explicit ? ticks : 160);
            return 0;
        }
        if (bench_logistics) {
            benchmark_logistics(seed, ticks_explicit ? ticks : 10000);
            return 0;
        }
        if (bench_professions) {
            benchmark_professions(seed, ticks_explicit ? ticks : 10000);
            return 0;
        }
        ribterra::World world(seed, population);
        for (std::uint64_t i = 0; i < ticks; ++i)
            world.tick();
        std::cout << "ticks=" << world.ticks() << " day=" << world.day()
                  << " population=" << world.residents().size() << " housing=" << world.housing()
                  << " resources=" << world.resources().wood << '/' << world.resources().stone << '/'
                  << world.resources().food << " checksum=" << world.checksum() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 3;
    }
}

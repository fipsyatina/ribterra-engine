// Genuine format 7 capture: compile ONLY with the frozen d3bc785 headers/libs.
// WorldState is observed for predicates; it is never edited or imported.
#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using namespace ribterra;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
constexpr auto commit = "d3bc78530ab2f03d8116bb9ef4c09ba71ca0232d";
constexpr auto content = "92e20f6f81770d8ca01a478753e8022ee600ca4fa9ddf0a88e07560043a23bcd";
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<std::uint8_t> bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read " + path.generic_string());
    return {std::istreambuf_iterator<char>(input), {}};
}
std::uint32_t word(const std::vector<std::uint8_t> &data, std::size_t offset) {
    require(data.size() >= offset + 4, "Truncated container header");
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(data[offset + i]) << (8U * i);
    return value;
}
void current_header(const std::vector<std::uint8_t> &data) {
    require(data.size() >= 96 && word(data, 12) == 7 && word(data, 16) == 6 && word(data, 20) == 2,
            "Expected genuine format 7/rules 6/generator 2 container");
    constexpr char hex[] = "0123456789abcdef";
    std::string hash;
    for (std::size_t i = 64; i < 96; ++i) {
        hash += hex[data[i] >> 4U];
        hash += hex[data[i] & 15U];
    }
    require(hash == content, "Unexpected format 7 gameplay hash");
}
Json writer() {
    return {{"source_commit", commit},
            {"engine", engine_version},
            {"save_format", 7},
            {"rules", rules_version},
            {"protocol", protocol_version},
            {"generator", generator_version},
            {"content_hash", default_content_hash()}};
}
Json coord(CellCoord value) {
    return {{"x", value.x}, {"y", value.y}, {"layer", static_cast<int>(value.layer)}};
}
Json resources(Resources value) {
    return {{"wood", value.wood}, {"stone", value.stone}, {"food", value.food}};
}
const Resident &resident(const World &world, int id) {
    const auto &people = world.residents();
    const auto it = std::find_if(people.begin(), people.end(), [=](const auto &r) { return r.id == id; });
    require(it != people.end(), "Resident is missing");
    return *it;
}
std::vector<int> all(const World &world) {
    std::vector<int> ids;
    for (const auto &r : world.residents())
        ids.push_back(r.id);
    return ids;
}
struct Scenario {
    World world;
    Json start, commands = Json::array();
    Scenario(std::uint32_t seed, int population)
        : world(seed, population), start{{"kind", "new_world"}, {"seed", seed}, {"population", population}} {}
    Scenario(World &&loaded, Json origin) : world(std::move(loaded)), start(std::move(origin)) {}
    void tick() {
        const auto before = world.ticks();
        world.tick();
        if (!commands.empty() && commands.back().value("op", "") == "tick") {
            commands.back()["count"] = commands.back()["count"].get<std::uint64_t>() + 1;
            commands.back()["last_tick"] = world.ticks();
        } else {
            commands.push_back(
                {{"op", "tick"}, {"first_tick", before + 1}, {"count", 1}, {"last_tick", world.ticks()}});
        }
    }
    template <class Predicate> void until(Predicate ready, const char *failure, int limit = 5000) {
        for (int i = 0; i < limit && !ready(); ++i)
            tick();
        require(ready(), failure);
    }
    std::uint64_t path(const char *op, const std::vector<int> &ids, CellCoord target) {
        const bool gathering = std::string_view(op) == "Gather";
        const auto result = gathering ? world.gather(ids, target.x, target.y, target.layer)
                                      : world.move(ids, target.x, target.y, target.layer);
        commands.push_back({{"op", op},
                            {"tick", world.ticks()},
                            {"residents", ids},
                            {"target", coord(target)},
                            {"accepted", result.accepted},
                            {"pending", result.pending},
                            {"path_batch_id", result.path_batch_id},
                            {"failure", static_cast<int>(result.failure)}});
        require(result.accepted, std::string(op) + " rejected: " + result.message);
        return result.path_batch_id;
    }
    std::vector<ResidentCommandOutcome> policy(const std::vector<int> &ids,
                                               const ProfessionAssignment &assignment, bool stop = false) {
        const auto result = stop ? world.stop(ids) : world.assign_profession(ids, assignment);
        Json record{{"op", stop ? "Stop" : "AssignProfession"},
                    {"tick", world.ticks()},
                    {"residents", ids},
                    {"outcomes", Json::array()}};
        if (!stop)
            record["assignment"] = {
                {"profession", static_cast<int>(assignment.profession)},
                {"worksite", assignment.worksite ? coord(*assignment.worksite) : Json(nullptr)},
                {"priorities", assignment.priorities},
                {"automation_enabled", assignment.automation_enabled}};
        for (const auto &item : result)
            record["outcomes"].push_back({{"resident_id", item.resident_id},
                                          {"status", static_cast<int>(item.status)},
                                          {"failure", static_cast<int>(item.failure)}});
        commands.push_back(std::move(record));
        require(result.size() == ids.size() &&
                    std::none_of(result.begin(), result.end(),
                                 [](const auto &r) { return r.status == ResidentCommandStatus::Rejected; }),
                "Profession command rejected");
        return result;
    }
    void house(CellCoord site) {
        const auto result = world.build(BuildingKind::House, site.x, site.y, site.layer);
        commands.push_back({{"op", "Build"},
                            {"kind", "House"},
                            {"tick", world.ticks()},
                            {"target", coord(site)},
                            {"accepted", result.accepted},
                            {"path_batch_id", result.path_batch_id}});
        require(result.accepted, "House rejected: " + result.message);
    }
};
Json summary(const World &world) {
    const auto state = world.export_state();
    Json result{{"seed", state.seed},
                {"resident_count", state.residents.size()},
                {"manual_override_count", 0},
                {"deferred_count", 0},
                {"profession_cursor_id", state.profession_cursor_id},
                {"path_batches", Json::array()},
                {"path_outcome_count", state.path_results.size()},
                {"routes", Json::array()},
                {"work_intents", Json::array()},
                {"cargo", Json::array()},
                {"construction", Json::array()},
                {"residents_sample", Json::array()},
                {"inventory_total", resources(world.inventory().total)},
                {"inventory_stored", resources(world.inventory().stored)},
                {"inventory_carried", resources(world.inventory().carried)}};
    for (const auto &r : state.residents) {
        if (r.profession.manual_override)
            result["manual_override_count"] = result["manual_override_count"].get<int>() + 1;
        if (r.profession.deferred)
            result["deferred_count"] = result["deferred_count"].get<int>() + 1;
        if (result["residents_sample"].size() < 8) {
            Json practice = Json::array();
            for (const auto &p : r.skill_practice)
                practice.push_back({{"experience_remainder", p.experience_remainder},
                                    {"familiarity_debt", p.familiarity_debt},
                                    {"last_practice_tick", p.last_practice_tick}});
            result["residents_sample"].push_back(
                {{"id", r.id},
                 {"x", r.x},
                 {"y", r.y},
                 {"job", static_cast<int>(r.job)},
                 {"health", r.health},
                 {"hunger", r.hunger},
                 {"pending_path_batch", r.pending_path_batch},
                 {"wood_xp", r.wood_xp},
                 {"mining_xp", r.mining_xp},
                 {"farming_xp", r.farming_xp},
                 {"additional_xp", r.additional_xp},
                 {"skill_practice", practice},
                 {"abilities_bp", r.abilities_bp},
                 {"profession", static_cast<int>(r.profession.assignment.profession)},
                 {"automation_enabled", r.profession.assignment.automation_enabled},
                 {"manual_override", r.profession.manual_override},
                 {"deferred", r.profession.deferred},
                 {"wait_reason", static_cast<int>(r.profession.wait_reason)},
                 {"next_evaluation_tick", r.profession.next_evaluation_tick}});
        }
    }
    for (const auto &batch : state.path_batches)
        result["path_batches"].push_back({{"id", batch.path_batch_id},
                                          {"command", static_cast<int>(batch.command)},
                                          {"phase", static_cast<int>(batch.phase)},
                                          {"resident_count", batch.resident_ids.size()},
                                          {"frontier_size", batch.frontier.size()},
                                          {"frontier_cursor", batch.frontier_cursor},
                                          {"nodes", batch.nodes.size()},
                                          {"drafts", batch.drafts.size()},
                                          {"reconstruction_resident", batch.reconstruction_resident},
                                          {"stage_cursor", batch.stage_cursor},
                                          {"work_units", batch.work_units_done}});
    for (const auto &route : state.routes)
        result["routes"].push_back(
            {{"resident_id", route.resident_id}, {"cursor", route.cursor}, {"points", route.points.size()}});
    for (const auto &intent : state.work_intents)
        result["work_intents"].push_back({{"id", intent.intent_id},
                                          {"kind", static_cast<int>(intent.kind)},
                                          {"phase", static_cast<int>(intent.phase)},
                                          {"completion", static_cast<int>(intent.completion)},
                                          {"failure", static_cast<int>(intent.failure)},
                                          {"resident_id", intent.resident_id},
                                          {"building_id", intent.building_id},
                                          {"path_batch_id", intent.path_batch_id},
                                          {"attempts", intent.attempts}});
    for (const auto &task : state.logistics_tasks)
        if (task.carry_stack_id)
            result["cargo"].push_back({{"task_id", task.task_id},
                                       {"resident_id", task.resident_id},
                                       {"kind", static_cast<int>(task.kind)},
                                       {"phase", static_cast<int>(task.phase)},
                                       {"carry_stack_id", task.carry_stack_id},
                                       {"quantity", task.quantity},
                                       {"item", static_cast<int>(task.item)},
                                       {"path_batch_id", task.path_batch_id}});
    for (const auto &site : state.construction_orders)
        result["construction"].push_back({{"building_id", site.building_id},
                                          {"phase", static_cast<int>(site.phase)},
                                          {"required", resources(site.required)},
                                          {"delivered", resources(site.delivered)}});
    return result;
}
struct Capture {
    fs::path root;
    Json report{{"schema", 1}, {"writer", writer()}, {"fixtures", Json::array()}, {"gaps", Json::array()}};
    std::set<std::string> names;
    explicit Capture(fs::path output) : root(std::move(output)) {
        require(!fs::exists(root / "capture-manifest.json"), "Refusing to overwrite capture manifest");
        require(!fs::exists(root / "fixtures") || fs::is_empty(root / "fixtures"),
                "Refusing to reuse a nonempty fixtures directory; choose a fresh output root");
        fs::create_directories(root / "fixtures");
        fs::create_directories(root / "inputs");
    }
    fs::path path(const std::string &name) const {
        return root / "fixtures" / ("world_save_v7_" + name + ".ribsave");
    }
    void save(Scenario &scenario, const std::string &name, const std::string &condition) {
        const auto file = path(name);
        require(!fs::exists(file), "Refusing to overwrite " + file.generic_string());
        std::string error;
        require(scenario.world.save(file, error), "Save failed: " + error);
        current_header(bytes(file));
        World restored(1, 1);
        require(restored.load(file, error), "Reload failed: " + error);
        require(restored.checksum() == scenario.world.checksum(), "Capture round trip changed state");
        report["fixtures"].push_back({{"name", name},
                                      {"file", fs::relative(file, root).generic_string()},
                                      {"start", scenario.start},
                                      {"commands", scenario.commands},
                                      {"condition", condition},
                                      {"initial_tick", scenario.world.ticks()},
                                      {"checksum", scenario.world.checksum()},
                                      {"summary", summary(scenario.world)}});
        names.insert(name);
        std::cout << "Captured " << name << " at tick " << scenario.world.ticks() << std::endl;
        // Do not advance scenario here: the next real planner phase may be one tick away.
    }
    Scenario load(const fs::path &source, const std::string &input_name, const std::string &kind) {
        const auto original = bytes(source);
        const auto copy = root / "inputs" / (input_name + ".ribsave");
        require(!fs::exists(copy), "Refusing to overwrite input copy");
        const auto format = word(original, 12);
        if (format == 7)
            current_header(original);
        require(fs::copy_file(source, copy, fs::copy_options::none), "Could not copy source save");
        World world(1, 1);
        std::string error;
        require(world.load(copy, error), "Source load failed: " + error);
        require(bytes(source) == original, "Loading a copy unexpectedly modified original source");
        return Scenario(std::move(world), {{"kind", kind},
                                           {"path", fs::absolute(source).generic_string()},
                                           {"sha256", sha256(original)},
                                           {"format", format},
                                           {"copied_input", fs::relative(copy, root).generic_string()}});
    }
    void gap(const std::string &name, const std::string &reason) {
        report["gaps"].push_back({{"scenario", name}, {"reason", reason}});
        std::cerr << "GAP " << name << ": " << reason << std::endl;
    }
    template <class Function> void attempt(const std::string &name, Function function) {
        try {
            function();
        } catch (const std::exception &error) {
            gap(name, error.what());
        }
    }
    void finish() {
        std::ofstream output(root / "capture-manifest.json");
        require(bool(output), "Cannot write capture manifest");
        output << report.dump(2) << '\n';
        require(bool(output), "Could not finish capture manifest");
    }
};
ProfessionAssignment assignment(Profession profession, std::optional<CellCoord> worksite = {}) {
    ProfessionAssignment result;
    result.profession = profession;
    result.worksite = worksite;
    return result;
}
bool intent_phase(const World &world, WorkIntentPhase phase, bool attempted = false) {
    return std::any_of(world.work_intents().begin(), world.work_intents().end(), [&](const auto &i) {
        return i.resident_id == 1 && i.kind == WorkIntentKind::GatherContinuation && i.phase == phase &&
               (!attempted || i.attempts > 0);
    });
}
bool manual_cargo(const World &world) {
    return resident(world, 1).profession.manual_override &&
           std::any_of(world.logistics_tasks().begin(), world.logistics_tasks().end(), [](const auto &t) {
               return t.resident_id == 1 && t.kind == LogisticsTaskKind::HaulToStorage &&
                      t.phase == LogisticsPhase::ToDropoff && t.carry_stack_id != 0 && t.quantity == 3;
           });
}
void deferred(Capture &capture, const std::string &source, const std::string &name, bool stop, bool cargo) {
    auto scenario = capture.load(capture.path(source), name, "captured_v7_branch");
    const auto before = scenario.world.export_state();
    const auto outcomes = scenario.policy({1}, assignment(Profession::Miner), stop);
    require(outcomes.front().status == ResidentCommandStatus::Deferred, "Expected Deferred policy outcome");
    const auto &profile = resident(scenario.world, 1).profession;
    require(profile.deferred && !profile.manual_override, "Deferred policy has incorrect manual provenance");
    const auto after = scenario.world.export_state();
    require(before.path_batches == after.path_batches && before.item_stacks == after.item_stacks &&
                before.logistics_tasks == after.logistics_tasks,
            "Deferred policy changed its protected executor");
    if (!cargo)
        require(resident(scenario.world, 2).profession.manual_override,
                "Changing one group member lost another member's manual provenance");
    capture.save(scenario, name,
                 cargo ? "Deferred policy while the exact carried stack/task is protected"
                       : "Deferred policy for resident 1; unchanged shared pending batch and other members");
}
void groups(Capture &capture, bool gather) {
    const std::string prefix = gather ? "group_gather_" : "group_move_";
    // Alternative populations only address a phase absent at a tick boundary.
    for (int population : {1500, 1499, 1400}) {
        if (capture.names.contains(prefix + "search") && capture.names.contains(prefix + "reconstruct") &&
            capture.names.contains(prefix + "stage"))
            break;
        Scenario scenario(70421, population);
        CellCoord target{90, 90};
        if (gather) {
            bool found = false;
            for (int y = 85; y <= 95 && !found; ++y)
                for (int x = 85; x <= 95 && !found; ++x) {
                    const auto &tile = scenario.world.tile(x, y);
                    if (tile.terrain == Terrain::Forest && tile.amount > 0 && scenario.world.walkable(x, y)) {
                        target = {x, y};
                        found = true;
                    }
                }
            require(found, "No distant generated forest for a real scout");
            scenario.path("Move", {population}, target);
            scenario.until(
                [&] {
                    const auto &r = resident(scenario.world, population);
                    return r.x == float(target.x) && r.y == float(target.y) && r.job == Job::Idle &&
                           scenario.world.tile(target.x, target.y).explored;
                },
                "Scout did not arrive and explore the resource", 4000);
        }
        require(scenario.world.walkable(target.x, target.y), "Distant group target is blocked");
        const auto batch_id = scenario.path(gather ? "Gather" : "Move", all(scenario.world), target);
        for (int tick = 0; tick < 1200; ++tick) {
            scenario.tick();
            const auto state = scenario.world.export_state();
            const auto batch = std::find_if(state.path_batches.begin(), state.path_batches.end(),
                                            [=](const auto &b) { return b.path_batch_id == batch_id; });
            if (batch == state.path_batches.end())
                break;
            std::string phase;
            if (batch->phase == PathPlannerPhase::Search && batch->frontier_cursor > 0)
                phase = "search";
            if (batch->phase == PathPlannerPhase::Reconstruct &&
                std::any_of(batch->drafts.begin(), batch->drafts.end(),
                            [](const auto &d) { return !d.staged.empty(); }))
                phase = "reconstruct";
            if (batch->phase == PathPlannerPhase::Stage)
                phase = "stage";
            if (!phase.empty() && !capture.names.contains(prefix + phase))
                capture.save(scenario, prefix + phase,
                             "External accepted group batch " + std::to_string(batch_id) +
                                 " observed naturally in " + phase + " after tick; no outcomes drained");
        }
    }
    for (const auto *phase : {"search", "reconstruct", "stage"})
        if (!capture.names.contains(prefix + phase))
            capture.gap(
                prefix + phase,
                "Not observed at a tick boundary for populations 1500, 1499, 1400 with normal commands");
}
void manual_gather(Capture &capture) {
    Scenario scenario(70421, 2);
    constexpr CellCoord target{22, 19};
    const auto gather_route = [&](bool travelling) {
        const auto state = scenario.world.export_state();
        const auto &r = state.residents.front();
        return r.id == 1 && r.job == Job::Woodcut && r.profession.manual_override &&
               r.order_phase == OrderPhase::Active && r.pending_path_batch == 0 &&
               std::any_of(state.routes.begin(), state.routes.end(), [&](const auto &route) {
                   return route.resident_id == 1 && route.work == target &&
                          (travelling ? route.cursor < route.points.size()
                                      : route.cursor == route.points.size());
               });
    };
    scenario.path("Gather", {1}, target);
    scenario.until([&] { return gather_route(true); }, "No installed manual Gather travel route", 100);
    capture.save(scenario, "manual_gather_travelling",
                 "Manual Gather with an installed active route to the real resource before first yield");
    scenario.until(
        [&] {
            const auto &r = resident(scenario.world, 1);
            return gather_route(false) && r.progress > 0.F && r.progress < 1.F && r.wood_xp == 0;
        },
        "No useful in-progress manual gathering before first yield", 1000);
    capture.save(scenario, "manual_gather_running",
                 "Manual Gather physically at the resource, making progress before its first yield");
    scenario.until([&] { return manual_cargo(scenario.world); }, "No real manual Gather cargo", 2000);
    capture.save(scenario, "manual_gather_cargo",
                 "Resident 1 manually gathers and physically carries 3 Wood");
    scenario.until([&] { return intent_phase(scenario.world, WorkIntentPhase::Waiting); },
                   "No Waiting continuation after physical dropoff", 2000);
    require(resident(scenario.world, 1).profession.manual_override,
            "Gather continuation lost manual provenance");
    capture.save(scenario, "manual_gather_waiting",
                 "Successful physical dropoff produced a manual Waiting Work intent");
    scenario.until([&] { return intent_phase(scenario.world, WorkIntentPhase::PathPending); },
                   "No PathPending continuation", 100);
    capture.save(scenario, "manual_gather_path_pending",
                 "Manual Gather continuation has an internal Work path correlation");
    const auto pending = std::find_if(
        scenario.world.work_intents().begin(), scenario.world.work_intents().end(), [](const auto &intent) {
            return intent.resident_id == 1 && intent.kind == WorkIntentKind::GatherContinuation &&
                   intent.phase == WorkIntentPhase::PathPending;
        });
    require(pending != scenario.world.work_intents().end(), "Captured pending Gather intent disappeared");
    const auto continuation_id = pending->intent_id;
    scenario.until(
        [&] {
            return gather_route(true) &&
                   std::any_of(scenario.world.work_intents().begin(), scenario.world.work_intents().end(),
                               [&](const auto &intent) {
                                   return intent.intent_id == continuation_id &&
                                          intent.phase == WorkIntentPhase::Complete &&
                                          intent.completion == WorkCompletionReason::Started &&
                                          intent.path_batch_id == 0 && intent.target == target;
                               });
        },
        "No Complete/Started continuation with its installed active Gather route", 100);
    capture.save(scenario, "manual_gather_installed_continuation",
                 "The previously pending Gather Work intent is Complete/Started and its corresponding manual "
                 "route is active");
}
void pressure_failure(Capture &capture) {
    Scenario scenario(70421, 2);
    scenario.path("Move", {2}, {26, 24});
    scenario.until(
        [&] {
            const auto &r = resident(scenario.world, 2);
            return r.x == 26.F && r.y == 24.F && scenario.world.path_diagnostics().queued_batches == 0;
        },
        "Pressure actor did not arrive", 1000);
    for (std::size_t i = 1; i < max_path_batches - 2; ++i) {
        scenario.path("Move", {2}, {26, 24});
        scenario.until([&] { return scenario.world.path_diagnostics().queued_batches == 0; },
                       "Repeated Move remained pending", 20);
    }
    require(scenario.world.export_state().path_results.size() == max_path_batches - 2,
            "Expected 510 deliberately undrained external outcomes");
    scenario.path("Gather", {1}, {22, 19});
    scenario.until(
        [&] {
            if (scenario.world.inventory().carried.wood != 3)
                return false;
            const auto state = scenario.world.export_state();
            const auto route = std::find_if(state.routes.begin(), state.routes.end(),
                                            [](const auto &r) { return r.resident_id == 1; });
            const auto &r = state.residents.front();
            return route != state.routes.end() && route->cursor == route->points.size() &&
                   r.layer == WorldLayer::Surface && std::abs(r.x - 24.F) + std::abs(r.y - 24.F) == 1.F;
        },
        "Manual cargo did not reach the actual dropoff boundary", 2000);
    scenario.path("Move", {2}, {26, 24});
    scenario.until([&] { return intent_phase(scenario.world, WorkIntentPhase::Waiting, true); },
                   "Queue pressure did not leave a Waiting continuation", 30);
    require(scenario.world.export_state().path_results.size() == max_path_batches,
            "Pressure must come from 512 real external outcomes");
    capture.save(scenario, "manual_gather_pressure_waiting",
                 "Manual continuation waits behind 512 retained external outcomes");
    scenario.until([&] { return intent_phase(scenario.world, WorkIntentPhase::Failed); },
                   "Bounded admission did not fail", 1400);
    require(scenario.world.inventory().stored.wood == 83 && scenario.world.inventory().carried.wood == 0 &&
                !resident(scenario.world, 1).profession.manual_override,
            "Terminal failure changed completed delivery or manual status");
    capture.save(
        scenario, "manual_gather_failed",
        "Terminal admission failure after successful delivery; automatic Unassigned does not revive it");
}
void skills_practice(Capture &capture) {
    Scenario scenario(70421, 1);
    scenario.policy({1}, assignment(Profession::Woodcutter, CellCoord{22, 19}));
    scenario.until([&] { return resident(scenario.world, 1).wood_xp >= 1; }, "Woodcutter gained no real XP",
                   3000);
    scenario.policy({1}, assignment(Profession::Miner, CellCoord{20, 19}));
    scenario.until([&] { return resident(scenario.world, 1).mining_xp >= 1; }, "Miner gained no real XP",
                   3000);
    scenario.policy({1}, {}, true);
    scenario.until([&] { return !resident(scenario.world, 1).profession.deferred; },
                   "Stop did not reach a safe boundary", 2000);
    for (int i = 0; i < 6000; ++i)
        scenario.tick();
    const auto mining_before = resident(scenario.world, 1).mining_xp;
    scenario.policy({1}, assignment(Profession::Miner, CellCoord{20, 19}));
    scenario.until([&] { return resident(scenario.world, 1).mining_xp > mining_before; },
                   "Miner did not resume useful work", 3000);
    require(resident(scenario.world, 1).skill_practice[1].familiarity_debt > 0,
            "Natural absence did not leave stored familiarity debt");
    scenario.policy({1}, assignment(Profession::Builder));
    scenario.house({26, 26});
    scenario.until(
        [&] {
            const auto &r = resident(scenario.world, 1);
            return r.additional_xp[0] > 0 && r.skill_practice[3].experience_remainder > 0;
        },
        "Builder did not produce independent XP and fractional credit", 5000);
    const auto &r = resident(scenario.world, 1);
    require(r.wood_xp > 0 && r.mining_xp > 0 && r.skill_practice[1].familiarity_debt > 0 &&
                !r.profession.manual_override,
            "Expected independent natural skill practice and automatic provenance");
    capture.save(scenario, "profession_skills_practice",
                 "Natural wood/mining/construction XP, fractional construction credit and stored mining "
                 "familiarity debt");
}
void paid_builder(Capture &capture) {
    Scenario scenario(70421, 10);
    CellCoord site{};
    bool found = false;
    for (int y = 20; y <= 30 && !found; ++y)
        for (int x = 20; x <= 30 && !found; ++x)
            if (scenario.world.can_build(BuildingKind::House, x, y)) {
                site = {x, y};
                found = true;
            }
    require(found, "No natural House site");
    scenario.house(site);
    scenario.until(
        [&] {
            return std::any_of(scenario.world.construction_orders().begin(),
                               scenario.world.construction_orders().end(),
                               [](const auto &s) { return s.phase == ConstructionPhase::Work; });
        },
        "House never reached paid Work");
    scenario.path("Move", all(scenario.world), {40, 40});
    scenario.until(
        [&] {
            const auto state = scenario.world.export_state();
            for (const auto &r : state.residents)
                if (r.job == Job::Build && !r.profession.manual_override &&
                    r.order_phase == OrderPhase::Active)
                    for (const auto &route : state.routes)
                        if (route.resident_id == r.id && route.cursor < route.points.size())
                            return true;
            return false;
        },
        "No automatic replacement builder physically travelled to the paid worksite");
    require(scenario.world.inventory().total.wood == 50 && scenario.world.inventory().total.stone == 25,
            "Paid construction charged materials twice");
    capture.save(
        scenario, "paid_builder_travelling",
        "Automatic replacement builder travelling to already paid Work construction; no manual override");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc <= 2, "Usage: capture.exe [fresh-output-root] (run from repository root)");
        require(engine_version == "0.9.0" && rules_version == 6 && protocol_version == 6 &&
                    generator_version == 2 && default_content_hash() == content,
                "Compile and link against the unchanged frozen d3bc785 writer only");
        Capture capture(argc == 2 ? fs::path(argv[1]) : fs::path(".deps/v7-capture-d3bc785"));
        capture.attempt("neutral", [&] {
            Scenario s(70421, 10);
            capture.save(s, "neutral", "Untouched initial Unassigned population");
        });
        capture.attempt("manual_move", [&] {
            Scenario s(70421, 3);
            s.path("Move", {1}, {26, 24});
            s.until(
                [&] {
                    const auto &r = resident(s.world, 1);
                    return r.job == Job::Move && r.profession.manual_override &&
                           (r.x != std::floor(r.x) || r.y != std::floor(r.y));
                },
                "No physical fractional manual movement", 100);
            capture.save(s, "manual_move", "Manual Move in physical movement, after planner completion");
        });
        capture.attempt("group_move", [&] { groups(capture, false); });
        capture.attempt("group_move_deferred_stop", [&] {
            deferred(capture, "group_move_search", "group_move_deferred_stop", true, false);
        });
        capture.attempt("group_gather", [&] { groups(capture, true); });
        capture.attempt("group_gather_deferred_assign", [&] {
            deferred(capture, "group_gather_search", "group_gather_deferred_assign", false, false);
        });
        capture.attempt("manual_gather", [&] { manual_gather(capture); });
        capture.attempt("deferred_assign_cargo", [&] {
            deferred(capture, "manual_gather_cargo", "deferred_assign_cargo", false, true);
        });
        capture.attempt("deferred_stop_cargo",
                        [&] { deferred(capture, "manual_gather_cargo", "deferred_stop_cargo", true, true); });
        capture.attempt("manual_gather_failed", [&] { pressure_failure(capture); });
        capture.attempt("legacy_v6_carry", [&] {
            auto s = capture.load("tests/fixtures/world_save_v6_carry.ribsave", "legacy_v6_carry",
                                  "genuine_v6_migrated_by_frozen_v7");
            require(std::none_of(s.world.residents().begin(), s.world.residents().end(),
                                 [](const auto &r) {
                                     return r.profession.manual_override ||
                                            r.profession.assignment.profession != Profession::Unassigned;
                                 }) &&
                        s.world.inventory().carried == Resources{0, 10, 0} &&
                        std::any_of(s.world.item_stacks().begin(), s.world.item_stacks().end(),
                                    [](const auto &stack) {
                                        return stack.stack_id == 4 && stack.item == ItemKind::Stone &&
                                               stack.quantity == 10 &&
                                               stack.location.kind == ItemLocationKind::Carried &&
                                               stack.location.entity_id == 4;
                                    }) &&
                        std::any_of(s.world.logistics_tasks().begin(), s.world.logistics_tasks().end(),
                                    [](const auto &task) {
                                        return task.task_id == 4 && task.resident_id == 4 &&
                                               task.kind == LogisticsTaskKind::DeliverConstruction &&
                                               task.phase == LogisticsPhase::ToDropoff &&
                                               task.carry_stack_id == 4 && task.path_batch_id == 7 &&
                                               task.item == ItemKind::Stone && task.quantity == 10;
                                    }),
                    "Legacy migration lost neutral carrying provenance");
            capture.save(
                s, "legacy_v6_carry",
                "Real v6 active cargo migrated to v7 with neutral profile, without new commands or ticks");
        });
        capture.attempt("profession_auto", [&] {
            auto s = capture.load("artifacts/profession-gpu-0.9.0/uk-miner.save", "profession_auto",
                                  "genuine_0.9_gpu");
            const auto &r = resident(s.world, 1);
            const bool executor =
                r.job == Job::Mine ||
                std::any_of(s.world.work_intents().begin(), s.world.work_intents().end(),
                            [](const auto &w) {
                                return w.resident_id == 1 && w.kind == WorkIntentKind::GatherContinuation &&
                                       w.job == Job::Mine &&
                                       (w.phase == WorkIntentPhase::Waiting ||
                                        w.phase == WorkIntentPhase::PathPending);
                            }) ||
                std::any_of(
                    s.world.logistics_tasks().begin(), s.world.logistics_tasks().end(), [](const auto &t) {
                        return t.resident_id == 1 && t.kind == LogisticsTaskKind::HaulToStorage &&
                               t.resume_job == Job::Mine &&
                               (t.phase == LogisticsPhase::ToPickup || t.phase == LogisticsPhase::ToDropoff);
                    });
            require(r.wood_xp > 0 && r.mining_xp > 0 && !r.profession.manual_override &&
                        !r.profession.deferred && r.profession.assignment.automation_enabled && executor &&
                        r.profession.assignment.profession == Profession::Miner,
                    "GPU source is not an automatic experienced Miner");
            capture.save(s, "profession_auto",
                         "Genuine 0.9 GPU Miner with retained independent wood/mining XP");
        });
        capture.attempt("profession_cursor", [&] {
            Scenario s(70421, 33);
            s.policy(all(s.world), assignment(Profession::Woodcutter));
            s.tick();
            require(s.world.export_state().profession_cursor_id > 1,
                    "Bounded admission did not advance the profession cursor");
            capture.save(s, "profession_cursor",
                         "Automatic Woodcutters after one bounded profession admission tick");
        });
        capture.attempt("profession_skills_practice", [&] { skills_practice(capture); });
        capture.attempt("paid_builder_travelling", [&] { paid_builder(capture); });
        capture.finish();
        std::cout << capture.report["fixtures"].size() << " fixtures; " << capture.report["gaps"].size()
                  << " explicitly reported gaps. Verification is a separate step.\n";
        return capture.report["gaps"].empty() ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

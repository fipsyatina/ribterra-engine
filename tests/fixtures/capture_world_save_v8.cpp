// Genuine format 8 capture: compile ONLY with the frozen 5476c90 headers/libs.
// WorldState is observed for predicates; it is never edited or imported.
#include "ribterra/foundation.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <array>
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
constexpr auto commit = "5476c90676dc3152ef044e80fc0d12884d994f99";
constexpr auto content = "5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606";
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
    require(data.size() >= 96 && word(data, 12) == 8 && word(data, 16) == 7 && word(data, 20) == 2,
            "Expected genuine format 8/rules 7/generator 2 container");
    constexpr char hex[] = "0123456789abcdef";
    std::string hash;
    for (std::size_t i = 64; i < 96; ++i) {
        hash += hex[data[i] >> 4U];
        hash += hex[data[i] & 15U];
    }
    require(hash == content, "Unexpected format 8 gameplay hash");
}
Json writer() {
    return {{"source_commit", commit},
            {"engine", engine_version},
            {"save_format", 8},
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
    void build(BuildingKind kind, CellCoord site) {
        const auto result = world.build(kind, site.x, site.y, site.layer);
        commands.push_back({{"op", "Build"},
                            {"kind", static_cast<int>(kind)},
                            {"tick", world.ticks()},
                            {"target", coord(site)},
                            {"accepted", result.accepted},
                            {"path_batch_id", result.path_batch_id}});
        require(result.accepted, "Build rejected: " + result.message);
    }
    std::vector<ResidentCommandOutcome> manual(const std::vector<int> &ids, ManualOrderRequest request,
                                               ManualQueueMode mode = ManualQueueMode::Replace) {
        const ManualOrderIssuer issuer{17, static_cast<std::uint64_t>(commands.size()) + 1};
        const auto result = world.issue_manual_order(ids, request, mode, issuer);
        Json record{{"op", "ManualOrder"},
                    {"tick", world.ticks()},
                    {"residents", ids},
                    {"kind", static_cast<int>(request.kind)},
                    {"target", request.target ? coord(*request.target) : Json(nullptr)},
                    {"gather_completion", static_cast<int>(request.gather_completion)},
                    {"queue_mode", static_cast<int>(mode)},
                    {"issuer", {{"player_id", issuer.player_id}, {"request_id", issuer.request_id}}},
                    {"outcomes", Json::array()}};
        for (const auto &r : result)
            record["outcomes"].push_back({{"resident_id", r.resident_id},
                                          {"status", static_cast<int>(r.status)},
                                          {"failure", static_cast<int>(r.failure)},
                                          {"order_id", r.order_id}});
        commands.push_back(std::move(record));
        require(result.size() == ids.size() &&
                    std::all_of(result.begin(), result.end(),
                                [](const auto &r) {
                                    return r.status != ResidentCommandStatus::Rejected && r.order_id != 0;
                                }),
                "Manual command rejected");
        return result;
    }
    std::vector<ResidentCommandOutcome> control(const char *op, std::uint64_t order_id = 0) {
        const auto result = std::string_view(op) == "CancelOrder" ? world.cancel_manual_order(order_id)
                                                                  : world.release_manual_control({1});
        Json record{{"op", op},
                    {"tick", world.ticks()},
                    {"order_id", order_id},
                    {"residents", {1}},
                    {"outcomes", Json::array()}};
        for (const auto &r : result)
            record["outcomes"].push_back({{"resident_id", r.resident_id},
                                          {"status", static_cast<int>(r.status)},
                                          {"failure", static_cast<int>(r.failure)},
                                          {"order_id", r.order_id}});
        commands.push_back(std::move(record));
        require(result.size() == 1 && result.front().status != ResidentCommandStatus::Rejected,
                "Control command rejected");
        return result;
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
                {"buildings", Json::array()},
                {"manual_orders", Json::array()},
                {"manual_order_cursor_id", state.manual_order_cursor_id},
                {"next_manual_order_id", state.next_manual_order_id},
                {"ground_food", Json::array()},
                {"food_logistics", Json::array()},
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
    for (const auto &batch : state.path_batches) {
        const auto resolved =
            std::count_if(batch.drafts.begin(), batch.drafts.end(), [](const auto &d) { return d.resolved; });
        const auto committed = std::count_if(batch.drafts.begin(), batch.drafts.end(), [](const auto &d) {
            return d.resolved && d.failure == PathFailure::None;
        });
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
                                          {"legacy_atomic", batch.legacy_atomic},
                                          {"resolved_drafts", resolved},
                                          {"successfully_resolved_drafts", committed},
                                          {"work_units", batch.work_units_done}});
    }
    for (const auto &o : state.manual_orders)
        result["manual_orders"].push_back(
            {{"order_id", o.order_id},
             {"resident_id", o.resident_id},
             {"kind", static_cast<int>(o.request.kind)},
             {"target", o.request.target ? coord(*o.request.target) : Json(nullptr)},
             {"gather_completion", static_cast<int>(o.request.gather_completion)},
             {"phase", static_cast<int>(o.phase)},
             {"failure", static_cast<int>(o.failure)},
             {"completion", static_cast<int>(o.completion)},
             {"legacy_compatibility", o.legacy_compatibility},
             {"executor", static_cast<int>(o.executor)},
             {"executor_id", o.executor_id}});
    for (const auto &b : state.buildings)
        result["buildings"].push_back({{"id", b.id},
                                       {"kind", static_cast<int>(b.kind)},
                                       {"site", coord({b.x, b.y, b.layer})},
                                       {"progress", b.progress}});
    for (const auto &s : state.item_stacks)
        if (s.item == ItemKind::Food && s.location.kind == ItemLocationKind::Ground)
            result["ground_food"].push_back(
                {{"stack_id", s.stack_id}, {"cell", coord(s.location.cell)}, {"quantity", s.quantity}});
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
    for (const auto &task : state.logistics_tasks) {
        if (task.item == ItemKind::Food)
            result["food_logistics"].push_back({{"task_id", task.task_id},
                                                {"resident_id", task.resident_id},
                                                {"kind", static_cast<int>(task.kind)},
                                                {"phase", static_cast<int>(task.phase)},
                                                {"source_stack_id", task.source_stack_id},
                                                {"carry_stack_id", task.carry_stack_id},
                                                {"reservation_id", task.reservation_id},
                                                {"destination_building_id", task.destination_building_id},
                                                {"quantity", task.quantity},
                                                {"path_batch_id", task.path_batch_id}});
        if (task.carry_stack_id)
            result["cargo"].push_back({{"task_id", task.task_id},
                                       {"resident_id", task.resident_id},
                                       {"kind", static_cast<int>(task.kind)},
                                       {"phase", static_cast<int>(task.phase)},
                                       {"carry_stack_id", task.carry_stack_id},
                                       {"quantity", task.quantity},
                                       {"item", static_cast<int>(task.item)},
                                       {"path_batch_id", task.path_batch_id}});
    }
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
        require(!fs::exists(root / "inputs") || fs::is_empty(root / "inputs"),
                "Refusing to reuse nonempty input copies; choose a fresh output root");
        fs::create_directories(root / "fixtures");
        fs::create_directories(root / "inputs");
    }
    fs::path path(const std::string &name) const {
        return root / "fixtures" / ("world_save_v8_" + name + ".ribsave");
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
    Scenario load(const fs::path &source, const std::string &input_name, const std::string &kind,
                  std::uint32_t expected_format = 8) {
        const auto original = bytes(source);
        const auto copy = root / "inputs" / (input_name + ".ribsave");
        require(!fs::exists(copy), "Refusing to overwrite input copy");
        const auto format = word(original, 12);
        require(format == expected_format, "Source file has an unexpected original format");
        if (format == 8)
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
ManualOrderRequest move_to(CellCoord target) {
    return {ManualOrderKind::Move, target};
}
ManualOrderRequest gather_at(GatherCompletion completion) {
    return {ManualOrderKind::Gather, CellCoord{22, 19}, completion};
}
const ManualOrderState &order(const World &world, std::uint64_t id) {
    const auto &orders = world.manual_orders();
    const auto it =
        std::find_if(orders.begin(), orders.end(), [=](const auto &o) { return o.order_id == id; });
    require(it != orders.end(), "Expected manual order is missing");
    return *it;
}
bool active_task(const LogisticsTaskState &task) {
    return task.phase != LogisticsPhase::Complete && task.phase != LogisticsPhase::Failed;
}
bool manual_cargo(const World &world) {
    return world.inventory().carried.wood == 3 &&
           std::any_of(world.logistics_tasks().begin(), world.logistics_tasks().end(), [](const auto &task) {
               return task.resident_id == 1 && task.kind == LogisticsTaskKind::HaulToStorage &&
                      task.phase == LogisticsPhase::ToDropoff && task.carry_stack_id &&
                      task.item == ItemKind::Wood && task.quantity == 3;
           });
}
void unchanged_physical(const WorldState &before, const WorldState &after) {
    require(before.item_stacks == after.item_stacks && before.item_reservations == after.item_reservations &&
                before.logistics_tasks == after.logistics_tasks && before.path_batches == after.path_batches,
            "Boundary command changed protected cargo/task/reservation/path state");
}
void manual_queued_and_move(Capture &capture) {
    Scenario s(70421, 1);
    const auto head = s.manual({1}, move_to({26, 25})).front().order_id;
    s.manual({1}, gather_at(GatherCompletion::OneDelivery), ManualQueueMode::Append);
    require(s.world.manual_orders().size() == 2 &&
                std::all_of(s.world.manual_orders().begin(), s.world.manual_orders().end(),
                            [](const auto &o) {
                                return o.phase == ManualOrderPhase::Queued &&
                                       o.executor == ManualExecutorKind::None &&
                                       o.issued_tick == std::uint64_t{0};
                            }),
            "Expected two genuine queued requests issued at tick zero");
    capture.save(s, "manual_queued",
                 "New Move head and OneDelivery Gather tail, explicit issuer, issued at tick zero");
    s.until(
        [&] {
            const auto &r = resident(s.world, 1);
            return order(s.world, head).phase == ManualOrderPhase::Active && r.job == Job::Move &&
                   (r.x != std::floor(r.x) || r.y != std::floor(r.y));
        },
        "Move never reached fractional physical movement", 1000);
    capture.save(s, "active_move",
                 "Active manual Move route at a fractional resident position, with queued Gather tail");
}
void shared_paths(Capture &capture) {
    Scenario s(70421, 1500);
    const auto admitted = s.manual(all(s.world), move_to({90, 90}));
    std::uint64_t batch_id = 0;
    bool cancelled = false;
    for (int tick = 0; tick < 300; ++tick) {
        s.tick();
        if (!batch_id && order(s.world, admitted[1].order_id).executor == ManualExecutorKind::Path)
            batch_id = order(s.world, admitted[1].order_id).executor_id;
        const auto state = s.world.export_state();
        const auto it = std::find_if(state.path_batches.begin(), state.path_batches.end(),
                                     [=](const auto &b) { return b.path_batch_id == batch_id; });
        if (it == state.path_batches.end()) {
            if (batch_id)
                break;
            continue;
        }
        require(it->command == PathCommandKind::Manual && !it->legacy_atomic &&
                    it->resident_ids.size() == 1500,
                "Expected one actual shared partial Manual batch");
        if (it->phase == PathPlannerPhase::Search && !capture.names.contains("shared_search"))
            capture.save(s, "shared_search",
                         "1500-actor new Manual batch in Search, correlated to admitted order IDs");
        if (it->phase == PathPlannerPhase::Reconstruct && !capture.names.contains("shared_reconstruct"))
            capture.save(s, "shared_reconstruct",
                         "Shared Manual reconstruction with a genuinely cancelled first participant");
        const auto successful = std::count_if(it->drafts.begin(), it->drafts.end(), [](const auto &d) {
            return d.resolved && d.failure == PathFailure::None;
        });
        const auto pending =
            std::count_if(it->drafts.begin(), it->drafts.end(), [](const auto &d) { return !d.resolved; });
        if (it->phase == PathPlannerPhase::Stage && successful > 0 && pending > 0 &&
            !capture.names.contains("shared_partial_stage")) {
            require(std::any_of(s.world.residents().begin(), s.world.residents().end(),
                                [](const auto &r) { return r.job == Job::Move; }),
                    "Resolved Stage drafts have not installed real movement");
            capture.save(s, "shared_partial_stage",
                         "Actual partially committed Stage: successful installed routes coexist with "
                         "unresolved peers");
        }
        if (!cancelled) {
            s.control("CancelOrder", admitted.front().order_id);
            require(order(s.world, admitted.front().order_id).completion == ManualOrderCompletion::Cancelled,
                    "Single cancellation did not produce completion8");
            cancelled = true;
        }
        if (capture.names.contains("shared_search") && capture.names.contains("shared_reconstruct") &&
            capture.names.contains("shared_partial_stage"))
            break;
    }
    for (const auto *name : {"shared_search", "shared_reconstruct", "shared_partial_stage"})
        if (!capture.names.contains(name))
            capture.gap(name,
                        "The requested natural phase was not observable at a tick boundary within 300 ticks");
}
void gathering(Capture &capture, GatherCompletion completion) {
    const bool once = completion == GatherCompletion::OneDelivery;
    Scenario s(70421, 1);
    const auto head = s.manual({1}, gather_at(completion)).front().order_id;
    s.manual({1}, move_to({27, 25}), ManualQueueMode::Append);
    s.until([&] { return manual_cargo(s.world); }, "Gather did not create Wood3 physical cargo", 2000);
    require(order(s.world, head).executor == ManualExecutorKind::Logistics &&
                order(s.world, head).request.gather_completion == completion,
            "Cargo is not bound to the intended manual Gather head");
    capture.save(s, once ? "one_delivery_cargo" : "until_exhausted_cargo",
                 once ? "OneDelivery head owns real Wood3 cargo; a Move tail waits"
                      : "UntilExhausted head owns real Wood3 cargo; a Move tail waits");
    if (once)
        return;
    s.until(
        [&] {
            const auto &o = order(s.world, head);
            return o.executor == ManualExecutorKind::Work &&
                   std::any_of(s.world.work_intents().begin(), s.world.work_intents().end(),
                               [&](const auto &w) {
                                   return w.intent_id == o.executor_id && w.resident_id == 1 &&
                                          w.kind == WorkIntentKind::GatherContinuation &&
                                          w.phase == WorkIntentPhase::PathPending;
                               });
        },
        "UntilExhausted never reached pending Work continuation", 2000);
    require(s.world.inventory().carried.wood == 0 && s.world.inventory().stored.wood == 83,
            "Pending continuation lost the completed first delivery");
    capture.save(s, "until_exhausted_pending_work",
                 "Completed Wood3 delivery and manual UntilExhausted head bound to pending internal Work, "
                 "with Move tail");
}
enum class BoundaryAction { Replace, CancelWithTail, CancelWithoutTail, Release, Stop, Assign };
void cargo_boundary(Capture &capture, const std::string &name, BoundaryAction action) {
    auto s = capture.load(capture.path("until_exhausted_cargo"), name, "captured_v8_branch");
    require(s.world.manual_orders().size() == 2 && manual_cargo(s.world),
            "Expected source head, tail and physical cargo");
    const auto head = s.world.manual_orders().front().order_id;
    const auto tail = s.world.manual_orders().back().order_id;
    const auto before = s.world.export_state();
    if (action == BoundaryAction::Replace) {
        const auto r = s.manual({1}, move_to({26, 25})).front();
        require(r.status == ResidentCommandStatus::Deferred &&
                    order(s.world, r.order_id).phase == ManualOrderPhase::WaitingBoundary &&
                    order(s.world, r.order_id).executor == ManualExecutorKind::Logistics,
                "Replacement did not retain the physical boundary");
    } else if (action == BoundaryAction::CancelWithTail || action == BoundaryAction::CancelWithoutTail) {
        if (action == BoundaryAction::CancelWithoutTail)
            s.control("CancelOrder", tail);
        require(s.control("CancelOrder", head).front().status == ResidentCommandStatus::Deferred,
                "Cargo cancellation did not defer at the safe boundary");
        require(order(s.world, head).completion == ManualOrderCompletion::Cancelled,
                "Single-order cancellation lost completion8");
        if (action == BoundaryAction::CancelWithTail)
            require(order(s.world, tail).phase == ManualOrderPhase::WaitingBoundary &&
                        order(s.world, tail).executor == ManualExecutorKind::Logistics,
                    "Cancelled head did not leave its tail waiting on the same physical task");
        else
            require(resident(s.world, 1).profession.deferred &&
                        !resident(s.world, 1).profession.manual_override,
                    "Cancelled final order did not retain a neutral deferred delivery");
    } else if (action == BoundaryAction::Release) {
        require(s.control("ReleaseControl").front().status == ResidentCommandStatus::Deferred &&
                    order(s.world, head).completion == ManualOrderCompletion::Released,
                "Release did not retain distinct Released completion at cargo boundary");
    } else {
        const bool stop = action == BoundaryAction::Stop;
        require(s.policy({1}, assignment(Profession::Miner), stop).front().status ==
                    ResidentCommandStatus::Deferred,
                "Policy change did not defer under cargo");
        require(order(s.world, head).completion ==
                    (stop ? ManualOrderCompletion::Stopped : ManualOrderCompletion::Replaced),
                "Deferred policy has wrong terminal manual reason");
    }
    unchanged_physical(before, s.world.export_state());
    capture.save(s, name,
                 "Normal command at real physical Wood3 boundary; existing stacks, reservations, tasks and "
                 "paths are unchanged");
}
void cancel_pending_work(Capture &capture, bool tail) {
    const std::string name =
        tail ? "cancelled_pending_work_with_tail" : "cancelled_pending_work_without_tail";
    auto s = capture.load(capture.path("until_exhausted_pending_work"), name, "captured_v8_branch");
    require(s.world.manual_orders().size() == 2, "Expected two-order pending Work source");
    const auto head_id = s.world.manual_orders().front().order_id;
    const auto tail_id = s.world.manual_orders().back().order_id;
    if (!tail)
        s.control("CancelOrder", tail_id);
    const auto stacks = s.world.item_stacks();
    require(s.control("CancelOrder", head_id).front().status == ResidentCommandStatus::Applied,
            "Internal Work cancellation remained deferred after completed delivery");
    require(order(s.world, head_id).completion == ManualOrderCompletion::Cancelled &&
                s.world.item_stacks() == stacks && s.world.path_diagnostics().queued_batches == 0,
            "Pending Work cancellation lost completion8, delivery or planner retirement");
    if (tail)
        require(order(s.world, tail_id).phase == ManualOrderPhase::Queued,
                "Next order did not become queued");
    else
        require(!resident(s.world, 1).profession.deferred,
                "No physical boundary exists after Work cancellation");
    capture.save(s, name,
                 "Single-order completion8 after delivered cargo: internal Work/path retired, stored "
                 "inventory preserved");
}
std::uint64_t ground_food(const World &world, CellCoord site) {
    std::uint64_t quantity = 0;
    for (const auto &s : world.item_stacks())
        if (s.item == ItemKind::Food && s.location.kind == ItemLocationKind::Ground &&
            s.location.cell == site)
            quantity += s.quantity;
    return quantity;
}
void passive_farm(Capture &capture) {
    Scenario s(70421, 10);
    std::optional<CellCoord> site;
    for (int y = 20; y <= 30 && !site; ++y)
        for (int x = 20; x <= 30 && !site; ++x)
            if (s.world.can_build(BuildingKind::Farm, x, y))
                site = CellCoord{x, y};
    require(site.has_value(), "No natural Farm site");
    s.build(BuildingKind::Farm, *site);
    const auto construction = [&]() -> const ConstructionOrderState & {
        const auto &orders = s.world.construction_orders();
        const auto found = std::find_if(orders.begin(), orders.end(), [&](const auto &o) {
            return o.kind == BuildingKind::Farm && o.site == *site;
        });
        require(found != orders.end(), "Actual Farm construction order is missing");
        return *found;
    };
    const auto building_id = construction().building_id;
    require(construction().phase == ConstructionPhase::Planned &&
                construction().required == World::cost(BuildingKind::Farm) &&
                construction().delivered == Resources{0, 0, 0},
            "Accepted Farm did not retain its planned physical-material order");
    capture.save(s, "farm_planned", "Normal accepted Farm command with its real Planned construction order");
    s.until([&] { return construction().phase == ConstructionPhase::Delivery; },
            "Farm did not expose its physical Delivery phase", 1000);
    require(construction().building_id == building_id &&
                std::any_of(s.world.logistics_tasks().begin(), s.world.logistics_tasks().end(),
                            [&](const auto &t) {
                                return t.kind == LogisticsTaskKind::DeliverConstruction &&
                                       t.destination_building_id == building_id && active_task(t);
                            }),
            "Farm Delivery lost its original construction/task correlation");
    capture.save(s, "farm_delivery", "Same Farm in Delivery with actual reserved construction hauls");
    s.until([&] { return construction().phase == ConstructionPhase::Work; },
            "Farm did not expose its already-paid Work phase", 5000);
    require(construction().building_id == building_id &&
                construction().required == World::cost(BuildingKind::Farm) &&
                construction().delivered == construction().required &&
                std::any_of(s.world.buildings().begin(), s.world.buildings().end(),
                            [&](const auto &b) {
                                return b.id == building_id && b.kind == BuildingKind::Farm &&
                                       b.progress >= 0 && b.progress < 1;
                            }),
            "Farm Work is not an unfinished building with all required materials already delivered");
    capture.save(s, "farm_paid_work",
                 "Same unfinished Farm in Work; required and delivered equal its fully paid material cost");
    s.until(
        [&] {
            return std::any_of(s.world.buildings().begin(), s.world.buildings().end(), [&](const auto &b) {
                return b.kind == BuildingKind::Farm && b.x == site->x && b.y == site->y && b.progress >= 1;
            });
        },
        "Physical Farm construction did not complete", 5000);
    capture.save(s, "farm_built",
                 "Farm completed through reserved physical materials, delivery and construction work");
    const auto pulse = (s.world.ticks() / 400 + 1) * 400;
    // New manual policy prevents an idle worker from being assigned to the Farm or its output.
    s.manual(all(s.world), move_to({90, 90}));
    s.until([&] { return s.world.ticks() == pulse - 1; }, "Could not reach the actual pre-production tick",
            400);
    require(std::none_of(s.world.residents().begin(), s.world.residents().end(),
                         [](const auto &r) { return r.job == Job::Build; }) &&
                std::any_of(s.world.residents().begin(), s.world.residents().end(),
                            [](const auto &r) {
                                return r.job == Job::Move || r.order_phase == OrderPhase::PathPending;
                            }),
            "Expected busy residents away from Farm work at the production boundary");
    const auto before = ground_food(s.world, *site);
    capture.save(s, "farm_busy_before_400",
                 "Completed passive Farm at tick mod400=399 while residents execute manual Move elsewhere");
    s.tick();
    require(s.world.ticks() % 400 == 0 && ground_food(s.world, *site) == before + 8,
            "Original passive Farm did not create exactly Food8 on its ground cell at the production pulse");
    capture.save(s, "farm_busy_after_400",
                 "Original passive Farm produced Ground Food8 despite busy workers; food consumption may "
                 "separately reduce stored Food");
}
void farm_food_delivery(Capture &capture) {
    auto s = capture.load(capture.path("farm_busy_after_400"), "farm_food_delivery", "captured_v8_branch");
    const auto &stacks = s.world.item_stacks();
    const auto source = std::find_if(stacks.begin(), stacks.end(), [&](const auto &stack) {
        return stack.item == ItemKind::Food && stack.location.kind == ItemLocationKind::Ground &&
               stack.quantity == 8 &&
               std::any_of(s.world.buildings().begin(), s.world.buildings().end(), [&](const auto &b) {
                   return b.kind == BuildingKind::Farm && b.progress >= 1 &&
                          stack.location.cell == CellCoord{b.x, b.y, b.layer};
               });
    });
    require(source != stacks.end(), "Missing the genuine passive Farm's exact Ground Food8 output");
    const auto source_id = source->stack_id;
    const auto pulse = (s.world.ticks() / 400 + 1) * 400;
    s.policy(all(s.world), {}, true);
    s.policy({1}, assignment(Profession::Hauler));
    std::uint64_t task_id{};
    s.until(
        [&] {
            const auto &tasks = s.world.logistics_tasks();
            const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto &t) {
                return t.kind == LogisticsTaskKind::HaulToStorage && t.source_stack_id == source_id &&
                       t.item == ItemKind::Food && t.quantity == 8 && t.phase == LogisticsPhase::ToDropoff &&
                       t.carry_stack_id != 0;
            });
            if (task == tasks.end())
                return false;
            task_id = task->task_id;
            return true;
        },
        "Normal Hauler did not pick up the actual Farm Food8 before another production pulse", 399);
    require(s.world.ticks() < pulse && s.world.inventory().carried.food == 8,
            "Farm cargo observation crossed the next harvest or lost the exact Food8 quantity");
    capture.save(s, "farm_food_cargo",
                 "Hauler carries exact Food8 from the tick400 Farm output; task source_stack_id preserves "
                 "the original ground-stack provenance");
    const auto task = [&]() -> const LogisticsTaskState & {
        const auto &tasks = s.world.logistics_tasks();
        const auto found =
            std::find_if(tasks.begin(), tasks.end(), [&](const auto &t) { return t.task_id == task_id; });
        require(found != tasks.end(), "Farm Food delivery task disappeared before its receipt");
        return *found;
    };
    const auto start_food = s.world.inventory().stored.food;
    int consumed = 0;
    while (task().phase != LogisticsPhase::Complete && s.world.ticks() + 1 < pulse) {
        require(task().phase == LogisticsPhase::ToDropoff, "Farm Food haul failed before Storage delivery");
        int consumption = 0;
        if ((s.world.ticks() + 1) % 200 == 0)
            consumption =
                static_cast<int>(std::count_if(s.world.residents().begin(), s.world.residents().end(),
                                               [](const auto &r) { return r.health > 0; }));
        require(s.world.inventory().stored.food >= consumption,
                "Farm delivery consumption oracle requires available stored Food for every live resident");
        const auto before = s.world.inventory().stored.food;
        s.tick();
        consumed += consumption;
        require(s.world.inventory().stored.food ==
                    before - consumption + (task().phase == LogisticsPhase::Complete ? 8 : 0),
                "Stored Food changed outside the exact Farm delivery and ordinary resident consumption");
    }
    require(task().phase == LogisticsPhase::Complete && task().source_stack_id == source_id &&
                task().quantity == 8 && s.world.inventory().carried.food == 0 &&
                s.world.inventory().stored.food == start_food + 8 - consumed,
            "Same Farm Food8 task did not complete exactly once into Storage before another harvest");
    capture.save(s, "farm_food_stored",
                 "Same Farm Food8 task completed into Storage exactly once, allowing only ordinary "
                 "200-tick stored-Food consumption; no second passive harvest occurred");
}
void farmer_unsupported(Capture &capture) {
    auto s = capture.load(capture.path("farm_built"), "farmer_no_compatible_work", "captured_v8_branch");
    const auto farm = std::find_if(s.world.buildings().begin(), s.world.buildings().end(), [](const auto &b) {
        return b.kind == BuildingKind::Farm && b.progress >= 1;
    });
    require(farm != s.world.buildings().end(), "Completed Farm source missing");
    const auto before = resident(s.world, 1).farming_xp;
    s.policy({1}, assignment(Profession::Farmer, CellCoord{farm->x, farm->y, farm->layer}));
    s.until(
        [&] {
            const auto &p = resident(s.world, 1).profession;
            return !p.deferred && !p.manual_override &&
                   p.wait_reason == ProfessionWaitReason::NoCompatibleWork;
        },
        "Original Farmer did not settle at NoCompatibleWork", 1000);
    require(resident(s.world, 1).farming_xp == before, "Assigning Farmer invented farming XP");
    capture.save(s, "farmer_no_compatible_work",
                 "Farmer assigned to a completed Farm, automatic enabled, no invented farming work or XP; "
                 "NoCompatibleWork");
}
void migrated_legacy(Capture &capture) {
    auto s = capture.load("tests/fixtures/world_save_v7_group_move_search.ribsave", "migrated_legacy_atomic",
                          "genuine_v7_migrated_by_frozen_v8", 7);
    const auto state = s.world.export_state();
    require(!s.world.manual_orders().empty() &&
                std::all_of(
                    s.world.manual_orders().begin(), s.world.manual_orders().end(),
                    [](const auto &o) { return o.legacy_compatibility && !o.issuer && !o.issued_tick; }) &&
                !state.path_batches.empty() &&
                std::all_of(state.path_batches.begin(), state.path_batches.end(),
                            [](const auto &b) { return b.legacy_atomic; }),
            "Genuine v7 path migration did not preserve legacy atomic/manual provenance");
    capture.save(s, "migrated_legacy_atomic",
                 "Genuine v7 shared Search loaded without new commands; old atomic executor and unknown "
                 "issuance provenance retained");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: capture.exe fresh-output-root (run from repository root)");
        require(engine_version == "0.10.0" && rules_version == 7 && protocol_version == 7 &&
                    generator_version == 2 && default_content_hash() == content &&
                    static_cast<int>(ManualOrderCompletion::Cancelled) == 8,
                "Compile/link only against frozen 5476c90 format8 writer inputs");
        Capture capture{fs::path(argv[1])};
        capture.attempt("manual_queued_and_active_move", [&] { manual_queued_and_move(capture); });
        capture.attempt("shared_manual_paths", [&] { shared_paths(capture); });
        capture.attempt("one_delivery_cargo", [&] { gathering(capture, GatherCompletion::OneDelivery); });
        capture.attempt("until_exhausted_cargo_and_work",
                        [&] { gathering(capture, GatherCompletion::UntilExhausted); });
        for (const auto &[name, action] : std::array<std::pair<const char *, BoundaryAction>, 6>{
                 {{"waiting_boundary", BoundaryAction::Replace},
                  {"cancelled_cargo_with_tail", BoundaryAction::CancelWithTail},
                  {"cancelled_cargo_without_tail", BoundaryAction::CancelWithoutTail},
                  {"released_cargo", BoundaryAction::Release},
                  {"stopped_cargo", BoundaryAction::Stop},
                  {"deferred_assign_cargo", BoundaryAction::Assign}}})
            capture.attempt(name, [&] { cargo_boundary(capture, name, action); });
        capture.attempt("cancelled_pending_work_with_tail", [&] { cancel_pending_work(capture, true); });
        capture.attempt("cancelled_pending_work_without_tail", [&] { cancel_pending_work(capture, false); });
        capture.attempt("passive_farm", [&] { passive_farm(capture); });
        capture.attempt("farm_food_delivery", [&] { farm_food_delivery(capture); });
        capture.attempt("farmer_no_compatible_work", [&] { farmer_unsupported(capture); });
        capture.attempt("migrated_legacy_atomic", [&] { migrated_legacy(capture); });
        capture.attempt("migrated_neutral_cargo", [&] {
            auto s =
                capture.load("tests/fixtures/world_save_v7_legacy_v6_carry.ribsave", "migrated_neutral_cargo",
                             "genuine_v7_with_v6_origin_migrated_by_frozen_v8", 7);
            require(s.world.manual_orders().empty() && s.world.inventory().carried == Resources{0, 10, 0},
                    "Neutral legacy cargo acquired a manual wrapper or lost Stone10");
            capture.save(s, "migrated_neutral_cargo",
                         "Real v6-origin Stone10 cargo carried through genuine v7 into v8, with no invented "
                         "manual provenance");
        });
        capture.gap("migrated_opaque_legacy",
                    "No genuine historical input with opaque manual provenance is available. The existing "
                    "opaque persistence regression edits a copied manual flag and is deliberately excluded "
                    "from authentic capture.");
        capture.finish();
        std::cout << capture.report["fixtures"].size() << " fixtures; " << capture.report["gaps"].size()
                  << " explicitly reported gaps. Run the frozen verifier separately for exact bytes and 64 "
                     "future ticks.\n";
        return capture.report["gaps"].empty() ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

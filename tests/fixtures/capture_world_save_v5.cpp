// Historical generator, excluded from the current build. Compile only with the copied 0.7
// headers/libraries recorded in the adjacent expected JSON. Never overwrite the frozen fixtures
// using a newer SimCore; the three captures were produced by real gather/build commands.
#include "ribterra/world_state.hpp"
#include "ribterra/foundation.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ribterra;
using Json = nlohmann::json;
Json cell(CellCoord c) { return {c.x, c.y, static_cast<int>(c.layer)}; }
void capture(World &world, const char *name) {
    const auto path = std::filesystem::path("tests/fixtures") / (std::string(name) + ".ribsave");
    std::string error;
    if (!world.save(path, error)) throw std::runtime_error(error);
    World check(1, 1);
    if (!check.load(path, error) || check.checksum() != world.checksum())
        throw std::runtime_error("0.7 fixture round-trip: " + error);
    const auto s = world.export_state();
    Json expected{{"seed", s.seed}, {"ticks", s.ticks}, {"checksum", world.checksum()},
                  {"world_id", s.world_id}, {"population", s.residents.size()},
                  {"resources", {s.resources.wood, s.resources.stone, s.resources.food}},
                  {"next_building_id", s.next_building_id}, {"next_path_batch_id", s.next_path_batch_id},
                  {"next_item_stack_id", s.next_item_stack_id},
                  {"next_item_reservation_id", s.next_item_reservation_id},
                  {"next_logistics_task_id", s.next_logistics_task_id},
                  {"item_stacks", Json::array()}, {"tasks", Json::array()},
                  {"path_batches", Json::array()}, {"construction_orders", Json::array()}};
    for (const auto &stack : s.item_stacks) expected["item_stacks"].push_back({
        {"id", stack.stack_id}, {"item", static_cast<int>(stack.item)}, {"quantity", stack.quantity},
        {"owner", stack.owner_nation}, {"location_kind", static_cast<int>(stack.location.kind)},
        {"cell", cell(stack.location.cell)}, {"entity_id", stack.location.entity_id}});
    for (const auto &task : s.logistics_tasks) expected["tasks"].push_back({
        {"id", task.task_id}, {"kind", static_cast<int>(task.kind)},
        {"phase", static_cast<int>(task.phase)}, {"failure", static_cast<int>(task.failure)},
        {"resident_id", task.resident_id}, {"destination", task.destination_building_id},
        {"source_stack", task.source_stack_id}, {"carry_stack", task.carry_stack_id},
        {"reservation", task.reservation_id}, {"path_batch", task.path_batch_id},
        {"item", static_cast<int>(task.item)}, {"quantity", task.quantity},
        {"resume_job", static_cast<int>(task.resume_job)}, {"resume_target", cell(task.resume_target)}});
    for (const auto &batch : s.path_batches) expected["path_batches"].push_back({
        {"id", batch.path_batch_id}, {"command", static_cast<int>(batch.command)},
        {"phase", static_cast<int>(batch.phase)}, {"residents", batch.resident_ids},
        {"target", cell(batch.target)}, {"job", static_cast<int>(batch.assigned_job)},
        {"building_id", batch.building_id}, {"work_units", batch.work_units_done},
        {"frontier_count", batch.frontier.size()}, {"node_count", batch.nodes.size()}});
    for (const auto &order : s.construction_orders) {
        float progress = 0;
        for (const auto &building : s.buildings) if (building.id == order.building_id) progress = building.progress;
        expected["construction_orders"].push_back({{"id", order.building_id},
            {"kind", static_cast<int>(order.kind)}, {"phase", static_cast<int>(order.phase)},
            {"required", {order.required.wood, order.required.stone, order.required.food}},
            {"delivered", {order.delivered.wood, order.delivered.stone, order.delivered.food}},
            {"progress", progress}, {"site", cell(order.site)}});
    }
    std::ofstream(std::filesystem::path(".deps/v5-capture") / (std::string(name) + ".state.json")) << expected.dump(2) << '\n';
    std::cout << Json{{"fixture", name}, {"ticks", world.ticks()}, {"content_hash", default_content_hash()},
                      {"checksum", world.checksum()}}.dump() << '\n';
}
int main() {
    World gathering(70421, 10);
    bool admitted = false;
    for (int radius = 1; radius <= 16 && !admitted; ++radius)
        for (int y = 24 - radius; y <= 24 + radius && !admitted; ++y)
            for (int x = 24 - radius; x <= 24 + radius && !admitted; ++x)
                if (gathering.tile(x, y).terrain == Terrain::Forest && gathering.tile(x, y).amount > 0 &&
                    gathering.tile(x, y).explored)
                    admitted = gathering.gather({1}, x, y).accepted;
    if (!admitted) throw std::runtime_error("No gathering target");
    bool captured_carry = false, captured_continuation = false;
    for (int tick = 0; tick < 1200 && !captured_continuation; ++tick) {
        gathering.tick();
        if (!captured_carry && std::any_of(gathering.item_stacks().begin(), gathering.item_stacks().end(),
            [](const auto &s) { return s.location.kind == ItemLocationKind::Carried; })) {
            capture(gathering, "world_save_v5_carry"); captured_carry = true;
        }
        if (captured_carry && std::any_of(gathering.logistics_tasks().begin(), gathering.logistics_tasks().end(),
            [](const auto &t) { return t.phase == LogisticsPhase::Complete && t.path_batch_id != 0 && t.resume_job != Job::Idle; })) {
            capture(gathering, "world_save_v5_continuation"); captured_continuation = true;
        }
    }
    if (!captured_carry || !captured_continuation) throw std::runtime_error("Gather capture phase not reached");
    World construction(70421, 10);
    admitted = false;
    for (int y = 20; y <= 30 && !admitted; ++y)
        for (int x = 20; x <= 30 && !admitted; ++x)
            if (construction.can_build(BuildingKind::House, x, y))
                admitted = construction.build(BuildingKind::House, x, y).accepted;
    if (!admitted) throw std::runtime_error("No construction site");
    bool captured_work = false;
    for (int tick = 0; tick < 1200 && !captured_work; ++tick) {
        construction.tick();
        if (std::any_of(construction.buildings().begin(), construction.buildings().end(),
            [](const auto &b) { return b.kind == BuildingKind::House && b.progress > 0 && b.progress < 1; })) {
            capture(construction, "world_save_v5_work"); captured_work = true;
        }
    }
    if (!captured_work) throw std::runtime_error("Paid Work phase not reached");
}

// Historical fixture generator: compile only against the copied 0.6 headers and unchanged
// 0.6 SimCore/Foundation libraries recorded in world_save_v4_builds.expected.json.
// It is deliberately not part of the current build and must not overwrite the frozen fixture.
#include "ribterra/world_state.hpp"
#include "ribterra/foundation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace ribterra;
    World world(707070, 10);
    auto site = [&](BuildingKind kind) {
        for (int radius=3; radius<20; ++radius)
            for (int y=24-radius; y<=24+radius; ++y)
                for (int x=24-radius; x<=24+radius; ++x)
                    if (world.can_build(kind,x,y)) return CellCoord{x,y,WorldLayer::Surface};
        throw std::runtime_error("No construction site");
    };
    const auto first_site=site(BuildingKind::House);
    const auto first=world.build(BuildingKind::House,first_site.x,first_site.y);
    if (!first.accepted || !first.pending) throw std::runtime_error("First build rejected");
    for (int i=0; i<1200 && world.buildings().size()==1; ++i) world.tick();
    if (world.buildings().size()!=2) throw std::runtime_error("First build failed");
    world.take_path_results();
    const auto second_site=site(BuildingKind::Farm);
    const auto second=world.build(BuildingKind::Farm,second_site.x,second_site.y);
    if (!second.accepted || !second.pending) throw std::runtime_error("Second build rejected");
    std::string error;
    const auto output=std::filesystem::path("tests/fixtures/world_save_v4_builds.ribsave");
    if (!world.save(output,error)) throw std::runtime_error(error);
    World restored(1,1);
    if (!restored.load(output,error) || restored.checksum()!=world.checksum()) throw std::runtime_error(error);
    const auto state=world.export_state();
    nlohmann::json j={{"seed",world.seed()},{"ticks",world.ticks()},{"checksum",world.checksum()},
        {"content_hash",default_content_hash()},{"population",world.residents().size()},
        {"next_building_id",state.next_building_id},{"next_path_batch_id",state.next_path_batch_id},
        {"world_id",state.world_id},{"resources",{{"wood",state.resources.wood},{"stone",state.resources.stone},{"food",state.resources.food}}},
        {"buildings",nlohmann::json::array()},{"path_batches",nlohmann::json::array()}};
    for (const auto& b:state.buildings) j["buildings"].push_back({{"id",b.id},{"kind",static_cast<int>(b.kind)},{"x",b.x},{"y",b.y},{"progress",b.progress}});
    for (const auto& b:state.path_batches) j["path_batches"].push_back({{"id",b.path_batch_id},{"building_id",b.building_id},{"kind",static_cast<int>(*b.build_kind)},{"phase",static_cast<int>(b.phase)},{"x",b.target.x},{"y",b.target.y},{"resident_ids",b.resident_ids},{"reserved",{{"wood",b.reserved_cost.wood},{"stone",b.reserved_cost.stone},{"food",b.reserved_cost.food}}}});
    std::ofstream(".deps/v4-capture/expected.json")<<j.dump(2)<<'\n';
    std::cout<<j.dump()<<'\n';
}

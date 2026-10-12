#include "ribterra/alpha_rules.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/world_state.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
namespace {
void check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
void load(World& world,WorldState state) { std::string error;check(world.import_state(std::move(state),error),error); }
void catalog_and_commands() {
    check(building_catalog.size()==20,"Historical building catalog was removed");
    check(std::count_if(building_catalog.begin(),building_catalog.end(),[](const auto& d){return alpha::buildable(d.kind);})==11,"Alpha needs exactly 11 buildable types");
    World world(70421,1);
    for(const auto& d:building_catalog)if(!alpha::buildable(d.kind)) {
        const auto before=world.checksum();
        check(!world.can_build(d.kind,26,26),"Deferred construction preview allowed");
        check(!world.build(d.kind,26,26).accepted&&world.checksum()==before,"Deferred construction changed the world");
    }
    for(bool lobby:{false,true}) {
        ServerConfig config;config.population=1;config.lobby_enabled=lobby;config.wildlife_enabled=true;
        GameServer server(config);auto player=server.join("Alpha",{});check(player.accepted,"Join failed");
        std::uint64_t request=0;
        auto send=[&](GameCommand command){
            command.request_id=++request;command.author=player.player;
            check(server.enqueue(player.player,command).status==CommandReplyStatus::Queued,"Command did not reach server");
            server.pump_boundary();auto replies=server.take_replies(player.player);
            auto final=std::find_if(replies.begin(),replies.end(),[](const auto& reply){return reply.status!=CommandReplyStatus::Queued;});
            check(final!=replies.end(),"Missing reply");return *final;
        };
        if(lobby) {
            GameCommand c;c.type=CommandType::Lobby;c.x=int(LobbyAction::Ready);c.y=1;send(c);
            c.x=int(LobbyAction::Start);check(send(c).status==CommandReplyStatus::Applied,"Host did not start");
        }
        for(const auto& d:building_catalog)if(d.kind!=BuildingKind::Camp&&!alpha::buildable(d.kind)) {
            const auto before=server.world().checksum();GameCommand c;c.type=CommandType::Build;c.building=d.kind;c.x=26;c.y=26;
            auto reply=send(c);check(reply.status==CommandReplyStatus::Rejected&&reply.reason=="alpha.building_unavailable"&&server.world().checksum()==before,"Server accepted deferred construction");
        }
        for(auto action:{WildernessAction::Explore,WildernessAction::Hunt,WildernessAction::Resources,WildernessAction::Attack}) {
            GameCommand c;c.type=CommandType::Wilderness;c.building=BuildingKind(action);c.residents={server.world().residents().front().id};c.x=26;c.y=26;c.building_id=1;c.settlement_revision=server.world().economy().wilderness.revision;
            auto reply=send(c);check(reply.status==CommandReplyStatus::Rejected&&reply.reason=="alpha.wilderness_unavailable","Server accepted a deferred expedition");
        }
    }
}
void preserved_data_and_atomic_rejection() {
    World world(70421,1);auto state=world.export_state();
    state.buildings.push_back({state.next_building_id++,BuildingKind::Temple,29,29,1,WorldLayer::Surface,1});
    for(auto item:{ItemKind::Tools,ItemKind::Spear,ItemKind::Hide})state.item_stacks.push_back({state.next_item_stack_id++,item,3,1,{ItemLocationKind::Storage,{},1}});
    load(world,state);const auto before=world.checksum();
    auto reject=[&](WorldState invalid,const char* key){std::string error;check(!world.import_state(std::move(invalid),error)&&error==key,error);check(world.checksum()==before,"Rejected import modified the live world");};
    auto attack=state;attack.economy.wilderness.enabled=true;attack.economy.wilderness.next_animal=2;
    attack.economy.wilderness.animals={{1,1,AnimalSpecies::Wolf,AnimalBehaviour::Attacking,24,24,24,24,36,1}};
    reject(attack,"alpha.save_attack");
    auto expedition=state;Expedition e;e.id=1;e.members={1};e.x=24;e.y=24;
    expedition.economy.wilderness.next_expedition=2;expedition.economy.wilderness.expeditions={e};reject(expedition,"alpha.save_expedition");
    auto rations=state;rations.settlement.reduced_rations=rations.economy.wilderness.reduced_rations=true;reject(rations,"alpha.save_policy");
    auto building=state;building.buildings.back().progress=.5f;reject(building,"alpha.save_building");
    auto production=state;production.buildings.back().kind=BuildingKind::Workshop;production.economy.next_order_id=2;
    production.economy.orders.push_back({1,2,1,9});reject(production,"alpha.save_production");
    const auto path=std::filesystem::temp_directory_path()/"ribterra-alpha-history.save";std::string error;
    check(world.save(path,error),error);World restored;check(restored.load(path,error),error);
    check(restored.checksum()==world.checksum()&&restored.economy().stored[int(ItemKind::Tools)]==3,"Historical objects or inventory lost");
    std::filesystem::remove(path);
}
void inventory_projection() {
    World world(70421,1);auto state=world.export_state();
    state.item_stacks.push_back({state.next_item_stack_id++,ItemKind::Grain,7,1,{ItemLocationKind::Ground,{24,24},0}});
    state.buildings.push_back({state.next_building_id++,BuildingKind::Sawmill,29,29,1,WorldLayer::Surface,1});
    load(world,state);check(world.production(2,1,3,1,0).accepted,"Projection production rejected");
    for(int i=0;i<1800&&!world.economy().carried[int(ItemKind::Planks)];++i)world.tick();
    const auto snapshot=world.economy();
    check(snapshot.carried[int(ItemKind::Planks)]==6&&snapshot.stored[int(ItemKind::Planks)]==0,"Carried output counted as delivered");
    check(snapshot.ground[int(ItemKind::Grain)]==7&&snapshot.stored[int(ItemKind::Food)]==world.inventory().stored.food,"Ingredients counted as ready food");
    check(decode_economy(encode_economy(snapshot))==snapshot,"Wire lost projected inventory");
    check(world.economy(2).carried[int(ItemKind::Planks)]==0&&world.economy(2).ground[int(ItemKind::Grain)]==0,"Inventory leaked to another nation");
}
}
int main(){try{catalog_and_commands();preserved_data_and_atomic_rejection();inventory_projection();std::cout<<"Alpha: 11 buildable types, local/host rejection, retained historical data, atomic incompatible-state rejection and inventory projection passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

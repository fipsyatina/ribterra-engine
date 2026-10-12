#include "ribterra/world_state.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/atomic_file.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
using Json=nlohmann::json;
namespace fs=std::filesystem;
namespace {
void check(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
std::vector<std::uint8_t> read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path&p,const std::vector<std::uint8_t>&b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());}
Json json(const fs::path&p){std::ifstream f(p);return Json::parse(f);}
struct Temp {
 fs::path path=fs::temp_directory_path()/("ribterra-save-compat-"+secure_token(16));
 Temp(){check(fs::create_directory(path),"Cannot create isolated test directory");}
 ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
Json observe(const World& world) {
 auto s=world.export_state();Json j={{"buildings",Json::array()},{"upgrades",Json::array()},{"people",Json::array()},{"items",Json::array()}};
 for(auto&b:s.buildings)j["buildings"].push_back({b.id,int(b.kind),b.x,b.y,b.progress,int(b.layer),b.nation});
 for(auto&u:s.economy.upgrades)j["upgrades"].push_back({u.building_id,u.level});
 for(auto&r:s.residents)j["people"].push_back({r.id,r.name,r.nation,r.health,r.x,r.y});
 for(auto&i:s.item_stacks)j["items"].push_back({i.stack_id,int(i.item),i.quantity,i.owner_nation,int(i.location.kind),i.location.cell.x,i.location.cell.y,int(i.location.cell.layer),i.location.entity_id});
 return j;
}
std::array<std::uint64_t,economy_item_count> totals(const World&w){std::array<std::uint64_t,economy_item_count> out{};for(auto&i:w.item_stacks())out[int(i.item)]+=i.quantity;return out;}
void fixtures(const fs::path& root) {
 const auto manifest=json(root/"provenance.json");check(manifest.at("writer")=="0.25.7"&&manifest.at("rules")==14,"Wrong frozen writer");
 for(const auto& record:manifest.at("fixtures")) {
  Temp temp;const auto name=record.at("name").get<std::string>();const auto original=read(root/(name+".ribsave"));
  check(sha256(original)==record.at("sha256").get<std::string>()&&original.at(16)==14,"Historical fixture changed: "+name);
  const auto source=temp.path/fs::path(u8"Старий світ.ribsave");write(source,original);
  World w(937,2);const auto initial=w.checksum();std::string error;const bool ok=w.load(source,error);
  check(read(source)==original,"Loading overwrote source: "+name);
  if(!record.at("supported").get<bool>()) {
   check(!ok&&error=="alpha.save_expedition","Active expedition did not give explicit refusal: "+name+" / "+error);
   check(w.checksum()==initial,"Rejected load changed world");
   check(std::distance(fs::directory_iterator(temp.path),fs::directory_iterator{})==1,"Rejected load wrote a file");
   continue;
  }
  check(ok,name+" / "+error);const auto info=w.last_load();
  check(info.copied&&info.source_path==source&&info.save_path!=source&&fs::exists(info.save_path),"Migration did not create a separate copy");
  check(info.normalized_rations==(name=="rations"||name=="combined"),"Wrong ration notice");
  check(info.neutralized_attacks==int(name=="attack"||name=="combined"),"Wrong attack notice");
  check(observe(w)==json(root/(name+".expected.json")),"Buildings, levels, owners, people or item locations changed: "+name);
  check(!w.settlement().reduced_rations&&!w.economy().wilderness.reduced_rations,"Rations not normalized");
  for(const auto&n:w.export_state().economy.nations)check(!n.policy.reduced_rations,"Foreign nation rations not normalized");
  for(const auto&a:w.economy().wilderness.animals)check(a.behaviour!=AnimalBehaviour::Attacking&&a.target==0&&a.next_attack==0,"Attack timer survived migration");
  const auto invariant=w.checksum();
  for(int cycle=0;cycle<2;++cycle) {
   check(w.save(info.save_path,error),error);World next(2,0);check(next.load(info.save_path,error),error);
   check(!next.last_load().copied&&next.checksum()==invariant,"Second migration or state loss on current save");w=std::move(next);
   check(read(source)==original,"Double roundtrip overwrote source");
  }
  if(name=="cargo"||name=="combined") {
   const auto inventory=totals(w);const auto stored=w.economy().stored[int(ItemKind::Planks)];
   const auto carried=w.economy().carried[int(ItemKind::Planks)];check(carried==6,"Expected physical six-plank delivery");
   for(int tick=0;tick<2400;++tick){const auto orders=w.economy().orders;if(std::none_of(orders.begin(),orders.end(),production_active))break;w.tick();}
   check(w.economy().stored[int(ItemKind::Planks)]==stored+6&&w.economy().carried[int(ItemKind::Planks)]==0,"Ordinary cargo not delivered once");
   for(int tick=0;tick<220;++tick)w.tick();
   check(w.economy().stored[int(ItemKind::Planks)]==stored+6,"Output duplicated after completion");
   const auto after=totals(w);for(int item=0;item<economy_item_count;++item)if(item!=int(ItemKind::Food))check(after[item]==inventory[item],"Non-food item lost or duplicated");
  } else {
   for(int tick=0;tick<300;++tick)w.tick();for(auto&r:w.residents())check(r.health==100,"Old attack dealt damage");
  }
  std::cout<<name<<": two exact roundtrips, source retained, cargo/policy passed\n";
 }
}
void boundary(const fs::path& root) {
 Temp temp;const auto original=read(root/"combined.ribsave");const auto source=temp.path/"original.ribsave";write(source,original);std::string error;
 ClientSession client;check(client.load(source,error),error);const auto copy=client.last_load().save_path;
 for(int i=0;i<10;++i)client.advance();check(client.save(source,error),error);check(read(source)==original,"Local F5 path overwrote original");
 ClientSession reopened;check(reopened.load(copy,error),error);check(reopened.ticks()==client.ticks(),"Local resume lost progress");
 const auto local_tick=reopened.ticks();const auto denied=temp.path/"active-expedition.ribsave";fs::copy_file(root/"expedition-returning.ribsave",denied);
 check(!reopened.load(denied,error)&&reopened.ticks()==local_tick,"Local refusal replaced the active session");
 net::HostConfig config;config.save_path=source;config.transport.bind_address="127.0.0.1";config.transport.port=27412;
 {net::NetworkServer host(config);check(host.save_path()!=source,"Host kept original save destination");for(int i=0;i<10;++i)host.tick();check(host.save(),host.last_error());host.shutdown();}
 check(read(source)==original,"Host save/shutdown overwrote original");
 ServerConfig online_config;online_config.lobby_enabled=true;online_config.max_players=2;
 GameServer online(online_config);const auto host_player=online.join("Old host",online.config().versions);std::uint64_t request=1;
 const auto command=[&](LobbyAction action,int value){GameCommand c;c.type=CommandType::Lobby;c.x=int(action);c.y=value;c.request_id=request++;c.author=host_player.player;online.enqueue(host_player.player,c);online.pump_boundary();};
 command(LobbyAction::Team,2);command(LobbyAction::Ready,1);command(LobbyAction::Start,0);
 const auto online_file=temp.path/"online-owner.save";check(online.save(online_file,error),error);ClientSession owner;
 check(owner.load(online_file,error)&&owner.nation_id()==2,"Loading lost saved host ownership");
 GameServer server;const auto joined=server.join("Before",server.config().versions);const auto before=server.world().checksum();
 World malformed(1,1);const auto bad=temp.path/"bad-online.save";check(malformed.save(bad,error,"{"),error);
 check(!server.load(bad,error)&&error=="Invalid saved online session.","Broken metadata accepted");check(server.world().checksum()==before&&server.connected(joined.player),"Server refusal changed live session");
 World current(1,1);check(current.load(copy,error),error);const auto stable=current.checksum();
 for(int mode=0;mode<4;++mode){auto damaged=original;
  if(mode==0)damaged.back()^=0x7f;else if(mode==1)damaged.resize(200);else if(mode==2)damaged[12]=99;else damaged[16]=255;
  const auto rejected=temp.path/("damaged-"+std::to_string(mode)+".save");write(rejected,damaged);check(!current.load(rejected,error)&&!error.empty(),"Damaged or unsupported file accepted");check(current.checksum()==stable&&read(rejected)==damaged,"Refusal overwrote file or live world");
 }
 // Publishing a migration must never replace even an unrelated existing destination.
 const auto a=temp.path/"publish.tmp",b=temp.path/"occupied.save";write(a,{1,2,3});write(b,{4,5});check(!atomic_create_file(a,b,error)&&read(b)==std::vector<std::uint8_t>({4,5}),"Atomic copy replaced existing file");
 std::cout<<"Local/host destinations, session transactionality, malformed/unsupported files and exclusive publication passed\n";
}
}
int main(){try{const auto root=fs::path(__FILE__).parent_path()/"fixtures/alpha-0257";fixtures(root);boundary(root);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

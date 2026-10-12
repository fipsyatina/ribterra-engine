#include "ribterra/lobby.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace ribterra {
std::string encode_lobby(const LobbyState&s){
 nlohmann::json j={{"enabled",s.enabled},{"started",s.started},{"paused",s.paused},{"capacity",s.capacity},{"speed",s.speed},{"host",s.host},{"pause_author",s.pause_author},{"revision",s.revision},{"players",nlohmann::json::array()}};
 for(const auto&p:s.players)j["players"].push_back({{"id",p.id},{"name",p.name},{"team",p.team},{"ready",p.ready},{"connected",p.connected},{"bot",p.bot}});
 return j.dump();
}
LobbyState decode_lobby(const std::string&v){
 if(v.empty())return {};
 if(v.size()>8192)throw std::runtime_error("Lobby too large");
 const auto j=nlohmann::json::parse(v);LobbyState s;
 s.enabled=j.at("enabled");s.started=j.at("started");s.paused=j.at("paused");s.capacity=j.at("capacity");s.speed=j.at("speed");s.host=j.at("host");s.pause_author=j.at("pause_author");s.revision=j.at("revision");
 if(s.capacity<2||s.capacity>6||(s.speed!=1&&s.speed!=2&&s.speed!=4)||j.at("players").size()>6)throw std::runtime_error("Invalid lobby");
 std::set<std::uint32_t> ids;
 for(const auto&p:j.at("players")){
  LobbyPlayer x{p.at("id"),p.at("name"),p.at("team"),p.at("ready"),p.at("connected"),p.value("bot",false)};
  if(!x.id||!ids.insert(x.id).second||x.name.empty()||x.name.size()>96||x.team<1||x.team>6)throw std::runtime_error("Invalid lobby member");
  s.players.push_back(x);
 }
 if(s.enabled&&!s.players.empty()&&!ids.contains(s.host))throw std::runtime_error("Invalid lobby host");
 return s;
}
}

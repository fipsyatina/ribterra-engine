#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ribterra {
struct LobbyPlayer {
 std::uint32_t id{}; std::string name; int team{}; bool ready{},connected{},bot{};
 bool operator==(const LobbyPlayer&)const=default;
};
struct LobbyState {
 bool enabled{},started{},paused{}; int capacity{6},speed{1}; std::uint32_t host{},pause_author{};
 std::uint64_t revision{}; std::vector<LobbyPlayer> players;
 bool operator==(const LobbyState&)const=default;
};
enum class LobbyAction { Team, Ready, Start, Pause, Speed, AddBot, RemoveBot, BotTeam };
std::string encode_lobby(const LobbyState&);
LobbyState decode_lobby(const std::string&);
}

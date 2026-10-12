#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ribterra {
struct LocalIpv4Interface {
    std::string address;
    std::string adapter_name;
    bool has_gateway{};
    bool tunnel{};
    std::uint32_t metric{};
};
// Ordered from the best invitation address to usable alternatives. These are
// local interface addresses; they do not imply public Internet reachability.
std::vector<LocalIpv4Interface> local_ipv4_interfaces();
std::vector<std::string> local_ipv4_addresses();
std::optional<std::string> environment_variable(std::string_view name);
std::vector<std::string> utf8_arguments(int argc, char** argv);
} // namespace ribterra

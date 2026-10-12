#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ribterra {
inline constexpr std::uint32_t protocol_version = 23;
inline constexpr std::uint32_t rules_version = 19;
inline constexpr std::uint32_t generator_version = 8;
inline constexpr std::string_view engine_version = "0.26.3";
// SHA256 of canonical gameplay content, replaced by ContentCatalog when loaded.
std::string default_content_hash();
std::string sha256(std::span<const std::uint8_t> bytes);
std::string secure_token(std::size_t bytes = 32);
bool constant_time_equal(std::string_view a, std::string_view b);
bool valid_utf8_text(std::string_view text, std::size_t maximum_bytes, bool multiline = false);
struct Versions {
    std::uint32_t protocol{protocol_version};
    std::uint32_t rules{rules_version};
    std::string content{default_content_hash()};
    bool operator==(const Versions &) const = default;
};
inline std::string version_mismatch(const Versions& expected,const Versions& received) {
    if(expected.protocol!=received.protocol)return "network.version_protocol";
    if(expected.rules!=received.rules)return "network.version_rules";
    if(expected.content!=received.content)return "network.version_content";
    return {};
}
} // namespace ribterra

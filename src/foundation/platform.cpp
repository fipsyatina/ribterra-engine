#include "ribterra/platform.hpp"
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// shellapi.h requires the Windows declarations above.
#include <shellapi.h>
#endif

namespace ribterra {
namespace {
#ifdef _WIN32
struct Ipv4Candidate {
    LocalIpv4Interface interface;
    std::uint32_t value{};
    std::uint32_t interface_index{};
    bool physical{};
};

std::string adapter_name(const wchar_t *value) {
    if (!value || !*value)
        return {};
    const int length = static_cast<int>(std::wcslen(value));
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), size,
                            nullptr, nullptr) != size)
        return {};
    return result;
}

bool usable_ipv4(std::uint32_t value) {
    const auto first = static_cast<std::uint8_t>(value >> 24U);
    const auto second = static_cast<std::uint8_t>(value >> 16U);
    return value != 0 && value != 0xffffffffU && first != 0 && first != 127 && first < 224 &&
           !(first == 169 && second == 254);
}

bool physical_adapter(ULONG type) {
    return type == IF_TYPE_ETHERNET_CSMACD || type == IF_TYPE_IEEE80211 ||
           type == IF_TYPE_WWANPP || type == IF_TYPE_WWANPP2;
}

bool has_ipv4_gateway(const IP_ADAPTER_ADDRESSES &adapter) {
    for (auto *entry = adapter.FirstGatewayAddress; entry; entry = entry->Next) {
        if (!entry->Address.lpSockaddr || entry->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        const auto *address = reinterpret_cast<const sockaddr_in *>(entry->Address.lpSockaddr);
        if (usable_ipv4(ntohl(address->sin_addr.s_addr)))
            return true;
    }
    return false;
}
#endif
} // namespace

std::vector<LocalIpv4Interface> local_ipv4_interfaces() {
    std::vector<LocalIpv4Interface> result;
#ifdef _WIN32
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage(size);
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data());
        status = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW)
            storage.resize(size);
    }
    if (status != NO_ERROR)
        return result;

    std::vector<Ipv4Candidate> candidates;
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data());
    for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        const bool gateway = has_ipv4_gateway(*adapter);
        const bool physical = physical_adapter(adapter->IfType);
        for (auto *entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
            if (!entry->Address.lpSockaddr || entry->Address.lpSockaddr->sa_family != AF_INET ||
                entry->DadState != IpDadStatePreferred)
                continue;
            const auto *address = reinterpret_cast<const sockaddr_in *>(entry->Address.lpSockaddr);
            const std::uint32_t value = ntohl(address->sin_addr.s_addr);
            if (!usable_ipv4(value))
                continue;
            char text[INET_ADDRSTRLEN]{};
            if (!InetNtopA(AF_INET, &address->sin_addr, text, sizeof(text)))
                continue;
            candidates.push_back({{text, adapter_name(adapter->FriendlyName), gateway,
                                   adapter->IfType == IF_TYPE_TUNNEL, adapter->Ipv4Metric},
                                  value, adapter->IfIndex, physical});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        // An active adapter with a configured gateway is the strongest signal
        // that another machine can reach this address. Prefer a physical link
        // within that group, then use Windows' route metric as the tie-breaker.
        return std::tuple{!left.interface.has_gateway, !left.physical, left.interface.metric,
                          left.interface_index, left.value} <
               std::tuple{!right.interface.has_gateway, !right.physical, right.interface.metric,
                          right.interface_index, right.value};
    });
    std::set<std::string> seen;
    for (auto &candidate : candidates)
        if (seen.insert(candidate.interface.address).second)
            result.push_back(std::move(candidate.interface));
#endif
    return result;
}

std::vector<std::string> local_ipv4_addresses() {
    std::vector<std::string> result;
    for (auto &interface : local_ipv4_interfaces())
        result.push_back(std::move(interface.address));
    return result;
}

std::optional<std::string> environment_variable(std::string_view name) {
    if (name.empty() || name.find('\0') != std::string_view::npos ||
        name.find('=') != std::string_view::npos)
        throw std::invalid_argument("Invalid environment name");
    const std::string key(name);
#ifdef _WIN32
    constexpr DWORD maximum_length = 1024 * 1024;
    if (key.size() > maximum_length)
        throw std::invalid_argument("Environment name is too long");
    const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.c_str(),
                                              static_cast<int>(key.size()), nullptr, 0);
    if (wide_size <= 0)
        throw std::invalid_argument("Invalid UTF-8 environment name");
    std::wstring wide_key(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.c_str(),
                            static_cast<int>(key.size()), wide_key.data(), wide_size) != wide_size)
        throw std::runtime_error("Environment name conversion failed");
    for (int attempt = 0; attempt < 3; ++attempt) {
        SetLastError(ERROR_SUCCESS);
        const DWORD required = GetEnvironmentVariableW(wide_key.c_str(), nullptr, 0);
        if (required == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_ENVVAR_NOT_FOUND)
                return {};
            if (error == ERROR_SUCCESS)
                return std::string{};
            throw std::runtime_error("Environment read failed");
        }
        if (required > maximum_length)
            throw std::runtime_error("Environment value is too long");
        std::wstring wide_value(required, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(wide_key.c_str(), wide_value.data(), required);
        if (length >= required)
            continue; // The environment changed between sizing and reading.
        if (length == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_ENVVAR_NOT_FOUND)
                return {};
            if (error == ERROR_SUCCESS)
                return std::string{};
            throw std::runtime_error("Environment read failed");
        }
        const int utf8_size =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_value.data(),
                                static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (utf8_size <= 0)
            throw std::runtime_error("Invalid Unicode environment value");
        std::string value(static_cast<std::size_t>(utf8_size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_value.data(),
                                static_cast<int>(length), value.data(), utf8_size, nullptr,
                                nullptr) != utf8_size)
            throw std::runtime_error("Environment value conversion failed");
        return value;
    }
    throw std::runtime_error("Environment changed repeatedly while reading");
#else
    if (const char *value = std::getenv(key.c_str()))
        return std::string(value);
#endif
    return {};
}
std::vector<std::string> utf8_arguments(int argc, char **argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        throw std::runtime_error("Cannot decode command line");
    std::unique_ptr<void, decltype(&LocalFree)> owner(wide, &LocalFree);
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, nullptr, 0,
                                             nullptr, nullptr);
        if (size <= 0)
            throw std::runtime_error("Invalid Unicode argument");
        std::string text(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, text.data(), size,
                                nullptr, nullptr) != size)
            throw std::runtime_error("Argument conversion failed");
        text.pop_back();
        result.push_back(std::move(text));
    }
#else
    for (int i = 0; i < argc; ++i)
        result.emplace_back(argv[i]);
#endif
    return result;
}
} // namespace ribterra

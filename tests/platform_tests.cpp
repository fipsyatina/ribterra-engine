#include "ribterra/platform.hpp"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
void check(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}

template <class Function> bool throws_invalid_argument(Function &&function) {
    try {
        function();
    } catch (const std::invalid_argument &) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

#ifdef _WIN32
std::string utf8(std::wstring_view value) {
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    check(size > 0, "Fixture name conversion failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), size, nullptr,
                              nullptr) == size,
          "Fixture name conversion changed size");
    return result;
}

struct EnvironmentFixture {
    std::wstring wide_name;
    std::string name;
    std::optional<std::wstring> previous;
    explicit EnvironmentFixture(std::wstring value)
        : wide_name(std::move(value)), name(utf8(wide_name)) {
        SetLastError(ERROR_SUCCESS);
        const DWORD required = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
        if (required) {
            std::wstring buffer(required, L'\0');
            const DWORD length =
                GetEnvironmentVariableW(wide_name.c_str(), buffer.data(), required);
            check(length < required, "Could not snapshot fixture environment variable");
            buffer.resize(length);
            previous = std::move(buffer);
        } else if (GetLastError() == ERROR_SUCCESS) {
            previous = std::wstring{};
        } else {
            check(GetLastError() == ERROR_ENVVAR_NOT_FOUND,
                  "Could not inspect fixture environment variable");
        }
    }
    ~EnvironmentFixture() {
        SetEnvironmentVariableW(wide_name.c_str(), previous ? previous->c_str() : nullptr);
    }
    void set(std::wstring_view value) {
        const std::wstring copy(value);
        check(SetEnvironmentVariableW(wide_name.c_str(), copy.c_str()) != 0,
              "Could not set fixture environment variable");
    }
    void unset() {
        check(SetEnvironmentVariableW(wide_name.c_str(), nullptr) != 0,
              "Could not clear fixture environment variable");
    }
};
#else
struct EnvironmentFixture {
    std::string name;
    std::optional<std::string> previous;
    explicit EnvironmentFixture(std::string value) : name(std::move(value)) {
        if (const char *found = std::getenv(name.c_str()))
            previous = found;
    }
    ~EnvironmentFixture() {
        if (previous)
            setenv(name.c_str(), previous->c_str(), 1);
        else
            unsetenv(name.c_str());
    }
    void set(std::string_view value) {
        const std::string copy(value);
        check(setenv(name.c_str(), copy.c_str(), 1) == 0,
              "Could not set fixture environment variable");
    }
    void unset() {
        check(unsetenv(name.c_str()) == 0, "Could not clear fixture environment variable");
    }
};
#endif
} // namespace

int main() {
    try {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
        EnvironmentFixture fixture(L"RIBTERRA_PLATFORM_TEST_" + std::to_wstring(stamp) +
                                   L"_СЕРЕДОВИЩЕ");
        fixture.unset();
        check(!ribterra::environment_variable(fixture.name),
              "Missing Unicode environment variable was reported as present");
        fixture.set(L"значення з пробілом");
        check(ribterra::environment_variable(fixture.name) ==
                  std::optional<std::string>(utf8(L"значення з пробілом")),
              "Unicode environment value did not roundtrip through UTF-8");
        fixture.set(L"");
        check(ribterra::environment_variable(fixture.name) == std::optional<std::string>(""),
              "Empty environment value was confused with missing");
        std::string invalid_utf8_name = "RIBTERRA_";
        invalid_utf8_name.push_back(static_cast<char>(0xc3));
        invalid_utf8_name.push_back('(');
        check(throws_invalid_argument(
                  [&] { (void)ribterra::environment_variable(invalid_utf8_name); }),
              "Invalid UTF-8 environment name was accepted");
        const wchar_t invalid_value[]{static_cast<wchar_t>(0xd800), L'\0'};
        check(SetEnvironmentVariableW(fixture.wide_name.c_str(), invalid_value) != 0,
              "Windows rejected the invalid-Unicode fixture value before it could be tested");
        bool invalid_value_rejected = false;
        try {
            (void)ribterra::environment_variable(fixture.name);
        } catch (const std::runtime_error &) {
            invalid_value_rejected = true;
        }
        check(invalid_value_rejected,
              "Invalid Unicode environment value was converted to invalid UTF-8");
#else
        EnvironmentFixture fixture("RIBTERRA_PLATFORM_TEST_" + std::to_string(stamp) +
                                   "_СЕРЕДОВИЩЕ");
        fixture.unset();
        check(!ribterra::environment_variable(fixture.name),
              "Missing Unicode environment variable was reported as present");
        fixture.set("значення з пробілом");
        check(ribterra::environment_variable(fixture.name) ==
                  std::optional<std::string>("значення з пробілом"),
              "Unicode environment value did not roundtrip");
        fixture.set("");
        check(ribterra::environment_variable(fixture.name) == std::optional<std::string>(""),
              "Empty environment value was confused with missing");
#endif
        const auto interfaces = ribterra::local_ipv4_interfaces();
        const auto addresses = ribterra::local_ipv4_addresses();
        check(interfaces.size() == addresses.size(),
              "Address compatibility wrapper changed interface count");
        std::set<std::string> unique_addresses;
        bool passed_gateway_group = false;
        for (std::size_t index = 0; index < interfaces.size(); ++index) {
            const auto &interface = interfaces[index];
            if (!interface.has_gateway)
                passed_gateway_group = true;
            check(!passed_gateway_group || !interface.has_gateway,
                  "Gateway-backed address was ranked behind a host-only alternative");
            check(unique_addresses.insert(interface.address).second,
                  "Local IPv4 enumeration returned a duplicate address");
            check(interface.address == addresses[index],
                  "Address compatibility wrapper changed interface order");
            check(interface.address != "127.0.0.1" &&
                      !interface.address.starts_with("169.254."),
                  "Local IPv4 enumeration returned loopback or APIPA");
#ifdef _WIN32
            check(!interface.adapter_name.empty(), "Windows adapter label is empty");
#endif
        }
        if (!interfaces.empty()) {
            std::cout << "Preferred local IPv4: " << interfaces.front().address << " ("
                      << interfaces.front().adapter_name << ", gateway="
                      << interfaces.front().has_gateway << ")\n";
            for (std::size_t index = 1; index < interfaces.size(); ++index)
                std::cout << "Alternative local IPv4: " << interfaces[index].address << " ("
                          << interfaces[index].adapter_name << ", gateway="
                          << interfaces[index].has_gateway << ")\n";
        }
        check(throws_invalid_argument([] { (void)ribterra::environment_variable(""); }),
              "Empty environment name was accepted");
        check(throws_invalid_argument([] { (void)ribterra::environment_variable("RIBTERRA=BAD"); }),
              "Environment name containing '=' was accepted");
        const std::string embedded_nul("RIBTERRA_BAD\0NAME", 17);
        check(throws_invalid_argument([&] { (void)ribterra::environment_variable(embedded_nul); }),
              "Environment name containing NUL was accepted");
        std::cout << "Platform environment variable validation and Unicode roundtrip passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

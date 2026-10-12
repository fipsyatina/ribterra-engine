#include "ribterra/localization.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
int failures{};
#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        if (!(value)) {                                                                                      \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #value "\n";                      \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

const std::vector<std::string> locales{"en", "uk", "ru", "pl", "es"};
const std::string plural = "{count, plural, one {one:#} few {few:#} many {many:#} other {other:#}}";

void write(const std::filesystem::path &path, std::string_view value) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(value.data(), static_cast<std::streamsize>(value.size())))
        throw std::runtime_error("fixture write failed");
}
std::string catalog(std::string_view plural_pattern = plural,
                    std::string_view pair_pattern = "{count, number} / {total, number}") {
    return "{\"items\":\"" + std::string(plural_pattern) + "\",\"pair\":\"" + std::string(pair_pattern) +
           "\",\"plain\":\"ready\",\"pseudo\":\"Count {count, number}\",\"error\":\"Could not "
           "save: {reason}\"}";
}
struct Fixture {
    std::filesystem::path path;
    Fixture() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path =
            std::filesystem::temp_directory_path() / ("ribterra-localization-test-" + std::to_string(stamp));
        std::filesystem::create_directory(path);
        for (const auto &locale : locales)
            write(path / (locale + ".json"), catalog());
    }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
template <class Function> bool rejects(Function &&function) {
    try {
        function();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}

void plural_rules_and_arguments() {
    Fixture fixture;
    ribterra::Localization localization(fixture.path);
    struct Case {
        const char *locale;
        double value;
        const char *category;
    };
    const Case cases[]{{"en", 1, "one"},     {"en", 2, "other"},   {"en", 1.5, "other"}, {"uk", 1, "one"},
                       {"uk", 2, "few"},     {"uk", 5, "many"},    {"uk", 1.5, "other"}, {"uk", 21, "one"},
                       {"uk", 22, "few"},    {"uk", 25, "many"},   {"ru", 1, "one"},     {"ru", 2, "few"},
                       {"ru", 5, "many"},    {"ru", 1.5, "other"}, {"ru", 21, "one"},    {"ru", 22, "few"},
                       {"ru", 25, "many"},   {"pl", 1, "one"},     {"pl", 2, "few"},     {"pl", 5, "many"},
                       {"pl", 1.5, "other"}, {"pl", 21, "many"},   {"pl", 22, "few"},    {"pl", 25, "many"},
                       {"es", 1, "one"},     {"es", 2, "other"},   {"es", 1.5, "other"}};
    for (const auto &item : cases) {
        localization.set_locale(item.locale);
        const auto value = localization.format("items", {{"count", item.value}});
        CHECK(value.starts_with(std::string(item.category) + ":"));
    }
    localization.set_locale("uk");
    CHECK(localization.format("error", {{"reason", "Немає місця <disk>"}}) ==
          "Could not save: Немає місця <disk>");
    CHECK(localization.format("error", {{"reason", ""}}) == "Could not save: ");
    CHECK(rejects([&] { (void)localization.format("error", {{"reason", std::string("\xff")}}); }));
    CHECK(rejects([&] { (void)localization.format("items", {{"count", "text"}}); }));
    CHECK(rejects([&] { (void)localization.format("pair", {{"count", 1}}); }));
    CHECK(rejects([&] { (void)localization.format("pair", {{"count", 1}, {"total", 2}, {"extra", 3}}); }));
    CHECK(!localization.format("pair", {{"total", 2}, {"count", 1}}).empty());
    CHECK(rejects(
        [&] { (void)localization.format("items", {{"count", std::numeric_limits<double>::infinity()}}); }));
    CHECK(rejects(
        [&] { (void)localization.format("items", {{"count", std::numeric_limits<double>::quiet_NaN()}}); }));
    CHECK(localization.locale() == "uk");
}

void failures_preserve_selection_and_pseudo_is_safe() {
    Fixture fixture;
    ribterra::Localization localization(fixture.path, "pl");
    CHECK(rejects([&] { (void)localization.text("missing"); }));
    for (const auto &locale : locales) {
        localization.set_locale(locale);
        CHECK(localization.text_or_literal("plain") == "ready");
        CHECK(localization.text_or_literal("Мешканця обрано") == "Мешканця обрано");
        CHECK(localization.text_or_literal("Оберіть вільну ділянку") == "Оберіть вільну ділянку");
        CHECK(localization.text_or_literal("network.unknown_error") == "network.unknown_error");
        CHECK(localization.text_or_literal("").empty());
        CHECK(rejects([&] { (void)localization.text("missing"); }));
    }
    localization.set_locale("pl");
    CHECK(localization.locale() == "pl");
    CHECK(rejects([&] { localization.set_locale("de"); }));
    CHECK(localization.locale() == "pl");
    const auto normal = localization.format("pseudo", {{"count", 17}});
    localization.set_pseudolocalized(true);
    const auto pseudo = localization.format("pseudo", {{"count", 17}});
    CHECK(pseudo.size() > normal.size());
    CHECK(pseudo.find("17") != std::string::npos);
    CHECK(pseudo.starts_with("[!! "));
    localization.set_pseudolocalized(false);
    CHECK(localization.format("pseudo", {{"count", 17}}) == normal);
    CHECK(ribterra::Localization::supported_locales() == locales);
}

void malformed_catalogs_are_transactional() {
    for (const auto &locale : locales) {
        Fixture fixture;
        write(fixture.path / (locale + ".json"), catalog("{count, plural, one {one}"));
        CHECK(rejects([&] { ribterra::Localization invalid(fixture.path); }));
    }
    {
        Fixture fixture;
        write(fixture.path / "es.json",
              "{\"items\":\"" + plural +
                  "\",\"pair\":\"{count, number} / {total, number}\",\"plain\":\"ready\"}");
        CHECK(rejects([&] { ribterra::Localization invalid(fixture.path); }));
    }
    {
        Fixture fixture;
        write(fixture.path / "pl.json", catalog(plural, "{count, number} / {amount, number}"));
        CHECK(rejects([&] { ribterra::Localization invalid(fixture.path); }));
    }
    {
        Fixture fixture;
        write(fixture.path / "pl.json", catalog("{count,number}"));
        CHECK(rejects([&] { ribterra::Localization invalid(fixture.path); }));
    }
    {
        Fixture fixture;
        auto invalid = catalog();
        const auto position = invalid.find("ready");
        invalid.replace(position, 5, std::string("\xC3\x28", 2));
        write(fixture.path / "ru.json", invalid);
        CHECK(rejects([&] { ribterra::Localization bad_utf8(fixture.path); }));
    }
}
} // namespace

int main() {
    plural_rules_and_arguments();
    failures_preserve_selection_and_pseudo_is_safe();
    malformed_catalogs_are_transactional();
    if (failures) {
        std::cerr << failures << " localization test(s) failed.\n";
        return 1;
    }
    std::cout << "Five-locale ICU plurals, signatures, failures and pseudolocalization passed.\n";
    return 0;
}

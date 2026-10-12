#include "ribterra/localization.hpp"
#include "ribterra/foundation.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <unicode/messagepattern.h>
#include <unicode/msgfmt.h>
#include <unicode/strenum.h>
#include <unicode/unistr.h>

namespace ribterra {
namespace {
std::string utf8(const icu::UnicodeString &text) {
    std::string out;
    text.toUTF8String(out);
    return out;
}
icu::UnicodeString unicode(std::string_view text) {
    if (!valid_utf8_text(text, 65536, true))
        throw std::invalid_argument("Localization text is not valid UTF-8");
    return icu::UnicodeString::fromUTF8(
        icu::StringPiece(text.data(), static_cast<std::int32_t>(text.size())));
}
std::string pseudolocalize(std::string_view text) {
    std::string result = "[!! ";
    for (char c : text) {
        switch (c) {
        case 'a':
            result += "áá";
            break;
        case 'e':
            result += "ëë";
            break;
        case 'i':
            result += "ïï";
            break;
        case 'o':
            result += "öö";
            break;
        case 'u':
            result += "üü";
            break;
        case 'A':
            result += "ÁÁ";
            break;
        case 'E':
            result += "ËË";
            break;
        default:
            result += c;
            break;
        }
    }
    return result + " !!]";
}
} // namespace

struct Localization::Impl {
    struct Message {
        std::unique_ptr<icu::MessageFormat> format;
        std::map<std::string, std::set<std::string>> arguments;
    };
    std::map<std::string, std::map<std::string, Message>> catalogs;
    std::string locale;
    bool pseudo{};

    explicit Impl(const std::filesystem::path &directory) {
        std::map<std::string, std::map<std::string, std::set<std::string>>> reference;
        for (const auto &language : Localization::supported_locales()) {
            const auto path = directory / (language + ".json");
            if (std::filesystem::file_size(path) > 1024 * 1024)
                throw std::runtime_error("Localization catalog exceeds 1 MiB");
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Could not read localization catalog: " + language);
            auto data = nlohmann::json::parse(stream);
            if (!data.is_object() || data.empty() || data.size() > 10000)
                throw std::runtime_error("Invalid localization catalog: " + language);
            std::map<std::string, Message> catalog;
            std::map<std::string, std::map<std::string, std::set<std::string>>> signature;
            for (const auto &[key, value] : data.items()) {
                if (key.empty() || key.size() > 128 || !value.is_string())
                    throw std::runtime_error("Invalid localization key/value: " + language);
                UErrorCode status = U_ZERO_ERROR;
                UParseError parse_error{};
                Message message;
                message.format =
                    std::make_unique<icu::MessageFormat>(unicode(value.get_ref<const std::string &>()),
                                                         icu::Locale(language.c_str()), parse_error, status);
                if (U_FAILURE(status))
                    throw std::runtime_error("Invalid ICU pattern " + language + ":" + key + " at " +
                                             std::to_string(parse_error.offset) + ": " + u_errorName(status));
                icu::MessagePattern pattern(unicode(value.get_ref<const std::string &>()), &parse_error,
                                            status);
                if (U_FAILURE(status))
                    throw std::runtime_error("Could not inspect ICU arguments: " + key);
                for (std::int32_t i = 0; i < pattern.countParts(); ++i) {
                    const auto &part = pattern.getPart(i);
                    if (part.getType() != UMSGPAT_PART_TYPE_ARG_START)
                        continue;
                    const auto name = utf8(pattern.getSubstring(pattern.getPart(i + 1)));
                    auto type = std::to_string(part.getArgType());
                    if (part.getArgType() == UMSGPAT_ARG_TYPE_SIMPLE)
                        type += ":" + utf8(pattern.getSubstring(pattern.getPart(i + 2)));
                    message.arguments[name].insert(std::move(type));
                }
                signature.emplace(key, message.arguments);
                catalog.emplace(key, std::move(message));
            }
            if (language == "en")
                reference = signature;
            else if (signature != reference)
                throw std::runtime_error("Localization keys/arguments differ from English: " + language);
            catalogs.emplace(language, std::move(catalog));
        }
    }
};

Localization::Localization(const std::filesystem::path &directory, std::string locale)
    : impl_(std::make_unique<Impl>(directory)) {
    set_locale(locale);
}
Localization::~Localization() = default;
void Localization::set_locale(std::string_view locale) {
    if (!impl_->catalogs.contains(std::string(locale)))
        throw std::invalid_argument("Unsupported locale: " + std::string(locale));
    impl_->locale = locale;
}
const std::string &Localization::locale() const {
    return impl_->locale;
}
std::string Localization::text(std::string_view key) const {
    return format(key, {});
}
std::string Localization::text_or_literal(std::string_view message) const {
    const auto &catalog = impl_->catalogs.at(impl_->locale);
    return catalog.contains(std::string(message)) ? text(message) : std::string(message);
}
std::string Localization::format(std::string_view key, const Arguments &arguments) const {
    const auto &catalog = impl_->catalogs.at(impl_->locale);
    const auto found = catalog.find(std::string(key));
    if (found == catalog.end())
        throw std::out_of_range("Missing localization key: " + std::string(key));
    std::set<std::string> supplied;
    std::vector<icu::UnicodeString> names;
    std::vector<icu::Formattable> values;
    for (const auto &[name, value] : arguments) {
        supplied.insert(name);
        names.push_back(unicode(name));
        if (const auto *number = std::get_if<double>(&value.value)) {
            if (!std::isfinite(*number))
                throw std::invalid_argument("Localization arguments must be finite");
            values.emplace_back(*number);
        } else {
            const auto &argument = std::get<std::string>(value.value);
            // Optional message fragments may be empty. Catalog patterns and
            // argument names still use the strict non-empty text validator.
            values.emplace_back(argument.empty() ? icu::UnicodeString{} : unicode(argument));
        }
    }
    std::set<std::string> required;
    for (const auto &[name, types] : found->second.arguments) {
        (void)types;
        required.insert(name);
    }
    if (supplied != required)
        throw std::invalid_argument("Localization arguments do not match key: " + std::string(key));
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString formatted;
    // ICU permits a null argument vector when count is zero.
    found->second.format->format(names.data(), values.data(), static_cast<std::int32_t>(values.size()),
                                 formatted, status);
    if (U_FAILURE(status))
        throw std::runtime_error("ICU formatting failed: " + std::string(u_errorName(status)));
    auto result = utf8(formatted);
    if (result.size() > 65536)
        throw std::length_error("Formatted localization text exceeds 64 KiB");
    return impl_->pseudo ? pseudolocalize(result) : result;
}
void Localization::set_pseudolocalized(bool enabled) {
    impl_->pseudo = enabled;
}
std::vector<std::string> Localization::supported_locales() {
    return {"en", "uk", "ru", "pl", "es"};
}

} // namespace ribterra

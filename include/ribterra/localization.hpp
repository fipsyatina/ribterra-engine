#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ribterra {

class Localization {
  public:
    struct Argument {
        std::variant<double, std::string> value;
        template <class Number>
            requires std::is_arithmetic_v<Number>
        Argument(Number number) : value(static_cast<double>(number)) {}
        Argument(std::string text) : value(std::move(text)) {}
        Argument(const char *text) : value(std::string(text)) {}
    };
    using Arguments = std::unordered_map<std::string, Argument>;
    explicit Localization(const std::filesystem::path &directory, std::string locale = "en");
    ~Localization();
    Localization(const Localization &) = delete;
    Localization &operator=(const Localization &) = delete;
    void set_locale(std::string_view locale);
    const std::string &locale() const;
    // Named ICU MessageFormat arguments support numeric plurals and text.
    // Missing keys or malformed messages are reported, not hidden.
    std::string text(std::string_view key) const;
    // Legacy command/status channels contain both catalog keys and finished text.
    // Use only at that boundary; UI labels should retain strict text()/format().
    std::string text_or_literal(std::string_view message) const;
    std::string format(std::string_view key, const Arguments &arguments) const;
    void set_pseudolocalized(bool enabled);
    static std::vector<std::string> supported_locales();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ribterra

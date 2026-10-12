#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <unicode/unistr.h>

namespace {
void check_glyphs(const std::filesystem::path &directory, const std::filesystem::path &font_path) {
    std::ifstream font_file(font_path, std::ios::binary);
    if (!font_file || std::filesystem::file_size(font_path) > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid localization font file");
    const std::vector<unsigned char> font_bytes((std::istreambuf_iterator<char>(font_file)), {});
    FT_Library library{};
    if (FT_Init_FreeType(&library))
        throw std::runtime_error("Could not initialize FreeType validation");
    struct LibraryGuard {
        FT_Library value;
        ~LibraryGuard() { FT_Done_FreeType(value); }
    } library_guard{library};
    FT_Face face{};
    if (FT_New_Memory_Face(library, font_bytes.data(), static_cast<FT_Long>(font_bytes.size()), 0,
                           &face))
        throw std::runtime_error("Could not load localization font");
    struct FaceGuard {
        FT_Face value;
        ~FaceGuard() { FT_Done_Face(value); }
    } face_guard{face};
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE))
        throw std::runtime_error("Localization font has no Unicode charmap");
    std::set<UChar32> checked;
    for (const auto &locale : ribterra::Localization::supported_locales()) {
        std::ifstream input(directory / (locale + ".json"));
        const auto catalog = nlohmann::json::parse(input);
        for (const auto &[key, value] : catalog.items()) {
            const auto text = icu::UnicodeString::fromUTF8(value.get<std::string>());
            for (std::int32_t i = 0; i < text.length();) {
                const auto codepoint = text.char32At(i);
                i += U16_LENGTH(codepoint);
                if (codepoint < 32 || !checked.insert(codepoint).second)
                    continue;
                if (!FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint)))
                    throw std::runtime_error("Missing font glyph " + std::to_string(codepoint) +
                                             " in " + locale + ":" + key);
            }
        }
    }
    std::cout << "Font contains all " << checked.size()
              << " codepoints used by the five locale catalogs.\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        if (args.size() != 2 && args.size() != 3)
            return 2;
        const auto path = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(args[1].data()), args[1].size()));
        ribterra::Localization catalog(path);
        if (args.size() == 3)
            check_glyphs(path,
                         std::filesystem::path(std::u8string(
                             reinterpret_cast<const char8_t *>(args[2].data()), args[2].size())));
        std::cout << "Five locale catalogs passed ICU pattern/key/argument validation.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

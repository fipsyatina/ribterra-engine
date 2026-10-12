#include "ribterra/texture_atlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace ribterra;
int failures{};
#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #value "\n";            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)
template <class Function> bool rejects(Function &&function) {
    try {
        function();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}

AtlasImage solid(std::string id, std::uint32_t width, std::uint32_t height,
                 std::array<std::uint8_t, 4> color) {
    AtlasImage image{std::move(id), width, height,
                     std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
        std::copy(color.begin(), color.end(), image.rgba.begin() + i);
    return image;
}
std::array<std::uint8_t, 4> pixel(const AtlasPage &page, std::uint32_t x, std::uint32_t y) {
    const auto offset = (static_cast<std::size_t>(y) * page.width + x) * 4;
    return {page.rgba[offset], page.rgba[offset + 1], page.rgba[offset + 2], page.rgba[offset + 3]};
}
const AtlasEntry &entry(const TextureAtlas &atlas, std::string_view id) {
    const auto found = std::find_if(atlas.entries.begin(), atlas.entries.end(),
                                    [&](const auto &value) { return value.id == id; });
    if (found == atlas.entries.end())
        throw std::runtime_error("Missing expected atlas entry");
    return *found;
}
bool same(const TextureAtlas &a, const TextureAtlas &b) {
    if (a.padding != b.padding || a.pages.size() != b.pages.size() ||
        a.entries.size() != b.entries.size())
        return false;
    for (std::size_t i = 0; i < a.pages.size(); ++i)
        if (a.pages[i].width != b.pages[i].width || a.pages[i].height != b.pages[i].height ||
            a.pages[i].rgba != b.pages[i].rgba)
            return false;
    for (std::size_t i = 0; i < a.entries.size(); ++i) {
        const auto &x = a.entries[i];
        const auto &y = b.entries[i];
        if (x.id != y.id || x.page != y.page || x.rect.x != y.rect.x || x.rect.y != y.rect.y ||
            x.rect.width != y.rect.width || x.rect.height != y.rect.height || x.uv.u0 != y.uv.u0 ||
            x.uv.v0 != y.uv.v0 || x.uv.u1 != y.uv.u1 || x.uv.v1 != y.uv.v1 ||
            x.pivot.x != y.pivot.x || x.pivot.y != y.pivot.y)
            return false;
    }
    return true;
}

void pixels_extrusion_and_determinism() {
    AtlasImage pattern{"pattern",
                       2,
                       2,
                       {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 128},
                       {0.25f, 0.75f}};
    auto blue = solid("blue", 2, 3, {5, 10, 200, 255});
    std::vector<AtlasImage> forward{pattern, blue}, reverse{blue, pattern};
    const auto first = pack_texture_atlas(forward, {8, 1, 4});
    const auto second = pack_texture_atlas(reverse, {8, 1, 4});
    CHECK(same(first, second));
    CHECK(first.entries[0].id == "blue" && first.entries[1].id == "pattern");
    const auto &placed = entry(first, "pattern");
    const auto &page = first.pages[placed.page];
    CHECK(
        (pixel(page, placed.rect.x, placed.rect.y) == std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    CHECK((pixel(page, placed.rect.x + 1, placed.rect.y) ==
           std::array<std::uint8_t, 4>{0, 255, 0, 255}));
    CHECK((pixel(page, placed.rect.x, placed.rect.y + 1) ==
           std::array<std::uint8_t, 4>{0, 0, 255, 255}));
    CHECK((pixel(page, placed.rect.x + 1, placed.rect.y + 1) ==
           std::array<std::uint8_t, 4>{255, 255, 0, 128}));
    CHECK((pixel(page, placed.rect.x - 1, placed.rect.y - 1) ==
           std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    CHECK((pixel(page, placed.rect.x + placed.rect.width, placed.rect.y - 1) ==
           std::array<std::uint8_t, 4>{0, 255, 0, 255}));
    CHECK((pixel(page, placed.rect.x - 1, placed.rect.y + placed.rect.height) ==
           std::array<std::uint8_t, 4>{0, 0, 255, 255}));
    CHECK((pixel(page, placed.rect.x + placed.rect.width, placed.rect.y + placed.rect.height) ==
           std::array<std::uint8_t, 4>{255, 255, 0, 128}));
    CHECK(placed.pivot.x == .25f && placed.pivot.y == .75f);
    CHECK(placed.uv.u0 >= 0 && placed.uv.v0 >= 0 && placed.uv.u1 <= 1 && placed.uv.v1 <= 1);
    for (std::size_t i = 0; i < first.entries.size(); ++i)
        for (std::size_t j = i + 1; j < first.entries.size(); ++j)
            if (first.entries[i].page == first.entries[j].page) {
                const auto &a = first.entries[i].rect;
                const auto &b = first.entries[j].rect;
                const auto p = first.padding;
                CHECK(a.x + a.width + p <= b.x - p || b.x + b.width + p <= a.x - p ||
                      a.y + a.height + p <= b.y - p || b.y + b.height + p <= a.y - p);
            }
}

void pages_and_failures() {
    std::vector<AtlasImage> large{solid("a", 6, 6, {1, 2, 3, 4}), solid("b", 6, 6, {2, 3, 4, 5}),
                                  solid("c", 6, 6, {3, 4, 5, 6})};
    const auto atlas = pack_texture_atlas(large, {8, 1, 3});
    CHECK(atlas.pages.size() == 3);
    CHECK(rejects([&] { (void)pack_texture_atlas(large, {8, 1, 2}); }));
    auto duplicate = large;
    duplicate[1].id = "a";
    CHECK(rejects([&] { (void)pack_texture_atlas(duplicate, {8, 1, 3}); }));
    auto bad_bytes = solid("bad", 2, 2, {0, 0, 0, 0});
    bad_bytes.rgba.pop_back();
    CHECK(rejects([&] { (void)pack_texture_atlas({&bad_bytes, 1}, {8, 1, 1}); }));
    auto zero = solid("zero", 1, 1, {0, 0, 0, 0});
    zero.width = 0;
    CHECK(rejects([&] { (void)pack_texture_atlas({&zero, 1}, {8, 1, 1}); }));
    auto oversized = solid("oversized", 7, 1, {0, 0, 0, 0});
    CHECK(rejects([&] { (void)pack_texture_atlas({&oversized, 1}, {8, 1, 1}); }));
    auto pivot = solid("pivot", 1, 1, {0, 0, 0, 0});
    pivot.pivot.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(rejects([&] { (void)pack_texture_atlas({&pivot, 1}, {8, 1, 1}); }));
    CHECK(rejects([&] { (void)pack_texture_atlas({}, {7, 1, 1}); }));
    CHECK(rejects([&] { (void)pack_texture_atlas({}, {8192, 1, 1}); }));
    CHECK(rejects([&] { (void)pack_texture_atlas({}, {8, 4, 1}); }));
    CHECK(rejects([&] { (void)pack_texture_atlas({}, {8, 1, 0}); }));
}
} // namespace

int main() {
    pixels_extrusion_and_determinism();
    pages_and_failures();
    if (failures) {
        std::cerr << failures << " texture atlas test(s) failed.\n";
        return 1;
    }
    std::cout << "Deterministic atlas packing, extrusion, pages and bounds passed.\n";
    return 0;
}

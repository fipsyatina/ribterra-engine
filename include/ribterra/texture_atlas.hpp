#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ribterra {

struct AtlasPivot {
    float x{0.5f}, y{0.5f};
};
struct AtlasImage {
    std::string id;
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
    AtlasPivot pivot{}; // Normalized image coordinates.
};
struct AtlasRect {
    std::uint32_t x{}, y{}, width{}, height{};
};
struct AtlasUv {
    float u0{}, v0{}, u1{}, v1{};
};
struct AtlasEntry {
    std::string id;
    std::uint32_t page{};
    AtlasRect rect{}; // Excludes padding.
    AtlasUv uv{};
    AtlasPivot pivot{};
};
struct AtlasPage {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
};
struct TextureAtlas {
    std::vector<AtlasPage> pages;
    std::vector<AtlasEntry> entries; // Sorted by stable ID.
    std::uint32_t padding{};
};
struct AtlasOptions {
    std::uint32_t page_size{2048}; // Power of two, at most 4096.
    std::uint32_t padding{1};
    std::uint32_t max_pages{16};
};

// Deterministic shelf packing with no rotation. Padding is filled by edge
// extrusion so linear sampling cannot pull transparent seams into a sprite.
TextureAtlas pack_texture_atlas(std::span<const AtlasImage> images, AtlasOptions options = {});

} // namespace ribterra

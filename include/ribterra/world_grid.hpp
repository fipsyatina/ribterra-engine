#pragma once
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace ribterra {
inline constexpr int chunk_size = 32, world_size = 832, world_min = -96, world_max = world_min + world_size;
inline constexpr int world_chunk_count = 2 * (world_size / chunk_size) * (world_size / chunk_size);
inline constexpr float starting_vision_radius = 16.5f;
inline constexpr float resident_vision_radius = 4.5f;
inline constexpr float entrance_vision_radius = 6.f;

enum class Biome : std::uint8_t {
    Meadow,
    Forest,
    Lake,
    Mountain,
    SnowMeadow,
    SnowForest,
    SnowLake,
    SnowMountain,
    Desert
};
constexpr bool snowy(Biome b) {
    return b >= Biome::SnowMeadow && b <= Biome::SnowMountain;
}
constexpr bool valid_chunk_position(int cx, int cy) {
    return cx >= world_min / chunk_size && cy >= world_min / chunk_size &&
           cx <= (world_max - 1) / chunk_size && cy <= (world_max - 1) / chunk_size;
}
enum class WorldLayer : std::uint8_t { Surface, Mine };
enum class Terrain : int { Grass, Water, Forest, Rock, MineFloor, MineOre };
struct Tile {
    Terrain terrain{Terrain::Grass};
    int amount{};
    bool explored{};
    Biome biome{Biome::Meadow};
    std::int16_t elevation{}; // Vertical world units; old saves default to flat ground.
    std::uint8_t trail_wear{}; // Authoritative desire-path wear; cosmetic, no speed bonus.
    friend bool operator==(const Tile &, const Tile &) = default;
};
struct CellCoord {
    int x{}, y{};
    WorldLayer layer{WorldLayer::Surface};
    friend bool operator==(const CellCoord &, const CellCoord &) = default;
};
struct ChunkKey {
    WorldLayer layer{WorldLayer::Surface};
    int cx{}, cy{};
    friend auto operator<=>(const ChunkKey &, const ChunkKey &) = default;
};
struct Chunk {
    ChunkKey key;
    std::uint64_t revision{};
    std::array<Tile, chunk_size * chunk_size> tiles{};
    friend bool operator==(const Chunk &, const Chunk &) = default;
};
struct Portal {
    CellCoord surface, mine;
    friend bool operator==(const Portal &, const Portal &) = default;
};
constexpr bool valid_layer(WorldLayer l) {
    return l == WorldLayer::Surface || l == WorldLayer::Mine;
}
constexpr bool inside_world(int x, int y) {
    return x >= world_min && y >= world_min && x < world_max && y < world_max;
}
constexpr int floor_div(int v, int d) {
    const int q = v / d;
    return v < 0 && q * d != v ? q - 1 : q;
}
constexpr ChunkKey chunk_key(CellCoord c) {
    return {c.layer, floor_div(c.x, chunk_size), floor_div(c.y, chunk_size)};
}
constexpr std::size_t local_index(int x, int y) {
    const int lx = x - floor_div(x, chunk_size) * chunk_size, ly = y - floor_div(y, chunk_size) * chunk_size;
    return (std::size_t)(ly * chunk_size + lx);
}
constexpr std::optional<std::size_t> world_cell_index(int x, int y) {
    if (!inside_world(x, y))
        return {};
    return (std::size_t)(y - world_min) * world_size + (std::size_t)(x - world_min);
}
constexpr std::optional<CellCoord> cell_from_index(std::size_t i, WorldLayer l = WorldLayer::Surface) {
    if (i >= (std::size_t)world_size * world_size || !valid_layer(l))
        return {};
    return CellCoord{world_min + (int)(i % world_size), world_min + (int)(i / world_size), l};
}
} // namespace ribterra

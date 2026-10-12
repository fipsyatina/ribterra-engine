#pragma once

#include "ribterra/terrain_rules.hpp"
#include "ribterra/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <utility>

namespace ribterra::test {
struct ReachableFishingSite {
    int x{};
    int y{};
    CellCoord production_approach{};
};

inline bool reveal_surface_radius(World &world, int radius, std::string &error) {
    // Test setup only, not a procedural-generation repair: seed 70421 has
    // buildable shore in its initial fog, but those cells are on the opposite
    // bank. Expand knowledge without changing any generated cell content.
    if (radius < 0 || world.residents().empty()) {
        error = "Fishing fixture has no resident-centered reveal origin";
        return false;
    }
    const auto &resident = world.residents().front();
    if (resident.layer != WorldLayer::Surface) {
        error = "Fishing fixture resident is not on the surface";
        return false;
    }
    const CellCoord center{static_cast<int>(std::lround(resident.x)),
                           static_cast<int>(std::lround(resident.y)), WorldLayer::Surface};
    const auto inside = [&](int x, int y) {
        const int dx = x - center.x;
        const int dy = y - center.y;
        return dx * dx + dy * dy <= radius * radius;
    };
    auto state = world.export_state();
    for (auto &chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface)
            continue;
        for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
            const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
            const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
            if (inside(x, y))
                chunk.tiles[i].explored = true;
        }
    }
    const auto nation = std::find_if(state.economy.nations.begin(), state.economy.nations.end(),
                                     [&](const auto &candidate) {
                                         return candidate.id == static_cast<int>(resident.nation);
                                     });
    if (nation == state.economy.nations.end() &&
        (!state.economy.nations.empty() || resident.nation != NationId{1})) {
        error = "Fishing fixture resident nation is missing";
        return false;
    }
    if (nation != state.economy.nations.end())
        for (int y = center.y - radius; y <= center.y + radius; ++y)
            for (int x = center.x - radius; x <= center.x + radius; ++x) {
                if (!inside(x, y) || x < world_min || x >= world_max ||
                    y < world_min || y >= world_max)
                    continue;
                const std::size_t bit = std::size_t(y - world_min) * world_size +
                                        std::size_t(x - world_min);
                auto &digit = nation->explored[bit / 4];
                const int nibble = digit <= '9' ? digit - '0' : digit - 'a' + 10;
                digit = "0123456789abcdef"[nibble | (1 << (bit % 4))];
            }
    ++state.topology_revision;
    return world.import_state(std::move(state), error);
}

inline std::optional<ReachableFishingSite> reachable_fishing_site(const World &world) {
    if (world.residents().empty()) return std::nullopt;
    const auto &resident = world.residents().front();
    if (resident.layer != WorldLayer::Surface) return std::nullopt;
    const CellCoord start{static_cast<int>(std::lround(resident.x)),
                          static_cast<int>(std::lround(resident.y)), WorldLayer::Surface};
    constexpr std::array<std::pair<int, int>, 4> directions{{
        {0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
    std::set<std::pair<int, int>> reached{{start.x, start.y}};
    std::queue<CellCoord> frontier;
    frontier.push(start);
    while (!frontier.empty()) {
        const auto current = frontier.front();
        frontier.pop();
        for (const auto [dx, dy] : directions) {
            const CellCoord next{current.x + dx, current.y + dy, WorldLayer::Surface};
            if (next.x < world_min || next.x >= world_max || next.y < world_min || next.y >= world_max ||
                reached.contains({next.x, next.y}) || !world.tile(next.x, next.y).explored ||
                !world.walkable(next.x, next.y) ||
                !surface_edge_traversable(world.tile(current.x, current.y),
                                          world.tile(next.x, next.y)))
                continue;
            reached.insert({next.x, next.y});
            frontier.push(next);
        }
    }
    const auto approach_reachable_without = [&](CellCoord blocked, CellCoord target) {
        if (start == blocked)
            return false;
        std::set<std::pair<int, int>> visited{{start.x, start.y}};
        std::queue<CellCoord> pending;
        pending.push(start);
        while (!pending.empty()) {
            const auto current = pending.front();
            pending.pop();
            if (current == target)
                return true;
            for (const auto [dx, dy] : directions) {
                const CellCoord next{current.x + dx, current.y + dy, WorldLayer::Surface};
                if (next == blocked || next.x < world_min || next.x >= world_max ||
                    next.y < world_min || next.y >= world_max ||
                    visited.contains({next.x, next.y}) || !world.tile(next.x, next.y).explored ||
                    !world.walkable(next.x, next.y) ||
                    !surface_edge_traversable(world.tile(current.x, current.y),
                                              world.tile(next.x, next.y)))
                    continue;
                visited.insert({next.x, next.y});
                pending.push(next);
            }
        }
        return false;
    };
    for (const auto &[x, y] : reached) {
        if (!world.can_build(BuildingKind::Fishing, x, y))
            continue;
        std::optional<CellCoord> approach;
        for (const auto [dx, dy] : directions) {
            const CellCoord candidate{x + dx, y + dy, WorldLayer::Surface};
            if (world.walkable(candidate.x, candidate.y)) {
                // Production uses the first walkable neighbor in this exact order.
                approach = candidate;
                break;
            }
        }
        const CellCoord blocked{x, y, WorldLayer::Surface};
        if (approach && approach_reachable_without(blocked, *approach))
            return ReachableFishingSite{x, y, *approach};
    }
    return std::nullopt;
}
} // namespace ribterra::test

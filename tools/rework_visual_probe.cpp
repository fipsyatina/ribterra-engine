#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/seasons.hpp"
#include "ribterra/terrain_rules.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <sstream>
#include <variant>
#include <vector>

using namespace ribterra;
namespace {
constexpr std::uint64_t fixture_seed = 70421;

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

struct MatrixCase {
    const char *locale;
    float scale;
    int width;
    int height;
};

struct FixturePaths {
    std::filesystem::path staged;
    std::filesystem::path winter;
    int stockpile{};
    std::size_t trail_tiles{};
    std::uint8_t max_trail_wear{};
    std::uint64_t total_trail_wear{};
};

FixturePaths prepare_fixture(const std::filesystem::path &output) {
    World ordinary(fixture_seed, 10);
    std::string error;
    const auto ordinary_save = output / "ordinary-start.save";
    check(ordinary.save(ordinary_save, error), error);

    World staged_source(fixture_seed, 10);
    const auto initial_inventory = staged_source.inventory();
    const CellCoord plaza{24, 26, WorldLayer::Surface};
    check(staged_source.walkable(plaza.x, plaza.y, plaza.layer),
          "Staged settlement plaza is not walkable");
    // A flat buildable bank may be on the other side of the river. Keep the
    // fixture on the plaza's already-known surface component, using the same
    // edge rule as production movement. This is site selection only: every
    // selected route is still physically walked by the real planner below.
    // There are no bridges in this ordinary start; never invent a crossing.
    check(std::none_of(staged_source.buildings().begin(), staged_source.buildings().end(),
                       [](const auto &building) { return building.kind == BuildingKind::Bridge; }),
          "Fixture reachability assumes an ordinary start without bridges");
    std::vector<bool> reachable(static_cast<std::size_t>(world_size) * world_size, false);
    std::vector<CellCoord> frontier{plaza};
    reachable[*world_cell_index(plaza.x, plaza.y)] = true;
    for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
        const auto from = frontier[cursor];
        for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}}) {
            const CellCoord to{from.x + dx, from.y + dy, WorldLayer::Surface};
            const auto index = world_cell_index(to.x, to.y);
            if (!index || reachable[*index] || !staged_source.tile(to.x, to.y).explored ||
                !staged_source.walkable(to.x, to.y, to.layer) ||
                !surface_edge_traversable(staged_source.tile(from.x, from.y),
                                          staged_source.tile(to.x, to.y)))
                continue;
            reachable[*index] = true;
            frontier.push_back(to);
        }
    }

    struct StagedSite {
        BuildingKind kind;
        CellCoord cell;
        bool level_two{};
    };
    // A compact, layered village rather than a presentation lineup. In
    // isometric projection x-y moves across the frame and x+y moves down it:
    // the mill/industry form the river-side back layer, services surround the
    // existing camp, and houses/kitchen form the near layer. Candidate cells
    // still have to pass the real generated-world placement policy below.
    const std::array<std::pair<BuildingKind, CellCoord>, 7> desired_sites{{
        {BuildingKind::Mill, {15, 22, WorldLayer::Surface}},
        {BuildingKind::Workshop, {22, 19, WorldLayer::Surface}},
        {BuildingKind::Farm, {28, 20, WorldLayer::Surface}},
        {BuildingKind::Stockpile, {17, 28, WorldLayer::Surface}},
        {BuildingKind::House, {28, 24, WorldLayer::Surface}},
        {BuildingKind::House, {28, 28, WorldLayer::Surface}},
        {BuildingKind::Kitchen, {24, 29, WorldLayer::Surface}},
    }};
    std::vector<StagedSite> sites;
    sites.reserve(desired_sites.size());
    const auto nearest_terrain = [&](int x, int y, Terrain wanted, int radius) {
        float distance = float(radius + 1);
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox)
                if (inside_world(x + ox, y + oy) &&
                    staged_source.tile(x + ox, y + oy).terrain == wanted)
                    distance = std::min(distance, std::hypot(float(ox), float(oy)));
        return distance;
    };
    const auto projected_separated = [](int x, int y, int other_x, int other_y) {
        const float across = std::abs(float((x - y) - (other_x - other_y)));
        const float down = std::abs(float((x + y) - (other_x + other_y))) * .5f;
        return across >= 2.25f || down >= 2.25f;
    };
    const auto separated = [&](int x, int y) {
        // Authored plates may overlap slightly in depth, as they do in the
        // target village, but their ground anchors and silhouettes must remain
        // individually legible. The camp at 24,24 participates in the same
        // rule even though it already exists in the ordinary start.
        if (std::hypot(float(x - 24), float(y - 24)) < 3.75f ||
            !projected_separated(x, y, 24, 24))
            return false;
        // The stockpile is the selected acceptance subject. Any later-depth
        // plate needs a full anchor-width lane beside it; otherwise a tall
        // foreground roof can hide the stockpile while both rectangles still
        // technically remain inside the camera safe area.
        for (const auto &site : sites)
            if (site.kind == BuildingKind::Stockpile &&
                x + y >= site.cell.x + site.cell.y &&
                std::abs((x - y) - (site.cell.x - site.cell.y)) < 5)
                return false;
        return std::none_of(sites.begin(), sites.end(), [&](const auto &site) {
            return std::hypot(float(x - site.cell.x), float(y - site.cell.y)) < 3.75f ||
                   !projected_separated(x, y, site.cell.x, site.cell.y);
        });
    };
    const auto compact_with = [&](int x, int y) {
        int across_low = 0, across_high = 0;
        int down_low = 48, down_high = 48;
        const auto include = [&](int cell_x, int cell_y) {
            const int across = cell_x - cell_y;
            const int down = cell_x + cell_y;
            across_low = std::min(across_low, across);
            across_high = std::max(across_high, across);
            down_low = std::min(down_low, down);
            down_high = std::max(down_high, down);
        };
        for (const auto &site : sites)
            include(site.cell.x, site.cell.y);
        include(x, y);
        return across_high - across_low <= 19 && down_high - down_low <= 23;
    };
    for (const auto &[kind, desired] : desired_sites) {
        std::optional<CellCoord> best;
        float best_score = std::numeric_limits<float>::max();
        for (int radius = 0; radius <= 8; ++radius)
            for (int y = desired.y - radius; y <= desired.y + radius; ++y)
                for (int x = desired.x - radius; x <= desired.x + radius; ++x) {
                    const auto index = world_cell_index(x, y);
                    if (std::max(std::abs(x - desired.x), std::abs(y - desired.y)) != radius ||
                        !index || !reachable[*index] ||
                        !separated(x, y) || !compact_with(x, y) ||
                        !staged_source.can_build(kind, x, y))
                        continue;
                    float score = std::hypot(float(x - desired.x), float(y - desired.y));
                    // The mill is intentionally the river-side anchor. This remains a
                    // preference only: World::can_build is still the placement authority.
                    if (kind == BuildingKind::Mill)
                        score += nearest_terrain(x, y, Terrain::Water, 10) * 1.5f;
                    // The workshop remains embedded in the resource edge instead of
                    // being arranged in a detached building lineup.
                    if (kind == BuildingKind::Workshop)
                        score += nearest_terrain(x, y, Terrain::Forest, 10);
                    if (score < best_score) {
                        best_score = score;
                        best = CellCoord{x, y, WorldLayer::Surface};
                    }
                }
        check(best.has_value(), std::string("No truthful staged site for ") +
                                    std::string(building_name(kind)));
        check(staged_source.can_build(kind, best->x, best->y),
              "Staged site did not pass World::can_build");
        if (kind == BuildingKind::Mill)
            check(nearest_terrain(best->x, best->y, Terrain::Water, 10) <= 8.f,
                  "Staged mill is not near the generated river");
        if (kind == BuildingKind::Workshop)
            check(nearest_terrain(best->x, best->y, Terrain::Forest, 10) <= 8.f,
                  "Staged workshop is not near generated forest");
        sites.push_back({kind, *best, kind == BuildingKind::Workshop ||
                                          (kind == BuildingKind::House &&
                                           std::none_of(sites.begin(), sites.end(), [](const auto &site) {
                                               return site.kind == BuildingKind::House;
                                           }))});
    }
    const auto projection_span = [&](bool across) {
        int low = across ? 0 : 48;
        int high = low;
        for (const auto &site : sites) {
            const int value = across ? site.cell.x - site.cell.y : site.cell.x + site.cell.y;
            low = std::min(low, value);
            high = std::max(high, value);
        }
        return high - low;
    };
    check(projection_span(true) <= 19 && projection_span(false) <= 23,
          "Truthful staged sites drifted out of the compact village composition");

    const auto terrain_before = staged_source.chunks();
    std::vector<int> resident_ids;
    for (const auto &resident : staged_source.residents())
        resident_ids.push_back(resident.id);
    const auto walk_group = [&](CellCoord destination) {
        const auto result = staged_source.move(resident_ids, destination.x, destination.y,
                                               destination.layer);
        check(result.accepted, "Truthful fixture resident move was rejected");
        bool planned = false;
        for (int tick = 0; tick < 4000; ++tick) {
            staged_source.tick();
            for (const auto &outcome : staged_source.take_path_results())
                if (outcome.path_batch_id == result.path_batch_id) {
                    check(outcome.accepted, "Truthful fixture path to " +
                        std::to_string(destination.x) + "," + std::to_string(destination.y) +
                        " could not be planned: " + outcome.message +
                        " (failure " + std::to_string(static_cast<unsigned>(outcome.failure)) + ")");
                    planned = true;
                }
            const bool arrived = std::all_of(
                staged_source.residents().begin(), staged_source.residents().end(),
                [&](const auto &resident) {
                    return resident.order_phase == OrderPhase::Idle &&
                           std::hypot(resident.x - destination.x, resident.y - destination.y) < .2f;
                });
            if (planned && arrived)
                return;
        }
        throw std::runtime_error("Truthful fixture resident walk exceeded its bounded time");
    };

    // Residents physically walk from the central plaza to every service and
    // return. Their repeated routes reveal the view and create authoritative
    // desire paths; the fixture never writes exploration or trail wear.
    walk_group(plaza);
    for (const auto &site : sites) {
        walk_group(site.cell);
        walk_group(plaza);
    }

    const auto &terrain_after = staged_source.chunks();
    check(terrain_before.size() == terrain_after.size(), "Fixture changed terrain chunk count");
    std::uint64_t wear_before = 0, wear_after = 0;
    std::size_t trail_tiles = 0;
    std::uint8_t max_trail_wear = 0;
    for (std::size_t chunk = 0; chunk < terrain_before.size(); ++chunk) {
        check(terrain_before[chunk].key == terrain_after[chunk].key &&
                  terrain_before[chunk].tiles.size() == terrain_after[chunk].tiles.size(),
              "Fixture changed terrain topology");
        for (std::size_t tile = 0; tile < terrain_before[chunk].tiles.size(); ++tile) {
            const auto &before = terrain_before[chunk].tiles[tile];
            const auto &after = terrain_after[chunk].tiles[tile];
            check(before.terrain == after.terrain && before.elevation == after.elevation &&
                      before.biome == after.biome && before.amount == after.amount,
                  "Fixture rewrote generated terrain or resources");
            wear_before += before.trail_wear;
            wear_after += after.trail_wear;
            if (after.trail_wear) {
                ++trail_tiles;
                max_trail_wear = std::max(max_trail_wear, after.trail_wear);
            }
        }
    }
    check(wear_after > wear_before, "Resident routes did not create real desire paths");
    const auto &walked_residents = staged_source.residents();
    check(walked_residents.size() == 10 &&
              std::all_of(walked_residents.begin(), walked_residents.end(), [](const auto &resident) {
                  return resident.health > 0 && resident.order_phase == OrderPhase::Idle;
              }),
          "Bounded fixture walks did not preserve ten living, available residents");
    const auto walked_inventory = staged_source.inventory();
    check(walked_inventory.total.wood == initial_inventory.total.wood &&
              walked_inventory.total.stone == initial_inventory.total.stone &&
              walked_inventory.total.food > 0 &&
              walked_inventory.total.food <= initial_inventory.total.food,
          "Bounded fixture walks changed non-consumable resources or exhausted food");

    auto state = staged_source.export_state();
    auto add = [&](BuildingKind kind, int x, int y) {
        const auto occupied = std::find_if(state.buildings.begin(), state.buildings.end(),
                                           [&](const auto &building) {
                                               return building.x == x && building.y == y &&
                                                      building.layer == WorldLayer::Surface;
                                           });
        check(occupied == state.buildings.end(), "Staged fixture position is occupied");
        const int id = state.next_building_id++;
        state.buildings.push_back({id, kind, x, y, 1, WorldLayer::Surface, 1});
        if (kind == BuildingKind::Stockpile)
            state.storages.push_back({id, 5000, 7, 0});
        return id;
    };

    // Buildings are explicitly staged as complete only after their locations
    // passed the production placement policy on untouched generated terrain.
    int stockpile = 0;
    for (const auto &site : sites) {
        const int id = add(site.kind, site.cell.x, site.cell.y);
        if (site.kind == BuildingKind::Stockpile)
            stockpile = id;
        if (site.level_two)
            state.economy.upgrades.push_back({id, 2});
    }
    check(stockpile != 0, "Staged settlement has no stockpile");

    World staged(fixture_seed, 0);
    check(staged.import_state(std::move(state), error), error);
    for (int tick = 0; tick < 240; ++tick)
        staged.tick();
    const auto staged_save = output / "staged-v03.save";
    check(staged.save(staged_save, error), error);
    auto winter_state = staged.export_state();
    // Compare winter art in the same readable daylight conditions as the
    // summer target. This is the real calendar noon, not a lighting override.
    winter_state.ticks = 3 * days_per_season * ticks_per_day + ticks_per_day / 4;
    World winter(fixture_seed, 0);
    check(winter.import_state(std::move(winter_state), error), error);
    const auto winter_save = output / "staged-v03-winter.save";
    check(winter.save(winter_save, error), error);
    return {staged_save, winter_save, stockpile, trail_tiles, max_trail_wear, wear_after};
}

std::size_t ui_commands(const render::Frame2D &frame) {
    return std::count_if(frame.commands.begin(), frame.commands.end(), [](const auto &command) {
        return std::visit([](const auto &draw) { return draw.pass == render::Pass::Ui; }, command);
    });
}

render::Rect sprite_bounds(const nlohmann::json &catalog, std::string_view id,
                           render::Point ground, float width) {
    for (const auto &page : catalog.at("pages"))
        for (const auto &sprite : page.at("sprites"))
            if (sprite.at("id").get_ref<const std::string &>() == id) {
                const auto &rect = sprite.at("rect");
                const auto &pivot = sprite.at("pivot");
                const float height = width * rect[3].get<float>() / rect[2].get<float>();
                return {ground.x - width * pivot[0].get<float>(),
                        ground.y - height * pivot[1].get<float>(), width, height};
            }
    throw std::runtime_error("Authored sprite metadata is missing: " + std::string(id));
}

std::string utc_now() {
    const auto value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &value);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
}

render::Rect projected_world_geometry_bounds(const render::Frame2D &frame) {
    render::Rect bounds{};
    bool first = true;
    for (const auto &command : frame.commands) {
        const auto *geometry = std::get_if<render::GeometryDraw>(&command);
        if (!geometry || (geometry->pass != render::Pass::Terrain &&
                          geometry->pass != render::Pass::WorldOrdered))
            continue;
        const auto end = std::size_t(geometry->first_index) + geometry->index_count;
        check(end <= frame.indices.size(), "World geometry index range is invalid");
        for (std::size_t index = geometry->first_index; index < end; ++index) {
            const auto vertex_index = frame.indices[index];
            check(vertex_index < frame.vertices.size(), "World geometry vertex index is invalid");
            const auto point = frame.vertices[vertex_index].position;
            if (first) {
                bounds = {point.x, point.y, 0, 0};
                first = false;
            } else {
                const float right = std::max(bounds.x + bounds.w, point.x);
                const float bottom = std::max(bounds.y + bounds.h, point.y);
                bounds.x = std::min(bounds.x, point.x);
                bounds.y = std::min(bounds.y, point.y);
                bounds.w = right - bounds.x;
                bounds.h = bottom - bounds.y;
            }
        }
    }
    check(!first, "Ordinary seed capture contains no projected world geometry");
    return bounds;
}

nlohmann::json rect_json(const render::Rect &rect) {
    return {{"x", rect.x}, {"y", rect.y}, {"width", rect.w}, {"height", rect.h}};
}

void capture_ordinary_seeds(const std::filesystem::path &output,
                            const std::filesystem::path &base) {
    constexpr int capture_width = 1920;
    constexpr int capture_height = 1080;
    constexpr float capture_zoom = 1.30f;
    constexpr std::array<std::uint32_t, 8> seeds{{
        70421U, 4204434016U, 572988801U, 2035859997U,
        301968671U, 2247006640U, 1337U, 3735928559U,
    }};

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra ordinary seed evidence", capture_width, capture_height,
                         SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    check(window != nullptr, SDL_GetError());
    render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Direct3D12});
    const auto &device = renderer.device_info();
    check(!device.software && device.backend == "direct3d12",
          "Ordinary seed captures require native Direct3D 12");
    int drawable_width = 0, drawable_height = 0;
    check(SDL_GetWindowSizeInPixels(window.get(), &drawable_width, &drawable_height), SDL_GetError());
    check(drawable_width == capture_width && drawable_height == capture_height,
          "Drawable size differs from requested ordinary seed resolution");
    const auto logical_size = renderer.fitted_logical_size();
    renderer.set_logical_size(logical_size);

    const auto settings = output / "ordinary-seeds-settings.json";
    std::ofstream(settings) << nlohmann::json{{"locale", "uk"}, {"ui_scale", 1.f}}.dump(2);
    AssetCatalog assets(base / "assets");
    Localization localization(base / "assets/locales");
    GameUi ui(renderer, assets, localization, settings, logical_size);
    GameView view(renderer);
    view.set_logical_size(logical_size);
    view.load_art(assets);
    nlohmann::json captures = nlohmann::json::array();

    for (std::size_t seed_index = 0; seed_index < seeds.size(); ++seed_index) {
        const auto seed = seeds[seed_index];
        std::cout << '[' << seed_index + 1 << '/' << seeds.size() << "] ordinary seed " << seed
                  << '\n' << std::flush;
        // ClientSession's embedded GameServer constructs the production
        // World(seed, 10) directly. No save rewrite or presentation fixture
        // modifies the generated snapshot before capture.
        ClientSession session(seed);

        const auto camps = std::count_if(session.buildings().begin(), session.buildings().end(),
                                         [](const auto &building) {
                                             return building.kind == BuildingKind::Camp &&
                                                    building.layer == WorldLayer::Surface;
                                         });
        check(camps == 1 && session.buildings().size() == 1,
              "Ordinary seed fixture contains staged or duplicate buildings");
        check(session.residents().size() == 10, "Ordinary seed resident count changed");
        const auto camp = std::find_if(session.buildings().begin(), session.buildings().end(),
                                       [](const auto &building) {
                                           return building.kind == BuildingKind::Camp;
                                       });
        check(camp != session.buildings().end() && camp->x == 24 && camp->y == 24,
              "Fresh GameView camera no longer centres the ordinary Camp");

        view.reset_world_state();
        ui.reset_world_state();
        ui.show_world();
        render::Frame2D frame;
        view.compose(session, 0, frame, false);
        const auto geometry_bounds = projected_world_geometry_bounds(frame);

        render::Rect explored_bounds{};
        bool first_explored = true;
        std::size_t explored_tiles = 0;
        const float tile_half_width = 29.f * capture_zoom;
        const float tile_half_height = 14.5f * capture_zoom;
        for (const auto &chunk : session.chunks()) {
            if (chunk.key.layer != WorldLayer::Surface)
                continue;
            for (std::size_t tile_index = 0; tile_index < chunk.tiles.size(); ++tile_index) {
                if (!chunk.tiles[tile_index].explored)
                    continue;
                const int x = chunk.key.cx * chunk_size + int(tile_index % chunk_size);
                const int y = chunk.key.cy * chunk_size + int(tile_index / chunk_size);
                const auto center = view.tile_screen(float(x), float(y));
                const render::Rect diamond{center.x - tile_half_width,
                                           center.y - tile_half_height,
                                           tile_half_width * 2.f,
                                           tile_half_height * 2.f};
                if (first_explored) {
                    explored_bounds = diamond;
                    first_explored = false;
                } else {
                    const float right = std::max(explored_bounds.x + explored_bounds.w,
                                                 diamond.x + diamond.w);
                    const float bottom = std::max(explored_bounds.y + explored_bounds.h,
                                                  diamond.y + diamond.h);
                    explored_bounds.x = std::min(explored_bounds.x, diamond.x);
                    explored_bounds.y = std::min(explored_bounds.y, diamond.y);
                    explored_bounds.w = right - explored_bounds.x;
                    explored_bounds.h = bottom - explored_bounds.y;
                }
                ++explored_tiles;
            }
        }
        check(!first_explored, "Ordinary seed has no explored surface terrain");

        ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface);
        ui.update(&session, "");
        ui.compose(frame);
        const auto filename = "ordinary-seed-" + std::to_string(seed) +
                              "-uk-100-1920x1080.png";
        renderer.render(frame, output / filename);
        captures.push_back({{"file", filename},
                            {"seed", seed},
                            {"camp_count", camps},
                            {"resident_count", session.residents().size()},
                            {"building_count", session.buildings().size()},
                            {"explored_surface_tiles", explored_tiles},
                            {"projected_bounds",
                             {{"world_geometry", rect_json(geometry_bounds)},
                              {"explored_tile_diamonds", rect_json(explored_bounds)}}},
                            {"camera", {{"x", camp->x}, {"y", camp->y}, {"zoom", capture_zoom},
                                        {"mode", "fresh GameView Camp-centred default"}}},
                            {"logical_world", {{"width", logical_size.width},
                                               {"height", logical_size.height}}},
                            {"drawable", {{"width", drawable_width}, {"height", drawable_height}}},
                            {"locale", "uk"},
                            {"ui_scale", 1.f},
                            {"backend", device.backend},
                            {"device", device.device}});
    }

    const nlohmann::json report{{"schema", 1},
                                {"generated_utc", utc_now()},
                                {"probe", "ReworkVisualProbe ordinary seeds"},
                                {"runtime_evidence", true},
                                {"mode", "ordinary-seeds"},
                                {"content_sha256", assets.content_hash()},
                                {"build", {{"engine", engine_version},
                                           {"protocol", protocol_version},
                                           {"rules", rules_version},
                                           {"generator", generator_version}}},
                                {"backend", device.backend},
                                {"device", device.device},
                                {"fixture", {{"population", 10},
                                             {"terrain_edits", false},
                                             {"resource_edits", false},
                                             {"exploration_edits", false},
                                             {"staged_buildings", false},
                                             {"input_events", false}}},
                                {"capture", {{"locale", "uk"}, {"ui_scale", 1.f},
                                             {"width", capture_width}, {"height", capture_height},
                                             {"zoom", capture_zoom}}},
                                {"captures", captures}};
    std::ofstream(output / "ordinary-seeds-manifest.json") << report.dump(2);
    std::ofstream(output / "ordinary-seeds-report.json") << report.dump(2);
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = utf8_arguments(argc, argv);
        check(args.size() == 2 ||
                  (args.size() == 3 &&
                   (args[2] == "--full-matrix" || args[2] == "--ordinary-seeds" ||
                    args[2] == "--linear-hdr-world")),
              "Usage: ReworkVisualProbe output-directory [--full-matrix|--ordinary-seeds|--linear-hdr-world]");
        const bool full_matrix = args.size() == 3 && args[2] == "--full-matrix";
        const bool ordinary_seeds = args.size() == 3 && args[2] == "--ordinary-seeds";
        const bool linear_hdr_world = args.size() == 3 && args[2] == "--linear-hdr-world";
        const auto output = std::filesystem::absolute(path(args[1]));
        check(!std::filesystem::exists(output), "Output already exists; preserving prior visual evidence");
        std::filesystem::create_directories(output);
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto base = path(SDL_GetBasePath());
        if (ordinary_seeds) {
            capture_ordinary_seeds(output, base);
            SDL_Quit();
            return 0;
        }
        const auto fixtures = prepare_fixture(output);
        constexpr std::array<MatrixCase, 6> quick_matrix{{
            {"uk", .85f, 1280, 720}, {"en", 1.f, 1920, 1080},
            {"ru", 1.15f, 2560, 1440}, {"pl", 1.30f, 1920, 1080},
            {"es", 1.f, 1280, 720},
            {"uk", 1.30f, 1280, 720},
        }};
        constexpr std::array<const char *, 5> locales{{"uk", "en", "ru", "pl", "es"}};
        constexpr std::array<float, 4> scales{{.85f, 1.f, 1.15f, 1.30f}};
        constexpr std::array<std::pair<int, int>, 3> resolutions{{
            {1280, 720}, {1920, 1080}, {2560, 1440},
        }};
        std::vector<MatrixCase> matrix;
        if (full_matrix) {
            for (const auto *locale : locales)
                for (const auto scale : scales)
                    for (const auto [width, height] : resolutions)
                        matrix.push_back({locale, scale, width, height});
        } else {
            matrix.assign(quick_matrix.begin(), quick_matrix.end());
        }
        nlohmann::json captures = nlohmann::json::array();
        std::string expected_backend;
        std::string expected_device;
        std::string expected_content_hash;
        std::size_t case_index = 0;

        for (const auto &test : matrix) {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra native rework acceptance", test.width, test.height,
                                 SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            check(window != nullptr, SDL_GetError());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {render::Backend::Direct3D12});
            check(!linear_hdr_world || renderer.world_hdr_status().available,
                  "Explicit HDR color diagnostic requires native linear HDR world support");
            const auto render_capture = [&](render::Frame2D& frame, const std::filesystem::path& file) {
                // Diagnostic opt-in only: GameView and the normal game/launcher
                // remain unchanged until the full material/light graph is ready.
                frame.linear_hdr_world = linear_hdr_world;
                frame.world_exposure = 1.f;
                renderer.render(frame, file);
            };
            const auto &device = renderer.device_info();
            check(!device.software && device.backend == "direct3d12",
                  "Acceptance captures require native Direct3D 12");
            expected_backend = device.backend;
            expected_device = device.device;
            int drawable_width = 0, drawable_height = 0;
            check(SDL_GetWindowSizeInPixels(window.get(), &drawable_width, &drawable_height),
                  SDL_GetError());
            check(drawable_width == test.width && drawable_height == test.height,
                  "Drawable size differs from requested acceptance resolution");
            const auto logical_size = renderer.fitted_logical_size();
            renderer.set_logical_size(logical_size);

            const std::string stem = std::string(test.locale) + "-" +
                                     std::to_string(int(test.scale * 100.f + .5f)) + "-" +
                                     std::to_string(test.width) + "x" + std::to_string(test.height);
            std::cout << '[' << ++case_index << '/' << matrix.size() << "] " << stem << '\n'
                      << std::flush;
            const auto settings = output / (stem + "-settings.json");
            std::ofstream(settings) << nlohmann::json{{"locale", test.locale},
                                                       {"ui_scale", test.scale}}
                                          .dump(2);
            AssetCatalog assets(base / "assets");
            if (expected_content_hash.empty())
                expected_content_hash = assets.content_hash();
            check(expected_content_hash == assets.content_hash(),
                  "Assets changed during the visual matrix");
            const auto art_catalog = nlohmann::json::parse(assets.read_path("art/world.json"));
            Localization localization(base / "assets/locales");
            GameUi ui(renderer, assets, localization, settings, logical_size);
            ui.show_world();
            GameView view(renderer);
            view.set_logical_size(logical_size);
            view.load_art(assets);

            auto capture = [&](const std::filesystem::path &save, const std::string &fixture,
                               int selected_building, const std::string &filename, bool verify_f1) {
                ClientSession session;
                std::string error;
                check(session.load(save, error), error);
                view.reset_world_state();
                ui.reset_world_state();
                ui.show_world();
                SDL_Event center{};
                center.type = SDL_EVENT_KEY_DOWN;
                center.key.key = SDLK_C;
                view.handle_event(center, session);
                float capture_zoom = 1.30f;
                float camera_x = 24.f, camera_y = 24.f;
                if (fixture != "ordinary-start") {
                    // Start close enough to judge authored detail, then prove the
                    // complete settlement fits before accepting this zoom.
                    SDL_Event closer{};
                    closer.type = SDL_EVENT_MOUSE_WHEEL;
                    closer.wheel.y = 2.f;
                    view.handle_event(closer, session);
                    capture_zoom += .18f;
                }
                render::Frame2D frame;
                view.compose(session, 0, frame, false);
                const auto settlement_bounds = [&] {
                    render::Rect result{};
                    bool first = true;
                    for (const auto &building : session.buildings()) {
                        if (building.layer != WorldLayer::Surface || building.progress < 1)
                            continue;
                        int level = 1;
                        for (const auto &upgrade : session.economy().upgrades)
                            if (upgrade.building_id == building.id) {
                                level = upgrade.level;
                                break;
                            }
                        const auto visual = resolve_building_visual(
                            building.kind, level, building.progress, session.winter());
                        const auto point = view.tile_screen(float(building.x), float(building.y));
                        auto bounds = sprite_bounds(
                            art_catalog, visual.asset_id,
                            {point.x, point.y + (building.kind == BuildingKind::Bridge ? 0.f : 15.f) *
                                                    capture_zoom},
                            visual.width * capture_zoom);
                        // Include selection outline, foundation lift and smoke fringe.
                        bounds.x -= 10.f;
                        bounds.y -= 16.f;
                        bounds.w += 20.f;
                        bounds.h += 24.f;
                        if (first) {
                            result = bounds;
                            first = false;
                        } else {
                            const float right = std::max(result.x + result.w, bounds.x + bounds.w);
                            const float bottom = std::max(result.y + result.h, bounds.y + bounds.h);
                            result.x = std::min(result.x, bounds.x);
                            result.y = std::min(result.y, bounds.y);
                            result.w = right - result.x;
                            result.h = bottom - result.y;
                        }
                    }
                    check(!first, "Acceptance fixture contains no completed buildings");
                    return result;
                };
                if (fixture != "ordinary-start") {
                    constexpr float safe_left = 24.f, safe_top = 110.f,
                                    safe_right_margin = 24.f;
                    auto bounds = settlement_bounds();
                    const float safe_width = logical_size.width - safe_left - safe_right_margin;
                    // The comparison must show the settlement in the gameplay
                    // window, not merely somewhere behind the HUD. At a 900dp
                    // logical height the authored target reserves everything
                    // below roughly y=700 for the inspector/action/minimap row.
                    const float safe_bottom = logical_size.height * (700.f / 900.f);
                    const float safe_height = safe_bottom - safe_top;
                    // If the authored sprites exceed the honest viewport, zoom out
                    // through supported input rather than clipping or changing state.
                    while ((bounds.w > safe_width || bounds.h > safe_height) &&
                           capture_zoom > 1.125f) {
                        SDL_Event farther{};
                        farther.type = SDL_EVENT_MOUSE_WHEEL;
                        farther.wheel.y = -1.f;
                        view.handle_event(farther, session);
                        capture_zoom -= .09f;
                        view.compose(session, 0, frame, false);
                        bounds = settlement_bounds();
                    }
                    check(bounds.w <= safe_width && bounds.h <= safe_height,
                          "Staged settlement cannot fit the truthful acceptance viewport");
                    float shift_x = 0.f, shift_y = 0.f;
                    if (bounds.x < safe_left)
                        shift_x = safe_left - bounds.x;
                    if (bounds.x + bounds.w + shift_x > logical_size.width - safe_right_margin)
                        shift_x += logical_size.width - safe_right_margin -
                                   (bounds.x + bounds.w + shift_x);
                    if (bounds.y < safe_top)
                        shift_y = safe_top - bounds.y;
                    if (bounds.y + bounds.h + shift_y > safe_bottom)
                        shift_y += safe_bottom - (bounds.y + bounds.h + shift_y);
                    if (std::abs(shift_x) > .5f || std::abs(shift_y) > .5f) {
                        SDL_Event drag{};
                        drag.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                        drag.button.button = SDL_BUTTON_MIDDLE;
                        drag.button.x = logical_size.width * .5f;
                        drag.button.y = logical_size.height * .5f;
                        view.handle_event(drag, session);
                        drag = {};
                        drag.type = SDL_EVENT_MOUSE_MOTION;
                        drag.motion.x = logical_size.width * .5f + shift_x;
                        drag.motion.y = logical_size.height * .5f + shift_y;
                        view.handle_event(drag, session);
                        drag = {};
                        drag.type = SDL_EVENT_MOUSE_BUTTON_UP;
                        drag.button.button = SDL_BUTTON_MIDDLE;
                        drag.button.x = logical_size.width * .5f + shift_x;
                        drag.button.y = logical_size.height * .5f + shift_y;
                        view.handle_event(drag, session);
                        const float tile_width = 58.f * capture_zoom;
                        const float tile_height = 29.f * capture_zoom;
                        camera_x -= shift_x / tile_width + shift_y / tile_height;
                        camera_y += shift_x / tile_width - shift_y / tile_height;
                        view.compose(session, 0, frame, false);
                        bounds = settlement_bounds();
                    }
                    check(bounds.x >= safe_left - 1.f && bounds.y >= safe_top - 1.f &&
                              bounds.x + bounds.w <= logical_size.width - safe_right_margin + 1.f &&
                              bounds.y + bounds.h <= safe_bottom + 1.f,
                          "Staged settlement framing clips an authored building");
                }
                if (selected_building) {
                    const auto building = std::find_if(session.buildings().begin(), session.buildings().end(),
                                                       [&](const auto &item) {
                                                           return item.id == selected_building;
                                                       });
                    check(building != session.buildings().end(), "Selected stockpile missing");
                    const auto point = view.tile_screen(float(building->x), float(building->y));
                    const auto visual = resolve_building_visual(
                        building->kind, 1, building->progress, session.winter());
                    const auto bounds = sprite_bounds(art_catalog,
                        visual.asset_id, {point.x, point.y + 15.f * capture_zoom},
                        visual.width * capture_zoom);
                    const auto selected_index = static_cast<std::size_t>(
                        std::distance(session.buildings().begin(), building));
                    for (std::size_t index = 0; index < session.buildings().size(); ++index) {
                        const auto &other = session.buildings()[index];
                        if (other.id == building->id || other.layer != WorldLayer::Surface ||
                            other.progress < 1)
                            continue;
                        const int selected_depth = building->x + building->y;
                        const int other_depth = other.x + other.y;
                        if (other_depth < selected_depth ||
                            (other_depth == selected_depth && index < selected_index))
                            continue;
                        int other_level = 1;
                        for (const auto &upgrade : session.economy().upgrades)
                            if (upgrade.building_id == other.id) {
                                other_level = upgrade.level;
                                break;
                            }
                        const auto other_visual = resolve_building_visual(
                            other.kind, other_level, other.progress, session.winter());
                        const auto other_point = view.tile_screen(float(other.x), float(other.y));
                        const auto other_bounds = sprite_bounds(
                            art_catalog, other_visual.asset_id,
                            {other_point.x, other_point.y + 15.f * capture_zoom},
                            other_visual.width * capture_zoom);
                        const float overlap_width = std::max(
                            0.f, std::min(bounds.x + bounds.w, other_bounds.x + other_bounds.w) -
                                     std::max(bounds.x, other_bounds.x));
                        const float overlap_height = std::max(
                            0.f, std::min(bounds.y + bounds.h, other_bounds.y + other_bounds.h) -
                                     std::max(bounds.y, other_bounds.y));
                        const float overlap_ratio = overlap_width * overlap_height /
                                                    std::max(1.f, bounds.w * bounds.h);
                        check(overlap_ratio <= .08f,
                              "Selected stockpile plate is occluded by foreground " +
                                  std::string(building_name(other.kind)) + " (ratio " +
                                  std::to_string(overlap_ratio) + ")");
                    }
                    for (float y_fraction : {.15f, .35f, .55f, .75f, .9f}) {
                        for (float x_fraction : {.1f, .3f, .5f, .7f, .9f}) {
                            view.clear_building_selection();
                            SDL_Event click{};
                            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                            click.button.button = SDL_BUTTON_LEFT;
                            click.button.x = bounds.x + bounds.w * x_fraction;
                            click.button.y = bounds.y + bounds.h * y_fraction;
                            view.handle_event(click, session);
                            if (view.selected_building_ids() == std::vector<int>{selected_building})
                                break;
                        }
                        if (view.selected_building_ids() == std::vector<int>{selected_building})
                            break;
                    }
                    check(view.selected_building_ids() == std::vector<int>{selected_building},
                          "Staged stockpile was not selected through map picking");
                }
                // The first compose prepared picking geometry. Recompose after
                // the click so the evidence includes the actual selection art.
                view.compose(session, 0, frame, false);
                ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface,
                                  view.selected_building_ids());
                ui.update(&session, "");
                ui.compose(frame);
                for (const auto *id : {"action-build", "action-gather", "action-assign", "action-cancel"}) {
                    const auto button = ui.element_bounds_for_test(id);
                    const auto label = ui.element_bounds_for_test(std::string(id) + "-label");
                    const float text_width = ui.element_text_width_for_test(std::string(id) + "-label");
                    // A flex-shrunk label box can fit while its glyphs overlap
                    // the next button. Include the actual 5dp side padding.
                    check(text_width > 0 && text_width + 10.f * ui.scale() + 4.f <= button.w,
                          stem + ": action glyphs overflow: " + id + " text=" +
                              std::to_string(text_width) + " button=" + std::to_string(button.w));
                    check(label.w > 0 && label.h > 0 && label.x >= button.x + 2.f &&
                              label.x + label.w <= button.x + button.w - 2.f &&
                              label.y >= button.y && label.y + label.h <= button.y + button.h,
                          stem + ": action label does not fit its own hit area: " + id);
                }
                render_capture(frame, output / filename);
                captures.push_back({{"file", filename},
                                    {"fixture", fixture},
                                    {"purpose", fixture == "ordinary-start"
                                                    ? "ordinary native start and HUD legibility"
                                                : fixture == "staged-v03-winter"
                                                    ? "staged V03 winter comparison, House II, Workshop II and selected stockpile"
                                                    : "staged V03 art fixture, House II, Workshop II and selected stockpile"},
                                    {"seed", fixture_seed},
                                    {"ticks", session.ticks()},
                                    {"camera", {{"x", camera_x}, {"y", camera_y},
                                                {"zoom", capture_zoom}}},
                                    {"layer", "surface"},
                                    {"selected_building", selected_building},
                                    {"logical_world", {{"width", logical_size.width},
                                                       {"height", logical_size.height}}},
                                    {"drawable", {{"width", drawable_width}, {"height", drawable_height}}},
                                    {"locale", test.locale},
                                    {"ui_scale", test.scale},
                                    {"backend", device.backend},
                                    {"validation", "real World save/load, ClientSession, GameView, native RmlUi and D3D12"}});
                if (verify_f1) {
                    const auto visible_ui = ui_commands(frame);
                    SDL_Event f1{};
                    f1.type = SDL_EVENT_KEY_DOWN;
                    f1.key.key = SDLK_F1;
                    check(ui.process_event(f1) && !ui.hud_visible(), "F1 did not hide the HUD");
                    view.set_annotations_visible(ui.hud_visible());
                    view.compose(session, 0, frame, false);
                    ui.compose(frame);
                    check(ui_commands(frame) < visible_ui, "F1 did not remove HUD render commands");
                    const auto hidden_name = stem + "-hud-hidden.png";
                    render_capture(frame, output / hidden_name);
                    captures.push_back({{"file", hidden_name}, {"fixture", fixture},
                                        {"purpose", "F1 hides the complete HUD and world annotations"},
                                        {"seed", fixture_seed}, {"ticks", session.ticks()},
                                        {"camera", {{"x", camera_x}, {"y", camera_y},
                                                    {"zoom", capture_zoom}}},
                                        {"layer", "surface"}, {"hud_visible", false},
                                        {"locale", test.locale}, {"ui_scale", test.scale},
                                        {"drawable", {{"width", drawable_width}, {"height", drawable_height}}},
                                        {"backend", device.backend}});
                    check(ui.process_event(f1) && ui.hud_visible(), "Second F1 did not restore the HUD");
                    view.set_annotations_visible(ui.hud_visible());
                    view.compose(session, 0, frame, false);
                    ui.compose(frame);
                    check(ui_commands(frame) == visible_ui, "Restored HUD differs from its visible state");
                    const auto restored_name = stem + "-hud-restored.png";
                    render_capture(frame, output / restored_name);
                    captures.push_back({{"file", restored_name}, {"fixture", fixture},
                                        {"purpose", "second F1 restores the complete HUD and annotations"},
                                        {"seed", fixture_seed}, {"ticks", session.ticks()},
                                        {"camera", {{"x", camera_x}, {"y", camera_y},
                                                    {"zoom", capture_zoom}}},
                                        {"layer", "surface"}, {"hud_visible", true},
                                        {"locale", test.locale}, {"ui_scale", test.scale},
                                        {"drawable", {{"width", drawable_width}, {"height", drawable_height}}},
                                        {"backend", device.backend}});
                }
            };

            capture(output / "ordinary-start.save", "ordinary-start", 0,
                    stem + "-ordinary.png", false);
            capture(fixtures.staged, "staged-v03", fixtures.stockpile,
                    stem + "-staged-selected-stockpile.png",
                    std::string_view(test.locale) == "en" && test.scale == 1.f &&
                        test.width == 1920 && test.height == 1080);
            if (std::string_view(test.locale) == "uk" && test.scale == .85f &&
                test.width == 1280 && test.height == 720)
                capture(fixtures.winter, "staged-v03-winter", fixtures.stockpile,
                        stem + "-staged-winter-selected-stockpile.png", false);
        }

        const nlohmann::json report{{"schema", 1},
                                    {"generated_utc", utc_now()},
                                    {"probe", "ReworkVisualProbe"},
                                    {"runtime_evidence", true},
                                    {"linear_hdr_world_diagnostic", linear_hdr_world},
                                    {"hdr_diagnostic_scope", "color/composite only; no full Dynamic lighting, concept or performance acceptance"},
                                    {"content_sha256", expected_content_hash},
                                    {"build", {{"engine", engine_version},
                                               {"protocol", protocol_version},
                                               {"rules", rules_version},
                                               {"generator", generator_version}}},
                                    {"backend", expected_backend},
                                    {"device", expected_device},
                                    {"fixture_note", "ordinary world plus a deterministic staged V03 settlement on untouched generated terrain"},
                                    {"staged_fixture", {{"placement", "every site passes World::can_build before staging"},
                                                        {"terrain_edits", false},
                                                        {"resource_edits", false},
                                                        {"exploration", "resident movement only"},
                                                        {"desire_paths", "resident movement only"},
                                                        {"trail_tiles", fixtures.trail_tiles},
                                                        {"max_trail_wear", fixtures.max_trail_wear},
                                                        {"total_trail_wear", fixtures.total_trail_wear}}},
                                    {"matrix_mode", full_matrix ? "full" : "quick"},
                                    {"matrix_cases", matrix.size()},
                                    {"f1_hide_restore_verified", true},
                                    {"captures", captures}};
        std::ofstream(output / "manifest.json") << report.dump(2);
        std::ofstream(output / "report.json") << report.dump(2);
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

#include "ribterra/client_session.hpp"
#include "ribterra/minimap_projection.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/seasons.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include "ribterra/world_grid.hpp"
#include "weather_fixture.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<ribterra::render::Vertex> expanded_ui(const ribterra::render::Frame2D &frame) {
    std::vector<ribterra::render::Vertex> result;
    for (const auto &command : frame.commands) {
        const auto *draw = std::get_if<ribterra::render::GeometryDraw>(&command);
        if (!draw || draw->pass != ribterra::render::Pass::Ui) continue;
        check(draw->first_index + draw->index_count <= frame.indices.size(),
              "UI draw indexes outside the frame index buffer");
        for (std::uint32_t i = 0; i < draw->index_count; ++i) {
            const auto index = frame.indices[draw->first_index + i];
            check(index < frame.vertices.size(), "UI draw references a missing vertex");
            result.push_back(frame.vertices[index]);
        }
    }
    return result;
}

bool same_vertex(const ribterra::render::Vertex &a, const ribterra::render::Vertex &b) {
    return a.position.x == b.position.x && a.position.y == b.position.y &&
           a.uv.x == b.uv.x && a.uv.y == b.uv.y &&
           a.color.r == b.color.r && a.color.g == b.color.g &&
           a.color.b == b.color.b && a.color.a == b.color.a;
}

void check_same_ui(const ribterra::render::Frame2D &cached,
                   const ribterra::render::Frame2D &fresh,
                   const std::string &stage) {
    const auto cached_ui = expanded_ui(cached);
    const auto fresh_ui = expanded_ui(fresh);
    check(cached_ui.size() == fresh_ui.size(),
          stage + ": cached and rebuilt UI triangle streams have different sizes");
    for (std::size_t i = 0; i < cached_ui.size(); ++i)
        check(same_vertex(cached_ui[i], fresh_ui[i]),
              stage + ": cached and rebuilt UI triangle streams differ at vertex " +
                  std::to_string(i));
}

void rebuild_reference(ribterra::GameView &reference, ribterra::ClientSession &session,
                       ribterra::WorldLayer layer, double seconds,
                       ribterra::render::Frame2D &frame) {
    const auto other = layer == ribterra::WorldLayer::Surface
        ? ribterra::WorldLayer::Mine : ribterra::WorldLayer::Surface;
    ribterra::render::Frame2D scratch;
    reference.set_layer(other);
    reference.compose(session, seconds, scratch, false);
    reference.set_layer(layer);
    reference.compose(session, seconds, frame, false);
}

ribterra::render::Frame2D compare_stage(ribterra::GameView &persistent,
                                        ribterra::GameView &reference,
                                        ribterra::ClientSession &session,
                                        ribterra::WorldLayer layer,
                                        double seconds, const std::string &stage) {
    persistent.set_layer(layer);
    ribterra::render::Frame2D cached, fresh;
    persistent.compose(session, seconds, cached, false);
    rebuild_reference(reference, session, layer, seconds, fresh);
    check_same_ui(cached, fresh, stage);
    return fresh;
}

std::vector<ribterra::CellCoord> sampled_surface_cells(const ribterra::ClientSession &session) {
    ribterra::MinimapBounds bounds;
    for (const auto &chunk : session.chunks()) {
        if (chunk.key.layer != ribterra::WorldLayer::Surface) continue;
        for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
            if (!chunk.tiles[i].explored) continue;
            bounds.include_cell(float(chunk.key.cx * ribterra::chunk_size + int(i % ribterra::chunk_size)),
                                float(chunk.key.cy * ribterra::chunk_size + int(i / ribterra::chunk_size)));
        }
    }
    check(bounds.ready, "Minimap fixture has no explored surface bounds");
    constexpr float map_x = 1440.f - 300.f, map_y = 900.f - 190.f;
    constexpr float map_w = 210.f, map_h = 162.f;
    constexpr int columns = 140, rows = 108;
    constexpr float pixel_w = map_w / columns, pixel_h = map_h / rows;
    const auto projection = ribterra::make_minimap_projection({map_x, map_y, map_w, map_h}, bounds);
    std::vector<ribterra::CellCoord> result;
    for (int py = 0; py < rows; ++py)
        for (int px = 0; px < columns; ++px) {
            const auto world = projection.unproject(map_x + (px + .5f) * pixel_w,
                                                    map_y + (py + .5f) * pixel_h);
            const int x = int(std::floor(world.x + .5f));
            const int y = int(std::floor(world.y + .5f));
            if (!ribterra::inside_world(x, y)) continue;
            const ribterra::CellCoord cell{x, y, ribterra::WorldLayer::Surface};
            if (std::find(result.begin(), result.end(), cell) == result.end()) result.push_back(cell);
        }
    return result;
}

ribterra::Tile &filtered_snapshot_tile(ribterra::ClientSession &session,
                                       ribterra::CellCoord cell) {
    // Test fixture only: emulate a filtered network chunk delta without adding
    // a production terrain mutation API.
    auto &chunks = const_cast<std::vector<ribterra::Chunk> &>(session.chunks());
    const auto key = ribterra::chunk_key(cell);
    const auto chunk = std::find_if(chunks.begin(), chunks.end(), [&](const auto &candidate) {
        return candidate.key == key;
    });
    check(chunk != chunks.end(), "Sampled minimap cell has no filtered chunk");
    // Production revisions are content hashes, so arithmetic on the current
    // value can alias a revision retained by the view after the authoritative
    // simulation restores its hash. Reserve a distinct monotonic test range.
    static std::uint64_t fixture_revision = UINT64_C(0xf17e000000000000);
    do { ++fixture_revision; } while (fixture_revision == chunk->revision);
    chunk->revision = fixture_revision;
    return chunk->tiles[ribterra::local_index(cell.x, cell.y)];
}

bool has_filtered_chunk(const ribterra::ClientSession &session, ribterra::CellCoord cell) {
    const auto key = ribterra::chunk_key(cell);
    return std::any_of(session.chunks().begin(), session.chunks().end(),
                       [&](const auto &chunk) { return chunk.key == key; });
}

ribterra::render::Frame2D compose_fresh(ribterra::render::Renderer2D &renderer,
                                        ribterra::ClientSession &session,
                                        double seconds) {
    ribterra::GameView view(renderer);
    ribterra::render::Frame2D frame;
    view.compose(session, seconds, frame, false);
    return frame;
}

void check_ui_changed(const ribterra::render::Frame2D &before,
                      const ribterra::render::Frame2D &after,
                      const std::string &stage) {
    const auto a = expanded_ui(before);
    const auto b = expanded_ui(after);
    const bool same = a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(), same_vertex);
    check(!same, stage + ": fixture mutation did not change rebuilt minimap UI geometry");
}

void static_background_cache_matches_rebuilds(ribterra::render::Renderer2D &renderer) {
    ribterra::ClientSession session(70421, "Minimap cache");
    ribterra::GameView persistent(renderer), reference(renderer);

    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  0.0, "initial frame");
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  0.0, "repeated frame");

    session.advance();
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  1.0 / 20.0, "unit snapshot advance");

    session.advance();
    const auto trail_sampled = sampled_surface_cells(session);
    const auto trail_found = std::find_if(trail_sampled.begin(), trail_sampled.end(), [&](const auto &cell) {
        const auto &tile = session.tile(cell.x, cell.y, cell.layer);
        return tile.explored && tile.terrain != ribterra::Terrain::Water;
    });
    check(trail_found != trail_sampled.end(), "Minimap fixture has no sampled explored land cell");
    const auto trail_cell = *trail_found;
    const auto before_trail = compose_fresh(renderer, session, 2.0 / 20.0);
    auto &trail = filtered_snapshot_tile(session, trail_cell);
    trail.trail_wear = trail.trail_wear > 16 ? 0 : 180;
    const auto after_trail = compose_fresh(renderer, session, 2.0 / 20.0);
    check_ui_changed(before_trail, after_trail, "sampled trail change");
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  2.0 / 20.0, "sampled trail chunk change");

    session.advance();
    const auto reveal_sampled = sampled_surface_cells(session);
    const auto reveal_found = std::find_if(reveal_sampled.begin(), reveal_sampled.end(), [&](const auto &cell) {
        return has_filtered_chunk(session, cell) &&
               !session.tile(cell.x, cell.y, cell.layer).explored;
    });
    check(reveal_found != reveal_sampled.end(), "Minimap fixture has no sampled unknown cell to reveal");
    const auto reveal_cell = *reveal_found;
    const auto before_reveal = compose_fresh(renderer, session, 3.0 / 20.0);
    auto &revealed = filtered_snapshot_tile(session, reveal_cell);
    revealed = {ribterra::Terrain::Grass, 0, true, ribterra::Biome::Meadow, 0, 0};
    const auto after_reveal = compose_fresh(renderer, session, 3.0 / 20.0);
    check_ui_changed(before_reveal, after_reveal, "sampled terrain reveal");
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  3.0 / 20.0, "sampled reveal chunk change");

    session.advance();
    const auto amount_sampled = sampled_surface_cells(session);
    const auto amount_found = std::find_if(amount_sampled.begin(), amount_sampled.end(), [&](const auto &cell) {
        const auto &tile = session.tile(cell.x, cell.y, cell.layer);
        return cell != trail_cell && cell != reveal_cell && tile.explored &&
               tile.terrain == ribterra::Terrain::Grass &&
               tile.biome == ribterra::Biome::Meadow && tile.trail_wear <= 16;
    });
    check(amount_found != amount_sampled.end(), "Minimap fixture has no separate sampled meadow cell");
    const auto amount_cell = *amount_found;
    const auto before_amount = compose_fresh(renderer, session, 4.0 / 20.0);
    auto &amount = filtered_snapshot_tile(session, amount_cell);
    amount.amount = amount.amount > 0 ? 0 : 1;
    const auto after_amount = compose_fresh(renderer, session, 4.0 / 20.0);
    check_ui_changed(before_amount, after_amount, "sampled resource amount change");
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  4.0 / 20.0, "sampled amount chunk change");

    persistent.set_logical_size({1600, 900});
    reference.set_logical_size({1600, 900});
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  4.0 / 20.0, "logical resize");

    compare_stage(persistent, reference, session, ribterra::WorldLayer::Mine,
                  4.0 / 20.0, "mine layer");
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  4.0 / 20.0, "surface layer roundtrip");

    persistent.set_annotations_visible(false);
    reference.set_annotations_visible(false);
    ribterra::render::Frame2D hidden_cached, hidden_fresh;
    persistent.compose(session, 4.0 / 20.0, hidden_cached, false);
    reference.compose(session, 4.0 / 20.0, hidden_fresh, false);
    check_same_ui(hidden_cached, hidden_fresh, "F1-equivalent HUD hide");
    persistent.set_annotations_visible(true);
    reference.set_annotations_visible(true);
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  4.0 / 20.0, "F1-equivalent HUD show");

    persistent.reset_world_state();
    reference.reset_world_state();
    persistent.set_logical_size({1600, 900});
    reference.set_logical_size({1600, 900});
    compare_stage(persistent, reference, session, ribterra::WorldLayer::Surface,
                  4.0 / 20.0, "world-state reset");

    ribterra::ClientSession other(99117, "Different minimap terrain");
    compare_stage(persistent, reference, other, ribterra::WorldLayer::Surface,
                  0.0, "different-seed session");
}

void winter_cache_matches_loaded_calendar(ribterra::render::Renderer2D& renderer,
                                          const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    std::filesystem::path run_output;
    for (std::uint64_t run = 1; run_output.empty(); ++run) {
        const auto candidate = output / ("run-" + std::to_string(run));
        if (std::filesystem::create_directory(candidate)) run_output = candidate;
    }
    ribterra::World original(70421, 10);
    std::string error;
    const auto summer_path=run_output/"calendar-spring.ribsave";
    check(original.save(summer_path,error),error);
    auto state=original.export_state();
    ribterra::test::set_fixture_simulation_tick(state,
        3*ribterra::days_per_season*ribterra::ticks_per_day+ribterra::ticks_per_day/4);
    ribterra::World winter(70421,0);
    check(winter.import_state(std::move(state),error),error);
    const auto winter_path=run_output/"calendar-winter.ribsave";
    check(winter.save(winter_path,error),error);
    ribterra::ClientSession session(70421,"Minimap calendar");
    check(session.load(summer_path,error),error);
    check(!session.winter(),"Spring calendar fixture is unexpectedly winter");
    ribterra::GameView persistent(renderer),reference(renderer);
    const auto before=compare_stage(persistent,reference,session,ribterra::WorldLayer::Surface,
                                    0.0,"loaded spring cache");
    check(session.load(winter_path,error),error);
    check(session.winter(),"Winter calendar fixture did not activate winter");
    const auto after=compare_stage(persistent,reference,session,ribterra::WorldLayer::Surface,
                                   0.0,"same-session winter load without view reset");
    check_ui_changed(before,after,"calendar winter palette");
    check(session.load(summer_path,error),error);
    const auto restored=compare_stage(persistent,reference,session,ribterra::WorldLayer::Surface,
                                      0.0,"winter to spring cache");
    check_same_ui(before,restored,"calendar roundtrip");
}

} // namespace

int main(int argc,char** argv) {
    try {
        check(argc==2,"Usage: MinimapCacheTests <new-calendar-fixture-directory>");
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window *window = SDL_CreateWindow("Ribterra minimap cache tests", 1100, 700,
                                              SDL_WINDOW_HIDDEN);
        check(window != nullptr, "Hidden SDL window creation failed");
        {
            const auto base = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
            ribterra::render::Renderer2D renderer(
                window, base / "assets", {ribterra::render::Backend::Software, false});
            static_background_cache_matches_rebuilds(renderer);
            winter_cache_matches_loaded_calendar(renderer,
                std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(argv[1]))));
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "minimap cache tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

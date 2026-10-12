#include "ribterra/seasons.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/biomes.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/terrain_surface.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace {
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        require(args.size() >= 2, "Expected output directory");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        const bool software = args.size() > 2 && args[2] == "--software";
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            using namespace ribterra;
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra art verification", 1440, 900, SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base / "assets");
            GameArt art(renderer);
            art.load(assets);
            require(art.ready(), "Art catalog empty");
            render::Frame2D frame;
            constexpr std::array<const char *, 5> states{{"stage1", "stage2", "stage3", "ready", "winter"}};
            for (int page = 0; page < 5; ++page) {
                frame.clear_commands();
                frame.clear = {61, 80, 56, 255};
                frame.logical_size = {1440, 900};
                for (int row = 0; row < 4 && row + page * 4 < int(building_catalog.size()); ++row)
                    for (int col = 0; col < 5; ++col) {
                        const auto &definition = building_catalog[row + page * 4];
                        const std::string id = std::string("building.") + definition.slug + "." + states[col];
                        require(art.contains(id), "Missing authored building state: " + id);
                        art.sprite(frame, id, {150.f + col * 282, 190.f + row * 220}, 152, render::Pass::Ui);
                        frame.commands.emplace_back(render::TextDraw{render::Pass::Ui,
                                                                     id,
                                                                     {28.f + col * 282, 195.f + row * 220},
                                                                     14,
                                                                     {238, 226, 197, 255},
                                                                     270});
                    }
                renderer.render(frame, output / ("buildings-" + std::to_string(page) + ".png"));
            }
            for (const auto &definition : building_catalog) {
                const auto visual = resolve_building_visual(definition.kind, 2, 1.f, false);
                require(art.contains(visual.asset_id), "Resolver selected missing art: " + visual.asset_id);
                require(building_has_level2_visual(definition.kind) == visual.asset_id.ends_with(".level2"),
                        "Resolver exposed a forbidden level2 plate");
            }
            int directional_resident_sprites = 0;
            for (int id : {1, 2})
                for (const auto load : {ResidentLoad::Empty, ResidentLoad::Logs, ResidentLoad::Sack,
                                        ResidentLoad::Cart})
                    for (int facing = 0; facing < 8; ++facing)
                        for (int phase = 0; phase < 4; ++phase) {
                            const auto sprite = resident_sprite(id, load, facing, phase);
                            require(art.contains(sprite), "Missing directional resident sprite: " + sprite);
                            ++directional_resident_sprites;
                        }

            // Fixed V03 visual passport: identical anchors and authored widths for
            // construction, ready, winter and the two gameplay-backed upgrades.
            frame.clear_commands();
            frame.clear = {78, 96, 91, 255};
            frame.logical_size = {1440, 900};
            const std::array<BuildingVisual, 9> calibration{{
                resolve_building_visual(BuildingKind::House, 1, .1f, false),
                resolve_building_visual(BuildingKind::House, 1, .5f, false),
                resolve_building_visual(BuildingKind::House, 1, .8f, false),
                resolve_building_visual(BuildingKind::House, 1, 1.f, false),
                resolve_building_visual(BuildingKind::House, 2, 1.f, false),
                resolve_building_visual(BuildingKind::Stockpile, 1, 1.f, false),
                resolve_building_visual(BuildingKind::Workshop, 1, 1.f, false),
                resolve_building_visual(BuildingKind::Workshop, 2, 1.f, false),
                resolve_building_visual(BuildingKind::House, 2, 1.f, true),
            }};
            for (int i = 0; i < int(calibration.size()); ++i) {
                const float x = 115.f + (i % 5) * 285.f;
                const float y = 300.f + (i / 5) * 300.f;
                art.tile(frame, "fx.3", {x, y + 8}, calibration[i].width * .55f,
                         calibration[i].width * .22f, render::Pass::Ui, {0, 0, 1, 1}, {20, 23, 18, 100});
                art.sprite(frame, calibration[i].asset_id, {x, y + 15}, calibration[i].width * 1.35f,
                           render::Pass::Ui,
                           calibration[i].winter_tint ? render::Color{224, 234, 239, 255}
                                                      : render::Color{255, 255, 255, 255});
                frame.commands.emplace_back(render::TextDraw{render::Pass::Ui, calibration[i].asset_id,
                    {x - 105, y + 40}, 14, {238, 226, 197, 255}, 250});
            }
            for (int facing = 0; facing < 8; ++facing) {
                const auto load = static_cast<ResidentLoad>(facing % 4);
                const float x = 210.f + facing * 135.f;
                constexpr float y = 810.f;
                art.tile(frame, "fx.3", {x, y}, resident_contact_shadow_half_width(load) * 1.7f,
                         resident_contact_shadow_half_height * 1.7f, render::Pass::Ui, {0, 0, 1, 1},
                         {18, 22, 20, 112});
                art.sprite(frame, resident_sprite(facing + 1, load, facing, 1), {x, y},
                           resident_sprite_width(load) * 1.7f, render::Pass::Ui);
            }
            renderer.render(frame, output / "v03-object-calibration.png");
            // Create a normal settlement and build through the authoritative rules.
            std::string error;
            World world(70421, 10);
            auto supplied_state = world.export_state();
            for (auto &storage : supplied_state.storages)
                if (storage.building_id == supplied_state.buildings.front().id)
                    storage.capacity = 10000;
            supplied_state.item_stacks.push_back(
                {supplied_state.next_item_stack_id++, ItemKind::Wood, 200, 1,
                 {ItemLocationKind::Storage, {}, supplied_state.buildings.front().id}});
            supplied_state.item_stacks.push_back(
                {supplied_state.next_item_stack_id++, ItemKind::Stone, 200, 1,
                 {ItemLocationKind::Storage, {}, supplied_state.buildings.front().id}});
            supplied_state.resources.wood += 200;
            supplied_state.resources.stone += 200;
            require(world.import_state(std::move(supplied_state), error), "V03 supplied fixture invalid: " + error);
            for (const auto kind : {BuildingKind::House, BuildingKind::Farm, BuildingKind::Stockpile,
                                    BuildingKind::Workshop}) {
                bool placed = false;
                for (int y = 22; y < 29 && !placed; ++y)
                    for (int x = 21; x < 29 && !placed; ++x)
                        if (world.can_build(kind, x, y))
                            placed = world.build(kind, x, y).accepted;
                require(placed, "Could not prepare building scene");
                for (int tick = 0; tick < 1000; ++tick)
                    world.tick();
            }
            auto upgraded_state = world.export_state();
            for (const auto &building : upgraded_state.buildings)
                if (building.progress == 1 &&
                    (building.kind == BuildingKind::House || building.kind == BuildingKind::Workshop))
                    upgraded_state.economy.upgrades.push_back({building.id, 2});
            require(world.import_state(std::move(upgraded_state), error), "V03 upgrade fixture invalid: " + error);
            GameView view(renderer);
            view.load_art(assets);
            Localization localization(base / "assets/locales");
            GameUi ui(renderer, assets, localization, output / "ui-settings.json");
            ui.show_world();
            nlohmann::json picking_diagnostics=nlohmann::json::array();
            for (int season = 0; season < 2; ++season) {
                if (season) {
                    auto state = world.export_state();
                    state.ticks = 3*ribterra::days_per_season*ribterra::ticks_per_day;
                    require(world.import_state(std::move(state), error), "Winter fixture invalid: " + error);
                }
                const auto save = output / (season ? "winter.save" : "summer.save");
                require(world.save(save, error), error);
                ClientSession session;
                require(session.load(save, error), error);
                require(session.winter() == bool(season), "Season projection mismatch");
                const auto before = session.ticks();
                view.compose(session, 0, frame, false);
                require(frame.vertices.size() > 100, "World composition missing");
                ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface);
                ui.update(&session, "");
                ui.compose(frame);
                renderer.render(frame, output / (season ? "winter.png" : "summer.png"));
                if (!season) {
                    const auto target = std::find_if(session.buildings().begin(), session.buildings().end(),
                                                     [](const auto &building) {
                                                         return building.kind == BuildingKind::Workshop;
                                                     });
                    require(target != session.buildings().end(), "V03 picking fixture has no workshop");
                    struct ZoomCheck { int wheel; const char *name; };
                    constexpr std::array<ZoomCheck, 3> zoom_checks{{{-100, "062"}, {0, "130"}, {100, "300"}}};
                    for (const auto check : zoom_checks) {
                        SDL_Event event{};
                        event.type = SDL_EVENT_KEY_DOWN;
                        event.key.key = SDLK_C;
                        view.handle_event(event, session);
                        if (check.wheel) {
                            event = {};
                            event.type = SDL_EVENT_MOUSE_WHEEL;
                            event.wheel.y = static_cast<float>(check.wheel);
                            view.handle_event(event, session);
                        }
                        // C centres the camp, not this workshop. Pan with the
                        // same bounded middle-drag events available to a player
                        // so every zoom assertion exercises an on-screen object.
                        const render::Point desired_target{frame.logical_size.width * .5f,
                                                           frame.logical_size.height * .58f};
                        for (int step = 0; step < 8; ++step) {
                            const auto current_target =
                                view.tile_screen(float(target->x), float(target->y));
                            const float dx = std::clamp(desired_target.x - current_target.x, -250.f, 250.f);
                            const float dy = std::clamp(desired_target.y - current_target.y, -250.f, 250.f);
                            if (std::abs(dx) < .5f && std::abs(dy) < .5f)
                                break;
                            const float drag_x = frame.logical_size.width * .5f;
                            const float drag_y = frame.logical_size.height * .5f;
                            event = {};
                            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                            event.button.button = SDL_BUTTON_MIDDLE;
                            event.button.x = drag_x;
                            event.button.y = drag_y;
                            view.handle_event(event, session);
                            event = {};
                            event.type = SDL_EVENT_MOUSE_MOTION;
                            event.motion.x = drag_x + dx;
                            event.motion.y = drag_y + dy;
                            view.handle_event(event, session);
                            event = {};
                            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                            event.button.button = SDL_BUTTON_MIDDLE;
                            event.button.x = drag_x + dx;
                            event.button.y = drag_y + dy;
                            view.handle_event(event, session);
                        }
                        view.compose(session, 0, frame, false);
                        const float zoom=check.wheel<0?.62f:check.wheel>0?3.f:1.3f;
                        auto ground=view.tile_screen(float(target->x),float(target->y));
                        const auto sample=[&](int x,int y){return session.tile(x,y,target->layer);};
                        const auto heights=terrain_corners(target->x,target->y,sample);
                        const float center=terrain_height(float(target->x),float(target->y),sample);
                        ground.y-=(*std::max_element(heights.begin(),heights.end())-center)*2.4f*zoom;
                        const auto visual=resolve_building_visual(target->kind,2,target->progress,false);
                        const render::Point sprite_ground{ground.x,ground.y+15.f*zoom};
                        auto pick = view.tile_screen(float(target->x), float(target->y));
                        const render::Point original_pick{pick.x,pick.y-36.f*zoom};
                        require(original_pick.x >= 0 && original_pick.x < frame.logical_size.width &&
                                    original_pick.y >= 0 && original_pick.y < frame.logical_size.height,
                                std::string("V03 workshop test point is off-screen at zoom ") + check.name);
                        const bool original_workshop_opaque=art.hit_test_sprite(
                            visual.asset_id,sprite_ground,visual.width*zoom,original_pick);
                        event = {};
                        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                        event.button.button = SDL_BUTTON_LEFT;
                        event.button.x = original_pick.x;
                        event.button.y = original_pick.y;
                        view.handle_event(event, session);
                        nlohmann::json diagnostic{{"zoom",check.name},
                                                  {"building_id",target->id},
                                                  {"building_depth",target->x+target->y+.5f},
                                                  {"visible_target",{pick.x,pick.y}},
                                                  {"original_point",{original_pick.x,original_pick.y}},
                                                  {"original_workshop_opaque",original_workshop_opaque},
                                                  {"original_buildings",view.selected_building_ids()},
                                                  {"original_residents",view.selected_ids()}};
                        if (std::find(view.selected_building_ids().begin(), view.selected_building_ids().end(),
                                      target->id) == view.selected_building_ids().end()) {
                            if(!view.selected_building_ids().empty()) {
                                nlohmann::json buildings=nlohmann::json::array();
                                float covering_depth=-100000.f;
                                for(const int id:view.selected_building_ids()) {
                                    const auto building=std::find_if(session.buildings().begin(),session.buildings().end(),
                                        [&](const auto &item){return item.id==id;});
                                    require(building!=session.buildings().end(),
                                            "Selected building is absent from projection");
                                    const float depth=building->x+building->y+.5f;
                                    covering_depth=std::max(covering_depth,depth);
                                    buildings.push_back({{"id",id},{"kind",int(building->kind)},
                                                         {"x",building->x},{"y",building->y},
                                                         {"depth",depth}});
                                }
                                diagnostic["covering_buildings"]=std::move(buildings);
                                if(original_workshop_opaque)
                                    require(covering_depth>=float(target->x+target->y)+.5f,
                                            std::string("V03 workshop pixel was stolen by a building behind it at zoom ")+
                                                check.name);
                                diagnostic["building_overlap_case"]=true;
                            } else if(view.selected_ids().empty()) {
                                require(!original_workshop_opaque,
                                        std::string("V03 opaque workshop pixel selected no object at zoom ")+
                                            check.name);
                                diagnostic["transparent_gutter_case"]=true;
                            }
                            nlohmann::json residents=nlohmann::json::array();
                            float front_depth=-100000.f;
                            for(const int id:view.selected_ids()) {
                                const auto resident=std::find_if(session.residents().begin(),session.residents().end(),
                                    [&](const auto &item){return item.id==id;});
                                require(resident!=session.residents().end(),"Selected resident is absent from projection");
                                const float depth=resident->x+resident->y+.8f;
                                front_depth=std::max(front_depth,depth);
                                const auto screen=view.resident_screen(session,id);
                                residents.push_back({{"id",id},{"x",resident->x},{"y",resident->y},
                                                     {"depth",depth},
                                                     {"screen",screen?nlohmann::json::array({screen->x,screen->y})
                                                                      :nlohmann::json(nullptr)}});
                            }
                            diagnostic["foreground_residents"]=std::move(residents);
                            if(!view.selected_ids().empty()) {
                                if(original_workshop_opaque) {
                                    require(front_depth>=float(target->x+target->y)+.5f,
                                            std::string("V03 opaque workshop pixel was stolen by a resident behind it at zoom ")+
                                                check.name);
                                    diagnostic["foreground_case"]=true;
                                } else {
                                    // A resident visible through authored alpha is
                                    // selectable regardless of anchor depth.
                                    diagnostic["transparent_resident_case"]=true;
                                }
                            }
                        } else {
                            require(original_workshop_opaque,
                                    std::string("V03 transparent workshop pixel selected the building at zoom ")+
                                        check.name);
                            diagnostic["foreground_case"]=false;
                        }

                        // Locate an authored opaque workshop pixel that is not
                        // covered by a later painter object. This separately
                        // verifies building selection without denying the valid
                        // foreground-resident case above.
                        const auto bounds=art.sprite_bounds(visual.asset_id,sprite_ground,visual.width*zoom);
                        std::optional<render::Point> workshop_pick;
                        // Search from the authored sprite centre outwards, and only
                        // accept points a player can actually click in this frame.
                        // This prevents an off-screen opaque gutter/roof pixel from
                        // being reported as successful zoom coverage.
                        for(int radius=0;radius<=15&&!workshop_pick;++radius)
                            for(int dy=-radius;dy<=radius&&!workshop_pick;++dy)
                                for(int dx=-radius;dx<=radius&&!workshop_pick;++dx) {
                                if(std::max(std::abs(dx),std::abs(dy))!=radius) continue;
                                const int column=15+dx, row=15+dy;
                                const render::Point candidate{
                                    bounds.x+(float(column)+.5f)/31.f*bounds.w,
                                    bounds.y+(float(row)+.5f)/31.f*bounds.h};
                                if(candidate.x<0 || candidate.x>=frame.logical_size.width ||
                                   candidate.y<0 || candidate.y>=frame.logical_size.height) continue;
                                if(!art.hit_test_sprite(visual.asset_id,sprite_ground,
                                                       visual.width*zoom,candidate)) continue;
                                view.clear_building_selection();
                                event={};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;
                                event.button.button=SDL_BUTTON_LEFT;
                                event.button.x=candidate.x;event.button.y=candidate.y;
                                view.handle_event(event,session);
                                if(view.selected_building_ids()==std::vector<int>{target->id})
                                    workshop_pick=candidate;
                            }
                        require(workshop_pick.has_value(),
                                std::string("V03 workshop has no opaque unoccluded pick point at zoom ")+check.name);
                        diagnostic["workshop_point"]={workshop_pick->x,workshop_pick->y};
                        diagnostic["workshop_selected_id"]=target->id;
                        std::cout << "V03 pick zoom=" << check.name << " building=" << target->id
                                  << " depth=" << target->x+target->y+.5f
                                  << " original_opaque=" << original_workshop_opaque
                                  << " original_residents=" << diagnostic["original_residents"].dump()
                                  << " workshop_point=" << workshop_pick->x << ',' << workshop_pick->y << '\n';
                        picking_diagnostics.push_back(std::move(diagnostic));
                        view.clear_building_selection();
                        view.compose(session, 0, frame, false);
                        renderer.render(frame, output / (std::string("v03-zoom-") + check.name + ".png"));
                    }
                    require(ui.activate_for_test("build-more"), "Catalog did not open");
                    view.compose(session, 0, frame, false);
                    ui.update(&session, "");
                    ui.compose(frame);
                    renderer.render(frame, output / "construction-catalog.png");
                    require(ui.activate_for_test("catalog-close"), "Catalog did not close");
                }
                require(session.ticks() == before, "Artwork changed simulation time");
            }
            // Render actual generated regions with a test-only exploration reveal.
            constexpr std::array<const char *, 9> names{"meadow",    "forest",        "lake",
                                                        "mountain",  "snow-meadow",   "snow-forest",
                                                        "snow-lake", "snow-mountain", "desert"};
            World landscape(70421, 1);
            const WorldGenerationConfig generation{70421, landscape.terrain_generator(),
                                                   alpine_valley_profile_id, terrain_rules_version};
            for (int biome = 0; biome < 9; ++biome) {
                int cx = 24, cy = 24, best = -1;
                for (int y = world_min + 24; y < world_max - 24; y += 9)
                    for (int x = world_min + 24; x < world_max - 24; x += 9) {
                        if (surface_biome(x, y, generation) != static_cast<Biome>(biome))
                            continue;
                        int score = 0;
                        for (int dy = -9; dy <= 9; dy += 3)
                            for (int dx = -9; dx <= 9; dx += 3)
                                score += surface_biome(x + dx, y + dy, generation) == static_cast<Biome>(biome);
                        if (score > best) {
                            best = score;
                            cx = x;
                            cy = y;
                        }
                    }
                require(best >= 0, "Missing generated biome");
                auto state = landscape.export_state();
                for (auto &chunk : state.chunks) {
                    if (chunk.key.layer != WorldLayer::Surface)
                        continue;
                    for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                        const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                        const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                        chunk.tiles[i].explored = (x - cx) * (x - cx) + (y - cy) * (y - cy) < 24 * 24;
                    }
                }
                World fixture(70421, 0);
                require(fixture.import_state(std::move(state), error), error);
                // Keep every diagnostic source world instead of rotating one
                // save and deleting it at the end of the probe.
                const auto save = output / (std::string("biome-preview-") + names[biome] + ".save");
                require(fixture.save(save, error), error);
                ClientSession session;
                require(session.load(save, error), error);
                GameView scene(renderer);
                scene.load_art(assets);
                scene.compose(session, 0, frame, false);
                SDL_Event event{};
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = SDL_BUTTON_MIDDLE;
                event.button.x = 720;
                event.button.y = 450;
                scene.handle_event(event, session);
                event = {};
                event.type = SDL_EVENT_MOUSE_MOTION;
                event.motion.x = 720 - ((cx - 24) - (cy - 24)) * 58 * 1.3f * .5f;
                event.motion.y =
                    450 - ((cx - 24) + (cy - 24)) * 29 * 1.3f * .5f +
                    surface_elevation(cx, cy, generation) * 1.3f;
                scene.handle_event(event, session);
                event = {};
                event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                event.button.button = SDL_BUTTON_MIDDLE;
                scene.handle_event(event, session);
                scene.compose(session, 0, frame, false);
                // Round-trip actual mesh picking at visible elevated tile centers.
                int picked = 0;
                for (int y = cy - 5; y <= cy + 5; ++y)
                    for (int x = cx - 5; x <= cx + 5; ++x) {
                        const auto screen = scene.tile_screen(float(x), float(y));
                        const auto cell = scene.cell_at_screen(screen.x, screen.y);
                        require(cell.x == x && cell.y == y, "Raised terrain picking mismatch");
                        ++picked;
                    }
                require(picked == 121, "Terrain picking coverage missing");
                renderer.render(frame, output / (std::string("biome-") + names[biome] + ".png"));
                event = {};
                event.type = SDL_EVENT_MOUSE_WHEEL;
                event.wheel.y = -7;
                scene.handle_event(event, session);
                scene.compose(session, 0, frame, false);
                renderer.render(frame, output / (std::string("overview-") + names[biome] + ".png"));
            }
            // Controlled slope fixture: real click orders and resident movement,
            // plus the existing camp foundation. This image is labeled as a test.
            World slope_world(70421, 1);
            auto slope_state = slope_world.export_state();
            for (auto &chunk : slope_state.chunks) {
                if (chunk.key.layer != WorldLayer::Surface)
                    continue;
                for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                    const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                    const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                    if (x >= 16 && x <= 40 && y >= 16 && y <= 36)
                        chunk.tiles[i] = {
                            Terrain::Grass, 0, true, Biome::Meadow,
                            static_cast<std::int16_t>(std::max(0, (x - 18) * 7 + (y - 18) * 2))};
                }
            }
            // The default (21,21) resident is fully behind the much deeper camp
            // silhouette on this exaggerated slope. Place this isolated picking
            // fixture on the same slope but outside that authored silhouette;
            // foreground occlusion is covered separately by ClientInputTests.
            require(!slope_state.residents.empty(), "Slope fixture has no resident");
            slope_state.residents.front().x = 18.f;
            slope_state.residents.front().y = 24.f;
            slope_state.residents.front().target_x = 18;
            slope_state.residents.front().target_y = 24;
            require(slope_world.import_state(std::move(slope_state), error), error);
            require(slope_world.save(output / "slope-test.save", error), error);
            ClientSession slope_session;
            require(slope_session.load(output / "slope-test.save", error), error);
            GameView slope_view(renderer);
            slope_view.load_art(assets);
            slope_view.compose(slope_session, 0, frame, false);
            auto click = [&](SDL_FPoint p, Uint8 button) {
                SDL_Event event{};
                event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = button;
                event.button.x = p.x;
                event.button.y = p.y;
                slope_view.handle_event(event, slope_session);
            };
            const auto start = slope_session.residents().front();
            const auto feet = slope_view.tile_screen(start.x, start.y);
            constexpr float slope_zoom = 1.3f;
            const auto resident_asset = resident_sprite(start.id, ResidentLoad::Empty, 2, 1);
            const render::Point resident_ground{feet.x, feet.y + 6.f * slope_zoom};
            const float resident_width = resident_sprite_width(ResidentLoad::Empty) * slope_zoom;
            require(art.contains(resident_asset), "Slope resident fixture is missing canonical art");
            const auto resident_bounds = art.sprite_bounds(resident_asset, resident_ground, resident_width);
            const render::Point old_person_point{feet.x, feet.y - 10.f * slope_zoom};
            const bool old_person_opaque = art.hit_test_sprite(
                resident_asset, resident_ground, resident_width, old_person_point);
            std::optional<render::Point> resident_pick;
            for (int radius = 0; radius <= 15 && !resident_pick; ++radius)
                for (int dy = -radius; dy <= radius && !resident_pick; ++dy)
                    for (int dx = -radius; dx <= radius && !resident_pick; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius)
                            continue;
                        const int column = 15 + dx, row = 15 + dy;
                        const render::Point candidate{
                            resident_bounds.x + (float(column) + .5f) / 31.f * resident_bounds.w,
                            resident_bounds.y + (float(row) + .5f) / 31.f * resident_bounds.h};
                        if (candidate.x < 0 || candidate.x >= frame.logical_size.width ||
                            candidate.y < 0 || candidate.y >= frame.logical_size.height)
                            continue;
                        if (!art.hit_test_sprite(
                                resident_asset, resident_ground, resident_width, candidate))
                            continue;
                        click({candidate.x, candidate.y}, SDL_BUTTON_LEFT);
                        if (slope_view.selected_ids() == std::vector<int>{start.id})
                            resident_pick = candidate;
                    }
            require(resident_pick.has_value(),
                    "Resident on slope has no visible opaque selectable pixel");
            picking_diagnostics.push_back({{"case", "slope-resident"},
                                           {"resident_id", start.id},
                                           {"asset", resident_asset},
                                           {"old_point", {old_person_point.x, old_person_point.y}},
                                           {"old_point_opaque", old_person_opaque},
                                           {"resident_point", {resident_pick->x, resident_pick->y}}});
            std::cout << "Slope resident pick id=" << start.id << " asset=" << resident_asset
                      << " old_opaque=" << old_person_opaque << " resident_point="
                      << resident_pick->x << ',' << resident_pick->y << '\n';
            click(slope_view.tile_screen(30.f, 24.f), SDL_BUTTON_RIGHT);
            bool arrived = false, fractional_step = false;
            for (int tick = 0; tick < 1000 && !arrived; ++tick) {
                slope_session.advance();
                slope_view.compose(slope_session, 0, frame, false);
                const auto &unit = slope_session.residents().front();
                const float h =
                    terrain_height(unit.x, unit.y, [&](int x, int y) { return slope_session.tile(x, y); });
                const auto projected = slope_view.tile_screen(unit.x, unit.y);
                constexpr float expected_terrain_vertical_scale = 2.4f;
                const float expected = 432.f + (unit.x + unit.y - 48) * 14.5f * 1.3f -
                                       h * expected_terrain_vertical_scale * 1.3f;
                require(std::abs(projected.y - expected) < .01f, "Moving resident floats above slope");
                fractional_step = fractional_step || std::abs(unit.x - std::round(unit.x)) > .05f;
                arrived = std::abs(unit.x - 30) < .01f && std::abs(unit.y - 24) < .01f;
            }
            require(arrived && fractional_step, "Slope click did not produce a continuous uphill walk");
            renderer.render(frame, output / "slope-movement-test.png");
            // Optional verification against a real package-0.12 save, kept untouched.
            for (std::size_t i = 2; i + 1 < args.size(); ++i) {
                if (args[i] != "--legacy-save")
                    continue;
                World legacy(1, 0);
                require(legacy.load(path(args[i + 1]), error), error);
                for (const auto &chunk : legacy.chunks())
                    for (const auto &tile : chunk.tiles)
                        require(tile.elevation == 0, "Old save was unexpectedly reshaped");
                require(legacy.save(output / "legacy-upgraded.save", error), error);
                World restored(2, 0);
                require(restored.load(output / "legacy-upgraded.save", error), error);
                require(restored.checksum() == legacy.checksum(), "Legacy upgrade lost world state");
            }
            const auto &device = renderer.device_info();
            std::ofstream(output / "art-report.json")
                << nlohmann::json{{"building_types", building_catalog.size()}, {"building_states", 5},
                                  {"content_sha256", assets.content_hash()},
                                  {"level2_building_types", 2}, {"directional_resident_sprites", directional_resident_sprites},
                                  {"terrain_pick_checks", 1089}, {"object_zoom_pick_checks", 3},
                                  {"object_pick_diagnostics",picking_diagnostics},
                                  {"uphill_click_and_movement", true},
                                  {"summer_and_winter", true},   {"backend", device.backend},
                                  {"device", device.device},     {"software", device.software}}
                       .dump(2);
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

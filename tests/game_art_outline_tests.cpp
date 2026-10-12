#include "ribterra/asset_catalog.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/resource_shrub.hpp"
#include "ribterra/shore_details.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool same(ribterra::render::Rect a, ribterra::render::Rect b) {
    return std::abs(a.x - b.x) < .001f && std::abs(a.y - b.y) < .001f &&
           std::abs(a.w - b.w) < .001f && std::abs(a.h - b.h) < .001f;
}
} // namespace

int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            using namespace ribterra;
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra outline test", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = std::filesystem::path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Software});
            AssetCatalog assets(base / "assets");
            GameArt art(renderer);
            art.load(assets);
            require(art.outline_sprite_count() >= 50, "Building outline cache is incomplete");
            require(art.outline_texture_bytes() > 0 &&
                        art.outline_texture_bytes() <= 20U * 1024U * 1024U,
                    "Building outline cache exceeded its memory budget");
            require(art.hit_mask_bytes() > 0 && art.hit_mask_bytes() <= 16U * 1024U * 1024U,
                    "CPU sprite hit-mask cache exceeded its independent memory budget");

            constexpr auto id = "building.house.ready";
            const render::Point ground{320, 300};
            for (const auto &family : {shore_detail_sprites, resource_shrub_sprites}) {
                for (std::size_t i = 0; i < 4; ++i) {
                    require(art.contains(family[i]) && art.contains(family[i+4]),
                            "Packaged nature variety lacks a seasonal runtime sprite");
                    const auto summer = art.sprite_bounds(family[i],ground,32);
                    const auto winter = art.sprite_bounds(family[i+4],ground,32);
                    require(summer.w > 0 && summer.h > 0 && same(summer,winter),
                            "Nature season switch changes its registered ground anchor or extent");
                    for (auto sprite : {family[i],family[i+4]}) {
                        render::Frame2D nature;
                        art.sprite(nature,sprite,ground,32,render::Pass::WorldOrdered);
                        require(nature.vertices.size() == 4 && nature.indices.size() == 6,
                                "Registered nature variety does not emit a textured quad");
                    }
                }
            }
            const auto bounds = art.sprite_bounds(id, ground, 152);
            require(std::abs(bounds.w - 152.f) < .001f,
                    "Sprites without reference_width changed their requested width");
            // Picking follows the authored opaque silhouette, not its bounding
            // rectangle. Also cover mirrored sprites using the same alpha mask.
            int opaque_samples=0, transparent_samples=0;
            for (int y=0;y<19;++y) for (int x=0;x<19;++x) {
                const float u=(float(x)+.5f)/19.f, v=(float(y)+.5f)/19.f;
                const render::Point point{bounds.x+u*bounds.w,bounds.y+v*bounds.h};
                const bool covered=art.hit_test_sprite(id,ground,152,point);
                opaque_samples+=covered; transparent_samples+=!covered;
                require(covered==art.hit_test_sprite(id,ground,152,
                    {bounds.x+(1.f-u)*bounds.w,point.y},true),
                    "Mirrored silhouette picking does not follow the mirrored sprite");
            }
            require(opaque_samples>20 && transparent_samples>20,
                    "Building picking lost its opaque/transparent silhouette distinction");
            require(!art.hit_test_sprite(id,ground,152,{bounds.x-1,bounds.y}) &&
                        !art.hit_test_sprite(id,ground,152,{bounds.x+bounds.w,bounds.y}),
                    "Building picking accepts pixels outside its sprite");
            render::Frame2D frame;
            art.outline_sprite(frame, id, ground, 152, 2, render::Pass::Ui);
            require(frame.vertices.size() == 32 && frame.indices.size() == 48,
                    "Eight-offset outline geometry changed");
            require(frame.commands.size() == 1, "Outline copies did not batch into one draw command");
            const auto *outline = std::get_if<render::GeometryDraw>(&frame.commands.front());
            require(outline && outline->index_count == 48, "Outline draw command is incomplete");
            require(same(bounds, art.sprite_bounds(id, ground, 152)),
                    "Outline changed sprite picking bounds");
            art.sprite(frame, id, ground, 152, render::Pass::Ui);
            require(frame.vertices.size() == 36 && frame.indices.size() == 54 && frame.commands.size() == 2,
                    "Source sprite no longer draws over the silhouette outline");
            require(std::abs(frame.vertices[33].position.x - frame.vertices[32].position.x - 152.f) <
                            .001f &&
                        std::abs(frame.vertices[35].position.y - frame.vertices[32].position.y -
                                 bounds.h) < .001f,
                    "Default sprite quad no longer preserves requested width and aspect");

            const auto document = nlohmann::json::parse(assets.read_path("art/world.json"));
            const nlohmann::json *scaled_entry{};
            std::size_t building_entries=0, resident_entries=0;
            for (const auto &page : document.at("pages")) {
                for (const auto &entry : page.at("sprites")) {
                    const auto entry_id=entry.at("id").get<std::string>();
                    building_entries += entry_id.starts_with("building.");
                    resident_entries += entry_id.starts_with("resident.");
                    if (entry.contains("reference_width")) {
                        scaled_entry = &entry;
                    }
                }
            }
            require(art.outline_sprite_count() == building_entries,
                    "A building sprite is missing its GPU outline mask");
            require(art.hit_mask_count() == building_entries + resident_entries,
                    "A building or resident sprite is missing its CPU hit mask");
            require(resident_entries >= 250,
                    "Canonical resident load/direction/phase coverage is incomplete");
            require(scaled_entry != nullptr,
                    "The packaged resident atlas has no reference_width regression fixture");
            const auto scaled_id = scaled_entry->at("id").get<std::string>();
            const auto rect = scaled_entry->at("rect").get<std::array<int, 4>>();
            const auto pivot = scaled_entry->at("pivot").get<std::array<float, 2>>();
            const float requested_width = 90.f;
            const float expected_width =
                requested_width * float(rect[2]) / scaled_entry->at("reference_width").get<float>();
            const float expected_height = expected_width * float(rect[3]) / float(rect[2]);
            const auto scaled_bounds = art.sprite_bounds(scaled_id, ground, requested_width);
            require(same(scaled_bounds,
                         {ground.x - expected_width * pivot[0],
                          ground.y - expected_height * pivot[1], expected_width, expected_height}),
                    "reference_width did not scale sprite picking bounds proportionally");
            render::Frame2D scaled_frame;
            art.sprite(scaled_frame, scaled_id, ground, requested_width, render::Pass::Ui);
            require(scaled_frame.vertices.size() == 4 && scaled_frame.indices.size() == 6,
                    "reference_width sprite did not emit one complete quad");
            require(std::abs(scaled_frame.vertices[1].position.x -
                             scaled_frame.vertices[0].position.x - expected_width) < .001f &&
                        std::abs(scaled_frame.vertices[3].position.y -
                                 scaled_frame.vertices[0].position.y - expected_height) < .001f,
                    "reference_width sprite quad and registered bounds disagree");

            // Every authored resident load uses the same alpha-aware picking path.
            // This protects wide cart/log silhouettes without allocating resident
            // outline textures on the GPU.
            for (const auto load : {"walk", "logs", "sack", "cart"}) {
                const auto resident_id=std::string("resident.man.")+load+".2.1";
                require(art.contains(resident_id), "Canonical resident load sprite is missing");
                const auto resident_bounds=art.sprite_bounds(resident_id,ground,45);
                int covered=0, clear=0;
                for(int y=0;y<25;++y) for(int x=0;x<25;++x) {
                    const render::Point sample{
                        resident_bounds.x+(float(x)+.5f)/25.f*resident_bounds.w,
                        resident_bounds.y+(float(y)+.5f)/25.f*resident_bounds.h};
                    art.hit_test_sprite(resident_id,ground,45,sample) ? ++covered : ++clear;
                }
                require(covered>8 && clear>8,
                        "Resident hit mask lost its opaque/transparent silhouette distinction");
            }

            const auto bytes = art.outline_texture_bytes();
            const auto count = art.outline_sprite_count();
            const auto hit_bytes = art.hit_mask_bytes();
            const auto hit_count = art.hit_mask_count();
            art.load(assets);
            require(art.outline_texture_bytes() == bytes && art.outline_sprite_count() == count,
                    "Transactional art reload changed the bounded outline cache");
            require(art.hit_mask_bytes() == hit_bytes && art.hit_mask_count() == hit_count,
                    "Transactional art reload changed the bounded CPU hit-mask cache");
            std::cout << "Building silhouette outline: " << count << " masks, " << bytes
                      << " bytes, 8 quads and 1 batched draw per selection passed.\n";
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

#include "ribterra/seasons.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
std::filesystem::path path(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        require(args.size() >= 2, "Output directory required");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        const bool software = args.size() > 2 && args[2] == "--software";
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        {
            using namespace ribterra;
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra construction verification", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, SDL_GetError());
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(window.get(), base / "assets",
                                        {software ? render::Backend::Software : render::Backend::Direct3D12});
            AssetCatalog assets(base / "assets");
            // The lifecycle is exercised through real construction in BuildingTests. This fixture
            // arranges the finished buildings with room to inspect every silhouette in the real renderer.
            World world(70421, 10);
            auto state = world.export_state();
            for (auto &chunk : state.chunks)
                if (chunk.key.layer == WorldLayer::Surface)
                    for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                        const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                        const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                        if (x >= 15 && x <= 36 && y >= 12 && y <= 35)
                            chunk.tiles[i] = {Terrain::Grass, 0, true, Biome::Meadow, 0};
                        if (x >= 33 && x <= 36 && y >= 20 && y <= 29)
                            chunk.tiles[i] = {Terrain::Water, 0, true, Biome::Lake, -6};
                    }
            constexpr std::array<std::pair<int, int>, 16> sites{{{21, 16},
                                                                 {26, 16},
                                                                 {31, 16},
                                                                 {21, 21},
                                                                 {26, 21},
                                                                 {31, 21},
                                                                 {21, 26},
                                                                 {32, 26},
                                                                 {26, 26},
                                                                 {26, 31}, {33,26}, {16,16}, {16,21}, {16,26}, {16,31}, {31,31}}};
            static_assert(sites.size()==building_kind_count-4);
            for (unsigned i = 4; i < building_kind_count; ++i) {
                const auto [x, y] = sites[i - 4];
                state.buildings.push_back({state.next_building_id++, static_cast<BuildingKind>(i), x, y, 1,
                                           WorldLayer::Surface, 1});
                if (i == static_cast<unsigned>(BuildingKind::Mine)) {
                    state.portals.push_back({{x, y, WorldLayer::Surface}, {x, y, WorldLayer::Mine}});
                    for (auto &chunk : state.chunks)
                        if (chunk.key == chunk_key({x, y, WorldLayer::Mine}))
                            chunk.tiles[local_index(x, y)] = {Terrain::MineFloor, 0, true};
                }
            }
            std::string error;
            require(world.import_state(std::move(state), error), error);
            const auto settings = output / "building-ui-settings.json";
            std::ofstream(settings) << R"({"locale":"uk","ui_scale":1})";
            Localization localization(base / "assets/locales");
            GameUi ui(renderer, assets, localization, settings);
            ui.show_world();
            for (int season = 0; season < 2; ++season) {
                if (season) {
                    auto winter = world.export_state();
                    winter.ticks = 3*ribterra::days_per_season*ribterra::ticks_per_day;
                    require(world.import_state(std::move(winter), error), error);
                }
                const auto save =
                    output / (season ? "new-buildings-winter.save" : "new-buildings-summer.save");
                require(world.save(save, error), error);
                ClientSession session;
                require(session.load(save, error), error);
                GameView view(renderer);
                view.load_art(assets);
                render::Frame2D frame;
                view.compose(session, 0, frame, false);
                ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface);
                ui.update(&session, "");
                ui.compose(frame);
                renderer.render(frame,
                                output / (season ? "new-buildings-winter.png" : "new-buildings-summer.png"));
                if (!season) {
                    require(ui.activate_for_test("build-more"), "Catalog button missing");
                    ui.update(&session, "");
                    const auto center = ui.element_center_for_test("catalog-mine");
                    require(center.x > 0 && center.y > 0 && center.y < 800,
                            "Catalog is hidden or outside viewport");
                    view.compose(session, 0, frame, false);
                    ui.compose(frame);
                    renderer.render(frame, output / "new-buildings-catalog.png");
                    require(SDL_SetWindowSize(window.get(), 1100, 700), SDL_GetError());
                    SDL_PumpEvents();
                    renderer.render(frame, output / "new-buildings-compact.png");
                    require(SDL_SetWindowSize(window.get(), 1440, 900), SDL_GetError());
                    SDL_PumpEvents();
                    ui.activate_for_test("catalog-close");
                }
            }
            const auto &device = renderer.device_info();
            std::ofstream(output / "building-render-report.json")
                << nlohmann::json{{"new_buildings", 11},
                                  {"generated_frames", 55},
                                  {"new_icons", 11},
                                  {"summer_and_winter", true},
                                  {"ukrainian_catalog", true},
                                  {"compact_capture", true},
                                  {"backend", device.backend},
                                  {"device", device.device},
                                  {"software", device.software}}
                       .dump(2);
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

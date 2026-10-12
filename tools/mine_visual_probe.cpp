#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        if (args.size() < 2 || args.size() > 3)
            return 2;
        const bool software = args.size() == 3 && args[2] == "--software";
        if (args.size() == 3 && !software)
            return 2;
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra mine visual probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "window creation failed");
            const auto base = path(SDL_GetBasePath());
            ribterra::render::Renderer2D renderer(window.get(), base / "assets",
                                                  {software
                                                       ? ribterra::render::Backend::Software
                                                       : ribterra::render::Backend::Direct3D12});
            const auto &device = renderer.device_info();
            std::ofstream(output / "mine-device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);

            ribterra::ClientSession session(70421, "Mine probe");
            require(session.portals().empty(), "New settlement has a free passage");
            require(session.build(ribterra::BuildingKind::Mine, 28, 24).accepted, "Mine construction rejected");
            for (int tick = 0; tick < 3000 && session.portals().empty(); ++tick) session.advance();
            require(session.portals().size() == 1, "Mine construction did not open passage");
            while (session.take_command_reply()) {}
            ribterra::GameView view(renderer);
            const auto resident = session.residents().front();
            SDL_Event select{};
            select.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            select.button.button = SDL_BUTTON_LEFT;
            const auto resident_screen = view.tile_screen(resident.x, resident.y);
            select.button.x = resident_screen.x;
            select.button.y = resident_screen.y;
            view.handle_event(select, session);
            require(view.selected_ids().size() == 1 && view.selected_ids().front() == resident.id,
                    "native selection event did not select resident 1");

            require(!session.portals().empty(), "completed mine passage is not visible to the client");
            const auto portal = session.portals().front();
            require(session
                        .move(view.selected_ids(), portal.mine.x, portal.mine.y,
                              ribterra::WorldLayer::Mine)
                        .accepted,
                    "mine passage movement was rejected");
            bool reached_mine = false;
            bool passage_applied = false;
            for (int tick = 0; tick < 5000; ++tick) {
                session.advance();
                while (const auto reply = session.take_command_reply()) {
                    require(reply->status != ribterra::CommandReplyStatus::Rejected,
                            "Mine passage was terminally rejected");
                    passage_applied =
                        passage_applied || reply->status == ribterra::CommandReplyStatus::Applied;
                }
                const auto current = std::find_if(
                    session.residents().begin(), session.residents().end(),
                    [&](const ribterra::Resident &value) { return value.id == resident.id; });
                if (current != session.residents().end() &&
                    current->layer == ribterra::WorldLayer::Mine) {
                    reached_mine = true;
                    break;
                }
            }
            require(reached_mine, "resident did not traverse the passage into the mine");
            require(passage_applied, "Mine passage lacked its terminal Applied reply");

            ribterra::CellCoord ore{};
            bool gathering = false;
            for (int y = ribterra::world_min; y < ribterra::world_max && !gathering; ++y)
                for (int x = ribterra::world_min; x < ribterra::world_max && !gathering; ++x) {
                    const auto &tile = session.tile(x, y, ribterra::WorldLayer::Mine);
                    if (tile.explored && tile.terrain == ribterra::Terrain::MineOre &&
                        tile.amount > 0 &&
                        session.gather(view.selected_ids(), x, y, ribterra::WorldLayer::Mine)
                            .accepted) {
                        for (int tick = 0; tick < 1202; ++tick) {
                            session.advance();
                            const auto reply = session.take_command_reply();
                            if (!reply || reply->status == ribterra::CommandReplyStatus::Queued)
                                continue;
                            gathering = reply->status == ribterra::CommandReplyStatus::Applied;
                            if (gathering)
                                ore = {x, y, ribterra::WorldLayer::Mine};
                            break;
                        }
                    }
                }
            require(gathering, "no reachable explored mine ore was available");
            const int initial_ore = session.tile(ore.x, ore.y, ore.layer).amount;
            bool depleted = false;
            for (int tick = 0; tick < 8000; ++tick) {
                session.advance();
                const auto &tile = session.tile(ore.x, ore.y, ore.layer);
                if (tile.terrain == ribterra::Terrain::MineFloor && tile.amount == 0) {
                    depleted = true;
                    break;
                }
            }
            require(depleted, "mine ore did not deplete into mine floor");

            const auto save = output / "mine-probe.save";
            std::string error;
            require(session.save(save, error), "mine probe save failed");
            ribterra::ClientSession restored(1, "Restored mine probe");
            require(restored.load(save, error), "mine probe resume failed");
            require(restored.tile(ore.x, ore.y, ore.layer).terrain == ribterra::Terrain::MineFloor,
                    "mine depletion was not preserved by save/resume");
            require(restored.chunks() == session.chunks() &&
                        restored.portals() == session.portals() &&
                        restored.residents() == session.residents() &&
                        restored.buildings() == session.buildings() &&
                        restored.resources() == session.resources() &&
                        restored.ticks() == session.ticks(),
                    "visible state differs after mine save/resume");
            std::ofstream(output / "mine-result.json") << nlohmann::json{
                {"seed", 70421},
                {"resident_id", resident.id},
                {"ticks", restored.ticks()},
                {"ore_x", ore.x},
                {"ore_y", ore.y},
                {"initial_ore", initial_ore},
                {"remaining_ore", restored.tile(ore.x, ore.y, ore.layer).amount},
                {"stone", restored.resources().stone},
                {"view_preserved_after_resume", true}}.dump(2);

            ribterra::AssetCatalog assets(base / "assets");
            view.load_art(assets);
            ribterra::Localization localization(base / "assets/locales");
            std::ofstream(output / "mine-ui-settings.json")
                << nlohmann::json{{"locale", "uk"}, {"ui_scale", 1.15}};
            ribterra::GameUi ui(renderer, assets, localization, output / "mine-ui-settings.json");
            ui.show_world();
            view.set_layer(ribterra::WorldLayer::Mine);
            ui.set_view_state(&restored, view.selected_ids(), false, 1, view.layer());
            ui.update(restored, localization.text("status.layer_mine"));
            ribterra::render::Frame2D frame;
            view.compose(restored, 0, frame, false);
            ui.compose(frame);
            renderer.render(frame, output / "mine-resumed.png");
            std::filesystem::remove(save);
        }
        SDL_Quit();
        std::cout << "Mine passage, depletion, save/resume and rendered HUD probe passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

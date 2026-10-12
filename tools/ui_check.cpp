#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <memory>

namespace {
void require_ui_geometry(const ribterra::render::Frame2D &frame) {
    const bool found =
        std::any_of(frame.commands.begin(), frame.commands.end(), [](const auto &command) {
            const auto *draw = std::get_if<ribterra::render::GeometryDraw>(&command);
            return draw && draw->pass == ribterra::render::Pass::Ui && draw->index_count > 0;
        });
    if (!found)
        throw std::runtime_error("Native UI validation produced no visible geometry");
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = ribterra::utf8_arguments(argc, argv);
        if (args.size() != 2)
            return 2;
        const auto path = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(args[1].data()), args[1].size()));
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra asset validation", 1440, 900, SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            if (!window)
                throw std::runtime_error(SDL_GetError());
            ribterra::render::Renderer2D renderer(window.get(), path,
                                                  {ribterra::render::Backend::Software});
            ribterra::AssetCatalog assets(path);
            ribterra::Localization localization(path / "locales");
            ribterra::GameUi ui(renderer, assets, localization,
                                path.parent_path() / ".ui-validation-settings.json");
            ribterra::ClientSession session(1234, "Asset validation");
            ribterra::render::Frame2D frame;
            ui.update(session);
            ui.compose(frame);
            require_ui_geometry(frame);
            renderer.render(frame);
            ui.show_world();
            frame.clear_commands();
            ui.update(session);
            ui.compose(frame);
            require_ui_geometry(frame);
            renderer.render(frame);
        }
        SDL_Quit();
        std::cout
            << "Native RmlUi menu/HUD, FreeType fonts and render geometry validation passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

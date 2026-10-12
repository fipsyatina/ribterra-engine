#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using json = nlohmann::json;

void check(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char *>(bytes.data()),
                                                   static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Could not read lifecycle fixture");
    return bytes;
}

void write_bytes(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char *>(bytes.data()),
                                                    static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Could not write lifecycle fixture");
}

std::string read_text(const std::filesystem::path &path) {
    const auto bytes = read_bytes(path);
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void write_text(const std::filesystem::path &path, std::string_view text) {
    write_bytes(path, {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

void refresh_manifest(const std::filesystem::path &assets, std::string_view changed_path) {
    const auto manifest_path = assets / "manifest.json";
    auto document = json::parse(read_text(manifest_path));
    bool found = false;
    for (auto &row : document.at("assets")) {
        if (row.at("path").get<std::string>() != changed_path)
            continue;
        const auto bytes = read_bytes(assets / std::filesystem::path(changed_path));
        row["bytes"] = bytes.size();
        row["sha256"] = ribterra::sha256(bytes);
        found = true;
    }
    check(found, "Changed UI asset is missing from the staged manifest");
    std::vector<json> rows(document.at("assets").begin(), document.at("assets").end());
    std::sort(rows.begin(), rows.end(), [](const json &left, const json &right) {
        return left.at("id").get<std::string>() < right.at("id").get<std::string>();
    });
    std::string canonical;
    for (const auto &row : rows) {
        for (const char *field :
             {"id", "path", "kind", "sha256", "license", "source", "author", "notice"}) {
            const auto value = row.at(field).get<std::string>();
            canonical += std::to_string(value.size()) + ":" + value;
        }
        canonical += '\n';
    }
    document["content_sha256"] = ribterra::sha256(
        std::span(reinterpret_cast<const std::uint8_t *>(canonical.data()), canonical.size()));
    write_text(manifest_path, document.dump(2) + "\n");
}

struct Workspace {
    std::filesystem::path path;
    Workspace() {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 64; ++attempt) {
            path = base / ("ribterra-ui-lifecycle-" + std::to_string(stamp) + "-" +
                           std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
                return;
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error("Could not create lifecycle workspace",
                                                        path, error);
        }
        throw std::runtime_error("Could not reserve lifecycle workspace");
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path copy_assets(const std::filesystem::path &source,
                                      std::string_view name) const {
        const auto destination = path / std::filesystem::path(name) / "assets";
        std::filesystem::create_directories(destination);
        std::filesystem::copy(source, destination,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        return destination;
    }
};

void render_valid(ribterra::render::Renderer2D &renderer, const std::filesystem::path &assets,
                  const std::filesystem::path &settings) {
    ribterra::AssetCatalog catalog(assets);
    ribterra::Localization localization(assets / "locales");
    ribterra::GameUi ui(renderer, catalog, localization, settings, {1440, 900});
    ribterra::render::Frame2D frame;
    ui.compose(frame);
    check(!frame.commands.empty(), "Recovered GameUi emitted no render commands");
    renderer.render(frame);
}

template <class Mutator>
void failure_then_recovery(ribterra::render::Renderer2D &renderer, const Workspace &workspace,
                           const std::filesystem::path &staged_assets,
                           const std::filesystem::path &valid_assets, std::string_view name,
                           Mutator &&mutate) {
    const auto broken_assets = workspace.copy_assets(staged_assets, name);
    mutate(broken_assets);
    ribterra::AssetCatalog catalog(broken_assets);
    ribterra::Localization localization(broken_assets / "locales");
    bool rejected = false;
    try {
        ribterra::GameUi ui(renderer, catalog, localization,
                            workspace.path / (std::string(name) + "-bad-settings.json"),
                            {1440, 900});
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid UI fixture was accepted");
    render_valid(renderer, valid_assets,
                 workspace.path / (std::string(name) + "-recovery-settings.json"));
}

void unsupported_filter_then_recovery(ribterra::render::Renderer2D &renderer,
                                      const Workspace &workspace,
                                      const std::filesystem::path &staged_assets,
                                      const std::filesystem::path &valid_assets) {
    const auto assets = workspace.copy_assets(staged_assets, "unsupported-filter");
    const auto style_path = assets / "ui/ribterra.rcss";
    auto style = read_text(style_path);
    style += "\n#new-game { filter: blur(2px); }\n";
    write_text(style_path, style);
    refresh_manifest(assets, "ui/ribterra.rcss");
    {
        ribterra::AssetCatalog catalog(assets);
        ribterra::Localization localization(assets / "locales");
        ribterra::GameUi ui(renderer, catalog, localization,
                            workspace.path / "unsupported-filter-settings.json", {1440, 900});
        ribterra::render::Frame2D frame;
        bool rejected = false;
        try {
            ui.compose(frame);
        } catch (const std::runtime_error &error) {
            rejected = std::string_view(error.what()).find("unsupported advanced rendering feature") !=
                       std::string_view::npos;
        }
        check(rejected, "Supported RML filter did not reach the adapter's explicit rejection path");
    }
    render_valid(renderer, valid_assets, workspace.path / "unsupported-filter-recovery-settings.json");
}
} // namespace

int main() {
    SDL_Window *window = nullptr;
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        window = SDL_CreateWindow("Ribterra UI lifecycle test", 1100, 700, SDL_WINDOW_HIDDEN);
        check(window != nullptr, "Window creation failed");
        const auto base = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
        const auto staged_assets = base / "assets";
        check(std::filesystem::is_regular_file(staged_assets / "manifest.json"),
              "Staged asset manifest is unavailable");
        Workspace workspace;
        const auto valid_assets = workspace.copy_assets(staged_assets, "valid");
        {
            ribterra::render::Renderer2D renderer(window, valid_assets,
                                                  {ribterra::render::Backend::Software, false});
            failure_then_recovery(
                renderer, workspace, staged_assets, valid_assets, "missing-menu-id",
                [](const auto &assets) {
                    const auto path = assets / "ui/menu.rml";
                    auto text = read_text(path);
                    const std::string expected = "id=\"new-game\"";
                    const auto position = text.find(expected);
                    check(position != std::string::npos, "Menu fixture lacks new-game ID");
                    text.replace(position, expected.size(), "id=\"new-game-missing\"");
                    write_text(path, text);
                    refresh_manifest(assets, "ui/menu.rml");
                });
            failure_then_recovery(renderer, workspace, staged_assets, valid_assets,
                                  "malformed-rcss", [](const auto &assets) {
                                      const auto path = assets / "ui/ribterra.rcss";
                                      auto text = read_text(path);
                                      text += "\n#new-game { definitely-not-a-property: 1dp; }\n";
                                      write_text(path, text);
                                      refresh_manifest(assets, "ui/ribterra.rcss");
                                  });
            unsupported_filter_then_recovery(renderer, workspace, staged_assets, valid_assets);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "RmlUi constructor failure cleanup and same-process recovery passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " SDL=" << SDL_GetError() << '\n';
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}

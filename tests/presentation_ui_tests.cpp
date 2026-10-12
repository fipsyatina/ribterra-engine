#include "ribterra/asset_catalog.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::filesystem::path unique_run_directory(const std::filesystem::path &root) {
    std::filesystem::create_directories(root);
    for (int run = 1; run <= 10000; ++run) {
        const auto candidate = root / ("run-" + std::to_string(run));
        if (std::filesystem::create_directory(candidate))
            return candidate;
    }
    throw std::runtime_error("Could not create a unique retained presentation UI test directory");
}

void write_new_json(const std::filesystem::path &path, const nlohmann::json &value) {
    check(!std::filesystem::exists(path), "Presentation UI fixture path already exists");
    std::ofstream output(path);
    output << value.dump(2) << '\n';
    check(static_cast<bool>(output), "Could not write presentation UI fixture");
}

nlohmann::json read_json(const std::filesystem::path &path) {
    std::ifstream input(path);
    check(static_cast<bool>(input), "Could not read presentation UI fixture");
    nlohmann::json value;
    input >> value;
    return value;
}

nlohmann::json settings(std::string mode, int frame_limit, int frames_in_flight) {
    return {{"locale", "pl"},
            {"ui_scale", 1.15},
            {"presentation", {{"mode", std::move(mode)},
                              {"frame_limit", frame_limit},
                              {"frames_in_flight", frames_in_flight}}}};
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Usage: PresentationUiTests <retained-artifact-output-root>");
        const auto run_directory = unique_run_directory(argv[1]);
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window *window = SDL_CreateWindow("Ribterra presentation UI test", 1280, 720, SDL_WINDOW_HIDDEN);
        check(window != nullptr, "SDL test window creation failed");
        {
            const auto base = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
            ribterra::AssetCatalog assets(base / "assets");
            assets.verify();
            ribterra::Localization localization(base / "assets" / "locales");
            ribterra::render::Renderer2D renderer(
                window, base / "assets", {ribterra::render::Backend::Software, false});

            const auto persistence_path = run_directory / "persistence-and-unknown-key.json";
            auto persistence = settings("vsync", 77, 2);
            persistence["future_setting"] = {{"kept", true}, {"version", 4}};
            write_new_json(persistence_path, persistence);
            {
                ribterra::GameUi ui(renderer, assets, localization, persistence_path, {1280, 720});
                const auto before = renderer.presentation_status();
                auto actions = ui.take_actions();
                check(actions.size() == 1 &&
                          actions.front().type == ribterra::UiActionType::PresentationSettings,
                      "Valid saved presentation settings must queue one typed restore intent");
                check(actions.front().presentation == ribterra::render::PresentationRequest{
                                                          ribterra::render::PresentMode::VSync, 77, 2},
                      "A custom loaded FPS cap must be retained exactly");
                check(renderer.presentation_status().requested == before.requested &&
                          renderer.presentation_status().effective == before.effective,
                      "Loading UI settings must not mutate the renderer");
                check(!ui.enabled_for_test("frames-in-flight") &&
                          ui.enabled_for_test("fps-cap"),
                      "Software UI must disable GPU queue depth but keep FPS cap enabled");
                check(!ui.select_option_rml_for_test("present-mode", 1).empty() &&
                          !ui.select_option_rml_for_test("present-mode", 2).empty(),
                      "Software UI must retain localized unsupported GPU mode choices");
                check(renderer.set_presentation(actions.front().presentation),
                      "Software renderer must accept caller-paced FPS with active GPU defaults");
                ui.presentation_applied();
                check(ui.activate_for_test("open-settings"), "Settings menu control is missing");
                ui.update(nullptr);
                check(ui.visible_for_test("settings-card"),
                      "Presentation capability check requires the visible settings card");
                check(ui.focus_for_test("present-mode"), "Presentation mode select must accept focus");
                SDL_Event key{};
                key.type = SDL_EVENT_KEY_DOWN;
                key.key.key = SDLK_RETURN;
                check(ui.process_event(key), "Presentation mode select must open from the keyboard");
                key.key.key = SDLK_DOWN;
                check(ui.process_event(key), "Presentation mode select must handle keyboard navigation");
                key.key.key = SDLK_RETURN;
                check(ui.process_event(key), "Presentation mode select must close from the keyboard");
                check(ui.activate_for_test("presentation-apply"),
                      "Presentation Apply control is missing after acknowledgement");
                actions = ui.take_actions();
                check(actions.size() == 1 &&
                          actions.front().presentation.mode == ribterra::render::PresentMode::VSync,
                      "Keyboard navigation must skip unsupported Immediate and Mailbox options");
            }
            const auto persisted = read_json(persistence_path);
            check(persisted.at("future_setting") == persistence.at("future_setting"),
                  "Saving presentation settings must retain unknown JSON keys");
            check(persisted.at("presentation").at("frame_limit") == 77,
                  "Successful acknowledgement must persist the exact custom FPS cap");
            {
                ribterra::GameUi restarted(renderer, assets, localization, persistence_path, {1280, 720});
                const auto actions = restarted.take_actions();
                check(actions.size() == 1 && actions.front().presentation.frame_limit == 77,
                      "A successful acknowledgement must restore on restart");
            }

            const auto cli_path = run_directory / "cli-override.json";
            write_new_json(cli_path, settings("vsync", 240, 2));
            {
                const auto before = renderer.presentation_status();
                ribterra::GameUi ui(renderer, assets, localization, cli_path, {1280, 720}, false);
                check(ui.take_actions().empty(), "Explicit CLI presentation flags must skip saved restore");
                check(renderer.presentation_status().requested == before.requested &&
                          renderer.presentation_status().effective == before.effective,
                      "CLI restore suppression must preserve the renderer's initial request");
            }

            const nlohmann::json invalid_presentations[] = {
                settings("unknown", 60, 2), settings("vsync", 1001, 2), settings("vsync", 60, 4)};
            for (std::size_t index = 0; index < std::size(invalid_presentations); ++index) {
                const auto path = run_directory / ("invalid-presentation-" + std::to_string(index) + ".json");
                write_new_json(path, invalid_presentations[index]);
                ribterra::GameUi ui(renderer, assets, localization, path, {1280, 720});
                check(ui.locale() == "pl" && ui.scale() > 1.14f && ui.scale() < 1.16f,
                      "Invalid presentation preferences must not reset valid locale or UI scale");
                check(ui.take_actions().empty(), "Invalid presentation preferences must not queue an intent");
            }

            const auto reset_path = run_directory / "reset-pending-edit.json";
            write_new_json(reset_path, settings("vsync", 144, 2));
            {
                ribterra::GameUi ui(renderer, assets, localization, reset_path, {1280, 720});
                check(ui.activate_for_test("presentation-reset"), "Presentation reset control is missing");
                check(ui.take_actions().empty(), "Reset to active must discard a queued unapplied edit");
            }

            const auto intent_path = run_directory / "runtime-intent.json";
            write_new_json(intent_path, {{"locale", "en"}, {"ui_scale", 1.0}});
            {
                ribterra::GameUi ui(renderer, assets, localization, intent_path, {1280, 720});
                const auto before = renderer.presentation_status();
                check(ui.activate_for_test("presentation-apply"), "Presentation Apply control is missing");
                const auto actions = ui.take_actions();
                check(actions.size() == 1 &&
                          actions.front().type == ribterra::UiActionType::PresentationSettings &&
                          actions.front().presentation == before.requested,
                      "Apply must queue the complete typed presentation request");
                check(renderer.presentation_status().requested == before.requested &&
                          renderer.presentation_status().effective == before.effective,
                      "Runtime UI edits must not mutate the renderer directly");
            }

            const auto new_preferences_path = run_directory / "new-preferences.json";
            check(!std::filesystem::exists(new_preferences_path),
                  "Brand-new preferences fixture unexpectedly exists");
            {
                ribterra::GameUi ui(renderer, assets, localization, new_preferences_path, {1280, 720});
                check(ui.activate_for_test("presentation-apply"),
                      "Apply control is missing for brand-new preferences");
                const auto actions = ui.take_actions();
                check(actions.size() == 1 &&
                          actions.front().type == ribterra::UiActionType::PresentationSettings,
                      "Brand-new preferences must still emit a typed Apply intent");
                check(renderer.set_presentation(actions.front().presentation),
                      "Renderer rejected the default brand-new presentation request");
                ui.presentation_applied();
            }
            const auto new_preferences = read_json(new_preferences_path);
            check(new_preferences.is_object() && new_preferences.contains("presentation") &&
                      new_preferences.contains("locale") && new_preferences.contains("ui_scale"),
                  "Acknowledging Apply must create a valid object for brand-new preferences");

            const auto invalid_target = run_directory / "settings-path-is-a-directory";
            check(std::filesystem::create_directory(invalid_target),
                  "Could not create retained invalid settings target fixture");
            {
                ribterra::GameUi ui(renderer, assets, localization, invalid_target, {1280, 720});
                const auto effective_before = renderer.presentation_status().effective;
                check(ui.activate_for_test("presentation-apply"),
                      "Apply control is missing for invalid settings target");
                auto actions = ui.take_actions();
                check(actions.size() == 1 && renderer.set_presentation(actions.front().presentation),
                      "Invalid settings target fixture requires a successfully applied runtime request");
                ui.presentation_applied();
                check(renderer.presentation_status().effective == effective_before,
                      "A save failure must not alter the already effective presentation tuple");
                check(ui.element_rml_for_test("presentation-explanation").find("could not be saved") !=
                          std::string::npos,
                      "A runtime-applied save failure must be reported honestly");
                check(ui.enabled_for_test("presentation-apply"),
                      "A save failure must clear pending state and re-enable Apply");
                const auto retained_failed_target =
                    run_directory / "settings-path-is-a-directory-retained";
                check(!std::filesystem::exists(retained_failed_target),
                      "Retained failed settings target unexpectedly exists");
                std::filesystem::rename(invalid_target, retained_failed_target);
                check(ui.activate_for_test("open-settings"),
                      "Settings menu control is missing for persistence recovery");
                ui.update(nullptr);
                check(ui.visible_for_test("settings-card"),
                      "Persistence recovery requires the visible settings card");
                check(ui.focus_for_test("language"),
                      "Language select must accept focus for persistence recovery");
                SDL_Event language_key{};
                language_key.type = SDL_EVENT_KEY_DOWN;
                language_key.key.key = SDLK_RETURN;
                check(ui.process_event(language_key), "Language select must open from the keyboard");
                language_key.key.key = SDLK_DOWN;
                check(ui.process_event(language_key), "Language select must change from the keyboard");
                language_key.key.key = SDLK_RETURN;
                check(ui.process_event(language_key), "Language select must close from the keyboard");
                check(ui.element_rml_for_test("presentation-explanation") ==
                          localization.text("settings.presentation_unavailable.backend"),
                      "A successful UI settings write must clear the stale save warning");
                const auto recovered = read_json(invalid_target);
                check(recovered.at("presentation").at("mode") == "vsync" &&
                          recovered.at("presentation").at("frame_limit") == effective_before.frame_limit &&
                          recovered.at("presentation").at("frames_in_flight") ==
                              effective_before.frames_in_flight,
                      "A successful locale write must also persist the already active presentation tuple");
                check(ui.activate_for_test("presentation-apply"),
                      "A save failure must allow retrying Apply");
                actions = ui.take_actions();
                check(actions.size() == 1 &&
                          actions.front().type == ribterra::UiActionType::PresentationSettings,
                      "Retry after a save failure must queue a fresh typed request");
            }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Presentation UI typed intents, capabilities, persistence and retained fixtures passed. "
                  << "Artifacts: " << run_directory.string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " SDL=" << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

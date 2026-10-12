#include "ribterra/asset_catalog.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace ribterra;
using namespace ribterra::render;
using Json = nlohmann::json;

void require(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}

std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

std::string_view mode_name(PresentMode mode) {
    switch (mode) {
    case PresentMode::VSync:
        return "vsync";
    case PresentMode::Immediate:
        return "immediate";
    case PresentMode::Mailbox:
        return "mailbox";
    }
    throw std::invalid_argument("Unknown presentation mode");
}

std::string_view reason_name(PresentationUnavailableReason reason) {
    switch (reason) {
    case PresentationUnavailableReason::None:
        return "none";
    case PresentationUnavailableReason::UnsupportedBackend:
        return "unsupported_backend";
    case PresentationUnavailableReason::UnsupportedMode:
        return "unsupported_mode";
    case PresentationUnavailableReason::ApplyFailed:
        return "apply_failed";
    }
    throw std::invalid_argument("Unknown presentation unavailable reason");
}

Json request_json(const PresentationRequest &value) {
    return {{"mode", mode_name(value.mode)},
            {"frame_limit", value.frame_limit},
            {"frames_in_flight", value.frames_in_flight}};
}

Json status_json(const PresentationStatus &value) {
    return {{"requested", request_json(value.requested)},
            {"effective", request_json(value.effective)},
            {"available", value.available},
            {"immediate_supported", value.immediate_supported},
            {"mailbox_supported", value.mailbox_supported},
            {"reason", reason_name(value.reason)},
            {"error", value.error}};
}

Json rect_json(const Rect &value) {
    return {{"x", value.x}, {"y", value.y}, {"width", value.w}, {"height", value.h}};
}

Json point_json(const Point &value) {
    return {{"x", value.x}, {"y", value.y}};
}

void write_new_json(const std::filesystem::path &destination, const Json &value) {
    require(!std::filesystem::exists(destination), "Refusing to overwrite retained JSON evidence");
    std::ofstream output(destination);
    output << value.dump(2) << '\n';
    require(output.good(), "Could not write JSON evidence");
}

Json read_json(const std::filesystem::path &source) {
    std::ifstream input(source);
    require(input.good(), "Could not read settings JSON");
    Json value;
    input >> value;
    return value;
}

struct Case {
    std::string name;
    Extent window_size;
    float ui_scale;
    PresentationRequest request;
    bool allow_unsupported{};
};

bool inside(const Rect &child, const Rect &parent, Extent viewport) {
    constexpr float epsilon = .5f;
    return child.w > 0 && child.h > 0 && child.x + epsilon >= std::max(0.f, parent.x) &&
           child.y + epsilon >= std::max(0.f, parent.y) &&
           child.x + child.w <= std::min(static_cast<float>(viewport.width), parent.x + parent.w) + epsilon &&
           child.y + child.h <= std::min(static_cast<float>(viewport.height), parent.y + parent.h) + epsilon;
}

Json action_bounds(GameUi &ui) {
    Json result = Json::object();
    for (const auto *id : {"presentation-reset", "presentation-apply", "settings-back"})
        result[id] = rect_json(ui.element_bounds_for_test(id));
    return result;
}

bool actions_inside(GameUi &ui, Extent viewport) {
    const auto card = ui.element_bounds_for_test("settings-card");
    for (const auto *id : {"presentation-reset", "presentation-apply", "settings-back"})
        if (!inside(ui.element_bounds_for_test(id), card, viewport))
            return false;
    return true;
}

void capture(GameUi &ui, Renderer2D &renderer, Extent logical_size,
             const std::filesystem::path &destination) {
    require(!std::filesystem::exists(destination), "Refusing to overwrite retained PNG evidence");
    Frame2D frame;
    frame.logical_size = logical_size;
    ui.update(nullptr);
    ui.compose(frame);
    renderer.render(frame, destination);
    require(renderer.frame_telemetry().submitted && renderer.frame_telemetry().capture,
            "Native renderer did not report a submitted synchronous capture");
}

Json run_case(const Case &test, const std::filesystem::path &output,
              const std::filesystem::path &assets_path, AssetCatalog &assets) {
    const auto settings_path = output / (test.name + "-settings.json");
    write_new_json(settings_path,
                   {{"locale", "uk"},
                    {"ui_scale", test.ui_scale},
                    {"presentation", request_json(test.request)}});

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow(("Ribterra settings proof: " + test.name).c_str(),
                         static_cast<int>(test.window_size.width),
                         static_cast<int>(test.window_size.height), SDL_WINDOW_RESIZABLE),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_HIDDEN) == 0,
            "Presentation settings proof requires a visible SDL window");
    SDL_PumpEvents();

    RendererOptions options;
    options.backend = Backend::Direct3D12;
    Renderer2D renderer(window.get(), assets_path, options);
    const auto &device = renderer.device_info();
    require(!device.software && device.backend == "direct3d12",
            "Presentation settings proof requires the native D3D12 renderer");
    renderer.set_logical_size(test.window_size);
    const auto logical_size = renderer.logical_size();
    const auto display = renderer.display_info();
    const auto before = renderer.presentation_status();

    Localization localization(assets_path / "locales");
    GameUi ui(renderer, assets, localization, settings_path, logical_size);
    require(ui.locale() == "uk" && std::abs(ui.scale() - test.ui_scale) < .001f,
            "GameUi did not restore the Ukrainian locale and requested UI scale");
    auto restore_actions = ui.take_actions();
    require(restore_actions.size() == 1 &&
                restore_actions.front().type == UiActionType::PresentationSettings &&
                restore_actions.front().presentation == test.request,
            "GameUi constructor did not queue the exact typed presentation restore");
    const auto after_constructor = renderer.presentation_status();
    require(after_constructor.requested == before.requested &&
                after_constructor.effective == before.effective,
            "GameUi constructor mutated renderer presentation state");

    Json result{{"name", test.name},
                {"settings_file", settings_path.filename().string()},
                {"window_requested", {test.window_size.width, test.window_size.height}},
                {"window_visible", true},
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software},
                {"drawable", {display.drawable.width, display.drawable.height}},
                {"logical", {logical_size.width, logical_size.height}},
                {"display", {{"refresh_hz", display.refresh_hz},
                              {"pixel_density", display.pixel_density},
                              {"display_scale", display.display_scale}}},
                {"capabilities", {{"available", before.available},
                                  {"immediate_supported", before.immediate_supported},
                                  {"mailbox_supported", before.mailbox_supported}}},
                {"requested", request_json(test.request)},
                {"status_before_constructor", status_json(before)},
                {"status_after_constructor", status_json(after_constructor)},
                {"typed_restore_queued", true},
                {"renderer_unchanged_before_app_apply", true},
                {"captures", Json::array()}};

    const bool mode_supported = test.request.mode == PresentMode::VSync ||
                                (test.request.mode == PresentMode::Immediate && before.immediate_supported) ||
                                (test.request.mode == PresentMode::Mailbox && before.mailbox_supported);
    if (!mode_supported) {
        require(test.allow_unsupported, "Required native presentation mode is unsupported");
        result["outcome"] = "skipped_unsupported_mode";
        result["skip_reason"] = "renderer capability reports requested mode unsupported";
        result["app_apply_called"] = false;
        result["effective"] = request_json(after_constructor.effective);
        result["capture_claim"] = false;
        result["pending_restore_not_rendered"] = true;
        result["settings_status"] = {{"locale", ui.locale()},
                                     {"ui_scale", ui.scale()},
                                     {"pending_restore", true},
                                     {"rendered", false}};
        return result;
    }

    const bool accepted = renderer.set_presentation(restore_actions.front().presentation);
    ui.presentation_applied();
    const auto applied = renderer.presentation_status();
    require(accepted && applied.requested == test.request && applied.effective == test.request &&
                applied.reason == PresentationUnavailableReason::None,
            "Native renderer did not apply the exact requested presentation configuration");
    require(ui.take_actions().empty() && ui.enabled_for_test("presentation-apply"),
            "GameUi retained a pending presentation claim after acknowledgement");
    const auto persisted = read_json(settings_path);
    require(persisted.at("locale") == "uk" &&
                std::abs(persisted.at("ui_scale").get<float>() - test.ui_scale) < .001f &&
                persisted.at("presentation") == request_json(test.request),
            "Acknowledged settings JSON does not match the applied request");

    require(ui.activate_for_test("open-settings"), "Actual menu Settings action is unavailable");
    ui.update(nullptr);
    require(ui.visible_for_test("settings-card") && !ui.visible_for_test("open-settings"),
            "Settings card did not replace the actual menu card");
    require(ui.enabled_for_test("presentation-apply"),
            "Captured settings UI still presents an unapplied pending state");

    const auto top_name = test.name + "-settings-top.png";
    capture(ui, renderer, logical_size, output / top_name);
    result["captures"].push_back(top_name);
    result["bounds_before_scroll"] = action_bounds(ui);
    result["settings_card_before_scroll"] = rect_json(ui.element_bounds_for_test("settings-card"));
    const bool needed_scroll = !actions_inside(ui, logical_size);
    int scroll_events = 0;
    int settling_frames = 0;
    if (needed_scroll) {
        const auto center = ui.element_center_for_test("settings-card");
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = center.x;
        motion.motion.y = center.y;
        require(ui.process_event(motion), "Settings card did not accept logical pointer motion");
        while (!actions_inside(ui, logical_size) && scroll_events < 32) {
            SDL_Event wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.wheel.mouse_x = center.x;
            wheel.wheel.mouse_y = center.y;
            wheel.wheel.y = -3.f;
            require(ui.process_event(wheel), "Settings card did not consume SDL wheel scrolling");
            ++scroll_events;
            // RmlUi smooth scrolling advances with elapsed time. Sending all
            // wheel events in a tight loop tests the animation's first frame,
            // not whether a player can reach the controls.
            for (int tick = 0; tick < 8 && !actions_inside(ui, logical_size); ++tick) {
                SDL_Delay(16);
                SDL_PumpEvents();
                Frame2D frame;
                frame.logical_size = logical_size;
                ui.update(nullptr);
                ui.compose(frame);
                renderer.render(frame);
                ++settling_frames;
            }
        }
        const auto bottom_name = test.name + "-settings-bottom.png";
        capture(ui, renderer, logical_size, output / bottom_name);
        result["captures"].push_back(bottom_name);
        if (!actions_inside(ui, logical_size)) {
            result["outcome"] = "failed_scroll_visibility";
            result["bounds_after_scroll"] = action_bounds(ui);
            result["settings_card_after_scroll"] = rect_json(ui.element_bounds_for_test("settings-card"));
            result["scroll_events"] = scroll_events;
            write_new_json(output / (test.name + "-failed-scroll.json"), result);
            throw std::runtime_error("Apply, Reset, and Back did not become fully visible after settings scrolling");
        }
    }
    require(actions_inside(ui, logical_size),
            "Apply, Reset, and Back are outside the settings card or logical viewport");

    result["outcome"] = "applied_and_captured";
    result["app_apply_called"] = true;
    result["effective"] = request_json(applied.effective);
    result["status_after_apply"] = status_json(applied);
    result["settings_status"] = {{"locale", ui.locale()},
                                 {"ui_scale", ui.scale()},
                                 {"persisted", persisted},
                                 {"pending_claim", false},
                                 {"presentation_explanation",
                                  ui.element_rml_for_test("presentation-explanation")}};
    result["scroll"] = {{"needed", needed_scroll},
                         {"event_source", needed_scroll ? "SDL_EVENT_MOUSE_WHEEL" : "none"},
                         {"logical_pointer", point_json(ui.element_center_for_test("settings-card"))},
                         {"events", scroll_events},
                         {"settling_frames", settling_frames},
                         {"settling_delay_ms_per_frame", 16},
                         {"actions_inside_after", true}};
    result["bounds_after_scroll"] = action_bounds(ui);
    result["settings_card_after_scroll"] = rect_json(ui.element_bounds_for_test("settings-card"));
    result["capture_after_explicit_apply"] = true;
    result["native_render"] = true;
    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: PresentationSettingsProbe <new-output-directory>");
        const auto output = std::filesystem::absolute(path(argv[1]));
        require(!std::filesystem::exists(output),
                "Output already exists; refusing to overwrite retained evidence");
        require(std::filesystem::create_directories(output), "Could not create output directory");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto assets_path = path(SDL_GetBasePath()) / "assets";
        AssetCatalog assets(assets_path);
        assets.verify();

        const std::array cases{
            Case{"uk-1280x720-scale130-immediate-cap120-frames1", {1280, 720}, 1.3f,
                 {PresentMode::Immediate, 120, 1}},
            Case{"uk-1920x1080-scale100-vsync-uncapped-frames2", {1920, 1080}, 1.f,
                 {PresentMode::VSync, 0, 2}},
            Case{"uk-1920x1080-scale100-mailbox-cap120-frames3", {1920, 1080}, 1.f,
                 {PresentMode::Mailbox, 120, 3}, true}};

        Json report{{"schema", 1},
                    {"probe", "native D3D12 RmlUi presentation settings proof"},
                    {"game_simulation", false},
                    {"asset_content_hash", assets.content_hash()},
                    {"output_directory_was_new", true},
                    {"cases", Json::array()},
                    {"complete", false}};
        for (const auto &test : cases)
            report["cases"].push_back(run_case(test, output, assets_path, assets));
        report["complete"] = true;
        write_new_json(output / "presentation-settings-report.json", report);
        SDL_Quit();
        std::cout << "Native presentation settings proof completed: " << output.string() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " SDL=" << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/frame_timing.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace {
using namespace ribterra;
using namespace ribterra::render;
using Json = nlohmann::json;

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

double ms(std::uint64_t begin, std::uint64_t end) {
    require(end >= begin, "Monotonic clock moved backwards during measurement");
    return static_cast<double>(end - begin) / 1'000'000.0;
}

std::string_view mode_name(PresentMode mode) {
    switch (mode) {
    case PresentMode::VSync: return "vsync";
    case PresentMode::Immediate: return "immediate";
    case PresentMode::Mailbox: return "mailbox";
    }
    throw std::invalid_argument("Unknown present mode");
}

Json request_json(PresentationRequest value) {
    return {{"mode", mode_name(value.mode)}, {"frame_limit", value.frame_limit},
            {"frames_in_flight", value.frames_in_flight}};
}

Json status_json(const PresentationStatus& value) {
    return {{"requested", request_json(value.requested)},
            {"effective", request_json(value.effective)}, {"available", value.available},
            {"immediate_supported", value.immediate_supported},
            {"mailbox_supported", value.mailbox_supported},
            {"reason", static_cast<int>(value.reason)}, {"error", value.error}};
}

Json summary_json(const FrameTimingWindow& values) {
    const auto s = values.summary();
    return {{"retained_samples", s.retained_samples}, {"total_samples", s.total_samples},
            {"mean_ms", s.mean_ms}, {"p50_ms", s.p50_ms}, {"p95_ms", s.p95_ms},
            {"p99_ms", s.p99_ms}, {"max_ms", s.max_ms},
            {"retained_over_8_333ms", s.over_budget}};
}

void check_telemetry(const RendererFrameTelemetry& value) {
    require(value.measured && !value.gpu_execution_ms.has_value(),
            "Telemetry must be measured CPU time, not fabricated GPU time");
    for (const auto duration : {value.validation_ms, value.resource_wait_ms,
             value.preparation_ms, value.encoding_ms, value.swapchain_wait_ms,
             value.submit_ms, value.capture_ms, value.cpu_total_ms}) {
        require(std::isfinite(duration) && duration >= 0.0, "Invalid CPU telemetry duration");
    }
    const double sum = value.validation_ms + value.resource_wait_ms + value.preparation_ms +
        value.encoding_ms + value.swapchain_wait_ms + value.submit_ms + value.capture_ms;
    require(sum <= value.cpu_total_ms + 0.05, "Overlapping CPU phase telemetry");
}

template<class Function>
void require_invalid(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid presentation request was not rejected");
}

void check_invalid_requests(Renderer2D& renderer) {
    const auto previous = renderer.presentation_status();
    require_invalid([&] { renderer.set_presentation({static_cast<PresentMode>(99), 0, 2}); });
    require_invalid([&] { renderer.set_presentation({PresentMode::VSync, 1001, 2}); });
    require_invalid([&] { renderer.set_presentation({PresentMode::VSync, 0, 0}); });
    require_invalid([&] { renderer.set_presentation({PresentMode::VSync, 0, 4}); });
    require(renderer.presentation_status().requested == previous.requested &&
            renderer.presentation_status().effective == previous.effective,
            "Invalid request changed requested/effective state");
}

void software_contract(const std::filesystem::path& assets) {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra presentation contract", 640, 480, SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    RendererOptions options;
    options.backend = Backend::Software;
    options.collect_telemetry = true;
    Renderer2D renderer(window.get(), assets, options);
    require(!renderer.presentation_status().available, "Software claimed GPU presentation support");
    check_invalid_requests(renderer);
    require(renderer.set_presentation({PresentMode::VSync, 60, 2}) &&
            renderer.presentation_status().effective.frame_limit == 60 &&
            renderer.presentation_status().reason == PresentationUnavailableReason::None,
            "Software rejected caller-side frame limiting");
    const auto previous = renderer.presentation_status().effective;
    const PresentationRequest requested{PresentMode::Immediate, 120, 1};
    require(!renderer.set_presentation(requested), "Software accepted GPU presentation request");
    require(renderer.presentation_status().requested == requested &&
            renderer.presentation_status().effective == previous &&
            renderer.presentation_status().reason == PresentationUnavailableReason::UnsupportedBackend,
            "Software fallback status is misleading");
    Frame2D frame;
    frame.logical_size = {640, 480};
    renderer.render(frame);
    check_telemetry(renderer.frame_telemetry());
    const auto sequence = renderer.frame_telemetry().sequence;
    renderer.set_telemetry_enabled(false);
    renderer.render(frame);
    require(!renderer.frame_telemetry().measured &&
            renderer.frame_telemetry().cpu_total_ms == 0 &&
            renderer.frame_telemetry().sequence > sequence, "Disabled telemetry retained stale timings");
    renderer.set_telemetry_enabled(true);
    frame.logical_size.width = 0;
    bool rejected = false;
    try { renderer.render(frame); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && !renderer.frame_telemetry().submitted &&
            !renderer.frame_telemetry().presentation_queued && renderer.frame_telemetry().sequence == 0,
            "Failed render retained successful-frame telemetry");
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> startup_window(
        SDL_CreateWindow("Ribterra caller FPS startup contract", 320, 240, SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(startup_window != nullptr, SDL_GetError());
    options.presentation = {PresentMode::VSync, 120, 2};
    {
        Renderer2D caller_limit(startup_window.get(), assets, options);
        require(caller_limit.presentation_status().effective.frame_limit == 120,
                "Software startup lost caller-side frame limit");
    }
    options.presentation = requested;
    bool startup_rejected = false;
    try { Renderer2D unsupported(startup_window.get(), assets, options); }
    catch (const std::runtime_error& error) {
        startup_rejected = std::string_view(error.what()).find("Presentation controls") != std::string_view::npos;
    }
    require(startup_rejected, "Software startup did not explicitly reject non-default GPU controls");
    std::cout << "Presentation software contract passed\n";
}

bool poll_quit() {
    SDL_Event event{};
    bool quit = false;
    while (SDL_PollEvent(&event))
        quit = quit || event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    return quit;
}

struct Case {
    std::string name;
    PresentationRequest request;
    bool hud{true};
};

Json benchmark_case(const Case& test, unsigned seconds, const std::filesystem::path& output,
                    Renderer2D& renderer, GameView& view, GameUi& ui, ClientSession& session) {
    require(renderer.set_presentation(test.request), renderer.presentation_status().error);
    if (ui.hud_visible() != test.hud) ui.toggle_hud();
    view.set_annotations_visible(test.hud);
    Frame2D frame;
    FrameLimiter limiter;
    limiter.set_limit(test.request.frame_limit);
    limiter.reset(SDL_GetTicksNS());
    constexpr std::array<std::string_view, 12> names{
        "start_interval", "world_compose", "ui_update", "ui_compose", "renderer_cpu_total",
        "renderer_validation", "renderer_resource_wait", "renderer_preparation",
        "renderer_encoding", "renderer_swapchain_wait", "renderer_submit", "pacing_wait"};
    // Allocate rings once, outside the measurement loop (roughly 768 KiB).
    std::vector<FrameTimingWindow> timings(names.size());
    FrameTimingWindow* intervals = &timings.front();
    const auto warmup_end = SDL_GetTicksNS() + 750'000'000ULL;
    const auto end = warmup_end + std::uint64_t(seconds) * 1'000'000'000ULL;
    std::uint64_t previous_start{}, sample_begin{}, sample_end{}, queued{}, skipped{}, frames{};
    std::uint64_t sample_begin_qpc{};
    const auto qpc_frequency=SDL_GetPerformanceFrequency();
    std::uint64_t vertices{}, indices{}, draws{}, uploads{};
    bool complete = true;
    while (SDL_GetTicksNS() < end) {
        if (poll_quit()) { complete = false; break; }
        const auto start = SDL_GetTicksNS();
        const bool measuring = start >= warmup_end;
        if (measuring && sample_begin == 0) {
            sample_begin = start;
            sample_begin_qpc=SDL_GetPerformanceCounter();
            previous_start = 0;
        }
        if (measuring && previous_start != 0) intervals->add(ms(previous_start, start));
        if (measuring) previous_start = start;
        frame.clear_commands();
        // Frozen real ClientSession; no simulation tick, saves or gameplay commands.
        view.compose(session, 3.0, frame, false);
        const auto world_end = SDL_GetTicksNS();
        ui.set_view_state(&session, {}, false, 1, WorldLayer::Surface);
        ui.update(&session);
        const auto update_end = SDL_GetTicksNS();
        ui.compose(frame);
        const auto ui_end = SDL_GetTicksNS();
        renderer.render(frame);
        const auto& telemetry = renderer.frame_telemetry();
        check_telemetry(telemetry);
        require(!telemetry.capture, "Readback entered benchmark sample");
        const auto pacing_start = SDL_GetTicksNS();
        const auto delay = limiter.delay_ns(pacing_start);
        if (delay != 0) SDL_DelayPrecise(delay);
        const auto pacing_end = SDL_GetTicksNS();
        if (!measuring) continue;
        timings[1].add(ms(start, world_end));
        timings[2].add(ms(world_end, update_end));
        timings[3].add(ms(update_end, ui_end));
        timings[4].add(telemetry.cpu_total_ms);
        timings[5].add(telemetry.validation_ms);
        timings[6].add(telemetry.resource_wait_ms);
        timings[7].add(telemetry.preparation_ms);
        timings[8].add(telemetry.encoding_ms);
        timings[9].add(telemetry.swapchain_wait_ms);
        timings[10].add(telemetry.submit_ms);
        timings[11].add(ms(pacing_start, pacing_end));
        ++frames;
        if (telemetry.presentation_queued) ++queued; else ++skipped;
        vertices += telemetry.vertices;
        indices += telemetry.indices;
        draws += telemetry.draws;
        uploads += telemetry.upload_bytes;
        sample_end = pacing_end;
    }
    // Match external ETW/PresentMon timestamps without counting warmup, captures,
    // statistics sorting, or visibility contracts in the sampled interval.
    const auto sample_end_qpc=frames?SDL_GetPerformanceCounter():0;
    Json measurements = Json::object();
    for (std::size_t i = 0; i < names.size(); ++i) measurements[names[i]] = summary_json(timings[i]);
    const auto interval_summary = intervals->summary();
    const double elapsed = sample_end > sample_begin ? ms(sample_begin, sample_end) : 0.0;
    const auto image = test.name + ".png";
    // Synchronous readback only AFTER all performance samples.
    if (complete) renderer.render(frame, output / image);
    const bool presentation_complete = complete && frames != 0 && queued == frames;
    std::cout << test.name << ": " << (interval_summary.mean_ms > 0 ? 1000 / interval_summary.mean_ms : 0)
              << " CPU-loop FPS, p95=" << interval_summary.p95_ms << " ms, queued=" << queued << '\n'
              << std::flush;
    return {{"name", test.name}, {"complete", presentation_complete}, {"execution_complete", complete},
            {"presentation_samples_valid", presentation_complete}, {"hud_visible", test.hud},
            {"presentation", status_json(renderer.presentation_status())}, {"timings", measurements},
            {"samples", frames}, {"queued_presentations", queued}, {"skipped_presentations", skipped},
            {"sample_elapsed_ms", elapsed}, {"cpu_loop_fps", interval_summary.mean_ms > 0 ?
               Json(1000.0 / interval_summary.mean_ms) : Json(nullptr)},
            {"sample_begin_qpc",sample_begin_qpc},{"sample_end_qpc",sample_end_qpc},
            {"qpc_frequency",qpc_frequency},
            {"gpu_execution_ms", nullptr}, {"displayed_fps", nullptr},
            {"mean_vertices", frames ? double(vertices) / frames : 0},
            {"mean_indices", frames ? double(indices) / frames : 0},
            {"mean_draws", frames ? double(draws) / frames : 0},
            {"mean_upload_bytes", frames ? double(uploads) / frames : 0},
            {"capture_after_sampling", complete ? Json(image) : Json(nullptr)}};
}

void native_probe(const std::filesystem::path& output, unsigned seconds, bool hidden,
                  const std::filesystem::path& assets_path) {
    require(!std::filesystem::exists(output), "Output already exists; refusing to overwrite evidence");
    std::filesystem::create_directories(output);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Ribterra: presentation benchmark (close to stop)", 1920, 1080,
                         hidden ? SDL_WINDOW_HIDDEN : 0), SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    RendererOptions options;
    options.collect_telemetry = true;
    Renderer2D renderer(window.get(), assets_path, options);
    require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
            "Native presentation benchmark requires D3D12");
    const auto display = renderer.display_info();
    require(display.drawable.width == 1920 && display.drawable.height == 1080,
            "Benchmark drawable is not 1920x1080");
    renderer.set_logical_size(renderer.fitted_logical_size());
    check_invalid_requests(renderer);
    Frame2D contract;
    contract.logical_size = renderer.logical_size();
    for (const auto count : {1u, 2u, 3u}) {
        require(renderer.set_presentation({PresentMode::VSync, 0, count}), "Frame ownership configuration failed");
        for (int i = 0; i < 7; ++i) renderer.render(contract);
    }
    for (const auto mode : {PresentMode::Immediate, PresentMode::Mailbox}) {
        const bool supported = mode == PresentMode::Immediate ? renderer.presentation_status().immediate_supported :
            renderer.presentation_status().mailbox_supported;
        const auto previous = renderer.presentation_status().effective;
        if (!supported) {
            require(!renderer.set_presentation({mode, 120, 2}) &&
                    renderer.presentation_status().effective == previous &&
                    renderer.presentation_status().reason == PresentationUnavailableReason::UnsupportedMode,
                    "Unsupported mode silently changed effective configuration");
        }
    }
    AssetCatalog assets(assets_path);
    Localization localization(assets_path / "locales");
    GameUi ui(renderer, assets, localization, output / "probe-settings.json", renderer.logical_size());
    ui.show_world();
    GameView view(renderer);
    view.set_logical_size(renderer.logical_size());
    view.load_art(assets);
    ClientSession session(70421);
    SDL_Event center{};
    center.type = SDL_EVENT_KEY_DOWN;
    center.key.key = SDLK_C;
    view.handle_event(center, session);
    std::vector<Case> cases{{"vsync-hud", {PresentMode::VSync, 0, 2}}};
    if (renderer.presentation_status().immediate_supported) {
        cases.push_back({"immediate-hud", {PresentMode::Immediate, 0, 2}});
        cases.push_back({"immediate-hud-hidden", {PresentMode::Immediate, 0, 2}, false});
        cases.push_back({"immediate-120-hud", {PresentMode::Immediate, 120, 2}});
    }
    if (renderer.presentation_status().mailbox_supported)
        cases.push_back({"mailbox-120-hud", {PresentMode::Mailbox, 120, 2}});
    Json report{{"schema", 1}, {"build", RIBTERRA_PROBE_BUILD},
        {"fixture", "frozen ordinary legacy game, seed 70421, real GameView + RmlUi; not full Dynamic engine"},
        {"backend", renderer.device_info().backend}, {"device", renderer.device_info().device},
        {"driver", renderer.device_info().driver}, {"asset_content_hash", assets.content_hash()},
        {"drawable", {display.drawable.width, display.drawable.height}},
        {"refresh_hz", display.refresh_hz}, {"pixel_density", display.pixel_density},
        {"display_scale", display.display_scale}, {"hidden_window", hidden}, {"render_scale", 1.0},
        {"sample_seconds_per_case", seconds}, {"warmup_seconds_per_case", 0.75},
        {"GPU_timestamp_API_available", false}, {"displayed_fps_measured", false},
        {"Dynamic_available", renderer.lighting_status().dynamic_available},
        {"simulation_ticks_during_samples", 0}, {"cases", Json::array()}, {"complete", true}};
    for (const auto& test : cases) {
        auto result = benchmark_case(test, seconds, output, renderer, view, ui, session);
        const bool complete = result["complete"].get<bool>();
        report["cases"].push_back(std::move(result));
        if (!complete) { report["complete"] = false; break; }
    }
    // Preserve the measured cases even if a later lifetime/window contract fails.
    report["performance_cases_complete"] = report["complete"];
    report["probe_complete"] = false;
    {
        std::ofstream performance(output / "performance-report.json");
        performance << report.dump(2) << '\n';
        require(performance.good(), "Could not preserve performance report");
    }
    if (report["complete"].get<bool>() && !hidden) {
        require(SDL_MinimizeWindow(window.get()), SDL_GetError());
        SDL_Delay(50);
        SDL_PumpEvents();
        require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0,
                "Native minimize contract did not minimize the window");
        renderer.render(contract);
        check_telemetry(renderer.frame_telemetry());
        require(!renderer.frame_telemetry().submitted && !renderer.frame_telemetry().presentation_queued,
                "Minimized window did not skip unused GPU submission");
        renderer.render(contract, output / "minimized-offscreen.png");
        require(renderer.frame_telemetry().submitted && renderer.frame_telemetry().capture &&
                !renderer.frame_telemetry().presentation_queued,
                "Minimized offscreen capture did not preserve explicit GPU work");
        require(SDL_RestoreWindow(window.get()), SDL_GetError());
        SDL_Delay(50);
        SDL_PumpEvents();
        renderer.render(contract);
        require(renderer.frame_telemetry().submitted && renderer.frame_telemetry().presentation_queued,
                "Restored window failed to resume presentation");
        report["minimize_capture_restore_contract"] = true;
    }
    require(session.ticks() == 0, "Benchmark unexpectedly advanced simulation");
    report["probe_complete"] = report["complete"];
    std::ofstream file(output / "presentation-report.json");
    file << report.dump(2) << '\n';
    require(file.good(), "Could not write presentation report");
    require(report["complete"].get<bool>(), "Benchmark stopped early; partial report preserved");
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool software = false, hidden = false;
        unsigned seconds = 3;
        std::filesystem::path output;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--software-contract") software = true;
            else if (argument == "--hidden") hidden = true;
            else if (argument == "--seconds" && i + 1 < argc) {
                const std::string_view value(argv[++i]);
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
                require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                        seconds >= 1 && seconds <= 30, "Seconds must be in [1, 30]");
            } else {
                require(output.empty(), "Unknown argument or duplicate output path");
                output = path(argument);
            }
        }
        require(software || !output.empty(), "Usage: PresentationProbe <new-output-directory> [--seconds N] [--hidden]");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        const auto assets = path(SDL_GetBasePath()) / "assets";
        if (software) software_contract(assets); else native_probe(output, seconds, hidden, assets);
        SDL_Quit();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        SDL_Quit();
        return 1;
    }
}

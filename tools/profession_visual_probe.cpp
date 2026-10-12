#include "ribterra/asset_catalog.hpp"
#include "ribterra/alpha_rules.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace ribterra;
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void load_wood_fixture(ClientSession &session, const std::filesystem::path &save) {
    constexpr CellCoord target{22, 19};
    World fixture(70421, 10);
    auto state = fixture.export_state();
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    require(chunk != state.chunks.end(), "Wood fixture chunk is missing");
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    require(fixture.import_state(std::move(state), error), "Wood fixture import failed: " + error);
    require(fixture.save(save, error), "Wood fixture save failed: " + error);
    require(session.load(save, error), "Wood fixture client load failed: " + error);
    std::filesystem::remove(save);
}
std::filesystem::path path(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
const ResidentProfessionView &profile(const ClientSession &session, int id) {
    const auto &views = session.resident_professions();
    const auto found =
        std::find_if(views.begin(), views.end(), [id](const auto &item) { return item.resident_id == id; });
    require(found != views.end(), "Resident profession projection missing");
    return *found;
}
template <class Predicate>
void until(ClientSession &session, Predicate ready, const char *message, int limit = 2400) {
    for (int i = 0; i < limit && !ready(); ++i)
        session.advance();
    require(ready(), std::string(message) + " at tick " + std::to_string(session.ticks()));
}
void same(const ClientSession &a, const ClientSession &b) {
    require(a.ticks() == b.ticks() && a.residents() == b.residents() && a.buildings() == b.buildings() &&
                a.inventory() == b.inventory() && a.resident_professions() == b.resident_professions() &&
                a.resident_logistics() == b.resident_logistics() && a.resident_work() == b.resident_work() &&
                a.construction_sites() == b.construction_sites(),
            "Profession, XP, route, cargo or construction projection changed across save/resume");
}
void resume(ClientSession &source, const std::filesystem::path &file) {
    std::string error;
    require(source.save(file, error), "Scene save failed: " + error);
    ClientSession restored(1, "Profession resume probe");
    require(restored.load(file, error), "Scene load failed: " + error);
    same(source, restored);
    require(restored.chunks() == source.chunks(), "Saving the profession scene changed its terrain");
    // Compare the still-live authoritative session against decoded state, so
    // losing a hidden scheduler/cargo continuation is observable on later ticks.
    for (int i = 0; i < 128; ++i) {
        source.advance();
        restored.advance();
        same(source, restored);
    }
    require(source.chunks() == restored.chunks(), "Resumed profession terrain diverged");
}
std::string percent(Localization &localization, int bp) {
    return (bp > 0 ? "+" : "") + localization.format("hud.number", {{"count", bp / 100.0}});
}
std::string breakdown(Localization &localization, const ModifierBreakdown &value) {
    return localization.format("profession.breakdown",
                               {{"skill", percent(localization, value.skill_sum_bp)},
                                {"ability", percent(localization, value.ability_sum_bp)},
                                {"practice", percent(localization, value.familiarity_bp)},
                                {"total", percent(localization, value.uncapped_bp)},
                                {"applied", percent(localization, value.applied_bp)},
                                {"cap", value.capped ? localization.text("profession.cap") : std::string{}}});
}
void key(GameUi &ui, SDL_Keycode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    ui.process_event(event);
    event.type = SDL_EVENT_KEY_UP;
    ui.process_event(event);
}
void motion(GameUi &ui, render::Point position) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = position.x;
    event.motion.y = position.y;
    ui.process_event(event);
}
void click(GameUi &ui, const char *id) {
    const auto center = ui.element_center_for_test(id);
    motion(ui, center);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = center.x;
    event.button.y = center.y;
    require(ui.process_event(event), std::string(id) + " did not consume pointer press");
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(ui.process_event(event), std::string(id) + " did not consume pointer release");
}
UiAction action(GameUi &ui, UiActionType expected) {
    auto actions = ui.take_actions();
    std::string actual = "expected=" + std::to_string(static_cast<int>(expected)) +
                         " count=" + std::to_string(actions.size());
    for (const auto &item : actions)
        actual += " type=" + std::to_string(static_cast<int>(item.type));
    require(actions.size() == 1 && actions.front().type == expected,
            "Profession pointer control did not emit exactly one expected action (" + actual + ")");
    return std::move(actions.front());
}
struct Hud {
    GameUi &ui;
    GameView &view;
    render::Renderer2D &renderer;
    Localization &localization;
    ClientSession &session;
    std::vector<int> selected;
    std::filesystem::path diagnostics;

    render::Frame2D frame() {
        ui.set_view_state(&session, selected, view.paused(), view.speed());
        ui.update(session);
        render::Frame2D result;
        view.compose(session, 0, result, false);
        ui.compose(result);
        return result;
    }
    void open(int resident) {
        selected = {resident};
        frame();
        click(ui, "selection-details");
        frame();
        require(ui.activate_for_test("profession-open"), "Profession details action unavailable");
        frame();
    }
    void scroll(bool bottom) {
        motion(ui, ui.element_center_for_test("profession-form"));
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.y = bottom ? -100.F : 100.F;
        require(ui.process_event(event), "Profession form did not consume wheel scrolling");
        float previous = ui.element_center_for_test("profession-skill-0").y;
        int stable = 0;
        for (int i = 0; i < 120 && stable < 4; ++i) {
            SDL_Delay(16); // Let RmlUi's real smooth scrolling settle while simulation remains paused.
            frame();
            const float current = ui.element_center_for_test("profession-skill-0").y;
            stable = i >= 4 && std::abs(current - previous) < .1F ? stable + 1 : 0;
            previous = current;
        }
        require(stable == 4, "Profession form scrolling did not settle");
    }
    void choose(const char *id, int index, int count) {
        require(ui.focus_for_test(id), std::string(id) + " rejected keyboard focus");
        key(ui, SDLK_RETURN);
        frame();
        for (int i = 0; i < count; ++i)
            key(ui, SDLK_UP);
        for (int i = 0; i < index; ++i)
            key(ui, SDLK_DOWN);
        key(ui, SDLK_RETURN);
        frame();
    }
    void profession(Profession value) {
        scroll(false);
        choose("profession-select", static_cast<int>(value), 6);
    }
    void apply(Profession expected, bool verify_frozen = false) {
        click(ui, "profession-apply");
        const auto command = action(ui, UiActionType::AssignProfession);
        require(command.residents == selected && command.assignment.profession == expected &&
                    command.assignment.automation_enabled && !command.assignment.worksite,
                "Keyboard profession form did not produce the expected automatic assignment");
        const auto original = selected;
        int other = 0;
        if (verify_frozen) {
            for (const auto &resident : session.residents())
                if (resident.id != original.front()) {
                    other = resident.id;
                    break;
                }
            require(other != 0, "Frozen-selection scenario needs two residents");
            selected = {other};
            frame();
        }
        view.cancel_worksite();
        require(session.assign_profession(command.residents, command.assignment).accepted,
                "Server did not queue pointer Apply");
        session.advance();
        require(profile(session, original.front()).assignment == command.assignment,
                "Authoritative assignment differs from the frozen UI command");
        if (other)
            require(profile(session, other).assignment.profession == Profession::Unassigned,
                    "Pointer Apply followed a later selection");
        selected = original;
        frame();
    }
    CellCoord choose_mine_worksite(int other) {
        profession(Profession::Miner);
        click(ui, "profession-worksite");
        const auto command = action(ui, UiActionType::ChooseWorksite);
        require(command.assignment.profession == Profession::Miner && command.residents == selected,
                "Map worksite action lost its draft or selected resident IDs");
        view.arm_worksite(command.assignment, command.residents);
        const auto original = selected;
        selected = {other};
        frame();
        std::optional<CellCoord> target;
        for (int y = 16; y <= 31 && !target; ++y)
            for (int x = 16; x <= 31 && !target; ++x) {
                const auto &tile = session.tile(x, y);
                const auto point = view.tile_screen(static_cast<float>(x), static_cast<float>(y));
                if (tile.explored && tile.terrain == Terrain::Rock && tile.amount > 0 && point.x > 350 &&
                    point.x < 1000 && point.y > 150 && point.y < 660)
                    target = CellCoord{x, y};
            }
        require(target.has_value(), "No visible Rock worksite near the settlement");
        const auto center = view.tile_screen(static_cast<float>(target->x), static_cast<float>(target->y));
        motion(ui, {center.x, center.y});
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = center.x;
        event.button.y = center.y;
        require(!ui.process_event(event), "Visible resource map click was intercepted by the HUD");
        view.handle_event(event, session);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        if (!ui.process_event(event))
            view.handle_event(event, session);
        require(!view.worksite_armed(), "Accepted worksite stayed armed");
        session.advance();
        require(profile(session, original.front()).assignment.profession == Profession::Miner &&
                    profile(session, original.front()).assignment.worksite == target &&
                    profile(session, other).assignment.profession == Profession::Unassigned,
                "Map worksite command did not preserve its frozen resident selection");
        open(original.front());
        return *target;
    }
    void stop() {
        view.set_paused(false);
        open(selected.front());
        scroll(false);
        require(ui.activate_for_test("profession-stop"), "Profession Stop action unavailable");
        const auto command = action(ui, UiActionType::Stop);
        view.cancel_worksite();
        require(command.residents == selected && session.stop(command.residents).accepted,
                "Pointer Stop did not queue the correct resident IDs");
        session.advance();
        require(!profile(session, selected.front()).assignment.automation_enabled,
                "Server Stop did not disable automatic work");
        until(
            session, [&] { return !profile(session, selected.front()).deferred; },
            "Stopped worker never finished the accepted delivery/path boundary");
        view.set_paused(true);
        frame();
    }
    void inspect(std::size_t skill, bool keyboard) {
        const auto paused_tick = session.ticks();
        view.set_paused(true);
        frame();
        scroll(true);
        if (keyboard) {
            require(skill > 0 && ui.focus_for_test("profession-skill-" + std::to_string(skill - 1)),
                    "Skill detail did not accept keyboard focus");
            key(ui, SDLK_TAB);
        } else
            motion(ui, ui.element_center_for_test("profession-skill-" + std::to_string(skill)));
        const auto focus_before = ui.focused_element_for_test();
        const auto actual_before = ui.element_rml_for_test("profession-breakdown");
        const auto expected =
            breakdown(localization, profile(session, selected.front()).skills[skill].modifier);
        const auto rendered = frame();
        const auto focus_after = ui.focused_element_for_test();
        const auto actual_after = ui.element_rml_for_test("profession-breakdown");
        const auto expected_focus = "profession-skill-" + std::to_string(skill);
        if (actual_before.find(expected) == std::string::npos ||
            actual_after.find(expected) == std::string::npos ||
            (keyboard && (focus_before != expected_focus || focus_after != expected_focus))) {
            const auto center = ui.element_center_for_test(expected_focus);
            const nlohmann::json detail{{"input", keyboard ? "keyboard" : "pointer"},
                                        {"locale", localization.locale()},
                                        {"resident", selected.front()},
                                        {"skill", skill},
                                        {"tick", session.ticks()},
                                        {"expected", expected},
                                        {"before_frame", actual_before},
                                        {"after_frame", actual_after},
                                        {"focus_before", focus_before},
                                        {"focus_after", focus_after},
                                        {"skill_center", {center.x, center.y}}};
            std::ofstream(diagnostics / (localization.locale() + "-skill-failure.json")) << detail.dump(2);
            renderer.render(rendered, diagnostics / (localization.locale() + "-skill-failure.png"));
            throw std::runtime_error("Paused skill inspection mismatch: " + detail.dump());
        }
        verify(skill);
        require(session.ticks() == paused_tick && ui.take_actions().empty(),
                "Paused skill inspection advanced simulation or emitted gameplay commands");
    }
    void verify(std::size_t skill) {
        const auto &value = profile(session, selected.front());
        const auto state =
            localization.text("profession.name." +
                              std::to_string(static_cast<int>(value.assignment.profession))) +
            " · " +
            localization.text("profession.wait." + std::to_string(static_cast<int>(value.wait_reason)));
        require(ui.element_rml_for_test("profession-state").find(state) != std::string::npos,
                "Profession/wait text differs from the localized server projection");
        for (std::size_t i = 0; i < value.skills.size(); ++i) {
            if (!alpha::active_skill(i)) {
                require(!ui.visible_for_test("profession-skill-" + std::to_string(i)),
                        "Inactive alpha skill is visible");
                continue;
            }
            const auto expected =
                localization.format("profession.skill_row",
                                    {{"name", localization.text("profession.skill." + std::to_string(i))},
                                     {"xp", value.skills[i].experience},
                                     {"effect", percent(localization, value.skills[i].modifier.applied_bp)}});
            require(ui.element_rml_for_test("profession-skill-" + std::to_string(i)).find(expected) !=
                        std::string::npos,
                    "Localized skill XP/modifier row differs from the authoritative projection");
        }
        const auto actual = ui.element_rml_for_test("profession-breakdown");
        const auto expected = breakdown(localization, value.skills[skill].modifier);
        require(actual.find(expected) != std::string::npos, "Skill breakdown differs: expected=[" + expected +
                                                                "]; actual=[" + actual +
                                                                "]; focus=" + ui.focused_element_for_test());
    }
    void capture(const std::filesystem::path &file) { renderer.render(frame(), file); }
};
std::filesystem::path carry_fixture(const std::filesystem::path &base) {
    for (const auto &root : {base, std::filesystem::current_path(), base / "../.."}) {
        const auto fixture = root / "tests/fixtures/world_save_v6_carry.ribsave";
        if (std::filesystem::is_regular_file(fixture))
            return std::filesystem::weakly_canonical(fixture);
    }
    throw std::runtime_error(
        "Cannot find tests/fixtures/world_save_v6_carry.ribsave beside the probe or in the repository");
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = utf8_arguments(argc, argv);
        require(args.size() == 2 || (args.size() == 3 && args[2] == "--software"),
                "Usage: ProfessionVisualProbe output-directory [--software]");
        const auto output = std::filesystem::absolute(path(args[1]));
        std::filesystem::create_directories(output);
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        nlohmann::json report = {{"locales", nlohmann::json::array()}, {"resume_ticks_per_scene", 128}};
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra professions probe", 1440, 900,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
                SDL_DestroyWindow);
            require(window != nullptr, "Window creation failed");
            const auto base = path(SDL_GetBasePath());
            render::Renderer2D renderer(
                window.get(), base / "assets",
                {args.size() == 3 ? render::Backend::Software : render::Backend::Direct3D12});
            const auto &device = renderer.device_info();
            std::ofstream(output / "device.json") << nlohmann::json{
                {"backend", device.backend},
                {"device", device.device},
                {"driver", device.driver},
                {"software", device.software}}.dump(2);
            AssetCatalog assets(base / "assets");
            for (const auto &locale : Localization::supported_locales()) {
                const auto settings = output / (locale + "-settings.json");
                std::ofstream(settings) << nlohmann::json{{"locale", locale}, {"ui_scale", 1.15}};
                Localization localization(base / "assets/locales");
                GameUi ui(renderer, assets, localization, settings);
                GameView view(renderer);
                ClientSession session(70421, "Profession visual probe");
                load_wood_fixture(session, output / (locale + "-wood-fixture.save"));
                const int resident = session.residents().at(0).id, other = session.residents().at(1).id;
                const auto initial = profile(session, resident).skills;
                ui.show_world();
                Hud hud{ui, view, renderer, localization, session, {resident}, output};
                hud.open(resident);
                hud.profession(Profession::Woodcutter);
                hud.choose("priority-0", 5, 6);
                hud.apply(Profession::Woodcutter, true);
                require(profile(session, resident).assignment.priorities[0] == 5,
                        "Keyboard priority was not applied by the server");
                require(session.gather({resident},22,19,WorldLayer::Surface,
                                       ManualQueueMode::Replace,GatherCompletion::OneDelivery).accepted,
                        "Explicit wood order rejected");
                require(session.gather({resident},22,19,WorldLayer::Surface,
                                       ManualQueueMode::Append,GatherCompletion::OneDelivery).accepted,
                        "Second finite wood order rejected");
                until(
                    session,
                    [&] {
                        return profile(session, resident).skills[0].experience >= initial[0].experience + 2;
                    },
                    "Explicit Woodcutter orders did not earn XP through useful work");
                const int wood_xp = profile(session, resident).skills[0].experience;
                hud.open(resident);
                view.set_paused(true);
                hud.scroll(false);
                hud.capture(output / (locale + "-woodcutter-assignment.png"));
                hud.inspect(0, false);
                hud.capture(output / (locale + "-woodcutter-skills.png"));
                resume(session, output / (locale + "-woodcutter.save"));

                view.set_paused(false);
                const auto site = hud.choose_mine_worksite(other);
                require(session.gather({resident},site.x,site.y,site.layer,
                                       ManualQueueMode::Replace,GatherCompletion::OneDelivery).accepted,
                        "Explicit mining order rejected");
                until(
                    session,
                    [&] { return profile(session, resident).skills[1].experience > initial[1].experience; },
                    "Map-assigned Miner did not earn XP through useful work");
                require(profile(session, resident).skills[0].experience >= wood_xp,
                        "Changing profession erased permanent Woodcutting XP");
                require(breakdown(localization, profile(session, resident).skills[0].modifier) !=
                            breakdown(localization, profile(session, resident).skills[1].modifier),
                        "Keyboard focus needs distinct earned Woodcutting and Mining modifiers");
                hud.inspect(1, true);
                hud.capture(output / (locale + "-miner-skills.png"));
                resume(session, output / (locale + "-miner.save"));
                if (locale == "uk") {
                    require(SDL_SetWindowSize(window.get(), 1100, 700), "Compact resize failed");
                    SDL_PumpEvents();
                    hud.capture(output / "uk-compact-miner-skills.png");
                    require(SDL_SetWindowSize(window.get(), 1440, 900), "Viewport restore failed");
                    SDL_PumpEvents();
                    hud.frame();
                }
                const int mine_xp = profile(session, resident).skills[1].experience;
                hud.stop();
                require(profile(session, resident).skills[0].experience >= wood_xp &&
                            profile(session, resident).skills[1].experience >= mine_xp,
                        "Stop erased accumulated skill experience");
                hud.capture(output / (locale + "-stopped.png"));
                resume(session, output / (locale + "-stopped.save"));
                report["locales"].push_back(
                    {{"locale", locale},
                     {"resident", resident},
                     {"woodcutting_xp", wood_xp},
                     {"mining_xp", mine_xp},
                     {"mine_worksite", {site.x, site.y}},
                     {"frozen_apply_and_map_selection", true},
                     {"pointer_hover_while_paused", true},
                     {"tab_focus_while_paused", true},
                     {"all_eight_skill_rows_verified", true},
                     {"server_stop", true},
                     {"localized_breakdown", ui.element_rml_for_test("profession-breakdown")}});

                if (locale == "uk") {
                    // Loading migrates its input. Always copy the immutable genuine
                    // 0.8 file and leave the tracked compatibility fixture untouched.
                    const auto source = output / "deferred-carry-source.save";
                    std::filesystem::copy_file(carry_fixture(base), source,
                                               std::filesystem::copy_options::overwrite_existing);
                    ClientSession carrying(1, "Profession cargo probe");
                    std::string error;
                    require(carrying.load(source, error), "Genuine v6 carry migration failed: " + error);
                    const auto found = std::find_if(carrying.resident_logistics().begin(),
                                                    carrying.resident_logistics().end(),
                                                    [](const auto &value) { return value.quantity > 0; });
                    require(found != carrying.resident_logistics().end(),
                            "Genuine v6 scene has no carried cargo");
                    const int carrier = found->resident_id;
                    const auto cargo_quantity = found->quantity;
                    const auto cargo_item = found->item;
                    Hud cargo_hud{ui, view, renderer, localization, carrying, {carrier}, output};
                    cargo_hud.open(carrier);
                    cargo_hud.profession(Profession::Miner);
                    view.set_paused(false);
                    cargo_hud.apply(Profession::Miner);
                    const auto &assigned = profile(carrying, carrier);
                    const auto after = std::find_if(
                        carrying.resident_logistics().begin(), carrying.resident_logistics().end(),
                        [carrier](const auto &value) { return value.resident_id == carrier; });
                    require(
                        assigned.deferred && assigned.wait_reason == ProfessionWaitReason::DeliveryBoundary &&
                            after != carrying.resident_logistics().end() &&
                            after->quantity == cargo_quantity && after->item == cargo_item,
                        "Profession change did not preserve and defer the current physical cargo delivery");
                    cargo_hud.capture(output / "uk-deferred-carry-assignment.png");
                    cargo_hud.inspect(1, true);
                    cargo_hud.capture(output / "uk-deferred-carry-skills.png");
                    resume(carrying, output / "uk-deferred-carry.save");
                    view.set_paused(false);
                    until(
                        carrying, [&] { return !profile(carrying, carrier).deferred; },
                        "Deferred profession never resumed after its cargo delivery");
                    require(profile(carrying, carrier).assignment.profession == Profession::Miner,
                            "The requested profession was lost at the delivery boundary");
                    report["deferred_carry"] = {{"genuine_v6_fixture_copied", true},
                                                {"resident", carrier},
                                                {"cargo_preserved_on_assignment", true},
                                                {"resumed_after_delivery_tick", carrying.ticks()}};
                }
            }
        }
        SDL_Quit();
        std::ofstream(output / "results.json") << report.dump(2);
        std::cout << "Five-locale profession pointer/keyboard controls, physical work XP, paused skill "
                     "details, Stop, compact HUD and genuine v6 deferred cargo save/resume passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

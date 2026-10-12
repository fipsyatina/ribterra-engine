#include "ribterra/asset_catalog.hpp"
#include "ribterra/alpha_rules.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world_state.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace ribterra;
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
void load_wood_fixture(ClientSession &session, const std::filesystem::path &save) {
    constexpr CellCoord target{22, 19};
    World fixture(70421, 10);
    auto state = fixture.export_state();
    const auto chunk = std::find_if(state.chunks.begin(), state.chunks.end(),
                                    [&](const auto &candidate) { return candidate.key == chunk_key(target); });
    check(chunk != state.chunks.end(), "Wood fixture chunk is missing");
    auto &resource = chunk->tiles[local_index(target.x, target.y)];
    resource.terrain = Terrain::Forest;
    resource.amount = 64;
    resource.biome = Biome::Forest;
    resource.explored = true;
    ++state.topology_revision;
    std::string error;
    check(fixture.import_state(std::move(state), error), "Wood fixture import failed: " + error);
    check(fixture.save(save, error), "Wood fixture save failed: " + error);
    check(session.load(save, error), "Wood fixture client load failed: " + error);
    std::filesystem::remove(save);
}
const ResidentProfessionView &profile(const ClientSession &session, int id) {
    const auto &views = session.resident_professions();
    const auto found =
        std::find_if(views.begin(), views.end(), [id](const auto &view) { return view.resident_id == id; });
    check(found != views.end(), "Authoritative profession projection missing");
    return *found;
}
void motion(GameUi &ui, render::Point point) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = point.x;
    event.motion.y = point.y;
    ui.process_event(event);
}
void click(GameUi &ui, const char *id) {
    check(ui.focus_for_test(id), std::string(id) + " did not accept focus");
    render::Frame2D focused;
    ui.compose(focused);
    const auto center = ui.element_center_for_test(id);
    motion(ui, center);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = center.x;
    event.button.y = center.y;
    check(ui.process_event(event), std::string(id) + " press reached the world");
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    check(ui.process_event(event), std::string(id) + " release reached the world");
}
void key(GameUi &ui, SDL_Keycode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    ui.process_event(event);
    event.type = SDL_EVENT_KEY_UP;
    ui.process_event(event);
}
void layout(GameUi &ui, GameView &view, ClientSession &session, const std::vector<int> &ids) {
    ui.set_view_state(&session, ids, view.paused(), view.speed());
    ui.update(session);
    render::Frame2D frame;
    view.compose(session, 0, frame, false);
    ui.compose(frame);
    check(!frame.commands.empty(), "Profession HUD produced no render commands");
}
void select_option(GameUi &ui, const char *id, int index, int count) {
    check(ui.focus_for_test(id), std::string(id) + " did not accept keyboard focus");
    key(ui, SDLK_RETURN); // Open real RmlUi options so arrow navigation sees them.
    render::Frame2D frame;
    ui.compose(frame);
    for (int i = 0; i < count; ++i)
        key(ui, SDLK_UP);
    for (int i = 0; i < index; ++i)
        key(ui, SDLK_DOWN);
    key(ui, SDLK_RETURN);
    frame.clear_commands();
    ui.compose(frame);
}
UiAction action(GameUi &ui, UiActionType type) {
    auto actions = ui.take_actions();
    check(actions.size() == 1 && actions.front().type == type,
          "Profession control did not emit exactly one correctly typed action");
    return std::move(actions.front());
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
void scroll_form(GameUi &ui, GameView &view, ClientSession &session, const std::vector<int> &ids,
                 bool bottom) {
    motion(ui, ui.element_center_for_test("profession-form"));
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = bottom ? -100.F : 100.F;
    check(ui.process_event(event), "Profession form did not consume wheel scrolling");
    // RmlUi uses wall-clock smooth scrolling; observe stable geometry instead of
    // moving the simulation or changing the element's private scroll position.
    float previous = ui.element_center_for_test("profession-skill-0").y;
    int stable = 0;
    for (int i = 0; i < 120 && stable < 4; ++i) {
        SDL_Delay(16);
        layout(ui, view, session, ids);
        const float current = ui.element_center_for_test("profession-skill-0").y;
        stable = i >= 4 && std::abs(current - previous) < .1F ? stable + 1 : 0;
        previous = current;
    }
    check(stable == 4, "Profession form scrolling did not settle");
}
CellCoord rock_site(const ClientSession &session, const GameView &view) {
    for (int y = 16; y <= 31; ++y)
        for (int x = 16; x <= 31; ++x) {
            const auto &tile = session.tile(x, y);
            const auto point = view.tile_screen(static_cast<float>(x), static_cast<float>(y));
            if (tile.explored && tile.terrain == Terrain::Rock && tile.amount > 0 && point.x > 350 &&
                point.x < 1000 && point.y > 150 && point.y < 660)
                return {x, y};
        }
    throw std::runtime_error("No visible Rock worksite near the starting settlement");
}
} // namespace

int main() {
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        {
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("Ribterra professions UI tests", 1440, 900, SDL_WINDOW_HIDDEN),
                SDL_DestroyWindow);
            check(window != nullptr, "Window creation failed");
            const auto base =
                std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
            const auto settings = std::filesystem::current_path() / "profession-ui-test-settings.json";
            std::ofstream(settings) << "{\"locale\":\"uk\",\"ui_scale\":1.15}\n";
            AssetCatalog assets(base / "assets");
            Localization localization(base / "assets/locales");
            render::Renderer2D renderer(window.get(), base / "assets", {render::Backend::Software});
            GameUi ui(renderer, assets, localization, settings);
            GameView view(renderer);
            ClientSession session;
            load_wood_fixture(session, std::filesystem::current_path() / "profession-ui-wood-fixture.save");
            const int first = session.residents().at(0).id, second = session.residents().at(1).id;
            ui.show_world();
            layout(ui, view, session, {first});
            click(ui, "action-assign");
            layout(ui, view, session, {first});
            check(ui.element_rml_for_test("profession-person").find(session.residents().front().name) !=
                      std::string::npos,
                  "Profession panel identifies the wrong resident");
            // RmlUi select options are non-DOM widget children and intentionally
            // absent from GetInnerRML. Verify the offered choices by navigating
            // the actual widget and reading its public typed actions instead.
            for (int supported = 0; supported <= static_cast<int>(Profession::Farmer); ++supported) {
                select_option(ui, "profession-select", supported, 7);
                click(ui, "profession-apply");
                const auto offered = action(ui, UiActionType::AssignProfession);
                check(static_cast<int>(offered.assignment.profession) == supported,
                      "Profession selector skipped supported keyboard option " + std::to_string(supported) +
                          "; actual=" + std::to_string(static_cast<int>(offered.assignment.profession)));
            }
            select_option(ui, "profession-select", 10, 7);
            click(ui, "profession-apply");
            const auto last_option = action(ui, UiActionType::AssignProfession);
            check(last_option.assignment.profession == Profession::Farmer,
                  "Profession selector offered an unimplemented option after Farmer; actual=" +
                      std::to_string(static_cast<int>(last_option.assignment.profession)));
            select_option(ui, "profession-select", static_cast<int>(Profession::Woodcutter), 7);
            select_option(ui, "priority-0", 5, 6);
            // A normal snapshot refresh must keep an unsent form draft intact.
            layout(ui, view, session, {first});
            click(ui, "profession-apply");
            const auto assignment = action(ui, UiActionType::AssignProfession);
            check(assignment.residents == std::vector<int>{first} &&
                      assignment.assignment.profession == Profession::Woodcutter &&
                      assignment.assignment.priorities[0] == 5 && assignment.assignment.automation_enabled &&
                      !assignment.assignment.worksite,
                  "Keyboard-edited profession or priority did not survive a snapshot refresh");
            layout(ui, view, session, {second});
            check(session.assign_profession(assignment.residents, assignment.assignment).accepted,
                  "Server did not queue the emitted profession command");
            session.advance();
            check(profile(session, first).assignment == assignment.assignment &&
                      profile(session, second).assignment.profession == Profession::Unassigned,
                  "Queued Apply followed the later UI selection instead of its frozen resident IDs");
            check(session.gather({first}, 22, 19).accepted, "Explicit skill-training order rejected");
            // Distinct, naturally earned modifiers make the later hover/focus
            // assertions sensitive to a stale breakdown instead of comparing zeros.
            for (int tick = 0; tick < 2400 && profile(session, first).skills[0].experience == 0; ++tick)
                session.advance();
            check(profile(session, first).skills[0].experience > 0,
                  "Explicit Woodcutter order did not produce observable skill experience");

            layout(ui, view, session, {first});
            select_option(ui, "profession-select", static_cast<int>(Profession::Miner), 7);
            click(ui, "profession-worksite");
            const auto worksite = action(ui, UiActionType::ChooseWorksite);
            check(worksite.residents == std::vector<int>{first} &&
                      worksite.assignment.profession == Profession::Miner,
                  "Worksite mode lost its profession draft or resident IDs");
            view.arm_worksite(worksite.assignment, worksite.residents);
            layout(ui, view, session, {second});
            const auto site = rock_site(session, view);
            const auto point = view.tile_screen(static_cast<float>(site.x), static_cast<float>(site.y));
            motion(ui, {point.x, point.y});
            SDL_Event map{};
            map.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            map.button.button = SDL_BUTTON_LEFT;
            map.button.x = point.x;
            map.button.y = point.y;
            check(!ui.process_event(map), "Chosen visible worksite was covered by a UI input region");
            view.handle_event(map, session);
            map.type = SDL_EVENT_MOUSE_BUTTON_UP;
            if (!ui.process_event(map))
                view.handle_event(map, session);
            check(!view.worksite_armed(), "Accepted map worksite did not leave placement mode");
            session.advance();
            check(profile(session, first).assignment.profession == Profession::Miner &&
                      profile(session, first).assignment.worksite == site &&
                      profile(session, second).assignment.profession == Profession::Unassigned,
                  "Map placement did not preserve the armed resident IDs and exact worksite");

            layout(ui, view, session, {first});
            click(ui, "action-assign");
            layout(ui, view, session, {first});
            const auto before_stop = profile(session, first).skills;
            click(ui, "profession-stop");
            const auto stop = action(ui, UiActionType::Stop);
            check(stop.residents == std::vector<int>{first} && session.stop(stop.residents).accepted,
                  "Pointer Stop did not send the selected resident's server command");
            session.advance();
            check(!profile(session, first).assignment.automation_enabled,
                  "Stop failed to disable automatic work");
            for (std::size_t i = 0; i < before_stop.size(); ++i)
                check(profile(session, first).skills[i].experience >= before_stop[i].experience,
                      "Stop erased permanent skill experience");

            view.set_paused(true);
            layout(ui, view, session, {first});
            const auto paused_tick = session.ticks();
            scroll_form(ui, view, session, {first}, true);
            const auto &skills = profile(session, first).skills;
            check(breakdown(localization, skills[0].modifier) != breakdown(localization, skills[1].modifier),
                  "Hover/focus scenario must have observably different Woodcutting and Mining modifiers");
            for (std::size_t i = 0; i < skills.size(); ++i) {
                if (!alpha::active_skill(i))
                    continue;
                const auto expected =
                    localization.format("profession.skill_row",
                                        {{"name", localization.text("profession.skill." + std::to_string(i))},
                                         {"xp", skills[i].experience},
                                         {"effect", percent(localization, skills[i].modifier.applied_bp)}});
                check(ui.element_rml_for_test("profession-skill-" + std::to_string(i)).find(expected) !=
                          std::string::npos,
                      "Skill row differs from the authoritative localized XP/modifier projection");
            }
            motion(ui, ui.element_center_for_test("profession-skill-0"));
            const auto hover_actual = ui.element_rml_for_test("profession-breakdown");
            const auto hover_expected = breakdown(localization, skills[0].modifier);
            check(hover_actual.find(hover_expected) != std::string::npos,
                  "Pointer hover breakdown mismatch: expected=[" + hover_expected + "]; actual=[" +
                      hover_actual + "]; focus=" + ui.focused_element_for_test());
            check(ui.focus_for_test("profession-skill-0"), "Skill button must accept keyboard focus");
            key(ui, SDLK_TAB);
            const auto focus_before = ui.focused_element_for_test();
            const auto breakdown_before = ui.element_rml_for_test("profession-breakdown");
            layout(ui, view, session, {first});
            const auto focus_after = ui.focused_element_for_test();
            const auto breakdown_after = ui.element_rml_for_test("profession-breakdown");
            const auto focus_expected = breakdown(localization, skills[1].modifier);
            check(breakdown_before.find(focus_expected) != std::string::npos &&
                      breakdown_after.find(focus_expected) != std::string::npos &&
                      focus_before == "profession-skill-1" && focus_after == "profession-skill-1",
                  "Paused Tab breakdown mismatch: expected=[" + focus_expected + "]; before=[" +
                      breakdown_before + "]; after=[" + breakdown_after + "]; focus before=" + focus_before +
                      "; focus after=" + focus_after);
            check(session.ticks() == paused_tick && ui.take_actions().empty(),
                  "Skill inspection advanced the world or emitted a gameplay command");

            // Application routing cancels an older map draft before a newer
            // profession/Stop command; exercise that contract through real buttons.
            scroll_form(ui, view, session, {first}, false);
            view.arm_worksite(worksite.assignment, {first});
            select_option(ui, "profession-select", static_cast<int>(Profession::Forager), 7);
            click(ui, "profession-apply");
            const auto newer_assignment = action(ui, UiActionType::AssignProfession);
            view.cancel_worksite();
            check(
                session.assign_profession(newer_assignment.residents, newer_assignment.assignment).accepted &&
                    !view.worksite_armed(),
                "Newer Apply did not retire the previous worksite draft");
            view.arm_worksite(worksite.assignment, {first});
            click(ui, "profession-stop");
            const auto newer_stop = action(ui, UiActionType::Stop);
            view.cancel_worksite();
            check(session.stop(newer_stop.residents).accepted && !view.worksite_armed(),
                  "Newer Stop did not retire the previous worksite draft");

            struct KeyboardButton {
                const char *id;
                SDL_Keycode key;
                UiActionType action;
            };
            for (const auto &button :
                 std::array{KeyboardButton{"profession-apply", SDLK_RETURN, UiActionType::AssignProfession},
                            KeyboardButton{"profession-apply", SDLK_KP_ENTER, UiActionType::AssignProfession},
                            KeyboardButton{"profession-stop", SDLK_SPACE, UiActionType::Stop}}) {
                check(ui.focus_for_test(button.id), "Profession action button rejected keyboard focus");
                const bool paused_before = view.paused();
                SDL_Event activate{};
                activate.type = SDL_EVENT_KEY_DOWN;
                activate.key.key = button.key;
                check(ui.process_event(activate), "Keyboard button activation reached the world");
                const auto emitted = action(ui, button.action);
                check(emitted.residents == std::vector<int>{first},
                      "Keyboard button activation lost the selected resident IDs");
                activate.key.repeat = true;
                for (int repeat = 0; repeat < 3; ++repeat) {
                    check(ui.process_event(activate), "Repeated keyboard activation reached the world");
                    check(ui.take_actions().empty(),
                          "Held activation key emitted a duplicate profession action");
                }
                activate.type = SDL_EVENT_KEY_UP;
                ui.process_event(activate);
                check(ui.take_actions().empty() && view.paused() == paused_before,
                      "Button key release emitted an action or changed world pause");
            }

            // Leave both an unsent form and a queued UI action behind. Reusing
            // resident ID 1 in another world must not resurrect either one.
            select_option(ui, "profession-select", static_cast<int>(Profession::Woodcutter), 7);
            select_option(ui, "priority-0", 5, 6);
            click(ui, "profession-apply");
            view.arm_worksite(worksite.assignment, {first});
            SDL_Event select_all{};
            select_all.type = SDL_EVENT_KEY_DOWN;
            select_all.key.key = SDLK_A;
            view.handle_event(select_all, session);
            check(!view.selected_ids().empty() && view.worksite_armed(),
                  "World reset scenario must contain an actual selection and armed worksite");
            ui.reset_world_state();
            view.reset_world_state();
            check(ui.take_actions().empty() && view.selected_ids().empty() && !view.worksite_armed(),
                  "World reset retained actions, selected residents or a worksite draft");
            ClientSession fresh;
            check(fresh.residents().front().id == first, "Reset regression requires a reused resident ID");
            ui.show_world();
            layout(ui, view, fresh, {first});
            click(ui, "action-assign");
            layout(ui, view, fresh, {first});
            scroll_form(ui, view, fresh, {first}, false);
            click(ui, "profession-apply");
            const auto fresh_assignment = action(ui, UiActionType::AssignProfession);
            check(fresh_assignment.residents == std::vector<int>{first} &&
                      fresh_assignment.assignment == ProfessionAssignment{},
                  "Reused resident ID retained the former world's profession or priority draft");
            click(ui, "profession-close");
            check(ui.take_actions().empty(), "Closing the profession panel emitted a gameplay command");

            // Fill the authoritative ingress queue without applying commands:
            // a subsequent map request must expose its immediate rejection and
            // leave the draft armed so the player can retry or cancel it.
            ClientSession pressure;
            layout(ui, view, pressure, {});
            bool rejected = false;
            for (int command = 0; command <= 40 && !rejected; ++command)
                rejected = !pressure.move({first}, 26, 26).accepted;
            check(rejected, "Immediate map rejection scenario did not reach server ingress pressure");
            view.arm_worksite(worksite.assignment, {first});
            motion(ui, {point.x, point.y});
            map.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            check(!ui.process_event(map), "Immediate rejection map point was covered by the HUD");
            view.handle_event(map, pressure);
            const auto immediate_error = view.take_worksite_error();
            check(view.worksite_armed() && immediate_error && !immediate_error->empty() &&
                      !view.take_worksite_error(),
                  "Immediate worksite rejection was hidden, repeated, or silently canceled its draft");
            view.reset_world_state();
            check(!view.take_worksite_error() && !view.worksite_armed(),
                  "World reset retained a failed worksite action or error");
            std::filesystem::remove(settings);
        }
        SDL_Quit();
        std::cout << "Profession keyboard form, pointer actions, frozen selection, map worksite, "
                     "server Stop and paused skill inspection passed.\n";
        return 0;
    } catch (const std::exception &error) {
        SDL_Quit();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "ribterra/world_state.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/renderer2d.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window *window = SDL_CreateWindow("Ribterra RmlUi test", 1440, 900, SDL_WINDOW_HIDDEN);
        check(window != nullptr, "window creation failed");
        {
            const auto base = std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
            const auto settings = std::filesystem::current_path() / "ui-test-settings.json";
            {
                std::ofstream output(settings);
                output << "{\"locale\":\"pl\",\"ui_scale\":1.15}\n";
            }
            ribterra::AssetCatalog assets(base / "assets");
            assets.verify();
            ribterra::Localization localization(base / "assets" / "locales");
            ribterra::render::Renderer2D renderer(window, base / "assets",
                                                  {ribterra::render::Backend::Software, false});
            ribterra::GameUi ui(renderer, assets, localization, settings, {1440, 900});
            check(ui.locale() == "pl" && ui.scale() > 1.14f && ui.scale() < 1.16f,
                  "settings must load before starting a world");
            ribterra::CommandReply bootstrap_reply;
            bootstrap_reply.status = ribterra::CommandReplyStatus::Applied;
            bootstrap_reply.reason = "wilderness.accepted";
            check(ui.format_command_reply(bootstrap_reply).empty(),
                  "Automatic wilderness bootstrap must not create persistent player feedback");
            for (const auto &locale : ribterra::Localization::supported_locales()) {
                localization.set_locale(locale);
                for (const int count : {1, 2, 5, 21, 22, 25}) {
                    const auto people = localization.format("hud.population", {{"count", count}});
                    check(!people.empty(), "all five locales must format ICU plural categories");
                }
            }
            localization.set_locale("pl");
            const auto new_game = ui.element_center_for_test("new-game");
            SDL_Event move{};
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.x = new_game.x;
            move.motion.y = new_game.y;
            check(ui.process_event(move), "menu must consume pointer input");
            SDL_Event click{};
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.button = SDL_BUTTON_LEFT;
            check(ui.process_event(click), "UI button press must not fall through to the world");
            click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(click), "UI button release must not fall through to the world");
            auto actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::NewGame,
                  "pointer click must route new-game intent");
            ui.show_world();
            ui.update(nullptr);
            SDL_Event f1{};
            f1.type = SDL_EVENT_KEY_DOWN;
            f1.key.key = SDLK_F1;
            check(ui.process_event(f1), "F1 must be consumed by the world HUD");
            check(ui.world_visible() && !ui.hud_visible(),
                  "First F1 press must hide the HUD without leaving the world");
            check(!ui.visible_for_test("action-strip"), "Hidden HUD must not expose V01 controls");
            f1.key.repeat = true;
            check(ui.process_event(f1) && !ui.hud_visible(), "Held F1 must not toggle the HUD repeatedly");
            f1.key.repeat = false;
            check(ui.process_event(f1), "Second F1 press must be consumed by the world HUD");
            check(ui.hud_visible() && ui.visible_for_test("action-strip"),
                  "Second F1 press must restore the complete V01 HUD");
            check(!ui.visible_for_test("system-controls"), "HUD utility tray must start collapsed");
            check(ui.activate_for_test("hud-settings-toggle"), "HUD settings toggle missing");
            ui.update(nullptr);
            check(ui.visible_for_test("system-controls"),
                  "Settings toggle must reveal the compact utility tray");
            SDL_Event close_tray{};
            close_tray.type = SDL_EVENT_KEY_DOWN;
            close_tray.key.key = SDLK_ESCAPE;
            check(ui.process_event(close_tray) && !ui.visible_for_test("system-controls"),
                  "Escape must close the utility tray before reaching GameView");
            check(ui.take_actions().empty(), "Closing the utility tray must not emit a world action");
            const auto minimap_narrow = ui.element_center_for_test("minimap-hit");
            renderer.set_logical_size({1600, 900});
            ui.set_logical_size({1600, 900});
            ui.update(nullptr);
            const auto minimap = ui.element_center_for_test("minimap-hit");
            check(minimap.x > minimap_narrow.x + 150 && std::abs(minimap.y - minimap_narrow.y) < .1f,
                  "Wider logical presentation did not keep the minimap at the right edge");
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.x = minimap.x;
            move.motion.y = minimap.y;
            check(ui.process_event(move), "Minimap must block pointer input from the world");
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.x = minimap.x;
            click.button.y = minimap.y;
            check(ui.process_event(click), "Minimap press must be consumed by the HUD");
            click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(click), "Minimap release must be consumed by the HUD");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions.front().type == ribterra::UiActionType::FocusMap &&
                      actions.front().x > 4900 && actions.front().x < 5100 &&
                      actions.front().y > 4900 && actions.front().y < 5100,
                  "Minimap click must emit its normalized camera target only");
            SDL_Event empty{};
            empty.type = SDL_EVENT_MOUSE_MOTION;
            empty.motion.x = 700;
            empty.motion.y = 450;
            ui.process_event(empty);
            empty.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            empty.button.button = SDL_BUTTON_LEFT;
            check(!ui.process_event(empty), "empty world click must propagate to GameView");
            check(!ui.visible_for_test("quick-build"), "Duplicate construction catalog remains");
            check(!ui.visible_for_test("nav-civic") && !ui.visible_for_test("catalog-house"),
                  "Construction entries must stay hidden until hammer click");
            check(ui.activate_for_test("build-more"), "Construction rail missing");
            ui.update(nullptr);
            check(ui.visible_for_test("nav-civic") && ui.visible_for_test("catalog-house"),
                  "Hammer must reveal categories and buildings together");
            check(ui.activate_for_test("build-more"), "Hammer toggle missing");
            ui.update(nullptr);
            check(!ui.visible_for_test("nav-civic") && !ui.visible_for_test("catalog-house"),
                  "Second hammer click must hide construction entries");
            check(ui.activate_for_test("build-more"), "Construction rail missing");
            check(ui.activate_for_test("catalog-house"), "HUD build element missing");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::Build &&
                      actions[0].building == ribterra::BuildingKind::House,
                  "RmlUi HUD must route typed build intent");
            ui.update(nullptr);
            check(!ui.visible_for_test("building-catalog"), "Build choice must close catalog");
            for (const auto &definition : ribterra::building_catalog) {
                if (!ribterra::alpha::buildable(definition.kind)) continue;
                check(ui.activate_for_test("build-more"), "Catalog toggle missing");
                ui.update(nullptr);
                const auto catalog_position = ui.element_center_for_test("catalog-house");
                check(catalog_position.x > 0 && catalog_position.y > 0, "Opened catalog is still hidden");
                const auto id = std::string("catalog-") + definition.slug;
                check(ui.element_rml_for_test(id).find("building-" + std::string(definition.slug) + ".png") != std::string::npos,
                      "Building icon missing from catalog");
                check(ui.activate_for_test(id), "Catalog building missing");
                actions = ui.take_actions();
                check(actions.size() == 1 && actions[0].building == definition.kind && actions[0].type == ribterra::UiActionType::Build,
                      "Catalog selected wrong building");
            }
            check(!ui.visible_for_test("chat"),"Chat must start closed");
            SDL_Event open_chat{};open_chat.type=SDL_EVENT_KEY_DOWN;open_chat.key.key=SDLK_RETURN;
            check(ui.process_event(open_chat) && ui.visible_for_test("chat"),"Enter must open chat");
            check(ui.take_actions().empty(),"Opening chat submitted an action");
            open_chat.key.key=SDLK_ESCAPE;
            check(ui.process_event(open_chat) && !ui.visible_for_test("chat"),"Escape must close chat");
            open_chat.key.key=SDLK_RETURN;ui.process_event(open_chat);
            check(ui.focus_for_test("chat-input"), "chat input must accept focus");
            SDL_Event text{};
            text.type = SDL_EVENT_TEXT_INPUT;
            text.text.text = "<b>owned</b>";
            check(ui.process_event(text), "focused chat must consume text input");
            SDL_Event enter{};
            enter.type = SDL_EVENT_KEY_DOWN;
            enter.key.key = SDLK_RETURN;
            check(ui.process_event(enter), "focused chat must consume keyboard input");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::Chat &&
                      actions[0].text == "<b>owned</b>",
                  "chat submit must preserve plain user text");
            enter.key.repeat = true;
            check(ui.process_event(enter), "repeated Return must remain consumed by focused chat");
            check(ui.take_actions().empty(), "held Return must not submit duplicate chat actions");
            enter.key.repeat = false;
            enter.key.key = SDLK_KP_ENTER;
            check(ui.process_event(enter), "keypad Enter must submit focused chat");
            check(ui.take_actions().size() == 1, "keypad Enter must have Return submission parity");
            ribterra::ClientSession session;
            if (!actions.empty()) {
                std::string error;
                check(session.send_chat(actions[0].text, error),
                      "embedded authoritative chat failed");
            }
            ui.clear_chat_input();
            // Sending text leaves chat open. Close it before testing panels:
            // Escape intentionally gives the chat input priority over stocks.
            check(ui.visible_for_test("chat"), "sending chat must not silently close its input");
            SDL_Event close_chat{};
            close_chat.type = SDL_EVENT_KEY_DOWN;
            close_chat.key.key = SDLK_ESCAPE;
            check(ui.process_event(close_chat) && !ui.visible_for_test("chat"),
                  "chat must be dismissed before panel interaction tests");
            const std::vector<int> selected{session.residents().front().id};
            ui.set_view_state(&session, selected, true, 2);
            check(ui.visible_for_test("action-strip"), "V01 action strip must remain visible in world view");
            check(ui.enabled_for_test("action-gather"),
                  "Gather shortcut must be enabled for a locally controlled selected resident");
            check(ui.activate_for_test("action-gather"), "Gather shortcut element missing");
            ui.update(&session);
            check(ui.visible_for_test("orders-panel"),
                  "Gather shortcut must open the existing orders flow");
            check(ui.take_actions().empty(), "Gather shortcut must not create a second gathering model");
            check(ui.activate_for_test("orders-close"), "Orders close action missing");
            check(ui.activate_for_test("action-assign"), "Assign shortcut element missing");
            ui.update(&session);
            check(ui.visible_for_test("profession-panel"),
                  "Assign shortcut must open the existing profession flow");
            check(ui.activate_for_test("profession-close"), "Profession close action missing");
            ui.set_view_state(&session, selected, true, 2, ribterra::WorldLayer::Surface, {}, true);
            check(ui.activate_for_test("action-cancel"), "Cancel shortcut missing");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions.front().type == ribterra::UiActionType::CancelInteraction,
                  "Cancel shortcut must emit presentation-only CancelInteraction");
            ui.set_view_state(&session, selected, true, 2, ribterra::WorldLayer::Mine, {}, false);
            check(!ui.enabled_for_test("action-build"), "Build shortcut must report disabled below ground");
            check(!ui.enabled_for_test("action-cancel"), "Cancel shortcut must report disabled without an interaction");
            check(ui.focus_for_test("action-build"), "Disabled Build shortcut must remain keyboard focusable");
            ui.update(&session);
            check(ui.visible_for_test("action-strip-hint"),
                  "Keyboard focus must reveal the reason a V01 action is unavailable");
            check(!ui.element_rml_for_test("action-strip-hint").empty(),
                  "Unavailable V01 action must provide a visible reason");
            check(ui.activate_for_test("action-build"), "Build shortcut missing");
            check(!ui.visible_for_test("building-catalog"), "Build shortcut must stay disabled below ground");
            ui.set_view_state(&session, selected, true, 2, ribterra::WorldLayer::Surface, {}, false);
            check(ui.enabled_for_test("action-build"), "Build shortcut must be enabled on the surface");
            check(ui.activate_for_test("action-build"), "Build shortcut element missing");
            ui.update(&session);
            check(ui.visible_for_test("building-catalog"),
                  "Build shortcut must open the existing construction catalog");
            check(ui.activate_for_test("catalog-close"), "Construction catalog close action missing");
            check(ui.element_rml_for_test("resident-name").find(session.residents().front().name) !=
                      std::string::npos,
                  "selected resident name must be shown by the RmlUi HUD");
            check(!ui.element_rml_for_test("resident-health").empty() &&
                      !ui.element_rml_for_test("resident-hunger").empty(),
                  "selected resident condition must be shown by the RmlUi HUD");
            {
                ribterra::World source(70421, 1);
                const auto save=std::filesystem::temp_directory_path()/"ribterra-satiety-ui.save";
                for(float hunger : {0.f, 2.5f, 10.f}) {
                    auto state=source.export_state();
                    state.residents.front().hunger=hunger;
                    std::string error;
                    check(source.import_state(std::move(state),error), "Satiety fixture rejected");
                    check(source.save(save,error) && session.load(save,error), "Satiety fixture load failed");
                    ui.set_view_state(&session, {session.residents().front().id}, true, 2);
                    check(ui.element_rml_for_test("resident-hunger")==localization.format("hud.hunger",{{"count",10.f-hunger}}),
                          "Satiety must show 10 for fed and 0 for starving");
                }
                std::filesystem::remove(save);
            }
            {
                ribterra::World source(70421, 1);
                auto state = source.export_state();
                int stock_x = -1, stock_y = -1;
                for (int y = 12; y < 40 && stock_x < 0; ++y)
                    for (int x = 12; x < 40; ++x)
                        if (source.explored({x, y, ribterra::WorldLayer::Surface}, 1) &&
                            source.can_build(ribterra::BuildingKind::Stockpile, x, y)) {
                            stock_x = x;
                            stock_y = y;
                            break;
                        }
                check(stock_x >= 0, "stockpile HUD fixture needs a valid site");
                const int stockpile_id = state.next_building_id++;
                state.buildings.push_back({stockpile_id, ribterra::BuildingKind::Stockpile,
                                           stock_x, stock_y, 1.0F});
                state.storages.push_back({stockpile_id, 5000, 7, 0});
                auto stack = std::find_if(state.item_stacks.begin(), state.item_stacks.end(),
                                          [](const auto &item) {
                                              return item.location.kind == ribterra::ItemLocationKind::Storage;
                                          });
                check(stack != state.item_stacks.end(), "stockpile HUD fixture needs stored goods");
                const auto stockpile_used = stack->quantity;
                stack->location.entity_id = stockpile_id;
                std::string error;
                check(source.import_state(std::move(state), error), "stockpile HUD fixture rejected");
                const auto save = std::filesystem::temp_directory_path() / "ribterra-stockpile-ui.save";
                check(source.save(save, error) && session.load(save, error),
                      "stockpile HUD fixture load failed");
                const auto projected_storage = std::find_if(
                    session.storages().begin(), session.storages().end(),
                    [&](const auto &storage) { return storage.building_id == stockpile_id; });
                check(projected_storage != session.storages().end() &&
                          projected_storage->used == stockpile_used &&
                          projected_storage->capacity == 5000,
                      "stockpile HUD fixture lost authoritative per-storage values");
                ui.set_view_state(&session, {}, true, 2, ribterra::WorldLayer::Surface,
                                  {stockpile_id});
                ui.update(&session);
                check(ui.visible_for_test("building-summary-storage"),
                      "selected stockpile must show compact occupancy");
                check(ui.element_rml_for_test("building-summary-storage-value") ==
                          localization.format("inspector.storage_amount",
                                              {{"used", stockpile_used}, {"capacity", 5000}}),
                      "stockpile occupancy must use authoritative per-storage values");
                check(ui.element_rml_for_test("building-summary-details") ==
                          localization.text("inspector.open_stocks"),
                      "stockpile compact action must explain that it opens stocks");
                check(ui.activate_for_test("building-summary-details"),
                      "stockpile stocks action must be keyboard reachable");
                ui.update(&session);
                check(ui.visible_for_test("stocks-panel"),
                      "stockpile compact action must open the real stocks panel");
                check(ui.take_actions().empty(),
                      "opening stock details must not emit a world command");
                check(ui.activate_for_test("stocks-close"), "stockpile stocks panel must close");
                std::filesystem::remove(save);
            }
            ui.set_view_state(&session, selected, true, 2);
            check(ui.element_rml_for_test("resident-layer")
                          .find(localization.text("layer.surface")) != std::string::npos,
                  "selected resident layer must be shown by the RmlUi HUD");
            check(ui.activate_for_test("mine-tab"), "mine layer tab must be available");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::Layer &&
                      actions[0].value == 1,
                  "mine tab must emit a typed layer intent");
            check(ui.activate_for_test("surface-tab"), "surface layer tab must be available");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::Layer &&
                      actions[0].value == 0,
                  "surface tab must emit a typed layer intent");
            check(ui.activate_for_test("use-passage"), "passage action must be available");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::Passage,
                  "passage button must emit a typed passage intent");
            ui.show_help();
            check(!ui.world_visible(),
                  "production help action must leave the world HUD and open the menu");
            check(ui.activate_for_test("help-back"), "help back action must be available");
            check(ui.world_visible(), "help opened from a live world must return to that world");
            ui.show_menu();
            ui.show_help();
            check(ui.activate_for_test("help-back"),
                  "pre-world help back action must be available");
            check(!ui.world_visible(),
                  "help opened from the main menu must return to the main menu");
            ui.show_world();
            ui.update(&session, "Ready");
            const auto chat_markup = ui.element_rml_for_test("chat-log");
            check(chat_markup.find("<b>owned</b>") == std::string::npos,
                  "chat text must not become executable RML markup");
            // A work-counter update between press and release must preserve the
            // actual RmlUi button node and its exact construction identity.
            const auto build = session.build(ribterra::BuildingKind::House, 26, 26);
            check(build.accepted && build.pending, "stocks test house must enter the server queue");
            int site_id = 0;
            for (int tick = 0; tick < 1200 && site_id == 0; ++tick) {
                session.advance();
                for (const auto &site : session.construction_sites())
                    if (site.phase == ribterra::ConstructionPhase::Work && site.work < .9F)
                        site_id = site.building_id;
            }
            check(site_id > 0, "stocks test house must receive materials and begin work");
            ui.update(&session, "Ready");
            const auto pointer = [&](const std::string &id) {
                const auto center = ui.element_center_for_test(id);
                check(center.x > 0 && center.y > 0, "stocks control must have visible geometry");
                SDL_Event motion{};
                motion.type = SDL_EVENT_MOUSE_MOTION;
                motion.motion.x = center.x;
                motion.motion.y = center.y;
                check(ui.process_event(motion), "stocks control must consume pointer motion");
            };
            SDL_Event stock_click{};
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            stock_click.button.button = SDL_BUTTON_LEFT;
            pointer("resource-wood");
            check(ui.process_event(stock_click), "stocks toggle must consume pointer press");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(stock_click), "stocks toggle must consume pointer release");
            ui.update(&session, "Ready");
            check(ui.take_actions().empty(), "opening stocks must not emit a world action");
            check(ui.visible_for_test("stocks-panel"), "resource click must open stocks before Escape test");
            SDL_Event dismiss_stocks{};
            dismiss_stocks.type = SDL_EVENT_KEY_DOWN;
            dismiss_stocks.key.key = SDLK_ESCAPE;
            check(ui.process_event(dismiss_stocks), "Escape must be consumed by the open stocks panel");
            check(!ui.visible_for_test("stocks-panel"), "Escape must close stocks before reaching GameView");
            check(ui.take_actions().empty(), "dismissing stocks must not emit Pause or another world action");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            pointer("resource-wood");
            check(ui.process_event(stock_click), "stocks must reopen after Escape dismissal");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(stock_click), "reopened stocks must consume pointer release");
            ui.update(&session, "Ready");
            const auto suffix = std::to_string(site_id);
            const auto work_before = ui.element_rml_for_test("site-work-" + suffix);
            check(!work_before.empty(), "stocks panel must display construction work");
            check(ui.focus_for_test("cancel-site-" + suffix), "delivery cancellation must remain keyboard reachable below stock rows");
            ui.update(&session, "Ready");
            pointer("cancel-site-" + suffix);
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            check(ui.process_event(stock_click), "cancel must consume pointer press");
            check(ui.take_actions().empty(), "cancel must not fire before pointer release");
            session.advance();
            ui.update(&session, "Ready");
            check(ui.element_rml_for_test("site-work-" + suffix) != work_before,
                  "interleaved simulation tick must actually change displayed work");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(stock_click),
                  "cancel must consume pointer release after update");
            actions = ui.take_actions();
            check(actions.size() == 1 &&
                      actions.front().type == ribterra::UiActionType::CancelConstruction &&
                      actions.front().value == site_id,
                  "cancel click must survive a work update and preserve the exact building ID");
            pointer("stocks-close");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            check(ui.process_event(stock_click), "stocks close must consume pointer press");
            stock_click.type = SDL_EVENT_MOUSE_BUTTON_UP;
            check(ui.process_event(stock_click), "stocks close must consume pointer release");
            ui.update(&session, "Ready");
            check(ui.take_actions().empty(), "closing stocks must not emit a world action");
            ribterra::render::Frame2D frame;
            ui.compose(frame);
            check(!frame.commands.empty(), "RmlUi must emit frame commands");
            bool geometry = false;
            const auto expected_blend=renderer.device_info().software?ribterra::render::Blend::Alpha:ribterra::render::Blend::PremultipliedAlpha;
            for (const auto &command : frame.commands)
                if (const auto *draw = std::get_if<ribterra::render::GeometryDraw>(&command)) {
                    geometry=true;
                    check(draw->blend==expected_blend,"UI alpha representation must match the rendering backend");
                }
            check(geometry,"UI must emit geometry");
            renderer.render(frame, "ui-menu-and-hud.png");
            ui.show_menu();
            ui.activate_for_test("open-online");
            ui.update(nullptr);
            check(ui.focus_for_test("online-name"), "Online name must accept focus");
            SDL_SetModState(SDL_KMOD_NONE);
            SDL_Event select_all{};
            select_all.type = SDL_EVENT_KEY_DOWN;
            select_all.key.key = 0x0444; // Ukrainian layout: physical A produces ф.
            select_all.key.scancode = SDL_SCANCODE_A;
            select_all.key.mod = SDL_KMOD_CTRL;
            check(ui.process_event(select_all), "Localized Ctrl+A must stay in the name field");
            SDL_Event replacement{};
            replacement.type = SDL_EVENT_TEXT_INPUT;
            replacement.text.text = "Guest replacement";
            ui.process_event(replacement);
            ui.activate_for_test("online-join");
            actions = ui.take_actions();
            check(actions.size() == 1 && actions[0].type == ribterra::UiActionType::JoinGame &&
                      actions[0].text.find("\"name\":\"Guest replacement\"") != std::string::npos,
                  "Ctrl+A must use event modifiers and physical letter, not append to Player");
            std::filesystem::remove(settings);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "RmlUi menu, settings, localization, action routing, focus and chat escaping "
                     "passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " SDL=" << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

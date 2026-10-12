// Included inside GameUi::Impl. The UI displays the server's order/control projection.
std::vector<int> manual_selection;
std::vector<ManualOrderView> manual_rows;
std::vector<ControlPlayerView> manual_players;
std::string manual_person_name, manual_control_text, manual_markup, manual_player_markup;
std::uint64_t manual_epoch{};
PlayerId manual_player_id{};
int manual_resident_id{};
bool manual_can_cancel{}, manual_can_control{};
bool manual_form_update{};

static bool terminal_manual(ManualOrderPhase phase) {
    return phase == ManualOrderPhase::Complete || phase == ManualOrderPhase::Failed ||
           phase == ManualOrderPhase::Cancelled;
}
void render_manual() {
    if (manual_selection.empty())
        return;
    text(hud, "orders-person",
         localization.format("profession.summary",
                             {{"name", manual_person_name}, {"count", manual_selection.size()}}));
    text(hud, "orders-control", manual_control_text);
    for (const auto *id : {"orders-release", "orders-transfer"}) {
        auto *button = hud->GetElementById(id);
        const bool enabled =
            manual_can_control && (std::string_view(id) != "orders-transfer" || !manual_players.empty());
        button->SetPseudoClass("disabled", !enabled);
        if (enabled)
            button->RemoveAttribute("disabled");
        else
            button->SetAttribute("disabled", "disabled");
    }
    std::string options;
    for (const auto &player : manual_players)
        options += "<option value=\"" + std::to_string(player.player_id) + "\">" + escaped(player.name) +
                   "</option>";
    if (options.empty())
        options = "<option value=\"0\">" + escaped(localization.text("manual.no_player")) + "</option>";
    if (options != manual_player_markup) {
        manual_player_markup = options;
        auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-player"));
        const auto previous = select->GetValue();
        select->RemoveAll();
        select->SetInnerRML(options);
        const bool still_present =
            std::any_of(manual_players.begin(), manual_players.end(),
                        [&](const auto &player) { return std::to_string(player.player_id) == previous; });
        select->SetValue(still_present            ? previous
                         : manual_players.empty() ? "0"
                                                  : std::to_string(manual_players.front().player_id));
    }
    std::string markup;
    for (const auto &row : manual_rows) {
        const auto id = std::to_string(row.order_id);
        std::string description =
            localization.text("manual.kind." + std::to_string(static_cast<int>(row.kind)));
        if (row.kind == ManualOrderKind::Gather)
            description += " · " + localization.text("manual.gather." +
                                                     std::to_string(static_cast<int>(row.gather_completion)));
        if (row.target)
            description +=
                " · " +
                localization.text(row.target->layer == WorldLayer::Surface ? "layer.surface" : "layer.mine") +
                " (" + std::to_string(row.target->x) + ", " + std::to_string(row.target->y) + ")";
        markup += "<div class=\"manual-row\"><div class=\"manual-name\">#" + id + " · " +
                  escaped(description) + "</div><div class=\"manual-detail\">" +
                  escaped(localization.text("manual.phase." + std::to_string(static_cast<int>(row.phase))));
        if (row.failure != PathFailure::None)
            markup += " · " + escaped(localization.text(path_failure_key(row.failure)));
        if (row.completion != ManualOrderCompletion::None)
            markup += " · " + escaped(localization.text("manual.completion." +
                                                        std::to_string(static_cast<int>(row.completion))));
        markup += "</div>";
        if (!terminal_manual(row.phase))
            markup += "<button id=\"order-cancel-" + id + "\" data-order-id=\"" + id + "\"" +
                      (manual_can_cancel ? "" : " disabled=\"disabled\"") + ">" +
                      escaped(localization.text("manual.cancel")) + "</button>";
        markup += "</div>";
    }
    if (markup.empty())
        markup = "<p>" + escaped(localization.text("manual.empty")) + "</p>";
    if (markup != manual_markup) {
        manual_markup = markup;
        hud->GetElementById("orders-list")->SetInnerRML(markup);
        for (const auto &row : manual_rows)
            if (auto *button = hud->GetElementById("order-cancel-" + std::to_string(row.order_id)))
                button->SetPseudoClass("disabled", !manual_can_cancel);
    }
}
void update_manual(const ClientSession *session, const Resident *resident, const std::vector<int> &ids) {
    manual_selection.clear();
    manual_rows.clear();
    manual_players.clear();
    manual_can_cancel = manual_can_control = false;
    if (!session || !resident) {
        visible(hud, "orders-panel", false);
        return;
    }
    manual_selection = profession_selection; // Already validated against this projection.
    (void)ids;
    manual_resident_id = resident->id;
    manual_person_name = resident->name;
    manual_epoch = session->control_epoch();
    manual_player_id = session->player_id();
    auto sorted_selection = manual_selection;
    std::sort(sorted_selection.begin(), sorted_selection.end());
    std::size_t mine = 0, others = 0;
    const ResidentControlView *first = nullptr;
    for (const auto &control : session->resident_controls()) {
        if (control.resident_id == resident->id)
            first = &control;
        if (std::binary_search(sorted_selection.begin(), sorted_selection.end(), control.resident_id)) {
            if (control.held_by_recipient)
                ++mine;
            else if (control.holder_id)
                ++others;
        }
    }
    manual_can_control = mine != 0;
    manual_can_cancel = first && first->held_by_recipient;
    const auto holder = !first || !first->holder_id ? localization.text("manual.free")
                        : first->held_by_recipient  ? localization.text("manual.you")
                                                    : first->holder_name;
    manual_control_text = localization.format("manual.holder", {{"name", holder}});
    if (first && first->disconnected_grace)
        manual_control_text += " · " + localization.text("manual.grace");
    if (manual_selection.size() > 1)
        manual_control_text +=
            "\n" +
            localization.format(
                "manual.group_control",
                {{"mine", mine}, {"other", others}, {"free", manual_selection.size() - mine - others}});
    for (const auto &player : session->control_players())
        if (player.player_id != manual_player_id)
            manual_players.push_back(player);
    // Show the selected person's queue, then at most four recent terminal results.
    for (const auto &row : session->manual_orders())
        if (row.resident_id == resident->id && !terminal_manual(row.phase))
            manual_rows.push_back(row);
    std::vector<ManualOrderView> history;
    for (const auto &row : session->manual_orders())
        if (row.resident_id == resident->id && terminal_manual(row.phase))
            history.push_back(row);
    std::sort(history.begin(), history.end(),
              [](const auto &a, const auto &b) { return a.order_id > b.order_id; });
    history.resize(std::min<std::size_t>(history.size(), 4));
    manual_rows.insert(manual_rows.end(), history.begin(), history.end());
    if (!hud->GetElementById("orders-panel")->IsClassSet("hidden"))
        render_manual();
}
void translate_manual() {
    manual_form_update = true;
    for (const auto &[id, key] :
         std::array<std::pair<const char *, const char *>, 11>{{{"orders-open", "title"},
                                                                {"orders-title", "title"},
                                                                {"orders-hint", "hint"},
                                                                {"orders-gather-label", "gather_label"},
                                                                {"orders-player-label", "player_label"},
                                                                {"orders-release", "release"},
                                                                {"orders-transfer", "transfer"},
                                                                {"orders-queue-title", "queue_title"},
                                                                {"orders-policy-hint", "policy_hint"},
                                                                {"orders-close", "close"},
                                                                {"orders-release-hint", "release_hint"}}})
        text(hud, id, localization.text(std::string("manual.") + key));
    auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-gather"));
    const auto previous = select->GetValue();
    select->RemoveAll();
    select->SetInnerRML("<option value=\"0\">" + escaped(localization.text("manual.gather.0")) +
                        "</option><option value=\"1\">" + escaped(localization.text("manual.gather.1")) +
                        "</option>");
    select->SetValue(previous == "0" ? "0" : "1");
    manual_player_markup.clear();
    render_manual();
    manual_form_update = false;
}
void reset_manual_form() {
    manual_form_update = true;
    manual_selection.clear();
    manual_rows.clear();
    manual_players.clear();
    manual_epoch = 0;
    manual_resident_id = 0;
    manual_markup.clear();
    manual_player_markup.clear();
    manual_can_cancel = manual_can_control = false;
    visible(hud, "orders-panel", false);
    hud->GetElementById("orders-list")->SetInnerRML("");
    dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-player"))->RemoveAll();
    dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-gather"))->SetValue("1");
    manual_form_update = false;
}
void open_manual() {
    if (manual_selection.empty())
        return;
    visible(hud, "profession-panel", false);
    visible(hud, "stocks-panel", false);
    hud->GetElementById("stocks-toggle")->SetClass("active", false);
    render_manual();
    visible(hud, "orders-panel", true);
    hud->GetElementById("orders-gather")->Focus();
}
void bind_manual() {
    listen(hud, "orders-open", "click", [&](auto &) { open_manual(); });
    listen(hud, "orders-close", "click", [&](auto &) {
        visible(hud, "orders-panel", false);
        hud->GetElementById("orders-open")->Focus();
    });
    listen(hud, "orders-gather", "change", [&](auto &) {
        if (manual_form_update)
            return;
        const auto value =
            dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-gather"))->GetValue();
        UiAction action;
        action.type = UiActionType::GatherMode;
        action.value = value == "1" ? 1 : 0;
        queue(std::move(action));
    });
    const auto control = [&](UiActionType type) {
        if (!manual_can_control || manual_selection.empty())
            return;
        UiAction action;
        action.type = type;
        action.residents = manual_selection;
        action.control_epoch = manual_epoch;
        if (type == UiActionType::TransferControl) {
            const auto value =
                dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("orders-player"))
                    ->GetValue();
            const auto target =
                std::find_if(manual_players.begin(), manual_players.end(),
                             [&](const auto &player) { return std::to_string(player.player_id) == value; });
            if (target == manual_players.end())
                return;
            action.target_player = target->player_id;
        }
        queue(std::move(action));
    };
    listen(hud, "orders-release", "click", [control](auto &) { control(UiActionType::ReleaseControl); });
    listen(hud, "orders-transfer", "click", [control](auto &) { control(UiActionType::TransferControl); });
    listen(hud, "orders-list", "click", [&](auto &event) {
        if (!manual_can_cancel)
            return;
        for (auto *element = event.GetTargetElement();
             element && element != hud->GetElementById("orders-list"); element = element->GetParentNode()) {
            const auto id = element->template GetAttribute<Rml::String>("data-order-id", "");
            const auto found = std::find_if(manual_rows.begin(), manual_rows.end(), [&](const auto &row) {
                return row.resident_id == manual_resident_id && std::to_string(row.order_id) == id &&
                       !terminal_manual(row.phase);
            });
            if (found == manual_rows.end())
                continue;
            UiAction action;
            action.type = UiActionType::CancelManualOrder;
            action.order_id = found->order_id;
            action.control_epoch = manual_epoch;
            queue(std::move(action));
            break;
        }
    });
}

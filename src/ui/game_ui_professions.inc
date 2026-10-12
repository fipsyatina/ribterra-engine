// Included inside GameUi::Impl; all displayed modifiers are server projections.
std::vector<int> profession_selection;
std::optional<ResidentProfessionView> profession_view;
std::string profession_person_name;
std::size_t profession_skill_index{};

std::string profession_percent(int bp) const {
    return (bp > 0 ? "+" : "") + localization.format("hud.number", {{"count", bp / 100.0}});
}
void sync_profession_form() {
    if (!profession_view)
        return;
    auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("profession-select"));
    const auto profession = profession_view->assignment.profession;
    select->SetValue(std::to_string(
        static_cast<int>(profession <= Profession::Farmer ? profession : Profession::Unassigned)));
    for (std::size_t i = 0; i < 5; ++i)
        dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("priority-" + std::to_string(i)))
            ->SetValue(std::to_string(profession_view->assignment.priorities[i]));
}
ProfessionAssignment profession_form() const {
    ProfessionAssignment assignment;
    auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("profession-select"));
    assignment.profession = static_cast<Profession>(std::stoi(select->GetValue()));
    for (std::size_t i = 0; i < 5; ++i)
        assignment.priorities[i] =
            static_cast<std::uint8_t>(std::stoi(dynamic_cast<Rml::ElementFormControlSelect *>(
                                                    hud->GetElementById("priority-" + std::to_string(i)))
                                                    ->GetValue()));
    return assignment;
}
void render_profession_breakdown() {
    if (!profession_view)
        return;
    const auto &b = profession_view->skills[profession_skill_index].modifier;
    text(hud, "profession-breakdown",
         localization.format("profession.breakdown",
                             {{"skill", profession_percent(b.skill_sum_bp)},
                              {"ability", profession_percent(b.ability_sum_bp)},
                              {"practice", profession_percent(b.familiarity_bp)},
                              {"total", profession_percent(b.uncapped_bp)},
                              {"applied", profession_percent(b.applied_bp)},
                              {"cap", b.capped ? localization.text("profession.cap") : std::string{}}}));
    for (std::size_t i = 0; i < skill_count; ++i)
        hud->GetElementById("profession-skill-" + std::to_string(i))
            ->SetClass("active", i == profession_skill_index);
}
void render_profession() {
    if (!profession_view)
        return;
    text(hud, "profession-person",
         localization.format("profession.summary",
                             {{"name", profession_person_name}, {"count", profession_selection.size()}}));
    text(hud, "profession-state",
         localization.text("profession.name." +
                           std::to_string(static_cast<int>(profession_view->assignment.profession))) +
             " · " +
             (!profession_view->manual_override && profession_view->wait_reason==ProfessionWaitReason::ManualOrder ? localization.text("profession.await_command") : localization.text("profession.wait." +
                               std::to_string(static_cast<int>(profession_view->wait_reason)))));
    for (std::size_t i = 0; i < skill_count; ++i) {
        visible(hud,("profession-skill-"+std::to_string(i)).c_str(),alpha::active_skill(i));
        if(!alpha::active_skill(i))continue;
        const auto &skill = profession_view->skills[i];
        text(hud, ("profession-skill-" + std::to_string(i)).c_str(),
             localization.format("profession.skill_row",
                                 {{"name", localization.text("profession.skill." + std::to_string(i))},
                                  {"xp", skill.experience},
                                  {"effect", profession_percent(skill.modifier.applied_bp)}}));
    }
    render_profession_breakdown();
}
void update_profession(const ClientSession *session, const Resident *resident, const std::vector<int> &ids) {
    const int old_id = profession_view ? profession_view->resident_id : 0;
    profession_view.reset();
    profession_selection.clear();
    if (session && resident) {
        for (const int id : ids) {
            if (std::any_of(session->residents().begin(), session->residents().end(),
                            [id](const auto &candidate) { return candidate.id == id; }))
                profession_selection.push_back(id);
        }
        const auto &views = session->resident_professions();
        const auto found = std::find_if(views.begin(), views.end(),
                                        [&](const auto &value) { return value.resident_id == resident->id; });
        if (found != views.end()) {
            profession_view = *found;
            profession_person_name = resident->name;
        }
    }
    if (!profession_view) {
        visible(hud, "profession-panel", false);
        return;
    }
    if (old_id != profession_view->resident_id)
        sync_profession_form();
    if (!hud->GetElementById("profession-panel")->IsClassSet("hidden"))
        render_profession();
}
void translate_professions() {
    for (const auto &[id, key] :
         std::array<std::pair<const char *, const char *>, 10>{{{"profession-open", "title"},
                                                                {"profession-title", "title"},
                                                                {"profession-label", "label"},
                                                                {"profession-priority-hint", "priority_hint"},
                                                                {"profession-apply", "apply"},
                                                                {"profession-worksite", "worksite"},
                                                                {"profession-stop", "stop"},
                                                                {"profession-skills-title", "skills_title"},
                                                                {"profession-close", "close"},
                                                                {"profession-skill-hint", "skill_hint"}}})
        text(hud, id, localization.text(std::string("profession.") + key));
    auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("profession-select"));
    const auto current = select->GetValue();
    std::string options;
    for (int i = 0; i <= static_cast<int>(Profession::Farmer); ++i)
        options += "<option value=\"" + std::to_string(i) + "\">" +
                   escaped(localization.text("profession.name." + std::to_string(i))) + "</option>";
    select->RemoveAll();
    select->SetInnerRML(options);
    select->SetValue(current.size() == 1 && current.front() >= '0' && current.front() <= '6' ? current : "0");
    for (std::size_t i = 0; i < 5; ++i) {
        text(hud, ("priority-label-" + std::to_string(i)).c_str(),
             localization.text("profession.job." + std::to_string(i)));
        auto *priority = dynamic_cast<Rml::ElementFormControlSelect *>(
            hud->GetElementById("priority-" + std::to_string(i)));
        const auto previous = priority->GetValue();
        options = "<option value=\"0\">" + escaped(localization.text("profession.off")) + "</option>";
        for (int level = 1; level <= 5; ++level)
            options +=
                "<option value=\"" + std::to_string(level) + "\">" + std::to_string(level) + "</option>";
        priority->RemoveAll();
        priority->SetInnerRML(options);
        priority->SetValue(previous.empty() ? "1" : previous);
    }
    render_profession();
}
void reset_profession_form() {
    profession_selection.clear();
    profession_view.reset();
    profession_person_name.clear();
    profession_skill_index = 0;
    visible(hud, "profession-panel", false);
    dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("profession-select"))->SetValue("0");
    for (std::size_t i = 0; i < 5; ++i)
        dynamic_cast<Rml::ElementFormControlSelect *>(hud->GetElementById("priority-" + std::to_string(i)))
            ->SetValue("1");
}
void open_profession() {
        if (!profession_view)
            return;
        visible(hud, "stocks-panel", false);
        visible(hud, "orders-panel", false);
        hud->GetElementById("stocks-toggle")->SetClass("active", false);
        sync_profession_form();
        render_profession();
        visible(hud, "profession-panel", true);
        hud->GetElementById("profession-select")->Focus();
}
void bind_professions() {
    listen(hud, "profession-open", "click", [&](auto &) {open_profession();});
    listen(hud, "profession-close", "click", [&](auto &) {
        visible(hud, "profession-panel", false);
        hud->GetElementById("profession-open")->Focus();
    });
    const auto submit = [&](UiActionType type) {
        if (profession_selection.empty())
            return;
        UiAction action;
        action.type = type;
        action.residents = profession_selection;
        if (type != UiActionType::Stop)
            action.assignment = profession_form();
        queue(std::move(action));
        if (type == UiActionType::ChooseWorksite)
            visible(hud, "profession-panel", false);
    };
    listen(hud, "profession-apply", "click", [submit](auto &) { submit(UiActionType::AssignProfession); });
    listen(hud, "profession-worksite", "click", [submit](auto &) { submit(UiActionType::ChooseWorksite); });
    listen(hud, "profession-stop", "click", [submit](auto &) { submit(UiActionType::Stop); });
    for (std::size_t i = 0; i < skill_count; ++i) {
        const auto choose = [&, i](auto &) {
            if(!alpha::active_skill(i))return;
            profession_skill_index = i;
            render_profession_breakdown();
        };
        for (const auto *event : {"focus", "mouseover", "click"})
            listen(hud, ("profession-skill-" + std::to_string(i)).c_str(), event, choose);
    }
}

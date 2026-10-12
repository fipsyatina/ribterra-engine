// Included in GameUi::Impl. Never mutates gameplay state directly.
std::uint64_t settlement_revision{};
std::vector<int> settlement_youth_ids;
std::string settlement_youth_markup;
static std::string remaining_time(std::uint64_t end,std::uint64_t tick) {
    const auto seconds=end>tick?(end-tick+19)/20:0;
    return std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
}
void translate_settlement() {
    for(const auto &[id,key]:std::array<std::pair<const char*,const char*>,8>{{
        {"settlement-title","title"},{"settlement-close","close"},{"settlement-night","night_button"},
        {"settlement-night-hint","night_hint"},{"settlement-recruit","recruit_button"},
        {"settlement-recruit-hint","recruit_hint"},{"settlement-population-title","population"},
        {"settlement-enterprises","enterprises"}}}) text(hud,id,localization.text(std::string("settlement.")+key));
    hud->GetElementById("settlement-open")->SetAttribute("title",localization.text("settlement.title"));
    for(const char* id:{"settlement-discontent","settlement-discontent-meter","settlement-causes","settlement-night","settlement-night-hint"})visible(hud,id,false);
    settlement_youth_markup.clear();
}
void update_settlement(const ClientSession *session) {
    if(!session){visible(hud,"settlement-panel",false);settlement_youth_ids.clear();return;}
    const auto &s=session->settlement();const auto tick=session->ticks();settlement_revision=s.revision;
    const auto calendar=session->calendar();
    text(hud,"settlement-discontent",localization.text("settlement.discontent")+": "+std::to_string(s.discontent())+" / 100");
    hud->GetElementById("settlement-meter-fill")->SetProperty("width",std::to_string(s.discontent())+"%");
    text(hud,"settlement-causes",localization.text("settlement.causes")+": "+std::to_string(s.night_discontent)+" / "+std::to_string(s.youth_discontent));
    text(hud,"settlement-schedule",localization.text(!calendar?"weather.unknown":!calendar->night?"settlement.day":s.night_shift(calendar->tick)?"settlement.forced_night":"settlement.night"));
    text(hud,"settlement-arrival",s.arrival_tick?localization.text("settlement.arriving")+" "+remaining_time(s.arrival_tick,tick):localization.text("settlement.no_arrival"));
    for(const auto &[id,enabled]:std::array<std::pair<const char*,bool>,2>{{
        {"settlement-night",calendar&&calendar->night&&!s.night_shift(calendar->tick)},
        {"settlement-recruit",calendar&&!s.arrival_tick&&session->inventory().available.food>=recruitment_food&&session->residents().size()<1500}}}) {
        auto *b=hud->GetElementById(id);b->SetPseudoClass("disabled",!enabled);
        if(enabled)b->RemoveAttribute("disabled");else b->SetAttribute("disabled","disabled");
    }
    settlement_youth_ids.clear();std::string markup;
    for(const auto &y:s.young) {
        const auto r=std::find_if(session->residents().begin(),session->residents().end(),[&](const auto &v){return v.id==y.resident_id;});
        if(r==session->residents().end())continue;
        markup+="<div class=\"settlement-young\">"+escaped(r->name)+" · "+escaped(localization.text("settlement.young"))+" · "+"<span id=\"young-timer-"+std::to_string(y.resident_id)+"\"></span>";
        if(y.forced_work)markup+="<p>"+escaped(localization.text("settlement.youth_forced"))+"</p>";
        else if(alpha::forced_work&&r->health>0) {
            settlement_youth_ids.push_back(y.resident_id);
            markup+="<button id=\"youth-force-"+std::to_string(y.resident_id)+"\" data-youth-id=\""+std::to_string(y.resident_id)+"\">"+escaped(localization.text("settlement.youth_button"))+"</button>";
        }
        markup+="</div>";
    }
    if(markup.empty())markup="<p>"+escaped(localization.text("settlement.no_youth"))+"</p>";
    if(markup!=settlement_youth_markup){settlement_youth_markup=markup;hud->GetElementById("settlement-youth")->SetInnerRML(markup);}
    for(const auto &y:s.young) if(auto *timer=hud->GetElementById("young-timer-"+std::to_string(y.resident_id)))
        timer->SetInnerRML(remaining_time(y.adult_tick,tick));
}
void bind_settlement() {
    listen(hud,"settlement-open","click",[&](auto &){
        visible(hud,"orders-panel",false);visible(hud,"profession-panel",false);visible(hud,"stocks-panel",false);
        visible(hud,"settlement-panel",true);hud->GetElementById("settlement-close")->Focus();
    });
    listen(hud,"settlement-close","click",[&](auto &){visible(hud,"settlement-panel",false);});
    const auto decision=[&](SettlementDecision kind,int id=0){
        if(!alpha::forced_work&&kind!=SettlementDecision::Recruit)return;
        UiAction a;a.type=UiActionType::SettlementDecision;a.value=int(kind);a.settlement_revision=settlement_revision;
        if(id)a.residents={id};queue(std::move(a));
    };
    listen(hud,"settlement-recruit","click",[decision](auto &){decision(SettlementDecision::Recruit);});
    listen(hud,"settlement-night","click",[decision](auto &){decision(SettlementDecision::NightShift);});
    listen(hud,"settlement-youth","click",[&,decision](auto &event){
        for(auto *e=event.GetTargetElement();e&&e!=hud->GetElementById("settlement-youth");e=e->GetParentNode()) {
            const auto id=e->template GetAttribute<Rml::String>("data-youth-id","");
            for(int valid:settlement_youth_ids)if(id==std::to_string(valid)){decision(SettlementDecision::YouthWork,valid);return;}
        }
    });
}

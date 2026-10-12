// A single, tab-free inspector. Commands use the same authoritative session as the HUD.
const ClientSession *bp_session{};
std::vector<int> bp_selection;
int bp_id{}, bp_kind{-1}, bp_order{}, bp_recipe{};
std::string bp_last_stats, bp_worker_options, bp_group_markup;
static constexpr std::array<const char*,20> bp_names{
 "Camp","House","Farm","Stockpile","Mine","Workshop","House of Faith","Tavern",
 "Cartographer's House","Research House","Smeltery","Fishing Station","Library","Military House",
 "Bridge","Sawmill","Mill","Kitchen","Clay Pit","Road"};
static constexpr std::array<const char*,15> bp_items{
 "Wood","Stone","Meals","Planks","Clay","Bricks","Grain","Vegetables","Fish","Meat","Flour","Tools","Firewood","Hide","Spears"};
static constexpr std::array<const char*,15> bp_recipes{
 "Wheat","Vegetables","Fish","Planks","Flour","Bread","Fish meal","Meat meal","Bricks","Stone tools","Firewood","Clay","Husbandry","Vegetable stew","Spears"};
void bp_enabled(const char*id,bool enabled) {
 auto *e=hud->GetElementById(id);
 if(enabled)e->RemoveAttribute("disabled");else e->SetAttribute("disabled","disabled");
 e->SetPseudoClass("disabled",!enabled);
}
std::string bp_value(const char*id) {
 auto*e=dynamic_cast<Rml::ElementFormControl*>(hud->GetElementById(id));return e?e->GetValue():"";
}
static int bp_number(const std::string&s,int fallback=1) {
 try {size_t end{};int n=std::stoi(s,&end);return end==s.size()?n:fallback;}catch(...){return fallback;}
}
void bp_building_preview(Rml::Element*element,BuildingKind kind,int level) {
 element->RemoveAttribute("rect");
 element->SetAttribute("src",building_preview_path(kind,level));
}
int bp_batches() {return std::clamp(bp_number(bp_value("bp-quantity")),1,20);}
std::vector<int> bp_targets() {
 std::vector<int> result;if(!bp_session)return result;
 const auto scope=bp_value("bp-scope");
 for(const auto&b:bp_session->buildings()) if(int(b.kind)==bp_kind&&b.nation==bp_session->nation_id()&&b.progress==1&&
  (scope=="all"||(scope=="selected"?std::find(bp_selection.begin(),bp_selection.end(),b.id)!=bp_selection.end():b.id==bp_id)))result.push_back(b.id);
 return result;
}
void bp_open_research(int kind) {
 if(!alpha::research)return;
 visible(hud,"building-research",true);
 std::string cards;
 for(int i=0;i<20;++i)cards+="<button id=\"br-card-"+std::to_string(i)+"\" data-tech=\""+std::to_string(i)+"\" class=\""+(i==kind?"active":"")+"\"><img src=\"art/building-"+building_catalog[i].slug+".png\"/><span>"+escaped(bp_names[i])+" II</span></button>";
 hud->GetElementById("br-cards")->SetInnerRML(cards);
 text(hud,"br-name",std::string(bp_names[kind])+" II");
 text(hud,"br-description","Unlocks the next version of this building.");
}
void bind_buildings() {
 listen(hud,"bp-close","click",[&](auto&){building_details_open=false;visible(hud,"building-panel",false);visible(hud,"building-research",false);hud->GetElementById("building-summary-details")->Focus();});
 listen(hud,"building-summary-close","click",[&](auto&){building_details_open=false;visible(hud,"building-panel",false);visible(hud,"building-summary",false);queue({UiActionType::ClearBuildingSelection});});
 listen(hud,"building-summary-details","click",[&](auto&){
 if(bp_id<=0||hud->GetElementById("building-summary-details")->HasAttribute("disabled"))return;
  if(bp_kind==int(BuildingKind::Stockpile)){open_stocks();return;}
  building_details_open=true;visible(hud,"building-panel",true);hud->GetElementById("bp-close")->Focus();
 });
 listen(hud,"br-close","click",[&](auto&){visible(hud,"building-research",false);});
 listen(hud,"bp-research","click",[&](auto&){if(bp_kind>=0)bp_open_research(bp_kind);});
 listen(hud,"br-cards","click",[&](auto&e){for(auto*p=e.GetTargetElement();p&&p!=hud->GetElementById("br-cards");p=p->GetParentNode())if(p->HasAttribute("data-tech")){const int k=p->template GetAttribute<int>("data-tech",0);if(k>=0&&k<20)bp_open_research(k);break;}});
 listen(hud,"bp-recipe","change",[&](auto&){bp_recipe=bp_number(bp_value("bp-recipe"),0);if(bp_recipe>=0&&bp_recipe<int(recipes.size()))dynamic_cast<Rml::ElementFormControlInput*>(hud->GetElementById("bp-quantity"))->SetValue("1");bp_last_stats.clear();});
 listen(hud,"bp-quantity","change",[&](auto&){bp_last_stats.clear();});
 listen(hud,"bp-scope","change",[&](auto&){bp_last_stats.clear();});
 listen(hud,"bp-worker-manage","click",[&](auto&){const int id=bp_number(bp_value("bp-worker"),0);if(id>0){UiAction a{UiActionType::ManageProductionWorker};a.value=id;queue(a);}});
 listen(hud,"bp-upgrade-start","click",[&](auto&){
  if(!bp_session||hud->GetElementById("bp-upgrade-start")->HasAttribute("disabled"))return;
  const auto b=std::find_if(bp_session->buildings().begin(),bp_session->buildings().end(),[&](const auto&b){return b.id==bp_id;});
  if(b==bp_session->buildings().end()||b->nation!=bp_session->nation_id()||b->progress!=1||
     (b->kind!=BuildingKind::House&&b->kind!=BuildingKind::Workshop))return;
  if(std::any_of(bp_session->economy().upgrades.begin(),bp_session->economy().upgrades.end(),[&](const auto&u){return u.building_id==bp_id;}))return;
  if(std::any_of(bp_session->economy().orders.begin(),bp_session->economy().orders.end(),[&](const auto&o){return o.building_id==bp_id&&production_active(o);}))return;
  UiAction a{UiActionType::Upgrade};a.value=bp_id;a.recipe=0;a.quantity=1;a.settlement_revision=bp_session->economy().revision;
  const int worker=bp_number(bp_value("bp-worker"),0);if(worker>0)a.residents={worker};queue(std::move(a));
 });
 listen(hud,"bp-main","click",[&](auto&){
  if(!bp_session||bp_kind<0||hud->GetElementById("bp-main")->HasAttribute("disabled"))return;
  const auto b=std::find_if(bp_session->buildings().begin(),bp_session->buildings().end(),[&](const auto&b){return b.id==bp_id;});if(b==bp_session->buildings().end()||b->nation!=bp_session->nation_id())return;
  if(b->progress<1){UiAction a{UiActionType::CancelConstruction};a.value=bp_id;queue(a);return;}
  if(bp_kind==0){visible(hud,"settlement-panel",true);return;}
  if(bp_kind==3){open_stocks();return;}
  if(bp_kind==4){UiAction a{UiActionType::Layer};a.value=1;queue(a);return;}
  if(bp_kind==9){bp_open_research(1);return;}
  if(bp_order){UiAction a{UiActionType::CancelProduction};a.value=bp_order;a.settlement_revision=bp_session->economy().revision;queue(a);return;}
  if(!alpha::recipe_available(bp_recipe)||recipes[bp_recipe].building!=bp_kind)return;
  UiAction a{UiActionType::BuildingProduction};a.buildings=bp_targets();a.recipe=bp_recipe;
  const int batches=bp_number(bp_value("bp-quantity"),0);if(batches<1||batches>20)return;
  const int worker=bp_number(bp_value("bp-worker"),0);if(worker>0)a.residents={worker};
  a.quantity=batches;queue(std::move(a));
 });
}

std::string bp_text(const std::string& key) const {
 if(key=="state.waiting_snapshot"||key=="hint.waiting_snapshot")return localization.text("v01.waiting_snapshot");
 return localization.text("inspector." + key);
}
std::string production_feedback_text(const ClientSession* session,const std::string& key,int worker=0) const {
 if(session&&key=="economy.not_controller")for(const auto&control:session->resident_controls())
  if(control.resident_id==worker&&control.holder_id&&!control.held_by_recipient)
   return localization.format("production.held_by",{{"name",control.holder_name}});
 return localization.text(key);
}
std::string bp_item(int kind, int quantity) const {
 return std::to_string(quantity)+" × "+localization.text(std::string("item.")+economy_item_keys[kind]);
}
// Both enterprise panels derive their explanation from the current server snapshot.
std::string production_state(const ClientSession& session, const ProductionOrder& order) const {
 const auto calendar=session.calendar();
 if(!calendar)return "waiting_snapshot";
 if(order.cancel_requested){
  if(order.reason=="refund_wait")return "refund_wait";
  for(const auto&l:session.resident_logistics())if(l.resident_id==order.resident_id&&l.quantity>0)return "finishing_delivery";
  return "cancelling";
 }
 if(!production_active(order))return order.reason=="output_ground"?"output_ground":order.phase==ProductionPhase::Cancelled?"cancelled":"complete";
 const Resident* worker=nullptr;
 for(const auto& r:session.residents())if(r.id==order.resident_id)worker=&r;
 if(!worker||worker->health<=0)return "no_worker";
 const ResidentLogisticsView* delivery=nullptr;
 if(order.logistics_id)for(const auto& l:session.resident_logistics())if(l.resident_id==worker->id)delivery=&l;
 // A carried load finishes its delivery even at night. Pickup can wait for dawn.
 const bool dropping=delivery&&delivery->phase==LogisticsPhase::ToDropoff;
 if(!dropping){
  for(const auto& a:session.economy().wilderness.animals)
   if(a.health>0&&a.behaviour==AnimalBehaviour::Attacking&&a.target==worker->id)return "danger";
  if(const auto* y=session.settlement().youth(worker->id,session.ticks());y&&!y->forced_work)return "youth";
   if(calendar->night&&!session.settlement().night_shift(calendar->tick))return "night";
 }
 if(order.reason=="storage_full"||(delivery&&delivery->failure==LogisticsFailure::StorageFull))return "storage_full";
 if(order.reason=="unreachable"||worker->order_phase==OrderPhase::PathFailed||
    (delivery&&delivery->failure==LogisticsFailure::Unreachable))return "unreachable";
 if(order.reason=="delivery_failed")return "delivery_failed";
 switch(order.phase){
 case ProductionPhase::Materials:return "materials";
 case ProductionPhase::Delivery:return "delivery";
 case ProductionPhase::Travelling:return "travelling";
 case ProductionPhase::Working:return "working";
 case ProductionPhase::Output:return "output";
 default:return "unavailable";
 }
}
std::string production_summary(const ProductionOrder& o) const {
 const int made=std::min(o.requested,o.completed+(o.phase==ProductionPhase::Output?1:0));
 return bp_text("made_batches")+": "+std::to_string(made)+" / "+std::to_string(o.requested)+
        " · "+bp_text("remaining_batches")+": "+std::to_string(std::max(0,o.requested-made));
}

void update_production_workers(const ClientSession& session){
 std::vector<std::pair<std::string,std::string>> options{{"0",localization.text("production.automatic")}};
 std::string signature=localization.locale();
 for(const auto&r:session.residents())if(r.nation==session.nation_id()){
  const auto reason=session.production_worker_reason(r.id);
  const auto profile=std::find_if(session.resident_professions().begin(),session.resident_professions().end(),[&](const auto&p){return p.resident_id==r.id;});
  const auto profession=profile==session.resident_professions().end()?Profession::Unassigned:profile->assignment.profession;
  std::string label=r.name+" #"+std::to_string(r.id)+" · "+localization.text("profession.name."+std::to_string(int(profession)));
  if(!reason.empty())label+=" · "+localization.text("production.busy_label");
  options.push_back({std::to_string(r.id),label});signature+=options.back().first+label+reason;
 }
 auto*select=dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("bp-worker"));
 if(signature!=bp_worker_options){
  bp_worker_options=signature;auto selected=select->GetValue();select->RemoveAll();
  for(const auto&[id,label]:options)select->Add(escaped(label),id);
  if(std::none_of(options.begin(),options.end(),[&](const auto&o){return o.first==selected;}))selected="0";
  select->SetValue(selected);
 }
 const int worker=bp_number(select->GetValue(),0);
 text(hud,"bp-worker-label",bp_text("worker"));
 text(hud,"bp-worker-manage",localization.text("production.manage_worker"));
 visible(hud,"bp-worker-manage",worker>0);
 std::string detail=localization.text("production.auto_hint");
 if(worker){
  const auto reason=session.production_worker_reason(worker);
  detail=production_feedback_text(&session,reason.empty()?"production.worker.available":reason,worker);
  for(const auto&r:session.resident_professions())if(r.resident_id==worker){
   detail+=" · "+localization.text("profession.name."+std::to_string(int(r.assignment.profession)));
   if(const auto&site=r.assignment.worksite)detail+=" ("+std::to_string(site->x)+", "+std::to_string(site->y)+")";
  }
  for(const auto&o:session.economy().orders)if(o.resident_id==worker&&production_active(o))detail+=" · #"+std::to_string(o.building_id);
  for(const auto&m:session.manual_orders())if(m.resident_id==worker&&m.phase!=ManualOrderPhase::Complete&&m.phase!=ManualOrderPhase::Failed&&m.phase!=ManualOrderPhase::Cancelled){
   detail+=" · "+localization.text("manual.kind."+std::to_string(int(m.kind)));
   if(m.target)detail+=" ("+std::to_string(m.target->x)+", "+std::to_string(m.target->y)+")";
   break;
  }
 }
 text(hud,"bp-worker-detail",detail);
 text(hud,"bp-batches-hint",localization.text("production.batch_hint"));
}
void update_production_results(const ClientSession& session){
 std::string markup;
 if(session.production_results().size()>1){
  markup="<h2>"+escaped(localization.text("production.group_results"))+"</h2>";
  for(const auto&result:session.production_results()){
   std::string name="#"+std::to_string(result.building_id);
   for(const auto&b:session.buildings())if(b.id==result.building_id)name=localization.text(building_definition(b.kind).label_key)+" "+name;
   if(result.resident_id)for(const auto&r:session.residents())if(r.id==result.resident_id)name+=" · "+r.name;
   markup+="<p>"+escaped(name)+"<br/>"+escaped((result.reason!="economy.accepted"&&result.reason!="production.queued"&&result.reason!="production.sending"?localization.text("production.skipped")+": ":"")+production_feedback_text(&session,result.reason,result.resident_id))+"</p>";
  }
 }
 if(markup!=bp_group_markup){bp_group_markup=markup;hud->GetElementById("bp-group-feedback")->SetInnerRML(markup);}
}
void update_buildings(const ClientSession*session,const std::vector<int>&selection) {
 bp_session=session;bp_selection=selection;
 const Building*b=nullptr;
 std::vector<const Building*> selected_buildings;
 if(session&&!selection.empty())for(const int id:selection)for(const auto&v:session->buildings())if(v.id==id){selected_buildings.push_back(&v);if(id==selection.back())b=&v;break;}
  if(!b){building_details_open=false;visible(hud,"building-summary",false);visible(hud,"building-panel",false);visible(hud,"building-research",false);bp_id=0;bp_kind=-1;bp_order=0;return;}
 const auto calendar=session->calendar();
 visible(hud,"building-summary",true);
 const bool same_kind=std::all_of(selected_buildings.begin(),selected_buildings.end(),[&](const auto*item){return item->kind==b->kind;});
 if(!same_kind){
  building_details_open=false;visible(hud,"building-panel",false);visible(hud,"building-research",false);
 text(hud,"building-summary-title",localization.format("v01.buildings_selected",{{"count",selected_buildings.size()}}));
 text(hud,"building-summary-level","");text(hud,"building-summary-state",localization.text("v01.mixed_selection"));
 text(hud,"building-summary-reason",localization.text("v01.group_common_actions"));
  visible(hud,"building-summary-storage",false);
  bp_building_preview(hud->GetElementById("building-summary-art"),b->kind,1);
  bp_enabled("building-summary-details",false);hud->GetElementById("building-summary-details")->SetAttribute("title",localization.text("v01.single_type_required"));
  bp_id=b->id;bp_kind=int(b->kind);bp_order=0;return;
 }
 visible(hud,"building-panel",building_details_open);
 const bool changed=bp_id!=b->id;
 if(changed){
  bp_id=b->id;bp_kind=int(b->kind);bp_last_stats.clear();bp_worker_options.clear();dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("bp-worker"))->SetValue("0");visible(hud,"building-research",false);
  visible(hud,"building-catalog",false);visible(hud,"economy-panel",false);visible(hud,"settlement-panel",false);
  hud->GetElementById("bp-extra")->SetInnerRML("");
  auto*select=dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("bp-recipe"));select->RemoveAll();bp_recipe=-1;
  for(int i=0;i<int(recipes.size());++i)if(alpha::recipe_available(i)&&recipes[i].building==bp_kind){if(bp_recipe<0)bp_recipe=i;select->Add(bp_text(std::string("recipe.")+recipes[i].key),std::to_string(i));}
  if(bp_recipe>=0){if(bp_kind==5)bp_recipe=8;if(bp_kind==17)bp_recipe=13;select->SetValue(std::to_string(bp_recipe));dynamic_cast<Rml::ElementFormControlInput*>(hud->GetElementById("bp-quantity"))->SetValue("1");}
  auto*scope=dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("bp-scope"));scope->RemoveAll();
  scope->Add(bp_text("scope.this"),"this");scope->Add(bp_text("scope.selected"),"selected");scope->Add(bp_text("scope.all"),"all");scope->SetValue(selected_buildings.size()>1?"selected":"this");
 }
 if(selected_buildings.size()>1)
  dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("bp-scope"))->SetValue("selected");
 update_production_workers(*session);
 text(hud,"bp-title",localization.text(building_definition(b->kind).label_key));
 text(hud,"bp-purpose",bp_text("purpose."+std::to_string(bp_kind)));
 text(hud,"bp-owner",bp_text("owner")+": "+bp_text("nation")+" #"+std::to_string(b->nation));
 text(hud,"bp-recipe-label",bp_text("recipe"));text(hud,"bp-quantity-label",localization.text("production.batches"));text(hud,"bp-scope-label",bp_text("scope"));
 int level=1;for(const auto&u:session->economy().upgrades)if(u.building_id==b->id)level=u.level;
 const StorageView*selected_storage=nullptr;
 for(const auto&storage:session->storages())if(storage.building_id==b->id){selected_storage=&storage;break;}
 bp_building_preview(hud->GetElementById("bp-art"),b->kind,level);
 text(hud,"bp-level",bp_text("level")+" "+(level==1?"I":"II"));
 text(hud,"bp-upgrade-name",localization.text(building_definition(b->kind).label_key)+" II");
 text(hud,"bp-research",bp_text("research"));visible(hud,"bp-research",false);
 const ProductionOrder*order=nullptr;
 for(const auto&o:session->economy().orders)if(o.building_id==b->id&&(!order||o.id>order->id))order=&o;
 const bool active=order&&production_active(*order);
 const bool upgrade_kind=b->kind==BuildingKind::House||b->kind==BuildingKind::Workshop;
 const bool upgrade_visible=upgrade_kind&&b->progress==1;
 const bool upgrade_ready=upgrade_visible&&level==1&&!active&&b->nation==session->nation_id();
 const int upgrade_worker=bp_number(bp_value("bp-worker"),0);
 const bool worker_available=upgrade_worker?session->production_worker_reason(upgrade_worker).empty():std::any_of(session->residents().begin(),session->residents().end(),[&](const auto&r){return session->production_worker_reason(r.id,true).empty();});
 const auto effect=localization.text(b->kind==BuildingKind::House?"upgrade.house_effect":"upgrade.workshop_effect");
 visible(hud,"bp-upgrade",upgrade_visible);
 text(hud,"bp-upgrade-note",effect);
 text(hud,"bp-upgrade-start",localization.text("upgrade.start"));
 visible(hud,"bp-upgrade-start",upgrade_ready);
 bp_enabled("bp-upgrade-start",upgrade_ready&&worker_available);
 std::string upgrade_cost=bp_item(upgrade_recipe.input1,upgrade_recipe.quantity1)+" + "+bp_item(upgrade_recipe.input2,upgrade_recipe.quantity2);
 std::string upgrade_hint=localization.text(level==2?"upgrade.complete":active?"upgrade.busy":"upgrade.delivery");
 if(upgrade_ready){
  if(!worker_available)upgrade_hint=upgrade_worker?production_feedback_text(session,session->production_worker_reason(upgrade_worker),upgrade_worker):bp_text("hint.no_worker");
  for(const auto [kind,quantity]:{std::pair{upgrade_recipe.input1,upgrade_recipe.quantity1},std::pair{upgrade_recipe.input2,upgrade_recipe.quantity2}}){
   const int available=std::max(0,session->economy().stored[kind]-session->economy().reserved[kind]);
   if(available<quantity)upgrade_hint+=" "+localization.format("upgrade.missing",{{"item",localization.text(std::string("item.")+economy_item_keys[kind])},{"count",quantity-available}});
  }
 }
 text(hud,"bp-upgrade-cost",localization.text("upgrade.cost")+": "+upgrade_cost);
 visible(hud,"bp-upgrade-cost",level==1);
 text(hud,"bp-upgrade-hint",upgrade_hint);
 bp_order=active?order->id:0;
 auto row=[](const std::string& label,const std::string& value){return "<div class=\"bp-row\"><span>"+escaped(label)+"</span><b>"+escaped(value)+"</b></div>";};
 std::string stats,status="ready",action,info;
 bool enabled=true;const bool producing=bp_recipe>=0;
 stats=row(bp_text("construction"),b->progress<1?std::to_string(int(b->progress*100))+"%":bp_text("built"));
 if(b->progress<1){
  status="construction";action=bp_text("cancel_construction");info=bp_text("hint.construction");
  for(const auto&site:session->construction_sites())if(site.building_id==b->id){
   stats+=row(bp_text("worker"),std::to_string(site.assigned_workers));
   stats+=row(localization.text("item.wood"),std::to_string(site.delivered.wood)+" / "+std::to_string(site.required.wood));
   stats+=row(localization.text("item.stone"),std::to_string(site.delivered.stone)+" / "+std::to_string(site.required.stone));
   if(site.failure==LogisticsFailure::Unreachable||site.worker_failure!=PathFailure::None)status="unreachable";
    else if(!calendar)status="waiting_snapshot";
    else if(calendar->night&&!session->settlement().night_shift(calendar->tick))status="night";
   else if(site.worker_phase==ConstructionWorkerPhase::Waiting||site.worker_phase==ConstructionWorkerPhase::None)status="no_worker";
   else if(site.phase==ConstructionPhase::Delivery||site.phase==ConstructionPhase::Planned)status="delivery";
   else if(site.worker_phase==ConstructionWorkerPhase::Working)status="working";
   else status="travelling";
   if(status!="working")info=bp_text("hint."+status);
  }
 }else if(producing||active){
  status=order?production_state(*session,*order):"no_order";
  const auto feedback=session->production_feedback(b->id);
  if(!order&&feedback=="inspector.hint.no_worker"){
   const int chosen=bp_number(bp_value("bp-worker"),0);
   const bool available=chosen?session->production_worker_reason(chosen).empty():std::any_of(session->residents().begin(),session->residents().end(),[&](const auto&r){return session->production_worker_reason(r.id,true).empty();});
   if(!available)status="no_worker";
  }
  info=bp_text("hint."+status);
  std::string worker=bp_text("unassigned");
  if(order){worker="#"+std::to_string(order->resident_id);for(const auto&r:session->residents())if(r.id==order->resident_id)worker=r.name;}
  stats+=row(bp_text(active?"worker":order?"last_worker":"worker"),worker);
  const int recipe_id=active?order->recipe:bp_recipe;
  const auto&r=recipes[recipe_id<0?order->recipe:recipe_id];
  const bool upgrade=active&&order->upgrade;
  if(upgrade&&status=="working")info=localization.text("upgrade.working");
  stats+=row(upgrade?localization.text("upgrade.task"):bp_text(active?"active_recipe":"recipe"),upgrade?localization.text("economy.upgrade"):bp_text(std::string("recipe.")+r.key));
  stats+=row(upgrade?localization.text("upgrade.effect"):bp_text("batch_output"),upgrade?effect:bp_item(r.output,r.quantity));
  std::string inputs;
  for(const auto [kind,quantity]:{std::pair{upgrade?upgrade_recipe.input1:r.input1,upgrade?upgrade_recipe.quantity1:r.quantity1},std::pair{upgrade?upgrade_recipe.input2:r.input2,upgrade?upgrade_recipe.quantity2:r.quantity2}})if(kind>=0){
   if(!inputs.empty())inputs+=" + ";inputs+=bp_item(kind,quantity);
  }
  stats+=row(upgrade?localization.text("upgrade.cost"):bp_text("batch_inputs"),inputs.empty()?bp_text("no_inputs"):inputs);
  if(order){
   if(!active)stats+=row(bp_text("last_recipe"),order->upgrade?localization.text("economy.upgrade"):bp_text(std::string("recipe.")+recipes[order->recipe].key));
   const int made=std::min(order->requested,order->completed+(order->phase==ProductionPhase::Output?1:0));
   if(!order->upgrade){stats+=row(bp_text("made_batches"),std::to_string(made)+" / "+std::to_string(order->requested));
   stats+=row(bp_text("remaining_batches"),std::to_string(std::max(0,order->requested-made)));}
   if(!order->upgrade)stats+=row(bp_text("made_items"),bp_item(recipes[order->recipe].output,made*recipes[order->recipe].quantity));
   if(active){
    const int duration=(upgrade?upgrade_recipe.ticks:r.ticks)/(level==2&&!upgrade?2:1);
    stats+=row(upgrade?localization.text("upgrade.progress"):bp_text("batch_work"),std::to_string(std::clamp(order->work*100/duration,0,100))+"%");
   }
   if(!upgrade)stats+="<p>"+escaped(bp_text("delivery_note"))+"</p>";
  }
  if(active){action=order->cancel_requested?bp_text("state.cancelling"):bp_text("stop");enabled=!order->cancel_requested;}
  else {
   action=bp_text("start");const int batches=bp_batches();
   stats+=row(bp_text("new_order"),std::to_string(batches)+" × "+bp_text("batch")+" = "+bp_item(r.output,batches*r.quantity));
   for(const auto [kind,quantity]:{std::pair{r.input1,r.quantity1},std::pair{r.input2,r.quantity2}})if(kind>=0)
    stats+=row(localization.text(std::string("item.")+economy_item_keys[kind]),std::to_string(quantity*batches*int(bp_targets().size()))+" / "+std::to_string(std::max(0,session->economy().stored[kind]-session->economy().reserved[kind])));
   stats+="<p>"+escaped(bp_text("needed_free"))+"</p>";
   const int quantity=bp_number(bp_value("bp-quantity"),0);const int worker_id=bp_number(bp_value("bp-worker"),0);
   enabled=!bp_targets().empty()&&quantity>=1&&quantity<=20&&(worker_id==0||session->production_worker_reason(worker_id).empty());
  }
 }else {
  if(!upgrade_ready)stats+=row(bp_text("worker"),bp_text("not_required"));
  info=bp_text("purpose."+std::to_string(bp_kind));
  switch(b->kind){
  case BuildingKind::Camp:action=localization.text("settlement.title");break;
  case BuildingKind::House:stats+=row(bp_text("housing"),std::to_string(level==2?10:6));break;
  case BuildingKind::Stockpile:
   action=localization.text("inspector.open_stocks");
   if(selected_storage)stats+=row(localization.text("inspector.storage_used"),
    localization.format("inspector.storage_amount",{{"used",selected_storage->used},{"capacity",selected_storage->capacity}}));
   break;
  case BuildingKind::Mine:action=bp_text("underground");enabled=!session->portals().empty();break;
  case BuildingKind::Research:action=bp_text("research");break;
  case BuildingKind::Road:break;
  case BuildingKind::Bridge:break;
  default:status="unavailable";enabled=false;break;
  }
 }
 if(b->kind!=BuildingKind::Camp&&!alpha::buildable(b->kind)){status="unavailable";action.clear();info=localization.text("alpha.old_building");}
  if(b->nation!=session->nation_id()){enabled=false;info=bp_text("foreign");}
 if(!calendar){status="waiting_snapshot";enabled=false;info=localization.text("v01.waiting_snapshot");}
 if(stats!=bp_last_stats){bp_last_stats=stats;hud->GetElementById("bp-stats")->SetInnerRML(stats);}
  hud->GetElementById("bp-status")->SetClass("bp-waiting",status=="waiting_snapshot"||status=="materials"||status=="no_worker"||status=="night"||status=="unreachable"||status=="storage_full"||status=="output_ground"||status=="delivery_failed");
  text(hud,"bp-status",bp_text("state."+status));text(hud,"bp-info",info);text(hud,"bp-main",action);
 visible(hud,"bp-main",!action.empty());bp_enabled("bp-main",enabled);
 visible(hud,"bp-form",producing&&!active&&b->progress==1);
 visible(hud,"bp-worker-controls",!active&&b->progress==1&&(producing||upgrade_ready));
 visible(hud,"bp-batches-hint",producing&&!active);
 visible(hud,"bp-scope-row",producing&&b->nation==session->nation_id()&&b->progress==1&&!active);
 const auto feedback=session->production_feedback(b->id);
 int feedback_worker=0;for(const auto&r:session->production_results())if(r.building_id==b->id)feedback_worker=r.resident_id;
 text(hud,"bp-feedback",feedback.empty()?std::string{}:production_feedback_text(session,feedback,feedback_worker));
 update_production_results(*session);
 const bool group=selected_buildings.size()>1;
 const auto building_name=localization.text(building_definition(b->kind).label_key);
 text(hud,"building-summary-title",group?localization.format("v01.building_group",{{"count",selected_buildings.size()},{"name",building_name}}):building_name);
 text(hud,"building-summary-level",group?std::string{}:bp_text("level")+" "+(level==1?"I":"II"));
 text(hud,"building-summary-state",group?localization.text("v01.group_selection"):bp_text("state."+status));
 text(hud,"building-summary-reason",group?localization.text("v01.group_common_actions"):info);
 const bool storage_card=!group&&b->kind==BuildingKind::Stockpile&&selected_storage;
 visible(hud,"building-summary-storage",storage_card);
 if(storage_card){
  text(hud,"building-summary-storage-label",localization.text("inspector.storage_used"));
  text(hud,"building-summary-storage-value",localization.format("inspector.storage_amount",{{"used",selected_storage->used},{"capacity",selected_storage->capacity}}));
  const auto percent=selected_storage->capacity?100.f*float(selected_storage->used)/float(selected_storage->capacity):0.f;
  hud->GetElementById("building-summary-storage-fill")->SetProperty("width",std::to_string(std::clamp(percent,0.f,100.f))+"%");
  text(hud,"building-summary-state","");text(hud,"building-summary-reason","");
 }
 bp_building_preview(hud->GetElementById("building-summary-art"),b->kind,level);
 bp_enabled("building-summary-details",true);hud->GetElementById("building-summary-details")->RemoveAttribute("title");
 text(hud,"building-summary-details",storage_card?localization.text("inspector.open_stocks"):localization.text("v01.details"));
 if(group){
  text(hud,"bp-title",localization.format("v01.building_group",{{"count",selected_buildings.size()},{"name",building_name}}));
  text(hud,"bp-status",localization.text("v01.group_selection"));text(hud,"bp-info",localization.text("v01.group_common_actions"));
  visible(hud,"bp-stats",false);visible(hud,"bp-upgrade",false);
  const bool group_ready=std::all_of(selected_buildings.begin(),selected_buildings.end(),[&](const auto*item){
   return item->progress==1&&item->nation==session->nation_id()&&std::none_of(session->economy().orders.begin(),session->economy().orders.end(),[&](const auto&o){return o.building_id==item->id&&production_active(o);});
  });
  visible(hud,"bp-main",group_ready&&producing);bp_enabled("bp-main",group_ready&&producing&&enabled);
  visible(hud,"bp-form",group_ready&&producing);visible(hud,"bp-worker-controls",group_ready&&producing);
  visible(hud,"bp-scope-row",false);
  }else visible(hud,"bp-stats",true);
 if(!calendar){
  const auto waiting=localization.text("v01.waiting_snapshot");
  text(hud,"bp-status",waiting);text(hud,"bp-info",waiting);
  text(hud,"building-summary-state",waiting);text(hud,"building-summary-reason",waiting);
  bp_enabled("bp-main",false);bp_enabled("bp-upgrade",false);
 }
}

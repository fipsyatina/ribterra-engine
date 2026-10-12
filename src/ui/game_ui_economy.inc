std::string alpha_stock_table(const ClientSession& session) const {
 const auto& s=session.economy();
 std::string table;
 const auto section=[&](const char* title,std::initializer_list<int> items,bool historical=false){
  if(historical&&std::none_of(items.begin(),items.end(),[&](int i){return s.stored[i]||s.reserved[i]||s.carried[i]||s.ground[i];}))return;
  table+="<h2>"+escaped(localization.text(title))+"</h2><div class=\"stock-row stock-head\"><span class=\"stock-name\"></span>";
  for(const auto* key:{"alpha.stored","inventory.reserved","alpha.carried","alpha.ground"})table+="<span>"+escaped(localization.text(key))+"</span>";
  table+="</div>";
  for(int i:items){
   if(historical&&!s.stored[i]&&!s.reserved[i]&&!s.carried[i]&&!s.ground[i])continue;
   table+="<div class=\"stock-row\" data-item=\""+std::to_string(i)+"\"><span class=\"stock-name\">"+escaped(localization.text(std::string("item.")+economy_item_keys[i]))+"</span>";
   for(int amount:{s.stored[i],s.reserved[i],s.carried[i],s.ground[i]})table+="<span>"+std::to_string(amount)+"</span>";
   table+="</div>";
  }
 };
 section("alpha.materials",{0,1,3,4,5,12});
 section("alpha.ingredients",{6,7,8,9,10});
 section("alpha.meals",{2});
 section("alpha.historical_items",{11,13,14},true);
 return table;
}
std::uint64_t economy_revision{};
int economy_selected{};
std::string economy_markup;
void translate_economy(){
 bp_id=0;bp_kind=-1;
 for(const auto&[id,key]:std::array<std::pair<const char*,const char*>,4>{{{"economy-title","title"},{"economy-open","title"},{"economy-close","close"},{"economy-quantity-label","quantity"}}})text(hud,id,localization.text(std::string("economy.")+key));
 economy_markup.clear();
}
void update_economy(const ClientSession*session,const Resident*selected){
 if(!session){visible(hud,"economy-panel",false);return;}
 const auto&s=session->economy();economy_revision=s.revision;economy_selected=selected?selected->id:0;
 std::string current;for(const auto&o:s.orders)if(production_active(o)&&o.resident_id==economy_selected)current=localization.text("economy.title")+" #"+std::to_string(o.building_id)+" · "+bp_text("state."+production_state(*session,o));
 text(hud,"resident-production",current);
 if(hud->GetElementById("economy-panel")->IsClassSet("hidden"))return;
 text(hud,"economy-worker",localization.text("economy.worker")+": "+(selected?selected->name:localization.text("economy.select_worker")));
 hud->GetElementById("economy-stock")->SetInnerRML(alpha_stock_table(*session));
 const auto material=[&](int i,int n){return i<0?std::string{}:escaped(localization.text(std::string("item.")+economy_item_keys[i]))+" ×"+std::to_string(n);};
 std::string markup;
 for(const auto&b:session->buildings()){
  if(b.progress!=1||b.nation!=session->nation_id())continue;
  std::string buttons;const ProductionOrder*active=nullptr;
  for(const auto&o:s.orders)if(o.building_id==b.id&&production_active(o))active=&o;
  int level=1;for(const auto&u:s.upgrades)if(u.building_id==b.id)level=u.level;
  if(!active){
   for(int i=0;i<int(recipes.size());++i){const auto&r=recipes[i];if(!alpha::recipe_available(i)||r.building!=int(b.kind))continue;
    buttons+="<button id=\"production-"+std::to_string(b.id)+"-"+std::to_string(i)+"\" data-site=\""+std::to_string(b.id)+"\" data-recipe=\""+std::to_string(i)+"\">"+escaped(localization.text("economy.order"))+": "+material(r.output,r.quantity)+" — "+material(r.input1,r.quantity1)+(r.input2>=0?" + ":"")+material(r.input2,r.quantity2)+" · "+std::to_string(r.ticks/20/(level==2?2:1))+" s</button>";
   }
  }
  if(buttons.empty()&&!active)continue;
  markup+="<div class=\"economy-site\"><h2>"+escaped(localization.text(building_definition(b.kind).label_key))+" #"+std::to_string(b.id)+" · "+std::to_string(level)+"</h2>";
  if(active){
   const auto&o=*active;const auto&r=recipes[o.recipe];std::string worker=std::to_string(o.resident_id);
   for(const auto&p:session->residents())if(p.id==o.resident_id)worker=p.name;
   markup+="<p>"+escaped(worker)+" · "+(o.upgrade?escaped(localization.text("economy.upgrade")):material(r.output,r.quantity))+" · "+escaped(production_summary(o))+"</p><p>"+escaped(bp_text("state."+production_state(*session,o)))+"</p>";
   if(o.upgrade)markup+="<p>"+material(3,6)+" + "+material(5,4)+"</p>";
   else if(r.input1>=0)markup+="<p>"+material(r.input1,r.quantity1)+(r.input2>=0?" + ":"")+material(r.input2,r.quantity2)+"</p>";
   markup+="<p>"+escaped(bp_text("hint."+production_state(*session,o)))+"</p>";
   markup+="<p>"+escaped(localization.text("economy.work_remaining"))+": <span id=\"production-timer-"+std::to_string(o.id)+"\"></span></p><button data-cancel=\""+std::to_string(o.id)+"\""+(o.cancel_requested?" disabled":"")+">"+escaped(localization.text(o.cancel_requested?"inspector.state.cancelling":"economy.cancel"))+"</button>";
  }else markup+=buttons;
  markup+="</div>";
 }
 if(markup.empty())markup="<p>"+escaped(localization.text("economy.empty"))+"</p>";
 if(markup!=economy_markup){economy_markup=markup;hud->GetElementById("economy-sites")->SetInnerRML(markup);}
 for(const auto&o:s.orders)if(auto*timer=hud->GetElementById("production-timer-"+std::to_string(o.id))){int duration=o.upgrade?800:recipes[o.recipe].ticks;for(const auto&u:s.upgrades)if(u.building_id==o.building_id&&!o.upgrade)duration/=2;timer->SetInnerRML(std::to_string((std::max(0,duration*(o.requested-o.completed)-o.work)+19)/20)+" s");}
}
void bind_economy(){
 listen(hud,"economy-open","click",[&](auto&){visible(hud,"economy-panel",true);visible(hud,"settlement-panel",false);});
 listen(hud,"economy-close","click",[&](auto&){visible(hud,"economy-panel",false);});
 listen(hud,"economy-sites","click",[&](auto&event){
  for(auto*e=event.GetTargetElement();e&&e!=hud->GetElementById("economy-sites");e=e->GetParentNode()){
   int site=e->template GetAttribute<int>("data-site",0),cancel=e->template GetAttribute<int>("data-cancel",0);
   if((!site&&!cancel)||e->HasAttribute("disabled"))continue;
   UiAction a;a.type=cancel?UiActionType::CancelProduction:e->template GetAttribute<int>("data-upgrade",0)?UiActionType::Upgrade:UiActionType::Production;
   a.value=cancel?cancel:site;a.recipe=e->template GetAttribute<int>("data-recipe",0);a.residents={economy_selected};a.settlement_revision=economy_revision;
   auto*quantity=dynamic_cast<Rml::ElementFormControlSelect*>(hud->GetElementById("economy-quantity"));a.quantity=quantity?std::stoi(quantity->GetValue()):1;
   queue(std::move(a));return;
  }
 });
}

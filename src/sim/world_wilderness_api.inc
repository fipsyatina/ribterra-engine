void World::enable_wildlife(){impl_->populate_wildlife();}
CommandResult World::wilderness(WildernessAction action,const std::vector<int>&members,int x,int y,int argument,std::uint64_t revision,NationId nation){
 if(!alpha::expeditions&&action!=WildernessAction::Populate)return {false,"alpha.wilderness_unavailable"};
 auto&s=*impl_;auto&w=s.economy.wilderness;
 const auto reject=[](const char*why){return CommandResult{false,std::string("wilderness.")+why};};
 if(revision!=w.revision||w.revision==UINT64_MAX)return reject("stale");
 if(unsigned(action)>unsigned(WildernessAction::Populate)||argument<0)return reject("invalid");
 if(action==WildernessAction::Populate){s.populate_wildlife();return {true,"wilderness.accepted"};}
 if(action==WildernessAction::Rations){if(nation==1)w.reduced_rations=argument!=0;s.policy(nation).reduced_rations=argument!=0;++s.policy(nation).revision;++w.revision;return {true,"wilderness.accepted"};}
 if(action>=WildernessAction::Recall){
  auto it=std::find_if(w.expeditions.begin(),w.expeditions.end(),[&](const auto&e){return e.id==argument&&expedition_active(e)&&!e.members.empty()&&s.resident_nation(e.members.front())==nation;});
  if(it==w.expeditions.end())return reject("unavailable");auto&e=*it;
  if(action==WildernessAction::ContinueNight){
   auto day=calendar_day(s.calendar_tick());
   if(!is_night(s.calendar_tick())||e.night_prompt!=day)return reject("unavailable");
   if(e.night_permission==day)return {true,"wilderness.accepted"};
   if(e.outcome!="night")return reject("unavailable");
   e.night_permission=day;e.outcome.clear();auto*carcass=s.animal(e.target);e.phase=carcass&&carcass->health==0&&e.progress>0?ExpeditionPhase::Processing:ExpeditionPhase::Travel;
   s.policy(nation).night_discontent=std::min(100,s.policy(nation).night_discontent+5);++s.policy(nation).revision;
  }else if(action==WildernessAction::Recall)s.wild_return(e,"recalled");
  else if(action==WildernessAction::Retreat){
   if(!inside_world(x,y)||!tile(x,y).explored||!s.walkable({x,y}))return reject("invalid");
   e.x=x;e.y=y;e.phase=ExpeditionPhase::Retreat;e.outcome="retreated";
   for(int id:e.members)for(auto&t:s.logistics_tasks)if(t.resident_id==id&&t.phase!=LogisticsPhase::Complete&&t.phase!=LogisticsPhase::Failed)s.fail_task(t,LogisticsFailure::TargetInvalid);
  }else if(action==WildernessAction::Attack){
   auto*a=s.animal(e.target);if(!a||a->health<=0||!s.animal_visible(*a,nation))return reject("unavailable");
   e.phase=ExpeditionPhase::Defence;
  }
  ++w.revision;s.cache_dirty=true;return {true,"wilderness.accepted"};
 }
 const int quota=argument%16,target=argument/16;
 if(!inside_world(x,y)||(action!=WildernessAction::Explore&&!tile(x,y).explored)||members.empty()||members.size()>16||quota<1||quota>10||(w.expeditions.size()>=64&&std::none_of(w.expeditions.begin(),w.expeditions.end(),[](const auto&e){return !expedition_active(e);}))||w.next_expedition==INT_MAX)return reject("invalid");
 std::set<int>unique;
 for(int id:members){auto a=s.find_resident(id);
  if(!unique.insert(id).second||a==entt::null||s.registry.get<Identity>(a).nation!=nation||s.registry.get<Vital>(a).health<=0||s.registry.get<Position>(a).layer!=WorldLayer::Surface||s.economy_worker(id)||s.expedition_worker(id)||s.resident_busy(id)||s.manual_head(id)||s.registry.get<OrderStatus>(a).phase==OrderPhase::PathPending)return reject("busy");
  if(s.policy(nation).youth(id,s.ticks))return reject("youth");
 }
 if(target){auto*a=s.animal(target);if(action!=WildernessAction::Hunt||!a||a->health<=0||!s.animal_visible(*a,nation)||s.wild_distance(a->x,a->y,float(x),float(y))>12)return reject("unavailable");}
 s.populate_wildlife();
 Expedition e;e.id=w.next_expedition++;e.kind=ExpeditionKind(action);e.members=members;e.x=x;e.y=y;e.quota=target?1:quota;e.target=target;e.specific_target=target;e.deadline=s.ticks+3600;
 // Spears are taken from unreserved central inventory and returned physically, never duplicated.
 for(auto&stack:s.item_stacks)if(stack.item==ItemKind::Spear&&stack.owner_nation==nation&&stack.location.kind==ItemLocationKind::Storage&&!s.production_material(stack)){
  int n=std::min(int(stack.quantity-s.reserved(stack.stack_id)),int(members.size())-e.spears);stack.quantity-=n;e.spears+=n;
 }
 s.supersede_work_for_residents(members);
 for(int id:members){auto actor=s.find_resident(id);s.registry.get<Route>(actor)={};s.registry.get<Activity>(actor)={};s.registry.get<ProfessionState>(actor).assignment={};}
 if(w.expeditions.size()>=48)std::erase_if(w.expeditions,[](const auto&old){return !expedition_active(old);});
 w.expeditions.push_back(e);++w.revision;s.cache_dirty=true;return {true,"wilderness.accepted"};
}

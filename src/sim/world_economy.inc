// Bounded production jobs use the existing physical stack, reservation and hauling system.
EconomyState economy;
bool production_material(const ItemStackState &item) const {
 if(item.location.kind!=ItemLocationKind::Storage)return false;
 for(const auto&o:economy.orders)if(production_active(o)&&o.building_id==item.location.entity_id){
  if(o.upgrade)return item.item==ItemKind::Planks||item.item==ItemKind::Bricks;
  const auto&r=recipes[o.recipe];return int(item.item)==r.input1||int(item.item)==r.input2;
 }
 return false;
}
bool economy_worker(int id) const {
 return std::any_of(economy.orders.begin(),economy.orders.end(),[&](const auto&o){return production_active(o)&&o.resident_id==id;});
}
int building_level(int id) const {
 for(const auto&u:economy.upgrades)if(u.building_id==id)return u.level;return 1;
}
bool enterprise(BuildingKind kind) const {
 return kind==BuildingKind::Farm||kind==BuildingKind::Fishing||kind==BuildingKind::Workshop||kind==BuildingKind::Sawmill||kind==BuildingKind::Mill||kind==BuildingKind::Kitchen||kind==BuildingKind::ClayPit;
}

void release_production_worker(int id) {
 auto e=find_resident(id);if(e==entt::null)return;
 stop_actor(id);
 auto&p=registry.get<ProfessionState>(e);
 p.deferred=false;p.manual_override=false;p.next_evaluation_tick=ticks;
 p.wait_reason=p.assignment.automation_enabled?ProfessionWaitReason::Unassigned:ProfessionWaitReason::Stopped;
}
// Retire a completed physical leg before checking cancellation or worker health.
bool settle_production_delivery(ProductionOrder&o) {
 if(!o.logistics_id)return true;
 auto*t=task(o.logistics_id);
 if(t&&t->phase!=LogisticsPhase::Complete&&t->phase!=LogisticsPhase::Failed)return false;
 if(o.phase==ProductionPhase::Output){
  ++o.completed;o.work=0;
  o.phase=o.completed>=o.requested&&!o.cancel_requested?ProductionPhase::Complete:ProductionPhase::Materials;
  o.reason=t&&t->phase==LogisticsPhase::Failed?"output_ground":"";
  if(!production_active(o)){++economy.revision;release_production_worker(o.resident_id);}
 }else{o.phase=ProductionPhase::Materials;o.reason=t&&t->phase==LogisticsPhase::Failed?"delivery_failed":"";}
 o.logistics_id=0;cache_dirty=true;return true;
}
bool finish_production_cancel(ProductionOrder&o) {
 if(!o.cancel_requested)return false;
 if(o.logistics_id){
  auto*t=task(o.logistics_id);
  if(t&&t->phase!=LogisticsPhase::Complete&&t->phase!=LogisticsPhase::Failed){
   if(t->carry_stack_id||t->phase==LogisticsPhase::ToDropoff)return false;
   fail_task(*t,LogisticsFailure::Cancelled); // Release a pickup reservation without creating cargo.
  }
  if(!settle_production_delivery(o))return false;
 }
 auto e=find_resident(o.resident_id);
 if(e!=entt::null&&(resident_busy(o.resident_id)||registry.get<OrderStatus>(e).phase==OrderPhase::PathPending))return false;
 auto*b=building(o.building_id);if(!b)return false;
 const ItemLocation ground{ItemLocationKind::Ground,{b->x,b->y,b->layer},0};
 if(o.work>0&&o.phase!=ProductionPhase::Output){
  auto recipe=recipes[o.recipe];if(o.upgrade)recipe=upgrade_recipe;
  const std::array<std::pair<int,int>,2>inputs{{{recipe.input1,recipe.quantity1},{recipe.input2,recipe.quantity2}}};
  std::size_t new_stacks=0;
  // Preflight both refunds before changing either. A full inventory retains the
  // refundable inputs in the order until space exists; retries cannot duplicate them.
  for(const auto [kind,quantity]:inputs)if(kind>=0){
   std::uint64_t total=quantity;bool merge=false;
   for(const auto&item:item_stacks)if(int(item.item)==kind&&item.owner_nation==b->nation){
    total+=item.quantity;merge|=item.location==ground&&item.quantity<=unsigned(INT_MAX-quantity);
   }
   if(total>INT_MAX){o.reason="refund_wait";return false;}
   if(!merge)++new_stacks;
  }
  if(item_stacks.size()+new_stacks>max_item_stacks||new_stacks>UINT64_MAX-next_item_stack_id){o.reason="refund_wait";return false;}
  for(const auto [kind,quantity]:inputs)if(kind>=0)add_stack(ItemKind(kind),quantity,b->nation,ground);
 }
 if(b->kind!=BuildingKind::Camp&&b->kind!=BuildingKind::Stockpile)
  for(auto&item:item_stacks)if(item.location.kind==ItemLocationKind::Storage&&item.location.entity_id==b->id)item.location=ground;
 o.work=0;o.logistics_id=0;o.phase=ProductionPhase::Cancelled;o.cancel_requested=false;
 if(o.reason!="output_ground")o.reason.clear();
 release_production_worker(o.resident_id);++economy.revision;cache_dirty=true;return true;
}
void process_economy(){
 for(auto&o:economy.orders){
  if(!production_active(o))continue;
  if(o.cancel_requested){finish_production_cancel(o);continue;}
  if(!settle_production_delivery(o)||!production_active(o))continue;
  auto*b=building(o.building_id);auto e=find_resident(o.resident_id);
  if(!b||e==entt::null||registry.get<Vital>(e).health<=0){o.reason="unavailable";o.phase=ProductionPhase::Blocked;continue;}
  const CellCoord site{b->x,b->y,b->layer};
  if(resident_busy(o.resident_id)||registry.get<OrderStatus>(e).phase==OrderPhase::PathPending)continue;
  if(!work_allowed(e,Job::Build,site)){o.reason="night";continue;}
  auto&r=registry.get<Route>(e);auto&a=registry.get<Activity>(e);
  if(r.cursor<r.points.size())continue;
  auto recipe=recipes[o.recipe];
  if(o.upgrade)recipe=upgrade_recipe;
  const std::array<std::pair<int,int>,2>inputs{{{recipe.input1,recipe.quantity1},{recipe.input2,recipe.quantity2}}};
  if(o.work==0){
   bool waiting=false;
   for(const auto [kind,quantity]:inputs){
    if(kind<0)continue;
    unsigned here=0;
    for(const auto&s:item_stacks)if(int(s.item)==kind&&s.location.kind==ItemLocationKind::Storage&&s.location.entity_id==b->id)here+=s.quantity-reserved(s.stack_id);
    if(here>=unsigned(quantity))continue;
    waiting=true;o.phase=ProductionPhase::Materials;o.reason="materials";
    for(const auto&s:item_stacks){
     if(production_material(s)||int(s.item)!=kind||s.owner_nation!=b->nation||s.location.kind!=ItemLocationKind::Storage||s.location.entity_id==b->id||s.quantity<=reserved(s.stack_id))continue;
     // Only central stores supply enterprise deliveries, never steal another job's workbench.
     auto*source=building(s.location.entity_id);if(!source||(source->kind!=BuildingKind::Camp&&source->kind!=BuildingKind::Stockpile))continue;
     if(logistics_tasks.size()>=max_logistics_tasks||item_reservations.size()>=max_item_reservations||next_logistics_task_id==UINT64_MAX||next_item_reservation_id==UINT64_MAX||ticks>UINT64_MAX-10000)break;
     LogisticsTaskState t;t.task_id=next_logistics_task_id++;t.kind=LogisticsTaskKind::HaulToStorage;
     t.resident_id=o.resident_id;t.phase=LogisticsPhase::ToPickup;t.source_stack_id=s.stack_id;t.item=s.item;
     t.quantity=std::min({unsigned(quantity)-here,s.quantity-reserved(s.stack_id),resident_carry_capacity});
     t.destination_building_id=b->id;t.reservation_id=next_item_reservation_id++;t.deadline_tick=ticks+10000;
     item_reservations.push_back({t.reservation_id,s.stack_id,t.task_id,t.quantity,b->nation});
     logistics_tasks.push_back(t);active_task_by_resident[o.resident_id]=t.task_id;
     o.logistics_id=t.task_id;o.phase=ProductionPhase::Delivery;o.reason="";a={Job::Haul,0};r={};break;
    }
    break;
   }
   if(waiting)continue;
  }
  if(!at_location(o.resident_id,site,true)){
   o.phase=ProductionPhase::Travelling;o.reason="";
   std::optional<CellCoord> approach;
   for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{0,-1},{-1,0},{1,0},{0,1}}})if(walkable({site.x+dx,site.y+dy,site.layer})){approach=CellCoord{site.x+dx,site.y+dy,site.layer};break;}
   if(!approach){o.reason="unreachable";continue;}
   if(registry.get<OrderStatus>(e).phase==OrderPhase::PathFailed&&ticks%20)continue;
   auto result=queue_path(PathCommandKind::Move,{o.resident_id},*approach,false,Job::Move);
   if(!result.accepted)o.reason="unreachable";
   continue;
  }
  if(o.work==0){
   for(const auto [kind,quantity]:inputs){int left=quantity;if(kind<0)continue;
    for(auto&s:item_stacks)if(int(s.item)==kind&&s.location.kind==ItemLocationKind::Storage&&s.location.entity_id==b->id){auto n=std::min(unsigned(left),s.quantity-reserved(s.stack_id));s.quantity-=n;left-=int(n);if(!left)break;}
   }
   erase_empty_stacks();
  }
  a={Job::Idle,0};r={};o.phase=ProductionPhase::Working;o.reason="";
  const int duration=recipe.ticks/(building_level(b->id)==2&&!o.upgrade?2:1);
  o.work=std::min(duration,o.work+1);
  if(o.work<duration)continue;
  if(o.upgrade){
   economy.upgrades.push_back({b->id,2});
   for(auto&storage:storages)if(storage.building_id==b->id&&b->kind==BuildingKind::Stockpile)storage.capacity=10000;
   o.completed=1;o.phase=ProductionPhase::Complete;++economy.revision;release_production_worker(o.resident_id);continue;
  }
  if(!choose_storage(site,b->nation,ItemKind(recipe.output),recipe.quantity)){o.reason="storage_full";continue;}
  if(!gather_yield(e,ItemKind(recipe.output),recipe.quantity,Job::Idle,site)){o.reason="storage_full";continue;}
  o.logistics_id=active_task_by_resident[o.resident_id];o.phase=ProductionPhase::Output;
 }
}

EconomyState World::economy(NationId nation) const {
 auto result=EconomyState{};result.wilderness=impl_->economy.wilderness;result.revision=impl_->economy.revision;result.next_order_id=impl_->economy.next_order_id;result.orders=impl_->economy.orders;result.upgrades=impl_->economy.upgrades;
 std::unordered_map<std::uint64_t,std::uint32_t>held;
 for(const auto&r:impl_->item_reservations)held[r.stack_id]+=r.quantity;
 for(const auto&s:impl_->item_stacks)if(s.owner_nation==nation){
  if(s.location.kind==ItemLocationKind::Storage)result.stored[int(s.item)]+=int(s.quantity);
  if(s.location.kind==ItemLocationKind::Carried)result.carried[int(s.item)]+=int(s.quantity);
  if(s.location.kind==ItemLocationKind::Ground)result.ground[int(s.item)]+=int(s.quantity);
  result.reserved[int(s.item)]+=impl_->production_material(s)?int(s.quantity):int(held[s.stack_id]);
 }
 result.nations.clear();
 std::erase_if(result.orders,[&](const auto&o){const auto*b=impl_->building(o.building_id);return !b||b->nation!=nation;});
 std::erase_if(result.upgrades,[&](const auto&o){const auto*b=impl_->building(o.building_id);return !b||b->nation!=nation;});
 std::erase_if(result.wilderness.expeditions,[&](const auto&e){return e.members.empty()||impl_->resident_nation(e.members.front())!=nation;});
 if(!impl_->economy.nations.empty())std::erase_if(result.wilderness.animals,[&](const auto&a){return !impl_->animal_visible(a,nation);});
 result.wilderness.reduced_rations=impl_->policy(nation).reduced_rations;
 return result;
}
CommandResult World::production(int building_id,int resident_id,int recipe,int quantity,std::uint64_t revision,bool upgrade,NationId nation){
 auto&s=*impl_;auto*b=s.building(building_id);auto e=s.find_resident(resident_id);
 const auto reject=[](const char*reason){return CommandResult{false,std::string("economy.")+reason};};
 if(revision!=s.economy.revision||revision==UINT64_MAX)return reject("stale");
 if(b&&b->nation!=nation)return reject("foreign_building");
 if(!b||b->progress!=1||e==entt::null||s.registry.get<Identity>(e).nation!=nation||s.registry.get<Vital>(e).health<=0||!s.explored_by({b->x,b->y,b->layer},nation))return reject("unavailable");
 if(const auto*y=s.policy(nation).youth(resident_id,s.ticks);y&&!y->forced_work)return reject("youth");
 if(has_permanent_work(s.registry.get<ProfessionState>(e).assignment))return reject("assigned");
 if(s.registry.get<ProfessionState>(e).deferred)return reject("busy");
 if(s.expedition_worker(resident_id)||s.economy_worker(resident_id)||s.resident_busy(resident_id)||s.registry.get<OrderStatus>(e).phase==OrderPhase::PathPending||(s.registry.get<Activity>(e).job!=Job::Idle && s.registry.get<ProfessionState>(e).wait_reason!=ProfessionWaitReason::Strolling) || s.manual_head(resident_id))return reject("busy");
 if(std::any_of(s.economy.orders.begin(),s.economy.orders.end(),[&](const auto&o){return o.building_id==building_id&&production_active(o);}))return reject("busy");
 if(!upgrade&&!alpha::recipe_available(recipe))return {false,"alpha.recipe_unavailable"};
 if(recipe<0||recipe>=int(recipes.size())||quantity<1||quantity>20||std::count_if(s.economy.orders.begin(),s.economy.orders.end(),production_active)>=256||s.economy.next_order_id==INT_MAX)return reject("invalid");
 if(upgrade){if(s.building_level(building_id)!=1||(b->kind!=BuildingKind::House&&b->kind!=BuildingKind::Stockpile&&b->kind!=BuildingKind::Workshop))return reject("invalid");quantity=1;}
 else if(int(b->kind)!=recipes[recipe].building)return reject("invalid");
 if(std::none_of(s.storages.begin(),s.storages.end(),[&](const auto&v){return v.building_id==building_id;}))s.storages.push_back({building_id,100,7,0});
 s.supersede_work_for_residents({resident_id});
 s.registry.get<Activity>(e)={};s.registry.get<Route>(e)={};s.registry.get<OrderStatus>(e)={};
 std::size_t terminal=0;for(const auto&o:s.economy.orders)if(!production_active(o))++terminal;
 std::size_t remaining=s.economy.orders.size();
 std::erase_if(s.economy.orders,[&](const auto&o){if(production_active(o)||(terminal<=64&&remaining<256))return false;--terminal;--remaining;return true;});
 s.economy.orders.push_back({s.economy.next_order_id++,building_id,resident_id,recipe,quantity,0,0,ProductionPhase::Materials,0,upgrade,{}});
 ++s.economy.revision;s.cache_dirty=true;return {true,"economy.accepted"};
}
CommandResult World::cancel_production(int id,std::uint64_t revision,NationId nation){
 auto&s=*impl_;if(revision!=s.economy.revision)return {false,"economy.stale"};
 for(auto&o:s.economy.orders)if(o.id==id&&production_active(o)&&s.building(o.building_id)&&s.building(o.building_id)->nation==nation){
  if(!o.cancel_requested){o.cancel_requested=true;++s.economy.revision;s.cache_dirty=true;}
  s.finish_production_cancel(o);
  return {true,o.cancel_requested?"economy.cancelling":"economy.cancelled"};
 }
 return {false,"economy.unavailable"};
}

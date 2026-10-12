// Included in World::Impl. Deadlines use simulation ticks; night uses CalendarTick.
SettlementState settlement;
bool work_allowed(entt::entity e, Job job, std::optional<CellCoord> site = {}) const {
    if(e==entt::null) return false;
    if(job==Job::Idle || job==Job::Move) return true;
    const auto &id=registry.get<Identity>(e);
    for(const auto&a:economy.wilderness.animals)if(a.health>0&&a.behaviour==AnimalBehaviour::Attacking&&a.target==id.id)return false;
    if(const auto *y=policy(id.nation).youth(id.id,ticks);y && !y->forced_work) return false;
    if(!is_night(calendar_tick())) return true;
    if(auto*party=const_cast<Impl*>(this)->expedition_for(id.id))return job==Job::Haul||party->night_permission==calendar_day(calendar_tick());
    if(policy(id.nation).night_shift(calendar_tick())) return true;
    if(job==Job::Haul) {
        auto task_id=active_task_by_resident.find(id.id);
        if(task_id!=active_task_by_resident.end())
            for(const auto &t:logistics_tasks)
                if(t.task_id==task_id->second && t.phase==LogisticsPhase::ToDropoff) return true;
    }
    if(site) for(const auto &b:buildings)
        if(b.nation==id.nation && b.progress==1 && b.x==site->x && b.y==site->y && b.layer==site->layer &&
           (b.kind==BuildingKind::Tavern || b.kind==BuildingKind::Temple)) return true;
    return false;
}
void population_tick(NationId nation=1) {
    auto &nation_policy=policy(nation);
    const auto before=nation_policy.young.size();
    std::erase_if(nation_policy.young,[&](const auto &y){return ticks>=y.adult_tick;});
    if(before!=nation_policy.young.size()) { ++nation_policy.revision; cache_dirty=true; }
    if(!nation_policy.arrival_tick || ticks<nation_policy.arrival_tick) return;
    CellCoord spawn{24,24};
    for(const auto &b:buildings) if(b.nation==nation && b.kind==BuildingKind::Camp && b.progress==1) {
        spawn={b.x,b.y,b.layer};break;
    }
    for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{0,1},{-1,0},{0,-1}}})
        if(walkable({spawn.x+dx,spawn.y+dy,spawn.layer})) {spawn.x+=dx;spawn.y+=dy;break;}
    int id=1; for(const auto &[existing,e]:resident_by_id) { (void)e;id=std::max(id,existing+1); }
    auto e=registry.create();
    registry.emplace<Identity>(e,id,"Settler "+std::to_string(id),nation);
    registry.emplace<Position>(e,float(spawn.x),float(spawn.y),spawn,spawn.layer);
    registry.emplace<Activity>(e);registry.emplace<Vital>(e);registry.emplace<Skills>(e);
    registry.emplace<ProfessionState>(e);registry.emplace<Route>(e);registry.emplace<OrderStatus>(e);
    resident_by_id.emplace(id,e);
    nation_policy.young.push_back({id,ticks+youth_ticks,false});
    nation_policy.arrival_tick=0;++nation_policy.revision;cache_dirty=true;
    event("A young settler arrived.");
}

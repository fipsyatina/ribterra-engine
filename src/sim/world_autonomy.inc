// Free settlers use the same bounded path planner, cargo and work executors as orders.
void free_activity(entt::entity e) {
    auto &p=registry.get<ProfessionState>(e);
    const auto id=registry.get<Identity>(e);
    if(ticks<400 || manual_head(id.id)) { p.wait_reason=ProfessionWaitReason::Unassigned; return; }
    p.next_evaluation_tick=ticks+120+(id.id%5)*17;
    const auto from=resident_cell(id.id);
    if(from.layer!=WorldLayer::Surface) return;
    const unsigned choice=unsigned(ticks/160+id.id*7)%3;
    if(choice==0) {
        for(auto other:entities()) {
            if(other==e) continue;
            const auto oid=registry.get<Identity>(other);
            auto &op=registry.get<ProfessionState>(other);
            const auto at=resident_cell(oid.id);
            if(oid.nation!=id.nation || at.layer!=from.layer ||
               std::abs(at.x-from.x)+std::abs(at.y-from.y)>2 ||
               registry.get<Vital>(other).health<=0 || !op.assignment.automation_enabled ||
               (op.assignment.profession!=Profession::Unassigned &&
                op.assignment.profession!=Profession::Woodcutter &&
                op.assignment.profession!=Profession::Miner &&
                op.assignment.profession!=Profession::Forager) || op.manual_override || op.deferred ||
               manual_head(oid.id) || waiting_for_player(other) || resident_busy(oid.id) || active_work_by_resident.contains(oid.id) ||
               registry.get<Activity>(other).job!=Job::Idle ||
               registry.get<OrderStatus>(other).phase==OrderPhase::PathPending) continue;
            p.wait_reason=op.wait_reason=ProfessionWaitReason::Socializing;
            p.next_evaluation_tick=op.next_evaluation_tick=ticks+100;
            cache_dirty=true;
            return;
        }
    }
    CellCoord center=from;
    for(const auto &b:buildings) if(b.nation==id.nation && b.layer==from.layer && b.kind==BuildingKind::Camp) {
        center={b.x,b.y,b.layer}; break;
    }
    for(int attempt=0;attempt<12;++attempt) {
        const int variation=int((ticks/120+id.id*31+attempt*17)%121);
        CellCoord target{center.x+variation%11-5,center.y+variation/11-5,center.layer};
        const auto *t=&tile(target);
        if(target==from || !t || !t->explored || !walkable(target)) continue;
        if(queue_path(PathCommandKind::Move,{id.id},target,false,Job::Move).accepted) {
            p.wait_reason=ProfessionWaitReason::Strolling; return;
        }
    }
    p.wait_reason=ProfessionWaitReason::Unassigned;
}

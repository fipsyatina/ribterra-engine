// A finite batch of ordinary server commands, correlated to one reply at a time.
std::deque<int> group_sites;
int group_recipe{},group_batches{},group_applied{},group_skipped{},group_preferred_worker{};
std::uint64_t last_issued_request{},group_request{},group_sent_revision{};
bool group_wait_snapshot{};
std::string group_status;
std::vector<ClientSession::ProductionResult> group_feedback;
std::vector<int> group_used_workers;
void clear_group(){group_sites.clear();group_request=0;group_wait_snapshot=false;group_status.clear();group_feedback.clear();group_used_workers.clear();}
NationId controlled_nation() const {
 if(!view.lobby.enabled)return 1;
 const auto actor=network?network->player():player;
 for(const auto&p:view.lobby.players)if(p.id==actor)return NationId(p.team);
 return 0; // No authoritative membership yet.
}
std::string worker_reason(int id,bool automatic=false) const {
 const auto r=std::find_if(view.residents.begin(),view.residents.end(),[&](const auto&r){return r.id==id;});
 if(r==view.residents.end()||r->nation!=controlled_nation()||r->health<=0)return "economy.unavailable";
 if(const auto*y=view.settlement.youth(id,view.tick);y&&(automatic||!y->forced_work))return "economy.youth";
 if(std::any_of(view.resident_controls.begin(),view.resident_controls.end(),[&](const auto&c){return c.resident_id==id&&c.holder_id&&!c.held_by_recipient;}))return "economy.not_controller";
 const auto profile=std::find_if(view.resident_professions.begin(),view.resident_professions.end(),[&](const auto&p){return p.resident_id==id;});
 if(profile==view.resident_professions.end())return "economy.unavailable";
 if(has_permanent_work(profile->assignment)||(automatic&&(profile->assignment.profession!=Profession::Unassigned||profile->assignment.worksite)))return "economy.assigned";
 for(const auto&o:view.economy.orders)if(o.resident_id==id&&production_active(o))return "production.worker.production";
 for(const auto&e:view.economy.wilderness.expeditions)if(expedition_active(e)&&std::find(e.members.begin(),e.members.end(),id)!=e.members.end())return "production.worker.expedition";
 for(const auto&m:view.manual_orders)if(m.resident_id==id&&m.phase!=ManualOrderPhase::Complete&&m.phase!=ManualOrderPhase::Failed&&m.phase!=ManualOrderPhase::Cancelled)return "production.worker.manual";
 for(const auto&l:view.resident_logistics)if(l.resident_id==id&&l.phase!=LogisticsPhase::Complete&&l.phase!=LogisticsPhase::Failed)return "production.worker.delivery";
 if(r->order_phase==OrderPhase::PathPending||profile->deferred)return "production.worker.pending";
 if(r->job!=Job::Idle&&profile->wait_reason!=ProfessionWaitReason::Strolling)return "production.worker.busy";
 return {};
}
ClientSession::ProductionResult& group_result(int site){
 return *std::find_if(group_feedback.begin(),group_feedback.end(),[&](const auto&r){return r.building_id==site;});
}
void group_reply(const CommandReply&r){
 if(!group_request||r.request_id!=group_request||r.status==CommandReplyStatus::Queued)return;
 auto&entry=group_result(group_sites.front());entry.reason=r.status==CommandReplyStatus::Applied?"economy.accepted":r.reason;
 group_request=0;
 if(r.status==CommandReplyStatus::Applied){++group_applied;group_wait_snapshot=true;group_used_workers.push_back(entry.resident_id);}
 else {++group_skipped;group_status="Order rejected: "+r.reason;}
 group_sites.pop_front();
}
void pump_group(){
 if(group_request || (network&&!network->ready()))return;
 if(group_wait_snapshot){if(view.economy.revision<=group_sent_revision)return;group_wait_snapshot=false;}
 if(group_sites.empty())return;
 const int site=group_sites.front();auto&entry=group_result(site);
 const auto b=std::find_if(view.buildings.begin(),view.buildings.end(),[&](const auto&b){return b.id==site;});
 auto skip=[&](const std::string&reason){++group_skipped;entry.reason=reason;group_sites.pop_front();group_status=std::to_string(group_applied)+" orders started; "+std::to_string(group_skipped)+" skipped. "+reason;};
 if(b==view.buildings.end()||b->nation!=controlled_nation()||b->progress!=1||int(b->kind)!=recipes[group_recipe].building){skip("economy.unavailable");return;}
 for(const auto&o:view.economy.orders)if(o.building_id==site&&production_active(o)){skip("economy.busy");return;}
 int worker=group_preferred_worker;
 if(worker){
  entry.resident_id=worker;
  if(std::find(group_used_workers.begin(),group_used_workers.end(),worker)!=group_used_workers.end()){skip("production.worker.used");return;}
  const auto reason=worker_reason(worker);if(!reason.empty()){skip(reason);return;}
 }else for(const auto&r:view.residents){
  if(std::find(group_used_workers.begin(),group_used_workers.end(),r.id)==group_used_workers.end()&&worker_reason(r.id,true).empty()){worker=r.id;break;}
 }
 if(!worker){skip("inspector.hint.no_worker");return;}
 entry.resident_id=worker;group_sent_revision=view.economy.revision;
 const auto result=queue(CommandType::Production,{worker},group_recipe,group_batches,BuildingKind::House,WorldLayer::Surface,site,{},ManualQueueMode::Replace,GatherCompletion::UntilExhausted,0,0,group_sent_revision);
 if(result.accepted&&result.pending){group_request=last_issued_request;entry.reason="production.sending";group_status="Sending orders: "+std::to_string(group_applied)+" started, "+std::to_string(group_sites.size())+" pending.";}
 else skip(result.message);
}

LobbyState GameServer::lobby_snapshot() const {
 auto result=lobby_;result.host=host_;result.revision=revision_;
 for(const auto &[id,p]:players_)if(!p.observer)result.players.push_back({id,p.name,p.team,p.ready,p.connected,p.bot});
 return result;
}
CommandReply GameServer::lobby_command(PlayerId id,const GameCommand&c){
 auto reply=CommandReply{c.request_id,world_.ticks(),id,CommandReplyStatus::Rejected,PathFailure::None,"lobby.invalid"};
 if(!lobby_.enabled||!players_.contains(id)||!players_.at(id).connected||!c.residents.empty()||c.x<0||c.x>int(LobbyAction::BotTeam))return reply;
 auto &p=players_.at(id);
 switch(LobbyAction(c.x)){
 case LobbyAction::Team:
  if(lobby_.started||c.y<1||c.y>6)break;
  p.team=c.y;for(auto&[pid,member]:players_){(void)pid;member.ready=member.bot;}reply.status=CommandReplyStatus::Applied;break;
 case LobbyAction::Ready:
  if(lobby_.started||c.y<0||c.y>1)break;
  p.ready=c.y!=0;reply.status=CommandReplyStatus::Applied;break;
 case LobbyAction::Start: {
  if(id!=host_){reply.reason="lobby.host_only";break;}
  if(lobby_.started)break;
  if(std::count_if(players_.begin(),players_.end(),[](const auto&v){return v.second.connected&&!v.second.observer;})<1){reply.reason="lobby.need_players";break;}
  if(std::any_of(players_.begin(),players_.end(),[](const auto&v){return v.second.connected&&!v.second.observer&&!v.second.ready;})){reply.reason="lobby.not_ready";break;}
  std::vector<int> teams;for(const auto&[pid,member]:players_){(void)pid;if(!member.observer)teams.push_back(member.team);}
  world_.create_nations(teams,lobby_.capacity,config_.population);
  lobby_.started=true;reply.status=CommandReplyStatus::Applied;break;
 }
 case LobbyAction::AddBot: {
  if(id!=host_){reply.reason="lobby.host_only";break;}
  if(lobby_.started||players_.size()>=config_.max_players)break;
  Player bot;bot.bot=true;bot.ready=true;bot.connected=true;bot.token=secure_token();
  for(int team=1;team<=6;++team)if(std::none_of(players_.begin(),players_.end(),[&](const auto&v){return v.second.team==team;})){bot.team=team;break;}
  if(!bot.team)bot.team=1;
  const auto bot_id=next_player_++;bot.name="Bot "+std::to_string(bot_id);players_.emplace(bot_id,std::move(bot));
  for(auto&[pid,member]:players_)member.ready=member.bot;
  reply.status=CommandReplyStatus::Applied;break;
 }
 case LobbyAction::RemoveBot: {
  if(id!=host_){reply.reason="lobby.host_only";break;}
  auto found=players_.find(PlayerId(c.y));
  if(lobby_.started||found==players_.end()||!found->second.bot)break;
  players_.erase(found);for(auto&[pid,member]:players_)member.ready=member.bot;
  reply.status=CommandReplyStatus::Applied;break;
 }
 case LobbyAction::BotTeam: {
  if(id!=host_){reply.reason="lobby.host_only";break;}
  const int team=c.y%10;auto found=players_.find(PlayerId(c.y/10));
  if(lobby_.started||team<1||team>6||found==players_.end()||!found->second.bot)break;
  found->second.team=team;for(auto&[pid,member]:players_)member.ready=member.bot;
  reply.status=CommandReplyStatus::Applied;break;
 }
 case LobbyAction::Pause:
  if(!c.y&&id!=host_){reply.reason="lobby.host_only";break;}
  if(!lobby_.started||c.y<0||c.y>1)break;
  lobby_.paused=c.y!=0;lobby_.pause_author=c.y?id:0;reply.status=CommandReplyStatus::Applied;break;
 case LobbyAction::Speed:
  if(id!=host_){reply.reason="lobby.host_only";break;}
  if(!lobby_.started||(c.y!=1&&c.y!=2&&c.y!=4))break;
  lobby_.speed=c.y;reply.status=CommandReplyStatus::Applied;break;
 }
 if(reply.status==CommandReplyStatus::Applied){++revision_;reply.reason="lobby.updated";}
 return reply;
}

// Server-owned residents use the same bounded orders and physical delivery as humans.
void GameServer::tick_bots(){
 if(!lobby_.enabled||!lobby_.started||world_.ticks()%200!=0)return;
 std::set<int> visited;
 for(const auto&[id,p]:players_){
  if(!p.bot||!visited.insert(p.team).second)continue;
  const NationId nation=NationId(p.team);
  // In a shared team, the bot only manages residents explicitly transferred to it.
  const bool shared=std::any_of(players_.begin(),players_.end(),[&](const auto&v){return !v.second.bot&&v.second.team==p.team;});
  int home_x=0,home_y=0;bool camp=false,house=false,farm=false;std::optional<CellCoord> completed_farm;
  for(const auto&b:world_.buildings())if(b.nation==nation){if(b.kind==BuildingKind::Camp){home_x=b.x;home_y=b.y;camp=true;}house|=b.kind==BuildingKind::House;farm|=b.kind==BuildingKind::Farm;if(b.kind==BuildingKind::Farm&&b.progress>=1)completed_farm=CellCoord{b.x,b.y};}
  if(!camp)continue;
  if(!shared&&world_.ticks()%1200==0&&(!house||!farm)){
   const auto kind=!house?BuildingKind::House:BuildingKind::Farm;
   bool placed=false;for(int dy=-5;dy<=5&&!placed;++dy)for(int dx=-5;dx<=5&&!placed;++dx)
    if(world_.can_build(kind,home_x+dx,home_y+dy,WorldLayer::Surface,nation)){world_.build(kind,home_x+dx,home_y+dy,WorldLayer::Surface,nation);placed=true;}
  }
  const auto stock=world_.inventory(nation).stored;
  const Terrain wanted=stock.food<140?Terrain::Grass:stock.wood<120?Terrain::Forest:Terrain::Rock;
  std::vector<int> idle;
  for(const auto&r:world_.residents())if(r.nation==nation&&r.profession.assignment.profession!=Profession::Farmer&&r.job==Job::Idle&&r.order_phase==OrderPhase::Idle&&idle.size()<2){
   auto lease=control_leases_.find(r.id);
   const auto holder=lease==control_leases_.end()?players_.end():players_.find(lease->second.holder);
   if(shared&&(holder==players_.end()||!holder->second.bot))continue;
   if(holder!=players_.end()&&!holder->second.bot)continue;
   const bool active=std::any_of(world_.manual_orders().begin(),world_.manual_orders().end(),[&](const auto&o){return o.resident_id==r.id&&o.phase!=ManualOrderPhase::Complete&&o.phase!=ManualOrderPhase::Failed&&o.phase!=ManualOrderPhase::Cancelled;});
   if(!active&&world_.work_allowed(r.id,wanted==Terrain::Grass?Job::Forage:wanted==Terrain::Forest?Job::Woodcut:Job::Mine))idle.push_back(r.id);
  }
  if(!shared&&completed_farm&&!idle.empty()&&std::none_of(world_.residents().begin(),world_.residents().end(),[&](const auto&r){return r.nation==nation&&r.profession.assignment.profession==Profession::Farmer;})){
   ProfessionAssignment assignment;assignment.profession=Profession::Farmer;assignment.worksite=completed_farm;
   world_.assign_profession({idle.front()},assignment);idle.erase(idle.begin());
  }
  if(idle.empty())continue;
  bool issued=false;for(int radius=1;radius<=14&&!issued;++radius)for(int dy=-radius;dy<=radius&&!issued;++dy)for(int dx=-radius;dx<=radius&&!issued;++dx){
   CellCoord c{home_x+dx,home_y+dy};const auto&t=world_.tile(c.x,c.y);
   if(t.terrain!=wanted||t.amount<=0||!world_.explored(c,nation))continue;
   ManualOrderRequest order;order.kind=ManualOrderKind::Gather;order.target=c;order.gather_completion=GatherCompletion::OneDelivery;
   world_.issue_manual_order(idle,order,ManualQueueMode::Replace,ManualOrderIssuer{id,world_.ticks()+1});issued=true;
  }
 }
}

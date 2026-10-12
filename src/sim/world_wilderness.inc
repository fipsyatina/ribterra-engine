// Real actors, bounded jobs, and physical cargo. State rides the versioned economy envelope.
Expedition* expedition_for(int id) {
 for(auto&e:economy.wilderness.expeditions)if(expedition_active(e)&&std::find(e.members.begin(),e.members.end(),id)!=e.members.end())return &e;
 return nullptr;
}
bool expedition_worker(int id) const {return const_cast<Impl*>(this)->expedition_for(id)!=nullptr;}
CellCoord expedition_home(NationId nation=1) const {
 for(const auto&b:buildings)if(b.kind==BuildingKind::Camp&&b.nation==nation&&b.progress==1)return {b.x,b.y,b.layer};
 return {24,24};
}
static float wild_distance(float x,float y,float xx,float yy){return std::hypot(x-xx,y-yy);}
bool animal_visible(const Animal&a,NationId nation=1) const {
 for(auto e:entities()){
  const auto&p=registry.get<Position>(e);
  if(registry.get<Identity>(e).nation==nation&&registry.get<Vital>(e).health>0&&p.layer==WorldLayer::Surface&&wild_distance(p.x,p.y,a.x,a.y)<=4.5f)return true;
 }
 const auto home=expedition_home(nation);return wild_distance(float(home.x),float(home.y),a.x,a.y)<=10.5f;
}
Animal* animal(int id){for(auto&a:economy.wilderness.animals)if(a.id==id)return &a;return nullptr;}
void wild_route(int id,CellCoord target){
 auto e=find_resident(id);if(e==entt::null||registry.get<Vital>(e).health<=0||resident_busy(id)||registry.get<OrderStatus>(e).phase==OrderPhase::PathPending)return;
 if(!walkable(target)){
  bool found=false;for(int r=1;r<=3&&!found;++r)for(int dx=-r;dx<=r&&!found;++dx)for(int dy=-r;dy<=r&&!found;++dy)
   if(walkable({target.x+dx,target.y+dy,target.layer})){target.x+=dx;target.y+=dy;found=true;}
  if(!found)return;
 }
 const auto&p=registry.get<Position>(e);if(wild_distance(p.x,p.y,float(target.x),float(target.y))<.7f)return;
 queue_path(PathCommandKind::Move,{id},target,false,Job::Move);
}
void wild_return(Expedition&e,const char*reason){
 if(std::string_view(reason)=="night"&&e.phase==ExpeditionPhase::Processing)e.progress=std::max(1,e.progress);
 else e.progress=0;
 e.phase=ExpeditionPhase::Returning;e.outcome=reason;
 for(int id:e.members){auto actor=find_resident(id);if(actor!=entt::null&&!resident_busy(id))registry.get<Route>(actor)={};}
 ++economy.wilderness.revision;
}
void populate_wildlife(){
 auto&w=economy.wilderness;if(w.enabled)return;w.enabled=true;
 for(int attempt=0;attempt<6000&&w.animals.size()<96;++attempt){
  const auto home=expedition_home(economy.nations.empty()?NationId{1}:NationId(economy.nations[std::size_t(attempt)%economy.nations.size()].id));
  const auto h=mix(seed+unsigned(attempt)*7919u+240u);
  const int span=attempt<2500?100:600;
  int x=home.x+int(h%unsigned(span))-span/2,y=home.y+int((h>>12)%unsigned(span))-span/2;
  CellCoord c{x,y};if(!walkable(c)||wild_distance(float(x),float(y),float(home.x),float(home.y))<9)continue;
  auto b=tile(c).biome;
  if(b==Biome::Desert||b==Biome::Mountain||b==Biome::SnowMountain||b==Biome::Lake||b==Biome::SnowLake)continue;
  const bool forest=b==Biome::Forest||b==Biome::SnowForest;
  AnimalSpecies species=forest?AnimalSpecies((h>>24)%4):((h&1)?AnimalSpecies::Deer:AnimalSpecies::Hare);
  if((species==AnimalSpecies::Wolf||species==AnimalSpecies::Boar)&&wild_distance(float(x),float(y),float(home.x),float(home.y))<24)continue;
  int count=species==AnimalSpecies::Deer?3:1,herd=w.next_animal;
  for(int n=0;n<count&&w.animals.size()<96;++n){if(!walkable({x+n,y}))continue;
   int health=species==AnimalSpecies::Hare?8:species==AnimalSpecies::Boar?48:species==AnimalSpecies::Wolf?36:24;
   w.animals.push_back({w.next_animal++,herd,species,AnimalBehaviour::Feeding,float(x+n),float(y),float(x),float(y),health});
  }
 }
 ++w.revision;
}
void animal_step(Animal&a,float x,float y,float speed){
 const float d=wild_distance(a.x,a.y,x,y);if(d<.05f)return;
 const float nx=a.x+(x-a.x)/d*std::min(d,speed),ny=a.y+(y-a.y)/d*std::min(d,speed);
 if(walkable({int(std::round(nx)),int(std::round(ny))})){a.x=nx;a.y=ny;}
}
void process_animals(){
 auto&w=economy.wilderness;if(!w.enabled)return;
 const auto actors=entities();
 const bool winter=calendar_view(calendar_tick()).season==Season::Winter;
 for(auto&a:w.animals){
  if(a.health<=0){a.behaviour=AnimalBehaviour::Dead;continue;}
  entt::entity nearest=entt::null;float distance=8;
  for(auto actor:actors){
   const auto&p=registry.get<Position>(actor);if(p.layer!=WorldLayer::Surface||registry.get<Vital>(actor).health<=0)continue;
   const float d=wild_distance(a.x,a.y,p.x,p.y);if(d<distance){distance=d;nearest=actor;}
  }
  if(!alpha::animal_attacks&&a.behaviour==AnimalBehaviour::Attacking){a.target=0;a.next_attack=0;a.behaviour=AnimalBehaviour::Feeding;}
  if(alpha::animal_attacks&&a.behaviour==AnimalBehaviour::Attacking){
   auto victim=find_resident(a.target);
   if(victim==entt::null||registry.get<Vital>(victim).health<=0||registry.get<Position>(victim).layer!=WorldLayer::Surface){a.target=0;a.behaviour=AnimalBehaviour::Feeding;continue;}
   const auto&p=registry.get<Position>(victim);
   if(wild_distance(a.x,a.y,p.x,p.y)>18){a.target=0;a.behaviour=AnimalBehaviour::Feeding;continue;}
   animal_step(a,p.x,p.y,.14f);
   if(wild_distance(a.x,a.y,p.x,p.y)<1.2f&&ticks>=a.next_attack){
    a.next_attack=ticks+40;auto&v=registry.get<Vital>(victim);v.health=std::max(0,v.health-(a.species==AnimalSpecies::Boar?9:7));
    auto*party=expedition_for(a.target);
    if(party&&party->phase!=ExpeditionPhase::Defence&&party->phase!=ExpeditionPhase::Retreat){party->phase=ExpeditionPhase::Defence;party->target=a.id;party->outcome="attacked";++w.revision;event("wilderness.attacked #"+std::to_string(party->id));}
    else if(!party)event("wilderness.attacked_resident #"+std::to_string(a.target));
    // Nearby people defend themselves and their neighbours; town boundaries confer no immunity.
    for(auto defender:actors){
     const auto&dp=registry.get<Position>(defender);if(dp.layer!=WorldLayer::Surface||registry.get<Vital>(defender).health<=0||wild_distance(a.x,a.y,dp.x,dp.y)>2.5f)continue;
     int id=registry.get<Identity>(defender).id;auto*group=expedition_for(id);
     a.health=std::max(0,a.health-(group&&group->spears?6:2));
     if(!resident_busy(id))registry.get<Route>(defender)={};
    }
   }
   continue;
  }
  if(nearest!=entt::null&&distance<5){
   const auto&p=registry.get<Position>(nearest);int nearby=0;
   for(auto actor:actors){const auto&q=registry.get<Position>(actor);if(q.layer==WorldLayer::Surface&&registry.get<Vital>(actor).health>0&&wild_distance(a.x,a.y,q.x,q.y)<5)++nearby;}
   if(alpha::animal_attacks&&((a.species==AnimalSpecies::Boar&&(distance<1.7f||a.health<48))||(a.species==AnimalSpecies::Wolf&&nearby<=2&&distance<3.5f))){
    a.target=registry.get<Identity>(nearest).id;a.behaviour=AnimalBehaviour::Attacking;continue;
   }
   a.alarm_until=ticks+100;a.behaviour=AnimalBehaviour::Fleeing;
   for(auto&other:w.animals)if(other.herd==a.herd&&other.health>0){other.alarm_until=std::max(other.alarm_until,a.alarm_until);other.behaviour=AnimalBehaviour::Fleeing;}
   animal_step(a,a.x+(a.x-p.x)*4,a.y+(a.y-p.y)*4,a.species==AnimalSpecies::Hare?.24f:a.species==AnimalSpecies::Deer?.18f:.13f);continue;
  }
  if(a.alarm_until>ticks){a.behaviour=AnimalBehaviour::Fleeing;animal_step(a,a.home_x+10,a.home_y+8,.18f);continue;}
  if(alpha::animal_attacks&&a.species==AnimalSpecies::Wolf){
   Animal*prey=nullptr;float closest=7;
   for(auto&other:w.animals)if(other.health>0&&(other.species==AnimalSpecies::Hare||other.species==AnimalSpecies::Deer)){
    float d=wild_distance(a.x,a.y,other.x,other.y);if(d<closest){closest=d;prey=&other;}
   }
   if(prey){prey->alarm_until=ticks+80;animal_step(a,prey->x,prey->y,.19f);if(closest<1&&ticks%40==0)prey->health=std::max(0,prey->health-12);continue;}
  }
  const bool resting=(ticks/400+std::uint64_t(a.herd))%3==0;
  a.behaviour=resting?AnimalBehaviour::Resting:AnimalBehaviour::Feeding;
  const auto h=mix(unsigned(a.herd)+unsigned(ticks/400));
  const float range=winter?3.f:7.f;
  animal_step(a,a.home_x+(resting?0:(float(h%101)/50-1)*range),a.home_y+(resting?0:(float((h>>10)%101)/50-1)*range),winter?.022f:.035f);
 }
}
void process_expeditions(){
 if(!alpha::expeditions)return;
 auto&w=economy.wilderness;if(!w.enabled)return;
 for(auto&e:w.expeditions){
  const auto nation=resident_nation(e.members.front());const auto home=expedition_home(nation);
  if(!expedition_active(e))continue;
  std::vector<int> alive;for(int id:e.members){auto a=find_resident(id);if(a!=entt::null&&registry.get<Vital>(a).health>0)alive.push_back(id);}
  if(alive.empty()){e.phase=ExpeditionPhase::Complete;e.outcome="lost";if(e.spears)add_stack(ItemKind::Spear,e.spears,nation,{ItemLocationKind::Ground,{e.x,e.y},0});e.spears=0;++w.revision;continue;}
  bool home_now=true;for(int id:alive){const auto&p=registry.get<Position>(find_resident(id));if(p.layer!=home.layer||wild_distance(p.x,p.y,float(home.x),float(home.y))>4||resident_busy(id))home_now=false;}
  if(is_night(calendar_tick())&&e.night_permission!=calendar_day(calendar_tick())&&e.night_prompt!=calendar_day(calendar_tick())&&!home_now&&e.phase!=ExpeditionPhase::Returning&&e.phase!=ExpeditionPhase::Retreat&&e.phase!=ExpeditionPhase::Defence){
   e.night_prompt=calendar_day(calendar_tick());wild_return(e,"night");event("wilderness.night #"+std::to_string(e.id));
  }
  if(e.phase==ExpeditionPhase::Returning){
   if(home_now){
    if(e.completed<e.quota&&e.outcome.empty()&&ticks<e.deadline){e.phase=ExpeditionPhase::Travel;}
    else{e.phase=ExpeditionPhase::Complete;if(e.outcome.empty())e.outcome=e.completed?"delivered":"empty";
     if(e.spears)add_stack(ItemKind::Spear,e.spears,nation,{ItemLocationKind::Ground,home,0});e.spears=0;
     for(int id:alive){auto actor=find_resident(id);registry.get<Activity>(actor)={};registry.get<Route>(actor)={};registry.get<ProfessionState>(actor).manual_override=false;}
     ++w.revision;
    }
   }else if(ticks%40==0)for(int id:alive)wild_route(id,{home.x+2,home.y});
   continue;
  }
  if(e.phase==ExpeditionPhase::Retreat){if(ticks%40==0)for(int id:alive)wild_route(id,{e.x,e.y});bool reached=true;for(int id:alive){const auto&p=registry.get<Position>(find_resident(id));if(wild_distance(p.x,p.y,float(e.x),float(e.y))>2)reached=false;}if(reached)wild_return(e,"retreated");continue;}
  if(e.phase==ExpeditionPhase::Defence){
   auto*a=animal(e.target);if(!a||a->health<=0){wild_return(e,"defended");continue;}
   if(ticks%40==0)for(int id:alive){const auto&q=registry.get<Position>(find_resident(id));if(wild_distance(q.x,q.y,a->x,a->y)<2.5f)a->health=std::max(0,a->health-(e.spears?8:3));else wild_route(id,{int(std::round(a->x)),int(std::round(a->y))});}
   continue;
  }
  if(ticks>=e.deadline){wild_return(e,e.completed?"search_finished":"empty");continue;}
  if(is_night(calendar_tick())&&e.night_permission!=calendar_day(calendar_tick())){wild_return(e,"night");continue;}
  auto leader=find_resident(alive.front());const auto&p=registry.get<Position>(leader);
  if(e.phase==ExpeditionPhase::Travel){
   if(wild_distance(p.x,p.y,float(e.x),float(e.y))<=3){e.phase=ExpeditionPhase::Search;}
   else if(ticks%40==0)for(int id:alive)wild_route(id,{e.x,e.y});
   continue;
  }
  if(e.kind==ExpeditionKind::Explore){e.completed=e.quota;wild_return(e,"explored");continue;}
  if(e.kind==ExpeditionKind::Resources){
   // One finite load per actor, constrained to the chosen explored circle.
   if(ticks%40)continue;
   bool found=false;
   for(int dx=-e.radius;dx<=e.radius&&!found;++dx)for(int dy=-e.radius;dy<=e.radius&&!found;++dy){
    CellCoord c{e.x+dx,e.y+dy};auto*t=mutable_tile(c);
    if(dx*dx+dy*dy>e.radius*e.radius||!t||!t->explored||t->amount<=0||(t->terrain!=Terrain::Forest&&t->terrain!=Terrain::Rock))continue;
    found=true;for(int id:alive){if(e.completed>=e.quota)break;auto actor=find_resident(id);const auto&q=registry.get<Position>(actor);if(resident_busy(id))continue;
     if(wild_distance(q.x,q.y,float(c.x),float(c.y))<=1.5f){int amount=std::min({int(t->amount),e.quota-e.completed,10});if(amount>0&&gather_yield(actor,t->terrain==Terrain::Forest?ItemKind::Wood:ItemKind::Stone,unsigned(amount),Job::Idle,c)){t->amount-=amount;changed(c);e.completed+=amount;}}
     else wild_route(id,{c.x+1,c.y});
    }
   }
   if(!found||e.completed>=e.quota)wild_return(e,!found?"exhausted":"");continue;
  }
  auto*a=animal(e.target);
  if(e.phase==ExpeditionPhase::Processing){
   bool all_hunters_gathered=true;
   if(a)for(int id:alive){const auto&q=registry.get<Position>(find_resident(id));if(wild_distance(q.x,q.y,a->x,a->y)>2){all_hunters_gathered=false;if(ticks%40==0)wild_route(id,{int(std::round(a->x)),int(std::round(a->y))});}}
   if(!all_hunters_gathered)continue;
   if(++e.progress<160)continue;
   // A resident has one cargo slot. A second hunter carries hide; surplus stays physically on the ground.
   const CellCoord carcass{a?int(std::round(a->x)):e.x,a?int(std::round(a->y)):e.y};
   if(item_stacks.size()+2>=max_item_stacks||logistics_tasks.size()+2>=max_logistics_tasks)continue;
   const int meat=a&&a->species==AnimalSpecies::Hare?2:8;
   if(!gather_yield(leader,ItemKind::Meat,unsigned(meat),Job::Idle,carcass))continue;
   if(alive.size()>1){if(!gather_yield(find_resident(alive[1]),ItemKind::Hide,1,Job::Idle,carcass))add_stack(ItemKind::Hide,1,nation,{ItemLocationKind::Ground,carcass,0});}
   else add_stack(ItemKind::Hide,1,nation,{ItemLocationKind::Ground,carcass,0});
   ++e.completed;e.target=0;wild_return(e,e.specific_target?"delivered":"");continue;
  }
  if(!a||a->health<=0||wild_distance(a->x,a->y,float(e.x),float(e.y))>e.radius){
   if(e.specific_target&&e.target){wild_return(e,"escaped");continue;}
   e.target=0;e.phase=ExpeditionPhase::Search;
   for(auto&candidate:w.animals)if(candidate.health>0&&candidate.species!=AnimalSpecies::Wolf&&(!e.specific_target||candidate.id==e.specific_target)&&wild_distance(candidate.x,candidate.y,float(e.x),float(e.y))<=e.radius&&animal_visible(candidate)){e.target=candidate.id;break;}
   a=animal(e.target);
   if(!a){if(ticks%120==0){auto h=mix(unsigned(e.id)+unsigned(ticks/120));int dx=int(h%17)-8,dy=int((h>>12)%17)-8;for(int id:alive)wild_route(id,{e.x+dx,e.y+dy});}continue;}
  }
  e.phase=ExpeditionPhase::Approach;
  if(ticks%20==0)for(int id:alive){auto actor=find_resident(id);const auto&q=registry.get<Position>(actor);
   const bool armed=std::find(alive.begin(),alive.end(),id)-alive.begin()<e.spears;
   const float range=armed?6.f:1.4f;
   if(wild_distance(q.x,q.y,a->x,a->y)<=range){e.phase=ExpeditionPhase::Hunt;a->health=std::max(0,a->health-(armed?12:3));}
   else wild_route(id,{int(std::round(a->x)),int(std::round(a->y))});
  }
  if(a->health<=0){a->behaviour=AnimalBehaviour::Dead;e.phase=ExpeditionPhase::Processing;e.progress=0;for(int id:alive){auto actor=find_resident(id);registry.get<Route>(actor)={};registry.get<Activity>(actor)={};}}
 }
}

void World::create_nations(const std::vector<int>&teams,int slots,int population){
 auto&s=*impl_;if(slots<2||slots>6)throw std::invalid_argument("Invalid nation creation");
 std::set<int> unique(teams.begin(),teams.end());if(unique.empty()||*unique.begin()<1||*unique.rbegin()>6)throw std::invalid_argument("Invalid teams");
 if(s.economy.nations.empty()&&(s.ticks||!s.manual_orders.empty()||s.buildings.size()>1)){
  const auto home=s.expedition_home();s.economy.nations.push_back({1,home.x,home.y,{},std::string(nation_fog_size,'0')});
  for(const auto&chunk:s.chunks)for(std::size_t i=0;i<chunk.tiles.size();++i)if(chunk.tiles[i].explored)s.remember_cell({chunk.key.cx*chunk_size+int(i%chunk_size),chunk.key.cy*chunk_size+int(i/chunk_size),chunk.key.layer},1);
 }
 if(s.economy.nations.empty()){
 s.economy.wilderness={};
 s.registry.clear();s.resident_by_id.clear();s.buildings.clear();s.storages.clear();s.item_stacks.clear();s.portals.clear();s.next_building_id=1;s.next_item_stack_id=1;
 for(auto&chunk:s.chunks)for(auto&t:chunk.tiles)t.explored=false;
 }
 const double angle=double(s.seed%360)*3.141592653589793/180.;const double radius=250.;
 const WorldGenerationConfig generation{s.seed,s.terrain_generator,alpine_valley_profile_id,terrain_rules_version};
 auto prepare_site=[&](int reserved_x,int reserved_y,int team){
  // Keep each nation close to its evenly spaced reserved point, but avoid
  // founding a camp in a lake or on a cliff when nearby natural ground exists.
  int best_x=reserved_x,best_y=reserved_y;std::uint64_t best_score=UINT64_MAX;
  for(int oy=-24;oy<=24;oy+=4)for(int ox=-24;ox<=24;ox+=4){
   const int cx=reserved_x+ox,cy=reserved_y+oy;const auto center=surface_tile(cx,cy,generation);
   std::uint64_t score=std::uint64_t(ox*ox+oy*oy)*24+mix(s.seed^std::uint32_t(team)*2246822519U^
    std::uint32_t(cx)*73856093U^std::uint32_t(cy)*19349663U)%23;
   if(center.terrain==Terrain::Water)score+=1000000;
   else if(center.terrain==Terrain::Rock)score+=120000;
   else if(center.terrain==Terrain::Forest)score+=12000;
   for(int sy=-6;sy<=6;sy+=6)for(int sx=-6;sx<=6;sx+=6){
    const auto sample=surface_tile(cx+sx,cy+sy,generation);
    if(sample.terrain==Terrain::Water)score+=6000;
    score+=std::uint64_t(std::abs(int(sample.elevation)-int(center.elevation)))*12;
   }
   if(score<best_score){best_score=score;best_x=cx;best_y=cy;}
  }
  const auto foundation_elevation=surface_tile(best_x,best_y,generation).elevation;
  auto clear_land=[&](int cx,int cy){
   auto*t=s.mutable_tile({cx,cy});if(!t)return;
   const bool snow=snowy(t->biome);t->terrain=Terrain::Grass;t->amount=0;
   t->biome=snow?Biome::SnowMeadow:Biome::Meadow;t->elevation=foundation_elevation;s.changed({cx,cy});
  };
  // A small round clearing is sufficient for the camp and its residents.
  // The generated biome and elevation remain intact immediately outside it.
  for(int dy=-5;dy<=5;++dy)for(int dx=-5;dx<=5;++dx)if(dx*dx+dy*dy<=25)clear_land(best_x+dx,best_y+dy);

  struct Patch{int x{},y{};Terrain terrain{Terrain::Grass};int amount{};};
  std::vector<Patch> patches;patches.reserve(24);
  const auto layout=mix(s.seed^std::uint32_t(team)*3266489917U);
  const double phase=double(layout%360)*3.141592653589793/180.;
  const int type_shift=int((layout>>9)%3);
  for(int slot=0;slot<9;++slot){
   const auto variation=mix(layout^std::uint32_t(slot+1)*668265263U);
   const double jitter=(double(int(variation%25)-12))*3.141592653589793/180.;
   const double a=phase+double(slot)*2*3.141592653589793/9+jitter;
   const int distance=7+int((variation>>8)%2);
   const int px=best_x+int(std::lround(distance*std::cos(a)));
   const int py=best_y+int(std::lround(distance*std::sin(a)));
   const int kind=(slot+type_shift)%3;
   const auto terrain=kind==0?Terrain::Forest:kind==1?Terrain::Rock:Terrain::Grass;
   const int amount=(terrain==Terrain::Forest?16:terrain==Terrain::Rock?14:10)+int((variation>>16)%9);

   // Carve only the short final connection from the camp clearing. This makes
   // every guaranteed patch reachable without replacing the surrounding biome.
   int cx=best_x,cy=best_y,dx=std::abs(px-cx),sx=cx<px?1:-1,dy=-std::abs(py-cy),sy=cy<py?1:-1,err=dx+dy;
   for(;;){
    if(cx==px&&cy==py)break;
    if((cx-best_x)*(cx-best_x)+(cy-best_y)*(cy-best_y)>25)clear_land(cx,cy);
    const int e2=2*err;
    if(e2>=dy){err+=dy;cx+=sx;if(cx==px&&cy==py)break;if((cx-best_x)*(cx-best_x)+(cy-best_y)*(cy-best_y)>25)clear_land(cx,cy);}
    if(e2<=dx){err+=dx;cy+=sy;}
   }
   patches.push_back({px,py,terrain,amount});
   // Seeded, asymmetric patch shapes prevent every start from being a rotated
   // copy while keeping the guarantees compact and inside starting vision.
   const int extra=1+int((variation>>20)%2);
   for(int n=0;n<extra;++n){
    const int direction=int((variation>>(22+n*3))&7);static constexpr int nx[8]{1,1,0,-1,-1,-1,0,1};
    static constexpr int ny[8]{0,1,1,1,0,-1,-1,-1};
    patches.push_back({px+nx[direction],py+ny[direction],terrain,std::max(6,amount-3-n*2)});
   }
  }
  for(const auto&p:patches){
   auto*t=s.mutable_tile({p.x,p.y});if(!t)continue;const bool snow=snowy(t->biome);
   t->terrain=p.terrain;t->amount=p.amount;t->biome=snow?Biome::SnowMeadow:Biome::Meadow;t->elevation=foundation_elevation;s.changed({p.x,p.y});
  }
  return CellCoord{best_x,best_y};
 };
 // Six reserved sites are deterministic; unused sites stay available for late arrivals.
 for(int team:unique){
  if(s.nation_state(NationId(team)))continue;
  const double a=angle+double(team-1)*2*3.141592653589793/6;
  const int reserved_x=320+int(std::lround(radius*std::cos(a))),reserved_y=320+int(std::lround(radius*std::sin(a)));
  const auto site=prepare_site(reserved_x,reserved_y,team);const int x=site.x,y=site.y;
  s.economy.nations.push_back({team,x,y,{},std::string(nation_fog_size,'0')});
  const int initial_population=std::clamp(population,1,100);const auto food=s.initial_food(initial_population);
  const int camp=s.next_building_id++;s.buildings.push_back({camp,BuildingKind::Camp,x,y,1,WorldLayer::Surface,NationId(team)});s.storages.push_back({camp,s.initial_camp_capacity(food),7,0});
  s.add_stack(ItemKind::Wood,80,NationId(team),{ItemLocationKind::Storage,{},camp});s.add_stack(ItemKind::Stone,40,NationId(team),{ItemLocationKind::Storage,{},camp});s.add_stack(ItemKind::Food,food,NationId(team),{ItemLocationKind::Storage,{},camp});
  for(int i=0;i<initial_population;++i){int id=1;for(const auto&[existing,actor]:s.resident_by_id){(void)actor;id=std::max(id,existing+1);}auto e=s.registry.create();const int xx=x-3+i%7,yy=y-3+(i/7)%7;
   s.registry.emplace<Identity>(e,id,"Settler "+std::to_string(id),NationId(team));s.registry.emplace<Position>(e,float(xx),float(yy),CellCoord{xx,yy},WorldLayer::Surface);s.registry.emplace<Activity>(e);s.registry.emplace<Vital>(e);s.registry.emplace<Skills>(e);s.registry.emplace<ProfessionState>(e);s.registry.emplace<Route>(e);s.registry.emplace<OrderStatus>(e);s.resident_by_id.emplace(id,e);
  }
  s.reveal({x,y},starting_vision_radius,NationId(team));
 }
 s.populate_wildlife();s.cache_dirty=true;++s.topology_revision;
}

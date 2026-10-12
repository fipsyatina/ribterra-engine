NationState* nation_state(NationId id){for(auto&n:economy.nations)if(static_cast<NationId>(n.id)==id)return &n;return nullptr;}
const NationState* nation_state(NationId id)const{return const_cast<Impl*>(this)->nation_state(id);}
SettlementState& policy(NationId id){auto*n=nation_state(id);return id==1||!n?settlement:n->policy;}
const SettlementState& policy(NationId id)const{return const_cast<Impl*>(this)->policy(id);}
static std::size_t fog_cell(CellCoord c){return std::size_t(c.layer)*832*832+std::size_t(c.y-world_min)*832+std::size_t(c.x-world_min);}
static int fog_nibble(char c){return c<='9'?c-'0':c-'a'+10;}
bool explored_by(CellCoord c,NationId id)const{
 if(!inside_world(c.x,c.y)||!valid_layer(c.layer))return false;
 const auto*n=nation_state(id);if(!n)return economy.nations.empty()&&id==1?tile(c).explored:false;
 const auto bit=fog_cell(c);return (fog_nibble(n->explored[bit/4])&(1<<(bit%4)))!=0;
}
void remember_cell(CellCoord c,NationId id){
 if(auto*n=nation_state(id)){const auto bit=fog_cell(c);auto&v=n->explored[bit/4];v="0123456789abcdef"[fog_nibble(v)|(1<<(bit%4))];}
}
NationId resident_nation(int id)const{const auto e=find_resident(id);return e==entt::null?1:registry.get<Identity>(e).nation;}

#include "ribterra/world_state.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using namespace ribterra;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr std::uintmax_t output_limit = 128u * 1024u * 1024u;
constexpr std::uintmax_t free_space_required = 384u * 1024u * 1024u;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a-b) <= .00001f; }
Json cell_json(CellCoord c) { return {c.x,c.y,static_cast<unsigned>(c.layer)}; }
Json rows_json(const std::vector<OutdoorWorkRemainderState>& rows) {
    Json result=Json::array();
    for(const auto& r:rows) result.push_back({{"resident",r.resident_id},{"job",static_cast<int>(r.job)},
        {"target",cell_json(r.target)},{"building",r.building_id},{"remainder",r.remainder_bp}});
    return result;
}
int experience(const Resident& r, Skill skill) {
    const auto index=static_cast<std::size_t>(skill);
    return index==0u?r.wood_xp:index==1u?r.mining_xp:index==2u?r.farming_xp:r.additional_xp[index-3u];
}
Json practice_json(const Resident& r, Skill skill) {
    const auto& p=r.skill_practice[static_cast<std::size_t>(skill)];
    return {{"xp",experience(r,skill)},{"remainder",p.experience_remainder},
        {"familiarity_debt",p.familiarity_debt},{"last_tick",p.last_practice_tick}};
}
Skill job_skill(Job job) {
    return job==Job::Woodcut?Skill::Woodcutting:job==Job::Mine?Skill::Mining:
        job==Job::Build?Skill::Construction:Skill::Farming;
}
void passed(Json& ids, const char* id) { ids.push_back({{"id",id},{"passed",true}}); }
Tile& tile(WorldState& s, CellCoord c) {
    const auto found=std::find_if(s.chunks.begin(),s.chunks.end(),[&](const auto& chunk){return chunk.key==chunk_key(c);});
    check(found!=s.chunks.end(),"Fixture chunk missing");
    return found->tiles[local_index(c.x,c.y)];
}
void import(World& world, WorldState state) {
    std::string error;
    const bool accepted=world.import_state(std::move(state),error);
    check(accepted,"Fixture import: "+error);
}
World from_state(const WorldState& state) {
    World world(1,0);import(world,state);return world;
}
// Explicit diagnostic clock jump. Declared cover/history is retained, never
// replayed or silently repaired by the production import/load boundary.
void set_tick(WorldState& s,std::uint64_t tick) {
    const WeatherContext context{s.seed,{s.climate.algorithm_version,s.climate.climate_profile,s.climate.scenario}};
    const auto calendar=make_calendar_tick(tick,s.climate.calendar_offset_ticks);
    check(calendar.has_value(),"Fixture calendar overflow");
    WeatherScheduleCache cache(context,*calendar);
    auto climate=s.climate;climate.evaluated_simulation_tick=tick;climate.current=cache.sample(*calendar);
    validate_climate_runtime(climate,tick,context,cache);
    s.climate=std::move(climate);s.ticks=tick;
}
void normalize(Resident& r,std::uint64_t tick) {
    r.wood_xp=120;r.mining_xp=120;r.farming_xp=120;r.additional_xp.fill(120);r.abilities_bp.fill(0);
    r.skill_practice={};
    for(auto& p:r.skill_practice) p.last_practice_tick=tick;
    r.health=100;r.hunger=0;r.profession={};r.profession.assignment.automation_enabled=false;
    r.profession.wait_reason=ProfessionWaitReason::Stopped;r.profession.next_evaluation_tick=tick;
}
struct ColdCandidate { std::uint32_t seed{},day{};std::uint64_t first_tick{},checked_windows{};Json samples; };
ColdCandidate find_cold() {
    std::uint64_t windows{};
    for(std::uint32_t seed=0;seed<=4095u;++seed) {
        WeatherScheduleCache cache({seed,{}},CalendarTick{33u*ticks_per_day});
        for(std::uint32_t day=34;day<=37;++day) {
            const auto start=static_cast<std::uint64_t>(day-1u)*ticks_per_day;
            cache.move_to(CalendarTick{start});
            for(std::uint64_t in_day=0;in_day+20u<daylight_ticks;in_day+=600u) {
                ++windows;bool valid=true;
                for(std::uint64_t i=0;i<20u;++i) {
                    const auto sample=cache.sample(CalendarTick{start+in_day+i});
                    if(sample.temperature_deci_c>-200 || sample.outdoor_work_rate_bp!=8500u ||
                       sample.work_reason!=WeatherWorkReason::Cold) {valid=false;break;}
                }
                if(!valid)continue;
                Json samples=Json::array();
                for(std::uint64_t i=0;i<20u;++i) {
                    const auto t=CalendarTick{start+in_day+i};const auto s=cache.sample(t);
                    check(!is_night(t),"Cold candidate outside daytime");
                    samples.push_back({{"tick",t.value},{"temperature",s.temperature_deci_c},
                        {"rate",s.outdoor_work_rate_bp},{"reason",static_cast<unsigned>(s.work_reason)},
                        {"kind",static_cast<unsigned>(s.kind)},{"phase",static_cast<unsigned>(s.precipitation)},
                        {"period_start",s.period_start_calendar_tick},{"period_end",s.period_end_calendar_tick}});
                }
                return {seed,day,start+in_day,windows,std::move(samples)};
            }
        }
    }
    throw std::runtime_error("No real daytime <=-200 Cold8500 next20 candidate within seeds0..4095/days34..37");
}
void debit_delivered(WorldState& s,const Resources& cost) {
    for(const auto [kind,quantity]:std::array<std::pair<ItemKind,int>,3>{{
        {ItemKind::Wood,cost.wood},{ItemKind::Stone,cost.stone},{ItemKind::Food,cost.food}}}) {
        auto remaining=static_cast<std::uint32_t>(quantity);
        for(auto& stack:s.item_stacks) {
            if(stack.item!=kind || stack.location.kind!=ItemLocationKind::Storage)continue;
            const auto used=std::min(stack.quantity,remaining);stack.quantity-=used;remaining-=used;
        }
        check(remaining==0u,"Initial physical stock cannot cover authored delivery");
    }
    std::erase_if(s.item_stacks,[](const auto& stack){return stack.quantity==0u;});
    s.resources.wood-=cost.wood;s.resources.stone-=cost.stone;s.resources.food-=cost.food;
}
World fixture(const ColdCandidate& cold,Job job,WorldLayer layer=WorldLayer::Surface,int population=1) {
    World world(cold.seed,population);auto s=world.export_state();set_tick(s,cold.first_tick-1u);
    for(int y=17;y<=29;++y)for(int x=19;x<=29;++x)
        tile(s,{x,y,WorldLayer::Surface})={Terrain::Grass,0,true,Biome::Meadow,0};
    ++s.topology_revision;
    if(layer==WorldLayer::Mine) {
        const int mine_id=s.next_building_id++;
        s.buildings.push_back({mine_id,BuildingKind::Mine,28,24,1.f,WorldLayer::Surface,1u});
        s.portals.push_back({{28,24,WorldLayer::Surface},{28,24,WorldLayer::Mine}});
        tile(s,{28,24,WorldLayer::Mine})={Terrain::MineFloor,0,true};
    }
    int building{};
    if(job==Job::Build) {
        building=s.next_building_id++;const CellCoord site{26,26,WorldLayer::Surface};
        const auto cost=World::cost(BuildingKind::House);debit_delivered(s,cost);
        s.buildings.push_back({building,BuildingKind::House,26,26,.25f,WorldLayer::Surface,1u});
        s.construction_orders.push_back({building,BuildingKind::House,site,1u,cost,cost,ConstructionPhase::Work,0u});
    }
    for(std::size_t i=0;i<s.residents.size();++i) {
        auto& r=s.residents[i];normalize(r,s.ticks);
        const int y=19+static_cast<int>(i);
        const CellCoord target=job==Job::Build?CellCoord{26,26,layer}:CellCoord{22,y,layer};
        const CellCoord actor=job==Job::Build?CellCoord{25,26,layer}:CellCoord{21,y,layer};
        if(layer==WorldLayer::Mine)tile(s,actor)={Terrain::MineFloor,0,true};
        if(job!=Job::Build)tile(s,target)={job==Job::Woodcut?Terrain::Forest:job==Job::Mine?
            (layer==WorldLayer::Mine?Terrain::MineOre:Terrain::Rock):Terrain::Grass,64,true};
        r.x=static_cast<float>(actor.x);r.y=static_cast<float>(actor.y);r.layer=layer;
        r.target_x=target.x;r.target_y=target.y;r.job=job;r.progress=job==Job::Build?.25f:0.f;
        r.order_phase=OrderPhase::Active;r.path_failure=PathFailure::None;r.pending_path_batch=0;
        auto& route=s.routes[i];route.points.clear();route.cursor=0;route.work=target;
    }
    import(world,std::move(s));
    for(const auto& r:world.residents()) {
        const auto modifier=world.skill_breakdown(r.id,job_skill(job));
        check(modifier&&modifier->speed_bp==10000,"Normalized fixture speed differs from10000");
    }
    return world;
}
const OutdoorWorkRemainderState& single_row(const WorldState& s) {
    check(s.climate.outdoor_work_remainders.size()==1u,"Expected one genuine work row");
    return s.climate.outdoor_work_remainders.front();
}
float progress(const World& w,Job job) {
    if(job!=Job::Build)return w.residents().front().progress;
    const auto id=w.construction_orders().front().building_id;
    const auto b=std::find_if(w.buildings().begin(),w.buildings().end(),[&](const auto& item){return item.id==id;});
    check(b!=w.buildings().end(),"Work building missing");return b->progress;
}
Json twenty(World& w,Job job,bool underground) {
    Json steps=Json::array(),remainders=Json::array(),practices=Json::array();
    const auto skill=job_skill(job);const auto initial_practice=practice_json(w.residents().front(),skill);
    const float initial=progress(w,job);std::uint32_t expected_remainder{};unsigned advances{};
    for(unsigned i=0;i<20u;++i) {
        const auto before=practice_json(w.residents().front(),skill);const auto previous=progress(w,job);
        const auto total=expected_remainder+8500u;const bool expected=underground||total>=10000u;
        if(!underground)expected_remainder=total-(expected?10000u:0u);
        w.tick();const auto actual=progress(w,job);const auto state=w.export_state();
        check(w.weather().current.temperature_deci_c<=-200 && w.weather().current.outdoor_work_rate_bp==8500u &&
              w.weather().current.work_reason==WeatherWorkReason::Cold,"Actual tick left selected cold window");
        check((actual>previous)==expected,"Actual progress disagrees with fixed-point weather gate");
        if(expected)++advances;
        else check(practice_json(w.residents().front(),skill)==before,"Denied tick changed XP/practice");
        if(underground)check(state.climate.outdoor_work_remainders.empty(),"Underground work accumulated weather");
        else check(single_row(state).remainder_bp==expected_remainder,"Actual Activity remainder differs from literal math");
        steps.push_back(expected);remainders.push_back(underground?Json(nullptr):Json(expected_remainder));
        practices.push_back(practice_json(w.residents().front(),skill));
    }
    check(advances==(underground?20u:17u),"Twenty-tick advance count");
    const float nominal=job==Job::Build?skill_build_base_progress():skill_gather_base_progress();
    check(near(progress(w,job),initial+static_cast<float>(advances)*nominal),"Twenty-tick nominal progress");
    check(experience(w.residents().front(),skill)==120,"Short work incorrectly changed integer XP");
    const auto& practice=w.residents().front().skill_practice[static_cast<std::size_t>(skill)];
    check(practice.experience_remainder==(job==Job::Build?1700u:0u),"Actual useful-work credit differs from accepted steps");
    if(!underground) {
        check(remainders.at(0)==8500 && remainders.at(1)==7000 && remainders.at(2)==5500 &&
              remainders.at(3)==4000 && steps.at(0)==false && steps.at(1)==true && steps.at(2)==true &&
              steps.at(3)==true,"First-four literal Activity vector");
    }
    return {{"rate_bp",8500},{"eligible_ticks",20},{"advance_count",advances},{"baseline_speed_bp",10000},
        {"progress_before",initial},{"progress_after",progress(w,job)},
        {"first4_remainders",underground?Json::array():Json{8500,7000,5500,4000}},
        {"first4_advances",underground?Json{true,true,true,true}:Json{false,true,true,true}},
        {"useful_work_units",job==Job::Build?1700u:0u},{"integer_xp_before",120},{"integer_xp_after",120},
        {"no_outdoor_record",underground},
        {"steps",steps},{"remainders",remainders},{"initial_practice",initial_practice},{"practices",practices},
        {"rows",rows_json(w.export_state().climate.outdoor_work_remainders)}};
}
WorldState nonzero(const ColdCandidate& cold,Job job) {
    auto w=fixture(cold,job);w.tick();w.tick();auto s=w.export_state();
    check(single_row(s).remainder_bp==7000u,"Nonzero fixture was not generated by two actual work ticks");
    return s;
}
PathBatchState replan(WorldState& s,bool expired) {
    auto& r=s.residents.front();const auto target=*s.routes.front().work;
    PathBatchState batch;batch.path_batch_id=s.next_path_batch_id++;
    batch.command=PathCommandKind::Replan;batch.assigned_job=r.job;batch.to_adjacent=true;
    batch.resident_ids={r.id};batch.target=target;
    batch.enqueued_tick=expired?s.ticks-1200u:s.ticks;batch.deadline_tick=batch.enqueued_tick+1200u;
    check(s.topology_revision>0u,"Replan fixture requires prior topology revision");
    batch.search_topology_revision=s.topology_revision-1u;
    const CellCoord start{static_cast<int>(std::lround(r.x)),static_cast<int>(std::lround(r.y)),r.layer};
    PathRouteDraftState draft;draft.resident_id=r.id;draft.start=start;draft.start_x=r.x;draft.start_y=r.y;
    draft.reconstruction_cursor=start;batch.drafts.push_back(draft);
    return batch;
}
Json frozen_tick(World& w,Job job,unsigned ticks,const char* label) {
    const auto before=w.export_state();const auto rows=before.climate.outdoor_work_remainders;
    const auto old_progress=progress(w,job);const auto practice=practice_json(w.residents().front(),job_skill(job));
    for(unsigned i=0;i<ticks;++i) {
        w.tick();const auto current=w.export_state();
        check(current.climate.outdoor_work_remainders==rows,std::string(label)+" changed keyed remainder");
        check(progress(w,job)==old_progress&&practice_json(w.residents().front(),job_skill(job))==practice,
              std::string(label)+" advanced work/XP");
    }
    return {{"ticks",ticks},{"before_rows",rows_json(rows)},{"after_rows",rows_json(w.export_state().climate.outdoor_work_remainders)},
        {"progress",old_progress},{"practice",practice},{"checksum_after",w.checksum()}};
}
Json freeze_controls(const ColdCandidate& cold,Json& ids) {
    const auto source=nonzero(cold,Job::Woodcut);auto night=source;
    set_tick(night,(night.ticks/ticks_per_day)*ticks_per_day+daylight_ticks);
    auto w=from_state(night);check(w.calendar().night,"Night fixture is daytime");
    auto night_result=frozen_tick(w,Job::Woodcut,4,"Night");passed(ids,"WAIT-night");
    auto moving=source;moving.routes.front().points={{21,18,WorldLayer::Surface},{22,18,WorldLayer::Surface}};
    moving.routes.front().cursor=0;auto movement=from_state(moving);
    const auto old_x=movement.residents().front().x,old_y=movement.residents().front().y;
    auto movement_result=frozen_tick(movement,Job::Woodcut,4,"Movement");
    check(movement.residents().front().x!=old_x||movement.residents().front().y!=old_y,"Movement control did not move");
    movement_result["position_before"]={old_x,old_y};
    movement_result["position_after"]={movement.residents().front().x,movement.residents().front().y};passed(ids,"WAIT-movement");
    auto pending=source;auto& actor=pending.residents.front();actor.x=200;actor.y=200;
    const auto work=*pending.routes.front().work;
    // An explicitly authored connected region exceeds one real planner budget.
    // Keep the genuine resource/key; no selected seed's natural island shape
    // may make this positive wait control finish Unreachable on its first tick.
    for(int y=-20;y<=70;++y)for(int x=-20;x<=70;++x) {
        const CellCoord cell{x,y,WorldLayer::Surface};
        if(cell!=work)tile(pending,cell)={Terrain::Grass,0,true,Biome::Meadow,0};
    }
    ++pending.topology_revision;
    tile(pending,{200,200,WorldLayer::Surface})={Terrain::Grass,0,true,Biome::Meadow,0};
    auto batch=replan(pending,false);actor.order_phase=OrderPhase::PathPending;actor.pending_path_batch=batch.path_batch_id;
    pending.path_batches.push_back(batch);auto path=from_state(pending);
    auto path_result=frozen_tick(path,Job::Woodcut,1,"Pending path");
    check(path.residents().front().order_phase==OrderPhase::PathPending&&!path.export_state().path_batches.empty(),
          "Path wait control finished instead of remaining pending");
    path_result["batch_id"]=batch.path_batch_id;
    path_result["authored_connected_cells"]=91u*91u-1u;
    path_result["planner_budget_per_tick"]=path_work_units_per_tick;
    passed(ids,"WAIT-path");
    return {{"night",night_result},{"movement",movement_result},{"path",path_result}};
}
std::uint64_t issue_gather(World& w,CellCoord target) {
    const auto results=w.issue_manual_order({1},{ManualOrderKind::Gather,target,GatherCompletion::UntilExhausted},ManualQueueMode::Replace);
    check(results.size()==1u&&results.front().status==ResidentCommandStatus::Applied&&results.front().order_id!=0,
          "Actual manual Gather replacement rejected");return results.front().order_id;
}
unsigned until_nonzero(World& w) {
    for(unsigned i=0;i<20u;++i) {
        w.tick();const auto s=w.export_state();
        if(s.climate.outdoor_work_remainders.size()==1u&&s.climate.outdoor_work_remainders.front().remainder_bp!=0u)return i+1u;
    }
    throw std::runtime_error("Actual new manual Activity did not obtain nonzero remainder in bounded window");
}
Json reset_controls(const ColdCandidate& cold,Json& ids) {
    const auto source=nonzero(cold,Job::Woodcut);const auto before=rows_json(source.climate.outdoor_work_remainders);
    auto replacement=from_state(source);const auto order=issue_gather(replacement,*source.routes.front().work);
    check(replacement.export_state().climate.outdoor_work_remainders.empty(),"Same-key replacement retained previous accumulator");
    const auto replacement_checksum=replacement.checksum();const auto replacement_ticks=until_nonzero(replacement);
    const auto fresh=replacement.export_state();check(single_row(fresh).resident_id==1&&single_row(fresh).target==*source.routes.front().work,
          "Same-key replacement lost actual key");passed(ids,"RESET-same-key-replace");
    const auto cancelled=replacement.cancel_manual_order(order);
    check(cancelled.size()==1u&&cancelled.front().status!=ResidentCommandStatus::Rejected&&
          replacement.export_state().climate.outdoor_work_remainders.empty(),"Manual cancellation retained key");
    passed(ids,"RESET-manual-cancel");
    auto stopped=from_state(source);const auto stopping=stopped.stop({1});
    check(stopping.size()==1u&&stopping.front().status!=ResidentCommandStatus::Rejected&&
          stopped.residents().front().job==Job::Idle&&stopped.export_state().climate.outdoor_work_remainders.empty(),"Stop retained Activity key");
    passed(ids,"RESET-stop");
    const auto build_source=nonzero(cold,Job::Build);auto build=from_state(build_source);
    const int building=build.construction_orders().front().building_id;
    check(build.cancel_construction(building).accepted,"Actual construction cancellation rejected");
    check(build.export_state().climate.outdoor_work_remainders.empty()&&
          build.construction_orders().front().phase==ConstructionPhase::Cancelled,"Construction cancellation retained key");
    passed(ids,"RESET-construction-cancel");
    const Json witnesses=Json::array({
        {{"id","RESET-same-key-replace"},{"public_commands",1},{"actual_ticks_to_new_key",replacement_ticks},
            {"before_rows",before},{"rows_immediately_after_command",Json::array()},
            {"fresh_actual_rows",rows_json(fresh.climate.outdoor_work_remainders)}},
        {{"id","RESET-manual-cancel"},{"public_commands",1},{"actual_ticks_after_command",0},
            {"before_rows",rows_json(fresh.climate.outdoor_work_remainders)},{"after_rows",Json::array()}},
        {{"id","RESET-stop"},{"public_commands",1},{"actual_ticks_after_command",0},
            {"before_rows",before},{"after_rows",Json::array()}},
        {{"id","RESET-construction-cancel"},{"public_commands",1},{"actual_ticks_after_command",0},
            {"before_rows",rows_json(build_source.climate.outdoor_work_remainders)},{"after_rows",Json::array()}}});
    return {{"witnesses",witnesses},{"before",before},{"replacement_order",order},{"after_replacement_checksum",replacement_checksum},
        {"fresh_actual_rows",rows_json(fresh.climate.outdoor_work_remainders)},
        {"manual_cancel_rows",rows_json(replacement.export_state().climate.outdoor_work_remainders)},
        {"stop_rows",rows_json(stopped.export_state().climate.outdoor_work_remainders)},
        {"build_before",rows_json(build_source.climate.outdoor_work_remainders)},
        {"cancelled_building",building},{"build_after",rows_json(build.export_state().climate.outdoor_work_remainders)}};
}
Json earned_retry(const ColdCandidate& cold,Json& ids) {
    // Explicitly exhaust and later restore the diagnostic logistics ID limit;
    // progress is earned exclusively by actual ticks before the yield retry.
    auto w=fixture(cold,Job::Woodcut);auto s=w.export_state();
    // A declared partial-progress baseline makes the blocked yield land on the
    // genuine twenty-tick remainder0 boundary; every subsequent step is actual.
    s.residents.front().progress=.58f;
    s.next_logistics_task_id=std::numeric_limits<std::uint64_t>::max();import(w,s);
    unsigned work_ticks{};
    while(w.residents().front().progress<1.f&&work_ticks<200u){w.tick();++work_ticks;}
    check(w.residents().front().progress==1.f&&w.residents().front().job==Job::Woodcut,"Actual gather did not reach blocked earned yield");
    const auto earned=w.export_state();const auto row=single_row(earned);const auto practice=practice_json(w.residents().front(),Skill::Woodcutting);
    check(work_ticks==20u&&row.remainder_bp==0u,"Earned yield did not retain the genuine twenty-tick boundary remainder");
    w.tick();check(w.residents().front().progress==1.f&&w.export_state().climate.outdoor_work_remainders==earned.climate.outdoor_work_remainders&&
        practice_json(w.residents().front(),Skill::Woodcutting)==practice,"Blocked yield retry accumulated weather/XP");
    // Select the same actual canonical next-tick cold sample, retaining earned
    // progress and its genuine remainder0. This clock jump does not replay cover.
    auto retry=w.export_state();set_tick(retry,cold.first_tick-1u);
    retry.next_logistics_task_id=1u;import(w,retry);
    const auto before_amount=w.tile(22,19).amount;w.tick();
    check(w.weather().current.outdoor_work_rate_bp==8500u&&w.inventory().carried.wood==3&&
        w.tile(22,19).amount==before_amount-1&&w.residents().front().wood_xp==121&&
        w.export_state().climate.outdoor_work_remainders.empty(),"Earned yield was incorrectly gated by weather or lost physical credit");
    passed(ids,"YIELD-earned-retry");
    return {{"declared_partial_progress_before",.58f},{"actual_ticks_to_earned_progress",work_ticks},{"earned_progress",1.f},{"earned_row",rows_json({row})},
        {"blocked_retry_frozen",true},{"blocked_retry_ticks",1},{"successful_retry_ticks",1},
        {"explicit_restored_logistics_limit",1},{"retained_retry_remainder",0},
        {"would_deny_rate",8500},{"would_deny_from_zero",true},{"carried_wood",w.inventory().carried.wood},
        {"xp_after",w.residents().front().wood_xp},{"practice_before",practice},
        {"practice_after",practice_json(w.residents().front(),Skill::Woodcutting)}};
}
Json failure_controls(const ColdCandidate& cold,Json& ids) {
    auto s=nonzero(cold,Job::Woodcut);set_tick(s,(s.ticks/ticks_per_day)*ticks_per_day+daylight_ticks);
    auto batch=replan(s,true);s.residents.front().order_phase=OrderPhase::PathPending;
    s.residents.front().pending_path_batch=batch.path_batch_id;s.path_batches.push_back(batch);
    auto w=from_state(s);const auto before_progress=w.residents().front().progress;
    const auto before_practice=practice_json(w.residents().front(),Skill::Woodcutting);w.tick();
    const auto results=w.take_path_results();check(results.size()==1u&&results.front().path_batch_id==batch.path_batch_id&&
        !results.front().accepted&&results.front().failure==PathFailure::Timeout,"Correlated legacy Replan did not time out");
    check(w.residents().front().order_phase==OrderPhase::PathFailed&&w.residents().front().path_failure==PathFailure::Timeout&&
        w.export_state().climate.outdoor_work_remainders.empty()&&w.residents().front().progress==before_progress&&
        practice_json(w.residents().front(),Skill::Woodcutting)==before_practice,"Correlated failed Replan retained or spent key");
    passed(ids,"PATH-correlated-replan-timeout");
    auto live=from_state(nonzero(cold,Job::Woodcut));auto terrain=live.export_state();
    tile(terrain,{23,19,WorldLayer::Surface})={Terrain::Forest,64,true};import(live,terrain);
    issue_gather(live,{23,19,WorldLayer::Surface});const auto newer_ticks=until_nonzero(live);
    const auto newer=live.export_state();auto bad=newer;auto old=replan(bad,true);
    old.target={22,19,WorldLayer::Surface};bad.path_batches.push_back(old);
    const auto checksum=live.checksum();const auto weather=live.weather();std::string error;
    check(!live.import_state(std::move(bad),error)&&error=="Path resident reservation is invalid.","Obsolete legacy batch was admitted against newer Activity");
    check(live.checksum()==checksum&&live.weather()==weather&&
        live.export_state().climate.outdoor_work_remainders==newer.climate.outdoor_work_remainders,"Obsolete candidate changed newer Activity");
    passed(ids,"PATH-obsolete-candidate-rejected");
    return {{"timeout_batch",batch.path_batch_id},{"timeout_actual_ticks",1},{"newer_actual_ticks",newer_ticks},
        {"before_rows",rows_json(s.climate.outdoor_work_remainders)},
        {"after_rows",rows_json(w.export_state().climate.outdoor_work_remainders)},
        {"timeout_progress_unchanged",true},{"timeout_practice_unchanged",true},{"newer_rows",rows_json(newer.climate.outdoor_work_remainders)},
        {"obsolete_candidate_error",error},{"obsolete_candidate_unchanged",true},
        {"obsolete_branch_scope","candidate admission guard; impossible accepted legacy reservation mismatch is source-reviewed"}};
}
Json binding_rejects(const ColdCandidate& cold) {
    const auto gather=nonzero(cold,Job::Woodcut),build=nonzero(cold,Job::Build);
    auto twin=fixture(cold,Job::Woodcut,WorldLayer::Surface,2);twin.tick();const auto sorted=twin.export_state();
    check(sorted.climate.outdoor_work_remainders.size()==2u,"Two genuine rows required for ordering negative");
    Json results=Json::array();
    const auto reject=[&](const char* id,const WorldState& source,auto mutate) {
        auto w=from_state(source);auto bad=source;mutate(bad);
        check(source.ticks>0u&&!source.climate.outdoor_work_remainders.empty(),"Binding rejection must start from nonzero live work");
        const auto checksum=w.checksum();const auto weather=w.weather();std::string error;
        check(!w.import_state(std::move(bad),error)&&!error.empty(),std::string("Invalid row accepted: ")+id);
        const auto current=w.export_state();check(w.checksum()==checksum&&w.weather()==weather&&
            current.climate.outdoor_work_remainders==source.climate.outdoor_work_remainders,std::string("Rejected row mutated world: ")+id);
        results.push_back({{"id",id},{"simulation_tick",source.ticks},{"before_rows",rows_json(source.climate.outdoor_work_remainders)},
            {"rejected",true},{"unchanged",true},{"checksum",checksum},{"error",error}});
    };
    reject("BIND-resident-missing",gather,[](auto& s){s.climate.outdoor_work_remainders[0].resident_id=999;});
    reject("BIND-resident-zero",gather,[](auto& s){s.climate.outdoor_work_remainders[0].resident_id=0;});
    reject("BIND-job-unaffected",gather,[](auto& s){s.climate.outdoor_work_remainders[0].job=Job::Idle;});
    reject("BIND-job-live-mismatch",gather,[](auto& s){s.climate.outdoor_work_remainders[0].job=Job::Mine;});
    reject("BIND-target-live-mismatch",gather,[](auto& s){++s.climate.outdoor_work_remainders[0].target.x;});
    reject("BIND-target-layer",gather,[](auto& s){s.climate.outdoor_work_remainders[0].target.layer=WorldLayer::Mine;});
    reject("BIND-gather-building",gather,[](auto& s){s.climate.outdoor_work_remainders[0].building_id=1;});
    reject("BIND-build-id-zero",build,[](auto& s){s.climate.outdoor_work_remainders[0].building_id=0;});
    reject("BIND-build-id-missing",build,[](auto& s){s.climate.outdoor_work_remainders[0].building_id=999;});
    reject("BIND-build-id-live-mismatch",build,[](auto& s){s.climate.outdoor_work_remainders[0].building_id=1;});
    reject("BIND-duplicate",gather,[](auto& s){s.climate.outdoor_work_remainders.push_back(s.climate.outdoor_work_remainders.front());});
    reject("BIND-unsorted",sorted,[](auto& s){std::reverse(s.climate.outdoor_work_remainders.begin(),s.climate.outdoor_work_remainders.end());});
    reject("BIND-remainder-overflow",gather,[](auto& s){s.climate.outdoor_work_remainders[0].remainder_bp=10000;});
    reject("BIND-dead-actor",gather,[](auto& s){s.residents.front().health=0;});
    reject("BIND-route-missing",gather,[](auto& s){s.routes.front().work.reset();});
    reject("BIND-activity-live-mismatch",gather,[](auto& s){s.residents.front().job=Job::Move;});
    return results;
}
Json continuation(const fs::path& output,const ColdCandidate& cold,Job job,Json& ids) {
    auto w=from_state(nonzero(cold,job));const auto source=w.export_state();const auto before=w.checksum();
    const auto name=job==Job::Build?"work-build.save":"work-gather.save";const auto path=output/name;
    check(!fs::exists(path)&&!fs::exists(fs::path(path.string()+".tmp"))&&!fs::exists(fs::path(path.string()+".bak")),"Save output must be new");
    std::string error;const bool saved=w.save(path,error);check(saved,"Nonzero work save: "+error);World restored(1,0);
    const bool loaded=restored.load(path,error);check(loaded,"Nonzero work load: "+error);
    check(restored.checksum()==before&&restored.export_state().climate==source.climate,"Loaded key/remainder checksum differs");
    Json checksums=Json::array(),rows=Json::array();
    for(unsigned i=0;i<200u;++i) {
        w.tick();restored.tick();check(w.checksum()==restored.checksum()&&w.weather()==restored.weather(),"Actual save continuation diverged");
        check(w.export_state().climate==restored.export_state().climate,"Private climate/work remainder continuation differs");
        checksums.push_back(w.checksum());rows.push_back(rows_json(w.export_state().climate.outdoor_work_remainders));
    }
    passed(ids,job==Job::Build?"SAVE-build-200":"SAVE-gather-200");
    return {{"filename",name},{"source_tick",source.ticks},{"source_rows",rows_json(source.climate.outdoor_work_remainders)},
        {"source_checksum",before},{"continuation_ticks",200},{"checksum_same",true},{"checksums",checksums},{"work_rows",rows},
        {"final_checksum",w.checksum()},{"loaded_binding_exact",true},{"actual_save_load",true}};
}
std::uintmax_t output_bytes(const fs::path& output) {
    std::uintmax_t total{};
    for(const auto& entry:fs::directory_iterator(output)) {
        check(entry.is_regular_file()&&!entry.is_symlink(),"Unexpected output entry");total+=entry.file_size();
    }
    check(total<=output_limit,"Output exceeds128MiB");return total;
}
void publish(const fs::path& output,const Json& report) {
    const auto path=output/"weather-work-report.json";check(!fs::exists(path),"Refuse existing report");
    const auto text=report.dump(2)+"\n";check(output_bytes(output)+text.size()<=output_limit,"Report exceeds output bound");
    std::ofstream stream(path,std::ios::binary);check(bool(stream),"Report open failed");stream<<text;stream.close();
    check(bool(stream),"Report flush failed");
}
} // namespace

int main(int argc,char** argv) {
    Json report={{"schema",1},{"complete",false}};fs::path output;bool owns=false;const auto begin=Clock::now();
    try {
        check(argc==2,"Usage: WeatherWorkProbe output-directory");output=fs::absolute(argv[1]);
        check(!fs::exists(output)&&fs::is_directory(output.parent_path()),"Output directory must be fresh with existing parent");
        check(fs::space(output.parent_path()).available>=free_space_required,"At least384MiB free required");
        check(fs::create_directory(output),"Fresh output creation failed");owns=true;
        auto& d=report["deterministic"];d["controls"]=Json::array();auto& ids=d["controls"];
        const auto search_begin=Clock::now();const auto cold=find_cold();
        report["measured"]["cold_search_ms"]=std::chrono::duration<double,std::milli>(Clock::now()-search_begin).count();
        d["cold_candidate"]={{"seed",cold.seed},{"day",cold.day},{"calendar_tick",cold.first_tick},{"window_ticks",20},
            {"checked_windows",cold.checked_windows},{"seed_bound",4095},{"days",{34,35,36,37}},
            {"samples",cold.samples},{"scenario",0},{"history","explicit canonical time jump; cover history unreplayed"}};
        passed(ids,"DOMAIN-real-cold");
        for(const auto [job,name,id]:std::array<std::tuple<Job,const char*,const char*>,3>{{
            {Job::Woodcut,"woodcut","GATHER-woodcut20"},{Job::Mine,"mine","GATHER-mine20"},{Job::Forage,"forage","GATHER-forage20"}}}) {
            auto w=fixture(cold,job);d["surface"][name]=twenty(w,job,false);passed(ids,id);
        }
        auto build=fixture(cold,Job::Build);d["surface"]["build"]=twenty(build,Job::Build,false);passed(ids,"BUILD-cold20");
        auto mine=fixture(cold,Job::Mine,WorldLayer::Mine);d["mine"]=twenty(mine,Job::Mine,true);passed(ids,"MINE-underground20");
        d["freeze"]=freeze_controls(cold,ids);d["resets"]=reset_controls(cold,ids);
        d["earned_yield"]=earned_retry(cold,ids);d["failed_replan"]=failure_controls(cold,ids);
        d["invalid_controls"]=binding_rejects(cold);
        d["persistence"]["gather"]=continuation(output,cold,Job::Woodcut,ids);
        d["persistence"]["build"]=continuation(output,cold,Job::Build,ids);
        check(ids.size()==18u&&d["invalid_controls"].size()==16u,"Expected control coverage missing");
        d["scope"]="Actual World Activity ticks/public commands/save-load; explicitly authored fixtures, no replayed history";
        report["versions"]={{"save",9},{"protocol",protocol_version},{"rules",rules_version},{"generator",generator_version}};
        report["open_delivery"]={"full simulation performance/one-percent overhead","renderer/HUD native delivery",
            "weather FX and reference visuals","transport adversarial completion","full suite and two-PC/cross-network testing",
            "shipping winter seed certification","full engine completion"};
        report["measured"]["output_bytes_before_report"]=output_bytes(output);
        report["measured"]["total_ms"]=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        report["complete"]=true;publish(output,report);std::cout<<"weather work checks passed\n";return 0;
    } catch(const std::exception& error) {
        report["complete"]=false;report["error"]=error.what();std::cerr<<error.what()<<'\n';
        if(owns)try {publish(output,report);}catch(const std::exception& publication){std::cerr<<publication.what()<<'\n';}
        return 1;
    }
}

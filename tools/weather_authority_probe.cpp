#include "ribterra/client_session.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/protocol.hpp"
#include "ribterra/world_state.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace ribterra;
using namespace ribterra::net;
using Json = nlohmann::json;
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream),"Read failed: " + path.string());
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void write_new(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    check(!std::filesystem::exists(path),"Refuse existing output: " + path.string());
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    stream.close();
    check(bool(stream),"Write failed: " + path.string());
}
Json weather_json(const WeatherView& w) {
    Json forecasts=Json::array();
    for (const auto& day:w.forecast) {
        Json intervals=Json::array();
        for(std::size_t i=0;i<day.interval_count;++i) {
            const auto& p=day.intervals[i];
            intervals.push_back({p.start_tick_in_day,p.end_tick_in_day,static_cast<unsigned>(p.kind)});
        }
        forecasts.push_back({{"day",day.calendar_day},{"minimum",day.minimum_temperature_deci_c},
            {"maximum",day.maximum_temperature_deci_c},{"kind",static_cast<unsigned>(day.dominant_kind)},
            {"precipitation",static_cast<unsigned>(day.precipitation_summary)},
            {"storm",day.has_storm},{"intervals",intervals}});
    }
    const auto& s=w.current;
    return {{"known",w.known},{"algorithm",w.algorithm_version},
        {"profile",static_cast<unsigned>(w.climate_profile)},{"scenario",static_cast<unsigned>(w.scenario)},
        {"offset",w.calendar_offset_ticks},{"simulation_tick",w.evaluated_simulation_tick},
        {"calendar_tick",w.calendar_tick},{"kind",static_cast<unsigned>(s.kind)},
        {"phase",static_cast<unsigned>(s.precipitation)},{"temperature",s.temperature_deci_c},
        {"wind_direction",static_cast<unsigned>(s.wind_direction)},{"wind",s.wind_permille},
        {"intensity",s.precipitation_permille},{"rate",s.outdoor_work_rate_bp},
        {"reason",static_cast<unsigned>(s.work_reason)},{"period_start",s.period_start_calendar_tick},
        {"period_end",s.period_end_calendar_tick},{"snow",w.snow_cover_permille},
        {"wet",w.wetness_permille},{"wet_remainder",w.wetness_remainder},
        {"next_storm_start",w.next_storm_start_calendar_tick},
        {"next_storm_end",w.next_storm_end_calendar_tick},{"forecasts",forecasts}};
}
ClimateConfig winter_config() {
    ClimateConfig c;c.scenario=ClimateScenarioId::FirstWinterStorm;return c;
}
Json world_controls(const std::filesystem::path& output) {
    World standard(70421,10);
    World winter(70421,10,generator_version,winter_config());
    check(standard.ticks()==0 && standard.day()==1 && clock_time(standard.calendar().tick)=="06:00",
          "Standard calendar origin");
    check(winter.ticks()==0 && winter.day()==31 && winter.winter() &&
          clock_time(winter.calendar().tick)=="06:00","Winter offset leaked into simulation time");
    check(standard.inventory().stored.food==100 && winter.inventory().stored.food==1200,
          "Physical winter food provisioning");
    const auto initial=winter.export_state();
    check(initial.climate.snow_cover_permille==650 && initial.climate.snow_cover_remainder==0 &&
          initial.climate.wetness_permille==0 && initial.climate.wetness_remainder==0 &&
          initial.climate.outdoor_work_remainders.empty(),"Initial climate fields");
    Json rejects=Json::array();
    const auto reject=[&](std::string id,auto mutate) {
        auto state=winter.export_state();mutate(state);
        const auto checksum=winter.checksum();const auto climate=winter.weather();std::string error;
        check(!winter.import_state(std::move(state),error),"Invalid import accepted: "+id);
        check(winter.checksum()==checksum && winter.weather()==climate,"Invalid import changed live world: "+id);
        rejects.push_back({{"id",id},{"rejected",true},{"unchanged",true},{"error",error}});
    };
    reject("tick0-snow-history",[](auto& s){s.climate.snow_cover_permille=649;});
    reject("evaluated-tick",[](auto& s){s.climate.evaluated_simulation_tick=1;});
    reject("scenario-offset",[](auto& s){s.climate.calendar_offset_ticks=0;});
    reject("algorithm-version",[](auto& s){s.climate.algorithm_version=2;});
    reject("current-temperature",[](auto& s){++s.climate.current.temperature_deci_c;});
    reject("snow-range",[](auto& s){s.climate.snow_cover_permille=1001;});
    reject("wet-remainder-range",[](auto& s){s.climate.wetness_remainder=28800;});
    reject("missing-activity-key",[](auto& s){s.climate.outdoor_work_remainders.push_back(
        {1,Job::Woodcut,{22,19,WorldLayer::Surface},0,7500});});
    const WeatherContext context{70421,winter_config()};
    WeatherScheduleCache cache(context,winter.calendar().tick);
    auto expected=initialize_climate(context,cache);
    for(std::uint64_t tick=1;tick<=1201;++tick) {
        winter.tick();advance_climate_tick(expected,tick,cache);
        check(winter.weather()==cache.view(expected),"World climate single-tick authority");
    }
    check(winter.export_state().climate.snow_cover_remainder==expected.snow_cover_remainder,
          "World private snow remainder authority");
    const auto save=output/"authority-current.save";std::string error;
    check(winter.save(save,error),error);
    const auto prior=bytes(save);const auto saved_checksum=winter.checksum();
    winter.tick();check(winter.save(save,error),error);
    check(bytes(std::filesystem::path(save.string()+".bak"))==prior,"Backup lost previous valid primary");
    World restored(1,0);
    check(restored.load(save,error),error);
    check(restored.checksum()==winter.checksum() && restored.weather()==winter.weather(),"Save restore climate checksum");
    for(int i=0;i<200;++i) {
        winter.tick();restored.tick();
        check(restored.checksum()==winter.checksum() && restored.weather()==winter.weather(),"Restored cache continuation");
    }
    const auto continued_weather=weather_json(winter.weather());
    const auto continued_checksum=winter.checksum();
    const auto storm_sample_tick=cache.view(initial.climate).next_storm_start_calendar_tick-
        initial.climate.calendar_offset_ticks+600;
    while(winter.ticks()<storm_sample_tick)winter.tick();
    const auto storm_state=winter.export_state();
    check(storm_state.climate.current.kind==WeatherKind::Storm &&
          storm_state.climate.snow_cover_remainder!=0 && storm_state.climate.wetness_remainder!=0,
          "Actual first-storm save did not exercise both signed cover remainders");
    const auto storm_save=output/"authority-storm.save";
    check(winter.save(storm_save,error),error);
    World storm_restored(1,0);check(storm_restored.load(storm_save,error),error);
    check(storm_restored.export_state().climate==storm_state.climate &&
          storm_restored.checksum()==winter.checksum(),"Nonzero signed storm remainder restore");
    for(int i=0;i<200;++i) {
        winter.tick();storm_restored.tick();
        check(storm_restored.checksum()==winter.checksum() && storm_restored.weather()==winter.weather(),
              "Nonzero signed storm remainder continuation");
    }
    auto altered=winter.export_state();
    altered.climate.wetness_permille=static_cast<std::uint16_t>(altered.climate.wetness_permille==999?998:999);
    altered.climate.wetness_remainder=0;
    World changed(1,0);check(changed.import_state(altered,error),error);
    check(changed.checksum()!=winter.checksum(),"Climate wetness omitted from checksum");
    Json provisioning=Json::array();
    for(const auto population:{0,83,1500}) {
        World large(70421,population,generator_version,winter_config());
        const int food=std::max(100,population*120);
        const auto state=large.export_state();
        check(large.inventory().stored.food==food && state.storages.front().capacity==
              static_cast<std::uint32_t>(std::max(10000,120+food)),
              "Winter food/capacity boundary");
        provisioning.push_back({{"population",population},{"food",food},{"capacity",state.storages.front().capacity}});
    }
    return {{"standard_initial",weather_json(standard.weather())},{"winter_initial",weather_json(cache.view(initial.climate))},
        {"world_single_step_ticks",1201},{"restore_continuation_ticks",200},
        {"saved_checksum",saved_checksum},{"continued_checksum",continued_checksum},
        {"initial_food",1200},{"provisioning",provisioning},{"import_rejections",rejects},
        {"wetness_changes_checksum",true},{"continued_weather",continued_weather},
        {"actual_storm_save_tick",storm_sample_tick},
        {"actual_storm_snow_remainder",storm_state.climate.snow_cover_remainder},
        {"actual_storm_wet_remainder",storm_state.climate.wetness_remainder},
        {"actual_storm_weather",weather_json(storm_restored.weather())},
        {"nonzero_signed_remainder_continuation_ticks",200}};
}
Json persistence_controls(const std::filesystem::path& output) {
    World world(70421,0);std::string error;
    const auto source=output/"guard-source.save";
    check(world.save(source,error),error);const auto valid=bytes(source);
    Json cases=Json::array();
    const auto guarded=[&](const std::string& id,const std::vector<std::uint8_t>& primary,
                          const std::optional<std::vector<std::uint8_t>>& backup,
                          const char* collision) {
        const auto path=output/(id+".save");write_new(path,primary);
        const auto bak=std::filesystem::path(path.string()+".bak");
        if(backup)write_new(bak,*backup);
        if(collision)write_new(std::filesystem::path(path.string()+collision),{42,43,44});
        check(!world.save(path,error),"Guard allowed overwrite: "+id);
        check(bytes(path)==primary && (!backup || bytes(bak)==*backup),"Guard changed prior files: "+id);
        if(collision)check(bytes(std::filesystem::path(path.string()+collision))==std::vector<std::uint8_t>({42,43,44}),"Collision bytes changed");
        check(collision || (!std::filesystem::exists(path.string()+".tmp") &&
              !std::filesystem::exists(path.string()+".bak.tmp")),"Rejected target created stage");
        cases.push_back({{"id",id},{"rejected",true},{"unchanged",true},{"error",error}});
    };
    auto old=valid;old[12]=8;old[13]=old[14]=old[15]=0;
    guarded("old-primary",old,std::nullopt,nullptr);
    guarded("old-backup",valid,old,nullptr);
    auto malformed=valid;malformed.back()^=1;
    guarded("malformed-current-backup",valid,malformed,nullptr);
    guarded("stage-collision",valid,std::nullopt,".tmp");
    guarded("backup-stage-collision",valid,std::nullopt,".bak.tmp");
    World loader(99,0);const auto unchanged=loader.checksum();
    check(!loader.load(output/"old-primary.save",error) && error=="save.climate_schema_required" &&
          loader.checksum()==unchanged,"Old load allocated/published incompatible world");
#ifdef _WIN32
    const auto locked=output/"locked-primary.save";write_new(locked,valid);
    const auto locked_backup=std::filesystem::path(locked.string()+".bak");write_new(locked_backup,valid);
    const auto handle=CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    check(handle!=INVALID_HANDLE_VALUE,"Could not establish real primary delete-share lock");
    world.tick();const bool saved=world.save(locked,error);CloseHandle(handle);
    check(!saved && bytes(locked)==valid && bytes(locked_backup)==valid &&
          std::filesystem::exists(locked.string()+".tmp") &&
          std::filesystem::exists(locked.string()+".bak.tmp"),"Failed primary publication lost prior generation/stages");
    cases.push_back({{"id","locked-primary-promotion"},{"rejected",true},{"unchanged",true},{"stages_retained",true},{"error",error}});
#endif
    return {{"guards",cases},{"old_load_rejected_before_decode",true}};
}
Json transport_controls() {
    ServerConfig config;config.population=0;config.climate=winter_config();
    GameServer server(config);const auto joined=server.join("Weather host",{});check(joined.accepted,joined.reason);
    auto source=server.snapshot(joined.player);const auto id=server.world().export_state().world_id;
    check(source.weather==server.world().weather() && source.day==31 && source.winter,"Host snapshot weather authority");
    auto full=make_snapshot(source,id);std::string error;
    auto decoded=decode(encode(Packet{1,full}),error);check(decoded.has_value(),error);
    ViewSnapshot shown;WorldId shown_id{};
    check(apply_snapshot(shown,shown_id,std::get<Snapshot>(decoded->payload),error),error);
    check(shown.weather==source.weather,"Full weather roundtrip");
    server.tick();auto next=server.snapshot(joined.player);auto delta=make_snapshot(next,id,&source);
    check(!delta.full && delta.weather==next.weather,"Delta must include complete WeatherView");
    decoded=decode(encode(Packet{2,delta}),error);check(decoded.has_value(),error);
    check(apply_snapshot(shown,shown_id,std::get<Snapshot>(decoded->payload),error) && shown.weather==next.weather,
          "Delta weather replacement");
    // Real valid bulk metadata guarantees multiple parts, rather than weakening
    // domain validation to manufacture a malformed-weather mismatch.
    const auto world_chunks=server.world().export_state().chunks;
    for(const auto& chunk:world_chunks) {
        if(chunk.key.layer!=WorldLayer::Surface)continue;
        if(std::none_of(source.chunks.begin(),source.chunks.end(),[&](const auto& c){return c.key==chunk.key;})) {
            auto diagnostic_chunk=chunk;
            for(auto& tile:diagnostic_chunk.tiles)tile.explored=true;
            source.chunks.push_back(std::move(diagnostic_chunk));
        }
        if(source.chunks.size()>=40)break;
    }
    full=make_snapshot(source,id);auto parts=split_snapshot(full);check(parts.size()>1,"Split fixture is not multipart");
    const auto untouched=shown.weather;const auto untouched_revision=shown.revision;
    SnapshotAssembler assembler;
    check(assembler.accept(shown,shown_id,parts[0],error)==AssemblyResult::Pending &&
          shown.weather==untouched && shown.revision==untouched_revision,"Partial weather leaked");
    auto legal_mismatch=parts[1];++legal_mismatch.weather.snow_cover_permille;
    validate_weather_view(legal_mismatch.weather,legal_mismatch.tick);
    check(assembler.accept(shown,shown_id,legal_mismatch,error)==AssemblyResult::Rejected &&
          shown.weather==untouched && shown.revision==untouched_revision,"Valid split weather mismatch accepted");
    auto now=std::chrono::steady_clock::time_point{};
    SnapshotAssembler timeout([&]{return now;});
    check(timeout.accept(shown,shown_id,parts[0],error)==AssemblyResult::Pending,"Timeout fixture start");
    now+=std::chrono::seconds(11);
    check(timeout.expired() && shown.weather==untouched,"Expired assembly leaked weather");
    assembler.reset();shown={};shown_id={};std::size_t maximum_packet=0;
    for(std::size_t i=0;i<parts.size();++i) {
        const auto packet=encode(Packet{static_cast<std::uint64_t>(i+10),parts[i]});
        maximum_packet=std::max(maximum_packet,packet.size());decoded=decode(packet,error);check(decoded.has_value(),error);
        const auto result=assembler.accept(shown,shown_id,std::get<Snapshot>(decoded->payload),error);
        check(result==(i+1==parts.size()?AssemblyResult::Applied:AssemblyResult::Pending),error);
    }
    check(shown.weather==source.weather && maximum_packet<=max_packet_bytes,"Multipart weather roundtrip/packet cap");
    ServerConfig lobby_config=config;lobby_config.lobby_enabled=true;
    GameServer lobby(lobby_config);const auto player=lobby.join("Lobby host",{});
    check(player.accepted && lobby.snapshot(player.player).weather==lobby.world().weather(),"Waiting-lobby weather omitted");
    return {{"full_roundtrip",true},{"delta_replacement",true},{"split_parts",parts.size()},
        {"maximum_encoded_packet_bytes",maximum_packet},{"legal_weather_header_mismatch_rejected",true},
        {"partial_unchanged",true},{"timeout_unchanged",true},{"waiting_lobby_weather",true},
        {"full_weather",weather_json(source.weather)},{"delta_weather",weather_json(next.weather)}};
}
Json client_controls() {
    ClientSession local(70421,"Local",winter_config());
    check(local.weather().known && local.calendar() && local.day()==31 && local.winter(),"Local calendar API");
    local.advance();check(local.weather().evaluated_simulation_tick==local.ticks(),"Local weather stale after advance");
    HostConfig config;config.game.population=0;config.game.climate=winter_config();
    config.transport.bind_address="127.0.0.1";
    std::unique_ptr<NetworkServer> host;
    for(std::uint16_t port=29920;port<29940 && !host;++port) {
        config.transport.port=port;auto candidate=std::make_unique<NetworkServer>(config);
        if(candidate->listen())host=std::move(candidate);
    }
    check(bool(host),"No available loopback host port");
    ClientSession remote(1,"Remote");std::string error;
    check(remote.connect_remote("127.0.0.1:"+std::to_string(host->port()),"",error),error);
    check(!remote.weather().known && !remote.calendar() && remote.day()==0,"Remote invented weather before snapshot");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!remote.world_ready() && std::chrono::steady_clock::now()<deadline) {
        host->poll();remote.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(remote.world_ready() && remote.weather()==host->game().world().weather() && remote.day()==31,
          "Actual loopback host weather differs from presentation");
    const auto joined=weather_json(remote.weather());
    host->shutdown();
    const auto disconnect_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(remote.ready() && std::chrono::steady_clock::now()<disconnect_deadline) {
        remote.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(!remote.ready() && !remote.weather().known && !remote.calendar(),"Disconnected client exposed cached weather");
    return {{"local_typed_calendar",true},{"remote_before_snapshot_unknown",true},
        {"actual_loopback_same_host_weather",true},{"remote_disconnected_unknown",true},{"joined_weather",joined}};
}
} // namespace
int main(int argc,char** argv) {
    Json report={{"schema",1},{"complete",false}};std::filesystem::path output;bool owns=false;
    try {
        check(argc==2,"Usage: WeatherAuthorityProbe output-directory");output=std::filesystem::absolute(argv[1]);
        check(!std::filesystem::exists(output),"Output directory must be new");
        std::filesystem::create_directory(output);owns=true;
        report["world"]=world_controls(output);report["persistence"]=persistence_controls(output);
        report["transport"]=transport_controls();report["client"]=client_controls();
        report["versions"]={{"protocol",protocol_version},{"rules",rules_version},{"generator",generator_version},{"save",9}};
        report["open_delivery"]={"actual Activity lifecycle and underground controls","full suite and production renderer/HUD build",
            "first-winter seed certification","weather FX and references","two-PC/cross-network testing","full simulation performance"};
        report["complete"]=true;
    } catch(const std::exception& error) {report["error"]=error.what();std::cerr<<error.what()<<'\n';}
    if(owns) {
        const auto text=report.dump(2)+"\n";write_new(output/"weather-authority-report.json",{text.begin(),text.end()});
    }
    if(report["complete"]==true) {std::cout<<"weather authority checks passed\n";return 0;}
    return 1;
}

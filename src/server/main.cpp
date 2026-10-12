#include "ribterra/network_session.hpp"
#include "ribterra/platform.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted=0;
void stop(int){interrupted=1;}
std::uint64_t number(std::string_view text,std::uint64_t maximum){
    std::uint64_t value{};auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size() || value>maximum)throw std::invalid_argument("Invalid numeric option");
    return value;
}
}
int main(int argc,char** argv){
    try{
        ribterra::net::HostConfig config;
        config.game.wildlife_enabled=true;
        std::uint64_t limit=0;
        std::filesystem::path ready_file;
        if(auto password=ribterra::environment_variable("RIBTERRA_SERVER_PASSWORD"))config.password=*password;
        const auto arguments=ribterra::utf8_arguments(argc,argv);
        for(std::size_t i=1;i<arguments.size();++i){
            const std::string option=arguments[i];
            if(option=="--help"){
                std::cout<<"RibterraServer --bind 127.0.0.1 --port 27020 --world path --ticks N --ready-file path\n"
                           "Optional password: RIBTERRA_SERVER_PASSWORD environment variable. Ctrl+C saves and exits.\n";return 0;
            }
            if(i+1>=arguments.size())throw std::invalid_argument("Missing option value");
            const std::string value=arguments[++i];
            if(option=="--bind")config.transport.bind_address=value;
            else if(option=="--port"){
                config.transport.port=static_cast<std::uint16_t>(number(value,65535));
                if(!config.transport.port)throw std::invalid_argument("GNS requires an explicit port between 1 and 65535");
            }
            else if(option=="--world")config.save_path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
            else if(option=="--seed")config.game.seed=static_cast<std::uint32_t>(number(value,UINT32_MAX));
            else if(option=="--population")config.game.population=static_cast<int>(number(value,1500));
            else if(option=="--players")config.game.max_players=static_cast<std::size_t>(number(value,6));
            else if(option=="--lobby")config.game.lobby_enabled=number(value,1)!=0;
            else if(option=="--ticks")limit=number(value,1000000000);
            else if(option=="--ready-file")ready_file=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
            else throw std::invalid_argument("Unknown server option: "+option);
        }
        ribterra::net::NetworkServer server(config);
        if(!server.listen())throw std::runtime_error(server.last_error());
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        nlohmann::json ready={{"status","listening"},{"port",server.port()},{"bind",config.transport.bind_address},{"protocol",ribterra::protocol_version}};
        const auto destination=server.save_path().u8string();
        ready["save_path"]=std::string(reinterpret_cast<const char*>(destination.data()),destination.size());
        if(server.game().last_load().copied)ready["migration"]={{"source_version",server.game().last_load().source_version()},{"original_retained",true},{"normal_rations",server.game().last_load().normalized_rations},{"attacks_cleared",server.game().last_load().neutralized_attacks}};
        if(!ready_file.empty()){
            if(ready_file.has_parent_path())std::filesystem::create_directories(ready_file.parent_path());
            std::ofstream output(ready_file);output<<ready.dump();if(!output)throw std::runtime_error("Cannot write ready file");
        }
        std::cout<<ready.dump()<<std::endl;
        auto next=std::chrono::steady_clock::now();std::uint64_t ticks=0;
        while(!interrupted && (!limit || ticks<limit)){
            server.poll();auto now=std::chrono::steady_clock::now();int budget=0;
            while(now>=next && budget++<4 && (!limit || ticks<limit)){
                server.tick();++ticks;next+=std::chrono::milliseconds(50);
            }
            if(now-next>std::chrono::seconds(1))next=now;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(!server.save())throw std::runtime_error(server.last_error());
        std::cout<<nlohmann::json{{"status","stopped"},{"ticks",server.game().world().ticks()},{"checksum",server.game().world().checksum()}}.dump()<<std::endl;
        server.shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<"RibterraServer: "<<e.what()<<'\n';return 1;}
}

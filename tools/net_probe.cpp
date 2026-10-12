#include "ribterra/network_session.hpp"
#include "ribterra/platform.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

int main(int argc, char **argv) {
    try {
        ribterra::net::ClientConfig config;
        std::string endpoint = "127.0.0.1:27020", chat;
        bool build = false;
        if (auto password = ribterra::environment_variable("RIBTERRA_SERVER_PASSWORD"))
            config.password = *password;
        const auto arguments = ribterra::utf8_arguments(argc, argv);
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            const std::string option = arguments[i];
            if (option == "--build") {
                build = true;
                continue;
            }
            if (i + 1 >= arguments.size())
                throw std::invalid_argument("Missing option value");
            const std::string value = arguments[++i];
            if (option == "--endpoint")
                endpoint = value;
            else if (option == "--name")
                config.name = value;
            else if (option == "--chat")
                chat = value;
            else
                throw std::invalid_argument("Unknown probe option");
        }
        ribterra::net::NetworkClient client(config);
        if (!client.connect(endpoint))
            throw std::runtime_error(client.last_error());
        const auto start = std::chrono::steady_clock::now();
        bool sent = false, terminal_reply = !build, chat_received = chat.empty();
        bool saw_reserved = false, saw_carried = false, saw_delivery = false;
        int wood_before_delivery = -1, available_after_reservation = -1;
        std::string terminal_status = "none";
        std::uint64_t request = 0;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(15)) {
            client.poll();
            if (client.ready() && !sent) {
                sent = true;
                if (build) {
                    ribterra::GameCommand command;
                    command.type = ribterra::CommandType::Build;
                    command.building = ribterra::BuildingKind::House;
                    command.x = 26;
                    command.y = 24;
                    request = client.command(command);
                }
                if (!chat.empty())
                    client.chat(chat);
            }
            for (const auto &reply : client.take_replies())
                if (reply.request_id == request &&
                    (reply.status == ribterra::CommandReplyStatus::Applied ||
                     reply.status == ribterra::CommandReplyStatus::Rejected)) {
                    terminal_reply = true;
                    terminal_status = reply.status == ribterra::CommandReplyStatus::Applied
                                          ? "applied"
                                          : "rejected";
                }
            for (const auto &entry : client.take_chat())
                if (entry.text == chat)
                    chat_received = true;
            const auto &view = client.view();
            if (view.inventory.reserved.wood >=
                ribterra::World::cost(ribterra::BuildingKind::House).wood) {
                saw_reserved = true;
                if (wood_before_delivery < 0) {
                    wood_before_delivery = view.inventory.total.wood;
                    available_after_reservation = view.inventory.available.wood;
                }
            }
            saw_carried =
                saw_carried || view.inventory.carried.wood > 0 || view.inventory.carried.stone > 0;
            for (const auto &site : view.construction_sites)
                if (site.site == ribterra::CellCoord{26, 24, ribterra::WorldLayer::Surface})
                    saw_delivery =
                        saw_delivery || site.delivered.wood > 0 || site.delivered.stone > 0;
            const bool construction_completed =
                std::any_of(view.buildings.begin(), view.buildings.end(), [](const auto &building) {
                    return building.kind == ribterra::BuildingKind::House && building.x == 26 &&
                           building.y == 24 && building.layer == ribterra::WorldLayer::Surface &&
                           building.progress == 1.F;
                });
            if (client.ready() && view.tick >= 20 && terminal_reply && chat_received &&
                (!build || construction_completed)) {
                std::cout << nlohmann::json{{"status", "synchronized"},
                                            {"player", client.player()},
                                            {"tick", client.view().tick},
                                            {"revision", client.view().revision},
                                            {"residents", client.view().residents.size()},
                                            {"buildings", client.view().buildings.size()},
                                            {"wood", client.view().resources.wood},
                                            {"wood_available", view.inventory.available.wood},
                                            {"wood_reserved", view.inventory.reserved.wood},
                                            {"wood_total_before_delivery", wood_before_delivery},
                                            {"wood_available_after_reservation",
                                             available_after_reservation},
                                            {"saw_reserved_materials", saw_reserved},
                                            {"saw_carried_materials", saw_carried},
                                            {"saw_delivery", saw_delivery},
                                            {"construction_completed", construction_completed},
                                            {"command_reply_status", terminal_status},
                                            {"chat", chat_received},
                                            {"command_reply_terminal", terminal_reply}}
                                 .dump()
                          << '\n';
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Probe timeout: " + client.last_error());
    } catch (const std::exception &e) {
        std::cerr << "RibterraNetProbe: " << e.what() << '\n';
        return 1;
    }
}

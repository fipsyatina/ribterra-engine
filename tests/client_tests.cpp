#include "ribterra/unit_animation.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/ground_cover.hpp"
#include "ribterra/shore_details.hpp"
#include "ribterra/resource_shrub.hpp"
#include "ribterra/relief_details.hpp"
#include "ribterra/minimap_projection.hpp"
#include "ribterra/network_session.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/terrain_rules.hpp"
#include "ribterra/view.hpp"
#include "ribterra/world.hpp"
#include "ribterra/world_state.hpp"
#include "work_projection_fixture.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void key(ribterra::GameView &view, ribterra::ClientSession &world, SDL_Keycode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    view.handle_event(event, world);
}
void click(ribterra::render::Renderer2D &renderer, SDL_Window *window, ribterra::GameView &view,
           ribterra::ClientSession &world, SDL_FPoint logical, Uint8 button) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = button;
    event.button.windowID = SDL_GetWindowID(window);
    const auto physical = renderer.logical_to_window({logical.x, logical.y});
    event.button.x = physical.x;
    event.button.y = physical.y;
    renderer.convert_event(event);
    check(std::abs(event.button.x - logical.x) < .1f && std::abs(event.button.y - logical.y) < .1f,
          "window event must roundtrip to logical coordinates");
    view.handle_event(event, world);
}
ribterra::CommandReply await_terminal(ribterra::ClientSession &session) {
    for (int tick = 0; tick < 1202; ++tick) {
        session.advance();
        if (auto reply = session.take_command_reply();
            reply && (reply->status == ribterra::CommandReplyStatus::Applied ||
                      reply->status == ribterra::CommandReplyStatus::Rejected))
            return *reply;
    }
    throw std::runtime_error("Server did not finish the queued input command");
}

template <class Predicate>
bool wait_remote(ribterra::net::NetworkServer &server, ribterra::ClientSession &session,
                 Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        server.poll();
        session.poll();
        server.tick();
        server.poll();
        session.poll();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
bool await_job(ribterra::ClientSession &session, ribterra::Job expected) {
    for (int tick = 0; tick < 1202; ++tick) {
        if (session.residents().front().job == expected)
            return true;
        session.advance();
    }
    return false;
}
std::uint64_t visible_checksum(const ribterra::ClientSession &session) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto add = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= static_cast<std::uint8_t>(value >> (i * 8));
            hash *= 1099511628211ULL;
        }
    };
    add(session.ticks());
    add(session.resources().wood);
    add(session.resources().stone);
    add(session.resources().food);
    for (const auto &counts :
         {session.inventory().total, session.inventory().available, session.inventory().reserved,
          session.inventory().stored, session.inventory().carried}) {
        add(counts.wood);
        add(counts.stone);
        add(counts.food);
    }
    add(session.tutorial_step());
    for (const auto &chunk : session.chunks()) {
        add(static_cast<int>(chunk.key.layer));
        add(chunk.key.cx);
        add(chunk.key.cy);
        for (const auto &tile : chunk.tiles) {
            add(static_cast<int>(tile.terrain));
            add(tile.amount);
            add(tile.explored);
        }
    }
    for (const auto &resident : session.residents()) {
        add(resident.id);
        add(std::bit_cast<std::uint32_t>(resident.x));
        add(std::bit_cast<std::uint32_t>(resident.y));
        add(static_cast<int>(resident.job));
        add(std::bit_cast<std::uint32_t>(resident.progress));
        add(static_cast<int>(resident.order_phase));
        add(static_cast<int>(resident.path_failure));
        add(resident.pending_path_batch);
    }
    for (const auto &building : session.buildings()) {
        add(building.id);
        add(static_cast<int>(building.kind));
        add(building.x);
        add(building.y);
        add(std::bit_cast<std::uint32_t>(building.progress));
    }
    for (const auto &resident : session.resident_logistics()) {
        add(resident.resident_id);
        add(static_cast<int>(resident.item));
        add(resident.quantity);
        add(resident.capacity);
        add(static_cast<int>(resident.phase));
        add(static_cast<int>(resident.failure));
    }
    for (const auto &work : session.resident_work()) {
        add(work.resident_id);
        add(static_cast<int>(work.kind));
        add(static_cast<int>(work.phase));
        add(static_cast<int>(work.failure));
        add(static_cast<int>(work.completion));
        add(work.attempts);
    }
    for (const auto &profile : session.resident_professions()) {
        add(profile.resident_id);
        add(static_cast<int>(profile.assignment.profession));
        add(profile.assignment.automation_enabled);
        add(profile.assignment.worksite.has_value());
        if (profile.assignment.worksite) {
            add(profile.assignment.worksite->x);
            add(profile.assignment.worksite->y);
            add(static_cast<int>(profile.assignment.worksite->layer));
        }
        for (auto priority : profile.assignment.priorities)
            add(priority);
        add(profile.manual_override);
        add(profile.deferred);
        add(static_cast<int>(profile.wait_reason));
        for (const auto &skill : profile.skills) {
            add(static_cast<int>(skill.skill));
            add(skill.experience);
            add(skill.modifier.skill_sum_bp);
            add(skill.modifier.ability_sum_bp);
            add(skill.modifier.familiarity_bp);
            add(skill.modifier.uncapped_bp);
            add(skill.modifier.applied_bp);
            add(skill.modifier.speed_bp);
            add(skill.modifier.capped);
        }
    }
    for (const auto &site : session.construction_sites()) {
        add(site.building_id);
        add(static_cast<int>(site.kind));
        add(site.site.x);
        add(site.site.y);
        add(static_cast<int>(site.site.layer));
        for (const auto &counts : {site.required, site.delivered}) {
            add(counts.wood);
            add(counts.stone);
            add(counts.food);
        }
        add(std::bit_cast<std::uint32_t>(site.work));
        add(static_cast<int>(site.phase));
        add(static_cast<int>(site.failure));
        add(static_cast<int>(site.worker_phase));
        add(static_cast<int>(site.worker_failure));
        add(site.assigned_workers);
    }
    return hash;
}
} // namespace

int main() {
    try {
        {
            const ribterra::Tile land{ribterra::Terrain::Grass,0,true,ribterra::Biome::Meadow,10};
            auto water = land; water.terrain = ribterra::Terrain::Water; water.elevation = 0;
            auto hidden_water = water; hidden_water.explored = false;
            auto hidden_land = land; hidden_land.explored = false;
            unsigned straight_count = 0, corner_count = 0, secondary_count = 0, flat_primary_count = 0;
            std::set<unsigned> variants;
            for (int y = -20; y < 20; ++y) for (int x = -20; x < 20; ++x) {
                const auto straight = [&](int sx,int sy) { return sx == x+1 && sy == y ? water : land; };
                const auto corner = [&](int sx,int sy) {
                    return (sx == x+1 && sy == y) || (sx == x && sy == y+1) ? water : land;
                };
                const auto a = ribterra::shore_details(x,y,land,false,true,straight);
                const auto b = ribterra::shore_details(x,y,land,false,true,corner);
                check(a == ribterra::shore_details(x,y,land,false,true,straight),
                      "Shore decoration is not deterministic");
                straight_count += a.count > 0; corner_count += b.count > 0;
                secondary_count += b.count == 2;
                flat_primary_count += a.count > 0 && a.details[0].width < 36;
                for (const auto &details : {a,b}) {
                    check(details.count <= 2, "Shore decoration exceeded its bounded draw count");
                    for (unsigned i = 0; i < details.count; ++i) {
                        const auto &point = details.details[i];
                        check(std::isfinite(point.width) && point.width >= 12 && point.width <= 48 &&
                              std::abs(point.dx) < .5f && std::abs(point.dy) < .5f,
                              "Shore decoration leaves its authoritative land cell");
                        check(ribterra::shore_detail_depth(point) > -.5f &&
                              ribterra::shore_detail_depth(point) < .5f &&
                              point.variant < ribterra::shore_detail_sprites.size(),
                              "Shore decoration has invalid depth or silhouette");
                        variants.insert(point.variant);
                    }
                }
                for (unsigned i = 0; i < a.count; ++i)
                    check(a.details[i].dx > .25f, "Shore stones are not on the known water-side edge");
                const auto hidden_a = [&](int,int) { return hidden_water; };
                const auto hidden_b = [&](int,int) { return hidden_land; };
                check(ribterra::shore_details(x,y,land,false,true,hidden_a) ==
                      ribterra::shore_details(x,y,land,false,true,hidden_b) &&
                      ribterra::shore_details(x,y,land,false,true,hidden_a).count == 0,
                      "Shore decoration reveals hidden water");
                auto resource = land; resource.amount = 1;
                auto trail = land; trail.trail_wear = 24;
                auto forest = land; forest.terrain = ribterra::Terrain::Forest;
                for (const auto &forbidden : {water,hidden_land,resource,trail,forest})
                    check(ribterra::shore_details(x,y,forbidden,false,true,corner).count == 0,
                          "Shore decoration obscured water, fog, resource or trail");
                check(ribterra::shore_details(x,y,land,false,false,corner).count == 0,
                      "Shore decoration entered a building apron");
                const auto winter = ribterra::shore_details(x,y,land,true,true,corner);
                auto snow_land = land; snow_land.biome = ribterra::Biome::SnowMeadow;
                check(winter == ribterra::shore_details(x,y,snow_land,false,true,corner) &&
                      winter.count == b.count, "Snow-biome bank differs from seasonal snow");
                for (unsigned i = 0; i < winter.count; ++i) {
                    auto paired = winter.details[i]; paired.variant -= 4;
                    check(paired == b.details[i],
                          "Season change moved, resized or changed the bank silhouette");
                }
                if (b.count == 2)
                    check(b.details[0].variant != b.details[1].variant,
                          "Same-cell bank details repeat one silhouette");
            }
            check(straight_count > 960 && straight_count < 1280 && corner_count > straight_count &&
                  secondary_count > 800 && flat_primary_count*100U > straight_count*36U &&
                  flat_primary_count*100U < straight_count*48U &&
                  variants.size() == 4,
                  "Shore decoration regressed to sparse identical isolated stones");
        }
        {
            const ribterra::Tile land{ribterra::Terrain::Grass,0,true,ribterra::Biome::Meadow,10};
            const ribterra::TerrainRockFace steep{.78f,.20f,.96f,6.f,2.f,true};
            std::array<ribterra::TerrainRockFace,4> corners{{steep,steep,steep,steep}};
            unsigned clusters = 0;
            for (int y = -30; y < 30; ++y) for (int x = -30; x < 30; ++x) {
                const auto summer = ribterra::relief_details(x,y,land,false,true,corners);
                const auto winter = ribterra::relief_details(x,y,land,true,true,corners);
                check(summer == ribterra::relief_details(x,y,land,false,true,corners),
                      "Relief detail is not deterministic");
                clusters += summer.count > 0;
                check(summer.count == 0 || summer.count == 2,
                      "Smooth relief did not keep a bounded primary-secondary group");
                check(winter.count == summer.count,
                      "Season changed relief density or draw count");
                for (unsigned i = 0; i < summer.count; ++i) {
                    const auto &point = summer.details[i];
                    check(std::abs(point.dx) < .45f && std::abs(point.dy) < .45f &&
                          ribterra::shore_detail_depth(point) > -.5f &&
                          ribterra::shore_detail_depth(point) < .5f &&
                          winter.details[i].variant == point.variant + 4U,
                          "Relief detail left its cell, depth range or seasonal pair");
                    auto paired = winter.details[i]; paired.variant -= 4U;
                    check(paired == point, "Season moved or resized a relief detail");
                    check(i == 0 ? point.width >= 32.f && point.width <= 44.f
                                 : point.width >= 12.f && point.width <= 22.f,
                          "Relief detail left its primary or secondary size range");
                    for (unsigned j = 0; j < i; ++j)
                        check(point.variant != summer.details[j].variant,
                              "Relief group repeated one silhouette");
                }
                if (summer.count > 0) {
                    check(ribterra::relief_details(x+1,y,land,false,true,corners).count == 0 &&
                          ribterra::relief_details(x-1,y,land,false,true,corners).count == 0 &&
                          ribterra::relief_details(x,y+1,land,false,true,corners).count == 0 &&
                          ribterra::relief_details(x,y-1,land,false,true,corners).count == 0,
                          "Relief priority placed adjacent outcrop groups");
                }
            }
            check(clusters >= 520 && clusters <= 920,
                  "Relief density left the intended sparse one-in-four-to-seven range");

            auto unknown = corners; unknown[2].known_footprint = false;
            auto isolated = corners; isolated[1].exposure = .55f; isolated[3].exposure = .55f;
            auto gentle = corners;
            for (auto &corner : gentle) { corner.gradient_x = 4.f; corner.gradient_y = 0.f; }
            auto resource = land; resource.amount = 1;
            auto trail = land; trail.trail_wear = 24;
            auto hidden = land; hidden.explored = false;
            auto forest = land; forest.terrain = ribterra::Terrain::Forest;
            for (int y = -30; y < 30; ++y) for (int x = -30; x < 30; ++x) {
                check(ribterra::relief_details(x,y,land,false,true,unknown).count == 0 &&
                      ribterra::relief_details(x,y,land,false,true,isolated).count == 0 &&
                      ribterra::relief_details(x,y,land,false,true,gentle).count == 0 &&
                      ribterra::relief_details(x,y,resource,false,true,corners).count == 0 &&
                      ribterra::relief_details(x,y,trail,false,true,corners).count == 0 &&
                      ribterra::relief_details(x,y,hidden,false,true,corners).count == 0 &&
                      ribterra::relief_details(x,y,forest,false,true,corners).count == 0 &&
                      ribterra::relief_details(x,y,land,false,false,corners).count == 0,
                      "Relief detail ignored its known steep empty-grass mask");
            }
            auto curved = corners; curved[0].light = .82f;
            bool found_curved = false;
            for (int y = -20; y < 20 && !found_curved; ++y)
                for (int x = -20; x < 20 && !found_curved; ++x) {
                    const auto detail = ribterra::relief_details(x,y,land,false,true,curved);
                    if (detail.count == 0) continue;
                    found_curved = true;
                    check(detail.count == 3 && detail.details[0].variant != detail.details[2].variant &&
                          detail.details[1].variant != detail.details[2].variant,
                          "Strong relief curvature lost its third distinct form");
                }
            check(found_curved, "Synthetic steep region produced no curved relief group");
        }
        {
            std::array<unsigned,4> counts{};
            for (int y = -30; y < 30; ++y) for (int x = -30; x < 30; ++x) {
                const auto shrub = ribterra::resource_shrub(x,y,false);
                auto winter = ribterra::resource_shrub(x,y,true);
                check(shrub == ribterra::resource_shrub(x,y,false),
                      "Berry appearance is not deterministic");
                check(shrub.variant < 4 && winter.variant == shrub.variant+4,
                      "Berry appearance has no corresponding seasonal silhouette");
                winter.variant -= 4;
                check(shrub == winter, "Snow moved or resized a gatherable berry target");
                check(std::abs(shrub.dx) < .3f && std::abs(shrub.dy) < .3f &&
                      shrub.width >= 22 && shrub.width <= 30 &&
                      ribterra::resource_shrub_depth(shrub) > -.5f &&
                      ribterra::resource_shrub_depth(shrub) < .5f,
                      "Berry ground anchor or depth left its authoritative cell");
                ++counts[shrub.variant];
            }
            for (auto count : counts)
                check(count > 720 && count < 1080, "Berry silhouette selection lacks spatial variety");
        }
        {
            constexpr auto kind = ribterra::BuildingKind::House;
            constexpr float diagonal = .70710678f;
            const auto clear = [&](float across, float along) {
                return ribterra::building_apron_clear(kind,24,24,
                    24.35f+(along+across)*diagonal,24.35f+(along-across)*diagonal);
            };
            check(!clear(0,0) && !clear(0,3.f), "Ground cover entered a house yard or front approach");
            check(clear(3.f,0) && clear(0,-2.7f),
                  "Ground cover still leaves an oversized empty circle beside or behind a house");
        }
        {
            const ribterra::Tile meadow{ribterra::Terrain::Grass, 0, true,
                                       ribterra::Biome::Meadow, 10};
            int decorated = 0;
            std::set<unsigned> variants;
            for (int y = -30; y < 30; ++y) for (int x = -30; x < 30; ++x) {
                const auto cover = ribterra::ground_cover(x,y,meadow,false,true,false);
                check(cover == ribterra::ground_cover(x,y,meadow,false,true,false),
                      "Ground cover changed on repeated composition");
                decorated += cover.count > 0;
                check(cover.count <= 3, "Ground cover exceeded its bounded draw count");
                for (unsigned i = 0; i < cover.count; ++i) {
                    const auto &detail = cover.details[i];
                    check(ribterra::ground_cover_depth(detail) > -.5f &&
                          ribterra::ground_cover_depth(detail) < .5f,
                          "Ground cover is ordered behind its own ground or over the next row");
                    check(std::abs(detail.dx) < .5f && std::abs(detail.dy) < .5f,
                          "Ground cover leaked beyond its authoritative known cell");
                    check(detail.variant < ribterra::ground_cover_sprites.size(),
                          "Ground cover selected a missing silhouette");
                    variants.insert(detail.variant);
                    for (unsigned j = 0; j < i; ++j)
                        check(std::hypot(detail.dx-cover.details[j].dx,
                                         detail.dy-cover.details[j].dy) > .25f,
                              "Ground cover clumped multiple sprites onto one point");
                }
                for (auto forbidden : {ribterra::Terrain::Water, ribterra::Terrain::Rock,
                                       ribterra::Terrain::Forest}) {
                    auto tile = meadow; tile.terrain = forbidden;
                    check(ribterra::ground_cover(x,y,tile,false,true,false).count == 0,
                          "Decorative meadow cover obscured an actionable resource or water");
                }
                auto harvested = meadow; harvested.amount = 5;
                auto unknown = meadow; unknown.explored = false;
                auto trail = meadow; trail.trail_wear = 24;
                auto snow = meadow; snow.biome = ribterra::Biome::SnowMeadow;
                check(ribterra::ground_cover(x,y,harvested,false,true,false).count == 0 &&
                      ribterra::ground_cover(x,y,unknown,false,true,false).count == 0 &&
                      ribterra::ground_cover(x,y,trail,false,true,false).count == 0 &&
                      ribterra::ground_cover(x,y,snow,false,true,false).count == 0 &&
                      ribterra::ground_cover(x,y,meadow,true,true,false).count == 0 &&
                      ribterra::ground_cover(x,y,meadow,false,false,false).count == 0,
                      "Ground cover ignored resource, fog, season, trail or apron clearance");
            }
            check(decorated > 2160 && decorated < 2664 && variants.size() == 5,
                  "Ground cover regressed to sparse identical stamps");
        }
        {
            ribterra::net::HostConfig config;
            config.game.lobby_enabled = true;
            config.game.max_players = 2;
            config.transport.bind_address = "127.0.0.1";
            std::unique_ptr<ribterra::net::NetworkServer> server;
            for (std::uint16_t port = 27650; port < 27750 && !server; ++port) {
                config.transport.port = port;
                auto candidate = std::make_unique<ribterra::net::NetworkServer>(config);
                if (candidate->listen())
                    server = std::move(candidate);
            }
            check(server != nullptr, "Remote readiness test could not open a local server");

            ribterra::ClientSession remote(70421, "Remote readiness");
            std::string error;
            const auto address = "127.0.0.1:" + std::to_string(server->port());
            check(remote.connect_remote(address, {}, error), "Remote readiness test could not connect");
            check(!remote.snapshot_ready(), "Handshake exposed an authoritative snapshot early");
            check(!remote.world_ready(), "Handshake exposed the world before its snapshot");
            check(wait_remote(*server, remote, [&] { return remote.snapshot_ready(); }),
                  "Remote lobby snapshot did not arrive");
            check(remote.lobby().enabled && !remote.lobby().started,
                  "Remote client did not enter the authoritative lobby phase");
            check(!remote.world_ready(), "Pre-start lobby exposed an empty world");

            check(remote.lobby_action(ribterra::LobbyAction::Ready, 1).accepted,
                  "Host could not mark itself ready");
            check(wait_remote(*server, remote, [&] {
                      return !remote.lobby().players.empty() && remote.lobby().players.front().ready;
                  }),
                  "Ready state did not reach the client");
            check(remote.lobby_action(ribterra::LobbyAction::Start).accepted,
                  "Host could not start the lobby world");
            check(wait_remote(*server, remote, [&] { return remote.world_ready(); }),
                  "Started lobby never produced a presentation-ready world");
            check(remote.lobby().started && !remote.chunks().empty(),
                  "World readiness did not include started lobby terrain");
            server->shutdown();
            const auto disconnect_deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (remote.snapshot_ready() &&
                   std::chrono::steady_clock::now() < disconnect_deadline) {
                server->poll();
                remote.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(!remote.snapshot_ready() && !remote.world_ready(),
                  "Reconnect retained a stale world's presentation readiness");
        }
        {
            ribterra::World authority(70421, 10);
            ribterra::ClientSession client(70421, "Build preview agreement");
            int allowed = 0, steep = 0;
            for (int y = ribterra::world_min; y < ribterra::world_max; ++y)
                for (int x = ribterra::world_min; x < ribterra::world_max; ++x) {
                    const auto reason = ribterra::surface_foundation_support(
                        x, y, [&](int sample_x, int sample_y) {
                            return client.tile(sample_x, sample_y, ribterra::WorldLayer::Surface);
                        });
                    if (reason == ribterra::SurfaceReason::Allowed)
                        ++allowed;
                    else if (reason == ribterra::SurfaceReason::SlopeTooSteep)
                        ++steep;
                    check(client.can_build(ribterra::BuildingKind::House, x, y) ==
                              authority.can_build(ribterra::BuildingKind::House, x, y),
                          "Client build preview disagrees with authoritative gen7 terrain rules");
                }
            check(allowed > 0, "Gen7 build-preview fixture has no valid site");
            check(steep > 0, "Gen7 build-preview fixture does not exercise steep relief");

            auto legacy_state = authority.export_state();
            legacy_state.terrain_generator = 6;
            std::string error;
            check(authority.import_state(std::move(legacy_state), error), error.c_str());
            const auto legacy_path = std::filesystem::temp_directory_path() /
                                     "ribterra-client-gen6-preview.save";
            check(authority.save(legacy_path, error), error.c_str());
            ribterra::ClientSession legacy_client(1, "Legacy build preview agreement");
            check(legacy_client.load(legacy_path, error), error.c_str());
            int legacy_sites = 0, policy_differences = 0;
            for (int y = 0; y <= 48; ++y)
                for (int x = 0; x <= 52; ++x) {
                    const auto sample = [&](int sample_x, int sample_y) {
                        return legacy_client.tile(sample_x, sample_y,
                                                  ribterra::WorldLayer::Surface);
                    };
                    const auto old_support = ribterra::surface_buildability(x, y, sample);
                    const auto strict_support = ribterra::surface_foundation_support(x, y, sample);
                    policy_differences += old_support != strict_support;
                    const bool preview = legacy_client.can_build(
                        ribterra::BuildingKind::House, x, y);
                    const bool authoritative = authority.can_build(
                        ribterra::BuildingKind::House, x, y);
                    legacy_sites += preview;
                    check(preview == authoritative,
                          "Client build preview disagrees with authoritative gen6 terrain rules");
                }
            check(legacy_sites > 0, "Gen6 build-preview fixture has no valid site");
            check(policy_differences > 0,
                  "Gen6 preview fixture does not distinguish legacy and strict policies");
            std::filesystem::remove(legacy_path);
        }
        ribterra::UnitAnimation walk;
        walk.update(0, 0, 0);
        check(!walk.moving, "Idle unit animated");
        std::set<int> phases;
        for (int i=1; i<=12; ++i) {
            walk.update(i*.125f, 0, i);
            phases.insert(walk.frame());
            check(walk.moving && walk.direction==0 && !walk.flipped, "East walking direction incorrect");
        }
        check(phases.size()==4, "Walk cycle did not visit four frames");
        const auto phase=walk.frame();
        walk.update(1.5f, 0, 12);
        check(walk.frame()==phase, "Paused render advanced walking");
        walk.update(1.5f, 0, 13);
        check(!walk.moving, "Arrived unit continued walking");
        walk.update(1.375f, 0, 14);
        check(walk.direction==1 && walk.flipped, "West walking direction incorrect");
        walk.update(30, 30, 15);
        check(!walk.moving, "Teleport played walking");
        {
            ribterra::ClientSession profession(70421, "Profession commands");
            ribterra::ProfessionAssignment assignment;
            assignment.profession = ribterra::Profession::Miner;
            check(profession.assign_profession({1, 999}, assignment).pending,
                  "profession command must use server queue");
            check(profession.resident_professions().front().assignment.profession ==
                      ribterra::Profession::Unassigned,
                  "profession must not change before authority tick");
            const auto result = await_terminal(profession);
            check(result.status == ribterra::CommandReplyStatus::Applied && result.residents.size() == 2,
                  "client must retain partial profession results");
            check(result.residents.front().status == ribterra::ResidentCommandStatus::Applied &&
                      result.residents.back().status == ribterra::ResidentCommandStatus::Rejected,
                  "client partial outcomes must preserve resident identity and acceptance");
            check(profession.resident_professions().front().assignment == assignment,
                  "profession profile must expose server assignment");
            check(profession.stop({1}).pending, "Stop must use server queue");
            check(await_terminal(profession).status == ribterra::CommandReplyStatus::Applied,
                  "Stop must receive a terminal acceptance");
            check(!profession.resident_professions().front().assignment.automation_enabled,
                  "Stop must disable automatic profession work");
        }
        {
            ribterra::ClientSession replies(70421, "Concurrent reply test");
            check(replies.move({1}, 26, 25).pending && replies.move({2}, 27, 25).pending,
                  "two independent client orders must queue");
            std::set<std::uint64_t> queued, applied;
            for (int tick = 0; tick < 1202 && applied.size() != 2; ++tick) {
                replies.advance();
                while (const auto reply = replies.take_command_reply()) {
                    if (reply->status == ribterra::CommandReplyStatus::Queued)
                        check(queued.insert(reply->request_id).second, "duplicate client Queued reply");
                    else {
                        check(reply->status == ribterra::CommandReplyStatus::Applied,
                              "independent client order failed");
                        check(applied.insert(reply->request_id).second, "duplicate client terminal reply");
                    }
                }
            }
            check(queued.size() == 2 && applied == queued,
                  "one poll must retain both phases of both client commands");
        }
        {
            ribterra::test::WorkProjectionSave saved;
            ribterra::World fixture(70421, 4);
            std::string error;
            check(fixture.import_state(ribterra::test::work_projection_state(), error),
                  "work projection fixture import failed");
            check(fixture.save(saved.path, error), "work projection fixture save failed");
            ribterra::ClientSession work(70421, "Saved retry"), restored(1, "Restored retry");
            check(work.load(saved.path, error), "client work projection load failed");
            check(work.resident_work().size() == 3, "work projection must omit foreign intent");
            check(work.resident_work().front().phase == ribterra::WorkIntentPhase::Waiting,
                  "saved automatic retry must remain waiting");
            check(work.save(saved.path, error), "client work projection save failed");
            check(restored.load(saved.path, error), "client work projection restore failed");
            check(visible_checksum(work) == visible_checksum(restored),
                  "work intent save/load changed visible state");
            for (int tick = 0; tick < 80; ++tick) {
                work.advance();
                restored.advance();
                check(!work.take_command_reply() && !restored.take_command_reply(),
                      "internal work path must not emit a client command reply");
            }
            check(visible_checksum(work) == visible_checksum(restored),
                  "saved work retry must resume deterministically");
        }
        check(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        SDL_Window *window = SDL_CreateWindow("Ribterra input validation", 1100, 700, SDL_WINDOW_HIDDEN);
        check(window != nullptr, "window creation failed");
        {
            ribterra::ClientSession world(70421, "Client test");
            const auto base =
                std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(SDL_GetBasePath())));
            ribterra::render::Renderer2D renderer(window, base / "assets",
                                                  {ribterra::render::Backend::Software, false});
            ribterra::GameView view(renderer);
            {
                ribterra::ClientSession swapped(70421, "Chunk replacement regression");
                ribterra::GameView reused(renderer), clean(renderer);
                ribterra::Chunk old_chunk, next_chunk;
                old_chunk.key = {ribterra::WorldLayer::Surface, 0, 0};
                next_chunk.key = {ribterra::WorldLayer::Surface, 1, 0};
                old_chunk.revision = next_chunk.revision = 1;
                for (auto &tile : old_chunk.tiles)
                    tile = {ribterra::Terrain::Grass,0,true,ribterra::Biome::Meadow,40};
                for (auto &tile : next_chunk.tiles)
                    tile = {ribterra::Terrain::Grass,0,true,ribterra::Biome::Meadow,12};
                // Inject a controlled *presentation snapshot* only. This models
                // a remote delta removing A and adding B with the same count;
                // no production mutation API or simulation edit is introduced.
                auto &chunks = const_cast<std::vector<ribterra::Chunk>&>(swapped.chunks());
                chunks = {old_chunk};
                ribterra::render::Frame2D cached, fresh;
                reused.compose(swapped,0,cached,false);
                swapped.advance(); // new snapshot epoch, as for a network delta
                chunks = {next_chunk};
                reused.compose(swapped,0,cached,false);
                clean.compose(swapped,0,fresh,false);
                const auto old_cached = reused.tile_screen(24,24);
                const auto old_clean = clean.tile_screen(24,24);
                check(std::abs(old_cached.y-old_clean.y)<.001f,
                      "Same-count chunk replacement retained a removed chunk's height");
                check(reused.focus_minimap(swapped,.5f,.5f) &&
                      clean.focus_minimap(swapped,.5f,.5f),
                      "Same-count chunk replacement retained obsolete minimap bounds");
                const auto focused_cached = reused.tile_screen(48,16);
                const auto focused_clean = clean.tile_screen(48,16);
                check(std::abs(focused_cached.x-focused_clean.x)<.001f &&
                      std::abs(focused_cached.y-focused_clean.y)<.001f,
                      "Cached minimap focuses differently after chunk replacement");
            }
            {
                ribterra::GameView wide(renderer);
                wide.set_logical_size({1600, 900});
                ribterra::render::Frame2D wide_frame;
                wide.compose(world, 0, wide_frame, false);
                check(wide_frame.logical_size.width == 1600 && wide_frame.logical_size.height == 900,
                      "GameView discarded the fitted logical presentation");
                const auto camp = wide.tile_screen(24, 24);
                const auto picked = wide.cell_at_screen(camp.x, camp.y);
                check(picked.x == 24 && picked.y == 24,
                      "Wide presentation changed world projection input roundtrip");
                const auto minimap_corner = std::find_if(
                    wide_frame.vertices.begin(), wide_frame.vertices.end(), [](const auto &vertex) {
                        return std::abs(vertex.position.x - 1300.f) < .01f &&
                               std::abs(vertex.position.y - 710.f) < .01f &&
                               vertex.color.r == 5 && vertex.color.g == 13 && vertex.color.b == 22 &&
                               vertex.color.a == 255;
                    });
                check(minimap_corner != wide_frame.vertices.end(),
                      "GameView minimap did not follow the widened HUD anchor");
                int min_x=ribterra::world_max,min_y=ribterra::world_max;
                int max_x=ribterra::world_min,max_y=ribterra::world_min;
                ribterra::MinimapBounds known_map;
                for(const auto &chunk:world.chunks())
                    if(chunk.key.layer==ribterra::WorldLayer::Surface)
                        for(std::size_t i=0;i<chunk.tiles.size();++i) if(chunk.tiles[i].explored) {
                            const int x=chunk.key.cx*ribterra::chunk_size+int(i%ribterra::chunk_size);
                            const int y=chunk.key.cy*ribterra::chunk_size+int(i/ribterra::chunk_size);
                            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
                            min_y=std::min(min_y,y);max_y=std::max(max_y,y);
                            known_map.include_cell(float(x),float(y));
                        }
                const auto minimap=ribterra::make_minimap_projection(
                    {1300,710,210,162},known_map);
                check((known_map.max_v-known_map.min_v)*minimap.scale >= 162.f*.90f,
                      "Explored map is vertically crushed into a small oval again");
                check(std::abs((minimap.project(25,23).x-minimap.project(24,24).x)-
                               (minimap.project(25,25).y-minimap.project(24,24).y))<.001f,
                      "Overhead minimap distorts equal world-axis diagonals");
                for(const auto point:std::array<ribterra::render::Point,3>{{
                        {24,24},{float(min_x),float(max_y)},{float(max_x),float(min_y)}}}) {
                    const auto screen=minimap.project(point.x,point.y);
                    const auto restored=minimap.unproject(screen.x,screen.y);
                    check(std::abs(restored.x-point.x)<.001f&&std::abs(restored.y-point.y)<.001f,
                          "Overhead minimap projection does not roundtrip world coordinates");
                }
                const auto camp_on_map=minimap.normalized(24,24);
                check(wide.focus_minimap(world,camp_on_map.x,camp_on_map.y),
                      "Minimap click did not focus its explored camp marker");
                const auto recentered=wide.tile_screen(24,24);
                check(std::abs(recentered.x-1600*.48f)<.01f,
                      "Minimap focus did not recenter the clicked world cell");
                bool rejected_unknown=false;
                for(int y=std::max(ribterra::world_min,min_y-3);y<=std::min(ribterra::world_max-1,max_y+3)&&!rejected_unknown;++y)
                    for(int x=std::max(ribterra::world_min,min_x-3);x<=std::min(ribterra::world_max-1,max_x+3)&&!rejected_unknown;++x) {
                        const auto normalized=minimap.normalized(float(x),float(y));
                        if(normalized.x<0||normalized.x>1||normalized.y<0||normalized.y>1||
                           world.tile(x,y).explored) continue;
                        rejected_unknown=!wide.focus_minimap(world,normalized.x,normalized.y);
                    }
                check(rejected_unknown,"Minimap fixture did not reject an unknown in-frame cell");
            }
            view.set_paused(true);
            view.set_speed(4);
            check(view.paused() && view.speed() == 4,
                  "Synchronizing speed must not hide an authoritative pause");
            view.set_speed(1);
            view.set_paused(false);
            ribterra::render::Frame2D frame;
            const auto before_compose = visible_checksum(world);
            view.compose(world, 0, frame);
            check(visible_checksum(world) == before_compose,
                  "frame composition must not mutate simulation state");
            auto prior = ribterra::render::Pass::Terrain;
            for (const auto &command : frame.commands) {
                const auto pass = std::visit([](const auto &item) { return item.pass; }, command);
                check(static_cast<int>(pass) >= static_cast<int>(prior),
                      "frame passes must preserve terrain, world, fog and UI order");
                prior = pass;
            }
            {
                ribterra::GameView reused(renderer), fresh(renderer);
                ribterra::AssetCatalog assets(base / "assets");
                reused.load_art(assets); fresh.load_art(assets);
                ribterra::ClientSession other(99117, "Different terrain");
                ribterra::render::Frame2D cached_frame, fresh_frame;
                reused.compose(world, 0, cached_frame, false);
                reused.compose(world, 0, fresh_frame, false);
                auto same_geometry = [&] {
                    return cached_frame.indices == fresh_frame.indices &&
                           cached_frame.vertices.size() == fresh_frame.vertices.size() &&
                           std::memcmp(cached_frame.vertices.data(), fresh_frame.vertices.data(),
                               cached_frame.vertices.size() * sizeof(ribterra::render::Vertex)) == 0;
                };
                check(same_geometry(), "Unchanged terrain changed after a repeated frame");
                reused.compose(other, 0, cached_frame, false);
                fresh.compose(other, 0, fresh_frame, false);
                check(same_geometry(), "Terrain cache retained a previous world's ground or fog");
                // Camera translation reuses cached meshes; compare to geometry
                // freshly built at the same camera rather than a golden image.
                SDL_Event pan{}; pan.type=SDL_EVENT_KEY_DOWN; pan.key.key=SDLK_RIGHT;
                reused.handle_event(pan, other); fresh.handle_event(pan, other);
                reused.compose(other, 0, cached_frame, false);
                fresh.reset_world_state();
                fresh.compose(other, 0, fresh_frame, false);
                check(cached_frame.vertices.size()==fresh_frame.vertices.size(), "Panning lost cached ground geometry");
                for (std::size_t i=0;i<cached_frame.vertices.size();++i) {
                    const auto &a=cached_frame.vertices[i], &b=fresh_frame.vertices[i];
                    check(std::abs(a.position.x-b.position.x)<.002f && std::abs(a.position.y-b.position.y)<.002f &&
                          std::memcmp(&a.color,&b.color,sizeof(a.color))==0,
                          "Translated terrain cache changed geometry or shading");
                }
                const auto trail_wear = [](const ribterra::ClientSession &session) {
                    std::uint64_t total = 0;
                    for (const auto &chunk : session.chunks())
                        if (chunk.key.layer == ribterra::WorldLayer::Surface)
                            for (const auto &tile : chunk.tiles) total += tile.trail_wear;
                    return total;
                };
                const auto wear_before = trail_wear(other);
                check(other.move({other.residents().front().id}, 26, 25).accepted,
                      "Terrain cache trail fixture move was rejected");
                check(await_terminal(other).status == ribterra::CommandReplyStatus::Applied,
                      "Terrain cache trail fixture move was not applied");
                for (int i=0; i<400 && trail_wear(other)==wear_before; ++i) other.advance();
                check(trail_wear(other)>wear_before,
                      "Terrain cache trail fixture did not change a chunk revision");
                reused.compose(other, 0, cached_frame, false);
                // Force only the terrain cache through a clean rebuild. Resetting
                // GameView would also reset resident interpolation/animation and
                // make this a comparison of two different presentation epochs.
                fresh.set_layer(ribterra::WorldLayer::Mine);
                ribterra::render::Frame2D mine_frame;
                fresh.compose(other, 0, mine_frame, false);
                fresh.set_layer(ribterra::WorldLayer::Surface);
                fresh.compose(other, 0, fresh_frame, false);
                if (cached_frame.indices != fresh_frame.indices ||
                    cached_frame.vertices.size() != fresh_frame.vertices.size()) {
                    std::cerr << "trail topology vertices=" << cached_frame.vertices.size() << '/'
                              << fresh_frame.vertices.size() << " indices=" << cached_frame.indices.size()
                              << '/' << fresh_frame.indices.size() << '\n';
                    const auto equivalent=[](const auto&a,const auto&b) {
                        return std::abs(a.position.x-b.position.x)<.002f &&
                               std::abs(a.position.y-b.position.y)<.002f &&
                               std::abs(a.uv.x-b.uv.x)<.00001f && std::abs(a.uv.y-b.uv.y)<.00001f &&
                               std::memcmp(&a.color,&b.color,sizeof(a.color))==0;
                    };
                    const auto count=std::min(cached_frame.vertices.size(),fresh_frame.vertices.size());
                    for(std::size_t i=0;i<count;++i) if(!equivalent(cached_frame.vertices[i],fresh_frame.vertices[i])) {
                        const auto&a=cached_frame.vertices[i],&b=fresh_frame.vertices[i];
                        std::cerr << "first semantic vertex=" << i << " pos=" << a.position.x << ','
                                  << a.position.y << '/' << b.position.x << ',' << b.position.y
                                  << " uv=" << a.uv.x << ',' << a.uv.y << '/' << b.uv.x << ',' << b.uv.y
                                  << " rgba=" << int(a.color.r) << ',' << int(a.color.g) << ',' << int(a.color.b)
                                  << ',' << int(a.color.a) << '/' << int(b.color.r) << ',' << int(b.color.g)
                                  << ',' << int(b.color.b) << ',' << int(b.color.a) << '\n';
                        break;
                    }
                }
                check(cached_frame.indices == fresh_frame.indices &&
                          cached_frame.vertices.size() == fresh_frame.vertices.size(),
                      "Incremental trail refresh changed clean-rebuild mesh topology");
                for (std::size_t i=0;i<cached_frame.vertices.size();++i) {
                    const auto &a=cached_frame.vertices[i], &b=fresh_frame.vertices[i];
                    check(std::abs(a.position.x-b.position.x)<.002f &&
                              std::abs(a.position.y-b.position.y)<.002f &&
                              std::abs(a.uv.x-b.uv.x)<.00001f && std::abs(a.uv.y-b.uv.y)<.00001f &&
                              std::memcmp(&a.color,&b.color,sizeof(a.color))==0,
                          "Incremental trail refresh differs from a clean terrain rebuild");
                }
            }
            {
                ribterra::GameView selection(renderer);
                selection.compose(world,0,frame,false);
                auto send=[&](Uint32 type,float x,float y) {
                    SDL_Event event{};event.type=type;
                    if(type==SDL_EVENT_MOUSE_MOTION){event.motion.x=x;event.motion.y=y;}
                    else {event.button.button=SDL_BUTTON_LEFT;event.button.x=x;event.button.y=y;}
                    selection.handle_event(event,world);
                };
                float left=1440,top=900,right=0,bottom=0;
                for(const auto &unit:world.residents()) {
                    auto point=selection.tile_screen(unit.x,unit.y);point.y-=10;
                    left=std::min(left,point.x-25);right=std::max(right,point.x+25);
                    top=std::min(top,point.y-25);bottom=std::max(bottom,point.y+25);
                }
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,right,bottom);send(SDL_EVENT_MOUSE_MOTION,left,top);
                check(selection.selected_ids().size()==10,"Reverse drag must select every resident in its rectangle");
                selection.compose(world,0,frame,false);renderer.render(frame,"selection-drag.png");
                send(SDL_EVENT_MOUSE_BUTTON_UP,left,top);
                check(!selection.selection_drag_active(),"Selection drag did not finish");
                auto a=selection.tile_screen(world.residents()[0].x,world.residents()[0].y);a.y-=10;
                auto b=selection.tile_screen(world.residents()[1].x,world.residents()[1].y);b.y-=10;
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,a.x,a.y);send(SDL_EVENT_MOUSE_BUTTON_UP,a.x,a.y);
                SDL_Event alt{};alt.type=SDL_EVENT_KEY_DOWN;alt.key.key=SDLK_LALT;selection.observe_input(alt);
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,b.x,b.y);send(SDL_EVENT_MOUSE_BUTTON_UP,b.x,b.y);
                check(selection.selected_ids().size()==2,"Alt click must add a resident");
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,b.x,b.y);send(SDL_EVENT_MOUSE_BUTTON_UP,b.x,b.y);
                check(selection.selected_ids().size()==2,"Alt click duplicated or removed the resident");
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,left,top);
                SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;selection.observe_input(lost);
                check(!selection.selection_drag_active(),"Focus loss left selection captured");
                const auto selected_before_cancel = selection.selected_ids();
                send(SDL_EVENT_MOUSE_BUTTON_DOWN,right,bottom);
                send(SDL_EVENT_MOUSE_MOTION,left,top);
                check(selection.interaction_active(),"Selection drag must be reported as an active interaction");
                selection.cancel_interaction();
                check(!selection.interaction_active(),"CancelInteraction left selection drag active");
                check(selection.selected_ids()==selected_before_cancel,
                      "CancelInteraction must restore the selection from before a drag");
                selection.arm_build(ribterra::BuildingKind::House);
                check(selection.interaction_active(),"Building placement must be cancellable");
                selection.cancel_interaction();
                check(!selection.interaction_active(),"CancelInteraction left building placement active");
                ribterra::ProfessionAssignment assignment{};
                selection.arm_worksite(assignment,{world.residents().front().id});
                check(selection.interaction_active(),"Worksite targeting must be cancellable");
                selection.cancel_interaction();
                check(!selection.interaction_active()&&!selection.worksite_armed(),
                      "CancelInteraction left worksite targeting active");
            }
            {
                // Building/resident selection must use the same authored pixels and
                // painter depths as the frame at every supported zoom.
                const auto make_fixture = [&](const std::filesystem::path &path,
                                              float resident_x, float resident_y) {
                    ribterra::World authority(70421, 1);
                    auto state=authority.export_state();
                    for(auto &chunk:state.chunks) if(chunk.key.layer==ribterra::WorldLayer::Surface) {
                        for(std::size_t i=0;i<chunk.tiles.size();++i) {
                            const int x=chunk.key.cx*ribterra::chunk_size+int(i%ribterra::chunk_size);
                            const int y=chunk.key.cy*ribterra::chunk_size+int(i/ribterra::chunk_size);
                            if(x<18||x>30||y<18||y>30) continue;
                            auto &tile=chunk.tiles[i];
                            tile.terrain=ribterra::Terrain::Grass; tile.amount=0; tile.explored=true;
                            tile.biome=ribterra::Biome::Meadow; tile.elevation=10;
                        }
                    }
                    auto &resident=state.residents.front();
                    resident.x=resident_x; resident.y=resident_y;
                    resident.target_x=int(std::lround(resident_x));
                    resident.target_y=int(std::lround(resident_y));
                    ++state.topology_revision;
                    std::string error;
                    check(authority.import_state(std::move(state),error),error.c_str());
                    check(authority.save(path,error),error.c_str());
                };
                const auto foreground_path=std::filesystem::temp_directory_path()/
                                           "ribterra-pick-foreground.save";
                const auto background_path=std::filesystem::temp_directory_path()/
                                           "ribterra-pick-background.save";
                make_fixture(foreground_path,24.f,24.f); // depth 48.8, above camp 48.5
                make_fixture(background_path,23.f,24.f); // depth 47.8, behind camp
                std::string error;
                ribterra::ClientSession foreground(1,"Foreground pick fixture");
                ribterra::ClientSession background(1,"Background pick fixture");
                check(foreground.load(foreground_path,error),error.c_str());
                check(background.load(background_path,error),error.c_str());
                std::filesystem::remove(foreground_path);
                std::filesystem::remove(background_path);

                ribterra::AssetCatalog assets(base/"assets");
                ribterra::GameArt pick_art(renderer);
                pick_art.load(assets);
                ribterra::GameView pick_view(renderer);
                pick_view.load_art(assets);
                const auto find_overlap = [&](ribterra::ClientSession &session, float resident_x,
                                              float resident_y, float zoom) {
                    const auto camp=std::find_if(session.buildings().begin(),session.buildings().end(),
                        [](const auto &building){return building.kind==ribterra::BuildingKind::Camp;});
                    check(camp!=session.buildings().end(),"Picking fixture has no camp");
                    const auto camp_visual=ribterra::resolve_building_visual(
                        camp->kind,1,camp->progress,session.winter());
                    const auto camp_point=pick_view.tile_screen(float(camp->x),float(camp->y));
                    const ribterra::render::Point camp_ground{
                        camp_point.x,camp_point.y+15.f*zoom};
                    const auto resident_point=pick_view.tile_screen(resident_x,resident_y);
                    const ribterra::render::Point resident_ground{
                        resident_point.x,resident_point.y+6.f*zoom};
                    const auto resident_id=ribterra::resident_sprite(
                        1,ribterra::ResidentLoad::Empty,2,1);
                    const float resident_width=ribterra::resident_sprite_width(
                        ribterra::ResidentLoad::Empty)*zoom;
                    const auto a=pick_art.sprite_bounds(
                        camp_visual.asset_id,camp_ground,camp_visual.width*zoom);
                    const auto b=pick_art.sprite_bounds(resident_id,resident_ground,resident_width);
                    const float left=std::max(a.x,b.x), top=std::max(a.y,b.y);
                    const float right=std::min(a.x+a.w,b.x+b.w), bottom=std::min(a.y+a.h,b.y+b.h);
                    for(float y=top+.25f;y<bottom;y+=.5f)
                        for(float x=left+.25f;x<right;x+=.5f) {
                            const ribterra::render::Point point{x,y};
                            if(pick_art.hit_test_sprite(camp_visual.asset_id,camp_ground,
                                   camp_visual.width*zoom,point) &&
                               pick_art.hit_test_sprite(resident_id,resident_ground,
                                   resident_width,point)) return SDL_FPoint{x,y};
                        }
                    throw std::runtime_error("Picking fixture has no shared opaque pixel");
                };
                struct PickZoom { float wheel, zoom; };
                for(const auto check_zoom:std::array<PickZoom,3>{{{-100,.62f},{0,1.3f},{100,3.f}}}) {
                    for(auto *session:{&foreground,&background}) {
                        pick_view.reset_world_state();
                        key(pick_view,*session,SDLK_C);
                        if(check_zoom.wheel!=0) {
                            SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;
                            wheel.wheel.y=check_zoom.wheel;
                            pick_view.handle_event(wheel,*session);
                        }
                        ribterra::render::Frame2D pick_frame;
                        pick_view.compose(*session,0,pick_frame,false);
                        const bool in_front=session==&foreground;
                        const auto point=find_overlap(*session,in_front?24.f:23.f,24.f,check_zoom.zoom);
                        click(renderer,window,pick_view,*session,point,SDL_BUTTON_LEFT);
                        if(in_front) {
                            check(pick_view.selected_ids()==std::vector<int>{1} &&
                                      pick_view.selected_building_ids().empty(),
                                  "Foreground resident did not win painter-order picking");
                        } else {
                            check(pick_view.selected_ids().empty() &&
                                      pick_view.selected_building_ids()==std::vector<int>{1},
                                  "Opaque building did not hide a resident behind it");
                        }
                    }
                }
            }
            key(view, world, SDLK_A);
            check(view.selected_ids().size() == 10, "A must select all ten residents");
            key(view, world, SDLK_SPACE);
            check(view.paused(), "space must pause");
            key(view, world, SDLK_2);
            check(!view.paused() && view.speed() == 2, "2 must resume at 2x");
            key(view, world, SDLK_1);
            click(renderer, window, view, world, view.tile_screen(26, 25), SDL_BUTTON_RIGHT);
            check(world.residents()[0].job == ribterra::Job::Idle,
                  "command must wait for the server tick boundary");
            const auto accepted_move = await_terminal(world);
            check(accepted_move.status == ribterra::CommandReplyStatus::Applied &&
                      !accepted_move.residents.empty() && accepted_move.residents.front().order_id != 0,
                  "rightclick move must receive acceptance with a stable manual order ID");
            check(await_job(world, ribterra::Job::Move),
                  "rightclick must issue move through logical coordinates");
            for (int i = 0; i < 200; ++i)
                world.advance();
            check(world.tutorial_step() == 1, "movement result must advance tutorial");

            bool issued = false;
            for (int y = 17; y < 32 && !issued; ++y)
                for (int x = 17; x < 32 && !issued; ++x) {
                    const auto p = view.tile_screen(static_cast<float>(x), static_cast<float>(y));
                    if (world.tile(x, y).terrain == ribterra::Terrain::Forest && world.tile(x, y).explored &&
                        p.x > 200 && p.x < 1000 && p.y > 180 && p.y < 700) {
                        click(renderer, window, view, world, p, SDL_BUTTON_RIGHT);
                        issued = await_terminal(world).status == ribterra::CommandReplyStatus::Applied &&
                                 await_job(world, ribterra::Job::Woodcut);
                    }
                }
            check(issued, "resource rightclick must issue gathering");
            const int wood_before = world.resources().wood;
            for (int i = 0; i < 900; ++i)
                world.advance();
            check(world.resources().wood > wood_before, "UI gathering must yield wood");
            check(world.tutorial_step() == 2, "resource yield must advance tutorial");

            const auto count_before = world.buildings().size();
            click(renderer, window, view, world, {470, 848}, SDL_BUTTON_LEFT);
            click(renderer, window, view, world, {980, 35}, SDL_BUTTON_LEFT);
            check(world.buildings().size() == count_before, "HUD must not place a building behind UI");
            click(renderer, window, view, world, view.tile_screen(26, 24), SDL_BUTTON_LEFT);
            check(world.buildings().size() == count_before,
                  "build command must wait for the server tick boundary");
            check(await_terminal(world).status == ribterra::CommandReplyStatus::Applied,
                  "build palette order must receive a terminal Applied reply");
            if (world.buildings().size() != count_before + 1) {
                std::cerr << "can_build=" << world.can_build(ribterra::BuildingKind::House, 26, 24)
                          << " resources=" << world.resources().wood << '/' << world.resources().stone << '/'
                          << world.resources().food << '\n';
                view.compose(world, 12, frame);
                renderer.render(frame, "client-input-failure.png");
            }
            check(world.buildings().size() == count_before + 1, "build palette must place a house");
            check(world.buildings().back().kind == ribterra::BuildingKind::House,
                  "house palette selects correct recipe");
            check(!world.construction_sites().empty() && world.buildings().back().progress == 0,
                  "accepted house must wait for physical materials before work");
            // Free settlers can now be returning from a walk when construction is ordered.
            for (int i = 0; i < 1800 && world.buildings().back().progress < 1.0f; ++i)
                world.advance();
            if (world.buildings().back().progress < 1.0f) {
                std::cerr << "Construction progress=" << world.buildings().back().progress << '\n';
                for (const auto &r : world.residents())
                    std::cerr << r.id << " job=" << int(r.job) << " health=" << r.health << '\n';
            }
            check(world.buildings().back().progress == 1.0f, "construction must complete after UI order");
            check(world.tutorial_step() == 3, "completion must finish tutorial");

            std::string error;
            const auto save = std::filesystem::current_path() / "client-integration.save";
            check(world.save(save, error), "save after UI play failed");
            ribterra::ClientSession restored(1, "Restored client");
            check(restored.load(save, error), "load after UI play failed");
            check(visible_checksum(restored) == visible_checksum(world),
                  "UI play roundtrip changed visible state");
            for (int i = 0; i < 40; ++i) {
                world.advance();
                restored.advance();
            }
            check(visible_checksum(restored) == visible_checksum(world), "resuming saved UI play diverged");
            std::filesystem::remove(save);
            // V02 reveals a wider useful starting valley. Zoom out so this
            // framebuffer check still samples genuinely unknown terrain.
            SDL_Event zoom_out{};
            zoom_out.type = SDL_EVENT_MOUSE_WHEEL;
            zoom_out.wheel.y = -8.0f;
            view.handle_event(zoom_out, world);
            view.compose(world, 12, frame);
            renderer.render(frame, "client-integration.png");
            SDL_Surface *pixels = SDL_LoadPNG("client-integration.png");
            check(pixels != nullptr, "load captured framebuffer failed");
            for (float y : {180.0f, 280.0f, 380.0f}) {
                const auto physical = renderer.logical_to_window({20.0f, y});
                Uint8 red{}, green{}, blue{}, alpha{};
                check(SDL_ReadSurfacePixel(pixels, static_cast<int>(physical.x), static_cast<int>(physical.y),
                                           &red, &green, &blue, &alpha),
                      "fog pixel read failed");
                if (!(red >= 80 && blue > red && green > red && alpha == 255))
                    throw std::runtime_error("unknown map must be opaque blue-gray fog: rgba=" +
                                             std::to_string(red) + "," + std::to_string(green) + "," +
                                             std::to_string(blue) + "," + std::to_string(alpha));
            }
            SDL_DestroySurface(pixels);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Compact-window input, move, harvest, construction, tutorial, save and resume "
                     "passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " SDL=" << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
}

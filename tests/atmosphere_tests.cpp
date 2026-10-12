#include "ribterra/seasons.hpp"
#include "ribterra/terrain_appearance.hpp"
#include "ribterra/ecology_appearance.hpp"
#include "ribterra/world_state.hpp"
#include "ribterra/terrain_surface.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ribterra;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        check(days_per_season==10 && calendar_view(CalendarTick{9*ticks_per_day}).season==Season::Spring &&
              calendar_view(CalendarTick{10*ticks_per_day}).season==Season::Summer &&
              calendar_view(CalendarTick{20*ticks_per_day}).season==Season::Autumn &&
              calendar_view(CalendarTick{30*ticks_per_day}).season==Season::Winter &&
              calendar_view(CalendarTick{40*ticks_per_day}).season==Season::Spring,"Season length mismatch");
        check(daylight_ticks*tick_seconds==720 && ticks_per_day*tick_seconds==1440,"Day/night duration mismatch");
        check(!is_night(CalendarTick{daylight_ticks-1})&&is_night(CalendarTick{daylight_ticks})&&
              !is_night(CalendarTick{ticks_per_day}),"Day/night boundary mismatch");
        check(calendar_day(CalendarTick{ticks_per_day-1})==1&&calendar_day(CalendarTick{ticks_per_day})==2,"Calendar boundary mismatch");
        check(clock_time(CalendarTick{0})=="06:00"&&clock_time(CalendarTick{daylight_ticks})=="18:00"&&
              clock_time(CalendarTick{ticks_per_day})=="06:00","Clock mismatch");
        check(night_amount(CalendarTick{daylight_ticks/2})==0&&
              night_amount(CalendarTick{daylight_ticks+daylight_ticks/2})==1,"Lighting phases mismatch");
        auto boundary = [](int x, int) { return x < 32 ? 8 : 7; };
        float previous = 0;
        int mixed = 0;
        for (int x = 27; x <= 37; ++x) {
            const auto weights = terrain_material_weights(x, 12, 8, boundary);
            check(std::abs(weights[7] + weights[8] - 1) < .0001f, "Blend is not normalized");
            check(weights[7] >= previous, "Snow transition reverses");
            mixed += weights[7] > .01f && weights[7] < .99f;
            previous = weights[7];
        }
        check(mixed >= 4, "Biome transition is still a single hard tile");
        const auto hidden =
            terrain_material_weights(32, 12, 8, [](int x, int) { return x < 32 ? 8 : unknown_material; });
        check(hidden[8] == 1 && hidden[7] == 0, "Hidden biome leaked into texture blending");
        const auto shore = terrain_material_weights(32, 12, 8, [](int x, int) { return x < 32 ? 8 : 4; });
        check(shore[5] > .1f && shore[3] > .3f, "Coast lacks shallow water and sand");
        check(shore == terrain_material_weights(32,12,4,[](int x,int){return x<32?8:4;}), "Land/water boundary has a seam");
        const auto deep=terrain_material_weights(0,0,4,[](int,int){return 4;});
        check(deep[4]==1 && deep[3]==0, "Deep water became a sand flat");
        const auto narrow_river = terrain_material_weights(
            0, 0, 4, [](int x, int) { return std::abs(x) < 2 ? 4 : 0; });
        check(narrow_river[4] + narrow_river[5] > .85f && narrow_river[0] < .1f,
              "Land blend swallowed the blue core of a narrow river");
        int willows=0;
        for(unsigned seed=0;seed<1000;++seed) {
            willows+=ecological_tree(Biome::Meadow,1,false,false,seed)==6;
            check(ecological_tree(Biome::Meadow,6,false,false,seed)!=6,"Inland willow");
            check(ecological_tree(Biome::Mountain,1,false,false,seed)==2 ||
                  ecological_tree(Biome::Mountain,1,false,false,seed)==3,"Mountain tree mismatch");
            check(ecological_tree(Biome::SnowForest,1,false,false,seed)>=8,"Snow forest lacks snow trees");
        }
        check(willows>550 && willows<750,"Willows are not predominantly riparian");
        check(ecological_grass(Biome::Forest,6)[2]>.7f,"Forest floor missing");
        for(auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
            auto ramp=bridge_approach(0,0,dx,dy);
            check(ramp[1].first*dx+ramp[1].second*dy>1.f,"Bridge ramp fails to reach dry land");
        }
        auto coast=[](int x,int){return Tile{x<0?Terrain::Water:Terrain::Grass,0,true,Biome::Meadow,80};};
        check(terrain_corner(0,0,coast)==0 && terrain_corner(1,0,coast)>0 &&
              terrain_corner(1,0,coast)<terrain_corner(2,0,coast) && terrain_corner(3,0,coast)==80,
              "Coast is not a gradual slope to flat water");
        auto known = [](int x, int) { return x < 32; };
        previous = 0;
        for (float x = 27.5f; x <= 31.5f; x += .25f) {
            const float fog = frontier_fog(x, 12.5f, known);
            check(fog >= previous && fog >= 0 && fog <= 1, "Fog frontier is not a smooth bounded falloff");
            previous = fog;
        }
        check(frontier_fog(31.5f, 12.5f, known) == 1, "Terrain edge is not fully hidden");
        check(frontier_fog(25, 12, known) == 0, "Mist covers clear interior");
        check(starting_vision_radius == 11 * 1.5f && resident_vision_radius == 3 * 1.5f &&
                  entrance_vision_radius == 4 * 1.5f,
              "Vision ratio changed");
        World sight(70421, 1);
        check(sight.tile(40, 28).explored && !sight.tile(40, 29).explored,
              "Starting view is not a 16.5-cell circle");
        auto state = sight.export_state();
        for (auto &chunk : state.chunks)
            if (chunk.key.layer == WorldLayer::Surface)
                for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                    int x = chunk.key.cx * chunk_size + int(i % chunk_size),
                        y = chunk.key.cy * chunk_size + int(i / chunk_size);
                    if (x >= 75 && x <= 88 && y >= 74 && y <= 86)
                        chunk.tiles[i] = {Terrain::Grass, 0, y == 80 && (x == 80 || x == 81)};
                }
        auto &r = state.residents.front();
        r.x = r.target_x = 80;
        r.y = r.target_y = 80;
        std::string error;
        check(sight.import_state(std::move(state), error), error.c_str());
        check(sight.move({1}, 81, 80).accepted, "Vision movement rejected");
        for (int i = 0; i < 100; ++i)
            sight.tick();
        check(sight.tile(85, 80).explored && sight.tile(84, 83).explored && !sight.tile(86, 80).explored &&
                  !sight.tile(85, 83).explored,
              "Moving resident vision is not a 4.5-cell circle");
        World fed(70421, 2);
        for (int i = 0; i < 1600; ++i)
            fed.tick();
        for (auto &resident : fed.residents())
            check(resident.health == 100 && resident.hunger == 0, "Fed resident lost health without cause");
        World hungry(70421, 1);
        hungry.stop({1}); // Keep this starvation fixture from foraging independently.
        state = hungry.export_state();
        std::erase_if(state.item_stacks, [](const auto &s) { return s.item == ItemKind::Food; });
        state.resources.food = 0;
        check(hungry.import_state(std::move(state), error), error.c_str());
        for (int i = 0; i < 800; ++i)
            hungry.tick();
        check(hungry.residents()[0].health == 100 && hungry.residents()[0].hunger == 4,
              "Damage occurred before starvation threshold");
        for (int i = 0; i < 200; ++i)
            hungry.tick();
        check(hungry.residents()[0].health == 99 && hungry.residents()[0].hunger == 5,
              "Starvation cause does not match displayed rule");
        auto refill = fed.export_state().item_stacks;
        state = hungry.export_state();
        for (auto stack : refill)
            if (stack.item == ItemKind::Food) {
                stack.stack_id = state.next_item_stack_id++;
                state.item_stacks.push_back(stack);
                state.resources.food = int(stack.quantity);
            }
        check(hungry.import_state(std::move(state), error), error.c_str());
        for (int i = 0; i < 400; ++i)
            hungry.tick();
        check(hungry.residents()[0].health == 99 && hungry.residents()[0].hunger < 5,
              "Health kept falling after meals resumed");
        std::cout << "Gradual biome blending, soft ground fog, exact 1.5x vision and starvation-only health "
                     "loss passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

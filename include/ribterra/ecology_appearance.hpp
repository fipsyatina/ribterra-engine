#pragma once
#include "ribterra/biomes.hpp"
#include <array>
#include <cmath>

namespace ribterra {
// Only revealed terrain contributes, so vegetation never discloses hidden rivers.
template<class Sample> float water_distance(float x, float y, const Sample &sample) {
    float distance=6.f;
    for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
        const int sx=int(std::floor(x))+dx, sy=int(std::floor(y))+dy;
        const auto tile=sample(sx,sy);
        if(tile.explored && tile.terrain==Terrain::Water)
            distance=std::min(distance,std::hypot(sx-x,sy-y));
    }
    return distance;
}
inline int ecological_tree(Biome biome, float water, bool winter, bool autumn, unsigned seed) {
    const auto choice=biome_hash(seed)%100;
    const bool mountain=biome==Biome::Mountain || biome==Biome::SnowMountain;
    const bool forest=biome==Biome::Forest || biome==Biome::SnowForest;
    if(winter || snowy(biome)) {
        if(mountain) return choice<65 ? 10 : 11;
        if(forest) return choice<55 ? 10 : choice<85 ? 11 : 9;
        return choice<60 ? 9 : 8;
    }
    if(mountain || biome==Biome::Desert) return choice<60 ? 2 : 3;
    if(water<=2.25f) return choice<65 ? 6 : choice<90 ? 1 : 0;
    int tree;
    if(forest) tree=choice<35 ? 2 : choice<55 ? 3 : choice<75 ? 0 : choice<90 ? 1 : 7;
    else tree=choice<45 ? 0 : choice<75 ? 1 : 7;
    if(autumn && (tree==0 || tree==7)) return 4;
    if(autumn && tree==1) return 5;
    return tree;
}
inline std::array<float,5> ecological_grass(Biome biome,float water) {
    if(biome==Biome::Forest || biome==Biome::SnowForest) return {.05f,.15f,.8f,0,0};
    if(biome==Biome::Desert || biome==Biome::Mountain || biome==Biome::SnowMountain)
        return {0,.15f,.1f,.75f,0};
    if(water<=2.25f) return {.8f,.15f,.05f,0,0};
    return {.5f,.2f,0,.05f,.25f};
}
// Each ramp overlaps its bridge and reaches the adjacent dry cell's center.
inline std::array<std::pair<float,float>,4> bridge_approach(int x,int y,int dx,int dy) {
    constexpr float half=.43f, start=.3f, end=1.15f;
    const float px=-float(dy)*half, py=float(dx)*half;
    return {{{x+dx*start+px,y+dy*start+py},{x+dx*end+px,y+dy*end+py},
             {x+dx*end-px,y+dy*end-py},{x+dx*start-px,y+dy*start-py}}};
}
}

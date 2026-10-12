#include "ribterra/art_alpha.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using Pixel = std::array<std::uint8_t,4>;
void check(bool ok) { if (!ok) throw std::runtime_error("Artwork matte decode failed"); }
int main() {
    try {
        Pixel background{255,0,255,255}; ribterra::decode_art_magenta(background.data());
        check(background==Pixel{0,0,0,0});
        for (Pixel material : {Pixel{140,130,110,255},Pixel{230,231,228,255},Pixel{51,74,35,255},Pixel{240,170,30,255}}) {
            auto decoded=material;ribterra::decode_art_magenta(decoded.data());check(decoded==material);
        }
        Pixel edge{190,65,182,255};ribterra::decode_art_magenta(edge.data());
        check(edge[3]>30 && edge[3]<240 && edge[0]-edge[1]<70 && edge[2]-edge[1]<70);
        Pixel leaf_edge{255,170,180,255};ribterra::decode_art_magenta(leaf_edge.data(),true);
        check(leaf_edge[0]<=leaf_edge[1]+8 && leaf_edge[2]<=leaf_edge[1]);
        Pixel autumn{220,160,40,255};auto autumn_copy=autumn;
        ribterra::decode_art_magenta(autumn.data(),true);check(autumn==autumn_copy);
        Pixel black{0,0,0,255};ribterra::decode_art_luminance(black.data());check(black[3]==0);
        Pixel smoke{128,128,128,200};ribterra::decode_art_luminance(smoke.data());
        check(smoke==Pixel{255,255,255,100});
        std::cout<<"Artwork matte and edge checks passed\n";
    } catch(const std::exception &e) {std::cerr<<e.what();return 1;}
}

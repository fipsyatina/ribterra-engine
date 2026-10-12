bool contact_world_valid(float world_tag)
{
    return world_tag == 1.0 || world_tag == 2.0;
}

#define RIBTERRA_GROUND_WORLD_VALID(world_tag) contact_world_valid(world_tag)
#include "ground_lighting.frag.hlsl"

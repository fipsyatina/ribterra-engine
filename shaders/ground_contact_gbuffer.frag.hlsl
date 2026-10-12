float contact_receiver_tag(uint4 material_flags)
{
    return material_flags.y == 0u ? 1.0 : (material_flags.y <= 3u ? 2.0 : 0.0);
}

#define RIBTERRA_GROUND_WORLD_TAG(material_flags) contact_receiver_tag(material_flags)
#include "ground_gbuffer.frag.hlsl"

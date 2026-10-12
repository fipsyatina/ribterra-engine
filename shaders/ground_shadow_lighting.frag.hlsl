#define RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b2
#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t4
#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s4
#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t5
#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s5
#define RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t6
#include "sky_shadow.hlsli"

#define RIBTERRA_SUN_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)
#define RIBTERRA_MOON_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)
#include "ground_sky_lighting.frag.hlsl"

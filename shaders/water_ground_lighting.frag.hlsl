#define RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b2
#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t7
#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s7
#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t8
#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s8
#define RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t9
#include "sky_shadow.hlsli"

#define RIBTERRA_SUN_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)
#define RIBTERRA_MOON_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)
#define RIBTERRA_MOON_LIGHTING 1
#define RIBTERRA_MOON_LIGHTING_REGISTER b1
#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity;
#define RIBTERRA_WATER_GROUND_LIGHTING 1
#define RIBTERRA_GROUND_WORLD_VALID(world_tag) ((world_tag) == 1.0 || (world_tag) == 2.0)
#include "ground_lighting.frag.hlsl"

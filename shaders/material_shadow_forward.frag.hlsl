#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t5
#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s5
#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t6
#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s6
#define RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t7
#define RIBTERRA_SKY_SHADOW_EMBED_MOON
#include "sky_shadow.hlsli"

#define RIBTERRA_MOON_LIGHTING_REGISTER b3
#define RIBTERRA_MOON_BUFFER_NAME SkyMoonLighting
#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow;
#define RIBTERRA_SUN_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)
#define RIBTERRA_MOON_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)
#include "material_sky_forward.frag.hlsl"

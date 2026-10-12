#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t5
#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s5
#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t6
#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s6
#define RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t7
#define RIBTERRA_SKY_SHADOW_EMBED_MOON
#include "sky_shadow.hlsli"

#define RIBTERRA_LOCAL_OCCLUDER_REGISTER RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER
#include "local_occlusion.hlsli"
#undef RIBTERRA_LOCAL_OCCLUDER_REGISTER

cbuffer ReflectionMetadata : register(b2, space3)
{
    // strength, fade exponent, source reveal, reserved zero
    float4 reflection_response;
    // receiver mask, owner, reserved zero
    uint4 reflection_receiver;
};

#define RIBTERRA_MOON_LIGHTING 1
#define RIBTERRA_MOON_LIGHTING_REGISTER b3
#define RIBTERRA_MOON_BUFFER_NAME SkyMoonLighting
#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow;
#define RIBTERRA_SUN_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)
#define RIBTERRA_MOON_DIRECT_VISIBILITY(position) \
    sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)
#define RIBTERRA_LOCAL_LIGHT_ATTENUATION(position, normal, light) \
    ((((reflection_receiver.x & light.occluder_range_flags.z) & 0xffu) != 0u) \
        ? local_light_visibility(position, normal, light.anchor_radius.xyz, \
                                 light.occluder_range_flags.x, \
                                 light.occluder_range_flags.y, reflection_receiver.y) \
        : 0.0)
#define main lit_source_main
#include "material_sprite.frag.hlsl"
#undef main

float4 main(Input input) : SV_Target0
{
    const float4 source = lit_source_main(input);
    const float fade = pow(saturate(input.uv.y), max(reflection_response.y, 1.0));
    const float alpha = saturate(source.a * reflection_response.x
                               * reflection_response.z * fade);
    return float4(max(source.rgb, 0.0) * alpha, alpha);
}

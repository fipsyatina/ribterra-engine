#ifndef RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER
#define RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t5
#endif
#define RIBTERRA_LOCAL_OCCLUDER_REGISTER RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER
#include "local_occlusion.hlsli"
#undef RIBTERRA_LOCAL_OCCLUDER_REGISTER

cbuffer ForwardMetadata : register(b2, space3)
{
    float4 reveal;
    uint4 receiver;
};

#define RIBTERRA_LOCAL_LIGHT_ATTENUATION(position, normal, light) \
    ((((receiver.x & light.occluder_range_flags.z) & 0xffu) != 0u) \
        ? local_light_visibility(position, normal, light.anchor_radius.xyz, \
                                 light.occluder_range_flags.x, \
                                 light.occluder_range_flags.y, receiver.y) \
        : 0.0)
#define main legacy_material_main
#include "material_sprite.frag.hlsl"
#undef main
#undef RIBTERRA_LOCAL_LIGHT_ATTENUATION

struct ForwardOutput
{
    float4 scene : SV_Target0;
    float4 metadata : SV_Target1;
};

ForwardOutput main(Input input)
{
#ifdef RIBTERRA_ATMOSPHERE
    if (reveal.x == 0.0)
        discard;
#endif
    ForwardOutput output;
    output.scene = legacy_material_main(input);

    uint depth_width, depth_height;
    camera_depth_texture.GetDimensions(depth_width, depth_height);
    const float depth_sample = camera_depth_texture.Sample(
        camera_depth_sampler,
        atlas_uv(input.uv, uv_regions[2], uint2(depth_width, depth_height)));
    const float3 position = reconstruct_position(input.uv, depth_sample);
    const float coverage = saturate(output.scene.a);
    const float depth = dot(position, normalize(float3(1.0, 1.0, 1.0)));
    output.metadata = float4(depth * coverage, saturate(reveal.x) * coverage,
                             0.0, coverage);
    return output;
}

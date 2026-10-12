// Probe-only, quantitative float output. Keep the production reconstruction
// and oct-normal functions byte-unchanged and compile outside its manifest.
#define main production_material_main
#include "material_sprite.frag.hlsl"
#undef main

struct CalibrationOutput
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
};

CalibrationOutput main(Input input)
{
    uint aw, ah, nw, nh, dw, dh;
    albedo_texture.GetDimensions(aw, ah);
    normal_material_texture.GetDimensions(nw, nh);
    camera_depth_texture.GetDimensions(dw, dh);
    const float coverage = albedo_texture.Sample(albedo_sampler,
        atlas_uv(input.uv, uv_regions[0], uint2(aw, ah))).a * input.color.a;
    const float4 material = normal_material_texture.Sample(normal_material_sampler,
        atlas_uv(input.uv, uv_regions[1], uint2(nw, nh)));
    const float depth = camera_depth_texture.Sample(camera_depth_sampler,
        atlas_uv(input.uv, uv_regions[2], uint2(dw, dh)));
    // ambient.w is a strictly zero diagnostic sentinel. Referencing it keeps
    // b0/b1 slots identical to the production ABI after shader optimization.
    const float checked_coverage = coverage + ambient.w;
    CalibrationOutput output;
    output.position = float4(reconstruct_position(input.uv, depth), checked_coverage);
    output.normal = float4(decode_octahedral(material.rg), checked_coverage);
    return output;
}

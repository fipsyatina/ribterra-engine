#define RIBTERRA_CONTACT_TARGET_FORMAT R8G8B8A8_UNORM_SRGB
#define RIBTERRA_CONTACT_COLOR_WRITE_MASK A
#define RIBTERRA_CONTACT_BLEND ZERO_ONE_MINUS_SRC_ALPHA_ADD
#define RIBTERRA_CONTACT_ACTIVE_TARGET_READS 0

Texture2D<float4> surface_world_texture : register(t0, space2);
SamplerState surface_world_sampler : register(s0, space2);
Texture2D<float4> surface_flags_texture : register(t1, space2);
SamplerState surface_flags_sampler : register(s1, space2);
Texture2D<float> contact_mask_texture : register(t2, space2);
SamplerState contact_mask_sampler : register(s2, space2);

cbuffer ContactSurface : register(b0, space3)
{
    float4 anchor_opacity;
    float4 axis_u_extent;
    float4 axis_v_extent;
    float4 uv_region;
    float4 pivot_plane;
};

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0
{
    const float4 world = surface_world_texture.SampleLevel(surface_world_sampler, input.uv, 0.0);
    const float4 flags = surface_flags_texture.SampleLevel(surface_flags_sampler, input.uv, 0.0);
    if (world.w != 2.0 || flags.a != 1.0)
        discard;

    const float3 normal_unnormalized = cross(axis_u_extent.xyz, axis_v_extent.xyz);
    const float normal_length_squared = dot(normal_unnormalized, normal_unnormalized);
    if (normal_length_squared <= 1.0e-12)
        discard;
    const float3 normal = normal_unnormalized * rsqrt(normal_length_squared);
    const float3 offset = world.xyz - anchor_opacity.xyz;
    if (normal.z <= 0.0 || abs(dot(offset, normal)) > pivot_plane.z)
        discard;

    const float u = pivot_plane.x + dot(offset, axis_u_extent.xyz) / (2.0 * axis_u_extent.w);
    const float v = pivot_plane.y + dot(offset, axis_v_extent.xyz) / (2.0 * axis_v_extent.w);
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
        discard;

    const float2 mask_uv = uv_region.xy + float2(u, v) * uv_region.zw;
    const float mask = contact_mask_texture.SampleLevel(contact_mask_sampler, mask_uv, 0.0);
    return float4(0.0, 0.0, 0.0, saturate(mask * anchor_opacity.w));
}

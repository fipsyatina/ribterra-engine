Texture2D<float4> albedo_texture : register(t0, space2);
SamplerState albedo_sampler : register(s0, space2);

cbuffer GroundMaterial : register(b0, space3)
{
    float4 response;
    uint4 flags;
    float4 visibility;
};

#ifdef RIBTERRA_WATER_GBUFFER
cbuffer WaterGround : register(b1, space3)
{
    // R water coverage, G surface class / 255, B shore mask / 15,
    // A reflection permission. Contact classification remains SurfaceWorld.W.
    float4 water_receiver;
};
#endif

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

struct Output
{
    float4 albedo : SV_Target0;
    float4 world : SV_Target1;
    float4 material : SV_Target2;
    float4 surface_flags : SV_Target3;
#ifdef RIBTERRA_WATER_GBUFFER
    float4 water_coverage : SV_Target4;
#endif
};

#ifndef RIBTERRA_GROUND_WORLD_TAG
#define RIBTERRA_GROUND_WORLD_TAG(material_flags) 1.0
#endif

float3 safe_normalize(float3 value, float3 fallback)
{
    const float squared_length = dot(value, value);
    return squared_length > 1.0e-12 ? value * rsqrt(squared_length) : fallback;
}

float2 encode_octahedral(float3 value)
{
    float3 normal = safe_normalize(value, float3(0.0, 0.0, 1.0));
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    if (normal.z < 0.0)
    {
        const float2 signs = float2(normal.x >= 0.0 ? 1.0 : -1.0,
                                    normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signs;
    }
    return normal.xy * 0.5 + 0.5;
}

Output main(Input input)
{
    // The typed source sampler decodes authored sRGB to linear. The sRGB MRT
    // re-encodes storage; GroundLighting decodes that intermediate once when
    // sampling it. There is no manual shader decode. Vertex tint stays white.
    const float4 sampled = albedo_texture.Sample(albedo_sampler, input.uv) * input.color;
    Output output;
    output.albedo = float4(sampled.rgb, saturate(visibility.x));
    output.world = float4(input.world_position, RIBTERRA_GROUND_WORLD_TAG(flags));
    output.material = float4(encode_octahedral(input.normal),
                             saturate(response.x), saturate(visibility.y));
    output.surface_flags = float4(float(flags.x & 255u) / 255.0,
                                  saturate(response.y), saturate(response.z),
                                  saturate(response.w));
#ifdef RIBTERRA_WATER_GBUFFER
    output.water_coverage = saturate(water_receiver);
#endif
    return output;
}

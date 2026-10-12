#include "color_transfer.hlsli"

Texture2D<float4> image_texture : register(t0, space2);
SamplerState image_sampler : register(s0, space2);

cbuffer WorldAlpha : register(b0, space3)
{
    uint4 alpha_mode; // x=1 for encoded premultiplied input; reserved fields zero.
};

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(Input input) : SV_Target0
{
    const float4 sampled = image_texture.Sample(image_sampler, input.uv) * input.color;
    if (alpha_mode.x == 1u)
    {
        const float3 straight = sampled.a > 0.0 ? sampled.rgb / sampled.a : float3(0.0, 0.0, 0.0);
        return float4(linear_from_srgb(saturate(straight)) * sampled.a, sampled.a);
    }
    return float4(linear_from_srgb(saturate(sampled.rgb)), sampled.a);
}

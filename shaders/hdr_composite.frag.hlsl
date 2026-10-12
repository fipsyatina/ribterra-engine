#include "color_transfer.hlsli"

Texture2D<float4> scene_linear : register(t0, space2);
SamplerState scene_sampler : register(s0, space2);

cbuffer Composite : register(b0, space3)
{
    float4 exposure; // x > 0; reserved fields zero. SDR clamp only here.
};

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(Input input) : SV_Target0
{
    const float4 scene = scene_linear.Sample(scene_sampler, input.uv);
    return float4(saturate(srgb_from_linear(scene.rgb * exposure.x)), 1.0);
}

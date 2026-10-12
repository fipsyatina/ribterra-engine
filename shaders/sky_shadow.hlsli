#ifndef RIBTERRA_SKY_SHADOW_HLSLI
#define RIBTERRA_SKY_SHADOW_HLSLI

#ifndef RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER
#error RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER must name the sun shadow texture register
#endif
#ifndef RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER
#error RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER must name the sun shadow sampler register
#endif
#ifndef RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER
#error RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER must name the moon shadow texture register
#endif
#ifndef RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER
#error RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER must name the moon shadow sampler register
#endif

struct ShadowSourceGpu
{
    float4 u_min;
    float4 v_min;
    float4 f_min;
    float4 inverse_span_enabled;
    float4 bias_texel;
};

#ifndef RIBTERRA_SKY_SHADOW_EMBED_MOON
#ifndef RIBTERRA_SKY_SHADOW_BUFFER_REGISTER
#error RIBTERRA_SKY_SHADOW_BUFFER_REGISTER must name the SkyShadows register
#endif
cbuffer SkyShadows : register(RIBTERRA_SKY_SHADOW_BUFFER_REGISTER, space3)
{
    ShadowSourceGpu sun_shadow;
    ShadowSourceGpu moon_shadow;
};
#endif

Texture2D<float> sun_shadow_texture
    : register(RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER, space2);
SamplerState sun_shadow_sampler
    : register(RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER, space2);
Texture2D<float> moon_shadow_texture
    : register(RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER, space2);
SamplerState moon_shadow_sampler
    : register(RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER, space2);

float sky_shadow_visibility(float3 position, ShadowSourceGpu source,
                            Texture2D<float> shadow_texture,
                            SamplerState shadow_sampler)
{
    if (source.inverse_span_enabled.w == 0.0)
        return 1.0;
    if (any(abs(position) > float3(32.0, 32.0, 32.0)))
        return 1.0;

    const float2 uv = float2(
        (dot(position, source.u_min.xyz) - source.u_min.w) * source.inverse_span_enabled.x,
        (dot(position, source.v_min.xyz) - source.v_min.w) * source.inverse_span_enabled.y);
    const float receiver_depth =
        (dot(position, source.f_min.xyz) - source.f_min.w) * source.inverse_span_enabled.z;
    if (any(uv < 0.0) || any(uv > 1.0)
        || receiver_depth <= 0.0 || receiver_depth >= 1.0)
        return 1.0;

    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 tap_uv = uv + float2(x, y) * source.bias_texel.y;
            if (any(tap_uv < 0.0) || any(tap_uv > 1.0))
            {
                visibility += 1.0;
                continue;
            }
            const float stored_depth = shadow_texture.SampleLevel(shadow_sampler, tap_uv, 0.0);
            visibility += receiver_depth - source.bias_texel.x <= stored_depth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

#endif

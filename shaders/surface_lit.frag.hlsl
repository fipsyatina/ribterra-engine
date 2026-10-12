Texture2D<float4> image_texture : register(t0, space2);
SamplerState image_sampler : register(s0, space2);

struct LightGpu
{
    float4 anchor_radius;
    float4 direction_cone;
    float4 color_intensity;
    uint4 occluder_range_flags;
    float4 height_falloff_reveal;
};

cbuffer SurfaceLighting : register(b0, space3)
{
    float4 ambient;
    float4 sun_direction;
    float4 sun_color_intensity;
    uint4 counts;
    LightGpu lights[4];
};

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

float3 safe_normalize(float3 value, float3 fallback)
{
    const float squared_length = dot(value, value);
    return squared_length > 1.0e-12 ? value * rsqrt(squared_length) : fallback;
}

float srgb_channel_to_linear(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float3 srgb_to_linear(float3 value)
{
    return float3(srgb_channel_to_linear(value.r), srgb_channel_to_linear(value.g),
                  srgb_channel_to_linear(value.b));
}

float linear_channel_to_srgb(float value)
{
    value = max(value, 0.0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 linear_to_srgb(float3 value)
{
    return float3(linear_channel_to_srgb(value.r), linear_channel_to_srgb(value.g),
                  linear_channel_to_srgb(value.b));
}

float4 main(Input input) : SV_Target0
{
    const float4 sampled = image_texture.Sample(image_sampler, input.uv) * input.color;
    const float3 albedo = srgb_to_linear(saturate(sampled.rgb));
    const float3 normal = safe_normalize(input.normal, float3(0.0, 0.0, 1.0));
    const float3 sun_to_source = safe_normalize(sun_direction.xyz, float3(0.0, 0.0, 1.0));
    float3 illumination = ambient.rgb;
    illumination += sun_color_intensity.rgb * sun_color_intensity.a
                    * max(dot(normal, sun_to_source), 0.0);

    const uint light_count = min(counts.x, 4u);
    for (uint index = 0; index < light_count; ++index)
    {
        const LightGpu light = lights[index];
        const float3 to_light = light.anchor_radius.xyz - input.world_position;
        const float distance_squared = dot(to_light, to_light);
        const float3 light_to_source = distance_squared > 1.0e-12
            ? to_light * rsqrt(distance_squared) : float3(0.0, 0.0, 0.0);
        const float radius_squared = light.anchor_radius.w * light.anchor_radius.w;
        const float radial = radius_squared > 1.0e-12
            ? saturate(1.0 - distance_squared / radius_squared) : 0.0;
        float attenuation = pow(radial, max(light.height_falloff_reveal.y, 0.0))
                            * max(light.height_falloff_reveal.z, 0.0);

        const bool point_source = light.direction_cone.w == -1.0
                                  && light.height_falloff_reveal.x == -1.0;
        if (!point_source)
        {
            const float3 direction = safe_normalize(light.direction_cone.xyz,
                                                    float3(0.0, 0.0, -1.0));
            const float cone = dot(-light_to_source, direction);
            attenuation *= smoothstep(light.direction_cone.w,
                                      light.height_falloff_reveal.x, cone);
        }

        illumination += light.color_intensity.rgb * light.color_intensity.a
                        * max(dot(normal, light_to_source), 0.0) * attenuation;
    }

#ifdef SURFACE_LINEAR_HDR
    return float4(albedo * illumination, 1.0);
#else
    return float4(linear_to_srgb(albedo * illumination), 1.0);
#endif
}

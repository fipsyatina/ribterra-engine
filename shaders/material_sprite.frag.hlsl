Texture2D<float4> albedo_texture : register(t0, space2);
SamplerState albedo_sampler : register(s0, space2);
Texture2D<float4> normal_material_texture : register(t1, space2);
SamplerState normal_material_sampler : register(s1, space2);
Texture2D<float> camera_depth_texture : register(t2, space2);
SamplerState camera_depth_sampler : register(s2, space2);
Texture2D<float> emission_mask_texture : register(t3, space2);
SamplerState emission_mask_sampler : register(s3, space2);
Texture2D<float4> surface_mask_texture : register(t4, space2);
SamplerState surface_mask_sampler : register(s4, space2);

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
#ifdef RIBTERRA_ATMOSPHERE
    float4 atmosphere_origin_time;
    float4 atmosphere_depth;
    float4 atmosphere_density;
    float4 atmosphere_far_color_noise;
    float4 atmosphere_morning_color;
    float4 atmosphere_coastal_color;
    float4 atmosphere_wind_shore;
    uint4 atmosphere_counts_version;
    float4 atmosphere_water_aabbs[32];
#endif
};

#ifdef RIBTERRA_ATMOSPHERE
#include "atmosphere.hlsli"
#ifndef RIBTERRA_ATMOSPHERE_REVEAL
#define RIBTERRA_ATMOSPHERE_REVEAL 1.0
#endif
#endif

cbuffer SpriteSurface : register(b1, space3)
{
    float4 uv_regions[5];
    float4 anchor_export;
    float4 source_size_pivot;
    float4 depth_scale;
    float4 emission;
    float4 response;
};

#ifdef RIBTERRA_MOON_LIGHTING
#ifndef RIBTERRA_MOON_BUFFER_NAME
#define RIBTERRA_MOON_BUFFER_NAME MoonLighting
#endif
cbuffer RIBTERRA_MOON_BUFFER_NAME : register(RIBTERRA_MOON_LIGHTING_REGISTER, space3)
{
    RIBTERRA_MOON_LIGHTING_FIELDS
};
#endif

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

#ifndef RIBTERRA_LOCAL_LIGHT_ATTENUATION
#define RIBTERRA_LOCAL_LIGHT_ATTENUATION(position, normal, light) 1.0
#endif

#ifndef RIBTERRA_SUN_DIRECT_VISIBILITY
#define RIBTERRA_SUN_DIRECT_VISIBILITY(position) 1.0
#endif

#ifndef RIBTERRA_MOON_DIRECT_VISIBILITY
#define RIBTERRA_MOON_DIRECT_VISIBILITY(position) 1.0
#endif

float3 safe_normalize(float3 value, float3 fallback)
{
    const float squared_length = dot(value, value);
    return squared_length > 1.0e-12 ? value * rsqrt(squared_length) : fallback;
}

float2 atlas_uv(float2 local_uv, float4 region, uint2 texture_size)
{
    const float2 texel_half = 0.5 / float2(texture_size);
    const float2 first_center = region.xy + texel_half;
    const float2 last_center = region.xy + region.zw - texel_half;
    return clamp(region.xy + local_uv * region.zw, first_center, last_center);
}

float3 decode_octahedral(float2 encoded)
{
    const float2 value = encoded * 2.0 - 1.0;
    float3 normal = float3(value, 1.0 - abs(value.x) - abs(value.y));
    if (normal.z < 0.0)
    {
        const float2 signs = float2(normal.x >= 0.0 ? 1.0 : -1.0,
                                    normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signs;
    }
    return safe_normalize(normal, float3(0.0, 0.0, 1.0));
}

float3 reconstruct_position(float2 local_uv, float depth_sample)
{
    const float2 source_pixel = local_uv * source_size_pivot.xy;
    const float2 screen_local = (source_pixel - source_size_pivot.zw) / anchor_export.w;
    const float local_depth = lerp(depth_scale.x, depth_scale.y, depth_sample);
    const float a = screen_local.x / 29.0;
    const float b = screen_local.y / 14.5;
    const float q = local_depth * sqrt(3.0);
    const float3 local_position = float3((b + 2.0*q + 3.0*a) / 6.0,
                                         (b + 2.0*q - 3.0*a) / 6.0,
                                         (q - b) / 3.0);
    return anchor_export.xyz + local_position * depth_scale.z;
}

float specular_term(float3 normal, float3 light_direction, float3 view_direction,
                    float roughness, float strength)
{
    const float normal_light = max(dot(normal, light_direction), 0.0);
    const float3 halfway = safe_normalize(light_direction + view_direction, normal);
    const float exponent = lerp(96.0, 8.0, saturate(roughness));
    return pow(max(dot(normal, halfway), 0.0), exponent) * normal_light * strength;
}

float4 main(Input input) : SV_Target0
{
    uint albedo_width, albedo_height;
    uint normal_width, normal_height;
    uint depth_width, depth_height;
    uint emission_width, emission_height;
    uint surface_width, surface_height;
    albedo_texture.GetDimensions(albedo_width, albedo_height);
    normal_material_texture.GetDimensions(normal_width, normal_height);
    camera_depth_texture.GetDimensions(depth_width, depth_height);
    emission_mask_texture.GetDimensions(emission_width, emission_height);
    surface_mask_texture.GetDimensions(surface_width, surface_height);
    // The SRGB texture format performs the only albedo decode. Filtering is
    // therefore linear-light; do not add a shader decode here.
    const float4 albedo_sample = albedo_texture.Sample(albedo_sampler,
        atlas_uv(input.uv, uv_regions[0], uint2(albedo_width, albedo_height)))
                                 * input.color;
    const float4 normal_material = normal_material_texture.Sample(
        normal_material_sampler,
        atlas_uv(input.uv, uv_regions[1], uint2(normal_width, normal_height)));
    const float depth_sample = camera_depth_texture.Sample(
        camera_depth_sampler,
        atlas_uv(input.uv, uv_regions[2], uint2(depth_width, depth_height)));
    const float emission_mask = emission_mask_texture.Sample(
        emission_mask_sampler,
        atlas_uv(input.uv, uv_regions[3], uint2(emission_width, emission_height)));
    const float4 surface_mask = surface_mask_texture.Sample(
        surface_mask_sampler,
        atlas_uv(input.uv, uv_regions[4], uint2(surface_width, surface_height)));

    const float3 position = reconstruct_position(input.uv, depth_sample);
    const float3 normal = decode_octahedral(normal_material.rg);
    const float authored_ao = saturate(normal_material.a);
    const float wetness = saturate(surface_mask.r * response.x);
    const float roughness = saturate(normal_material.b * (1.0 - 0.5 * wetness));
    const float specular_strength = response.y * (1.0 + wetness);
    const float3 view_direction = normalize(float3(1.0, 1.0, 1.0));
    const float3 sun_to_source = safe_normalize(sun_direction.xyz, float3(0.0, 0.0, 1.0));

    float3 radiance = albedo_sample.rgb * ambient.rgb * authored_ao;
    const float sun_diffuse = max(dot(normal, sun_to_source), 0.0);
    const float3 sun_radiance = sun_color_intensity.rgb * sun_color_intensity.a;
    const float sun_direct_visibility = RIBTERRA_SUN_DIRECT_VISIBILITY(position);
    radiance += albedo_sample.rgb * sun_radiance * sun_diffuse * sun_direct_visibility;
    radiance += sun_radiance * specular_term(normal, sun_to_source, view_direction,
                                              roughness, specular_strength)
                * sun_direct_visibility;

#ifdef RIBTERRA_MOON_LIGHTING
    const float3 moon_to_source = safe_normalize(moon_direction.xyz, float3(0.0, 0.0, 1.0));
    const float moon_diffuse = max(dot(normal, moon_to_source), 0.0);
    const float3 moon_radiance = moon_color_intensity.rgb * moon_color_intensity.a;
    const float moon_direct_visibility = RIBTERRA_MOON_DIRECT_VISIBILITY(position);
    radiance += albedo_sample.rgb * moon_radiance * moon_diffuse * moon_direct_visibility;
    radiance += moon_radiance * specular_term(normal, moon_to_source, view_direction,
                                               roughness, specular_strength)
                * moon_direct_visibility;
#endif

    const uint light_count = min(counts.x, 4u);
    for (uint index = 0; index < light_count; ++index)
    {
        const LightGpu light = lights[index];
        const float3 to_light = light.anchor_radius.xyz - position;
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
        const float normal_light = max(dot(normal, light_to_source), 0.0);
        if (attenuation > 0.0 && normal_light > 0.0)
            attenuation *= RIBTERRA_LOCAL_LIGHT_ATTENUATION(position, normal, light);
        const float3 light_radiance = light.color_intensity.rgb * light.color_intensity.a;
        radiance += albedo_sample.rgb * light_radiance
                    * normal_light * attenuation;
        radiance += light_radiance * specular_term(normal, light_to_source, view_direction,
                                                    roughness, specular_strength) * attenuation;
    }

    radiance += emission.rgb * emission.a * emission_mask;
#ifdef RIBTERRA_ATMOSPHERE
    radiance = apply_atmosphere(radiance, position, RIBTERRA_ATMOSPHERE_REVEAL);
#endif
    return float4(radiance, albedo_sample.a);
}

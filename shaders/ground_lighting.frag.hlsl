Texture2D<float4> ground_albedo_texture : register(t0, space2);
SamplerState ground_albedo_sampler : register(s0, space2);
Texture2D<float4> surface_world_texture : register(t1, space2);
SamplerState surface_world_sampler : register(s1, space2);
Texture2D<float4> surface_material_texture : register(t2, space2);
SamplerState surface_material_sampler : register(s2, space2);
Texture2D<float4> surface_flags_texture : register(t3, space2);
SamplerState surface_flags_sampler : register(s3, space2);

#ifdef RIBTERRA_WATER_GROUND_LIGHTING
Texture2D<float4> water_coverage_texture : register(t4, space2);
SamplerState water_coverage_sampler : register(s4, space2);
Texture2D<float4> reflection_texture : register(t5, space2);
SamplerState reflection_sampler : register(s5, space2);
Texture2D<float4> water_normal_texture : register(t6, space2);
SamplerState water_normal_sampler : register(s6, space2);

cbuffer WaterSurface : register(b3, space3)
{
    float4 water_world_origin_time;
    float4 water_flow_response;
    float4 water_tint;
    float4 water_viewport;
};
#endif

#ifndef RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER
#define RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t4
#endif
#define RIBTERRA_LOCAL_OCCLUDER_REGISTER RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER
#include "local_occlusion.hlsli"
#undef RIBTERRA_LOCAL_OCCLUDER_REGISTER

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
#endif

#ifdef RIBTERRA_MOON_LIGHTING
cbuffer MoonLighting : register(RIBTERRA_MOON_LIGHTING_REGISTER, space3)
{
    RIBTERRA_MOON_LIGHTING_FIELDS
};
#endif

struct Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct Output
{
    float4 scene : SV_Target0;
    float4 metadata : SV_Target1;
};

#ifndef RIBTERRA_GROUND_WORLD_VALID
#define RIBTERRA_GROUND_WORLD_VALID(world_tag) (!((world_tag) <= 0.0))
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

float specular_term(float3 normal, float3 light_direction, float3 view_direction,
                    float roughness, float strength)
{
    const float normal_light = max(dot(normal, light_direction), 0.0);
    const float3 halfway = safe_normalize(light_direction + view_direction, normal);
    const float exponent = lerp(96.0, 8.0, saturate(roughness));
    return pow(max(dot(normal, halfway), 0.0), exponent) * normal_light * strength;
}

Output main(Input input)
{
    const float4 world = surface_world_texture.Sample(surface_world_sampler, input.uv);
    Output output;
    if (!RIBTERRA_GROUND_WORLD_VALID(world.w))
    {
        output.scene = 0.0;
        output.metadata = 0.0;
        return output;
    }

    const float4 albedo_ao = ground_albedo_texture.Sample(ground_albedo_sampler, input.uv);
    const float4 material = surface_material_texture.Sample(surface_material_sampler, input.uv);
    const float4 flags = surface_flags_texture.Sample(surface_flags_sampler, input.uv);
    float3 albedo = albedo_ao.rgb;
    float3 normal = decode_octahedral(material.rg);
    float roughness = saturate(material.b * (1.0 - 0.5 * flags.g));
    float specular_strength = saturate(0.05 * (1.0 + flags.g));
#ifdef RIBTERRA_WATER_GROUND_LIGHTING
    const float4 water_receiver = water_coverage_texture.Sample(
        water_coverage_sampler, input.uv);
    const uint water_class = (uint)round(saturate(water_receiver.g) * 255.0);
    const uint shore_mask = (uint)round(saturate(water_receiver.b) * 15.0) & 15u;
    const float water_coverage = saturate(water_receiver.r);
    const bool water_surface = water_class == 2u && flags.a >= (254.5 / 255.0)
                               && water_coverage > 0.0;
    const bool reflection_eligible = water_surface && water_receiver.a >= (254.5 / 255.0);
    float2 water_normal_xy = 0.0;
    if (water_surface)
    {
        const float2 flow = water_flow_response.xy;
        // GBuffer world XY is camera-relative; adding the absolute origin
        // restores the stable world point before applying the normative WFX
        // scale and shown-time phase. Calm wind still has slow visible flow.
        const float2 base_uv = (world.xy + water_world_origin_time.xy) * 0.18;
        const float flow_speed = 0.018 + 0.022 * water_world_origin_time.w;
        const float2 phase = base_uv + flow * (water_world_origin_time.z * flow_speed);
        const float2 secondary_base = base_uv * 1.73;
        const float2 rotated_secondary = float2(
            0.7986355 * secondary_base.x - 0.6018150 * secondary_base.y,
            0.6018150 * secondary_base.x + 0.7986355 * secondary_base.y);
        const float2 phase_b = rotated_secondary
            - flow * (0.61 * water_world_origin_time.z * flow_speed);
        const float2 normal_a = water_normal_texture.Sample(water_normal_sampler, phase).rg * 2.0 - 1.0;
        const float2 normal_b = water_normal_texture.Sample(water_normal_sampler, phase_b).rg * 2.0 - 1.0;
        water_normal_xy = clamp(normal_a + normal_b * 0.55, -1.0, 1.0)
                        * saturate(water_flow_response.w) * water_coverage;
        normal = safe_normalize(float3(water_normal_xy, 1.35), float3(0.0, 0.0, 1.0));
        albedo = lerp(albedo, max(water_tint.rgb, 0.0), 0.65 * water_coverage);
        const float2 cell = frac(world.xy + water_world_origin_time.xy + 0.5);
        float edge = 1.0;
        if ((shore_mask & 1u) != 0u) edge = min(edge, cell.y);
        if ((shore_mask & 2u) != 0u) edge = min(edge, 1.0 - cell.x);
        if ((shore_mask & 4u) != 0u) edge = min(edge, 1.0 - cell.y);
        if ((shore_mask & 8u) != 0u) edge = min(edge, cell.x);
        const float foam_noise = dot(water_normal_xy, float2(0.37, -0.29));
        const float foam = shore_mask != 0u
            ? 1.0 - smoothstep(0.025, 0.13, edge + foam_noise * 0.025)
            : 0.0;
        // Foam is an authored diffuse surface response, never free emission.
        albedo = lerp(albedo, float3(0.72, 0.82, 0.78), foam * 0.42 * water_coverage);
        roughness = lerp(roughness, saturate(water_tint.a), water_coverage);
        specular_strength = lerp(specular_strength,
            saturate(0.18 + 0.42 * (1.0 - roughness)), water_coverage);
    }
#endif
    const float3 view_direction = normalize(float3(1.0, 1.0, 1.0));
    const float3 sun_to_source = safe_normalize(sun_direction.xyz, float3(0.0, 0.0, 1.0));
    const uint receiver_mask = (uint)round(saturate(flags.r) * 255.0) & 0xffu;

    float3 radiance = albedo * ambient.rgb * saturate(albedo_ao.a);
    const float sun_diffuse = max(dot(normal, sun_to_source), 0.0) * saturate(material.a);
    const float3 sun_radiance = sun_color_intensity.rgb * sun_color_intensity.a;
    const float sun_direct_visibility = RIBTERRA_SUN_DIRECT_VISIBILITY(world.xyz);
    radiance += albedo * sun_radiance * sun_diffuse * sun_direct_visibility;
    radiance += sun_radiance * specular_term(normal, sun_to_source, view_direction,
                                              roughness, specular_strength)
                * saturate(material.a) * sun_direct_visibility;

#ifdef RIBTERRA_MOON_LIGHTING
    const float3 moon_to_source = safe_normalize(moon_direction.xyz, float3(0.0, 0.0, 1.0));
    const float moon_diffuse = max(dot(normal, moon_to_source), 0.0);
    const float3 moon_radiance = moon_color_intensity.rgb * moon_color_intensity.a;
    const float moon_direct_visibility = RIBTERRA_MOON_DIRECT_VISIBILITY(world.xyz);
    radiance += albedo * moon_radiance * moon_diffuse * moon_direct_visibility;
    radiance += moon_radiance * specular_term(normal, moon_to_source, view_direction,
                                               roughness, specular_strength)
                * moon_direct_visibility;
#endif

    const uint light_count = min(counts.x, 4u);
    for (uint index = 0; index < light_count; ++index)
    {
        const LightGpu light = lights[index];
        const float3 to_light = light.anchor_radius.xyz - world.xyz;
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
        const uint light_receiver_mask = light.occluder_range_flags.z & 0xffu;
        attenuation *= (receiver_mask & light_receiver_mask) != 0u ? 1.0 : 0.0;
        if (attenuation > 0.0 && normal_light > 0.0)
        {
            attenuation *= local_light_visibility(
                world.xyz, normal, light.anchor_radius.xyz,
                light.occluder_range_flags.x, light.occluder_range_flags.y, 0u);
        }
        const float3 light_radiance = light.color_intensity.rgb * light.color_intensity.a;
        radiance += albedo * light_radiance
                    * normal_light * attenuation;
        radiance += light_radiance * specular_term(normal, light_to_source, view_direction,
                                                    roughness, specular_strength) * attenuation;
    }

#ifdef RIBTERRA_ATMOSPHERE
    radiance = apply_atmosphere(radiance, world.xyz, flags.a);
#endif

#ifdef RIBTERRA_WATER_GROUND_LIGHTING
    if (water_surface)
    {
        uint coverage_width, coverage_height;
        water_coverage_texture.GetDimensions(coverage_width, coverage_height);
        const float reference_height = max(water_viewport.y, 1.0);
        const float scaled_budget = water_viewport.x * float(coverage_height) / reference_height;
        const float2 distortion = water_normal_xy * scaled_budget /
            float2(max(coverage_width, 1u), max(coverage_height, 1u));
        if (reflection_eligible)
        {
            const float4 reflected = reflection_texture.Sample(
                reflection_sampler, saturate(input.uv + distortion));
            // Reflection target RGB is already premultiplied by its alpha.
            radiance = radiance * (1.0 - reflected.a * water_coverage)
                     + reflected.rgb * water_coverage;
        }
    }
#endif

    const float depth = dot(world.xyz, normalize(float3(1.0, 1.0, 1.0)));
    output.scene = float4(max(radiance, 0.0), 1.0);
    output.metadata = float4(depth, saturate(flags.a), 0.0, 1.0);
    return output;
}

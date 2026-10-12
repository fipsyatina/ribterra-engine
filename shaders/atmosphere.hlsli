#ifndef RIBTERRA_ATMOSPHERE_HLSLI
#define RIBTERRA_ATMOSPHERE_HLSLI

float3 apply_atmosphere(float3 radiance, float3 camera_position, float reveal)
{
    if (atmosphere_counts_version.z == 0u)
        return radiance;

    const float2 absolute_xy = camera_position.xy + atmosphere_origin_time.xy;
    const float view_far = -(camera_position.x + camera_position.y);
    const float far_weight = smoothstep(atmosphere_depth.x, atmosphere_depth.y, view_far);
    const float height_weight = exp(-max(camera_position.z - atmosphere_density.z, 0.0)
                                    * atmosphere_density.w);

    float water_weight = 0.0;
    const uint water_count = min(atmosphere_counts_version.x, 32u);
    [loop]
    for (uint index = 0u; index < water_count; ++index)
    {
        const float4 bounds = atmosphere_water_aabbs[index];
        const float2 outside = max(max(bounds.xy - absolute_xy,
                                       absolute_xy - bounds.zw), 0.0);
        const float distance_to_water = length(outside);
        water_weight = max(water_weight,
            1.0 - smoothstep(0.0, atmosphere_wind_shore.w, distance_to_water));
    }

    const float morning_weight = atmosphere_density.x * atmosphere_origin_time.w * height_weight;
    const float coastal_weight = atmosphere_density.y * water_weight * height_weight;
    const float weight_sum = far_weight + morning_weight + coastal_weight;
    if (weight_sum <= 0.0)
        return radiance;

    const float2 noise_position = absolute_xy
        + atmosphere_wind_shore.xy * (atmosphere_origin_time.z * atmosphere_wind_shore.z);
    const float noise_wave = sin(dot(noise_position, float2(1.61803399, 2.41421356))
                                 * 6.28318531);
    const float noise = max(0.0, 1.0 + atmosphere_far_color_noise.w * noise_wave);
    const float optical = atmosphere_depth.z * weight_sum * noise;
    const float opacity = min(atmosphere_depth.w, 1.0 - exp(-max(optical, 0.0)));
    const float amount = opacity * saturate(reveal);
    const float3 atmosphere_color =
        (atmosphere_far_color_noise.rgb * far_weight
         + atmosphere_morning_color.rgb * morning_weight
         + atmosphere_coastal_color.rgb * coastal_weight) / weight_sum;
    return lerp(radiance, atmosphere_color, amount);
}

#endif

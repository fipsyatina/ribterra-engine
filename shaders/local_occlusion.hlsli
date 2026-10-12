#ifndef RIBTERRA_LOCAL_OCCLUSION_HLSLI
#define RIBTERRA_LOCAL_OCCLUSION_HLSLI

#ifndef RIBTERRA_LOCAL_OCCLUDER_REGISTER
#error RIBTERRA_LOCAL_OCCLUDER_REGISTER must name the StructuredBuffer register
#endif

struct OccluderGpu
{
    float4 center_base;
    float4 axis_x_extent;
    float4 axis_y_radius;
    uint4 shape_flags_id;
};

StructuredBuffer<OccluderGpu> local_occluders
    : register(RIBTERRA_LOCAL_OCCLUDER_REGISTER, space2);

static const float local_occlusion_bias = 0.002;
static const float local_occlusion_epsilon = 1.0e-5;
static const float local_occlusion_endpoint_epsilon = 1.0e-7;
static const uint local_occluder_capacity = 256u;
static const uint local_occluders_per_light = 32u;

bool local_occlusion_slab(float origin, float direction, float half_extent,
                          inout float first, inout float last)
{
    const float interior_extent = half_extent - local_occlusion_epsilon;
    if (interior_extent <= 0.0)
        return false;
    if (abs(direction) <= local_occlusion_epsilon)
        return abs(origin) < interior_extent;

    const float reciprocal = 1.0 / direction;
    const float near_value = (-interior_extent - origin) * reciprocal;
    const float far_value = (interior_extent - origin) * reciprocal;
    first = max(first, min(near_value, far_value));
    last = min(last, max(near_value, far_value));
    return last > first;
}

bool local_occlusion_height(float origin, float direction, float base_height,
                            float top_height, inout float first, inout float last)
{
    const float lower = min(base_height, top_height) + local_occlusion_epsilon;
    const float upper = max(base_height, top_height) - local_occlusion_epsilon;
    if (upper <= lower)
        return false;
    if (abs(direction) <= local_occlusion_epsilon)
        return origin > lower && origin < upper;

    const float reciprocal = 1.0 / direction;
    const float near_value = (lower - origin) * reciprocal;
    const float far_value = (upper - origin) * reciprocal;
    first = max(first, min(near_value, far_value));
    last = min(last, max(near_value, far_value));
    return last > first;
}

bool local_occlusion_circle(float2 origin, float2 direction, float radius,
                            float height_first, float height_last)
{
    const float interior_radius = radius - local_occlusion_epsilon;
    const float a = dot(direction, direction);
    if (interior_radius <= 0.0)
        return false;
    if (a <= local_occlusion_epsilon * local_occlusion_epsilon)
        return dot(origin, origin) < interior_radius * interior_radius
               && height_last > height_first;

    // Closest-point form avoids subtracting two large squared terms for a
    // small capsule viewed along a long, bounded camera-relative segment.
    const float closest_t = -dot(origin, direction) / a;
    const float2 closest = origin + direction * closest_t;
    const float interior_squared = interior_radius * interior_radius - dot(closest, closest);
    if (interior_squared <= 0.0)
        return false;
    const float extent_t = sqrt(interior_squared / a);
    const float first = max(height_first, closest_t - extent_t);
    const float last = min(height_last, closest_t + extent_t);
    return last > first;
}

bool local_occlusion_obb(float3 origin, float3 direction, OccluderGpu occluder)
{
    const float2 relative = origin.xy - occluder.center_base.xy;
    const float2 axis_x = occluder.axis_x_extent.xy;
    const float2 axis_y = occluder.axis_y_radius.xy;
    float first = local_occlusion_endpoint_epsilon;
    float last = 1.0 - local_occlusion_endpoint_epsilon;
    return local_occlusion_slab(dot(relative, axis_x), dot(direction.xy, axis_x),
                                occluder.axis_x_extent.z, first, last)
           && local_occlusion_slab(dot(relative, axis_y), dot(direction.xy, axis_y),
                                   occluder.axis_y_radius.z, first, last)
           && local_occlusion_height(origin.z, direction.z, occluder.center_base.w,
                                     occluder.axis_x_extent.w, first, last);
}

bool local_occlusion_capsule(float3 origin, float3 direction, OccluderGpu occluder)
{
    const float2 relative = origin.xy - occluder.center_base.xy;
    const float2 axis_x = occluder.axis_x_extent.xy;
    const float2 axis_y = float2(-axis_x.y, axis_x.x);
    const float2 local_origin = float2(dot(relative, axis_x), dot(relative, axis_y));
    const float2 local_direction = float2(dot(direction.xy, axis_x),
                                          dot(direction.xy, axis_y));
    const float half_length = occluder.axis_x_extent.z;
    const float radius = occluder.axis_y_radius.w;

    float height_first = local_occlusion_endpoint_epsilon;
    float height_last = 1.0 - local_occlusion_endpoint_epsilon;
    if (!local_occlusion_height(origin.z, direction.z, occluder.center_base.w,
                                occluder.axis_x_extent.w, height_first, height_last))
        return false;

    float rectangle_first = height_first;
    float rectangle_last = height_last;
    if (local_occlusion_slab(local_origin.x, local_direction.x, half_length,
                             rectangle_first, rectangle_last)
        && local_occlusion_slab(local_origin.y, local_direction.y, radius,
                                rectangle_first, rectangle_last))
        return true;

    return local_occlusion_circle(local_origin - float2(-half_length, 0.0),
                                  local_direction, radius, height_first, height_last)
           || local_occlusion_circle(local_origin - float2(half_length, 0.0),
                                     local_direction, radius, height_first, height_last);
}

float local_light_visibility(float3 position, float3 normal, float3 light_anchor,
                             uint first_occluder, uint occluder_count, uint receiver_owner)
{
    uint uploaded_count, stride;
    local_occluders.GetDimensions(uploaded_count, stride);
    uploaded_count = min(uploaded_count, local_occluder_capacity);
    if (first_occluder >= uploaded_count)
        return 1.0;
    const uint bounded_count = min(min(occluder_count, local_occluders_per_light),
                                   uploaded_count - first_occluder);
    const float3 origin = position + normal * local_occlusion_bias;
    const float3 direction = light_anchor - origin;
    for (uint offset = 0u; offset < bounded_count; ++offset)
    {
        const OccluderGpu occluder = local_occluders[first_occluder + offset];
        if (occluder.shape_flags_id.y == 0u)
            continue;
        if (receiver_owner != 0u && occluder.shape_flags_id.w == receiver_owner)
            continue;
        const bool blocked = occluder.shape_flags_id.x == 1u
            ? local_occlusion_obb(origin, direction, occluder)
            : (occluder.shape_flags_id.x == 2u
               && local_occlusion_capsule(origin, direction, occluder));
        if (blocked)
            return 0.0;
    }
    return 1.0;
}

#endif

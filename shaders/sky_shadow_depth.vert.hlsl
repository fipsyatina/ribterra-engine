cbuffer ShadowSource : register(b0, space1)
{
    float4 u_min;
    float4 v_min;
    float4 f_min;
    float4 inverse_span_enabled;
    float4 bias_texel;
};

struct Input
{
    float3 position : TEXCOORD0;
};

float4 main(Input input) : SV_Position
{
    const float u = (dot(input.position, u_min.xyz) - u_min.w) * inverse_span_enabled.x;
    const float v = (dot(input.position, v_min.xyz) - v_min.w) * inverse_span_enabled.y;
    const float depth = (dot(input.position, f_min.xyz) - f_min.w) * inverse_span_enabled.z;
    return float4(2.0 * u - 1.0, 1.0 - 2.0 * v, depth, 1.0);
}

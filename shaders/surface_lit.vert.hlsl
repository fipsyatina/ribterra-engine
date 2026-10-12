cbuffer Frame : register(b0, space1)
{
    float4 logical_extent;
};

struct Input
{
    float2 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float3 normal : TEXCOORD4;
};

struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float3 normal : TEXCOORD3;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position.x * 2.0 / logical_extent.x - 1.0,
                             1.0 - input.position.y * 2.0 / logical_extent.y, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.world_position = input.world_position;
    output.normal = input.normal;
    return output;
}

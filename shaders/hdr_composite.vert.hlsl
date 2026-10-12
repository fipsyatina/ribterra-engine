struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Output main(uint vertex_id : SV_VertexID)
{
    const float2 uv = float2((vertex_id << 1u) & 2u, vertex_id & 2u);
    Output output;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

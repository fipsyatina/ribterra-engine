float linear_channel_from_srgb(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float3 linear_from_srgb(float3 value)
{
    return float3(linear_channel_from_srgb(value.r), linear_channel_from_srgb(value.g),
                  linear_channel_from_srgb(value.b));
}

float srgb_channel_from_linear(float value)
{
    value = max(value, 0.0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 srgb_from_linear(float3 value)
{
    return float3(srgb_channel_from_linear(value.r), srgb_channel_from_linear(value.g),
                  srgb_channel_from_linear(value.b));
}

#define RIBTERRA_MOON_LIGHTING
#define RIBTERRA_MOON_LIGHTING_REGISTER b1
#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity;
#include "ground_contact_lighting.frag.hlsl"

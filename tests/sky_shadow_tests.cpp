#include "ribterra/sky_shadow.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace ribterra::render;
std::size_t checks{};

void check(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
    ++checks;
}
void close(float actual, float expected, float tolerance, std::string_view message) {
    check(std::abs(actual - expected) <= tolerance, message);
}
template<class Function>
void reject(Function&& function, std::string_view message) {
    bool rejected{};
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}

SkyShadowTriangle triangle(std::uint32_t mask = 3u, float reveal = 1.f) {
    SkyShadowTriangle result;
    result.vertices = {{{{-1.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}}}};
    result.source_mask = mask;
    result.reveal = reveal;
    return result;
}
SurfaceLightingFrame sun(bool enabled = true) {
    SurfaceLightingFrame lighting;
    lighting.sun_direction = {0.f, 0.f, 1.f, 0.f};
    lighting.sun_color_intensity = {1.f, .8f, .6f, enabled ? 1.f : 0.f};
    return lighting;
}
MoonLightingGpu moon(bool enabled = true) {
    MoonLightingGpu value;
    value.color_intensity[3] = enabled ? 1.f : 0.f;
    return value;
}
bool zero(const ShadowSourceGpu& source) {
    for (const auto* field : {&source.u_min, &source.v_min, &source.f_min,
                              &source.inverse_span_enabled, &source.bias_texel})
        for (const float value : *field) if (value != 0.f) return false;
    return true;
}

void abi_and_defaults() {
    check(sizeof(SkyShadowVertex) == 12, "Sky shadow vertex ABI size changed");
    check(sizeof(SkyShadowTriangle) == 44, "Sky shadow triangle ABI size changed");
    check(sizeof(ShadowSourceGpu) == 80, "Shadow source ABI size changed");
    check(alignof(ShadowSourceGpu) == 16, "Shadow source ABI alignment changed");
    check(sizeof(SkyShadowGpu) == 160, "Sky shadow GPU ABI size changed");
    check(alignof(SkyShadowGpu) == 16, "Sky shadow GPU ABI alignment changed");
    check(sizeof(SkyMoonLightingGpu) == 192, "Combined sky/moon GPU ABI size changed");
    check(alignof(SkyMoonLightingGpu) == 16, "Combined sky/moon GPU ABI alignment changed");
    check(offsetof(SkyMoonLightingGpu, moon) == 0, "Combined moon offset changed");
    check(offsetof(SkyMoonLightingGpu, shadows) == 32, "Combined sky shadows offset changed");
    check(std::is_standard_layout_v<SkyMoonLightingGpu>, "Combined sky/moon is not standard layout");
    check(std::is_trivially_copyable_v<SkyMoonLightingGpu>, "Combined sky/moon is not trivially copyable");
    check(offsetof(ShadowSourceGpu, u_min) == 0, "U/min offset changed");
    check(offsetof(ShadowSourceGpu, v_min) == 16, "V/min offset changed");
    check(offsetof(ShadowSourceGpu, f_min) == 32, "F/min offset changed");
    check(offsetof(ShadowSourceGpu, inverse_span_enabled) == 48, "Inverse span offset changed");
    check(offsetof(ShadowSourceGpu, bias_texel) == 64, "Bias/texel offset changed");
    check(std::is_standard_layout_v<SkyShadowVertex>, "Sky vertex is not standard layout");
    check(std::is_trivially_copyable_v<SkyShadowVertex>, "Sky vertex is not trivially copyable");
    check(std::is_standard_layout_v<SkyShadowTriangle>, "Sky triangle is not standard layout");
    check(std::is_trivially_copyable_v<SkyShadowTriangle>, "Sky triangle is not trivially copyable");
    check(std::is_standard_layout_v<ShadowSourceGpu>, "Shadow source is not standard layout");
    check(std::is_trivially_copyable_v<ShadowSourceGpu>, "Shadow source is not trivially copyable");
    check(std::is_standard_layout_v<SkyShadowGpu>, "Sky GPU block is not standard layout");
    check(std::is_trivially_copyable_v<SkyShadowGpu>, "Sky GPU block is not trivially copyable");
    const SkyShadowTriangle defaults;
    check(defaults.source_mask == 3u, "Default source mask changed");
    check(defaults.reveal == 1.f, "Default reveal changed");
    check(sky_shadow_triangle_capacity == 4096, "Raw triangle cap changed");
    check(sky_shadow_active_triangle_capacity == 1024, "Active per-source cap changed");
    check(sky_shadow_coordinate_bound == 32.f, "Fixed camera cube changed");
    check(sky_shadow_map_resolution == 1024.f, "Shadow resolution contract changed");
}

void preparation_and_oracles() {
    const std::array input{triangle(1u), triangle(2u), triangle(3u), triangle(3u, 0.f)};
    const auto prepared = prepare_sky_shadows(input, sun(), moon());
    check(prepared.first_vertex == std::array<std::uint32_t, 2>{0u, 6u},
          "Per-source first vertices changed");
    check(prepared.vertex_count == std::array<std::uint32_t, 2>{6u, 6u},
          "Per-source vertex counts changed");
    check(prepared.vertices.size() == 12u, "Prepared vertex total changed");
    check(prepared.vertices[0].position == input[0].vertices[0].position,
          "Sun vertex order changed");
    check(prepared.vertices[3].position == input[2].vertices[0].position,
          "Shared sun vertex order changed");
    check(prepared.vertices[6].position == input[1].vertices[0].position,
          "Moon vertex order changed");
    check(prepared.vertices[9].position == input[2].vertices[0].position,
          "Shared moon vertex order changed");

    const auto& source = prepared.gpu.sources[0];
    check(source.u_min[0] == -1.f && source.u_min[1] == 0.f && source.u_min[2] == 0.f,
          "Canonical U basis changed");
    check(source.v_min[0] == 0.f && source.v_min[1] == 1.f && source.v_min[2] == 0.f,
          "Canonical V basis changed");
    check(source.f_min[0] == 0.f && source.f_min[1] == 0.f && source.f_min[2] == -1.f,
          "Canonical forward basis changed");
    const float padded_span = 64.f / (1.f - 4.f / 1024.f);
    close(source.u_min[3], -padded_span * .5f, 1e-5f, "Canonical U minimum changed");
    close(source.v_min[3], -padded_span * .5f, 1e-5f, "Canonical V minimum changed");
    close(source.f_min[3], -32.f - .08f, 1e-6f, "Canonical depth minimum changed");
    close(source.inverse_span_enabled[0], 1.f / padded_span, 1e-7f, "Inverse U span changed");
    close(source.inverse_span_enabled[1], 1.f / padded_span, 1e-7f, "Inverse V span changed");
    close(source.inverse_span_enabled[2], 1.f / 64.16f, 1e-7f, "Inverse depth span changed");
    check(source.inverse_span_enabled[3] == 1.f, "Enabled flag changed");
    close(source.bias_texel[0], .01f / 64.16f, 1e-8f, "Normalized receiver bias changed");
    close(source.bias_texel[1], 1.f / 1024.f, 1e-9f, "Texel size changed");
    check(source.bias_texel[2] == 0.f && source.bias_texel[3] == 0.f,
          "Bias reserved components changed");
    check(prepared.gpu.sources[0].u_min == prepared.gpu.sources[1].u_min &&
          prepared.gpu.sources[0].v_min == prepared.gpu.sources[1].v_min &&
          prepared.gpu.sources[0].f_min == prepared.gpu.sources[1].f_min,
          "Equal sun/moon directions produced unequal fixed windows");

    auto tilted_sun = sun();
    tilted_sun.sun_direction = {.6f, 0.f, .8f, 0.f};
    const auto tilted = prepare_sky_shadows(std::span<const SkyShadowTriangle>{}, tilted_sun, moon(false));
    const auto& tilted_source = tilted.gpu.sources[0];
    const std::array<float, 3> u{tilted_source.u_min[0], tilted_source.u_min[1], tilted_source.u_min[2]};
    const std::array<float, 3> v{tilted_source.v_min[0], tilted_source.v_min[1], tilted_source.v_min[2]};
    const std::array<float, 3> f{tilted_source.f_min[0], tilted_source.f_min[1], tilted_source.f_min[2]};
    const auto dot3 = [](const auto& a, const auto& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    close(dot3(u, u), 1.f, 1e-6f, "Tilted U is not unit length");
    close(dot3(v, v), 1.f, 1e-6f, "Tilted V is not unit length");
    close(dot3(f, f), 1.f, 1e-6f, "Tilted F is not unit length");
    close(dot3(u, v), 0.f, 1e-6f, "Tilted U/V are not orthogonal");
    close(dot3(u, f), 0.f, 1e-6f, "Tilted U/F are not orthogonal");
    close(dot3(v, f), 0.f, 1e-6f, "Tilted V/F are not orthogonal");
    for (const float x : {-32.f, 32.f})
        for (const float y : {-32.f, 32.f})
            for (const float z : {-32.f, 32.f}) {
                const std::array<float, 3> corner{x, y, z};
                const float mapped_u = (dot3(corner, u) - tilted_source.u_min[3]) *
                                       tilted_source.inverse_span_enabled[0];
                const float mapped_v = (dot3(corner, v) - tilted_source.v_min[3]) *
                                       tilted_source.inverse_span_enabled[1];
                check(mapped_u >= 2.f / 1024.f - 1e-6f &&
                      mapped_u <= 1.f - 2.f / 1024.f + 1e-6f,
                      "Cube corner escaped padded U window");
                check(mapped_v >= 2.f / 1024.f - 1e-6f &&
                      mapped_v <= 1.f - 2.f / 1024.f + 1e-6f,
                      "Cube corner escaped padded V window");
            }

    auto rounded_sun = sun();
    rounded_sun.sun_direction = {0.f, 0.f, std::sqrt(1.0005f), 0.f};
    const auto rounded = prepare_sky_shadows(std::span<const SkyShadowTriangle>{},
                                             rounded_sun, moon(false));
    const auto& rounded_source = rounded.gpu.sources[0];
    check(rounded_source.f_min[0] == 0.f && rounded_source.f_min[1] == 0.f &&
          rounded_source.f_min[2] == -1.f,
          "Accepted direction roundoff did not match normalized lighting direction");
    close(rounded_source.u_min[0] * rounded_source.u_min[0] +
          rounded_source.u_min[1] * rounded_source.u_min[1] +
          rounded_source.u_min[2] * rounded_source.u_min[2], 1.f, 1e-6f,
          "Roundoff-control U basis is not unit length");
    close(rounded_source.v_min[0] * rounded_source.v_min[0] +
          rounded_source.v_min[1] * rounded_source.v_min[1] +
          rounded_source.v_min[2] * rounded_source.v_min[2], 1.f, 1e-6f,
          "Roundoff-control V basis is not unit length");
    close(rounded_source.f_min[0] * rounded_source.f_min[0] +
          rounded_source.f_min[1] * rounded_source.f_min[1] +
          rounded_source.f_min[2] * rounded_source.f_min[2], 1.f, 1e-6f,
          "Roundoff-control F basis is not unit length");
}

void disabled_and_capacity() {
    const std::array one{triangle()};
    auto disabled_sun = sun(false);
    auto disabled_moon = moon(false);
    const auto disabled = prepare_sky_shadows(one, disabled_sun, disabled_moon);
    check(zero(disabled.gpu.sources[0]) && zero(disabled.gpu.sources[1]),
          "Disabled source GPU records were not zero");
    check(disabled.vertices.empty(), "Disabled sources retained vertices");
    check(disabled.first_vertex == std::array<std::uint32_t, 2>{0u, 0u},
          "Disabled first vertices changed");
    check(disabled.vertex_count == std::array<std::uint32_t, 2>{0u, 0u},
          "Disabled vertex counts changed");

    disabled_sun = sun(); disabled_sun.sun_color_intensity[0] = 0.f;
    disabled_sun.sun_color_intensity[1] = 0.f; disabled_sun.sun_color_intensity[2] = 0.f;
    disabled_moon = moon(); disabled_moon.color_intensity[0] = 0.f;
    disabled_moon.color_intensity[1] = 0.f; disabled_moon.color_intensity[2] = 0.f;
    const auto colorless = prepare_sky_shadows(one, disabled_sun, disabled_moon);
    check(zero(colorless.gpu.sources[0]) && zero(colorless.gpu.sources[1]),
          "Colorless positive-intensity sources were enabled");

    std::vector<SkyShadowTriangle> maximum(1024, triangle(1u));
    const auto at_capacity = prepare_sky_shadows(maximum, sun(), moon(false));
    check(at_capacity.vertex_count[0] == 3072u, "Active capacity boundary changed");
    maximum.push_back(triangle(1u));
    reject([&] { (void)prepare_sky_shadows(maximum, sun(false), moon(false)); },
           "Active cap was hidden by disabled lighting");
    std::vector<SkyShadowTriangle> raw(4096, triangle(3u, 0.f));
    const auto hidden = prepare_sky_shadows(raw, sun(), moon());
    check(hidden.vertices.empty(), "Reveal-zero triangles consumed active capacity");
    raw.push_back(triangle(3u, 0.f));
    reject([&] { (void)prepare_sky_shadows(raw, sun(), moon()); },
           "Raw triangle cap was exceeded");
}

void triangle_negatives() {
    const auto reject_triangle = [](SkyShadowTriangle value) {
        const std::array rows{value};
        reject([&] { (void)prepare_sky_shadows(rows, sun(false), moon(false)); },
               "Invalid triangle was accepted");
    };
    for (const auto mask : {0u, 4u}) { auto value = triangle(); value.source_mask = mask; reject_triangle(value); }
    for (const float reveal : {-.001f, 1.001f, std::numeric_limits<float>::quiet_NaN()}) {
        auto value = triangle(); value.reveal = reveal; reject_triangle(value);
    }
    for (const float coordinate : {32.001f, -32.001f, std::numeric_limits<float>::infinity(),
                                   std::numeric_limits<float>::quiet_NaN()}) {
        auto value = triangle(3u, 0.f); value.vertices[0].position[0] = coordinate;
        reject_triangle(value);
    }
    auto degenerate = triangle(); degenerate.vertices[2] = degenerate.vertices[1];
    reject_triangle(degenerate);
    auto collinear = triangle(); collinear.vertices[2].position = {3.f, 0.f, 0.f};
    reject_triangle(collinear);
    auto boundary = triangle(); boundary.vertices[0].position = {-32.f, 32.f, -32.f};
    const std::array boundary_rows{boundary};
    const auto accepted = prepare_sky_shadows(boundary_rows, sun(false), moon(false));
    check(accepted.vertices.empty(), "Inclusive cube boundary was rejected");
}

void light_negatives() {
    const std::array input{triangle()};
    const auto reject_sun = [&](SurfaceLightingFrame value) {
        reject([&] { (void)prepare_sky_shadows(input, value, moon(false)); },
               "Invalid sun contract was accepted");
    };
    auto lighting = sun(false); lighting.sun_direction[0] = std::numeric_limits<float>::quiet_NaN();
    reject_sun(lighting);
    lighting = sun(false); lighting.sun_direction[3] = 1.f; reject_sun(lighting);
    lighting = sun(false); lighting.sun_direction = {0.f, 0.f, 0.f, 0.f}; reject_sun(lighting);
    lighting = sun(false); lighting.sun_color_intensity[0] = -.001f; reject_sun(lighting);
    lighting = sun(false); lighting.sun_color_intensity[1] = 64.001f; reject_sun(lighting);
    lighting = sun(false); lighting.sun_color_intensity[3] = std::numeric_limits<float>::infinity();
    reject_sun(lighting);
    lighting = sun(); lighting.sun_direction = {1.f, 0.f, 0.f, 0.f}; reject_sun(lighting);
    lighting = sun(); lighting.sun_direction = {std::sqrt(1.f - sky_shadow_min_direction_z *
        sky_shadow_min_direction_z), 0.f, sky_shadow_min_direction_z, 0.f};
    (void)prepare_sky_shadows(input, lighting, moon(false));
    check(true, "Exact 20-degree sun boundary was rejected");
    lighting.sun_direction[2] = sky_shadow_min_direction_z - .0001f;
    lighting.sun_direction[0] = std::sqrt(1.f - lighting.sun_direction[2] * lighting.sun_direction[2]);
    reject([&] { (void)prepare_sky_shadows(input, lighting, moon(false)); },
           "Normalized sun direction 0.0001 below the 20-degree horizon was accepted");

    auto moon_value = moon(false); moon_value.direction[3] = 1.f;
    reject([&] { (void)prepare_sky_shadows(input, sun(false), moon_value); },
           "Disabled malformed moon was accepted");
    moon_value = moon(); moon_value.direction = {1.f, 0.f, 0.f, 0.f};
    reject([&] { (void)prepare_sky_shadows(input, sun(false), moon_value); },
           "Enabled below-horizon moon was accepted");
}
} // namespace

int main() {
    try {
        abi_and_defaults();
        preparation_and_oracles();
        disabled_and_capacity();
        triangle_negatives();
        light_negatives();
        std::cout << "Sky shadow CPU contract: " << checks << " checks passed\n";
        return checks >= 51 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Sky shadow CPU contract failed after " << checks << " checks: "
                  << error.what() << '\n';
        return 1;
    }
}

#include "ribterra/world_projection.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace ribterra::render;

void check(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

bool close(float lhs, float rhs, float tolerance) {
    return std::abs(lhs - rhs) <= tolerance;
}

void check_xyz(const std::array<float, 3>& actual, const std::array<float, 3>& expected,
               float tolerance, std::string_view message) {
    check(close(actual[0], expected[0], tolerance) &&
              close(actual[1], expected[1], tolerance) &&
              close(actual[2], expected[2], tolerance),
          message);
}

void reference_basis_roundtrips() {
    check(reference_tile_width == 58.f && reference_tile_height == 29.f &&
              reference_tile_half_width == 29.f && reference_tile_half_height == 14.5f,
          "Reference rhombus constants changed");
    check(close(render_z_from_legacy_elevation(29.f), 2.4f, .00001f) &&
              close(legacy_elevation_from_render_z(2.4f), 29.f, .00001f),
          "Legacy elevation/render-Z conversion is not canonical");

    const std::array<std::array<float, 3>, 8> points{{
        {12.f, 8.f, render_z_from_legacy_elevation(18.f)},   // roof ridge
        {11.5f, 8.f, render_z_from_legacy_elevation(9.f)},  // facade
        {11.25f, 8.75f, render_z_from_legacy_elevation(0.f)}, // feet
        {9.f, 13.f, render_z_from_legacy_elevation(4.f)},   // terrain
        {-7.5f, 3.25f, render_z_from_legacy_elevation(-12.f)},
        {-4.f, -9.f, render_z_from_legacy_elevation(32.f)},
        {0.f, 0.f, render_z_from_legacy_elevation(-32.f)},
        {27.75f, -18.5f, render_z_from_legacy_elevation(96.f)}}};
    for (const auto& point : points) {
        const auto projected = project_render_xyz(point);
        const auto reconstructed = reconstruct_render_xyz(projected, camera_depth(point));
        check_xyz(reconstructed, point, .0001f,
                  "Affine projection/camera-depth inverse did not round-trip a control point");
        const auto xy = reconstruct_render_xy(projected, point[2]);
        check(close(xy.x, point[0], .0001f) && close(xy.y, point[1], .0001f),
              "Known-Z affine inverse did not round-trip a control point");
    }
}

void camera_relative_large_origins() {
    const std::array<float, 3> camera{1'000'000.f, -1'000'000.f, 8'000.f};
    const std::array<float, 3> world{1'000'012.25f, -999'990.5f, 8'002.75f};
    const auto relative = camera_relative_render_xyz(world, camera);
    check_xyz(relative, {12.25f, 9.5f, 2.75f}, 0.f,
              "Camera-relative subtraction lost a representable large-origin offset");
    const auto reconstructed = reconstruct_render_xyz(project_render_xyz(relative),
                                                       camera_depth(relative));
    check_xyz(reconstructed, relative, .0001f,
              "Large-origin camera-relative point did not survive the affine basis");
}

void depth_r16_and_asset_pixel_calibration() {
    constexpr float depth_min = -96.f;
    constexpr float depth_max = 96.f;
    constexpr Point pivot{256.f, 192.f};
    constexpr float export_scale = 4.f;
    const std::array<std::array<float, 3>, 4> points{{
        {2.25f, -1.75f, render_z_from_legacy_elevation(24.f)}, // roof
        {2.25f, -1.75f, render_z_from_legacy_elevation(10.f)}, // facade
        {1.8f, -1.2f, render_z_from_legacy_elevation(0.f)},    // feet
        {-3.5f, 4.25f, render_z_from_legacy_elevation(-8.f)}}}; // terrain

    for (const float zoom : {.62f, 1.30f, 3.f}) {
        for (const auto& point : points) {
            const auto projected = project_render_xyz(point);
            const auto authored_source = source_pixel_from_reference_offset(
                projected, pivot, export_scale);
            const Point stored_source{std::round(authored_source.x), std::round(authored_source.y)};
            const auto screen_offset = screen_offset_from_source_pixel(
                stored_source, pivot, export_scale, zoom);
            const auto decoded_source = source_pixel_from_screen_offset(
                screen_offset, pivot, export_scale, zoom);
            check(close(decoded_source.x, stored_source.x, .0001f) &&
                      close(decoded_source.y, stored_source.y, .0001f),
                  "Source-pixel/pivot/export-scale inverse mapping drifted across zoom");

            const auto recovered_reference = reference_offset_from_source_pixel(
                decoded_source, pivot, export_scale);
            const float pixel_error = std::max(std::abs(recovered_reference.x - projected.x),
                                               std::abs(recovered_reference.y - projected.y)) * zoom;
            check(pixel_error <= 1.f,
                  "Asset calibration exceeded one native pixel at a control zoom");

            const auto encoded = encode_camera_depth_r16(camera_depth(point), depth_min, depth_max);
            const auto reconstructed = reconstruct_render_xyz(
                recovered_reference, decode_camera_depth_r16(encoded, depth_min, depth_max));
            const float world_error = std::max({std::abs(reconstructed[0] - point[0]),
                                                std::abs(reconstructed[1] - point[1]),
                                                std::abs(reconstructed[2] - point[2])});
            check(world_error <= .02f,
                  "R16 depth plus source-pixel calibration exceeded .02 tile");
        }
    }
}

template <class Operation>
void check_invalid(Operation operation, std::string_view message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, message);
}

void legacy_float_order_is_preserved() {
    const auto historical = [](float x, float y, float camera_x, float camera_y, float zoom) {
        const float tile_width = 58.f * zoom, tile_height = 29.f * zoom;
        return Point{(x-y-camera_x+camera_y)*tile_width*.5f,
                     (x+y-camera_x-camera_y)*tile_height*.5f};
    };
    const auto exact = [](Point actual, Point expected) {
        return std::bit_cast<std::uint32_t>(actual.x) == std::bit_cast<std::uint32_t>(expected.x) &&
               std::bit_cast<std::uint32_t>(actual.y) == std::bit_cast<std::uint32_t>(expected.y);
    };
    constexpr Point initial_camera{24.f, 24.f};
    for (const Point camera : std::array<Point, 5>{{initial_camera,
                                                  {20.674100875854492f, 20.674100875854492f},
                                                  {20.692890167236328f, 20.692890167236328f},
                                                  {19.25f, 27.125f}, {-3.375f, 41.625f}}})
        for (float zoom : {.62f, 1.3f, 3.f})
            for (const Point point : std::array<Point, 5>{{{0,0},{17.5f,33.5f},{24,24},
                                                          {-3.5f,4.25f},{47.5f,0.5f}}}) {
                const auto actual = project_legacy_screen_xy(point.x,point.y,camera.x,camera.y,zoom);
                check(exact(actual,historical(point.x,point.y,camera.x,camera.y,zoom)),
                      "Legacy projection reassociated its floating-point operations");
                for (const Point viewport : std::array<Point, 3>{{{1600,900},{1280,720},{1919,1079}}}) {
                    const auto screen = [viewport](Point projected) {
                        return Point{viewport.x*.48f+projected.x, viewport.y*.48f+projected.y};
                    };
                    const auto initial = screen(project_legacy_screen_xy(
                        point.x,point.y,initial_camera.x,initial_camera.y,zoom));
                    const auto old_origin = screen(project_legacy_screen_xy(
                        0,0,initial_camera.x,initial_camera.y,zoom));
                    const auto new_origin = screen(project_legacy_screen_xy(0,0,camera.x,camera.y,zoom));
                    // Match flat_iso base addition, cached_tile origin subtraction,
                    // then append_geometry's addition to the stored vertex.
                    const Point cached{initial.x+(new_origin.x-old_origin.x),
                                       initial.y+(new_origin.y-old_origin.y)};
                    const auto reference_initial = screen(historical(
                        point.x,point.y,initial_camera.x,initial_camera.y,zoom));
                    const auto reference_old = screen(historical(
                        0,0,initial_camera.x,initial_camera.y,zoom));
                    const auto reference_new = screen(historical(0,0,camera.x,camera.y,zoom));
                    check(exact(cached,{reference_initial.x+(reference_new.x-reference_old.x),
                                        reference_initial.y+(reference_new.y-reference_old.y)}),
                          "Legacy cached-origin translation changed its floating-point order");
                }
            }
}

void invalid_inputs_are_rejected() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check_invalid([&] { (void)project_render_xyz({nan, 0.f, 0.f}); },
                  "NaN render coordinate was accepted");
    check_invalid([] { (void)encode_camera_depth_r16(0.f, 1.f, 1.f); },
                  "Empty camera-depth range was accepted");
    check_invalid([] { (void)encode_camera_depth_r16(2.f, -1.f, 1.f); },
                  "Out-of-range camera depth was accepted");
    check_invalid([] { (void)reference_offset_from_source_pixel({}, {}, 0.f); },
                  "Zero asset export scale was accepted");
    check_invalid([] { (void)source_pixel_from_screen_offset({}, {}, 1.f, 0.f); },
                  "Zero projection zoom was accepted");
}

} // namespace

int main() {
    try {
        reference_basis_roundtrips();
        camera_relative_large_origins();
        depth_r16_and_asset_pixel_calibration();
        legacy_float_order_is_preserved();
        invalid_inputs_are_rejected();
        std::cout << "World projection CPU contracts passed (no native shader validation).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

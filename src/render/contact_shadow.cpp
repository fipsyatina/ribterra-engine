#include "ribterra/contact_shadow.hpp"
#include "ribterra/world_projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ribterra::render {
namespace {
constexpr float axis_tolerance = .001f;
bool finite_unit(float value) noexcept {
    return std::isfinite(value) && value >= 0.f && value <= 1.f;
}
float dot_axes(const std::array<float, 4>& a, const std::array<float, 4>& b) noexcept {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
std::array<float, 3> plane_normal(const ContactSurfaceGpu& surface) noexcept {
    const auto& u = surface.axis_u_extent;
    const auto& v = surface.axis_v_extent;
    return {u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]};
}

std::size_t validate_contact_region(const ContactMaskRegion& region) {
    if (region.texture_width < contact_mask_min_dimension ||
        region.texture_height < contact_mask_min_dimension ||
        region.texture_width > contact_mask_max_dimension ||
        region.texture_height > contact_mask_max_dimension)
        throw std::invalid_argument("Contact mask texture dimensions are invalid");
    const auto byte_count = static_cast<std::uint64_t>(region.texture_width) * region.texture_height;
    if (byte_count > contact_mask_max_bytes ||
        byte_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::invalid_argument("Contact mask byte count exceeds the per-art limit");
    if (region.width < contact_mask_min_dimension || region.height < contact_mask_min_dimension ||
        static_cast<std::uint64_t>(region.x) + region.width > region.texture_width ||
        static_cast<std::uint64_t>(region.y) + region.height > region.texture_height)
        throw std::invalid_argument("Contact mask region is invalid");
    return static_cast<std::size_t>(byte_count);
}

double finite_floor(double value, const char* diagnostic) {
    if (!std::isfinite(value)) throw std::invalid_argument(diagnostic);
    return std::floor(value);
}

double finite_ceil(double value, const char* diagnostic) {
    if (!std::isfinite(value)) throw std::invalid_argument(diagnostic);
    return std::ceil(value);
}
} // namespace

void validate_contact_projection(const ContactProjection& projection) {
    if (!std::isfinite(projection.origin_xy[0]) || !std::isfinite(projection.origin_xy[1]) ||
        !std::isfinite(projection.zoom) || !(projection.zoom > 0.f))
        throw std::invalid_argument("Contact projection origin/zoom is invalid");
}

std::array<float, 2> contact_project_logical(
    const std::array<float, 3>& render_xyz, const ContactProjection& projection) {
    validate_contact_projection(projection);
    const auto projected = project_render_xyz(render_xyz);
    const std::array<float, 2> result{
        projection.origin_xy[0] + projection.zoom * projected.x,
        projection.origin_xy[1] + projection.zoom * projected.y};
    if (!std::isfinite(result[0]) || !std::isfinite(result[1]))
        throw std::invalid_argument("Contact logical projection result is invalid");
    return result;
}

void validate_contact_art(const ContactArtDesc& desc, std::span<const std::uint8_t> r8_bytes) {
    const std::size_t byte_count = validate_contact_region(desc.region);
    if (static_cast<std::uint32_t>(desc.facing) > static_cast<std::uint32_t>(ContactFacing::NorthWest))
        throw std::invalid_argument("Contact art facing is invalid");
    if (r8_bytes.size() != byte_count)
        throw std::invalid_argument("Contact mask byte count does not match its dimensions");

    const auto& region = desc.region;
    const auto texel = [&](std::uint32_t x, std::uint32_t y) -> std::uint8_t {
        return r8_bytes[static_cast<std::size_t>(y) * region.texture_width + x];
    };
    bool interior_nonzero = false;
    for (std::uint32_t y = region.y; y < region.y + region.height; ++y) {
        for (std::uint32_t x = region.x; x < region.x + region.width; ++x) {
            const bool outer_ring = x == region.x || y == region.y ||
                x + 1 == region.x + region.width || y + 1 == region.y + region.height;
            if (outer_ring && texel(x, y) != 0)
                throw std::invalid_argument("Contact mask region outer ring must be transparent");
            if (!outer_ring && texel(x, y) != 0) interior_nonzero = true;
        }
    }
    if (!interior_nonzero)
        throw std::invalid_argument("Contact mask region has no nonzero interior texel");
}

std::array<float, 4> contact_mask_uv(const ContactMaskRegion& region) {
    (void)validate_contact_region(region);
    const float texture_width = static_cast<float>(region.texture_width);
    const float texture_height = static_cast<float>(region.texture_height);
    return {(static_cast<float>(region.x) + .5f) / texture_width,
            (static_cast<float>(region.y) + .5f) / texture_height,
            static_cast<float>(region.width - 1U) / texture_width,
            static_cast<float>(region.height - 1U) / texture_height};
}

float contact_receiver_tag(ContactReceiver receiver) {
    switch (receiver) {
    case ContactReceiver::Ordinary: return 1.f;
    case ContactReceiver::Flat:
    case ContactReceiver::Slope:
    case ContactReceiver::Bridge: return 2.f;
    }
    throw std::invalid_argument("Unknown contact receiver classification");
}

ContactCorners contact_shadow_corners(const ContactSurfaceGpu& surface) noexcept {
    ContactCorners result{};
    constexpr std::array<std::array<float, 2>, 4> mask_corners{{{0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f}}};
    for (std::size_t corner = 0; corner < result.size(); ++corner) {
        const float u = (mask_corners[corner][0]-surface.pivot_plane[0])*2.f*surface.axis_u_extent[3];
        const float v = (mask_corners[corner][1]-surface.pivot_plane[1])*2.f*surface.axis_v_extent[3];
        for (std::size_t axis = 0; axis < 3; ++axis)
            result[corner][axis] = surface.anchor_opacity[axis] + u*surface.axis_u_extent[axis] + v*surface.axis_v_extent[axis];
    }
    return result;
}

ContactScissor contact_shadow_scissor(
    const ContactSurfaceGpu& surface, const ContactProjection& projection,
    const ContactPhysicalLayout& layout, const ContactLogicalRect& world_rect) {
    validate_contact_projection(projection);
    if (!std::isfinite(layout.x) || !std::isfinite(layout.y) || !std::isfinite(layout.scale) ||
        !(layout.scale > 0.0) || layout.pixel_width == 0 || layout.pixel_height == 0 ||
        layout.pixel_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        layout.pixel_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Contact physical layout is invalid");
    if (!std::isfinite(world_rect.x) || !std::isfinite(world_rect.y) ||
        !std::isfinite(world_rect.w) || !std::isfinite(world_rect.h) ||
        !(world_rect.w > 0.0) || !(world_rect.h > 0.0))
        throw std::invalid_argument("Contact logical world rectangle is invalid");

    const auto& u = surface.axis_u_extent;
    const auto& v = surface.axis_v_extent;
    const std::array<double, 3> normal_raw{
        static_cast<double>(u[1])*v[2] - static_cast<double>(u[2])*v[1],
        static_cast<double>(u[2])*v[0] - static_cast<double>(u[0])*v[2],
        static_cast<double>(u[0])*v[1] - static_cast<double>(u[1])*v[0]};
    const double normal_length = std::sqrt(normal_raw[0]*normal_raw[0] +
        normal_raw[1]*normal_raw[1] + normal_raw[2]*normal_raw[2]);
    if (!std::isfinite(normal_length) || !(normal_length > 0.0))
        throw std::invalid_argument("Contact scissor received invalid surface geometry");
    const std::array<double, 3> normal{
        normal_raw[0]/normal_length, normal_raw[1]/normal_length, normal_raw[2]/normal_length};
    const double tolerance = surface.pivot_plane[2];
    if (!std::isfinite(tolerance) || tolerance < contact_shadow_min_plane_tolerance ||
        tolerance > contact_shadow_max_plane_tolerance)
        throw std::invalid_argument("Contact scissor received invalid plane tolerance");

    double minimum_x = std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();
    for (const auto& corner : contact_shadow_corners(surface)) {
        for (const double sign : {-1.0, 1.0}) {
            std::array<float, 3> extruded{};
            for (std::size_t axis = 0; axis < extruded.size(); ++axis)
                extruded[axis] = static_cast<float>(
                    static_cast<double>(corner[axis]) + sign*normal[axis]*tolerance);
            const auto logical = contact_project_logical(extruded, projection);
            const double physical_x = layout.x + static_cast<double>(logical[0])*layout.scale;
            const double physical_y = layout.y + static_cast<double>(logical[1])*layout.scale;
            if (!std::isfinite(physical_x) || !std::isfinite(physical_y))
                throw std::invalid_argument("Contact scissor projection overflowed");
            minimum_x = std::min(minimum_x, physical_x);
            minimum_y = std::min(minimum_y, physical_y);
            maximum_x = std::max(maximum_x, physical_x);
            maximum_y = std::max(maximum_y, physical_y);
        }
    }

    const char* diagnostic = "Contact scissor endpoint is invalid";
    double left = finite_floor(minimum_x - 1.0, diagnostic);
    double top = finite_floor(minimum_y - 1.0, diagnostic);
    double right = finite_ceil(maximum_x + 1.0, diagnostic);
    double bottom = finite_ceil(maximum_y + 1.0, diagnostic);
    const double world_right = world_rect.x + world_rect.w;
    const double world_bottom = world_rect.y + world_rect.h;
    if (!std::isfinite(world_right) || !std::isfinite(world_bottom))
        throw std::invalid_argument("Contact logical world rectangle overflowed");
    left = std::max(left, finite_floor(layout.x + world_rect.x*layout.scale, diagnostic));
    top = std::max(top, finite_floor(layout.y + world_rect.y*layout.scale, diagnostic));
    right = std::min(right, finite_ceil(layout.x + world_right*layout.scale, diagnostic));
    bottom = std::min(bottom, finite_ceil(layout.y + world_bottom*layout.scale, diagnostic));
    left = std::clamp(left, 0.0, static_cast<double>(layout.pixel_width));
    top = std::clamp(top, 0.0, static_cast<double>(layout.pixel_height));
    right = std::clamp(right, 0.0, static_cast<double>(layout.pixel_width));
    bottom = std::clamp(bottom, 0.0, static_cast<double>(layout.pixel_height));
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    return {static_cast<int>(left), static_cast<int>(top),
            static_cast<int>(right-left), static_cast<int>(bottom-top)};
}

void validate_contact_shadows(std::span<const ContactShadowDraw> draws) {
    if (draws.size() > contact_shadow_capacity)
        throw std::invalid_argument("Contact caster overflow: maximum 128; no casters discarded");
    for (std::size_t index = 0; index < draws.size(); ++index) {
        const auto& draw = draws[index];
        const auto& surface = draw.surface;
        const auto finite = [](const auto& values) {
            return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
        };
        if (!finite(surface.anchor_opacity) || !finite(surface.axis_u_extent) ||
            !finite(surface.axis_v_extent) || !finite(surface.uv_region) || !finite(surface.pivot_plane))
            throw std::invalid_argument("Contact surface contains a non-finite value");
        if (!draw.art.generation || !draw.stable_id || !draw.owner_id || draw.reveal != 1.f ||
            draw.padding_zero[0] || draw.padding_zero[1] ||
            static_cast<std::uint32_t>(draw.facing) > static_cast<std::uint32_t>(ContactFacing::NorthWest))
            throw std::invalid_argument("Prepared contact identity/reveal/facing is invalid");
        for (std::size_t previous = 0; previous < index; ++previous)
            if (draws[previous].stable_id == draw.stable_id)
                throw std::invalid_argument("Duplicate contact stable id");
        if (!finite_unit(surface.anchor_opacity[3]) || surface.anchor_opacity[3] == 0.f ||
            !finite_unit(surface.pivot_plane[0]) ||
            !finite_unit(surface.pivot_plane[1]) ||
            surface.pivot_plane[2] < contact_shadow_min_plane_tolerance ||
            surface.pivot_plane[2] > contact_shadow_max_plane_tolerance || surface.pivot_plane[3] != 0.f)
            throw std::invalid_argument("Contact opacity/pivot/plane tolerance is invalid");
        const auto& region = surface.uv_region;
        if (!finite_unit(region[0]) || !finite_unit(region[1]) || region[2] <= 0.f || region[3] <= 0.f ||
            region[0]+region[2] > 1.f || region[1]+region[3] > 1.f)
            throw std::invalid_argument("Contact mask UV region is invalid");
        const auto& u = surface.axis_u_extent;
        const auto& v = surface.axis_v_extent;
        if (u[3] < contact_shadow_min_extent || v[3] < contact_shadow_min_extent ||
            std::abs(dot_axes(u,u)-1.f) > axis_tolerance || std::abs(dot_axes(v,v)-1.f) > axis_tolerance ||
            std::abs(dot_axes(u,v)) > axis_tolerance)
            throw std::invalid_argument("Contact requires positive extents and orthonormal tangent axes");
        auto normal = plane_normal(surface);
        if (normal[2] <= 0.f)
            throw std::invalid_argument("Contact receiver plane must face upward; cliffs are unsupported");
        const float length = std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        for (auto& value : normal) value /= length;
        if (normal[2] < contact_shadow_min_upward_normal)
            throw std::invalid_argument("Contact plane exceeds the supported 60-degree slope");
        for (const auto& corner : contact_shadow_corners(surface))
            for (std::size_t axis = 0; axis < corner.size(); ++axis)
                if (!std::isfinite(corner[axis]) ||
                    std::abs(corner[axis])+std::abs(normal[axis])*surface.pivot_plane[2] > contact_shadow_coordinate_bound)
                    throw std::invalid_argument("Contact footprint/plane band exceeds the camera-relative bound");
    }
}

} // namespace ribterra::render

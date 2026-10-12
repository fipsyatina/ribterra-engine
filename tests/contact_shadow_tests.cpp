#include "ribterra/contact_shadow.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace ribterra::render;
std::size_t checks{};
void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
    ++checks;
}
ContactShadowDraw valid_draw() {
    ContactShadowDraw draw{};
    draw.art = {0u, 1u}; // Slot zero is not assumed to be a registry sentinel.
    draw.stable_id = 1u;
    draw.owner_id = 7u;
    return draw;
}
void reject(const ContactShadowDraw& draw, std::string_view message) {
    const auto before = draw;
    bool rejected{};
    try { validate_contact_shadows(std::span{&draw, 1}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
    check(std::memcmp(&before, &draw, sizeof(before)) == 0, "Validation mutated its input");
}
template <class Call>
void reject_call(Call&& call, std::string_view message) {
    bool rejected{};
    try { call(); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}
bool near(float actual, float expected, float tolerance = 1.e-6f) {
    return std::abs(actual-expected) <= tolerance;
}
void geometry_tests() {
    auto draw = valid_draw();
    validate_contact_shadows(std::span{&draw, 1});
    const auto flat = contact_shadow_corners(draw.surface);
    check(flat[0] == std::array{-1.f,-1.f,0.f} && flat[2] == std::array{1.f,1.f,0.f}, "Flat footprint differs");
    draw.surface.pivot_plane[0] = 0.f;
    const auto offset = contact_shadow_corners(draw.surface);
    check(offset[0][0] == 0.f && offset[2][0] == 2.f, "Authored pivot was incorrectly centred");
    draw.surface.anchor_opacity[0] = 31.f;
    reject(draw, "Off-centre footprint escaped the bound");
    draw = valid_draw();
    draw.surface.axis_u_extent = {.8f,0.f,.6f,1.f};
    validate_contact_shadows(std::span{&draw, 1});
    const auto slope = contact_shadow_corners(draw.surface);
    check(std::abs(slope[0][2]+.6f) < 1.e-6f && std::abs(slope[2][2]-.6f) < 1.e-6f, "Slope lost its world Z");
    draw.surface.anchor_opacity[2] = 2.f;
    validate_contact_shadows(std::span{&draw, 1});
    check(std::abs(contact_shadow_corners(draw.surface)[2][2]-2.6f) < 1.e-6f, "Elevated deck lost its anchor");
    draw = valid_draw();
    draw.surface.axis_v_extent = {0.f,-1.f,0.f,1.f};
    reject(draw, "Downward plane accepted");
    draw.surface.axis_u_extent = {0.f,0.f,1.f,1.f};
    reject(draw, "Vertical cliff plane accepted");
    for (const float upward : {contact_shadow_min_upward_normal,
        std::nextafter(contact_shadow_min_upward_normal, 1.f)}) {
        draw = valid_draw();
        draw.surface.axis_u_extent = {upward,0.f,std::sqrt(1.f-upward*upward),1.f};
        validate_contact_shadows(std::span{&draw, 1});
        check(draw.surface.axis_u_extent[0] == upward, "Supported slope boundary changed");
    }
    draw = valid_draw();
    const float too_steep = std::nextafter(contact_shadow_min_upward_normal, 0.f);
    draw.surface.axis_u_extent = {too_steep,0.f,std::sqrt(1.f-too_steep*too_steep),1.f};
    reject(draw, "Adjacent unsupported slope accepted");
    draw.surface.axis_u_extent = {.00001f,0.f,1.f,1.f};
    reject(draw, "Nearly vertical cliff accepted");
    draw = valid_draw();
    draw.surface.axis_v_extent[0] = .01f;
    reject(draw, "Non-orthogonal axes accepted");
    draw = valid_draw();
    draw.surface.axis_u_extent[0] = 1.01f;
    reject(draw, "Non-unit axis accepted");
    draw = valid_draw();
    draw.surface.axis_u_extent[3] = contact_shadow_min_extent;
    draw.surface.axis_v_extent[3] = contact_shadow_min_extent;
    validate_contact_shadows(std::span{&draw, 1});
    draw.surface.axis_u_extent[3] = contact_shadow_min_extent*.5f;
    reject(draw, "Sub-contract extent accepted");
    draw = valid_draw();
    draw.surface.anchor_opacity[2] = 32.f;
    reject(draw, "Plane band escaped the bound");
}
void metadata_tests() {
    const auto original = valid_draw();
    for (const auto value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (std::size_t lane = 0; lane < 5; ++lane)
            for (std::size_t component = 0; component < 4; ++component) {
                auto draw = original;
                const std::array lanes{&draw.surface.anchor_opacity, &draw.surface.axis_u_extent,
                    &draw.surface.axis_v_extent, &draw.surface.uv_region, &draw.surface.pivot_plane};
                (*lanes[lane])[component] = value;
                reject(draw, "Non-finite surface lane accepted");
            }
    }
    for (float reveal : {0.f,.5f,2.f,std::numeric_limits<float>::quiet_NaN()}) {
        auto draw = original; draw.reveal = reveal;
        reject(draw, "Hidden/invalid caster entered prepared list");
    }
    auto draw = original; draw.art.generation = 0;
    reject(draw, "Uninitialized art generation accepted");
    draw = original; draw.stable_id = 0;
    reject(draw, "Zero stable id accepted");
    draw = original; draw.owner_id = 0;
    reject(draw, "Zero owner id accepted");
    draw = original; draw.facing = static_cast<ContactFacing>(99u);
    reject(draw, "Unknown facing accepted");
    draw = original; draw.padding_zero[1] = 1u;
    reject(draw, "Nonzero padding accepted");
    for (const auto facing : {ContactFacing::Omnidirectional, ContactFacing::North, ContactFacing::NorthEast,
        ContactFacing::East, ContactFacing::SouthEast, ContactFacing::South, ContactFacing::SouthWest,
        ContactFacing::West, ContactFacing::NorthWest}) {
        draw = original; draw.facing = facing; validate_contact_shadows(std::span{&draw, 1});
        check(draw.facing == facing, "Facing unexpectedly changed");
    }
    for (const float opacity : {-1.f, 0.f, 1.01f}) {
        draw = original; draw.surface.anchor_opacity[3] = opacity; reject(draw, "Invalid opacity accepted");
    }
    draw = original;
    draw.surface.uv_region = {.75f,0.f,.5f,1.f}; reject(draw, "UV region escaped texture");
    draw = original; draw.surface.uv_region[2] = 0.f; reject(draw, "Empty UV region accepted");
    draw = original; draw.surface.pivot_plane[0] = -1.f; reject(draw, "Invalid pivot accepted");
    draw = original; draw.surface.pivot_plane[2] = contact_shadow_min_plane_tolerance;
    validate_contact_shadows(std::span{&draw, 1});
    check(draw.surface.pivot_plane[2] == contact_shadow_min_plane_tolerance,
        "Minimum plane tolerance changed");
    draw.surface.pivot_plane[2] = std::nextafter(contact_shadow_min_plane_tolerance, 0.f);
    reject(draw, "Sub-contract plane tolerance accepted");
    draw = original; draw.surface.pivot_plane[2] = contact_shadow_max_plane_tolerance;
    validate_contact_shadows(std::span{&draw, 1});
    check(draw.surface.pivot_plane[2] == contact_shadow_max_plane_tolerance,
        "Maximum plane tolerance changed");
    draw.surface.pivot_plane[2] = std::nextafter(contact_shadow_max_plane_tolerance, 1.f);
    reject(draw, "Excessive plane band accepted");
    draw = original; draw.surface.pivot_plane[3] = 1.f; reject(draw, "Reserved data accepted");
}
void projection_and_scissor_tests() {
    const ContactProjection projection{{3.f,-4.f},1.25f};
    validate_contact_projection(projection);
    const auto projected = contact_project_logical({2.f,-1.f,.5f}, projection);
    check(near(projected[0], 111.75f, 1.f/1024.f) && near(projected[1], -4.f, 1.f/1024.f),
        "Shared contact projection differs from the mathematical oracle");
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()}) {
        auto candidate = projection; candidate.origin_xy[0] = invalid;
        reject_call([&] { validate_contact_projection(candidate); }, "Non-finite projection origin accepted");
        candidate = projection; candidate.zoom = invalid;
        reject_call([&] { validate_contact_projection(candidate); }, "Non-finite projection zoom accepted");
    }
    for (const float invalid_zoom : {0.f,-1.f}) {
        auto candidate = projection; candidate.zoom = invalid_zoom;
        reject_call([&] { validate_contact_projection(candidate); }, "Non-positive projection zoom accepted");
    }
    reject_call([&] { (void)contact_project_logical(
        {std::numeric_limits<float>::infinity(),0.f,0.f}, projection); },
        "Non-finite render position accepted");
    ContactProjection overflowing{{std::numeric_limits<float>::max(),0.f},2.f};
    reject_call([&] { (void)contact_project_logical(
        {std::numeric_limits<float>::max()/58.f,0.f,0.f}, overflowing); },
        "Overflowing logical projection accepted");

    const auto surface = valid_draw().surface;
    const ContactPhysicalLayout layout{10.0,20.0,2.0,400u,300u};
    const ContactLogicalRect world{0.0,0.0,200.0,100.0};
    const auto scissor = contact_shadow_scissor(surface, {{50.f,60.f},1.f}, layout, world);
    check(scissor.x == 10 && scissor.y == 78 && scissor.w == 217 && scissor.h == 124,
        "Flat scissor differs from the eight-corner floor/ceil oracle");

    auto sloped = surface;
    sloped.anchor_opacity = {1.25f,-.75f,2.5f,1.f};
    sloped.axis_u_extent = {.8f,0.f,.6f,1.75f};
    sloped.axis_v_extent = {0.f,1.f,0.f,.625f};
    sloped.pivot_plane = {.2f,.7f,.03f,0.f};
    const auto odd = contact_shadow_scissor(
        sloped, {{127.25f,81.75f},.85f}, {3.25,5.5,1.5,503u,307u},
        {2.5,3.25,281.75,166.5});
    check(odd.x >= 0 && odd.y >= 0 && odd.w > 0 && odd.h > 0 &&
        odd.x+odd.w <= 503 && odd.y+odd.h <= 307,
        "Odd off-centre slope scissor was empty or escaped the pixel extent");

    auto high_zoom_surface = surface;
    high_zoom_surface.anchor_opacity = {3.25f,-2.5f,1.75f,1.f};
    high_zoom_surface.axis_u_extent = {.8f,0.f,.6f,1.125f};
    high_zoom_surface.axis_v_extent = {0.f,1.f,0.f,.875f};
    high_zoom_surface.pivot_plane = {.17f,.83f,.02f,0.f};
    const ContactProjection high_zoom{{-464064.f,6306624.f},32768.f};
    const ContactPhysicalLayout high_layout{100.0,200.0,1.0,12000000u,12000000u};
    const ContactLogicalRect high_world{0.0,0.0,11999800.0,11999600.0};
    const auto high_scissor = contact_shadow_scissor(
        high_zoom_surface, high_zoom, high_layout, high_world);
    check(high_scissor.x > 100 && high_scissor.y > 200 &&
        high_scissor.x+high_scissor.w < static_cast<int>(high_layout.pixel_width) &&
        high_scissor.y+high_scissor.h < static_cast<int>(high_layout.pixel_height),
        "High-zoom cancellation control was clipped instead of producing a bounded scissor");
    const auto& high_u = high_zoom_surface.axis_u_extent;
    const auto& high_v = high_zoom_surface.axis_v_extent;
    std::array<double, 3> high_normal{
        static_cast<double>(high_u[1])*high_v[2] - static_cast<double>(high_u[2])*high_v[1],
        static_cast<double>(high_u[2])*high_v[0] - static_cast<double>(high_u[0])*high_v[2],
        static_cast<double>(high_u[0])*high_v[1] - static_cast<double>(high_u[1])*high_v[0]};
    const double high_normal_length = std::sqrt(high_normal[0]*high_normal[0] +
        high_normal[1]*high_normal[1] + high_normal[2]*high_normal[2]);
    for (auto& component : high_normal) component /= high_normal_length;
    for (const auto& corner : contact_shadow_corners(high_zoom_surface)) {
        for (const double sign : {-1.0,1.0}) {
            std::array<float, 3> extruded{};
            for (std::size_t axis = 0; axis < extruded.size(); ++axis)
                extruded[axis] = static_cast<float>(static_cast<double>(corner[axis]) +
                    sign*high_normal[axis]*high_zoom_surface.pivot_plane[2]);
            const auto logical = contact_project_logical(extruded, high_zoom);
            const double physical_x = high_layout.x + static_cast<double>(logical[0])*high_layout.scale;
            const double physical_y = high_layout.y + static_cast<double>(logical[1])*high_layout.scale;
            check(physical_x >= high_scissor.x &&
                physical_x <= static_cast<double>(high_scissor.x+high_scissor.w) &&
                physical_y >= high_scissor.y &&
                physical_y <= static_cast<double>(high_scissor.y+high_scissor.h),
                "Canonical high-zoom projected corner escaped the contact scissor");
        }
    }
    const auto clipped = contact_shadow_scissor(
        surface, {{-10000.f,-10000.f},1.f}, {0.0,0.0,1.0,17u,19u},
        {0.0,0.0,17.0,19.0});
    check(clipped.x >= 0 && clipped.y >= 0 && clipped.w >= 0 && clipped.h >= 0 &&
        clipped.x+clipped.w <= 17 && clipped.y+clipped.h <= 19,
        "Offscreen scissor produced negative or overflowing dimensions");

    auto invalid_layout = layout; invalid_layout.scale = 0.0;
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, invalid_layout, world); },
        "Zero physical scale accepted");
    invalid_layout = layout; invalid_layout.x = std::numeric_limits<double>::infinity();
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, invalid_layout, world); },
        "Non-finite physical layout accepted");
    invalid_layout = layout; invalid_layout.pixel_width = 0;
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, invalid_layout, world); },
        "Empty physical pixel extent accepted");
    invalid_layout = layout;
    invalid_layout.pixel_width = std::numeric_limits<std::uint32_t>::max();
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, invalid_layout, world); },
        "Unrepresentable physical pixel extent accepted");
    auto invalid_world = world; invalid_world.w = 0.0;
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, layout, invalid_world); },
        "Empty logical world rectangle accepted");
    invalid_world = world; invalid_world.y = std::numeric_limits<double>::quiet_NaN();
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, layout, invalid_world); },
        "Non-finite logical world rectangle accepted");
    invalid_world = {std::numeric_limits<double>::max(),0.0,
        std::numeric_limits<double>::max(),1.0};
    reject_call([&] { (void)contact_shadow_scissor(surface, projection, layout, invalid_world); },
        "Overflowing logical world rectangle accepted");
    auto invalid_projection = projection; invalid_projection.zoom = -1.f;
    reject_call([&] { (void)contact_shadow_scissor(surface, invalid_projection, layout, world); },
        "Invalid projection entered scissor calculation");
}

void contact_art_tests() {
    ContactArtDesc art{{7u,6u,1u,1u,5u,4u},ContactFacing::SouthWest};
    std::vector<std::uint8_t> pixels(42u, 0u);
    pixels[0] = 255u; // Bytes outside the authored rect are not its transparent gutter.
    pixels[2u*7u+3u] = 192u;
    validate_contact_art(art, pixels);
    check(art.facing == ContactFacing::SouthWest, "Known contact-art facing changed");
    for (const auto facing : {ContactFacing::Omnidirectional, ContactFacing::North,
        ContactFacing::NorthEast, ContactFacing::East, ContactFacing::SouthEast,
        ContactFacing::South, ContactFacing::SouthWest, ContactFacing::West,
        ContactFacing::NorthWest}) {
        art.facing = facing;
        validate_contact_art(art, pixels);
        check(art.facing == facing, "Known contact-art facing was rejected or changed");
    }
    const auto uv = contact_mask_uv(art.region);
    check(near(uv[0],1.5f/7.f) && near(uv[1],1.5f/6.f) &&
        near(uv[2],4.f/7.f) && near(uv[3],3.f/6.f),
        "Canonical UV region does not address first/last texel centres");
    check(near(uv[0]+uv[2],5.5f/7.f) && near(uv[1]+uv[3],4.5f/6.f),
        "Canonical UV far corner is not the last declared texel centre");

    auto damaged = pixels; damaged[1u*7u+2u] = 1u;
    reject_call([&] { validate_contact_art(art, damaged); },
        "Nontransparent authored outer ring accepted");
    auto empty = pixels; empty[2u*7u+3u] = 0u;
    reject_call([&] { validate_contact_art(art, empty); },
        "Outside-neighbour data was mistaken for a nonzero authored interior");
    reject_call([&] { validate_contact_art(art, std::span<const std::uint8_t>{pixels.data(),pixels.size()-1}); },
        "Inexact R8 byte count accepted");
    auto invalid_art = art; invalid_art.facing = static_cast<ContactFacing>(99u);
    reject_call([&] { validate_contact_art(invalid_art, pixels); }, "Unknown contact-art facing accepted");

    ContactMaskRegion invalid_region = art.region; invalid_region.texture_width = 2u;
    reject_call([&] { (void)contact_mask_uv(invalid_region); }, "Undersized texture accepted");
    invalid_region = art.region; invalid_region.texture_width = 1025u;
    reject_call([&] { (void)contact_mask_uv(invalid_region); }, "Oversized texture accepted");
    invalid_region = art.region; invalid_region.width = 2u;
    reject_call([&] { (void)contact_mask_uv(invalid_region); }, "Undersized authored rect accepted");
    invalid_region = art.region; invalid_region.x = 6u;
    reject_call([&] { (void)contact_mask_uv(invalid_region); }, "Out-of-bounds authored rect accepted");
    invalid_region = art.region; invalid_region.x = std::numeric_limits<std::uint32_t>::max();
    reject_call([&] { (void)contact_mask_uv(invalid_region); }, "Overflowing authored rect accepted");

    ContactArtDesc minimum{{3u,3u,0u,0u,3u,3u},ContactFacing::Omnidirectional};
    std::array<std::uint8_t, 9> minimum_pixels{};
    minimum_pixels[4] = 255u;
    validate_contact_art(minimum, minimum_pixels);
    const auto minimum_uv = contact_mask_uv(minimum.region);
    check(near(minimum_uv[0],.5f/3.f) && near(minimum_uv[1],.5f/3.f) &&
        near(minimum_uv[0]+minimum_uv[2],2.5f/3.f) &&
        near(minimum_uv[1]+minimum_uv[3],2.5f/3.f),
        "Minimum mask UVs do not land on the corner texel centres");

    ContactArtDesc maximum{{1024u,1024u,0u,0u,1024u,1024u},ContactFacing::Omnidirectional};
    std::vector<std::uint8_t> maximum_pixels(contact_mask_max_bytes, 0u);
    maximum_pixels[512u*1024u+512u] = 1u;
    validate_contact_art(maximum, maximum_pixels);
    check(maximum_pixels.size() == contact_mask_max_bytes, "One-MiB mask boundary changed");
}
void list_and_receiver_tests() {
    validate_contact_shadows({});
    std::vector<ContactShadowDraw> draws(contact_shadow_capacity, valid_draw());
    for (std::size_t index = 0; index < draws.size(); ++index)
        draws[index].stable_id = static_cast<std::uint32_t>(index+1);
    validate_contact_shadows(draws); // Repeated owner permitted, stable IDs unique.
    check(draws.size() == contact_shadow_capacity, "Valid capacity changed");
    draws.push_back(valid_draw());
    draws.back().stable_id = 129u;
    bool rejected{};
    try { validate_contact_shadows(draws); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && draws.size() == 129, "Overflow was accepted or silently truncated");
    draws.resize(2); draws[1].stable_id = draws[0].stable_id;
    rejected = false;
    try { validate_contact_shadows(draws); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Duplicate stable id accepted");
    check(contact_receiver_tag(ContactReceiver::Ordinary) == 1.f, "Ordinary receiver encoding changed");
    for (const auto receiver : {ContactReceiver::Flat, ContactReceiver::Slope, ContactReceiver::Bridge})
        check(contact_receiver_tag(receiver) == 2.f, "Eligible receiver encoding differs");
    rejected = false;
    try { (void)contact_receiver_tag(static_cast<ContactReceiver>(255u)); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Unknown receiver classification accepted");
}
} // namespace

int main() {
    try {
        geometry_tests(); metadata_tests(); projection_and_scissor_tests();
        contact_art_tests(); list_and_receiver_tests();
        std::cout << "Contact CPU contract V1: " << checks << " checks passed; no GPU/art registry acceptance\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Contact CPU contract failed: " << error.what() << '\n';
        return 1;
    }
}

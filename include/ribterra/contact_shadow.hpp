#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ribterra::render {

// Contact-only preparation/registry contract, not the completed Dynamic engine.
inline constexpr std::uint32_t contact_shadow_contract_version = 1;
inline constexpr std::size_t contact_shadow_capacity = 128; // Provisional, not a measured FPS budget.
inline constexpr std::size_t contact_art_capacity = 256;
inline constexpr std::uint32_t contact_mask_min_dimension = 3;
inline constexpr std::uint32_t contact_mask_max_dimension = 1024;
inline constexpr std::size_t contact_mask_max_bytes = 1024U * 1024U;
inline constexpr float contact_shadow_coordinate_bound = 32.f;
inline constexpr float contact_shadow_min_extent = 1.e-4f;
inline constexpr float contact_shadow_min_plane_tolerance = .02f;
inline constexpr float contact_shadow_max_plane_tolerance = .04f;
// Conservative renderer policy: at most 60 degrees from horizontal. This does
// not redefine terrain traversal/buildability; steeper faces must clip contact.
inline constexpr float contact_shadow_min_upward_normal = .5f;

// These are not local-light receiver bits. The explicit contact graph transports
// this classification separately; the old GroundForward contract stays intact.
enum class ContactReceiver : std::uint32_t { Ordinary, Flat, Slope, Bridge };
enum class ContactFacing : std::uint32_t {
    Omnidirectional, North, NorthEast, East, SouthEast, South, SouthWest, West, NorthWest
};
struct ContactArtHandle {
    std::uint32_t slot{}, generation{};
    friend bool operator==(const ContactArtHandle&, const ContactArtHandle&) = default;
};

struct ContactProjection {
    std::array<float, 2> origin_xy{};
    float zoom{1.f};
};
struct ContactPhysicalLayout {
    double x{}, y{}, scale{1.};
    std::uint32_t pixel_width{}, pixel_height{};
};
struct ContactLogicalRect { double x{}, y{}, w{}, h{}; };
struct ContactScissor { int x{}, y{}, w{}, h{}; };
struct ContactMaskRegion {
    std::uint32_t texture_width{}, texture_height{}, x{}, y{}, width{}, height{};
};
struct ContactArtDesc {
    ContactMaskRegion region{};
    ContactFacing facing{ContactFacing::Omnidirectional};
};

void validate_contact_projection(const ContactProjection& projection);
[[nodiscard]] std::array<float, 2> contact_project_logical(
    const std::array<float, 3>& render_xyz, const ContactProjection& projection);
void validate_contact_art(const ContactArtDesc& desc, std::span<const std::uint8_t> r8_bytes);
[[nodiscard]] std::array<float, 4> contact_mask_uv(const ContactMaskRegion& region);

// Contact V1 SurfaceWorld.W values: 0 clear/invalid, 1 valid ordinary,
// 2 valid contact receiver. Never coverage, a mask bitfield, or a blend weight.
[[nodiscard]] float contact_receiver_tag(ContactReceiver receiver);

struct alignas(16) ContactSurfaceGpu {
    std::array<float, 4> anchor_opacity{0.f, 0.f, 0.f, 1.f};
    std::array<float, 4> axis_u_extent{1.f, 0.f, 0.f, 1.f};
    std::array<float, 4> axis_v_extent{0.f, 1.f, 0.f, 1.f};
    std::array<float, 4> uv_region{0.f, 0.f, 1.f, 1.f};
    // Pivot in normalized mask coordinates, plane tolerance, reserved zero.
    // The positive normal is cross(axis_u, axis_v). The anchor is the pivot,
    // not necessarily the geometric centre of this authored footprint.
    std::array<float, 4> pivot_plane{.5f, .5f, .04f, 0.f};
};
static_assert(sizeof(ContactSurfaceGpu) == 80 && alignof(ContactSurfaceGpu) == 16);
static_assert(std::is_standard_layout_v<ContactSurfaceGpu> && std::is_trivially_copyable_v<ContactSurfaceGpu>);
static_assert(offsetof(ContactSurfaceGpu, anchor_opacity) == 0);
static_assert(offsetof(ContactSurfaceGpu, axis_u_extent) == 16);
static_assert(offsetof(ContactSurfaceGpu, axis_v_extent) == 32);
static_assert(offsetof(ContactSurfaceGpu, uv_region) == 48);
static_assert(offsetof(ContactSurfaceGpu, pivot_plane) == 64);

struct ContactShadowDraw {
    ContactSurfaceGpu surface{};
    ContactArtHandle art{};
    std::uint32_t stable_id{}, owner_id{};
    float reveal{1.f};
    ContactFacing facing{ContactFacing::Omnidirectional};
    std::array<std::uint32_t, 2> padding_zero{};
};
static_assert(sizeof(ContactShadowDraw) == 112);
static_assert(std::is_standard_layout_v<ContactShadowDraw> && std::is_trivially_copyable_v<ContactShadowDraw>);

using ContactCorners = std::array<std::array<float, 3>, 4>;
// Requires validated geometry; shared by the bounded-scissor path.
[[nodiscard]] ContactCorners contact_shadow_corners(const ContactSurfaceGpu& surface) noexcept;
// Geometry is already validated by validate_contact_shadows(). This helper
// independently validates projection/layout/clip inputs and never truncates.
[[nodiscard]] ContactScissor contact_shadow_scissor(
    const ContactSurfaceGpu& surface, const ContactProjection& projection,
    const ContactPhysicalLayout& layout, const ContactLogicalRect& world_rect);

// A prepared list contains only fully revealed, positive-opacity casters. The
// art adapter must filter hidden/zero-opacity casters BEFORE identity/cap checks.
// Invalid opacity (NaN, negative, >1) is an error, not a filter. Never truncate.
// Resource liveness/format/rect alignment/facing binding require the renderer's
// typed art registry and are intentionally not claimed by this CPU validator.
void validate_contact_shadows(std::span<const ContactShadowDraw> draws);

} // namespace ribterra::render

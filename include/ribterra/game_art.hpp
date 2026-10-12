#pragma once

#include "ribterra/render_frame.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace ribterra {
class AssetCatalog;
namespace render {
class Renderer2D;
}

// Retained CPU coverage and outline texture bytes, independently bounded.
// Optional catalogs receive the remaining cap after the original catalog.
struct GameArtCacheLimits {
    std::size_t hit_mask_bytes{16u * 1024u * 1024u};
    std::size_t outline_texture_bytes{20u * 1024u * 1024u};
};

// Presentation-only artwork. It never changes a world cell or simulation rule.
class GameArt {
  public:
    explicit GameArt(render::Renderer2D &renderer);
    ~GameArt();
    GameArt(const GameArt &) = delete;
    GameArt &operator=(const GameArt &) = delete;
    void load(const AssetCatalog &assets);
    void load(const AssetCatalog &assets, GameArtCacheLimits limits);
    // Legacy/BakedCompat routing only. Lit-only catalogs use contains_lit()
    // and lit_sprite() explicitly until the Dynamic GameView is integrated.
    bool ready() const;
    bool contains(std::string_view id) const;
    bool contains_lit(std::string_view id) const;
    // Explicit render-space anchor; schema-1 sprites are not silently promoted.
    // The caller supplies the same zoom/anchor used by the terrain projection.
    // Opacity is independent coverage, quantized into existing vertex alpha.
    // Quantized zero appends nothing; authored lights/shadows are separate calls.
    void lit_sprite(render::Frame2D &frame, std::string_view id, render::Point ground,
                    const std::array<float, 3> &render_anchor, float effective_zoom,
                    float geometry_scale = 1.f, float wetness = 0.f,
                    std::uint32_t receiver_owner = 0u, float emission_scale = 1.f,
                    float opacity = 1.f) const;
    // Uses the same source-pixel pivot/export scale as lit_sprite. CPU picking
    // follows its authored albedo coverage; reveal/opacity remain caller-owned.
    render::Rect lit_sprite_bounds(std::string_view id, render::Point ground,
                    float effective_zoom, float geometry_scale = 1.f) const;
    bool hit_test_lit_sprite(std::string_view id, render::Point ground,
                    float effective_zoom, render::Point point, float geometry_scale = 1.f) const;
    // Lazy, single-mip white coverage texture; the eight offset quads share
    // lit drawing/picking projection and append neutral companions atomically.
    void outline_lit_sprite(render::Frame2D &frame, std::string_view id,
                    render::Point ground, float effective_zoom, float geometry_scale,
                    float radius, render::Pass pass = render::Pass::WorldOrdered,
                    render::Color tint = {54, 226, 255, 245}) const;
    // Authored exterior emitters use the same instance anchor/scale as the sprite.
    // Color/intensity comes from its material, not a separately baked ground glow.
    // Fully hidden/disabled emitters consume no light slots or stable identities.
    // Enabled authored lights require independent, nonzero, collision-free IDs;
    // all rows/count are published atomically only after transforms pass.
    std::size_t append_lit_emitters(render::Frame2D &frame, std::string_view id,
                    const std::array<float, 3> &render_anchor, float geometry_scale,
                    std::uint32_t first_stable_id, bool enabled = true,
                    float reveal = 1.f) const;
    // Appends authored vertical prisms with instance-local owner/stable IDs.
    // Partially revealed instances cast no blocker; catalog metadata stores no IDs.
    std::size_t append_lit_occluders(render::Frame2D &frame, std::string_view id,
                    const std::array<float, 3> &render_anchor, float geometry_scale,
                    std::uint32_t owner_id, std::uint32_t first_stable_id,
                    float reveal = 1.f) const;
    // Appends independent authored world triangles for the fixed-camera sky
    // shadow graph. Geometry is transformed to camera-relative render space;
    // reveal remains filter-only in the renderer (zero emits no rows).
    std::size_t append_lit_sky_casters(render::Frame2D &frame, std::string_view id,
                    const std::array<float, 3> &world_anchor, float geometry_scale,
                    const std::array<float, 3> &camera_world, float reveal = 1.f) const;
    // Receiver-plane basis comes from the same terrain triangle as the feet.
    // Hidden/zero-opacity contacts consume no identity or capacity; publication
    // is atomic after validating the complete prospective contact list.
    std::size_t append_lit_contacts(render::Frame2D &frame, std::string_view id,
                    const std::array<float, 3> &render_anchor, float geometry_scale,
                    const std::array<float, 3> &axis_u, const std::array<float, 3> &axis_v,
                    std::uint32_t owner_id, std::uint32_t first_stable_id,
                    float reveal = 1.f, float opacity_scale = 1.f) const;
    render::Rect sprite_bounds(std::string_view id, render::Point ground, float width) const;
    // Selection follows opaque authored coverage, not transparent atlas gutters.
    bool hit_test_sprite(std::string_view id, render::Point ground, float width,
                         render::Point point, bool flip_x = false) const;
    bool use_native_cursor(std::string_view id) const;
    void sprite(render::Frame2D &frame, std::string_view id, render::Point ground, float width,
                render::Pass pass = render::Pass::WorldOrdered,
                render::Color tint = {255, 255, 255, 255}, bool flip_x = false) const;
    void outline_sprite(render::Frame2D &frame, std::string_view id, render::Point ground, float width,
                        float radius, render::Pass pass = render::Pass::WorldOrdered,
                        render::Color tint = {54, 226, 255, 245}, bool flip_x = false) const;
    std::size_t outline_texture_bytes() const noexcept;
    std::size_t outline_sprite_count() const noexcept;
    std::size_t hit_mask_bytes() const noexcept;
    std::size_t hit_mask_count() const noexcept;
    void tile(render::Frame2D &frame, std::string_view id, render::Point center, float half_width,
              float half_height, render::Pass pass = render::Pass::Terrain,
              render::Rect region = {0, 0, 1, 1}, render::Color tint = {255, 255, 255, 255}) const;

    void surface(render::Frame2D &frame, std::string_view id, const std::array<render::Point, 4> &corners,
                 render::Rect region = {0, 0, 1, 1}, render::Color tint = {255, 255, 255, 255}) const;

    void surface_colors(render::Frame2D &frame, std::string_view id,
                        const std::array<render::Point, 4> &corners, render::Rect region,
                        const std::array<render::Color, 4> &colors) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ribterra

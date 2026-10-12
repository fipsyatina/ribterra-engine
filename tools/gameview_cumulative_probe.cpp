#include "engine_control_probe.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_art.hpp"
#include <limits>
#include <map>
#include <span>

namespace {
constexpr double shown_seconds = 10.;
constexpr std::array<std::string_view, 8> categories{
    "house", "stockpile", "workshop", "tree", "rock",
    "resident.empty", "resident.log", "resident.sack"};

Json read_control_metadata(const fs::path& kit) {
    std::ifstream stream(kit / "art/control-kit.json", std::ios::binary);
    require(stream.good(), "Missing diagnostic control metadata");
    Json metadata; stream >> metadata;
    require(metadata.at("production_catalog").get<bool>() == false &&
            metadata.at("category_count").get<std::size_t>() == categories.size(),
            "Cumulative probe requires the diagnostic eight-category kit");
    return metadata;
}
const Json& category_metadata(const Json& metadata, std::string_view id) {
    for (const auto& row : metadata.at("categories"))
        if (row.at("id").get<std::string>() == "control." + std::string(id)) return row;
    throw std::runtime_error("Missing diagnostic category: " + std::string(id));
}
std::size_t changed_pixels(const Image& first, const Image& second) {
    require(first.rgba.size() == second.rgba.size(), "Different capture dimensions");
    std::size_t count{};
    for (std::size_t p = 0; p < first.rgba.size(); p += 4u)
        if (!std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                        first.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                        second.rgba.begin() + static_cast<std::ptrdiff_t>(p))) ++count;
    return count;
}
void ui_exact(const Image& first, const Image& second) {
    for (std::size_t y = 14; y < 50; ++y) for (std::size_t x = 14; x < 90; ++x) {
        const auto p = (y * extent.width + x) * 4u;
        require(std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(p),
            first.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
            second.rgba.begin() + static_cast<std::ptrdiff_t>(p)), "Opaque diagnostic UI changed");
    }
}
void ui_marker_present(const Image& image) {
    constexpr std::array<std::uint8_t, 4> marker{31u, 189u, 78u, 255u};
    for (std::size_t y = 14; y < 50; ++y) for (std::size_t x = 14; x < 90; ++x) {
        const auto p = (y * extent.width + x) * 4u;
        require(std::equal(marker.begin(), marker.end(),
            image.rgba.begin() + static_cast<std::ptrdiff_t>(p)), "Baseline opaque diagnostic UI marker is absent or changed");
    }
}
Image cumulative_capture(Renderer2D& renderer, const Frame2D& source, const fs::path& output,
                         std::string_view name, Json& captures) {
    require(captures.size() < 50u, "Cumulative PNG bound exceeded");
    Frame2D frame = source; append_ui(frame);
    const auto file = output / (std::string(name) + ".png");
    require(!fs::exists(file), "Refusing cumulative capture overwrite");
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    renderer.render(frame, file);
    const auto telemetry = renderer.frame_telemetry();
    std::vector<WorldGraphPass> order{WorldGraphPass::SunShadow, WorldGraphPass::MoonShadow,
        WorldGraphPass::Reflection, WorldGraphPass::GroundGBuffer};
    if (!frame.contact_shadows.empty()) order.push_back(WorldGraphPass::GroundContact);
    order.insert(order.end(), {WorldGraphPass::GroundLighting, WorldGraphPass::WorldForward,
                              WorldGraphPass::Composite, WorldGraphPass::Ui});
    require(telemetry.submitted && telemetry.capture && telemetry.graph_pass_count == order.size() &&
            std::equal(order.begin(), order.end(), telemetry.graph_passes.begin()) &&
            telemetry.contact_casters == frame.contact_shadows.size(),
            "Cumulative contact/water/atmosphere pass order or contact count is wrong");
    captures.push_back({{"file", file.filename().string()}, {"bytes", fs::file_size(file)},
        {"graph_passes", telemetry.graph_pass_count}, {"contacts", telemetry.contact_casters},
        {"water_draws", telemetry.water_draws}, {"reflection_draws", telemetry.reflection_draws},
        {"gpu_execution_ms", nullptr}});
    check_budget(output); return read_image(file);
}
Json geometry_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass), static_cast<int>(draw.blend), draw.texture.slot,
        draw.texture.generation, draw.first_index, draw.index_count, draw.scissor.x,
        draw.scissor.y, draw.scissor.w, draw.scissor.h, draw.scissor_enabled, draw.surface_lit,
        draw.sprite_material_index, draw.ground_material_index};
}
// Field-wise serialization avoids uninitialized CPU-wrapper padding. This is an
// internal transactional comparison, not a replacement for build source bindings.
std::string frame_fingerprint(const Frame2D& frame) {
    Json rows{{"size", {frame.logical_size.width, frame.logical_size.height}},
        {"clear", {frame.clear.r, frame.clear.g, frame.clear.b, frame.clear.a}},
        {"indices", frame.indices}, {"hdr", frame.linear_hdr_world}, {"exposure", frame.world_exposure},
        {"water_normal", {frame.water_normal.slot, frame.water_normal.generation}}};
    for (const auto& v : frame.vertices)
        rows["vertices"].push_back({v.position.x, v.position.y, v.uv.x, v.uv.y,
                                   v.color.r, v.color.g, v.color.b, v.color.a});
    for (const auto& v : frame.surface_vertices) rows["surfaces"].push_back({v.world_position, v.normal});
    for (const auto& command : frame.commands) {
        if (const auto* draw = std::get_if<GeometryDraw>(&command))
            rows["commands"].push_back({{"geometry", geometry_row(*draw)}});
        else {
            const auto& t = std::get<TextDraw>(command);
            rows["commands"].push_back({{"text", t.text}, {"pass", static_cast<int>(t.pass)},
                {"position", {t.position.x, t.position.y}}, {"size", t.size},
                {"color", {t.color.r, t.color.g, t.color.b, t.color.a}}, {"max_width", t.max_width}});
        }
    }
    for (const auto& d : frame.sprite_material_draws)
        rows["sprites"].push_back({d.material.slot, d.material.generation, d.surface.uv_regions,
            d.surface.anchor_export, d.surface.source_size_pivot, d.surface.depth_scale, d.surface.emission,
            d.surface.response, d.reveal, d.receiver_mask, d.receiver_owner, d.emission_scale});
    for (const auto& d : frame.ground_material_draws)
        rows["ground"].push_back({d.albedo.slot, d.albedo.generation, d.surface.response, d.surface.flags,
            d.surface.visibility, static_cast<int>(d.contact_receiver), static_cast<int>(d.water_surface_class),
            d.shore_edges, d.geometric_coverage});
    for (const auto& d : frame.local_occluders)
        rows["occluders"].push_back({d.center_base, d.axis_x_extent, d.axis_y_radius, d.shape_flags_id});
    for (const auto& d : frame.contact_shadows)
        rows["contacts"].push_back({d.surface.anchor_opacity, d.surface.axis_u_extent, d.surface.axis_v_extent,
            d.surface.uv_region, d.surface.pivot_plane, d.art.slot, d.art.generation, d.stable_id, d.owner_id,
            d.reveal, static_cast<int>(d.facing), d.padding_zero});
    for (const auto& d : frame.sky_shadow_triangles)
        rows["sky"].push_back({d.vertices[0].position, d.vertices[1].position,
                              d.vertices[2].position, d.source_mask, d.reveal});
    for (const auto& d : frame.reflection_draws)
        rows["reflections"].push_back({geometry_row(d.draw), d.strength, d.fade});
    rows["lighting"] = {frame.surface_lighting.ambient, frame.surface_lighting.sun_direction,
                        frame.surface_lighting.sun_color_intensity, frame.surface_lighting.counts};
    for (const auto& light : frame.surface_lighting.lights)
        rows["lights"].push_back({light.anchor_radius, light.direction_cone, light.color_intensity,
                                 light.occluder_range_flags, light.height_falloff_reveal});
    if (frame.water_surface) {
        const auto& w = *frame.water_surface;
        rows["water"] = {w.world_origin_time, w.flow_response, w.tint, w.viewport};
    }
    if (frame.atmosphere) {
        const auto& a = *frame.atmosphere;
        // AtmosphereGpu is a validated GPU payload, without CPU-wrapper gaps.
        rows["atmosphere_bytes"] = sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(&a), sizeof(a)));
    }
    if (frame.contact_projection)
        rows["projection"] = {frame.contact_projection->origin_xy, frame.contact_projection->zoom};
    if (frame.moon_lighting)
        rows["moon"] = {frame.moon_lighting->direction, frame.moon_lighting->color_intensity};
    const auto text = rows.dump();
    return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
const SpriteMaterialDraw& material_for(const Frame2D& frame, std::uint32_t owner) {
    const SpriteMaterialDraw* found{};
    for (const auto& material : frame.sprite_material_draws) if (material.receiver_owner == owner) {
        require(found == nullptr, "Diagnostic sprite owner is not unique"); found = &material;
    }
    require(found != nullptr, "Diagnostic sprite owner has no material"); return *found;
}
Point probe_screen(const Frame2D& frame, std::uint32_t owner, const std::array<float, 2>& source) {
    const auto& material = material_for(frame, owner);
    const auto material_index = static_cast<std::uint32_t>(&material - frame.sprite_material_draws.data()) + 1u;
    const auto& size = material.surface.source_size_pivot;
    require(source[0] >= 0.f && source[0] < size[0] && source[1] >= 0.f && source[1] < size[1],
            "Category authored probe lies outside its material bounds");
    for (const auto& command : frame.commands) {
        const auto* draw = std::get_if<GeometryDraw>(&command);
        if (!draw || draw->sprite_material_index != material_index) continue;
        require(draw->index_count == 6u, "Diagnostic material is not a quad");
        const auto& first = frame.vertices.at(frame.indices.at(draw->first_index)).position;
        const auto& opposite = frame.vertices.at(frame.indices.at(draw->first_index + 2u)).position;
        // Exported source_pixel already denotes the centre x+.5/y+.5.
        return {first.x + (opposite.x - first.x) * source[0] / size[0],
                first.y + (opposite.y - first.y) * source[1] / size[1]};
    }
    throw std::runtime_error("Category material has no ordinary sprite command");
}
double patch_energy(const Image& image, Point point) {
    const int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    require(x >= 2 && y >= 2 && x < static_cast<int>(extent.width) - 2 &&
            y < static_cast<int>(extent.height) - 2, "Category probe patch lies outside the capture");
    double energy{};
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const auto pixel = (static_cast<std::size_t>(y + dy) * extent.width + static_cast<std::size_t>(x + dx)) * 4u;
        for (std::size_t channel = 0; channel < 3u; ++channel) energy += image.rgba[pixel + channel];
    }
    return energy / 75.;
}
std::size_t patch_difference(const Image& first, const Image& second, Point point) {
    (void)patch_energy(first, point); // Also validates the complete patch bounds.
    const int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    std::size_t changed{};
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const auto p = (static_cast<std::size_t>(y + dy) * extent.width + static_cast<std::size_t>(x + dx)) * 4u;
        int difference{};
        for (std::size_t channel = 0; channel < 3u; ++channel)
            difference += std::abs(static_cast<int>(first.rgba[p + channel]) - second.rgba[p + channel]);
        if (difference > 2) ++changed;
    }
    return changed;
}
double visible_patch_response(const Image& source, const Image& black,
                              const Image& local, const Image& removed, Point point) {
    (void)patch_energy(source, point);
    const int x = static_cast<int>(std::floor(point.x)), y = static_cast<int>(std::floor(point.y));
    double delta{}; std::size_t components{};
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const auto p = (static_cast<std::size_t>(y + dy) * extent.width + static_cast<std::size_t>(x + dx)) * 4u;
        int ownership_difference{};
        for (std::size_t channel = 0; channel < 3u; ++channel)
            ownership_difference += std::abs(static_cast<int>(local.rgba[p + channel]) - removed.rgba[p + channel]);
        if (ownership_difference <= 2) continue;
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            delta += static_cast<int>(source.rgba[p + channel]) - black.rgba[p + channel]; ++components;
        }
    }
    require(components > 0u, "Category response has no removal-qualified native pixels");
    return delta / static_cast<double>(components);
}
Frame2D black_frame(Frame2D frame) {
    frame.surface_lighting.ambient = {0.f, 0.f, 0.f, 0.f};
    frame.surface_lighting.sun_color_intensity[3] = 0.f;
    frame.surface_lighting.counts = {};
    frame.moon_lighting->color_intensity[3] = 0.f;
    for (auto& material : frame.sprite_material_draws) material.emission_scale = 0.f;
    frame.atmosphere->counts_version[0] = 0u; frame.atmosphere->counts_version[2] = 0u;
    frame.atmosphere->water_aabbs = {};
    return frame;
}
std::size_t land_difference(const Image& first, const Image& second, const WaterCoverageReadback& carrier) {
    std::size_t pixels{};
    for (std::size_t p = 0; p < first.rgba.size(); p += 4u)
        if (carrier.rgba8[p + 1u] == 1u &&
            !std::equal(first.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                first.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                second.rgba.begin() + static_cast<std::ptrdiff_t>(p))) ++pixels;
    return pixels;
}

using FixtureBlobs = std::map<std::string, std::vector<std::uint8_t>>;
std::vector<std::uint8_t> text_bytes(std::string_view value) {
    return {reinterpret_cast<const std::uint8_t*>(value.data()),
            reinterpret_cast<const std::uint8_t*>(value.data() + value.size())};
}
// Each negative owns a tiny, retained catalog. A freshly hashed manifest keeps
// AssetCatalog valid, so rejection evidence belongs to the C++ GameArt loader.
// No production kit file is edited or copied and no generated file is replaced.
fs::path loader_fixture(const fs::path& output, std::string_view name, const Json& document,
                        FixtureBlobs blobs) {
    const auto root = output / ("loader-" + std::string(name));
    require(!fs::exists(root) && fs::create_directory(root), "Loader fixture path is not fresh");
    blobs.emplace("art/world.json", text_bytes(document.dump(2) + "\n"));
    Json records = Json::array();
    for (const auto& [relative, bytes] : blobs) {
        fs::create_directories((root / relative).parent_path());
        save_bytes(root / relative, bytes);
        records.push_back({{"id", "fixture." + relative}, {"path", relative}, {"kind", "data"},
            {"sha256", sha256(bytes)}, {"license", "LicenseRef-Ribterra-Diagnostic"},
            {"source", "native bounded loader fixture"}, {"author", "Ribterra project"},
            {"notice", "NOTICE.txt"}, {"bytes", bytes.size()}});
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.at("id").template get<std::string>() < b.at("id").template get<std::string>();
    });
    std::string canonical;
    for (const auto& row : records) {
        for (const std::string_view field : {"id", "path", "kind", "sha256", "license", "source", "author", "notice"}) {
            const auto value = row.at(std::string(field)).get<std::string>();
            canonical += std::to_string(value.size()) + ":" + value;
        }
        canonical += '\n';
    }
    save_report(root / "manifest.json", {{"schema", 1}, {"assets", records},
        {"content_sha256", sha256(text_bytes(canonical))}});
    save_bytes(root / "NOTICE.txt", text_bytes("Generated native diagnostic loader fixture; not production artwork.\n"));
    check_budget(output); return root;
}
std::pair<Json, FixtureBlobs> small_loader_catalog() {
    FixtureBlobs blobs; Json textures = Json::array(), channels = Json::object();
    constexpr std::array<std::string_view, 6> names{
        "albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask"};
    constexpr std::array<std::string_view, 6> formats{
        "rgba8_unorm_srgb", "rgba8_unorm", "r16_unorm", "r8_unorm", "rgba8_unorm", "r8_unorm"};
    constexpr std::array<std::size_t, 6> texel_bytes{4u, 4u, 2u, 1u, 4u, 1u};
    for (std::size_t channel = 0; channel < names.size(); ++channel) {
        const auto name = std::string(names[channel]), id = "fixture.texture." + name;
        const auto path = "art/" + name + ".rtex";
        std::vector<std::uint8_t> bytes(9u * texel_bytes[channel], 255u);
        blobs.emplace(path, bytes);
        textures.push_back({{"id", id}, {"path", path}, {"format", formats[channel]},
            {"width", 3}, {"height", 3}, {"layout", "tight_le"}, {"mip_levels", 1},
            {"bytes", bytes.size()}, {"sha256", sha256(bytes)}});
        channels[name] = {{"texture", id}, {"rect", {0, 0, 3, 3}}};
    }
    const std::vector<std::uint8_t> mask{0u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 0u};
    blobs.emplace("art/contact.rtex", mask);
    Json document{{"schema", 2}, {"pages", Json::array()}, {"textures", textures},
        {"materials", Json::array({{{"id", "fixture.material"}, {"family", "DynamicSprite25D"},
            {"alpha", "straight"}, {"camera", "ribterra-affine-v1"}, {"revision", 1},
            {"source_hash", sha256(text_bytes("native diagnostic three-pixel material"))},
            {"channels", channels}, {"emission", {0.f, 0.f, 0.f, 0.f}}}})},
        {"contact_arts", Json::array({{{"id", "fixture.contact"}, {"path", "art/contact.rtex"},
            {"format", "r8_unorm"}, {"layout", "tight_le"}, {"width", 3}, {"height", 3},
            {"region", {0, 0, 3, 3}}, {"facing", "omnidirectional"}, {"bytes", mask.size()},
            {"sha256", sha256(mask)}}})},
        {"lit_sprites", Json::array({{{"id", "fixture.sprite"}, {"material", "fixture.material"},
            {"source_size", {3.f, 3.f}}, {"pivot_px", {1.5f, 1.5f}}, {"depth_range", {-1.f, 1.f}},
            {"export_pixels_per_reference_pixel", 1.f},
            {"contact_shadows", Json::array({{{"art", "fixture.contact"}, {"anchor_local", {0.f, 0.f, 0.f}},
                {"half_extent", {.25f, .25f}}, {"pivot", {.5f, .5f}}, {"opacity", .5f},
                {"plane_tolerance", .04f}}})}}})}};
    return {std::move(document), std::move(blobs)};
}
struct StartupChecks {
    Json loader = Json::array(), adapter = Json::array();
    ContactShadowDraw released_art_draw{};
};
StartupChecks startup_contacts(Renderer2D& renderer, const fs::path& kit, const fs::path& output,
                               const Json& metadata) {
    require(!renderer.has_submitted_frames(), "Startup loader checks ran after submission");
    StartupChecks checks;
    const auto initial = renderer.contact_art_status();
    const auto [original, original_blobs] = small_loader_catalog();
    {
        GameArt art(renderer);
        art.load(AssetCatalog(loader_fixture(output, "positive", original, original_blobs)));
        require(art.contains_lit("fixture.sprite") && renderer.contact_art_status().live_arts == initial.live_arts + 1u,
                "Native minimal contact catalog did not install");
        Frame2D published;
        require(art.append_lit_contacts(published, "fixture.sprite", {0.f, 0.f, 0.f}, 1.f,
            {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 21u, 31u) == 1u, "Positive loader contact did not append");
        checks.released_art_draw = published.contact_shadows.front();
        const auto fingerprint = frame_fingerprint(published);
        checks.loader.push_back({{"id", "positive-three-pixel-schema2"}, {"installed_contact_arts", 1}});
        const auto reject = [&](std::string_view name, const auto& mutate) {
            auto document = original; auto blobs = original_blobs; mutate(document, blobs);
            const AssetCatalog candidate(loader_fixture(output, name, document, std::move(blobs)));
            const auto before = renderer.contact_art_status();
            std::string failure;
            try { art.load(candidate); } catch (const std::exception& caught) { failure = caught.what(); }
            const auto after = renderer.contact_art_status();
            Frame2D recovered;
            require(!failure.empty() && after.live_arts == before.live_arts &&
                after.logical_bytes == before.logical_bytes && after.creation_attempts == before.creation_attempts &&
                art.contains_lit("fixture.sprite") && art.append_lit_contacts(recovered, "fixture.sprite",
                    {0.f, 0.f, 0.f}, 1.f, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 21u, 31u) == 1u &&
                frame_fingerprint(recovered) == fingerprint && !renderer.has_submitted_frames(),
                "Native contact loader rejection uploaded GPU data or replaced its prior catalog");
            checks.loader.push_back({{"id", name}, {"cpp_loader_rejected", true}, {"error", failure},
                {"gpu_creation_attempts_exact", true}, {"previous_catalog_exact", true}});
        };
        reject("format", [](auto& d, auto&) { d["contact_arts"][0]["format"] = "rgba8_unorm"; });
        reject("layout", [](auto& d, auto&) { d["contact_arts"][0]["layout"] = "padded"; });
        reject("dimension", [](auto& d, auto&) { d["contact_arts"][0]["width"] = 1025; });
        reject("region", [](auto& d, auto&) { d["contact_arts"][0]["region"][0] = 1; });
        reject("region-type", [](auto& d, auto&) { d["contact_arts"][0]["region"][0] = true; });
        reject("byte-ledger", [](auto& d, auto&) { d["contact_arts"][0]["bytes"] = 8; });
        reject("descriptor-hash", [](auto& d, auto&) { d["contact_arts"][0]["sha256"] = std::string(64u, '0'); });
        reject("facing", [](auto& d, auto&) { d["contact_arts"][0]["facing"] = "invalid"; });
        reject("duplicate-art", [](auto& d, auto&) { const auto copy = d["contact_arts"][0]; d["contact_arts"].push_back(copy); });
        reject("art-extra-field", [](auto& d, auto&) { d["contact_arts"][0]["unexpected"] = 1; });
        reject("unknown-art", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["art"] = "missing"; });
        reject("anchor-off-plane", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["anchor_local"][2] = .01f; });
        reject("anchor-type", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["anchor_local"][0] = true; });
        reject("extent", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["half_extent"][0] = 0; });
        reject("pivot", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["pivot"][0] = 1.1f; });
        reject("opacity", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["opacity"] = 1.1f; });
        reject("plane-tolerance", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["plane_tolerance"] = .01f; });
        reject("footprint-extra-field", [](auto& d, auto&) { d["lit_sprites"][0]["contact_shadows"][0]["unexpected"] = 1; });
        reject("mask-gutter", [](auto& d, auto& b) {
            b.at("art/contact.rtex")[0] = 255u; d["contact_arts"][0]["sha256"] = sha256(b.at("art/contact.rtex"));
        });
        reject("mask-empty", [](auto& d, auto& b) {
            b.at("art/contact.rtex")[4] = 0u; d["contact_arts"][0]["sha256"] = sha256(b.at("art/contact.rtex"));
        });
    }
    require(renderer.contact_art_status().live_arts == initial.live_arts &&
            renderer.contact_art_status().logical_bytes == initial.logical_bytes,
            "Temporary loader catalog leaked contact registry resources");
    {
        GameArt art(renderer); art.load(AssetCatalog(kit));
        Frame2D published; std::size_t contacts{};
        for (std::size_t index = 0; index < categories.size(); ++index) {
            const auto& row = category_metadata(metadata, categories[index]);
            require(art.contains_lit(row.at("sprite_id").get<std::string>()), "Actual kit category did not load in C++");
            contacts += art.append_lit_contacts(published, row.at("sprite_id").get<std::string>(),
                {0.f, 0.f, 0.f}, 1.f, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                101u + static_cast<std::uint32_t>(index), 1001u + static_cast<std::uint32_t>(index) * 32u);
        }
        require(contacts == metadata.at("contact_art_count").get<std::size_t>() && contacts == 10u,
                "Actual native exported kit lost a body or independent cargo contact");
        checks.loader.push_back({{"id", "actual-neutral-eight-export"}, {"installed_contact_arts", contacts},
                                 {"categories_loaded", categories.size()}});
        const auto fingerprint = frame_fingerprint(published);
        const auto sprite = category_metadata(metadata, categories[0]).at("sprite_id").get<std::string>();
        const auto cargo_sprite = category_metadata(metadata, categories[6]).at("sprite_id").get<std::string>();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (const float reveal : {0.f, .5f})
            require(art.append_lit_contacts(published, "invalid-filtered-contact", {nan, nan, nan}, nan,
                {nan, nan, nan}, {nan, nan, nan}, 0u, 0u, reveal) == 0u,
                "Hidden contact was validated before filtering");
        require(art.append_lit_contacts(published, "invalid-filtered-contact", {nan, nan, nan}, nan,
            {nan, nan, nan}, {nan, nan, nan}, 0u, 0u, 1.f, 0.f) == 0u &&
            frame_fingerprint(published) == fingerprint, "Zero opacity consumed a row, slot or identity");
        checks.adapter.push_back({{"id", "hidden-and-zero-opacity-filter-before-validation"}, {"frame_exact", true}});
        const auto reject = [&](std::string_view label, const auto& append) {
            auto candidate = published; bool rejected{};
            try { append(candidate); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && frame_fingerprint(candidate) == fingerprint,
                    "Native contact adapter rejection partially appended rows");
            checks.adapter.push_back({{"id", label}, {"frame_exact", true}});
        };
        const auto append = [&](Frame2D& frame, std::string_view id, std::uint32_t owner, std::uint32_t first,
                                float scale = 1.f, float reveal = 1.f, float opacity = 1.f) {
            (void)art.append_lit_contacts(frame, id, {0.f, 0.f, 0.f}, scale,
                {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, owner, first, reveal, opacity);
        };
        reject("duplicate-id", [&](auto& frame) { append(frame, sprite, 777u, 1001u); });
        reject("zero-owner", [&](auto& frame) { append(frame, sprite, 0u, 4001u); });
        reject("zero-stable-id", [&](auto& frame) { append(frame, sprite, 777u, 0u); });
        reject("cargo-id-overflow", [&](auto& frame) { append(frame, cargo_sprite, 777u, UINT32_MAX); });
        reject("zero-scale", [&](auto& frame) { append(frame, sprite, 777u, 4001u, 0.f); });
        reject("nonfinite-reveal", [&](auto& frame) { append(frame, sprite, 777u, 4001u, 1.f, nan); });
        reject("negative-opacity", [&](auto& frame) { append(frame, sprite, 777u, 4001u, 1.f, 1.f, -.1f); });
        reject("nonfinite-anchor", [&](auto& frame) {
            (void)art.append_lit_contacts(frame, sprite, {nan, 0.f, 0.f}, 1.f,
                {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 777u, 4001u);
        });
        reject("nonorthogonal-plane", [&](auto& frame) {
            (void)art.append_lit_contacts(frame, sprite, {0.f, 0.f, 0.f}, 1.f,
                {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, 777u, 4001u);
        });
        Frame2D full;
        for (std::size_t index = 0; index < contact_shadow_capacity; ++index) {
            auto row = published.contact_shadows.front(); row.stable_id = 20001u + static_cast<std::uint32_t>(index);
            full.contact_shadows.push_back(row);
        }
        validate_contact_shadows(full.contact_shadows);
        const auto full_fingerprint = frame_fingerprint(full); bool overflow{};
        try { append(full, sprite, 777u, 4001u); } catch (const std::invalid_argument&) { overflow = true; }
        require(overflow && frame_fingerprint(full) == full_fingerprint, "Contact capacity overflow mutated the full list");
        checks.adapter.push_back({{"id", "capacity-128-no-truncation"}, {"frame_exact", true}});
    }
    require(renderer.contact_art_status().live_arts == initial.live_arts &&
        renderer.contact_art_status().logical_bytes == initial.logical_bytes && !renderer.has_submitted_frames(),
        "Temporary actual-kit adapter checks leaked contacts or submitted a frame");
    save_report(output / "startup-contact-checks.json", {{"cpp_loader", checks.loader},
        {"cpp_adapter", checks.adapter}, {"ran_before_frame_submission", true},
        {"fixture_scope", "retained three-pixel schema2 loader catalogs; actual kit loaded separately"}});
    return checks;
}
void create_steep_fixture(const fs::path& target) {
    // Independent, empty-population diagnostic world: changing an elevation in
    // a loaded gameplay fixture can invalidate its stored traversal seams.
    // The native resident below is an explicit presentation control, not an
    // actor added to this World or an alteration of the baseline saved actors.
    World world(70421, 0); std::string error;
    auto state = world.export_state();
    require(state.residents.empty() && state.routes.empty() && state.path_batches.empty(),
            "Steep diagnostic world unexpectedly owns actor routes or searches");
    for (auto& chunk : state.chunks) {
        if (chunk.key.layer != WorldLayer::Surface) continue;
        for (std::size_t index = 0; index < chunk.tiles.size(); ++index) {
            const int x = chunk.key.cx * chunk_size + static_cast<int>(index % chunk_size);
            const int y = chunk.key.cy * chunk_size + static_cast<int>(index / chunk_size);
            if (x >= 33 && x <= 39 && y >= 14 && y <= 20)
                chunk.tiles[index] = Tile{Terrain::Grass, 0, true, Biome::Meadow,
                    static_cast<std::int16_t>(x >= 36 ? 160 : 0)};
        }
    }
    ++state.topology_revision;
    const bool imported = world.import_state(std::move(state), error);
    require(imported, "Steep diagnostic World import failed: " + error);
    require(!fs::exists(target), "Steep diagnostic save path already exists");
    const bool saved = world.save(target, error);
    require(saved, "Steep diagnostic World save failed: " + error);
}
} // namespace

int main(int argc, char** argv) {
    fs::path output; bool owned{}; Json captures = Json::array(), category_checks = Json::array();
    try {
        require(argc == 4, "Usage: GameViewWaterProbe runtime-assets kit fresh-output (cumulative probe)");
        const auto assets = fs::absolute(argument(argv[1])), kit = fs::absolute(argument(argv[2]));
        output = fs::absolute(argument(argv[3]));
        require(output.parent_path() == assets.parent_path().parent_path() && !fs::exists(output),
                "Cumulative output must be a new sibling build child");
        require(fs::space(output.parent_path()).available >= 384u * 1024u * 1024u, "Insufficient headroom");
        const auto metadata = read_control_metadata(kit);
        require(fs::create_directory(output), "Could not create cumulative output"); owned = true;
        create_fixture(output / "cumulative-world-current.save");
        create_steep_fixture(output / "steep-world-current.save");
        require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra cumulative neutral controls - not final art", 1920, 1080, 0), SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()), "Native window failed");
        RendererOptions options; options.world_graph = WorldGraph::WaterAtmosphereSlice;
        options.collect_telemetry = true;
        Renderer2D renderer(window.get(), assets, options); renderer.set_logical_size(extent);
        require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
                "Cumulative proof requires hardware Direct3D12");
        const auto startup = startup_contacts(renderer, kit, output, metadata);
        GameView view(renderer); view.set_logical_size(extent);
        view.load_engine_control_art(AssetCatalog(kit)); view.load_engine_water_art();
        ClientSession session(70421); std::string error;
        const bool baseline_loaded = session.load(output / "cumulative-world-current.save", error);
        require(baseline_loaded, "Baseline diagnostic ClientSession load failed: " + error);
        require(!session.last_load().copied, "Baseline current-schema diagnostic load unexpectedly copied/migrated the save");
        EngineControlSettings settings; settings.camera_x = 28.f; settings.camera_y = 25.f;
        settings.cell_radius = 12; settings.water_enabled = true; settings.contact_enabled = true;
        settings.environment = EngineEnvironmentSettingsV1{1u, 12.f, 0.f, .25f, true};
        constexpr std::array<std::array<float, 2>, 8> positions{{
            {25.f, 22.f}, {31.f, 21.f}, {21.f, 22.f}, {34.f, 24.f},
            {23.f, 29.f}, {25.f, 24.f}, {28.f, 25.f}, {32.f, 28.f}}};
        std::vector<EngineControlInstance> instances;
        std::size_t expected_contacts{};
        for (std::size_t index = 0; index < categories.size(); ++index) {
            const auto& row = category_metadata(metadata, categories[index]);
            require(!row.at("probes").empty() && !row.at("contact_shadows").empty(),
                    "Every neutral category requires authored material probes and contact masks");
            expected_contacts += row.at("contact_shadows").size();
            const auto ordinal = static_cast<std::uint32_t>(index);
            instances.push_back({row.at("sprite_id").get<std::string>(), positions[index][0], positions[index][1],
                1.f, 101u + ordinal, 1001u + ordinal * 32u, 0.f, true,
                index == 0u ? 9001u : 0u, 2001u + ordinal * 32u});
        }
        Frame2D baseline;
        const auto stats = view.compose_engine_control(session, shown_seconds, settings, instances, baseline);
        require(stats.instances_drawn == 8u && stats.instances_hidden == 0u && stats.water_cells > 0u &&
            stats.bridge_decks == 3u && stats.sky_triangles > 0u && stats.local_occluders > 0u &&
            stats.local_lights == 2u && stats.reflection_draws > 0u && stats.atmosphere_water_patches > 0u &&
            stats.contact_shadows == expected_contacts && baseline.contact_shadows.size() == expected_contacts,
            "Eight-category cumulative production composition is vacuous or incomplete");
        require(baseline.vertices.size() == baseline.surface_vertices.size(), "Companion stream mismatch");
        validate_contact_shadows(baseline.contact_shadows);
        for (const auto& row : baseline.contact_shadows) {
            require(std::any_of(instances.begin(), instances.end(), [&](const auto& instance) {
                return row.owner_id == instance.owner_id && row.stable_id >= instance.first_contact_id &&
                       row.stable_id < instance.first_contact_id + 32u;
            }), "Contact escaped its independent owner/identity range");
            if (row.owner_id == instances[6].owner_id)
                require(std::abs(row.surface.anchor_opacity[2] - .08f) < 1.e-5f &&
                    row.surface.axis_u_extent[2] == 0.f && row.surface.axis_v_extent[2] == 0.f,
                    "Completed bridge contact is not attached to its deck plane");
        }
        const auto& deck_anchor = material_for(baseline, instances[6].owner_id).surface.anchor_export;
        const auto deck_projected = contact_project_logical({deck_anchor[0], deck_anchor[1], deck_anchor[2]},
                                                            *baseline.contact_projection);
        const auto deck_public = view.tile_screen(instances[6].x, instances[6].y);
        require(std::abs(deck_public.x - deck_projected[0]) < 1.e-4f &&
                std::abs(deck_public.y - deck_projected[1]) < 1.e-4f,
                "Published public tile screen disagrees with the completed-deck sprite feet");
        Json registry_rejections = Json::array();
        const auto registry_reject = [&](std::string_view label, Frame2D candidate, std::string_view expected_error) {
            const auto baseline_fingerprint = frame_fingerprint(baseline);
            const auto registry = renderer.contact_art_status(); std::string failure;
            try { renderer.render(candidate); } catch (const std::invalid_argument& caught) { failure = caught.what(); }
            const auto recovered = renderer.contact_art_status();
            require(failure.find(expected_error) != std::string::npos && !renderer.has_submitted_frames() &&
                registry.live_arts == recovered.live_arts && registry.logical_bytes == recovered.logical_bytes &&
                registry.creation_attempts == recovered.creation_attempts &&
                frame_fingerprint(baseline) == baseline_fingerprint,
                "Typed contact registry rejection submitted a frame or mutated the published candidate");
            registry_rejections.push_back({{"id", label}, {"error", failure}, {"before_frame_submission", true}});
        };
        auto stale = baseline; stale.contact_shadows = {startup.released_art_draw};
        registry_reject("released-art-generation", std::move(stale), "stale or released");
        auto wrong_uv = baseline; wrong_uv.contact_shadows.front().surface.uv_region = {.1f, .1f, .5f, .5f};
        registry_reject("authoritative-art-uv", std::move(wrong_uv), "UV/facing");
        const auto base_image = cumulative_capture(renderer, baseline, output, "cumulative-noon", captures);
        ui_marker_present(base_image);
        const auto carrier = renderer.read_water_coverage_v1();
        const auto counts = carrier_counts(carrier); save_bytes(output / "cumulative-carrier.rgba8", carrier.rgba8);
        const auto checked = [&](const Frame2D& frame, std::string_view name, bool same_carrier = true) {
            const auto image = cumulative_capture(renderer, frame, output, name, captures); ui_exact(base_image, image);
            const auto raw = renderer.read_water_coverage_v1();
            if (same_carrier) require(raw.rgba8 == carrier.rgba8, "Light/contact/atmosphere changed raw water carrier");
            else (void)carrier_counts(raw);
            return image;
        };
        view.set_paused(true);
        Frame2D paused_frame; (void)view.compose_engine_control(session, shown_seconds, settings, instances, paused_frame);
        require(frame_fingerprint(paused_frame) == frame_fingerprint(baseline) &&
            checked(paused_frame, "cumulative-paused-repeat").rgba == base_image.rgba,
            "Paused fixed-time cumulative frame is not exact");
        auto hidden_instances = instances;
        require(!session.tile(33, 32).explored, "Hidden diagnostic cell is accidentally revealed");
        hidden_instances.push_back({"invalid-hidden-contact", 33.f, 32.f,
            std::numeric_limits<float>::quiet_NaN(), 0u, 0u, 0.f, true, 0u, 0u});
        Frame2D hidden_frame;
        const auto hidden_stats = view.compose_engine_control(session, shown_seconds, settings, hidden_instances, hidden_frame);
        require(hidden_stats.instances_hidden == 1u && hidden_stats.instances_drawn == 8u &&
            hidden_stats.contact_shadows == expected_contacts && frame_fingerprint(hidden_frame) == frame_fingerprint(baseline) &&
            checked(hidden_frame, "cumulative-hidden-filtered").rgba == base_image.rgba,
            "Hidden control leaked a row, validation error, source or native pixel");

        auto dark = black_frame(baseline);
        const auto dark_image = checked(dark, "cumulative-black-reference");
        auto ambient = dark; ambient.surface_lighting.ambient = {.18f, .18f, .18f, 0.f};
        const auto contact_on = checked(ambient, "cumulative-ambient-contact-on");
        auto no_contact = ambient; no_contact.contact_shadows.clear();
        const auto contact_off = checked(no_contact, "cumulative-ambient-contact-off");
        std::size_t contact_land{}, contact_deck{};
        for (std::size_t p = 0; p < contact_on.rgba.size(); p += 4u) {
            if (std::equal(contact_on.rgba.begin() + static_cast<std::ptrdiff_t>(p),
                contact_on.rgba.begin() + static_cast<std::ptrdiff_t>(p + 4u),
                contact_off.rgba.begin() + static_cast<std::ptrdiff_t>(p))) continue;
            require(carrier.rgba8[p + 1u] == 1u || carrier.rgba8[p + 1u] == 3u,
                    "Contact-only delta leaked into water, unknown space or UI");
            if (carrier.rgba8[p + 1u] == 1u) ++contact_land; else ++contact_deck;
        }
        require(contact_land > 10u && contact_deck > 1u, "Contact land/deck native delta is vacuous");
        auto sun = dark;
        sun.surface_lighting.sun_direction = {.57735026f, .57735026f, .57735026f, 0.f};
        sun.surface_lighting.sun_color_intensity = {1.f, .9f, .75f, .8f};
        const auto sun_image = checked(sun, "cumulative-direct-sun-contact-on");
        auto sun_contact_off = sun; sun_contact_off.contact_shadows.clear();
        require(checked(sun_contact_off, "cumulative-direct-sun-contact-off").rgba == sun_image.rgba,
                "Contact masks attenuated direct-only radiance with ambient zero");
        auto moon = dark; moon.moon_lighting->direction = {.57735026f, -.57735026f, .57735026f, 0.f};
        moon.moon_lighting->color_intensity = {.65f, .78f, 1.f, .8f};
        const auto moon_image = checked(moon, "cumulative-direct-moon");
        auto sun_open = sun; sun_open.sky_shadow_triangles.clear();
        auto moon_open = moon; moon_open.sky_shadow_triangles.clear();
        const auto sun_open_image = checked(sun_open, "cumulative-sun-sky-off");
        const auto moon_open_image = checked(moon_open, "cumulative-moon-sky-off");
        const auto sun_shadow_pixels = land_difference(sun_image, sun_open_image, carrier);
        const auto moon_shadow_pixels = land_difference(moon_image, moon_open_image, carrier);
        require(sun_shadow_pixels > 10u && moon_shadow_pixels > 10u, "Independent Sun/Moon ground shadows are vacuous");
        for (std::size_t index = 0; index < categories.size(); ++index) {
            const auto& instance = instances[index];
            const auto& row = category_metadata(metadata, categories[index]);
            const auto& probe = row.at("probes").at(0);
            const auto point = probe_screen(baseline, instance.owner_id, probe.at("source_pixel").get<std::array<float, 2>>());
            const auto reference = patch_energy(dark_image, point);
            const auto ambient_delta = patch_energy(contact_on, point) - reference;
            const auto sun_delta = patch_energy(sun_open_image, point) - reference;
            const auto moon_delta = patch_energy(moon_open_image, point) - reference;
            const auto context = "Category " + std::string(categories[index]) + " owner " +
                std::to_string(instance.owner_id) + " sample(" + std::to_string(point.x) + "," +
                std::to_string(point.y) + ")";
            category_checks.push_back({{"category", categories[index]}, {"owner", instance.owner_id},
                {"probe_scope", "authored face patch; only category-removal-qualified pixels, not every texel"},
                {"screen", {point.x, point.y}}, {"source_pixel", probe.at("source_pixel")},
                {"authored_position", probe.at("position")}, {"authored_normal", probe.at("normal")},
                {"instance_anchor", material_for(baseline, instance.owner_id).surface.anchor_export},
                {"ambient_delta", ambient_delta}, {"sun_delta", sun_delta}, {"moon_delta", moon_delta}});
            auto& category_check = category_checks.back();
            require(ambient_delta > 2. && sun_delta > 2. && moon_delta > 2.,
                context + " ambient/Sun/Moon response must exceed2: " +
                std::to_string(ambient_delta) + "/" + std::to_string(sun_delta) + "/" + std::to_string(moon_delta));
            const auto position = probe.at("position").get<std::array<float, 3>>();
            const auto normal = probe.at("normal").get<std::array<float, 3>>();
            const auto& anchor = material_for(baseline, instance.owner_id).surface.anchor_export;
            auto local = dark; local.surface_lighting.counts = {1u, 0u, 0u, 0u};
            auto& light = local.surface_lighting.lights[0];
            // black_frame disables sources but preserves their rows. Replace
            // the entire authored house spotlight with an actual point source,
            // including its cone, falloff/reveal and blocker-range defaults.
            light = LightGpu{};
            for (std::size_t axis = 0; axis < 3u; ++axis)
                light.anchor_radius[axis] = anchor[axis] + position[axis] + normal[axis] * .8f;
            light.anchor_radius[3] = 2.f; light.color_intensity = {1.f, 1.f, 1.f, 1.f};
            light.occluder_range_flags = {0u, 0u, 1u, 10001u + static_cast<std::uint32_t>(index)};
            category_check["local_source"] = {{"anchor_radius", light.anchor_radius},
                {"direction_cone", light.direction_cone}, {"falloff_reveal", light.height_falloff_reveal},
                {"occluder_range_flags", light.occluder_range_flags}};
            const auto local_image = checked(local, "cumulative-category-local-" + std::to_string(index));
            const auto local_delta = patch_energy(local_image, point) - reference;
            category_check["local_delta"] = local_delta;
            require(local_delta > 2., context + " normal-position point-light response must exceed2, got " +
                    std::to_string(local_delta));
            auto category_removed = local;
            const auto& material = material_for(category_removed, instance.owner_id);
            const auto material_index = static_cast<std::uint32_t>(&material - category_removed.sprite_material_draws.data()) + 1u;
            const auto commands_before = category_removed.commands.size();
            std::erase_if(category_removed.commands, [&](const auto& command) {
                const auto* draw = std::get_if<GeometryDraw>(&command);
                return draw && draw->sprite_material_index == material_index;
            });
            require(category_removed.commands.size() + 1u == commands_before,
                    context + " visibility control must remove exactly one ordinary sprite draw");
            const auto removed_image = checked(category_removed, "cumulative-category-removed-" + std::to_string(index));
            const auto visible_pixels = patch_difference(local_image, removed_image, point);
            category_check["category_removal_patch_pixels"] = visible_pixels;
            require(visible_pixels > 0u, context + " removal-qualified native pixels must exceed0, got " +
                    std::to_string(visible_pixels));
            const auto owned_ambient = visible_patch_response(contact_on, dark_image, local_image, removed_image, point);
            const auto owned_sun = visible_patch_response(sun_open_image, dark_image, local_image, removed_image, point);
            const auto owned_moon = visible_patch_response(moon_open_image, dark_image, local_image, removed_image, point);
            const auto owned_local = visible_patch_response(local_image, dark_image, local_image, removed_image, point);
            category_check.update({{"visible_ambient_delta", owned_ambient}, {"visible_sun_delta", owned_sun},
                {"visible_moon_delta", owned_moon}, {"visible_local_delta", owned_local}});
            require(owned_ambient > 2. && owned_sun > 2. && owned_moon > 2. && owned_local > 2.,
                context + " removal-qualified ambient/Sun/Moon/local response must exceed2: " +
                std::to_string(owned_ambient) + "/" + std::to_string(owned_sun) + "/" +
                std::to_string(owned_moon) + "/" + std::to_string(owned_local));
        }

        auto night_settings = settings;
        night_settings.environment = EngineEnvironmentSettingsV1{1u, 0.f, 0.f, 0.f, false};
        Frame2D night; (void)view.compose_engine_control(session, shown_seconds, night_settings, instances, night);
        const auto night_image = checked(night, "cumulative-night-authored-on");
        auto local_off = night;
        for (std::uint32_t index = 0; index < local_off.surface_lighting.counts[0]; ++index)
            local_off.surface_lighting.lights[index].color_intensity[3] = 0.f;
        const auto local_off_image = checked(local_off, "cumulative-night-authored-local-off");
        const auto authored_local_pixels = land_difference(night_image, local_off_image, carrier);
        require(authored_local_pixels > 10u, "Authored window lights have no ground radiance");
        auto emission = dark;
        for (auto& material : emission.sprite_material_draws)
            if (material.receiver_owner == instances.front().owner_id) material.emission_scale = .125f;
        const auto emission_image = checked(emission, "cumulative-window-emission-isolated");
        std::size_t warm_pixels{};
        for (std::size_t p = 0; p < emission_image.rgba.size(); p += 4u) {
            const int red = static_cast<int>(emission_image.rgba[p]) - dark_image.rgba[p];
            const int green = static_cast<int>(emission_image.rgba[p + 1u]) - dark_image.rgba[p + 1u];
            const int blue = static_cast<int>(emission_image.rgba[p + 2u]) - dark_image.rgba[p + 2u];
            if (red > 3 && red > green && green > blue) ++warm_pixels;
        }
        require(warm_pixels > 10u, "Authored isolated window emission is not warm");
        auto no_reflection = baseline; no_reflection.reflection_draws.clear();
        const auto reflection_off = checked(no_reflection, "cumulative-reflection-off");
        const auto reflection_pixels = water_only_difference(base_image, reflection_off, baseline);
        require(reflection_pixels > 10u, "Cumulative water reflection is vacuous");
        auto no_fog = baseline;
        no_fog.atmosphere->counts_version[0] = 0u; no_fog.atmosphere->counts_version[2] = 0u;
        no_fog.atmosphere->water_aabbs = {};
        require(changed_pixels(base_image, checked(no_fog, "cumulative-atmosphere-off")) > 100u,
                "Cumulative atmosphere has no native contribution");

        auto water_instances = instances;
        require(session.tile(28, 23).terrain == Terrain::Water && session.tile(28, 23).explored,
                "Ordinary-water negative fixture is not revealed water");
        water_instances.push_back({"control.resident.empty.sprite", 28.f, 23.f, 1.f,
                                  501u, 15001u, 0.f, false, 0u, 16001u});
        // Same water cell as the completed deck, but beyond its exact .22 Y
        // half width: the opening must retain ordinary-water contact semantics.
        water_instances.push_back({"control.resident.empty.sprite", 28.f, 25.4f, 1.f,
                                  502u, 17001u, 0.f, false, 0u, 18001u});
        Frame2D water_frame;
        const auto water_stats = view.compose_engine_control(session, shown_seconds, settings, water_instances, water_frame);
        require(water_stats.instances_drawn == 10u && water_stats.contact_shadows == expected_contacts &&
            water_stats.contact_receivers_skipped == 2u &&
            std::none_of(water_frame.contact_shadows.begin(), water_frame.contact_shadows.end(),
                         [](const auto& row) { return row.owner_id == 501u || row.owner_id == 502u; }),
            "Ordinary water or a bridge opening accepted a contact receiver");
        (void)checked(water_frame, "cumulative-water-nonreceiver");
        auto no_water_settings = settings; no_water_settings.environment.reset();
        no_water_settings.atmosphere.reset(); no_water_settings.water_enabled = false;
        no_water_settings.camera_x = 28.f; no_water_settings.camera_y = 23.f; no_water_settings.cell_radius = 3;
        const std::array<EngineControlInstance, 3> no_water_instances{instances[0], water_instances[8], instances[6]};
        Frame2D no_water_frame;
        const auto no_water_stats = view.compose_engine_control(session, shown_seconds,
            no_water_settings, no_water_instances, no_water_frame);
        const auto no_water_expected_contacts = category_metadata(metadata, categories[0]).at("contact_shadows").size() +
            category_metadata(metadata, categories[6]).at("contact_shadows").size();
        require(no_water_stats.instances_drawn == 3u && no_water_stats.water_cells == 0u &&
            no_water_stats.bridge_decks == 3u && no_water_stats.contact_shadows == no_water_expected_contacts &&
            no_water_stats.contact_receivers_skipped == 1u && !no_water_frame.water_surface &&
            std::none_of(no_water_frame.contact_shadows.begin(), no_water_frame.contact_shadows.end(),
                         [](const auto& row) { return row.owner_id == 501u; }),
            "Disabling water FX promoted semantic water to a contact receiver");
        const auto& no_water_deck = material_for(no_water_frame, instances[6].owner_id).surface.anchor_export;
        require(std::abs(no_water_deck[2] - .08f) < 1.e-5f, "Disabling water FX removed physical completed-deck feet");
        std::size_t no_water_triangles{};
        for (const auto& command : no_water_frame.commands) {
            const auto* draw = std::get_if<GeometryDraw>(&command);
            if (!draw || draw->pass != Pass::Terrain || !draw->ground_material_index) continue;
            bool queried_water = true;
            for (std::uint32_t offset = 0; offset < draw->index_count; ++offset) {
                const auto& position = no_water_frame.surface_vertices.at(
                    no_water_frame.indices.at(draw->first_index + offset)).world_position;
                queried_water = queried_water && std::abs(position[0]) <= .5f && std::abs(position[1]) <= .5f;
            }
            if (!queried_water) continue;
            require(no_water_frame.ground_material_draws.at(draw->ground_material_index - 1u).contact_receiver ==
                    ContactReceiver::Ordinary, "FX-disabled water GBuffer geometry remains contact eligible");
            no_water_triangles += draw->index_count / 3u;
        }
        require(no_water_triangles == 2u, "FX-disabled semantic water geometry check is vacuous");
        // Composition only: WaterAtmosphereSlice itself requires a water payload.
        // Do not relabel this as a captured FX-disabled renderer frame.
        const Json no_water_check{{"scope", "production GameView CPU composition only"},
            {"ordinary_water_triangles", no_water_triangles}, {"skipped_contact_receivers", 1},
            {"water_fx_disabled_contact_filter", true}, {"physical_bridge_decks", 3},
            {"physical_deck_contacts", category_metadata(metadata, categories[6]).at("contact_shadows").size()}};

        ClientSession steep_session(70421);
        const bool steep_loaded = steep_session.load(output / "steep-world-current.save", error);
        require(steep_loaded, "Steep diagnostic ClientSession load failed: " + error);
        require(!steep_session.last_load().copied, "Steep current-schema diagnostic load unexpectedly copied/migrated the save");
        auto steep_settings = settings; steep_settings.camera_x = 36.f; steep_settings.camera_y = 17.f;
        steep_settings.cell_radius = 2;
        const std::array<EngineControlInstance, 1> steep_instances{{
            {"control.resident.empty.sprite", 36.f, 17.f, 1.f, 601u, 19001u, 0.f, false, 0u, 20001u}}};
        Frame2D steep_frame;
        const auto steep_stats = view.compose_engine_control(steep_session, shown_seconds,
            steep_settings, steep_instances, steep_frame);
        require(steep_stats.instances_drawn == 1u && steep_stats.contact_receivers_skipped == 1u &&
            steep_stats.contact_shadows == 0u && steep_frame.contact_shadows.empty(),
            "Unsupported steep receiver did not skip contacts without losing the visible instance");
        std::size_t steep_triangles{};
        for (const auto& command : steep_frame.commands) {
            const auto* draw = std::get_if<GeometryDraw>(&command);
            if (!draw || draw->pass != Pass::Terrain || !draw->ground_material_index) continue;
            for (std::uint32_t offset = 0; offset < draw->index_count; offset += 3u) {
                const auto& vertex = steep_frame.surface_vertices.at(steep_frame.indices.at(draw->first_index + offset));
                if (vertex.normal[2] >= contact_shadow_min_upward_normal) continue;
                require(steep_frame.ground_material_draws.at(draw->ground_material_index - 1u).contact_receiver ==
                        ContactReceiver::Ordinary, "Unsupported steep triangle remained contact eligible");
                ++steep_triangles;
            }
        }
        require(steep_triangles > 0u, "Unsupported steep geometry control is vacuous");
        ui_exact(base_image, cumulative_capture(renderer, steep_frame, output, "cumulative-steep-nonreceiver", captures));
        auto disabled_settings = settings; disabled_settings.contact_enabled = false;
        Frame2D disabled;
        const auto disabled_stats = view.compose_engine_control(session, shown_seconds, disabled_settings, instances, disabled);
        require(disabled.contact_shadows.empty() && disabled_stats.contact_shadows == 0u,
                "Disabled contact adapter appended rows");
        (void)checked(disabled, "cumulative-contact-adapter-disabled");

        Json camera_cases = Json::array();
        constexpr std::array<std::array<float, 3>, 3> camera_matrix{{
            {28.f, 25.f, .62f}, {27.5f, 25.5f, 1.3f}, {28.25f, 25.f, 2.f}}};
        for (std::size_t index = 0; index < camera_matrix.size(); ++index) {
            auto camera = settings; camera.camera_x = camera_matrix[index][0];
            camera.camera_y = camera_matrix[index][1]; camera.zoom = camera_matrix[index][2];
            Frame2D frame;
            const auto camera_stats = view.compose_engine_control(session, shown_seconds, camera, instances, frame);
            require(camera_stats.instances_drawn == 8u && camera_stats.contact_shadows == expected_contacts &&
                    camera_stats.bridge_decks == 3u, "Camera control lost a category, deck or contact");
            std::size_t picks{};
            for (const auto& instance : instances) {
                const auto& anchor = material_for(frame, instance.owner_id).surface.anchor_export;
                const auto projected = contact_project_logical({anchor[0], anchor[1], anchor[2]}, *frame.contact_projection);
                require(view.cell_at_screen(projected[0], projected[1]) ==
                    CellCoord{static_cast<int>(instance.x), static_cast<int>(instance.y), WorldLayer::Surface},
                    "Camera control broke shared feet/deck picking");
                ++picks;
            }
            const auto image = checked(frame, "cumulative-camera-" + std::to_string(index), false);
            require(changed_pixels(base_image, image) > 100u, "Pan/zoom matrix contains a vacuous case");
            camera_cases.push_back({{"camera", {camera.camera_x, camera.camera_y}}, {"zoom", camera.zoom},
                {"picked_feet", picks}, {"contacts", camera_stats.contact_shadows}});
        }

        Frame2D published; (void)view.compose_engine_control(session, shown_seconds, settings, instances, published);
        const auto published_fingerprint = frame_fingerprint(published);
        const auto old_screen = view.tile_screen(25.f, 24.f);
        const auto old_pick = view.cell_at_screen(old_screen.x, old_screen.y);
        Json rejections = Json::array();
        const auto reject = [&](std::string_view label, std::vector<EngineControlInstance> bad) {
            auto attempted = settings; attempted.camera_x += .25f; attempted.camera_y -= .125f;
            bool rejected{};
            try { (void)view.compose_engine_control(session, shown_seconds, attempted, bad, published); }
            catch (const std::invalid_argument&) { rejected = true; }
            const auto screen = view.tile_screen(25.f, 24.f);
            require(rejected && frame_fingerprint(published) == published_fingerprint &&
                screen.x == old_screen.x && screen.y == old_screen.y &&
                view.cell_at_screen(old_screen.x, old_screen.y) == old_pick,
                "Rejected contact identity published partial frame, camera or picks");
            rejections.push_back({{"id", label}, {"frame_camera_pick_exact", true}});
        };
        auto duplicate = instances; duplicate[1].first_contact_id = duplicate[0].first_contact_id;
        reject("duplicate-contact-stable-id", duplicate);
        auto overflow = instances; overflow[6].first_contact_id = std::numeric_limits<std::uint32_t>::max();
        reject("cargo-contact-stable-id-overflow", overflow);
        auto zero = instances; zero[0].first_contact_id = 0u; reject("zero-contact-stable-id", zero);
        require(checked(published, "cumulative-after-invalid").rgba == base_image.rgba,
                "Rejected contact controls changed the recovered native frame");
        save_report(output / "cumulative-native-result.json", {{"complete", true},
            {"scope", "bounded production GameView neutral-eight checkpoint, not full engine or reference acceptance"},
            {"backend", renderer.device_info().backend}, {"gpu", renderer.device_info().device},
            {"driver", renderer.device_info().driver}, {"graph", "WaterAtmosphereSlice with optional GroundContact"},
            {"resolution", {1920, 1080}}, {"captures", captures}, {"categories", category_checks},
            {"instances", stats.instances_drawn}, {"contacts", stats.contact_shadows},
            {"contact_land_changed_pixels", contact_land}, {"contact_deck_changed_pixels", contact_deck},
            {"direct_only_contact_exact", true}, {"sun_shadow_land_pixels", sun_shadow_pixels},
            {"moon_shadow_land_pixels", moon_shadow_pixels}, {"authored_local_land_pixels", authored_local_pixels},
            {"warm_emission_pixels", warm_pixels}, {"reflection_changed_pixels", reflection_pixels},
            {"raw_carrier", counts}, {"light_contact_carrier_exact", true}, {"diagnostic_ui_exact", true},
            {"paused_fixed_time_exact", true}, {"hidden_frame_pixel_exact", true}, {"water_contact_filtered", true},
            {"camera_cases", camera_cases}, {"transactional_rejections", rejections},
            {"cpp_loader_checks", startup.loader}, {"cpp_adapter_checks", startup.adapter},
            {"typed_registry_rejections", registry_rejections}, {"water_fx_disabled_composition", no_water_check},
            {"unsupported_steep_triangles", steep_triangles}, {"steep_contacts_skipped", true},
            {"steep_fixture_scope", "independent zero-population current-schema diagnostic world with a supplied resident control"},
            {"public_deck_feet_exact", true},
            {"targets", stable_targets(renderer.ground_forward_status())}, {"fps", nullptr},
            {"gpu_execution_ms", nullptr}, {"full_engine", "OPEN"}, {"reference_fidelity", "NOT_ACCEPTED"},
            {"limitations", {"Diagnostic current-schema terrain and neutral controls, not a natural developed settlement",
                "Category lighting samples representative authored faces, not every texel or all production assets",
                "UI is an opaque diagnostic marker; native RmlUi/DPI/input acceptance remains open",
                "Fixed shown time and three bounded cameras; no continuous motion/video/weather authority",
                "No performance/peak-VRAM/AA acceptance; transactional checks do not inspect private caches"}}});
        check_budget(output); std::cout << "Cumulative neutral-eight native checks passed\n"; return 0;
    } catch (const std::exception& failure) {
        if (owned) try { save_report(output / "cumulative-native-result.json", {{"complete", false},
            {"error", failure.what()}, {"captures", captures}, {"categories", category_checks},
            {"full_engine", "OPEN"}}); } catch (...) {}
        std::cerr << "Cumulative probe failed: " << failure.what() << '\n'; return 1;
    }
}

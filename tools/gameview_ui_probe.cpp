#include "engine_control_probe.hpp"
#include "ribterra/game_ui.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/localization.hpp"
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <set>

namespace {
constexpr std::size_t capture_limit = 15u;
constexpr std::string_view world_save = "ui-world-current.save";
constexpr Color background{20u,30u,40u,255u};

Json draw_row(const GeometryDraw& draw) {
    return {static_cast<int>(draw.pass),static_cast<int>(draw.blend),draw.texture.slot,
        draw.texture.generation,draw.first_index,draw.index_count,draw.scissor.x,draw.scissor.y,
        draw.scissor.w,draw.scissor.h,draw.scissor_enabled,draw.surface_lit,
        draw.sprite_material_index,draw.ground_material_index};
}

// Complete semantic comparison for these contact-free fixtures, including the
// new CPU content field. No wrapper padding or native pointers participate.
Json frame_state(const Frame2D& frame) {
    Json result{{"content",static_cast<int>(frame.content)},
        {"size",{frame.logical_size.width,frame.logical_size.height}},
        {"clear",{frame.clear.r,frame.clear.g,frame.clear.b,frame.clear.a}},
        {"indices",frame.indices},{"hdr",frame.linear_hdr_world},{"exposure",frame.world_exposure},
        {"water_normal",{frame.water_normal.slot,frame.water_normal.generation}},
        {"vertices",Json::array()},{"surfaces",Json::array()},{"commands",Json::array()},
        {"sprites",Json::array()},{"ground",Json::array()},{"occluders",Json::array()},
        {"sky",Json::array()},{"reflections",Json::array()},
        {"lighting",{frame.surface_lighting.ambient,frame.surface_lighting.sun_direction,
            frame.surface_lighting.sun_color_intensity,frame.surface_lighting.counts}},
        {"lights",Json::array()}};
    require(frame.contact_shadows.empty(),"UI fixture unexpectedly contains contact shadows");
    result["contact_projection"] = frame.contact_projection
        ? Json{frame.contact_projection->origin_xy,frame.contact_projection->zoom} : Json(nullptr);
    for (const auto& vertex : frame.vertices)
        result["vertices"].push_back({vertex.position.x,vertex.position.y,vertex.uv.x,vertex.uv.y,
            vertex.color.r,vertex.color.g,vertex.color.b,vertex.color.a});
    for (const auto& vertex : frame.surface_vertices)
        result["surfaces"].push_back({vertex.world_position,vertex.normal});
    for (const auto& command : frame.commands) {
        if (const auto* draw = std::get_if<GeometryDraw>(&command))
            result["commands"].push_back({{"geometry",draw_row(*draw)}});
        else {
            const auto& text = std::get<TextDraw>(command);
            result["commands"].push_back({{"text",{static_cast<int>(text.pass),text.text,
                text.position.x,text.position.y,text.size,text.color.r,text.color.g,
                text.color.b,text.color.a,text.max_width}}});
        }
    }
    for (const auto& light : frame.surface_lighting.lights)
        result["lights"].push_back({light.anchor_radius,light.direction_cone,light.color_intensity,
            light.occluder_range_flags,light.height_falloff_reveal});
    for (const auto& draw : frame.sprite_material_draws)
        result["sprites"].push_back({draw.material.slot,draw.material.generation,
            draw.surface.uv_regions,draw.surface.anchor_export,draw.surface.source_size_pivot,
            draw.surface.depth_scale,draw.surface.emission,draw.surface.response,draw.reveal,
            draw.receiver_mask,draw.receiver_owner,draw.emission_scale});
    for (const auto& draw : frame.ground_material_draws)
        result["ground"].push_back({draw.albedo.slot,draw.albedo.generation,draw.surface.response,
            draw.surface.flags,draw.surface.visibility,static_cast<int>(draw.contact_receiver),
            static_cast<int>(draw.water_surface_class),draw.shore_edges,draw.geometric_coverage});
    for (const auto& draw : frame.local_occluders)
        result["occluders"].push_back({draw.center_base,draw.axis_x_extent,draw.axis_y_radius,draw.shape_flags_id});
    for (const auto& draw : frame.sky_shadow_triangles)
        result["sky"].push_back({draw.vertices[0].position,draw.vertices[1].position,
            draw.vertices[2].position,draw.source_mask,draw.reveal});
    for (const auto& draw : frame.reflection_draws)
        result["reflections"].push_back({draw_row(draw.draw),draw.strength,draw.fade});
    result["moon"] = frame.moon_lighting
        ? Json{frame.moon_lighting->direction,frame.moon_lighting->color_intensity} : Json(nullptr);
    if (frame.water_surface) {
        const auto& water = *frame.water_surface;
        result["water"] = {water.world_origin_time,water.flow_response,water.tint,water.viewport};
    } else result["water"] = nullptr;
    if (frame.atmosphere) {
        const auto& fog = *frame.atmosphere;
        result["atmosphere"] = {fog.origin_time,fog.depth,fog.density,fog.far_color_noise,
            fog.morning_color,fog.coastal_color,fog.wind_shore,fog.counts_version,fog.water_aabbs};
    } else result["atmosphere"] = nullptr;
    return result;
}

Json target_state(const Renderer2D& renderer) {
    const auto& ground = renderer.ground_forward_status();
    auto result = stable_targets(ground);
    result["creation_attempts"] = ground.creation_attempts;
    result["candidate_live_textures"] = ground.candidate_live_textures;
    const auto& sky = renderer.sky_shadow_status();
    result["sky"] = {sky.available,sky.d32_supported,sky.map_size,sky.frame_sets,
        sky.candidate_live_textures,sky.attachment_bytes,sky.creation_attempts,
        sky.target_generation,sky.active_set_ids};
    const auto& hdr = renderer.world_hdr_status();
    result["hdr"] = {hdr.available,hdr.width,hdr.height,hdr.frame_sets,hdr.attachment_bytes};
    const auto& contact = renderer.contact_art_status();
    result["contact"] = {contact.live_arts,contact.logical_bytes,contact.creation_attempts};
    return result;
}

Json telemetry_row(const RendererFrameTelemetry& value) {
    Json passes = Json::array();
    for (std::uint32_t index = 0; index < value.graph_pass_count; ++index)
        passes.push_back(static_cast<int>(value.graph_passes[index]));
    return {{"submitted",value.submitted},{"capture",value.capture},
        {"vertices",value.vertices},{"indices",value.indices},{"draws",value.draws},
        {"surface_draws",value.surface_draws},{"ground_draws",value.ground_draws},
        {"material_draws",value.material_draws},{"occluders",value.occluders},
        {"occluder_upload_bytes",value.occluder_upload_bytes},{"contact_casters",value.contact_casters},
        {"contact_draws",value.contact_draws},{"contact_mask_binds",value.contact_mask_binds},
        {"water_draws",value.water_draws},{"reflection_draws",value.reflection_draws},
        {"upload_bytes",value.upload_bytes},{"frame_slot",value.frame_slot},
        {"graph_pass_count",value.graph_pass_count},{"graph_passes",std::move(passes)},
        {"gpu_execution_ms",nullptr}};
}

void check_ui_telemetry(const RendererFrameTelemetry& value) {
    require(value.measured && value.submitted && value.graph_pass_count == 1u &&
        value.graph_passes[0] == WorldGraphPass::Ui && !value.gpu_execution_ms &&
        value.surface_draws == 0u && value.ground_draws == 0u && value.material_draws == 0u &&
        value.occluders == 0u && value.occluder_upload_bytes == 0u &&
        value.contact_casters == 0u && value.contact_draws == 0u && value.contact_mask_binds == 0u &&
        value.water_draws == 0u && value.reflection_draws == 0u,
        "UiOnly executed or reported a world pass/upload/draw");
}

void check_world_telemetry(const RendererFrameTelemetry& value) {
    constexpr std::array<WorldGraphPass,8> order{WorldGraphPass::SunShadow,WorldGraphPass::MoonShadow,
        WorldGraphPass::Reflection,WorldGraphPass::GroundGBuffer,WorldGraphPass::GroundLighting,
        WorldGraphPass::WorldForward,WorldGraphPass::Composite,WorldGraphPass::Ui};
    require(value.measured && value.submitted && value.graph_pass_count == order.size() &&
        std::equal(order.begin(),order.end(),value.graph_passes.begin()) &&
        value.water_draws > 0u && value.reflection_draws == 2u &&
        value.contact_casters == 0u && value.contact_draws == 0u && !value.gpu_execution_ms,
        "Typed World did not retain the eight real contact-free passes");
}

Image read_png(const fs::path& path, Extent expected) {
    png_image raw{}; raw.version = PNG_IMAGE_VERSION;
    struct Free { png_image* value; ~Free() { png_image_free(value); } } free{&raw};
    const auto name = path.u8string();
    require(png_image_begin_read_from_file(&raw,reinterpret_cast<const char*>(name.c_str())) != 0,
        "UI PNG read failed");
    require(raw.width == expected.width && raw.height == expected.height,"UI PNG extent differs");
    raw.format = PNG_FORMAT_RGBA;
    Image result{std::vector<std::uint8_t>(PNG_IMAGE_SIZE(raw))};
    require(png_image_finish_read(&raw,nullptr,result.rgba.data(),0,nullptr) != 0,"UI PNG decode failed");
    for (std::size_t offset = 3; offset < result.rgba.size(); offset += 4u)
        require(result.rgba[offset] == 255u,"UI final alpha is not opaque");
    return result;
}

Image capture_frame(Renderer2D& renderer, const Frame2D& frame, const fs::path& output,
                    std::string_view name, Json& rows, Extent expected = extent) {
    require(rows.size() < capture_limit,"UI capture inventory exceeded");
    const auto path = output / (std::string(name) + ".png");
    require(!fs::exists(path),"Refusing UI capture overwrite");
    SDL_Event event{}; while (SDL_PollEvent(&event)) {}
    renderer.render(frame,path);
    const auto& measured = renderer.frame_telemetry();
    if (frame.content == FrameContent::UiOnly) check_ui_telemetry(measured);
    else check_world_telemetry(measured);
    require(measured.capture,"Actual UI capture telemetry is absent");
    rows.push_back({{"file",path.filename().string()},{"width",expected.width},{"height",expected.height},
        {"bytes",fs::file_size(path)},{"content",static_cast<int>(frame.content)},
        {"telemetry",telemetry_row(measured)}});
    check_budget(output);
    return read_png(path,expected);
}

std::size_t difference(const Image& first, const Image& second) {
    require(first.rgba.size() == second.rgba.size(),"Pixel comparison extents differ");
    std::size_t changed{};
    for (std::size_t offset = 0; offset < first.rgba.size(); offset += 4u)
        if (!std::equal(first.rgba.begin()+static_cast<std::ptrdiff_t>(offset),
                        first.rgba.begin()+static_cast<std::ptrdiff_t>(offset+4u),
                        second.rgba.begin()+static_cast<std::ptrdiff_t>(offset))) ++changed;
    return changed;
}

std::array<std::uint8_t,4> pixel(const Image& image, std::uint32_t x, std::uint32_t y) {
    const auto offset = (std::size_t(y)*extent.width+x)*4u;
    require(offset+4u <= image.rgba.size(),"UI sample is outside its image");
    return {image.rgba[offset],image.rgba[offset+1u],image.rgba[offset+2u],image.rgba[offset+3u]};
}

Frame2D fresh_ui() {
    Frame2D frame; frame.logical_size = extent; frame.clear = background; frame.content = FrameContent::UiOnly;
    return frame;
}

Frame2D direct_ui(Renderer2D& renderer) {
    auto frame = fresh_ui();
    const std::array<std::uint8_t,16> texels{64u,32u,16u,128u,64u,32u,16u,128u,
        64u,32u,16u,128u,64u,32u,16u,128u};
    const auto texture = renderer.create_texture({2u,2u},texels);
    frame.vertices = {{{100.f,100.f},{0.f,0.f},{255u,255u,255u,255u}},
        {{300.f,100.f},{1.f,0.f},{255u,255u,255u,255u}},
        {{300.f,220.f},{1.f,1.f},{255u,255u,255u,255u}},
        {{100.f,220.f},{0.f,1.f},{255u,255u,255u,255u}}};
    frame.indices = {0u,1u,2u,0u,2u,3u};
    GeometryDraw draw; draw.pass = Pass::Ui; draw.blend = Blend::PremultipliedAlpha;
    draw.texture = texture; draw.index_count = 6u; draw.scissor = {140.f,130.f,120.f,60.f};
    draw.scissor_enabled = true; frame.commands.emplace_back(draw);
    TextDraw text; text.text = "Ribterra UI 123"; text.position = {400.f,80.f}; text.size = 26.f;
    text.color = {230u,220u,200u,255u}; frame.commands.emplace_back(std::move(text));
    return frame;
}

Json rml_counts(const Frame2D& frame, std::size_t first_command = 0u) {
    std::size_t textured{},premultiplied{},draws{};
    std::set<std::pair<std::uint32_t,std::uint32_t>> textures;
    for (std::size_t index = first_command; index < frame.commands.size(); ++index) {
        const auto* draw = std::get_if<GeometryDraw>(&frame.commands[index]);
        require(draw && draw->pass == Pass::Ui,"RmlUi emitted a non-Ui geometry command");
        require(!draw->surface_lit && !draw->sprite_material_index && !draw->ground_material_index,
            "RmlUi emitted world lighting/material indices");
        ++draws;
        if (draw->texture.slot) { ++textured; textures.emplace(draw->texture.slot,draw->texture.generation); }
        if (draw->blend == Blend::PremultipliedAlpha) ++premultiplied;
    }
    require(draws > 0u && textured > 0u && textures.size() >= 2u && premultiplied > 0u,
        "Actual RmlUi font/image geometry is vacuous");
    return {{"vertices",frame.vertices.size()},{"indices",frame.indices.size()},
        {"commands",frame.commands.size()},{"surface_vertices",frame.surface_vertices.size()},
        {"ui_draws",draws},{"textured_draws",textured},{"distinct_textures",textures.size()},
        {"premultiplied_draws",premultiplied}};
}

void check_prefix(const Frame2D& original, const Frame2D& composed) {
    require(composed.vertices.size() > original.vertices.size() &&
        composed.indices.size() > original.indices.size() && composed.commands.size() > original.commands.size() &&
        composed.surface_vertices.size() == composed.vertices.size(),"Typed HUD append is vacuous or misaligned");
    auto prefix = composed;
    prefix.vertices.resize(original.vertices.size()); prefix.indices.resize(original.indices.size());
    prefix.commands.resize(original.commands.size()); prefix.surface_vertices.resize(original.surface_vertices.size());
    require(frame_state(prefix) == frame_state(original),"RmlUi changed the typed World prefix or metadata");
    const SurfaceVertex neutral{};
    for (std::size_t index = original.surface_vertices.size(); index < composed.surface_vertices.size(); ++index)
        require(composed.surface_vertices[index].world_position == neutral.world_position &&
            composed.surface_vertices[index].normal == neutral.normal,"HUD companion suffix is not neutral");
    for (std::size_t index = original.indices.size(); index < composed.indices.size(); ++index)
        require(composed.indices[index] >= original.vertices.size() &&
            composed.indices[index] < composed.vertices.size(),"HUD references or overwrites a World prefix vertex");
}

std::vector<std::uint8_t> file_bytes(const fs::path& path) {
    require(fs::file_size(path) > 0u && fs::file_size(path) < budget,"Fixture preservation size is invalid");
    std::ifstream file(path,std::ios::binary); require(file.good(),"Fixture preservation read failed");
    std::vector<std::uint8_t> result{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    require(!file.bad() && result.size() == fs::file_size(path),"Fixture preservation read incomplete");
    return result;
}

constexpr std::string_view query_id = "query.typed.sprite";
constexpr std::string_view legacy_query_id = "building.query.reference";
constexpr std::array<std::string_view,5> query_roots{"typed-query-fixture","typed-query-invalid-size",
    "typed-query-invalid-rect","typed-query-invalid-hash","typed-query-invalid-budget"};
constexpr std::array<std::string_view,10> query_files{"art/material/albedo.rtex",
    "art/material/normal-material.rtex","art/material/camera-depth.rtex","art/material/emission-mask.rtex",
    "art/material/surface-mask.rtex","art/material/caster-mask.rtex","source.png","art/world.json",
    "manifest.json","NOTICE.txt"};
constexpr std::array<std::string_view,6> query_channels{"albedo","normal_material","camera_depth",
    "emission_mask","surface_mask","caster_mask"};
constexpr std::array<std::string_view,6> query_formats{"rgba8_unorm_srgb","rgba8_unorm","r16_unorm",
    "r8_unorm","rgba8_unorm","r8_unorm"};
constexpr std::array<unsigned,6> query_bpp{4u,4u,2u,1u,4u,1u};
constexpr Point query_ground{640.f,400.f};
constexpr std::array<std::array<float,2>,3> query_transforms{{{1.f,1.f},{.625f,1.5f},{1.75f,.8f}}};

void save_text(const fs::path& path, std::string_view text) {
    save_bytes(path,std::vector<std::uint8_t>(text.begin(),text.end()));
}

// Tiny retained diagnostic source, independent of the implementation's cached
// masks. The consumer derives coverage only through public GameArt queries.
Json make_query_catalog(const fs::path& output, std::size_t variant) {
    require(variant < query_roots.size(),"Unknown typed-query source variant");
    const auto root = output/query_roots[variant];
    require(!fs::exists(root) && fs::create_directories(root/"art/material"),"Query root is not fresh");
    constexpr std::uint32_t width = 205u,height = 149u;
    constexpr std::array<int,4> crop{5,7,193,129};
    std::array<std::vector<std::uint8_t>,6> raw;
    for (std::size_t channel = 0; channel < raw.size(); ++channel)
        raw[channel].resize(std::size_t(width)*height*query_bpp[channel]);
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
        std::uint8_t alpha = 255u;
        if (x >= 5u && x < 198u && y >= 7u && y < 136u) {
            const auto local_x = x-5u,local_y = y-7u;
            alpha = static_cast<std::uint8_t>(local_x < 48u ? 0u : local_x < 96u ? 63u : local_x < 144u ? 64u : 255u);
            if (local_x >= 112u && local_x < 132u && local_y >= 48u && local_y < 80u) alpha = 0u;
            // The last pooled column spans source x191 and x192. Its first
            // source alpha is zero and its second is exactly the threshold.
            if (local_x == 191u) alpha = 0u;
            if (local_x == 192u) alpha = 64u;
        }
        const auto texel = std::size_t(y)*width+x;
        const auto rgba = texel*4u;
        raw[0][rgba] = 170u; raw[0][rgba+1u] = 130u; raw[0][rgba+2u] = 90u; raw[0][rgba+3u] = alpha;
        raw[1][rgba] = 128u; raw[1][rgba+1u] = 128u; raw[1][rgba+2u] = 255u; raw[1][rgba+3u] = 255u;
        raw[2][texel*2u] = 0u; raw[2][texel*2u+1u] = 128u;
        raw[4][rgba+3u] = 255u;
        raw[5][texel] = static_cast<std::uint8_t>(alpha >= 64u ? 255u : 0u);
    }
    Json doc{{"schema",2},{"pages",Json::array()},{"textures",Json::array()},
        {"materials",Json::array()},{"lit_sprites",Json::array()}};
    Json bindings = Json::object();
    for (std::size_t channel = 0; channel < raw.size(); ++channel) {
        const auto id = "query.texture."+std::string(query_channels[channel]);
        save_bytes(root/query_files[channel],raw[channel]);
        doc["textures"].push_back({{"id",id},{"path",query_files[channel]},{"format",query_formats[channel]},
            {"width",width},{"height",height},{"layout","tight_le"},{"mip_levels",1},
            {"bytes",raw[channel].size()},{"sha256",sha256(raw[channel])}});
        bindings[std::string(query_channels[channel])] = {{"texture",id},{"rect",crop}};
    }
    png_image png{}; png.version = PNG_IMAGE_VERSION; png.width = width; png.height = height; png.format = PNG_FORMAT_RGBA;
    struct Free { png_image* value; ~Free() { png_image_free(value); } } free{&png};
    const auto png_name = (root/"source.png").u8string();
    require(!fs::exists(root/"source.png") && png_image_write_to_file(&png,
        reinterpret_cast<const char*>(png_name.c_str()),0,raw[0].data(),0,nullptr) != 0,"Query PNG write failed");
    doc["pages"].push_back({{"path","source.png"},{"sprites",Json::array({{
        {"id",legacy_query_id},{"rect",crop},{"pivot",{47.25f/193.f,118.5f/129.f}},{"reference_width",193.f}}})}});
    doc["materials"].push_back({{"id","query.typed.material"},{"family","DynamicSprite25D"},
        {"alpha","straight"},{"camera","ribterra-affine-v1"},{"revision",1},
        {"source_hash",sha256(raw[0])},{"channels",std::move(bindings)},{"emission",{0.f,0.f,0.f,0.f}}});
    const Json sprite{{"id",query_id},{"material","query.typed.material"},{"source_size",{193.f,129.f}},
        {"pivot_px",{47.25f,118.5f}},{"depth_range",{-1.f,1.f}},{"export_pixels_per_reference_pixel",2.25f}};
    doc["lit_sprites"].push_back(sprite);
    if (variant == 1u) doc["lit_sprites"][0]["source_size"][0] = 0.f;
    if (variant == 2u) doc["materials"][0]["channels"]["albedo"]["rect"][0] = 5.5;
    if (variant == 3u) doc["textures"][0]["sha256"] = std::string(64u,'0');
    if (variant == 4u) for (std::size_t index = 1u; index < 684u; ++index) {
        auto extra = sprite; extra["id"] = std::string(query_id)+"."+std::to_string(index);
        doc["lit_sprites"].push_back(std::move(extra));
    }
    save_report(root/"art/world.json",doc);
    save_text(root/"NOTICE.txt","Ribterra diagnostic typed-query fixture. Authored integer coverage; no production/reference claim.\n");
    Json records = Json::array();
    for (std::size_t index = 0; index < 8u; ++index) {
        const auto path = std::string(query_files[index]);
        const auto bytes = file_bytes(root/path);
        records.push_back({{"id","query.asset."+std::to_string(index)},{"path",path},
            {"kind",index == 6u ? "texture" : "data"},{"sha256",sha256(bytes)},{"bytes",bytes.size()},
            {"license","CC0-1.0"},{"source","Ribterra diagnostic fixture"},{"author","Ribterra"},
            {"notice","NOTICE.txt"}});
    }
    std::string canonical;
    constexpr std::array<std::string_view,8> fields{"id","path","kind","sha256","license","source","author","notice"};
    for (const auto& record : records) {
        for (const auto field : fields) {
            const auto value = record.at(std::string(field)).get<std::string>();
            canonical += std::to_string(value.size())+":"+value;
        }
        canonical += '\n';
    }
    const auto content_hash = sha256(std::span(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
    save_report(root/"manifest.json",Json{{"schema",1},{"content_sha256",content_hash},{"assets",std::move(records)}});
    Json files = Json::array();
    for (const auto path : query_files) {
        const auto bytes = file_bytes(root/path);
        files.push_back({{"path",path},{"bytes",bytes.size()},{"sha256",sha256(bytes)}});
    }
    return {{"root",query_roots[variant]},{"variant",variant},{"manifest_content_sha256",content_hash},{"files",std::move(files)}};
}

std::array<float,4> rect_row(Rect value) { return {value.x,value.y,value.w,value.h}; }

std::vector<std::uint8_t> query_coverage(const GameArt& art, std::string_view id, Point ground,
                                      float zoom, float scale, Extent mask, Rect bounds) {
    std::vector<std::uint8_t> result(std::size_t(mask.width)*mask.height);
    for (std::uint32_t y = 0; y < mask.height; ++y) for (std::uint32_t x = 0; x < mask.width; ++x) {
        const Point point{bounds.x+(float(x)+.5f)*bounds.w/float(mask.width),
            bounds.y+(float(y)+.5f)*bounds.h/float(mask.height)};
        result[std::size_t(y)*mask.width+x] = static_cast<std::uint8_t>(art.hit_test_lit_sprite(id,ground,zoom,point,scale) ? 255u : 0u);
    }
    return result;
}

Json query_row(const GameArt& art, const AssetCatalog& catalog, const Json& doc, const Json& source) {
    const auto id = source.at("id").get<std::string>();
    const auto material_id = source.at("material").get<std::string>();
    const auto material = std::find_if(doc.at("materials").begin(),doc.at("materials").end(),
        [&](const auto& row){ return row.at("id") == material_id; });
    require(material != doc.at("materials").end(),"Query source material is missing");
    const auto& binding = material->at("channels").at("albedo");
    const auto texture_id = binding.at("texture").get<std::string>();
    const auto texture = std::find_if(doc.at("textures").begin(),doc.at("textures").end(),
        [&](const auto& row){ return row.at("id") == texture_id; });
    require(texture != doc.at("textures").end(),"Query source albedo is missing");
    const auto crop = binding.at("rect").get<std::array<int,4>>();
    const auto size = source.at("source_size").get<std::array<float,2>>();
    const auto pivot = source.at("pivot_px").get<std::array<float,2>>();
    const float export_scale = source.at("export_pixels_per_reference_pixel").get<float>();
    const float reduction = std::min(1.f,192.f/float(std::max(crop[2],crop[3])));
    const Extent mask{static_cast<std::uint32_t>(std::max(1L,std::lround(float(crop[2])*reduction))),
        static_cast<std::uint32_t>(std::max(1L,std::lround(float(crop[3])*reduction)))};
    const auto albedo_path = texture->at("path").get<std::string>();
    const auto source_bytes = catalog.read_path(albedo_path);
    Json result{{"id",id},{"crop",crop},{"atlas_size",{texture->at("width"),texture->at("height")}},
        {"source_size",size},{"pivot_px",pivot},{"export_scale",export_scale},{"mask_size",{mask.width,mask.height}},
        {"albedo_path",albedo_path},{"albedo_sha256",sha256(source_bytes)},{"transforms",Json::array()}};
    std::string original_hash;
    for (const auto transform : query_transforms) {
        const auto zoom = transform[0],scale = transform[1];
        const auto bounds = art.lit_sprite_bounds(id,query_ground,zoom,scale);
        const float k = zoom*scale/export_scale;
        require(rect_row(bounds) == std::array<float,4>{query_ground.x-pivot[0]*k,
            query_ground.y-pivot[1]*k,size[0]*k,size[1]*k},"Public typed bounds differ from source pivot/scale");
        Frame2D emitted; emitted.linear_hdr_world = true;
        art.lit_sprite(emitted,id,query_ground,{0.f,0.f,0.f},zoom,scale);
        require(emitted.vertices.size() == 4u && emitted.indices.size() == 6u && emitted.commands.size() == 1u &&
            emitted.sprite_material_draws.size() == 1u &&
            emitted.vertices[0].position.x == bounds.x && emitted.vertices[0].position.y == bounds.y &&
            emitted.vertices[1].position.x == bounds.x+bounds.w && emitted.vertices[1].position.y == bounds.y &&
            emitted.vertices[2].position.x == bounds.x+bounds.w && emitted.vertices[2].position.y == bounds.y+bounds.h &&
            emitted.vertices[3].position.x == bounds.x && emitted.vertices[3].position.y == bounds.y+bounds.h,
            "Emitted typed quad differs from explicit bounds");
        const auto coverage = query_coverage(art,id,query_ground,zoom,scale,mask,bounds);
        const auto hash = sha256(coverage);
        const auto covered = static_cast<std::size_t>(std::count(coverage.begin(),coverage.end(),std::uint8_t{255u}));
        require(covered > 0u && covered < coverage.size(),"Typed query coverage is vacuous");
        if (original_hash.empty()) {
            original_hash = hash; result["coverage_sha256"] = hash; result["covered"] = covered;
            result["clear"] = coverage.size()-covered;
        } else require(hash == original_hash,"Typed picking changes under zoom/scale");
        const bool right = !art.hit_test_lit_sprite(id,query_ground,zoom,{bounds.x+bounds.w,bounds.y+bounds.h*.5f},scale);
        const bool bottom = !art.hit_test_lit_sprite(id,query_ground,zoom,{bounds.x+bounds.w*.5f,bounds.y+bounds.h},scale);
        const bool left = !art.hit_test_lit_sprite(id,query_ground,zoom,
            {std::nextafter(bounds.x,-std::numeric_limits<float>::infinity()),bounds.y+bounds.h*.5f},scale);
        const bool top = !art.hit_test_lit_sprite(id,query_ground,zoom,
            {bounds.x+bounds.w*.5f,std::nextafter(bounds.y,-std::numeric_limits<float>::infinity())},scale);
        require(right && bottom && left && top,"Typed picking includes a half-open/outside edge");
        result["transforms"].push_back({{"ground",{query_ground.x,query_ground.y}},{"zoom",zoom},{"scale",scale},
            {"bounds",rect_row(bounds)},{"emitted_exact",true},{"coverage_sha256",hash},
            {"covered",covered},{"clear",coverage.size()-covered}});
    }
    result["halfopen_checks"] = {{"right_rejected",true},{"bottom_rejected",true},
        {"left_outside_rejected",true},{"top_outside_rejected",true}};
    return result;
}

Json query_cache(const GameArt& art) {
    return {{"ready",art.ready()},{"hit_mask_count",art.hit_mask_count()},{"hit_mask_bytes",art.hit_mask_bytes()},
        {"outline_count",art.outline_sprite_count()},{"outline_bytes",art.outline_texture_bytes()},
        {"legacy_contains",art.contains(legacy_query_id)},{"typed_contains",art.contains_lit(query_id)}};
}

Json legacy_query_checks(const GameArt& art, const Json& row) {
    Json widths = Json::array(),bounds_rows = Json::array();
    std::string original_hash,mirror_hash;
    std::size_t covered{},clear{};
    const Extent mask{192u,128u};
    constexpr std::array<Point,8> outline_directions{{{-1.f,-1.f},{0.f,-1.f},{1.f,-1.f},{1.f,0.f},
        {1.f,1.f},{0.f,1.f},{-1.f,1.f},{-1.f,0.f}}};
    for (std::size_t transform_index = 0; transform_index < query_transforms.size(); ++transform_index) {
        const auto transform = query_transforms[transform_index];
        const float width = 193.f*(transform[0]*transform[1]/2.25f);
        widths.push_back(width);
        const auto bounds = art.sprite_bounds(legacy_query_id,query_ground,width);
        bounds_rows.push_back(rect_row(bounds));
        const auto typed_bounds = row.at("transforms")[transform_index].at("bounds").get<std::array<float,4>>();
        const auto legacy_bounds = rect_row(bounds);
        for (std::size_t axis = 0; axis < legacy_bounds.size(); ++axis)
            require(std::abs(legacy_bounds[axis]-typed_bounds[axis]) <= .001f,"Legacy source pivot/scale differs");
        const auto typed = query_coverage(art,query_id,query_ground,transform[0],transform[1],mask,
            art.lit_sprite_bounds(query_id,query_ground,transform[0],transform[1]));
        std::vector<std::uint8_t> normal(typed.size()),mirrored(typed.size()),expected_mirror(typed.size());
        for (std::uint32_t y = 0; y < mask.height; ++y) for (std::uint32_t x = 0; x < mask.width; ++x) {
            const Point point{bounds.x+(float(x)+.5f)*bounds.w/float(mask.width),
                bounds.y+(float(y)+.5f)*bounds.h/float(mask.height)};
            const auto index = std::size_t(y)*mask.width+x;
            normal[index] = static_cast<std::uint8_t>(art.hit_test_sprite(legacy_query_id,query_ground,width,point) ? 255u : 0u);
            mirrored[index] = static_cast<std::uint8_t>(art.hit_test_sprite(legacy_query_id,query_ground,width,point,true) ? 255u : 0u);
            expected_mirror[index] = typed[std::size_t(y)*mask.width+(mask.width-1u-x)];
        }
        require(normal == typed && mirrored == expected_mirror && mirrored != normal,
            "Legacy alpha/mirror picking differs from typed source coverage");
        if (original_hash.empty()) {
            original_hash = sha256(normal); mirror_hash = sha256(mirrored);
            covered = static_cast<std::size_t>(std::count(normal.begin(),normal.end(),std::uint8_t{255u}));
            clear = normal.size()-covered;
        } else require(sha256(normal) == original_hash && sha256(mirrored) == mirror_hash,
            "Legacy picking changes under scale");
        require(!art.hit_test_sprite(legacy_query_id,query_ground,width,{bounds.x+bounds.w,bounds.y+bounds.h*.5f}) &&
            !art.hit_test_sprite(legacy_query_id,query_ground,width,{bounds.x+bounds.w*.5f,bounds.y+bounds.h}),
            "Legacy half-open picking regressed");
        Frame2D outline; art.outline_sprite(outline,legacy_query_id,query_ground,width,2.f);
        require(outline.vertices.size() == 32u && outline.indices.size() == 48u && outline.commands.size() == 1u &&
            std::get<GeometryDraw>(outline.commands[0]).texture.slot != 0u &&
            std::get<GeometryDraw>(outline.commands[0]).index_count == 48u,
            "Legacy outline is not the existing eight quads");
        for (std::size_t index = 0; index < outline_directions.size(); ++index) {
            const auto& first = outline.vertices[index*4u].position;
            require(std::abs(first.x-(bounds.x+outline_directions[index].x*2.f)) <= .001f &&
                std::abs(first.y-(bounds.y+outline_directions[index].y*2.f)) <= .001f,
                "Legacy cached outline geometry/texture changed");
        }
    }
    return {{"id",legacy_query_id},{"widths",std::move(widths)},{"bounds",std::move(bounds_rows)},
        {"coverage_sha256",original_hash},{"mirrored_sha256",mirror_hash},{"covered",covered},{"clear",clear},
        {"bounds_equivalent",true},{"picking_equivalent",true},{"mirror_equivalent",true},
        {"outline_quads",8},{"outline_geometry",true}};
}

void invalid_query_checks(const GameArt& art, Json& rows) {
    Frame2D retained; retained.logical_size = extent; retained.linear_hdr_world = true;
    art.lit_sprite(retained,query_id,query_ground,{0.f,0.f,0.f},1.f);
    retained.surface_vertices.resize(retained.vertices.size());
    const auto before = frame_state(retained);
    const auto rejected = [&](std::string_view id, const std::vector<std::string>& apis,
                              const std::function<void(std::string_view,Frame2D&)>& invoke, bool unknown = false) {
        std::string errors;
        for (const auto& api : apis) {
            auto candidate = retained; bool failed{}; std::string error;
            try { invoke(api,candidate); }
            catch (const std::out_of_range& exception) { require(unknown,"Query used wrong exception type"); failed = true; error = exception.what(); }
            catch (const std::invalid_argument& exception) { require(!unknown,"Unknown query ID changed exception type"); failed = true; error = exception.what(); }
            require(failed && !error.empty() && frame_state(candidate) == before,"Invalid query/draw changed prior frame or accepted input");
            if (!errors.empty()) errors += " | ";
            errors += api+": "+error;
        }
        rows.push_back({{"id",id},{"rejected",true},{"unchanged",true},{"error",errors},{"apis",apis}});
    };
    const auto projection = [&](std::string_view id, Point ground, float zoom, float scale) {
        rejected(id,{"bounds","hit","draw"},[&](std::string_view api,Frame2D& frame){
            if (api == "bounds") (void)art.lit_sprite_bounds(query_id,ground,zoom,scale);
            else if (api == "hit") (void)art.hit_test_lit_sprite(query_id,ground,zoom,query_ground,scale);
            else art.lit_sprite(frame,query_id,ground,{0.f,0.f,0.f},zoom,scale);
        });
    };
    const auto nan = std::numeric_limits<float>::quiet_NaN(),inf = std::numeric_limits<float>::infinity();
    projection("Q-ground-NaN",{nan,400.f},1.f,1.f);
    projection("Q-ground-infinity",{640.f,inf},1.f,1.f);
    projection("Q-zoom-NaN",query_ground,nan,1.f);
    projection("Q-zoom-infinity",query_ground,inf,1.f);
    projection("Q-zoom-zero",query_ground,0.f,1.f);
    projection("Q-zoom-negative",query_ground,-1.f,1.f);
    projection("Q-scale-NaN",query_ground,1.f,nan);
    projection("Q-scale-infinity",query_ground,1.f,inf);
    projection("Q-scale-zero",query_ground,1.f,0.f);
    projection("Q-scale-negative",query_ground,1.f,-1.f);
    projection("Q-underflow",query_ground,std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::denorm_min());
    projection("Q-overflow",query_ground,std::numeric_limits<float>::max(),std::numeric_limits<float>::max());
    projection("Q-collapsed",{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},1.f,1.f);
    for (const auto value : std::array<std::pair<std::string_view,float>,2>{{{"Q-point-NaN",nan},{"Q-point-infinity",inf}}})
        rejected(value.first,{"hit"},[&](std::string_view,Frame2D&){
            (void)art.hit_test_lit_sprite(query_id,query_ground,1.f,{value.second,-10000.f}); });
    rejected("Q-unknown-bounds",{"bounds"},[&](std::string_view,Frame2D&){
        (void)art.lit_sprite_bounds("query.missing",query_ground,1.f); },true);
    rejected("Q-unknown-hit",{"hit"},[&](std::string_view,Frame2D&){
        (void)art.hit_test_lit_sprite("query.missing",query_ground,1.f,{-10000.f,-10000.f}); },true);
    rejected("Q-unknown-draw",{"draw"},[&](std::string_view,Frame2D& frame){
        art.lit_sprite(frame,"query.missing",query_ground,{0.f,0.f,0.f},1.f); },true);
    rejected("Q-draw-anchor",{"draw"},[&](std::string_view,Frame2D& frame){
        art.lit_sprite(frame,query_id,query_ground,{nan,0.f,0.f},1.f); });
    rejected("Q-draw-wetness",{"draw"},[&](std::string_view,Frame2D& frame){
        art.lit_sprite(frame,query_id,query_ground,{0.f,0.f,0.f},1.f,1.f,inf); });
    rejected("Q-draw-opacity",{"draw"},[&](std::string_view,Frame2D& frame){
        art.lit_sprite(frame,query_id,query_ground,{0.f,0.f,0.f},1.f,1.f,0.f,0u,1.f,nan); });
    {
        auto candidate = retained; candidate.linear_hdr_world = false;
        const auto candidate_before = frame_state(candidate); bool failed{}; std::string error;
        try { art.lit_sprite(candidate,query_id,query_ground,{0.f,0.f,0.f},1.f); }
        catch (const std::invalid_argument& exception) { failed = true; error = exception.what(); }
        require(failed && !error.empty() && frame_state(candidate) == candidate_before,"Non-HDR invalid draw mutated frame");
        rows.push_back({{"id","Q-draw-HDR"},{"rejected",true},{"unchanged",true},{"error",error},{"apis",{"draw"}}});
    }
    rejected("Q-draw-zero-invalid-geometry",{"draw"},[&](std::string_view,Frame2D& frame){
        art.lit_sprite(frame,query_id,query_ground,{0.f,0.f,0.f},0.f,1.f,0.f,0u,1.f,0.f); });
    for (const float opacity : std::array<float,2>{0.f,.001f}) {
        auto candidate = retained;
        art.lit_sprite(candidate,query_id,query_ground,{0.f,0.f,0.f},1.f,1.f,0.f,0u,1.f,opacity);
        require(frame_state(candidate) == before,"Quantized-zero valid draw changed frame");
    }
    require(rows.size() == 23u,"Typed query negative plan is incomplete");
}

Json prepare_typed_queries(Renderer2D& renderer, const AssetCatalog& kit, const fs::path& output,
                           GameArt& controls, GameArt& fixture) {
    require(!renderer.has_submitted_frames(),"Typed query catalog controls must run before any submit");
    Json result{{"scope","Diagnostic typed bounds/CPU picking; legacy outline preserved; typed outlines and normal GameView integration OPEN"},
        {"threshold",64},{"max_dimension",192},{"budgets",{{"hit",16777216},{"outline",20971520}}},
        {"controls",Json::array()},{"invalid_controls",Json::array()},{"rows",Json::array()},
        {"catalog_rejections",Json::array()},{"fixtures",Json::array()}};
    const auto positive = [&](std::string_view id){ result["controls"].push_back({{"id",id},{"passed",true}}); };
    controls.load(kit);
    const auto kit_doc = Json::parse(kit.read_path("art/world.json"));
    require(kit_doc.at("lit_sprites").size() == 8u && !controls.ready(),"Eight lit-only control routes are missing");
    const std::set<std::string> expected_ids{"control.house.sprite","control.workshop.sprite","control.tree.sprite",
        "control.rock.sprite","control.stockpile.sprite","control.resident.empty.sprite",
        "control.resident.log.sprite","control.resident.sack.sprite"};
    std::set<std::string> actual_ids;
    std::size_t expected_bytes{};
    for (const auto& source : kit_doc.at("lit_sprites")) {
        const auto id = source.at("id").get<std::string>();
        actual_ids.emplace(id);
        require(controls.contains_lit(id) && !controls.contains(id),"Control typed/legacy routing changed");
        auto row = query_row(controls,kit,kit_doc,source);
        const auto mask = row.at("mask_size").get<std::array<std::size_t,2>>();
        expected_bytes += mask[0]*mask[1]; result["rows"].push_back(std::move(row));
    }
    require(actual_ids == expected_ids && controls.hit_mask_count() == 8u && controls.hit_mask_bytes() == expected_bytes &&
        controls.outline_sprite_count() == 0u && controls.outline_texture_bytes() == 0u,"Control cache accounting differs");
    result["control_catalog"] = {{"ready",false},{"legacy_contains",false},{"hit_mask_count",8},
        {"hit_mask_bytes",expected_bytes},{"outline_count",0},{"outline_bytes",0}};
    positive("Q-control-bounds"); positive("Q-control-coverage");
    for (std::size_t variant = 0; variant < query_roots.size(); ++variant)
        result["fixtures"].push_back(make_query_catalog(output,variant));
    AssetCatalog valid(output/query_roots[0]);
    const auto doc = Json::parse(valid.read_path("art/world.json")); fixture.load(valid);
    require(fixture.ready() && fixture.contains(legacy_query_id) && !fixture.contains(query_id) &&
        fixture.contains_lit(query_id) && !fixture.contains_lit(legacy_query_id),"Diagnostic legacy/typed routes overlap");
    const auto row = query_row(fixture,valid,doc,doc.at("lit_sprites")[0]);
    const auto caches = query_cache(fixture);
    require(fixture.hit_mask_count() == 2u && fixture.hit_mask_bytes() == 49152u &&
        fixture.outline_sprite_count() == 1u && fixture.outline_texture_bytes() == 98304u,"Combined fixture cache budget differs");
    const auto bounds = fixture.lit_sprite_bounds(query_id,query_ground,1.f);
    const auto coverage = query_coverage(fixture,query_id,query_ground,1.f,1.f,{192u,128u},bounds);
    const auto at = [&](std::uint32_t x,std::uint32_t y){ return coverage[std::size_t(y)*192u+x]; };
    require(at(20u,20u) == 0u && at(70u,20u) == 0u && at(100u,20u) == 255u &&
        at(170u,20u) == 255u && at(120u,60u) == 0u && at(100u,60u) == 255u && at(191u,20u) == 255u,
        "Cropped 0/63/64/255 threshold, interior hole or atlas confinement is wrong");
    result["fixture"] = {{"row",row},{"legacy",legacy_query_checks(fixture,row)},{"caches",caches},
        {"alpha_controls",{{"zero_clear",true},{"63_clear",true},{"64_covered",true},{"255_covered",true},
            {"hole_clear",true},{"hole_neighbor_covered",true},{"opaque_padding_excluded",true},{"pooled_pair_covered",true}}},
        {"quantized_zero_opacities",{0.f,.001f}},{"budget_candidate_typed_ids",684},
        {"budget_candidate_requested_bytes",16834560}};
    positive("Q-crop-bounds"); positive("Q-crop-alpha"); positive("Q-half-open");
    positive("Q-legacy-mirror"); positive("Q-cache-outline");
    fixture.load(valid);
    require(query_cache(fixture) == caches && query_row(fixture,valid,doc,doc.at("lit_sprites")[0]) == row &&
        legacy_query_checks(fixture,row) == result["fixture"]["legacy"],"Cold reload changed query/cache/outline semantics");
    result["fixture"]["cold_reload"] = {{"succeeded",true},{"queries_same",true},{"caches_same",true}};
    positive("Q-cold-reload");
    constexpr std::array<std::string_view,4> ids{"Q-catalog-size","Q-catalog-rect","Q-catalog-hash","Q-catalog-budget"};
    constexpr std::array<std::string_view,4> reasons{"Invalid lit sprite source geometry","Material channel rectangle",
        "Material texture bytes/hash","Sprite hit-mask memory budget exceeded"};
    for (std::size_t variant = 1; variant < query_roots.size(); ++variant) {
        AssetCatalog invalid(output/query_roots[variant]); bool rejected{}; std::string error;
        try { fixture.load(invalid); } catch (const std::runtime_error& exception) { rejected = true; error = exception.what(); }
        require(rejected && error.find(reasons[variant-1u]) != std::string::npos && query_cache(fixture) == caches &&
            query_row(fixture,valid,doc,doc.at("lit_sprites")[0]) == row &&
            legacy_query_checks(fixture,row) == result["fixture"]["legacy"],"Invalid catalog changed retained queries/cache or rejected for wrong reason");
        result["catalog_rejections"].push_back({{"id",ids[variant-1u]},{"root",query_roots[variant]},
            {"reason",reasons[variant-1u]},{"rejected",true},{"unchanged",true},{"error",error},
            {"caches_before",caches},{"caches_after",query_cache(fixture)}});
    }
    positive("Q-candidate-atomic");
    invalid_query_checks(fixture,result["invalid_controls"]);
    positive("Q-api-atomic"); positive("Q-quantized-zero");
    return result;
}

void check_query_startup_guard(GameArt& fixture, const fs::path& output, Json& queries) {
    AssetCatalog valid(output/query_roots[0]);
    const auto doc = Json::parse(valid.read_path("art/world.json"));
    bool rejected{}; std::string error;
    try { fixture.load(valid); } catch (const std::runtime_error& exception) { rejected = true; error = exception.what(); }
    require(rejected && error.find("startup-only") != std::string::npos && query_cache(fixture) == queries["fixture"]["caches"] &&
        query_row(fixture,valid,doc,doc.at("lit_sprites")[0]) == queries["fixture"]["row"] &&
        legacy_query_checks(fixture,queries["fixture"]["row"]) == queries["fixture"]["legacy"],
        "After-UI startup reload did not preserve typed/legacy queries");
    queries["fixture"]["startup_reload"] = {{"rejected",true},{"unchanged",true},{"error",error}};
    queries["controls"].push_back({{"id","Q-startup-only"},{"passed",true}});
    require(queries["controls"].size() == 12u && queries["catalog_rejections"].size() == 4u,
        "Typed query positive/catalog control plan is incomplete");
}
} // namespace

int main(int argc, char** argv) {
    fs::path output; bool owned{};
    Json report{{"schema",1},{"complete",false}};
    const auto started = std::chrono::steady_clock::now();
    try {
        require(argc == 5,"Usage: GameViewUiProbe runtime-assets control-kit ui-kit fresh-output");
        const auto assets = fs::absolute(argument(argv[1])), kit_path = fs::absolute(argument(argv[2]));
        const auto ui_path = fs::absolute(argument(argv[3])); output = fs::absolute(argument(argv[4]));
        require(output.parent_path() == assets.parent_path().parent_path() && !fs::exists(output),
            "UI output must be a fresh sibling build child");
        require(fs::space(output.parent_path()).available >= 384u*1024u*1024u,"Insufficient evidence headroom");
        require(fs::create_directory(output),"Could not create UI output"); owned = true;
        create_fixture(output / world_save); const auto saved_fixture = file_bytes(output / world_save);
        Json data{{"controls",Json::array()},{"invalid_controls",Json::array()},{"captures",Json::array()},
            {"world_saves",{world_save}},{"capture_limit",capture_limit},{"backend","direct3d12"},
            {"ui",{{"prefix_invalid_controls",Json::array()}}},
            {"scope","Actual single RmlUi lifecycle; direct UI Legacy/cumulative oracle; diagnostic typed World"}};
        const auto positive = [&](std::string_view id) { data["controls"].push_back({{"id",id},{"passed",true}}); };
        require(SDL_Init(SDL_INIT_VIDEO),"SDL initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra actual RmlUi engine controls",1920,1080,0),SDL_DestroyWindow);
        require(window && SDL_ShowWindow(window.get()) && SDL_SyncWindow(window.get()),"Native UI window failed");
        RendererOptions options; options.world_graph = WorldGraph::WaterAtmosphereSlice; options.collect_telemetry = true;
        Renderer2D renderer(window.get(),assets,options); renderer.set_logical_size(extent);
        require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12","Hardware D3D12 required");
        const auto& hardware = renderer.device_info();
        data["hardware"] = {{"backend",hardware.backend},{"gpu",hardware.device},{"driver",hardware.driver},
            {"software",hardware.software},{"resolution",{extent.width,extent.height}},
            {"graph","WaterAtmosphereSlice"},{"fps",nullptr},{"gpu_execution_ms",nullptr}};
        ClientSession session(70421); std::string error;
        require(session.load(output/world_save,error),"UI session load: "+error);
        require(!session.last_load().copied && session.snapshot_ready() && session.world_ready(),"Current actual fixture did not load");
        GameView view(renderer); view.set_logical_size(extent);
        AssetCatalog kit(kit_path); view.load_engine_control_art(kit); view.load_engine_water_art();
        EngineControlSettings settings; settings.camera_x = 28.f; settings.camera_y = 25.f; settings.cell_radius = 8;
        settings.water_enabled = true; settings.environment = EngineEnvironmentSettingsV1{1u,12.f,.25f,.25f,true};
        const std::array<EngineControlInstance,2> instances{{
            {"control.house.sprite",26.f,22.f,1.f,101u,101u,0.f,true,1001u},
            {"control.resident.empty.sprite",26.f,24.f,1.f,102u,111u,0.f,false,0u}}};
        Frame2D world; const auto stats = view.compose_engine_control(session,10.,settings,instances,world);
        require(stats.bridge_decks == 3u && stats.water_cells > 10u && stats.reflection_draws == 2u &&
            world.linear_hdr_world && world.water_surface && world.atmosphere && world.moon_lighting &&
            world.surface_vertices.size() == world.vertices.size(),"Typed UI World fixture is vacuous");
        GameArt query_controls(renderer),query_fixture(renderer);
        data["typed_queries"] = prepare_typed_queries(renderer,kit,output,query_controls,query_fixture);
        const auto initial_targets = target_state(renderer);
        const auto empty = fresh_ui();
        const auto empty_image = capture_frame(renderer,empty,output,"ui-empty",data["captures"]);
        require(renderer.has_submitted_frames(),"Actual empty UI submit is missing");
        check_query_startup_guard(query_fixture,output,data["typed_queries"]);
        require(target_state(renderer) == initial_targets,"Empty UiOnly allocated World targets");
        const std::array<std::uint8_t,4> clear{background.r,background.g,background.b,background.a};
        for (std::size_t offset = 0; offset < empty_image.rgba.size(); offset += 4u)
            require(std::equal(clear.begin(),clear.end(),empty_image.rgba.begin()+static_cast<std::ptrdiff_t>(offset)),
                "Empty UiOnly clear differs");
        auto cleared = empty; cleared.clear_commands();
        require(cleared.content == FrameContent::World,"clear_commands retained an implicit UiOnly policy");
        positive("UI-empty");

        // The oracle owns its own window, texture and text resources. No RmlUi
        // instance or renderer handle is shared across these two backends.
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> oracle_window(
            SDL_CreateWindow("Ribterra direct UI oracle",1920,1080,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(oracle_window != nullptr,"Native UI oracle window failed");
        RendererOptions legacy_options; legacy_options.collect_telemetry = true;
        Renderer2D legacy(oracle_window.get(),assets,legacy_options); legacy.set_logical_size(extent);
        require(!legacy.device_info().software && legacy.device_info().backend == "direct3d12","Legacy oracle is not hardware D3D12");
        const auto oracle_ui = direct_ui(legacy), direct = direct_ui(renderer);
        const auto oracle_image = capture_frame(legacy,oracle_ui,output,"ui-direct-legacy",data["captures"]);
        const auto direct_image = capture_frame(renderer,direct,output,"ui-direct-cumulative",data["captures"]);
        require(oracle_image.rgba == direct_image.rgba,"Configured UiOnly differs from the direct Legacy oracle");
        require(target_state(renderer) == initial_targets,"Direct UiOnly allocated World targets");
        positive("UI-direct-legacy-equivalence");
        const std::array<std::uint8_t,4> expected{74u,47u,36u,255u};
        const auto actual = pixel(direct_image,180u,150u);
        for (std::size_t channel = 0; channel < 4u; ++channel)
            require(std::abs(int(actual[channel])-int(expected[channel])) <= 1,"Premultiplied UI blend differs");
        require(pixel(direct_image,120u,150u) == clear && pixel(direct_image,280u,150u) == clear &&
            pixel(direct_image,180u,115u) == clear && pixel(direct_image,180u,205u) == clear,"UI scissor did not clip");
        std::size_t text_pixels{};
        for (std::uint32_t y = 70u; y < 150u; ++y) for (std::uint32_t x = 390u; x < 700u; ++x)
            if (pixel(direct_image,x,y) != clear) ++text_pixels;
        require(text_pixels > 100u,"Actual direct UI text raster is vacuous");
        data["direct"] = {{"pixels_same",true},{"premultiplied_expected",expected},{"premultiplied_actual",actual},
            {"scissor_outside_same",true},{"text_changed_pixels",text_pixels},
            {"changed_pixels",difference(empty_image,direct_image)}};
        positive("UI-premultiplied-scissor-text");

        const auto world_image = capture_frame(renderer,world,output,"ui-world-before",data["captures"]);
        const auto carrier = renderer.read_water_coverage_v1(); save_bytes(output/"ui-carrier.rgba8",carrier.rgba8);
        auto carrier_info = carrier_counts(carrier); carrier_info.erase("frame_slot"); carrier_info.erase("target_generation");
        data["carrier"] = std::move(carrier_info); const auto world_targets = target_state(renderer);

        AssetCatalog ui_assets(ui_path); Localization locale(ui_path/"locales","uk");
        const auto settings_path = output/"ui-settings-unwritten.json";
        require(!fs::exists(settings_path),"Fresh UI settings unexpectedly exist");
        {
            GameUi ui(renderer,ui_assets,locale,settings_path,extent,false);
            require(ui.locale() == "uk" && ui.take_actions().empty(),"Fresh UI locale/actions differ");
            ui.show_menu(); ui.update(&session);
            auto menu = fresh_ui(); ui.compose(menu);
            require(menu.content == FrameContent::UiOnly && menu.surface_vertices.empty(),"Legacy absent-stream menu convention changed");
            auto menu_counts = rml_counts(menu);
            require(ui.visible_for_test("menu-card") && ui.element_text_width_for_test("app-title") > 20.f &&
                ui.element_bounds_for_test("menu-landscape").w > 100.f,"Actual menu text/image elements are vacuous");
            const auto menu_image = capture_frame(renderer,menu,output,"ui-rml-menu",data["captures"]);
            menu_counts["changed_pixels"] = difference(empty_image,menu_image);
            require(menu_counts["changed_pixels"].get<std::size_t>() > 10000u,"Actual menu pixel contribution is vacuous");
            data["ui"]["locale"] = ui.locale(); data["ui"]["menu"] = std::move(menu_counts);
            positive("RML-menu-actual"); positive("RML-legacy-absent-stream");
            auto neutral_menu = fresh_ui(); neutral_menu.vertices.push_back({{0.f,0.f},{0.f,0.f},{0u,0u,0u,255u}});
            neutral_menu.surface_vertices.push_back({}); ui.compose(neutral_menu);
            require(neutral_menu.content == FrameContent::UiOnly && neutral_menu.surface_vertices.size() == neutral_menu.vertices.size(),
                "Optional neutral UI companion suffix is misaligned");
            const SurfaceVertex neutral{};
            for (const auto& surface : neutral_menu.surface_vertices)
                require(surface.world_position == neutral.world_position && surface.normal == neutral.normal,
                    "Optional UI companion stream contains a nonneutral value");
            const auto neutral_image = capture_frame(renderer,neutral_menu,output,"ui-rml-menu-neutral",data["captures"]);
            require(neutral_image.rgba == menu_image.rgba,"Neutral companion convention changed RmlUi pixels");
            data["ui"]["menu_neutral"] = rml_counts(neutral_menu); data["ui"]["menu_neutral"]["pixels_same"] = true;
            positive("RML-menu-neutral");

            ui.show_world(); ui.set_view_state(&session,{},false,1); ui.update(session);
            auto hud = world; ui.compose(hud); check_prefix(world,hud);
            auto hud_counts = rml_counts(hud,world.commands.size());
            require(ui.world_visible() && ui.hud_visible() && ui.element_text_width_for_test("hud-title") > 20.f &&
                ui.element_rml_for_test("wood") != "0","Actual HUD text/state did not update");
            const auto hud_image = capture_frame(renderer,hud,output,"ui-rml-hud",data["captures"]);
            const auto hud_changed = difference(world_image,hud_image);
            require(hud_changed > 1000u && renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,
                "HUD pixels are vacuous or changed the raw World carrier");
            hud_counts["changed_pixels"] = hud_changed;
            hud_counts["prefix_vertices"] = world.vertices.size(); hud_counts["prefix_indices"] = world.indices.size();
            hud_counts["prefix_commands"] = world.commands.size(); hud_counts["prefix_preserved"] = true;
            hud_counts["neutral_suffix"] = true; hud_counts["carrier_same"] = true;
            data["ui"]["hud"] = std::move(hud_counts);
            positive("RML-world-prefix-preserved"); positive("RML-world-neutral-suffix");

            const auto reject_prefix = [&](std::string_view id, Frame2D bad) {
                const auto before = frame_state(bad); const auto targets = target_state(renderer);
                bool rejected{}; std::string failure;
                try { ui.compose(bad); } catch (const std::invalid_argument& exception) { rejected = true; failure = exception.what(); }
                require(rejected && frame_state(bad) == before && target_state(renderer) == targets,
                    "Malformed RmlUi prefix was accepted or modified");
                renderer.render(direct); check_ui_telemetry(renderer.frame_telemetry());
                data["ui"]["prefix_invalid_controls"].push_back({{"id",id},{"rejected",true},
                    {"unchanged",true},{"next_valid_rendered",true},{"error",failure}});
            };
            auto bad = world; bad.surface_vertices.clear(); reject_prefix("RML-HDR-missing-companions",std::move(bad));
            bad = world; bad.surface_vertices.pop_back(); reject_prefix("RML-world-misaligned",std::move(bad));
            bad = direct; bad.surface_vertices.resize(bad.vertices.size()); bad.surface_vertices[0].normal[0] = 1.f;
            reject_prefix("RML-UiOnly-non-neutral",std::move(bad));
            bad = direct; bad.surface_vertices.resize(1u); reject_prefix("RML-UiOnly-misaligned",std::move(bad));
            positive("RML-invalid-prefix-atomic");

            // The retained HUD/menu atlas handles are never rendered after the
            // next compose. Only plain authored World and direct UI are reused.
            ui.show_menu(); ui.update(&session); auto interleave = fresh_ui(); ui.compose(interleave);
            (void)capture_frame(renderer,interleave,output,"ui-interleave-menu",data["captures"]);
            require(target_state(renderer) == world_targets,"RmlUi-only rendering changed retained World targets");
            const auto after_image = capture_frame(renderer,world,output,"ui-world-after",data["captures"]);
            require(after_image.rgba == world_image.rgba && renderer.read_water_coverage_v1().rgba8 == carrier.rgba8 &&
                target_state(renderer) == world_targets,"World-UiOnly-World changed pixels/carrier/targets");
            data["interleave"] = {{"pixels_same",true},{"carrier_same",true},{"targets_unchanged",true},
                {"targets",world_targets}};
            positive("World-UI-World"); positive("UI-retains-world-targets");
            require(ui.take_actions().empty() && !fs::exists(settings_path),"RmlUi queued actions or wrote settings");
        }

        data["frames_in_flight"] = Json::array();
        for (const std::uint32_t depth : {1u,2u,3u}) {
            const auto previous = renderer.presentation_status().effective;
            auto requested = previous; requested.frames_in_flight = depth;
            const bool supported = renderer.set_presentation(requested);
            const auto effective = renderer.presentation_status().effective;
            require(supported ? effective == requested : effective == previous,"Presentation changed after a rejected request");
            std::set<std::uint32_t> slots;
            const auto retained_targets = target_state(renderer);
            const auto count = effective.frames_in_flight*2u+1u;
            for (std::uint32_t index = 0; index < count; ++index) {
                renderer.render(direct); check_ui_telemetry(renderer.frame_telemetry());
                slots.insert(renderer.frame_telemetry().frame_slot);
            }
            require(slots.size() == effective.frames_in_flight && target_state(renderer) == retained_targets,
                "UiOnly did not safely visit every actual presentation slot");
            const auto flight_image = capture_frame(renderer,direct,output,"ui-flight-"+std::to_string(depth),data["captures"]);
            require(flight_image.rgba == direct_image.rgba,"Presentation depth changed direct UI pixels");
            renderer.render(world); check_world_telemetry(renderer.frame_telemetry());
            require(renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,"World carrier changed after presentation-slot UI reuse");
            data["frames_in_flight"].push_back({{"requested",depth},{"effective",effective.frames_in_flight},
                {"supported",supported},{"slots",slots},{"actual_ui_submissions",count+1u},
                {"pixels_same",true},{"carrier_same",true},{"world_targets_unchanged",true}});
        }
        positive("UI-frames-in-flight");
        const auto before_resize = target_state(renderer);
        const auto odd = resize_window(window.get(),renderer,{1601u,901u});
        (void)capture_frame(renderer,direct,output,"ui-odd-resize",data["captures"],{1601u,901u});
        require(target_state(renderer) == before_resize,"UiOnly odd resize allocated/replaced World targets");
        renderer.render(world); check_world_telemetry(renderer.frame_telemetry());
        const auto odd_carrier = renderer.read_water_coverage_v1();
        require(odd_carrier.size.width == 1601u && odd_carrier.size.height == 901u,"Odd World carrier extent differs");
        const auto odd_targets = target_state(renderer);
        const auto restored = resize_window(window.get(),renderer,extent);
        const auto restored_image = capture_frame(renderer,direct,output,"ui-restored",data["captures"]);
        require(restored_image.rgba == direct_image.rgba && target_state(renderer) == odd_targets,
            "Restored UiOnly changed pixels or resized retained World targets");
        renderer.render(world); check_world_telemetry(renderer.frame_telemetry());
        require(renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,"Restored World carrier differs");
        data["resize"] = {{"odd",odd},{"restored",restored},{"ui_targets_unchanged",true},
            {"ui_pixels_restored",true},{"world_carrier_restored",true},
            {"before_targets",before_resize},{"odd_world_targets",odd_targets},{"restored_world_targets",target_state(renderer)}};
        positive("UI-odd-resize-restore");

        const auto negative = [&](std::string_view id, const std::function<void(Frame2D&)>& mutate) {
            auto bad = direct; mutate(bad);
            const auto targets = target_state(renderer); const auto before = renderer.frame_telemetry();
            bool rejected{}; std::string failure;
            try { renderer.render(bad); } catch (const std::exception& exception) { rejected = true; failure = exception.what(); }
            const auto after = renderer.frame_telemetry();
            const bool no_completed_submission = (after.sequence == before.sequence && after.submitted == before.submitted) ||
                (after.sequence == 0u && !after.submitted);
            require(rejected && no_completed_submission && target_state(renderer) == targets,
                "Invalid UI candidate submitted or mutated World target state");
            renderer.render(direct); check_ui_telemetry(renderer.frame_telemetry());
            require(target_state(renderer) == targets,"Valid recovery UI changed World targets");
            data["invalid_controls"].push_back({{"id",id},{"rejected",true},{"targets_unchanged",true},
                {"completed_submission_unchanged",true},{"next_valid_rendered",true},{"error",failure}});
        };
        negative("CONTENT-unknown",[](auto& f){ f.content = static_cast<FrameContent>(99); });
        negative("UI-hdr",[](auto& f){ f.linear_hdr_world = true; });
        negative("UI-exposure",[](auto& f){ f.world_exposure = 2.f; });
        negative("UI-ground",[](auto& f){ f.ground_material_draws.emplace_back(); });
        negative("UI-sprite",[](auto& f){ f.sprite_material_draws.emplace_back(); });
        negative("UI-water",[](auto& f){ f.water_surface.emplace(); });
        negative("UI-water-normal",[](auto& f){ f.water_normal = {1u,1u}; });
        negative("UI-atmosphere",[](auto& f){ f.atmosphere.emplace(); });
        negative("UI-moon",[](auto& f){ f.moon_lighting.emplace(); });
        negative("UI-contact",[](auto& f){ f.contact_shadows.emplace_back(); });
        negative("UI-contact-projection",[](auto& f){ f.contact_projection.emplace(); });
        negative("UI-occluder",[](auto& f){ f.local_occluders.emplace_back(); });
        negative("UI-sky",[](auto& f){ f.sky_shadow_triangles.emplace_back(); });
        negative("UI-reflection",[](auto& f){ f.reflection_draws.emplace_back(); });
        negative("UI-inactive-light",[](auto& f){ f.surface_lighting.lights[3].color_intensity[0] = .5f; });
        negative("UI-nonUi-geometry",[](auto& f){ std::get<GeometryDraw>(f.commands[0]).pass = Pass::WorldOrdered; });
        negative("UI-nonUi-text",[](auto& f){ std::get<TextDraw>(f.commands[1]).pass = Pass::Atmosphere; });
        negative("UI-surface-lit",[](auto& f){ std::get<GeometryDraw>(f.commands[0]).surface_lit = true; });
        negative("UI-non-neutral",[](auto& f){ f.surface_vertices.resize(f.vertices.size()); f.surface_vertices[0].world_position[0] = 1.f; });
        negative("UI-misaligned",[](auto& f){ f.surface_vertices.resize(1u); });
        negative("UI-stale-texture",[](auto& f){ std::get<GeometryDraw>(f.commands[0]).texture.generation += 100u; });
        negative("UI-NaN-vertex",[](auto& f){ f.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN(); });
        negative("UI-scissor",[](auto& f){ std::get<GeometryDraw>(f.commands[0]).scissor.w = -1.f; });
        negative("UI-text-size",[](auto& f){ std::get<TextDraw>(f.commands[1]).size = std::numeric_limits<float>::quiet_NaN(); });
        negative("WORLD-default-incomplete",[](auto& f){ f = Frame2D{}; f.logical_size = extent; });
        const auto recovered = capture_frame(renderer,world,output,"ui-invalid-recovered",data["captures"]);
        require(recovered.rgba == world_image.rgba && renderer.read_water_coverage_v1().rgba8 == carrier.rgba8,
            "Valid World did not recover exact pixels/carrier after invalid UI candidates");
        require(!fs::exists(settings_path) && !fs::exists(output/(std::string(world_save)+".bak")) &&
            file_bytes(output/world_save) == saved_fixture,"Settings or original fixture bytes changed");
        data["final_preservation"] = {{"settings_absent",true},{"settings_actions",0},
            {"fixture_bytes_unchanged",true},{"fixture_bak_absent",true},{"world_pixels_same",true},{"carrier_same",true}};
        positive("UI-settings-preserved");
        require(data["controls"].size() == 14u && data["invalid_controls"].size() == 25u &&
            data["captures"].size() == capture_limit,"UI control/capture plan is incomplete");
        data["open"] = {"normal main/startup integration","production material art and reference fidelity",
            "real RmlUi on a separate Legacy renderer; direct UI oracle only","all other UI panels/settings actions",
            "precipitation/snow/smoke/fire FX","full-engine FPS/GPU duration/reference acceptance"};
        report["deterministic"] = std::move(data);
        report["measured"] = {{"total_ms",std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count()}};
        report["complete"] = true;
        check_budget(output);
        const auto report_bytes = report.dump(2).size()+1u;
        std::set<std::string> expected_files{std::string(world_save),"ui-carrier.rgba8"};
        constexpr std::array<std::string_view,15> capture_files{"ui-empty.png","ui-direct-legacy.png",
            "ui-direct-cumulative.png","ui-world-before.png","ui-rml-menu.png","ui-rml-menu-neutral.png",
            "ui-rml-hud.png","ui-interleave-menu.png","ui-world-after.png","ui-flight-1.png",
            "ui-flight-2.png","ui-flight-3.png","ui-odd-resize.png","ui-restored.png","ui-invalid-recovered.png"};
        for (const auto name : capture_files) expected_files.emplace(name);
        std::set<std::string> expected_directories;
        for (const auto root : query_roots) {
            expected_directories.emplace(root); expected_directories.emplace(std::string(root)+"/art");
            expected_directories.emplace(std::string(root)+"/art/material");
            for (const auto file : query_files) expected_files.emplace(std::string(root)+"/"+std::string(file));
        }
        for (const auto& fixture : report["deterministic"]["typed_queries"]["fixtures"])
            for (const auto& file : fixture["files"]) {
                const auto path = output/fixture["root"].get<std::string>()/file["path"].get<std::string>();
                const auto bytes = file_bytes(path);
                require(bytes.size() == file["bytes"].get<std::size_t>() && sha256(bytes) == file["sha256"].get<std::string>(),
                    "Retained typed-query source fixture changed");
            }
        std::uintmax_t retained_bytes{}; std::set<std::string> retained_files,retained_directories;
        for (const auto& file : fs::recursive_directory_iterator(output)) {
            require(!file.is_symlink(),"Unexpected UI output symlink");
            const auto name = file.path().lexically_relative(output).generic_string();
            if (file.is_directory()) retained_directories.emplace(name);
            else {
                require(file.is_regular_file(),"Unexpected UI output entry");
                retained_bytes += file.file_size(); retained_files.emplace(name);
            }
        }
        require(expected_files.size() == 67u && retained_files == expected_files &&
            retained_directories == expected_directories && report_bytes <= 1024u*1024u &&
            retained_bytes+report_bytes <= budget &&
            fs::space(output).available >= 384u*1024u*1024u+report_bytes,
            "Final UI inventory/report storage budget exceeded");
        save_report(output/"ui-report.json",report);
        std::cout << "gameview ui checks passed\n"; return 0;
    } catch (const std::exception& exception) {
        report["complete"] = false;
        std::cerr << exception.what() << '\n';
        if (owned && !fs::exists(output/"ui-report.json")) {
            report["failure"] = exception.what();
            try { save_report(output/"ui-report.json",report); } catch (...) {}
        }
        return 1;
    }
}

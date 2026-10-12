#include "ribterra/game_art.hpp"
#include "ribterra/art_alpha.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/sky_shadow.hpp"
#include "ribterra/world_projection.hpp"
#include <array>
#include <algorithm>
#include <SDL3/SDL.h>
#include <cmath>
#include <nlohmann/json.hpp>
#include <png.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <span>

namespace ribterra {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::string_view, 6> material_channels{
    "albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask"};
constexpr std::array<render::SampledTextureFormat, 6> channel_formats{
    render::SampledTextureFormat::Rgba8UnormSrgb, render::SampledTextureFormat::Rgba8Unorm,
    render::SampledTextureFormat::R16Unorm, render::SampledTextureFormat::R8Unorm,
    render::SampledTextureFormat::Rgba8Unorm, render::SampledTextureFormat::R8Unorm};
constexpr std::uint32_t hit_mask_max_dimension = 192;

struct SpriteHitMask {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> coverage;
};

SpriteHitMask make_hit_mask(std::span<const std::uint8_t> rgba, render::Extent source_size,
                           const std::array<int,4>& rect, std::size_t remaining_bytes) {
    if (!source_size.width || !source_size.height || source_size.width > 4096u || source_size.height > 4096u ||
        rgba.size() != std::uint64_t(source_size.width)*source_size.height*4u ||
        rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 ||
        std::int64_t(rect[0])+rect[2] > source_size.width || std::int64_t(rect[1])+rect[3] > source_size.height)
        throw std::runtime_error("Invalid sprite hit-mask source rectangle");
    const auto longest = static_cast<std::uint32_t>(std::max(rect[2],rect[3]));
    const float scale = std::min(1.f,float(hit_mask_max_dimension)/float(longest));
    const auto width = static_cast<std::uint32_t>(std::max(1L,std::lround(float(rect[2])*scale)));
    const auto height = static_cast<std::uint32_t>(std::max(1L,std::lround(float(rect[3])*scale)));
    const auto bytes = std::size_t(width)*height;
    if (bytes > remaining_bytes) throw std::runtime_error("Sprite hit-mask memory budget exceeded");
    SpriteHitMask result{width,height,std::vector<std::uint8_t>(bytes)};
    for (std::uint32_t my = 0; my < height; ++my) {
        const int sy0 = rect[1]+int(std::uint64_t(my)*rect[3]/height);
        const int sy1 = rect[1]+int(std::uint64_t(my+1u)*rect[3]/height);
        for (std::uint32_t mx = 0; mx < width; ++mx) {
            const int sx0 = rect[0]+int(std::uint64_t(mx)*rect[2]/width);
            const int sx1 = rect[0]+int(std::uint64_t(mx+1u)*rect[2]/width);
            std::uint8_t alpha{};
            for (int sy = sy0; sy < std::max(sy0+1,sy1); ++sy)
                for (int sx = sx0; sx < std::max(sx0+1,sx1); ++sx)
                    alpha = std::max(alpha,rgba[(std::size_t(sy)*source_size.width+std::size_t(sx))*4u+3u]);
            result.coverage[std::size_t(my)*width+mx] = alpha >= 64u ? 255u : 0u;
        }
    }
    return result;
}

bool inside_sprite(render::Rect bounds, render::Point point) noexcept {
    return bounds.w > 0.f && bounds.h > 0.f && point.x >= bounds.x && point.y >= bounds.y &&
        point.x < bounds.x+bounds.w && point.y < bounds.y+bounds.h;
}

bool sample_hit_mask(const SpriteHitMask& coverage, render::Rect bounds,
                     render::Point point, bool flip_x = false) noexcept {
    float u = (point.x-bounds.x)/bounds.w;
    if (flip_x) u = 1.f-u;
    const auto x = std::min(coverage.width-1u,std::uint32_t(u*coverage.width));
    const auto y = std::min(coverage.height-1u,std::uint32_t((point.y-bounds.y)/bounds.h*coverage.height));
    return coverage.coverage[std::size_t(y)*coverage.width+x] != 0u;
}

render::Rect project_lit_sprite(const std::array<float,2>& source_size,
                               const std::array<float,2>& pivot, float export_scale,
                               render::Point ground, float zoom, float geometry_scale) {
    if (!std::isfinite(ground.x) || !std::isfinite(ground.y) ||
        !std::isfinite(zoom) || zoom <= 0.f || !std::isfinite(geometry_scale) || geometry_scale <= 0.f)
        throw std::invalid_argument("Lit sprite projection requires finite ground and positive zoom/scale");
    const float pixel_scale = zoom*geometry_scale/export_scale;
    const float width = source_size[0]*pixel_scale, height = source_size[1]*pixel_scale;
    const float left = ground.x-pivot[0]*pixel_scale, top = ground.y-pivot[1]*pixel_scale;
    const float right = left+width, bottom = top+height;
    if (!std::isfinite(pixel_scale) || pixel_scale <= 0.f ||
        !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(right) || !std::isfinite(bottom) || width <= 0.f || height <= 0.f ||
        right <= left || bottom <= top)
        throw std::invalid_argument("Lit sprite projected rectangle must be finite and noncollapsed");
    return {left,top,width,height};
}

bool valid_hash(std::string_view hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::uint32_t material_integer(const Json &row, std::string_view field, std::uint32_t maximum) {
    const auto &value = row.at(std::string(field));
    if (!value.is_number_integer() || value.get<double>() < 1 || value.get<double>() > maximum)
        throw std::runtime_error("Invalid positive material integer: " + std::string(field));
    return value.get<std::uint32_t>();
}
std::array<int,4> material_rectangle(const Json& value) {
    if (!value.is_array() || value.size() != 4u)
        throw std::runtime_error("Material channel rectangle requires four bounded integers");
    std::array<int,4> result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        const auto& component = value[axis];
        const double minimum = axis < 2u ? 0. : 1.;
        if (!component.is_number_integer() || component.get<double>() < minimum || component.get<double>() > 4096.)
            throw std::runtime_error("Material channel rectangle requires four bounded integers");
        result[axis] = component.get<int>();
    }
    return result;
}
struct MaterialTextureSource {
    std::string id;
    render::SampledTextureDesc desc;
    std::vector<std::uint8_t> bytes;
};
struct MaterialSource {
    std::string id;
    std::array<std::size_t, 6> textures{};
    std::array<render::Rect, 6> uv{};
    std::array<int, 2> region_size{};
    std::array<int, 4> albedo_rect{};
    std::array<float, 4> emission{};
};
struct ContactArtSource {
    std::string id;
    render::ContactArtDesc desc;
    std::vector<std::uint8_t> bytes;
};
struct LitSpriteSource {
    struct Occluder {
        render::OccluderShape shape{render::OccluderShape::Obb};
        std::array<float, 2> center{}, axis_x{}, axis_y{};
        float half_x{}, half_y{}, radius{}, base_z{}, top_z{};
    };
    struct SkyCaster {
        std::string id;
        std::array<std::array<float, 3>, 3> vertices{};
        std::uint32_t source_mask{};
    };
    struct Contact {
        std::size_t art{};
        std::array<float, 3> anchor{};
        std::array<float, 2> half_extent{}, pivot{};
        float opacity{}, plane_tolerance{};
    };
    std::string id;
    std::size_t material{};
    std::array<float, 2> size{}, pivot{}, depth{};
    float export_scale{};
    std::vector<Occluder> occluders;
    std::vector<SkyCaster> sky_casters;
    std::vector<render::LightGpu> emitters;
    std::vector<Contact> contacts;
};
struct MaterialCatalogSource {
    std::vector<MaterialTextureSource> textures;
    std::vector<MaterialSource> materials;
    std::vector<LitSpriteSource> sprites;
    std::vector<ContactArtSource> contacts;
};

float contact_number(const Json &value, float minimum, float maximum) {
    if (!value.is_number() || value.is_boolean())
        throw std::runtime_error("Contact component must be a bounded finite number");
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < minimum || number > maximum)
        throw std::runtime_error("Contact component must be a bounded finite number");
    const float canonical = static_cast<float>(number);
    if (!std::isfinite(canonical) || (number != 0. && canonical == 0.f))
        throw std::runtime_error("Contact component must be a bounded finite number");
    return canonical;
}
template<std::size_t N>
std::array<float, N> contact_vector(const Json &value, float minimum, float maximum) {
    if (!value.is_array() || value.size() != N)
        throw std::runtime_error("Contact vector dimension is invalid");
    std::array<float, N> result{};
    for (std::size_t axis = 0; axis < N; ++axis)
        result[axis] = contact_number(value[axis], minimum, maximum);
    return result;
}
render::ContactFacing contact_facing(std::string_view value) {
    constexpr std::array<std::string_view, 9> names{
        "omnidirectional", "north", "north-east", "east", "south-east",
        "south", "south-west", "west", "north-west"};
    const auto found = std::find(names.begin(), names.end(), value);
    if (found == names.end()) throw std::runtime_error("Unknown contact art facing");
    return static_cast<render::ContactFacing>(found - names.begin());
}

float emitter_number(const Json &value, float minimum, float maximum) {
    if (!value.is_number() || value.is_boolean())
        throw std::runtime_error("Emitter component must be a bounded finite number");
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < minimum || number > maximum)
        throw std::runtime_error("Emitter component must be a bounded finite number");
    const float canonical = static_cast<float>(number);
    if (!std::isfinite(canonical) || static_cast<double>(canonical) != number)
        throw std::runtime_error("Emitter component must be canonical float32");
    return canonical;
}

// Matches the shader's receiver bias and exceeds its 1e-5 prism inset. Apply
// after float32 narrowing, and again after instance scaling, to reject anchors
// that could numerically enter their own prism in a foreign receiver's ray.
constexpr float local_emitter_clearance = .002f;
bool inside_authored_occluder(const std::array<float, 4> &anchor,
                              const LitSpriteSource::Occluder &occluder,
                              float clearance = local_emitter_clearance) {
    if (anchor[2] < occluder.base_z - clearance || anchor[2] > occluder.top_z + clearance) return false;
    const float dx = anchor[0] - occluder.center[0], dy = anchor[1] - occluder.center[1];
    const float along = dx * occluder.axis_x[0] + dy * occluder.axis_x[1];
    if (occluder.shape == render::OccluderShape::Obb) {
        const float across = dx * occluder.axis_y[0] + dy * occluder.axis_y[1];
        return std::abs(along) <= occluder.half_x + clearance &&
               std::abs(across) <= occluder.half_y + clearance;
    }
    const float nearest = std::clamp(along, -occluder.half_x, occluder.half_x);
    const float distance_x = dx - nearest * occluder.axis_x[0];
    const float distance_y = dy - nearest * occluder.axis_x[1];
    const float expanded_radius = occluder.radius + clearance;
    return distance_x * distance_x + distance_y * distance_y <= expanded_radius * expanded_radius;
}

float canonical_authored_coordinate(const Json &value) {
    if (!value.is_number() || value.is_boolean())
        throw std::runtime_error("Sky shadow vertex component must be a number");
    const double authored = value.get<double>();
    // Establish a representable, bounded domain before narrowing untrusted JSON.
    if (!std::isfinite(authored) || std::abs(authored) > 8192.0)
        throw std::runtime_error("Sky shadow vertex component must be canonical bounded float32");
    const float canonical = static_cast<float>(authored);
    if (!std::isfinite(canonical) ||
        static_cast<double>(canonical) != authored || std::abs(canonical) > 8192.f)
        throw std::runtime_error("Sky shadow vertex component must be canonical bounded float32");
    return canonical;
}

bool nondegenerate_sky_triangle(const std::array<std::array<float, 3>, 3> &vertices) {
    std::array<double, 3> first{}, second{}, cross{};
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        first[axis] = static_cast<double>(vertices[1][axis]) - vertices[0][axis];
        second[axis] = static_cast<double>(vertices[2][axis]) - vertices[0][axis];
    }
    cross = {first[1] * second[2] - first[2] * second[1],
             first[2] * second[0] - first[0] * second[2],
             first[0] * second[1] - first[1] * second[0]};
    const double area_squared = cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];
    return std::isfinite(area_squared) && area_squared != 0.0;
}

// Validate the complete schema-2 CPU candidate before any catalog GPU upload.
// Raw blobs bypass the 8-bit PNG decoder; every path still uses AssetCatalog.
MaterialCatalogSource parse_material_catalog(const Json &doc, const AssetCatalog &assets) {
    MaterialCatalogSource result;
    std::unordered_map<std::string, std::size_t> texture_ids, material_ids;
    std::unordered_map<std::string, std::size_t> contact_ids;
    if (const auto rows = doc.find("contact_arts"); rows != doc.end()) {
        if (!rows->is_array() || rows->size() > render::contact_art_capacity)
            throw std::runtime_error("Contact art descriptors exceed the registry bound");
        std::uint64_t contact_bytes{};
        for (const auto &row : *rows) {
            if (!row.is_object() || row.size() != 10)
                throw std::runtime_error("Contact art identity, format or fields are invalid");
            ContactArtSource source;
            source.id = row.at("id").get<std::string>();
            if (source.id.empty() ||
                source.id.size() > 256 ||
                !contact_ids.emplace(source.id, result.contacts.size()).second ||
                row.at("format") != "r8_unorm" || row.at("layout") != "tight_le")
                throw std::runtime_error("Contact art identity, format or fields are invalid");
            const auto width = material_integer(row, "width", render::contact_mask_max_dimension);
            const auto height = material_integer(row, "height", render::contact_mask_max_dimension);
            const auto &region = row.at("region");
            if (!region.is_array() || region.size() != 4)
                throw std::runtime_error("Contact art region must have four integer components");
            std::array<std::uint32_t, 4> rect{};
            for (std::size_t component = 0; component < rect.size(); ++component) {
                const auto &value = region[component];
                if (!value.is_number_integer() || value.get<double>() < 0. ||
                    value.get<double>() > render::contact_mask_max_dimension)
                    throw std::runtime_error("Contact art region component is invalid");
                rect[component] = value.get<std::uint32_t>();
            }
            source.desc = {{width, height, rect[0], rect[1], rect[2], rect[3]},
                           contact_facing(row.at("facing").get<std::string>())};
            const auto expected = std::uint64_t(width) * height;
            contact_bytes += expected;
            if (!row.at("bytes").is_number_integer() ||
                row.at("bytes").get<double>() != static_cast<double>(expected) ||
                contact_bytes > 16ULL * 1024 * 1024)
                throw std::runtime_error("Contact art byte ledger exceeds its budget or is inconsistent");
            const auto hash = row.at("sha256").get<std::string>();
            source.bytes = assets.read_path(row.at("path").get<std::string>());
            if (!valid_hash(hash) || source.bytes.size() != expected || sha256(source.bytes) != hash)
                throw std::runtime_error("Contact art bytes/hash do not match its descriptor");
            render::validate_contact_art(source.desc, source.bytes);
            result.contacts.push_back(std::move(source));
        }
    }
    std::uint64_t texture_bytes{};
    for (const auto &row : doc.at("textures")) {
        MaterialTextureSource texture;
        texture.id = row.at("id").get<std::string>();
        const auto format = row.at("format").get<std::string>();
        unsigned bpp{};
        if (format == "rgba8_unorm_srgb") { texture.desc.format = channel_formats[0]; bpp = 4; }
        else if (format == "rgba8_unorm") { texture.desc.format = channel_formats[1]; bpp = 4; }
        else if (format == "r16_unorm") { texture.desc.format = channel_formats[2]; bpp = 2; }
        else if (format == "r8_unorm") { texture.desc.format = channel_formats[3]; bpp = 1; }
        else throw std::runtime_error("Unsupported material texture format: " + format);
        texture.desc.size = {material_integer(row, "width", 4096), material_integer(row, "height", 4096)};
        if (texture.id.empty() || !texture_ids.emplace(texture.id, result.textures.size()).second ||
            !texture.desc.size.width || !texture.desc.size.height ||
            texture.desc.size.width > 4096 || texture.desc.size.height > 4096 ||
            row.at("layout") != "tight_le" || row.at("mip_levels") != 1)
            throw std::runtime_error("Invalid material texture ID, extent, layout or mip policy");
        const auto expected = std::uint64_t(texture.desc.size.width) * texture.desc.size.height * bpp;
        texture_bytes += expected;
        if (texture_bytes > 256ULL * 1024 * 1024 || row.at("bytes").get<std::uint64_t>() != expected)
            throw std::runtime_error("Material texture byte ledger exceeds budget or is inconsistent");
        const auto hash = row.at("sha256").get<std::string>();
        texture.bytes = assets.read_path(row.at("path").get<std::string>());
        if (!valid_hash(hash) || texture.bytes.size() != expected || sha256(texture.bytes) != hash)
            throw std::runtime_error("Material texture bytes/hash do not match its descriptor");
        result.textures.push_back(std::move(texture));
    }
    for (const auto &row : doc.at("materials")) {
        MaterialSource material;
        material.id = row.at("id").get<std::string>();
        if (material.id.empty() || !material_ids.emplace(material.id, result.materials.size()).second ||
            row.at("family") != "DynamicSprite25D" || row.at("alpha") != "straight" ||
            row.at("camera") != "ribterra-affine-v1" || material_integer(row, "revision", UINT32_MAX) == 0 ||
            !valid_hash(row.at("source_hash").get<std::string>()))
            throw std::runtime_error("Invalid DynamicSprite25D identity, family, alpha or source basis");
        const auto &channels = row.at("channels");
        if (!channels.is_object() || channels.size() != material_channels.size())
            throw std::runtime_error("DynamicSprite25D requires all six named channels");
        for (std::size_t c = 0; c < material_channels.size(); ++c) {
            const auto &binding = channels.at(std::string(material_channels[c]));
            const auto id = binding.at("texture").get<std::string>();
            const auto index = texture_ids.find(id);
            if (index == texture_ids.end()) throw std::runtime_error("Unknown material channel texture: " + id);
            const auto &texture = result.textures[index->second];
            const auto rect = material_rectangle(binding.at("rect"));
            if (texture.desc.format != channel_formats[c] || rect[0] < 0 || rect[1] < 0 ||
                rect[2] <= 0 || rect[3] <= 0 || std::int64_t(rect[0]) + rect[2] > texture.desc.size.width ||
                std::int64_t(rect[1]) + rect[3] > texture.desc.size.height ||
                (c && material.region_size != std::array<int, 2>{rect[2], rect[3]}))
                throw std::runtime_error("Material channel format, rectangle or geometry alignment mismatch");
            material.region_size = {rect[2], rect[3]};
            if (c == 0u) material.albedo_rect = rect;
            material.textures[c] = index->second;
            material.uv[c] = {float(rect[0]) / texture.desc.size.width, float(rect[1]) / texture.desc.size.height,
                              float(rect[2]) / texture.desc.size.width, float(rect[3]) / texture.desc.size.height};
        }
        material.emission = row.at("emission").get<std::array<float, 4>>();
        if (!std::all_of(material.emission.begin(), material.emission.end(), [](float v) {
                return std::isfinite(v) && v >= 0.f && v <= 64.f;
            })) throw std::runtime_error("Invalid material linear emission color/intensity");
        result.materials.push_back(std::move(material));
    }
    std::unordered_map<std::string, bool> sprite_ids;
    for (const auto &row : doc.at("lit_sprites")) {
        LitSpriteSource sprite;
        sprite.id = row.at("id").get<std::string>();
        const auto material = material_ids.find(row.at("material").get<std::string>());
        if (sprite.id.empty() || !sprite_ids.emplace(sprite.id, true).second || material == material_ids.end())
            throw std::runtime_error("Duplicate/empty lit sprite or unknown material");
        sprite.material = material->second;
        sprite.size = row.at("source_size").get<std::array<float, 2>>();
        sprite.pivot = row.at("pivot_px").get<std::array<float, 2>>();
        sprite.depth = row.at("depth_range").get<std::array<float, 2>>();
        sprite.export_scale = row.at("export_pixels_per_reference_pixel").get<float>();
        if (const auto rows = row.find("contact_shadows"); rows != row.end()) {
            if (!rows->is_array() || rows->size() > render::contact_shadow_capacity)
                throw std::runtime_error("Lit sprite contact_shadows exceeds its bound");
            for (const auto &source : *rows) {
                if (!source.is_object() || source.size() != 6)
                    throw std::runtime_error("Contact footprint fields or art reference are invalid");
                const auto art = contact_ids.find(source.at("art").get<std::string>());
                if (art == contact_ids.end())
                    throw std::runtime_error("Contact footprint fields or art reference are invalid");
                LitSpriteSource::Contact contact;
                contact.art = art->second;
                contact.anchor = contact_vector<3>(source.at("anchor_local"), -8192.f, 8192.f);
                contact.half_extent = contact_vector<2>(source.at("half_extent"),
                                                       render::contact_shadow_min_extent, 8192.f);
                contact.pivot = contact_vector<2>(source.at("pivot"), 0.f, 1.f);
                contact.opacity = contact_number(source.at("opacity"), 0.f, 1.f);
                contact.plane_tolerance = contact_number(source.at("plane_tolerance"),
                    render::contact_shadow_min_plane_tolerance, render::contact_shadow_max_plane_tolerance);
                if (source.at("anchor_local")[2].get<double>() != 0.)
                    throw std::runtime_error("Contact footprint anchor must lie on its receiver plane");
                sprite.contacts.push_back(contact);
            }
        }
        if (const auto found = row.find("local_occluders"); found != row.end()) {
            if (!found->is_array() || found->size() > render::local_occluders_per_light)
                throw std::runtime_error("Lit sprite local_occluders exceeds the per-light bound");
            for (const auto &primitive : *found) {
                LitSpriteSource::Occluder occluder;
                const auto shape = primitive.at("shape").get<std::string>();
                if (shape == "obb") occluder.shape = render::OccluderShape::Obb;
                else if (shape == "capsule") occluder.shape = render::OccluderShape::Capsule;
                else throw std::runtime_error("Unsupported local occluder shape");
                occluder.center = primitive.at("center_xy").get<std::array<float, 2>>();
                occluder.axis_x = primitive.at("axis_x").get<std::array<float, 2>>();
                occluder.axis_y = primitive.at("axis_y").get<std::array<float, 2>>();
                occluder.base_z = primitive.at("base_z").get<float>();
                occluder.top_z = primitive.at("top_z").get<float>();
                if (occluder.shape == render::OccluderShape::Obb) {
                    occluder.half_x = primitive.at("half_extent_x").get<float>();
                    occluder.half_y = primitive.at("half_extent_y").get<float>();
                    if (primitive.size() != 8)
                        throw std::runtime_error("OBB local occluder fields do not match the contract");
                } else {
                    occluder.half_x = primitive.at("half_length").get<float>();
                    occluder.radius = primitive.at("radius").get<float>();
                    if (primitive.size() != 8)
                        throw std::runtime_error("Capsule local occluder fields do not match the contract");
                }
                const auto finite = [](float value) { return std::isfinite(value); };
                const float x_length = std::hypot(occluder.axis_x[0], occluder.axis_x[1]);
                const float y_length = std::hypot(occluder.axis_y[0], occluder.axis_y[1]);
                const float dot = occluder.axis_x[0] * occluder.axis_y[0] +
                                  occluder.axis_x[1] * occluder.axis_y[1];
                if (!std::all_of(occluder.center.begin(), occluder.center.end(), finite) ||
                    !std::all_of(occluder.axis_x.begin(), occluder.axis_x.end(), finite) ||
                    !std::all_of(occluder.axis_y.begin(), occluder.axis_y.end(), finite) ||
                    !finite(occluder.base_z) || !finite(occluder.top_z) ||
                    !finite(occluder.half_x) || !finite(occluder.half_y) || !finite(occluder.radius) ||
                    std::abs(x_length - 1.f) > 1e-5f ||
                    (occluder.shape == render::OccluderShape::Obb &&
                        (std::abs(y_length - 1.f) > 1e-5f || std::abs(dot) > 1e-5f ||
                         occluder.half_x < render::local_occluder_min_dimension ||
                         occluder.half_y < render::local_occluder_min_dimension)) ||
                    (occluder.shape == render::OccluderShape::Capsule &&
                        (occluder.axis_y != std::array<float, 2>{0.f, 0.f} ||
                         occluder.half_x < 0.f || occluder.radius < render::local_occluder_min_dimension)) ||
                    occluder.top_z-occluder.base_z < render::local_occluder_min_dimension ||
                    std::abs(occluder.center[0]) > 8192.f || std::abs(occluder.center[1]) > 8192.f ||
                    std::abs(occluder.base_z) > 8192.f || std::abs(occluder.top_z) > 8192.f ||
                    occluder.half_x > 8192.f || occluder.half_y > 8192.f || occluder.radius > 8192.f)
                    throw std::runtime_error("Invalid local occluder geometry");
                sprite.occluders.push_back(occluder);
            }
        }
        if (const auto found = row.find("sky_shadow_triangles"); found != row.end()) {
            if (!found->is_array() || found->size() > render::sky_shadow_triangle_capacity)
                throw std::runtime_error("Lit sprite sky_shadow_triangles exceeds the raw bound");
            std::unordered_set<std::string> caster_ids;
            std::array<std::size_t, 2> active_by_source{};
            caster_ids.reserve(found->size());
            sprite.sky_casters.reserve(found->size());
            for (const auto &primitive : *found) {
                if (!primitive.is_object() || primitive.size() != 3 ||
                    !primitive.contains("id") || !primitive.contains("vertices") ||
                    !primitive.contains("source_mask"))
                    throw std::runtime_error("Sky shadow triangle requires exactly id, vertices and source_mask");
                LitSpriteSource::SkyCaster caster;
                if (!primitive.at("id").is_string())
                    throw std::runtime_error("Sky shadow triangle id must be a string");
                caster.id = primitive.at("id").get<std::string>();
                // JSON parsing already enforces valid UTF-8/surrogate encoding.
                // Caster IDs are opaque descriptor identities, not display text,
                // so do not impose Foundation's additional control/bidi policy.
                if (caster.id.empty() || caster.id.size() > 256 ||
                    !caster_ids.emplace(caster.id).second)
                    throw std::runtime_error("Sky shadow triangle id must be nonempty, at most 256 UTF-8 bytes and unique");
                const auto &vertices = primitive.at("vertices");
                if (!vertices.is_array() || vertices.size() != caster.vertices.size())
                    throw std::runtime_error("Sky shadow triangle requires exactly three vertices");
                for (std::size_t vertex = 0; vertex < caster.vertices.size(); ++vertex) {
                    const auto &source_vertex = vertices[vertex];
                    if (!source_vertex.is_array() || source_vertex.size() != caster.vertices[vertex].size())
                        throw std::runtime_error("Sky shadow vertex requires exactly three components");
                    for (std::size_t axis = 0; axis < caster.vertices[vertex].size(); ++axis)
                        caster.vertices[vertex][axis] = canonical_authored_coordinate(source_vertex[axis]);
                }
                if (!nondegenerate_sky_triangle(caster.vertices))
                    throw std::runtime_error("Sky shadow triangle must be nondegenerate after float32 canonicalization");
                const auto &mask = primitive.at("source_mask");
                if ((!mask.is_number_integer() && !mask.is_number_unsigned()) || mask.is_boolean())
                    throw std::runtime_error("Sky shadow source_mask must be an integer");
                if (mask.is_number_unsigned()) {
                    const auto source_mask = mask.get<std::uint64_t>();
                    if (source_mask < 1u || source_mask > 3u)
                        throw std::runtime_error("Sky shadow source_mask must be 1, 2 or 3");
                    caster.source_mask = static_cast<std::uint32_t>(source_mask);
                } else {
                    const auto source_mask = mask.get<std::int64_t>();
                    if (source_mask < 1 || source_mask > 3)
                        throw std::runtime_error("Sky shadow source_mask must be 1, 2 or 3");
                    caster.source_mask = static_cast<std::uint32_t>(source_mask);
                }
                for (std::size_t source = 0; source < active_by_source.size(); ++source)
                    if ((caster.source_mask & (1u << source)) != 0u &&
                        ++active_by_source[source] > render::sky_shadow_active_triangle_capacity)
                        throw std::runtime_error("Lit sprite sky-shadow per-source capacity exceeded");
                sprite.sky_casters.push_back(std::move(caster));
            }
        }
        if (const auto found = row.find("local_emitters"); found != row.end()) {
            if (!found->is_array() || found->size() > render::surface_light_capacity)
                throw std::runtime_error("Lit sprite local_emitters exceeds the four-light bound");
            std::unordered_set<std::string> emitter_ids;
            for (const auto &source : *found) {
                if (!source.is_object() || source.size() != 7)
                    throw std::runtime_error("Local emitter fields do not match the contract");
                const auto emitter_id = source.at("id").get<std::string>();
                if (emitter_id.empty() || emitter_id.size() > 256 ||
                    emitter_id.find_first_not_of(" \t\r\n") == std::string::npos ||
                    !emitter_ids.insert(emitter_id).second)
                    throw std::runtime_error("Local emitter ID must be nonempty, bounded and unique");
                const auto &anchor = source.at("anchor_xyz");
                const auto &direction = source.at("direction_cone");
                if (!anchor.is_array() || anchor.size() != 3 || !direction.is_array() || direction.size() != 4)
                    throw std::runtime_error("Local emitter anchor/direction dimensions are invalid");
                render::LightGpu emitter;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    emitter.anchor_radius[axis] = emitter_number(anchor[axis], -8192.f, 8192.f);
                    emitter.direction_cone[axis] = emitter_number(direction[axis], -1.f, 1.f);
                }
                emitter.anchor_radius[3] = emitter_number(source.at("radius"), 0.f, 32.f);
                emitter.direction_cone[3] = emitter_number(direction[3], -1.f, 1.f);
                emitter.height_falloff_reveal[0] = emitter_number(source.at("inner_cosine"), -1.f, 1.f);
                emitter.height_falloff_reveal[1] = emitter_number(source.at("falloff"), 0.f, 16.f);
                emitter.occluder_range_flags[2] = material_integer(source, "receiver_mask", 255u);
                emitter.color_intensity = result.materials[sprite.material].emission;
                const double direction_length =
                    double(emitter.direction_cone[0]) * emitter.direction_cone[0] +
                    double(emitter.direction_cone[1]) * emitter.direction_cone[1] +
                    double(emitter.direction_cone[2]) * emitter.direction_cone[2];
                const bool point = emitter.direction_cone[3] == -1.f && emitter.height_falloff_reveal[0] == -1.f;
                if (emitter.anchor_radius[3] <= 0.f || emitter.height_falloff_reveal[1] <= 0.f ||
                    std::abs(direction_length - 1.0) > 1e-4 ||
                    (!point && emitter.height_falloff_reveal[0] <= emitter.direction_cone[3]) ||
                    emitter.color_intensity[3] <= 0.f ||
                    std::none_of(emitter.color_intensity.begin(), emitter.color_intensity.begin() + 3,
                                 [](float value) { return value > 0.f; }) ||
                    std::any_of(sprite.occluders.begin(), sprite.occluders.end(),
                                [&](const auto &occluder) { return inside_authored_occluder(emitter.anchor_radius, occluder); }))
                    throw std::runtime_error("Local emitter cone/range/radiance or exterior anchor is invalid");
                sprite.emitters.push_back(emitter);
            }
        }
        const auto region = result.materials[sprite.material].region_size;
        if (sprite.size != std::array<float, 2>{float(region[0]), float(region[1])} ||
            !std::isfinite(sprite.export_scale) || sprite.export_scale < .001f || sprite.export_scale > 64.f ||
            !std::isfinite(sprite.depth[0]) || !std::isfinite(sprite.depth[1]) ||
            sprite.depth[0] >= sprite.depth[1] || std::abs(sprite.depth[0]) > 8192.f || std::abs(sprite.depth[1]) > 8192.f ||
            !std::isfinite(sprite.pivot[0]) || !std::isfinite(sprite.pivot[1]) ||
            sprite.pivot[0] < 0.f || sprite.pivot[1] < 0.f ||
            sprite.pivot[0] > sprite.size[0] || sprite.pivot[1] > sprite.size[1])
            throw std::runtime_error("Invalid lit sprite source geometry, pivot, export scale or depth range");
        result.sprites.push_back(std::move(sprite));
    }
    if (result.textures.empty() || result.materials.empty() || result.sprites.empty())
        throw std::runtime_error("Schema-2 material catalog cannot be empty");
    return result;
}
} // namespace
struct GameArt::Impl {
    struct Sprite {
        render::TextureHandle texture;
        render::Rect uv;
        float aspect;
        render::Point pivot;
        float width_scale{1.f};
        render::MaterialHandle source_material{};
    };
    render::Renderer2D &renderer;
    std::vector<render::TextureHandle> textures;
    std::vector<render::MaterialHandle> materials;
    struct ContactArt {
        render::ContactArtHandle handle{};
        render::ContactArtDesc desc{};
    };
    std::vector<ContactArt> contact_arts;
    struct LitSprite {
        render::MaterialHandle material;
        std::array<float, 2> size{}, pivot{}, depth{};
        float export_scale{};
        std::vector<LitSpriteSource::Occluder> occluders;
        std::vector<LitSpriteSource::SkyCaster> sky_casters;
        std::vector<render::LightGpu> emitters;
        std::vector<LitSpriteSource::Contact> contacts;
    };
    std::unordered_map<std::string, LitSprite> lit_sprites;
    std::unordered_map<std::string, SDL_Cursor *> cursors;
    std::unordered_map<std::string, Sprite> sprites;
    std::unordered_map<std::string, Sprite> outline_sprites;
    using HitMask = SpriteHitMask;
    std::unordered_map<std::string, HitMask> hit_masks;
    std::size_t outline_bytes{};
    std::size_t hit_mask_bytes{};
    GameArtCacheLimits cache_limits{};
    explicit Impl(render::Renderer2D &r) : renderer(r) {}
    ~Impl() {
        for (const auto &art : contact_arts) {
            try { renderer.release_contact_art(art.handle); }
            catch (const std::exception &error) {
                // The immutable registry still owns this live slot. Renderer
                // shutdown retires it; a destructor must never retry or throw.
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Contact art retained until renderer shutdown: %s", error.what());
            }
            catch (...) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Contact art retained until renderer shutdown after an unknown release failure");
            }
        }
        for (auto [id,cursor] : cursors) {
            if (SDL_GetCursor() == cursor) SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_DestroyCursor(cursor);
        }
        for (auto m : materials)
            renderer.release_sprite_material(m);
        for (auto t : textures)
            renderer.release_texture(t);
    }

    void quad(render::Frame2D &f, const Sprite &s, const std::array<render::Point, 4> &p, render::Pass pass,
              render::Color tint) const {
        const auto v = static_cast<std::uint32_t>(f.vertices.size());
        const auto first = static_cast<std::uint32_t>(f.indices.size());
        const auto &u = s.uv;
        const std::array<render::Point, 4> uv{
            {{u.x, u.y}, {u.x + u.w, u.y}, {u.x + u.w, u.y + u.h}, {u.x, u.y + u.h}}};
        for (std::size_t i = 0; i < 4; ++i)
            f.vertices.push_back({p[i], uv[i], tint});
        f.indices.insert(f.indices.end(), {v, v + 1, v + 2, v, v + 2, v + 3});
        if (!f.commands.empty()) {
            if (auto *previous = std::get_if<render::GeometryDraw>(&f.commands.back());
                previous && previous->texture == s.texture && previous->pass == pass &&
                previous->blend == render::Blend::Alpha && !previous->scissor_enabled &&
                previous->sprite_material_index == 0 &&
                previous->first_index + previous->index_count == first) {
                previous->index_count += 6;
                return;
            }
        }
        f.commands.emplace_back(render::GeometryDraw{pass, render::Blend::Alpha, s.texture, first, 6});
    }
};
GameArt::GameArt(render::Renderer2D &r) : impl_(std::make_unique<Impl>(r)) {}
GameArt::~GameArt() = default;
bool GameArt::ready() const {
    return !impl_->sprites.empty();
}
bool GameArt::contains(std::string_view id) const {
    return impl_->sprites.contains(std::string(id));
}
bool GameArt::contains_lit(std::string_view id) const {
    return impl_->lit_sprites.contains(std::string(id));
}
void GameArt::lit_sprite(render::Frame2D &frame, std::string_view id, render::Point ground,
                         const std::array<float, 3> &anchor, float zoom,
                         float scale, float wetness, std::uint32_t receiver_owner,
                         float emission_scale, float opacity) const {
    if (!frame.linear_hdr_world || !std::isfinite(ground.x) || !std::isfinite(ground.y) ||
        !std::all_of(anchor.begin(), anchor.end(), [](float v) { return std::isfinite(v); }) ||
        !std::isfinite(zoom) || zoom <= 0.f || !std::isfinite(scale) || scale <= 0.f ||
        !std::isfinite(wetness) || wetness < 0.f || wetness > 1.f ||
        !std::isfinite(emission_scale) || emission_scale < 0.f || emission_scale > 1.f ||
        !std::isfinite(opacity) || opacity < 0.f || opacity > 1.f)
        throw std::invalid_argument("Lit sprite requires HDR and finite render anchor/zoom/scale/wetness/emission/opacity");
    const auto &sprite = impl_->lit_sprites.at(std::string(id));
    const auto bounds = project_lit_sprite(sprite.size,sprite.pivot,sprite.export_scale,ground,zoom,scale);
    const auto alpha = static_cast<std::uint8_t>(std::lround(opacity * 255.f));
    if (!alpha) return;
    const float width = bounds.w, height = bounds.h, left = bounds.x, top = bounds.y;
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    render::SpriteMaterialDraw material;
    material.material = sprite.material;
    material.surface.anchor_export = {anchor[0], anchor[1], anchor[2], sprite.export_scale};
    material.surface.source_size_pivot = {sprite.size[0], sprite.size[1], sprite.pivot[0], sprite.pivot[1]};
    material.surface.depth_scale = {sprite.depth[0], sprite.depth[1], scale, 0.f};
    material.surface.response = {wetness, .08f, 0.f, 0.f};
    material.receiver_owner = receiver_owner;
    material.emission_scale = emission_scale;
    frame.sprite_material_draws.push_back(material);
    const render::Color white{255, 255, 255, alpha};
    frame.vertices.insert(frame.vertices.end(), {
        {{left, top}, {0, 0}, white}, {{left + width, top}, {1, 0}, white},
        {{left + width, top + height}, {1, 1}, white}, {{left, top + height}, {0, 1}, white}});
    frame.indices.insert(frame.indices.end(), {first_vertex, first_vertex + 1, first_vertex + 2,
                                              first_vertex, first_vertex + 2, first_vertex + 3});
    render::GeometryDraw draw{render::Pass::WorldOrdered, render::Blend::Alpha, {}, first_index, 6};
    draw.sprite_material_index = static_cast<std::uint32_t>(frame.sprite_material_draws.size());
    frame.commands.emplace_back(draw);
}
render::Rect GameArt::lit_sprite_bounds(std::string_view id, render::Point ground,
                                       float zoom, float scale) const {
    const auto& sprite = impl_->lit_sprites.at(std::string(id));
    return project_lit_sprite(sprite.size,sprite.pivot,sprite.export_scale,ground,zoom,scale);
}
bool GameArt::hit_test_lit_sprite(std::string_view id, render::Point ground,
                                 float zoom, render::Point point, float scale) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
        throw std::invalid_argument("Lit sprite hit-test point must be finite");
    const std::string key(id);
    const auto& sprite = impl_->lit_sprites.at(key);
    const auto bounds = project_lit_sprite(sprite.size,sprite.pivot,sprite.export_scale,ground,zoom,scale);
    return inside_sprite(bounds,point) && sample_hit_mask(impl_->hit_masks.at(key),bounds,point);
}
void GameArt::outline_lit_sprite(render::Frame2D &frame, std::string_view id,
                                 render::Point ground, float zoom, float scale,
                                 float radius, render::Pass pass, render::Color tint) const {
    const std::string key(id);
    const auto& sprite = impl_->lit_sprites.at(key);
    if (pass != render::Pass::WorldOrdered || frame.content != render::FrameContent::World ||
        !std::isfinite(radius) || radius < 0.f)
        throw std::invalid_argument("Lit outline requires a world-ordered World frame and finite nonnegative radius");
    (void)project_lit_sprite(sprite.size,sprite.pivot,sprite.export_scale,ground,zoom,scale);
    constexpr std::array<render::Point,8> directions{{{-1,-1},{0,-1},{1,-1},{1,0},
                                                    {1,1},{0,1},{-1,1},{-1,0}}};
    std::array<render::Vertex,32> vertices{};
    constexpr std::array<render::Point,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    for (std::size_t offset = 0; offset < directions.size(); ++offset) {
        const auto bounds = project_lit_sprite(sprite.size,sprite.pivot,sprite.export_scale,
            {ground.x+directions[offset].x*radius,ground.y+directions[offset].y*radius},zoom,scale);
        const std::array<render::Point,4> positions{{{bounds.x,bounds.y},{bounds.x+bounds.w,bounds.y},
            {bounds.x+bounds.w,bounds.y+bounds.h},{bounds.x,bounds.y+bounds.h}}};
        for (std::size_t corner = 0; corner < positions.size(); ++corner)
            vertices[offset*4u+corner] = {positions[corner],uv[corner],tint};
    }
    if (!frame.surface_vertices.empty() && frame.surface_vertices.size() != frame.vertices.size())
        throw std::invalid_argument("Lit outline surface prefix must be vertex-aligned");
    if (frame.surface_vertices.empty() && std::any_of(frame.commands.begin(),frame.commands.end(),[](const auto& command) {
            const auto* draw = std::get_if<render::GeometryDraw>(&command);
            return draw && (draw->surface_lit || draw->ground_material_index);
        }))
        throw std::invalid_argument("Lit outline cannot synthesize an authored ground/surface prefix");
    if (!tint.a || radius == 0.f) return;
    if (frame.vertices.size() > std::numeric_limits<std::uint32_t>::max()-vertices.size() ||
        frame.indices.size() > std::numeric_limits<std::uint32_t>::max()-48u)
        throw std::length_error("Lit outline frame indices exceed uint32 capacity");
    const auto first_vertex = static_cast<std::uint32_t>(frame.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(frame.indices.size());
    std::array<std::uint32_t,48> indices{};
    for (std::uint32_t offset = 0; offset < 8u; ++offset) {
        const auto v = first_vertex+offset*4u;
        const std::array<std::uint32_t,6> quad{v,v+1u,v+2u,v,v+2u,v+3u};
        std::copy(quad.begin(),quad.end(),indices.begin()+offset*6u);
    }
    const bool companions = frame.linear_hdr_world || !frame.surface_vertices.empty();
    static_assert(std::is_nothrow_copy_constructible_v<render::Vertex> &&
                  std::is_nothrow_copy_constructible_v<render::GeometryDraw> &&
                  std::is_nothrow_copy_constructible_v<render::TextureHandle> &&
                  std::is_nothrow_default_constructible_v<render::SurfaceVertex> &&
                  std::is_nothrow_constructible_v<std::variant<render::GeometryDraw,render::TextDraw>,render::GeometryDraw>);
    // Capacity changes are not published frame payload. No allocating append
    // remains after the cache node is transferred and the owned handle recorded.
    frame.vertices.reserve(frame.vertices.size()+vertices.size());
    frame.indices.reserve(frame.indices.size()+indices.size());
    frame.commands.reserve(frame.commands.size()+1u);
    if (companions) frame.surface_vertices.reserve(frame.vertices.size()+vertices.size());
    auto cached = impl_->outline_sprites.find(key);
    if (cached == impl_->outline_sprites.end()) {
        const auto& mask = impl_->hit_masks.at(key);
        const auto bytes = mask.coverage.size()*4u;
        if (impl_->outline_bytes > impl_->cache_limits.outline_texture_bytes ||
            bytes > impl_->cache_limits.outline_texture_bytes-impl_->outline_bytes)
            throw std::runtime_error("Lit outline texture budget exceeded");
        std::vector<std::uint8_t> rgba(bytes,255u);
        for (std::size_t pixel = 0; pixel < mask.coverage.size(); ++pixel)
            rgba[pixel*4u+3u] = mask.coverage[pixel];
        // Allocate the node before uploading. Node transfer uses the same
        // allocator; reserving both containers precedes live publication.
        decltype(impl_->outline_sprites) staged;
        staged.emplace(key,Impl::Sprite{{},{0,0,1,1},sprite.size[1]/sprite.size[0],{},1.f,sprite.material});
        auto node = staged.extract(key);
        impl_->outline_sprites.reserve(impl_->outline_sprites.size()+1u);
        impl_->textures.reserve(impl_->textures.size()+1u);
        const auto handle = impl_->renderer.create_texture({mask.width,mask.height},rgba,render::TextureOptions{1});
        try {
            node.mapped().texture = handle;
            const auto inserted = impl_->outline_sprites.insert(std::move(node));
            if (!inserted.inserted) throw std::logic_error("Lit outline cache identity unexpectedly changed");
            cached = inserted.position;
        } catch (...) {
            impl_->renderer.release_texture(handle);
            throw;
        }
        impl_->textures.push_back(handle);
        impl_->outline_bytes += bytes;
    } else if (cached->second.source_material != sprite.material) {
        throw std::logic_error("Lit outline cache material identity differs from its sprite");
    }
    const auto texture = cached->second.texture;
    frame.vertices.insert(frame.vertices.end(),vertices.begin(),vertices.end());
    frame.indices.insert(frame.indices.end(),indices.begin(),indices.end());
    if (companions) frame.surface_vertices.resize(frame.vertices.size());
    if (!frame.commands.empty()) {
        auto* previous = std::get_if<render::GeometryDraw>(&frame.commands.back());
        if (previous && previous->texture == texture && previous->pass == pass &&
            previous->blend == render::Blend::Alpha && !previous->scissor_enabled &&
            !previous->sprite_material_index && !previous->ground_material_index && !previous->surface_lit &&
            std::uint64_t(previous->first_index)+previous->index_count == first_index) {
            previous->index_count += static_cast<std::uint32_t>(indices.size());
            return;
        }
    }
    frame.commands.emplace_back(render::GeometryDraw{pass,render::Blend::Alpha,texture,first_index,48u});
}
std::size_t GameArt::append_lit_emitters(render::Frame2D &frame, std::string_view id,
                                       const std::array<float, 3> &anchor, float scale,
                                       std::uint32_t first_stable_id, bool enabled, float reveal) const {
    if (!std::isfinite(reveal) || reveal < 0.f || reveal > 1.f)
        throw std::invalid_argument("Lit emitter reveal must be finite and within [0, 1]");
    if (!enabled || reveal < 1.f) return 0;
    const auto &sprite = impl_->lit_sprites.at(std::string(id));
    const auto count = sprite.emitters.size();
    if (!count) return 0;
    const auto first = frame.surface_lighting.counts[0];
    if (!std::all_of(anchor.begin(), anchor.end(), [](float value) { return std::isfinite(value); }) ||
        !std::isfinite(scale) || scale <= 0.f || !first_stable_id ||
        first > render::surface_light_capacity || count > render::surface_light_capacity - first ||
        count > std::size_t(std::numeric_limits<std::uint32_t>::max() - first_stable_id) + 1u)
        throw std::invalid_argument("Lit emitter anchor/scale, identity or capacity is invalid");
    auto staged = frame.surface_lighting;
    for (std::size_t index = 0; index < count; ++index) {
        auto emitter = sprite.emitters[index];
        if (std::any_of(sprite.occluders.begin(), sprite.occluders.end(),
                        [&](const auto &occluder) {
                            return inside_authored_occluder(emitter.anchor_radius, occluder,
                                                           local_emitter_clearance / scale);
                        }))
            throw std::invalid_argument("Lit emitter scaled exterior clearance is below 0.002");
        const auto stable_id = first_stable_id + static_cast<std::uint32_t>(index);
        for (std::uint32_t previous = 0; previous < staged.counts[0]; ++previous)
            if (staged.lights[previous].occluder_range_flags[3] == stable_id)
                throw std::invalid_argument("Duplicate local emitter stable ID");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            emitter.anchor_radius[axis] = anchor[axis] + emitter.anchor_radius[axis] * scale;
            if (!std::isfinite(emitter.anchor_radius[axis]) || std::abs(emitter.anchor_radius[axis]) > 32.f)
                throw std::invalid_argument("Lit emitter transformed anchor exceeds [-32, 32]");
        }
        emitter.anchor_radius[3] *= scale;
        if (!std::isfinite(emitter.anchor_radius[3]) || emitter.anchor_radius[3] <= 0.f || emitter.anchor_radius[3] > 32.f)
            throw std::invalid_argument("Lit emitter transformed radius must be within (0, 32]");
        emitter.occluder_range_flags[3] = stable_id;
        emitter.height_falloff_reveal[2] = reveal;
        staged.lights[staged.counts[0]++] = emitter;
    }
    frame.surface_lighting = staged;
    return count;
}
std::size_t GameArt::append_lit_occluders(render::Frame2D &frame, std::string_view id,
                                          const std::array<float, 3> &anchor, float scale,
                                          std::uint32_t owner_id, std::uint32_t first_stable_id,
                                          float reveal) const {
    const auto &sprite = impl_->lit_sprites.at(std::string(id));
    if (!std::all_of(anchor.begin(), anchor.end(), [](float value) { return std::isfinite(value); }) ||
        !std::isfinite(scale) || scale <= 0.f || !std::isfinite(reveal) || reveal < 0.f || reveal > 1.f)
        throw std::invalid_argument("Lit occluders require finite anchor/positive scale, owner and stable ID");
    // Filtering precedes budgets/identity checks: an invisible instance cannot
    // consume space or trigger a visible-frame failure through a duplicate id.
    if (reveal < 1.f) return 0;
    if (!owner_id || !first_stable_id)
        throw std::invalid_argument("Lit occluders require finite anchor/positive scale, owner and stable ID");
    const auto count = sprite.occluders.size();
    if (frame.local_occluders.size() > render::local_occluder_capacity ||
        count > render::local_occluder_capacity - frame.local_occluders.size() ||
        count > std::size_t(std::numeric_limits<std::uint32_t>::max() - first_stable_id) + 1u)
        throw std::invalid_argument("Lit occluder capacity or stable ID range overflow");
    for (std::size_t index = 0; index < count; ++index) {
        const auto stable_id = first_stable_id + static_cast<std::uint32_t>(index);
        if (std::any_of(frame.local_occluders.begin(), frame.local_occluders.end(),
                        [stable_id](const render::OccluderGpu &row) {
                            return row.shape_flags_id[2] == stable_id;
                        }))
            throw std::invalid_argument("Duplicate local occluder stable ID");
    }
    std::vector<render::OccluderGpu> staged;
    staged.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto &source = sprite.occluders[index];
        render::OccluderGpu row;
        row.center_base = {anchor[0] + source.center[0] * scale,
                           anchor[1] + source.center[1] * scale, 0.f,
                           anchor[2] + source.base_z * scale};
        row.axis_x_extent = {source.axis_x[0], source.axis_x[1], source.half_x * scale,
                             anchor[2] + source.top_z * scale};
        row.axis_y_radius = {source.axis_y[0], source.axis_y[1], source.half_y * scale,
                             source.radius * scale};
        row.shape_flags_id = {static_cast<std::uint32_t>(source.shape), 1u,
                              first_stable_id + static_cast<std::uint32_t>(index), owner_id};
        if (!std::all_of(row.center_base.begin(), row.center_base.end(), [](float v) { return std::isfinite(v); }) ||
            !std::all_of(row.axis_x_extent.begin(), row.axis_x_extent.end(), [](float v) { return std::isfinite(v); }) ||
            !std::all_of(row.axis_y_radius.begin(), row.axis_y_radius.end(), [](float v) { return std::isfinite(v); }))
            throw std::invalid_argument("Lit occluder transformed geometry overflow");
        if (row.axis_x_extent[3]-row.center_base[3] < render::local_occluder_min_dimension ||
            (source.shape == render::OccluderShape::Obb &&
             (row.axis_x_extent[2] < render::local_occluder_min_dimension ||
              row.axis_y_radius[2] < render::local_occluder_min_dimension)) ||
            (source.shape == render::OccluderShape::Capsule &&
             row.axis_y_radius[3] < render::local_occluder_min_dimension))
            throw std::invalid_argument("Lit occluder transformed dimensions must be at least 0.0001");
        staged.push_back(row);
    }
    frame.local_occluders.insert(frame.local_occluders.end(), staged.begin(), staged.end());
    return staged.size();
}
std::size_t GameArt::append_lit_contacts(render::Frame2D &frame, std::string_view id,
                                       const std::array<float, 3> &anchor, float scale,
                                       const std::array<float, 3> &axis_u,
                                       const std::array<float, 3> &axis_v,
                                       std::uint32_t owner_id, std::uint32_t first_stable_id,
                                       float reveal, float opacity_scale) const {
    if (!std::isfinite(reveal) || reveal < 0.f || reveal > 1.f ||
        !std::isfinite(opacity_scale) || opacity_scale < 0.f || opacity_scale > 1.f)
        throw std::invalid_argument("Lit contact reveal/opacity must be finite and within [0, 1]");
    if (reveal < 1.f || opacity_scale == 0.f) return 0;
    const auto &sprite = impl_->lit_sprites.at(std::string(id));
    const auto count = static_cast<std::size_t>(std::count_if(
        sprite.contacts.begin(), sprite.contacts.end(), [](const auto &source) { return source.opacity > 0.f; }));
    if (!count) return 0;
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(anchor.begin(), anchor.end(), finite) ||
        !std::all_of(axis_u.begin(), axis_u.end(), finite) ||
        !std::all_of(axis_v.begin(), axis_v.end(), finite) ||
        !std::isfinite(scale) || scale <= 0.f || !owner_id || !first_stable_id ||
        frame.contact_shadows.size() > render::contact_shadow_capacity ||
        count > render::contact_shadow_capacity - frame.contact_shadows.size() ||
        count > std::size_t(std::numeric_limits<std::uint32_t>::max() - first_stable_id) + 1u)
        throw std::invalid_argument("Lit contact transform, identity or capacity is invalid");
    auto staged = frame.contact_shadows;
    staged.reserve(staged.size() + count);
    std::uint32_t active_index{};
    for (const auto &source : sprite.contacts) {
        if (source.opacity == 0.f) continue;
        const auto &art = impl_->contact_arts.at(source.art);
        render::ContactShadowDraw draw;
        draw.art = art.handle;
        draw.facing = art.desc.facing;
        draw.owner_id = owner_id;
        draw.stable_id = first_stable_id + active_index++;
        draw.surface.anchor_opacity[3] = source.opacity * opacity_scale;
        for (std::size_t component = 0; component < 3; ++component) {
            draw.surface.anchor_opacity[component] = anchor[component] + scale *
                (axis_u[component] * source.anchor[0] + axis_v[component] * source.anchor[1]);
            draw.surface.axis_u_extent[component] = axis_u[component];
            draw.surface.axis_v_extent[component] = axis_v[component];
        }
        draw.surface.axis_u_extent[3] = source.half_extent[0] * scale;
        draw.surface.axis_v_extent[3] = source.half_extent[1] * scale;
        draw.surface.uv_region = render::contact_mask_uv(art.desc.region);
        draw.surface.pivot_plane = {source.pivot[0], source.pivot[1], source.plane_tolerance, 0.f};
        staged.push_back(draw);
    }
    render::validate_contact_shadows(staged);
    frame.contact_shadows.swap(staged);
    return count;
}
std::size_t GameArt::append_lit_sky_casters(render::Frame2D &frame, std::string_view id,
                                             const std::array<float, 3> &world_anchor,
                                             float geometry_scale,
                                             const std::array<float, 3> &camera_world,
                                             float reveal) const {
    const auto &sprite = impl_->lit_sprites.at(std::string(id));
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(world_anchor.begin(), world_anchor.end(), finite) ||
        !std::all_of(camera_world.begin(), camera_world.end(), finite) ||
        !std::isfinite(geometry_scale) || geometry_scale <= 0.f ||
        !std::isfinite(reveal) || reveal < 0.f || reveal > 1.f)
        throw std::invalid_argument(
            "Lit sky casters require finite anchors, positive scale and reveal within [0,1]");
    if (reveal == 0.f) return 0;
    const auto count = sprite.sky_casters.size();
    if (frame.sky_shadow_triangles.size() > render::sky_shadow_triangle_capacity ||
        count > render::sky_shadow_triangle_capacity - frame.sky_shadow_triangles.size())
        throw std::invalid_argument("Lit sky caster raw triangle capacity exceeded");

    std::vector<render::SkyShadowTriangle> staged;
    staged.reserve(count);
    for (const auto &source : sprite.sky_casters) {
        render::SkyShadowTriangle triangle;
        triangle.source_mask = source.source_mask;
        triangle.reveal = reveal;
        for (std::size_t vertex = 0; vertex < triangle.vertices.size(); ++vertex) {
            std::array<float, 3> world{};
            for (std::size_t axis = 0; axis < world.size(); ++axis)
                world[axis] = world_anchor[axis] + source.vertices[vertex][axis] * geometry_scale;
            triangle.vertices[vertex].position =
                render::camera_relative_render_xyz(world, camera_world);
        }
        staged.push_back(triangle);
    }

    // The production validator is the final independent guard for the complete
    // prospective frame: transformed bounds/area, authored masks and both
    // per-source active capacities. Disabled but valid lights avoid producing
    // throwaway raster vertices while retaining every CPU contract check.
    auto prospective = frame.sky_shadow_triangles;
    prospective.insert(prospective.end(), staged.begin(), staged.end());
    const render::SurfaceLightingFrame disabled_sun{};
    const render::MoonLightingGpu disabled_moon{};
    (void)render::prepare_sky_shadows(prospective, disabled_sun, disabled_moon);

    frame.sky_shadow_triangles.insert(frame.sky_shadow_triangles.end(),
                                      staged.begin(), staged.end());
    return staged.size();
}
render::Rect GameArt::sprite_bounds(std::string_view id, render::Point p, float width) const {
    const auto &s=impl_->sprites.at(std::string(id));
    width *= s.width_scale;
    const float height=width*s.aspect;
    return {p.x-width*s.pivot.x,p.y-height*s.pivot.y,width,height};
}
bool GameArt::hit_test_sprite(std::string_view id, render::Point ground, float width,
                              render::Point point, bool flip_x) const {
    const auto bounds=sprite_bounds(id,ground,width);
    if (bounds.w<=0 || bounds.h<=0 || point.x<bounds.x || point.y<bounds.y ||
        point.x>=bounds.x+bounds.w || point.y>=bounds.y+bounds.h) return false;
    const auto mask=impl_->hit_masks.find(std::string(id));
    if (mask==impl_->hit_masks.end()) return true;
    return sample_hit_mask(mask->second,bounds,point,flip_x);
}
void GameArt::load(const AssetCatalog &assets) {
    load(assets,{});
}
void GameArt::load(const AssetCatalog &assets, GameArtCacheLimits limits) {
    const GameArtCacheLimits maximum;
    if (limits.hit_mask_bytes > maximum.hit_mask_bytes ||
        limits.outline_texture_bytes > maximum.outline_texture_bytes)
        throw std::invalid_argument("Game art cache caps exceed retained memory limits");
    const auto outline_memory_budget = limits.outline_texture_bytes;
    const auto hit_mask_memory_budget = limits.hit_mask_bytes;
    auto candidate = std::make_unique<Impl>(impl_->renderer);
    candidate->cache_limits = limits;
    const auto cache_hit_mask = [&](const std::string& id, std::span<const std::uint8_t> rgba,
                                   render::Extent source_size, const std::array<int,4>& rect) -> const Impl::HitMask& {
        if (candidate->hit_mask_bytes > hit_mask_memory_budget)
            throw std::runtime_error("Sprite hit-mask memory budget exceeded");
        auto mask = make_hit_mask(rgba,source_size,rect,hit_mask_memory_budget-candidate->hit_mask_bytes);
        const auto [it,inserted] = candidate->hit_masks.emplace(id,std::move(mask));
        if (!inserted) throw std::runtime_error("Duplicate sprite hit-mask ID");
        candidate->hit_mask_bytes += it->second.coverage.size();
        return it->second;
    };
    const auto bytes = assets.read_path("art/world.json");
    const auto doc = nlohmann::json::parse(bytes);
    const auto schema = doc.at("schema").get<int>();
    if (schema != 1 && schema != 2)
        throw std::runtime_error("Unsupported world art schema");
    MaterialCatalogSource material_source;
    if ((schema == 2 || !impl_->materials.empty()) && candidate->renderer.has_submitted_frames())
        throw std::runtime_error("Schema-2 installation is startup-only until fence-safe catalog retirement exists");
    if (schema == 2) {
        if (!candidate->renderer.world_hdr_status().available)
            throw std::runtime_error("Schema-2 materials require an available linear HDR world path: " +
                                     candidate->renderer.world_hdr_status().reason);
        material_source = parse_material_catalog(doc, assets);
        for (const auto &page : doc.at("pages"))
            for (const auto &entry : page.at("sprites"))
                for (const auto &sprite : material_source.sprites)
                    if (entry.at("id").get<std::string>() == sprite.id)
                        throw std::runtime_error("BakedCompat and Dynamic sprite IDs must not overlap");
        candidate->textures.reserve(material_source.textures.size() + doc.at("pages").size() * 2);
        candidate->materials.reserve(material_source.materials.size());
        candidate->contact_arts.reserve(material_source.contacts.size());
    }
    for (const auto &page : doc.at("pages")) {
        const auto encoded = assets.read_path(page.at("path").get<std::string>());
        png_image im{};
        im.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_memory(&im, encoded.data(), encoded.size()))
            throw std::runtime_error("Cannot decode world art PNG");
        struct Cleanup {
            png_image *im;
            ~Cleanup() { png_image_free(im); }
        } cleanup{&im};
        if (!im.width || !im.height || im.width > 4096 || im.height > 4096)
            throw std::runtime_error("World art dimensions exceed budget");
        im.format = PNG_FORMAT_RGBA;
        std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(im));
        if (!png_image_finish_read(&im, nullptr, pixels.data(), 0, nullptr))
            throw std::runtime_error("Cannot read world art pixels");
        const auto alpha_mode = page.value("alpha_mode", std::string{});
        const bool foliage_despill = page.value("foliage_despill",false);
        if (!alpha_mode.empty() && alpha_mode != "magenta" && alpha_mode != "luminance")
            throw std::runtime_error("Unsupported world art alpha mode");
        if (!alpha_mode.empty())
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                if (alpha_mode == "magenta") decode_art_magenta(pixels.data() + i,foliage_despill);
                else decode_art_luminance(pixels.data() + i);
            }
        // Fog assets are alpha masks. White RGB lets the renderer color the mist.
        for (const auto &entry : page.at("sprites")) {
            if (!entry.at("id").get<std::string>().starts_with("fog-masks."))
                continue;
            const auto rect = entry.at("rect").get<std::array<int, 4>>();
            if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 ||
                std::int64_t(rect[0]) + rect[2] > im.width || std::int64_t(rect[1]) + rect[3] > im.height)
                throw std::runtime_error("Invalid fog rectangle");
            for (int y = rect[1]; y < rect[1] + rect[3]; ++y)
                for (int x = rect[0]; x < rect[0] + rect[2]; ++x) {
                    const auto i = (std::size_t(y) * im.width + x) * 4;
                    pixels[i] = pixels[i + 1] = pixels[i + 2] = 255;
                }
        }
        const auto texture = candidate->renderer.create_texture(
            {im.width, im.height}, pixels, render::TextureOptions{4});
        candidate->textures.push_back(texture);
        for (const auto &entry : page.at("sprites")) {
            const auto rect = entry.at("rect").get<std::array<int, 4>>();
            const auto pivot = entry.at("pivot").get<std::array<float, 2>>();
            if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 ||
                std::int64_t(rect[0]) + rect[2] > im.width || std::int64_t(rect[1]) + rect[3] > im.height ||
                !std::isfinite(pivot[0]) || !std::isfinite(pivot[1]) || pivot[0] < 0 || pivot[0] > 1 ||
                pivot[1] < 0 || pivot[1] > 1)
                throw std::runtime_error("Invalid world art rectangle or pivot");
            const auto id = entry.at("id").get<std::string>();
            float reference_width = float(rect[2]);
            if (entry.contains("reference_width")) {
                if (!entry.at("reference_width").is_number())
                    throw std::runtime_error("Invalid world art reference width");
                reference_width = entry.at("reference_width").get<float>();
                if (!std::isfinite(reference_width) || reference_width <= 0.f ||
                    reference_width > 4096.f)
                    throw std::runtime_error("Invalid world art reference width");
            }
            if(id == "icons-cursors.24" || id == "icons-cursors.26" || id == "icons-cursors.27") {
                const int width=32, height=std::max(1,int(std::lround(32.f*rect[3]/rect[2])));
                auto *source=SDL_CreateSurfaceFrom(int(im.width),int(im.height),SDL_PIXELFORMAT_RGBA32,pixels.data(),int(im.width)*4);
                auto *surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);
                const SDL_Rect crop{rect[0],rect[1],rect[2],rect[3]};
                if(source && surface) {
                    SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);
                    if(SDL_BlitSurfaceScaled(source,&crop,surface,nullptr,SDL_SCALEMODE_LINEAR)) {
                        auto *cursor=SDL_CreateColorCursor(surface,std::clamp(int(pivot[0]*width),0,width-1),std::clamp(int(pivot[1]*height),0,height-1));
                        if(cursor) candidate->cursors.emplace(id,cursor);
                    }
                }
                SDL_DestroySurface(surface); SDL_DestroySurface(source);
            }
            Impl::Sprite s{texture,
                           {float(rect[0]) / im.width, float(rect[1]) / im.height, float(rect[2]) / im.width,
                            float(rect[3]) / im.height},
                           float(rect[3]) / rect[2],
                           {pivot[0], pivot[1]},
                           float(rect[2]) / reference_width};
            if (!candidate->sprites.emplace(entry.at("id").get<std::string>(), s).second)
                throw std::runtime_error("Duplicate world art sprite ID");
            const bool building_mask = id.starts_with("building.");
            const bool resident_mask = id.starts_with("resident.");
            if (building_mask || resident_mask) {
                const auto& hit_mask = cache_hit_mask(id,pixels,{im.width,im.height},rect);
                // Residents need CPU coverage for painter-order picking, but
                // never allocate the GPU outline texture used by buildings.
                if (building_mask) {
                    const auto mask_bytes = hit_mask.coverage.size()*4u;
                    if (candidate->outline_bytes > outline_memory_budget ||
                        mask_bytes > outline_memory_budget - candidate->outline_bytes)
                        throw std::runtime_error("Building outline texture budget exceeded");
                    std::vector<std::uint8_t> mask(mask_bytes,255u);
                    for (std::size_t pixel = 0; pixel < hit_mask.coverage.size(); ++pixel)
                        mask[pixel*4u+3u] = hit_mask.coverage[pixel];
                    const auto outline_texture = candidate->renderer.create_texture(
                        {hit_mask.width,hit_mask.height}, mask, render::TextureOptions{1});
                    candidate->textures.push_back(outline_texture);
                    candidate->outline_bytes += mask.size();
                    candidate->outline_sprites.emplace(
                        id, Impl::Sprite{outline_texture, {0, 0, 1, 1}, s.aspect, s.pivot,
                                         s.width_scale});
                }
            }
        }
    }
    if (schema == 2) {
        for (const auto& sprite : material_source.sprites) {
            const auto& source = material_source.materials[sprite.material];
            const auto& albedo = material_source.textures[source.textures[0]];
            (void)cache_hit_mask(sprite.id,albedo.bytes,albedo.desc.size,source.albedo_rect);
        }
        std::vector<render::TextureHandle> material_textures;
        material_textures.reserve(material_source.textures.size());
        for (const auto &texture : material_source.textures) {
            const auto handle = candidate->renderer.create_sampled_texture(texture.desc, texture.bytes);
            try { candidate->textures.push_back(handle); }
            catch (...) { candidate->renderer.release_texture(handle); throw; }
            material_textures.push_back(handle);
        }
        for (const auto &source : material_source.materials) {
            render::SpriteMaterialDesc desc;
            for (std::size_t c = 0; c < source.textures.size(); ++c)
                desc.channels[c] = {material_textures[source.textures[c]], source.uv[c]};
            desc.emission = source.emission;
            const auto handle = candidate->renderer.create_sprite_material(desc);
            try { candidate->materials.push_back(handle); }
            catch (...) { candidate->renderer.release_sprite_material(handle); throw; }
        }
        for (const auto &contact : material_source.contacts) {
            const auto handle = candidate->renderer.create_contact_art(contact.desc, contact.bytes);
            // Reserved vector + trivially copied descriptor: owning handle is
            // recorded immediately, with no allocation after registry upload.
            candidate->contact_arts.push_back({handle, contact.desc});
        }
        for (const auto &sprite : material_source.sprites) {
            if (candidate->sprites.contains(sprite.id))
                throw std::runtime_error("BakedCompat and Dynamic sprite IDs must not overlap");
            candidate->lit_sprites.emplace(sprite.id, Impl::LitSprite{
                candidate->materials[sprite.material], sprite.size, sprite.pivot, sprite.depth,
                sprite.export_scale, sprite.occluders, sprite.sky_casters, sprite.emitters, sprite.contacts});
        }
    }
    impl_.swap(candidate);
}
bool GameArt::use_native_cursor(std::string_view id) const {
    const auto it=impl_->cursors.find(std::string(id));
    if(it==impl_->cursors.end()) { SDL_ShowCursor(); return false; }
    if(SDL_GetCursor()!=it->second && !SDL_SetCursor(it->second)) return false;
    return SDL_ShowCursor();
}
void GameArt::sprite(render::Frame2D &f, std::string_view id, render::Point p, float width, render::Pass pass,
                     render::Color tint, bool flip_x) const {
    auto s = impl_->sprites.at(std::string(id));
    if (flip_x) { s.uv.x += s.uv.w; s.uv.w = -s.uv.w; }
    width *= s.width_scale;
    const float height = width * s.aspect, x = p.x - width * s.pivot.x, y = p.y - height * s.pivot.y;
    impl_->quad(f, s, {{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}}, pass, tint);
}
void GameArt::outline_sprite(render::Frame2D &f, std::string_view id, render::Point p, float width,
                             float radius, render::Pass pass, render::Color tint, bool flip_x) const {
    auto s = impl_->outline_sprites.at(std::string(id));
    if (flip_x) { s.uv.x += s.uv.w; s.uv.w = -s.uv.w; }
    width *= s.width_scale;
    const float height = width * s.aspect;
    constexpr std::array<render::Point, 8> directions{{{-1, -1}, {0, -1}, {1, -1}, {1, 0},
                                                       {1, 1}, {0, 1}, {-1, 1}, {-1, 0}}};
    for (const auto direction : directions) {
        const float x = p.x + direction.x * radius - width * s.pivot.x;
        const float y = p.y + direction.y * radius - height * s.pivot.y;
        impl_->quad(f, s, {{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}}, pass, tint);
    }
}
std::size_t GameArt::outline_texture_bytes() const noexcept {
    return impl_->outline_bytes;
}
std::size_t GameArt::outline_sprite_count() const noexcept {
    return impl_->outline_sprites.size();
}
std::size_t GameArt::hit_mask_bytes() const noexcept {
    return impl_->hit_mask_bytes;
}
std::size_t GameArt::hit_mask_count() const noexcept {
    return impl_->hit_masks.size();
}
void GameArt::tile(render::Frame2D &f, std::string_view id, render::Point p, float hw, float hh,
                   render::Pass pass, render::Rect region, render::Color tint) const {
    auto s = impl_->sprites.at(std::string(id));
    s.uv = {s.uv.x + region.x * s.uv.w, s.uv.y + region.y * s.uv.h, s.uv.w * region.w, s.uv.h * region.h};
    impl_->quad(f, s, {{{p.x, p.y - hh}, {p.x + hw, p.y}, {p.x, p.y + hh}, {p.x - hw, p.y}}}, pass, tint);
}
void GameArt::surface(render::Frame2D &f, std::string_view id,
                      const std::array<render::Point, 4> &corners,
                      render::Rect region, render::Color tint) const {
    auto s = impl_->sprites.at(std::string(id));
    s.uv = {s.uv.x + region.x * s.uv.w, s.uv.y + region.y * s.uv.h,
            s.uv.w * region.w, s.uv.h * region.h};
    impl_->quad(f, s, corners, render::Pass::WorldOrdered, tint);
}
void GameArt::surface_colors(render::Frame2D &f, std::string_view id,
                             const std::array<render::Point, 4> &corners, render::Rect region,
                             const std::array<render::Color, 4> &colors) const {
    const auto first = f.vertices.size();
    surface(f, id, corners, region);
    for (std::size_t i = 0; i < 4; ++i) f.vertices[first + i].color = colors[i];
}
} // namespace ribterra

#include "ribterra/seasons.hpp"
#include "ribterra/ecology_appearance.hpp"
#include "ribterra/unit_animation.hpp"
#include "ribterra/resident_art.hpp"
#include "ribterra/unit_motion.hpp"
#include "ribterra/building_catalog.hpp"
#include "ribterra/building_visual.hpp"
#include "ribterra/view.hpp"
#include "ribterra/localization.hpp"
#include "ribterra/minimap_projection.hpp"
#include "ribterra/ground_cover.hpp"
#include "ribterra/shore_details.hpp"
#include "ribterra/relief_details.hpp"
#include "ribterra/resource_shrub.hpp"

#include "ribterra/biomes.hpp"
#include "ribterra/terrain_surface.hpp"
#include "ribterra/terrain_appearance.hpp"
#include "ribterra/client_session.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"
#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <nlohmann/json.hpp>
#include <png.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ribterra {
namespace {

constexpr float pi = 3.1415926535f;

using Col = render::Color;
constexpr Col navy{12, 24, 38, 248}, navy2{20, 38, 55, 245};
constexpr Col gold{215, 166, 76, 255}, cream{238, 226, 197, 255};
constexpr Col green{76, 109, 64, 255}, green2{82, 119, 68, 255};
constexpr Col river_water_color{45, 117, 156, 255};
constexpr Col ground_mist{139, 158, 171, 255};
using Json = nlohmann::json;

bool plain_geometry(const render::GeometryDraw& draw) noexcept {
    return !draw.sprite_material_index && !draw.ground_material_index && !draw.surface_lit;
}

bool hash_string(std::string_view value) noexcept {
    return value.size() == 64u && std::all_of(value.begin(),value.end(),[](char c){
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::string json_hash(const Json& value) {
    const auto text = value.dump()+"\n";
    return sha256(std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}

std::array<int,4> registered_rectangle(const Json& value) {
    if (!value.is_array() || value.size() != 4u)
        throw std::invalid_argument("Registered crop requires four bounded integers");
    std::array<int,4> result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        if (!value[axis].is_number_integer() || value[axis].get<double>() < (axis < 2u ? 0. : 1.) ||
            value[axis].get<double>() > 4096.)
            throw std::invalid_argument("Registered crop requires four bounded integers");
        result[axis] = value[axis].get<int>();
    }
    return result;
}

void exact_fields(const Json& value, std::initializer_list<std::string_view> fields) {
    if (!value.is_object() || value.size() != fields.size())
        throw std::invalid_argument("Neutral material object has missing or extra fields");
    for (const auto field : fields)
        if (!value.contains(std::string(field)))
            throw std::invalid_argument("Neutral material object has missing or extra fields");
}
std::string bounded_text(const Json& value, std::size_t maximum = 4096u) {
    if (!value.is_string()) throw std::invalid_argument("Neutral material text requires UTF-8 strings");
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > maximum ||
        std::any_of(text.begin(),text.end(),[](unsigned char c){return c < 32u || c == 127u;}) ||
        std::all_of(text.begin(),text.end(),[](unsigned char c){return c == ' ';}))
        throw std::invalid_argument("Neutral material text is empty, oversized or contains controls");
    (void)Json(text).dump(); // Reject invalid UTF-8 even for programmatically supplied JSON.
    return text;
}
std::string portable_material_path(const Json& value) {
    auto text = bounded_text(value,1024u);
    if (text.front() == '/' || text.back() == '/' || text.find_first_of("\\:<>\"|?*") != std::string::npos)
        throw std::invalid_argument("Neutral material path must be canonical and relative");
    std::size_t first{};
    while (first < text.size()) {
        const auto last = text.find('/',first);
        const auto component = std::string_view(text).substr(first,last == std::string::npos ? text.size()-first : last-first);
        if (component.empty() || component == "." || component == ".." ||
            component.back() == '.' || component.back() == ' ')
            throw std::invalid_argument("Neutral material path has a noncanonical component");
        std::string stem(component.substr(0,component.find('.')));
        std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c+32u : c); });
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4u && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
            throw std::invalid_argument("Neutral material path contains a reserved device name");
        if (last == std::string::npos) break;
        first = last+1u;
    }
    return text;
}
std::string material_digest(const Json& value) {
    auto text = bounded_text(value,64u);
    if (!hash_string(text)) throw std::invalid_argument("Neutral material digest must be lowercase SHA256");
    return text;
}
std::uint64_t bounded_integer(const Json& value, std::uint64_t maximum, std::uint64_t minimum = 0u) {
    if (!value.is_number_integer() || value.get<double>() < static_cast<double>(minimum) ||
        value.get<double>() > static_cast<double>(maximum))
        throw std::invalid_argument("Neutral material integer is outside its bounded range");
    return value.get<std::uint64_t>();
}
double material_scalar(const Json& value) {
    if (!value.is_number()) throw std::invalid_argument("Neutral material scalar requires a number");
    const auto scalar = value.get<double>();
    if (!std::isfinite(scalar) || scalar < 0. || scalar > 1.)
        throw std::invalid_argument("Neutral material scalar must be finite within [0,1]");
    return scalar;
}
void bounded_pair(const Json& value, double minimum, double maximum) {
    if (!value.is_array() || value.size() != 2u)
        throw std::invalid_argument("Neutral material coordinate pair requires two numbers");
    for (const auto& component : value) {
        if (!component.is_number() || !std::isfinite(component.get<double>()) ||
            component.get<double>() < minimum || component.get<double>() > maximum)
            throw std::invalid_argument("Neutral material coordinate pair is outside its finite bounds");
    }
}
std::uint8_t palette_byte(const Json& value) {
    const double scaled = material_scalar(value)*255.;
    const double lower = std::floor(scaled), remainder = scaled-lower;
    const double rounded = lower + (remainder > .5 || (remainder == .5 && std::fmod(lower,2.) != 0.) ? 1. : 0.);
    return static_cast<std::uint8_t>(rounded); // Python binary64 round: .30 -> 76.
}
void validate_provenance(const Json& value, bool original) {
    if (original) exact_fields(value,{"license","author","source","notice_path","notice_sha256","asset_source_sha256"});
    else exact_fields(value,{"license","author","source","notice_path","notice_sha256"});
    for (const auto field : {"license","author","source"}) (void)bounded_text(value.at(field),512u);
    (void)portable_material_path(value.at("notice_path"));
    (void)material_digest(value.at("notice_sha256"));
    if (original) (void)material_digest(value.at("asset_source_sha256"));
}
void validate_geometry_provenance(const Json& value) {
    exact_fields(value,{"kind","author"});
    if (value.at("kind") != "authored-proxy-xyz-v1")
        throw std::invalid_argument("Neutral geometry provenance kind is unsupported");
    (void)bounded_text(value.at("author"),512u);
}
std::string canonical_material_id(const Json& value) {
    auto id = bounded_text(value,192u);
    if (id.front() < 'a' || id.front() > 'z' ||
        !std::all_of(id.begin(),id.end(),[](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        }))
        throw std::invalid_argument("Neutral material/triangle ID must be canonical lowercase ASCII");
    return id;
}
std::string triangle_identifier(const Json& value) {
    if (!value.is_string()) throw std::invalid_argument("Neutral triangle ID requires a Unicode string");
    auto id = value.get<std::string>();
    if (id.empty() || id.size() > 256u*4u)
        throw std::invalid_argument("Neutral triangle ID requires 1..256 Unicode characters");
    (void)Json(id).dump(); // Validate UTF-8 before counting leading codepoint bytes.
    const auto characters = std::count_if(id.begin(),id.end(),[](unsigned char c) { return (c & 0xc0u) != 0x80u; });
    if (characters > 256) throw std::invalid_argument("Neutral triangle ID exceeds 256 Unicode characters");
    return id;
}
struct MaterialPathLess {
    bool operator()(const std::string& left, const std::string& right) const {
#ifdef _WIN32
        const auto wide = [](const std::string& text) {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size())).native();
        };
        const auto lhs = wide(left), rhs = wide(right);
        const auto order = CompareStringOrdinal(lhs.c_str(),-1,rhs.c_str(),-1,TRUE);
        if (!order) throw std::invalid_argument("Neutral path ordinal comparison failed");
        return order == CSTR_LESS_THAN;
#else
        const auto fold = [](std::string text) {
            std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c+32u : c); });
            return text;
        };
        return fold(left) < fold(right);
#endif
    }
};
void validate_references(const Json& rows) {
    if (!rows.is_array() || rows.size() > 64u) throw std::invalid_argument("Neutral references exceed 64 rows");
    std::set<std::string,MaterialPathLess> paths;
    for (const auto& row : rows) {
        exact_fields(row,{"path","sha256","purpose"});
        if (row.at("purpose") != "visual-only" || !paths.emplace(portable_material_path(row.at("path"))).second)
            throw std::invalid_argument("Neutral reference scope/path is invalid or duplicated");
        (void)material_digest(row.at("sha256"));
    }
}
void validate_bindings(const Json& rows, bool runtime) {
    if (!rows.is_array() || rows.size() > 64u) throw std::invalid_argument("Neutral source bindings exceed 64 rows");
    std::set<std::string,MaterialPathLess> paths;
    for (const auto& row : rows) {
        if (runtime) exact_fields(row,{"relative_path","bytes","sha256"});
        else exact_fields(row,{"path","bytes","sha256"});
        if (!paths.emplace(portable_material_path(row.at(runtime ? "relative_path" : "path"))).second)
            throw std::invalid_argument("Neutral source binding path is duplicated");
        (void)bounded_integer(row.at("bytes"),128u*1024u*1024u);
        (void)material_digest(row.at("sha256"));
    }
}
std::filesystem::path material_root(const AssetCatalog& assets) {
    return assets.resolve("art/world.json").parent_path().parent_path();
}
void reject_reparse(const std::filesystem::path& path) {
    for (auto component = path; !component.empty(); ) {
        const auto status = std::filesystem::symlink_status(component);
        if (std::filesystem::is_symlink(status)) throw std::invalid_argument("Neutral material read rejects symlinks");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(component.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("Neutral material read rejects absent/reparse paths");
#endif
        const auto parent = component.parent_path();
        if (parent == component) break;
        component = parent;
    }
}
std::vector<std::uint8_t> bounded_material_file(const AssetCatalog& assets, const Json& relative,
                                               std::size_t maximum, const std::string& digest) {
    const auto name = portable_material_path(relative);
    const auto path = material_root(assets)/std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
    reject_reparse(path);
    if (!std::filesystem::is_regular_file(path)) throw std::invalid_argument("Neutral material input must be a regular file");
    const auto size = std::filesystem::file_size(path);
    if (!size || size > maximum) throw std::invalid_argument("Neutral material input exceeds its byte limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())) ||
        stream.peek() != std::char_traits<char>::eof() || std::filesystem::file_size(path) != size || sha256(bytes) != digest)
        throw std::invalid_argument("Neutral material input changed or differs from its declared SHA256");
    reject_reparse(path);
    return bytes;
}
void validate_notice(const AssetCatalog& assets, const Json& provenance) {
    const auto bytes = bounded_material_file(assets,provenance.at("notice_path"),128u*1024u,
        material_digest(provenance.at("notice_sha256")));
    const std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if (std::any_of(text.begin(),text.end(),[](unsigned char c){return (c < 32u && c != '\n' && c != '\r' && c != '\t') || c == 127u;}) ||
        std::all_of(text.begin(),text.end(),[](unsigned char c){return c == ' ' || c == '\n' || c == '\r' || c == '\t';}))
        throw std::invalid_argument("Neutral notice requires nonempty UTF-8 text without binary controls");
    (void)Json(text).dump();
}
Json strict_material_json(std::span<const std::uint8_t> bytes) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t event,Json& parsed) {
        if (depth > 32) throw std::invalid_argument("Neutral material JSON nesting exceeds its bound");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.back().emplace(parsed.get<std::string>()).second)
            throw std::invalid_argument("Neutral material JSON contains duplicate object fields");
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}

void bounded_palette_strings(const Json& value) {
    if (value.is_string()) (void)bounded_text(value);
    else if (value.is_array() || value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (value.is_object()) (void)bounded_text(Json(it.key()));
            // Triangle names retain the source master's identifier contract,
            // including JSON-escaped characters; they are never file paths.
            if (value.is_object() && it.key() == "triangle_id") {
                (void)triangle_identifier(it.value());
                continue;
            }
            bounded_palette_strings(it.value());
        }
    }
}
struct NeutralPaletteMaterial {
    std::array<std::uint8_t,3> rgb{};
    std::uint8_t roughness{}, emission{}, wet{}, caster{};
    std::shared_ptr<const std::vector<std::uint8_t>> rgb_membership;
};
struct TexturedSourceCache {
    struct Source { std::string path; Json identity; std::shared_ptr<const std::vector<std::uint8_t>> bytes; };
    std::map<std::string,Source,MaterialPathLess> sources;
    std::uint64_t bytes{};
};
constexpr std::size_t rgb24_membership_bytes = (1u<<24u)/8u;
std::map<std::string,std::shared_ptr<const std::vector<std::uint8_t>>> validate_textured_sources(
    const AssetCatalog& assets,const Json& item,const Json& palette,TexturedSourceCache& cache) {
    if (assets.read_profile() != AssetCatalogReadProfile::MaterialCollectionTextured)
        throw std::invalid_argument("Textured material collection requires the explicit MaterialCollectionTextured read profile");
    const auto& rows = palette.at("textures");
    const auto& bindings = item.at("albedo_source").at("source_albedo");
    if (!rows.is_array() || rows.size() > 4u || !bindings.is_array() || bindings.size() != rows.size())
        throw std::invalid_argument("Textured palette/source_albedo counts differ or exceed four");
    std::map<std::string,Json> table;
    std::set<std::string,MaterialPathLess> source_paths,package_paths;
    for (const auto& row : rows) {
        exact_fields(row,{"id","path","sha256","bytes","width","height","encoding","sampler","provenance"});
        const auto id = canonical_material_id(row.at("id"));
        if (!table.emplace(id,row).second || !source_paths.emplace(portable_material_path(row.at("path"))).second)
            throw std::invalid_argument("Textured palette source ID/path is duplicated or aliased");
    }
    std::map<std::string,std::shared_ptr<const std::vector<std::uint8_t>>> raw_sources;
    std::string previous;
    std::uint64_t item_bytes{};
    for (const auto& binding : bindings) {
        exact_fields(binding,{"id","source_path","path","sha256","bytes","width","height","encoding","sampler","provenance"});
        const auto id = canonical_material_id(binding.at("id"));
        if ((!previous.empty() && id <= previous) || !table.contains(id))
            throw std::invalid_argument("Textured source_albedo must be unique and sorted by ID");
        previous = id;
        const auto& row = table.at(id);
        for (const auto key : {"sha256","bytes","width","height","encoding","sampler","provenance"})
            if (row.at(key) != binding.at(key)) throw std::invalid_argument("Textured source_albedo differs from its palette row");
        const auto original_path = portable_material_path(binding.at("source_path"));
        const auto path = portable_material_path(binding.at("path"));
        if (portable_material_path(row.at("path")) != original_path || binding.at("encoding") != "tight-rgb8-srgb" ||
            binding.at("sampler") != "nearest-clamp-v1" || !package_paths.emplace(path).second)
            throw std::invalid_argument("Textured source encoding/sampler/path is invalid or aliased");
        const auto width = bounded_integer(binding.at("width"),1024u,1u), height = bounded_integer(binding.at("height"),1024u,1u);
        const auto bytes = bounded_integer(binding.at("bytes"),3u*1024u*1024u,1u);
        if (width*height > 1048576u || bytes != width*height*3u)
            throw std::invalid_argument("Textured source payload requires exact 3WH within 3 MiB");
        item_bytes += bytes;
        if (item_bytes > 12u*1024u*1024u) throw std::invalid_argument("Textured item source bytes exceed 12 MiB");
        const auto digest = material_digest(binding.at("sha256"));
        const auto& p = binding.at("provenance");
        validate_provenance(p,false); validate_notice(assets,p);
        const auto& record = assets.find(item.at("namespace").get<std::string>()+".source_albedo."+id);
        if (path != "art/materials/"+item.at("namespace").get<std::string>()+"/source-albedo/"+id+".rgb8.rtex" ||
            record.path != path || record.kind != "data" || record.sha256 != digest || record.bytes != bytes ||
            record.license != p.at("license").get<std::string>() || record.author != p.at("author").get<std::string>() ||
            record.notice != p.at("notice_path").get<std::string>() || record.source != assets.find("material.collection.metadata").source)
            throw std::invalid_argument("Textured source manifest/provenance identity differs from its source record");
        Json identity = binding; identity.erase("id"); identity.erase("path");
        const auto found = cache.sources.find(original_path);
        if (found != cache.sources.end() && (found->second.path != original_path || found->second.identity != identity))
            throw std::invalid_argument("Textured shared source has conflicting or case-aliased bindings");
        if (found == cache.sources.end() && (cache.sources.size() >= 8u || cache.bytes+bytes > 24u*1024u*1024u))
            throw std::invalid_argument("Textured shared source count/bytes exceed eight or 24 MiB");
        // Reverify even shared immutable RGB. All reads precede bitmap allocation.
        auto raw = assets.read(record.id);
        if (raw.size() != bytes || sha256(raw) != digest) throw std::invalid_argument("Textured source tight bytes/hash differ");
        if (found == cache.sources.end()) {
            auto shared = std::make_shared<const std::vector<std::uint8_t>>(std::move(raw));
            cache.bytes += bytes;
            cache.sources.emplace(original_path,TexturedSourceCache::Source{original_path,std::move(identity),shared});
            raw_sources.emplace(id,std::move(shared));
        } else raw_sources.emplace(id,found->second.bytes);
    }
    if (item_bytes+rows.size()*rgb24_membership_bytes > 20u*1024u*1024u ||
        cache.bytes+rows.size()*rgb24_membership_bytes > 32u*1024u*1024u)
        throw std::invalid_argument("Textured RGB/membership component envelope exceeds 20/32 MiB");
    std::map<std::string,std::shared_ptr<const std::vector<std::uint8_t>>> result;
    for (const auto& [id,raw] : raw_sources) {
        auto membership = std::make_shared<std::vector<std::uint8_t>>(rgb24_membership_bytes,0u);
        for (std::size_t offset=0; offset<raw->size(); offset+=3u) {
            const auto rgb = std::uint32_t((*raw)[offset]) | (std::uint32_t((*raw)[offset+1u])<<8u) | (std::uint32_t((*raw)[offset+2u])<<16u);
            (*membership)[rgb>>3u] |= static_cast<std::uint8_t>(1u<<(rgb&7u));
        }
        result.emplace(id,std::move(membership));
    }
    return result;
}
std::vector<NeutralPaletteMaterial> validate_neutral_palette(const AssetCatalog& assets, const Json& item,
                                                            TexturedSourceCache& source_cache) {
    exact_fields(item,{"namespace","legacy_key","sprite_id","material_id","source_hash","master_sha256",
        "registration","geometry_provenance","export_pixels_per_reference_pixel","depth_range","triangle_count",
        "supported_alpha_samples","uncovered_alpha_samples","probes","albedo_source","generated_albedo_sha256","coverage"});
    const auto& source = item.at("albedo_source");
    const bool textured = source.at("kind") == "authored-xyz-triangle-textures-v1";
    if (textured) exact_fields(source,{"kind","palette_path","palette_sha256","provenance","source_albedo"});
    else exact_fields(source,{"kind","palette_path","palette_sha256","provenance"});
    if (!textured && source.at("kind") != "authored-xyz-triangle-materials-v1")
        throw std::invalid_argument("Neutral albedo source kind is unsupported");
    const auto palette_path = portable_material_path(source.at("palette_path"));
    const auto palette_hash = material_digest(source.at("palette_sha256"));
    (void)material_digest(item.at("generated_albedo_sha256"));
    validate_provenance(source.at("provenance"),false);
    validate_geometry_provenance(item.at("geometry_provenance"));
    const auto& registration = item.at("registration");
    exact_fields(registration,{"page_path","rect","pivot","pivot_px","source_size","reference_width","png_sha256",
        "decoded_crop_sha256","catalog_entry_sha256","caster_alpha_policy","asset_source","provenance","references","source_bindings"});
    if (registration.at("caster_alpha_policy") != "authored-mask")
        throw std::invalid_argument("Neutral albedo requires the independent authored caster mask");
    (void)portable_material_path(registration.at("page_path"));
    (void)registered_rectangle(registration.at("rect"));
    for (const auto field : {"png_sha256","decoded_crop_sha256","catalog_entry_sha256"})
        (void)material_digest(registration.at(field));
    bounded_pair(registration.at("pivot"),0.,1.);
    bounded_pair(registration.at("pivot_px"),0.,4096.);
    if (!registration.at("reference_width").is_number() ||
        !std::isfinite(registration.at("reference_width").get<double>()) ||
        registration.at("reference_width").get<double>() <= 0. || registration.at("reference_width").get<double>() > 4096.)
        throw std::invalid_argument("Neutral registration reference width is invalid");
    bounded_pair(item.at("depth_range"),-8192.,8192.);
    if (item.at("depth_range")[0].get<double>() >= item.at("depth_range")[1].get<double>())
        throw std::invalid_argument("Neutral metadata depth interval is collapsed");
    if (!item.at("probes").is_array() || item.at("probes").size() > 10000u)
        throw std::invalid_argument("Neutral metadata numeric probes exceed their bound");
    validate_provenance(registration.at("provenance"),true);
    validate_references(registration.at("references"));
    validate_bindings(registration.at("source_bindings"),true);
    const auto& original_source = registration.at("asset_source");
    const std::set<std::string> source_fields{"id","license","author","source","notice","prompt_file","source_images"};
    if (!original_source.is_object()) throw std::invalid_argument("Neutral original asset provenance is not an object");
    for (auto it = original_source.begin(); it != original_source.end(); ++it)
        if (!source_fields.contains(it.key())) throw std::invalid_argument("Neutral original asset provenance contains unknown fields");
    for (const auto field : {"id","license","author","source"}) (void)bounded_text(original_source.at(field),512u);
    (void)portable_material_path(original_source.at("notice"));
    if (original_source.contains("prompt_file")) (void)portable_material_path(original_source.at("prompt_file"));
    if (original_source.contains("source_images")) {
        const auto& images = original_source.at("source_images");
        if (!images.is_array() || images.size() > 64u) throw std::invalid_argument("Neutral original source images exceed 64 rows");
        std::set<std::string,MaterialPathLess> paths;
        for (const auto& image : images) {
            exact_fields(image,{"path","sha256"});
            if (!paths.emplace(portable_material_path(image.at("path"))).second)
                throw std::invalid_argument("Neutral original source image path is duplicated");
            (void)material_digest(image.at("sha256"));
        }
    }
    if (json_hash(original_source) != registration.at("provenance").at("asset_source_sha256").get<std::string>())
        throw std::invalid_argument("Neutral original asset provenance digest differs from its record");
    const auto& record = assets.find(item.at("namespace").get<std::string>()+".palette");
    const auto& provenance = source.at("provenance");
    if (record.path != palette_path || record.kind != "data" || record.sha256 != palette_hash ||
        !record.bytes || record.bytes > 512u*1024u ||
        record.license != provenance.at("license").get<std::string>() ||
        record.author != provenance.at("author").get<std::string>() ||
        record.notice != provenance.at("notice_path").get<std::string>() ||
        record.source != assets.find("art.world").source || record.source != assets.find("material.collection.metadata").source)
        throw std::invalid_argument("Delivered neutral palette manifest/provenance binding is inconsistent");
    const auto raw = bounded_material_file(assets,source.at("palette_path"),512u*1024u,palette_hash);
    if (raw.size() != record.bytes || assets.resolve(palette_path) != material_root(assets)/
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(palette_path.data()),palette_path.size())))
        throw std::invalid_argument("Delivered neutral palette size/path differs from the registered file");
    validate_notice(assets,provenance);
    validate_notice(assets,registration.at("provenance"));
    const auto palette = strict_material_json(raw);
    if (textured) exact_fields(palette,{"schema","kind","status","materials","triangle_materials","authoring","geometry_source",
        "source_registration_target","original_visual_provenance","source_geometry_provenance","references",
        "source_bindings","draft_notice","legacy_alpha_gap","open_delivery","textures"});
    else exact_fields(palette,{"schema","kind","status","materials","triangle_materials","authoring","geometry_source",
        "source_registration_target","original_visual_provenance","source_geometry_provenance","references",
        "source_bindings","draft_notice","legacy_alpha_gap","open_delivery"});
    bounded_palette_strings(palette);
    if (bounded_integer(palette.at("schema"),textured ? 2u : 1u,textured ? 2u : 1u) != (textured ? 2u : 1u) ||
        palette.at("kind") != (textured ? "ribterra-neutral-textured-palette-v1" : "ribterra-neutral-palette-v1") ||
        palette.at("status") != "DRAFT / SOURCE ONLY / NOT_ACCEPTED")
        throw std::invalid_argument("Neutral palette schema/kind/DRAFT status is unsupported");
    const auto triangles = bounded_integer(item.at("triangle_count"),10000u,1u);
    const auto& rows = palette.at("materials");
    if (!rows.is_array() || rows.empty() || rows.size() > 256u)
        throw std::invalid_argument("Neutral palette requires 1..256 materials");
    std::unordered_map<std::string,std::size_t> material_ids;
    std::map<std::string,std::shared_ptr<const std::vector<std::uint8_t>>> texture_memberships;
    if (textured) texture_memberships = validate_textured_sources(assets,item,palette,source_cache);
    std::set<std::string> used_textures;
    std::vector<NeutralPaletteMaterial> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        const bool texture_material = textured && row.contains("albedo_texture");
        if (texture_material) exact_fields(row,{"id","albedo_texture","roughness","emission_mask","wet_mask","caster_mask"});
        else exact_fields(row,{"id","albedo_srgb","roughness","emission_mask","wet_mask","caster_mask"});
        const auto id = canonical_material_id(row.at("id"));
        if (!material_ids.emplace(id,result.size()).second)
            throw std::invalid_argument("Neutral palette material ID is duplicated");
        NeutralPaletteMaterial material;
        if (texture_material) {
            const auto texture_id = canonical_material_id(row.at("albedo_texture"));
            if (!texture_memberships.contains(texture_id)) throw std::invalid_argument("Textured material names an unknown source texture");
            material.rgb_membership = texture_memberships.at(texture_id); used_textures.emplace(texture_id);
        } else {
            const auto& rgb = row.at("albedo_srgb");
            if (!rgb.is_array() || rgb.size() != 3u) throw std::invalid_argument("Neutral albedo requires three authored sRGB bytes");
            for (std::size_t channel = 0; channel < 3u; ++channel)
                material.rgb[channel] = static_cast<std::uint8_t>(bounded_integer(rgb[channel],255u));
        }
        material.roughness = palette_byte(row.at("roughness"));
        material.emission = palette_byte(row.at("emission_mask"));
        material.wet = palette_byte(row.at("wet_mask"));
        material.caster = palette_byte(row.at("caster_mask"));
        result.push_back(material);
    }
    const auto& assignments = palette.at("triangle_materials");
    if (!assignments.is_array() || assignments.size() != triangles)
        throw std::invalid_argument("Neutral palette assignment count differs from declared triangles");
    std::set<std::string> triangle_ids,used_materials;
    for (const auto& assignment : assignments) {
        if (!textured) exact_fields(assignment,{"triangle_id","material_id"});
        const auto triangle = triangle_identifier(assignment.at("triangle_id"));
        const auto material = canonical_material_id(assignment.at("material_id"));
        if (!triangle_ids.emplace(triangle).second || !material_ids.contains(material))
            throw std::invalid_argument("Neutral palette triangle assignment is duplicated or names an unknown material");
        if (result[material_ids.at(material)].rgb_membership) {
            exact_fields(assignment,{"triangle_id","material_id","uv"});
            const auto& uv = assignment.at("uv");
            if (!uv.is_array() || uv.size() != 3u) throw std::invalid_argument("Textured triangle UV requires three vertex-ordered pairs");
            for (const auto& point : uv) bounded_pair(point,0.,1.);
        } else exact_fields(assignment,{"triangle_id","material_id"});
        used_materials.emplace(material);
    }
    if (used_materials.size() != result.size()) throw std::invalid_argument("Neutral palette contains an unassigned material");
    if (used_textures.size() != texture_memberships.size()) throw std::invalid_argument("Textured palette contains an unused source texture");
    const auto& authoring = palette.at("authoring");
    exact_fields(authoring,{"author","colour_encoding","alpha_policy","lighting_policy","glass_policy","emission_policy","proposed_export_mode"});
    for (auto it = authoring.begin(); it != authoring.end(); ++it) (void)bounded_text(it.value());
    if (authoring.at("proposed_export_mode") != source.at("kind") || authoring.at("author") != provenance.at("author"))
        throw std::invalid_argument("Neutral palette authoring mode differs from its active source record");
    const auto& geometry = palette.at("geometry_source");
    exact_fields(geometry,{"path","sha256","triangle_count","numeric_probe_count","sky_triangle_count","unchanged"});
    const auto archive_path = portable_material_path(geometry.at("path"));
    const auto archive_hash = material_digest(geometry.at("sha256"));
    if (bounded_integer(geometry.at("triangle_count"),10000u,1u) != triangles)
        throw std::invalid_argument("Neutral archived geometry triangle count is inconsistent");
    (void)bounded_integer(geometry.at("numeric_probe_count"),10000u);
    (void)bounded_integer(geometry.at("sky_triangle_count"),10000u);
    if (!geometry.at("unchanged").is_array() || geometry.at("unchanged").size() > 64u)
        throw std::invalid_argument("Neutral archived geometry claims exceed their bound");
    for (const auto& claim : geometry.at("unchanged")) (void)bounded_text(claim);
    const auto& target = palette.at("source_registration_target");
    exact_fields(target,{"legacy_key","original_page","original_rect","normalized_pivot","kind","png_sha256",
        "catalog_entry_sha256","source_size","pivot_px","reference_width","caster_alpha_policy","active_registration","extent_policy"});
    bounded_pair(target.at("normalized_pivot"),0.,1.);
    bounded_pair(target.at("pivot_px"),0.,4096.);
    if (target.at("legacy_key") != item.at("legacy_key") || target.at("kind") != "original-catalog-screen-v1" ||
        !target.at("active_registration").is_boolean() || target.at("active_registration").get<bool>() ||
        (target.at("caster_alpha_policy") != "painted-alpha-times-authored-mask" && target.at("caster_alpha_policy") != "authored-mask") ||
        portable_material_path(target.at("original_page")) != "assets/"+registration.at("page_path").get<std::string>() ||
        registered_rectangle(target.at("original_rect")) != registered_rectangle(registration.at("rect")) ||
        target.at("source_size") != registration.at("source_size") || target.at("pivot_px") != registration.at("pivot_px") ||
        target.at("reference_width") != registration.at("reference_width") ||
        target.at("normalized_pivot").get<std::array<float,2>>() != registration.at("pivot").get<std::array<float,2>>() ||
        material_digest(target.at("png_sha256")) != material_digest(registration.at("png_sha256")) ||
        material_digest(target.at("catalog_entry_sha256")) != material_digest(registration.at("catalog_entry_sha256")))
        throw std::invalid_argument("Neutral archived registration differs from its original registration evidence");
    validate_provenance(palette.at("original_visual_provenance"),true);
    validate_geometry_provenance(palette.at("source_geometry_provenance"));
    if (palette.at("original_visual_provenance") != registration.at("provenance") ||
        palette.at("source_geometry_provenance") != item.at("geometry_provenance"))
        throw std::invalid_argument("Neutral archived visual/geometry provenance is inconsistent");
    validate_references(palette.at("references"));
    validate_bindings(palette.at("source_bindings"),false);
    const auto archive_binding = std::find_if(palette.at("source_bindings").begin(),palette.at("source_bindings").end(),
        [&](const Json& row) { return row.at("path").get<std::string>() == archive_path && row.at("sha256").get<std::string>() == archive_hash; });
    if (archive_binding == palette.at("source_bindings").end())
        throw std::invalid_argument("Neutral archived geometry is missing its declared source binding");
    const auto& notice = palette.at("draft_notice");
    exact_fields(notice,{"path","sha256"});
    if (portable_material_path(notice.at("path")) != provenance.at("notice_path").get<std::string>() ||
        material_digest(notice.at("sha256")) != provenance.at("notice_sha256").get<std::string>())
        throw std::invalid_argument("Neutral palette authored notice differs from its active provenance");
    const auto& gap = palette.at("legacy_alpha_gap");
    exact_fields(gap,{"source_nonzero_alpha_pixels","source_pixels_supported_by_old_geometry","source_pixels_unsupported_by_old_geometry",
        "source_map","neutral_output_alpha_measured","policy"});
    const auto total = bounded_integer(gap.at("source_nonzero_alpha_pixels"),4096u*4096u);
    const auto supported = bounded_integer(gap.at("source_pixels_supported_by_old_geometry"),total);
    if (bounded_integer(gap.at("source_pixels_unsupported_by_old_geometry"),total) != total-supported ||
        !gap.at("neutral_output_alpha_measured").is_boolean() || gap.at("neutral_output_alpha_measured").get<bool>())
        throw std::invalid_argument("Neutral archived alpha gap must remain unmeasured and arithmetically consistent");
    (void)portable_material_path(gap.at("source_map"));
    if (!palette.at("open_delivery").is_array() || palette.at("open_delivery").size() > 64u)
        throw std::invalid_argument("Neutral palette delivery declarations exceed their bound");
    for (const auto& pending : palette.at("open_delivery")) (void)bounded_text(pending);
    // Archived geometry hashes/policy are evidence declarations, not active
    // master identity and not instructions to fetch unshipped source masters.
    return result;
}

std::uint64_t palette_pixel_key(const NeutralPaletteMaterial& material) noexcept {
    std::uint64_t key{};
    for (std::size_t channel = 0; channel < material.rgb.size(); ++channel)
        key |= std::uint64_t(material.rgb[channel]) << (channel*8u);
    return key | (std::uint64_t(material.roughness)<<24u) | (std::uint64_t(material.emission)<<32u) |
        (std::uint64_t(material.wet)<<40u) | (std::uint64_t(material.caster)<<48u);
}
void validate_neutral_pixels(const AssetCatalog& assets, const Json& item, const Json& material,
                             const Json& world, const std::array<int,4>& crop,
                             std::span<const std::uint8_t> original,
                             const std::vector<NeutralPaletteMaterial>& palette) {
    constexpr std::array<std::string_view,6> names{"albedo","normal_material","camera_depth","emission_mask","surface_mask","caster_mask"};
    constexpr std::array<std::string_view,6> formats{"rgba8_unorm_srgb","rgba8_unorm","r16_unorm","r8_unorm","rgba8_unorm","r8_unorm"};
    constexpr std::array<std::size_t,6> strides{4u,4u,2u,1u,4u,1u};
    const auto pixels = std::size_t(crop[2])*crop[3];
    std::array<std::vector<std::uint8_t>,6> channels;
    const auto& provenance = item.at("albedo_source").at("provenance");
    for (std::size_t channel = 0; channel < names.size(); ++channel) {
        const auto& binding = material.at("channels").at(std::string(names[channel]));
        const auto id = binding.at("texture").get<std::string>();
        const Json* texture{};
        for (const auto& row : world.at("textures")) if (row.at("id").get<std::string>() == id) {
            if (texture) throw std::invalid_argument("Neutral channel texture ID is ambiguous");
            texture = &row;
        }
        if (!texture || texture->at("format").get<std::string>() != formats[channel] ||
            bounded_integer(texture->at("width"),4096u,1u) != static_cast<std::uint64_t>(crop[2]) ||
            bounded_integer(texture->at("height"),4096u,1u) != static_cast<std::uint64_t>(crop[3]) ||
            registered_rectangle(binding.at("rect")) != std::array<int,4>{0,0,crop[2],crop[3]})
            throw std::invalid_argument("Neutral channel extent/format/subrect differs from the registered source");
        const auto path = portable_material_path(texture->at("path"));
        const auto digest = material_digest(texture->at("sha256"));
        const auto& record = assets.find(id);
        if (record.path != path || record.kind != "data" || record.sha256 != digest || record.bytes != pixels*strides[channel] ||
            record.license != provenance.at("license").get<std::string>() ||
            record.author != provenance.at("author").get<std::string>()+"; geometry: "+item.at("geometry_provenance").at("author").get<std::string>() ||
            record.source != assets.find("art.world").source || record.notice != provenance.at("notice_path").get<std::string>())
            throw std::invalid_argument("Neutral channel manifest/provenance/byte binding differs from its descriptor");
        channels[channel] = bounded_material_file(assets,texture->at("path"),pixels*strides[channel],digest);
        if (channels[channel].size() != pixels*strides[channel] || assets.resolve(path) != material_root(assets)/
            std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),path.size())))
            throw std::invalid_argument("Neutral channel tight byte size/path is inconsistent");
    }
    if (sha256(channels[0]) != material_digest(item.at("generated_albedo_sha256")))
        throw std::invalid_argument("Generated neutral albedo digest differs from actual delivered RGBA");
    std::set<std::uint64_t> allowed;
    std::map<std::uint32_t,std::vector<std::shared_ptr<const std::vector<std::uint8_t>>>> textured_groups;
    for (const auto& entry : palette) {
        if (!entry.rgb_membership) allowed.emplace(palette_pixel_key(entry));
        else {
            auto& group = textured_groups[static_cast<std::uint32_t>(palette_pixel_key(entry)>>24u)];
            if (std::find(group.begin(),group.end(),entry.rgb_membership) == group.end()) group.push_back(entry.rgb_membership);
        }
    }
    const bool textured = item.at("albedo_source").at("kind") == "authored-xyz-triangle-textures-v1";
    std::uint64_t original_count{},generated_count{},overlap{};
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto offset = pixel*4u;
        const auto alpha = channels[0][offset+3u];
        if (alpha != 0u && alpha != 255u) throw std::invalid_argument("Generated neutral alpha must be binary opaque geometry coverage");
        if (!alpha && (channels[0][offset] || channels[0][offset+1u] || channels[0][offset+2u]))
            throw std::invalid_argument("Generated neutral transparent RGBA must be zero");
        if (textured && !alpha)
            for (std::size_t channel=1u; channel<channels.size(); ++channel)
                for (std::size_t byte=0; byte<strides[channel]; ++byte)
                    if (channels[channel][pixel*strides[channel]+byte])
                        throw std::invalid_argument("Generated textured channels must be zero outside geometry coverage");
        original_count += original[offset+3u] != 0u;
        generated_count += alpha != 0u;
        overlap += original[offset+3u] != 0u && alpha != 0u;
        if (alpha) {
            NeutralPaletteMaterial sample{{channels[0][offset],channels[0][offset+1u],channels[0][offset+2u]},
                channels[1][offset+2u],channels[3][pixel],channels[4][offset],channels[5][pixel],{}};
            const auto key = palette_pixel_key(sample);
            bool member = allowed.contains(key);
            const auto group = textured_groups.find(static_cast<std::uint32_t>(key>>24u));
            if (!member && group != textured_groups.end()) {
                const auto rgb = static_cast<std::uint32_t>(key & 0xffffffu);
                member = std::any_of(group->second.begin(),group->second.end(),[&](const auto& index){
                    return ((*index)[rgb>>3u] & static_cast<std::uint8_t>(1u<<(rgb&7u))) != 0u;
                });
            }
            if (!member || channels[4][offset+1u] || channels[4][offset+2u] || channels[4][offset+3u] != 255u)
                throw std::invalid_argument("Generated neutral material bytes differ from the bound authored palette");
        }
    }
    if (!generated_count) throw std::invalid_argument("Generated neutral albedo has no opaque geometry");
    const auto& coverage = item.at("coverage");
    exact_fields(coverage,{"original_alpha_samples","generated_alpha_samples","overlap_alpha_samples","omitted_original_alpha_samples","generated_only_alpha_samples"});
    if (bounded_integer(coverage.at("original_alpha_samples"),pixels) != original_count ||
        bounded_integer(coverage.at("generated_alpha_samples"),pixels) != generated_count ||
        bounded_integer(coverage.at("overlap_alpha_samples"),pixels) != overlap ||
        bounded_integer(coverage.at("omitted_original_alpha_samples"),pixels) != original_count-overlap ||
        bounded_integer(coverage.at("generated_only_alpha_samples"),pixels) != generated_count-overlap ||
        bounded_integer(item.at("supported_alpha_samples"),pixels) != generated_count ||
        bounded_integer(item.at("uncovered_alpha_samples"),pixels) != original_count-overlap)
        throw std::invalid_argument("Generated neutral coverage declarations differ from actual original/generated alpha");
    // RGB24 membership is grouped by the four scalar bytes. It permits only
    // colors from an assigned texture/constant, not arbitrary mixed tuples.
    // This is not exact per-pixel UV or triangle-winner certification; the
    // independent CPU XYZ/UV oracle owns those proofs. Component caps are not
    // whole-process RSS or GPU memory measurements.
}

void preflight_textured_channels(const Json& metadata,const Json& world) {
    if (metadata.at("items").size() > 8u) throw std::invalid_argument("Textured collection exceeds eight material namespaces");
    std::uint64_t final_bytes{},largest{};
    std::size_t new_kind_count{},sources{}; std::uint64_t source_bytes{};
    for (const auto& item : metadata.at("items")) {
        (void)canonical_material_id(item.at("namespace"));
        (void)portable_material_path(item.at("namespace"));
        const auto& size = item.at("registration").at("source_size");
        if (!size.is_array() || size.size() != 2u) throw std::invalid_argument("Textured output requires a bounded source_size pair");
        const auto width = bounded_integer(size[0],2048u,1u), height = bounded_integer(size[1],2048u,1u);
        const auto pixels = width*height;
        if (pixels > 1048576u) throw std::invalid_argument("Textured output exceeds 1048576 texels");
        const auto bytes = pixels*16u;
        final_bytes += bytes; largest = std::max(largest,bytes);
        if (item.contains("albedo_source") && item.at("albedo_source").at("kind") == "authored-xyz-triangle-textures-v1") {
            ++new_kind_count;
            const auto& rows = item.at("albedo_source").at("source_albedo");
            if (!rows.is_array() || rows.size() > 4u) throw std::invalid_argument("Textured source roles exceed four per namespace");
            std::uint64_t local{};
            for (const auto& row : rows) {
                const auto count = bounded_integer(row.at("bytes"),3u*1024u*1024u,1u);
                local += count; source_bytes += count; ++sources;
            }
            if (local > 12u*1024u*1024u) throw std::invalid_argument("Textured source namespace exceeds 12 MiB");
        }
    }
    if (!new_kind_count) throw std::invalid_argument("MaterialCollectionTextured requires at least one textured source item");
    if (sources > 8u || source_bytes > 24u*1024u*1024u)
        throw std::invalid_argument("Textured source collection exceeds eight roles or 24 MiB");
    if (final_bytes+largest > 80u*1024u*1024u)
        throw std::invalid_argument("Textured six-channel retained/transient component exceeds 80 MiB");
    // Check all delivered channel extents before any source RGB/index allocation.
    for (const auto& texture : world.at("textures")) {
        const auto width = bounded_integer(texture.at("width"),2048u,1u), height = bounded_integer(texture.at("height"),2048u,1u);
        if (width*height > 1048576u) throw std::invalid_argument("Textured delivered channel exceeds 1048576 texels");
    }
}

// Decode only a bounded, untransformed original RGBA page during cold loading.
// The cropped bytes/hash are temporary; GameArt remains the CPU mask owner.
std::vector<std::uint8_t> registered_paint(std::span<const std::uint8_t> encoded,
                                         const std::array<int,4>& crop) {
    constexpr std::array<std::uint8_t,8> signature{137u,80u,78u,71u,13u,10u,26u,10u};
    if (encoded.size() < signature.size() || !std::equal(signature.begin(),signature.end(),encoded.begin()))
        throw std::invalid_argument("Registered original paint must be a PNG");
    const auto be32 = [&](std::size_t offset) {
        return (std::uint32_t(encoded[offset])<<24u) | (std::uint32_t(encoded[offset+1u])<<16u) |
            (std::uint32_t(encoded[offset+2u])<<8u) | std::uint32_t(encoded[offset+3u]);
    };
    std::size_t offset = 8u; bool header{},ended{};
    while (encoded.size()-offset >= 12u) {
        const auto count = be32(offset);
        if (count > encoded.size()-offset-12u)
            throw std::invalid_argument("Registered original PNG chunk is truncated");
        const std::string_view tag(reinterpret_cast<const char*>(encoded.data()+offset+4u),4u);
        if (tag == "gAMA" || tag == "sRGB" || tag == "iCCP" || tag == "cHRM" || tag == "acTL")
            throw std::invalid_argument("Registered paint rejects colour transforms and animation");
        if (tag == "IHDR") {
            if (header || count != 13u || !be32(offset+8u) || !be32(offset+12u) ||
                be32(offset+8u) > 4096u || be32(offset+12u) > 4096u ||
                encoded[offset+16u] != 8u || encoded[offset+17u] != 6u ||
                encoded[offset+18u] || encoded[offset+19u] || encoded[offset+20u])
                throw std::invalid_argument("Registered original paint requires noninterlaced RGBA8");
            header = true;
        }
        offset += std::size_t(count)+12u;
        if (tag == "IEND") { ended = true; break; }
    }
    if (!header || !ended || offset != encoded.size())
        throw std::invalid_argument("Registered original PNG structure is invalid");
    png_image image{}; image.version = PNG_IMAGE_VERSION;
    struct Free { png_image* value; ~Free() { png_image_free(value); } } image_guard{&image};
    if (!png_image_begin_read_from_memory(&image,encoded.data(),encoded.size()))
        throw std::runtime_error("Cannot decode registered original paint");
    if (std::int64_t(crop[0])+crop[2] > image.width || std::int64_t(crop[1])+crop[3] > image.height)
        throw std::invalid_argument("Registered crop exceeds its original page");
    image.format = PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> rgba(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image,nullptr,rgba.data(),0,nullptr))
        throw std::runtime_error("Cannot read registered original paint");
    std::vector<std::uint8_t> result(std::size_t(crop[2])*crop[3]*4u);
    for (int y = 0; y < crop[3]; ++y) {
        const auto first = (std::size_t(crop[1]+y)*image.width+std::size_t(crop[0]))*4u;
        std::copy_n(rgba.begin()+static_cast<std::ptrdiff_t>(first),std::size_t(crop[2])*4u,
            result.begin()+static_cast<std::ptrdiff_t>(std::size_t(y)*crop[2]*4u));
    }
    return result;
}

bool supported_registered_key(std::string_view key) noexcept {
    return key.starts_with("building.") || key.starts_with("resident.") || key.starts_with("trees.018.") ||
        key.starts_with("rocks-mountains.") || key.starts_with("concept.pine.") || key.starts_with("concept.leafy") ||
        key.starts_with("concept.stone.") || key.starts_with("concept.boulders") || key.starts_with("concept.cliff");
}

constexpr std::size_t engine_control_instance_capacity = 32;
constexpr int engine_control_max_cell_radius = 12;
constexpr float engine_control_min_zoom = .5f;
constexpr float engine_control_max_zoom = 2.f;
constexpr float engine_control_coordinate_bound = 32.f;
constexpr std::array<std::string_view, 8> engine_control_sprite_ids{
    "control.house.sprite", "control.stockpile.sprite", "control.workshop.sprite",
    "control.tree.sprite", "control.rock.sprite", "control.resident.empty.sprite",
    "control.resident.log.sprite", "control.resident.sack.sprite"};

bool finite_control_value(float value) noexcept { return std::isfinite(value); }

render::AtmosphereGpu control_atmosphere(const EngineControlSettings& settings, double seconds) {
    const auto& input = *settings.atmosphere;
    if (!settings.water_enabled || !std::isfinite(seconds) || seconds < 0. || seconds > 1000000. ||
        !finite_control_value(input.time_hour) || input.time_hour < 0.f || input.time_hour >= 24.f ||
        !finite_control_value(input.weather_haze01) || input.weather_haze01 < 0.f || input.weather_haze01 > 1.f)
        throw std::invalid_argument("Engine atmosphere requires water, bounded shown time, hour and weather input");
    const auto smooth = [](float first, float last, float value) {
        const float t = std::clamp((value - first) / (last - first), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    const float morning = smooth(4.f, 6.f, input.time_hour) * (1.f - smooth(6.f, 9.f, input.time_hour));
    const float daylight = smooth(5.f, 9.f, input.time_hour) * (1.f - smooth(16.f, 21.f, input.time_hour));
    render::AtmosphereGpu result;
    result.origin_time = {settings.camera_x, settings.camera_y, static_cast<float>(seconds), morning};
    result.depth = {-12.f, 18.f, .10f + .22f * input.weather_haze01, .20f};
    result.density = {.40f + .30f * input.weather_haze01, .25f + .35f * input.weather_haze01, 0.f, 1.25f};
    constexpr std::array<float, 3> night{.025f, .045f, .08f};
    constexpr std::array<float, 3> far_day{.42f, .57f, .68f};
    constexpr std::array<float, 3> morning_day{.60f, .55f, .47f};
    constexpr std::array<float, 3> coastal_day{.34f, .49f, .60f};
    constexpr std::array<float, 3> overcast{.32f, .38f, .43f};
    for (std::size_t channel = 0; channel < 3u; ++channel) {
        const float far_color = std::lerp(far_day[channel], overcast[channel], input.weather_haze01);
        result.far_color_noise[channel] = std::lerp(night[channel], far_color, daylight);
        result.morning_color[channel] = std::lerp(night[channel], morning_day[channel], .25f + .75f * daylight);
        result.coastal_color[channel] = std::lerp(night[channel], coastal_day[channel], daylight);
    }
    result.wind_shore = {settings.water_flow[0], settings.water_flow[1], .035f, 3.f};
    result.counts_version[2] = input.enabled ? 1u : 0u;
    render::validate_atmosphere_gpu(result);
    return result;
}

bool control_coordinate_in_bounds(const std::array<float, 3> &value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float component) {
        return finite_control_value(component) && std::abs(component) <= engine_control_coordinate_bound;
    });
}

std::array<float, 3> control_normal(const std::array<float, 3> &a,
                                    const std::array<float, 3> &b,
                                    const std::array<float, 3> &c) {
    const std::array<float, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const std::array<float, 3> v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    std::array<float, 3> result{u[1] * v[2] - u[2] * v[1],
                                u[2] * v[0] - u[0] * v[2],
                                u[0] * v[1] - u[1] * v[0]};
    const float length = std::hypot(result[0], result[1], result[2]);
    if (!finite_control_value(length) || length <= std::numeric_limits<float>::epsilon())
        throw std::invalid_argument("Engine control terrain triangle is degenerate");
    for (auto &component : result) component /= length;
    return result;
}

void validate_control_sprite_bounds(const render::Frame2D &frame) {
    for (const auto &draw : frame.sprite_material_draws) {
        const auto &surface = draw.surface;
        for (const float x : {0.f, surface.source_size_pivot[0]})
            for (const float y : {0.f, surface.source_size_pivot[1]})
                for (const float depth : {surface.depth_scale[0], surface.depth_scale[1]}) {
                    const auto reconstructed = render::reconstruct_render_xyz(
                        render::reference_offset_from_source_pixel(
                            {x, y}, {surface.source_size_pivot[2], surface.source_size_pivot[3]},
                            surface.anchor_export[3]),
                        depth);
                    std::array<float, 3> world{};
                    for (std::size_t axis = 0; axis < world.size(); ++axis)
                        world[axis] = surface.anchor_export[axis] +
                                      reconstructed[axis] * surface.depth_scale[2];
                    if (!control_coordinate_in_bounds(world))
                        throw std::invalid_argument(
                            "Engine control sprite reconstruction exceeds [-32, 32]");
                }
    }
}

void validate_control_caster_bounds(const render::Frame2D &frame) {
    std::array<std::size_t, 2> active{};
    if (frame.sky_shadow_triangles.size() > render::sky_shadow_triangle_capacity)
        throw std::invalid_argument("Engine control sky triangle capacity exceeded");
    for (const auto &triangle : frame.sky_shadow_triangles) {
        if (triangle.source_mask != 3u)
            throw std::invalid_argument("Engine control sky casters require source mask 3");
        for (const auto &vertex : triangle.vertices)
            if (!control_coordinate_in_bounds(vertex.position))
                throw std::invalid_argument("Engine control sky caster exceeds [-32, 32]");
        if (triangle.reveal <= 0.f) continue;
        if (triangle.source_mask & 1u) ++active[0];
        if (triangle.source_mask & 2u) ++active[1];
    }
    if (active[0] > render::sky_shadow_active_triangle_capacity ||
        active[1] > render::sky_shadow_active_triangle_capacity)
        throw std::invalid_argument("Engine control active sky triangle capacity exceeded");
}

void validate_control_occluder_bounds(const render::Frame2D &frame) {
    if (frame.local_occluders.size() > render::local_occluder_capacity)
        throw std::invalid_argument("Engine control local occluder capacity exceeded");
    std::set<std::uint32_t> stable_ids;
    for (const auto &row : frame.local_occluders) {
        const auto bounded = [](const auto &values) {
            return std::all_of(values.begin(), values.end(), [](float value) {
                return finite_control_value(value) &&
                       std::abs(value) <= engine_control_coordinate_bound;
            });
        };
        if (!bounded(row.center_base) || !bounded(row.axis_x_extent) ||
            !bounded(row.axis_y_radius) || row.center_base[2] != 0.f ||
            row.axis_x_extent[3] <= row.center_base[3] || !row.shape_flags_id[1] ||
            !row.shape_flags_id[2] || !row.shape_flags_id[3] ||
            !stable_ids.insert(row.shape_flags_id[2]).second)
            throw std::invalid_argument("Engine control local occluder bounds or IDs are invalid");
        float extent_x{};
        float extent_y{};
        if (row.shape_flags_id[0] == static_cast<std::uint32_t>(render::OccluderShape::Obb)) {
            extent_x = std::abs(row.axis_x_extent[0]) * row.axis_x_extent[2] +
                       std::abs(row.axis_y_radius[0]) * row.axis_y_radius[2];
            extent_y = std::abs(row.axis_x_extent[1]) * row.axis_x_extent[2] +
                       std::abs(row.axis_y_radius[1]) * row.axis_y_radius[2];
        } else if (row.shape_flags_id[0] ==
                   static_cast<std::uint32_t>(render::OccluderShape::Capsule)) {
            extent_x = std::abs(row.axis_x_extent[0]) * row.axis_x_extent[2] +
                       row.axis_y_radius[3];
            extent_y = std::abs(row.axis_x_extent[1]) * row.axis_x_extent[2] +
                       row.axis_y_radius[3];
        } else {
            throw std::invalid_argument("Engine control local occluder shape is invalid");
        }
        if (std::abs(row.center_base[0]) + extent_x > engine_control_coordinate_bound ||
            std::abs(row.center_base[1]) + extent_y > engine_control_coordinate_bound)
            throw std::invalid_argument("Engine control local occluder footprint exceeds [-32, 32]");
    }
}

class FrameBuilder {
  public:
    explicit FrameBuilder(render::Frame2D &frame) : frame_(frame) {}
    void pass(render::Pass value) { pass_ = value; }
    void append_geometry(const render::Frame2D &mesh, SDL_FPoint offset) {
        // Cached terrain/minimap meshes contain plain geometry only. Material
        // tables/companions are not copied or silently rebound to destination IDs.
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || mesh.linear_hdr_world ||
            !mesh.surface_vertices.empty() || !mesh.sprite_material_draws.empty() || !mesh.ground_material_draws.empty() ||
            mesh.water_surface || mesh.atmosphere || mesh.water_normal != render::TextureHandle{} ||
            !mesh.reflection_draws.empty() || !mesh.local_occluders.empty() || !mesh.contact_shadows.empty() ||
            mesh.contact_projection || mesh.moon_lighting || !mesh.sky_shadow_triangles.empty())
            throw std::invalid_argument("Cached geometry cannot carry world material payloads");
        for (const auto& command : mesh.commands) {
            const auto* draw = std::get_if<render::GeometryDraw>(&command);
            if (!draw || !plain_geometry(*draw) || draw->first_index > mesh.indices.size() ||
                draw->index_count > mesh.indices.size()-draw->first_index)
                throw std::invalid_argument("Cached mesh requires bounded plain geometry commands");
        }
        for (const auto index : mesh.indices)
            if (index >= mesh.vertices.size()) throw std::invalid_argument("Cached mesh vertex index is invalid");
        const auto vertex_base = static_cast<std::uint32_t>(frame_.vertices.size());
        const auto index_base = static_cast<std::uint32_t>(frame_.indices.size());
        frame_.vertices.insert(frame_.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (std::size_t i = vertex_base; i < frame_.vertices.size(); ++i) {
            frame_.vertices[i].position.x += offset.x;
            frame_.vertices[i].position.y += offset.y;
        }
        for (const auto index : mesh.indices) frame_.indices.push_back(vertex_base + index);
        for (const auto &command : mesh.commands) {
            auto copy = std::get<render::GeometryDraw>(command);
            copy.first_index += index_base;
            // Preserve painter order; only adjacent compatible draws can merge.
            if (!frame_.commands.empty()) {
                auto *last = std::get_if<render::GeometryDraw>(&frame_.commands.back());
                if (last && plain_geometry(*last) && plain_geometry(copy) && last->pass == copy.pass && last->texture == copy.texture &&
                    last->blend == copy.blend && !last->scissor_enabled && !copy.scissor_enabled &&
                    last->first_index + last->index_count == copy.first_index) {
                    last->index_count += copy.index_count;
                    continue;
                }
            }
            frame_.commands.emplace_back(copy);
        }
    }
    void poly(std::initializer_list<SDL_FPoint> points, Col color) {
        if (points.size() < 3)
            return;
        const auto first_vertex = static_cast<std::uint32_t>(frame_.vertices.size());
        const auto first_index = static_cast<std::uint32_t>(frame_.indices.size());
        for (const auto p : points)
            frame_.vertices.push_back({{p.x, p.y}, {}, color});
        for (std::uint32_t i = 1; i + 1 < static_cast<std::uint32_t>(points.size()); ++i) {
            frame_.indices.insert(frame_.indices.end(),
                                  {first_vertex, first_vertex + i, first_vertex + i + 1});
        }
        append(first_index, static_cast<std::uint32_t>(frame_.indices.size()) - first_index,
               color.a == 255 ? render::Blend::Opaque : render::Blend::Alpha);
    }
    void fill(float x, float y, float w, float h, Col color) {
        poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, color);
    }
    void line(float x1, float y1, float x2, float y2, Col color) {
        const float dx = x2 - x1, dy = y2 - y1, length = std::max(.001f, std::hypot(dx, dy));
        const float nx = -dy * .5f / length, ny = dx * .5f / length;
        poly({{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}}, color);
    }
    void outline(SDL_FRect q, Col color) {
        line(q.x, q.y, q.x + q.w, q.y, color);
        line(q.x + q.w, q.y, q.x + q.w, q.y + q.h, color);
        line(q.x + q.w, q.y + q.h, q.x, q.y + q.h, color);
        line(q.x, q.y + q.h, q.x, q.y, color);
    }
    void diamond(float x, float y, float hw, float hh, Col color) {
        poly({{x, y - hh}, {x + hw, y}, {x, y + hh}, {x - hw, y}}, color);
    }
    void disc(float x, float y, float rx, float ry, Col color, int n = 22) {
        // Emit individual triangles because FrameBuilder::poly makes a fan from its first point.
        for (int i = 0; i < n; ++i) {
            float a = 2 * pi * i / n, b = 2 * pi * (i + 1) / n;
            poly({{x, y},
                  {x + std::cos(a) * rx, y + std::sin(a) * ry},
                  {x + std::cos(b) * rx, y + std::sin(b) * ry}},
                 color);
        }
    }

  private:
    void append(std::uint32_t first, std::uint32_t count, render::Blend blend) {
        if (!frame_.commands.empty())
            if (auto *draw = std::get_if<render::GeometryDraw>(&frame_.commands.back());
                draw && plain_geometry(*draw) && draw->texture == render::TextureHandle{} && draw->pass == pass_ && !draw->scissor_enabled &&
                draw->blend == blend && draw->first_index + draw->index_count == first) {
                draw->index_count += count;
                return;
            }
        frame_.commands.emplace_back(render::GeometryDraw{pass_, blend, {}, first, count});
    }
    render::Frame2D &frame_;
    render::Pass pass_{render::Pass::WorldOrdered};
};
void fill(FrameBuilder *b, float x, float y, float w, float h, Col c) {
    b->fill(x, y, w, h, c);
}
void line(FrameBuilder *b, float x1, float y1, float x2, float y2, Col c) {
    b->line(x1, y1, x2, y2, c);
}
void poly(FrameBuilder *b, std::initializer_list<SDL_FPoint> p, Col c) {
    b->poly(p, c);
}
void diamond(FrameBuilder *b, float x, float y, float hw, float hh, Col c) {
    b->diamond(x, y, hw, hh, c);
}
void disc(FrameBuilder *b, float x, float y, float rx, float ry, Col c, int n = 22) {
    b->disc(x, y, rx, ry, c, n);
}
bool inside(float x, float y, const SDL_FRect &q) {
    return x >= q.x && y >= q.y && x <= q.x + q.w && y <= q.y + q.h;
}

std::string job_ua(Job j) {
    switch (j) {
    case Job::Idle:
        return "Відпочиває";
    case Job::Move:
        return "Іде";
    case Job::Woodcut:
        return "Рубає ліс";
    case Job::Mine:
        return "Видобуває камінь";
    case Job::Forage:
        return "Збирає їжу";
    case Job::Farm:
        return "Працює на фермі";
    case Job::Build:
        return "Будує";
    }
    return {};
}
std::string kind_ua(BuildingKind k) { return building_definition(k).name_ua; }
std::string cost_ua(BuildingKind k) {
    const auto c = ClientSession::cost(k);
    std::string out;
    auto add = [&](int n, const char *word) {
        if (!n)
            return;
        if (!out.empty())
            out += " · ";
        out += std::to_string(n) + " " + word;
    };
    add(c.wood, "дер.");
    add(c.stone, "кам.");
    add(c.food, "їжі");
    return out;
}

} // namespace

struct GameView::Impl {
    render::Renderer2D &renderer;
    GameArt art;
    std::unique_ptr<AssetCatalog> original_assets;
    Json original_art;
    struct RegisteredMaterial {
        std::string sprite_id;
        float reference_width{},export_scale{};
    };
    std::unordered_map<std::string,RegisteredMaterial> registered_materials;
    std::unique_ptr<GameArt> material_art;
    bool material_active{};
    float material_wetness{};
    std::vector<std::pair<std::size_t,std::size_t>> material_vertex_ranges;
    std::unique_ptr<GameArt> engine_control_art;
    render::TextureHandle engine_control_ground{};
    render::TextureHandle engine_water_normal{};
    bool winter{};
    bool autumn{};
    double animation_tick{};
    std::unordered_map<int, UnitAnimation> unit_animations;
    MotionClock motion_clock;
    std::unordered_map<int, UnitMotion> resident_motion, animal_motion;
    const ClientSession *motion_session{};
    std::uint64_t motion_epoch{}, motion_revision{};
    bool motion_ready{};
    bool motion_tracks_refresh_pending{};
    struct PreparedMotion {
        MotionClock clock;
        const ClientSession *session{};
        std::uint64_t epoch{};
        bool ready{}, reset{};
    };
    PreparedMotion prepare_motion(const ClientSession &w, double seconds) const {
        const bool ready = w.ready() && !w.reconnecting();
        bool reset = motion_session != &w || motion_epoch != w.control_epoch() ||
                     (!motion_ready && ready);
        MotionClock clock = reset ? MotionClock{} : motion_clock;
        reset |= clock.update(w.ticks(), seconds,
            w.remote() ? w.lobby().speed : sim_speed,
            !ready || (w.remote() ? w.lobby().paused : pause), w.remote());
        return {clock, &w, w.control_epoch(), ready, reset};
    }
    void commit_motion(const PreparedMotion &prepared) noexcept {
        motion_clock = prepared.clock;
        motion_session = prepared.session;
        motion_epoch = prepared.epoch;
        motion_ready = prepared.ready;
        animation_tick = motion_clock.tick();
        motion_tracks_refresh_pending |= prepared.reset;
    }
    struct ResolvedSprite {
        const RegisteredMaterial* material{};
        float geometry_scale{};
    };
    ResolvedSprite resolve_sprite(std::string_view legacy_key, float width, bool flip_x = false) const {
        if (!material_active || !material_art || flip_x || active_layer != WorldLayer::Surface)
            return {};
        const auto found = registered_materials.find(std::string(legacy_key));
        if (found == registered_materials.end()) return {};
        // Width is the ordinary requested reference width including current
        // zoom/child scale. Preserve reference_width222 for resident crops.
        const auto& material = found->second;
        const float geometry_scale = (width/zoom)*material.export_scale/material.reference_width;
        if (!std::isfinite(geometry_scale) || geometry_scale <= 0.f)
            throw std::invalid_argument("Registered sprite reference scale is invalid");
        return {&material,geometry_scale};
    }
    ResolvedSprite resolve_sprite_body(std::string_view key, float width, Col tint, bool flip_x = false) const {
        return tint.r == 255u && tint.g == 255u && tint.b == 255u ? resolve_sprite(key,width,flip_x) : ResolvedSprite{};
    }
    render::Rect resolved_bounds(const ResolvedSprite& resolved, std::string_view key,
                                 render::Point ground, float width) const {
        return resolved.material ? material_art->lit_sprite_bounds(resolved.material->sprite_id,ground,zoom,resolved.geometry_scale)
                                 : art.sprite_bounds(key,ground,width);
    }
    bool hit_sprite(std::string_view key, render::Point ground, float width, render::Point point, bool flip_x = false,
                    bool material_body = true) const {
        const auto resolved = material_body ? resolve_sprite(key,width,flip_x) : ResolvedSprite{};
        if (resolved.material) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                throw std::invalid_argument("Registered sprite hit-test point must be finite");
            const auto bounds = resolved_bounds(resolved,key,ground,width);
            if (point.x < bounds.x || point.y < bounds.y || point.x >= bounds.x+bounds.w || point.y >= bounds.y+bounds.h)
                return false;
            return material_art->hit_test_lit_sprite(resolved.material->sprite_id,ground,zoom,point,resolved.geometry_scale);
        }
        return art.hit_test_sprite(key,ground,width,point,flip_x);
    }
    void draw_sprite(std::string_view key, render::Point ground, float width, float world_x, float world_y,
                     Col tint = {255,255,255,255}, bool flip_x = false) {
        draw_resolved_sprite(resolve_sprite_body(key,width,tint,flip_x),key,ground,width,world_x,world_y,tint,flip_x);
    }
    void draw_resolved_sprite(const ResolvedSprite& resolved, std::string_view key, render::Point ground,
                              float width, float world_x, float world_y, Col tint, bool flip_x = false) {
        if (!resolved.material) {
            art.sprite(*draw_frame,key,ground,width,render::Pass::WorldOrdered,tint,flip_x);
            return;
        }
        (void)resolved_bounds(resolved,key,ground,width);
        const auto plane = render::project_render_xyz({world_x-cam_x,world_y-cam_y,0.f});
        // Include the inherited plate's screen-ground offset in its anchor.
        // This preserves its old pivot alignment without changing world cells.
        const float anchor_z = (plane.y-(ground.y-logical_h*.48f)/zoom)/render::reference_tile_height;
        const std::array<float,3> anchor{world_x-cam_x,world_y-cam_y,anchor_z};
        const auto first = draw_frame->vertices.size();
        material_art->lit_sprite(*draw_frame,resolved.material->sprite_id,ground,anchor,zoom,
            resolved.geometry_scale,material_wetness,0u,1.f,float(tint.a)/255.f);
        if (draw_frame->vertices.size() != first)
            material_vertex_ranges.emplace_back(first,draw_frame->vertices.size());
    }
    MotionPosition resident_position(const Resident &u) const {
        const auto found = resident_motion.find(u.id);
        return found == resident_motion.end() ? MotionPosition{u.x, u.y, u.layer}
                                             : found->second.sample(animation_tick);
    }
    MotionPosition animal_position(const Animal &a) const {
        const auto found = animal_motion.find(a.id);
        return found == animal_motion.end() ? MotionPosition{a.x, a.y}
                                           : found->second.sample(animation_tick);
    }
    SDL_FPoint resident_point(const Resident &u) const {
        const auto p = resident_position(u);
        return iso(p.x, p.y);
    }
    struct ResidentVisual {
        MotionPosition position;
        SDL_FPoint feet;
        render::Point sprite_ground;
        std::string sprite;
        ResidentLoad load{ResidentLoad::Empty};
        float width{};
        float scale{};
        float depth{};
        bool flip_x{};
    };
    ResidentVisual resident_visual(const Resident &u, const ResidentLogisticsView *cargo) {
        const auto position = resident_position(u);
        const auto feet = iso(position.x, position.y);
        const float scale = zoom * (young_residents.contains(u.id) ? .72f : 1.f);
        auto &animation = unit_animations[u.id];
        animation.update(position.x, position.y, animation_tick);
        static constexpr int appearances[]{1,0,2,3,0,1,2,3,0,1};
        const auto legacy = "walk." + std::to_string(appearances[(u.id-1)%10]) + "." +
                            std::to_string(animation.direction) + "." +
                            std::to_string(animation.frame());
        const auto load = resident_load(cargo ? cargo->item : ItemKind::Wood,
                                        cargo ? cargo->quantity : 0);
        const auto canonical = resident_sprite(u.id, load, animation.facing, animation.frame());
        const bool directional = art.ready() && art.contains(canonical);
        return {position,
                feet,
                {feet.x, feet.y + 6 * scale},
                directional ? canonical : legacy,
                load,
                (directional ? resident_sprite_width(load) : 12.5f) * scale,
                scale,
                position.x + position.y + .8f,
                !directional && animation.flipped};
    }
    const ResidentLogisticsView *resident_cargo(const ClientSession &w, int resident_id) const {
        const auto found = std::find_if(w.resident_logistics().begin(), w.resident_logistics().end(),
                                        [&](const auto &cargo) {
                                            return cargo.resident_id == resident_id && cargo.quantity;
                                        });
        return found == w.resident_logistics().end() ? nullptr : &*found;
    }
    struct ResidentHit { int id{}; float depth{}; };
    std::optional<ResidentHit> resident_hit(const ClientSession &w, float sx, float sy) {
        std::optional<ResidentHit> hit;
        for (const auto &u : w.residents()) {
            if (u.layer != active_layer) continue;
            const auto visual = resident_visual(u, resident_cargo(w, u.id));
            const bool covered = art.ready()
                ? hit_sprite(visual.sprite, visual.sprite_ground, visual.width,
                                      {sx, sy}, visual.flip_x)
                : std::hypot(sx - visual.feet.x, sy - (visual.feet.y - 10 * zoom)) < 22.f;
            // Residents are appended after buildings and retain their vector
            // order under stable_sort, so an equal-depth later resident is on top.
            if (covered && (!hit || visual.depth >= hit->depth)) hit = {u.id, visual.depth};
        }
        return hit;
    }
    SDL_FPoint animal_point(const Animal &a) const {
        const auto p = animal_position(a);
        return iso(p.x, p.y);
    }
    void sync_motion(const ClientSession &w, double seconds) {
        commit_motion(prepare_motion(w, seconds));
        const bool reset = std::exchange(motion_tracks_refresh_pending, false);
        if (reset) {
            resident_motion.clear(); animal_motion.clear(); unit_animations.clear();
        }
        if (!reset && motion_revision == w.revision()) return;
        motion_revision = w.revision();
        for (const auto &u : w.residents()) {
            auto &track = resident_motion[u.id];
            if (track.add({u.x, u.y, u.layer}, w.ticks())) unit_animations.erase(u.id);
            track.revision = motion_revision;
        }
        for (const auto &a : w.economy().wilderness.animals) {
            auto &track = animal_motion[a.id];
            track.add({a.x, a.y}, w.ticks());
            track.revision = motion_revision;
        }
        std::erase_if(resident_motion, [&](const auto &p) { return p.second.revision != motion_revision; });
        std::erase_if(animal_motion, [&](const auto &p) { return p.second.revision != motion_revision; });
        std::erase_if(unit_animations, [&](const auto &p) { return !resident_motion.contains(p.first); });
    }
    FrameBuilder *draw{};
    FrameBuilder *r{}; // Short alias retained by the drawing recipes below.
    float logical_w{1440.f}, logical_h{900.f};
    float cam_x{24}, cam_y{24}, zoom{1.30f};
    bool annotations_visible{true};
    MinimapProjection minimap_projection{};
    bool minimap_ready{};
    MinimapBounds explored_minimap_bounds{};
    render::Frame2D minimap_background_cache;
    bool minimap_background_dirty{true};
    float minimap_cache_logical_w{}, minimap_cache_logical_h{};
    std::vector<int> selected_buildings;
    bool pause{}, tutorial{true}, dragging{};
    int sim_speed{1};
    GatherCompletion gather_mode{GatherCompletion::OneDelivery};
    SDL_Keymod observed_modifiers{};
    bool modifiers_observed{};
    WorldLayer active_layer{WorldLayer::Surface};
    float last_mouse_x{}, last_mouse_y{};
    std::vector<int> selected, selection_before_drag;
    bool selecting{}, selection_additive{};
    float selection_x{},selection_y{};
    void select_box(const ClientSession &w,float x,float y) {
        selected=selection_additive?selection_before_drag:std::vector<int>{};
        for(const auto &u:w.residents()) {
            if(u.layer!=active_layer || u.health<=0) continue;
            const auto point=resident_point(u);
            const float sy=point.y-10*zoom;
            if(point.x>=std::min(x,selection_x) && point.x<=std::max(x,selection_x) &&
               sy>=std::min(y,selection_y) && sy<=std::max(y,selection_y) && !is_selected(u.id)) selected.push_back(u.id);
        }
    }

    BuildingKind armed{BuildingKind::Camp};
    bool build_mode{};
    std::optional<ProfessionAssignment> worksite_assignment;
    std::vector<int> worksite_residents;
    std::optional<std::string> worksite_error;
    bool legacy_hud_active{true};
    std::string status{"Ласкаво просимо до Рібтерри"};
    double status_at{}, now{};
    std::optional<std::string> pending_status;

    explicit Impl(render::Renderer2D &value) : renderer(value), art(value) {}
    ~Impl() {
        if (engine_water_normal != render::TextureHandle{})
            renderer.release_texture(engine_water_normal);
        if (engine_control_ground != render::TextureHandle{})
            renderer.release_texture(engine_control_ground);
    }

    std::pair<float, float> pointer(float x, float y) const {
        // main.cpp converts pointer events through the renderer's logical
        // presentation before dispatch, so coordinates are already 1440x900.
        return {x, y};
    }
    // Compact presentation snapshot, never a pointer into a live ClientSession.
    // Unknown cells use a sentinel; water uses a second sentinel at sea level.
    std::vector<std::int16_t> heights;
    std::vector<std::uint8_t> materials;
    std::vector<Biome> biome_samples;
    std::unordered_map<int,std::array<float,5>> grass_corners;
    struct Appearance { MaterialWeights weights; float fog; float light; TerrainRockFace rock; };
    std::unordered_map<int, Appearance> corner_appearance;
    int material_sample(int x, int y) const {
        const auto i = world_cell_index(x,y);
        return i && !materials.empty() ? materials[*i] : unknown_material;
    }
    Appearance appearance(int x, int y, int domain) {
        const int key = ((y-world_min)*(world_size+1) + x-world_min)*3 +
                        (domain == 14 ? 2 : liquid_material(domain) ? 1 : 0);
        if (const auto it = corner_appearance.find(key); it != corner_appearance.end()) return it->second;
        auto weights = terrain_material_weights(
            x,y,domain,[&](int a,int b){return material_sample(a,b);});
        const auto rock = terrain_rock_face(x,y,[&](int a,int b){return height_sample(a,b);});
        weights = terrain_slope_rock_weights(domain,weights,rock);
        const auto value = Appearance{
            weights,
            frontier_fog(x-.5f,y-.5f,[&](int a,int b){return material_sample(a,b)!=unknown_material;}),
            terrain_corner_light(x,y,render::legacy_elevation_screen_scale,[&](int a,int b){return height_sample(a,b);}),
            rock};
        corner_appearance.emplace(key,value);
        return value;
    }
    WorldLayer height_layer{WorldLayer::Surface};
    std::vector<Portal> terrain_portals;
    struct PickSurface { int x,y; std::array<SDL_FPoint,4> points; };
    std::vector<PickSurface> pick_surfaces;
    struct VisibleTile {
        int x, y;
        const Tile *tile;
    };
    struct DrawObject {
        float depth;
        int type, index;
        GroundCoverDetail cover{};
        ShoreDetail shore{};
        ResourceShrub shrub{};
    };
    // Compose-only scratch. Pointer values observe the current ClientSession and
    // are rebuilt before use; only container allocations survive between frames.
    std::vector<VisibleTile> visible_tiles_scratch;
    std::vector<DrawObject> draw_objects_scratch;
    std::unordered_map<std::uint64_t,std::vector<const Building *>> building_bins_scratch;
    std::vector<const Building *> no_nearby_buildings_scratch;
    std::unordered_map<int, const ResidentLogisticsView *> cargo_by_resident_scratch;
    std::unordered_map<int, const ConstructionSiteView *> site_by_building_scratch;
    std::unordered_map<int, int> level_by_building_scratch;
    WorldLayer pick_layer{WorldLayer::Surface};
    struct DeckReceiver { float x{}, y{}, half_x{}, half_y{}; };
    std::vector<DeckReceiver> contact_decks;
    bool contact_surface_queries{};
    float pick_cam_x{},pick_cam_y{},pick_zoom{};
    std::unordered_map<int, std::uint64_t> terrain_chunk_revisions;
    std::vector<std::uint8_t> trail_samples;
    struct TileMesh { render::Frame2D frame; SDL_FPoint origin; };
    std::unordered_map<int, TileMesh> tile_meshes;
    float mesh_zoom{};
    const ClientSession *terrain_session{};
    std::uint64_t terrain_revision{};
    bool terrain_winter{};
    void sync_terrain(const ClientSession &w) {
        // A client snapshot changes on unit motion too. Chunk revisions let the
        // presentation retain its 832x832 samples and meshes unless terrain in
        // that chunk actually changed.
        const bool snow = w.winter() && active_layer == WorldLayer::Surface;
        if (!heights.empty() && height_layer == active_layer && terrain_winter == snow &&
            terrain_session == &w && terrain_revision == w.revision()) return;

        std::size_t layer_chunks = 0;
        bool chunk_keys_changed = false;
        const auto cache_key = [](const ChunkKey &key) {
            return (key.cy - floor_div(world_min, chunk_size)) * (world_size / chunk_size) +
                   key.cx - floor_div(world_min, chunk_size);
        };
        for (const auto &chunk : w.chunks()) {
            if (chunk.key.layer != active_layer) continue;
            ++layer_chunks;
            chunk_keys_changed |= !terrain_chunk_revisions.contains(cache_key(chunk.key));
        }
        const bool full_reset = heights.empty() || height_layer != active_layer ||
                                terrain_winter != snow || terrain_session != &w ||
                                chunk_keys_changed ||
                                terrain_chunk_revisions.size() != layer_chunks;
        if (full_reset) minimap_background_dirty = true;
        terrain_session = &w;
        terrain_revision = w.revision();
        terrain_winter = snow;
        height_layer = active_layer;
        terrain_portals = w.portals();
        if (full_reset) {
            heights.assign(world_size * world_size, -32768);
            materials.assign(world_size * world_size, unknown_material);
            biome_samples.assign(world_size * world_size, Biome::Meadow);
            trail_samples.assign(world_size * world_size, 0);
            terrain_chunk_revisions.clear();
            tile_meshes.clear();
            corner_appearance.clear();
            grass_corners.clear();
            explored_minimap_bounds = {};
        }

        bool exploration_removed = false;
        auto erase_corner_halo = [&](int x, int y) {
            // Material/fog/light kernels reach at most five corners from a
            // changed sample. Grass includes a water-distance kernel, so use a
            // six-cell tile halo across chunk seams.
            for (int cy = y - 5; cy <= y + 5; ++cy)
                for (int cx = x - 5; cx <= x + 5; ++cx) {
                    const int corner = (cy-world_min)*(world_size+1) + cx-world_min;
                    for (int domain = 0; domain < 3; ++domain)
                        corner_appearance.erase(corner*3 + domain);
                    grass_corners.erase(corner);
                }
            for (int ty = y - 6; ty <= y + 6; ++ty)
                for (int tx = x - 6; tx <= x + 6; ++tx)
                    if (const auto index = world_cell_index(tx, ty))
                        tile_meshes.erase(static_cast<int>(*index));
        };
        for (const auto &chunk : w.chunks()) {
            if (chunk.key.layer != active_layer) continue;
            const int chunk_key = cache_key(chunk.key);
            const auto known = terrain_chunk_revisions.find(chunk_key);
            if (!full_reset && known != terrain_chunk_revisions.end() &&
                known->second == chunk.revision)
                continue;
            minimap_background_dirty = true;
            terrain_chunk_revisions[chunk_key] = chunk.revision;
            for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                const auto &t = chunk.tiles[i];
                const int x = chunk.key.cx * chunk_size + int(i % chunk_size);
                const int y = chunk.key.cy * chunk_size + int(i / chunk_size);
                const auto cell = *world_cell_index(x, y);
                const bool was_explored = heights[cell] != -32768;
                const auto next_height = !t.explored ? std::int16_t{-32768}
                    : t.terrain == Terrain::Water ? std::int16_t{-32767} : t.elevation;
                const auto next_material = !t.explored ? std::uint8_t{unknown_material}
                    : static_cast<std::uint8_t>(terrain_material(t, snow));
                const bool surface_changed = was_explored != t.explored ||
                    heights[cell] != next_height || materials[cell] != next_material ||
                    (t.explored && biome_samples[cell] != t.biome);
                const bool trail_changed = trail_samples[cell] != (t.explored ? t.trail_wear : 0);
                if (!surface_changed && !trail_changed) continue;
                exploration_removed |= was_explored && !t.explored;
                heights[cell] = next_height;
                materials[cell] = next_material;
                biome_samples[cell] = t.explored ? t.biome : Biome::Meadow;
                trail_samples[cell] = t.explored ? t.trail_wear : 0;
                if (surface_changed) erase_corner_halo(x, y);
                else {
                    tile_meshes.erase(static_cast<int>(cell));
                    for (const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{0,1},{-1,0},{0,-1}}})
                        if (const auto neighbor=world_cell_index(x+dx,y+dy))
                            tile_meshes.erase(static_cast<int>(*neighbor));
                }
                if (t.explored && !was_explored) {
                    explored_minimap_bounds.include_cell(float(x), float(y));
                }
            }
        }
        if (exploration_removed) {
            explored_minimap_bounds = {};
            for (std::size_t i = 0; i < heights.size(); ++i) if (heights[i] != -32768) {
                const auto cell = *cell_from_index(i, active_layer);
                explored_minimap_bounds.include_cell(float(cell.x), float(cell.y));
            }
        }
    }
    Tile height_sample(int x, int y) const {
        const auto i = world_cell_index(x, y);
        if (active_layer == WorldLayer::Mine || active_layer != height_layer || !i || heights.empty())
            return {};
        const auto h = heights[*i];
        if (h == -32768)
            return {};
        return {h == -32767 ? Terrain::Water : Terrain::Grass, 0, true, biome_samples[*i],
                static_cast<std::int16_t>(h == -32767 ? 0 : h)};
    }
    std::array<float,5> grass_at(int x,int y) {
        const int key=(y-world_min)*(world_size+1)+x-world_min;
        if(auto found=grass_corners.find(key);found!=grass_corners.end()) return found->second;
        std::array<float,5> weights{}; float total=0;
        for(int dy=-2;dy<=1;++dy) for(int dx=-2;dx<=1;++dx) {
            const auto t=height_sample(x+dx,y+dy);
            if(!t.explored || t.terrain==Terrain::Water) continue;
            const auto values=ecological_grass(t.biome,water_distance(float(x+dx),float(y+dy),[&](int a,int b){return height_sample(a,b);}));
            const float factor=1.f/(1.f+(dx+.5f)*(dx+.5f)+(dy+.5f)*(dy+.5f));
            for(int i=0;i<5;++i) weights[i]+=values[i]*factor;
            total+=factor;
        }
        if(total>0) for(auto &v:weights) v/=total;
        else weights[0]=1;
        grass_corners.emplace(key,weights); return weights;
    }
    float ground_height(float x, float y) const {
        return terrain_height(x, y, [this](int a, int b) { return height_sample(a, b); });
    }
    SDL_FPoint flat_iso(float x, float y) const {
        const auto projected = render::project_legacy_screen_xy(x, y, cam_x, cam_y, zoom);
        return {logical_w * .48f + projected.x,
                logical_h * .48f + projected.y};
    }
    SDL_FPoint flat_iso(int x, int y) const { return flat_iso(float(x), float(y)); }
    SDL_FPoint iso(float x, float y) const {
        const bool published_surface = contact_surface_queries && active_layer == WorldLayer::Surface &&
            pick_layer == active_layer && pick_cam_x == cam_x && pick_cam_y == cam_y && pick_zoom == zoom;
        if (published_surface && std::any_of(contact_decks.begin(), contact_decks.end(), [&](const DeckReceiver &deck) {
                return std::abs(x - deck.x) <= deck.half_x && std::abs(y - deck.y) <= deck.half_y;
            })) {
            const auto projected = render::contact_project_logical(
                {x - cam_x, y - cam_y, .08f}, {{logical_w * .48f, logical_h * .48f}, zoom});
            return {projected[0], projected[1]};
        }
        if (published_surface && std::isfinite(x) && std::isfinite(y) &&
            x >= world_min && x < world_max && y >= world_min && y < world_max) {
            const int cell_x = static_cast<int>(std::floor(x + .5f));
            const int cell_y = static_cast<int>(std::floor(y + .5f));
            const auto surface = std::find_if(pick_surfaces.begin(), pick_surfaces.end(), [&](const PickSurface &entry) {
                return entry.x == cell_x && entry.y == cell_y;
            });
            if (surface != pick_surfaces.end()) {
                // Read the last published triangles, even if a rejected candidate
                // refreshed private terrain caches from a newer session snapshot.
                const float u = x - cell_x + .5f, v = y - cell_y + .5f;
                const auto &p = surface->points;
                if (u >= v) return {p[0].x * (1.f-u) + p[1].x * (u-v) + p[2].x * v,
                                    p[0].y * (1.f-u) + p[1].y * (u-v) + p[2].y * v};
                return {p[0].x * (1.f-v) + p[2].x * u + p[3].x * (v-u),
                        p[0].y * (1.f-v) + p[2].y * u + p[3].y * (v-u)};
            }
        }
        auto p = flat_iso(x, y);
        p.y -= ground_height(x, y) * render::legacy_elevation_screen_scale * zoom;
        return p;
    }
    std::array<SDL_FPoint, 4> surface_points(int x, int y) const {
        return {iso(x - .5f, y - .5f), iso(x + .5f, y - .5f), iso(x + .5f, y + .5f), iso(x - .5f, y + .5f)};
    }
    SDL_FPoint iso(int x, int y) const { return iso(static_cast<float>(x), static_cast<float>(y)); }
    std::pair<int, int> world_at(float sx, float sy) const {
        const auto flat = render::reconstruct_render_xy(
            {(sx - logical_w * .48f) / zoom, (sy - logical_h * .48f) / zoom}, 0.f);
        const int bx = static_cast<int>(std::floor(cam_x + flat.x + .5f));
        const int by = static_cast<int>(std::floor(cam_y + flat.y + .5f));
        auto in_triangle = [=](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c) {
            auto cross = [](SDL_FPoint p, SDL_FPoint q, SDL_FPoint r) {
                return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
            };
            const SDL_FPoint p{sx, sy};
            const float s = cross(a, b, c);
            if (std::abs(s) < .001f)
                return false;
            return s > 0 ? cross(a, b, p) >= -.01f && cross(b, c, p) >= -.01f && cross(c, a, p) >= -.01f
                         : cross(a, b, p) <= .01f && cross(b, c, p) <= .01f && cross(c, a, p) <= .01f;
        };
        // Hit the geometry of the displayed frame before using an inverse projection.
        // In particular, elevated ridges must never fall back to their sea-level footprint.
        if (pick_layer==active_layer && pick_cam_x==cam_x && pick_cam_y==cam_y && pick_zoom==zoom)
            for (auto it=pick_surfaces.rbegin();it!=pick_surfaces.rend();++it) {
                const auto &p=it->points;
                if (in_triangle(p[0],p[1],p[2]) || in_triangle(p[0],p[2],p[3])) return {it->x,it->y};
            }
        // Height is bounded [-32,512]. Walk the screen ray front to back,
        // exactly reversing the painter order when a ridge occludes a valley.
        for (int depth = 92; depth >= -10; --depth)
            for (int x = bx + depth / 2 + 2; x >= bx + depth / 2 - 2; --x) {
                const int y = bx + by + depth - x;
                if (!height_sample(x, y).explored)
                    continue;
                const auto p = surface_points(x, y);
                if (in_triangle(p[0], p[1], p[2]) || in_triangle(p[0], p[2], p[3]))
                    return {x, y};
            }
        return {bx, by};
    }
    render::Point text(std::string_view s, Col = cream, bool tiny = false) {
        return renderer.measure_text(s, tiny ? 14.0f : 18.0f);
    }
    void label(std::string_view s, float x, float y, Col c = cream, bool tiny = false, float maxw = 0) {
        draw_frame->commands.emplace_back(
            render::TextDraw{render::Pass::Ui, std::string(s), {x, y}, tiny ? 14.0f : 18.0f, c, maxw});
    }
    render::Frame2D *draw_frame{};
    void panel(SDL_FRect q, Col bg = navy) {
        draw->fill(q.x, q.y, q.w, q.h, bg);
        draw->outline(q, gold);
        draw->line(q.x + 8, q.y + 7, q.x + q.w - 8, q.y + 7, {111, 88, 51, 255});
    }
    void button(SDL_FRect q, std::string_view s, bool active = false) {
        draw->fill(q.x, q.y, q.w, q.h, active ? Col{40, 91, 132, 255} : Col{25, 45, 62, 250});
        draw->outline(q, active ? Col{244, 192, 83, 255} : Col{100, 115, 124, 255});
        auto t = text(s, active ? cream : Col{196, 203, 199, 255}, true);
        label(s, q.x + (q.w - t.x) * .5f, q.y + (q.h - t.y) * .5f, active ? cream : Col{196, 203, 199, 255},
              true);
    }

    void cached_tile(int x, int y, const Tile &t) {
        if (!art.ready()) { draw_tile(x, y, t, now); return; }
        if (mesh_zoom != zoom || tile_meshes.size() > 4096) {
            tile_meshes.clear(); mesh_zoom = zoom;
        }
        const int key = static_cast<int>(*world_cell_index(x, y));
        auto [entry, inserted] = tile_meshes.try_emplace(key);
        auto &mesh = entry->second;
        if (inserted) {
            mesh.origin = flat_iso(0, 0);
            FrameBuilder builder(mesh.frame);
            auto *prior_draw = draw;
            auto *prior_r = r;
            auto *prior_frame = draw_frame;
            draw = r = &builder;
            draw_frame = &mesh.frame;
            draw_tile(x, y, t, now);
            draw = prior_draw; r = prior_r; draw_frame = prior_frame;
        }
        const auto origin = flat_iso(0, 0);
        draw->append_geometry(mesh.frame, {origin.x - mesh.origin.x, origin.y - mesh.origin.y});
    }
    void draw_yard(int x, int y, const Tile &t,
                   const std::vector<const Building *> &nearby_buildings) {
        if (active_layer != WorldLayer::Surface || !t.explored ||
            t.terrain == Terrain::Water || !art.contains("concept.ground.earth")) return;
        std::array<float,4> cover{}, contact{};
        const std::array<std::pair<float,float>,4> offsets{{
            {-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
        for (const auto *building : nearby_buildings) {
            const auto &b=*building;
            if (b.layer != active_layer || b.kind == BuildingKind::Road ||
                b.kind == BuildingKind::Bridge || std::abs(b.x-x)>4 || std::abs(b.y-y)>4) continue;
            // A terrain-draped apron belongs only to this actual site. The
            // front lobe follows the shared isometric entrance direction; it
            // neither joins arbitrary buildings nor writes simulation traffic.
            for (int i=0; i<4; ++i) {
                const float wx=x+offsets[i].first, wy=y+offsets[i].second;
                const auto mask=building_apron_mask(b.kind,float(b.x),float(b.y),wx,wy);
                cover[i]=std::max(cover[i],mask.cover);
                contact[i]=std::max(contact[i],mask.contact);
            }
        }
        if (std::none_of(cover.begin(),cover.end(),[](float v){return v>0;})) return;
        const auto points=surface_points(x,y);
        std::array<render::Point,4> corners{};
        std::array<Col,4> colors{};
        const std::array<std::pair<int,int>,4> grid{{{x,y},{x+1,y},{x+1,y+1},{x,y+1}}};
        for (int i=0;i<4;++i) {
            corners[i]={points[i].x,points[i].y};
            const auto a=appearance(grid[i].first,grid[i].second,terrain_material(t,winter));
            const auto light=static_cast<std::uint8_t>(std::clamp(a.light*255.f,0.f,255.f));
            colors[i]={light,light,light,static_cast<std::uint8_t>(
                cover[i]*std::lerp(205.f,86.f,a.weights[7])*(1.f-a.fog))};
        }
        const auto [u,du]=terrain_material_uv_axis(x);
        const auto [v,dv]=terrain_material_uv_axis(y);
        art.surface_colors(*draw_frame,"concept.ground.earth",corners,{u,v,du,dv},colors);
        // A terrain-draped contact shadow embeds the site without painting a
        // flat diamond or inventing a path between buildings. Shared world
        // corners keep the shadow continuous across the ground mesh.
        const auto first = draw_frame->vertices.size();
        poly(r,{points[0],points[1],points[2],points[3]}, {26,35,34,254});
        for (int i=0;i<4;++i)
            draw_frame->vertices[first+i].color.a=static_cast<std::uint8_t>(
                contact[i]*108.f*(1.f-appearance(grid[i].first,grid[i].second,terrain_material(t,winter)).fog));
    }
    void draw_tile(int x, int y, const Tile &t, double seconds) {
        auto p = iso(x, y);
        float hw = render::reference_tile_half_width * zoom;
        float hh = render::reference_tile_half_height * zoom;
        if (!t.explored) {
            diamond(r, p.x, p.y, hw + .4f, hh + .3f, {135, 156, 174, 255});
            return;
        }
        if (art.ready()) {
            const int material = terrain_material(t, winter);
            const auto points = surface_points(x, y);
            const std::array<render::Point,4> corners{{{points[0].x,points[0].y}, {points[1].x,points[1].y},
                                                       {points[2].x,points[2].y}, {points[3].x,points[3].y}}};
            const std::array<Appearance,4> samples{{appearance(x,y,material),appearance(x+1,y,material),
                                                    appearance(x+1,y+1,material),appearance(x,y+1,material)}};
            // Four cells preserve broad world-space continuity without enlarging individual
            // blades or pebbles into obvious repeated landmarks. Neighboring periods mirror,
            // so a non-tiling source always meets the same edge instead of exposing hard seams.
            const auto [region_x, region_w] = terrain_material_uv_axis(x);
            const auto [region_y, region_h] = terrain_material_uv_axis(y);
            const render::Rect region{region_x, region_y, region_w, region_h};
            const bool concept_ground = art.contains("concept.ground.grass");
            const bool calm_meadow = concept_ground || art.contains("terrain-rework.grass");
            const auto meadow_id = concept_ground ? "concept.ground.grass" : "terrain-rework.grass";
            const std::array<std::pair<int,int>,4> world_corners{{
                {x,y},{x+1,y},{x+1,y+1},{x,y+1}}};
            std::array<float,4> accumulated{};
            for (int layer = 0; layer < 16; ++layer) {
                std::array<Col,4> colors{};
                bool present = false;
                for (int i = 0; i < 4; ++i) {
                    float weight = samples[i].weights[layer];
                    const float wet_corner = samples[i].weights[4] + samples[i].weights[5] +
                                             samples[i].weights[6];
                    // The wet apron should read as a narrow bank, not a broad brown riverbed.
                    if (layer == 3 && t.biome != Biome::Desert)
                        weight *= .34f - .18f * wet_corner;
                    // Grade shared vertices, not the current tile domain, otherwise
                    // the two sides of a shoreline edge receive different alphas.
                    if (layer == 4 || layer == 5 || layer == 6)
                        weight *= .52f + 1.28f * wet_corner;
                    accumulated[i] += weight;
                    present |= weight > 0;
                    const float alpha = accumulated[i] > 0 ? weight/accumulated[i] : 0;
                    const auto [world_x, world_y] = world_corners[i];
                    // Shared world-corner fields shape broad meadow masses;
                    // adjacent tiles therefore receive identical colors on
                    // their common edge instead of becoming tinted diamonds.
                    const float macro = .58f*std::sin(world_x*.11f+world_y*.07f)+
                                        .42f*std::sin(world_x*.047f-world_y*.13f);
                    const float field = .965f + .04f*std::sin(world_x*.17f+world_y*.11f) +
                                         .025f*std::sin(world_x*.07f-world_y*.19f);
                    const float illumination = samples[i].light * field *
                                               (layer == 8 ? samples[i].rock.light : 1.f);
                    const auto lit = [&](int channel) {
                        return static_cast<std::uint8_t>(
                            std::lround(std::clamp(float(channel) * illumination, 0.f, 255.f)));
                    };
                    // The original material photographs are deliberately neutral. Grade the
                    // broad world materials here so the valley keeps its fresh alpine hierarchy
                    // without baking lighting into the atlas or changing simulation data.
                    auto grade =
                        layer == 0 ? std::array<int, 3>{248, 250, 207}
                        : layer == 4 ? std::array<int, 3>{170, 204, 236}
                        : layer == 5 ? std::array<int, 3>{204, 224, 225}
                        : layer == 6 ? std::array<int, 3>{159, 195, 224}
                        : layer == 7 ? std::array<int, 3>{250, 253, 255}
                        : layer == 8 ? std::array<int, 3>{211, 224, 245}
                                     : std::array<int, 3>{255, 250, 238};
                    if (layer == 0) {
                        grade[0]=std::clamp(int(std::lround(grade[0]*(1.f+.02f*macro))),0,255);
                        grade[1]=std::clamp(int(std::lround(grade[1]*(1.f+.025f*macro))),0,255);
                        grade[2]=std::clamp(int(std::lround(grade[2]*(1.f-.035f*macro))),0,255);
                    }
                    colors[i] = {lit(grade[0]), lit(grade[1]), lit(grade[2]),
                                 static_cast<std::uint8_t>(std::lround(255 * alpha))};
                }
                if (!present) continue;
                if (layer == 0) {
                    // Broad continuous patches share corner weights; no per-tile checkerboard.
                    art.surface_colors(*draw_frame,
                                       calm_meadow ? meadow_id : "terrain-materials.0",
                                       corners,region,colors);
                    if (calm_meadow) {
                        // Two incommensurate world-space periods break the visible
                        // mirrored flower/soil diamonds. Every corner still samples
                        // the same UV and blend weight from either adjacent cell.
                        const auto [u, du] = terrain_material_uv_axis(x + 17, 7);
                        const auto [v, dv] = terrain_material_uv_axis(y - 11, 7);
                        auto variation_colors = colors;
                        for (int i = 0; i < 4; ++i) {
                            const auto [gx, gy] = world_corners[i];
                            const float blend = .40f + .16f * std::sin(gx*.217f + gy*.173f);
                            variation_colors[i].a = static_cast<std::uint8_t>(
                                std::lround(colors[i].a * blend));
                        }
                        art.surface_colors(*draw_frame, meadow_id, corners,
                                           {u,v,du,dv}, variation_colors);
                    }
                    std::array<float,4> sum{};
                    sum.fill(6.f); // V02 keeps broad material zones dominant over micro-detail.
                    for (int grass=0; !calm_meadow && grass<5; ++grass) {
                        auto grass_colors=colors;
                        bool visible=false;
                        for (int i=0; i<4; ++i) {
                            const auto [gx,gy]=world_corners[i];
                            const float weight=grass_at(gx,gy)[grass];
                            sum[i]+=weight;
                            visible |= weight>0;
                            grass_colors[i].a=std::uint8_t(std::lround(colors[i].a*(sum[i]>0?weight/sum[i]:0)));
                        }
                        if(visible) art.surface_colors(*draw_frame,"grass.018."+std::to_string(grass),corners,region,grass_colors);
                    }
                } else {
                    const auto material_id = concept_ground && (layer == 4 || layer == 5 || layer == 6)
                        ? std::string("concept.ground.water")
                        : layer == 7 && art.contains("concept.ground.snow") ? std::string("concept.ground.snow")
                        : concept_ground && layer == 3 ? std::string("concept.ground.earth")
                        : concept_ground && layer == 8 ? std::string("concept.ground.rock")
                        : "terrain-materials." + std::to_string(layer);
                    art.surface_colors(*draw_frame,material_id,corners,region,colors);
                }
            }
            if (t.terrain == Terrain::Water)
                poly(r, {points[0],points[1],points[2],points[3]}, {22, 71, 112, 26});
            if (concept_ground && active_layer==WorldLayer::Surface && !winter) {
                // Broad, low-contrast patches break up the lawn without a tile
                // grid. Shared world-space vertices keep adjacent edges equal.
                std::array<Col,4> earth{};
                for (int i=0;i<4;++i) {
                    const auto [wx,wy]=world_corners[i];
                    const float field=std::sin(wx*.23f+wy*.16f)*.55f+
                                      std::sin(wy*.31f-wx*.12f)*.29f+
                                      std::sin(wx*.09f+wy*.27f)*.16f;
                    const float amount=std::clamp((field-.08f)*.52f,0.f,.34f);
                    const auto shade=static_cast<std::uint8_t>(std::clamp(samples[i].light*255.f,0.f,255.f));
                    earth[i]={shade,shade,shade,static_cast<std::uint8_t>(amount*samples[i].weights[0]*255.f)};
                }
                art.surface_colors(*draw_frame,"concept.ground.earth",corners,region,earth);
            }
            if (t.terrain != Terrain::Water && t.trail_wear > 0) {
                std::array<std::uint8_t,4> neighbors{};
                const std::array<std::pair<int,int>,4> directions{{{1,0},{0,1},{-1,0},{0,-1}}};
                for (int i=0;i<4;++i) {
                    const auto [dx,dy]=directions[i];
                    if (const auto index=world_cell_index(x+dx,y+dy); index && !trail_samples.empty())
                        neighbors[i]=trail_samples[*index];
                }
                constexpr int divisions=6;
                constexpr float step=1.f/divisions;
                for (int sy=0;sy<divisions;++sy) for (int sx=0;sx<divisions;++sx) {
                    std::array<render::Point,4> ribbon{};
                    std::array<Col,4> colors{};
                    const std::array<std::pair<int,int>,4> offsets{{{0,0},{1,0},{1,1},{0,1}}};
                    bool visible=false;
                    for (int i=0;i<4;++i) {
                        const float u=(sx+offsets[i].first)*step, v=(sy+offsets[i].second)*step;
                        const auto point=iso(float(x)+u-.5f,float(y)+v-.5f);
                        ribbon[i]={point.x,point.y};
                        const float amount=trail_ribbon_opacity(u-.5f,v-.5f,t.trail_wear,neighbors);
                        // Shared corner snow coverage also keeps paths continuous
                        // across alpine/temperate biome boundaries outside winter.
                        const float snow_factor=terrain_trail_snow_factor(
                            {samples[0].weights[7],samples[1].weights[7],
                             samples[2].weights[7],samples[3].weights[7]},u,v);
                        const float light=std::lerp(std::lerp(samples[0].light,samples[1].light,u),
                                                   std::lerp(samples[3].light,samples[2].light,u),v);
                        const auto shade=static_cast<std::uint8_t>(std::clamp(light*255.f,0.f,255.f));
                        colors[i]={shade,
                                   static_cast<std::uint8_t>(std::lround(shade*.93f)),
                                   static_cast<std::uint8_t>(std::lround(shade*.82f)),
                                   static_cast<std::uint8_t>(amount*255.f*snow_factor)};
                        visible|=colors[i].a>0;
                    }
                    if (visible) art.surface_colors(*draw_frame,
                        concept_ground?"concept.ground.earth":"terrain-materials.13",ribbon,
                        {region.x+sx*step*region.w,region.y+sy*step*region.h,
                         step*region.w,step*region.h},colors);
                }
            }
            // Fade the surface itself into the mist background. No upright fog quads.
            const auto first = draw_frame->vertices.size();
            poly(r,{points[0],points[1],points[2],points[3]}, {ground_mist.r,ground_mist.g,ground_mist.b,254});
            for (int i = 0; i < 4; ++i)
                draw_frame->vertices[first+i].color.a = static_cast<std::uint8_t>(std::lround(255*samples[i].fog));
            std::array<Col,4> haze;
            bool edge = false;
            for(int i=0;i<4;++i) {
                const float opacity = samples[i].fog;
                haze[i] = {ground_mist.r,ground_mist.g,ground_mist.b,
                           static_cast<std::uint8_t>(std::lround(90*opacity*(1-opacity)))};
                edge |= haze[i].a > 0;
            }
            if(edge) art.surface_colors(*draw_frame,"fog-masks.13",corners,region,haze);
            return;
        }
        Col base = green;
        if (t.terrain == Terrain::Water)
            base = {static_cast<std::uint8_t>(38 + 7 * std::sin(seconds + x)), 104, 147, 255};
        else if (t.terrain == Terrain::Forest)
            base = {56, 94, 58, 255};
        else if (t.terrain == Terrain::Rock)
            base = {95, 104, 94, 255};
        else if (t.terrain == Terrain::MineFloor)
            base = {67, 60, 53, 255};
        else if (t.terrain == Terrain::MineOre)
            base = {106, 85, 62, 255};
        else if ((x + y) % 3 == 0)
            base = green2;
        const auto surface = surface_points(x, y);
        poly(r, {surface[0], surface[1], surface[2], surface[3]}, base);
        if (t.terrain == Terrain::Water) {
            line(r, p.x - hw * .58f, p.y - 1, p.x + hw * .2f, p.y - hh * .38f, {101, 175, 194, 145});
            line(r, p.x - hw * .08f, p.y + hh * .38f, p.x + hw * .57f, p.y + 1, {25, 79, 125, 150});
        } else {
            line(r, p.x - hw, p.y, p.x, p.y + hh, {48, 75, 48, 80});
            line(r, p.x, p.y + hh, p.x + hw, p.y, {117, 143, 83, 65});
        }
        if (t.explored && t.terrain == Terrain::Grass && (x * 31 + y * 13) % 5 == 0) {
            line(r, p.x - 8 * zoom, p.y + 2 * zoom, p.x - 6 * zoom, p.y - 4 * zoom, {144, 157, 86, 145});
            line(r, p.x - 6 * zoom, p.y + 2 * zoom, p.x - 2 * zoom, p.y - 3 * zoom, {53, 91, 48, 180});
        }
        if (t.explored && t.terrain == Terrain::Grass && t.amount > 0) {
            disc(r, p.x - 8 * zoom, p.y - 3 * zoom, 5 * zoom, 2 * zoom, {36, 75, 41, 255});
            disc(r, p.x - 10 * zoom, p.y - 5 * zoom, 1.5f * zoom, 1.2f * zoom, {177, 67, 74, 255});
            disc(r, p.x - 6 * zoom, p.y - 6 * zoom, 1.5f * zoom, 1.2f * zoom, {216, 99, 86, 255});
        }
    }
    void tree(float x, float y, int salt, Biome biome) {
        salt=int(biome_hash(unsigned(int(x))*73856093u ^ unsigned(int(y))*19349663u)&0x7fffffffu);
        // Stable sub-cell offsets break the orchard grid while preserving the authoritative
        // resource cell and deterministic ordering.
        x += (int(unsigned(salt) % 29u) - 14) * .024f;
        y += (int((unsigned(salt) >> 5u) % 29u) - 14) * .024f;
        auto p = iso(x, y);
        if (art.ready()) {
            if (biome != Biome::Desert && art.contains("concept.pine.0")) {
                const bool leafy = unsigned(salt) % 10u == 0;
                auto id = leafy ? std::string("concept.leafy")
                                : "concept.pine." + std::to_string((unsigned(salt) >> 8) % 3u);
                if ((winter || snowy(biome)) && art.contains(id + ".winter")) id += ".winter";
                const unsigned age=(unsigned(salt)>>13)%10u;
                const float width=leafy ? 36.f+float(age)*1.5f
                                       : age<3 ? 21.f+float(age)*3.f : 39.f+float(age-3)*5.f;
                draw_sprite(id,{p.x,p.y+5*zoom},width*zoom,x,y);
                return;
            }
            const float shore=water_distance(x,y,[&](int a,int b){return height_sample(a,b);});
            const auto index=ecological_tree(biome,shore,winter,autumn,unsigned(salt));
            draw_sprite("trees.018."+std::to_string(index),{p.x,p.y+8*zoom},(48+(salt%5)*3)*zoom,x,y);
            return;
        }
        float z = zoom, sway = .8f * static_cast<float>(std::sin(now * .7 + salt));
        disc(r, p.x + 5 * z, p.y + 4 * z, 13 * z, 5 * z, {15, 34, 31, 105});
        fill(r, p.x - 2 * z, p.y - 14 * z, 4 * z, 18 * z, {91, 57, 33, 255});
        poly(r, {{p.x + sway, p.y - 48 * z}, {p.x - 16 * z, p.y - 18 * z}, {p.x + 16 * z, p.y - 18 * z}},
             {25, 65, 57, 255});
        poly(r, {{p.x + sway, p.y - 39 * z}, {p.x - 20 * z, p.y - 9 * z}, {p.x + 20 * z, p.y - 9 * z}},
             {31, 78, 64, 255});
        poly(r, {{p.x + sway, p.y - 29 * z}, {p.x - 18 * z, p.y - 1 * z}, {p.x + 18 * z, p.y - 1 * z}},
             {38, 92, 70, 255});
        line(r, p.x - 2 * z, p.y - 35 * z, p.x - 10 * z, p.y - 18 * z, {91, 137, 104, 155});
    }
    void rock(float x, float y, int salt, Biome biome = Biome::Meadow) {
        const auto variation = biome_hash(unsigned(salt));
        if (active_layer == WorldLayer::Surface) {
            x += (int(variation % 23u) - 11) * .025f;
            y += (int((variation >> 6) % 23u) - 11) * .025f;
        }
        auto p = iso(x, y);
        if (art.ready()) {
            if (active_layer == WorldLayer::Surface && art.contains("concept.cliff")) {
                const bool cliff = (biome == Biome::Mountain || biome == Biome::SnowMountain) &&
                                   variation % 7u == 0;
                std::string id = cliff ? "concept.cliff" :
                    !winter && !snowy(biome) && art.contains("concept.stone.0")
                    ? "concept.stone." + std::to_string(variation%2u) : "concept.boulders";
                if ((winter || snowy(biome)) && art.contains(id + ".winter")) id += ".winter";
                draw_sprite(id,{p.x,p.y+5*zoom},(cliff ? 51.f + float(unsigned(salt)%5u)*4.f
                                                 : 17.f + float(variation%7u)*2.5f)*zoom,x,y);
                return;
            }
            const bool mountain = active_layer == WorldLayer::Surface &&
                                  (biome == Biome::Mountain || biome == Biome::SnowMountain);
            const int index =
                mountain ? (winter || snowy(biome) ? 13 : 12) + 2 * int(biome_hash(std::uint32_t(salt)) % 2)
                : active_layer == WorldLayer::Mine ? 4 + salt % 7
                                                   : salt % 4;
            draw_sprite("rocks-mountains." + std::to_string(index),{p.x,p.y+7*zoom},
                (mountain ? 64 + int(biome_hash(std::uint32_t(salt)) % 5) * 7 : 40)*zoom,x,y);
            return;
        }
        float z = zoom;
        disc(r, p.x + 3 * z, p.y + 2 * z, 14 * z, 5 * z, {31, 43, 42, 100});
        poly(r,
             {{p.x - 11 * z, p.y},
              {p.x - 7 * z, p.y - 12 * z},
              {p.x + 3 * z, p.y - 17 * z},
              {p.x + 13 * z, p.y - 5 * z},
              {p.x + 10 * z, p.y + 3 * z}},
             {119, 126, 120, 255});
        poly(r, {{p.x - 7 * z, p.y - 12 * z}, {p.x + 3 * z, p.y - 17 * z}, {p.x + 1 * z, p.y - 4 * z}},
             {174, 176, 159, 255});
        if (salt % 3 == 0)
            poly(r, {{p.x + 5 * z, p.y - 4 * z}, {p.x + 13 * z, p.y - 5 * z}, {p.x + 10 * z, p.y + 3 * z}},
                 {69, 82, 84, 255});
    }
    void building(const Building &b, int level, const ConstructionSiteView *site = nullptr, bool local_snow = false) {
        auto p = iso(b.x, b.y);
        float z = zoom;
        const auto foundation_heights =
            terrain_corners(b.x, b.y, [this](int x, int y) { return height_sample(x, y); });
        const float base = *std::max_element(foundation_heights.begin(), foundation_heights.end());
        if (*std::min_element(foundation_heights.begin(), foundation_heights.end()) < base) {
            const auto feet = surface_points(b.x, b.y);
            auto top = feet;
            for (std::size_t i = 0; i < 4; ++i)
                top[i].y -= (base - foundation_heights[i]) * render::legacy_elevation_screen_scale * z;
            poly(r, {top[1], top[2], feet[2], feet[1]}, {77, 83, 87, 255});
            poly(r, {top[2], top[3], feet[3], feet[2]}, {110, 115, 112, 255});
            poly(r, {top[0], top[1], top[2], top[3]}, {142, 145, 132, 255});
            p = flat_iso(float(b.x), float(b.y));
            p.y -= base * render::legacy_elevation_screen_scale * z;
        }
        if (art.ready()) {
            const bool building_selected = annotations_visible &&
                std::find(selected_buildings.begin(), selected_buildings.end(), b.id) != selected_buildings.end();
            const auto visual = resolve_building_visual(b.kind, level, b.progress, winter || local_snow);
            if(b.kind==BuildingKind::Bridge && b.progress>=1) {
                for(const auto [dx,dy]:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                    const auto neighbor=height_sample(b.x+dx,b.y+dy);
                    if(!neighbor.explored || neighbor.terrain==Terrain::Water) continue;
                    const auto points=bridge_approach(b.x,b.y,dx,dy);
                    std::array<render::Point,4> ramp{};
                    for(int i=0;i<4;++i) {
                        const auto [wx,wy]=points[i];
                        const auto screen=iso(wx,wy);
                        ramp[i]={screen.x,screen.y-2*z};
                    }
                    art.surface(*draw_frame,"terrain-materials.13",ramp);
                    line(r,ramp[0].x,ramp[0].y,ramp[1].x,ramp[1].y,{114,76,43,255});
                    line(r,ramp[2].x,ramp[2].y,ramp[3].x,ramp[3].y,{114,76,43,255});
                }
            }
            if(b.kind==BuildingKind::Road){
                std::array<render::Point,4> road{};
                const std::array<std::pair<float,float>,4> corners{{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
                for(int i=0;i<4;++i){auto q=iso(float(b.x)+corners[i].first,float(b.y)+corners[i].second);road[i]={q.x,q.y-1*z};}
                art.surface(*draw_frame,"terrain-materials.13",road);
                return;
            }
            if (b.kind!=BuildingKind::Bridge && art.contains("fx.3"))
                art.tile(*draw_frame,"fx.3",{p.x+6*z,p.y+8*z},visual.width*.55f*z,
                    visual.width*.22f*z,render::Pass::WorldOrdered,{0,0,1,1},{20,23,18,100});
            const auto tint = visual.winter_tint ? Col{224, 234, 239, 255} : Col{255, 255, 255, 255};
            const render::Point sprite_ground{p.x, p.y + (b.kind == BuildingKind::Bridge ? 0 : 15) * z};
            const auto resolved = resolve_sprite_body(visual.asset_id,visual.width*z,tint);
            if (building_selected) {
                const float radius = std::clamp(1.2f*z,1.2f,2.4f);
                if (resolved.material) {
                    // This normal LegacyCombined composition owns a neutral
                    // stream. Bodies/legacy geometry appended since the last
                    // outline may leave a deferred neutral tail until finish.
                    if (draw_frame->surface_vertices.size() > draw_frame->vertices.size())
                        throw std::logic_error("Normal material companion prefix exceeds its vertex stream");
                    draw_frame->surface_vertices.resize(draw_frame->vertices.size());
                    material_art->outline_lit_sprite(*draw_frame,resolved.material->sprite_id,sprite_ground,zoom,
                        resolved.geometry_scale,radius,render::Pass::WorldOrdered,{69,210,247,235});
                } else art.outline_sprite(*draw_frame,visual.asset_id,sprite_ground,visual.width*z,
                    radius,render::Pass::WorldOrdered,{69,210,247,235});
            }
            draw_resolved_sprite(resolved,visual.asset_id,sprite_ground,visual.width*z,float(b.x),float(b.y),tint);
            if (annotations_visible && b.progress < 1) {
                float delivery = 0;
                if (site) {
                    const int required = site->required.wood + site->required.stone + site->required.food;
                    const int delivered = site->delivered.wood + site->delivered.stone + site->delivered.food;
                    delivery = required ? float(delivered) / required : 1;
                }
                fill(r, p.x - 24 * z, p.y + 18 * z, 48 * z, 7 * z, {10, 18, 25, 230});
                fill(r, p.x - 23 * z, p.y + 19 * z, 46 * z * std::clamp(delivery, 0.f, 1.f), 2 * z,
                     {91, 205, 239, 255});
                fill(r, p.x - 23 * z, p.y + 22 * z, 46 * z * std::clamp(b.progress, 0.f, 1.f), 2 * z, gold);
            }
            return;
        }
        disc(r, p.x + 7 * z, p.y + 5 * z, 28 * z, 7 * z, {19, 31, 30, 120});
        if (b.progress < 1) {
            // An unfinished building is a real site, with materials arriving before work.
            poly(r, {{p.x, p.y - 13 * z}, {p.x + 27 * z, p.y}, {p.x, p.y + 13 * z}, {p.x - 27 * z, p.y}},
                 {91, 72, 51, 255});
            for (const auto &corner : {SDL_FPoint{p.x - 24 * z, p.y}, SDL_FPoint{p.x + 24 * z, p.y},
                                       SDL_FPoint{p.x, p.y - 11 * z}, SDL_FPoint{p.x, p.y + 11 * z}})
                line(r, corner.x, corner.y, corner.x, corner.y - (12 + 28 * b.progress) * z,
                     {173, 128, 71, 255});
            float delivery = 0;
            if (site) {
                const int required = site->required.wood + site->required.stone + site->required.food;
                const int delivered = site->delivered.wood + site->delivered.stone + site->delivered.food;
                delivery = required ? static_cast<float>(delivered) / required : 1;
                if (delivered > 0)
                    fill(r, p.x - 14 * z, p.y - 8 * z, (8 + 15 * delivery) * z, 8 * z, {153, 105, 53, 255});
            }
            fill(r, p.x - 23 * z, p.y + 15 * z, 46 * z, 6 * z, {10, 18, 25, 230});
            fill(r, p.x - 22 * z, p.y + 16 * z, 44 * z * delivery, 2 * z, {91, 205, 239, 255});
            fill(r, p.x - 22 * z, p.y + 19 * z, 44 * z * b.progress, 2 * z, gold);
            return;
        }
        if (b.kind == BuildingKind::Farm) {
            poly(r, {{p.x, p.y - 11 * z}, {p.x + 25 * z, p.y}, {p.x, p.y + 11 * z}, {p.x - 25 * z, p.y}},
                 {136, 104, 51, 255});
            for (int i = -2; i <= 2; ++i)
                line(r, p.x - 18 * z, p.y + i * 3 * z, p.x + 18 * z, p.y + i * 3 * z, {226, 185, 77, 255});
        } else if (b.kind == BuildingKind::Stockpile) {
            for (int i = -2; i < 3; ++i) {
                fill(r, p.x + i * 7 * z, p.y - 10 * z - std::abs(i) * z, 6 * z, 16 * z, {113, 68, 37, 255});
                disc(r, p.x + i * 7 * z + 3 * z, p.y - 10 * z - std::abs(i) * z, 3 * z, 2 * z,
                     {184, 117, 55, 255});
            }
        } else {
            float w = (b.kind == BuildingKind::Camp ? 38 : 48) * z,
                  h = (b.kind == BuildingKind::Camp ? 25 : 35) * z;
            fill(r, p.x - w * .5f, p.y - h, w, h, {123, 76, 42, 255});
            for (int i = 0; i < 3; ++i)
                line(r, p.x - w * .45f, p.y - h + 6 * i * z, p.x + w * .45f, p.y - h + 6 * i * z,
                     {72, 46, 31, 255});
            poly(r,
                 {{p.x, p.y - h - 25 * z},
                  {p.x - w * .68f, p.y - h + 2 * z},
                  {p.x + w * .68f, p.y - h + 2 * z}},
                 {20, 43, 63, 255});
            poly(r, {{p.x, p.y - h - 20 * z}, {p.x, p.y - h - 25 * z}, {p.x + w * .68f, p.y - h + 2 * z}},
                 {43, 68, 87, 255});
            fill(r, p.x - 6 * z, p.y - 15 * z, 12 * z, 15 * z, {54, 37, 27, 255});
            fill(r, p.x + w * .22f, p.y - 15 * z, 7 * z, 8 * z,
                 {237, 162, 55, static_cast<std::uint8_t>(205 + 45 * std::sin(now * 2 + b.id))});
            if (b.kind == BuildingKind::House) {
                fill(r, p.x + 12 * z, p.y - h - 24 * z, 6 * z, 15 * z, {77, 72, 65, 255});
                disc(r, p.x + 17 * z + static_cast<float>(std::sin(now)) * 3 * z, p.y - h - 34 * z, 7 * z,
                     5 * z, {196, 204, 193, 75});
            }
            if (b.kind == BuildingKind::Camp) {
                line(r, p.x - 24 * z, p.y - h - 3 * z, p.x - 24 * z, p.y - h - 37 * z, {74, 46, 28, 255});
                poly(r,
                     {{p.x - 23 * z, p.y - h - 36 * z},
                      {p.x - 5 * z, p.y - h - 30 * z},
                      {p.x - 23 * z, p.y - h - 23 * z}},
                     {37, 79, 125, 255});
                poly(r,
                     {{p.x - 18 * z, p.y - h - 31 * z},
                      {p.x - 13 * z, p.y - h - 29 * z},
                      {p.x - 18 * z, p.y - h - 26 * z}},
                     gold);
                disc(r, p.x + 15 * z, p.y + 3 * z, 8 * z, 3 * z, {226, 128, 43, 130});
            }
        }
        if (b.progress < 1) {
            fill(r, p.x - 23 * z, p.y + 12 * z, 46 * z, 4 * z, {10, 18, 25, 220});
            fill(r, p.x - 22 * z, p.y + 13 * z, 44 * z * std::clamp(b.progress, 0.f, 1.f), 2 * z, gold);
        }
    }
    bool is_selected(int id) const {
        return std::find(selected.begin(), selected.end(), id) != selected.end();
    }
    std::set<int> young_residents;
    void resident(const Resident &u, const ResidentLogisticsView *cargo = nullptr) {
        const auto visual = resident_visual(u, cargo);
        const auto p = visual.feet;
        const float z = visual.scale;
        float bob = static_cast<float>(std::sin(now * 5 + u.id)) * ((u.job == Job::Move) ? 1.5f : 0.5f) * z;
        if (annotations_visible && is_selected(u.id)) {
            disc(r, p.x, p.y + 3 * z, 12 * z, 5 * z, {43, 182, 221, 75});
            SDL_FRect q{p.x - 12 * z, p.y - 2 * z, 24 * z, 10 * z};
            draw->outline(q, {91, 205, 239, 255});
        }
        if (art.ready()) {
            bob = 0;
            if (art.contains("fx.3"))
                art.tile(*draw_frame, "fx.3", {p.x, p.y + 5 * z},
                         resident_contact_shadow_half_width(visual.load) * z,
                         resident_contact_shadow_half_height * z, render::Pass::WorldOrdered, {0, 0, 1, 1},
                         {18, 22, 20, 112});
            draw_sprite(visual.sprite,visual.sprite_ground,visual.width,visual.position.x,visual.position.y,
                {255,255,255,255},visual.flip_x);
            if(annotations_visible && is_selected(u.id) && u.job==Job::Idle && (!cargo || !cargo->quantity) &&
               u.profession.wait_reason==ProfessionWaitReason::Socializing && !u.profession.manual_override) {
                disc(r,p.x,p.y-43*z,11*z,7*z,{237,227,202,240});
                for(int dot=-1;dot<=1;++dot) disc(r,p.x+dot*5*z,p.y-43*z,1.3f*z,1.3f*z,{35,52,62,255});
                line(r,p.x-4*z,p.y-38*z,p.x-6*z,p.y-34*z,{237,227,202,240});
            }
            if (u.job == Job::Farm && u.progress > 0) {
                const float swing = static_cast<float>(std::sin(now * 7 + u.id)) * 4 * z;
                line(r, p.x + 4*z, p.y - 8*z, p.x + 12*z + swing, p.y + 1*z, {158,108,58,255});
                line(r, p.x + 10*z + swing, p.y + 1*z, p.x + 15*z + swing, p.y + 3*z, {179,191,185,255});
                if (annotations_visible) {
                    fill(r,p.x-10*z,p.y+10*z,20*z,3*z,{15,30,25,230});
                    fill(r,p.x-10*z,p.y+10*z,20*z*u.progress,3*z,{212,186,78,255});
                }
            }
            if (!visual.sprite.starts_with("resident.") && cargo && cargo->quantity) {
                const int item = cargo->item == ItemKind::Wood ? 0 : cargo->item == ItemKind::Stone ? 1 : 2;
                art.sprite(*draw_frame, "icons-cursors." + std::to_string(item),
                           {p.x + 10 * z, p.y - 4 * z + bob}, 12 * z);
            }
            return;
        }
        disc(r, p.x + 3 * z, p.y + 4 * z, 7 * z, 3 * z, {15, 25, 28, 110});
        fill(r, p.x - 4 * z, p.y - 16 * z + bob, 8 * z, 16 * z, {26, 74, 121, 255});
        poly(r,
             {{p.x - 6 * z, p.y - 12 * z + bob},
              {p.x + 6 * z, p.y - 12 * z + bob},
              {p.x + 4 * z, p.y - 2 * z + bob},
              {p.x - 4 * z, p.y - 2 * z + bob}},
             {33, 92, 151, 255});
        disc(r, p.x, p.y - 19 * z + bob, 5 * z, 6 * z, {209, 156, 110, 255});
        poly(
            r,
            {{p.x - 6 * z, p.y - 21 * z + bob}, {p.x, p.y - 28 * z + bob}, {p.x + 6 * z, p.y - 21 * z + bob}},
            {17, 39, 59, 255});
        line(r, p.x - 2 * z, p.y, p.x - 4 * z, p.y + 6 * z, {48, 35, 28, 255});
        line(r, p.x + 2 * z, p.y, p.x + 5 * z, p.y + 6 * z, {48, 35, 28, 255});
        if (u.job == Job::Woodcut)
            line(r, p.x + 5 * z, p.y - 9 * z, p.x + 11 * z, p.y - 18 * z, gold);
        if (cargo && cargo->quantity) {
            const Col color = cargo->item == ItemKind::Wood    ? Col{175, 116, 58, 255}
                              : cargo->item == ItemKind::Stone ? Col{160, 172, 183, 255}
                                                               : Col{223, 177, 60, 255};
            fill(r, p.x + 3 * z, p.y - 12 * z + bob, 9 * z, 9 * z, color);
            line(r, p.x + 7 * z, p.y - 12 * z + bob, p.x + 7 * z, p.y - 3 * z + bob, {71, 48, 31, 255});
        }
    }

    void draw_world(const ClientSession &w) {
        const auto calendar = w.calendar();
        winter = calendar && calendar->season == Season::Winter && active_layer == WorldLayer::Surface;
        autumn = calendar && calendar->season == Season::Autumn;
        draw->pass(render::Pass::Terrain);
        // One small per-compose index serves both terrain-draped aprons and
        // decorative-clearance checks. Buildings are inserted into every bin
        // touched by their expanded presentation radius, so tile lookup stays
        // O(nearby buildings) without allocating a world-sized grid.
        constexpr int presentation_bin_size=8;
        const auto presentation_bin_key=[](int bin_x,int bin_y) {
            return (std::uint64_t(std::uint32_t(bin_x))<<32) |
                   std::uint64_t(std::uint32_t(bin_y));
        };
        auto &building_bins=building_bins_scratch;
        for (auto &[key, buildings] : building_bins) {
            (void)key;
            buildings.clear();
        }
        building_bins.reserve(building_bins.size()+w.buildings().size()*2+1);
        for (const auto &building : w.buildings()) {
            if (building.layer!=active_layer) continue;
            const auto apron=building_apron_visual(building.kind);
            const int range=static_cast<int>(std::ceil(
                apron.radius+apron.entrance_reach+1.f));
            const int min_x=floor_div(building.x-range,presentation_bin_size);
            const int max_x=floor_div(building.x+range,presentation_bin_size);
            const int min_y=floor_div(building.y-range,presentation_bin_size);
            const int max_y=floor_div(building.y+range,presentation_bin_size);
            for (int bin_y=min_y;bin_y<=max_y;++bin_y)
                for (int bin_x=min_x;bin_x<=max_x;++bin_x)
                    building_bins[presentation_bin_key(bin_x,bin_y)].push_back(&building);
        }
        const auto &no_nearby_buildings=no_nearby_buildings_scratch;
        const auto nearby_buildings=[&](int x,int y)->const std::vector<const Building *> & {
            const auto found=building_bins.find(presentation_bin_key(
                floor_div(x,presentation_bin_size),floor_div(y,presentation_bin_size)));
            return found==building_bins.end()?no_nearby_buildings:found->second;
        };
        // Unknown space is a quiet mist field; its border fades along the ground mesh.
        auto &visible=visible_tiles_scratch;
        visible.clear();
        visible.reserve(pick_surfaces.size());
        for (const auto &chunk : w.chunks())
            if (chunk.key.layer == active_layer) {
                const int x0 = chunk.key.cx * chunk_size, y0 = chunk.key.cy * chunk_size;
                const auto top = flat_iso(x0, y0), left = flat_iso(x0, y0 + chunk_size - 1),
                           right = flat_iso(x0 + chunk_size - 1, y0),
                           bottom = flat_iso(x0 + chunk_size - 1, y0 + chunk_size - 1);
                if (right.x < -100 || left.x > logical_w + 100 ||
                    bottom.y < -100 - 32 * render::legacy_elevation_screen_scale * zoom ||
                    top.y > logical_h + 200 + 512 * render::legacy_elevation_screen_scale * zoom)
                    continue;
                for (std::size_t i = 0; i < chunk.tiles.size(); ++i) {
                    if (!chunk.tiles[i].explored)
                        continue;
                    int x = chunk.key.cx * chunk_size + static_cast<int>(i % chunk_size);
                    int y = chunk.key.cy * chunk_size + static_cast<int>(i / chunk_size);
                    auto p = iso(x, y);
                    if (p.x >= -100 && p.x <= logical_w + 100 &&
                        p.y >= -100 && p.y <= logical_h + 200)
                        visible.push_back({x, y, &chunk.tiles[i]});
                }
            }
        std::sort(visible.begin(), visible.end(), [](const auto &a, const auto &b) {
            return a.x + a.y != b.x + b.y ? a.x + a.y < b.x + b.y : a.x < b.x;
        });
        pick_surfaces.clear();
        pick_layer=active_layer; pick_cam_x=cam_x; pick_cam_y=cam_y; pick_zoom=zoom;
        for(const auto &cell:visible) pick_surfaces.push_back({cell.x,cell.y,surface_points(cell.x,cell.y)});
        draw->pass(render::Pass::WorldOrdered);
        auto &objects=draw_objects_scratch;
        objects.clear();
        objects.reserve(visible.size()+w.buildings().size()+w.residents().size()+
                        w.economy().wilderness.animals.size());
        for (std::size_t i = 0; i < visible.size(); ++i)
            objects.push_back({float(visible[i].x + visible[i].y) - .5f, 5, static_cast<int>(i)});
        for (const auto &cell : visible) {
            int x = cell.x, y = cell.y;
            auto &t = *cell.tile;
            const auto &cell_buildings=nearby_buildings(x,y);
            if (std::any_of(cell_buildings.begin(),cell_buildings.end(),[&](const auto *building) {
                    return building->x==x && building->y==y;
                })) continue;
            const auto i = *world_cell_index(x, y);
            // Presentation-only undergrowth. Stable placement, culled with the visible cells.
            const bool temperate_grass = active_layer == WorldLayer::Surface &&
                t.terrain == Terrain::Grass && !winter && !snowy(t.biome) &&
                t.biome != Biome::Desert;
            const bool near_water = temperate_grass &&
                water_distance(float(x), float(y), [&](int a, int b) { return height_sample(a, b); }) < 1.75f;
            const bool clear_of_site=std::none_of(
                cell_buildings.begin(),cell_buildings.end(),[&](const auto *building) {
                    return !building_apron_clear(building->kind,float(building->x),float(building->y),
                                                 float(x),float(y));
                });
            std::array<TerrainRockFace,4> rock_corners{};
            if (active_layer == WorldLayer::Surface && t.terrain == Terrain::Grass && t.amount == 0) {
                const int material = terrain_material(t, winter);
                rock_corners = {appearance(x,y,material).rock, appearance(x+1,y,material).rock,
                                appearance(x+1,y+1,material).rock, appearance(x,y+1,material).rock};
            }
            // Rock outcrops follow the real land/water boundary. Their stable
            // irregular spacing is decorative, never an extra resource node.
            if (active_layer == WorldLayer::Surface) {
                const auto add_rock = [&](const ShoreDetail &point) {
                    if (std::any_of(cell_buildings.begin(),cell_buildings.end(),[&](const auto *b) {
                            return !building_apron_clear(b->kind,float(b->x),float(b->y),
                                                         x+point.dx,y+point.dy);
                        })) return;
                    if (art.contains(shore_detail_sprites[point.variant]))
                        objects.push_back({float(x+y)+shore_detail_depth(point),
                                           8, static_cast<int>(i), {}, point});
                };
                const auto relief = relief_details(x,y,t,winter,clear_of_site,rock_corners);
                for (unsigned detail = 0; detail < relief.count; ++detail)
                    add_rock(relief.details[detail]);
                // One shared rock budget: a steep bank cannot receive both a
                // slope cluster and the normal shoreline stones in the same cell.
                if (relief.count == 0) {
                    const auto shore = shore_details(x,y,t,winter,clear_of_site,
                                                     [&](int a,int b) { return height_sample(a,b); });
                    for (unsigned detail = 0; detail < shore.count; ++detail)
                        add_rock(shore.details[detail]);
                }
            }
            if (active_layer == WorldLayer::Surface) {
                const auto cover = ground_cover(x, y, t, winter, clear_of_site, near_water);
                for (unsigned detail = 0; detail < cover.count; ++detail) {
                    const auto &point = cover.details[detail];
                    const float u = point.dx + .5f, v = point.dy + .5f;
                    const float rock = std::lerp(
                        std::lerp(rock_corners[0].exposure,rock_corners[1].exposure,u),
                        std::lerp(rock_corners[3].exposure,rock_corners[2].exposure,u),v);
                    // Keep grass pockets at a cap; do not paint flowers over a
                    // bare face or clear a square tile when just one corner rises.
                    constexpr float bare_rock_exposure = .55f;
                    if (point.variant < 3 && rock >= bare_rock_exposure) continue;
                    if (std::any_of(cell_buildings.begin(),cell_buildings.end(),[&](const auto *b) {
                            return !building_apron_clear(b->kind,float(b->x),float(b->y),
                                                         x+point.dx,y+point.dy);
                        })) continue;
                    if (art.contains(ground_cover_sprites[point.variant]))
                        objects.push_back({float(x+y)+ground_cover_depth(point),
                                           7, static_cast<int>(i), point});
                }
            }
            // Every real resource tile needs a visible harvest target. Vary
            // silhouette/scale/position, never hide a gatherable cell at random.
            if (t.terrain == Terrain::Forest)
                objects.push_back({float(x + y) + .15f, 0, static_cast<int>(i)});
            else if (t.terrain == Terrain::MineOre || t.terrain == Terrain::Rock)
                objects.push_back({float(x + y) + .1f, 1, static_cast<int>(i)});
            else if (art.ready() && t.terrain == Terrain::Grass && t.amount > 0) {
                const auto shrub = resource_shrub(x,y,winter || snowy(t.biome));
                objects.push_back({float(x+y)+resource_shrub_depth(shrub),
                                   4,static_cast<int>(i),{},{},shrub});
            }
        }
        for (size_t i = 0; i < w.buildings().size(); ++i)
            if (w.buildings()[i].layer == active_layer)
                objects.push_back(
                    {float(w.buildings()[i].x + w.buildings()[i].y) + .5f, 2, static_cast<int>(i)});
        for (size_t i = 0; i < w.residents().size(); ++i)
            if (w.residents()[i].layer == active_layer) {
                const auto p = resident_position(w.residents()[i]);
                objects.push_back({p.x + p.y + .8f, 3, static_cast<int>(i)});
            }
        if(active_layer==WorldLayer::Surface)for(std::size_t i=0;i<w.economy().wilderness.animals.size();++i){const auto&a=w.economy().wilderness.animals[i];if(a.health>0&&visible_animal(w,a)){const auto p=animal_position(a);objects.push_back({p.x+p.y+.7f,6,int(i)});}}
        std::stable_sort(objects.begin(), objects.end(), [](auto &a, auto &b) { return a.depth < b.depth; });
        auto &cargo_by_resident=cargo_by_resident_scratch;
        cargo_by_resident.clear();
        cargo_by_resident.reserve(w.resident_logistics().size());
        for (const auto &cargo : w.resident_logistics())
            if (cargo.quantity)
                cargo_by_resident.emplace(cargo.resident_id, &cargo);
        auto &site_by_building=site_by_building_scratch;
        site_by_building.clear();
        site_by_building.reserve(w.construction_sites().size());
        for (const auto &site : w.construction_sites())
            site_by_building.emplace(site.building_id, &site);
        auto &level_by_building=level_by_building_scratch;
        level_by_building.clear();
        level_by_building.reserve(w.economy().upgrades.size());
        for (const auto &upgrade : w.economy().upgrades)
            level_by_building.emplace(upgrade.building_id, upgrade.level);
        for (auto o : objects) {
            const auto first_vertex = draw_frame->vertices.size();
            float object_fog = 0;
            if (o.type != 5) {
                float x{},y{};
                if(o.type == 2) { x=float(w.buildings()[o.index].x); y=float(w.buildings()[o.index].y); }
                else if(o.type == 3) { const auto p=resident_position(w.residents()[o.index]); x=p.x; y=p.y; }
                else if(o.type == 6) { const auto p=animal_position(w.economy().wilderness.animals[o.index]); x=p.x; y=p.y; }
                else {
                    const auto c=*cell_from_index(o.index,active_layer); x=float(c.x); y=float(c.y);
                    if(o.type == 7) { x+=o.cover.dx; y+=o.cover.dy; }
                    else if(o.type == 8) { x+=o.shore.dx; y+=o.shore.dy; }
                    else if(o.type == 4) { x+=o.shrub.dx; y+=o.shrub.dy; }
                }
                object_fog = frontier_fog(x,y,[&](int a,int b){return material_sample(a,b)!=unknown_material;});
            }
            if (o.type == 8) {
                const auto cell = *cell_from_index(static_cast<std::size_t>(o.index), active_layer);
                const auto p = iso(cell.x+o.shore.dx,cell.y+o.shore.dy);
                // Loose bank/slope silhouettes are not actionable deposits.
                // Snow is painted in the paired asset, not a gray color filter.
                art.sprite(*draw_frame,shore_detail_sprites[o.shore.variant],
                           {p.x,p.y+3*zoom},o.shore.width*zoom,
                           render::Pass::WorldOrdered,
                           Col{255,255,255,235});
            } else if (o.type == 7) {
                const auto cell = *cell_from_index(static_cast<std::size_t>(o.index), active_layer);
                const auto p = iso(cell.x + o.cover.dx, cell.y + o.cover.dy);
                art.sprite(*draw_frame, ground_cover_sprites[o.cover.variant], {p.x,p.y+2*zoom},
                           o.cover.width*zoom, render::Pass::WorldOrdered,
                           {o.cover.shade,o.cover.shade,o.cover.shade,245});
            } else if (o.type == 6) {
                const auto&a=w.economy().wilderness.animals[o.index];auto p=animal_point(a);
                const char*names[]={"deer","hare","boar","wolf"};
                const int frame=a.behaviour==AnimalBehaviour::Resting?0:animal_walk_frame(animation_tick);
                const bool snow=winter||snowy(w.tile(int(a.x),int(a.y)).biome);
                art.sprite(*draw_frame,std::string("animal.")+names[int(a.species)]+(snow?".winter":".summer")+std::to_string(frame),{p.x,p.y+5*zoom},(a.species==AnimalSpecies::Hare?12.f:30.f)*zoom);
            }else if (o.type == 5) {
                const auto &cell = visible[o.index];
                cached_tile(cell.x, cell.y, *cell.tile);
                draw_yard(cell.x,cell.y,*cell.tile,
                          nearby_buildings(cell.x,cell.y));
                for (const auto &portal : terrain_portals) {
                    const auto entrance = active_layer == WorldLayer::Surface ? portal.surface : portal.mine;
                    if (entrance.x != cell.x || entrance.y != cell.y) continue;
                    if (active_layer == WorldLayer::Surface) {
                        const bool occupied = std::any_of(w.buildings().begin(), w.buildings().end(), [&](const auto &b) {
                            return b.kind == BuildingKind::Mine && b.x == cell.x && b.y == cell.y;
                        });
                        if (!occupied && art.ready()) {
                            const auto p = iso(cell.x, cell.y);
                            draw_sprite((winter || snowy(cell.tile->biome)) ? "building.mine.winter" : "building.mine.ready",
                                {p.x,p.y+15*zoom},building_definition(BuildingKind::Mine).width*zoom,float(cell.x),float(cell.y));
                        }
                        continue;
                    }
                    const auto p = iso(cell.x, cell.y);
                    diamond(r, p.x, p.y, 20 * zoom, 10 * zoom, {36, 25, 20, 255});
                    for (int step = 0; step < 4; ++step)
                        line(r, p.x + (-8 + step * 4) * zoom, p.y + (-7 + step * 3) * zoom,
                             p.x + (-13 + step * 4) * zoom, p.y + (-3 + step * 3) * zoom, gold);
                }
            } else if (o.type < 2) {
                const auto cell = *cell_from_index(static_cast<std::size_t>(o.index), active_layer);
                int x = cell.x, y = cell.y;
                auto p = iso(x, y);
                if (p.x < -100 || p.x > logical_w+100 || p.y < -100 || p.y > logical_h+120)
                    continue;
                o.type == 0 ? tree(static_cast<float>(x), static_cast<float>(y), o.index,
                                   w.tile(x, y, active_layer).biome)
                            : rock(static_cast<float>(x), static_cast<float>(y), o.index,
                                   w.tile(x, y, active_layer).biome);
            } else if (o.type == 2) {
                const auto &b = w.buildings()[o.index];
                const auto site = site_by_building.find(b.id);
                const auto level = level_by_building.find(b.id);
                building(b, level == level_by_building.end() ? 1 : level->second,
                         site == site_by_building.end() ? nullptr : site->second,
                         snowy(w.tile(b.x, b.y, b.layer).biome));
            } else if (o.type == 3) {
                const auto &u = w.residents()[o.index];
                const auto cargo = cargo_by_resident.find(u.id);
                auto displayed=u;
                for(const auto &profile:w.resident_professions()) if(profile.resident_id==u.id) {
                    displayed.profession.wait_reason=profile.wait_reason;
                    displayed.profession.manual_override=profile.manual_override;
                    break;
                }
                resident(displayed, cargo == cargo_by_resident.end() ? nullptr : cargo->second);
            } else {
                const auto cell = *cell_from_index(static_cast<std::size_t>(o.index), active_layer);
                const auto p = iso(cell.x+o.shrub.dx,cell.y+o.shrub.dy);
                const auto *sprite = resource_shrub_sprites[o.shrub.variant];
                if (!art.contains(sprite)) {
                    const bool snow = o.shrub.variant >= 4;
                    sprite = snow ? "concept.berries.winter" : "concept.berries";
                    if (!art.contains(sprite)) sprite = snow ? "nature.9" : "nature.8";
                }
                art.sprite(*draw_frame,sprite,{p.x,p.y+4*zoom},o.shrub.width*zoom);
            }
            if(object_fog > 0)
                for(auto i=first_vertex;i<draw_frame->vertices.size();++i)
                    draw_frame->vertices[i].color.a = static_cast<std::uint8_t>(std::lround(draw_frame->vertices[i].color.a*(1-object_fog)));
        }
        // Small animated highlights over already revealed water; the terrain mesh stays cached.
        if (active_layer == WorldLayer::Surface && art.contains("fx.4")) {
            unsigned count = 0;
            unsigned mist_count = 0;
            for (const auto &cell : visible) {
                if (cell.tile->terrain != Terrain::Water) continue;
                const bool covered_by_bridge = std::any_of(w.buildings().begin(), w.buildings().end(),
                    [&](const Building &building) {
                        return building.kind == BuildingKind::Bridge && building.progress >= 1 &&
                               building.layer == WorldLayer::Surface && building.x == cell.x &&
                               building.y == cell.y;
                    });
                if (covered_by_bridge) continue;
                const auto seed = biome_hash(unsigned(cell.x)*73u ^ unsigned(cell.y)*151u);
                const auto p = iso(cell.x,cell.y);
                const float wave = .5f+.5f*std::sin(float(now)*.65f+float(seed%100));
                const float clear = 1-frontier_fog(float(cell.x),float(cell.y),
                    [&](int a,int b){return material_sample(a,b)!=unknown_material;});
                bool shoreline = false;
                for (const auto [dx,dy] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                    const auto neighbor = height_sample(cell.x+dx,cell.y+dy);
                    shoreline |= neighbor.explored && neighbor.terrain != Terrain::Water;
                }
                if (shoreline) {
                    // A quiet connected edge is more legible than isolated bright foam stamps.
                    art.tile(*draw_frame,"fx.5",{p.x,p.y},34*zoom,12*zoom,
                        render::Pass::WorldOrdered,{0,0,1,1},
                        {222,232,224,static_cast<std::uint8_t>((24+16*wave)*clear)});
                    if (seed%5==0 && mist_count++ < 10) {
                        const float drift = std::sin(float(now)*.07f+float(seed%19));
                        art.tile(*draw_frame,"fx.2",{p.x+drift*12*zoom,p.y-12*zoom},
                            80*zoom,24*zoom,render::Pass::WorldOrdered,{0,0,1,1},
                            {190,206,208,static_cast<std::uint8_t>((winter?30:18)*clear)});
                    }
                }
                if (seed % 5 == 0 && count++ < 96) {
                    const auto alpha = static_cast<std::uint8_t>((18 + 12 * wave) * clear);
                    art.tile(*draw_frame,"fx.4",{p.x+std::sin(float(now)*.3f)*3*zoom,p.y},
                             28*zoom,10*zoom,render::Pass::WorldOrdered,{0,0,1,1},
                             {190,219,231,alpha});
                }
            }
        }
        // Match the former local-container lifetime: no observer pointer remains
        // stored after world composition, while vector and bucket storage is reusable.
        visible.clear();
        for (auto &[key, buildings] : building_bins) {
            (void)key;
            buildings.clear();
        }
        cargo_by_resident.clear();
        site_by_building.clear();
        level_by_building.clear();
        draw->pass(render::Pass::Fog);
        if (art.ready()) {
            if (annotations_visible && build_mode) {
                const auto [x, y] = world_at(last_mouse_x, last_mouse_y);
                if (inside_world(x, y)) {
                    auto p = iso(x, y);
                    const auto points = surface_points(x, y);
                    const auto preview_heights =
                        terrain_corners(x, y, [this](int a, int b) { return height_sample(a, b); });
                    p.y -= (*std::max_element(preview_heights.begin(), preview_heights.end()) -
                            ground_height(float(x), float(y))) *
                           render::legacy_elevation_screen_scale * zoom;
                    const bool valid = w.can_build(armed, x, y, active_layer);
                    const auto visual = resolve_building_visual(armed, 1, 0.f, false);
                    poly(r, {points[0], points[1], points[2], points[3]},
                         valid ? Col{70, 205, 150, 100} : Col{220, 70, 60, 100});
                    art.sprite(*draw_frame, visual.asset_id,
                               {p.x, p.y + 15 * zoom}, visual.width * zoom, render::Pass::Fog,
                               valid ? Col{180, 255, 220, 150} : Col{255, 140, 130, 150});
                }
            }
            return;
        }
        fill(r, 0, 96, logical_w, 28, {0, 0, 0, 82});
        fill(r, 0, 835, logical_w, 65, {0, 0, 0, 120});
    }

    void atmosphere(const ClientSession &w) {
        if (!art.contains("fx.0")) return;
        const auto calendar = w.calendar();
        const float night = active_layer == WorldLayer::Surface
            ? (calendar ? calendar->night_amount : 0.f) : 1.f;
        unsigned emitters = 0;
        for (const auto &b : w.buildings()) {
            if (b.layer != active_layer || b.progress < 1 || !w.tile(b.x,b.y,b.layer).explored) continue;
            auto p = iso(b.x,b.y);
            if (p.x < -100 || p.x > logical_w+100 || p.y < 0 || p.y > logical_h+100) continue;
            if (emitters++ >= 48) break;
            const auto building_heights = terrain_corners(b.x,b.y,[&](int x,int y){return height_sample(x,y);});
            p.y -= (*std::max_element(building_heights.begin(),building_heights.end())-ground_height(float(b.x),float(b.y)))*render::legacy_elevation_screen_scale*zoom;
            int level = 1;
            for (const auto &upgrade : w.economy().upgrades)
                if (upgrade.building_id == b.id) {
                    level = upgrade.level;
                    break;
                }
            const float width = resolve_building_visual(b.kind, level, b.progress, winter).width * zoom;
            const float clear = 1-frontier_fog(float(b.x),float(b.y),
                [&](int x,int y){return material_sample(x,y)!=unknown_material;});
            const bool working = std::any_of(w.economy().orders.begin(), w.economy().orders.end(),
                [&](const auto &order) {
                    return order.building_id == b.id && production_active(order) &&
                           order.phase == ProductionPhase::Working;
                });
            const bool hearth = b.kind==BuildingKind::House || b.kind==BuildingKind::Kitchen ||
                b.kind==BuildingKind::Tavern || b.kind==BuildingKind::Smelter;
            if (hearth && working) {
                const int puffs = zoom < .8f ? 2 : 4;
                for (int i=0;i<puffs;++i) {
                    const float phase = std::fmod(float(now)*.105f+float(b.id%19)*.071f+float(i)/puffs,1.f);
                    const float fade = std::sin(phase*3.14159265f);
                    const float size = (10+phase*25)*zoom;
                    const render::Point origin{p.x+width*.04f+phase*16*zoom+
                        std::sin(float(now)*.4f+b.id)*phase*3*zoom,
                        p.y-width*.70f-phase*42*zoom};
                    art.sprite(*draw_frame,i%2 ? "fx.1" : "fx.0",origin,size,render::Pass::Fog,
                        {190,190,184,static_cast<std::uint8_t>(fade*92*clear)});
                }
            }
            if (night > .05f && b.kind!=BuildingKind::Road && b.kind!=BuildingKind::Bridge &&
                b.kind!=BuildingKind::ClayPit && b.kind!=BuildingKind::Stockpile) {
                const float flicker=.92f+.08f*std::sin(float(now)*2.4f+b.id);
                art.tile(*draw_frame,"fx.3",{p.x,p.y+4*zoom},width*.48f,width*.25f,
                    render::Pass::Fog,{0,0,1,1},{255,171,67,static_cast<std::uint8_t>(night*90*clear*flicker)});
                art.sprite(*draw_frame,"fx.3",{p.x-width*.15f,p.y-width*.18f},width*.24f,
                    render::Pass::Fog,{255,189,85,static_cast<std::uint8_t>(night*105*clear*flicker)});
            }
            if ((b.kind==BuildingKind::Camp || b.kind==BuildingKind::Smelter) && working) {
                const float flicker=.88f+.12f*std::sin(float(now)*4+b.id);
                art.sprite(*draw_frame,"fx.6",{p.x+width*.10f,p.y+6*zoom},12*zoom*flicker,
                    render::Pass::Fog,{255,173,60,static_cast<std::uint8_t>(170*clear)});
            }
        }
    }

    void minimap(const ClientSession &w) {
        draw->pass(render::Pass::Ui);
        const float map_x = logical_w - 300.f, map_y = logical_h - 190.f;
        constexpr float map_w = 210.f, map_h = 162.f;
        constexpr int columns = 140, rows = 108;
        constexpr float pixel_w=map_w/columns, pixel_h=map_h/rows;

        if (!explored_minimap_bounds.ready) {
            minimap_ready=false;
            fill(r, map_x, map_y, map_w, map_h, {5, 13, 22, 255});
            draw->outline({map_x+.5f,map_y+.5f,map_w-1.f,map_h-1.f},{96,119,132,255});
            return;
        }
        minimap_projection=make_minimap_projection(
            {map_x,map_y,map_w,map_h},explored_minimap_bounds);
        minimap_ready=true;

        const auto shade_color=[](Col color,float shade) {
            color.r=std::uint8_t(std::clamp(float(color.r)*shade,0.f,255.f));
            color.g=std::uint8_t(std::clamp(float(color.g)*shade,0.f,255.f));
            color.b=std::uint8_t(std::clamp(float(color.b)*shade,0.f,255.f));
            return color;
        };

        if (minimap_cache_logical_w != logical_w || minimap_cache_logical_h != logical_h)
            minimap_background_dirty = true;
        if (minimap_background_dirty) {
            render::Frame2D next_cache;
            FrameBuilder cached_builder(next_cache);
            cached_builder.pass(render::Pass::Ui);
            fill(&cached_builder, map_x, map_y, map_w, map_h, {5, 13, 22, 255});
            for (int py = 0; py < rows; ++py)
                for (int px = 0; px < columns; ++px) {
                    const auto world=minimap_projection.unproject(
                        map_x+(px+.5f)*pixel_w,map_y+(py+.5f)*pixel_h);
                    const int wx=int(std::floor(world.x+.5f)), wy=int(std::floor(world.y+.5f));
                    if(!inside_world(wx,wy)) continue;
                    const auto &t = w.tile(wx, wy, active_layer);
                    if(!t.explored) continue; // Never turn the unknown field into terrain evidence.
                    const bool snow=active_layer==WorldLayer::Surface&&(w.winter()||snowy(t.biome));
                    Col c;
                    if(active_layer==WorldLayer::Mine)
                        c=t.terrain==Terrain::MineOre?Col{169,132,73,255}:Col{65,70,75,255};
                    else if(t.terrain==Terrain::Water) c=snow?Col{52,100,130,255}:Col{29,103,144,255};
                    else if(snow&&t.terrain==Terrain::Forest) c={67,96,91,255};
                    else if(snow&&(t.terrain==Terrain::Rock||t.biome==Biome::Mountain||t.biome==Biome::SnowMountain)) c={142,154,159,255};
                    else if(snow) c={199,216,222,255};
                    else if(t.biome==Biome::Desert) c={178,143,86,255};
                    else if(t.terrain==Terrain::Forest) c={30,91,53,255};
                    else if(t.terrain==Terrain::Rock||t.biome==Biome::Mountain) c={101,110,105,255};
                    else if(t.amount>0) c={139,128,57,255};
                    else c={75,126,70,255};
                    if(t.terrain!=Terrain::Water) {
                        float relief=float(t.elevation)/320.f;
                        if(inside_world(wx-1,wy-1)) {
                            const auto &north=w.tile(wx-1,wy-1,active_layer);
                            if(north.explored&&north.terrain!=Terrain::Water)
                                relief+=std::clamp(float(t.elevation-north.elevation)/96.f,-.14f,.14f);
                        }
                        c=shade_color(c,std::clamp(.80f+relief,.65f,1.24f));
                        if(t.trail_wear>16) {
                            const float wear=std::clamp((float(t.trail_wear)-16.f)/180.f,0.f,.48f);
                            c.r=std::uint8_t(std::lerp(float(c.r),178.f,wear));
                            c.g=std::uint8_t(std::lerp(float(c.g),139.f,wear));
                            c.b=std::uint8_t(std::lerp(float(c.b),84.f,wear));
                        }
                    }
                    fill(&cached_builder,map_x+px*pixel_w,map_y+py*pixel_h,
                         pixel_w+.08f,pixel_h+.08f,c);
                }
            minimap_background_cache = std::move(next_cache);
            minimap_cache_logical_w = logical_w;
            minimap_cache_logical_h = logical_h;
            minimap_background_dirty = false;
        }
        draw->append_geometry(minimap_background_cache, {0,0});
        const auto map_point=[&](float x,float y){const auto p=minimap_projection.project(x,y);return SDL_FPoint{p.x,p.y};};
        for (const auto &b : w.buildings()) {
            if (b.layer != active_layer || !w.tile(b.x, b.y, b.layer).explored) continue;
            const auto p = map_point(float(b.x), float(b.y));
            if(p.x>=map_x&&p.x<=map_x+map_w&&p.y>=map_y&&p.y<=map_y+map_h) {
                fill(r,p.x-2.25f,p.y-2.25f,4.5f,4.5f,{36,27,18,255});
                fill(r,p.x-1.4f,p.y-1.4f,2.8f,2.8f,{244,202,116,255});
            }
        }
        for (const auto &u : w.residents()) {
            if (u.layer != active_layer || u.health <= 0) continue;
            const auto world = resident_position(u); const auto p = map_point(world.x, world.y);
            if(p.x>=map_x&&p.x<=map_x+map_w&&p.y>=map_y&&p.y<=map_y+map_h)
                fill(r,p.x-1.f,p.y-1.f,2.f,2.f,{231,246,238,255});
        }
        const auto flat_world = [&](float sx, float sy) {
            const auto flat = render::reconstruct_render_xy(
                {(sx - logical_w * .48f) / zoom, (sy - logical_h * .48f) / zoom}, 0.f);
            return SDL_FPoint{cam_x + flat.x, cam_y + flat.y};
        };
        const auto a=map_point(flat_world(0.f,0.f).x,flat_world(0.f,0.f).y);
        const auto b=map_point(flat_world(logical_w,0.f).x,flat_world(logical_w,0.f).y);
        const auto c=map_point(flat_world(0.f,logical_h).x,flat_world(0.f,logical_h).y);
        const auto d=map_point(flat_world(logical_w,logical_h).x,flat_world(logical_w,logical_h).y);
        const float inset=2.f;
        const float left=std::clamp(std::min({a.x,b.x,c.x,d.x}),map_x+inset,map_x+map_w-inset);
        const float right=std::clamp(std::max({a.x,b.x,c.x,d.x}),map_x+inset,map_x+map_w-inset);
        const float top=std::clamp(std::min({a.y,b.y,c.y,d.y}),map_y+inset,map_y+map_h-inset);
        const float bottom=std::clamp(std::max({a.y,b.y,c.y,d.y}),map_y+inset,map_y+map_h-inset);
        if(right-left>1.f&&bottom-top>1.f) {
            draw->outline({left-1.f,top-1.f,right-left+2.f,bottom-top+2.f},{9,20,27,245});
            draw->outline({left,top,right-left,bottom-top},{248,222,164,255});
        }
        draw->outline({map_x+.5f,map_y+.5f,map_w-1.f,map_h-1.f},{92,119,128,255});
    }

    void resource_icon(float x, float y, int type) {
        if (type == 0) {
            for (int i = 0; i < 3; ++i) {
                fill(r, x + i * 5, y + i * 2, 13, 5, {155, 91, 42, 255});
                disc(r, x + 13 + i * 5, y + 2.5f + i * 2, 3, 2.5f, {218, 143, 66, 255});
            }
        } else if (type == 1) {
            poly(r, {{x, y + 12}, {x + 5, y + 1}, {x + 16, y}, {x + 21, y + 10}, {x + 13, y + 16}},
                 {158, 166, 171, 255});
            line(r, x + 5, y + 2, x + 13, y + 12, {218, 220, 211, 255});
        } else {
            poly(r, {{x, y + 15}, {x + 4, y + 3}, {x + 12, y}, {x + 20, y + 5}, {x + 18, y + 15}},
                 {221, 157, 52, 255});
            line(r, x + 5, y + 5, x + 16, y + 5, {255, 213, 96, 255});
        }
    }
    void hud(const ClientSession &w) {
        draw->pass(render::Pass::Ui);
        panel({0, 0, logical_w, 66});
        label("RIBTERRA", 22, 12, gold, false);
        label("ПЕРШЕ ПОСЕЛЕННЯ  •  ДЕНЬ " + std::to_string(w.day()), 22, 39, {164, 180, 178, 255}, true);
        const Resources &rs = w.resources();
        std::array<std::pair<const char *, int>, 3> vals{
            {{"Дерево", rs.wood}, {"Камінь", rs.stone}, {"Їжа", rs.food}}};
        for (int i = 0; i < 3; ++i) {
            float x = 390.0f + i * 190.0f;
            resource_icon(x, 22, i);
            label(vals[i].first, x + 31, 12, {168, 180, 178, 255}, true);
            label(std::to_string(vals[i].second), x + 31, 31, cream, false);
            if (i < 2)
                line(r, x + 165, 13, x + 165, 53, {91, 82, 60, 255});
        }
        label("Населення", 980, 12, {168, 180, 178, 255}, true);
        label(std::to_string(w.residents().size()) + " / " + std::to_string(w.housing()), 980, 31, cream,
              false);
        label(w.winter() ? "ЗИМА" : "ТЕПЛИЙ СЕЗОН", 1154, 24, w.winter() ? Col{138, 205, 229, 255} : gold,
              true);

        // Controls and minimap, bottom right.
        panel({1174, 676, 246, 204});
        label("КАРТА ДОЛИНИ", 1190, 689, gold, true);
        fill(r, 1190, 716, 214, 106, {0, 0, 0, 255});
        constexpr int pixels = 100;
        for (int py = 0; py < pixels; ++py)
            for (int px = 0; px < pixels; ++px) {
                const auto &t = w.tile(world_min + px * world_size / pixels,
                                       world_min + py * world_size / pixels, active_layer);
                const Col c = !t.explored                    ? Col{112, 135, 153, 255}
                              : snowy(t.biome)               ? Col{202, 222, 234, 255}
                              : t.terrain == Terrain::Water  ? Col{47, 116, 147, 255}
                              : t.biome == Biome::Desert     ? Col{191, 159, 104, 255}
                              : t.terrain == Terrain::Forest ? Col{38, 82, 57, 255}
                              : t.terrain == Terrain::Rock   ? Col{112, 119, 112, 255}
                                                             : Col{90, 124, 70, 255};
                fill(r, 1193 + px * 2.08f, 719.f + py, 2.1f, 1.1f, c);
            }
        SDL_FRect vp{1232, 746, 92, 53};
        draw->outline(vp, gold);
        std::array<const char *, 4> sp{{"ПАУЗА", "1x", "2x", "4x"}};
        for (int i = 0; i < 4; ++i)
            button({1190.0f + i * 53.0f, 832, 48, 34}, sp[i], i == 0 ? pause : sim_speed == (1 << (i - 1)));

        if (tutorial) {
            panel({1100, 86, 320, 300});
            label("ПЕРШІ КРОКИ", 1120, 102, gold, false);
            label("Розбудіть долину", 1120, 130, cream, true);
            std::array<std::string, 3> goals{
                {"1. Накажіть мешканцю йти", "2. Отримайте перший ресурс", "3. Завершіть будівництво"}};
            int step = w.tutorial_step();
            for (int i = 0; i < 3; ++i) {
                Col cc = i < step ? Col{113, 191, 131, 255} : (i == step ? cream : Col{117, 131, 137, 255});
                label((i < step ? "OK  " : ">   ") + goals[i], 1120, 165.0f + i * 35.0f, cc, true);
            }
            line(r, 1120, 275, 1400, 275, {78, 83, 76, 255});
            label("КЕРУВАННЯ", 1120, 292, gold, true);
            label("Стрілки — камера   Колесо — масштаб", 1120, 316, {180, 190, 185, 255}, true);
            label("ПКМ — дія   H/F/B — будівництво", 1120, 340, {180, 190, 185, 255}, true);
            label("F1 — сховати HUD", 1120, 364, {125, 142, 145, 255}, true);
        } else if (!selected.empty()) {
            panel({1120, 86, 300, 220});
            auto it = std::find_if(w.residents().begin(), w.residents().end(),
                                   [&](auto &u) { return u.id == selected.front(); });
            if (it != w.residents().end()) {
                label(it->name, 1140, 104, gold, false);
                label(job_ua(it->job), 1140, 137, cream, true);
                label("Здоров’я", 1140, 173, {169, 181, 176, 255}, true);
                fill(r, 1225, 177, 160, 7, {43, 57, 60, 255});
                fill(r, 1225, 177, 160 * it->health / 100.f, 7, {93, 174, 116, 255});
                label("Ситість  " + std::to_string(int(std::clamp(10.f-it->hunger,0.f,10.f))) + "/10", 1140, 199, {169, 181, 176, 255},
                      true);
                label("Досвід: дерево " + std::to_string(it->wood_xp) + "  камінь " +
                          std::to_string(it->mining_xp),
                      1140, 232, {145, 159, 158, 255}, true);
                label("Обрано: " + std::to_string(selected.size()), 1140, 269, gold, true);
            }
        }

        // Build palette.
        panel({380, 790, 680, 90});
        label(build_mode ? "РОЗМІСТІТЬ КРЕСЛЕННЯ" : "БУДІВНИЦТВО", 400, 804, gold, true);
        std::array<std::pair<BuildingKind, const char *>, 3> builds{{{BuildingKind::House, "H  Будинок"},
                                                                     {BuildingKind::Farm, "F  Ферма"},
                                                                     {BuildingKind::Stockpile, "B  Склад"}}};
        for (int i = 0; i < 3; ++i) {
            SDL_FRect q{400 + i * 214.f, 828, 198, 38};
            auto [k, n] = builds[i];
            fill(r, q.x, q.y, q.w, q.h,
                 build_mode && armed == k ? Col{40, 91, 132, 255} : Col{25, 45, 62, 250});
            draw->outline(q, build_mode && armed == k ? gold : Col{100, 115, 124, 255});
            label(n, q.x + 10, q.y + 2, cream, true);
            label(cost_ua(k), q.x + 10, q.y + 20, {139, 151, 149, 255}, true, 176);
        }
        if (build_mode) {
            auto [wx, wy] = world_at(last_mouse_x, last_mouse_y);
            if (inside_world(wx, wy)) {
                const auto points = surface_points(wx, wy);
                poly(r, {points[0], points[1], points[2], points[3]},
                     w.can_build(armed, wx, wy, active_layer) ? Col{67, 205, 155, 120}
                                                            : Col{223, 80, 73, 130});
            }
        }

        if (now - status_at < 4.5 || status_at == 0) {
            auto t = text(status, cream, true);
            panel({20, 842, std::min(330.f, t.x + 30), 38}, {16, 30, 42, 230});
            label(status, 35, 852, cream, true, 300);
        }
        label("РАННІЙ ІГРОВИЙ ПРОТОТИП • НЕ ФІНАЛЬНИЙ ІНТЕРФЕЙС", 20, 810, {215, 166, 76, 175}, true, 330);
        if (pause) {
            fill(r, 0, 66, logical_w, 724, {5, 12, 20, 125});
            panel({545, 330, 350, 160});
            label("ПАУЗА", 676, 352, gold, false);
            label("Долина завмерла", 646, 388, cream, true);
            button({625, 430, 190, 38}, "ПРОДОВЖИТИ  [SPACE]", true);
        }
    }

    void set_status(std::string s) {
        status = std::move(s);
        pending_status = status;
        status_at = now;
    }
    void apply_resident_selection(std::optional<ResidentHit> hit, bool additive) {
        if (hit) {
            if (!additive)
                selected.clear();
            auto it = std::find(selected.begin(), selected.end(), hit->id);
            if (it == selected.end())
                selected.push_back(hit->id);

            set_status("Мешканця обрано");
        } else if (!additive)
            selected.clear();
    }
    void command(ClientSession &w, float sx, float sy) {
        if (selected.empty()) {
            set_status("Спершу оберіть мешканця");
            return;
        }
        auto [x, y] = world_at(sx, sy);
        if (!inside_world(x, y))
            return;
        const auto farm = std::find_if(w.buildings().begin(), w.buildings().end(), [&](const auto &b) {
            if (b.kind != BuildingKind::Farm || b.progress != 1 || b.layer != active_layer) return false;
            const auto origin = iso(b.x, b.y);
            const float width = resolve_building_visual(b.kind, 1, b.progress, winter).width * zoom;
            return (b.x == x && b.y == y) ||
                   (sx >= origin.x-width*.45f && sx <= origin.x+width*.45f &&
                    sy >= origin.y-width*.95f && sy <= origin.y+12*zoom);
        });
        if (farm != w.buildings().end()) {
            ProfessionAssignment assignment;
            assignment.profession = Profession::Farmer;
            assignment.worksite = CellCoord{farm->x,farm->y,active_layer};
            const auto result = w.assign_profession(selected, assignment);
            set_status(result.accepted ? "Фермерів призначено. Вони працюватимуть і носитимуть урожай на склад."
                                       : "Призначити фермерів не вдалося: " + result.message);
            return;
        }
        const auto &t = w.tile(x, y, active_layer);
        const auto modifiers = modifiers_observed ? observed_modifiers : SDL_GetModState();
        const auto mode =
            (modifiers & SDL_KMOD_SHIFT) != 0 ? ManualQueueMode::Append : ManualQueueMode::Replace;
        CommandResult out = t.amount > 0 ? w.gather(selected, x, y, active_layer, mode, gather_mode)
                                         : w.move(selected, x, y, active_layer, mode);
        set_status(out.accepted ? (out.pending ? "Наказ у черзі. Планується маршрут."
                                               : (t.amount > 0 ? "Мешканці вирушили по ресурс"
                                                               : "Мешканці вирушили до цілі"))
                                : "Дія недоступна: " + out.message);
    }
    bool visible_animal(const ClientSession&w,const Animal&a)const{
        if(active_layer!=WorldLayer::Surface)return false;
        for(const auto&resident:w.residents())if(resident.health>0&&resident.layer==WorldLayer::Surface&&std::hypot(resident.x-a.x,resident.y-a.y)<=resident_vision_radius)return true;
        for(const auto&b:w.buildings())if(b.kind==BuildingKind::Camp&&std::hypot(float(b.x)-a.x,float(b.y)-a.y)<=starting_vision_radius)return true;
        return false;
    }
    void arm(BuildingKind k) {
        selecting=false;
        worksite_assignment.reset();
        worksite_residents.clear();
        worksite_error.reset();
        armed = k;
        build_mode = true;
        pause = false;
        set_status(k == BuildingKind::Bridge ? "Міст: почніть від берега, додавайте секції після завершення попередніх. ПКМ — вийти." : "Оберіть вільну ділянку");
    }

    void event(const SDL_Event &e, ClientSession &w) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            auto p = pointer(e.motion.x, e.motion.y);
            if(selecting && std::hypot(p.first-selection_x,p.second-selection_y)>5)
                select_box(w,p.first,p.second);
            if (dragging) {
                float dx = p.first - last_mouse_x, dy = p.second - last_mouse_y;
                const auto world_delta = render::reconstruct_render_xy({dx / zoom, dy / zoom}, 0.f);
                cam_x -= world_delta.x;
                cam_y -= world_delta.y;
            }
            last_mouse_x = p.first;
            last_mouse_y = p.second;
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            zoom = std::clamp(zoom + e.wheel.y * .09f, .62f, 3.f);
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            auto p = pointer(e.button.x, e.button.y);
            last_mouse_x = p.first;
            last_mouse_y = p.second;
            if (e.button.button == SDL_BUTTON_MIDDLE) {
                selecting=false;
                dragging = true;
                return;
            }
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (worksite_assignment) {
                    const auto [x, y] = world_at(p.first, p.second);
                    if (inside_world(x, y)) {
                        worksite_assignment->worksite = CellCoord{x, y, active_layer};
                        const auto result = w.assign_profession(worksite_residents, *worksite_assignment);
                        if (result.accepted) {
                            worksite_assignment.reset();
                            worksite_residents.clear();
                            worksite_error.reset();
                        } else
                            worksite_error = result.message;
                    }
                    return;
                }
                if (legacy_hud_active && inside(p.first, p.second, {1190, 832, 48, 34})) {
                    if (w.remote())
                        {auto result=w.lobby_action(LobbyAction::Pause,w.lobby().paused?0:1);if(!result.accepted)set_status(result.message);}
                    else
                        pause = !pause;
                    return;
                }
                for (int i = 1; legacy_hud_active && i < 4; ++i)
                    if (inside(p.first, p.second, {1190 + i * 53.f, 832, 48, 34})) {
                        if (w.remote())
                            {auto result=w.lobby_action(LobbyAction::Speed,1<<(i-1));if(!result.accepted)set_status(result.message);}
                        else {
                            sim_speed = 1 << (i - 1);
                            pause = false;
                        }
                        return;
                    }
                for (int i = 0; legacy_hud_active && i < 3; ++i)
                    if (inside(p.first, p.second, {400 + i * 214.f, 828, 198, 38})) {
                        arm(i == 0   ? BuildingKind::House
                            : i == 1 ? BuildingKind::Farm
                                     : BuildingKind::Stockpile);
                        return;
                    }
                if (legacy_hud_active && pause && inside(p.first, p.second, {625, 430, 190, 38})) {
                    pause = false;
                    return;
                }
                if (legacy_hud_active && (p.second <= 66 || p.second >= 790 || p.first >= 1090))
                    return;
                if (build_mode) {
                    auto [x, y] = world_at(p.first, p.second);
                    if (inside_world(x, y)) {
                        auto out = w.build(armed, x, y, active_layer);
                        set_status(out.message.empty()
                                       ? (out.accepted ? "Будівництво розпочато" : "Тут будувати не можна")
                                       : out.message);
                        if (out.accepted && armed != BuildingKind::Bridge)
                            build_mode = false;
                    }
                    return;
                }
                const bool add =
                    ((modifiers_observed ? observed_modifiers : SDL_GetModState()) & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                const Building *hit=nullptr;
                const auto person = resident_hit(w, p.first, p.second);
                if(art.ready()) for(const auto &b:w.buildings()) {
                    if(b.layer!=active_layer || !w.tile(b.x,b.y,b.layer).explored) continue;
                    auto q=iso(b.x,b.y);
                    const auto building_heights=terrain_corners(b.x,b.y,[&](int x,int y){return height_sample(x,y);});
                    q.y-=(*std::max_element(building_heights.begin(),building_heights.end())-ground_height(float(b.x),float(b.y)))*render::legacy_elevation_screen_scale*zoom;
                    int level = 1;
                    for (const auto &upgrade : w.economy().upgrades)
                        if (upgrade.building_id == b.id) {
                            level = upgrade.level;
                            break;
                        }
                    const auto visual = resolve_building_visual(
                        b.kind, level, b.progress, winter || snowy(w.tile(b.x,b.y,b.layer).biome));
                    bool inside_sprite=hit_sprite(visual.asset_id,
                        {q.x,q.y+(b.kind==BuildingKind::Bridge?0:15)*zoom},visual.width*zoom,
                        {p.first,p.second},false,!visual.winter_tint);
                    if(b.kind==BuildingKind::Road) {const auto cell=world_at(p.first,p.second);inside_sprite=cell.first==b.x&&cell.second==b.y;}
                    if(inside_sprite&&(!hit||b.x+b.y>=hit->x+hit->y)) hit=&b;
                }
                // Match the painter order: a hidden resident cannot intercept
                // the visible building in front, while a foreground or equal-depth
                // resident wins because residents are appended later before stable_sort.
                if(hit && (!person || float(hit->x+hit->y)+.5f > person->depth)) {
                    if(!add) selected_buildings.clear();
                    auto it=std::find(selected_buildings.begin(),selected_buildings.end(),hit->id);
                    if(it==selected_buildings.end())selected_buildings.push_back(hit->id);
                    else if(add)selected_buildings.erase(it);
                    selected.clear();selecting=false;return;
                }
                selected_buildings.clear();
                selecting=true; selection_additive=add;
                selection_x=p.first;selection_y=p.second;selection_before_drag=selected;
                apply_resident_selection(person, add);
                return;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                selecting=false;
                if (worksite_assignment) {
                    worksite_assignment.reset();
                    worksite_residents.clear();
                    worksite_error.reset();
                    return;
                }
                if (build_mode) {
                    build_mode = false;
                    set_status("Будівництво скасовано");
                } else if (!legacy_hud_active || (p.second > 66 && p.second < 790 && p.first < 1090))
                    command(w, p.first, p.second);
                return;
            }
        }
        if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT && selecting) {
            const auto point=pointer(e.button.x,e.button.y);
            if(std::hypot(point.first-selection_x,point.second-selection_y)>5) select_box(w,point.first,point.second);
            selecting=false;return;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_MIDDLE) {
            dragging = false;
            return;
        }
        if (e.type != SDL_EVENT_KEY_DOWN)
            return;
        SDL_Keycode k = e.key.key;
        if (e.key.repeat && k != SDLK_LEFT && k != SDLK_RIGHT && k != SDLK_UP && k != SDLK_DOWN)
            return;
        if (k == SDLK_ESCAPE) {
            selected_buildings.clear();
            if(selecting){selecting=false;selected=selection_before_drag;return;}
            if (worksite_assignment) {
                worksite_assignment.reset();
                worksite_residents.clear();
                worksite_error.reset();
                return;
            }
            if (build_mode) {
                build_mode = false;
                set_status("Будівництво скасовано");
            } else if (w.remote())
                {auto result=w.lobby_action(LobbyAction::Pause,w.lobby().paused?0:1);if(!result.accepted)set_status(result.message);}
            else
                pause = !pause;
        } else if (k == SDLK_SPACE) {
            if (w.remote())
                {auto result=w.lobby_action(LobbyAction::Pause,w.lobby().paused?0:1);if(!result.accepted)set_status(result.message);}
            else
                pause = !pause;
        } else if (k == SDLK_1 || k == SDLK_2 || k == SDLK_3) {
            if (w.remote())
                {auto result=w.lobby_action(LobbyAction::Speed,k==SDLK_1?1:k==SDLK_2?2:4);if(!result.accepted)set_status(result.message);}
            else {
                sim_speed = k == SDLK_1 ? 1 : k == SDLK_2 ? 2 : 4;
                pause = false;
            }
        } else if (k == SDLK_F1)
            tutorial = !tutorial;
        else if (k == SDLK_C) {
            cam_x = 24;
            cam_y = 24;
            for(const auto&b:w.buildings())if(b.kind==BuildingKind::Camp){cam_x=float(b.x);cam_y=float(b.y);break;}
            zoom = 1.30f;
            set_status("Камеру повернуто");
        } else if (k == SDLK_H)
            arm(BuildingKind::House);
        else if (k == SDLK_F)
            arm(BuildingKind::Farm);
        else if (k == SDLK_B)
            arm(BuildingKind::Stockpile);
        else if (k == SDLK_A) {
            selected.clear();
            for (auto &u : w.residents())
                if (u.layer == active_layer)
                    selected.push_back(u.id);
            set_status("Обрано всіх мешканців");
        } else if (k == SDLK_LEFT)
            cam_x -= 1.4f, cam_y += 1.4f;
        else if (k == SDLK_RIGHT)
            cam_x += 1.4f, cam_y -= 1.4f;
        else if (k == SDLK_UP)
            cam_x -= 1.2f, cam_y -= 1.2f;
        else if (k == SDLK_DOWN)
            cam_x += 1.2f, cam_y += 1.2f;
    }
};

GameView::GameView(render::Renderer2D &renderer) : impl_(std::make_unique<Impl>(renderer)) {}
GameView::~GameView() = default;
void GameView::load_art(const AssetCatalog &assets) {
    if (impl_->material_art)
        throw std::logic_error("Original art cannot be replaced while a registered collection is installed");
    auto original_assets = std::make_unique<AssetCatalog>(assets);
    auto original_art = Json::parse(assets.read_path("art/world.json"));
    impl_->art.load(assets);
    impl_->original_assets.swap(original_assets);
    impl_->original_art.swap(original_art);
    impl_->art.use_native_cursor("icons-cursors.24");
}
void GameView::load_material_collection(const AssetCatalog& assets) {
    if (impl_->renderer.has_submitted_frames())
        throw std::logic_error("Normal material collection installation is startup-only before any submission");
    if (!impl_->original_assets || impl_->original_art.at("schema") != 1 || !impl_->art.ready())
        throw std::logic_error("Normal materials require an already-loaded original schema-1 catalog");
    if (impl_->renderer.ground_forward_status().available || impl_->renderer.sky_shadow_status().available)
        throw std::invalid_argument("Normal mixed material composition requires LegacyCombined");
    if (!impl_->renderer.world_hdr_status().available)
        throw std::invalid_argument("Normal mixed materials require the hardware linear HDR path");
    const auto& metadata_record = assets.find("material.collection.metadata");
    if (metadata_record.path != "art/material-collection.json" || metadata_record.kind != "data" ||
        assets.find("art.world").path != "art/world.json")
        throw std::invalid_argument("Normal material collection metadata/world identity is invalid");
    const auto metadata = strict_material_json(assets.read("material.collection.metadata"));
    const auto world = Json::parse(assets.read_path("art/world.json"));
    const bool textured_profile = assets.read_profile() == AssetCatalogReadProfile::MaterialCollectionTextured;
    if (!metadata.at("schema").is_number_integer() || metadata.at("schema") != 1 ||
        metadata.at("kind") != "ribterra-material-collection-v1" ||
        !metadata.at("items").is_array() || metadata.at("items").empty() || metadata.at("items").size() > 32u ||
        !metadata.at("item_count").is_number_integer() || metadata.at("item_count") != metadata.at("items").size() ||
        !world.at("schema").is_number_integer() || world.at("schema") != 2 ||
        !world.at("pages").is_array() || !world.at("pages").empty() ||
        !world.at("lit_sprites").is_array() || world.at("lit_sprites").size() != metadata.at("items").size() ||
        !world.at("materials").is_array() || world.at("materials").size() != metadata.at("items").size() ||
        !world.at("textures").is_array() || world.at("textures").size() != metadata.at("items").size()*6u)
        throw std::invalid_argument("Normal material collection schema/counts are inconsistent");
    if (textured_profile) preflight_textured_channels(metadata,world);
    TexturedSourceCache source_cache;
    std::unordered_map<std::string,Impl::RegisteredMaterial> registrations;
    registrations.reserve(metadata.at("items").size());
    std::set<std::string> dynamic_ids,material_ids;
    const bool has_neutral = std::any_of(metadata.at("items").begin(),metadata.at("items").end(),
        [](const Json& item) { return item.contains("albedo_source"); });
    const bool has_textured = std::any_of(metadata.at("items").begin(),metadata.at("items").end(),[](const Json& item) {
        return item.contains("albedo_source") && item.at("albedo_source").is_object() &&
            item.at("albedo_source").contains("kind") && item.at("albedo_source").at("kind") == "authored-xyz-triangle-textures-v1";
    });
    if (has_textured && !textured_profile)
        throw std::invalid_argument("Textured material collection requires the explicit MaterialCollectionTextured read profile");
    const std::string expected_colour = textured_profile ? "authored-XYZ-textured-palette-draft-and-legacy-painted-neutral-acceptance-pending"
        : has_neutral ? "authored-XYZ-palette-draft-and-legacy-painted-neutral-acceptance-pending"
        : "legacy-painted-neutral-albedo-acceptance-pending";
    if (metadata.at("paint_colour").get<std::string>() != expected_colour)
        throw std::invalid_argument("Normal material collection colour-source disclosure differs from its active modes");
    for (const auto& item : metadata.at("items")) {
        const auto key = item.at("legacy_key").get<std::string>();
        const auto id = item.at("sprite_id").get<std::string>();
        const auto material_id = item.at("material_id").get<std::string>();
        const auto source_hash = item.at("source_hash").get<std::string>();
        const auto master_hash = item.at("master_sha256").get<std::string>();
        const bool neutral = item.contains("albedo_source");
        std::vector<NeutralPaletteMaterial> palette;
        Json source_record{{"master_sha256",master_hash},{"registration",item.at("registration")},
                           {"geometry_provenance",item.at("geometry_provenance")}};
        if (neutral) {
            const bool textured_item = item.at("albedo_source").at("kind") == "authored-xyz-triangle-textures-v1";
            if (textured_item && !textured_profile)
                throw std::invalid_argument("Textured material collection requires the explicit MaterialCollectionTextured read profile");
            if (!textured_item && assets.read_profile() != AssetCatalogReadProfile::MaterialCollection && !textured_profile)
                throw std::invalid_argument("Authored material collection requires the bounded MaterialCollection read profile");
            (void)canonical_material_id(item.at("namespace"));
            (void)bounded_text(item.at("legacy_key"),256u);
            (void)bounded_text(item.at("sprite_id"),256u);
            (void)bounded_text(item.at("material_id"),256u);
            palette = validate_neutral_palette(assets,item,source_cache);
            source_record["albedo_source"] = item.at("albedo_source");
            source_record["generated_albedo_sha256"] = item.at("generated_albedo_sha256");
        } else if (item.contains("generated_albedo_sha256") || item.contains("coverage"))
            throw std::invalid_argument("Generated albedo identity/coverage requires an explicit neutral source mode");
        if (!supported_registered_key(key) || !impl_->art.contains(key) || id != "dynamic."+key ||
            id.size() > 256u || material_id != item.at("namespace").get<std::string>()+".material" ||
            !hash_string(source_hash) || !hash_string(master_hash) ||
            source_hash != json_hash(source_record) ||
            !dynamic_ids.emplace(id).second || !material_ids.emplace(material_id).second ||
            registrations.contains(key))
            throw std::invalid_argument("Normal material mapping requires a unique exact supported original legacy key");
        const Json* original_page{}; const Json* original_sprite{};
        for (const auto& page : impl_->original_art.at("pages")) for (const auto& sprite : page.at("sprites"))
            if (sprite.at("id").get<std::string>() == key) {
                if (original_sprite) throw std::invalid_argument("Original registered key is ambiguous");
                original_page = &page; original_sprite = &sprite;
            }
        if (!original_sprite || original_page->size() != 2u)
            throw std::invalid_argument("Registered original paint must have an untransformed page");
        const auto crop = registered_rectangle(original_sprite->at("rect"));
        const auto pivot = original_sprite->at("pivot").get<std::array<float,2>>();
        const std::array<float,2> pivot_px{pivot[0]*float(crop[2]),pivot[1]*float(crop[3])};
        const float reference_width = original_sprite->value("reference_width",float(crop[2]));
        const auto& registration = item.at("registration");
        const auto page_path = original_page->at("path").get<std::string>();
        if (neutral) {
            const auto& original_source = registration.at("asset_source");
            const auto& original_record = impl_->original_assets->find(original_source.at("id").get<std::string>());
            const auto& original_provenance = registration.at("provenance");
            if (original_record.path != page_path || original_record.license != original_provenance.at("license").get<std::string>() ||
                original_record.author != original_provenance.at("author").get<std::string>() ||
                original_record.source != original_provenance.at("source").get<std::string>() ||
                original_record.notice != original_provenance.at("notice_path").get<std::string>())
                throw std::invalid_argument("Neutral original provenance differs from the loaded original asset registry");
            for (const auto field : {"license","author","source"})
                if (original_source.at(field) != original_provenance.at(field))
                    throw std::invalid_argument("Neutral original asset metadata/provenance fields are inconsistent");
            if (portable_material_path(original_source.at("notice")) != original_record.notice)
                throw std::invalid_argument("Neutral original asset notice identity is inconsistent");
            // The original registry notice is project-relative; check its
            // bounded packaged copy after the exact provenance identity above.
            validate_notice(assets,original_provenance);
        }
        const auto declared_pivot = registration.at("pivot_px").get<std::array<double,2>>();
        const auto declared_size = registration.at("source_size");
        const double declared_reference = registration.at("reference_width").get<double>();
        if (!std::isfinite(reference_width) || reference_width <= 0.f || reference_width > 4096.f ||
            registration.at("page_path").get<std::string>() != page_path ||
            registered_rectangle(registration.at("rect")) != crop || !declared_size.is_array() || declared_size.size() != 2u ||
            !declared_size[0].is_number_integer() || !declared_size[1].is_number_integer() ||
            declared_size[0].get<double>() != crop[2] || declared_size[1].get<double>() != crop[3] ||
            declared_pivot[0] != static_cast<double>(pivot_px[0]) || declared_pivot[1] != static_cast<double>(pivot_px[1]) ||
            declared_reference != static_cast<double>(reference_width) ||
            registration.at("catalog_entry_sha256").get<std::string>() != json_hash(
                Json{{"page",{{"path",page_path}}},{"sprite",*original_sprite}}))
            throw std::invalid_argument("Registered source crop/pivot/reference width/catalog hash differs from original");
        const auto encoded = impl_->original_assets->read_path(page_path);
        if (registration.at("png_sha256").get<std::string>() != sha256(encoded))
            throw std::invalid_argument("Registered original PNG hash differs from loaded art");
        const auto paint = registered_paint(encoded,crop);
        const auto paint_hash = sha256(paint);
        if (registration.at("decoded_crop_sha256").get<std::string>() != paint_hash)
            throw std::invalid_argument("Registered decoded paint hash differs from original crop");
        const Json* sprite{}; const Json* material{};
        for (const auto& row : world.at("lit_sprites")) if (row.at("id").get<std::string>() == id) {
            if (sprite) throw std::invalid_argument("Registered dynamic sprite ID is ambiguous");
            sprite = &row;
        }
        for (const auto& row : world.at("materials")) if (row.at("id").get<std::string>() == material_id) {
            if (material) throw std::invalid_argument("Registered material ID is ambiguous");
            material = &row;
        }
        if (!sprite || !material || sprite->at("material").get<std::string>() != material_id ||
            material->at("source_hash").get<std::string>() != source_hash ||
            sprite->at("source_size").get<std::array<float,2>>() != std::array<float,2>{float(crop[2]),float(crop[3])} ||
            sprite->at("pivot_px").get<std::array<float,2>>() != pivot_px)
            throw std::invalid_argument("Registered material/sprite source identity or geometry is inconsistent");
        if (neutral && item.at("depth_range") != sprite->at("depth_range"))
            throw std::invalid_argument("Neutral metadata depth range differs from its active sprite");
        const float export_scale = sprite->at("export_pixels_per_reference_pixel").get<float>();
        if (!std::isfinite(export_scale) || export_scale < .001f || export_scale > 64.f ||
            item.at("export_pixels_per_reference_pixel").get<double>() != static_cast<double>(export_scale))
            throw std::invalid_argument("Registered export scale is inconsistent");
        const auto& albedo = material->at("channels").at("albedo");
        const auto albedo_id = albedo.at("texture").get<std::string>();
        const Json* texture{};
        for (const auto& row : world.at("textures")) if (row.at("id").get<std::string>() == albedo_id) {
            if (texture) throw std::invalid_argument("Registered albedo ID is ambiguous");
            texture = &row;
        }
        if (!texture || texture->at("format") != "rgba8_unorm_srgb" ||
            texture->at("width").get<double>() != crop[2] || texture->at("height").get<double>() != crop[3] ||
            registered_rectangle(albedo.at("rect")) != std::array<int,4>{0,0,crop[2],crop[3]})
            throw std::invalid_argument(neutral ? "Registered albedo format/extent must preserve the original crop coordinates"
                : "Registered albedo must preserve the exact original cropped RGBA");
        if (neutral) validate_neutral_pixels(assets,item,*material,world,crop,paint,palette);
        else if (texture->at("sha256").get<std::string>() != paint_hash ||
                 sha256(assets.read_path(texture->at("path").get<std::string>())) != paint_hash)
            throw std::invalid_argument("Registered albedo must preserve the exact original cropped RGBA");
        registrations.emplace(key,Impl::RegisteredMaterial{id,reference_width,export_scale});
    }
    // Complete compatibility validation precedes GPU installation. Candidate
    // failure retains both accepted catalogs and the original cursor registry.
    source_cache.sources.clear(); // Sequential CPU RGB/index validation is complete.
    source_cache.bytes = 0u;
    const GameArtCacheLimits maximum;
    if (impl_->art.hit_mask_bytes() > maximum.hit_mask_bytes || impl_->art.outline_texture_bytes() > maximum.outline_texture_bytes)
        throw std::logic_error("Original retained art cache exceeds its published budget");
    const GameArtCacheLimits remaining{maximum.hit_mask_bytes-impl_->art.hit_mask_bytes(),
        maximum.outline_texture_bytes-impl_->art.outline_texture_bytes()};
    if (textured_profile) assets.verify();
    auto candidate = std::make_unique<GameArt>(impl_->renderer);
    // During replacement the old optional catalog and this bounded candidate
    // coexist temporarily. The published legacy+new catalog fits these caps.
    candidate->load(assets,remaining);
    for (const auto& [key,registration] : registrations) {
        (void)key;
        if (!candidate->contains_lit(registration.sprite_id))
            throw std::logic_error("Validated registered sprite was not installed");
    }
    if (textured_profile) assets.verify();
    impl_->material_art.swap(candidate);
    impl_->registered_materials.swap(registrations);
    impl_->material_active = false;
    impl_->material_wetness = 0.f;
    impl_->material_vertex_ranges.clear();
}
std::size_t GameView::registered_material_count() const noexcept { return impl_->registered_materials.size(); }
bool GameView::has_registered_material(std::string_view key) const {
    return impl_->registered_materials.contains(std::string(key));
}
void GameView::load_engine_control_art(const AssetCatalog &assets) {
    if (impl_->engine_control_art)
        throw std::logic_error("Engine control art is startup-only and is already loaded");
    auto candidate = std::make_unique<GameArt>(impl_->renderer);
    candidate->load(assets);
    for (const auto id : engine_control_sprite_ids)
        if (!candidate->contains_lit(id))
            throw std::invalid_argument("Engine control catalog is missing required sprite: " +
                                        std::string(id));
    constexpr std::array<std::uint8_t, 4> neutral_ground{148, 148, 148, 255};
    const auto ground = impl_->renderer.create_sampled_texture(
        {{1, 1}, render::SampledTextureFormat::Rgba8UnormSrgb}, neutral_ground);
    impl_->engine_control_ground = ground;
    impl_->engine_control_art = std::move(candidate);
}
void GameView::load_engine_water_art() {
    if (!impl_->engine_control_art || impl_->engine_water_normal != render::TextureHandle{})
        throw std::logic_error("Engine water art requires loaded controls and is startup-only");
    // A periodic technical normal field, not accepted final water albedo/art.
    // Integer frequencies make repeat filtering seamless in both axes.
    constexpr std::uint32_t dimension = 64;
    constexpr float tau = 6.28318530718f;
    std::array<std::uint8_t, dimension * dimension * 4u> bytes{};
    for (std::uint32_t y = 0; y < dimension; ++y)
        for (std::uint32_t x = 0; x < dimension; ++x) {
            const float u = (static_cast<float>(x) + .5f) / dimension;
            const float v = (static_cast<float>(y) + .5f) / dimension;
            const float nx = .42f * std::cos(tau * (3.f * u + v)) +
                             .18f * std::cos(tau * (u - 4.f * v));
            const float ny = .31f * std::sin(tau * (u + 2.f * v)) +
                             .12f * std::cos(tau * (4.f * u - v));
            const float inverse = 1.f / std::sqrt(nx * nx + ny * ny + 1.f);
            const auto offset = (static_cast<std::size_t>(y) * dimension + x) * 4u;
            for (std::size_t channel = 0; channel < 3u; ++channel) {
                const float component = (channel == 0u ? nx : channel == 1u ? ny : 1.f) * inverse;
                bytes[offset + channel] = static_cast<std::uint8_t>(std::lround(
                    std::clamp(component * .5f + .5f, 0.f, 1.f) * 255.f));
            }
            bytes[offset + 3u] = 255u;
        }
    impl_->engine_water_normal = impl_->renderer.create_sampled_texture(
        {{dimension, dimension}, render::SampledTextureFormat::Rgba8Unorm}, bytes);
}
void GameView::set_logical_size(render::Extent size) {
    if (!size.width || !size.height)
        throw std::invalid_argument("Game view logical size must be non-zero");
    if (impl_->logical_w != static_cast<float>(size.width) ||
        impl_->logical_h != static_cast<float>(size.height))
        impl_->minimap_background_dirty = true;
    impl_->logical_w = static_cast<float>(size.width);
    impl_->logical_h = static_cast<float>(size.height);
}
void GameView::compose_cursor(render::Frame2D &frame) {
    // The OS moves this cursor independently of simulation, save/load and rendering.
    (void)frame;
    const int index = impl_->build_mode ? 26 : impl_->worksite_assignment ? 27 : 24;
    impl_->art.use_native_cursor("icons-cursors." + std::to_string(index));
}
void GameView::handle_event(const SDL_Event &e, ClientSession &w) {
    observe_input(e);
    // Motion events only update pointer/camera state. Rebuilding the whole terrain
    // snapshot for every queued mouse event could starve the frame loop.
    if(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) impl_->sync_terrain(w);
    impl_->event(e, w);
}
void GameView::observe_input(const SDL_Event &event) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_MIDDLE)
        impl_->dragging = false;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        impl_->observed_modifiers = SDL_KMOD_NONE;
        impl_->modifiers_observed = true;
        impl_->dragging = false;
        impl_->selecting = false;
    } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        impl_->observed_modifiers = event.key.mod;
        const auto shift = event.key.key == SDLK_LSHIFT   ? SDL_KMOD_LSHIFT
                           : event.key.key == SDLK_RSHIFT ? SDL_KMOD_RSHIFT
                                                          : event.key.key == SDLK_LALT ? SDL_KMOD_LALT
                           : event.key.key == SDLK_RALT ? SDL_KMOD_RALT : SDL_KMOD_NONE;
        if (event.type == SDL_EVENT_KEY_DOWN)
            impl_->observed_modifiers |= shift;
        else
            impl_->observed_modifiers &= ~shift;
        impl_->modifiers_observed = true;
    }
}
void GameView::set_gather_completion(GatherCompletion completion) {
    if (completion == GatherCompletion::UntilExhausted || completion == GatherCompletion::OneDelivery)
        impl_->gather_mode = completion;
}
GatherCompletion GameView::gather_completion() const {
    return impl_->gather_mode;
}
void GameView::compose(const ClientSession &w, double seconds, render::Frame2D &frame, bool legacy_hud) {
    // Resolve authority before altering the outgoing frame or presentation clock.
    // Unknown/reconnecting sessions and Mine keep the ordinary legacy lighting.
    std::optional<EngineWeatherProjectionV1> weather;
    std::optional<EngineEnvironmentLightingV1> environment;
    if (impl_->material_art && impl_->active_layer == WorldLayer::Surface &&
        w.ready() && w.world_ready() && !w.reconnecting() && w.weather().known) {
        const auto motion = impl_->prepare_motion(w,seconds);
        weather = project_engine_weather(w.weather(),w.ticks(),motion.clock.tick(),false);
        environment = project_engine_environment(weather->environment,{});
    }
    impl_->contact_decks.clear();
    impl_->contact_surface_queries = false;
    frame.logical_size = {static_cast<std::uint32_t>(impl_->logical_w),
                          static_cast<std::uint32_t>(impl_->logical_h)};
    frame.clear = ground_mist;
    frame.clear_commands();
    impl_->material_active = environment.has_value();
    impl_->material_wetness = weather ? weather->wetness01 : 0.f;
    impl_->material_vertex_ranges.clear();
    if (environment) {
        frame.linear_hdr_world = true;
        frame.surface_lighting = environment->lighting;
        // Independent moon/shadow/water/fog payloads require the later graph.
    }
    FrameBuilder builder(frame);
    impl_->draw = &builder;
    impl_->r = &builder;
    impl_->draw_frame = &frame;
    impl_->now = seconds;
    impl_->sync_motion(w, seconds);
    impl_->young_residents.clear();
    for(const auto &y:w.settlement().young) impl_->young_residents.insert(y.resident_id);
    impl_->legacy_hud_active = legacy_hud;
    impl_->sync_terrain(w);
    impl_->draw_world(w);
    if(impl_->active_layer==WorldLayer::Surface) {
        const auto calendar = w.calendar();
        const float night = calendar ? calendar->night_amount : 0.f;
        const auto tint=[&](auto &color) {
            color.r=static_cast<std::uint8_t>(color.r*(1-.62f*night));
            color.g=static_cast<std::uint8_t>(color.g*(1-.48f*night));
            color.b=static_cast<std::uint8_t>(color.b*(1-.24f*night));
        };
        std::size_t range{};
        for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
            while (range < impl_->material_vertex_ranges.size() && impl_->material_vertex_ranges[range].second <= index) ++range;
            if (range == impl_->material_vertex_ranges.size() || index < impl_->material_vertex_ranges[range].first)
                tint(frame.vertices[index].color);
        }
        tint(frame.clear);
    }
    // Emissive windows and fires retain their amber color after the night grade.
    impl_->atmosphere(w);

    if (!legacy_hud && impl_->annotations_visible)
        impl_->minimap(w);

    if(impl_->annotations_visible && impl_->selecting && std::hypot(impl_->last_mouse_x-impl_->selection_x,impl_->last_mouse_y-impl_->selection_y)>5) {
        SDL_FRect box{std::min(impl_->selection_x,impl_->last_mouse_x),std::min(impl_->selection_y,impl_->last_mouse_y),
                     std::abs(impl_->last_mouse_x-impl_->selection_x),std::abs(impl_->last_mouse_y-impl_->selection_y)};
        fill(&builder,box.x,box.y,box.w,box.h,{60,164,213,35});
        builder.outline(box,{229,198,124,255});
    }
    if (legacy_hud)
        impl_->hud(w);
    if (frame.linear_hdr_world)
        frame.surface_vertices.resize(frame.vertices.size(),render::SurfaceVertex{});
    impl_->draw = nullptr;
    impl_->r = nullptr;
    impl_->draw_frame = nullptr;
}
EngineControlStats GameView::compose_engine_control(
    const ClientSession &session, double seconds, const EngineControlSettings &input_settings,
    std::span<const EngineControlInstance> instances, render::Frame2D &frame) {
    if (!impl_->engine_control_art || impl_->engine_control_ground == render::TextureHandle{})
        throw std::logic_error("Engine control art must be loaded at startup before composition");
    if (impl_->active_layer != WorldLayer::Surface)
        throw std::invalid_argument("Engine control composition supports only the surface layer");
    if (input_settings.atmosphere && input_settings.environment)
        throw std::invalid_argument("Manual atmosphere and environment V1 are mutually exclusive");
    if (!finite_control_value(input_settings.ground_wetness01) ||
        input_settings.ground_wetness01 < 0.f || input_settings.ground_wetness01 > 1.f)
        throw std::invalid_argument("Engine diagnostic ground wetness must be finite within [0, 1]");
    EngineControlSettings settings = input_settings;
    switch (settings.weather_source) {
    case EngineWeatherSource::ExplicitDiagnostic:
        break;
    case EngineWeatherSource::SessionAuthority:
        if (settings.time_source != EngineTimeSource::SessionShown || !session.ready() ||
            session.reconnecting() || !session.world_ready() || !session.weather().known ||
            !settings.water_enabled || !settings.environment || settings.atmosphere)
            throw std::invalid_argument("Engine weather authority requires session shown time, ready known world, water and environment V1");
        // Validate overridden diagnostic inputs too; invalid values cannot be
        // silently repaired merely by choosing a different weather provider.
        if (!finite_control_value(settings.water_wind) ||
            settings.water_wind < 0.f || settings.water_wind > 1.f)
            throw std::invalid_argument("Engine diagnostic water wind must be finite within [0, 1]");
        break;
    default:
        throw std::invalid_argument("Engine weather source is invalid");
    }
    std::optional<Impl::PreparedMotion> prepared_motion;
    std::optional<EngineWeatherProjectionV1> authoritative_weather;
    double shown_seconds = seconds;
    switch (settings.time_source) {
    case EngineTimeSource::ExplicitDiagnostic:
        break;
    case EngineTimeSource::SessionShown:
        if (!session.ready() || session.reconnecting() || !session.world_ready() ||
            !session.weather().known ||
            !settings.water_enabled || !settings.environment)
            throw std::invalid_argument("Engine session shown time requires a ready world, water and environment V1");
        prepared_motion = impl_->prepare_motion(session, seconds);
        settings.environment->time_hour = engine_time_hour(
            prepared_motion->clock.tick() + static_cast<double>(session.weather().calendar_offset_ticks));
        shown_seconds = prepared_motion->clock.tick() * tick_seconds;
        break;
    default:
        throw std::invalid_argument("Engine time source is invalid");
    }
    if (settings.weather_source == EngineWeatherSource::SessionAuthority) {
        // SessionShown already derived the hour. Check the caller's remaining
        // V1 fields/version before replacing cloud/haze with validated authority.
        (void)project_engine_environment(*settings.environment, settings.lighting);
        authoritative_weather = project_engine_weather(session.weather(), session.ticks(),
            prepared_motion->clock.tick(), settings.environment->atmosphere_enabled);
        settings.environment = authoritative_weather->environment;
        settings.water_wind = authoritative_weather->wind01;
        settings.ground_wetness01 = authoritative_weather->wetness01;
    }
    if (settings.environment) {
        if (!settings.water_enabled)
            throw std::invalid_argument("Engine environment V1 requires water and WaterAtmosphereSlice");
        const auto environment = *settings.environment;
        const auto resolved = project_engine_environment(environment, settings.lighting);
        settings.lighting = resolved.lighting;
        settings.moon = resolved.moon;
        settings.atmosphere = EngineAtmosphereSettings{
            environment.atmosphere_enabled, environment.time_hour, environment.haze01};
        settings.environment.reset();
    }
    if (!std::isfinite(seconds) || !finite_control_value(settings.camera_x) ||
        !finite_control_value(settings.camera_y) || !finite_control_value(settings.zoom) ||
        settings.zoom < engine_control_min_zoom ||
        settings.zoom > (settings.atmosphere ? 3.f : engine_control_max_zoom) ||
        settings.cell_radius < 1 || settings.cell_radius > engine_control_max_cell_radius ||
        !finite_control_value(settings.exposure) || settings.exposure <= 0.f ||
        settings.exposure > 64.f)
        throw std::invalid_argument("Engine control camera, time, radius, zoom or exposure is unsupported");
    if (settings.camera_x < static_cast<float>(world_min + settings.cell_radius) ||
        settings.camera_y < static_cast<float>(world_min + settings.cell_radius) ||
        settings.camera_x > static_cast<float>(world_max - 1 - settings.cell_radius) ||
        settings.camera_y > static_cast<float>(world_max - 1 - settings.cell_radius))
        throw std::invalid_argument("Engine control camera pan is outside the bounded world window");
    if (settings.lighting.counts[0] > render::surface_light_capacity ||
        settings.lighting.counts[1] || settings.lighting.counts[2] || settings.lighting.counts[3])
        throw std::invalid_argument("Engine control external light count/reserved fields are invalid");
    if (settings.water_enabled &&
        (impl_->engine_water_normal == render::TextureHandle{} || !std::isfinite(shown_seconds) ||
         shown_seconds < 0. || shown_seconds > 1000000. || !finite_control_value(settings.water_wind) ||
         settings.water_wind < 0.f || settings.water_wind > 1.f ||
         !finite_control_value(settings.water_flow[0]) || !finite_control_value(settings.water_flow[1]) ||
         std::abs(settings.water_flow[0] * settings.water_flow[0] +
                  settings.water_flow[1] * settings.water_flow[1] - 1.f) > 1.e-4f))
        throw std::invalid_argument("Engine water requires startup normals, a bounded shown clock and normalized flow");
    for (std::uint32_t index = 0; index < settings.lighting.counts[0]; ++index)
        if (settings.lighting.lights[index].occluder_range_flags[0] ||
            settings.lighting.lights[index].occluder_range_flags[1])
            throw std::invalid_argument("Engine control external lights require zero blocker ranges");
    render::validate_moon_lighting(settings.moon);
    const auto atmosphere = settings.atmosphere
        ? std::optional{control_atmosphere(settings, shown_seconds)} : std::nullopt;
    impl_->sync_terrain(session);

    struct GroundCell {
        int x{}, y{};
        bool water{};
        bool terrain_water{};
        std::uint32_t shore_edges{};
        std::array<float, 4> heights{};
        std::array<std::array<float, 3>, 4> render_corners{};
        std::array<SDL_FPoint, 4> screen_corners{};
        std::array<std::array<float, 3>, 2> normals{};
    };
    const render::ContactProjection projection{
        {impl_->logical_w * .48f, impl_->logical_h * .48f}, settings.zoom};
    render::validate_contact_projection(projection);
    const int center_x = static_cast<int>(std::floor(settings.camera_x + .5f));
    const int center_y = static_cast<int>(std::floor(settings.camera_y + .5f));
    std::vector<GroundCell> cells;
    const int diameter = settings.cell_radius * 2 + 1;
    cells.reserve(static_cast<std::size_t>(diameter * diameter));
    for (int y = center_y - settings.cell_radius; y <= center_y + settings.cell_radius; ++y)
        for (int x = center_x - settings.cell_radius; x <= center_x + settings.cell_radius; ++x) {
            if (!impl_->height_sample(x, y).explored) {
                if (settings.water_enabled) continue; // No unknown receiver or shore information.
                throw std::invalid_argument(
                    "Engine control ground window requires every requested cell to be explored");
            }
            GroundCell cell;
            cell.x = x;
            cell.y = y;
            cell.terrain_water = session.tile(x, y).terrain == Terrain::Water;
            cell.water = settings.water_enabled && cell.terrain_water;
            if (cell.water) {
                constexpr std::array<std::array<int, 2>, 4> neighbors{{{0,-1},{1,0},{0,1},{-1,0}}};
                for (std::size_t edge = 0; edge < neighbors.size(); ++edge) {
                    const auto sample = impl_->height_sample(x + neighbors[edge][0], y + neighbors[edge][1]);
                    if (sample.explored && sample.terrain != Terrain::Water)
                        cell.shore_edges |= 1u << edge;
                }
            }
            cell.heights = terrain_corners(x, y, [this](int sample_x, int sample_y) {
                return impl_->height_sample(sample_x, sample_y);
            });
            constexpr std::array<std::array<float, 2>, 4> offsets{
                {{{-.5f, -.5f}}, {{.5f, -.5f}}, {{.5f, .5f}}, {{-.5f, .5f}}}};
            for (std::size_t corner = 0; corner < cell.render_corners.size(); ++corner) {
                cell.render_corners[corner] = {
                    static_cast<float>(x) + offsets[corner][0] - settings.camera_x,
                    static_cast<float>(y) + offsets[corner][1] - settings.camera_y,
                    render::render_z_from_legacy_elevation(cell.heights[corner])};
                if (!control_coordinate_in_bounds(cell.render_corners[corner]))
                    throw std::invalid_argument("Engine control ground exceeds [-32, 32]");
                const auto projected = render::contact_project_logical(
                    cell.render_corners[corner], projection);
                cell.screen_corners[corner] = {projected[0], projected[1]};
            }
            cell.normals[0] = control_normal(cell.render_corners[0], cell.render_corners[1],
                                             cell.render_corners[2]);
            cell.normals[1] = control_normal(cell.render_corners[0], cell.render_corners[2],
                                             cell.render_corners[3]);
            cells.push_back(cell);
        }
    std::stable_sort(cells.begin(), cells.end(), [](const GroundCell &left, const GroundCell &right) {
        if (left.x + left.y != right.x + right.y) return left.x + left.y < right.x + right.y;
        return left.x < right.x;
    });

    struct VisibleInstance {
        const EngineControlInstance *source{};
        const GroundCell *cell{};
        float ground_height{};
        std::array<float, 3> normal{};
    };
    EngineControlStats stats;
    std::vector<VisibleInstance> visible;
    visible.reserve(std::min(instances.size(), engine_control_instance_capacity));
    for (const auto &instance : instances) {
        if (!finite_control_value(instance.x) || !finite_control_value(instance.y))
            throw std::invalid_argument("Engine control instance coordinates must be finite");
        constexpr float instance_world_min = static_cast<float>(world_min) - .5f;
        constexpr float instance_world_max = static_cast<float>(world_max) - .5f;
        if (instance.x < instance_world_min || instance.x >= instance_world_max ||
            instance.y < instance_world_min || instance.y >= instance_world_max)
            throw std::invalid_argument("Engine control instance coordinates are outside the world");
        const int cell_x = static_cast<int>(std::floor(instance.x + .5f));
        const int cell_y = static_cast<int>(std::floor(instance.y + .5f));
        if (!impl_->height_sample(cell_x, cell_y).explored) {
            ++stats.instances_hidden;
            continue;
        }
        const auto cell = std::find_if(cells.begin(), cells.end(), [&](const GroundCell &candidate) {
            return candidate.x == cell_x && candidate.y == cell_y;
        });
        if (cell == cells.end())
            throw std::invalid_argument("Visible engine control instance is outside the bounded ground window");
        if (visible.size() == engine_control_instance_capacity)
            throw std::invalid_argument("Engine control visible instance capacity exceeded");
        if (std::find(engine_control_sprite_ids.begin(), engine_control_sprite_ids.end(),
                      instance.sprite_id) == engine_control_sprite_ids.end())
            throw std::invalid_argument("Unsupported visible engine control sprite ID");
        if (!finite_control_value(instance.geometry_scale) || instance.geometry_scale != 1.f ||
            !finite_control_value(instance.depth_offset) || !instance.owner_id ||
            !instance.first_occluder_id)
            throw std::invalid_argument(
                "Visible engine controls require scale 1, finite depth and non-zero IDs");
        const float u = instance.x - static_cast<float>(cell_x) + .5f;
        const float v = instance.y - static_cast<float>(cell_y) + .5f;
        const auto &height = cell->heights;
        const float ground_height = u >= v
            ? height[0] * (1.f - u) + height[1] * (u - v) + height[2] * v
            : height[0] * (1.f - v) + height[2] * u + height[3] * (v - u);
        visible.push_back({&instance, &*cell, ground_height, cell->normals[u >= v ? 0u : 1u]});
    }
    std::stable_sort(visible.begin(), visible.end(), [](const VisibleInstance &left,
                                                        const VisibleInstance &right) {
        const float left_depth = left.source->x + left.source->y + left.source->depth_offset;
        const float right_depth = right.source->x + right.source->y + right.source->depth_offset;
        return left_depth < right_depth;
    });

    render::Frame2D candidate;
    candidate.logical_size = {static_cast<std::uint32_t>(impl_->logical_w),
                              static_cast<std::uint32_t>(impl_->logical_h)};
    candidate.clear = {8, 12, 18, 255};
    candidate.linear_hdr_world = true;
    candidate.world_exposure = settings.exposure;
    candidate.contact_projection = projection;
    candidate.surface_lighting = settings.lighting;
    candidate.moon_lighting = settings.moon;
    candidate.atmosphere = atmosphere;
    if (candidate.atmosphere && candidate.atmosphere->counts_version[2] != 0u) {
        // Merge only identical revealed-water row runs on immediately adjacent
        // rows. No rectangle may jump across land, an unknown tile or a hole.
        std::vector<std::array<int, 2>> water_cells;
        for (const auto& cell : cells) if (cell.water) water_cells.push_back({cell.x, cell.y});
        std::sort(water_cells.begin(), water_cells.end(), [](const auto& left, const auto& right) {
            return left[1] == right[1] ? left[0] < right[0] : left[1] < right[1];
        });
        auto& payload = *candidate.atmosphere;
        for (std::size_t index = 0; index < water_cells.size();) {
            const int first_x = water_cells[index][0];
            const int y = water_cells[index][1];
            int last_x = first_x;
            ++index;
            while (index < water_cells.size() && water_cells[index][1] == y &&
                   water_cells[index][0] == last_x + 1) last_x = water_cells[index++][0];
            const std::array<float, 4> run{static_cast<float>(first_x) - .5f,
                static_cast<float>(y) - .5f, static_cast<float>(last_x) + .5f, static_cast<float>(y) + .5f};
            const auto end = payload.water_aabbs.begin() + payload.counts_version[0];
            const auto previous = std::find_if(payload.water_aabbs.begin(), end, [&](const auto& patch) {
                return patch[0] == run[0] && patch[2] == run[2] && patch[3] == run[1];
            });
            if (previous != end) (*previous)[3] = run[3];
            else {
                if (payload.counts_version[0] == render::atmosphere_water_capacity)
                    throw std::invalid_argument("Engine atmosphere revealed-water patch capacity exceeded");
                payload.water_aabbs[payload.counts_version[0]++] = run;
            }
        }
        render::validate_atmosphere_gpu(payload);
        stats.atmosphere_water_patches = payload.counts_version[0];
    }
    if (settings.water_enabled) {
        render::WaterSurfaceGpu water;
        water.world_origin_time = {settings.camera_x, settings.camera_y,
                                   static_cast<float>(shown_seconds), settings.water_wind};
        water.flow_response = {settings.water_flow[0], settings.water_flow[1],
                               std::clamp(.16f + .18f * (1.f - settings.water_wind), .12f, .34f), .3f};
        candidate.water_surface = water;
        candidate.water_normal = impl_->engine_water_normal;
    }
    render::GroundMaterialDraw ground_material;
    ground_material.albedo = impl_->engine_control_ground;
    ground_material.surface.response = {1.f, settings.ground_wetness01, 0.f, 1.f};
    ground_material.surface.flags = {1u, 0u, 0u, 0u};
    ground_material.surface.visibility = {1.f, 1.f, 0.f, 0.f};
    if (settings.contact_enabled)
        ground_material.contact_receiver = render::ContactReceiver::Slope;
    candidate.ground_material_draws.push_back(ground_material);
    // Sixteen actual orthogonal shore patterns, not one material/draw per cell.
    if (settings.water_enabled) {
        candidate.ground_material_draws.front().contact_receiver = render::ContactReceiver::Slope;
        candidate.ground_material_draws.front().geometric_coverage = 0.f;
        for (std::uint32_t shore = 0; shore < 16u; ++shore) {
            auto material = ground_material;
            material.water_surface_class = render::GroundSurfaceClass::Water;
            material.contact_receiver = render::ContactReceiver::Ordinary;
            material.shore_edges = shore;
            material.geometric_coverage = 1.f;
            material.surface.response = {.22f, 1.f, 0.f, 1.f};
            candidate.ground_material_draws.push_back(material);
        }
    }
    std::uint32_t ordinary_ground_index{};
    if (settings.contact_enabled) {
        auto ordinary_ground = ground_material;
        ordinary_ground.contact_receiver = render::ContactReceiver::Ordinary;
        ordinary_ground.geometric_coverage = settings.water_enabled ? 0.f : 1.f;
        candidate.ground_material_draws.push_back(ordinary_ground);
        ordinary_ground_index = static_cast<std::uint32_t>(candidate.ground_material_draws.size());
    }
    constexpr std::array<render::Point, 4> ground_uvs{
        render::Point{0.f, 0.f}, render::Point{1.f, 0.f},
        render::Point{1.f, 1.f}, render::Point{0.f, 1.f}};
    constexpr std::array<std::size_t, 6> triangle_corners{{0, 1, 2, 0, 2, 3}};
    for (const auto &cell : cells) {
        const auto first_vertex = static_cast<std::uint32_t>(candidate.vertices.size());
        const auto first_index = static_cast<std::uint32_t>(candidate.indices.size());
        for (std::size_t triangle_vertex = 0; triangle_vertex < triangle_corners.size();
             ++triangle_vertex) {
            const auto corner = triangle_corners[triangle_vertex];
            const auto screen = cell.screen_corners[corner];
            candidate.vertices.push_back(
                {{screen.x, screen.y}, ground_uvs[corner], {255, 255, 255, 255}});
            candidate.surface_vertices.push_back(
                {cell.render_corners[corner], cell.normals[triangle_vertex < 3 ? 0 : 1]});
            candidate.indices.push_back(first_vertex + static_cast<std::uint32_t>(triangle_vertex));
        }
        render::GeometryDraw draw;
        draw.pass = render::Pass::Terrain;
        draw.blend = render::Blend::Opaque;
        draw.first_index = first_index;
        draw.index_count = 6;
        draw.ground_material_index = cell.water ? 2u + cell.shore_edges
            : settings.contact_enabled && cell.terrain_water ? ordinary_ground_index : 1u;
        if (settings.contact_enabled && !cell.terrain_water) {
            // Steep faces stay visible but are explicitly ineligible receivers.
            // Splitting only this diagnostic draw keeps the material ABI intact.
            const std::array<std::uint32_t, 2> receivers{
                cell.normals[0][2] >= render::contact_shadow_min_upward_normal ? 1u : ordinary_ground_index,
                cell.normals[1][2] >= render::contact_shadow_min_upward_normal ? 1u : ordinary_ground_index};
            draw.ground_material_index = receivers[0];
            if (receivers[0] == receivers[1]) candidate.commands.emplace_back(draw);
            else {
                draw.index_count = 3;
                candidate.commands.emplace_back(draw);
                draw.first_index += 3u;
                draw.ground_material_index = receivers[1];
                candidate.commands.emplace_back(draw);
            }
        } else candidate.commands.emplace_back(draw);
        if (cell.water) ++stats.water_cells;
    }
    stats.ground_cells = cells.size();

    // Completed bridges must overwrite the receiver before GroundLighting,
    // not hide the whole water cell in a later sprite pass. This bounded
    // technical deck is deliberately narrower than a tile, leaving openings.
    std::vector<Impl::PickSurface> deck_picks;
    std::vector<Impl::DeckReceiver> deck_receivers;
    if (settings.water_enabled || settings.contact_enabled) {
        auto deck_material = ground_material;
        deck_material.water_surface_class = settings.water_enabled
            ? render::GroundSurfaceClass::BridgeDeck : render::GroundSurfaceClass::Land;
        deck_material.contact_receiver = render::ContactReceiver::Bridge;
        deck_material.geometric_coverage = settings.water_enabled ? 0.f : 1.f;
        candidate.ground_material_draws.push_back(deck_material);
        const auto deck_material_index = static_cast<std::uint32_t>(candidate.ground_material_draws.size());
        for (const auto &building : session.buildings()) {
            if (building.layer != WorldLayer::Surface || building.kind != BuildingKind::Bridge ||
                building.progress < 1.f) continue;
            const auto cell = std::find_if(cells.begin(), cells.end(), [&](const GroundCell &entry) {
                return entry.x == building.x && entry.y == building.y && entry.terrain_water;
            });
            if (cell == cells.end()) continue;
            const auto west = impl_->height_sample(building.x - 1, building.y);
            const auto east = impl_->height_sample(building.x + 1, building.y);
            const bool connected_x = std::any_of(session.buildings().begin(), session.buildings().end(),
                [&](const Building &neighbor) {
                    return neighbor.layer == WorldLayer::Surface && neighbor.kind == BuildingKind::Bridge &&
                           neighbor.progress >= 1.f && neighbor.y == building.y &&
                           std::abs(neighbor.x - building.x) == 1;
                });
            const bool along_x = (west.explored && west.terrain != Terrain::Water) ||
                                 (east.explored && east.terrain != Terrain::Water) || connected_x;
            const float half_x = along_x ? .5f : .22f;
            const float half_y = along_x ? .22f : .5f;
            const std::array<std::array<float, 3>, 4> corners{{
                {building.x - settings.camera_x - half_x, building.y - settings.camera_y - half_y, .08f},
                {building.x - settings.camera_x + half_x, building.y - settings.camera_y - half_y, .08f},
                {building.x - settings.camera_x + half_x, building.y - settings.camera_y + half_y, .08f},
                {building.x - settings.camera_x - half_x, building.y - settings.camera_y + half_y, .08f}}};
            std::array<SDL_FPoint, 4> screen{};
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                const auto point = render::contact_project_logical(corners[corner], projection);
                screen[corner] = {point[0], point[1]};
            }
            render::GeometryDraw draw;
            draw.pass = render::Pass::Terrain;
            draw.blend = render::Blend::Opaque;
            draw.first_index = static_cast<std::uint32_t>(candidate.indices.size());
            draw.index_count = 6;
            draw.ground_material_index = deck_material_index;
            const auto first = static_cast<std::uint32_t>(candidate.vertices.size());
            for (std::size_t index = 0; index < triangle_corners.size(); ++index) {
                const auto corner = triangle_corners[index];
                candidate.vertices.push_back({{screen[corner].x, screen[corner].y}, ground_uvs[corner],
                                               {255, 255, 255, 255}});
                candidate.surface_vertices.push_back({corners[corner], {0.f, 0.f, 1.f}});
                candidate.indices.push_back(first + static_cast<std::uint32_t>(index));
            }
            candidate.commands.emplace_back(draw);
            deck_picks.push_back({building.x, building.y, screen});
            deck_receivers.push_back({static_cast<float>(building.x), static_cast<float>(building.y),
                                      half_x, half_y});
            ++stats.bridge_decks;
        }
    }

    const std::array<float, 3> camera_world{settings.camera_x, settings.camera_y, 0.f};
    for (const auto &entry : visible) {
        const auto &instance = *entry.source;
        const bool on_deck = settings.contact_enabled && std::any_of(
            deck_receivers.begin(), deck_receivers.end(), [&](const Impl::DeckReceiver &deck) {
                return std::abs(instance.x - deck.x) <= deck.half_x &&
                       std::abs(instance.y - deck.y) <= deck.half_y;
            });
        const std::array<float, 3> world_anchor{
            instance.x, instance.y,
            on_deck ? .08f : render::render_z_from_legacy_elevation(entry.ground_height)};
        const auto render_anchor = render::camera_relative_render_xyz(world_anchor, camera_world);
        if (!control_coordinate_in_bounds(render_anchor))
            throw std::invalid_argument("Engine control sprite anchor exceeds [-32, 32]");
        const auto projected = render::contact_project_logical(render_anchor, projection);
        const auto vertices_before = candidate.vertices.size();
        const auto surfaces_before = candidate.surface_vertices.size();
        impl_->engine_control_art->lit_sprite(
            candidate, instance.sprite_id, {projected[0], projected[1]}, render_anchor,
            settings.zoom, instance.geometry_scale, settings.ground_wetness01, instance.owner_id,
            instance.emission_enabled ? 1.f : 0.f);
        (void)impl_->engine_control_art->append_lit_emitters(
            candidate, instance.sprite_id, render_anchor, instance.geometry_scale,
            instance.first_light_id, instance.emission_enabled);
        if (settings.contact_enabled && (on_deck || (!entry.cell->terrain_water &&
            entry.normal[2] >= render::contact_shadow_min_upward_normal))) {
            const auto normal = on_deck ? std::array<float, 3>{0.f, 0.f, 1.f} : entry.normal;
            // Project +X into the exact receiver triangle, then complete a
            // right-handed orthonormal basis. Foot height uses that same triangle.
            const float length = std::hypot(normal[2], normal[0]);
            if (!std::isfinite(length) || length <= std::numeric_limits<float>::epsilon())
                throw std::invalid_argument("Engine contact receiver has no valid tangent");
            const std::array<float, 3> axis_u{normal[2] / length, 0.f, -normal[0] / length};
            const std::array<float, 3> axis_v{
                normal[1] * axis_u[2], normal[2] * axis_u[0] - normal[0] * axis_u[2],
                -normal[1] * axis_u[0]};
            stats.contact_shadows += impl_->engine_control_art->append_lit_contacts(
                candidate, instance.sprite_id, render_anchor, instance.geometry_scale,
                axis_u, axis_v, instance.owner_id, instance.first_contact_id);
        } else if (settings.contact_enabled) ++stats.contact_receivers_skipped;
        if (candidate.vertices.size() != vertices_before + 4u ||
            candidate.surface_vertices.size() != surfaces_before)
            throw std::logic_error("Engine control lit sprite companion-stream contract changed");
        candidate.surface_vertices.insert(candidate.surface_vertices.end(), 4u, {});
        if (settings.water_enabled) {
            const bool near_water = std::any_of(cells.begin(), cells.end(), [&](const GroundCell &cell) {
                const float dx = std::max(std::abs(instance.x - cell.x) - .5f, 0.f);
                const float dy = std::max(std::abs(instance.y - cell.y) - .5f, 0.f);
                return cell.water && dx * dx + dy * dy <= 2.5f * 2.5f;
            });
            if (near_water) {
                const auto source_draw = std::get<render::GeometryDraw>(candidate.commands.back());
                const auto plane = render::contact_project_logical({render_anchor[0], render_anchor[1], 0.f}, projection);
                const float height = std::abs(candidate.vertices[vertices_before + 2u].position.y -
                                              candidate.vertices[vertices_before].position.y);
                std::array<render::Vertex, 4> mirrored{};
                for (std::size_t index = 0; index < mirrored.size(); ++index) {
                    mirrored[index] = candidate.vertices[vertices_before + index];
                    mirrored[index].position.y = 2.f * plane[1] - mirrored[index].position.y + .06f * height;
                }
                const auto bounds = std::minmax_element(mirrored.begin(), mirrored.end(),
                    [](const render::Vertex &a, const render::Vertex &b) { return a.position.y < b.position.y; });
                const float margin = 32.f * impl_->logical_h / 900.f;
                if (bounds.second->position.y >= -margin && bounds.first->position.y <= impl_->logical_h + margin) {
                    const auto first = static_cast<std::uint32_t>(candidate.vertices.size());
                    const auto first_index = static_cast<std::uint32_t>(candidate.indices.size());
                    candidate.vertices.insert(candidate.vertices.end(), mirrored.begin(), mirrored.end());
                    candidate.surface_vertices.insert(candidate.surface_vertices.end(), 4u, {});
                    constexpr std::array<std::uint32_t, 6> quad_indices{0, 1, 2, 0, 2, 3};
                    for (const auto index : quad_indices) candidate.indices.push_back(first + index);
                    auto reflected = source_draw;
                    reflected.first_index = first_index;
                    reflected.scissor_enabled = false;
                    candidate.reflection_draws.push_back(
                        {reflected, candidate.water_surface->flow_response[2], 1.7f});
                    ++stats.reflection_draws;
                }
            }
        }
        const auto appended_occluders = impl_->engine_control_art->append_lit_occluders(
            candidate, instance.sprite_id, render_anchor, instance.geometry_scale,
            instance.owner_id, instance.first_occluder_id);
        if (candidate.local_occluders.size() > render::local_occluder_capacity)
            throw std::invalid_argument("Engine control local occluder capacity exceeded");
        stats.local_occluders += appended_occluders;
        stats.sky_triangles += impl_->engine_control_art->append_lit_sky_casters(
            candidate, instance.sprite_id, world_anchor, instance.geometry_scale, camera_world);
        ++stats.instances_drawn;
    }
    // Bounded control cohort: every visible blocker participates. Overflow
    // fails rather than silently dropping a source or its shadow candidates.
    const auto authored_light_first = settings.lighting.counts[0];
    if (candidate.surface_lighting.counts[0] > authored_light_first &&
        candidate.local_occluders.size() > render::local_occluders_per_light)
        throw std::invalid_argument("Engine control authored light blocker range exceeds 32");
    for (std::uint32_t index = authored_light_first; index < candidate.surface_lighting.counts[0]; ++index) {
        auto &range = candidate.surface_lighting.lights[index].occluder_range_flags;
        range[0] = 0u;
        range[1] = static_cast<std::uint32_t>(candidate.local_occluders.size());
    }
    stats.local_lights = candidate.surface_lighting.counts[0];
    if (candidate.vertices.size() != candidate.surface_vertices.size())
        throw std::logic_error("Engine control position/normal companion streams are misaligned");
    validate_control_sprite_bounds(candidate);
    validate_control_occluder_bounds(candidate);
    validate_control_caster_bounds(candidate);
    (void)render::prepare_sky_shadows(candidate.sky_shadow_triangles,
                                      candidate.surface_lighting, *candidate.moon_lighting);

    std::vector<Impl::PickSurface> candidate_pick_surfaces;
    candidate_pick_surfaces.reserve(cells.size());
    for (const auto &cell : cells)
        candidate_pick_surfaces.push_back({cell.x, cell.y, cell.screen_corners});
    candidate_pick_surfaces.insert(candidate_pick_surfaces.end(), deck_picks.begin(), deck_picks.end());
    stats.authoritative_weather = authoritative_weather;
    frame = std::move(candidate);
    impl_->material_active = false;
    impl_->material_vertex_ranges.clear();
    impl_->cam_x = settings.camera_x;
    impl_->cam_y = settings.camera_y;
    impl_->zoom = settings.zoom;
    impl_->pick_surfaces = std::move(candidate_pick_surfaces);
    impl_->pick_layer = impl_->active_layer;
    impl_->pick_cam_x = impl_->cam_x;
    impl_->pick_cam_y = impl_->cam_y;
    impl_->pick_zoom = impl_->zoom;
    impl_->contact_decks = settings.contact_enabled ? std::move(deck_receivers)
                                                   : std::vector<Impl::DeckReceiver>{};
    impl_->contact_surface_queries = settings.contact_enabled;
    // Candidate validation and all presentation publication precede the clock
    // commit. A rejected frame cannot change the next shown phase or track reset.
    if (prepared_motion) impl_->commit_motion(*prepared_motion);
    return stats;
}
bool GameView::paused() const {
    return impl_->pause;
}
int GameView::speed() const {
    return impl_->sim_speed;
}
void GameView::set_status(std::string s) {
    impl_->set_status(std::move(s));
}
std::optional<std::string> GameView::take_status_text(const Localization &localization) {
    auto notice = std::exchange(impl_->pending_status, std::nullopt);
    if (notice) *notice = localization.text_or_literal(*notice);
    return notice;
}
void GameView::set_paused(bool value) {
    impl_->pause = value;
}
void GameView::set_speed(int value) {
    impl_->sim_speed = std::clamp(value, 1, 4);
}
void GameView::set_annotations_visible(bool visible) {
    impl_->annotations_visible = visible;
}
std::optional<SDL_FPoint> GameView::resident_screen(const ClientSession &w, int id) const {
    for (const auto &u : w.residents())
        if (u.id == id && u.layer == impl_->active_layer) return impl_->resident_point(u);
    return {};
}
void GameView::arm_build(BuildingKind kind) {
    impl_->arm(kind);
}
void GameView::arm_worksite(ProfessionAssignment assignment, std::vector<int> residents) {
    impl_->build_mode = false;
    impl_->worksite_assignment = std::move(assignment);
    impl_->worksite_residents = std::move(residents);
    impl_->worksite_error.reset();
}
bool GameView::worksite_armed() const {
    return impl_->worksite_assignment.has_value();
}
void GameView::cancel_worksite() {
    impl_->worksite_assignment.reset();
    impl_->worksite_residents.clear();
    impl_->worksite_error.reset();
}
bool GameView::interaction_active() const {
    return impl_->build_mode || impl_->worksite_assignment.has_value() || impl_->selecting || impl_->dragging;
}
void GameView::cancel_interaction() {
    if (impl_->selecting)
        impl_->selected = impl_->selection_before_drag;
    impl_->selecting = false;
    impl_->dragging = false;
    impl_->build_mode = false;
    cancel_worksite();
}
std::optional<std::string> GameView::take_worksite_error() {
    std::optional<std::string> result;
    result.swap(impl_->worksite_error);
    return result;
}
void GameView::reset_world_state() {
    impl_->material_active = false;
    impl_->material_wetness = 0.f;
    impl_->material_vertex_ranges.clear();
    impl_->unit_animations.clear();
    impl_->animation_tick = 0;
    impl_->motion_session = nullptr;
    impl_->motion_clock = {};
    impl_->motion_tracks_refresh_pending = false;
    impl_->resident_motion.clear();
    impl_->animal_motion.clear();
    impl_->tile_meshes.clear();
    impl_->terrain_session = nullptr;
    impl_->heights.clear();
    impl_->materials.clear();
    impl_->biome_samples.clear();
    impl_->trail_samples.clear();
    impl_->terrain_chunk_revisions.clear();
    impl_->corner_appearance.clear();
    impl_->grass_corners.clear();
    impl_->pick_surfaces.clear();
    impl_->contact_decks.clear();
    impl_->contact_surface_queries = false;
    // Reset removes every compose-time observer; bucket/vector capacity may stay
    // allocated, but it carries no ClientSession state into the next world.
    impl_->visible_tiles_scratch.clear();
    impl_->draw_objects_scratch.clear();
    impl_->building_bins_scratch.clear();
    impl_->cargo_by_resident_scratch.clear();
    impl_->site_by_building_scratch.clear();
    impl_->level_by_building_scratch.clear();
    impl_->terrain_portals.clear();
    impl_->minimap_ready = false;
    impl_->minimap_background_dirty = true;
    impl_->gather_mode = GatherCompletion::OneDelivery;
    impl_->observed_modifiers = SDL_KMOD_NONE;
    impl_->modifiers_observed = true;
    cancel_worksite();
    impl_->selecting=false;
    impl_->selected.clear();
    impl_->selected_buildings.clear();
    impl_->build_mode = false;
    impl_->dragging = false;
    impl_->active_layer = WorldLayer::Surface;
    impl_->status.clear();
    impl_->pending_status.reset();
}
void GameView::set_layer(WorldLayer layer) {
    impl_->selecting=false;
    impl_->selected_buildings.clear();
    if (valid_layer(layer)) {
        impl_->material_active = false;
        impl_->active_layer = layer;
        impl_->minimap_ready = false;
        impl_->build_mode = false;
    }
}
WorldLayer GameView::layer() const {
    return impl_->active_layer;
}
const std::vector<int> &GameView::selected_ids() const {
    return impl_->selected;
}
void GameView::clear_building_selection() { impl_->selected_buildings.clear(); }
const std::vector<int> &GameView::selected_building_ids() const { return impl_->selected_buildings; }
CellCoord GameView::cell_at_screen(float x, float y) const {
    const auto cell = impl_->world_at(x, y);
    return {cell.first, cell.second, impl_->active_layer};
}
SDL_FPoint GameView::tile_screen(float x, float y) const {
    return impl_->iso(x, y);
}

} // namespace ribterra

bool ribterra::GameView::selection_drag_active() const { return impl_->selecting; }

namespace ribterra {
void GameView::focus_group(const ClientSession&s,const std::vector<int>&ids){impl_->selected=ids;for(const auto&r:s.residents())if(std::find(ids.begin(),ids.end(),r.id)!=ids.end()&&r.health>0){impl_->cam_x=r.x;impl_->cam_y=r.y;impl_->active_layer=r.layer;impl_->minimap_ready=false;break;}}
bool GameView::focus_minimap(const ClientSession&s,float normalized_x,float normalized_y){
    if(!impl_->minimap_ready)return false;
    const auto &projection=impl_->minimap_projection;
    const float screen_x=projection.screen.x+std::clamp(normalized_x,0.f,1.f)*projection.screen.w;
    const float screen_y=projection.screen.y+std::clamp(normalized_y,0.f,1.f)*projection.screen.h;
    const auto world=projection.unproject(screen_x,screen_y);
    const int cell_x=int(std::floor(world.x+.5f)),cell_y=int(std::floor(world.y+.5f));
    if(!inside_world(cell_x,cell_y)||!s.tile(cell_x,cell_y,impl_->active_layer).explored)return false;
    impl_->cam_x=float(cell_x);impl_->cam_y=float(cell_y);return true;
}
}

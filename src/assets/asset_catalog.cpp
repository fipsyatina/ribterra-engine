#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
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
constexpr std::size_t maximum_asset_bytes = 64 * 1024 * 1024;
constexpr std::size_t maximum_palette_bytes = 512 * 1024;
AssetCatalogReadProfile checked_profile(AssetCatalogReadProfile profile) {
    if (profile != AssetCatalogReadProfile::Default && profile != AssetCatalogReadProfile::MaterialCollection &&
        profile != AssetCatalogReadProfile::MaterialCollectionTextured)
        throw std::invalid_argument("Unknown asset catalog read profile");
    return profile;
}
std::filesystem::path raw_absolute_root(const std::filesystem::path& root) {
    if (root.empty() || (root.has_root_path() && !root.is_absolute()))
        throw std::invalid_argument("Material catalog root cannot be empty or drive/root-relative");
    // Preserve dots/parent components for the no-reparse walk; do not normalize
    // or canonicalize before checking the incoming route's ancestors.
    return root.is_absolute() ? root : std::filesystem::current_path()/root;
}
void reject_reparse(const std::filesystem::path& path) {
    for (auto component = path; !component.empty(); ) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(component)))
            throw std::invalid_argument("Material catalog rejects symlink/reparse paths");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(component.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("Material catalog rejects absent/reparse paths");
#endif
        const auto parent = component.parent_path();
        if (parent == component) break;
        component = parent;
    }
}
std::filesystem::path material_canonical_root(const std::filesystem::path& raw) {
    reject_reparse(raw);
    if (!std::filesystem::is_directory(raw))
        throw std::invalid_argument("Material catalog root must be a directory");
    return std::filesystem::canonical(raw);
}
std::size_t asset_byte_limit(AssetCatalogReadProfile profile, std::string_view id) noexcept {
    // IDs are canonical and case-sensitive. A differently named asset cannot
    // satisfy GameView's exact namespace+'.palette' identity.
    return profile == AssetCatalogReadProfile::MaterialCollection && id.ends_with(".palette")
        ? maximum_palette_bytes : maximum_asset_bytes;
}
std::filesystem::path from_utf8(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
std::vector<std::uint8_t> read_file(const std::filesystem::path &path, std::size_t limit = maximum_asset_bytes) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit)
        throw std::runtime_error(limit == maximum_palette_bytes ? "Material palette exceeds 512 KiB limit" : "Asset exceeds 64 MiB limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char *>(bytes.data()),
                                                   static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Could not read asset");
    if (limit == maximum_palette_bytes &&
        (stream.peek() != std::char_traits<char>::eof() || std::filesystem::file_size(path) != size))
        throw std::runtime_error("Material palette changed while reading");
    return bytes;
}
std::filesystem::path confined(const std::filesystem::path &root, std::string_view name,
                               const std::filesystem::path* read_root = nullptr) {
    if (name.empty() || name.find(':') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos || name.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Asset path must be a relative portable path");
    const auto relative = from_utf8(name).lexically_normal();
    if (relative.is_absolute() || relative.has_root_path())
        throw std::invalid_argument("Absolute asset paths are forbidden");
    for (const auto &part : relative)
        if (part == "..")
            throw std::invalid_argument("Asset path escapes its root");
    if (read_root) {
        reject_reparse(*read_root/from_utf8(name));
        if (std::filesystem::canonical(*read_root) != root)
            throw std::invalid_argument("Material catalog root identity changed");
    }
    const auto result = std::filesystem::weakly_canonical(root / relative);
    const auto inside = result.lexically_relative(root);
    if (inside.empty() || inside.is_absolute())
        throw std::invalid_argument("Asset path cannot be resolved inside its root");
    for (const auto &part : inside)
        if (part == "..")
            throw std::invalid_argument("Asset symlink escapes its root");
    return result;
}
bool hash_string(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string path_key(const std::filesystem::path &path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

constexpr std::uint64_t textured_manifest_limit = 512u*1024u;
constexpr std::uint64_t textured_notice_limit = 128u*1024u;
constexpr std::uint64_t textured_source_limit = 3u*1024u*1024u;
constexpr std::uint64_t textured_collection_limit = 128u*1024u*1024u;
using Json = nlohmann::json;
bool explicit_material_profile(AssetCatalogReadProfile profile) noexcept {
    return profile != AssetCatalogReadProfile::Default;
}
void textured_fields(const Json& value, std::initializer_list<std::string_view> names) {
    if (!value.is_object() || value.size() != names.size())
        throw std::invalid_argument("Textured material object has missing or extra fields");
    for (const auto name : names) if (!value.contains(std::string(name)))
        throw std::invalid_argument("Textured material object has missing or extra fields");
}
std::string textured_text(const Json& value, std::size_t limit = 1024u) {
    if (!value.is_string()) throw std::invalid_argument("Textured material field requires a string");
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > limit || std::any_of(text.begin(),text.end(),[](unsigned char c){return c < 32u || c == 127u;}))
        throw std::invalid_argument("Textured material string exceeds its bounds or contains controls");
    (void)Json(text).dump();
    return text;
}
std::string textured_id(const Json& value) {
    auto text = textured_text(value,192u);
    if (text.front() < 'a' || text.front() > 'z' || !std::all_of(text.begin(),text.end(),[](char c){
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    })) throw std::invalid_argument("Textured material ID must be canonical lowercase ASCII");
    return text;
}
std::uint64_t textured_integer(const Json& value, std::uint64_t limit, std::uint64_t minimum = 0u) {
    if (!value.is_number_integer() || value.get<double>() < static_cast<double>(minimum) || value.get<double>() > static_cast<double>(limit))
        throw std::invalid_argument("Textured material integer exceeds its bounds");
    return value.get<std::uint64_t>();
}
std::string textured_hash(const Json& value) {
    auto text = textured_text(value,64u);
    if (!hash_string(text)) throw std::invalid_argument("Textured material digest requires lowercase SHA256");
    return text;
}
std::string textured_path(const Json& value) {
    auto text = textured_text(value);
    if (text.front() == '/' || text.back() == '/' || text.find_first_of("\\:<>\"|?*") != std::string::npos)
        throw std::invalid_argument("Textured material path must be canonical and portable");
    std::size_t first{};
    while (first < text.size()) {
        const auto end = text.find('/',first);
        const auto part = std::string_view(text).substr(first,end == std::string::npos ? text.size()-first : end-first);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            throw std::invalid_argument("Textured material path has an unsafe component");
        std::string stem(part.substr(0,part.find('.')));
        std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(c >= 'A' && c <= 'Z' ? c+32u : c);});
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4u && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
            throw std::invalid_argument("Textured material path contains a reserved device name");
        if (end == std::string::npos) break;
        first = end+1u;
    }
    return text;
}
struct TexturedPathLess {
    bool operator()(const std::string& left, const std::string& right) const {
#ifdef _WIN32
        const auto lhs = from_utf8(left).native(), rhs = from_utf8(right).native();
        const auto comparison = CompareStringOrdinal(lhs.c_str(),-1,rhs.c_str(),-1,TRUE);
        if (!comparison) throw std::runtime_error("Textured path comparison failed");
        return comparison == CSTR_LESS_THAN;
#else
        const auto fold = [](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(c >= 'A' && c <= 'Z' ? c+32u : c);});return value;};
        return fold(left) < fold(right);
#endif
    }
};
Json textured_json(std::span<const std::uint8_t> bytes) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t event,Json& value){
        if (depth > 32) throw std::invalid_argument("Textured material JSON nesting exceeds its bound");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.back().emplace(value.get<std::string>()).second)
            throw std::invalid_argument("Textured material JSON contains duplicate fields");
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}

// A separate reader preserves the old profiles' exact behavior. The explicit
// profile checks actual size before allocation and handle identity after read.
std::vector<std::uint8_t> textured_read(const std::filesystem::path& root, const std::filesystem::path& raw_root,
                                      std::string_view name, std::uint64_t limit,
                                      const std::uint64_t* expected = nullptr) {
    const auto path = confined(root,name,&raw_root);
#ifdef _WIN32
    struct Handle {
        HANDLE value{INVALID_HANDLE_VALUE};
        ~Handle(){if(value != INVALID_HANDLE_VALUE) CloseHandle(value);}
    } file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
                      OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    BY_HANDLE_FILE_INFORMATION before{};
    if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value,&before) ||
        (before.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("Textured material read requires a regular non-reparse file");
    const auto size = (std::uint64_t(before.nFileSizeHigh)<<32u)|before.nFileSizeLow;
    if (size > limit || (expected && size != *expected))
        throw std::runtime_error("Textured material actual bytes exceed role limit or declared size differs");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1024u*1024u));
        DWORD read{};
        if (!ReadFile(file.value,bytes.data()+offset,count,&read,nullptr) || read != count)
            throw std::runtime_error("Textured material tight read failed");
        offset += read;
    }
    std::uint8_t extra{}; DWORD extra_read{};
    BY_HANDLE_FILE_INFORMATION after{},current{};
    Handle present{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
                              OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    const auto same = [](const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
            a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
            a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime && a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime &&
            !(b.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
    };
    if (!ReadFile(file.value,&extra,1u,&extra_read,nullptr) || extra_read ||
        !GetFileInformationByHandle(file.value,&after) || present.value == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(present.value,&current) || !same(before,after) || !same(before,current))
        throw std::runtime_error("Textured material changed/replaced while reading");
#else
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Textured material read requires a regular file");
    const auto size = std::filesystem::file_size(path);
    const auto modified = std::filesystem::last_write_time(path);
    if (size > limit || (expected && size != *expected))
        throw std::runtime_error("Textured material actual bytes exceed role limit or declared size differs");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) ||
        stream.peek() != std::char_traits<char>::eof() || std::filesystem::file_size(path) != size || std::filesystem::last_write_time(path) != modified)
        throw std::runtime_error("Textured material changed while reading");
#endif
    if (confined(root,name,&raw_root) != path) throw std::runtime_error("Textured material path identity changed");
    return bytes;
}

struct PlannedTexturedRole { std::string path,sha256; std::uint64_t bytes{},limit{}; };
std::vector<PlannedTexturedRole> plan_textured_roles(const std::filesystem::path& root,
        const std::filesystem::path& raw_root,const std::vector<AssetRecord>& records) {
    const auto record = [&](std::string_view id)->const AssetRecord& {
        const auto found = std::find_if(records.begin(),records.end(),[&](const AssetRecord& row){return row.id == id;});
        if (found == records.end()) throw std::invalid_argument("Textured material role is unregistered");
        return *found;
    };
    const auto registered_json = [&](const AssetRecord& row) {
        if (row.bytes > textured_manifest_limit || row.kind != "data")
            throw std::invalid_argument("Textured material JSON declared bytes exceed 512 KiB");
        const auto bytes = textured_read(root,raw_root,row.path,textured_manifest_limit,&row.bytes);
        if (sha256(bytes) != row.sha256) throw std::invalid_argument("Textured material JSON hash differs from its manifest");
        return textured_json(bytes);
    };
    const auto& metadata_record = record("material.collection.metadata");
    if (metadata_record.path != "art/material-collection.json" || record("art.world").path != "art/world.json")
        throw std::invalid_argument("Textured material metadata/world roles are inconsistent");
    const auto metadata = registered_json(metadata_record);
    if (metadata.at("schema") != 1 || metadata.at("kind") != "ribterra-material-collection-v1" ||
        !metadata.at("items").is_array() || metadata.at("items").empty() || metadata.at("items").size() > 8u ||
        textured_integer(metadata.at("item_count"),8u,1u) != metadata.at("items").size())
        throw std::invalid_argument("Textured material namespace count/schema exceeds bounds");
    const auto& world_record = record("art.world");
    if (world_record.source != metadata_record.source)
        throw std::invalid_argument("Textured material metadata/world source differs");
    const auto world = registered_json(world_record);
    textured_fields(world,{"schema","pages","textures","materials","lit_sprites"});
    const auto item_count = metadata.at("items").size();
    if (textured_integer(world.at("schema"),2u,2u) != 2u || !world.at("pages").is_array() || !world.at("pages").empty() ||
        !world.at("textures").is_array() || world.at("textures").size() != item_count*6u ||
        !world.at("materials").is_array() || world.at("materials").size() != item_count ||
        !world.at("lit_sprites").is_array() || world.at("lit_sprites").size() != item_count)
        throw std::invalid_argument("Textured material world schema/counts are inconsistent");
    const auto index_rows = [](const Json& rows) {
        std::map<std::string,const Json*> index;
        for (const auto& row : rows)
            if (!index.emplace(textured_text(row.at("id"),256u),&row).second)
                throw std::invalid_argument("Textured world ID is duplicated");
        return index;
    };
    const auto world_textures = index_rows(world.at("textures"));
    const auto world_materials = index_rows(world.at("materials"));
    const auto world_sprites = index_rows(world.at("lit_sprites"));
    std::set<std::string> registered_ids{"art.world","material.collection.metadata"};
    std::vector<PlannedTexturedRole> roles;
    std::map<std::string,std::size_t,TexturedPathLess> paths;
    std::set<std::string,TexturedPathLess> notice_paths;
    const auto add = [&](std::string path,std::string digest,std::uint64_t bytes,std::uint64_t limit) {
        const auto found = paths.find(path);
        if (found != paths.end()) {
            const auto& previous = roles[found->second];
            if (previous.path != path || previous.sha256 != digest || previous.bytes != bytes)
                throw std::invalid_argument("Textured material role path aliases/conflicting bindings");
            roles[found->second].limit = std::min(previous.limit,limit);
        } else { paths.emplace(path,roles.size()); roles.push_back({std::move(path),std::move(digest),bytes,limit}); }
    };
    add(metadata_record.path,metadata_record.sha256,metadata_record.bytes,textured_manifest_limit);
    add(world_record.path,world_record.sha256,world_record.bytes,textured_manifest_limit);
    if (world_record.bytes > textured_manifest_limit) throw std::invalid_argument("Textured material world declared bytes exceed 512 KiB");
    const auto notice = [&](const Json& provenance) {
        textured_fields(provenance,{"license","author","source","notice_path","notice_sha256"});
        for (const auto key : {"license","author","source"}) (void)textured_text(provenance.at(key),512u);
        const auto path = textured_path(provenance.at("notice_path")), digest = textured_hash(provenance.at("notice_sha256"));
        if (std::any_of(records.begin(),records.end(),[&](const auto& row){return !TexturedPathLess{}(row.path,path) && !TexturedPathLess{}(path,row.path);}))
            throw std::invalid_argument("Textured notice path collides with a registered payload");
        const auto resolved = confined(root,path,&raw_root);
        if (!std::filesystem::is_regular_file(resolved)) throw std::invalid_argument("Textured material notice is not a regular file");
        const auto bytes = std::filesystem::file_size(resolved);
        if (!bytes || bytes > textured_notice_limit) throw std::invalid_argument("Textured material notice exceeds 128 KiB");
        add(path,digest,bytes,textured_notice_limit);
        notice_paths.emplace(path);
    };
    std::set<std::string> namespaces,used_palettes,used_sources,used_sprite_ids;
    std::map<std::string,std::pair<std::string,Json>,TexturedPathLess> source_origins;
    std::size_t source_count{}; std::uint64_t source_total{},channel_total{},largest_channels{}; bool has_textured{};
    for (const auto& item : metadata.at("items")) {
        const auto name = textured_id(item.at("namespace"));
        (void)textured_path(item.at("namespace"));
        if (!namespaces.emplace(name).second) throw std::invalid_argument("Textured material namespace is duplicated");
        auto original_provenance = item.at("registration").at("provenance");
        textured_fields(original_provenance,{"license","author","source","notice_path","notice_sha256","asset_source_sha256"});
        (void)textured_hash(original_provenance.at("asset_source_sha256"));
        original_provenance.erase("asset_source_sha256");
        notice(original_provenance);
        // Derive every final GPU channel role from the bounded world/metadata,
        // including old painted/flat items in an explicit textured collection.
        const auto& provenance = item.contains("albedo_source") ? item.at("albedo_source").at("provenance") : original_provenance;
        const auto& source_size = item.at("registration").at("source_size");
        if (!source_size.is_array() || source_size.size() != 2u)
            throw std::invalid_argument("Textured generated source size requires two bounded integers");
        const auto generated_width = textured_integer(source_size[0],2048u,1u), generated_height = textured_integer(source_size[1],2048u,1u);
        const auto pixels = generated_width*generated_height;
        if (pixels > 1048576u) throw std::invalid_argument("Textured generated channels exceed 1048576 texels");
        const auto generated_bytes = pixels*16u;
        channel_total += generated_bytes; largest_channels = std::max(largest_channels,generated_bytes);
        if (channel_total+largest_channels > 80u*1024u*1024u)
            throw std::invalid_argument("Textured generated channel retained/transient envelope exceeds 80 MiB");
        const auto material_id = textured_text(item.at("material_id"),256u), sprite_id = textured_text(item.at("sprite_id"),256u);
        if (material_id != name+".material" || sprite_id != "dynamic."+textured_text(item.at("legacy_key"),192u) ||
            !world_materials.contains(material_id) || !world_sprites.contains(sprite_id) || !used_sprite_ids.emplace(sprite_id).second)
            throw std::invalid_argument("Textured generated material/sprite identity is inconsistent");
        const auto& world_material = *world_materials.at(material_id);
        const auto& world_sprite = *world_sprites.at(sprite_id);
        if (world_material.at("source_hash") != item.at("source_hash") || world_sprite.at("material") != material_id ||
            world_sprite.at("source_size") != source_size || world_sprite.at("pivot_px") != item.at("registration").at("pivot_px"))
            throw std::invalid_argument("Textured generated material/sprite source tuple differs");
        textured_fields(world_material.at("channels"),{"albedo","normal_material","camera_depth","emission_mask","surface_mask","caster_mask"});
        constexpr std::array<std::string_view,6> channel_names{"albedo","normal_material","camera_depth","emission_mask","surface_mask","caster_mask"};
        constexpr std::array<std::string_view,6> channel_files{"albedo.rtex","normal-material.rtex","camera-depth.rtex","emission-mask.rtex","surface-mask.rtex","caster-mask.rtex"};
        constexpr std::array<std::string_view,6> channel_formats{"rgba8_unorm_srgb","rgba8_unorm","r16_unorm","r8_unorm","rgba8_unorm","r8_unorm"};
        constexpr std::array<std::uint64_t,6> channel_strides{4u,4u,2u,1u,4u,1u};
        for (std::size_t channel=0; channel<channel_names.size(); ++channel) {
            const auto id = name+".texture."+std::string(channel_names[channel]);
            const auto path = "art/materials/"+name+"/"+std::string(channel_files[channel]);
            const auto bytes = pixels*channel_strides[channel];
            const auto& binding = world_material.at("channels").at(std::string(channel_names[channel]));
            textured_fields(binding,{"texture","rect"});
            const auto& rect = binding.at("rect");
            if (binding.at("texture") != id || !rect.is_array() || rect.size() != 4u ||
                textured_integer(rect[0],0u) != 0u || textured_integer(rect[1],0u) != 0u ||
                textured_integer(rect[2],generated_width,generated_width) != generated_width ||
                textured_integer(rect[3],generated_height,generated_height) != generated_height ||
                !world_textures.contains(id))
                throw std::invalid_argument("Textured generated channel ID/subrect differs from its material");
            const auto& texture = *world_textures.at(id);
            textured_fields(texture,{"id","path","format","width","height","layout","mip_levels","bytes","sha256"});
            const auto digest = textured_hash(texture.at("sha256"));
            if (textured_path(texture.at("path")) != path || !texture.at("format").is_string() ||
                std::string_view(texture.at("format").get_ref<const std::string&>()) != channel_formats[channel] ||
                texture.at("layout") != "tight_le" || textured_integer(texture.at("mip_levels"),1u,1u) != 1u ||
                textured_integer(texture.at("width"),2048u,1u) != generated_width || textured_integer(texture.at("height"),2048u,1u) != generated_height ||
                textured_integer(texture.at("bytes"),4u*1048576u,1u) != bytes)
                throw std::invalid_argument("Textured generated channel path/format/dimensions/tight bytes differ");
            const auto& row = record(id);
            if (row.path != path || row.kind != "data" || row.sha256 != digest || row.bytes != bytes ||
                !provenance.at("license").is_string() || row.license != provenance.at("license").get_ref<const std::string&>() || row.author != provenance.at("author").get<std::string>()+"; geometry: "+item.at("geometry_provenance").at("author").get<std::string>() ||
                !provenance.at("notice_path").is_string() || row.notice != provenance.at("notice_path").get_ref<const std::string&>() || row.source != metadata_record.source)
                throw std::invalid_argument("Textured generated channel manifest/provenance binding differs");
            if (!registered_ids.emplace(id).second) throw std::invalid_argument("Textured generated channel role is duplicated");
            add(path,digest,bytes,bytes);
        }
        if (!item.contains("albedo_source")) continue;
        const auto& source = item.at("albedo_source");
        const bool textured = source.at("kind") == "authored-xyz-triangle-textures-v1";
        if (textured) textured_fields(source,{"kind","palette_path","palette_sha256","provenance","source_albedo"});
        else {
            textured_fields(source,{"kind","palette_path","palette_sha256","provenance"});
            if (source.at("kind") != "authored-xyz-triangle-materials-v1") throw std::invalid_argument("Textured material source kind is unsupported");
        }
        has_textured |= textured;
        notice(source.at("provenance"));
        const auto palette_path = textured_path(source.at("palette_path")), palette_hash = textured_hash(source.at("palette_sha256"));
        const auto& palette_record = record(name+".palette");
        if (palette_record.path != palette_path || palette_record.sha256 != palette_hash ||
            palette_record.license != provenance.at("license").get_ref<const std::string&>() || palette_record.author != provenance.at("author").get_ref<const std::string&>() ||
            palette_record.notice != provenance.at("notice_path").get_ref<const std::string&>() || palette_record.source != metadata_record.source)
            throw std::invalid_argument("Textured palette manifest/provenance binding is inconsistent");
        const auto palette = registered_json(palette_record);
        used_palettes.emplace(palette_record.id);
        registered_ids.emplace(palette_record.id);
        add(palette_path,palette_hash,palette_record.bytes,textured_manifest_limit);
        if (!textured) continue;
        if (textured_integer(palette.at("schema"),2u,2u) != 2u || palette.at("kind") != "ribterra-neutral-textured-palette-v1" ||
            !palette.at("textures").is_array() || palette.at("textures").size() > 4u ||
            !source.at("source_albedo").is_array() || source.at("source_albedo").size() != palette.at("textures").size())
            throw std::invalid_argument("Textured palette schema or source texture count is inconsistent");
        std::map<std::string,Json> textures;
        std::set<std::string,TexturedPathLess> texture_paths;
        for (const auto& texture : palette.at("textures")) {
            textured_fields(texture,{"id","path","sha256","bytes","width","height","encoding","sampler","provenance"});
            if (!textures.emplace(textured_id(texture.at("id")),texture).second ||
                !texture_paths.emplace(textured_path(texture.at("path"))).second)
                throw std::invalid_argument("Textured palette source ID/path is duplicated or aliased");
        }
        const auto& materials = palette.at("materials");
        if (!materials.is_array() || materials.empty() || materials.size() > 256u)
            throw std::invalid_argument("Textured palette requires 1..256 materials");
        std::set<std::string> used_textures;
        for (const auto& material : materials) {
            const bool source_texture = material.contains("albedo_texture");
            if (source_texture) {
                textured_fields(material,{"id","albedo_texture","roughness","emission_mask","wet_mask","caster_mask"});
                const auto id = textured_id(material.at("albedo_texture"));
                if (!textures.contains(id)) throw std::invalid_argument("Textured palette material names an unknown source");
                used_textures.emplace(id);
            } else textured_fields(material,{"id","albedo_srgb","roughness","emission_mask","wet_mask","caster_mask"});
        }
        if (used_textures.size() != textures.size()) throw std::invalid_argument("Textured palette contains an unused source texture");
        std::string previous; std::uint64_t namespace_bytes{};
        for (const auto& binding : source.at("source_albedo")) {
            textured_fields(binding,{"id","source_path","path","sha256","bytes","width","height","encoding","sampler","provenance"});
            const auto id = textured_id(binding.at("id"));
            if ((!previous.empty() && previous >= id) || !textures.contains(id))
                throw std::invalid_argument("Textured source bindings must be unique and sorted by ID");
            previous = id;
            const auto& texture = textures.at(id);
            for (const auto key : {"sha256","bytes","width","height","encoding","sampler","provenance"})
                if (binding.at(key) != texture.at(key)) throw std::invalid_argument("Textured source binding differs from its palette");
            if (textured_path(binding.at("source_path")) != textured_path(texture.at("path")) ||
                binding.at("encoding") != "tight-rgb8-srgb" || binding.at("sampler") != "nearest-clamp-v1")
                throw std::invalid_argument("Textured source encoding/sampler/path is unsupported");
            const auto source_path = textured_path(binding.at("source_path"));
            Json origin_identity = binding; origin_identity.erase("id"); origin_identity.erase("path");
            const auto origin = source_origins.find(source_path);
            if (origin != source_origins.end() && (origin->second.first != source_path || origin->second.second != origin_identity))
                throw std::invalid_argument("Textured shared source origin has conflicting or case-aliased bindings");
            if (origin == source_origins.end()) source_origins.emplace(source_path,std::pair{source_path,std::move(origin_identity)});
            const auto width = textured_integer(binding.at("width"),1024u,1u), height = textured_integer(binding.at("height"),1024u,1u);
            const auto bytes = textured_integer(binding.at("bytes"),textured_source_limit,1u);
            if (width*height > 1048576u || bytes != width*height*3u)
                throw std::invalid_argument("Textured source payload must be exact 3WH within 3 MiB");
            const auto path = textured_path(binding.at("path")), digest = textured_hash(binding.at("sha256"));
            const auto& row = record(name+".source_albedo."+id);
            const auto& p = binding.at("provenance");
            notice(p);
            if (path != "art/materials/"+name+"/source-albedo/"+id+".rgb8.rtex" || row.path != path || row.kind != "data" ||
                row.sha256 != digest || row.bytes != bytes || row.license != p.at("license").get_ref<const std::string&>() || row.author != p.at("author").get_ref<const std::string&>() ||
                row.notice != p.at("notice_path").get_ref<const std::string&>() || row.source != metadata_record.source)
                throw std::invalid_argument("Textured source manifest/provenance/packaged path differs");
            used_sources.emplace(row.id); add(path,digest,bytes,textured_source_limit);
            registered_ids.emplace(row.id);
            namespace_bytes += bytes; source_total += bytes; ++source_count;
        }
        if (namespace_bytes > 12u*1024u*1024u || source_total > 24u*1024u*1024u || source_count > 8u)
            throw std::invalid_argument("Textured source aggregate exceeds namespace/collection limits");
    }
    if (!has_textured) throw std::invalid_argument("MaterialCollectionTextured requires an explicit textured source item");
    for (const auto& row : records) {
        if (!registered_ids.contains(row.id))
            throw std::invalid_argument("Textured material manifest contains an unplanned asset role");
        if ((row.id.ends_with(".palette") && !used_palettes.contains(row.id)) ||
            ((row.id.find(".source_albedo.") != std::string::npos || row.path.find("/source-albedo/") != std::string::npos ||
              row.path.ends_with(".rgb8.rtex")) && !used_sources.contains(row.id)))
            throw std::invalid_argument("Textured material palette/source role is unreferenced");
    }
    // The generated collection notice is a bounded auxiliary file, not a
    // registered texture/data role. Bind its actual bytes in this catalog.
    const auto collection_notice = confined(root,"NOTICE.txt",&raw_root);
    const auto notice_bytes = std::filesystem::file_size(collection_notice);
    if (!std::filesystem::is_regular_file(collection_notice) || !notice_bytes || notice_bytes > textured_notice_limit)
        throw std::invalid_argument("Textured collection notice exceeds 128 KiB or is not a regular file");
    std::uint64_t package_bytes = notice_bytes;
    for (const auto& row : records) package_bytes += row.bytes;
    for (const auto& role : roles)
        if (std::none_of(records.begin(),records.end(),[&](const auto& row){return row.path == role.path;})) package_bytes += role.bytes;
    package_bytes += std::filesystem::file_size(confined(root,"manifest.json",&raw_root));
    if (package_bytes > textured_collection_limit)
        throw std::invalid_argument("Textured material package payload exceeds 128 MiB");
    // All role sizes are checked before any RGB/notice payload allocation.
    for (const auto& role : roles) {
        const auto path = confined(root,role.path,&raw_root);
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != role.bytes || role.bytes > role.limit)
            throw std::invalid_argument("Textured material actual role bytes differ before allocation");
    }
    const auto notice_raw = textured_read(root,raw_root,"NOTICE.txt",textured_notice_limit,&notice_bytes);
    add("NOTICE.txt",sha256(notice_raw),notice_bytes,textured_notice_limit);
    notice_paths.emplace("NOTICE.txt");
    for (const auto& row : records)
        if (!notice_paths.contains(row.notice) || roles[paths.at(row.notice)].path != row.notice)
            throw std::invalid_argument("Textured manifest notice is not bound by active provenance");
    for (const auto& path : notice_paths) {
        const auto& role = roles[paths.at(path)];
        const auto bytes = textured_read(root,raw_root,role.path,role.limit,&role.bytes);
        if (sha256(bytes) != role.sha256) throw std::invalid_argument("Textured notice digest differs from active provenance");
        const std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        if (std::any_of(text.begin(),text.end(),[](unsigned char c){return (c < 32u && c != '\n' && c != '\r' && c != '\t') || c == 127u;}) ||
            std::all_of(text.begin(),text.end(),[](unsigned char c){return c == ' ' || c == '\n' || c == '\r' || c == '\t';}))
            throw std::invalid_argument("Textured notice requires nonempty UTF-8 text without binary controls");
        (void)Json(text).dump();
    }
    return roles;
}
} // namespace

AssetCatalog::AssetCatalog(const std::filesystem::path &root)
    : AssetCatalog(root,AssetCatalogReadProfile::Default) {}
AssetCatalog::AssetCatalog(const std::filesystem::path &root, AssetCatalogReadProfile profile)
    : read_profile_(checked_profile(profile)),
      read_root_(explicit_material_profile(read_profile_) ? raw_absolute_root(root) : std::filesystem::path{}),
      root_(explicit_material_profile(read_profile_) ? material_canonical_root(read_root_) : std::filesystem::canonical(root)) {
    if (explicit_material_profile(read_profile_)) reject_reparse(read_root_/"manifest.json");
    const bool textured = read_profile_ == AssetCatalogReadProfile::MaterialCollectionTextured;
    const auto bytes = textured ? textured_read(root_,read_root_,"manifest.json",textured_manifest_limit) : read_file(root_ / "manifest.json");
    const auto document = textured ? textured_json(bytes) : nlohmann::json::parse(bytes.begin(), bytes.end());
    if (document.at("schema") != 1 || !document.at("assets").is_array() ||
        document.at("assets").size() > (textured ? 256u : 100000u))
        throw std::runtime_error("Unsupported asset manifest schema or size");
    if (textured) textured_fields(document,{"schema","content_sha256","assets"});
    content_hash_ = document.at("content_sha256").get<std::string>();
    if (!hash_string(content_hash_))
        throw std::runtime_error("Invalid asset manifest hash");
    std::set<std::string> ids, paths;
    std::set<std::string,TexturedPathLess> folded_ids,folded_paths;
    std::uint64_t declared_total{};
    for (const auto &row : document.at("assets")) {
        if (textured) {
            textured_fields(row,{"id","path","kind","sha256","license","source","author","notice","bytes"});
            (void)textured_text(row.at("id"),256u); (void)textured_path(row.at("path"));
            (void)textured_path(row.at("notice"));
            for (const auto name : {"license","source","author"}) (void)textured_text(row.at(name),4096u);
            const auto count = textured_integer(row.at("bytes"),maximum_asset_bytes);
            declared_total += count;
            if (declared_total > textured_collection_limit) throw std::invalid_argument("Textured material declared payload exceeds 128 MiB");
        }
        AssetRecord record{row.at("id"),     row.at("path"),    row.at("kind"),
                           row.at("sha256"), row.at("license"), row.at("source"),
                           row.at("author"), row.at("notice"),  row.at("bytes")};
        if (record.id.empty() || record.id.size() > 256 || record.path.size() > 1024 ||
            record.license.empty() || record.source.empty() || record.author.empty() ||
            record.notice.empty() || !hash_string(record.sha256))
            throw std::runtime_error("Invalid asset manifest entry");
        const auto normalized = path_key(confined(root_, record.path,
            explicit_material_profile(read_profile_) ? &read_root_ : nullptr).lexically_relative(root_));
        static const std::map<std::string, std::string> kinds{
            {".ttf", "font"},   {".png", "texture"}, {".rml", "ui"},
            {".rcss", "style"}, {".json", "data"}, {".rtex", "data"}, {".dxil", "shader"}};
        const auto kind = kinds.find(path_key(from_utf8(record.path).extension()));
        if (kind == kinds.end() || kind->second != record.kind ||
            record.bytes > maximum_asset_bytes)
            throw std::runtime_error("Asset kind, extension or byte count is invalid");
        if (record.bytes > asset_byte_limit(read_profile_,record.id))
            throw std::runtime_error("Material palette declared bytes exceed 512 KiB limit");
        if (normalized != record.path || !ids.insert(record.id).second ||
            !paths.insert(record.path).second)
            throw std::runtime_error("Duplicate or noncanonical asset ID/path");
        if (textured && (!folded_ids.emplace(record.id).second || !folded_paths.emplace(record.path).second))
            throw std::invalid_argument("Textured material asset ID/path has a case-conflicting alias");
        assets_.push_back(std::move(record));
    }
    std::sort(assets_.begin(), assets_.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::string canonical;
    for (const auto &asset : assets_) {
        for (const auto *field : {&asset.id, &asset.path, &asset.kind, &asset.sha256,
                                  &asset.license, &asset.source, &asset.author, &asset.notice})
            canonical += std::to_string(field->size()) + ":" + *field;
        canonical += '\n';
    }
    if (sha256(std::span(reinterpret_cast<const std::uint8_t *>(canonical.data()),
                         canonical.size())) != content_hash_)
        throw std::runtime_error("Asset manifest content hash mismatch");
    if (textured) {
        const auto roles = plan_textured_roles(root_,read_root_,assets_);
        textured_roles_.reserve(roles.size()+1u);
        for (const auto& role : roles) textured_roles_.push_back({role.path,role.sha256,role.bytes,role.limit});
        textured_roles_.push_back({"manifest.json",sha256(bytes),bytes.size(),textured_manifest_limit});
    }
    verify();
}
const AssetRecord &AssetCatalog::find(std::string_view id) const {
    const auto found = std::find_if(assets_.begin(), assets_.end(),
                                    [&](const auto &item) { return item.id == id; });
    if (found == assets_.end())
        throw std::out_of_range("Unknown asset ID: " + std::string(id));
    return *found;
}
AssetCatalogReadProfile AssetCatalog::read_profile() const noexcept { return read_profile_; }
std::filesystem::path AssetCatalog::resolve(std::string_view relative_path) const {
    const auto path = confined(root_, relative_path,
        explicit_material_profile(read_profile_) ? &read_root_ : nullptr);
    const auto relative = path_key(path.lexically_relative(root_));
    if (std::none_of(assets_.begin(), assets_.end(),
                     [&](const auto &item) { return item.path == relative; }))
        throw std::out_of_range("Unregistered asset path: " + relative);
    return path;
}
std::vector<std::uint8_t> AssetCatalog::read(std::string_view id) const {
    const auto &record = find(id);
    auto bytes = [&] {
        if (read_profile_ != AssetCatalogReadProfile::MaterialCollectionTextured)
            return read_file(resolve(record.path),asset_byte_limit(read_profile_,record.id));
        const auto role = std::find_if(textured_roles_.begin(),textured_roles_.end(),[&](const auto& entry){return entry.path == record.path;});
        if (role == textured_roles_.end()) throw std::runtime_error("Textured material read rejects an unplanned asset role");
        return textured_read(root_,read_root_,record.path,role->limit,&record.bytes);
    }();
    if (bytes.size() != record.bytes || sha256(bytes) != record.sha256)
        throw std::runtime_error("Asset content hash/size mismatch: " + record.id);
    return bytes;
}
const std::vector<AssetRecord> &AssetCatalog::records() const { return assets_; }
std::vector<std::uint8_t> AssetCatalog::read_path(std::string_view relative_path) const {
    const auto relative = path_key(resolve(relative_path).lexically_relative(root_));
    const auto found = std::find_if(assets_.begin(), assets_.end(),
                                    [&](const auto &item) { return item.path == relative; });
    return read(found->id);
}
const std::string &AssetCatalog::content_hash() const { return content_hash_; }
void AssetCatalog::verify() const {
    if (read_profile_ == AssetCatalogReadProfile::MaterialCollectionTextured)
        for (const auto& role : textured_roles_) {
            if (std::any_of(assets_.begin(),assets_.end(),[&](const auto& row){return row.path == role.path;})) continue;
            const auto bytes = textured_read(root_,read_root_,role.path,role.limit,&role.bytes);
            if (sha256(bytes) != role.sha256) throw std::runtime_error("Textured material planned role hash mismatch");
        }
    for (const auto &record : assets_)
        (void)read(record.id);
}

} // namespace ribterra

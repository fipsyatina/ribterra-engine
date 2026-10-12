#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"
#include "ribterra/game_art.hpp"
#include "ribterra/renderer2d.hpp"
#include "ribterra/world_projection.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef RIBTERRA_PROBE_BUILD
#define RIBTERRA_PROBE_BUILD "unknown"
#endif

namespace authored_sky_bridge_probe {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using ribterra::AssetCatalog;
using ribterra::GameArt;
using ribterra::render::Backend;
using ribterra::render::Frame2D;
using ribterra::render::Renderer2D;
using ribterra::render::RendererOptions;
using ribterra::render::SkyShadowTriangle;
using ribterra::render::WorldGraph;

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}

fs::path from_utf8(std::string_view value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string to_utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    const auto size = fs::file_size(path);
    require(size <= std::uintmax_t(std::numeric_limits<std::size_t>::max()) &&
                size <= std::uintmax_t(std::numeric_limits<std::streamsize>::max()),
            "Input file exceeds addressable probe memory");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "Could not open probe input file");
    if (!bytes.empty())
        require(bool(stream.read(reinterpret_cast<char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))),
                "Could not read complete probe input file");
    return bytes;
}

std::string read_text(const fs::path& path) {
    const auto bytes = read_bytes(path);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void write_bytes_new(const fs::path& path, std::span<const std::uint8_t> bytes) {
    require(!fs::exists(path), "Probe writer refuses to overwrite retained evidence");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not create probe evidence file");
    if (!bytes.empty())
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    stream.close();
    require(stream.good(), "Could not finish probe evidence file");
}

void write_text_new(const fs::path& path, std::string_view text) {
    write_bytes_new(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()),
                                    text.size()));
}

void write_json_new(const fs::path& path, const Json& value) {
    write_text_new(path, value.dump(2) + "\n");
}

std::string digest(std::span<const std::uint8_t> bytes) {
    return ribterra::sha256(bytes);
}

std::string digest(std::string_view text) {
    return digest(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::string file_digest(const fs::path& path) {
    return digest(read_bytes(path));
}

constexpr std::uintmax_t derived_logical_budget = 128u * 1024u * 1024u;
constexpr std::uintmax_t derived_free_space_floor = 384u * 1024u * 1024u;

std::uintmax_t logical_tree_bytes(const fs::path& root) {
    std::uintmax_t result{};
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) result += entry.file_size();
    return result;
}

void require_derived_budget(const fs::path& output, std::string_view phase) {
    const auto logical = logical_tree_bytes(output);
    require(logical <= derived_logical_budget,
            std::string(phase) + " exceeded the 128 MiB logical evidence budget");
    require(fs::space(output).available >= derived_free_space_floor,
            std::string(phase) + " has less than 384 MiB free space");
}

void require_derived_addition(const fs::path& output, std::uintmax_t bytes,
                              std::string_view phase) {
    const auto logical = logical_tree_bytes(output);
    require(logical <= derived_logical_budget &&
                bytes <= derived_logical_budget - logical,
            std::string(phase) + " would exceed the 128 MiB logical evidence budget");
    require(fs::space(output).available >= derived_free_space_floor,
            std::string(phase) + " has less than 384 MiB free space");
}

std::string manifest_content_hash(const Json& assets) {
    std::vector<const Json*> sorted;
    sorted.reserve(assets.size());
    for (const auto& row : assets) sorted.push_back(&row);
    std::sort(sorted.begin(), sorted.end(), [](const Json* left, const Json* right) {
        return left->at("id").get<std::string>() < right->at("id").get<std::string>();
    });
    std::string canonical;
    for (const auto* row : sorted) {
        for (const auto field : {"id", "path", "kind", "sha256", "license", "source",
                                 "author", "notice"}) {
            const auto value = row->at(field).get<std::string>();
            canonical += std::to_string(value.size()) + ":" + value;
        }
        canonical += '\n';
    }
    return digest(canonical);
}

const Json& first_triangles(const Json& world) {
    const auto& sprites = world.at("lit_sprites");
    require(sprites.is_array() && !sprites.empty(), "Exported kit has no lit sprites");
    const auto& triangles = sprites.at(0).at("sky_shadow_triangles");
    require(triangles.is_array(), "Exported sky_shadow_triangles is not an array");
    return triangles;
}

Json& mutable_first_triangles(Json& world) {
    auto& sprites = world.at("lit_sprites");
    require(sprites.is_array() && !sprites.empty(), "Variant source has no lit sprites");
    return sprites.at(0)["sky_shadow_triangles"];
}

struct Variant {
    fs::path root;
    std::string world_sha256;
    std::string manifest_sha256;
};

Variant make_compact_fixture(const fs::path& exported, const fs::path& output,
                             const Json& exported_world) {
    require_derived_budget(output, "before compact parser fixture");
    const auto root = output / "compact-fixture";
    require(!fs::exists(root), "Compact parser fixture root already exists");
    require(fs::create_directories(root), "Could not create compact parser fixture root");

    Json manifest = Json::parse(read_text(exported / "manifest.json"));
    Json world = exported_world;
    require(world.at("textures").is_array() && world.at("textures").size() == 6u,
            "Compact parser fixture requires exactly six exported texture channels");
    const auto original_triangles = first_triangles(exported_world);
    for (auto& texture : world.at("textures")) {
        const auto format = texture.at("format").get<std::string>();
        std::vector<std::uint8_t> bytes;
        if (format == "rgba8_unorm_srgb") bytes = {255u, 255u, 255u, 255u};
        else if (format == "rgba8_unorm") bytes = {128u, 128u, 255u, 255u};
        else if (format == "r16_unorm") bytes = {0u, 0u};
        else if (format == "r8_unorm") bytes = {0u};
        else throw std::runtime_error("Compact fixture encountered an unsupported texture format");
        texture["width"] = 1u;
        texture["height"] = 1u;
        texture["bytes"] = bytes.size();
        texture["sha256"] = digest(bytes);
        const auto relative_text = texture.at("path").get<std::string>();
        require_derived_addition(output, bytes.size(), "compact texture write");
        write_bytes_new(root / from_utf8(relative_text), bytes);
        bool found{};
        for (auto& row : manifest.at("assets")) {
            if (row.at("path") != relative_text) continue;
            row["bytes"] = bytes.size();
            row["sha256"] = digest(bytes);
            found = true;
            break;
        }
        require(found, "Compact texture is absent from the exported manifest");
    }
    for (auto& material : world.at("materials"))
        for (auto& [name, channel] : material.at("channels").items()) {
            (void)name;
            channel["rect"] = {0, 0, 1, 1};
        }
    for (std::size_t index = 0; index < world.at("lit_sprites").size(); ++index) {
        auto& sprite = world.at("lit_sprites").at(index);
        const auto& original = exported_world.at("lit_sprites").at(index);
        sprite["source_size"] = {1.f, 1.f};
        sprite["pivot_px"] = {0.f, 0.f};
        sprite["depth_range"] = {0.f, 1.f};
        sprite["export_pixels_per_reference_pixel"] = 1.f;
        require(sprite.at("id") == original.at("id") &&
                    sprite.at("material") == original.at("material") &&
                    sprite.at("local_occluders") == original.at("local_occluders") &&
                    sprite.at("sky_shadow_triangles") == original_triangles,
                "Compact fixture changed sprite identity or authored geometry");
    }
    for (std::size_t index = 0; index < world.at("materials").size(); ++index)
        require(world.at("materials").at(index).at("id") ==
                    exported_world.at("materials").at(index).at("id") &&
                    world.at("materials").at(index).at("source_hash") ==
                    exported_world.at("materials").at(index).at("source_hash"),
                "Compact fixture changed material identity or source provenance");

    const auto world_text = world.dump(2) + "\n";
    require_derived_addition(output, world_text.size(), "compact world descriptor write");
    write_text_new(root / "art/world.json", world_text);
    bool found_world{};
    for (auto& row : manifest.at("assets")) {
        const auto relative_text = row.at("path").get<std::string>();
        if (relative_text == "art/world.json") {
            row["bytes"] = world_text.size();
            row["sha256"] = digest(world_text);
            found_world = true;
            continue;
        }
        const auto destination = root / from_utf8(relative_text);
        if (fs::exists(destination)) continue;
        const auto bytes = read_bytes(exported / from_utf8(relative_text));
        require_derived_addition(output, bytes.size(), "compact unchanged asset copy");
        write_bytes_new(destination, bytes);
        require(bytes.size() == row.at("bytes").get<std::size_t>() &&
                    digest(bytes) == row.at("sha256").get<std::string>(),
                "Unchanged compact-fixture asset disagrees with its manifest row");
    }
    require(found_world, "Exported manifest does not register art/world.json");
    manifest["content_sha256"] = manifest_content_hash(manifest.at("assets"));
    const auto manifest_text = manifest.dump(2) + "\n";
    require_derived_addition(output, manifest_text.size(), "compact manifest write");
    write_text_new(root / "manifest.json", manifest_text);
    require_derived_budget(output, "after compact parser fixture");
    return {root, digest(world_text), digest(manifest_text)};
}

Variant make_variant(const fs::path& base, const fs::path& output, std::string_view name,
                     const std::function<void(Json&)>& mutate) {
    require_derived_budget(output, "before compact derived parser fixture");
    const auto root = output / "derived-kits" / std::string(name);
    require(!fs::exists(root), "Derived kit root already exists");
    require(fs::create_directories(root), "Could not create derived kit root");

    Json manifest = Json::parse(read_text(base / "manifest.json"));
    Json world = Json::parse(read_text(base / "art/world.json"));
    mutate(world);
    const auto world_text = world.dump(2) + "\n";
    bool found_world{};
    for (auto& row : manifest.at("assets")) {
        const auto relative_text = row.at("path").get<std::string>();
        const auto relative = from_utf8(relative_text);
        const auto destination = root / relative;
        if (relative_text == "art/world.json") {
            require_derived_addition(output, world_text.size(), "derived world descriptor write");
            write_text_new(destination, world_text);
            row["bytes"] = world_text.size();
            row["sha256"] = digest(world_text);
            found_world = true;
            continue;
        }
        require(!fs::exists(destination), "Derived asset destination already exists");
        require_derived_addition(output, fs::file_size(base / relative),
                                 "derived compact asset link");
        fs::create_directories(destination.parent_path());
        std::error_code link_error;
        fs::create_hard_link(base / relative, destination, link_error);
        if (link_error) {
            std::error_code copy_error;
            const bool copied = fs::copy_file(base / relative, destination,
                                              fs::copy_options::none, copy_error);
            require(copied && !copy_error, "Could not preserve a derived kit asset");
        }
    }
    require(found_world, "Exported manifest does not register art/world.json");
    manifest["content_sha256"] = manifest_content_hash(manifest.at("assets"));
    const auto manifest_text = manifest.dump(2) + "\n";
    require_derived_addition(output, manifest_text.size(), "derived manifest write");
    write_text_new(root / "manifest.json", manifest_text);
    require_derived_budget(output, "after compact derived parser fixture");
    return {root, digest(world_text), digest(manifest_text)};
}

Json generated_triangle(std::size_t index, std::uint32_t mask) {
    const float offset = static_cast<float>(index % 128u) * .03125f;
    return {{"id", "generated." + std::to_string(index)},
            {"vertices", {{offset, 0.f, 0.f}, {offset + .25f, 0.f, 0.f},
                          {offset, .25f, 0.f}}},
            {"source_mask", mask}};
}

struct Reporter {
    fs::path output;
    Json checks = Json::array();
    Json variants = Json::array();
    std::size_t check_count{};
    std::size_t serial{};

    void pass(std::string_view name, Json evidence = Json::object()) {
        ++check_count;
        checks.push_back({{"name", name}, {"passed", true}, {"evidence", std::move(evidence)}});
    }

    void checkpoint(std::string_view phase) {
        write_json_new(output / ("authored-sky-progress-" + std::to_string(serial++) + ".json"),
                       {{"schema", 1}, {"complete", false}, {"phase", phase},
                        {"build", RIBTERRA_PROBE_BUILD}, {"check_count", check_count},
                        {"checks", checks}});
    }
};

bool same_rows(const std::vector<SkyShadowTriangle>& left,
               const std::vector<SkyShadowTriangle>& right) {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(left.data(), right.data(),
                                    left.size() * sizeof(SkyShadowTriangle)) == 0);
}

template<class Operation>
std::string rejected(Operation&& operation, std::string_view label) {
    try {
        operation();
    } catch (const std::exception& error) {
        return error.what();
    }
    throw std::runtime_error(std::string(label) + " was unexpectedly accepted");
}

template<class Operation>
std::string rejected_without_frame_mutation(Frame2D& frame, Operation&& operation,
                                             std::string_view label) {
    const auto before = frame.sky_shadow_triangles;
    const auto diagnostic = rejected(std::forward<Operation>(operation), label);
    require(same_rows(frame.sky_shadow_triangles, before),
            std::string(label) + " partially mutated the frame");
    return diagnostic;
}

SkyShadowTriangle valid_existing(std::uint32_t mask) {
    SkyShadowTriangle result;
    result.vertices[0].position = {0.f, 0.f, 0.f};
    result.vertices[1].position = {1.f, 0.f, 0.f};
    result.vertices[2].position = {0.f, 1.f, 0.f};
    result.source_mask = mask;
    result.reveal = 1.f;
    return result;
}

void settle_window(SDL_Window* window) {
    require(SDL_ShowWindow(window), SDL_GetError());
    for (int attempt = 0; attempt < 100; ++attempt) {
        SDL_PumpEvents();
        int width{}, height{};
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width > 0 && height > 0 &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) == 0u) return;
        SDL_Delay(10);
    }
    throw std::runtime_error("Authored sky bridge window did not become drawable");
}

Json device_json(const ribterra::render::DeviceInfo& device) {
    return {{"backend", device.backend}, {"device", device.device},
            {"driver", device.driver}, {"software", device.software}};
}

struct FixtureSet {
    Variant omitted;
    Variant control_id;
    Variant boundary_id;
    std::vector<std::pair<std::string, Variant>> invalid;
};

FixtureSet create_fixture_set(const Variant& compact, const Json& exported_world,
                              Reporter& report) {
    const auto compact_world = Json::parse(read_text(compact.root / "art/world.json"));
    require(first_triangles(compact_world) == first_triangles(exported_world),
            "Compact parser fixture does not preserve the original authored descriptor");
    FixtureSet result;
    result.omitted = make_variant(compact.root, report.output, "optional-field-omitted", [](Json& world) {
        for (auto& sprite : world.at("lit_sprites")) sprite.erase("sky_shadow_triangles");
    });
    result.control_id = make_variant(compact.root, report.output, "opaque-control-id", [](Json& world) {
        auto& triangles = mutable_first_triangles(world);
        require(!triangles.empty(), "Control-ID fixture requires authored triangles");
        triangles.at(0)["id"] = std::string("opaque\x01identity", 15);
    });
    result.boundary_id = make_variant(compact.root, report.output, "utf8-id-exactly-256-bytes", [](Json& world) {
        auto& triangles = mutable_first_triangles(world);
        require(!triangles.empty(), "UTF-8 boundary fixture requires authored triangles");
        std::string id;
        id.reserve(256);
        for (std::size_t index = 0; index < 128u; ++index) id += "\xC3\xA9";
        require(id.size() == 256u, "UTF-8 boundary fixture ID has the wrong byte length");
        triangles.at(0)["id"] = std::move(id);
    });

    using Mutation = std::pair<std::string, std::function<void(Json&)>>;
    std::vector<Mutation> mutations;
    mutations.emplace_back("field-not-array", [](Json& world) {
        world.at("lit_sprites").at(0)["sky_shadow_triangles"] = Json::object();
    });
    mutations.emplace_back("unknown-object-field", [](Json& world) {
        mutable_first_triangles(world).at(0)["unknown"] = 1;
    });
    mutations.emplace_back("missing-object-field", [](Json& world) {
        mutable_first_triangles(world).at(0).erase("id");
    });
    mutations.emplace_back("empty-id", [](Json& world) {
        mutable_first_triangles(world).at(0)["id"] = "";
    });
    mutations.emplace_back("id-over-256-bytes", [](Json& world) {
        mutable_first_triangles(world).at(0)["id"] = std::string(257, 'x');
    });
    mutations.emplace_back("duplicate-id", [](Json& world) {
        auto& triangles = mutable_first_triangles(world);
        require(triangles.size() >= 2, "Duplicate-ID fixture needs two triangles");
        triangles.at(1)["id"] = triangles.at(0).at("id");
    });
    mutations.emplace_back("wrong-triangle-cardinality", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].erase(2);
    });
    mutations.emplace_back("wrong-vertex-cardinality", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].at(0).erase(2);
    });
    mutations.emplace_back("coordinate-not-number", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].at(0).at(0) = nullptr;
    });
    mutations.emplace_back("coordinate-not-canonical-float32", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].at(0).at(0) = 0.1;
    });
    mutations.emplace_back("coordinate-double-out-of-range", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].at(0).at(0) =
            std::numeric_limits<double>::max();
    });
    mutations.emplace_back("coordinate-out-of-range", [](Json& world) {
        mutable_first_triangles(world).at(0)["vertices"].at(0).at(0) = 8192.5f;
    });
    mutations.emplace_back("degenerate-triangle", [](Json& world) {
        auto& vertices = mutable_first_triangles(world).at(0)["vertices"];
        vertices.at(1) = vertices.at(0);
        vertices.at(2) = vertices.at(0);
    });
    mutations.emplace_back("mask-is-float", [](Json& world) {
        mutable_first_triangles(world).at(0)["source_mask"] = 1.0;
    });
    mutations.emplace_back("mask-is-boolean", [](Json& world) {
        mutable_first_triangles(world).at(0)["source_mask"] = true;
    });
    mutations.emplace_back("mask-out-of-range", [](Json& world) {
        mutable_first_triangles(world).at(0)["source_mask"] = 4;
    });
    mutations.emplace_back("raw-capacity-overflow", [](Json& world) {
        Json rows = Json::array();
        for (std::size_t index = 0; index < 4097u; ++index)
            rows.push_back(generated_triangle(index, 3u));
        world.at("lit_sprites").at(0)["sky_shadow_triangles"] = std::move(rows);
    });
    mutations.emplace_back("sun-capacity-overflow", [](Json& world) {
        Json rows = Json::array();
        for (std::size_t index = 0; index < 1025u; ++index)
            rows.push_back(generated_triangle(index, 1u));
        world.at("lit_sprites").at(0)["sky_shadow_triangles"] = std::move(rows);
    });
    mutations.emplace_back("moon-capacity-overflow", [](Json& world) {
        Json rows = Json::array();
        for (std::size_t index = 0; index < 1025u; ++index)
            rows.push_back(generated_triangle(index, 2u));
        world.at("lit_sprites").at(0)["sky_shadow_triangles"] = std::move(rows);
    });

    for (const auto& [name, mutation] : mutations) {
        auto variant = make_variant(compact.root, report.output, "invalid-" + name, mutation);
        report.variants.push_back({{"name", name}, {"root", to_utf8(variant.root)},
                                   {"world_sha256", variant.world_sha256},
                                   {"manifest_sha256", variant.manifest_sha256}});
        result.invalid.emplace_back(name, std::move(variant));
    }
    report.pass("derived-fixtures-created-write-once",
                {{"scope", "compact 1x1 six-channel schema-2 parser fixtures"},
                 {"compact_root", to_utf8(compact.root)},
                 {"compact_world_sha256", compact.world_sha256},
                 {"compact_manifest_sha256", compact.manifest_sha256},
                 {"invalid_count", result.invalid.size()},
                 {"omitted_root", to_utf8(result.omitted.root)},
                 {"control_id_root", to_utf8(result.control_id.root)},
                 {"boundary_id_root", to_utf8(result.boundary_id.root)}});
    return result;
}

Json validate_exported_descriptor(const Json& world) {
    const auto& sprites = world.at("lit_sprites");
    const auto& canonical = first_triangles(world);
    require(canonical.size() == 6u, "Exported diagnostic descriptor must contain six triangles");
    bool has_sun{}, has_moon{}, has_shared{};
    for (const auto& row : canonical) {
        const auto mask = row.at("source_mask").get<std::uint32_t>();
        has_sun = has_sun || mask == 1u;
        has_moon = has_moon || mask == 2u;
        has_shared = has_shared || mask == 3u;
    }
    require(has_sun && has_moon && has_shared,
            "Exported diagnostic descriptor lacks sun, moon or shared source coverage");
    for (const auto& sprite : sprites)
        require(sprite.at("sky_shadow_triangles") == canonical,
                "Exported lit sprites disagree on canonical sky caster metadata");
    return {{"lit_sprite_count", sprites.size()}, {"triangle_count", canonical.size()},
            {"sun_only", has_sun}, {"moon_only", has_moon}, {"shared", has_shared},
            {"nlohmann_compact_json_sha256", digest(canonical.dump())}};
}

Json parser_transaction_contract(Renderer2D& renderer, GameArt& art,
                                 const AssetCatalog& exported_catalog,
                                 const Json& exported_world, const FixtureSet& fixtures,
                                 Reporter& report) {
    const auto sprite_id = exported_world.at("lit_sprites").at(0).at("id").get<std::string>();
    art.load(AssetCatalog(fixtures.omitted.root));
    require(art.contains_lit(sprite_id), "Omitted-field schema-2 sprite was not installed");
    Frame2D absent;
    require(art.append_lit_sky_casters(absent, sprite_id, {0.f, 0.f, 0.f}, 1.f,
                                       {0.f, 0.f, 0.f}) == 0u &&
                absent.sky_shadow_triangles.empty(),
            "Omitted sky_shadow_triangles did not install empty semantics");
    require(!renderer.has_submitted_frames(), "Schema parser baseline unexpectedly submitted a frame");
    report.pass("optional-field-omitted-is-empty", {{"sprite", sprite_id}});

    Json rejected_cases = Json::array();
    for (const auto& [name, variant] : fixtures.invalid) {
        const auto diagnostic = rejected([&] { art.load(AssetCatalog(variant.root)); }, name);
        require(diagnostic != "Injected sampled texture candidate creation failure",
                "Invalid descriptor reached sampled-texture upload");
        require(art.contains_lit(sprite_id), "Invalid descriptor replaced the old GameArt registry");
        Frame2D retained;
        require(art.append_lit_sky_casters(retained, sprite_id, {0.f, 0.f, 0.f}, 1.f,
                                           {0.f, 0.f, 0.f}) == 0u,
                "Invalid descriptor replaced optional-empty caster metadata");
        require(!renderer.has_submitted_frames(), "Invalid descriptor reached frame submission");
        rejected_cases.push_back({{"case", name}, {"diagnostic", diagnostic},
                                  {"old_registry_retained", true},
                                  {"frame_submitted", false}});
    }
    report.pass("strict-invalid-descriptors-reject-before-upload",
                {{"cases", rejected_cases.size()}, {"sentinel_pending", true}});

    const auto sentinel = rejected([&] { art.load(exported_catalog); }, "valid upload sentinel");
    require(sentinel == "Injected sampled texture candidate creation failure",
            "Invalid reloads consumed the next sampled-texture creation attempt");
    require(art.contains_lit(sprite_id), "Injected valid candidate replaced the old GameArt registry");
    require(!renderer.has_submitted_frames(), "Upload sentinel unexpectedly submitted a frame");
    report.pass("upload-attempt-sentinel-proves-invalids-preupload",
                {{"diagnostic", sentinel}, {"old_registry_retained", true}});

    art.load(AssetCatalog(fixtures.control_id.root));
    Frame2D control_id;
    require(art.append_lit_sky_casters(control_id, sprite_id, {0.f, 0.f, 0.f}, 1.f,
                                       {0.f, 0.f, 0.f}) == 6u,
            "Opaque control byte in non-display caster ID was incorrectly rejected");
    report.pass("opaque-valid-utf8-id-is-not-display-text",
                {{"triangle_count", control_id.sky_shadow_triangles.size()}});

    art.load(AssetCatalog(fixtures.boundary_id.root));
    Frame2D boundary_id;
    require(art.append_lit_sky_casters(boundary_id, sprite_id, {0.f, 0.f, 0.f}, 1.f,
                                       {0.f, 0.f, 0.f}) == 6u,
            "Exactly 256 UTF-8 ID bytes were incorrectly rejected");
    report.pass("utf8-id-256-byte-boundary-accepted",
                {{"triangle_count", boundary_id.sky_shadow_triangles.size()},
                 {"id_bytes", 256}});

    art.load(exported_catalog);
    return {{"optional_absent", true}, {"invalid_candidates", std::move(rejected_cases)},
            {"parser_fixture_scope", "compact 1x1 six-channel schema-2 catalogs"},
            {"adapter_catalog_scope", "original fresh exported catalog"},
            {"invalids_before_sampled_upload", true},
            {"old_registry_retained", true}, {"opaque_control_id_accepted", true},
            {"utf8_256_byte_id_accepted", true}};
}

Json adapter_contract(GameArt& art, const Json& exported_world, Reporter& report) {
    const auto& sprite = exported_world.at("lit_sprites").at(0);
    const auto sprite_id = sprite.at("id").get<std::string>();
    const auto& authored = sprite.at("sky_shadow_triangles");
    const std::array<float, 3> anchor{3.25f, -2.5f, 1.75f};
    const std::array<float, 3> camera{.5f, -1.f, .25f};
    constexpr float scale = 1.5f;

    Frame2D full;
    require(art.append_lit_sky_casters(full, sprite_id, anchor, scale, camera, 1.f) == 6u,
            "Valid authored sky descriptor did not append six triangles");
    require(full.sky_shadow_triangles.size() == authored.size(),
            "Authored adapter appended the wrong triangle count");
    for (std::size_t triangle = 0; triangle < authored.size(); ++triangle) {
        const auto& source = authored.at(triangle);
        const auto& actual = full.sky_shadow_triangles.at(triangle);
        require(actual.source_mask == source.at("source_mask").get<std::uint32_t>() &&
                    actual.reveal == 1.f,
                "Authored adapter changed source mask or reveal");
        for (std::size_t vertex = 0; vertex < actual.vertices.size(); ++vertex) {
            std::array<float, 3> world{};
            for (std::size_t axis = 0; axis < world.size(); ++axis) {
                const float local = source.at("vertices").at(vertex).at(axis).get<float>();
                world[axis] = anchor[axis] + local * scale;
            }
            const auto expected = ribterra::render::camera_relative_render_xyz(world, camera);
            require(actual.vertices[vertex].position == expected,
                    "Authored adapter transform disagrees with the canonical camera-relative mapping");
        }
    }
    report.pass("canonical-six-triangle-transform-exact",
                {{"sprite", sprite_id}, {"anchor", anchor}, {"scale", scale},
                 {"camera", camera}, {"triangle_count", full.sky_shadow_triangles.size()}});

    const std::array<float, 3> camera_delta{2.f, -3.f, .5f};
    const std::array<float, 3> moved_camera{camera[0] + camera_delta[0],
                                            camera[1] + camera_delta[1],
                                            camera[2] + camera_delta[2]};
    Frame2D camera_moved;
    require(art.append_lit_sky_casters(camera_moved, sprite_id, anchor, scale,
                                       moved_camera, 1.f) == 6u,
            "Camera-translation control did not append the authored descriptor");
    require(camera_moved.sky_shadow_triangles.size() == full.sky_shadow_triangles.size(),
            "Camera translation changed the authored triangle count");
    for (std::size_t triangle = 0; triangle < full.sky_shadow_triangles.size(); ++triangle) {
        const auto& source = authored.at(triangle);
        for (std::size_t vertex = 0;
             vertex < full.sky_shadow_triangles[triangle].vertices.size(); ++vertex) {
            std::array<float, 3> world{};
            for (std::size_t axis = 0; axis < world.size(); ++axis) {
                const float local = source.at("vertices").at(vertex).at(axis).get<float>();
                world[axis] = anchor[axis] + local * scale;
            }
            const auto expected = ribterra::render::camera_relative_render_xyz(world, moved_camera);
            require(camera_moved.sky_shadow_triangles[triangle].vertices[vertex].position == expected,
                    "Moved camera disagrees with the exact canonical mapping");
            for (std::size_t axis = 0; axis < camera_delta.size(); ++axis) {
                const float observed_delta =
                    camera_moved.sky_shadow_triangles[triangle].vertices[vertex].position[axis] -
                    full.sky_shadow_triangles[triangle].vertices[vertex].position[axis];
                require(std::abs(observed_delta + camera_delta[axis]) <= 1e-5f,
                        "Camera translation did not apply the negative vertex delta");
            }
        }
    }
    Frame2D repeated;
    art.append_lit_sky_casters(repeated, sprite_id, anchor, scale, camera, 1.f);
    require(same_rows(repeated.sky_shadow_triangles, full.sky_shadow_triangles),
            "Camera control mutated the stored authored descriptor");
    report.pass("camera-translation-is-explicit-and-registry-immutable",
                {{"camera_delta", camera_delta}, {"vertex_delta", {-camera_delta[0],
                                                                     -camera_delta[1],
                                                                     -camera_delta[2]}}});

    Frame2D half;
    require(art.append_lit_sky_casters(half, sprite_id, anchor, scale, camera, .5f) == 6u,
            "Positive partial reveal failed to append opaque-filter casters");
    require(std::all_of(half.sky_shadow_triangles.begin(), half.sky_shadow_triangles.end(),
                        [](const auto& triangle) { return triangle.reveal == .5f; }),
            "Runtime reveal was not preserved on every staged triangle");
    Frame2D hidden;
    hidden.sky_shadow_triangles.resize(ribterra::render::sky_shadow_triangle_capacity);
    require(art.append_lit_sky_casters(hidden, sprite_id, anchor, scale, camera, 0.f) == 0u &&
                hidden.sky_shadow_triangles.size() == ribterra::render::sky_shadow_triangle_capacity,
            "Reveal zero consumed or validated frame capacity");
    report.pass("reveal-filter-contract", {{"zero_appended", 0}, {"half_appended", 6},
                                            {"one_appended", 6}});

    Json failures = Json::array();
    auto expect_failure = [&](Frame2D& frame, auto&& operation, std::string_view name) {
        const auto diagnostic = rejected_without_frame_mutation(
            frame, std::forward<decltype(operation)>(operation), name);
        failures.push_back({{"case", name}, {"diagnostic", diagnostic},
                            {"frame_unchanged", true}});
    };

    Frame2D raw_full;
    raw_full.sky_shadow_triangles.assign(ribterra::render::sky_shadow_triangle_capacity,
                                         valid_existing(3u));
    expect_failure(raw_full, [&] {
        art.append_lit_sky_casters(raw_full, sprite_id, anchor, scale, camera);
    }, "existing plus staged raw capacity");

    Frame2D sun_full;
    sun_full.sky_shadow_triangles.assign(ribterra::render::sky_shadow_active_triangle_capacity,
                                         valid_existing(1u));
    expect_failure(sun_full, [&] {
        art.append_lit_sky_casters(sun_full, sprite_id, anchor, scale, camera);
    }, "existing plus staged sun capacity");
    Frame2D moon_full;
    moon_full.sky_shadow_triangles.assign(ribterra::render::sky_shadow_active_triangle_capacity,
                                          valid_existing(2u));
    expect_failure(moon_full, [&] {
        art.append_lit_sky_casters(moon_full, sprite_id, anchor, scale, camera);
    }, "existing plus staged moon capacity");

    Frame2D malformed;
    malformed.sky_shadow_triangles.push_back(valid_existing(0u));
    expect_failure(malformed, [&] {
        art.append_lit_sky_casters(malformed, sprite_id, anchor, scale, camera);
    }, "malformed existing source mask");
    Frame2D malformed_bounds;
    auto outside = valid_existing(3u);
    outside.vertices[0].position[0] = 32.01f;
    malformed_bounds.sky_shadow_triangles.push_back(outside);
    expect_failure(malformed_bounds, [&] {
        art.append_lit_sky_casters(malformed_bounds, sprite_id, anchor, scale, camera);
    }, "malformed existing bounds");
    Frame2D malformed_reveal;
    auto bad_reveal = valid_existing(3u);
    bad_reveal.reveal = std::numeric_limits<float>::quiet_NaN();
    malformed_reveal.sky_shadow_triangles.push_back(bad_reveal);
    expect_failure(malformed_reveal, [&] {
        art.append_lit_sky_casters(malformed_reveal, sprite_id, anchor, scale, camera);
    }, "malformed existing reveal");
    Frame2D malformed_degenerate;
    auto degenerate = valid_existing(3u);
    degenerate.vertices[2] = degenerate.vertices[0];
    malformed_degenerate.sky_shadow_triangles.push_back(degenerate);
    expect_failure(malformed_degenerate, [&] {
        art.append_lit_sky_casters(malformed_degenerate, sprite_id, anchor, scale, camera);
    }, "malformed existing degenerate triangle");

    Frame2D out_of_cube;
    expect_failure(out_of_cube, [&] {
        art.append_lit_sky_casters(out_of_cube, sprite_id, {100.f, 100.f, 100.f}, scale,
                                   {0.f, 0.f, 0.f});
    }, "transformed out of fixed camera cube");
    Frame2D nonfinite;
    expect_failure(nonfinite, [&] {
        const float maximum = std::numeric_limits<float>::max();
        art.append_lit_sky_casters(nonfinite, sprite_id, {maximum, maximum, maximum}, 1.f,
                                   {-maximum, -maximum, -maximum});
    }, "transformed nonfinite overflow");
    Frame2D collapsed;
    expect_failure(collapsed, [&] {
        art.append_lit_sky_casters(collapsed, sprite_id, {1.f, 1.f, 1.f},
                                   std::numeric_limits<float>::denorm_min(),
                                   {0.f, 0.f, 0.f});
    }, "transformed float collapse");
    Frame2D invalid_input;
    expect_failure(invalid_input, [&] {
        art.append_lit_sky_casters(invalid_input, sprite_id,
                                   {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f},
                                   1.f, {0.f, 0.f, 0.f});
    }, "nonfinite runtime anchor");
    expect_failure(invalid_input, [&] {
        art.append_lit_sky_casters(invalid_input, sprite_id, {0.f, 0.f, 0.f}, 0.f,
                                   {0.f, 0.f, 0.f});
    }, "nonpositive runtime scale");
    expect_failure(invalid_input, [&] {
        art.append_lit_sky_casters(invalid_input, sprite_id, {0.f, 0.f, 0.f}, 1.f,
                                   {0.f, 0.f, 0.f}, 1.01f);
    }, "runtime reveal above one");
    report.pass("adapter-failures-are-transactional", {{"cases", failures.size()}});

    Frame2D cleared;
    art.append_lit_sky_casters(cleared, sprite_id, anchor, scale, camera);
    cleared.clear_commands();
    require(cleared.sky_shadow_triangles.empty(),
            "Frame2D::clear_commands retained authored sky triangles");
    report.pass("frame-clear-removes-authored-casters");

    Json transformed = Json::array();
    for (const auto& triangle : full.sky_shadow_triangles) {
        Json vertices = Json::array();
        for (const auto& vertex : triangle.vertices) vertices.push_back(vertex.position);
        transformed.push_back({{"vertices", std::move(vertices)},
                               {"source_mask", triangle.source_mask},
                               {"reveal", triangle.reveal}});
    }
    return {{"sprite", sprite_id}, {"authored", authored}, {"world_anchor", anchor},
            {"geometry_scale", scale}, {"camera_world", camera},
            {"transformed", std::move(transformed)}, {"failure_matrix", std::move(failures)},
            {"clear_commands", true}};
}

int run(SDL_Window* window, const fs::path& runtime_assets, const fs::path& exported,
        const AssetCatalog& exported_catalog,
        const Json& exported_world, const FixtureSet& fixtures, Reporter& report,
    const fs::path& executable) {
    const auto texture_count = exported_world.at("textures").size();
    require(texture_count == 6u,
            "Original and compact catalogs must retain the exact six sampled channels");
    RendererOptions options;
    options.backend = Backend::Direct3D12;
    options.world_graph = WorldGraph::SkyShadowSlice;
    options.collect_telemetry = true;
    options.test_fail_sampled_texture_creation_at = static_cast<std::uint32_t>(texture_count + 1u);
    Renderer2D renderer(window, runtime_assets, options);
    require(!renderer.device_info().software && renderer.device_info().backend == "direct3d12",
            "Authored sky bridge requires native Direct3D 12");
    require(renderer.world_hdr_status().available && renderer.sky_shadow_status().available,
            "Authored sky bridge requires available HDR and SkyShadowSlice targets");
    report.pass("native-d3d12-construction", {{"device", device_json(renderer.device_info())},
                                               {"sampled_upload_sentinel",
                                                options.test_fail_sampled_texture_creation_at}});

    GameArt art(renderer);
    const auto parser = parser_transaction_contract(renderer, art, exported_catalog,
                                                    exported_world, fixtures, report);
    const auto adapter = adapter_contract(art, exported_world, report);
    require(!renderer.has_submitted_frames(),
            "Focused loader/adapter bridge unexpectedly submitted a GPU frame");

    std::string source_hash = "unavailable";
    const fs::path source_path = fs::path(__FILE__);
    if (fs::is_regular_file(source_path)) source_hash = file_digest(source_path);
    std::string executable_hash = "unavailable";
    if (fs::is_regular_file(executable)) executable_hash = file_digest(executable);
    const auto descriptor_hash = digest(first_triangles(exported_world).dump());
    const auto calibration_bytes = exported_catalog.read_path("art/material/calibration.json");
    const Json calibration = Json::parse(calibration_bytes.begin(), calibration_bytes.end());
    const auto exporter_descriptor_hash =
        calibration.at("sky_shadow_triangles_sha256").get<std::string>();
    const auto exporter_descriptor_basis = first_triangles(exported_world).dump() + "\n";
    const auto recomputed_exporter_descriptor_hash = digest(exporter_descriptor_basis);
    require(exporter_descriptor_hash.size() == 64u &&
                calibration.at("sky_shadow_triangle_count") == 6u &&
                recomputed_exporter_descriptor_hash == exporter_descriptor_hash,
            "Exporter calibration does not bind the six-triangle descriptor");
    const Json final_report{
        {"schema", 1}, {"complete", false}, {"build", RIBTERRA_PROBE_BUILD},
        {"scope", "focused authored schema-2 sky-caster loader and CPU frame adapter"},
        {"device", device_json(renderer.device_info())},
        {"source_sha256", source_hash}, {"executable_sha256", executable_hash},
        {"runtime_shader_manifest_sha256",
         file_digest(runtime_assets / "shaders/manifest.json")},
        {"exported_manifest_sha256", file_digest(exported / "manifest.json")},
        {"exported_world_sha256", file_digest(exported / "art/world.json")},
        {"exported_content_sha256", exported_catalog.content_hash()},
        {"authored_descriptor_nlohmann_compact_json_sha256", descriptor_hash},
        {"exporter_sky_shadow_triangles_sha256", exporter_descriptor_hash},
        {"exporter_sky_shadow_triangles_recomputed_sha256",
         recomputed_exporter_descriptor_hash},
        {"exporter_sky_shadow_triangles_hash_basis",
         "nlohmann ordered-object compact UTF-8 JSON plus one trailing newline"},
        {"descriptor", validate_exported_descriptor(exported_world)},
        {"parser_transaction", parser}, {"adapter", adapter},
        {"parser_fixture_scope", "compact 1x1 six-channel schema-2 derived catalogs"},
        {"adapter_catalog_scope", "original fresh exported schema-2 catalog"},
        {"derived_variants", report.variants},
        {"check_count", report.check_count}, {"checks", report.checks},
        {"resource_count_observability",
         "Renderer exposes no typed-texture/material live counters. The exact N+1 sentinel proves each invalid consumes zero sampled uploads, and the callable old registry proves publication atomicity; this does not prove partial-upload cleanup or live-resource counts."},
        {"native_authored_shadow", "NOT_RUN"},
        {"completion_reason",
         "Focused loader/adapter bridge passed; authored causal GPU shadow rendering is deliberately not run here."},
        {"claims", {"strict schema-2 authored-caster import", "pre-upload invalid-candidate rejection",
                    "registry-publication atomicity", "transactional camera-relative frame adaptation",
                    "native D3D12 resource construction"}},
        {"non_claims", {"authored causal shadow pixels", "native receiver or UI invariance",
                        "partial-upload resource cleanup or typed-resource live counts",
                        "full authored-sky milestone completion", "GameView or gameplay integration",
                        "final art", "performance, GPU timing or 120 FPS", "E01-E17 acceptance"}}};
    write_json_new(report.output / "authored-sky-bridge-report.json", final_report);
    std::cout << "Authored sky bridge: " << report.check_count
              << " focused checks; native authored shadow NOT_RUN; evidence preserved at "
              << to_utf8(report.output) << '\n';
    return 0;
}

} // namespace authored_sky_bridge_probe

int main(int argc, char** argv) {
    using namespace authored_sky_bridge_probe;
    fs::path output;
    std::unique_ptr<Reporter> report;
    try {
        require(argc == 4,
                "Usage: AuthoredSkyBridgeProbe <runtime-assets> <exported-kit> <NEW-output>");
        const auto runtime_assets = from_utf8(argv[1]);
        const auto exported = from_utf8(argv[2]);
        output = from_utf8(argv[3]);
        require(fs::is_directory(runtime_assets), "Runtime assets directory is missing");
        require(fs::is_directory(exported), "Exported authored kit directory is missing");
        require(!fs::exists(output), "Authored sky bridge refuses an existing output root");
        require(fs::create_directories(output), "Could not create authored sky bridge output root");
        report = std::make_unique<Reporter>();
        report->output = output;
        report->checkpoint("output-created-before-derived-fixtures");

        const AssetCatalog exported_catalog(exported);
        const Json exported_world = Json::parse(read_text(exported / "art/world.json"));
        const auto descriptor = validate_exported_descriptor(exported_world);
        report->pass("exported-six-triangle-descriptor", descriptor);
        const auto compact = make_compact_fixture(exported, output, exported_world);
        const auto fixtures = create_fixture_set(compact, exported_world, *report);
        report->checkpoint("derived-fixtures-complete-before-sdl");

        require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Ribterra Authored Sky Bridge Probe", 640, 480,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        settle_window(window.get());
        report->checkpoint("visible-window-ready-before-renderer");
        return run(window.get(), runtime_assets, exported, exported_catalog,
                   exported_world, fixtures, *report, from_utf8(argv[0]));
    } catch (const std::exception& error) {
        try {
            if (report) {
                write_json_new(output / ("authored-sky-bridge-failure-" +
                                          std::to_string(report->serial++) + ".json"),
                               {{"schema", 1}, {"complete", false},
                                {"build", RIBTERRA_PROBE_BUILD}, {"error", error.what()},
                                {"check_count", report->check_count}, {"checks", report->checks},
                                {"derived_variants", report->variants},
                                {"native_authored_shadow", "NOT_RUN"}});
            }
        } catch (...) {}
        std::cerr << "Authored sky bridge failed: " << error.what() << '\n';
        return 1;
    }
}

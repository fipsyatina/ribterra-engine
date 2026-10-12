#include "ribterra/foundation.hpp"
#include "ribterra/platform.hpp"
#include "ribterra/texture_atlas.hpp"

#include <nlohmann/json.hpp>
#include <png.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using json = nlohmann::json;
constexpr std::uint64_t maximum_config_bytes = 1024 * 1024;
constexpr std::uint64_t maximum_source_bytes = 512ull * 1024 * 1024;

std::string utf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
std::filesystem::path from_utf8(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
std::filesystem::path native_io_path(const std::filesystem::path &path) {
#if defined(_WIN32)
    auto absolute = path.is_absolute() ? path : std::filesystem::absolute(path);
    absolute = absolute.lexically_normal();
    const auto value = absolute.native();
    if (value.starts_with(L"\\\\?\\"))
        return absolute;
    if (value.starts_with(L"\\\\"))
        return std::filesystem::path(L"\\\\?\\UNC\\" + value.substr(2));
    return std::filesystem::path(L"\\\\?\\" + value);
#else
    return path;
#endif
}
std::filesystem::path confined(const std::filesystem::path &root, std::string_view name) {
    if (name.empty() || name.find(':') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos || name.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Atlas input path must be relative and portable: " +
                                    std::string(name));
    const auto relative = from_utf8(name).lexically_normal();
    if (relative.is_absolute() || relative.has_root_path())
        throw std::invalid_argument("Absolute atlas input paths are forbidden");
    for (const auto &part : relative)
        if (part == "..")
            throw std::invalid_argument("Atlas input path escapes the config directory");
    const auto resolved = std::filesystem::weakly_canonical(native_io_path(root / relative));
    const auto inside = resolved.lexically_relative(root);
    if (inside.empty() || inside.is_absolute())
        throw std::invalid_argument("Atlas input cannot be resolved inside the config directory");
    for (const auto &part : inside)
        if (part == "..")
            throw std::invalid_argument("Atlas input symlink escapes the config directory");
    return resolved;
}
std::vector<std::uint8_t> read_file(const std::filesystem::path &path, std::uint64_t limit) {
    const auto native_path = native_io_path(path);
    const auto size = std::filesystem::file_size(native_path);
    if (size > limit)
        throw std::length_error("Atlas input file exceeds its size limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(native_path, std::ios::binary);
    if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char *>(bytes.data()),
                                                   static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Could not read atlas input: " + utf8(path));
    return bytes;
}
json read_json(const std::filesystem::path &path) {
    const auto bytes = read_file(path, maximum_config_bytes);
    return json::parse(bytes.begin(), bytes.end());
}
std::uint32_t unsigned_option(const json &document, std::string_view name, std::uint32_t fallback) {
    const auto found = document.find(name);
    if (found == document.end())
        return fallback;
    if (!found->is_number_unsigned())
        throw std::invalid_argument("Atlas option must be an unsigned integer: " +
                                    std::string(name));
    const auto value = found->get<std::uint64_t>();
    if (value > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Atlas option exceeds uint32 range: " + std::string(name));
    return static_cast<std::uint32_t>(value);
}

ribterra::AtlasImage decode_png(const std::filesystem::path &path, std::string id,
                                ribterra::AtlasPivot pivot, std::uint32_t maximum_dimension) {
    const auto source = read_file(path, 64ull * 1024 * 1024);
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (source.empty() || !png_image_begin_read_from_memory(&image, source.data(), source.size()))
        throw std::runtime_error("Invalid PNG header: " + utf8(path));
    struct Cleanup {
        png_image *image;
        ~Cleanup() { png_image_free(image); }
    } cleanup{&image};
    if (!image.width || !image.height || image.width > maximum_dimension ||
        image.height > maximum_dimension)
        throw std::invalid_argument("PNG dimensions exceed atlas bounds: " + utf8(path));
    const auto bytes = static_cast<std::uint64_t>(image.width) * image.height * 4;
    if (bytes > maximum_source_bytes || bytes > std::numeric_limits<std::size_t>::max())
        throw std::length_error("Decoded PNG exceeds memory limit");
    ribterra::AtlasImage result{std::move(id), image.width, image.height,
                                std::vector<std::uint8_t>(static_cast<std::size_t>(bytes)), pivot};
    image.format = PNG_FORMAT_RGBA;
    if (!png_image_finish_read(&image, nullptr, result.rgba.data(), 0, nullptr))
        throw std::runtime_error("Could not decode PNG: " + utf8(path));
    return result;
}
std::vector<std::uint8_t> encode_png(const ribterra::AtlasPage &page) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = page.width;
    image.height = page.height;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size{};
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, page.rgba.data(), 0, nullptr))
        throw std::runtime_error("Could not size encoded atlas PNG");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!png_image_write_to_memory(&image, bytes.data(), &size, 0, page.rgba.data(), 0, nullptr))
        throw std::runtime_error("Could not encode atlas PNG");
    bytes.resize(static_cast<std::size_t>(size));
    return bytes;
}
void write_file(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    std::ofstream stream(native_io_path(path), std::ios::binary);
    if (!stream || (!bytes.empty() && !stream.write(reinterpret_cast<const char *>(bytes.data()),
                                                    static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Could not write atlas output: " + utf8(path));
}

struct Provenance {
    std::string path, license, source, author, notice_input, notice_output, notice_sha256;
};
class TemporaryDirectory {
  public:
    TemporaryDirectory(const std::filesystem::path &destination) {
        const auto parent = destination.parent_path();
        if (parent.empty() || !std::filesystem::is_directory(native_io_path(parent)))
            throw std::invalid_argument("Atlas output parent must already exist");
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 64; ++attempt) {
            auto name = destination.filename();
            name += from_utf8(".tmp-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            path_ = parent / name;
            std::error_code error;
            if (std::filesystem::create_directory(native_io_path(path_), error))
                return;
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "Could not create atlas temporary directory", path_, error);
        }
        throw std::runtime_error("Could not reserve atlas temporary directory");
    }
    ~TemporaryDirectory() {
        if (active_) {
            std::error_code ignored;
            std::filesystem::remove_all(native_io_path(path_), ignored);
        }
    }
    const std::filesystem::path &path() const { return path_; }
    void commit(const std::filesystem::path &destination) {
        std::filesystem::rename(native_io_path(path_), native_io_path(destination));
        active_ = false;
    }

  private:
    std::filesystem::path path_;
    bool active_{true};
};

void run(const std::filesystem::path &config_path, const std::filesystem::path &requested_output) {
    const auto config = std::filesystem::canonical(native_io_path(config_path));
    const auto root = config.parent_path();
    auto document = read_json(config);
    if (document.at("schema") != 1 || !document.at("images").is_array() ||
        document.at("images").empty() || document.at("images").size() > 100000)
        throw std::invalid_argument("Unsupported atlas config schema or image count");
    ribterra::AtlasOptions options;
    options.page_size = unsigned_option(document, "page_size", options.page_size);
    options.padding = unsigned_option(document, "padding", options.padding);
    options.max_pages = unsigned_option(document, "max_pages", options.max_pages);
    if (!options.page_size || (options.page_size & (options.page_size - 1)) != 0 ||
        options.page_size > 4096)
        throw std::invalid_argument("Atlas page size must be a power of two no greater than 4096");
    if (options.padding > 16 || options.padding * 2 >= options.page_size)
        throw std::invalid_argument("Atlas padding is outside supported bounds");
    if (!options.max_pages || options.max_pages > 256)
        throw std::invalid_argument("Atlas page count is outside supported bounds");
    std::vector<ribterra::AtlasImage> images;
    images.reserve(document.at("images").size());
    std::map<std::string, Provenance> provenance;
    std::map<std::string, std::vector<std::uint8_t>> notices;
    std::uint64_t decoded_bytes{};
    const std::regex license_pattern("[A-Za-z0-9][A-Za-z0-9.+-]*");
    for (const auto &row : document.at("images")) {
        const auto id = row.at("id").get<std::string>();
        const auto relative = row.at("path").get<std::string>();
        const auto license = row.at("license").get<std::string>();
        const auto source = row.at("source").get<std::string>();
        const auto author = row.at("author").get<std::string>();
        const auto notice = row.at("notice").get<std::string>();
        if (id.empty() || id.size() > 256 || source.empty() || author.empty() ||
            !std::regex_match(license, license_pattern))
            throw std::invalid_argument("Invalid atlas ID or provenance metadata");
        const auto source_path = confined(root, relative);
        if (source_path.extension() != ".png" ||
            !std::filesystem::is_regular_file(native_io_path(source_path)))
            throw std::invalid_argument("Atlas source must be an existing PNG");
        const auto notice_path = confined(root, notice);
        if (!std::filesystem::is_regular_file(native_io_path(notice_path)) ||
            std::filesystem::file_size(native_io_path(notice_path)) == 0 ||
            std::filesystem::file_size(native_io_path(notice_path)) > maximum_config_bytes)
            throw std::invalid_argument("Atlas source requires a non-empty bounded notice");
        const auto notice_bytes = read_file(notice_path, maximum_config_bytes);
        const auto notice_sha256 = ribterra::sha256(notice_bytes);
        const auto notice_output = "notices/" + notice_sha256 + ".txt";
        const auto [notice_found, notice_inserted] = notices.emplace(notice_sha256, notice_bytes);
        if (!notice_inserted && notice_found->second != notice_bytes)
            throw std::runtime_error("SHA-256 collision between atlas notices");
        ribterra::AtlasPivot pivot{};
        if (const auto found = row.find("pivot"); found != row.end()) {
            if (!found->is_array() || found->size() != 2)
                throw std::invalid_argument("Atlas pivot must contain two numbers");
            pivot = {found->at(0).get<float>(), found->at(1).get<float>()};
        }
        auto image = decode_png(source_path, id, pivot, options.page_size);
        decoded_bytes += image.rgba.size();
        if (decoded_bytes > maximum_source_bytes)
            throw std::length_error("Decoded atlas inputs exceed memory limit");
        images.push_back(std::move(image));
        if (!provenance
                 .emplace(id, Provenance{relative, license, source, author, notice, notice_output,
                                         notice_sha256})
                 .second)
            throw std::invalid_argument("Duplicate atlas image ID");
    }
    auto atlas = ribterra::pack_texture_atlas(images, options);
    const auto output = std::filesystem::absolute(requested_output).lexically_normal();
    if (std::filesystem::exists(native_io_path(output)))
        throw std::invalid_argument("Atlas output destination already exists");
    TemporaryDirectory temporary(output);
    if (!std::filesystem::create_directory(native_io_path(temporary.path() / "notices")))
        throw std::runtime_error("Could not create atlas notice directory");
    for (const auto &[sha256, bytes] : notices)
        write_file(temporary.path() / "notices" / (sha256 + ".txt"), bytes);
    json manifest = {{"schema", 1},
                     {"page_size", options.page_size},
                     {"padding", atlas.padding},
                     {"pages", json::array()},
                     {"entries", json::array()}};
    for (std::size_t index = 0; index < atlas.pages.size(); ++index) {
        const auto filename = "atlas-" + std::to_string(index) + ".png";
        const auto path = temporary.path() / filename;
        const auto bytes = encode_png(atlas.pages[index]);
        write_file(path, bytes);
        manifest["pages"].push_back({{"path", filename},
                                     {"width", atlas.pages[index].width},
                                     {"height", atlas.pages[index].height},
                                     {"sha256", ribterra::sha256(bytes)}});
    }
    for (const auto &entry : atlas.entries) {
        const auto &meta = provenance.at(entry.id);
        manifest["entries"].push_back(
            {{"id", entry.id},
             {"page", entry.page},
             {"rect", {entry.rect.x, entry.rect.y, entry.rect.width, entry.rect.height}},
             {"uv", {entry.uv.u0, entry.uv.v0, entry.uv.u1, entry.uv.v1}},
             {"pivot", {entry.pivot.x, entry.pivot.y}},
             {"license", meta.license},
             {"source", meta.source},
             {"author", meta.author},
             {"notice", meta.notice_output},
             {"notice_input", meta.notice_input},
             {"notice_sha256", meta.notice_sha256},
             {"input", meta.path}});
    }
    const auto canonical = manifest.dump();
    manifest["content_sha256"] = ribterra::sha256(
        std::span(reinterpret_cast<const std::uint8_t *>(canonical.data()), canonical.size()));
    std::ofstream output_stream(native_io_path(temporary.path() / "manifest.json"),
                                std::ios::binary);
    const auto rendered = manifest.dump(2) + "\n";
    if (!output_stream ||
        !output_stream.write(rendered.data(), static_cast<std::streamsize>(rendered.size())))
        throw std::runtime_error("Could not write atlas manifest");
    output_stream.close();
    if (!output_stream)
        throw std::runtime_error("Could not finalize atlas manifest");
    temporary.commit(output);
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto arguments = ribterra::utf8_arguments(argc, argv);
        if (arguments.size() != 3) {
            std::cerr << "Usage: AtlasTool <input.json> <output-directory>\n";
            return 2;
        }
        run(from_utf8(arguments[1]), from_utf8(arguments[2]));
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

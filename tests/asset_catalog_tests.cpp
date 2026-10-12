#include "ribterra/asset_catalog.hpp"
#include "ribterra/foundation.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using ribterra::AssetCatalog;
int failures{};
#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #value "\n";            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

std::string digest(std::string_view value) {
    return ribterra::sha256(
        std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
}
void write(const std::filesystem::path &path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(value.data(), static_cast<std::streamsize>(value.size())))
        throw std::runtime_error("fixture write failed");
}
struct Row {
    std::string id, path, hash;
};
void manifest(const std::filesystem::path &root, const std::vector<Row> &rows,
              std::string content_hash = {}) {
    std::string canonical;
    for (const auto &row : rows) {
        for (const auto &field :
             std::vector<std::string>{row.id, row.path, "data", row.hash, "LicenseRef-Test",
                                      "fixture", "Ribterra tests", "NOTICE.txt"})
            canonical += std::to_string(field.size()) + ":" + field;
        canonical += '\n';
    }
    if (content_hash.empty())
        content_hash = digest(canonical);
    std::string json = "{\"schema\":1,\"content_sha256\":\"" + content_hash + "\",\"assets\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i)
            json += ",";
        const auto &row = rows[i];
        json += "{\"id\":\"" + row.id + "\",\"path\":\"" + row.path +
                "\",\"kind\":\"data\",\"sha256\":\"" + row.hash +
                "\",\"license\":\"LicenseRef-Test\",\"source\":\"fixture\",\"author\":\"Ribterra "
                "tests\",\"notice\":\"NOTICE.txt\",\"bytes\":" +
                std::to_string(std::filesystem::exists(root / row.path)
                                   ? std::filesystem::file_size(root / row.path)
                                   : 0) +
                "}";
    }
    write(root / "manifest.json", json + "]}");
}
template <class Function> bool rejects(Function &&function) {
    try {
        function();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}

struct Workspace {
    std::filesystem::path path;
    Workspace() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("ribterra-asset-catalog-test-" + std::to_string(stamp));
        std::filesystem::create_directory(path);
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void valid_and_corruption() {
    Workspace workspace;
    const auto root = workspace.path / "assets";
    const std::string body = "{\"value\":42}\n";
    write(root / "data/item.json", body);
    manifest(root, {{"data:item", "data/item.json", digest(body)}});
    AssetCatalog catalog(root);
    CHECK(catalog.records().size() == 1);
    CHECK(catalog.find("data:item").path == "data/item.json");
    CHECK(catalog.content_hash().size() == 64);
    const auto bytes = catalog.read("data:item");
    CHECK(std::string(bytes.begin(), bytes.end()) == body);
    CHECK(catalog.read_path("data/item.json") == bytes);
    CHECK(rejects([&] { (void)catalog.find("missing"); }));
    CHECK(rejects([&] { (void)catalog.resolve("manifest.json"); }));
    CHECK(rejects([&] { (void)catalog.resolve("../outside.json"); }));
    write(root / "data/item.json", "corrupted");
    CHECK(rejects([&] { (void)catalog.read("data:item"); }));
    CHECK(rejects([&] { catalog.verify(); }));
}

void malformed_manifests() {
    Workspace workspace;
    const auto root = workspace.path / "assets";
    const std::string body = "{}";
    const auto hash = digest(body);
    write(root / "one.json", body);
    write(root / "two.json", body);
    manifest(root, {{"duplicate", "one.json", hash}, {"duplicate", "two.json", hash}});
    CHECK(rejects([&] { AssetCatalog catalog(root); }));
    manifest(root, {{"one", "one.json", hash}}, std::string(64, '0'));
    CHECK(rejects([&] { AssetCatalog catalog(root); }));
    manifest(root, {{"escape", "../two.json", hash}});
    CHECK(rejects([&] { AssetCatalog catalog(root); }));
    manifest(root, {{"wrong-hash", "one.json", std::string(64, '0')}});
    CHECK(rejects([&] { AssetCatalog catalog(root); }));
}

void escaping_symlink() {
    Workspace workspace;
    const auto root = workspace.path / "assets";
    const auto outside = workspace.path / "outside.json";
    const std::string body = "{}";
    write(outside, body);
    std::filesystem::create_directories(root / "data");
    std::error_code error;
    std::filesystem::create_symlink(outside, root / "data/link.json", error);
    if (error) {
        std::cout << "Symlink confinement test skipped: " << error.message() << '\n';
        return;
    }
    manifest(root, {{"escape", "data/link.json", digest(body)}});
    CHECK(rejects([&] { AssetCatalog catalog(root); }));
}
} // namespace

int main() {
    valid_and_corruption();
    malformed_manifests();
    escaping_symlink();
    if (failures) {
        std::cerr << failures << " asset catalog test(s) failed.\n";
        return 1;
    }
    std::cout << "Asset catalog hashing, confinement and registered-ID reads passed.\n";
    return 0;
}

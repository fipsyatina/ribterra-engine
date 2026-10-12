#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ribterra {

enum class AssetCatalogReadProfile { Default, MaterialCollection, MaterialCollectionTextured };

struct AssetRecord {
    std::string id, path, kind, sha256, license, source, author, notice;
    std::uint64_t bytes{};
};

// Validated manifest of packaged files. A registered path is confined to the
// asset root, including after symlink resolution. No raw external file access.
class AssetCatalog {
  public:
    explicit AssetCatalog(const std::filesystem::path &root);
    AssetCatalog(const std::filesystem::path &root, AssetCatalogReadProfile profile);
    AssetCatalogReadProfile read_profile() const noexcept;
    const AssetRecord &find(std::string_view id) const;
    std::filesystem::path resolve(std::string_view relative_path) const;
    std::vector<std::uint8_t> read(std::string_view id) const;
    std::vector<std::uint8_t> read_path(std::string_view relative_path) const;
    const std::vector<AssetRecord> &records() const;
    const std::string &content_hash() const;
    void verify() const;

  private:
    AssetCatalogReadProfile read_profile_{AssetCatalogReadProfile::Default};
    // Explicit material profiles retain the raw absolute input route so each
    // subsequent read can reject aliases before canonicalization erases them.
    std::filesystem::path read_root_;
    std::filesystem::path root_;
    std::vector<AssetRecord> assets_;
    std::string content_hash_;
    // The explicit textured profile plans every bounded read before verify().
    // Auxiliary notices/manifest are retained separately from registered assets.
    struct TexturedReadRole {
        std::string path, sha256;
        std::uint64_t bytes{}, limit{};
    };
    std::vector<TexturedReadRole> textured_roles_;
};

} // namespace ribterra

#include "ribterra/texture_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace ribterra {
namespace {
constexpr std::uint32_t maximum_page_size = 4096;
constexpr std::uint32_t maximum_padding = 16;
constexpr std::uint32_t maximum_pages = 256;
constexpr std::uint64_t maximum_atlas_bytes = 512ull * 1024 * 1024;

bool power_of_two(std::uint32_t value) { return value && !(value & (value - 1)); }
std::uint64_t image_bytes(std::uint32_t width, std::uint32_t height) {
    return static_cast<std::uint64_t>(width) * height * 4;
}

struct ShelfPage {
    std::uint32_t x{}, y{}, shelf_height{};
};
struct Placement {
    std::uint32_t page{}, outer_x{}, outer_y{};
};

bool place(ShelfPage &page, std::uint32_t size, std::uint32_t width, std::uint32_t height,
           Placement &placement, std::uint32_t index) {
    if (page.x + width <= size && page.y + height <= size) {
        placement = {index, page.x, page.y};
        page.x += width;
        page.shelf_height = std::max(page.shelf_height, height);
        return true;
    }
    const auto next_y = page.y + page.shelf_height;
    if (next_y + height > size)
        return false;
    page.x = width;
    page.y = next_y;
    page.shelf_height = height;
    placement = {index, 0, next_y};
    return true;
}

void copy_extruded(AtlasPage &page, const AtlasImage &image, const Placement &placement,
                   std::uint32_t padding) {
    const auto outer_width = image.width + padding * 2;
    const auto outer_height = image.height + padding * 2;
    for (std::uint32_t y = 0; y < outer_height; ++y) {
        const auto source_y =
            std::clamp<std::int64_t>(static_cast<std::int64_t>(y) - padding, 0, image.height - 1);
        for (std::uint32_t x = 0; x < outer_width; ++x) {
            const auto source_x = std::clamp<std::int64_t>(static_cast<std::int64_t>(x) - padding,
                                                           0, image.width - 1);
            const auto source = (static_cast<std::uint64_t>(source_y) * image.width + source_x) * 4;
            const auto destination =
                (static_cast<std::uint64_t>(placement.outer_y + y) * page.width +
                 placement.outer_x + x) *
                4;
            std::copy_n(image.rgba.data() + source, 4, page.rgba.data() + destination);
        }
    }
}
} // namespace

TextureAtlas pack_texture_atlas(std::span<const AtlasImage> images, AtlasOptions options) {
    if (!power_of_two(options.page_size) || options.page_size > maximum_page_size)
        throw std::invalid_argument("Atlas page size must be a power of two no greater than 4096");
    if (options.padding > maximum_padding || options.padding * 2 >= options.page_size)
        throw std::invalid_argument("Atlas padding is outside supported bounds");
    if (!options.max_pages || options.max_pages > maximum_pages)
        throw std::invalid_argument("Atlas page count is outside supported bounds");
    const auto page_bytes = image_bytes(options.page_size, options.page_size);
    if (page_bytes > maximum_atlas_bytes)
        throw std::length_error("A single atlas page exceeds the memory limit");

    std::unordered_set<std::string> ids;
    std::vector<const AtlasImage *> ordered;
    ordered.reserve(images.size());
    for (const auto &image : images) {
        if (image.id.empty() || image.id.size() > 256 || !ids.insert(image.id).second)
            throw std::invalid_argument(
                "Atlas image IDs must be unique and between 1 and 256 bytes");
        if (!image.width || !image.height)
            throw std::invalid_argument("Atlas image dimensions must be non-zero");
        if (image.width > options.page_size - options.padding * 2 ||
            image.height > options.page_size - options.padding * 2)
            throw std::invalid_argument("Atlas image does not fit a page without rotation");
        const auto bytes = image_bytes(image.width, image.height);
        if (bytes > std::numeric_limits<std::size_t>::max() ||
            image.rgba.size() != static_cast<std::size_t>(bytes))
            throw std::invalid_argument("Atlas image is not tightly packed RGBA8");
        if (!std::isfinite(image.pivot.x) || !std::isfinite(image.pivot.y) || image.pivot.x < 0 ||
            image.pivot.x > 1 || image.pivot.y < 0 || image.pivot.y > 1)
            throw std::invalid_argument("Atlas pivot must be finite normalized coordinates");
        ordered.push_back(&image);
    }
    std::sort(ordered.begin(), ordered.end(), [&](const auto *left, const auto *right) {
        const auto lh = left->height + options.padding * 2,
                   rh = right->height + options.padding * 2;
        if (lh != rh)
            return lh > rh;
        const auto lw = left->width + options.padding * 2, rw = right->width + options.padding * 2;
        if (lw != rw)
            return lw > rw;
        return left->id < right->id;
    });

    TextureAtlas atlas;
    atlas.padding = options.padding;
    std::vector<ShelfPage> shelves;
    for (const auto *image : ordered) {
        const auto outer_width = image->width + options.padding * 2;
        const auto outer_height = image->height + options.padding * 2;
        Placement placement{};
        bool packed = false;
        for (std::uint32_t page = 0; page < shelves.size(); ++page)
            if (place(shelves[page], options.page_size, outer_width, outer_height, placement,
                      page)) {
                packed = true;
                break;
            }
        if (!packed) {
            if (shelves.size() >= options.max_pages ||
                page_bytes * (shelves.size() + 1) > maximum_atlas_bytes)
                throw std::length_error("Atlas images exceed the page or memory limit");
            shelves.push_back({});
            atlas.pages.push_back(
                {options.page_size, options.page_size,
                 std::vector<std::uint8_t>(static_cast<std::size_t>(page_bytes))});
            if (!place(shelves.back(), options.page_size, outer_width, outer_height, placement,
                       static_cast<std::uint32_t>(shelves.size() - 1)))
                throw std::logic_error("Validated atlas image could not be placed");
        }
        copy_extruded(atlas.pages[placement.page], *image, placement, options.padding);
        const AtlasRect rect{placement.outer_x + options.padding,
                             placement.outer_y + options.padding, image->width, image->height};
        const auto scale = 1.f / options.page_size;
        atlas.entries.push_back({image->id,
                                 placement.page,
                                 rect,
                                 {rect.x * scale, rect.y * scale, (rect.x + rect.width) * scale,
                                  (rect.y + rect.height) * scale},
                                 image->pivot});
    }
    std::sort(atlas.entries.begin(), atlas.entries.end(),
              [](const auto &left, const auto &right) { return left.id < right.id; });
    return atlas;
}

} // namespace ribterra

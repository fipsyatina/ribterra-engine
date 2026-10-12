import hashlib
import json
import struct
import sys
import zlib
from functools import lru_cache
from pathlib import Path


root = Path(sys.argv[1]).resolve()
passport = json.loads((root / "docs/visual-targets/v03-asset-passport.json").read_text(encoding="utf-8"))
world = json.loads((root / "assets/art/world.json").read_text(encoding="utf-8"))
sources = json.loads((root / "content/asset_sources.json").read_text(encoding="utf-8"))["overrides"]

sprites = {}
page_modes = {}
for page in world["pages"]:
    assert page.get("alpha_mode") in (None, "magenta", "luminance"), page["path"]
    page_modes[page["path"]] = page.get("alpha_mode", "rgba")
    for sprite in page["sprites"]:
        assert sprite["id"] not in sprites, sprite["id"]
        sprites[sprite["id"]] = (page["path"], sprite)


def verify_file(record, dimensions):
    data = (root / "assets" / record["path"]).read_bytes()
    assert hashlib.sha256(data).hexdigest() == record["sha256"], record["path"]
    assert data[:8] == b"\x89PNG\r\n\x1a\n", record["path"]
    assert list(struct.unpack(">II", data[16:24])) == dimensions, record["path"]
    source = sources[record["path"]]
    assert source["license"] == passport["license"]
    assert source["notice"] == passport["notice"]
    if "source_id" in record:
        assert source["id"] == record["source_id"]
    if "prompt_file" in source:
        assert (root / source["prompt_file"]).read_text(encoding="utf-8").strip(), source["prompt_file"]
        if "prompt_sha256" in source:
            assert hashlib.sha256((root / source["prompt_file"]).read_bytes()).hexdigest() == \
                source["prompt_sha256"], source["prompt_file"]
    if "generated_source" in source:
        generated = root / source["generated_source"]
        assert hashlib.sha256(generated.read_bytes()).hexdigest() == source["generated_source_sha256"], generated
        assert generated.read_bytes() == data, generated
    for source_image in source.get("source_images", []):
        source_path = root / ("assets" if source_image["path"].startswith("art/") else "") / source_image["path"]
        assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_image["sha256"], source_image["path"]


@lru_cache(maxsize=len(page_modes))
def rgba_alpha_rows(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    offset = 8
    compressed = []
    width = height = None
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            assert (depth, color, compression, filtering, interlace) == (8, 6, 0, 0, 0), path
        elif kind == b"IDAT":
            compressed.append(payload)
        elif kind == b"IEND":
            break
    stride = width * 4
    raw = zlib.decompress(b"".join(compressed))
    assert len(raw) == height * (stride + 1), path
    previous = bytearray(stride)
    alpha_rows = []
    transparent_colored_pixels = 0

    def paeth(a, b, c):
        estimate = a + b - c
        distances = (abs(estimate - a), abs(estimate - b), abs(estimate - c))
        return (a, b, c)[distances.index(min(distances))]

    cursor = 0
    for y in range(height):
        filter_type = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor:cursor + stride])
        cursor += stride
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                row[index] = (row[index] + left) & 255
            elif filter_type == 2:
                row[index] = (row[index] + above) & 255
            elif filter_type == 3:
                row[index] = (row[index] + ((left + above) // 2)) & 255
            elif filter_type == 4:
                row[index] = (row[index] + paeth(left, above, upper_left)) & 255
            else:
                assert filter_type == 0, path
        alpha_rows.append(bytes(row[3::4]))
        transparent_colored_pixels += sum(
            row[index + 3] == 0 and any(row[index:index + 3]) for index in range(0, stride, 4))
        previous = row
    return width, height, tuple(alpha_rows), transparent_colored_pixels


def rgba_alpha_bounds(path, threshold=32, region=None):
    width, height, alpha_rows, _ = rgba_alpha_rows(path)
    minimum_x, minimum_y, maximum_x, maximum_y = width, height, -1, -1
    for y in range(height):
        region_x, region_y, region_width, region_height = region or (0, 0, width, height)
        if y < region_y or y >= region_y + region_height:
            continue
        for x in range(region_x, region_x + region_width):
            if alpha_rows[y][x] >= threshold:
                minimum_x, minimum_y = min(minimum_x, x), min(minimum_y, y)
                maximum_x, maximum_y = max(maximum_x, x), max(maximum_y, y)
    assert maximum_x >= minimum_x and maximum_y >= minimum_y, path
    return [minimum_x, minimum_y, maximum_x - minimum_x + 1, maximum_y - minimum_y + 1]


def rgba_alpha_components(path, threshold, region):
    _, _, alpha_rows, _ = rgba_alpha_rows(path)
    region_x, region_y, region_width, region_height = region
    visited = bytearray(region_width * region_height)
    components = []
    for local_y in range(region_height):
        for local_x in range(region_width):
            start = local_y * region_width + local_x
            if visited[start] or alpha_rows[region_y + local_y][region_x + local_x] < threshold:
                continue
            visited[start] = 1
            pending = [start]
            count = 0
            minimum_x = maximum_x = local_x
            minimum_y = maximum_y = local_y
            while pending:
                index = pending.pop()
                y, x = divmod(index, region_width)
                count += 1
                minimum_x, maximum_x = min(minimum_x, x), max(maximum_x, x)
                minimum_y, maximum_y = min(minimum_y, y), max(maximum_y, y)
                for neighbor_x, neighbor_y in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if not (0 <= neighbor_x < region_width and 0 <= neighbor_y < region_height):
                        continue
                    neighbor = neighbor_y * region_width + neighbor_x
                    if visited[neighbor] or \
                            alpha_rows[region_y + neighbor_y][region_x + neighbor_x] < threshold:
                        continue
                    visited[neighbor] = 1
                    pending.append(neighbor)
            components.append((count, [region_x + minimum_x, region_y + minimum_y,
                                       maximum_x - minimum_x + 1, maximum_y - minimum_y + 1]))
    return sorted(components, reverse=True)


def rgba_alpha_region_metrics(path, region):
    _, _, alpha_rows, _ = rgba_alpha_rows(path)
    x, y, width, height = region
    values = [alpha_rows[row][column]
              for row in range(y, y + height) for column in range(x, x + width)]
    edge_alpha = [
        max(alpha_rows[row][x] for row in range(y, y + height)),
        max(alpha_rows[row][x + width - 1] for row in range(y, y + height)),
        max(alpha_rows[y][column] for column in range(x, x + width)),
        max(alpha_rows[y + height - 1][column] for column in range(x, x + width)),
    ]
    return sum(0 < value < 32 for value in values), edge_alpha


families = passport["building_families"]
assert len(families) == 12
assert sum(f["availability"] == "buildable" for f in families) == 11
assert passport["art_replacement_status"] == "partial"
assert passport["concept_acceptance"] == "unverified"
assert "All runtime building states" in passport["partial_reason"]
assert "concept acceptance" in passport["partial_reason"]
services_source = sources["art/concept-services.png"]
assert hashlib.sha256(services_source["prompt"].encode("utf-8")).hexdigest() == \
    "87d2406bd6176df4efb4110ace56ebb7c48a06eb6a15338ed16d2040f3ff993f"
winter_services_source = sources["art/concept-services-winter.png"]
assert hashlib.sha256(winter_services_source["prompt"].encode("utf-8")).hexdigest() == \
    "f6a5dac89f43b2d1ffb4ab08f798c9a456a682d9bc5e97587aef0c606feb23e3"
for source_image in winter_services_source["source_images"]:
    source_path = root / ("assets" if source_image["path"].startswith("art/") else "") / source_image["path"]
    assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_image["sha256"]

overrides = {}
alpha_bounds_cache = {}
for record in passport["runtime_state_overrides"]:
    key = (record["slug"], record["state"])
    assert key not in overrides, key
    overrides[key] = record
    verify_file(record, record["dimensions"])
    assert record["alpha_mode"] == "rgba"
    assert page_modes[record["path"]] == record["alpha_mode"]
    asset_id = f"building.{record['slug']}.{record['state']}"
    assert asset_id in sprites, asset_id
    path, sprite = sprites[asset_id]
    assert path == record["path"], asset_id
    assert sprite["rect"] == record["rect"], asset_id
    assert sprite["pivot"] == record["pivot"], asset_id
    x, y, width, height = record["rect"]
    assert x >= 0 and y >= 0 and width > 0 and height > 0, asset_id
    assert x + width <= record["dimensions"][0], asset_id
    assert y + height <= record["dimensions"][1], asset_id
    assert all(0 <= value <= 1 for value in record["pivot"]), asset_id
    if "alpha32_bounds" in record:
        padding = record["padding"]
        alpha_x, alpha_y, alpha_width, alpha_height = record["alpha32_bounds"]
        assert record["rect"] == [alpha_x - padding, alpha_y - padding,
                                  alpha_width + padding * 2, alpha_height + padding * 2], asset_id
        cache_key = (record["path"], tuple(record["quadrant"]))
        if cache_key not in alpha_bounds_cache:
            alpha_bounds_cache[cache_key] = rgba_alpha_bounds(root / "assets" / record["path"],
                                                               region=record["quadrant"])
        assert alpha_bounds_cache[cache_key] == record["alpha32_bounds"], asset_id
    if "alpha32_main_component_bounds" in record:
        region_x, region_y, region_width, region_height = record["region"]
        alpha_x, alpha_y, alpha_width, alpha_height = record["alpha32_main_component_bounds"]
        assert region_x <= alpha_x and region_y <= alpha_y, asset_id
        assert alpha_x + alpha_width <= region_x + region_width, asset_id
        assert alpha_y + alpha_height <= region_y + region_height, asset_id
        assert x <= alpha_x and y <= alpha_y, asset_id
        assert alpha_x + alpha_width <= x + width, asset_id
        assert alpha_y + alpha_height <= y + height, asset_id

expected_override_states = {
    "camp": {"stage1", "stage2", "stage3", "ready", "winter"},
    "house": {"stage1", "stage2", "stage3", "ready", "winter", "level2", "level2-winter"},
    "farm": {"stage1", "stage2", "stage3", "ready", "winter"},
    "stockpile": {"stage1", "stage2", "stage3", "ready", "winter"},
    "mine": {"stage1", "stage2", "stage3", "ready", "winter"},
    "workshop": {"stage1", "stage2", "stage3", "ready", "winter", "level2", "level2-winter"},
    "lumbermill": {"stage1", "stage2", "stage3", "ready", "winter"},
    "kitchen": {"stage1", "stage2", "stage3", "ready", "winter"},
    "mill": {"stage1", "stage2", "stage3", "ready", "winter"},
    "fishing": {"stage1", "stage2", "stage3", "ready", "winter"},
    "bridge": {"stage1", "stage2", "stage3", "ready", "winter"},
    "claypit": {"stage1", "stage2", "stage3", "ready", "winter"},
}
assert len(overrides) == 64
assert set(slug for slug, _ in overrides) == set(expected_override_states)
for slug, states in expected_override_states.items():
    assert {state for family, state in overrides if family == slug} == states

for family in families:
    verify_file(family, family["dimensions"])
    assert page_modes[family["path"]] == passport["alpha_mode"]
    for state in passport["building_states"]:
        asset_id = f"building.{family['slug']}.{state}"
        assert asset_id in sprites, asset_id
        path, sprite = sprites[asset_id]
        runtime = overrides.get((family["slug"], state))
        assert path == (runtime["path"] if runtime else family["path"])
        assert len(sprite["rect"]) == 4 and len(sprite["pivot"]) == 2
        dimensions = runtime["dimensions"] if runtime else family["dimensions"]
        x, y, width, height = sprite["rect"]
        assert x >= 0 and y >= 0 and width > 0 and height > 0, asset_id
        assert x + width <= dimensions[0] and y + height <= dimensions[1], asset_id
        assert all(0 <= value <= 1 for value in sprite["pivot"])

assert passport["level2_runtime_families"] == ["house", "workshop"]
for variant in passport["level2_winter_variants"]:
    verify_file(variant, variant["dimensions"])
    assert page_modes[variant["path"]] == "rgba"
    runtime = overrides.get((variant["slug"], "level2-winter"))
    expected_path = runtime["path"] if runtime else variant["path"]
    assert sprites[f"building.{variant['slug']}.level2-winter"][0] == expected_path
snow_surface = passport["snow_surface"]
verify_file(snow_surface, snow_surface["dimensions"])
assert sprites["concept.ground.snow"][0] == snow_surface["path"]
assert sprites["concept.ground.snow"][1]["rect"] == [0,0,1254,1254]
details = passport["ground_details"]
verify_file(details, details["dimensions"])
assert page_modes[details["path"]] == "rgba"
for detail in details["sprites"]:
    path, sprite = sprites[detail["id"]]
    assert path == details["path"]
    assert sprite["rect"] == detail["rect"]
    assert rgba_alpha_bounds(root / "assets" / path, region=detail["quadrant"]) == detail["alpha32_bounds"]
    x, y, width, height = detail["alpha32_bounds"]
    assert detail["rect"] == [x-3,y-3,width+6,height+6]
nature = passport["nature_variety"]
assert nature["dimensions"] == [1774, 887]
assert nature["alpha_mode"] == "rgba"
assert nature["concept_acceptance"] == "unverified"
assert "does not hide or clean" in nature["known_art_review"]["approval"]
nature_pages = {page["season"]: page for page in nature["pages"]}
assert set(nature_pages) == {"summer", "winter"}
for page in nature_pages.values():
    verify_file(page, nature["dimensions"])
    assert page_modes[page["path"]] == "rgba"
    _, _, _, transparent_colored = rgba_alpha_rows(root / "assets" / page["path"])
    assert transparent_colored == page["transparent_colored_pixels"], \
        (page["path"], transparent_colored, page["transparent_colored_pixels"])

assert len(nature["sprites"]) == 8
expected_nature_ids = {f"concept.bank.{index}" for index in range(4)} | \
                      {f"concept.shrub.{index}" for index in range(4)}
assert {record["id"] for record in nature["sprites"]} == expected_nature_ids
isolation_cells = [record["isolation_cell"] for record in nature["sprites"]]
for index, first in enumerate(isolation_cells):
    first_x, first_y, first_width, first_height = first
    for second in isolation_cells[index + 1:]:
        second_x, second_y, second_width, second_height = second
        overlap_x = first_x < second_x + second_width and second_x < first_x + first_width
        overlap_y = first_y < second_y + second_height and second_y < first_y + first_height
        assert not (overlap_x and overlap_y), (first, second)
for record in nature["sprites"]:
    assert record["winter_id"] == record["id"] + ".winter"
    cell_x, cell_y, cell_width, cell_height = record["isolation_cell"]
    rect_x, rect_y, rect_width, rect_height = record["rect"]
    assert cell_x <= rect_x and cell_y <= rect_y
    assert rect_x + rect_width <= cell_x + cell_width
    assert rect_y + rect_height <= cell_y + cell_height
    seasonal_bounds = []
    for season, asset_id in (("summer", record["id"]), ("winter", record["winter_id"])):
        path = nature_pages[season]["path"]
        registered_path, sprite = sprites[asset_id]
        assert registered_path == path
        assert sprite["rect"] == record["rect"]
        assert sprite["pivot"] == record["pivot"]
        metrics = record[season]
        components1 = rgba_alpha_components(root / "assets" / path, 1, record["isolation_cell"])
        components32 = rgba_alpha_components(root / "assets" / path, 32, record["isolation_cell"])
        assert components1[0][1] == metrics["alpha1_main_component_bounds"], asset_id
        assert components32[0][1] == metrics["alpha32_main_component_bounds"], asset_id
        opaque_bounds = rgba_alpha_bounds(root / "assets" / path, threshold=32,
                                          region=record["isolation_cell"])
        opaque_x, opaque_y, opaque_width, opaque_height = opaque_bounds
        assert rect_x <= opaque_x and rect_y <= opaque_y, asset_id
        assert opaque_x + opaque_width <= rect_x + rect_width, asset_id
        assert opaque_y + opaque_height <= rect_y + rect_height, asset_id
        low_alpha, edge_alpha = rgba_alpha_region_metrics(root / "assets" / path,
                                                           record["isolation_cell"])
        assert low_alpha == metrics["low_alpha_pixels"], asset_id
        assert edge_alpha == metrics["edge_alpha_max_lrtb"], asset_id
        assert max(edge_alpha) <= 1, asset_id
        seasonal_bounds.append(metrics["alpha1_main_component_bounds"])
    union_x = min(bounds[0] for bounds in seasonal_bounds)
    union_y = min(bounds[1] for bounds in seasonal_bounds)
    union_right = max(bounds[0] + bounds[2] for bounds in seasonal_bounds)
    union_bottom = max(bounds[1] + bounds[3] for bounds in seasonal_bounds)
    assert record["rect"] == [union_x - 3, union_y - 3,
                              union_right - union_x + 6, union_bottom - union_y + 6], record["id"]

fringe = nature["known_art_review"]["winter_shrub_2_cyan_fringe"]
winter_path = root / "assets" / nature_pages["winter"]["path"]
_, _, winter_alpha, _ = rgba_alpha_rows(winter_path)
shrub_2 = next(record for record in nature["sprites"] if record["id"] == "concept.shrub.2")
fringe_y = shrub_2["winter"]["alpha32_main_component_bounds"][1]
cell_x, cell_y, cell_width, _ = shrub_2["isolation_cell"]
fringe_values = [winter_alpha[y][x] for y in range(cell_y, fringe_y)
                 for x in range(cell_x, cell_x + cell_width) if winter_alpha[y][x] > 0]
assert len(fringe_values) == fringe["alpha_gt_0_pixels"]
assert sum(value >= 8 for value in fringe_values) == fringe["alpha_ge_8_pixels"]
assert sum(value >= 32 for value in fringe_values) == fringe["alpha_ge_32_pixels"]
assert max(fringe_values) == fringe["maximum_alpha"]
resident = passport["resident"]
assert resident["authored_contact_poses"] == 2
assert resident["phase_source"] == {"0": "A", "1": "B", "2": "A reused", "3": "B reused"}
assert resident["canonical_replacements"] == ["walk", "logs", "sack", "cart"]
assert resident["legacy_fallbacks"] == []
assert resident["concept_acceptance"] == "unverified"
for page in resident["pages"]:
    verify_file(page, resident["dimensions"])

resident_concept_pages = {
    "walk": "art/concept-residents-walk.png",
    "logs": "art/concept-residents-logs.png",
    "sack": "art/concept-residents-sack.png",
    "cart": "art/concept-residents-cart.png",
}
cell_x = [round(index * resident["dimensions"][0] / resident["directions"])
          for index in range(resident["directions"] + 1)]
cell_y = [round(index * resident["dimensions"][1] / resident["phases"])
          for index in range(resident["phases"] + 1)]
inspection_by_path = {record["path"]: record for record in resident["atlas_inspection"]}
assert set(inspection_by_path) == set(resident_concept_pages.values())
expected_cart_padding_overrides = {
    "resident.man.cart.1.1": {"padding_ltrb": [3, 3, 3, 2], "person_foot_anchor": [368.5, 431.5]},
    "resident.woman.cart.1.0": {"padding_ltrb": [3, 0, 3, 2], "person_foot_anchor": [376.5, 657.5]},
    "resident.woman.cart.3.0": {"padding_ltrb": [3, 3, 3, 1], "person_foot_anchor": [773.5, 655.5]},
    "resident.woman.cart.1.1": {"padding_ltrb": [3, 0, 3, 3], "person_foot_anchor": [385.5, 864.5]},
    "resident.woman.cart.3.1": {"padding_ltrb": [3, 0, 3, 3], "person_foot_anchor": [772.5, 861.5]},
}
assert inspection_by_path[resident_concept_pages["cart"]]["alpha32_safe_padding_ltrb_overrides"] == \
    expected_cart_padding_overrides
for path, inspection in inspection_by_path.items():
    assert inspection["alpha_threshold"] == 32
    assert inspection["row_semantics"] == ["man A", "man B", "woman A", "woman B"]
    assert len(inspection["alpha32_main_component_bounds"]) == 4
    for row, bounds_by_direction in enumerate(inspection["alpha32_main_component_bounds"]):
        assert len(bounds_by_direction) == resident["directions"]
        for direction, bounds in enumerate(bounds_by_direction):
            x, y, width, height = bounds
            assert x >= 0 and y >= 0, path
            assert x + width <= resident["dimensions"][0], path
            assert y + height <= resident["dimensions"][1], path
            # Bounds describe the complete connected silhouette, not a nominal
            # grid cell that may cut through boots at a row boundary.
            assert x < cell_x[direction + 1] and x + width > cell_x[direction], path
            assert y < cell_y[row + 1] and y + height > cell_y[row], path

count = 0
for look in resident["looks"]:
    for load in resident["loads"]:
        for direction in range(resident["directions"]):
            for phase in range(resident["phases"]):
                asset_id = f"resident.{look}.{load}.{direction}.{phase}"
                assert asset_id in sprites, asset_id
                path, sprite = sprites[asset_id]
                assert path == resident_concept_pages[load], asset_id
                assert sprite["reference_width"] == 222, asset_id
                authored_row = (0 if look == "man" else 2) + (phase % 2)
                bounds = inspection_by_path[path]["alpha32_main_component_bounds"][authored_row][direction]
                alpha_x, alpha_y, alpha_width, alpha_height = bounds
                authored_id = f"resident.{look}.{load}.{direction}.{phase % 2}"
                override = expected_cart_padding_overrides.get(authored_id)
                padding_left, padding_top, padding_right, padding_bottom = \
                    override["padding_ltrb"] if override else [3, 3, 3, 3]
                rect_x = max(0, alpha_x - padding_left)
                rect_y = max(0, alpha_y - padding_top)
                expected_rect = [
                    rect_x,
                    rect_y,
                    min(resident["dimensions"][0], alpha_x + alpha_width + padding_right) - rect_x,
                    min(resident["dimensions"][1], alpha_y + alpha_height + padding_bottom) - rect_y,
                ]
                assert sprite["rect"] == expected_rect, asset_id
                cache_key = (path, tuple(expected_rect))
                if cache_key not in alpha_bounds_cache:
                    alpha_bounds_cache[cache_key] = rgba_alpha_bounds(root / "assets" / path,
                                                                       region=expected_rect)
                assert alpha_bounds_cache[cache_key] == bounds, (asset_id, alpha_bounds_cache[cache_key], bounds)
                if load != "cart":
                    # Walk/Logs/Sack are grounded on the audited lowest boot
                    # contact rather than the old 222 px row boundary.
                    ground_y = sprite["rect"][1] + sprite["pivot"][1] * sprite["rect"][3]
                    assert abs(ground_y - (alpha_y + alpha_height - .5)) < .001, asset_id
                elif override:
                    pivot_x = sprite["rect"][0] + sprite["pivot"][0] * sprite["rect"][2]
                    pivot_y = sprite["rect"][1] + sprite["pivot"][1] * sprite["rect"][3]
                    assert abs(pivot_x - override["person_foot_anchor"][0]) < .001, asset_id
                    assert abs(pivot_y - override["person_foot_anchor"][1]) < .001, asset_id
                assert sprites[f"resident.{look}.{load}.{direction}.{phase % 2}"][1]["rect"] == \
                    sprite["rect"], asset_id
                assert sprites[f"resident.{look}.{load}.{direction}.{phase % 2}"][1]["pivot"] == \
                    sprite["pivot"], asset_id
                legacy_id = f"legacy.resident.{look}.{load}.{direction}.{phase}"
                assert legacy_id in sprites, legacy_id
                count += 1
assert count == 256
print("V03 asset passport: PARTIAL acceptance, 64 concept-derived runtime building overrides; "
      "Walk/Logs/Sack/Cart use two authored contact poses reused across four phases with reference_width 222; "
      "historical provenance, exact rectangles, alpha modes, bounds, hashes and notices verified; "
      "concept acceptance remains unverified.")

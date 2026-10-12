# Asset and localization pipeline

`AssetPipeline` is a CPU-only target. It does not depend on SDL, RmlUi, the simulation or the network. It contains a validated asset catalog and deterministic RGBA texture-atlas packing. `AtlasTool`, `LocalizationCheck` and `UiCheck` are offline build tools.

## Packaged registry

`content/asset_sources.json` assigns ownership and source/license notices. Original UI, locale and compiled-shader files have project metadata. Third-party files require an explicit override with a real notice file. Gameplay artwork should receive an explicit stable ID in an override; the `project:<path>` default is for internal files and changes when their path changes.

`tools/asset_tool.py` checks portable confined paths, case collisions, IDs, file limits, declared formats, PNG header dimensions, RML/RCSS references, duplicate JSON keys, five-language key parity and license metadata. It generates `assets/manifest.json` in the build directory. The generation SHA256 covers sorted records: UTF-8 byte-length-prefixed `id`, `path`, `kind`, `sha256`, `license`, `source`, `author`, `notice`, followed by one newline per record. This hash identifies the asset generation; it is distinct from the gameplay rules hash negotiated by the server.

`AssetCatalog` checks this identity, format/extension consistency, declared byte counts and actual file hashes. Registered-path reads return verified bytes. The catalog provides package validation; it is not yet a runtime generation manager or an atomic hot-reload system.

## Build staging

The build prepares a fresh candidate under its own build directory. It runs the Python registry validator, native ICU pattern and FreeType glyph checks, and native RmlUi menu/HUD rendering before replacing the previous build assets. A validation failure preserves that previous directory. An exception while renaming rolls it back. This build procedure does not claim protection against process termination between the two directory renames; runtime hot reload and crash-safe generation selection remain separate work.

## Texture atlases

Build the desktop preset, then run:

```powershell
.\build\engine-windows\AtlasTool.exe .\art-source\atlas.json .\art-source\export-001
```

The output directory must not exist and its parent must already exist. Input PNGs and notices stay within the config directory. An example input is:

```json
{
  "schema": 1,
  "page_size": 2048,
  "padding": 2,
  "max_pages": 16,
  "images": [{
    "id": "building.house.level1",
    "path": "sprites/house.png",
    "pivot": [0.5, 0.9],
    "license": "LicenseRef-Proprietary",
    "source": "project",
    "author": "Ribterra project",
    "notice": "NOTICE.txt"
  }]
}
```

Packing is deterministic across input order, without rotation. Page dimensions are powers of two up to 4096. Rectangles exclude padding; edge pixels are extruded into the padding. The output contains PNG pages and a manifest with stable IDs, rectangles, UVs, pivots, provenance and content hashes. Invalid dimensions, duplicate IDs, decode failures and memory/page limits fail the export. Atlas production is an offline tool. The current client loads production sprites through assets/art/world.json; the earlier procedural-only description is obsolete. See [the dated runtime inventory](visual-targets/ASSET_ENGINE_AUDIT_2026_09_28_UA.md) for loaded versus selected assets and memory arithmetic.

Atlas hashes identify encoded artifacts from the pinned libpng build, not equivalence of pixels across different encoders. License notice bytes are copied into each export under hash-derived names and their SHA256 values enter the manifest identity. Unicode source and output paths use filesystem-aware byte I/O on Windows.

## Localization

English is the base catalog. EN, UK, RU, PL and ES use identical keys and ICU argument names/types. Named values accept numbers or text; plural selection and numeric formatting use the active ICU locale. Missing keys, invalid patterns, wrong arguments and nonfinite numbers are errors. Pseudolocalization expands rendered text for layout checks. `LocalizationCheck` also checks that the packaged Noto font contains every codepoint in the five catalogs.

User chat and dynamic status text must be escaped before entering RML. The renderer consumes RmlUi's premultiplied vertices/textures with matching blending. Locale coverage of the current menu/HUD does not imply that every planned gameplay screen, guide or event has been authored.

## Remaining scope

Tiled conversion, production artwork and animation metadata, runtime generation reload with retention of the last valid resources, invalid-shader fallback, audio imports and complete content coverage remain in `ENGINE_REQUIREMENTS.md`.

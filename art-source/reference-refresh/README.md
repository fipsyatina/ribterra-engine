# Ribterra reference graphics refresh

Generated from the user's medieval settlement references on 2026-09-15. These are runtime assets, with provenance and original source plates retained here. The game still uses its existing isometric sprite renderer; this is not a new 3D engine or a claim of pixel-for-pixel reproduction of the concept scenes.

## Asset coverage

30 source plates register 256 sprite/material IDs:

| Family | Plates | Registered entries |
| --- | ---: | ---: |
| All 20 building kinds | 20 | 120 |
| Ground, water, snow, cave materials | 1 | 16 |
| Ecological grass and soil | 1 | 6 |
| Trees and seasonal variants | 1 | 12 |
| Rocks, ore and mountain silhouettes | 1 | 16 |
| Undergrowth, berries and settlement props | 1 | 14, including 2 berry aliases |
| Smoke, mist, glow, ripples, foam, fire, embers | 1 | 8 |
| Four resident appearances, two directions, four phases | 2 | 32 |
| Deer, hare, boar, wolf; summer/winter; four phases | 2 | 32 |

Buildings: camp, house, farm, stockpile, mine, workshop, temple, tavern, cartographer, research, smelter, fishing station, library, barracks, bridge, sawmill, mill, kitchen, clay pit, road. Each has three construction frames, completed, winter and a proposed level-II frame.

## Connected in the game

Existing building, tree, ground, ore, berry, resident and animal IDs are replaced in `assets/art/world.json`. Source plates are copied without resampling; UV rectangles and foot pivots are registered separately. Resident frames use a consistent crop size per appearance/direction to avoid frame-dependent scaling. Runtime reads chroma-key source mattes once, removes foliage color spill, and derives VFX opacity from grayscale masks. Original PNG pixels remain unchanged on disk.

The world renderer adds depth-sorted undergrowth, moving river glints, shoreline foam/mist, chimney smoke, fire, soft building contact shadows and amber night light. Effects are bounded (96 water highlights, 12 mist patches, 48 building emitters) and only originate in explored, visible areas. Terrain geometry caching and network movement interpolation remain in use. Camera zoom now reaches 3.0 instead of 1.55. Roads share their boundaries; shallow water retains a water appearance instead of becoming mostly sand. A former ploughed-ground overlay is removed from snow.

## Prepared versus active

The level-II building plates, extra well/fence/crate/lantern props and ember mask are prepared assets, not new gameplay systems. Existing completed/winter building selection still chooses the current stage-I artwork. This change does not add research mechanics, redesigned building panels, physical water-wheel animation, volumetric lighting, or new monster types. Those should not be inferred from the reference artwork. Existing UI textures and frontier fog masks remain where their interfaces require them.

## Rebuild and review

`tools/register_reference_art.py` requires Pillow and NumPy for read-only rectangle analysis. It uses the stored source paths if available and otherwise the durable copies in `sources/`. It updates the art registry, asset provenance and `integration-report.json`. `world-before.json` is the original ID registry for reproducible replacement without duplicate IDs.

Use the Windows release CMake preset to build the game. `ReferenceVisualProbe` captures an explicitly arranged village at day, winter and night using Direct3D 12; this is an art fixture, not proof of world-generation layout. `BuildingVisualProbe` exercises the building catalog and seasonal appearance. `AtmosphereVisualProbe` captures actual generated biome transitions. Evidence and performance JSON files are under `artifacts/reference-refresh/`.

## Validation on this machine

- Release build and staging validation passed for fonts, localization, RmlUi and assets.
- `art_alpha`, `atmosphere`, `unit_motion` passed, including foliage edge cleanup, retained autumn colors, continuous shoreline blends and motion timing.
- Native Direct3D 12 captures: day, winter, night, two effect times; building catalog and compact layout; generated coast, mountain/snow and desert transitions.
- Two-client local online probe, seed 70421, 10 residents per nation, wildlife enabled, 240 measured frames after warmup, 60 FPS target, RTX 3050 Laptop GPU: before 16.671 ms mean wall frame / 8.806 ms CPU frame; after 16.673 ms / 9.544 ms. After: 59.975 FPS, CPU p95 12.517 ms. This short controlled comparison demonstrates retained frame pacing in this fixture, not a guarantee for every population, device or long session.

The separate playable package is `dist/reference-refresh/Ribterra/`. It keeps the current 0.25.5 gameplay/network version and has its own desktop shortcut.

# Ribterra 0.11.1 — painted textures

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Local update of the presentation from 9 September 2026. The original images of the generator are stored in `art-source/production-v1`. With the user's direct permission, separate copies are technically prepared: transparency, boundaries, support points, edges of surfaces and packaging. The originals were not repainted or changed.

## What's connected

- There are four game facilities available: camp, house, farm, warehouse. The state is chosen according to the actual progress: 0–1/3, 1/3–2/3, 2/3–1, ready, ready for winter. The figures for the delivery of materials and work remain.
- Residents: professional straights, front/rear angles, selection markers and a drawn load indicator.
- Four types of trees with winter variations, berries, stones, roots and mountains; grass, water, snow, gravel and the underground surface.
- Fog at the edge of the open area; And the unidentified cells don't reveal themselves.
- Panels and buttons with a scale of nine areas; the resource icons; The course and the options for construction and job selection. A window to the front of the construction site.

## Content and boundaries

`assets/art/world.json` describes **189 sprays in four PNG- atlases 2048×2048**. Metadata checks the boundaries of rectangles and pivots; AssetCatalog checks the manifest files. Separate copies UI are located in `assets/ui/art`.

55 building states cover 11 types: house, camp, farm, warehouse, tavern, barracks, temple, town hall, sawmill, smithy, and mine. Seven additional types are loaded from the atlases and shown by the art test; their economic functions and construction commands are not yet implemented in the simulation. Replacing a texture does not add those functions.

Units have static positions at two angles; Full animation cycles and eight directions are not included. Water uses a static surface. The map geometry and layout HUD remain the current game systems: this release is not a complete implementation of the whole concept.

## Reproduction

1. Run `tools/prepare_game_art.py` Python from Pillow and numpy; I need to get `AtlasTool.exe` together.
2. `scripts/build.ps1 -SkipBootstrap` performs compiling, asset verification, CTest and installation.
3. `ArtVisualProbe.exe <directory>` renders all 55 states and actual client summer/winter scenes. `--software` selects the software renderer. The winter condition is set only in the check box. The game client gets the season from the server image.
4. `MineVisualProbe.exe <directory>` checks an underground client with the same atlases.
5. `scripts/package.ps1 -SkipBuild` creates a portable Windows ZIP from a verified installation folder.

## Locally verified

- MSVC Release compiled without C++ warnings/errors. Assets, five locations and RML/RCSS have been staged.
- The main line: 51/52. The only failure is  Windows Windows UDP relay `WinError 10054` when a successful network scenario is completed. Repeat targeted scan: 15/15, including this scenario and the new `art_client_rendered`. After the final alignment of the edge of the fog: 4/4 art checks, input, renderer and client. All 53 test scenarios are confirmed; It's not just one 53/53.
- Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU: all 55 building condition, summer and winter scene, mine after passage, mining and conservation/restoration.
- Through the visible window of the installed 0.11.1: a new settlement, resident selection, construction mode and cursor, site placement, house completion, zoom, pause, and F12. The window was left paused.
- SHA-256 of all 10 endings PNG coincide with the original `files.json`. Prior to the restart 0.10.2, the current world and a separate copy of the previous saves in `.deps/pre-art-save-20260909-162344` are preserved.
- Portable `dist/Ribterra-Windows-x64-0.11.1.zip`: 48 701 096 bytes. The freshly unpacked copy verifies the hashes of all 71 assets, launching a compact menu, and a demo settlement on Direct3D 12; Both EXE- launches confirmed the 0.11.1 version. SHA-256 the archive is recorded in the neighboring `.sha256` and `artifacts/art-0.11.1/package.json`.

Pictures and machine reports: `artifacts/art-0.11.1`; The final logs are — `.deps/art-0111-*.log`. This is a local Windows check. CI and GitHub releases in this work were not executed.

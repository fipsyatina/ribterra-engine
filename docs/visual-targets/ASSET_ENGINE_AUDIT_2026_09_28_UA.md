# Ribterra — graphics inventory and engine capabilities

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Audit 28.09.2026: `bdc53e8`, version 0.26.0, the work tree contains the uncommitted preparation LightingMode and documents. This is a code check, JSON and PNG headers; The new graphics executable in this audit was not being created.

[Full machine register with SHA256, each sprite size and ID](verification/assets-audit-2026-09-28.json). Reproduction: execute Node script [.omx/plans/audit-graphics-assets-2026-09-28.cjs](../../.omx/plans/audit-graphics-assets-2026-09-28.cjs) from the checkout. The script reads the assets, checks the rectangles, and records only that report. It doesn't check the decoding quality of all the pixels or the look of the game.

## Being present is not the same as using

[GameArt::load](../../src/client/game_art.cpp:72) reads all pages `assets/art/world.json`, decodes to RGBA, applies magenta/luminance alpha, and registers the sprites. The candidate replaces the old set after successful loading; a duplicate ID is an error. It's not a ready-made interface for hot reloading all the game content.

| The group | The actual volume | How to use / what to take into account |
|---|---|---|
| World runtime set | 42 PNG- pages; 622 records, 622 unique ID | All are loaded; not all are selected during gameplay. Source world.json and build/engine-windows/assets/art/world.json now have the same SHA256 |
| The old atlas-0...3 | 4 × 2048²; 110 records | Among them 10 states townhall/blacksmith, which are not in the current catalogue; Other groups cannot be deleted with them. |
| refresh buildings | 20 × 1536×1024; 120 records | By stage1/2/3, ready, winter, level2. `.level2` does not select the current [selector](../../src/client/view.cpp:661). These are 20 loaded but not connected states. |
| refresh-materials | 1254²; 16 materials | Land/stone/water; Not normal/roughness maps |
| refresh-grass / trees / rocks / detail | 6 / 12 / 16 / 14 records | Variations of decoration; The current view uses only a small portion of the detail. The presence of rock texture does not create an impassable slope |
| refresh-effects | 8 luminance records | Soft overlays, smoke, fire, flashes; Not physical light and water |
| The old refresh-walk | 2 × 16 records | Fallback animation; the main resident uses the directional family |
| Animals | 32 records | Summer/winter, four types × four phases; Only existing animals are needed. |
| Directional residents | 8 PNG × 1774×887; 256 records | man/woman × walk/logs/sack/cart × 8 directions × 4 phase. Cargo takes [resident_art](../../include/ribterra/resident_art.hpp:16) from real carried items |
| UI PNG | 83 files | 19 building icons, 14 glyphs, 30 icons/cursors, 16 ui-kit, 4 menu/lobby images; It's a separate group from the world's atlases. |
| UI fonts | NotoSans-Regular.ttf, CormorantGaramond.ttf | [Load](../../src/ui/game_ui.cpp:687); Noto for numbers and long explanations; Cormorant for headings |

20 building families: camp, house, farm, stockpile, mine, workshop, temple, tavern, cartographer, research, smelter, fishing, library, barracks, bridge, lumbermill, mill, kitchen, claypit, road. The [alpha rules](../../include/ribterra/alpha_rules.hpp:12) allow the construction of 12 types; after removing Road — 11. Camp is a separate starting object. The presence of temple/barracks art does not allow the locked mechanics to be returned to the demo.

### Memory

The sum of the `width × height × 4` world pages: **306 197 408 bytes = 292,01 MiB**. UI PNG: **31,52 MiB**, if all of them are decoded at the same time. It is an arithmetic dimension of the full RGBA8- images, **not measured process VRAM**: real memory also depends on UI crop-copy, upload buffers, render targets, driver alignment, and a simultaneous old/new generation. GameArt decodes the pages in sequence; We don't claim that all CPU- copies live in RAM at the same time.

The planned budget 256 MiB from the previous production spec is already smaller than the basic world set. So this is the goal of optimization, not a proven property. Before normal/depth/wet/snow layers, you need a ledger and a peak measurement. Do not multiply the entire set to the 3–4 material maps automatically.

### The required frames already exist

`ui-kit.0.png` and `ui-kit.4.png` are visually verified: dark blue in the middle, metallic gold frame and decorative corners. [theme.rcss](../../assets/ui/art/theme.rcss:1) defines six nine-patch decorators. But later connected [concept.rcss](../../assets/ui/concept.rcss:2) sets `decorator: none` for the cards and buttons. First restore the frames **for the new HUD through scoped selectors**, then evaluate the need for new images. Changing the entire menu/lobby cascade globally is dangerous.

### Production sources

Folders `art-source/production-v1…v4`, `reference-refresh`, `runtime-0.11.1/export-*` — source sheets, prompts and exports. They are not evidence of editable 3D models, animation rigs, or synchronous export of material maps. Every master needs a passport of origin and a camera check. A copy of the file in `art-source` does not mean that the game reads it: runtime source — asset registry and world.json.

## Engine: confirmed boundary

| Area | The present | The need for further updates |
|---|---|---|
| Renderer | C++ / SDL_GPU, [Frame2D](../../include/ribterra/render_frame.hpp:15), painter order, textures, scissor, four blend modes | V01–V03 can be built on the current track. `Pass` — logical category, not automatic GPU render graph |
| Shader | [texture × vertex color](../../shaders/sprite.frag.hlsl:13), [one color target without depth](../../src/render/renderer2d.cpp:473) | There's no multichannel material, normal lighting or depth-aware mist |
| UI | [RmlUi 6.3](../../CMakeLists.txt:238), real controls, font glyphs, nine-patch | The new HUD requires layout, events, and cascade control; it does not require a new engine |
| Geometry | [1440×900 logical](../../src/client/view.cpp:1611), shared terrain corners, height sampling | To V01 save current projection/letterbox. New adaptive world viewport not considered ready |
| Lighting | [night color grade](../../src/client/view.cpp:1625), warm sprite overlays | The light from the drawn window does not illuminate the floor/resident; Full Dynamic deferred |
| Shadows | Building contact overlay; [fallback shadow of the inhabitant](../../src/client/view.cpp:829) | The art-ready branch returns earlier. V03 adds a separate contact shadow, not promising a moving shadow from the source |
| Water | [fx.4 animation and haze](../../src/client/view.cpp:1032) | V02: readable channel/banks/overlays. Flow/reflections/physics do not arise from the new atlas |
| Weather | [calendar of seasons](../../include/ribterra/seasons.hpp:10); winter sprite; The temperature is now [empty](../../src/ui/game_ui.cpp:1240) | V04 — weather state, forecast, save/net; V05 — cover/melting. No temperature and forecast |
| Snow tracks | world.json has no footprint family | Separate stamps + bounded cache + walking events; Not a deformation of geometry |
| Physics | Gameplay movement/traversability/work | Full rigid-body/cloth/fluid solver does not include these updates |
| Dynamic scaffold | [API/status](../../include/ribterra/renderer2d.hpp:18), [Dynamic rejection](../../src/render/renderer2d.cpp:260) | `effective=Legacy`, `dynamic_available=false`; There's no new light and no switch. |

The manifest does not have separate normal/depth/roughness/emission/material/snow-mask/flow/footprint map contracts. The pixels of the warm window in the color PNG do not replace the emission mask. These are the requirements for future content and loader, not just existing textures.

## What to Do With the Result

Detailed maps V01–V05 and deferred DL: [ flow plan](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md). Each stage uses only the necessary part of the inventory. Old atlas/orphan IDs should be deleted only after full analysis of links and package tests, separately diff; Do not clean them out of sight during the change UI.

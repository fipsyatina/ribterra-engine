# GameView  limited control GPU- paths

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **The technical component has been tested; Full engine NOT accepted.**

Added separate `load_engine_control_art` and `compose_engine_control`: real `ClientSession` and cache locations GameView, but eight external neutral presentation controls. The usual `compose`, launch, launcher, generator, HUD and final art catalog are not switched to this path. It's not a new version of the game, nor is it a replica of the final look.

## Implementation

- The relief is taken from real shared terrain corners. Each quad has two NW–SE triangular planes with their unit normals and coordinate camera-relative P/N. The height of the control leg is interpolated on these same triangles.
- Ground and lit-sprite draws are passed through typed `SkyShadowSlice`; The schema-2 materials have six channels. Sky triangles and local-occluder rows come from the same anchors and scale.
- Neutral kit: house, warehouse, workshop, wood, rock, unloaded trunk, with a deck and bag. The building has two emission-window areas. It is technical geometry, **not a copy of concept art**. The light propagating from the windows around this passage is unproven.
- Unknown objects are passed to sprite ID/scale/ID checks and to fill in visible buffers. Coordinates are checked to float→int, without clamping. Caller frame is published only after successful compilation; The complete invariance of private files on each failure path is not declared.
- Time limits: camera-relative ±32, zoom 0,5–2, cell radius ≤12, scale=1, ≤32 of visible controls, only surface layer and fully detected requested ground window. This is not a full pan/zoom/reveal contract game.

## Actual evidence

Fresh `build/gameview-engine-2026-10-08-attempt05`: 21 production translation unit + probe, `/W4 /WX`, 27 command sidecars exit0. 96/96 repository/snapshot source bindings, 8/8 protected inputs, 52/52 individual kit files and 69/69 shader files are verified. Sheddars reused from the accepted authored attempt10 after hash/source-closure verification; There is no **new shader compilation/negative verification**.

Build root: 113 824 517 bytes, within 128 MiB; on C: measured 97 412 231 168 free bytes to native run, over 384 MiB. Early build01–04 failed and were corrected: scoped-name/explicit-conversion warnings, isolated GNS include/static-link settings, and spelling UV aggregate. Warning repairs do not alter mechanics or ordinary repairs.

`GameViewEngineProbe.exe` actually run twice, each exit0:

- `build/gameview-engine-native-2026-10-08-attempt01`
- `build/gameview-engine-native-2026-10-08-attempt02`

Each root: 5 PNG size **1920×1080** + complete JSON, 299 858 bytes. **6/6 byte-exact** files between the probes, including the report. The only independent review confirming this bounded gate.

- Native D3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver 32.0.15.9282, software=false.
- Real offline seed70421, camera(21,21), zoom1,3/radius8: 289 ground cells, 8 controls, 248 sky triangles, 10 local occluders, 8 accurate cell picks.
- Each capture: submitted/captured, exact pass order SunShadow→MoonShadow→GroundGBuffer→GroundLighting→WorldForward→Composite→UI (`[6,7,0,1,2,3,4]`), exact UI RGBA(215,42,76,255).
- Sun and repeat Sun byte-exact on different slots. The addition of an unknown control with incorrect ID/NaN scale/zero IDs does not change the geometry or native pixels.
- Removing only sky casters changes the 53 850 pixels. Sun→Moon changes lighting without changing the geometry of the fingerprint.
- The invalid radius and the final `FLT_MAX` coordinate deviate without changing the caller frame.
- The separate existing-output negative: actual exit1 to SDL run; 6/6 existing files are unchanged, failure JSON is not created in another directory.

Main reviewed the `gameview-sun.png` and `gameview-moon.png` cheeses. You can see a neutral relief, a change in color temperature, emission windows and shadow directions. It's a GPU readback from a separate visible SDL- window, not a desktop scanout, an interactive control-test or unassembled video. Native computer-use APIs are not available in the current tool.

## Hash anchors

| The file | SHA-256 |
| --- | --- |
| `src/client/view.cpp` | `69BD83299440672813C85C0D6134BAE2AFFE3976BFFC1640B35A5148C8DAA88F` |
| `tools/gameview_engine_probe.cpp` | `CD5BFA9B40AD426A20D295D2CB5A53FFAC2587C70C2301BAC6DAEA843490CD3B` |
| `GameViewEngineProbe.exe` | `69512E6426332756133B316B891CF6B128C25B8A8BEEF8C5E131E6C10E9BFC53` |
| `build-report.json` | `815F2398BF02BF315A5C64B5D365E033060E8846943743394DF1AAAC4CFF03E4` |
| Both `gameview-engine-report.json` | `4191CEA2E84DDEC928BB00E98EAE6D2084114D6A2402E7988EF228B13B970022` |

## What's left OPEN

The usual GameView, all world objects, full camera/zoom/reveal matrix, water/bridge receivers, causal local/contact pixels, real distribution of light emission, full scene with weather/mist/snow, artistic match to concepts and HUD. GPU timing in captures — null; Readback is not a performance measurement. **120 FPS was not measured**, full CTest was not launched, E01–E17 was not closed. Further — the expansion of the engine-road production and its inspections; I'm not going to the game mechanics.

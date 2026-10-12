# GameView Window Light — limited evidence of local light from windows

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **The narrow component is approved; full engine, regular game launch and artistic appearance are not accepted.**

This checkpoint checks only the opt-in path `GameArt` → `GameView` → native D3D12 for two local light sources of a neutral diagnostic home. The control kit is required for measured technical proof and ** is not a concept art or production catalogue**.

The roots:

- strict build: `build/gameview-engine-2026-10-08-attempt10`;
- two native streaks: `build/gameview-engine-native-2026-10-08-attempt06` and `attempt07`;
- kit: `build/engine-control-kit-2026-10-08-check05/export-a`.

## Decision

**APPROVE only bounded window-light lane.** Subject: fresh strict build, two repeated native runs, accurate source/artifact bindings, causal on/off and blocker fixtures, transactional negative paths, and independent read-only audits. The main checkpoint and the wider reception remain separate next steps.

## Contract of component

- Catalogue — schema 2. `lit_sprites.local_emitters` not required for old descriptions; In the control kit only `control.house.sprite` has two emitters, the other seven categories have `[]`.
- The number of emitter-describing fields canonised in float32 to the record JSON; the same exact descriptors are written in `world.json` and `control-kit.json`.
- House anchors have a local render Y=`0.9300000071525574`. Minimum external clearance from the owner occluder — float32 `0.0020000000949949026`; anchor inside, at or near the boundary of this distance from the 3D owner volume deviates.
- The color and intensity are re-derived from material emission. The emitter metadata has no duplicate color and no baked ground glow.
- Limits: maximum 4 surface lights, maximum 32 blocker rows for a single light and maximum 256 global local-occluder rows. The ranges are assembled atoms: overload or incorrect range does not publish a partial frame.
- Source-off switches off both the material emission scale and automatic local sources without changing the geometry of the fingerprint.
- The storage of the external slot0 is proven only for pre-existing external lights with zero blocker ranges. The wider external-light merge contract is not claimed.

## Strict build and attachment sources

Attempt10 compiled MSVC C++20 from `/W4 /WX`. Save the 27 command exit sidecars; Each one contains the actual `0`.

The independent audit re-calculated:

- **96/96** unique repository/snapshot source bindings — hash and bytes are matched;
- **8/8** protected paths — before/after/current converge;
- 22 object files, 4 tool binaries, 4 generated headers, 3 runtime files and 54 the command-evidence files — ledger accurate;
- **69/69** shader files in accepted origin and staged copy — byte-exact; 34 binary/reflection manifest pairs valid;
- **52/52** files are kit; 50 manifest assets valid, export-a/export-b byte-exact;
- kit check root: 18 878 600 bytes, stock up to 128 MiB — 115 339 128 bytes.

The Shader cohort is reused from the accepted authored-sky attempt10. It's not a new shader compilation or a new general shader acceptance.

## Native evidence

`GameViewEngineProbe.exe` root-run run for native06 and native07; Both launches had an actual exit0. A separate native exit sidecar is not preserved, so exit0 is a certified root-run result rather than a conclusion from JSON.

Each native root contains exactly **13 PNG + 1 report = 14 files**. All **14/14** byte-exact files between native06 and native07, including the report. For each root:

- all 13 PNG have 1920×1080, the correct PNG header and hash from the report;
- PNG size range: 18 440–77 858 bytes;
- root size: 693 157 bytes; the reserve to 128 MiB — 133 524 571 bytes;
- backend `direct3d12`, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, `software=false`;
- Each capture has `submitted=true`, `capture=true`, exact graph order `[6,7,0,1,2,3,4]`;
- `gpu_execution_ms=null`, so this is not FPS/performance evidence.

Raw `gameview-window-radiance-on.png` and `gameview-window-radiance-off.png` are reviewed separately. On-frame shows a warm local lighting ground and typed receiver near the neutral house; The off-frame keeps the geometry, but without that spread. It's a GPU readback from a native control window, not a desktop scanout, video or proof of artistic compatibility.

## Causes of pixel assertions

- Actual GameView ground: `[8,8,6,255]` → `[54,41,22,255]`.
- Typed resident receiver: `[8,8,6,255]` → `[52,40,22,255]`.
- Window source: off `[24,17,4,255]`; self-only and authored-lights-on `[255,255,196,255]`.
- Self-only frame leaves independent ground and typed-receiver pixels accurate to off; So the emission surface does not replace the evidence of local light propagation.
- Disabled source + external light from the non-matching receiver mask gives PNG byte-exact to source-off.
- Red fixture: red `160→14`, blue remaining `14`.
- Blue fixture: blue `157→14`, red remains `14`.

These fixtures provide a corresponding blocker-range and receiver-mask separation in a narrow scene. They don't provide all the possible coverage, materials, scales or world objects.

## Camera and transactional negative scope

CPU composition is tested in **6 cases**: 3 zoom (`0.62`, `1.3`, `2.0`) × 2 camera positions. Each case stored 2 authored lights, 0 lights in source-off, 2 accurate terrain picks, world-anchor stability and an unchanged geometry fingerprint.

This is the **CPU camera/composition matrix **, not the six individual GPU zoom captures. The full camera/zoom/reveal matrix is unproven.

Seven GameView rejection cases test the exact consistency of frame, lighting, camera projection and picking:

1. unsupported cell radius;
2. finite coordinate outside the world;
3. NaN coordinate;
4. external-light count greater than four;
5. external light with nonzero blocker range;
6. collision of a second authored stable identity;
7. authored blocker range over 32.

Six direct-emitter cases store the full `SurfaceLightingFrame` byte-exact:

1. stable ID zero;
2. stable ID overflow;
3. collision of the second emitter;
4. capacity greater than four;
5. the scaled exterior clearance below `0.002`;
6. disabled invalid ID/anchor filtered without an append.

A separate existing-output negative was a root-run with actual exit1 before creating new evidence. The existing root of **14/14 files remained unchanged**, failure JSON in the external directory was not created. Separate failure JSON as positive proof is neither present nor claimed.

## Hash anchors

| The artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/gameview-engine-2026-10-08-attempt10/build-report.json` | 199 854 | `0A93F603BC7A54C19D0E46687A7426A530CD6B8C3A8E2867E255E2CC1B60E06D` |
| `tools/gameview_engine_probe.cpp` | 59 681 | `1D102D56ACD12891A005540AFB528273DDCBB4254D30508DE04BCA32095D9711` |
| `GameViewEngineProbe.exe` | 41 962 496 | `9B2408E77E52851A49ED2A6327618A5DE8935C1803916EBC691C2EB6CBEB0A08` |
| both `gameview-engine-report.json` | 12 062 | `1DB08269820E59FC6AB5B9275EF63F16624029F25A226903A30188D591769A56` |
| shader `manifest.json` | — | `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1` |
| kit `manifest.json` | — | `98B48D003C1DEFD1C6A68EE3F9A36FB1798E8B5BFA59AE04D4B9F4B495263017` |

## The Effects of Working Skills

- `parallel review` — implementation, contract criticism and independent evidence audit were separated;
- `minimal C++ ABI-stable changes` — emitter metadata/adaptation added without expanding GPU ABI or changing old raster channels;
- `measured bounded proof` — The solution is based on exact hashes, bytes, pixels, caps and transactional negatives, not the overall image of the frame.

## What is NOT proven and remains OPEN

- E01–E17 and all broader engine acceptance gates;
- full engine, conventional GameView, launcher or startup game;
- production catalog, concept-art match, final HUD or final appearance;
- FPS, frame pacing, GPU timing or goal 120 FPS;
- video, desktop scanout or interactive long play session;
- full suite/CTest;
- full camera/zoom/reveal, weather, water, fog, particles and all world objects;
- runtime invalid-catalog reload rollback/GPU-upload counter proof. There are exporter negatives and source-level transactional checks, but they don't replace native reload evidence.

Neutral controls remain a diagnosis, ** not a concept-art**. Separate [reported compiled-only clearance](../GAMEVIEW_BUILD_COMPACTION_2026_10_08_UA.md) completed after this bounded approval: old GameView06–09 compiled-copies are removed, but GameView10/latest,05/rollback, kit and all native PNG/cores are preserved. It doesn't expand engine acceptance.

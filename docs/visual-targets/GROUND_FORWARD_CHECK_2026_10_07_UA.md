# Production GroundForwardSlice — verification 07.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: ** Limited engineering step implemented and verified; The full engine is NOT RELATED**. The production Renderer2D has an opt-in `GroundGBuffer → GroundLighting → WorldForward → Composite → UI`. The final 16/16 selected CTest, native D3D12 matrix and independent review were passed. **All E01–E17 are open; Dynamic GameView, concept compatibility and ≥120 FPS in 1920×1080 have not been proven.** The generator, mechanics, mass art catalog and the subsequent HUD are waiting for the full engine-first threshold.

## What Really Changed

- [Renderer2D](../../src/render/renderer2d.cpp) received a separate internal `WorldGraph::GroundForwardSlice`, strict routing, and seven attachments for each of the two frame sets. It's not a user-facing preset and it's not an activation `LightingMode::Dynamic`. The usual GameView/launcher is left with BakedCompat.
- [Frame contract](../../include/ribterra/render_frame.hpp): ground material index — one-based, explicit typed sRGB opaque albedo, index-aligned geometry XYZ/N and white tint. Ground only accepts opaque Terrain; WorldForward — only schema-2 material draws in painter order. Mixed/legacy/Atmosphere/Fog compatibility roles, no geometry, cutout ground, NaN and unacceptable metadata are deviated. Relative batching takes into account the ground material index; A/B/A is not sorted.
- `GroundMaterialGpu` — 48 bytes; `ForwardMetadataGpu` — 16 bytes. GPU `SpriteSurfaceGpu` 160 bytes and lighting 384/80-byte ABI remain unchanged. Reveal — separate CPU-field draw and separate uniform, not redefining reserved SpriteSurface components.
- The new [GBuffer](../../shaders/ground_gbuffer.frag.hlsl), [GroundLighting](../../shaders/ground_lighting.frag.hlsl) and [WorldForward](../../shaders/material_forward.frag.hlsl) programs are also available. The forward wrapper reuses the unchanged production material shader, not its diagnostic copy. GroundLighting supports bounded ambient/sun and up to four unoccluded point/spot lights; native controls below check one local source. Shadow/occlusion is not realized by this.
- Four exact format/usage queries (`COLOR_TARGET | SAMPLER`), separate MRT/ABI/binding/blend contracts, recursive include SHA256. CMake tracks the entire bounded shader source tree, including new nested leaf files. Angle/macro include fail-closed deviations; canonical quoted relative includes have a full recursive ledger.

| Attachment | Format | Content |
|---|---|---|
| ground_albedo | RGBA8_UNORM_SRGB | Albedo, A = AO/contact visibility |
| surface_world | RGBA16F | Camera-relative XYZ, W = validity |
| surface_material | RGBA8_UNORM | Oct normal RG, roughness B, sun visibility A |
| surface_flags | RGBA8_UNORM | Receiver byte, wetness, shore/snow, reveal |
| scene_linear | RGBA16F | Linear ground + straight-alpha forward objects |
| world_meta | RG16F | Coverage-weighted depth/reveal |
| final | RGBA8_UNORM | SDR Composite + single constant UI |

GBuffer source albedo hardware-decodes sRGB, sRGB MRT hardware-encodes it for storage, Lighting sample again hardware-decides storage. There is no manual double decode. Pass closes before sampling its targets. GroundLighting fullscreen UV attached to **the entire physical drawable**, with scissor world rectangle: letterbox does not stretch GBuffer. UI does not pass through exposure or world lighting.

## Final native proof

[GroundForwardProbe](../../tools/ground_forward_probe.cpp), [final JSON](../../artifacts/ground-forward-2026-10-07/native-final-05/ground-forward-report.json), [actual exit0 log](../../artifacts/ground-forward-2026-10-07/check-final-04/native-final.log). Windows / D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver 32.0.15.9282; actual initial drawable **1280×720**, build label 0.26.3. This is not 1080p performance foam.

A [fresh export kit](../../artifacts/ground-forward-2026-10-07/master-export-a/manifest.json) from the [-edited master](../../art-source/engine-calibration/material-master.json) is used, without changing its geometry or production material shader. Cabin and slope — simple diagnostic surfaces, **no concept-ready art kit**.

### Sun, window, UI and two frame slots

The sequence `A0 → B1 → B0 → A1`, the slots `0/1/0/1`. A: sun +Z, emission off. B: sun along the master slope normal, emission on. A0/A1 and B1/B0 PNG are approximately the same ** between different slots**, and reusing each slot changes the lighting state. All four submissions have five passes in the order required, one batched ground draw and one material draw.

The slope is only 7.956° from +Z; So its N is equal to `[-0.06189845,-0.12379691,0.99037516]`. For the main normal oracle ground dry/roughness=0: sharp specular contribution in these directions does not override diffuse contrast. Albedo is the same. Flat/slope N-dot `1 / 0.990375` in A and vice versa in B. RGB slope A `[149,173,156]`, B `[150,174,157]`; flat A `[150,174,157]`, B `[149,173,156]`. Assertions require both reversal and positive causal change of ** of each ** receiver. It's control of the norm, not acceptance wetness/specular.

The particular pair for the unchanging sun: window `[8,10,11] → [221,138,70]`, delta RGB `[213,128,59]`; the nonemissive facade `[8,10,11]` is unchanged. Emission is bright, but the ** window is not yet a connected source for adjacent surfaces**. UI reference `[37,211,149,255]` is accurate in all control states.

### The coordinates of the world and point/spot

Equivalent to geometry/albedo, sun intensity 0, ambient .025. Point light is transferred between the **source XYZ** centroid, to Z+1, radius 2.5, color/intensity `[1,.6,.2,5]`. The spot remains above the slope; only the direction down/up, the cone cosines, is changed. 6/.9.

| Control | RGB slope | RGB flat earth |
|---|---|---|
| Point over the slope | 127,117,62 | 5,7,5 |
| Point over the plain | 5,7,5 | 128,118,62 |
| Spot pointed at slope | 127,117,62 | 5,7,5 |
| Spot pointed from slope | 5,7,5 | 5,7,5 |

Weight RGB brightness change: slope +108.72, flat +109.65, spot +108.72; the range of each causal delta — 16. Point assertions check both within-frame reversal and direct cross-capture deltas. Ground outside the spot radius and UI remain accurate. This is an actual production GroundLighting response to P, **not occlusion/shadow or the entire source system **.

### Precision and metadata blending

A separate native readback production GroundGBuffer shader tested four constant cases with ±31.99/±32 and an affine interior pixel. Tight little-endian [RGBA16F raw buffer](../../artifacts/ground-forward-2026-10-07/native-final-05/ground-position-rgba16f.bin) — 1024 bytes. Max component error **0.00562477** for threshold .0078125; max Euclidean **0.00810665** for the threshold. 02. Therefore this fail-closed graph limits each camera-relative P component and reconstructed forward corners to **±32**. It's not the universal accuracy of any zoom/visible range; Legacy boundaries are not narrowed. Five interior controls do not provide accuracy for all surfaces.

A separate D3D12 output-merger test of the same material-forward shader checked two alpha coverages 64/255 and 153/255 on RG16F metadata: expected depth 38.3069496, actual 38.3125, error .00555038 < .03125. expected reveal .200313732, actual .200317383 < .002 error. Scene alpha straight, metadata coverage-premultiplied with `ONE / ONE_MINUS_SRC_ALPHA`. Native proof confirms the source alpha blend even in a two-component target; This is not a complete Composite/FoW proof. Context: [D3D12 per-target blend fields](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_render_target_blend_desc).

Both independent controls have the same backend/device/driver as the production probe; Assertion does not allow you to test another adapter imperceptibly.

### Resize, lifetime and presentation

Two sets, seven targets. Allocate RAII candidate → complete old slot fences → atomic swap. Repeat resize `1120×630 → 800×600 → 1280×720`: actual drawable/status/bytes are matched, generation grows `1→2→3→4`, IDs `1/2→3/4→5/6→7/8`, candidate_live=0. Strict 4:3 control cannot be omitted: both ground samples coincide, UI is accurate, upper/lower bands — `[6,9,14,255]`, calculated by an independent sRGB decode → linear exposure .72 → encode oracle.

Faults on creation attempts **15/21/28** — first, middle and last target candidate after 14 active creations. The exact injected-error prefix and attempt number are required. Both active IDs/dimensions/generation are stored, candidate_live=0; **both old-slot PNG byte-exact after rollback**. The next resize installs fresh sets; And their frames are also byte-equal. This is a check of owned GPU textures, not proof of absence of all driver allocations/leaks.

Immediate/Mailbox/VSync all advertised/applied and actually rendered a five-pass frame with exact reference pixels. It's not measuring display intervals, tearing or fluidity.

Base attachment ledger: 36 bytes/full-pixel/set; **66,355,200 bytes** for two sets in this 1280×720 control, **149,299,200 bytes ** computed in 1920×1080. Resize old+candidate doubles the base ledger. It's not measured VRAM: atlases, buffers, legacy resources and future reflection are not included here. GPU execution unavailable; capture/readback overhead is not used as FPS or GPU time.

## Collection, testing and regression

[Release build exit0](../../artifacts/ground-forward-2026-10-07/build-full-12.log); C4324 alignment warnings present, warning-free result not declared. [Snapshot](../../artifacts/ground-forward-2026-10-07/check-final-04/snapshot.json) has 355 hashed runtime files; [additional diagnostic copy](../../artifacts/ground-forward-2026-10-07/check-final-04/diagnostic-copy.json) — three files.

Apart from [target Ribterra is also collected, exit0](../../artifacts/ground-forward-2026-10-07/build-game-13.log). It's a local source build, not installed preview/release and not enabling a new graph in GameView. `Play Ribterra Rework.cmd` still indicates the conserved `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2`; It wasn't moved to the probe or the mid-engine.

[Final 16/16 CTest, 11.99 s](../../artifacts/ground-forward-2026-10-07/check-final-04/ctest-final.log): shader_contract, shader_negative, material_calibration_shader_contract, material_master_export, ground_forward_contract, game_art_outline, presentation_ui, render_frame, frame_timing, world_projection, render_batch, minimap_cache, presentation_contract, surface_lighting_contract, hdr_color_contract, material_bridge_contract. This is a selected suite, not a complete simulation/network suite. Software contracts do not replace native evidence.

[LastTest.log](../../artifacts/ground-forward-2026-10-07/check-final-04/runtime/Testing/Temporary/LastTest.log) maintains a negative result: **20** wrong register/space/output/ABI mutations rejected, **3** direct + **1** two-level include mutations detected, **2** angle/macro include forms rejected. The manifest contains 18 stages / 9 programs and exact current source/dependency hashes. The outdated early shader manifest was not used as a final proof.

Separate Topics GPU-Regressions have passed from runtime `check-final-02`: [RenderFrame](../../artifacts/ground-forward-2026-10-07/check-final-02/render-frame-gpu.log), [batching](../../artifacts/ground-forward-2026-10-07/check-final-02/render-batch-gpu.log), [surface lights](../../artifacts/ground-forward-2026-10-07/check-final-02/surface-native.log), [HDR](../../artifacts/ground-forward-2026-10-07/check-final-02/hdr-native.log), [material bridge](../../artifacts/ground-forward-2026-10-07/check-final-02/material-native.log). [Hash verifier](../../artifacts/ground-forward-2026-10-07/check-final-04/after-run-hashes.json) confirms that all six of these compatibility executables, including ReworkVisualProbe, are roughly the same as the final runtime; The only thing that changed was the new GroundForwardProbe.

[Fresh GameView quick matrix](../../artifacts/ground-forward-2026-10-07/check-final-02/gameview-native.log): 6 cases, 15 captures. [Full PNG ledger](../../artifacts/ground-forward-2026-10-07/legacy-comparison.json) — **15/15 byte-identical** versus retained material-bridge legacy-final-v3, including from F1 hide/restore. This is a regression of the old BakedCompat path, not a new HUD/V01–V03 path.

[After-run verifier](../../tools/verify_ground_forward_snapshot.ps1) has confirmed **355 runtime + 26 external command + 3 diagnostic files** without hash changes. Output isolation is not a hermetic source snapshot: transitive repository inputs are not frozen; The live test inputs on the final run were unchanged.

[Reuse verifier](../../tools/verify_ground_forward_reuse.ps1) repeated the probe in the busy output-root: [expected exit1](../../artifacts/ground-forward-2026-10-07/check-final-04/reuse-final/refusal.log), [all 37 files are unchanged before/after](../../artifacts/ground-forward-2026-10-07/check-final-04/reuse-final/reuse-report.json). The final set has **34 PNG + 2 JSON + 1 raw buffer**. The previous failures/snapshots/logs/fixtures is saved, nothing deleted.

## The view and extent of the artistic output

Reviewed unassembled final-target frame [point over the slope](../../artifacts/ground-forward-2026-10-07/native-final-05/point-above-slope.png), [point over the flat](../../artifacts/ground-forward-2026-10-07/native-final-05/point-above-flat.png), [spot](../../artifacts/ground-forward-2026-10-07/native-final-05/spot-aimed-at-slope.png), [emission](../../artifacts/ground-forward-2026-10-07/native-final-05/emission-on-same-sun.png), [4:3](../../artifacts/ground-forward-2026-10-07/native-final-05/resize-800x600.png). Assertion-only final hardening retains [34/34 PNG byte equality](../../artifacts/ground-forward-2026-10-07/native-04-05-comparison.json) with the previous revised set.

Separately reviewed [actual GameView](../../artifacts/ground-forward-2026-10-07/legacy-native/uk-85-1280x720-staged-selected-stockpile.png) and [target-0262](images/target-0262.png): the bases UI, earth/water materials, scene density and depth, scale and causal light still do not fully reproduce the concept. Test roms and cabin blocks do not replace the necessary wood, occupants, buildings, water and a full art kit. Native readback PNG is not a monitor scan-out or video. Native desktop-control API is not available in this session; user-authorized visible SDL/D3D12 probe windows were used, but desktop control is not declared.

Independent read-only source/evidence review: **APPROVE only this bounded slice, 0 open findings**. The hashes/number of files, causal deltas, raw precision, alpha oracle, resize IDs/rollback, presentation modes and Legacy equivalence are listed. It's not an artistic acceptance, it's not all E02 and it's no completion of a common goal.

## Identification and next boundary

| Input | SHA256 |
|---|---|
| GroundForwardProbe.exe | c91b5622694409e8cd2fd650102adf114ee328a6c9365d05e2243f270c5b41b9 |
| Production shader manifest | 904ab229eb79c49b78fbf10d12bfc9ef30dfefcd82ca57b955f3c857f0afbf4e |
| Unchanged material shader | 853120dad680adae861d3f0354c1efd4e8056778771cd28acdf0c10253ea1f09 |
| Editable master | cca93e76374baeeb1189cb2e1ff351b17b43c1566b20d53e3c7606b7ddd8e531 |
| Kit manifest / content ledger | 8b800ef5822d09c7eb2660ad89994b580d7dd99dbcc2cff07b56a7a6376e1f4e / 4be653ee31809ae1bcd73019e6a4c05c529f367d57429e4084526d4ea34eee33 |

The following is the full graph with Reflection/FoW, causal source linkage and blockers/shades, time/sun/moon/weather, material/water/atmosphere, FX, concept-faithful minimum kit, Dynamic GameView, actual quality presets and confirmation ≥120 FPS/flatness in 1080p. Receiver/shore/reveal metadata is stored here, but downstream effect/secrecy is not yet accepted. Do not continue gameplay or switch off the launcher until [full E01–E17 reception](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

Ponytail/ultrawork divided renderer, shaders, native proof and independent review; karpathy-guidelines/cpp-coding-standards affected additive opt-in scope, typed ABI, strict validation and RAII/fence lifetime. The user's requirements are not reduced. Commit/push/install/release and the new elevated profiler launch failed.

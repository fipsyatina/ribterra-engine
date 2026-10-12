# Geometry → material maps → direct GPU-control, 06.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: **partial engineered step, no receipt of graph or release**. The determined exporter with edited triangle geometry and native D3D12- controls passed. 15/15 selected CTest, 72 direct P/N- controls and 180 production color checks. **All E01–E17 are open; GameView remains BakedCompat, the compatibility of the concepts and ≥120 FPS has not been proven.**

## What is added

- [Edited master](../../art-source/engine-calibration/material-master.json): a diagnostic volume with two facades, a double-slope roof, a sloping surface and a window cover. It's a simple control geometry, **not a concept house and not a ready-made neutral art kit **. The normals are derived from the winding triangles, XYZ — from the barycentric interpolation; They don't get along with the old PNG.
- [Offline exporter](../../tools/export_material_master.py), Python standard library: fixed orthographic basis 58×29, one sample per texel center, frontmost camera-depth, integer-snapped bounds/pivot. Six coordinated `.rtex` channels and schema-2 off/on descriptors. Albedo is pretty much the same in both emission states. Master, exporter, camera and settings have separate SHA256.
- The boundaries of authored XYZ and reconstructed eight source/depth corners — ±8192 at anchor=0, geometry scale=1. Export scale 0.001...64 and depth endpoints canonised to float32 to raster/encoding/JSON; Endpoints cover geometry and cannot be wrapped in a single float. Source extent ≤2048; Random runtime anchors/scales and wide precision ranges are not accepted here.
- [Native probe](../../tools/material_calibration_probe.cpp) with separate `RGBA16F` position/normal targets. [Diagnostic shader](../../shaders/material_calibration.frag.hlsl) includes unchanged production material functions; The production shader and program manifest are not interchangeable. CTest checks two MRT outputs, `SpriteSurface` 160-byte ABI, b0/b1, and registers/spaces. Reflection declares five sampler slots; The optimized diagnostic DXIL actually uses the first three.
- GPU-resources probe have a separate owner, closed passes, fence waits and RAII retirement. Failed submit does not attempt to repeat the already consumed command buffer. The output-root cannot exist in advance; The results of the previous study are not replaced or deleted.

The Exporter does not perform semantic mip generation, atlas packing, multisample AA, model animation or full art-production validation. Caster mask is exported and accepted GameArt, but the shader shades in this step do not use it.

## Direct numerical proof of geometry

[Final native JSON](../../artifacts/material-master-2026-10-06/native-final/material-calibration-report.json), [actual exit0 log](../../artifacts/material-master-2026-10-06/check-final-v2/native-final.log). Windows / D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU, driver 32.0.15.9282, ** Actual drawable 1280×720**, build label 0.26.3. This is not a 1080p performance measurement.

12 safe internal controls × zoom 0.62/1.30/3.00 × origin/panned-elevated = **72 samples**. Panned case: camera XYZ [10.25, −6.125, 0], anchor [10.375, −6.5, legacy-elevation-4 converted Z], geometry scale 1.15. The JSON records the actual physical pixel center, camera-relative anchor, expected/GPU camera-relative XYZ, and normal. The CPU oracle intersects the camera ray with the real master plane and independently checks winding, canonical C++ projection, and barycentric coverage. Lighting color **is not used as proof of correct P**.

| Size | Maximum in these controls | The Port |
|---|---:|---:|
| XYZ, max absolute component error | 0.003792286 the tail | 0.02 |
| The angle GPU is normal versus the geometry is normal | 0.3189811° | 1.5° |
| The length deviation GPU normal | 0.000480533 | 0.001 |
| Reprojection in native drawable | 0.312011719 px | 1 px |

The **12 tightly packed little-endian RGBA16F raw buffers** are preserved. They are the numerical readback. Six decoded-normal PNGs only visualize the normals; they are not raw-float proof. Verification is limited to 72 internal samples, not all silhouettes, all DPI/scales, or arbitrary material descriptors.

## Production lighting and inspection

The same geometry kit is actually loaded through GameArt and painted production Renderer2D: **15 unassembled final-target PNG / 180 samples**. Three zoom × sun / point +X / point −X / emission off / emission on. RGB/alpha range — 4 byte values. Sun is normalized; point positions/colors, ambient, exposure and emission state recorded in JSON. Specular strength clearly 0: wet materials/flashes with this oracle have not been tested.

The final footage was reviewed: [sun](../../artifacts/material-master-2026-10-06/native-final/production-sun-300.png), [lamp + X](../../artifacts/material-master-2026-10-06/native-final/production-point-x-positive-300.png), [lamp -X](../../artifacts/material-master-2026-10-06/native-final/production-point-x-negative-300.png), [emission on](../../artifacts/material-master-2026-10-06/native-final/production-emission-on-300.png), [decoded normal preview](../../artifacts/material-master-2026-10-06/native-final/origin-300-decoded-normal-preview.png). You can see a change in the lighted side without changing the albedo and a separate glare of the window. **The window is not yet a source of lighting for nearby objects; shadows/occlusion is unproven.**

These simple control surfaces do not intentionally repeat the detail, materials, and palette [target-0262](images/target-0262.png). They can't be counted as V03 art acceptance. Native readback PNG — not a screenshot of the monitor scan-out, not a motion video, nor proof of desktop control; native desktop-control API is not available in this session.

## Repetition, testing and preservation

[Release build, exit0](../../artifacts/material-master-2026-10-06/build-final.log). There is an alignment warning C4324; warning-free or full suite results are not declared.

[Snapshot](../../artifacts/material-master-2026-10-06/check-final-v2/snapshot.json): 330 runtime files. Diagnostic shader/JSON/contract additionally copied to `runtime/calibration-shaders`; [3 matching SHA256](../../artifacts/material-master-2026-10-06/check-final-v2/diagnostic-copy.log). After the [project](../../artifacts/material-master-2026-10-06/check-final-v2/after-run-hashes.log) all 330 files and 26 external command files remained hash-identical. Output isolation ** is not a hermetic source snapshot**: Python tests, exporter/master and transitive inputs remain live; They were not changed in the final race.

The following are the results of the [CTest 15/15, 14.59 with ](../../artifacts/material-master-2026-10-06/check-final-v2/ctest-final-v2.log), [ LastTest.log](../../artifacts/material-master-2026-10-06/check-final-v2/runtime/Testing/Temporary/LastTest.log): `shader_contract`, `shader_negative`, `material_calibration_shader_contract`, `material_master_export`, `game_art_outline`, `presentation_ui`, `render_frame`, `frame_timing`, `world_projection`, `render_batch`, `minimap_cache`, `presentation_contract`, `surface_lighting_contract`, `hdr_color_contract`, `material_bridge_contract`. Software contract testing is not native lighting proof.

[Final exporter report](../../artifacts/material-master-2026-10-06/export-tests-n/test-report.json): two exports of 10 files are nearly identical; **14 exact negative cases** return exit2 with the desired cause, leave no partial output and retain the existing-output sentinel. The separate regression shows raw-double ePPP snapped extent 832×644 versus runtime-float32 831×644; The exporter uses the second. The registered CTest repeated the exporter's verification in its own new retained directory.

Independent read-only source/evidence review  **APPROVE for this bounded slice**: re-calculated snapshot/kit/shader hashes, number and file size, all geometric maxima and production-color deltas (maximum 1 for threshold 4). The [CTest-generated exporter report](../../artifacts/material-master-2026-10-06/check-final-v2/runtime/material-master-test-artifacts/material-master-8mm6icy0/test-report.json) also has PASS / 10 deterministic files / 14 negative cases. It's not an art reception or a full suite.

A native reboot to an already occupied output-root returned [ the expected exit1](../../artifacts/material-master-2026-10-06/check-final-v2/reuse-final/refusal.log); The [complete before/after SHA256 ledger of all 34 unchanged files ](../../artifacts/material-master-2026-10-06/check-final-v2/reuse-final/reuse-report.json) is saved by a separate [reuse check](../../tools/verify_material_calibration_reuse.ps1). The initial one-line hash summary is also saved, but it is not itself this complete ledger. Interim failed/preliminary snapshots, logs and fixtures are saved. The initial `ctest` is not found in PATH; `ctest-final.log` is not a green proof — specified `ctest-final-v2.log` created by the actual launch of the absolute executable.

## Identification of the person

| Input | SHA256 |
|---|---|
| MaterialCalibrationProbe.exe | `734adfd9f950633e1ac50bf3ce3248be86580bd2fe598ca8cebc9ee7c30d384e` |
| Editable master | `cca93e76374baeeb1189cb2e1ff351b17b43c1566b20d53e3c7606b7ddd8e531` |
| Exporter | `e3b8e5898b4ffea08886dc6b1f659aa9ebab5c31759a46807e7b20cadb766911` |
| Exported kit manifest file | `8b800ef5822d09c7eb2660ad89994b580d7dd99dbcc2cff07b56a7a6376e1f4e` |
| Exported kit content ledger | `4be653ee31809ae1bcd73019e6a4c05c529f367d57429e4084526d4ea34eee33` |
| Diagnostic DXIL | `ef122e74c51e05c67e05d0e346c7c0b8a8acd5acc6bc6c695f6f4873b0b36a22` |
| Production material HLSL | `853120dad680adae861d3f0354c1efd4e8056778771cd28acdf0c10253ea1f09` |
| Production shader manifest | `d55f4e1327682af5a2a5d535ddee44f9aed833d4daf00e483fc4535d84fa7ce8` |
| Ribterra.exe | `9569dd1db478b99ee03fadece0569a68ae70b21a8f36b5830dbdac22ed565d4b` |

Production executable/material shader/manifest not changed from [material-bridge v3](MATERIAL_BRIDGE_CHECK_2026_10_06_UA.md); His 15-image legacy regression doesn't appear to be a new launch at this step. Launcher is not overloaded. Commit/push/install/release and the new elevated profiler launch failed. No file has been deleted.

## What's left to play

The following dependence — is real production GroundGBuffer → GroundLighting → WorldForward, with real slope receiver, long-lasting surface metadata, two frame sets, and the retention of a single Composite/UI. This probe MRT ** is not a realization of this graph**. Reflection, FoW secrecy, blockers/shadows, sun/moon/time/weather, water/atmosphere, wet materials, minimal concept-faithful art kit, Dynamic GameView, temporal captures and 1080p ≥120 FPS remain open.

The [specific next implementation plan](../../.omx/plans/2026-10-06-ground-forward-slice-ua.md) is saved with ownership, pass/attachment contract, lifetime, memory ledger and native A/B/A/B proof. It's a plan, not a finished graph.

Ponytail/ultrawork helped separate exporter work, native proof, and independent review; karpathy-guidelines/cpp-coding-standards helped maintain additive scope, typed boundaries, and resource lifetime. The user's requirements did not diminish. The full scope is [E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md); generator work, new mechanics, and the subsequent HUD do not continue until it is accepted.

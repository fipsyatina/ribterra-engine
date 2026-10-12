# Material bridge — partial engine step, 06.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: **local engineering control, not graphics acceptance**. Added typed textures, a separate lit sprite path and schema-2 uploading materials to production Renderer2D/GameArt. 13/13 selected CTest and native material testing have passed. **E01–E17 open; Dynamic scene, concept compatibility and ≥120 FPS have not been proven.** Normal GameView still uses BakedCompat; The launcher is unchanged.

## Implemented behavior

- The six mandatory channels are albedo `RGBA8_UNORM_SRGB`, normal/material `RGBA8_UNORM`, camera-depth `R16_UNORM`, emission `R8_UNORM`, surface mask `RGBA8_UNORM`, caster mask `R8_UNORM`. The format/usage is verified on a particular GPU; The software backend explicitly rejects this path.
- Albedo is decoded by the sRGB sampler before bilinear filtering; There's no repeated shader decode. Data maps remain linear. `.rtex` carries dense raw bytes, R16 little-endian, without the 8-bit PNG- decoder.
- Oct-normal, AO, reconstructed position, ambient, sun, up to four point/spot sources and individual emission come into the RGBA16F world. Composite and byte-authored UI remain separate. Wetness/roughness/specular prepared in code, but full native wet-material control is still needed.
- The per-channel texel-center clamp with the actual dimensions isolates the atlas rectangles. Valid UV are attached to whole texels; The permitted float roundoff is canonized. The constant material descriptor contains UV/emission; Caller-side change of their copies does not change the material.
- Typed textures cannot be entered into a legacy draw or used a legacy-decoded texture as a data channel. Handles have generation; stale textures/materials are rejected. The material is not installed without an accessible HDR path.
- Schema 2 checks family/camera/revision, hash/bytes, channels/formats, coordinated rectangles, source size/pivot/export scale and depth range before GPU- loading. The entire candidate is committed only after success. The schema 1 remains BakedCompat; `ready()/contains()`  legacy routing only, `contains_lit()/lit_sprite()`  the explicit new path.
- GPU ABI: `SurfaceLighting` 384 bytes / fragment b0 space3 and `SpriteSurface` 160 bytes / b1 space3; Five sampled channels. **Caster mask is still validated and preserved, but not consumed by the shadow shader.** The full program/render-graph schema from Core spec is not yet complete.

## Coordinates and Restrictions

The combined adapter — `include/ribterra/world_projection.hpp`: reference tile 58×29, half extents 29/14.5, `renderZ = legacyElevation × 2.4 / 29`. Camera-depth — dot(XYZ, normalize(1,1,1)); inverse affine basis is the same in terms of content in CPU and material shader. The pixel source relative to the pivot is translated through the export scale, then the geometry scale and the camera-relative anchor. It's not a screen-space spot of lighting.

CPU `world_projection` checked the roof/façade/feet/terrain points, R16 range −96...96, zoom 0.62/1.30/3.00, source-pixel rounding, ≤1 px / ≤0.02 tile in these controls, and representable offsets at large coordinates. **This is not GPU reprojection proof for the real house/resident master.** Wider depth ranges/scales require a separate precision audit; do not extend these CPU tolerances to arbitrary descriptors.

The Legacy compatibility adapter keeps the historical order of float computation and zoom multiplication in the same affine basis. Reassociation in intermediate v2 changed the coordinates less than to 0.0005 px ** in the legacy matrix/fixture coordinates** analyzed, not in the full arbitrary finite range, and rare RGB bits. V3 adds bit-exact CPU controls, particularly for cached-origin translation; The native matrix below confirms the recovery of the previous 15 PNG. The new 3D/material adapter is not replaced by legacy projection.

After an independent review, **only the CPU test was extended**: asymmetric camera pan and the exact sequence viewport-base → cached-origin subtraction → vertex append. Additional [build](../../artifacts/material-bridge-2026-10-06/build-a/projection-hardening-build.log) and [direct run](../../artifacts/material-bridge-2026-10-06/build-a/projection-hardening-check.log) — exit0. This is a separate test, not a new 13 test snapshot; production exe/shaders and the native captures listed remained the same.

Sprite source dimensions ≤4096, export scale 0.001...64, geometry scale (0...64], anchor/depth/reconstructed corners within ±8192. There are eight affine corners. Radiance inputs ≤64, specular strength ≤0.99: upper arithmetic boundary with five sources, wet multiplier, ambient/emission — 65190.4, below the finite RGBA16F limit 65504. The old surface-kernel coordinate bound is not narrowed by this step.

Loading schema 2 **startup-only**, even switching from it to schema 1 after submit is rejected. Hot reload/fence-safe retirement remains an open requirement, rather than an assumed success. All new maps are base-level-only; The semantic mip generation/coverage policy is not implemented.

## Current evidence

[Snapshot 329 hash-verified runtime files](../../artifacts/material-bridge-2026-10-06/check-final-v3/snapshot.json), [build](../../artifacts/material-bridge-2026-10-06/build-a/build-final-v3.log). Output-isolated, **not hermetic**: Python/source/toolchain inputs remain live, after final assembly until the launch were unchanged. Ninja Release / MSVC; warnings alignment and previously existing view/UI warnings are not declared corrected.

13/13 CTest passed in 12.85 seconds: `shader_contract`, `shader_negative`, `game_art_outline`, `presentation_ui`, `render_frame`, `frame_timing`, `world_projection`, `render_batch`, `minimap_cache`, `presentation_contract`, `surface_lighting_contract`, `hdr_color_contract`, `material_bridge_contract`. [Aggregate CTest log](../../artifacts/material-bridge-2026-10-06/check-final-v3/ctest-final.log), [LastTest.log](../../artifacts/material-bridge-2026-10-06/check-final-v3/runtime/Testing/Temporary/LastTest.log). It's not a full suite; material/HDR software tests prove validation/rejection, not native shader output. 12 shader register/space/output/ABI mutations are rejected after a valid baseline.

Native MaterialBridgeProbe: **actual exit0, 22 unedited final-target PNG**, Windows/D3D12, RTX 3050 Laptop, driver 32.0.15.9282, drawable 640×480, build 0.26.3. [JSON from samples/counts/negative cases](../../artifacts/material-bridge-2026-10-06/native-final-v3/material-bridge-report.json), [log](../../artifacts/material-bridge-2026-10-06/check-final-v3/native-final.log). The input numbers light/exposure and validation bounds here are not all serialized; source constants are attached to the hash of the final executable, JSON is not a self-contained iteration of all inputs.

| Control | The actual RGB |
|---|---|
| Encoded gray128, one decode/encode | 128 / 128 / 128 |
| Black/white bilinear midpoint | 188 / 188 / 188 |
| Gray128/white midpoint | 205 / 205 / 205 |
| Linear AO128 and emission mask128 | 188 / 188 / 188 |
| Straight alpha128 over the black | 188 / 188 / 188 |
| +X/+Y normal under the corresponding sun | 255 / 255 / 255 |
| -X/−Y normal under the same sun | 0 / 0 / 0 |
| Near/far R16 depth under one point light | 220 → 113 |
| Emission off/on, unchanged albedo hash | 0 → 188 |
| UI over the world | 31 / 157 / 219 without grading |
| Right-edge albedo near the white texel | 128, no bleeding |
| Right-edge AO255 near AO0 | 255, no bleeding |
| Right-edge emission128 near mask255 | 188, no bleeding |

Each sample has alpha255. Direct negative controls: missing/wrong-format/stale bindings, fractional UV, legacy data binding and typed legacy draw. Six invalid manifest-valid schema-2 candidates are saved; The old catalogue remains intact. Fault positions 1/3/6 new load — absolute attempts 7/9/12 after six baseline textures — accept only exact injected error; After each fault, the old catalog was actually rendered/captured/decoded. The caller UV/emission mutation does not affect the stored material.

Separate current native HDRColorProbe (34 PNG), SurfaceLightingProbe (18 PNG), RenderFrameTests/text+buffer recovery and RenderBatchTests (1 PNG) — exit0. [HDR report](../../artifacts/material-bridge-2026-10-06/hdr-final-v3/hdr-color-report.json), [surface report](../../artifacts/material-bridge-2026-10-06/surface-final-v3/surface-lighting-report.json), [frame log](../../artifacts/material-bridge-2026-10-06/check-final-v3/render-frame-native.log), [batch log](../../artifacts/material-bridge-2026-10-06/check-final-v3/render-batch-native.log).

## GameView and visual acceptance

Compared to the previous HDR checkpoint: 34/34 HDR PNG and 18/18 surface PNG byte-identical corresponding captures. It's a check of these control scenes, not the overall GameView or performance.

The recent Legacy GameView/RmlUi/D3D12 matrix has completed exit0, 15 PNG / 6 locale-resolution-scale cases. F1 hidden clears HUD commands/annotations; F1 restored **byte-identical to the corresponding staged-selected-stockpile frame**, not to any other ordinary fixture. [Report](../../artifacts/material-bridge-2026-10-06/legacy-final-v3/report.json), [native log](../../artifacts/material-bridge-2026-10-06/check-final-v3/gameview-native.log).

Comparison of v3 to the previous `artifacts/hdr-color-2026-10-06/legacy-02`: **15/15 same-name PNG byte-identical to SHA256**. The intermediate v2 with minor RGB- differences is preserved, but it is not a final proof. Fresh summer/winter/F1-hidden actual footage and target-0262 reviewed: the live world/HUD still does not fully fit the concept. Simple analytical panels demonstrate shader operations, not the artistic appearance of the home/person. Readback PNG is not a monitor scan-out or video; native desktop-control surface available in this session is not supported.

## Identification of the final material build

| Input | SHA256 |
|---|---|
| Ribterra.exe | `9569dd1db478b99ee03fadece0569a68ae70b21a8f36b5830dbdac22ed565d4b` |
| MaterialBridgeProbe.exe | `4e6222a76bbc5eb0bbe9836b78c2cd4cffff55ded6e9291877bba8a3c22a51b8` |
| ReworkVisualProbe.exe | `fbd987800676342013ebe0c009b28ee547d9ddf41262ee11ff44d9d65af7e583` |
| Shader manifest | `d55f4e1327682af5a2a5d535ddee44f9aed833d4daf00e483fc4535d84fa7ce8` |
| Asset content | `e428def3f77bf6f1a5365dc21bda9d2c57f9962c47d0afe79935012f7a06121c` |

Interim build logs, snapshots, fixtures, native-a/native-edge are saved. Preliminary check-a had a stale source/manifest failure during subsequent shader changes; It's not used as green proof. Build/test files are not deleted. The launcher still points to preview-2026-10-05-r2, not this probe. It's not install/release/commit/push.

## What's next, until the game continues

1. [Added direct native GPU calibration P/N to the edited geometry master](MATERIAL_GEOMETRY_CALIBRATION_CHECK_2026_10_06_UA.md): 72 interior samples. This is a diagnostic cabin/slope, not a concept-ready house/resident kit; The regression compatibility adapter is closed by the above matrix.
2. Minimum neutral house/store/workshop/tree/resident+body/cargo/terrain/water/bridge kit for Art spec with real normal/depth/emission/masks and production GameView receiver integration. The current `source_hash` only declares identity; The full master/export provenance is not yet here.
3. Full Core graph, independent sources/occlusion, contact and directional shadows, sun/moon/time/weather, wet materials, water/reflections, spatial atmosphere and seasonal effects.
4. Temporal concept matrix, effect-preserving quality settings and profiling ≥120 FPS at 1920×1080 at RTX 3050. This capture-based control doesn't measure performance.

Ponytail/ultrawork divided by projection/material/probe/review; karpathy-guidelines/cpp-coding-standards is applied to typed boundaries, local scope and candidate RAII. Open requirements were not reduced. The direct requirement of engine-first takes precedence over the old deferred-Dynamic order of documents.

Technical references: [Direct3D sRGB conversion-before-filter](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#FormatModifiers), SDL [format/usage query](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat), [tight transfer layout](https://wiki.libsdl.org/SDL3/SDL_GPUTextureTransferInfo), [GPU upload timeline](https://wiki.libsdl.org/SDL3/SDL_UploadToGPUTexture). The full range is: [E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

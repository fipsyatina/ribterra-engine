# Ribterra — Render Core technical specification for 0.26.1

> English translation of a historical project document. Technical literals and recorded results are preserved; these records describe their original snapshots and do not validate the current build. Original wording is available in import commit `7388a2f`.

**New E10 contract 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md) adds 3D distance/morning/coastal density in GroundLighting, WorldForward, and Reflection source alpha, not in Composite. The opt-in `WaterAtmosphereSlice` uses a SurfaceLighting384 prefix + Atmosphere640 =1024-byte b0; it adds no attachments or uniform slots. Higher-level engine gates remain OPEN; the contract is not native or performance acceptance evidence.

**New bounded follow-up 08.10:** [raw binary WaterCoverage / odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md), fresh build02/native05–06, records actual GPU tuples and fault36/retry54/restore72. It does not cover intermediate AA, every recovery failure, normal startup, or full acceptance. Checkpoint records are preserved as their historical source cohorts.

**Current bounded checkpoint 08.10:** [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) runs in the shared native renderer through an opt-in GameView adapter. Its [single contract](WATER_REFLECTION_V1_CONTRACT_UA.md) defines a new graph: 5 MRTs, a separate RGBA8 coverage carrier, and half-resolution HDR Reflection; older graphs and Contact V1 W=0/1/2 remain unchanged. This is technical E09 proof, not concept-art acceptance. E01–E17, normal startup, AA, input, video, presets, and ≥120 FPS remain OPEN.

**Active partial authored checkpoint 08.10:** The [GameArt sky-triangle bridge](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md) verifies independent schema-2 local XYZ triangles, an explicit world-to-camera adapter, and frame-lifetime/registry behavior. It is not yet an authored GPU shadow or full Render Core acceptance. E01–E17, GameView, art, presets, and ≥120 FPS remain OPEN.

**Active partial checkpoint 08.10:** [Sun/Moon SkyShadowSlice](SKY_SHADOW_CHECK_2026_10_08_UA.md) adds source-specific D32 depth passes to the opt-in production renderer, limited to bounded fixed-camera opaque triangles. This is shadow-only geometry, not a transition of the entire game to 3D. The historical prohibition on a depth target below does not describe this new prerequisite; world HDR/UI and old graph outputs remain preserved. E01–E17, authored art, GameView, the full suite, presets, and ≥120 FPS remain OPEN; the engine-first order takes priority over the older scaffold/deferred plan.

**Historical decision 27.09, not the current bounded opt-in graph:** The full graph below describes the future Dynamic mode and scaffold API. No release number is assigned, and normal startup does not use it. The current order is [full engine before game integration](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md), with actual partial evidence recorded above. [DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) and [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md) are retained as roadmap/algorithm documents, not readiness confirmation.
**Date:** 27 September 2026.
**Status:** **PROPOSED / UNMEASURED**. This is an implementation plan; the described passes, GPU layouts, and new effects do not yet exist at runtime.
**Goal:** establish a controlled 2.5D renderer on the current SDL_GPU/D3D12 path, from small acceptance samples through mass asset production.
**Main Plan:** [Graphics Engine Plan V2](GRAPHICS_ENGINE_PLAN_V2_UA.md).
**Public contract:** [World FX](WORLD_FX_SPEC_UA.md).
**Primary SDL sources:** links and scope are listed in [section 12 of the main plan](GRAPHICS_ENGINE_PLAN_V2_UA.md#12-primary-technical-sources).

## 1. Decision and boundaries
The working architecture is a 2.5D renderer with a fixed isometric camera, painter ordering for sprites, and separate bounded passes for materials, local light, and World FX.
This decision remains in effect until concrete proof demonstrates a specific insurmountable limitation.
Concept-art terminology alone does not authorize replacing the engine or moving to a full 3D scene.
The future Dynamic mode plans:
- dynamic lighting on terrain, facades, and residents using normal/depth material data and separate emission; contact shadows for every resident and authored directional silhouettes; local-light occlusion through OBB/capsule-prism approximations;
- wetness response on eligible ground receivers; a flat river, bounded reflections, shoreline masks, and World FX; world-space haze that does not replace Fog of War;
- rain/snow particles and cosmetic footprint/compressed-snow shading; one viewport/DPI/input contract for the world and RmlUi; schema 2 for transactional material upload and a schema 1 adapter.
Not included:
- a depth target, perspective camera, or full 3D geometry; full deferred PBR, GI, ray tracing, SSR, or a full-frame planar capture; rigid-body, cloth, or fluid physics;
- physical snow deformation or collisions; waterfalls, currents, multiple water levels, or simulation feedback from the renderer; automatically rewriting the engine after one shader experiment fails.

## 2. Verified current baseline
The table below is the **historical source snapshot from the initial plan**, not the current state after engine-first work on 05–07.10.2026. [GroundForward](GROUND_FORWARD_CHECK_2026_10_07_UA.md) and [local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md) document the active production opt-in foundation and open boundaries; normal GameView remains BakedCompat. The table is preserved to retain the provenance of the earlier plan.
| Location | Current fact | Required change for V2 |
|---|---|---|
| `include/ribterra/render_frame.hpp` | `Frame2D` has 2D vertices, UVs, RGBA8 color, texture handles, and painter commands routed through Terrain/WorldOrdered/Atmosphere/Fog/Ui | No material, light, receiver, decal, or reflection contracts |
| `src/render/renderer2d.cpp` | GPU path creates one `R8G8B8A8_UNORM` offscreen target | Requires new attachments, pipelines, and lifecycle management |
| `src/render/renderer2d.cpp` | Two allowed frames in flight; each frame has fence and upload buffers | New attachments also have two independent sets |
| `shaders/sprite.vert.hlsl` | Only the `logical_extent` uniform | New programs need their own exact binding contracts |
| `shaders/sprite.frag.hlsl` | Output is sampled texture × vertex color | A PNG alone cannot create wetness, occlusion, or a bounded reflection |
| `tests/shader_contract.py` | Manifest schema 1 expects one sampler in fragment and one uniform buffer in vertex | The test and manifest must expand by program |
| `src/client/game_art.cpp` | Loader accepts only `world.json` schema 1 and loads an RGBA atlas page | Schema 2 must be transactional; the old catalog uses an adapter |
| `include/ribterra/world_projection.hpp`, `src/client/view.cpp` | Shared affine projection uses a reference 58×29 diamond; view still has logical 1440×900 | 1440×900 cannot remain a hidden DPI contract |
| `src/client/main.cpp` | After `GameView::compose`, the frame receives 1440×900 again before UI is added | World, UI, and input must move to one drawable-pixel viewport |
| `src/render/renderer2d.cpp` | Pointer events are converted through window/pixel/logical scaling | After V2, convert window→drawable exactly once |
The software backend is useful for command-generation tests.
It does not prove DXIL bindings, HDR targets, blending, mask leakage, timing, or pixels from the actual D3D12 renderer.

## 3. Canonical coordinate spaces
### 3.1. One world metric
All V2 world interfaces use `render_xyz` in tile-equivalent units:
- `render_x` and `render_y` — position in the cells of the world; `render_z = legacy_elevation × 2.4 / 29.0`; camera-relative coordinates are calculated on the CPU before float32 upload;
- `screen_y` subtracts `29 × render_z × effective_zoom`; lights, occluders, decals, reflection casters, and receivers use the same metric; the shader does not infer height from painter order or alpha.
Current authored footprint polygons are converted to OBB or capsule-prism approximations during frame preparation.
The original polygon remains a CPU validation/debug source; the GPU does not receive an arbitrary polygon solver.
### 3.2. Camera and zoom
Canonical scale:
`effective_zoom = user_zoom × drawable_height / 900`.
`drawable_height` is the current frame's physical drawable-pixel height, not window points or the old logical 900.
Subtract the camera origin before upload so large world coordinates do not degrade float precision.
Control zoom levels for dev.09:
- `0.62` — wide overview and stress test for mask seams; `1.30` — primary gameplay frame; `3.00` — leakage, anchor-drift, and authored-silhouette checks.
### 3.3. World depth without a depth buffer
Painter order remains authoritative for sprites.
`world_meta.r` contains a camera-relative depth metric for haze and diagnostics, but does not enable depth testing.
Cliffs, roofs, and the bridge overlap world objects through stable ordering and receiver/occlusion masks.

## 4. Proposed CPU frame contract
`Frame2D` remains the top-level frame container, but receives V2 world inputs.
All types below are **PROPOSED interface names**, not existing header declarations.
| Type | Minimum fields | Owner and policy |
|---|---|---|
| `WorldFxFrame` | presentation time, epoch, rain/snow/haze, wind, temperature, snow cover, quality | Client projection of authoritative state; renderer does not change simulation |
| `MaterialDesc` | material id, albedo, normal/material map, camera-depth map, emission map, receiver flags, alpha mode, color space, dry/wet response | GameArt schema 2; immutable during submitted frame |
| `LocalLight2D` | stable id, render XYZ anchor, radius, direction/cone, linear RGB/intensity, receiver mask, occluder range | GameView/renderer preparation; window light is not fullscreen glow |
| `Occluder2D` | stable id, OBB or capsule prism, base/top render Z, shadow class, reveal | Prepared from an authored footprint; not a physics body |
| `GroundDecal2D` | stable id, render XYZ anchor, axes/extent, material/decal id, opacity, receiver mask | Contact shadow or footprint; cosmetic only |
| `ReflectionCaster2D` | stable id, sprite/material, anchor/bounds, water plane, strength, reveal | Only eligible visible casters; UI and particles are excluded |
Frame preparation preserves stable ordering.
No effect input receives a callback into SimCore.
Reveal filtering occurs before GPU buffer upload.

## 5. Material and receiver contract
### 5.1. `MaterialDesc`
Every world draw references a material ID instead of an arbitrary set of texture handles.
Required semantic bindings:
| Binding | Color space | Alpha | Purpose |
|---|---|---|---|
| albedo | explicit sRGB decode to linear | straight coverage | neutral base color |
| normal/data | linear | linear data | authored normal XY, roughness/wet eligibility for approved packing |
| emission | sRGB RGB → linear; mask linear | straight mask | emissive RGB/strength without another night-darkening pass |
| camera-depth | linear R16_UNORM + range | data | sprite surface reconstruction using the affine basis from the Dynamic Lighting specification |
| mask | linear | linear data | receiver, water, shore, snow or decal masks |
| UI atlas | legacy byte path | legacy premultiplied | UI pass only, no world grading |
World targets and all lighting calculations operate in linear color.
Texture creation does not rely on implicit swapchain decoding.
Albedo RGB is decoded from sRGB exactly once.
Mask, normal, and roughness values never pass through an sRGB transform.
World albedo sources use straight alpha.
Before linear alpha blending, the shader either produces premultiplied output or the pipeline explicitly uses straight-alpha blending; one draw cannot mix both conventions.
The manifest fixes the convention for each program.
The UI retains its current premultiplied bytes and corresponding blend path.
### 5.2. Ground data and world metadata
Four Ground G-buffer targets:
- ground_albedo RGBA8_UNORM_SRGB: RGB neutral albedo, A contact/AO visibility; shader outputs linear, target encodes, sampler decodes exactly once.
- surface_world RGBA16F: camera-relative render XYZ; W semantics depend on the explicit graph version described below. Do not treat the contact tag as coverage.
- surface_material RGBA8_UNORM: RG oct normal, B roughness, A sun visibility.
- surface_flags RGBA8_UNORM: R receiver bitfield, G wetness, B shore01 for Water or snowCover01 for land, A reveal.

Flags use point sampling; coverage/material sampling follows the corresponding program contract. A contact shadow writes only ground_albedo.A; a directional silhouette writes only surface_material.A. Blending multiplies destination visibility without changing RGB or normal data. A footprint changes only approved albedo/normal/roughness channels, not world position, class, or reveal.

**Versioned Contact V1 (2026-10-07; partial GPU proof, not full engine).** See the [current report](CONTACT_GPU_CHECK_2026_10_07_UA.md). GroundForwardSlice leaves SurfaceWorld.W=`0` for clear/invalid and `1` for valid; existing readback assertions are not weakened. A separate opt-in GroundContactSlice uses W=`0` invalid, `1` ordinary valid, and `2` eligible valid. The world target is opaque with no blending and point/clamp sampling. Contact accepts exactly W2/reveal1; new Lighting accepts only W1/2. All 8 local bits and SurfaceFlags RGBA remain unchanged. Typed Ordinary/Flat/Slope/Bridge data is transported through a copy of GroundMaterialGpu.flags[1] only in the new graph; caller-side reserved fields and the entire old lane remain zero. Native paired controls prove receiver/reveal/plane exclusions, but there is no new raw-W readback.

This bounded contact version **does not support Reflection/Water** and does not store antialiased coverage in W. The previously described future coverage semantics belong to a separate full-engine graph, not this version. Before full-engine Reflection, a separate versioned coverage carrier requires native edge proof and an updated memory/resize/performance ledger; no specific carrier has been accepted yet. Do not use `saturate(W)`, interpolation, or the contact tag as coverage. This does not remove the Reflection requirement and is not full-engine acceptance.

ContactSurfaceGpu80: anchor/opacity, U/halfExtent, V/halfExtent, canonical texel-centre UV, pivot/planeTolerance/reserved0; offsets 0/16/32/48/64. Prepared Draw112 with zero padding, a noncentral pivot, orthonormal tangents, extents≥1e-4, normalized plane band **[.02,.04]**, all four corners±band within ±32, and normal.z≥.5. The old CPU prototype minimum 1e-4 is historical, not the GPU tolerance. This is renderer policy, not terrain traversal/buildability. Require reveal1, opacity(0,1], known facing, nonzero stable/owner, and unique stable IDs; one owner may have several parts. Cap128 is provisional; overflow rejects. The registry checks an alive generation, R8, integer rect/zero gutter, and canonical UV/facing before command acquisition; cold release waits for idle and does not wrap a live generation. Projection/scissor/GPU pass are implemented; GameArt filtering/adapter and concept kit are not.

Contact pass: fullscreen triangle and an eight-corner bounded physical scissor from the shared canonical projection. Reopen only ground_albedo with LOAD/STORE; use nearest World/Flags plus a linear-clamp R8 mask at LOD0. A-only write mask with alpha ZERO/ONE_MINUS_SRC_ALPHA/ADD: newAO=oldAO*(1-mask*opacity), with no active-target reads or RGB changes. The existing 7 attachments/lighting384 remain; the graph has 5 passes when empty and 6 when populated. Registry limits are ≤256 live textures/16 MiB logical R8, dimensions 3..1024, and ≤1 MiB per art item; texel-centre endpoints must lie in a declared rect≥3×3 with a zero outer ring and nonzero interior. Allocation/upload failures publish nothing; handle-only frames perform stale checks before GPU work. Native 53 checks×2, 211 CPU checks, and shader contracts provide partial proof, not full engine/FPS/art/motion acceptance.

world_meta RG16F is created in GroundLighting: R haze depth, G reveal. World sprites then write depth/reveal for the actual visible fragment. For coverage, output=float4(depth*coverage,reveal*coverage,0,coverage) with ONE/ONE_MINUS_SRC_ALPHA blending. A native backend test must prove source-alpha use for RG16F; otherwise promote to RGBA16F with a new memory ledger. World must not sample world_meta while writing it.

## 6. Explicit GPU layouts
When an SDL shader program receives a `StructuredBuffer`, the CPU buffer must match the HLSL fields by offset and size.
Do not cite `std430`; it is not the SDL/DXIL StructuredBuffer contract.

Production clarification 07.10: pinned SDL 3.4.16 D3D12 creates a RAW storage SRV, while pinned shadercross 3.0.0-preview.2 produces byte-addressed DXIL through SPIR-V round-trip lowering. Therefore, the shader contract must verify either the exact reflected struct or the corresponding RAW resource handle, `index << 6`, loads at 0/16/32/48, and byte-count → record-count `/64`. Merely declaring a struct in the original HLSL is insufficient. Native proof and boundaries: [local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md).
### 6.1. `LightGpu` — 80 bytes
| Offset | HLSL field | Value |
|---:|---|---|
| 0 | `float4 anchor_radius` | camera-relative render XYZ, radius |
| 16 | `float4 direction_cone` | normalized direction XYZ, outer cosine |
| 32 | `float4 color_intensity` | linear RGB, intensity |
| 48 | `uint4 occluder_range_flags` | first occluder, count, receiver mask, stable id |
| 64 | `float4 height_falloff_reveal` | inner cosine, falloff exponent, reveal01, reserved=0 |
CPU declaration has `alignas(16)` and five 16-byte members.
Mandatory compile-time checks: size 80; offsets 0/16/32/48/64.
The HLSL declaration has the same five members in the same order.
### 6.2. `OccluderGpu` — 64 bytes
| Offset | HLSL field | Value |
|---:|---|---|
| 0 | `float4 center_base` | center render XY, reserved Z=0, base height |
| 16 | `float4 axis_x_extent` | normalized XY axis X, half extent X / capsule half length, top height |
| 32 | `float4 axis_y_radius` | normalized XY axis Y for OBB, half extent Y, capsule radius |
| 48 | `uint4 shape_flags_id` | shape 1=OBB / 2=capsule, fully-revealed flag=1, stable id, receiver owner id |
CPU declaration has `alignas(16)`, size 64 and offsets 0/16/32/48.
Capsules use axis X + half length + radius; axis Y/extent Y remain zero.
OBBs use both axes and extents; radius is zero.

**Versioned clarification of the reserved word, 07.10.2026:** `shape_flags_id.w` in the production GroundForward local-visibility extension is a nonzero owner ID, not reserved zero. Only the matching nonzero receiver owner excludes its own approximation from local visibility; unrelated blockers remain. Ground owner=0. `ForwardMetadataGpu` was extended from 16 to 32 bytes: reveal `float4` at offset 0 and `uint4(mask, owner, 0, 0)` at offset 16. `SpriteSurfaceGpu`160 / `LightGpu`80 / lighting384 remain unchanged. Legacy does not consume local occluder lists/ranges.

OBB axes are unit length and orthogonal; the capsule perpendicular derives from axis X, not reserved axis Y. Minimum height, OBB half extents, and capsule radius are `1e-4` in float32; capsule half length may be zero. Export canonicalizes numbers to float32 **before** validation/output; GameArt transactionally rechecks transformed dimensions. Blockers with `reveal<1` receive no runtime IDs, consume no budget, and are not uploaded. The current camera-relative GroundForward domain ±32 covers P, local-light anchors, and the entire prism footprint/Z; it is not the authoring/export domain ±8192 or a global Legacy limit.
### 6.3. Caps and overflow
The initial Medium cap is **256 global visible occluders** and **32 occluders per light**.
The 32-per-light value is provisional and **PROPOSED / UNMEASURED**; carry it into the main plan at the next revision.
CPU culling order:
1. reveal and layer;
2. expanded viewport;
3. light influence intersection;
4. distance to light;
5. stable ID as the tie-breaker.
Global overflow must not discard required blockers: conservatively disable the corresponding light contribution with a diagnostic; acceptance fails.
Per-light overflow in the control scene is a debug/acceptance failure.
The renderer must not silently discard a blocker and show light through a building.

The implemented bounded slice from 07.10 strictly fails closed by rejecting **the entire invalid frame before GPU submission** at the 257th global blocker, 33rd per-light blocker, or an invalid range, rather than truncating silently. Spatial culling/tiled light lists, preset-dependent caps, and fallback of an individual contribution are not yet implemented or measured. The 256/32 limits do not prove ≥120 FPS.

## 7. Future graph: six passes, eight attachments
This graph supersedes the previous seven-target/half-LocalLight proposal. That proposal was insufficient for materials and dynamically lit sprites and is no longer used.

| Pass | Input and output |
|---|---|
| 1 Reflection | Half-resolution RGBA16F CLEAR/STORE; eligible casters are forward-lit using normal/depth/emission for bounded billboard reflections. It does not copy the full frame |
| 2 GroundGBuffer | Four MRT CLEAR/STORE targets from section 5.2: terrain/water/bridge base, footprints, AO, and sun silhouettes. No target is sampled while it is written |
| 3 GroundLighting | Reads four G-buffer targets + reflection + CPU tile light/blocker lists. Writes full-resolution scene_linear RGBA16F and world_meta RG16F. Provides complete ground lighting with sharp albedo/normal/specular; half LocalLight is removed |
| 4 WorldForward | LOAD/STORE scene_linear + world_meta MRT. Ground buffers are read-only. Painter-ordered lit sprites reconstruct P/N and evaluate their own local lights; they do not overlay a ground-light texture on a facade. Emission/weather use explicit material families |
| 5 Composite | Reads scene/meta, performs output encoding and final secrecy FoW, and writes final RGBA8_UNORM. No UI or second night grade is applied here |
| 6 UI | LOAD/STORE final, current RmlUi blending and text, without world grading or fog |

Haze is applied in GroundLighting/WorldForward from actual P before alpha blending. Particles also receive haze in their fragment shader; Composite does not add it again. world_meta supports diagnostics and final reveal. All opaque/alpha materials use the same haze function.

Half-resolution reflection uses full drawable coordinates/NDC with a half-size viewport. Do not divide camera zoom a second time. Texture UVs are normalized to target size; distortion pixel amplitude is multiplied by the internal scale.

Full-resolution GroundLighting uses CPU screen-tile lists with an initial 32×32 px bin. Verify 4/8/12 lights per ground receiver for Low/Medium/High; do not execute global 48×32 ray tests in every pixel. Per-sprite lists are capped separately. All caps are initial and unmeasured.

## 8. Light, emission and shadows
### 8.1. Lighting order
For terrain and world sprites: decode albedo → reconstruct P/N → albedo*(ambient*AO + sun diffuse*sun visibility + local diffuse*per-light visibility) + material specular + emission → haze → output/FoW. The exact formula and depth basis are defined in the Dynamic Lighting specification. A sun shadow does not darken another local light.
Do not apply a second night-grade operation after emission.
A window remains emissive at night, while its projected ground light falls off by radius/cone and occlusion.
Remove the old `fx.3` from migrated objects to avoid double glow.
### 8.2. Contact shadows
Every visible unit, including directional resident sprites and carried cargo, has a contact-shadow decal.
The contact shadow is tied to the ground anchor, scales within limits based on render Z, and receives no input hit target.
Water, roofs, UI, and unrevealed surfaces are not contact receivers.
### 8.3. Directional silhouettes

**Versioned direct-sky prerequisite 07.10:** SkyContactSlice has a separate MoonLightingGpu32
in ground b1/material-forward b3; lighting384 and seven targets remain unchanged. The Moon direct term
does not use sunvisibility/contactAO/localvisibility. This is still not a directional-silhouette
pass/carrier or a time/weather implementation; see [Sky direct](SKY_DIRECT_CHECK_2026_10_07_UA.md) for details and evidence.
An authored directional shadow silhouette is a separate art channel from the visual sprite.
It is selected by facing/light class and projected onto an eligible ground receiver.
Per-light occlusion for a window cone uses a separate OBB/capsule-prism system; a silhouette texture does not replace it.
Limits:
- arbitrary self-shadowing on a vertical facade is unsupported; shadows do not physically bend across an arbitrary cliff; a supported slope requires separate receiver proof;
- on an unsupported cliff, the shadow is clipped or uses a documented fallback; leakage onto unknown terrain, water, a roof, or UI is a failure.

## 9. Render targets and memory
Eight attachments for each of the two frame sets:

| Attachment | Format | Bytes/full pixel |
|---|---|---:|
| reflection 0.5×0.5 | RGBA16F | 2 |
| ground_albedo full | RGBA8_UNORM_SRGB | 4 |
| surface_world full | RGBA16F | 8 |
| surface_material full | RGBA8_UNORM | 4 |
| surface_flags full | RGBA8_UNORM | 4 |
| scene_linear full | RGBA16F | 8 |
| world_meta full | RG16F | 4 |
| final full | RGBA8_UNORM | 4 |

Total: 38 bytes/full pixel/set; two sets: 1920×1080=150.29 MiB, 2560×1440=267.19 MiB. Initial attachment caps are 176/304 MiB; this is arithmetic, not a GPU profile. Asset atlases, UI, buffers, upload, and temporary resize overlap are counted separately. Transactional resize holds both old and new sets simultaneously; verify this peak explicitly. An RG16F→RGBA16F metadata fallback adds 4 bytes/full pixel/set and requires recalculation.

There is no depth target; a sprite's R16 data texture is not a depth attachment.

## 10. Capability query, resize and lifetime
Before creating each target, the renderer calls a format-support query with the actual format and usage flags.
A format check without `COLOR_TARGET | SAMPLER` usage is not evidence of suitability.
Resize transaction:
1. get non-zero drawable size;
2. wait on fences for both old frame sets;
3. test formats/usages for new size;
4. create the complete candidate set of eight attachments × two sets;
5. create / test size-dependent views and pipelines;
6. swap the active set only after complete success;
7. release old resources after the fences complete.
When minimized or drawable size is zero, the renderer skips world submission and does not create a 0×0 texture.
On partial allocation failure, release the entire candidate; the active set remains valid and an error/diagnostic is recorded.
This fallback does not allow the development stage to count as complete.
Failure during fence wait, shader creation, format query, target creation, upload or submit must contain program/format/usage/size/frame-set context.
Silently disabling a required pass is forbidden.

## 11. Shader program contracts
The single generic sampler/uniform count from the current `shader()` is insufficient.
Each program pair has a separate manifest record:
- entry point and stage; vertex inputs/outputs; color target count/formats;
- uniform buffers; samplers; read-only storage buffers;
- storage textures, if introduced; blend/write masks; source/DXIL/reflection hashes;
- declared alpha and color-space convention.
Scheduled program families:
- ground material/MRT; water/reflection composite in Ground; reflection caster;
- local light; world sprite Dynamic2_5D/albedo+normal+depth/emission; contact/directional shadow;
- weather/decal; composite; UI legacy.
`tests/shader_contract.py` moves to schema 2 and compares program-specific counts against SDL create info.
DXIL reflection must confirm the exact `LightGpu`/`OccluderGpu` strides.
C++ static assertions confirm sizes/offsets independently of reflection JSON.

## 12. Art schema 2 and transactional upload
### 12.1. Schema 2
`assets/art/world.json` schema 2 describes pages, textures, color space, alpha convention, materials, sprite regions, pivots, and normal/depth/emission/mask channels by material family. Dynamic2_5D requires normal/depth data; missing data rejects the catalog, and schema1/BakedCompat does not pass dynamic proof.
A material cannot reference a page with incompatible semantics.
Atlas limits and rectangle validation from schema 1 remain.
### 12.2. Schema 1 adapter
Schema 1 is not removed in 0.26.1.
The adapter creates an albedo-only `MaterialDesc` with:
- explicit sRGB albedo; straight alpha for world sprites; default neutral normal/data;
- zero emission; no wet/light/reflection receiver until the role is added explicitly; existing pivot/rect/texture IDs without renumbering.
The adapter provides compatible loading but does not present an old asset as V2 material proof.
### 12.3. Transaction
The loader first parses and fully validates a candidate catalog.
It then uploads every candidate texture/material buffer under a temporary owner.
Any error removes the entire candidate without changing the active catalog.
The active catalog swaps only after complete upload and validation.
The old catalog is released after fences for frames that still reference it.
Failure injection must cover the first, middle, and last texture, as well as material-buffer upload.
After every rejection, active handles remain valid and leaked resource count is zero.

## 13. Viewport, DPI, input and RmlUi
The current fixed 1440×900 size does not match the target 720p/1080p/1440p captures.
V2 contract:
- `Frame2D.logical_size` equals drawable width/height in physical pixels; world projection uses the same drawable viewport; `effective_zoom` multiply by `drawable_height / 900`;
- RmlUi context dimensions equal the same drawable pixels; SDL mouse coordinates convert window→drawable exactly once; renderer, GameView, and RmlUi do not perform another letterbox conversion;
- picking and scissor use drawable coordinates; user UI scale is a separate parameter from OS DPI and world zoom.
Actual rasterized font size after all scale operations cannot be less than 15 physical pixels on 720p.
Text layout, hit regions, and visible glyph size are checked together.
Increasing UI scale does not change world zoom or mouse world position.
Mandatory viewport cases:
- 1280×720, 100% OS scale; 1920×1080, 100% and 150%; 2560×1440, 100% and 150%;
- resize during the game; minimize/restore; drag selection and minimap click after DPI transition.

## 14. Source integration map
**CURRENT** denotes an existing file; **PROPOSED** denotes a recommended integration point that still must be created.
| Path | Status | V2 responsibility |
|---|---|---|
| `include/ribterra/render_frame.hpp` | CURRENT, change planned | Frame inputs, material/light/decal/reflection handles, drawable size |
| `include/ribterra/renderer2d.hpp` | CURRENT, change planned | Renderer options, capabilities, telemetry, viewport conversion |
| `src/render/renderer2d.cpp` | CURRENT, major change planned | resources, six passes, fences, shader programs, capture |
| `src/client/view.cpp` | CURRENT, change planned | render XYZ, receiver geometry, painter inputs, shadows |
| `src/client/main.cpp` | CURRENT, change planned | drawable sizing, one input conversion, UI/world frame order |
| `src/client/game_art.cpp` | CURRENT, change planned | schema 2 transaction and schema 1 adapter |
| `include/ribterra/render_materials.hpp` | PROPOSED | `MaterialDesc`, receiver flags, CPU/GPU ABI |
| `include/ribterra/world_fx.hpp` | PROPOSED | `WorldFxFrame`, decals, reflection casters, FX telemetry |
| `src/render/world_fx_renderer.cpp` | PROPOSED | water/reflection/haze/weather/footprint draw preparation |
| `shaders/*.hlsl` | CURRENT directory, new programs proposed | program-specific contracts with one linear/alpha policy |
| `tests/render_frame_tests.cpp` | CURRENT, extend | validation, ordering, ABI, failure cases |
| `tests/shader_contract.py` | CURRENT, extend | schema 2 program reflection/bindings/hashes |
`MaterialDesc` defines material bindings; an arbitrary texture slot in view code is not a new material API.
World FX uses the same attachments and contracts rather than creating a parallel renderer.

## 15. Stages and dependencies
The user deferred full Dynamic mode. Execute [DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) when that work resumes. The early dev.01–09 items from the older graphics plan are technical work packages, not an instruction to implement Dynamic in the current 0.26.1. Only the API scaffold exists now; the six-pass graph has not been added.

Begin with capability/resource/material contracts, then a small lit kit and shape reconstruction, followed by source/occlusion proof and performance; add the UI setting/persistence only after those steps. Do not produce mass lit assets before the small scene is accepted.

## 16. Future stop/go gate for Dynamic
The gate requires continuous native D3D12 videos from the production executable, not a software probe:
- zoom `0.62`, `1.30`, `3.00`; day and night; clear/dry, rain/wet, snow and storm;
- camera pan over terrain, slope, river, bank, bridge, and revealed frontier; a unit with contact/directional shadow and footprints; window emission/light with a blocker before/after movement;
- resize or restore no stale resource; debug counters for light/occluder overflow, attachments and memory ledger.
Every video/manifest records the commit, build, shader/asset hashes, GPU, driver, backend, drawable size, preset, seed/save hash, camera/zoom, and diagnostic/authoritative source.
A software capture, generated image, or isolated shader sample does not close the gate.
Dynamic proof only happens if:
- all six passes work in the production graph; occluder overflow in the control scene is zero; decals do not corrupt receiver metadata, and lit sprites update world_meta;
- the UI is not graded and input matches after DPI conversion; no mandatory effect is disabled by fallback; memory/resource numbers are recorded from measurements rather than copied from this plan;
- native video shows temporal stability at all three zoom levels.

## 17. Automated tests
| ID | Check |
|---|---|
| `RC-ABI-01` | `LightGpu` size/offset 80 and 0/16/32/48/64 match in C++/DXIL |
| `RC-ABI-02` | `OccluderGpu` size/offset 64 and 0/16/32/48 match in C++/DXIL |
| `RC-SHD-01` | Every program pair has exact sampler/uniform/storage counts and target formats |
| `RC-COL-01` | sRGB albedo decode once; linear mask does not change gamma path |
| `RC-ALP-01` | straight world and premult UI test patterns do not have dark/bright fringe |
| `RC-MRT-01` | A ground decal changes scene color while metadata bytes/half-floats remain unchanged |
| `RC-PAS-01` | resource state audit does not allow sample+target one subresource in one pass |
| `RC-LGT-01` | ambient applied to base to additive light/emission; Composite does not dim emission second |
| `RC-OCC-01` | OBB/capsule selection is stable; the 33rd blocker produces overflow failure in the acceptance fixture |
| `RC-SHDW-01` | each unit has contact shadow; directional silhouette does not become light occluder shortcut |
| `RC-MEM-01` | attachment ledger matches drawable size, two-set count and queried formats |
| `RC-RSZ-01` | Resize transaction does not activate a partial set and releases the candidate after every injected failure |
| `RC-UPL-01` | schema 2 failure on first/middle/last upload leaves active catalog and zero leaks |
| `RC-DPI-01` | One window→drawable conversion produces the same world pick at 100%/150% DPI |
| `RC-UI-01` | 720p actual font ≥15 px; UI does not fall into world Composite |
| `RC-FOW-01` | hidden caster/light/particle does not change revealed pixels beyond the AA boundary |
| `RC-CAP-01` | native capture contains correct final target no UI/world double conversion |
Pixel/unit tests are required but do not replace video checks for movement, shimmer, shadow leakage, and DPI interaction.

## 18. Failure matrix
| Failure | Runtime action | Delivery status |
|---|---|---|
| RGBA16F/RG16F format+usage unsupported | abort V2 initialization with an exact diagnostic; review formats/architecture separately | FAIL, not completed |
| one of 16 attachments cannot be created | release the candidate, keep a valid old set if one exists | FAIL |
| shader manifest/binding mismatch | program not created | FAIL |
| per-light occluders >32 in the acceptance scene | counter + diagnostic, blocker is not silently discarded | FAIL |
| global occluders >quality cap | stable cull + visible counter | allowed only outside the acceptance fixture and after review |
| schema 2 asset/mask missing | rollback candidate catalog | FAIL corresponding dev |
| reflection/local-light allocation failure | a safe render path may remain for diagnostics | mandatory effect not accepted |
| metadata overwritten by a decal | disable bad program + diagnostic | FAIL |
| zero-size minimized window | skip submission, preserve resources | allowed |
| device/submit/fence failure | stop frame, record SDL/backend context | FAIL; do not count success frame |
| software probe passes, D3D12 video absent | do not make a runtime claim | UNVERIFIED |

## 19. Cross-platform and backend discipline
Public CPU contracts, asset schema, color/alpha semantics, and shader manifest must not depend on Windows-only struct packing.
All GPU structs use fixed-width fields, explicit 16-byte members and compile-time offsets.
Current release proof for 0.26.1 — native SDL_GPU Direct3D 12 on Windows.
This does not allow a software renderer to count as proof of GPU correctness.
Another native SDL backend requires its own compiled shaders, capability query, native video, and performance evidence; without that proof, mark it unverified rather than “works cross-platform.”

## 20. Unresolved decisions before implementation
1. Confirm that target GPUs support `RGBA16F` and `RG16F` with the required `COLOR_TARGET | SAMPLER` usage flags.
2. Check if final `RGBA8_UNORM` + explicit linear→sRGB conversion matches actual swapchain format; do not add second conversion during blit.
3. Prove the packing/basis/depth precision from the Dynamic Lighting specification on a small asset kit.
4. Measure full-resolution ground lighting and per-sprite forward lighting; do not treat culling/caps as optimized before profiling.
5. Verify the provisional cap of 32 occluders per light and global Medium cap of 256 in `visual_slice_v2` and the stress scene.
6. Define exact shadow behavior on a supported slope and the documented cliff fallback before dev.05.
7. Define the native timing method; do not present CPU submit time as GPU time.
8. Fix ownership of final UI composition between Renderer2D and the RmlUi adapter so a second hidden offscreen target cannot appear.
No budget or cap number in this specification has been measured yet.
Implementation of the future mode begins with DL-01; current work is limited to the scaffold, and the UI setting remains deferred.

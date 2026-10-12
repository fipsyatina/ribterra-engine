# Contact GPU V1 — partial engine step

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

07.10.2026. **Contact-only verified; The full engine is NOT ACCEPTED.** All E01–E17, concept matching and ≥120 FPS in 1920×1080 remain open. GameView is left BakedCompat, Dynamic is unavailable; The launcher, the HUD, the generator, the mechanics, and the art catalog remained the same.

## What Really Happened

A separate `WorldGraph::GroundContactSlice` was implemented: GroundGBuffer → Contact → GroundLighting → WorldForward → Composite → UI. The empty list is passed by Contact and retains five passes. Use the same seven attachments and lighting384; The new target was not added.

Contact only changes to ground_albedo.A: `newAO=oldAO*(1-mask*opacity)`. Target LOAD/STORE, A-only write mask, alpha blend ZERO/ONE_MINUS_SRC_ALPHA/ADD; Active albedo is not dimmed. World/Flags — nearest; the individual R8-mask — linear clamp, explicit LOD0. The solar/local inputs and UI are not dimmed. The emission is independent of the shader contract, but there is no new separate native emission fixture at this step.

GroundForward kept the old W0/1 and reserved-zero rules. The new Contact graph uses discrete W0 invalid / W1 ordinary / W2 eligible and typed Ordinary/Flat/Slope/Bridge; All eight local receiver bits are constant. Contact requires W2/reveal1 and a permissible distance to the plane. Ordinary, partial reveal and other altitude surfaces are checked by paired negative/positive controls. It's not automatic recognition water/roof/cliff in GameArt: their future adapter should be an Ordinary.

Joint canonical projection: origin + zoom × project_render_xyz(P). Ground vertices are checked with the 1/1024 logical pixel allowance. Eight extruded corners use the same float function; physical layout, extrema, padding ±1px, floor/ceil and clipping are calculated double. Plane tolerance [.02,.04] is matched with bounded RGBA16F error; The CPU prototype with the smaller minimum remains a historic, not new GPU contract. Bounds ±32, normal.z≥.5, cap128 without truncation — provisional renderer limits, no FPS or terrain gameplay rules.

Typed immutable registry has exact validated R8 bytes: dimensions3..1024, ≤1 MiB/art, ≤ 256 live arts and ≤16 MiB logical R8 bytes. The integer subrect≥3×3 has a zero outer one-texel ring within the rect and a nonzero interior. UV is set to first/last texel centres; GPU draw must match the registry exactly UV/facing. Publication only after successful upload; allocation/pre-submit upload/idle-wait faults do not change the live sentinel. Cold release is waiting for GPU idle, changing generation; Generation wrap permanently retires the slot. Render is not reentrant, caller-serialised; All stale handles are checked before command acquisition, resolved GPU pointers are not coming out of render.

## Current evidence

The main capsule: `build/contact-targeted-2026-10-07-attempt03/`.

- Fresh direct MSVC C++20 `/O2 /MT /W4 /WX /permissive-`, three current renderer/contact/probe TU, only SDL3/SDL3_ttf imports. No old Ribterra objects/libs, CMake or full asset staging were used. The separate ABI assertions control the GPU80/160/48 and CPU wrappers; C4324 locally turned off only for two intentionally padded CPU wrappers, not wire structs/global flags.
- `cpu-tests.log`: **211 CPU assertions PASS**. This is a standalone test, not a CTest run.
- `shader-contract.log`: compiled DXIL interfaces, bindings, ABI and manifest PASS. `shader-negative-fixed-01.log`: **34 register/space/output/ABI mutations**, 10 direct +1 two-level include mutations and other semantic/numerical negatives rejected.
- The original `shader-negative.log` — **FAIL**, not renamed to PASS: fixture31 left an undefined identifier after renaming. `negative31-diagnostic.log` keeps the cause. Fixed, compilable negative script separately stored in `source/shader_negative-fixed-01.py`; This is the only intentional delta from the original script snapshot, not a production change DXIL.
- `native-01` and `native-02`: **53/53 native checks in each **, actual D3D12, RTX3050Laptop, driver32.0.15.9282; GPU execution time=null. 35 output files; **31/31 PNG between repeated byte-exact** (`native-repeat-comparison.json`) launches.
- Ambient sample168/156/135 →125/116/99; 128 repeated-value1 masks leave behind 168/156/135, 128 distinct1..128 masks give 24/22/17. Both matched frames have 128 actual draws/binds/casters. It's a pixel discriminator, not just counters.
- Paired Ordinary/Flat, reveal.5/1, lower/upper plane, slope/bridge, shifted anchor and discriminator off-center pivot PASS. Atlas outside255/outside0 in two slots gives byte-exact pixels with real interior darkening. Sun-only/local-only/UI, both frame slots, minimize/no-submit, letterbox/resize, A-B-A, stale/mixed, handles, cap129 and registry256/16MiB boundary PASS.
- `renderer-regression-01/report.json`: fresh direct RenderFrameTests `/W4 /WX`; software and `--gpu` exit0. This is a supplementary selected suite, **not a new full CTest/full game run**. All 49 staged shader inputs are unchanged.
- `prior-input-hashes-after-repair.json`: the old Local/Ground EXE and shader manifest **3/3 hash-identical **. `reuse-native-01.json`: expected exit1, all 35 existing files are unchanged. New helpers and fail-if-exists; pre/post compiler headroom≥384MiB and root logical budget≤128MiB. Logical bytes from hardlinks do not equal allocated disk bytes or VRAM.
- Independent source/oracle/evidence review: **APPROVE only partial Contact-only milestone**. The default compiler attempts01/02, first negative run and intermediate sources are saved. Existing files were neither deleted nor overwritten.

Native probe SHA256: `7E2D8609F0298F94CE52A71E351C16D07D9F766954A1EBE5D3874A1BE5DD9357`.

## What this does not establish

No new raw W readback, desktop scan-out/video, motion acceptance, artist/concept-ready house/tree/resident/cargo kit, GameArt loader/adapter integration or representative performance. This is the new shader/registry route with diagnostic masks, not the final game. Full CTest, shipping GameView, full graphics presets and 120 FPS are not accepted here. Sun/moon and directional silhouettes, reflections/coverage carrier, atmosphere/fog/water/weather, and other engine gates remain mandatory.

Next engine work: independent sun/moon and source-specific sky visibility, authored small calibration kit, then unexecuted effects and full GameView/presets/performance gate. Do not move to the game modification V01–V03 until the full engine checklist is accepted.

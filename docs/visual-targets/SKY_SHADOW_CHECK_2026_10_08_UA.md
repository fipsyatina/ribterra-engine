# Straight shadows Sun/Moon — partial engine checkpoint

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026 (Europe/Warsaw). **BOUNDED PREREQUISITE VERIFIED / Full length is not allowed.**

The production renderer includes an explicit opt-in `SkyShadowSlice`: independent maps of the sun and moon depths, which only obscure the contribution of the corresponding source on the actual ground P and reconstructed P typeed sprite. This is not a new game: the usual GameView remains BakedCompat, Dynamic unavailable, launcher unchanged. **E01–E17, the concept matching and ≥120 FPS in 1920×1080 remain OPEN.** The gameplay/HUD/ generator/catalogue has not been moved.

## What Has Been Done and Verified

- Frame-owned opaque triangles XYZ, source mask 1/2/3; not shadows, drawn in PNG, and not re-using contact AO or local OBB/capsule.
- Two D32_FLOAT DEPTH_STENCIL_TARGET|SAMPLER 1024² maps for each of the two frame slots. Combined format support is checked; fallback is not presented as an enabled effect.
- SunShadow → MoonShadow → the previous ground/forward/composite/UI graph. Ground and typed sprite sample each card in its own position, not ground-behind-sprite.
- Nearest depth, two-sided triangles, nearest 3×3 PCF, clearly visible=1 outside coverage/tap. The base common to raster/sample, is calculated from the normalized direction.
- Check all raw rows, including from reveal=0: finite/nondegenerate XYZ in the included camera-relative ±32 cube, mask and reveal [0,1], raw≤4096, active≤1024/source. Reveal=0 does not rasterizes; any reveal>0 is a completely opaque caster, ** not a partial alpha/fade**.
- The activated sources of this prerequisite require elevation≥20°. It's a constraint, not an east/west policy or a low moon.
- Four depth targets are created transactionally; fault on candidate No3 does not publish a half-sample, a repeat attempt is successful. Frame fences before reuse; RAII lifetime.
- Material ABI: new clear `SkyMoonLighting192` in b3 = Moon32 + Shadow160. Ground has separate Moon32/b1 and Shadow160/b2. Not use reserved legacy fields; Surface384/Sprite160/Metadata32 is unchanged. No more than four uniforms buffers/stage — actual boundary pinned SDL 3.4.16.

16 MiB — nominal payload of four D32-cards, **no** measurement of driver allocation, peak VRAM or performance.

## Actual evidence

The only final capsule: `build/sky-shadow-2026-10-07-attempt09/`. The name is preserved from the beginning of the work; final checks were carried out 08.10 local time. Fresh standalone MSVC C++20/O2/MT/W4/WX/permissive/UTF-8 snapshot build; It's not a full CMake/CTest or game package. Helper and verifier ended with actual exit0.

| The verification | The Actual Result |
|---|---|
| SkyShadow CPU / Moon CPU / Contact CPU | 110 / 51 / 211 assertions PASS |
| 17 shader programs / 68 DXIL+reflection artifacts | Strict bindings, ABI, include closure and hashes PASS |
| Shader negatives | Rejected 48 register/space/output/ABI mutations, 20 direct + 1 transitive include-hash mutations,2 noncanonical includes, owner/capsule-axis/stride, 2 numerical, 3, moon-independence and 4 compiled shadow-semantic mutations |
| Native SkyShadow | 12 named groups PASS ×2; for 27 PNG; D3D12, NVIDIA RTX 3050 Laptop GPU, driver 32.0.15.9282, software=false |
| Cross frame slots | Sun: open0/shadow1/shadow0/open1; Moon: open0/mask1-open1/mask2-shadow0/mask2-shadow1. Real slot IDs asserted; Both are positive sources on both slots |
| Repeat / previous SkyDirect / Contact | 27/27 / 26/26 / 31/31 PNG byte-exact |
| Previous shaders | 56/56 old DXIL/reflection files byte-exact |
| Selected RenderFrameTests | Software and actual D3D12 PASS; It's not a full suite. |
| Provenance | 7 EXE, 68 shader artifacts + hash-anchored manifest on build completion; 104 frozen source/root-lineage entries are unchanged at the time of final verifier; 7 protected old artifacts unchanged |

The native matrix demonstrates source-specific ground/material causality, masks 1/2/3, opposite directions, caster movement and change of direction of the same sun, reverse winding, nearest blocker in front of typed receiver z=3 versus blocker behind it, shadows on tilted slope, zero caster compatibility and conservation ambient/contact/local/emission/UI for disabled direct. There are 10 pre-command invalid-payload cases and capacity boundary 1024 active + 3072 hidden. Final independent review: **APPROVE only bounded fixed-camera prerequisite**.

EXE SHA-256: `DC250FA0FA021EFE14381A7EA6C7D50BCF34E9A9F7B49396369F9992C9E27828`.
Shader manifest SHA-256: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`.
The exact commands/exit provenance: `verification-command-01.json`; comparisons, source lineage and protected hashes — separate JSON in the capsule. Ledger recorded 54,198,638 logical bytes before adding a command record; The final root remains below 128 MiB. This is not an GPU VRAM ledger.

## The stored failures — not listed on PASS

- Attempt01: ADL ambiguity in the probe; attempt02: 1-ULP CPU threshold fixture; attempt03: shader-negative source-independence fixture. Fixtures have not weakened the production horizon or shader semantics.
- Attempt03/04 native: heap corruption C0000374. Pinned SDL from debug=false did not reject five fragment uniform buffers to D3D12 fixed-array record. The new ABI combines Moon32+Shadow160 into a clear b3; renderer/shader preflight decreases count>4. The old dangerous EXE not to reboot.
- Attempt05: W4/WX rejected the local name that hid member. Attempt06: build passed, but review found the same-slot, not cross-slot repeat, and incomplete shader/verifier hash anchor; It's not native acceptance.
- Attempt07: native rejected nearest-depth fixture without mandatory ground draw. Save 18 PNG; Report counter19 included a broken 19 capture attempt. Added real ground control outside the caster footprint, renderer validator unchanged.
- Attempt08: native/CPU/shaders/regression passed, but verifier actual exit1 due to the error expected PNG count31. The actual count27 is assigned to all capture calls and files. Attempt09 repeated the entire string from count27 and exact 12-name inventory; No more imaginary extra staff.

All roots, logs and artifacts are preserved. The files were not deleted; There was no install/cleanup, privileged profiler, commit/push/release or launcher change.

## Visual boundaries and subsequent work

Reviewed unmodified native PNG from the final capsule: source-direction relocation and tilted-slope shadow; Additionally in attempt08 — nearest low/high blocker. Also reviewed `target-0262.png`. Diagnostic rumba/typed 1×1 fixtures ** are not similar to concept art and are not an artistic result**. PNG — GPU readback, not desktop scanout, laptop control, motion video or FPS evidence.

Also OPEN: authored world-triangle asset adapter, all real building/tree/resident/cargo category and silhouette quality, camera-following/snapped maps/cascades and pan/zoom stability, self-shadow/bias, policy, partial/alpha, coverage, low-horizon/time/weather, water/reflections/FoW/atmosphere/FX, art kit, GameView, presets, full current suite and ≥120 FPS/smoothness.

The next engine-only step has already started with a separate cohort: optional authored `sky_shadow_triangles` in the diagnostic schema-2 exporter and typed GameArt world→camera adapter. Its new changes ** are not yet adopted**; The frozen 104-entry checkpoint above is not a test of these later changes. The minimum diagnostic kit is not a new catalog or a ready-made copy of the model. The full engine-first gate remains open, the game awaits.

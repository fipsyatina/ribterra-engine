# Authored Sky Bridge — verified importer/adapter, no engine reception

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

## Status

A narrow importer/adapter component is adopted: the description of the author triangles in the schema-2 asset passes through the real `AssetCatalog` and `GameArt` in the camera-relative `Frame2D::sky_shadow_triangles`. Verifier attempt05 ended with actual exit0. At that point GPU-the frames were `NOT_RUN`; native01–04 subsequently became a diagnosed failure attempt. **The current next bounded native checkpoint is:** [native05/06](AUTHORED_SKY_NATIVE_CHECK_2026_10_08_UA.md), each exit0,13checks/32PNG,32/32repeatPNG byte-exact and independent approval. It's not a full engine or a regular GameView; the separate control path GameView is checked by the next step.

All E01–E17, full Dynamic/GameView, concept compliance, charting, ≥120 FPS in 1920×1080 and full engine reception remain OPEN. The game, the generator, HUD, the art catalog and the launcher in this step did not change. The overall goal is ACTIVE.

## Contract executed

- The editable master has an independent `sky_shadow_triangles`, not derived from the raster `caster_mask`. The diagnostic set contains six asymmetrical facade/gable/roof triangles with Sun/Moon/shared masks 1/2/3; It's not the ultimate art kit.
- The exporter canonizes the float32 coordinates, checks the exact shape of the records, infertility and limits, and adds an array to both lit-sprite descriptions. The missing master field is exported as `[]`; The old schema-2 descriptor without a field is imported as a blank list.
- The importer re-verifies the data independently of exporter/manifest. The description is equal to `id`, `vertices`, `source_mask`; ID is empty, unique, up to 256 UTF-8 bytes. It's a blurred ID, not display text. Masks — are only an integer 1/2/3, not bool/float. The termination coordinates are exactly represented by float32, in ±8192; double bound is checked by ** to ** narrowing.
- Raw limit 4096 and limit 1024 triangles per source are preserved. The incorrect candidate rejects the sampled upload and does not publish a new registry.
- `append_lit_sky_casters` applies `world_anchor + local_vertex * geometry_scale`, then obviously takes away the camera world through `camera_relative_render_xyz`. The mask is kept; reveal=0 adds nothing, positive reveal remains opaque/filter-only, not alpha coverage.
- All existing and staged rows are checked before inserting: finite, ±32 camera-relative cube, infertility, reveal/masks/capacities. The error leaves the byte-exact frame unchanged. `clear_commands` cleans the sky rows.

## Fresh source-frozen proof

The basic root: `build/sky-authored-2026-10-08-attempt05`.
The separate exporter root is `build/sky-authored-2026-10-08-export-checks01`.

- Fresh MSVC C++20/O2 /MT /W4 /WX /permissive- /utf-8: eight translation units, two new EXE, actual helper exit 0. The old production .obj/.lib was not used. `skill_balance_generated.h` generated from snapshotted generator/data.
- Shader cohort — ** verified reuse**, not a new compilation/negative run: 68 artifacts + manifest from the accepted SkyShadow attempt09, unchanged source closure and hash equality origin/copy.
- 103 authoritative/snapshot source entries matched before and after checks. 12 protected previous EXE/manifest/report/log files are not changed.
- The independent reviewer re-checked the actual reports, exit sidecars, 103 source pairs, EXE and 12 prior paths and approved **except this bounded loader/adapter component**. No broader engine/art/FPS status has been elevated.
- Exporter: 10 deterministic files, 12 probes, **41 negative case** with actual exit 2 and required diagnosis; invalid inputs did not create output, existing-output sentinel is saved.
- Fresh MaterialBridge software contract: exit 0. Native D3D12 material regression: exit 0, **22 PNG**, hardware sRGB/linear channels and candidate rollback controls. Main reviewed the unchanged `game-art-schema2-lit-sprite.png` and `material-world-ui-order.png`; It's gray/color controls, not concept-ready art and not desktop scanout/video.
- Authored bridge: **two actual exit launches 0**, each with equal **13 named groups**, **19 schema invalids** and **13 transactional adapter failures**. Exact transform, camera delta, reveal, ID boundary/control identity, raw/per-source caps, malformed existing rows, nonfinite/out-of-cube/float-collapse, and clearing were passed.
- Parser/adapter semantics and full compact-fixture/derived-kit file ledgers collided between the repeated launches. Authored bridge did not submit any GPU frame and did not produce PNG.
- Backend: native `direct3d12`, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, software=false, the same in both material/bridge runs.
- After verifier: the root 60,024,495 logical bytes; exporter root 70,892,693 bytes, both ≤128 MiB. Ledger measures the byte to its own record, so its fields are fairly named `*_before_ledger`; The final guard is executed after the protected-final.

## What the Sentinel proves and what he doesn't prove

For negative parser fixtures, clearly marked valid 1×1 six-channel catalogues with the same authored geometry are used. The final adapter test loads the **original full-size fresh export**. Production parser was not weakened.

Baseline load consumes exactly six sampled creation attempts. Fault on N+1=7 proves that each invalid descriptor consumed zero sampled uploads, and the old callable registry was not replaced. It's not proof of partial-upload RAII cleanup or live-resource counters — they're not declared. The particular MaterialBridge regression has its own candidate-failure checks.

## Basic hash anchors

- MaterialBridgeProbe.exe: `0641A8451F56AE25D20F3439A454EC7F7D25A861B60D9D11244CFE2A1D55A645`
- AuthoredSkyBridgeProbe.exe: `D4AF8BFA11A432247030298F1CDF40432B3F69A287AEBB868A1B4877970CE85F`
- Shader manifest: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`
- Editable master: `6b362340248c1b8eef3a6ebe850fb01d1962485d910d9605a6d25b087b923359`
- Exporter: `0616a51fc385ee0ed3d9aa7028f7ae6d6c23b03438c8786d91fc55c9a8cac8f4`
- Canonical authored descriptor: `ca6eaf077d02207939147ebe00cc6e0c15d2f3cdced2c8a2d05b69a5d2af773f`
- Verifier: `4BAA115C0D299B7C8591C607DB3AA690DACCC9520F48EAF5AEF6EED6A3209F47`

The hash descriptor is verified by repeating the calculation with loaded `world.json`: ordered-object compact UTF-8 JSON + one LF. The separate no-newline nlohmann hash in the report is clearly named and does not replace the exporter hash. This is a verified equivalence of the current set, not a general guarantee of the canonicality of the various JSON serializers.

## The Reserved Failure

1. Attempt01: helper exit 1 to compile, PowerShell 5.1 does not have `Path.GetRelativePath`; Now PS7 guard to create root.
2. Attempt02: helper exit 1 after six production TUs, MSVC C7772/C1903 due to escaping build-label argument; The helper argument is corrected.
3. Attempt03: build and focused bridge exit 0, but logical root 275,574,337 bytes through full-size fixture hardlinks; **not accepted** because of the budget. Fixtures are reduced, evidence is preserved.
4. Attempt04: compact build exit 0; verifier exit 1 in preflight `Compare-Object` binding, to the tests. Fixed named operands, collected new frozen attempt05.

At the time of the bridge attempt05 files were not deleted. Backups and these authored outputs are now saved. Later, at the direct request of the user, [cleared individual historical runtime copies](../ARTIFACT_CLEANUP_2026_10_08_UA.md); This proof didn't go into the cleansing. The compiler used only announced new `compiler-temp` directories for its own service files. The [restricted storage rule](../ARTIFACT_RETENTION_UA.md) continues to apply, not the endless accumulation of failed collections.

## The next open door

A separate native causal probe is to perform nine groups of authored-path plan: nonrectangular source masks, world anchor movement, sprite rectangle isolation, camera lockstep/translation, caster without sprite, raster-mask independence, vertex mutation, actual frame slots/UI/target rollback. Until this matrix passes completely (latest run03 FAILED), the full authored prerequisite is not completed. Full suite/CTest, all art categories, camera-pan/cascades, alpha/self, filtering, time/weather, water/mist/particles, presets, 1080p performance and comparison with concepts remain OPEN.

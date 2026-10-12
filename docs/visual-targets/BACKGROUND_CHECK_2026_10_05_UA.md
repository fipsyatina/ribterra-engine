# V01–V03: background checkpoint 05.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

This report is pre-recorded for preview **r2**. Further changes to the miniature map, land and generator have a separate report [](MAP_GROUND_CHECK_2026_10_05_UA.md); listed here 92/92 They don't spread to them automatically.

Status of the artwork: **REBUILD / not accepted**. This is a proof of an unfinished process 0.26.3, not a release, not a claim on the exact reproduction of concepts.

At the user's request, no window was opened, closed or controlled by the keyboard/mouse. The frames below are created by hidden native D3D12- windows. F1 is checked by the same software processor, but this ** is not a new manual verification **.

## Fixed at this checkpoint

- The boundaries of the five author Cart poses: the cut no longer captures the adjacent row of atlases; Ten runtime records count repeats. The leg position and `reference_width=222` are preserved. PNG has not changed. Two author poses are two, not four unique frames.
- The rock on the slopes is only found at the actual height of the common peaks and known water. No false entries or separate geometry are added. After the first weak result, the coastal threshold is recalibrated on the generated land; For seed 70421 the average extra weight of the stone at the 146 coastal corners increased from 0.0161 to 0.0563, 33 corners have an increase ≥0.10. It's a diagnostically detected area, not a change in the player's intelligence or a similarity assessment to the concept.
- Automatic `--smoke`/capture launches no longer move the system cursor and use a separate profile near its output. The usual interactive profile is unchanged.
- After independent auditing, the profiles are made unique for each automatic launch: bare `--smoke` uses a new folder in TEMP, capture — a new neighboring folder. Two consecutive smoke ended with exit 0, created two different saved profiles, and did not create `smoke-test-profile` in the workbook.
- The switch «Notice»/«Cast» in uk130 is removed by extending the action strip from 34% to 42% for the large scale. The font and icons are not down. The actual text width test originally fell: write 133 px, button 126.9 px; After the correction, the full matrix is passed. An independent pixel review confirmed no new encounters with the inspector/minicard. Previously, only span-bounds gave a false PASS and are no longer sufficient.
- AtlasTool supports Windows extended-length paths. The long isolated TEMP has revealed a true decline in the Unicode notice. The diagnostic decoding is corrected, assertions are saved, a separate test of the output path of the >260 characters is added: to the correction 2/7 rejection, after — 7/7.
- The genuine v6 carry fixture is copied and hashed alongside the isolated probe. Hidden search is not used due to compile-time `__FILE__`.
- The `RIBTERRA_PRESERVE_BUILD_ARTIFACTS=1` mode retains previous assets and rejected candidates. Added tests of successful and unsuccessful staging in this mode. The Art probe stores nine separate biome-saves instead of deleting one shared file.

## Evidence

The roots of the new results: `artifacts/concept-rebuild-2026-10-05/`.

| The verification | The result |
|---|---|
| `focused-rock-atlas.xml` | 3/3: passport, terrain_v02, game_art_outline |
| `terrain-bank-tuned.xml` | 1/1 after calibration; includes 20 000 seed, 5 known seed regressions and generated-bank contract |
| `matrix-before-rock`, `matrix-after-rock`, `matrix-bank-tuned` | By 5/5 quick D3D12- configurations, the same fixture/camera for comparison |
| `art-rock-d3d12/art-report.json` | Exit 0: 20 catalog entries × 5 states, 2 level-II types, 1089 terrain picks, 3 zoom picks, movement of the slope; until the final calibration of the material |
| `residents-safe-atlas/report.json` | Exit 0: 256 addressed frames, Wood/Food/Stone delivered to Camp and Stockpile, save/resume; until the final calibration of the material |
| `full-suite.xml` | Historical passage 05.10: 91/92, the only refusal — search v6 fixture in isolated copy |
| `matrix-full/report.json` | Historical 60/60, but the old test missed the glyphs; Not proof of a justification |
| `hud-glyph-regression-before` | Expected rejection at uk130, text=133 / button=126.9; Red-stage regression |
| `matrix-hud-fixed/report.json` | 60/60: 5 languages × 4 scale × 3 resolution, 123 native D3D12 captures; the actual text width and F1 hide/restore verified |
| `ordinary-seeds-hud-fixed/ordinary-seeds-report.json` | 8/8 regular starts on the current content: by 1 Camp, 10 residents, 861 explored tile; without editing land/resources/exploration |
| `full-suite-hud-fixed.xml` | 91/92 for 480.63 s; the only rejection — long Unicode output AtlasTool, now corrected and independently verified |
| `full-suite-final.xml` | **92/92**, 0 rejection, 427.30, s, after the AtlasTool/profile/fixture correction; local software/dummy CTest, not physical two-PC/WAN |
| `installed-preview-r2-renderer.json` | Locally preview: exit 0, Direct3D12 / NVIDIA RTX 3050 Laptop GPU, software=false, version 0.26.3 |
| `launcher-r2-renderer.json` | The `Play Ribterra Rework.cmd` itself is verified by a hidden smoke-start: exit 0, D3D12, seed 1337; We can see the user window didn't hit. |

Current content SHA-256: `e904fc36677b228732158c379634033933c4034f90db986ad8c3208c9a027b2e` (183 assets). Build and installed r2 executable have the same SHA-256: `DCF9514876AD73ED0B4EDA8E6DFBE1C452835A4E478B6AF007DB657E58706B35`. The matrix uses the same GameUI/Renderer/content; The next main correction was just to isolate the automatic profile.

[Comparison of concept, native frame and HUD to/after](BACKGROUND_COMPARE_2026_10_05.html). This local HTML- page did not open in the user window.

The historical `artifacts/concept-rebuild-2026-09-30/full-suite-twelfth.xml` was completed **91/92**, not pending. The only refusal was `v03_asset_passport`; Her cause is justified. The current 92/92 is confirmed by the new `full-suite-final.xml`, not by the sum of individual lines.

The `ai-regression-testing` habit is applied to actually found defects: HUD glyph overflow and long Unicode lines are first deformed by failing tests, then checked after correction. The independent reviewer accepted a narrow guarantee background-safety/output-isolation, **not** of overall artistic relevance or readiness for release.

## The Fastest Demand

| The Need | Accurate estimate |
|---|---|
| V01: decorative panels, antiques, painted icons, 4 main action, inspector | Integrated and visible in the native frame; The composition is not an exact copy of the target. |
| F1 hides/returns HUD and markers | The full native matrix passes; Repeat manual verification is not as requested by the user |
| V02: new seed, different rivers and mixed resources, reproduction, old save | Full suite and 8/8 fixed-seed launch frames pass; This is not a new interactive random seed selection test and not an artificial intake |
| The hills, the coastal cliffs, the natural landscape | Partially: real altitude is there, but the rocky shores and the tiny composition are still weaker than the target |
| The units are tracking the tracks | Traces from the actual movement, save/load is checked; No routes or hidden map changes |
| V03: new buildings, construction, seasonal conditions, people/vantage | Atlas and runtime coverage are; The scale, the repetition and the artistic integrity still require monsters. |
| Ideal compatibility with concepts | **Not done**: large open areas, gray exploration boundaries, a mini map in a black field, insufficient coastal detail and atmosphere |

The full version checklist of the final result is stored in the [ main processing document ](CONCEPT_REBUILD_2026_09_30_UA.md). The usual start  Camp + 10 people; The settlement developed on the frames is clearly situated on unaltered generated land. Forecast/°C/rain/storm — delayed V04- systems, numbers in the picture are not falsified.

## Local launch

`C:\Users\fipst\Desktop\ribterra\Play Ribterra Rework.cmd` now leads to `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2/Ribterra/Ribterra.exe`. The old package 30.09 and the first package 05.10 are preserved. The usual public launcher/current-build pointer is unchanged. All 184 files of the installed assets-catalog (183 assets + manifest) are assigned to the set over SHA-256. Pack — local ** unacceptable preview**, unreleased release.

## File preservation

Old screenshots, packages and assets are left in place. Three full lengths use the separate `full-suite-work`, `full-suite-work-hud-fixed`, `full-suite-final`. Each user is warned to remove only their own temporary test-save, backups, settings, and scratch directories within the respective `runtime`/`temp`. These are temporary test copies, not user files; The deleted scratch files are not recovered separately, reports/frames and output fixtures are saved.

The last `full-suite-final/snapshot.json` checks the 277 copied files — EXE/DLL, assets, shaders, and genuine v6 fixture — hashes source-before/copy/source-after. Apart from the written hash of the converted CTest and 24 external command files. It is a **results isolation**, not a sealed or portable copy of the entire checkout: Python tests and their transitive source inputs remain external, as clearly indicated in the manifest. They don't change during the drive.

After completing CTest, a repeat check for the manifest detected **0 changes** among 277 runtime inputs and **0 changes** among 24 external command files.

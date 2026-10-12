# Ribterra recycling V01–V03 — checking 30.09.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

## Decision

**PARTIAL — Substantial local processing has been implemented, but full artistic match to the references is not accepted.** This is not a published release and is not a statement that all V01–V03 are ready. The sequence of results: [acceptance checklist](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md).

The total graph volume is **V01 HUD → V02 land/city/street → V03 buildings/houses**. The old `target-0261/0262/0263` also contains a different gameplay sequence: weather, housing, farm mode. It is not issued as being realized; The question of how to combine these two volumes remains open. Forecast, temperature, rain, snow cover simulation, minimum with public targets and Dynamic Lighting are not included in this rework.

## What Really Changed

### V01

- Compact resource counters, navy/ivory/brass palette, lower inspector, four basic actions with icons. The data and the teams are left out of the real simulation.
- Permanent internal message of the original generation of wilderness has been removed; The usual feedback disappears in five seconds.
- F1 hides and returns not only RmlUi panels but also selection markers, progress annotations and social bubbles around the world. Holding the key does not switch HUD again.
- Escape closes the open panel to transmit the event to GameView. Chat is a priority. The visibility is counted right away, not just in the next shot.
- The correct cut Prolong» on pause and long Polish name Surface.

### V02

- Quieter meadow material, wide material areas, less contrasting small noise, more natural decoration and coastal detail.
- Fixed common mirrored boundary UV, particularly for negative coordinates. The color of the coastline is determined by the common peaks, not by the type of the individual cell.
- For world textures, a maximum of four MP-levels are enabled; It reduces grinding and grain removal. UI and text are left at the base level.
- Saved authoritative prototyping from real moves, save/load and legacy Road without a speed bonus. There is no new manual.

### V03

- Building scale — 1.35 from catalogue (except for bridge and legacy Road); Larger residents and contact shadows. Simulation footprint is not increased.
- House II and Workshop II received separate winter sprites: winter no longer replaces them with Level I silhouettes.
- New sources, accurate prompts, pivots, hashes and notices are recorded in `art-source/rework-v03-winter/`, `assets/art/world.json` and passports.
- The renderer, the picking, and the thumbnail use the same resolver. Visual cargo remains attached to the actual item/quantity.

## False Evidence

All the way down — from the root of the repository. The final directory of evidence: `artifacts/rework-v01-v03-2026-09-30/`.

| ID | The Evidence | Limitations of the statement |
|---|---|---|
| T | `ctest-certified.xml` | PASS: 91/91, 0 failures, 674.79 s, final executables/assets; local/software/network checks do not replace individual native evidence |
| M | `matrix-certified/report.json` | PASS start: 60 combinations, 123 PNG; uk/en/ru/pl/es × scale 0.85/1.00/1.15/1.30 × 1280×720/1920×1080/2560×1440; This captures, not complete longest-text/input acceptance |
| A | `art-certified/art-report.json` | Native D3D12, 20 catalog families × 5 states, 2 level-II families, 256 resident sprites, 1089 terrain picks, 3 zoom picks, uphill movement |
| R | `residents-certified/report.json` | Native D3D12: 256 footage; actual delivery to camp on tick 212 and stockpile on tick252, both from save/resume |
| P | `build-evidence.json`, `package-certified-*.png` | PASS local launch: accurate hashes, fresh package, stocks→Escape without unpause, F1 hide/restore with steel 06:42/80 wood/40 stone/60 food/10 people |

GPU: **NVIDIA GeForce RTX 3050 Laptop GPU**, backend `direct3d12`, with no software fallback. The `RenderFrameTests.exe --gpu` exception has been followed by the final changes: minification, blending, clipping, painter order, Unicode, resize/input mapping. The software renderer is independently verified and is not called a GPU-proof.

In M ordinary-start — the real new world; staged-v03 — clearly prepared control stage with buildings. Both pass through `World` save/load, `ClientSession`, `GameView`, RmlUi and D3D12. The staged scene does not appear to be a natural growth settlement. Seed 70421, camera (24,24), zoom 1.30; tick/state/resolution are stored in the manifest of each frame.

Manual control of the game window revealed a cut pause button and Escape, which had previously reached pause. Verified launch of the new settlement, resource panel, Escape closure, F1 hide/restore and group order 10 to residents. F12-the footage is not a video recording; The test game didn't rewrite the old user saves.

The previous `pass-01`, `pass-02`, `final`, `matrix-final`, `matrix-final-v2`, `art-final`, `residents-final`, `ctest-final.xml`, `ctest-postfix.xml` are preserved as a history of iterations. They **do not replace ** the final `*-certified` evidence. `ctest-postfix.xml` contains two found and corrected errors, not a green run.

## That Has Not Been Accepted

1. **Artistic output:** The world is still much less saturated with details and differs from the composition and styling of the sent concepts. In winter, the large boundaries of material zones are visible. The UV test proves the continuity of the coordinates, but not the artistic quality of the entire surface.
2. **Full UI-matrix:** frames do not automatically prove the longest warning/details, all hit rectangles, ≥36 dp, OS DPI 100/125/150%, union HUD ≤30% or absence of texture outflow after 50 inspector cycles.
3. **Movement:** there is no mandatory unassembled video of all facing/cargo, bridge crossing and track accumulation. The delivery sequences are confirmed by individual native frames and simulation assertions.
4. **Remember:** computational RGBA8 payload 45 world pages is 310.01 MiB at the base level and 411.68 MiB with four levels. The previous 292.01 MiB is not preserved. This is the arithmetic of texels, **non-measured allocation VRAM**; There is no control. p95/p99/peak memory comparison. It needs repack/lazy loading and profiling, not hidden quality reduction.
5. **Network:** local tests on one PC are not two physical Windows PCs or cross-network acceptance.
6. **Production art:** added PNG — prepared raster sprites, not 3D masters and not sets of normal/depth/material maps for future Dynamic Lighting.

Because of these open points neither V01, nor V02 nor V03 here get the final mark PASS.

## Launch

From the root of the repository open **`Play Ribterra Rework.cmd`**. It only launches `dist/rework-0.26.3-2026-09-30/Ribterra/Ribterra.exe`, doesn't download anything, and doesn't replace the result with an old public package. F1 — hide/turn HUD; F12 — save the frame.

The old `Play Ribterra.cmd` is left unchanged release-route. Commit, push, GitHub release and external publishing failed.

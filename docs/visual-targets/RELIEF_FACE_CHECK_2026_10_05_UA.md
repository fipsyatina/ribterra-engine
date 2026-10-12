# Real relief: bare rock — 05.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Intermediate processing step V01–V03. The overall artistic correspondence: REBUILD. Not a release.**

After the [ seasonal diversity of nature](NATURE_VARIETY_CHECK_2026_10_05_UA.md) the readability of true steep slopes is enhanced. The user window is not operated; All new native footage was obtained by the hidden D3D12 probe.

## Implemented

- `terrain_rock_face()` uses the same height `terrain_corner` that the surface mouse is already projecting and selecting. No new heights, rock collisions, resources or vertical walls have been added.
- The initial weak layer is preserved. Additional reinforcement starts only at gradients above 4, smoothly reaching 90% stone at 8. The full sampling space `[-4,+3]²` must be explored.
- The wind blows the stone dark, and the edge of the rock is slightly exposed. The multiple is limited to 0.82–1.10; It only works on stone. His shade got colder.
- Snow does not disappear: winter material transfer to the rock is limited 34%. The river water and the mining material do not receive a new rock deposit.
- Neighbouring cells use the same world tops. Height, picking, generation, passage, resources, wood, buildings and building rules are not changed by this passage.

It is a **material step**, not a new trident mesh, not new cliff-sliding, and not a completed set of coastal steps.

## Checks

The source of the new evidence: `artifacts/relief-face-2026-10-05`.

| The verification | The result |
|---|---|
| Ribterra / ReworkVisualProbe / ClientInputTests / TerrainV02Tests | Collected; staging saved previous files |
| Isolated runtime | 279 files, checksum before/after copying; `check/snapshot.json` |
| terrain_v02 + client_input | `check/terrain-client.xml`: 2/2 PASS; 99.99 s and 28.41 s respectively |
| The usual start | `ordinary-rock-face/ordinary-seeds-report.json`: 8/8, 1 camp, 10, residents, 861, dispersed cell |
| Native quick-matrix | `matrix-rock-face/report.json`: 6/6, 15 frames, F1 hide/restore true; the matrix is not complete 60/60 |
| Independent code/pixel review | The material step is approved; no visible seams or outlets of undetected geometry have been identified; the overall scene REBUILD |

The new tests test the actual reinforced path, the unknown end of the sampling point, the boundaries of rock/snow/light, the sign of curvature, the normalization of materials, the common peaks and the unchanging world. The old Gen7 bank regression is preserved; The actual Gen8 launches are tested separately.

### The size of real startups

Without artificial opening of the map or lifting heights; `World(seed,10)`. [Diagnosis after change](../../artifacts/relief-face-2026-10-05/relief_face_after_stats.json).

| Seed | Known land tops | Nudity of stone ≥70% | The floor/top plates ≤4 | Changed the slope to the previous formula |
|---|---:|---:|---:|---:|
| 1337 | 443 | 66 | 300 | 0 |
| 70421 | 429 | 18 | 283 | 0 |
| 4204434016 | 446 | 29 | 355 | 0 |

The world checksum before and after the scan is the same for all three. It's proof of the unconscionable computation of the material, not proof of the concatenation.

## The Outstanding Result and Limitations

[Comparison to/after, wipe, overlay and concept](RELIEF_FACE_COMPARE_2026_10_05.html).

The usual seed1337 and seed70421 in 1920×1080, the post-summer settlement in 1920×1080 and winter in 1280×720, were reviewed. The former green slope 1337 is now read as a solid gray rock. The effect on 70421 is smaller. The snow remained dominant, with no visible rhombus net.

The remainder: the great slope is still too uniform; The concept has more distinct rock speeches, the transition from the coral reef to the natural rock formation. Further global stone weight increases are not the solution. The composition of the settlement, the extent of exploration and the fullness of the weather HUD are also unfinished. Tests and these frames do not accept the whole V01–V03.

### Limitations of comparison

Before: `artifacts/concept-rebuild-2026-10-05-map-ground/{ordinary-nature-variety,matrix-nature-variety}`. After: current `ordinary-rock-face` and `matrix-rock-face`. The reported capture/fixture metadata is matched except for the report time. Gen8 / protocol22 / rules18 / engine0.26.3; D3D12 on the NVIDIA RTX3050 laptop GPU. The content resources are unchanged: `37d1a772babac87a8a585478db09f444d6e3964b0fd20df318027391a2d7f4c6`.

The conventional probe creates a production ClientSession direction without rewriting save or setting. His report does not record per-capture tick/weather/checksum, nor does driver/quality. Comparing these frames is not called a full-pixel-perfect experiment. The Quick-matrix runs on the real save/load, buildability, composition selection and F1; It's not a manual test in the user window.

After capture, a separate read-only loader downloaded all three pairs of quick-matrix savings. [Full disclosure](../../artifacts/relief-face-2026-10-05/save-loader-results.txt), [provided by Diagnostics](../../artifacts/relief-face-2026-10-05/save_loader_diagnostic_oct5.cpp). For each pair, the `World::checksum()`, seed70421, Gen8 and 1352 chunks are matched:

| The Preservation | Tick | Checksum before = after |
|---|---:|---|
| ordinary-start.save | 0 | 3315416272074499671 |
| staged-v03.save | 1247 | 11764719575540080685 |
| staged-v03-winter.save | 871200 | 13346885934572110410 |

SHA-256 save files themselves are different: each new World has a random `world_id` included in the header and compressed payload. `World::checksum()` does not contain this ID. So the pair state is confirmed within the current simulation checksum, not as byte-identical files. No overwriting or normalization of saves. This is additional confirmation for the quick-matrix; It's not being put as a checksum of eight individual ordinary-seed captures.

## Identity and transmission

SHA-256 verified files in `build/concept-rebuild-validation`:

- `Ribterra.exe`: `2365B75FCFA5C183B73D5622CC0958A3B8AB138C285561C4DDDACBC74269638D`.
- `ReworkVisualProbe.exe`: `8C87F397064C84033943C26D370F65B9899BF576ED69BAA43826CDADAFAF9F25`.
- `ClientInputTests.exe`: `9E0CBD118F3615D7FFD6F4563BD5266FDDA8376E344E169960EA41C14A1345FF`.
- `TerrainV02Tests.exe`: `C742838531581DC9875E2AEA523EF92037E894C3745123B6F008DF4631CF0FEF`.
- The previous probe is stored in `nature-check/runtime`: `9CC413C231C4C1303EA6B415656FF99DD88535419C8661FB2C5634245E09730D`.

The preview r2, `Play Ribterra Rework.cmd` and open game are not replaced. SHA-256 r2 is left with `DCF9514876AD73ED0B4EDA8E6DFBE1C452835A4E478B6AF007DB657E58706B35`.

Before the tests, six of the own temporary save files in the new `check/temp` and `check/runtime` were listed; The test cleanup was about these files. The existing preservation and evidence are preserved. HTML-the equation has verified local links; The browser and user panel didn't open.

The full suite in this new condition did not launch. Historical 92/92 preview r2 is not a verification of Gen8 or current changes. The last general Gen8 strain was 84/92, after which individual strains were tested selectively. There's no fresh one 92/92. There is no new release, commit/push or external Windows/WAN reception.

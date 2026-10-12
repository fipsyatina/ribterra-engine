# Ribterra 0.26.3 concept-rebuild preview — boundaries and unconfirmed points

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

## Status

This is a local preview of the V01–V03 rework, not a published release. New art and functional changes are integrated into the original branch, but the exact correspondence target-0261/0262/0263 is not yet accepted. Current status: **REBUILD / not accepted**. On the 05.10 full suite 92/92, native matrix — 60/60; And then, after the changes, they have to be repeated. The technical PASS is not an artistic reception.

## Current local evidence

Current status at 05.10.2026: [phone checkpoint](BACKGROUND_CHECK_2026_10_05_UA.md). **92/92** in the new `full-suite-final.xml`, **60/60** native D3D12 cases with real glyph-width verification, **8/8** regular fixed-seed starters. The new preview and its launcher passed hidden D3D12 smoke; The manual window control was not executed. All points below the next section — history 30.09; `full-suite-twelfth.xml` has actually completed **91/92**, and its only atlas rejection has already been corrected. This is not the final reception.

- `coop-terrain-twelfth.xml`: **2/2** (`coop_start` 11.2251 s, `terrain_v02` 37.6921 (s) and `storage-snapshot-regression-second.xml`: **2/2** (`server` 8.15315 s, `protocol` 1.39713 s). Local snapshot/coop- regression is closed; The physical two-PC/WAN scenario is not proven.
- The last incremental set completed **40 targets** without errors; The full 92-test suite is left with a separate pending evidence.
- `matrix-twelfth-atlas`: quick **5/5** (13 native D3D12 captures) and `art-twelfth-d3d12` exit 0 on a set of 183 assets, content hash `112d8f79c132f87e4e6d197e40c821503575022c7b94c5d82f941878bb49185e`. Pixel review eight resident calibration positions did not detect head-row bleed or severed heads/pews in ordinary, staged and winter frames.
- `focused-twelfth.xml`: **4/5** — `v03_asset_passport` has alpha-bounds assertion for `resident.man.cart.1.1`; latest passport PASS is not claimed. The full 92-test suite is still running, so its result cannot be considered known.
- `ordinary-seeds-eleventh`: **8/8** native D3D12 captures; `matrix-eleventh-ground`: quick **5/5**. They predate the recent spatial-bin and atlas changes and remain historical evidence of that state.
- `matrix-tenth-full`: **60/60** — historically complete D3D12- matrix until the last changes.

These results are confirmed by individual local contracts, but are not artistic reception, full latest-runtime PASS or proof of release readiness.

## What is already in the current realization

- the version of the game 0.26.3;
- save format 8, rules 18, generator profile 7, network protocol 21;
- random seed for each normal new game and reproduced by the obvious `--seed`;
- a normal start with Tabor and 10 residents;
- new HUD, fonts, icons and art of nature, buildings, construction, seasonal conditions and inhabitants;
- high relief, river change, mixed resource areas and natural paths away from actual movement;
- F1 hides and returns the whole HUD; F12 stores the actual footage of the game being played;
- The old supported worlds keep their generator profile and don't re-generate the hidden ones.

## What else to prove before receiving

1. Compare the fresh native footage of the conventional start, advanced settlement and winter alongside the relevant concepts.
2. Check composition HUD, readability, portraits, miniature, choice contours and the absence of overlays in target resolutions and scales UI.
3. Check in the game all available buildings, House II/Workshop II, three stages of construction, summer and winter conditions without returning the old art.
4. Check the direction of the walk, the size of the people, the actual cargo, the delivery and the coverage of the residents.
5. Check the accumulation and save/load natural paths after the actual movement, not by post-editing the map.
6. Repeat the full set of tests after the final asset integration and correct all rejections.
7. Separately, run a cooperative scenario on two physical Windows PCs via a real network. Two local customers on the same PC don't do that.

## Deliberately Postponed

The four-day forecast, temperature, rain, storm, weather accumulation and full dynamic lighting are all part of V04 and later stages. In 0.26.3 they cannot be replaced by decorative numbers or weather messages from concept art.

## Conditions for safe checks

- Before you discover an important old world, copy its file from `%APPDATA%\Ribterra\Ribterra` to a separate location.
- Do not rewrite a single backup through F5 until you check the loaded world, the relief, the buildings and the residents.
- For repeated comparison use one clear seed; to test the randomness of the game, create a few regular new games without `--seed`.
- Don't give a post-development settlement a regular start: a regular start — only Taber and 10 people.
- For networking games, use the same local packet on the client and server. Protocol 21, rules and content should be in sync.

The test is considered acceptable only after fresh native footage, repeated full test run, and closure of the points. The very fact of the presence of new assets or the success of individual focused-tests does not confirm the ideal fit of concepts.

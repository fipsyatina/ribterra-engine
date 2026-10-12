# Ribterra verification 0.26.3 V03

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

> The historical account of a particular collection is below, not of visual conformity. After user comments 30.09 the cumulative processing [ is started V01–V03](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md); Its status is prioritized for the current outcome. The hash EXE and the statement of absence of winter-II plates below apply only to sets 29.09.

Verified 29 September 2026 in `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`. This is local proof of implementation V03, not a statement about a published release or external reception.

## Result

- The full release collection is complete: 137 build-steps after incremental configuration, asset staging — 155 validated files.
- The full sequence CTest: **91/91 passed**, 0 failed, `1066.83 s`.
- The previous parallel loop gave 90/91: `coop_start` exceeded the timeout under load. The same test was immediately passed separately for `9.01 s`, and then passed in a full sequential suite for `8.92 s`. It's recorded as load-sensitive test behavior, not hidden.
- Focused V03: **11/11** — `building_visual`, `unit_motion`, `art_alpha`, `art_client_rendered`, `texture_atlas`, `atlas_tool`, `asset_staging`, `upgrades`, `upgrade_flow`, `v03_asset_passport`, `render_frame`.
- Additional UI/input regression checks are: **4/4** — `ui`, `ui_lifecycle`, `client_input`, `building_inspector`.
- `git diff --check` — without error. Impeccable detector for V03 presentation files — `[]`.

## D3D12 and native evidence

- `RenderFrameTests.exe --gpu`: `direct3d12`, `NVIDIA GeForce RTX 3050 Laptop GPU` — renderer contract passed.
- Native ArtVisualProbe: 20 building families × 5 base states, 2 allowed level-II family, 256 directional resident sprites, summer/winter, 1089 terrain-pick checks, 3 object zoom/pick checks, uphill movement.
- Native ResidentVisualProbe: 256 sprite frames; actual cargo delivered to the TAB by tick 212 and to the TAC by tick storage 252; the cargo disappears after delivery; Both scenarios survived save/resume.

The main evidence files:

- [Summer V03 world + HUD](../artifacts/graphics-v03-0.26.3/native/summer.png)
- [Object calibration: stages, ready, II, winter, 8 facing/load samples](../artifacts/graphics-v03-0.26.3/native/v03-object-calibration.png)
- [Zoom 0.62](../artifacts/graphics-v03-0.26.3/native/v03-zoom-062.png), [zoom 1.30](../artifacts/graphics-v03-0.26.3/native/v03-zoom-130.png), [zoom 3.00](../artifacts/graphics-v03-0.26.3/native/v03-zoom-300.png)
- [Winter state](../artifacts/graphics-v03-0.26.3/native/winter.png)
- [Real cargo before delivery ](../artifacts/graphics-v03-0.26.3/residents/stockpile-carrying.png) and [after delivery](../artifacts/graphics-v03-0.26.3/residents/stockpile-delivered.png)
- [Native art metadata](../artifacts/graphics-v03-0.26.3/native/art-report.json), [resident metadata](../artifacts/graphics-v03-0.26.3/residents/report.json), [SHA256 manifest](../artifacts/graphics-v03-0.26.3/manifest.json)

Imagegen [V03 target](visual-targets/progress/v03-objects.png) is used only as an artistic orientation. It's not runtime proof and it doesn't replace native footage above.

## Confirmed contracts

- Renderer and picking use the same `building_visual` resolver and the same width.
- `stage1/2/3` depends on the construction progress; the completed I levels are selected `ready/winter`.
- Only House II and Workshop II select `.level2`; During the upgrade, the current level remains until the authoritative upgrade is completed.
- Winter does not lower the II level: the level-II plate is kept with a cold tint.
- Simulation footprint, entry cell, projection 58×29, protocol, rules, generator and save format remained the same.
- Directional resident: 2 images × 4 load states × 8 directions × 4 phase = 256 verified IDs.
- The cargo comes from the actual `ResidentLogisticsView.item/quantity`; `quantity == 0` picks an empty walk.
- Contact shadow is painted to art-ready resident sprite and has a wider ground contact for the cart.
- Production smoke/fire needs a real active order in phase `Working`.
- The inspector preview shows level-II art only for House II/Workshop II.

## Versions and boundaries

Engine `0.26.3`; protocol `20`; rules `18`; generator `6`; save format `8`. There's no new schema/network bump because V03 is presentation-only.

Not closed by this local lane:

- uninstalled native video movement/pause/4× not recorded; It is replaced by runtime motion tests, 256-frame GPU probes and before/after delivery captures;
- clean install/package operation from a separate fresh folder failed;
- two physical Windows PCs and cross-network have not been verified;
- `level2-winter` PNG no: a stable cold tint is used, and a full snow cover is left V05;
- The stock is not tied to the actual stock in V03, because bounded inventory-art binding is missing;
- The new PNG was not packed, so AtlasTool did not start production export into the new catalog; The current atlas/manifest/tool/staging contracts have passed the test.

The file `Ribterra.exe`: 47 082 496 bytes, SHA256 `04d96f4ff2e1895f671f8a62e3f393150fa1f25022bfb9cf222983c8b320f939`.

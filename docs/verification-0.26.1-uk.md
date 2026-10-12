# The test is called Ribterra 0.26.1 — V01

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

> The historical account of a particular collection is below, not of visual conformity. After user comments 30.09 the cumulative processing [ is started V01–V03](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md); Its status is prioritized for the current outcome.

The date of the local inspection: **28 September 2026**. Verified working tree `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`. This is a report on a local collection with no changes, not a published release.

## What Has Been Checked

- It is built with Windows x64 executables for UI, inspectors, economics, professions, manual commands, upgrade flow, input feedback, and render-frame.
- `UiCheck` downloaded Noto and Cormorant, checked RmlUi/RCSS and rendered geometry.
- Five catalogues **UK/EN/RU/PL/ES** have been tested for identical keys, ICU- arguments, and 172 required glyphs.
- Asset staging checked **155 assets**, final content hash: `79c2bfb55b7f96e78b87dd71d2a2d77b730171d3404358842280da042fed35d2`.
- The CTest target set was passed by **12/12** to **133.68 with **:
  `manual_order_ui`, `building_inspector`, `economy_ui`, `profession_ui`, `localization`, `asset_staging`, `ui`, `ui_lifecycle`, `render_frame`, `upgrade_flow`, `input_feedback`, `client_input`.
- `RenderFrameTests.exe --gpu` passed on to **Direct3D 12 / NVIDIA GeForce RTX 3050 Laptop GPU**.
- `git diff --check` ended with 0. The messages were only about future normalization CRLF/LF.

## Native evidence

- The picture is: `artifacts/graphics-v01-0.26.1/v01-native-1440x900.png`
- SHA-256: `492A5D5694D19ACBBDD5C14AD9FF86C400837740DB4014E87CEE5E71E162F3B8`
- Compact frame: `artifacts/graphics-v01-0.26.1/v01-native-compact.png` (1100×700)
- SHA-256 compact frame: `244EF1CD6559BC439450C34E40D9DBE8874D00205B513C68E8F2E483CFFFAF21`
- Renderer metadata: `artifacts/graphics-v01-0.26.1/renderer.json` and `renderer-compact.json`
- Backend: `direct3d12`; device: `NVIDIA GeForce RTX 3050 Laptop GPU`; driver: `32.0.15.9282`; `software=false`; Title: `Ribterra 0.26.1`.

The footage is taken from the normal native world `--demo` on 1440×900 and 1100×700. They show compact resources, calendar/sphere, short resident inspector, horizontal actions, and system controls without overlapping. The land, fog, vegetation, buildings and inhabitants remained in the style V00, as required by the boundary V01.

## Verified contracts V01

- F1 hides the whole HUD without leaving the world; holding the key does not turn it off again, and the second single press completely restores HUD.
- `Будувати` (Build), `Збирати` (Gather), and `Призначити` (Assign) open the existing directories/panels; There's no specific model of orders.
- `Скасувати` (Cancel) issues a separate presentation intent and completes placement, worksite, or drag-selection without cancelling the construction or row.
- The short inspector does not replace the full: House II and Workshop II are running an existing upgrade-flow.
- Group selection does not copy the status of the first occupant or building; It's just a number and a combination.
- Panels that can be long are rolled inside and have a shutter button available.
- The rules **17**, protocol **19**, save format **8** and generator **5** are not changed.

## What this does not establish

- The full CTest set outside of 12 target tests did not launch.
- No installed/ZIP- package 0.26.1 was created and no freshly unpacked launcher was tested.
- No video recording of all interactions, physical verification on two Windows PCs or cross-network/WAN was performed.
- V02–V05, new generation, weather, temperature, snow and new world/building/unit assets are not included in 0.26.1 and are not declared ready.

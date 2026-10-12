# Ribterra verification 0.25.8

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

23 September 2026. Windows release, MSVC, `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`.

## Verified result

`BuildingInspectorProbe` reproduces the **53 script** in the real RmlUi and rendering game. The scenarios use authoritative simulation, preservation and `ClientSession`; It's not a HTML- layout or a change of panel text.

For the Logging (Boards), Workshops (Stone Tools) and Kitchens (Food) checked: no order, transfer of raw materials, work, moving to another building and back, ready batch on the way, completion with goods in storage, shortage of raw materials; overnight rest, renewal after night shift permit, no route, filled storage and renewal of delivery after clearance. Separately checked the delivery of the cargo at night and the completion of the goods on the ground after the delivery failed.

In the absence of a live freelancer, the explanation and error shown in the panel of the relevant structure; The neighboring building doesn't inherit them. A message from a right-click on a map without a selected resident is consumed exactly once and gets to RmlUi `status`. In the 1100×700 window, the command button is available after you click the keypad.

`BuildingPanelVisualProbe` checked 20 panel types, building selection by clicking on the map, closing, saving/loading, and two group orders with different employees at two local network clients. All five language directories have been checked for keys, ICU- parameters, and font coverage.

## Evidence

- `artifacts/inspector-0.25.8/report.txt`  the result of 53 scenarios.
- `artifacts/inspector-0.25.8/` — three-story frames and a compact window; and reviewed `lumbermill-output.png`, `kitchen-materials.png`, `compact-action.png`.
- `artifacts/inspector-0.25.8/catalog/building-render-report.json` — Verify 20 panels, two local clients and save.
- `build/0258-first-tests.log` — first 6/6 CTest-check: economy, economy_ui, localization, ui, ui_lifecycle, client_input.
- `build/0258-final-build.log`, `build/0258-install.log` — final compilation and installation.
- `artifacts/inspector-0.25.8/installed-renderer.json` and `launcher-renderer.json` — two successful launches of the installed game and the default launcher: `engine_version=0.25.8`, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, the software renderer turned off. Both processes ended with the code 0.
- `artifacts/inspector-0.25.8/package-verification.json` — integrity ZIP and compatibility EXE, HUD, styles, Ukrainian localization, launcher and description of the update between the installed folder and the archive. EXE also matches the newly collected file.

The folder is set to `dist/releases/0.25.8/Ribterra`. `dist/current-build.txt` indicates its `Ribterra.exe`. Both available labels on the Desktop refer to the verified `Play Ribterra.cmd`; The lights themselves didn't change.

The archives: `dist/Ribterra-Windows-x64-0.25.8.zip`, 158 777 452 bytes. SHA256: `7d2733a6f8f3b09844332d79f9aab37404c553e95434a1bce09f22f576a0e763`; the control amount is also recorded in the neighboring `.zip.sha256`.

## Limitations

Final break after compilation 0.25.8: **12/12 CTest passed**, 176,51 p. The following are the most common types of checks: buildings, economy, logistics, wilderness_ui, economy_ui, network_session, profession_ui, client_input, ui_lifecycle, ui, localization, building_inspector. The latter repeated all 53 scenarios for 75,23 p. The journal is `build/0258-final-tests.log`; report: `build/engine-windows/building-inspector/report.txt`. `git diff --check` passed. The compiler leaves pre-conversion/switching warnings in view and C5054 in game_ui.

The inspector's visual scenarios are rendered by a software; The game and launcher installed were independently verified via Direct3D 12. The full set of all CTest didn't start. Previous changes to the coverage of the Expedition panel and the plan documents are preserved. The source code was not published in GitHub within this task.

The recipe and simulation of production remain unchanged. The inspector shall show the batches manufactured and the current delivery status; it does not attribute all manufactured goods to available stock. General reorganization of employee appointments, cancellation of delivery and national ownership remain the next versions of the plan. Access via the external Internet was not verified.

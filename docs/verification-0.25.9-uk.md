# Ribterra verification 0.25.9

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

23 September 2026. The local Windows Release collection, MSVC, `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`.

## Result

Selection of 1–20 parties, automatic or specific employee, explanation of employment, transition to existing management of professions and group order report are implemented. AutoSelection reads the server projection of the profession, skipping minors and permanent residents. The obviously stopped specialist can be selected manually; occupations, jobs and priorities remain. The group does not repeat the construction or the workers.

Cancellation is the preserved intention. The cargo completes the physical delivery, the next batch is not launched, the worker is dismissed. Unfinished work returns the raw materials to the ground. The completion of physical delivery is processed to check the health of the worker, so the finished product after the failed delivery does not cause the waste material to be returned again. The return of several ingredients checks the limits for any change in quantity.

## Checks

- The following are the most commonly used methods for calculating the value of a business: **18/18 CTest**, 198,82 c: production_control, production_control_ui, economy, economy_ui, professions, profession_ui, logistics, logistics_adversarial, persistence, manual_orders, manual_order_persistence, protocol, network_session, client_input, ui_lifecycle, ui, localization, building_inspector. The magazine: `build/0259-tests.log`.
- After recent readability changes and reports  **6/6 repeat checks**, 66,98 c: production_control, production_control_ui, building_inspector, economy_ui, ui, localization. The magazine: `build/0259-final-tests.log`. Compilation: `build/0259-final-build.log`.
- `ProductionControlTests`: two batches = 12 delivered logs of 6 wood; the continuation of the second cycle after storage; nine cancellation limits, including with raw material, cargo, route search, start/mid-work, finished goods on the road and completed delivery. After each termination — verification of the number of items retained, dismissal of the employee and a fixed extension after save/load.
- In particular, refusing to take away the assigned farmer without changing the world. the maintenance of the profession after a manifest cessation; the refusal of simultaneous production, professional and manual orders; the death of the carrier of the finished product; the return of two ingredients; blocking the second return by the item limit and repeating without duplicating the first; the old JSON order without cancellation flag; The new flag is being transmitted over the network.
- `ProductionControlProbe`: **13 scenarios in real RmlUi**, with input of the number of batches and employee selection by keyboard events, switching to control, explicit farmer stop, unloading and loading, partial group result, repeated ID construction, visible by one employee to the group, and fielding when the structure changes. The 1100×700 button can be reached by scrolling through the focus. Report: `artifacts/production-0.25.9/report.txt`.
- `BuildingInspectorProbe` repeated the **53 scenario** Forestry, Workshops and Kitchens from 0.25.8. `BuildingPanelVisualProbe` checked **20 panels and two local network clients**, different group employees, for the retention and absence of duplication of repeat devices. Report: `artifacts/production-0.25.9/catalog/building-render-report.json`.
- A copy of the original 0.25.8 save with rules 14 (`artifacts/inspector-0.25.8/catalog/orders.save`) is loaded by the 0.25.9 server, runs 20 ticks, and is saved in the new format; exit code 0. The original remains unchanged. Log: `build/0259-legacy-save.log`.
- Five locations have been checked for keys, ICU- parameters and font. `git diff --check` passed.

The actual frames that were viewed are: `farmer-busy-details.png`, `farmer-management.png`, `group-partial.png`, `finishing-delivery.png`, `two-batches.png`, `compact-action.png` in `artifacts/production-0.25.9/`.

## Package and launch

Set to `dist/releases/0.25.9/Ribterra`. `dist/current-build.txt` indicates his `Ribterra.exe`. Installed game and default `Play Ribterra.cmd` run from `--smoke`; Both ended with the code 0. Both of these reports confirm that **0.25.9, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, software=false**. Both available Desktop tags are directed at this launcher; The lights were unchanged.

The archives: `dist/Ribterra-Windows-x64-0.25.9.zip`, 158808115 bytes. SHA256: `4668600d7e3e8d152949b70cb692444f173c0b1f013cef15f0fdf7f069b0ede6`. Integrity ZIP checked; four EXE coincide between the set, the installation and ZIP. HUD, style, Ukrainian localization, launcher, README and description of the update are also faithful. The evidence is `artifacts/production-0.25.9/package-verification.json`, `installed-renderer.json`, `launcher-renderer.json`, neighboring `.zip.sha256`.

## Limitations

This is a ready **local** package. The Git committee, push and publish GitHub Release failed. Previous unmodified 0.25.8, hidden Expeditions» and plan documents are preserved. The full set of all CTest and external internet/NAT were not verified. Visual scripts are rendered by software; The installed game is individually checked via Direct3D 12. The correction of the rule of independent nations remains 0.25.10.

The version of the rules — 15; the file format — prefix, the protocol — 19. A new flag of abolition has been added to the JSON economy; Old records without it read like regular active orders. The client and server for the shared game must be the same version.

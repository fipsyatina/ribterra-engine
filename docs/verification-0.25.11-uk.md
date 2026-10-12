# Ribterra verification 0.25.11

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

23 September 2026. Windows Release, MSVC, `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, database `0fe2454` with local changes 0.25.8–0.25.11.

## Result

The shared alpha policy is applied to the World, a single client, host, and a separate server. The new catalog has Tables and **12 building types**. Seven deferred facilities, new tools/lists, expeditions and attacks are unavailable in the simulation. The code, schedule and data of future buildings are kept; Workshop and Clay quarry are available. The reserves separate materials, raw materials, ready-to-eat food, reserves, cargo, and items on the ground.

## Checks

**20 different CTests were performed: ** alpha_rules, alpha_rules_ui, buildings, building_inspector, economy, production_control, production_control_ui, coop_start, settlement, wilderness, persistence, lobby, server, network_session, network_join, network_lobby_join, protocol, localization, ui, ui_lifecycle.

The initial progenitor is `build/02511-tests.log` — 17/20. The overlap of the resource concealment class with the HUD theme and the old expectation of a full catalogue were revealed. Later, a delivery check revealed an embedded scrolling, through which the cancellation button was not available: the scrolling area was fixed, and the re-pressing after the construction update was completed. The modified test-safe generator has dropped the mandatory empty vectors; It's corrected to check the compatibility of the rules, not the error of the file structure.

The final confirmation: `build/02511-final-tests.log` (buildings, alpha_rules, wilderness), `build/02511-ui-final2.log` (**2/2**, 8,80 (c), `build/02511-remaining-tests.log` (**3/3**, 89,73 (c). Machine-readable summary of the final result of each test — `artifacts/alpha-0.25.11/test-results.json`. The full set of all CTest didn't start.

- **AlphaRulesTests:** all 20 catalog records are saved, allowing exactly 12 new types. Directly prohibited construction orders do not change the world. Both GameServer modes — without a lobby and with lobby/start — exclude hidden buildings and expeditions. The completed historical building and old tools/lists/skins survive save/load. Uncompatible states are turning away from world change.
- **BuildingTests:** actual construction and physical delivery of materials for each of the 12 types.
- **EconomyTests:** launch and delivery of all 13 permitted recipes; Both of the deferred prescriptions are deviating. Material/food chains, a break at night, daytime extension and improvement of production remain operational.
- **ProductionControlTests/Probe:** two batches of boards, explicit adult selection, assignment preservation, cancellation at reservation/transfer/manufacture/delivery boundaries, return of two raw-material types, and continuation after save/load.
- **WildernessTests:** passive wildlife moves; 300 ticks near a wolf and boar do not harm residents or start Attacking. Directly deferred actions are reverted without change. A separate test rejects a saved active Attacking state, and attack execution is additionally disabled in the simulation loop.
- **PersistenceTests:** Five previous rule files 16 with active attack, expedition, reduced packages, ordering tools or hidden unfinished construction give the corresponding reason `alpha.save_*`. The current world, the output file bytes and the absence of a new backup are checked after the refusal. The traditional and old supported formats have passed the rest of the persistence set.
- The actual rules-**16** save from `artifacts/coop-0.25.10/legacy-0259.save` is copied to `artifacts/alpha-0.25.11/legacy-02510.save`. The server loaded the copy, executed 20 ticks, and saved it with rules **17**, exit code 0; the source remained at rules 16. Log: `build/02511-legacy-save.log`.
- **CoopStartProbe:** Joint teams No1 and No2, construction of the Fox Mill, two batches/12 boards, equal customer results, employee transfer and protection of another building. All network checks are done on the same PC.
- Five languages, ICU, fonts, native RmlUi, 154 staged-assets and `git diff --check` have passed.

## The image of the real interface

`artifacts/alpha-0.25.11/` contains **10 frames 1440×900**, and `compact/` contains another **10 frames1100×700**. The compact probe completed successfully (`build/02511-alpha-compact.log`). The HUD, starting and scrollable stockpiles, and Workshop in a compact window were reviewed. The view shows 6 boards in transit and 0 in the stockpile, with finished food and historical items shown separately. The stockpile panel scrolls with the mouse wheel; the cancel button remains available. The old Research House has an explanation without an active action. The absence of Research preview, seven resources, temperature, idle skills, and forced decisions was verified.

## Package and launch

Set to `dist/releases/0.25.11/Ribterra`; `dist/current-build.txt` indicates the new EXE. The established game and the default `Play Ribterra.cmd` have passed `--smoke`, both with code **0**. The reports confirm **0.25.11, Direct3D 12, NVIDIA, GeForce RTX 3050 Laptop GPU, software=false**. Both Desktop launchers point to this standard launcher and remain unchanged.

ZIP: `dist/Ribterra-Windows-x64-0.25.11.zip`, **158780985 bytes**. SHA256: `5a4d710c0baee0e48405fd0b6dc9364a9ac8cc262b709caf0e025993eb93b797`. Verified CRC of all ZIP; four EXE is the same between the collection, the installation and the archive. HUD, style, five languages, launcher, README and description of the version match the sources. All seven images of the hidden buildings are preserved. The evidence is `artifacts/alpha-0.25.11/package-verification.json`, `installed-renderer.json`, `launcher-renderer.json` and neighboring `.zip.sha256`.

The rules are **17**, the protocol **19**, the format of the file **8**, the generator **5**. Git-comit, push, and GitHub Release were not executed.

**Checking on two physical Windows PCs NOT WORKED:** second PC is unavailable, as confirmed by the user. This remains an open criterion 0.25.10; local tests do not confirm the external network or NAT. The complex active states of the old caves await safe migration 0.25.12.

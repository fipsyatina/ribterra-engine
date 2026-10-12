# Ribterra verification 0.25.12

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

23 September 2026. Windows Release, MSVC, `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, database `0fe2454` with local changes 0.25.8–0.25.12.

## Outcome and Limitations

Supported worlds 0.25.7–0.25.10 (formatting 8, rules 14–16, generator 5) are opened through a verified new copy next to the original. The single game and host continue to keep a copy. The original is preserved intact. Reduced portions of all nations are normalized, old attacks and timers of animals are dropped without changing health and goods. All active phases of expeditions deviate with an explanation.

Completed old buildings, improvements, owners, occupants, and inactive objects are preserved. Forced labor, active tool/list production, and unfinished hidden construction remain incompatible. Old format converters up to 0.25.7 with backup copies have not been replaced by universal migration. The new warranty is limited by the conditions described; Details  [ description of the update](update-0.25.12-uk.md).

## Origin of control files

In `tests/fixtures/alpha-0257/` the **9 files `.ribsave`** are stored, the expected data and `provenance.json` from SHA256. They were recorded by the code **0.25.7** from the committee `0fe2454f45aeb4087f5832fe0c42d7b8303bac99`, separately collected from unchanged old sources. Control state generator — `tests/fixtures/capture_alpha_0257.cpp`. These are synthetic states tested by an old simulation; The normal cargo is the product of the old production. The version number in the finished file has not been changed. The manifesto also contains Git-hashes of old key sources and SHA256 libraries and generators.

Five supported states: historic structures/objects, shortened portions, active attack, ordinary cargo and their supported combination. Four are rejected: expedition departure, hunting, return, self-defense.

## Checks

**16 different CTests were performed:** save_compatibility, save_compatibility_ui, persistence, server, lobby, network_session, network_join, network_lobby_join, production_control, production_control_ui, coop_start, alpha_rules, ui, ui_lifecycle, localization, protocol.

The main family is: `build/02512-tests.log`, **16/16**, 73,97 p. After the server recovery clarification, repeat save_compatibility/server/lobby: **3/3**, 19,53 with (`build/02512-final-tests.log`). After final messages and compiling, repeat save_compatibility_ui/ui/localization: **3/3**, 6,39 c (`build/02512-ui-final.log`). The final result of each test is recorded in `artifacts/compatibility-0.25.12/test-results.json`. The full set of CTest didn't start.

- Each of the five supported files is saved and uploaded twice with an exact match to the status check. A new migration does not start when the copy is reopened. Identifiers, types, quantities, owners and locations of all items, buildings, levels and occupants were compared with the old generator data.
- In both loaded states **6 the tablets are delivered once **. After completion, another 220 tactic is performed: no additional goods appear, no non-food items are lost. Food is consumed normally after the simulation begins. In both of these cycles, the whole thing, including the food, is exactly the same.
- Old portions are off in all nations. The old attacks are cleared; In the case of non-production loads 300, the following tactics do not harm the inhabitants.
- Refusal in all four phases of the expedition does not alter the world, file or catalogue. The corrupted check amount, the deleted file, the unknown format/rules are deleted without changing the source. The road is verified with Ukrainian symbols.
- Old-fashioned storage after a successful transfer, host storage, and server completion do not rescript the original. Re-opening the copy keeps the game going. Publishing a copy does not replace an existing file.
- Retired master from team No2. The invalid data of the network room does not replace the active world and player. A rejected load in a local client does not destroy the current session.
- Preliminary inspections of production, cancellation, lobbying, joint commands and property protection were carried out locally. For current rules 17, states prohibited and continue to be derogated; The normalisation only applies to the old rules that are maintained.

## Real interface and server

`artifacts/compatibility-0.25.12/` contains **8 frames 1440×900**, `compact/` — further **8 frames1100×700** from the real SDL/RmlUi. Reports of a new copy, normal portions and a clean attack, four shipment rejections, damage, unknown format and host rejection. After the refusal, the action of the new game is available. The usual and compact frames are reviewed; The status does not cover the enterprise panel name or the verification button. The compact case is `build/02512-compact-final.log`, `compact/report.txt`.

A separate `RibterraServer.exe` loaded a copy of the combined old state, ran 20 ticks, and exited with code 0. Relaunching with the reported `save_path` continued from the copy and exited with code 0. The returning-expedition file is rejected with code 1 and reason `alpha.save_expedition`. Both source SHA256 values are unchanged. Evidence: `cli-verification.json` and `build/02512-cli-first.log`, `02512-cli-resume.log`, `02512-cli-rejected.log`. To run the separate server again, pass the copy path to `--world`.

## Package and launch

Set to `dist/releases/0.25.12/Ribterra`; `dist/current-build.txt` refers to **0.25.12**. The set game and the default `Play Ribterra.cmd` have been launched `--smoke`, both with the code **0**. The reports confirm **0.25.12, Direct3D 12, NVIDIA, GeForce RTX 3050 Laptop GPU, software=false**.

ZIP: `dist/Ribterra-Windows-x64-0.25.12.zip`, **158818064 bytes **. SHA256: `ef5eeb3f6da4c2aa960bb5ae4c5953a33481c68727e07089a10c033078961f3a`. Verified CRC of all ZIP; four EXE is the same between the collection, the installation and the archive. The panels, styles, five languages, launcher, README and version description match the sources. Images of all seven hidden buildings are preserved. The evidence is `artifacts/compatibility-0.25.12/package-verification.json`, `installed-renderer.json`, `launcher-renderer.json` and neighboring `.zip.sha256`.

The rules are **17**, the protocol **19**, the format of the file **8**, the generator **5**. `git diff --check` passed. Git-comit, push, and GitHub Release were not executed.

**Checking on two physical Windows PCs NOT WORKED:** second PC is unavailable, as confirmed by the user. This remains an open criterion 0.25.10. Local network tests do not confirm the external network or NAT.

# Ribterra verification 0.25.10

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

23 September 2026. Windows Release, MSVC, `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, database `0fe2454` with local changes.

## Outcome and extent of reception

A master's joint command is implemented by default for new people, explanation of the lobby settlement, access to own production in an authorized nation, server protection of other facilities, management of the production worker, and localized reasons for refusal. Before launching, the server does not open the initial world to lobbyists. Old commands don't merge.

**Checking on two physical Windows PCs is NOT FACTED.** The user confirmed that the second PC is unavailable, and asked to mark the check as unchecked. All of the above network results are obtained on one physical PC via a local loop. It's not a confirmation of LAN/VPN, an external Internet connection, or NAT between two PCs. [The script](coop-two-pc-0.25.10-uk.md) that was produced remains an unmet reception criterion.

## Functional verification

- **16/16 CTest, 82,76 c:** lobby, server, network_session, network_join, network_lobby_join, production_control, production_control_ui, economy, building_inspector, localization, ui, ui_lifecycle, protocol, persistence, host_lobby_flow, online_entry_flow. The magazine: `build/02510-final-tests.log`.
- Additional passes **coop_start** (9,74 (c), repeated ui and localization after a change in lobby style, and **lobby_client_rendered** (4,86 (c). Various CTest 18 were tested together. The journals: `build/02510-rendered-tests.log`, `build/02510-lobby-final.log`. The first magazine retains the original error of the lobby script: it considered the guests a separate team and did not wait for a re-appointment before the "Ready" The test now clearly assigns independent commands; The final replay is over. The full set of all CTest didn't start.
- `CoopStartProbe`: separately the shared commands **No1 and No2**, two real network ClientSession and authoritative NetworkServer. The guest inherits the master's team; The original inhabitants and the buildings collide. The logging is done by a regular team with physical delivery of building materials.
- The actual RmlUi verified the owner, available production form, building list, and entry of two batches. While finished products are delivered, economy and logistics match on both clients. After two deliveries, the stockpile contains **12 boards**. The guest receives a rejection when trying to cancel an order controlled by the host; the visible message names the host. After the worker is explicitly transferred, the guest runs another batch; both clients see **18 boards**.
- The late addition of a third participant inherits the team and keeps the available supplies. It does not create a new settlement instead of a common one.
- The independent **No2** team builds and runs its own Forestry. Its construction does not cost the wood/stone of the No. 1 team; The manufactured 12 board is of 2. The attempt by Team No1 to order a foreign construct after the failed ID is rejected as `economy.foreign_building` without a new production order.
- Three incompatible clients — other protocol, rules or content  rejected. For each, the corresponding Ukrainian message was checked in the following menu.
- `LobbyVisualProbe`: six local UDP- clients, default community, clear individual commands, readiness, start, chat, synchronous timing, add/remove bots, pause/speed rights, and copying of the selected network address. An additional full vacancy in **1100×700** passed: `build/02510-lobby-compact.log`.
- The actual 0.25.9 save with rules **15**, containing a cancellation during delivery (`artifacts/production-0.25.9/cancelling.save`), is copied to `artifacts/coop-0.25.10/legacy-0259.save`. The 0.25.10 server loaded it, ran 20 ticks, and saved rules **16**, exiting with code 0. The original save remains unchanged. Log: `build/02510-legacy-save.log`.
- Five languages passed key checks, ICU and fonts; native RmlUi and staging 154 assets passed. Modified CoopPerformanceProbe is assembled, but the full load scenario is not running. `git diff --check` passed.

## Visual Evidence

`artifacts/coop-0.25.10/report.json` contains the result of the new cooperative scenario and **23 frameworks**. Reviewed `team-2-lobby.png`, `team-2-guest-output.png`, `team-2-rejected.png`, `nation-2-delivered.png`, `network.version_protocol.png`, and also `lobby-compact/lobby-default-coop.png`. They show the No2 command, the product on the road, named controller, completed own order, localized non-compliance, and compact lobby. Visual tests are run through software rendering; the installation of the game is checked separately on GPU.

## Package and launch

Set to `dist/releases/0.25.10/Ribterra`; `dist/current-build.txt` indicates his EXE. The fixed EXE and the standard `Play Ribterra.cmd` completed `--smoke` with the code **0**. Both reports confirm **0.25.10, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, software=false**. Both available Desktop launchers are aimed at the standard launcher; The lights were unchanged.

The archives are `dist/Ribterra-Windows-x64-0.25.10.zip`, **158816454 bytes**. SHA256: `614975b32e5e6a352aa517cf24839b78ef6dab109f7f7faf7f105fa167f33af6`.

The integrity ZIP is verified. The four EXE converge between the set, the installation and ZIP; menu/HUD/style, five locations, launcher, README, version description and script of the two PCs matched with sources. The evidence is `artifacts/coop-0.25.10/package-verification.json`, `installed-renderer.json`, `launcher-renderer.json`, neighboring `.zip.sha256`. The package contains previous local changes 0.25.8/0.25.9 and the hidden «Expedition» panel.

Rules **16**, protocol **19**, the format of the file — **8**, generator **5**. For a joint game, both sides must use the same full package. The Git committee, push and publish GitHub Release failed.

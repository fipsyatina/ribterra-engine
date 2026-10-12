# Ribterra verification 0.25.6

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 20 September 2026. The local Windows release, MSVC.

## Correction

Open HUD immediately after `connect_remote`. The client expects all parts of the full picture to be applied, then shows the authoritative lobby or ready world. The initial delta packet does not create an empty base. After the network is lost, the image display is discarded; The change in world id or control epoch also makes the customer state change. There are five languages for the waiting time.

## Results

Successful 18 of various targeted CTest checks:

- `lobby`, `server`, `protocol`, `network_conform`, `network_session`, `network_join`, `network_lobby_join`, `chunk_network`.
- `lobby_client_rendered`, `localization`, `ui`, `ui_lifecycle`, `client_input`, `host_lobby_flow`, `online_entry_flow`, `client_smoke`, `network_client_rendered`.
- `network_adverse_3pct`: 150 mS of the base RTT, 3% accidental loss of UDP- packets, duplication, change of order and temporary disconnection. The test was completed in 137,52 p.

New checks deliberately delay the handshake and portions of the full image, reject the original delta, and check the full image recovery. The real server is checked with the guest before, after, and after the backup is loaded. The state of the territory, residents, and guest stocks coincides with its server projection. Returning an authenticated host confirms the preservation of its territory, residents, and reserves after a new player joins.

The graphics test uses six real local UDP- clients: lobbying, teams, readiness, startup, chat, nation, and time synchronization. A verification is added that the forced open HUD closes before the first server packet and the connection message is visible. A separate test of the client checks the readiness of the server after the server is turned off.

The installed package `dist/releases/0.25.6/Ribterra` went through a separate dedicated server launch + two independent processes RibterraNetProbe + a graphics client Direct3D 12. Both probes received the same status after construction: 10 residents, 2 buildings, 50 trees; I've checked the chat, the response to the orders and the delivery of the materials.

The collection was completed successfully; The full set of pre-warnings in the simulation and rendering files remain. `git diff --check` passed. The full set of CTest tests in this patch did not run.

## Evidence and limitations

- The journals are: `build/0256-build.log`, `build/0256-final-build.log`, `build/0256-network-tests.log`, `build/0256-client-tests.log`, `build/0256-adverse-tests.log`, `build/0256-installed-network.log`.
- In the first network run, the retention check compared the project of the disabled host, which is intentionally empty. The test is fixed: the host resumes its session before the comparison. Corrected `network_lobby_join` went through the journal `0256-client-tests.log`.
- The frames are: `artifacts/online-0.25.6/connecting.png`, `lobby.png`, `world.png`, `installed-network.png`.

The physical second computer and external Internet in this session are not available for verification. Network processes and simulation of an unfavorable connection were launched on one PC. For user verification, both PCs must run a fully unpacked 0.25.6 package.

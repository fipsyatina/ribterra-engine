# Ribterra 0.26.3

> English translation of a historical project document. Technical literals and recorded results are preserved; they describe their named snapshots and do not validate the current build. Original wording is available in import commit `7388a2f`.

[Build and validation retention](docs/ARTIFACT_RETENTION_UA.md): after replacement, retain the current package and one verified rollback version instead of full copies of every test runtime. Announce exact paths before cleanup; source code, original art, saves, and active dependencies are protected.

**V01–V03 rework, 30 September — PARTIAL:** the [acceptance list](docs/visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md) and [final report](docs/verification-rework-0.26.3-uk.md) separate implemented code from artistic fidelity that remains unconfirmed. 91/91 CTests and 60 native D3D12 matrix cases passed. The locally verified package launches through `Play Ribterra Rework.cmd`; the ordinary `Play Ribterra.cmd` uses a separate published-release path.

[Buildings and residents V03 in 0.26.3](docs/update-0.26.3-uk.md): existing detailed sprites now use one runtime resolver, stable scale, and ground contact. House II and Workshop II now have distinct appearances, directional residents are easier to read, and cargo and work effects show only actual state. [Local validation](docs/verification-0.26.3-uk.md).

[Valley, river, and worn paths V02 in 0.26.2](docs/update-0.26.2-uk.md): a new deterministic surface profile, shared movement/building rules, grouped nature, and paths gradually left by units. The manual Road was removed; Bridge remains. [Local validation](docs/verification-0.26.2-uk.md).

[Compact HUD V01 in 0.26.1](docs/update-0.26.1-uk.md): resources, warnings, a compact inspector, and contextual actions are arranged around the screen edges. Full panels and all current commands remain available. The world, simulation, protocol, and save format were unchanged. [Validation](docs/verification-0.26.1-uk.md).

[Materials and upgrades 0.26.0](docs/update-0.26.0-uk.md): House II (+4 capacity) and Workshop II (work phase twice as fast) are available for 6 planks and 4 bricks. Stockpile II is hidden. [Validation](docs/verification-0.26.0-uk.md).

[Post-action crash fix 0.25.13](docs/update-0.25.13-uk.md): resident selection, construction, and order notifications no longer pass ready-made text as a translation key.

[Safe old worlds 0.25.12](docs/update-0.25.12-uk.md): supported 0.25.7 saves migrate into a new copy, yields use normal quantities, old attacks are cleared, and goods persist. Active expeditions are rejected while preserving the source save. [Validation](docs/verification-0.25.12-uk.md).

[Alpha storage 0.25.11](docs/update-0.25.11-uk.md): Camp and 12 buildable types, real inventories, and shared client/simulation constraints. Workshop and Clay Quarry remain; new expeditions and attacks are disabled. [Validation](docs/verification-0.25.11-uk.md).

[Cooperative start and ownership 0.25.10](docs/update-0.25.10-uk.md): one shared team by default, team-owned enterprises, and explicit worker control. [Local validation](docs/verification-0.25.10-uk.md). **Testing on two physical Windows PCs was not performed because a second PC was unavailable.**

[Production management 0.25.9](docs/update-0.25.9-uk.md): batches, explicit worker selection, profession persistence, and safe cancellation while carrying cargo. [Validation](docs/verification-0.25.9-uk.md).

[Building inspector 0.25.8](docs/update-0.25.8-uk.md): state, idle reason, recipe, and physical delivery. [Local validation](docs/verification-0.25.8-uk.md).

[Current first-alpha and demo plan](docs/ALPHA_DEMO_PLAN_UA.md): complete settlement development, material chains, and existing upgrades, then prove external cooperative play. Workshop and Clay Quarry remain. The Expeditions panel and its pop-up offers were removed from the UI; earlier descriptions of that panel below and in version logs are historical.

[Actual feature audit](docs/FEATURE_AUDIT_UA.md): every building, resource, recipe, profession, and displayed placeholder, extended with a new-world check through simulation commands. Future systems were hidden in 0.25.11; the remaining plan continues in later versions.

[Detailed small-release plan](docs/ALPHA_DEMO_SMALL_RELEASES_UA.md): additions, removals, improvements, and criteria for each release. V01 was implemented locally as 0.26.1, V02 as 0.26.2, and V03 as 0.26.3; V04–V05 remain separate graphics stages. Acceptance on two physical PCs remains pending.

[Online lobby and commands 0.25.0](docs/update-0.25.0-uk.md).

After manual orders: [30-second timeout in 0.24.1](docs/update-0.24.1-uk.md).

Working expeditions, hunting, and living surroundings: [rules for 0.24.0](docs/update-0.24.0-uk.md).

# Ribterra

Private development of a cooperative isometric strategy game about settlement, survival, and named residents. The authoritative requirements source is revision 1.4 of the plan dated 7 September 2026 under `docs/source`.

Code **0.10** adds a queue of up to eight manual orders per resident, partial results for group Move/Gather, and an explicit gather mode: one delivery or until the resource is exhausted. The Orders panel shows the queue and completion reasons. One order can be cancelled, while control of a resident can be released or transferred to another connected player. Physical cargo completes delivery before a profession change. The [manual-order contract](docs/MANUAL_ORDERS.md) extends [professions](docs/PROFESSIONS_PLAN_UA.md), [eight independent skills](docs/SKILLS.md), and [physical logistics](docs/LOGISTICS_PLAN.md).

**Local version — 0.23.0.** Enterprises receive bounded orders with a worker, materials, and physical delivery of output. Sawmill, Mill, Kitchen, Clay Quarry, roads, and initial upgrades were added. The Enterprises button is at the lower left. [Rules and recipes for 0.23.0](docs/update-0.23.0-uk.md).

**Update 0.18.0.** Satiety 10/10 means a well-fed resident; four new appearances with walk animation, 12 new trees, and 6 grass textures. [Changes in 0.18.0](docs/LIVING_WORLD_0_18_UA.md).

**Cursor and coastline 0.16.1.** The system renders the cursor independently from game frames, and shores received smooth transitions and gentle slopes. [Changes in 0.16.1](docs/COAST_CURSOR_0_16_1_UA.md).

**Atmosphere 0.16.0.** Biomes blend smoothly, fog follows the terrain with a soft boundary, and the view distance increased by 1.5×. The food-shortage warning explains health loss from hunger. [Changes in 0.16.0](docs/ATMOSPHERE_0_16_UA.md).

**Farm work 0.15.0.** A Farm produces food only through residents working on site. Farmers harvest crops and carry them to storage. Select residents and right-click a completed Farm. [Changes in 0.15.0](docs/FARMING_0_15_UA.md).

Version 0.14.0 added 10 buildings with separate textures, construction stages, winter appearances, and catalog icons. A Mine must be built; the Fishing Station operates on a shore. [Buildings in 0.14.0](docs/BUILDINGS_0_14_UA.md).

Volumetric terrain: hills, depressions, mountain slopes, and raised plateaus form one continuous surface; residents and objects account for ground elevation. New terrain is generated in new worlds. The map is 832×832 (692,224 cells per layer, 10.56 times larger than 256×256), with lakes, forests, clearings, mountains, deserts, and permanent snow variants. Unexplored territory is covered by fog using existing assets. [Terrain in 0.13.0](docs/TERRAIN_0_13_UA.md).

**Historical acceptance for 0.10.1.** The paused update missed because of a delayed ACK was fixed; automatic reconnect and safe replay of unacknowledged orders were added. Local checks passed 52/52 tests, two network profiles at 150 ms RTT with 1%/3% loss, the GPU check, and 4/4 extracted-package checks. CI source `13ed5c2` passed Windows 52/52, Linux 35/35, and UBSan 13/13 without C++ warnings or errors. [Changes and launch for 0.10.1](docs/RELEASE_0_10_UA.md), [evidence and validation limits](docs/VALIDATION.md).

The complete engine, P0/P1, and Steam release remain future work. All 38 original plan rows and the user-added assets, large concept-based terrain, and coordinate indicator in the [requirements register](docs/ENGINE_REQUIREMENTS.md) remain in force. Version 0.11.1 updates presentation; changing textures does not add later engine systems.

## Local game on Windows

**Play Ribterra.cmd** launches the current verified build. In an extracted ZIP, open `Ribterra/Ribterra.exe`. The commands below are for the repository directory; in the portable package, run the corresponding executable directly from the Ribterra directory.

Launch the packaged build from the repository:

```powershell
.\dist\releases\0.21.0\Ribterra\Ribterra.exe
```

A local game uses the same authoritative `GameServer` as dedicated mode. The graphical client sees only its filtered state snapshot and submits commands through the validated server queue.

Controls:

- Left mouse button selects a resident; Shift adds to the selection; A selects everyone.
- Right mouse button moves or gathers a resource. On a completed Farm, it assigns selected residents as farmers. Shift + right-click appends a move/gather order.
- **Orders** on the selection card controls gather mode, queue, single-order cancellation, release, and transfer.
- H / F / B selects House / Farm / Stockpile; left-click places it.
- **Inventory** on the top bar shows available and reserved items, cargo, construction deliveries, and worker status; active construction can be cancelled.
- Arrow keys / middle mouse button move the camera; the wheel zooms; C centers.
- Page Up / Page Down shows Surface or Mine; these keys change only the view.
- Space pauses; 1 / 2 / 3 selects 1× / 2× / 4× speed.
- F1 hides/shows the complete HUD; F5 saves; F9 loads; F12 captures a screenshot.

Local save: `%APPDATA%\Ribterra\Ribterra\settlement.save`. Package 0.10 uses format 8: manual queues and executor links were added to chunks, layers, portals, professions/XP, routes, physical items, and work intents. Migration 7→8 preserves old search, logistics, and XP and retains the exact original at `.v7.bak`; 25 genuine v7 fixtures were checked across all 65 historical control states. Control rights belong to the session: loading preserves orders but starts a new control epoch. The FlatBuffers payload is compressed with zstd in a 96-byte `RIBSV2` container. Earlier explicit migrations 6/5/4/3/2 and text format 1 remain; details and limits are in [SAVE_FORMAT.md](docs/SAVE_FORMAT.md). Local-client autosave is not implemented yet.

Wood, Stone, and Food exist on the ground, in storage, in a resident's hands, or at a construction site. The top bar shows available quantities; Inventory explains reservations and totals. Starting resources are stored in Camp. A resident carries up to 10 units of one kind, traverses a mine portal, and physically places cargo. Construction materials are reserved immediately but consumed only after full delivery. A blue site bar shows delivery; a gold bar shows work. Cancellation leaves materials where they are; returning them to storage requires transport. Roads, route danger, the complete resource catalog, and production recipes remain pending.

After delivery, a resident may wait to return to gathering when the path queue is full. The HUD distinguishes this automatic wait from an error and from normal completion caused by resource exhaustion. A paid construction without a worker preserves progress and waits for an available resident of its nation; work resumes after arrival. The panel shows whether builders are waiting, travelling, or working. An accepted manual move or gather replaces the previous automatic intent; a rejected order does not change it. If a newly accepted route later cannot be found, the old intent is not restored automatically.

The RmlUi menu is available before world creation. It stores language and UI scale in `%APPDATA%\Ribterra\Ribterra\ui-settings.json`. The HUD shows resources, day, population, selected resident, layer and path state, pause/speed, construction, save/load, help, and local or network chat. `Queued`, `Applied`, `Rejected`, and typed rejection reasons have messages in English, Ukrainian, Russian, Polish, and Spanish through ICU MessageFormat. Surface/Mine buttons and Page Up/Page Down switch the view without moving units. Use passage sends an ordinary server transition command for a selected resident near a known portal. FreeType renders staged Noto Sans. F1 hides the complete HUD without pausing or leaving the world; pressing it again restores the HUD. Continue restores a hidden local or remote session without restarting it.

The **Profession and Skills** button opens assignments for the selected resident or group: automatic work, workplace selection on the map, five priorities, and Stop. Lumberjack, Miner, Gatherer, Builder, and Carrier are available; Farmer, Blacksmith, Medic, and Guard do not yet have execution scenarios. Hover or keyboard focus on a skill shows the server breakdown, including while paused. New Assign/Stop clears the old workplace-selection mode, while replacing the world clears the selection and an unfinished form. Professions and skills were introduced in 0.9 and are available in the current 0.10 package.

## Dedicated server and direct IP

The server listens on `127.0.0.1:27020` by default. Set a password and start the world in the first PowerShell window:

```powershell
$env:RIBTERRA_SERVER_PASSWORD = '<strong private password>'
.\dist\releases\0.21.0\Ribterra\RibterraServer.exe --bind 127.0.0.1 --port 27020 --world .\worlds\settlement.save
```

Set the same password and connect the client in a second window:

```powershell
$env:RIBTERRA_SERVER_PASSWORD = '<strong private password>'
.\dist\releases\0.21.0\Ribterra\Ribterra.exe --connect 127.0.0.1:27020 --name 'Player 1'
```

Ctrl+C cleanly saves the server world. When `--world` is present, the server also saves it every 60 seconds. A remote client cannot use F5/F9 because the server owns the save.

For manual direct IP on a local network, run the server with `--bind 0.0.0.0`, allow the UDP port through the firewall, and connect to the host's LAN address. Automatic LAN discovery is not implemented, and operation on two physical PCs is not confirmed by the current validation.

Package 0.10 aligns **protocol 7/rules 7/generator 2** and gameplay hash `5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606`. Client and server must use identical versions. The session uses challenge/password proof, a reconnect token, separate priority channels for commands, chat, and snapshots, plus full and delta snapshots with ACK/resync. Large chunk snapshots split into at most 128 parts. The client assembles them atomically with a 16 MiB limit and 10-second timeout. Manual orders, phases, and control rights extend the owner's inventory, cargo, work status, professions, and eight server skill breakdowns. Internal practice timestamps, task IDs, reservation tables, complete routes, and hidden coordinates remain server-side. Local network smoke and CI 0.10 passed; adverse-network and physical-PC validation remain planned.

A command first receives `Queued` and, after server validation, `Applied` or `Rejected` with a typed reason and per-resident outcomes. For Move/Gather, `Applied` means that the manual order entered the queue. Later path search, movement, gathering, delivery, and completion appear separately in the order projection. Internal logistics routes and work intents do not create responses to player commands. A terminal reply also clears the unacknowledged `Queued` state; duplicates, reordered phases, and reconnect must not produce contradictory results. After disconnect, control is retained for 30 seconds of monotonic time, including while paused: a short reconnect restores it, while expiry performs Release without losing physical cargo delivery. Terminal replies remain available for reconnect during the process lifetime. The graphical client tries to return to the same session automatically for 30 seconds after detecting an interruption. Unacknowledged orders younger than 30 seconds replay with the same ID and complete payload only in the same world, for the same player, token, and control epoch. Server deduplication prevents a second effect. A stale or incompatible intent does not start automatically; the HUD asks the player to inspect the settlement. Replayed chat history is deduplicated. Session tokens and the request cache are not yet restored after restart.

## Building

Required: Git, Python 3, Visual Studio Build Tools with **Desktop development with C++**, the Windows SDK, and C++ CMake tools. From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
```

The script verifies and extracts SDL3 3.4.16 and SDL_ttf 3.2.2, prepares vcpkg at pinned baseline `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4`, builds Release, runs CTest, and after success installs the current code under `dist/releases/0.15.0/Ribterra` while preserving open previous versions. `Play Ribterra.cmd` points to 0.15.0 through `dist/current-build.txt`. Build-time tools compile HLSL into DXIL/reflection and generate `data/skills.json`, validate the asset catalog, five ICU catalogs, font glyphs, and RML/RCSS. `AtlasTool` separately exports PNG atlases with metadata from input JSON. The package contains only staged assets and ready shaders, without runtime compilers. The headless configuration provides CMake option `RIBTERRA_ENABLE_UBSAN` and preset `headless-ubsan`; current 0.10 results are in [VALIDATION.md](docs/VALIDATION.md). Asset details are in [ASSETS.md](docs/ASSETS.md). The first build requires internet access. The local CMake directory is `build/engine-windows`.

The Windows client requests SDL_GPU backend `direct3d12` by default without a hidden fallback. `--software` enables a separate SDL_Renderer backend for automated tests; `--gpu-debug` requests GPU validation, and `--renderer-info <file.json>` records the selected backend/device/driver and software flag. That JSON describes backend selection but does not by itself prove physical-GPU execution or performance.

After a build, repeat the checks with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -SkipBootstrap
.\build\engine-windows\SimRunner.exe --ticks 1200 --population 10 --seed 70421
.\build\engine-windows\SimRunner.exe --benchmark
.\build\engine-windows\SimRunner.exe --benchmark-logistics
.\build\engine-windows\SimRunner.exe --benchmark-professions --ticks 10000
```

Create an archive of an already verified build with `powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -SkipBuild`. Without `-SkipBuild`, the script first performs a complete build and CTest.

Historically, after fix `d9e35cc`, local Windows build 0.5 passed **30/30 tests in 32.60 s** without MSVC warnings. `SimRunner` for seed 70421 and 160 ticks recorded checksum `9495560223965183407`. A large network snapshot containing all 128 chunks consisted of 19 parts in the verified scenario. Native D3D12 renderer and MineVisualProbe, a separate server plus two probes, the graphical GPU client, and repeated smoke of the extracted corrected archive passed. These results are not validation of 0.7 code.

[CI 0.5](https://github.com/fipsyatina/ribterra/actions/runs/34213349737) for initial commit `036b08b` had Windows success, 30/30 in 26.88 s, and Linux 10/19. Nine Linux tests ended with `std::bad_alloc` because of undefined behavior: coordinates were multiplied as signed `int` before conversion to `uint32_t` in the terrain generator. `asset_tool` passed in that run. Commit `d9e35cc` performs multiplication as `uint32_t`, adds three boundary terrain regression checks, and removes a GCC warning in a structured binding. The [new CI for the fix](https://github.com/fipsyatina/ribterra/actions/runs/34214848608) had Linux success: 19/19 in 15.06 s and four targeted UBSan suites in 1.68 s without runtime errors or compiler warnings; Windows also succeeded: 30/30 in 27.86 s. The complete engine, 60 FPS, Steam, a physical LAN session, and the complete performance gate remain open; current evidence is in [VALIDATION.md](docs/VALIDATION.md).

Verified milestone **0.6**: local 33/33 tests, D3D12 HUD in five languages, and smoke of the extracted archive. [CI `1ef93e9`](https://github.com/fipsyatina/ribterra/actions/runs/34221544539) had Windows 33/33, Linux 21/21, and UBSan 5/5. In the narrow planner fixture at 4×1500, all four orders completed, p95 tick was 3.069 ms, and p99 was 4.701 ms. The complete performance gate and the rest of the engine remain in progress; see [details and archive checksum](docs/VALIDATION.md).

## Documentation

- [Complete requirements and evidence register](docs/ENGINE_REQUIREMENTS.md)
- [Development status](docs/DEVELOPMENT_UA.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Work continuation and builder assignment 0.8](docs/WORK_INTENTS.md)
- [Professions: scope and acceptance criteria 0.9](docs/PROFESSIONS_PLAN_UA.md)
- [Skills, practice, and server modifiers](docs/SKILLS.md)
- [Manual queues and control rights 0.10](docs/MANUAL_ORDERS.md)
- [Control development plan](docs/CONTROL_PLAN_UA.md)
- [0.9 save fixture for future migration checks](tests/fixtures/WORLD_SAVE_V7.md)
- [0.10 save fixture for future migration checks](tests/fixtures/WORLD_SAVE_V8.md)
- [Physical logistics and its limits](docs/LOGISTICS_PLAN.md)
- [Actual validation](docs/VALIDATION.md)
- [Development backlog](docs/BACKLOG_UA.md)
- [Art direction](docs/ART_DIRECTION.md)
- [Source material](docs/source/README.md)
- [Dependencies and licenses](THIRD_PARTY_NOTICES.md)

Ribterra code is private; all rights reserved. Concepts from the document are AI-generated visual references, not game screenshots.

[0.25.1 — concept-based lobby and bots](docs/update-0.25.1-uk.md)

[0.25.2 — initial online screen and room-creation fix](docs/update-0.25.2-uk.md)

Update 0.25.3: [online smoothness and natural starts](docs/update-0.25.3-uk.md).

Update 0.25.4: [speed control and continuation of the online world](docs/update-0.25.4-uk.md).

Update 0.25.5: [smooth online movement and terrain-geometry caching](docs/update-0.25.5-uk.md).

Update 0.25.6: [waiting for the complete world and correct lobby entry](docs/update-0.25.6-uk.md).

Update 0.25.7: [network-address selection and internet connection](docs/update-0.25.7-uk.md).

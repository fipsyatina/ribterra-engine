# The development stage of Ribterra

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date of start: 8 September 2026 of the year. Source of requirements — provided by the author's editorial plan 1.4. Currently completed set **0.10.1: protocol 7/rules 7/generator 2/save 8** adds the name of the receiver Move/Gather, the steel ID and the rows of manual commands, single delivery or set until exhausted, cancelled, and transfer control between players. The conservation of rows continues the professions, skills and physical logistics 0.9. Final fix restores missed through delayed ACK images on pause and automatically re-connects the client with limited safe repeat of unconfirmed commands. Contracts: [hand commands](MANUAL_ORDERS.md), [nuts](SKILLS.md), [seves](SAVE_FORMAT.md); The current test results, GPU, package and CI — [VALIDATION.md](VALIDATION.md). All 38 initial requirements and V01–V03 remain in force; P0, P1 and full motion are not complete.

Historical 0.8 added GatherContinuation and BuildAssignment: retained waiting, bounded retry, resource depletion and builder replacement; It is a version of — save 6, protocol 5/rules 5/generator 2. [WORK_INTENTS.md](WORK_INTENTS.md) retains this contract. Its local 40/40 CTest, GPU, benchmark and package smoke and CI `f62cea0` (Windows 40/40, Linux 26/26, UBSan 8/8) has been passed; It's evidence of a previous version. FIFO-planner, two-phase path replies, server, RmlUi and SDL_GPU/D3D12- basis are saved.

## The current implementation

The graphics client and dedicated server use the same authoritative `GameServer`. Even a local game sends movement, assembly, and construction through a server queue, and the interface only receives filtered `ViewSnapshot`. The camera and UI do not have direct access to the recording to SimCore.

A separate `RibterraServer` receives direct IP:port through GameNetworkingSockets 1.6.0. It was accomplished:

- 2–4 networking sessions in one cooperative nation;
- the verification protocol/rules/content, name, request ID, limits and frequency of the commands;
- challenge/password proof and random reconnect token;
- the command's idempotent repetition, the session closure for the modified repetition, phases `Queued` → `Applied`/`Rejected`, typical path failure causes, phase-specific batched ack, and the cursor of the next request after reconnect;
- the individual priority channels of commands, chat and pictures;
- visible full image, delta from the confirmed baseline, ack and resync;
- chunk-full and chunk-delta images, maximum division by 128 parts and atomic folding on a client with a boundary of 16 MiB and a timeout of 10 seconds;
- Unicode server chat, rate limit, short history and mute at level API;
- dedicated autosave every 60 second if set to `--world`.

The 8 file format stores chunk-is, their revision, both layers, portals, EnTT-components, FIFO path batches, search/reconstruction/staging progress, physical stacks, warehouses, reserves, logistics, construction orders and work intents with retry/deadline/attempts and the planner's courses. Added occupation, priorities, manual/deferred status, eight scales XP/practice/abilities and a job choice cursor. The format 8 also retains the performers' manual lines and connections; genuine format 7 migrates without loss of professions/XP, physical objects and pending paths and leaves `.v7.bak`. Genuine format 6 migrates with preserving the old XP and the accurate planner/work/logistics state and `.v6.bak`. The container stores the `RIBSV2`, 96-byte header and FlatBuffers/zstd payload. Before replacement, the temporary file is checked; the previous slot is kept as `.bak`. The format 5 from `.v5.bak` transfers the unfinished extension of the collection to Work intent without re-searching or delivery. The 4/3/2 and 1 text formats and their backup suffixes are supported; The paid construction and physical reserves are maintained. Fixtures and byte-level contracts are described in [SAVE_FORMAT.md](SAVE_FORMAT.md). A single seed creates different worlds ID.

The resource has one physical position: land, warehouse, hands, or builder. The carrier shall carry up to 10 units of one species; pickup/dropoff uses internal routes, and the reserve does not allow you to take the same object twice. Construction work begins after the shipment is complete. Cancellation and failure to deliver material physically; The first thing that comes back to work is to check the limits. The previous 0.7 has checks on reservation, pickup, transfer, partial delivery and operation; A repeat local verification of these scenarios for 0.8 has been done, including all 17 GPU logistics frameworks. The **Supply panel** shows availability, reserve, total quantity, cargo, delivery, progress, workers and cancellation of the facility; The catalogs cover five languages.

A successful delivery remains complete regardless of return to collection. The separate work intent has Waiting/PathPending/Complete/Failed and separate failure and completion. Started means route transfer, Exhausted — normal depletion of the expected resource. The planner limits attendance to 128 and admission to 32 per tick, maximum 64 attempts per 1200 ticks. A paid Work-place worker without an employee is waiting for a free resident of the same nation; progress continues after arrival without a new subscription. The accepted manual replace the automatic intent, and the rejected setup retains it. Internal Work/Logistics paths do not create external CommandReply. Owner-only full/delta/removal transmits the status of work and workforce without duplicating inventory; The local [ reception and CI 0.8 ](WORK_INTENTS.md) are executed.

The native Windows build uses pinned vcpkg baseline and static release-triplet. A separate `Renderer2D` accepts a backend-neutral `Frame2D`: Presentation only forms the ordered geometry/text- commands Terrain → WorldOrdered → Fog → UI, while the renderer has SDL_GPU/SDL_ttf resources, letterbox input mapping, and offscreen capture. Windows startup asks D3D12; The software backend is only obviously turned off for testing. HLSL compils the host-tool `shadercross` in DXIL and reflection during assembly. For the headless added optional `RIBTERRA_ENABLE_UBSAN`, preset `headless-ubsan` and target CI suites; their current result is given in VALIDATION. Historically, the 0.5 Linux CI ran through all four of the then-existing suites without runtime errors or compiler warnings.

RmlUi forms the menu before creating the world and HUD the surface `Frame2D` without a separate render pass. New settlement, Continue, Settings, Help, Quit, resources/day/population, selected resident, its layer and path phase/failure, construction, pause/speed, save/load and server chat are available. `Queued`, `Applied`, `Rejected` and all typical path failures have messages in English, Ukrainian, Russian, Polish and Español. Surface/Mine and Page Up/Page Down just switch off the viewing layer. Use passage sends a server transfer command to a selected resident near a known portal. The focus of the text field blocks the game shortening; The chat and the names are displayed. F1 and Help Back keep the current local or remote session.

AssetPipeline allows only registered relative paths in the asset root and checks SHA-256 and metadata manifest. During staging localization/font check and native `UiCheck` reject incorrect directories, missing glyphs, RML/RCSS- errors, and unsupported render features until the previous set is replaced. A separate `AtlasTool` exports PNG- pages from UV/pivot, provenance, notices, and hashes. Details — in [ASSETS.md](ASSETS.md). The world is not yet being painted through the production atlas.

Historically after fixing `d9e35cc`, the local Windows runway 0.5 passed 30/30 tests for 32.60 with no warnings MSVC. `SimRunner` from the seed 70421 after the 160 ticks kept the checksum `9495560223965183407`. The big network scenario passed all 128 chunk in 19 parts and included full join, late join, transition to mining, prohibition of foreign nation governance, reconnect, timeout/resync, premature ack, and continuation of the safe. Repeated GPU, MineVisualProbe and unpacked server + two probes + GPU client passed; 23 PNG have been reviewed. These historical results do not confirm the current code 0.7.

The historical Windows 0.4 run passed 26/26 tests in 26.58 s with no MSVC warnings, and the physical D3D12 renderer and network GPU scenarios passed on an NVIDIA GeForce RTX 3050 Laptop. Windows CI 0.4 succeeded. [CI 0.5 for commit `036b08b`](https://github.com/fipsyatina/ribterra/actions/runs/34213349737) succeeded on Windows with 30/30 tests in 26.88 s; Linux passed 10 of 19. Nine tests ended with `std::bad_alloc` because a signed `int` overflowed in the terrain generator, while `asset_tool` passed. Commit `d9e35cc` converts operands to `uint32_t` before multiplication, adds three boundary-terrain regression tests, and fixes the GCC structured-binding warning. In the [new CI](https://github.com/fipsyatina/ribterra/actions/runs/34214848608), Linux passed 19/19 in 15.06 s and UBSan passed 4/4 in 1.68 s without runtime errors or compiler warnings; Windows also succeeded with 30/30 in 27.86 s.

## Limitations of the current game

The world 0.7 has 128 pre-created chunks 32×32: per 256×256 surface cells and mines within the coordinates `[-96, 160)`. This is a fixed network without streaming/unload. The open portal connects `(28, 24, Surface)` and `(28, 24, Mine)`. `MineOre` blocks movement; The resident picks it up from the next cell, and the worn-out tile becomes `MineFloor`. The first opening of the entrance adds only the known paired exit cell, not the radius of the mine.

EnTT 3.16 is the actual store of identity, position, activity, vitals, skills, routes and state of order. One FIFO batch uses a common reverse BFS from the target or its passing neighbors, a canonical order resident ID and no more than 4096 search, reconstruction, and staging units per tick. Successful batch routes are applied together; timeout, repeated topology changes and limits give a typical terminal rejection without partial application. The outer DTO are sorted by the stable ID.

The regular session still has one managed cooperative nation, a camp, a house, a farm, a warehouse, physical resources of three kinds, movement, collection, simplified food/hunger/season, and three learning goals. The global `Resources` is now a derivative projection of physical reserves. Roads, hazardous routes, manufacturing recipes, upgrades, caravans, food spoilage, a complete catalog of materials and complex property are still missing. The 6000 population limit in validation is a temporary protection for the four nations, not a confirmation of the product scale. The hierarchy of regions, flow fields, reusable route cache, crowd avoidance, collision and full AI are not implemented.

The current reconnect only works in the memory of the running server. After restarting, the world is loaded, but the session tokens, roles and cache of undefined commands are not restored. The chat already has a graphics panel, but lobby/roles UI and persistent moderation are missing. LAN works as a manual connection to the host address; There is no automatic discovery. The physical session between the two PCs and the Steam session has not yet been verified.

## What else is needed for full traffic

All requirements [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md) remain in effect. Among the large unfinished blocks:

- D3D12 render performance gate, full world atlas rendering, batching/culling/static-cache/hot reload, miniaudio and extension AssetPipeline;
- streaming/unload of chunk, full generation of biomes/geology/cave, path hierarchy/flow fields/cache/crowd avoidance/collisions and the rest of physical logistics/manufacturing;
- the family, full executive professional/skill scenarios, full AI, health, society, events, animals and combat;
- maps with remembered/visible statuses, markers, filters and mapping;
- full training, key rebinding, accessibility, lobby and other game screens;
- durable session after restart, autosave ring, cloud conflict, LAN discovery and Steam adapter;
- The actual 4×1500, p95/p99 tick/render, RAM/VRAM, loss/latency/soak/sanitizer gates;
- final content, balance, macOS and Steamworks/SteamCMD checks.

EnTT is now connected to `SimCore` and used for current components. Catch2 is fixed in the manifest, but current tests don't use it. An existing ECS-base does not mean completed AI, logistics, or performance gates. RmlUi/ICU milestone does not mean a ready frontend: no lobby, full set of colony screens, key rebinding, runtime UI hot reload, audio settings or texture registry for the world. SDL_Renderer is used only by the obvious software test backend.

## Next engineering break

Manual commands 0.10 continue [ the profession and experience 0.9](PROFESSIONS_PLAN_UA.md). Move/Gather are now accepted separately for group residents; Shift adds the command to the line. The panel shows the active line and final finishes, allows one to cancel an order or release or transfer control. The completion of the delivery protects the physical cargo; Cancelling the internal return to collection doesn't restore the old work. The player who is disabled has 30 seconds of monotonous time to return, including a pause. Details and boundaries  [MANUAL_ORDERS.md](MANUAL_ORDERS.md); These mechanics do not close the full Utility AI, occupations catalog or other requirements of the engine.

Next stage — [manufacturing on the farm 0.11](FARM_PRODUCTION_PLAN_UA.md): worker, finished line, retained labor and physical delivery. It's a plan, but it's not even started.

The next simulation roughness expands on the already implemented physical warehouses, reserves, shipments and building deliveries with manufacturing recipes and lines, upgrades, roads, route risk and the rest of the resource catalog. The following path levels should add hierarchy/flow fields/cache, crowd avoidance, and collision over the current FIFO/shared-reverse-BFS base. Even with fixed limits, you need dimensions 10/100/500/1500/4×1500 with complex AI, four clients, save/load and memory/network/render indicators; The current code doesn't close the performance gate itself.

Durable reconnect after restart, autosave ring, LAN discovery, lobby/roles, audio and network loss/latency remain separate mandatory blocks; The final performance gates are to measure the enhanced simulation.

After that simulation/content blocks should be added with vertical scenarios, each with save/load, multiplayer, hidden-information and performance proofs. The deadlines for the original plan remain estimates, not a confirmed schedule.

# Budgeted routes: contract and verification

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

> The 0.6 version implements a shared search, a full queue state in the safe and a two-phase network response. Locals 33/33 CTest, 1500/4×1500 planner fixtures, D3D12 UI and the unpacked package has been inspected; The evidence and limitations are given in `VALIDATION.md`. Version 0.5 remains the historical basis for comparison.

## Why Change Is Needed

The plan requires 20 ticks/s, p95 tick to 10 ms, p99 to 20 ms, hierarchical search `region → chunk → cell`, budget lines per tick and shared routes for group traffic ([source, rows 221–245](source/development-plan-ua.txt)). Separately, limited queues, a determined application of results at the tick boundary and an understandable state of expectation for a mass order are required ([source, rows 897–906](source/development-plan-ua.txt), [984–999](source/development-plan-ua.txt)).

In 0.5 BFS is synchronous. The exact production callsites:

- `src/sim/world.cpp`, `World::Impl::set_route`  creates and completely surrounds an array of 131072 two-layer cells.
- `World::move` — calls `set_route` for each ID, before copying the entire `WorldState` for rollback.
- `World::gather` repeats the same pattern for the route to the neighboring resource cell.
- `World::build` — synchronously searches for routes for the three builders.
- `World::tick` — synchronously rebuilds the route if the next cell becomes impassable.
- `src/server/game_server.cpp`, `GameServer::tick` → `apply` — first exhausts the entire command line; Each Move/Gather can perform all of the above BFS before the beginning of `World::tick`.

`ClientSession::Impl::queue` and transport only put the server command in line; They don't build roads. The measured `SimRunner --benchmark-chunks` confirms the problem in the original route construction.

## The contract of the line

1. Add to `World::Impl` the global FIFO `PathBatchQueue`, one `ActivePathSearch`, the monotonic `next_path_batch_id` and `topology_revision`. Maximum — 512 batches and 1500 unique resident ID in batch, according to current server limits.
2. `World::move` and `World::gather` after checking in cheap reserve residents and return extended `CommandResult { accepted, message, path_batch_id, pending }`. The tick boundary reservation stops only the locomotion of the old Route, fixes the current float Position and start cell, but does not stop hunger/health and other simulation systems. Until commit Position is unchanged; commit sets a new route without recording the coordinates, so there's no teleport. The second order for the reserved resident at this stage is rejected; Cancellation/replacement is a separate function.
3. One batch with a common target uses one reverse BFS: for Move — from the target, for Gather/Build — from all passing neighbours of the target. The initial resident ID is canonically sorted. The order of neighbors remains fixed `N, W, E, S`, followed by the portal. It removes 1500 the same circuits and keeps the reproduction.
4. At the beginning `World::tick` the planner spends no more than `path_work_units_per_tick = 4096`. One unit — BFS expansion, one parent-step reconstruction or one `CellCoord`, added/copied to the staged Route; So the final tick doesn't copy 1500 routes without a budget. The starting limits are `max_path_cells = 2048`, `max_staged_route_cells = 3'072'000` (1500×2048) and `max_planner_bytes = 64 MiB`; Excess terminally rejects the entire batch. These are the determinants of work and memory, not the wall-clock deadline. FIFO batch is fully cooked or completely rejected; already staged routes of all its inhabitants are applied simultaneously at the tick boundary only through swap/move, in order resident ID. The movement is carried out after this phase.
5. Construction and automatic replanes also create internal batches. No direct call of the full BFS is left outside the planner phase.
6. `topology_revision` changes with the appearance/disappearance of the blocker, portal change and transition `MineOre → MineFloor`. If the revision of the active search for outlays, its progress is determined to be reduced to the beginning to the budget cost of the current tick. The batch stores `enqueued_tick`, has `deadline_tick = enqueued_tick + 1200` (60 s at 20 Hz) and a maximum of three topology restarts. Deadline or fourth invalidation gives terminal failure, releases all reservations and allows the next FIFO batch to run; The partial or outdated path never gets to `Route`.

The separate ECS- component `OrderStatus` retains the phase, typical cause of rejection, and ID expected batch. The prominent DTO inhabitant contains `OrderPhase { Idle, PathPending, Active, PathFailed }`, `PathFailure` and `pending_path_batch`. The search frontier, parent map and full routes in `ViewSnapshot` are not transmitted. HUD shows the number of selected inhabitants in the queue and the reason for the rejection in the language of the interface.

## Answer commands save and network

Move/Gather requires a two-phase response: `Queued` after batch acceptance and one terminal `Applied` or `Rejected` after atomic commit. In `schemas/network.fbs` the field `Reply` should be enum-state; dedup and ACK must identify `(request_id, phase)` because the current `NetworkClient` rejects the second response with the same request ID. `GameServer` holds `path_batch_id → (PlayerId, GameCommand)` and moves the command from pending to completed only after the terminal response. Repeating the identical request ID returns the current phase; The change in repetition is still a conflict.

`WorldState` and `schemas/world_save.fbs` store FIFO batch-i, `next_path_batch_id`, `topology_revision`, planner phase, `work_units_done`, frontier/cursor, parent nodes and partially prepared routes; These fields are in the checksum. The 4 format keeps the full progress without repeating the load search. Temporary indexes are recovered from the nodes tested. The fixed v3 migration creates a blank queue and keeps the original as `.v3.bak`; The old v2 and text v1 are still supported.

Server/session queues are not part of world save. The successful `GameServer::load` disables the old transport session and clears its correlations; They loaded the simulation batch and continued to work without the old network responses. A failed load keeps the world and the session up. A normal disconnect does not cancel an already-received command: its terminal result remains available when reconnect to the same server process. Durable replies after full restart remain a separate unclosed contract.

## Acceptance tests

1. **Hard budget:** a long route takes several ticks; The amount of work per tick expansion/reconstruction/copy does not exceed 4096, staged routes and planner memory do not exceed the set limits, and the Move call does not build the route synchronously.
2. **Group atomicity:** 1500 residents with the same goal have one shared search; While waiting, their position does not change, hunger/health continues to update, and commit does not teleport the fractional position. After completion, all 1500 move to `PathPending → Active` for one tick.
3. **Determinism and save/resume:** two identical worlds give the same batch IDs, completion ticks, routes and checksum; save/load in the middle of the search matches a continuous run on each subsequent tick.
4. **Topology invalidation:** blocking the cell during search increases revision and restarts search; fourth invalidation or tick 1200 terminally completes the batch and misses the next one. No path used passes through the new blocker. The opening MineFloor after the ore has been exhausted gives the route only through the usual line.
5. **Reply/idempotency:** the network client receives a level `Queued`, followed by one terminal phase; The repeat packet does not create a batch, the altered reuse deviates, ACK of each phase does not waste the next.
6. **Behind limits and privacy:** Batch 513 and overlapping resident orders are rejected; a long headless run does not increase the line without limits; The snapshot does not contain frontier, hidden cells, or foreign target/route data.

## Unverified Decisions

- The value 4096 is a starting fixed budget, not a confirmed performance optimum; It should be approved by the benchmarks 10/100/500/1500 and 4×1500.
- `planner_bytes` — Determined estimate of the state by element size and fixed overhead costs of indices. The 64 MiB limit is not a measurement of the physical peak memory of the process: the container capacity, allocator, and time buffers save/load are checked separately.
- Hierarchical region/chunk cache and local congestion avoidance are out of the minimum queue milestone. Shared reverse BFS can be implemented now, and hierarchy — after the profile.
- You need to measure the size of the pending-search save, load time and peak memory save/load. The frontier and parent state are kept in the direction; There's no full replay algorithm.

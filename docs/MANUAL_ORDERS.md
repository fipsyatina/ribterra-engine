# Manual Order Queue 0.10

> English translation of the historical document; snapshot-specific evidence and scope are preserved from the original wording imported in commit `7388a2f`.

**Status: the 0.10 line was completed with build 0.10.1.** Local results: 52/52 CTest, final network profiles at 150 ms RTT with 1%/3% loss, GPU checks in five languages, and 4/4 package smoke. CI `13ed5c2` passed Windows 52/52, Linux 35/35, and UBSan 13/13 without C++ warnings or errors. Five 0.10.0 benchmark scenarios are retained as a historical measurement of the unchanged simulation. Source, packaged runtime, exact results, and limits are recorded in [VALIDATION.md](VALIDATION.md). The full engine has separate acceptance criteria.

This stage implements queues for the existing Move/Gather commands and their partial group execution. It does not close all of S07: take/drop/interact/talk/defend, complete persisted groups and a context menu, and other requirements remain in the [engine register](ENGINE_REQUIREMENTS.md). Direction and compatibility requirements are documented in [CONTROL_PLAN_UA.md](CONTROL_PLAN_UA.md) and [MANUAL_QUEUE_MIGRATION_UA.md](MANUAL_QUEUE_MIGRATION_UA.md).

## Public contract

Types and APIs are defined in [world.hpp](../include/ribterra/world.hpp); persisted additions are in [world_state.hpp](../include/ribterra/world_state.hpp).

- `World::issue_manual_order(ids, request, mode, issuer)` returns a separate `ResidentCommandOutcome` for every unique ID in the ordered selection. `Applied` means the queue item was accepted; physical execution occurs later. `Deferred` means the item was accepted while waiting for a protected executor. `Rejected` has a specific `PathFailure` and a zero `order_id`.
- `ManualOrderRequest` contains `Move` or `Gather`, a target, and `GatherCompletion::{UntilExhausted, OneDelivery}`. `Legacy` is reserved for a compatibility link to an existing old executor; it is not accepted as a new command type.
- Every accepted item receives a monotonic `uint64` ID. IDs are not reused after completion or cancellation. Counter overflow rejects a new order before the current queue changes.
- Optional `ManualOrderIssuer{player_id, request_id}` values describe provenance. They do not grant control rights: client, ownership, and lease validation belong to the server. Unknown historical author and time values remain absent.
- `manual_orders()` returns authoritative records; `manual_diagnostics()` returns diagnostics for the current instance. Internal path/Work/logistics IDs are not new client request IDs.

Each resident may have up to 8 unfinished items, with no more than 8192 records in total. Completed history retains a tail of up to 256 records and may be pruned earlier to admit new work. Counting all completions from only the visible history tail is therefore incorrect.

## Phases and physical completion

The logic is in [world_manual.inc](../src/sim/world_manual.inc); [world_manual_validation.inc](../src/sim/world_manual_validation.inc) validates cross-references.

| Phase | Meaning |
|---|---|
| `Queued` | The item is waiting for its turn or planner admission. Only the head can receive an executor. |
| `WaitingBoundary` | A new order was accepted, but the previous physical task or old atomic path remains protected. |
| `PathPending` | The head owns a specific unfinished path batch. |
| `Active` | A route, Work continuation, or logistics task is executing. This does not necessarily mean that the resident is already moving or gathering. |
| `Complete` | Semantic completion was reached: `Arrived`, `Delivered`, `Exhausted`, or compatible `LegacyFinished`. |
| `Failed` | A terminal `PathFailure` exists; the result is not masked as success. |
| `Cancelled` | The item was individually cancelled, replaced, stopped, or released: `Cancelled`, `Replaced`, `Stopped`, or `Released`. |

Move completes after the route is physically traversed and the destination is reached, not after a path is assigned. `OneDelivery` completes after one productive gather and physical placement of its cargo in storage. `UntilExhausted` continues gather-and-delivery cycles until normal resource exhaustion. A depleted target produces `Complete/Exhausted`, while a changed, incompatible target produces failure.

For `OneDelivery`, the absence of storage after output is created is not reported as a delivery: output remains a physical Ground stack and the manual order fails. This stage uses the existing `PathFailure::TargetInvalid`; there is no separate public `StorageFull` code in the manual result.

## Replace, append, and the delivery boundary

`Replace` first validates the request and capacity. A blocked Move, empty/incompatible Gather target, or exhausted capacity does not remove the current queue. For an accepted Replace, old items are cancelled; an unprotected route or Work can be interrupted immediately. `Append` adds an item after the current head. Automatic profession work does not acquire the resident between consecutive items.

A reserved physical task completes its safe boundary. The new head waits in `WaitingBoundary` and then begins its route. The queue does not copy items, reservations, or delivery credit: [logistics](../src/sim/world_logistics.inc) continues to own them.

`cancel_manual_order(order_id)` cancels exactly the specified item with reason `ManualOrderCompletion::Cancelled = 8`. This is a new value after unchanged values 0–7; it does not release control rights. The server preserves the current lease. If the active head is cancelled, the next item remains and waits for a physical boundary when necessary. Cancelling a tail item does not interrupt the head.

After delivery, an unfinished Work continuation can be cancelled together with its internal pending path. Such a task is not protected cargo: the cancelled Gather is not restored and the next item remains `Queued`. Protection for an old external atomic batch and physical logistics remains.

`stop(ids)` clears the manual queue with reason `Stopped` and disables automation until a new explicit assignment. `assign_profession` replaces the queue and records a new profession policy with the previous Deferred semantics. `release_manual_control(ids)` clears the queue with reason `Released` but preserves the profession selection and `automation_enabled` value; it is suitable when temporary control rights are lost. Release does not enable automation that Stop previously disabled. These boundaries are also handled in [world_professions.inc](../src/sim/world_professions.inc) and [world_work.inc](../src/sim/world_work.inc).

## Shared planner and cost bounds

New routes use `PathCommandKind::Manual` and `legacy_atomic=false`. A group of compatible, ready heads uses one shared reverse BFS; it does not become one independent BFS per resident. A draft stores `manual_order_id`, the named-completion flag, and failure. An unavailable or cancelled participant does not cancel the routes of others. Route application in Stage can occur for different participants on different ticks.

The shared planner retains a limit of 4096 path-work units per tick. The manual scheduler visits up to 1500 heads for admission and makes up to 32 shared-batch admission attempts per tick. This permits one pass over the maximum group of 1500 residents. Under queue pressure, it uses backoff of 2/4/8/16/32 ticks, up to 64 attempts, and up to 1200 admission ticks from the head's first consideration. Waiting behind other queue items does not start this counter.

**1500 visits is not a limit on all tick operations.** Heads are reconciled separately with their actual executors before the profession scheduler and after physical work so every exported view has valid correlations. Indexing, grouping, physics, professions, and logistics also have costs. Actual command time, tick time, and memory must be evaluated by measuring the complete scenario.

Group acceptance reserves record memory once, for a request of no more than 8192 items, before changing any resident. Vector capacity is not part of the checksum. Rejected participants in a full queue do not rescan history when there are no completed records to prune; participant cancellation finds its ordered draft by binary search. However, a mass Replace in a full queue can still repeat actual record compaction, and stopping active Work scans its history. This code does not claim linear cost for every group command.

Internal Manual outcomes do not enter `take_path_results()` and do not complete old external request correlations. The old `World::move/gather` APIs preserve atomic path outcomes; a compatible queue record is added for their manual executor.

## Final 0.10.1 networking fix

A periodic update missed because of a delayed ACK remains requested until it is actually sent. A subsequent pause therefore does not leave the client with a stale projection. The ordinary 5 Hz cadence remains on a fast connection; pending snapshots are not replaced before ACK.

The graphical client automatically reconnects a previously established session after a transport interruption: one retry per second for up to 30 seconds after detection. An explicit disconnect stops automatic attempts. A failed initial connection does not start this loop.

Up to 2048 unacknowledged commands are retained in client memory only. Replay is allowed after a full snapshot only when endpoint, world ID, player ID, session token, and control epoch are unchanged, and no later than 30 seconds after issue. The same request ID and full payload are replayed; the server returns the previous result or accepts a command that it did not receive earlier. Replays are submitted in increasing ID order, no more frequently than one per 50 ms; new commands wait for this submission to complete. A record is removed after a terminal reply. This is not durable replay after a client or server restart.

Stale or incompatible intents do not execute automatically. The HUD reports reconnection, success, failure, and the need to review unacknowledged orders in five languages. Reconnection alone does not mean that orders completed physically. Re-received chat history is deduplicated by sequence, so old messages do not appear twice.

Coverage includes deterministic interruptions before command acceptance and after acceptance but before the terminal receipt, an expired intent, a changed epoch, and explicit disconnect. Separately, it includes real encrypted UDP through a relay with a 150 ms base RTT, 1%/3% loss in each direction, duplication, added delay, and interruption through transport timeout. Actual run status is in [VALIDATION.md](VALIDATION.md). These scenarios do not substitute for tests on physical PCs.

## Save 8 and compatibility

The target set for the final fix is engine 0.10.1, save 8, rules 7, protocol 7, generator 2. New queues, counters, admission cursor/attempts, executor links, and named draft fields are stored and included in the checksum. Diagnostic time and visit metrics are not persisted gameplay truth.

`migrate_legacy_manual_orders` first performs complete prior semantic validation and then adds records in increasing resident-ID order. A new transactional validation is required after conversion. A corrupt import does not replace the current World.

- Confirmed `manual_override` and path/route/Work/logistics links allow restoration of a known Move/Gather head. `Job` alone is insufficient.
- An unusual but historically valid manual executor receives `Legacy` linked to the actual executor, without inventing an order type or target.
- Neutral work in v7 does not become manual after loading v6. Deferred Assign/Stop does not revive a cancelled Gather.
- Accepted v7 batches retain the previous atomic completion, frontier, cursors, and budget accounting. Their logical draft/batch/outcome costs remain 72/256/48 bytes; extending a new C++ DTO does not change the old planner-state acceptance threshold.
- Opaque Legacy cannot be admitted to planning as `Queued` or `WaitingBoundary` with an unknown target. A current Legacy link must be `Active` or `PathPending`; later new commands contain a concrete request.

The genuine starting material is [25 fixtures from the unchanged v7 writer](../tests/fixtures/WORLD_SAVE_V7.md). The final persistence suite compared all 65 old control states in every fixture; the complete local 0.10.1 CTest passed 52/52. Evidence and limits are in [VALIDATION.md](VALIDATION.md).

## Tests and benchmark

[manual_order_tests.cpp](../tests/manual_order_tests.cpp) checks Move → OneDelivery → Move without automatic work between steps, a partially unavailable group using shared search, cancellation of one of 1500 participants, Replace/Stop/Release/Cancel while carrying cargo, exact output/XP, exhaustion, and stepwise import/resume. Group cases fill all 8192 records, verify rejection without a checksum change, then test the next Replace and partial acceptance near the `uint64` boundary. Cancellation of a real Gather continuation in Work `PathPending` is tested after delivery both with and without a tail. Separate negative cases reject an empty legacy target, a detached pending head, incompatible executor/job, and terminal phase/completion combinations including `Complete + Cancelled`.

[manual_order_persistence_tests.cpp](../tests/manual_order_persistence_tests.cpp) covers old fixtures, persistence of new state, rejection of corrupt payloads, and comparison with old semantics. These tests complement rather than replace network and visual checks.

[ManualOrderBench](../tools/manual_order_bench.cpp) is a separate controlled benchmark for 10/100/500/1500/6000 residents; the final five rows and independent review are in [VALIDATION.md](VALIDATION.md). The scenario uses flat terrain, co-located residents, increased Food/storage, and one shared Wood deposit. It measures commands separately from `world.tick`; result observation occurs outside the tick timer. Initial Move and Append timing does not evaluate mass Replace during active Work, network transfer, or the GPU.

At scale 6000, the 8192 limit is deliberately filled: 6000 Move and 2192 accepted OneDelivery items, while the remaining 3808 Append operations must be rejected for capacity. The oracle requires all 6000 residents to arrive physically, exactly one XP/output for every accepted Gather, exactly 3 delivered Wood per output, and zero cargo, active orders, and pending paths at the end. It does not count completions from the truncated history. This is a queue-and-logistics scenario under specified conditions, not evidence of whole-engine performance or of all 6000 Gather operations running concurrently.

# Frozen save format 8 fixtures

These **27 genuine saves** were written and verified by the unchanged Ribterra 0.10 writer. The canonical capture is `.deps/v8-capture-5476c90/trial-2`: capture returned **2**, reporting one explicit unavailable opaque-legacy scenario; verification returned **0** for all 27 captured files. Every file re-encoded byte-identically and preserved equal gameplay checksums and undrained path outcomes at the initial state and all 64 subsequent ticks.

The binary files total **15,094,832 bytes**. Their 27 decoded expected JSON files total **70,040,368 bytes**. [world_save_v8.provenance.json](world_save_v8.provenance.json) records every binary/JSON hash, the original command histories, all 65 checksum checkpoints per fixture and the writer inputs. [manual-v8-fixtures.json](../../docs/evidence/manual-v8-fixtures.json) is the compact file ledger. The independent [fixture review](../../docs/evidence/manual-v8-fixture-review.json) found no authenticity defect and confirmed the additional Farm states against actual trial2 results.

This catalog preserves version 0.10 inputs for the proposed [Farm production migration](../../docs/FARM_PRODUCTION_PLAN_UA.md). It does not implement or prove a later save-format migration.

## Writer provenance

The writer is source commit `5476c90676dc3152ef044e80fc0d12884d994f99`: engine **0.10.0**, save **8**, rules **7**, protocol **7**, generator **2**, gameplay content SHA-256 `5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606`.

The ignored `.deps/v8-capture-5476c90` snapshot contains 24 hashed inputs: 19 public headers, SimCore/Foundation libraries, two generated headers and the source schema. Its historical manifest still records the pending CI status at the instant of capture. The original source subsequently passed Windows 50/50, Linux 33/33 and UBSan 12/12 in [recorded CI evidence](../../docs/evidence/manual-ci-5476c90.json), with zero Windows C++ warnings and [10 GCC warnings across Linux/UBSan](../../docs/evidence/manual-ci-linux-5476c90-quality.json). Later warning-cleanup source and binaries do not replace this historical writer.

The [format 8 baseline](../../schemas/baseline/world_save_v8.fbs) is an exact copy of the frozen schema, SHA-256 `9436b9c029107c13d43c8db71b72e7967b9ac4caf8a12f165365b6908a16becd`. The frozen manifest, its 24 inputs, the first trial and all pre-existing v7 fixtures remain unchanged.

## Captured states

Each suffix below identifies `world_save_v8_<suffix>.ribsave` and its matching `.expected.json`. Ticks are the actual canonical observations.

| Suffix | Tick | State preserved |
|---|---:|---|
| `manual_queued` | 0 | New manual Move head and appended OneDelivery Gather tail, before scheduler admission. |
| `active_move` | 2 | The accepted Move has an installed route and real fractional movement. |
| `shared_search` | 2 | Shared Manual Move search for 1,500 residents toward (90,90); accepted batch identity retained. |
| `shared_reconstruct` | 8 | The same group batch reconstructs routes after one participant was cancelled normally. |
| `shared_partial_stage` | 56 | Actual partial Stage: 805 successful participants, one cancelled, 694 pending. |
| `one_delivery_cargo` | 61 | Manual OneDelivery Gather carries Wood 3 with an appended Move tail. |
| `until_exhausted_cargo` | 61 | Manual UntilExhausted Gather carries Wood 3 with an appended Move tail. |
| `until_exhausted_pending_work` | 120 | First Wood delivery finished; the head owns its pending internal Work continuation and stored Wood is 83. |
| `waiting_boundary` | 61 | Replacement Move waits on the existing physical Wood delivery. |
| `cancelled_cargo_with_tail` | 61 | Head completion Cancelled=8; the surviving tail waits on the same cargo task. |
| `cancelled_cargo_without_tail` | 61 | Final manual order cancelled; neutral deferred delivery retains physical Wood. |
| `released_cargo` | 61 | Release completion Released=6 with the existing delivery protected. |
| `stopped_cargo` | 61 | Stop completion Stopped=5; automation disabled, delivery deferred safely. |
| `deferred_assign_cargo` | 61 | Miner assignment deferred under cargo; old manual order completed Replaced=4. |
| `cancelled_pending_work_with_tail` | 120 | Delivered Wood retained, internal Work/path retired, surviving Move tail Queued. |
| `cancelled_pending_work_without_tail` | 120 | Delivered Wood retained and internal Work/path retired without inventing another physical boundary. |
| `farm_planned` | 0 | Farm order 2 is Planned; actual material reservations and construction tasks exist before the building is installed. |
| `farm_delivery` | 1 | The same Farm at (20,20) enters Delivery with physical task/reservation/path correlations. |
| `farm_paid_work` | 91 | The same unfinished Farm enters Work at progress approximately 0.0092; Wood 20, Stone 10 and Food 5 already delivered. |
| `farm_built` | 199 | The Farm is genuinely complete after delivery and construction work. |
| `farm_busy_before_400` | 399 | Completed passive Farm while residents execute manual Move elsewhere; stored Food 85 and no Farm Ground Food. |
| `farm_busy_after_400` | 400 | Passive production created Ground Food 8 in stack 8 despite busy residents; normal consumption reduced stored Food to 75. |
| `farm_food_cargo` | 606 | Hauler 1 carries that exact Food 8 in stack 9, task 5, source stack 8, toward Camp 1; stored Food 65. |
| `farm_food_stored` | 655 | The same task 5 completed into Camp storage exactly once; cargo cleared and stored Food 73. |
| `farmer_no_compatible_work` | 200 | Farmer assigned to the completed Farm with automation enabled, no deferred/manual override, NoCompatibleWork and Farming XP 0. |
| `migrated_legacy_atomic` | 1 | Genuine v7 group Move Search loaded through the frozen v8 writer; legacy atomic batch and unknown manual issuance provenance retained. |
| `migrated_neutral_cargo` | 18 | Genuine v6-origin Stone 10, previously preserved in v7, loaded through v8 without inventing a manual wrapper. |

The Farm captures follow one normal construction chain. At tick 91, resident 1 has real Construction practice remainder 92; at completion it is 2,000. The food branch loads a copy of the tick-400 capture, issues Stop to all residents and assigns Hauler to resident 1. It observes the original source stack and the same delivery task through pickup and completion. Ordinary food consumption at tick 600 explains stored Food 75 → 65 before pickup; delivery then produces 65 → 73. No second passive harvest occurred before either food capture.

The old passive rule creates Food 8 every 400 ticks even without a Farm worker. At tick 400, simultaneous consumption by ten living residents makes total Food 85 → 83 while Ground Food rises by eight. A future migration must preserve already-produced Food without treating that total-inventory change as a failed delivery. Removing the old passive rule intentionally changes future economics; these historical sequences do not promise identical old/new checksums after that change.

## Capture and verification discipline

[capture_world_save_v8.cpp](capture_world_save_v8.cpp) uses normal World creation, ManualOrder/Append, CancelOrder, ReleaseControl, Move/Gather, Build, AssignProfession, Stop and tick APIs. Exported state is observed to select a real phase and record its summary. It is never edited or imported. No terrain, XP, cargo, planner phase or historical provenance is manufactured, and no path outcomes are drained.

Each save is immediately reloaded to compare its captured checksum. Saving does not advance the original scenario, so short-lived construction and planner phases remain observable. Inputs are copied into the fresh output root before load, including current-format branch sources. This protects historical v7 files from normal migration and its `.v7.bak` rewrite. The manifest records each original path/hash/format, copied path, accepted command results and tick sequence; original source bytes are checked afterward.

[verify_world_save_v8.cpp](verify_world_save_v8.cpp) loads each original, saves a separate copy and requires byte-identical re-encoding. It compares original-load and reloaded-copy worlds at 65 checkpoints over 64 subsequent ticks. At every checkpoint it checks the full undrained outcome vector and the retained initial outcome prefix. The original fixture bytes are checked again at completion. This verifier does not replay the command trace from scratch; independent source review checks normal-command authenticity.

The preservation step checks container magic/version, lengths, compressed payload checksum, exact gameplay hash and decoded world identity. It decodes with the frozen schema using `flatc --json --strict-json --defaults-json`, one file at a time. Expected JSON retains all fields except the large `chunks` array and obsolete `tiles` vector. Terrain remains in each binary and is covered by its SHA-256 and gameplay checksum. Parse checksum sequences as unsigned 64-bit integers, not JavaScript floating-point values.

The first trial's 22 outputs remain in their original ignored directory. The final trial added five real Farm observations before promotion; the earlier uncommitted promoted copies were replaced only after all 27 final files verified. No historical writer or v7 fixture was rewritten.

## Reproduction and limits

Run the helpers from the repository root. The prepared ignored `.deps/compile-v8-capture-helpers.cmd` uses the Visual Studio x64 compiler with `/std:c++20 /EHsc /O2 /MT /W4 /utf-8`, includes the frozen `include` and `generated` directories first, and links only the copied SimCore/Foundation writer libraries with the existing vcpkg dependency libraries. Both helper sources reject a writer with different version/content identifiers. Do not link them against mutable shared build output.

Use a fresh output root; capture requires the argument, while verification accepts it as an optional argument:

```powershell
& .deps/v8-capture-5476c90/capture.exe .deps/v8-capture-5476c90/reproduction-new
& .deps/v8-capture-5476c90/verify.exe .deps/v8-capture-5476c90/reproduction-new
```

Capture refuses an existing manifest, nonempty fixture/input directories and existing output filenames. It returns 2 when its explicit gaps list is nonempty. Verification also refuses existing outputs. A new independently created world may have a different world identity; byte identity is required for each captured world's own load/re-encode pair.

The sole reported capture gap is `migrated_opaque_legacy`: no natural historical producer is available. The existing opaque persistence regression changes a copied manual flag and is deliberately excluded from genuine fixtures. This finite set also has no separate Farmer-specific Stop/manual-override snapshot or UntilExhausted Work Waiting snapshot. Abilities retain their actual default zero; nonzero abilities, all professions, arbitrary later ticks, malformed payloads and every possible queue combination need separate tests. Server leases/control epochs are not World-save state. These limits do not imply that the broader Farm production plan or the full game is complete.

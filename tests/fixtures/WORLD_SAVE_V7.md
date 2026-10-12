# Frozen save format 7 fixtures

These 25 immutable saves were written and verified by the unchanged Ribterra 0.9 writer. The canonical capture is `.deps/v7-capture-d3bc785/trial-2`: capture and verification both returned 0, with 25 fixtures and zero gaps. Every fixture re-encoded byte-identically and produced equal gameplay checksums and undrained path outcomes at all 64 subsequent ticks. The complete capture trace, hashes and provenance are in [world_save_v7.provenance.json](world_save_v7.provenance.json); the 65-value checksum sequences and verification results are also retained in [profession-v7-fixtures.json](../../docs/evidence/profession-v7-fixtures.json).

This is preparation for future migration, not evidence that a format-7-to-newer migration exists. Coverage follows [CONTROL_PLAN_UA.md](../../docs/CONTROL_PLAN_UA.md), especially the distinction between manual execution, automatic work, neutral legacy continuations and deferred policy changes.

The writer is the unchanged Ribterra 0.9.0 implementation at source commit `d3bc78530ab2f03d8116bb9ef4c09ba71ca0232d`: save format 7, rules 6, protocol 6, generator 2, gameplay content SHA-256 `92e20f6f81770d8ca01a478753e8022ee600ca4fa9ddf0a88e07560043a23bcd`. Its copied headers, generated headers, schema and libraries are preserved under `.deps/v7-capture-d3bc785`, with original file hashes in `provenance.json`. That snapshot must remain unchanged, including its historical CI status at capture time. Later source warning cleanups are not the writer provenance of these fixtures.

The original writer passed Windows 46/46, Linux 30/30 and UBSan 10/10 in [CI run 34248830961](https://github.com/fipsyatina/ribterra/actions/runs/34248830961), with 60 GCC missing-field-initializer warnings across Linux/UBSan. The subsequent `42036ff` initializer cleanup passed the same 46/30/10 suites with zero C++ warnings in [run 34250688287](https://github.com/fipsyatina/ribterra/actions/runs/34250688287). These fixtures retain the original `d3bc785` writer; neither the newer source nor its binaries replaced the historical snapshot.

## Capture discipline

[capture_world_save_v7.cpp](capture_world_save_v7.cpp) calls normal World construction, Move, Gather, Build, AssignProfession, Stop and tick APIs. Exported state is read only to identify a genuine phase or record its summary. The helper never calls `import_state`, changes terrain, edits a save header, injects an executor, changes XP directly or manufactures a planner phase.

Each capture is saved and reloaded immediately to check the captured checksum. Capturing does not tick the original scenario: this preserves the chance to observe a short next phase. No path outcomes are drained. Long scout travel, food consumption, hunger and health changes are real consequences of the recorded ticks.

All input saves are copied into the selected output root's `inputs` directory **before** `World::load`, including current-format branches. Original source paths, SHA-256 values, container formats and copied input paths are recorded. This matters for v6 inputs because the old writer's normal load performs migration and creates `.v6.bak` beside its input. The immutable source is never the load target and is checked for unchanged bytes afterward.

The helper refuses an existing capture manifest, a nonempty fixtures directory and any existing output/input filename. Use a new trial directory after a partial or failed run. A completed capture returns 0 when all planned conditions were observed, 2 when it wrote an explicit `gaps` list, or 1 for a setup/fatal error. A gap is not a migration failure and must not be hidden by relaxing its state predicate.

## Captured states

Every binary has the prefix `world_save_v7_` and suffix `.ribsave`, with a matching `.expected.json`. The following ticks are read from the canonical capture manifest. Both group scenarios exposed all three phases with population 1500; the helper's fallback populations were not needed.

| Suffix | Initial tick | State to preserve |
|---|---:|---|
| `neutral` | 0 | Seed 70421, population 10, unchanged Unassigned profiles. |
| `manual_move` | 1 | Resident 1 moving physically toward (26,24), with fractional position, installed route and manual override. |
| `group_move_search` | 1 | All 1500 residents in the external Move batch searching toward (90,90). |
| `group_move_reconstruct` | 7 | The same accepted group batch reconstructing real staged routes. |
| `group_move_stage` | 55 | The same shared group batch in Stage at a tick boundary. |
| `group_move_deferred_stop` | 1 | Stop applied to resident 1 only: one Deferred profile, 1499 manual members, unchanged shared path/task/stack state. |
| `group_gather_search` | 1015 | External Gather batch 2 searching for 1500 residents after scout arrival and exploration at Forest (88,85); scout outcome retained. |
| `group_gather_reconstruct` | 1020 | The same group Gather batch reconstructing real staged routes. |
| `group_gather_stage` | 1066 | The same shared Gather batch in Stage. |
| `group_gather_deferred_assign` | 1015 | Miner assigned to resident 1 only: one Deferred profile, 1499 manual members, protected shared executor unchanged. |
| `manual_gather_travelling` | 1 | Resident 1 travelling on the installed manual Gather route to Forest (22,19), before first yield. |
| `manual_gather_running` | 17 | Resident physically at the resource with gathering progress between zero and one and Woodcutting XP still zero. |
| `manual_gather_cargo` | 60 | Manual HaulToStorage carrying 3 Wood in its existing stack/task. |
| `manual_gather_waiting` | 118 | Successful physical dropoff followed by a Waiting GatherContinuation; manual provenance retained. |
| `manual_gather_path_pending` | 119 | The same continuation correlated to its pending internal Work path. |
| `manual_gather_installed_continuation` | 120 | That exact Work intent Complete/Started with retired pending path ID and the corresponding active manual Gather route. |
| `deferred_assign_cargo` | 60 | Miner assignment Deferred under manual Gather cargo; existing path batches, stacks and tasks unchanged. |
| `deferred_stop_cargo` | 60 | Stop Deferred under the same original cargo, with automatic work disabled and protected execution unchanged. |
| `manual_gather_pressure_waiting` | 684 | Attempted manual continuation waiting behind exactly 512 real undrained external outcomes after successful delivery. |
| `manual_gather_failed` | 1883 | Terminal continuation failure, stored Wood 83, no Wood cargo, retired manual override and all 512 outcomes retained. |
| `legacy_v6_carry` | 18 | Genuine v6 input migrated by the frozen writer without new commands or ticks: Stone 10 in stack 4, resident 4, construction task 4, path batch 7; neutral Unassigned profiles. |
| `profession_auto` | 474 | Genuine GPU Miner source with automatic execution enabled, no deferred/manual policy, and independent Woodcutting and Mining XP. |
| `profession_cursor` | 1 | Population 33 assigned Woodcutter; bounded admission leaves profession cursor 33. |
| `profession_skills_practice` | 6654 | Automatic Builder after real wood/mining work and a 6000-tick stopped interval: wood XP 1, mining XP 2, construction XP 1, construction remainder 48, stored mining familiarity debt 2397. |
| `paid_builder_travelling` | 350 | Resident 7 automatically returning to paid House Work while nine other residents still execute manual Move. Inventory Wood 50 and Stone 25; no second construction charge. |

All listed phase and natural skill predicates passed in the canonical run. Abilities remain their genuine default zero: ordinary commands do not set nonzero innate abilities. This set therefore does not test nonzero ability migration, every profession or every possible manual-state combination. These are immutable valid inputs; malformed-payload rejection and future version migration require separate tests.

The first trial retained 21 outputs and reported one fixture-predicate gap: it incorrectly expected Wood in the v6 cargo input. The predicate was corrected against the immutable expected JSON to exact Stone 10 and its original stack/task/path correlation; three additional Gather observations were added before the successful fresh trial. No prior fixture or writer bytes were edited.

`legacy_v6_carry` is deliberately different from new manual cargo and from Deferred Assign/Stop. All can have current execution, but the first has no historical manual provenance and the latter explicitly replaces policy at a protected boundary. Future migration must not infer a manual order from `Job` alone, restart a frontier, duplicate cargo, repay a construction site or revive terminal work.

## Compile and run with the frozen writer

Run from `C:\Users\fipst\Desktop\ribterra`. The prepared ignored script `.deps/compile-v7-capture-helpers.cmd` invokes the Visual Studio x64 environment and runs these two compiler commands. They link only the copied SimCore/Foundation libraries and include the copied public/generated headers first:

```bat
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /utf-8 /I.deps\v7-capture-d3bc785\include /I.deps\v7-capture-d3bc785\generated /Ivcpkg_installed\x64-windows-release\include tests\fixtures\capture_world_save_v7.cpp /Fo.deps\v7-capture-d3bc785\capture.obj /Fe.deps\v7-capture-d3bc785\capture.exe /link .deps\v7-capture-d3bc785\SimCore.lib .deps\v7-capture-d3bc785\Foundation.lib vcpkg_installed\x64-windows-release\lib\libcrypto.lib vcpkg_installed\x64-windows-release\lib\flatbuffers.lib vcpkg_installed\x64-windows-release\lib\zstd.lib crypt32.lib ws2_32.lib shell32.lib advapi32.lib user32.lib
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /utf-8 /I.deps\v7-capture-d3bc785\include /I.deps\v7-capture-d3bc785\generated /Ivcpkg_installed\x64-windows-release\include tests\fixtures\verify_world_save_v7.cpp /Fo.deps\v7-capture-d3bc785\verify.obj /Fe.deps\v7-capture-d3bc785\verify.exe /link .deps\v7-capture-d3bc785\SimCore.lib .deps\v7-capture-d3bc785\Foundation.lib vcpkg_installed\x64-windows-release\lib\libcrypto.lib vcpkg_installed\x64-windows-release\lib\flatbuffers.lib vcpkg_installed\x64-windows-release\lib\zstd.lib crypt32.lib ws2_32.lib shell32.lib advapi32.lib user32.lib
```

Both executables accept one optional output-root argument. The canonical run used `trial-2`; reproduce with a new output directory because existing results are never overwritten:

```powershell
& .deps/v7-capture-d3bc785/capture.exe .deps/v7-capture-d3bc785/reproduction-new
& .deps/v7-capture-d3bc785/verify.exe .deps/v7-capture-d3bc785/reproduction-new
```

The default output root, if omitted, is `.deps/v7-capture-d3bc785`. Neither helper invokes CMake, changes shared build output or recompiles the writer.

## Output and verification contract

`capture-manifest.json` has `schema: 1`, the exact `writer` identity, `fixtures` and `gaps`. Each fixture records `name`, its relative `file` under `fixtures`, `start` with input provenance or new-world seed/population, ordered `commands`, a human-readable `condition`, `initial_tick`, `checksum` and `summary`. Every explicit command includes its tick, target or assignment, selected IDs and returned admission result. Consecutive ticks are recorded as one `tick` operation with `first_tick`, `count` and `last_tick`. Summaries include manual/deferred counts, profession cursor, path/frontier/draft progress, undrained outcome count, route sizes, Work correlations, carried stack/task IDs, construction payments and a clearly named first-eight-resident sample. They do not replace the full decoded expected payload.

[verify_world_save_v7.cpp](verify_world_save_v7.cpp) reads that manifest, checks the exact writer and captured checksum, loads each source, saves it under a separate `verification` directory and requires byte-identical re-encoding. It reloads that copy and compares both worlds at the initial state and every one of 64 subsequent ticks. Neither side drains outcomes; the retained initial outcome prefix and the complete current outcome vector are checked at every tick. Original fixture bytes are checked again at the end.

`verification.json` has `schema: 1`, `writer` and `fixtures`. Each verified fixture records `name`, `file`, `sha256`, `bytes`, `initial_tick`, `checksum`, all 65 values in `checksum_sequence`, `resume_ticks: 64`, `final_checksum`, `byte_identical_reencoding: true`, `undrained_outcomes_preserved: true`, and initial/final path outcome counts. Parse checksums as unsigned 64-bit integers, not JavaScript floating-point numbers. Existing verification output is never overwritten.

The preservation step selected trial-2 and recorded capture/helper/library hashes and container checks in [world_save_v7.provenance.json](world_save_v7.provenance.json). Each expected JSON was decoded with the frozen [format 7 schema](../../schemas/baseline/world_save_v7.fbs), retaining every field except the large `chunks` array and obsolete `tiles` vector. Terrain remains in each binary and is covered by its byte hash and world checksum. Original and LF-normalized schema hashes are recorded separately. Preserve these inputs and their original bytes when implementing a future `.v7.bak` migration; do not regenerate them with a newer writer.

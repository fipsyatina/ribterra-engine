# Ribterra checks 0.10: local, GPU, package and CI

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Updated: 9 September 2026 (Europe/Warsaw). Environment: Windows 11 Pro 10.0.26200; AMD Ryzen 5 5600H, 6 cores / 12 threads; AMD Radeon Graphics and NVIDIA GeForce RTX 3050 Laptop GPU; MSVC 19.50; Release x64; C++20.

This document separates current validation of manual orders in 0.10 from historical results for 0.2–0.9/M0. The complete engine gate and user additions are defined in [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md) and have not yet passed.

## Final correction to the 0.10 line: 0.10.1

Runtime source: `27da15308cb3f9f9a1c824decde26d6cd4f80a15`. The complete local suite passed **52/52 in 296,46 s**, with no C++ warnings or errors. The final network test was then strengthened from graceful reconnect to automatic return after a real transport timeout; both final profiles, **1% and 3%**, also passed. Runtime code did not change between these checks. [Local report](evidence/release0101-local.json), [CTest](evidence/release0101-ctest.txt). Final [CI `13ed5c2`](https://github.com/fipsyatina/ribterra/actions/runs/34285248451) succeeded: **Windows 52/52 in 360.27 s, Linux 35/35 in 251.01 s, UBSan 13/13 in 135.18 s**. It includes the final strengthened interruption tests and a compatible sanitizer configuration; C++ warnings/errors and UBSan errors: 0. [Metadata](evidence/manual-ci-13ed5c2.json), [Windows](evidence/manual-ci-windows-13ed5c2.txt), [Linux](evidence/manual-ci-linux-13ed5c2.txt), [UBSan](evidence/manual-ci-ubsan-13ed5c2.txt), [Windows quality](evidence/manual-ci-windows-13ed5c2-quality.json), [Linux quality](evidence/manual-ci-linux-13ed5c2-quality.json).

CI `13ed5c2` differs from runtime `27da153` only in sanitizer configuration and its documentation. The first run of the new `network_session` under UBSan found vptr incompatibility with GNS without RTTI: [failed run](evidence/release0101-ubsan-initial-failure.txt). Only the NetTransport adapter disables vptr; the other checks remain enabled. [Exact coverage and limitations](SANITIZERS.md). The Windows runtime was not changed.

A defect was found and reproduced: if the next 5 Hz boundary arrived while waiting for an ACK to an old snapshot, pausing could leave the client with stale state. [Failed run before the fix](evidence/release0101-before-snapshot-fix.json). The server now remembers the update request until it is fulfilled; the pending transition remains unchanged until ACK.

The graphical client automatically returns to the same session after a transport interruption. Replay of unacknowledged commands is limited to 30 seconds, preserves the complete payload and ID, and is allowed only when endpoint/world/player/token/epoch remain unchanged. Interruptions before acceptance and after acceptance but before the terminal receipt were checked: exactly one order and one visible terminal result. An expired intent or changed epoch does not start replay, and an explicit disconnect does not reconnect. Chat history is not duplicated. [Tests](../tests/network_recovery_tests.inc), [contract](MANUAL_ORDERS.md).

| Network profile | Actual results |
|---|---|
| 150 ms base RTT, 1% loss in each direction | 19-part initial snapshot; 12 terminal replies; automatic return after interruption; exact final projection on both clients; Wood 83 in storage, cargo 0 |
| 150 ms base RTT, 3% loss in each direction | The same invariants; the conflict has one winner; transfer/cancel/release work; a real 30-second grace period expires without advancing simulation time; the old control right does not return |

The profiles also apply 2% UDP datagram duplication and a 5% chance of an extra 90 ms delay. Both actually lost and duplicated packets; counters are recorded for [1%](evidence/release0101-adverse-1pct.json) and [3%](evidence/release0101-adverse-3pct.json). These are two clients on one PC using real encrypted UDP and a controlled relay. The initial generated world is intentionally fully revealed in the test to exercise multipart transfer; later commands are ordinary. This is not validation on physical PCs, of bandwidth, or of the game’s complete network load.

GPU D3D12 / RTX 3050 Laptop: **36 frames**. All 16 earlier order frames are byte-identical to previously reviewed frames; 20 new frames show four recovery messages in five languages. The longest message in each language and all four Ukrainian messages were reviewed—8 new frames. They remain readable at 1100×700 without blocking truncation. [Device](evidence/release0101-gpu-device.json), [executed UI assertions](evidence/release0101-gpu-results.json), [visual review](evidence/release0101-gpu-review.json). This probe supplies HUD text explicitly; separate executable tests above confirm real recovery behavior. This is not acceptance of concept art or native OS automation.

The extracted Windows package passed **4/4**: menu, mine/save-resume, orders panel, and dedicated server + two probes + graphical client. All 36 package order frames are byte-identical to the source probe. Three package scenes were reviewed separately. [Smoke](evidence/release0101-package-smoke.json), [network](evidence/release0101-package-network.json), [visual comparison](evidence/release0101-package-visual.json), [archive metadata](evidence/release0101-package.json).

Final archive: `dist/Ribterra-Windows-x64-0.10.1.zip`, **30,238,278 bytes**, SHA-256 `B3C6C057CA71CD72ECC43CB5DB38640E4A59B7E6AF0EBA2E6B9479B35B681EBE`. After the final CI record, only README was updated: **49 of 50 files** are byte-identical to the archive that passed smoke. Every EXE, DLL, asset, and license is unchanged; additional probe EXEs exist only in the validation extraction. [Comparison with the validated package](evidence/release0101-package-readme-comparison.json). The launcher points to `dist/releases/0.10.1/Ribterra/Ribterra.exe`.

Save **8**, protocol/rules **7**, generator **2**, and gameplay hash `5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606` are unchanged. Old genuine v7/v8 inputs are preserved. Runtime 0.10.0 and the older checks below remain historical evidence. This completion did not begin 0.11 implementation; the complete engine and V01–V03 retain separate acceptance criteria.

## Historical manual-order baseline: 0.10.0

Contract: **protocol 7 / rules 7 / generator 2 / save 8**. Named Move/Gather commands receive stable IDs when accepted; Shift appends to the queue, while an ordinary command replaces it after validation. Gathering has explicit OneDelivery or UntilExhausted completion. Cancelling one order does not release control; Release and Transfer are separate server actions. Disconnect has a 30-second monotonic grace period that expires even while paused. Cargo completes physical delivery before the resident changes activity. [Contract and limitations](MANUAL_ORDERS.md).

### Local run

The final MSVC Release build and **50/50 CTest in 191,09 s** passed ([log](evidence/manual-ctest.txt), [versions, hashes, and binary metadata](evidence/manual-local-validation.json)). The new `manual_orders` and `manual_order_persistence` took 17,00 and 34,54 s; the complete earlier `persistence` took 43,58 s. Coverage included partial group results, limits 8/8192, ID overflow, rejected Replace without changing the old queue, physical boundaries, capture/transfer/release and reconnect, pause-boundary snapshots, 64-bit IDs, full/delta/removal/multipart, substituted relationships, and completion reasons.

Before the final run, fixes addressed a separate `Cancelled=8` cause, an RmlUi dropdown that accumulated old options after SetInnerRML, and cancellation of internal Work PathPending after delivery. The last case could resume cancelled gathering or leave the next order with the wrong executor; its regression covers queues with and without a tail, exact 83 Wood / 1 XP, and valid state after saving. The preceding complete run before this final fix had 50/50 in 195,27 s; it was not substituted for the final evidence.

Migration compares every old field across **25 genuine v7 fixtures**, a frozen v7 checksum at each of 65 historical points, and subsequent deterministic continuation in the new world. `.v7.bak` preserves exact old bytes; nullable issuer/tick values do not invent command provenance. Earlier explicit migrations remain supported. These are finite histories with zero innate abilities, not every possible world or a complete fault-injection gate. [Format and limitations](SAVE_FORMAT.md).

### GPU and input

The final `ManualOrderVisualProbe` passed on **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. Real SDL pointer/key events covered Shift queueing, Gather selection, cancellation of a middle ID, group Release, repeated-Space protection, reset, physical Wood3 cargo → delivery → next Move, and save/resume. Two loopback clients transferred control at tick 0; the previous holder received a typed rejection containing the resident ID. [GPU](evidence/manual-gpu-device.json), [results](evidence/manual-gpu-results.json).

**16 EN/UK/RU/PL/ES frames** were reviewed, including 1100×700. The queue, buttons, and translations are readable; Close remains available while scrolling. A cancelled order shows “Cancelled by player,” and the holder remains “You”; after Release, other orders have the separate reason “Control released.” [UK](evidence/manual-visual-uk-final.json), [EN/RU](evidence/manual-visual-en-ru-final.json), [PL/ES](evidence/manual-visual-pl-es-final.json), [cargo and next order](screenshots/manual-orders/uk-cargo-and-next-order.png), [compact panel](screenshots/manual-orders/uk-compact-control.png). Initial frames containing the identified inaccuracy are preserved separately in `screenshots/manual-orders-initial`.

This reviews the actual SDL_GPU framebuffer and events delivered through SDL. Native OS-level screen control was unavailable in the environment. Technical world art is not concept acceptance; complete keyboard-only/IME/4K coverage, other GPUs, and 2–4 physical PCs remain open.

### Controlled benchmark

`ManualOrderBench` sequentially measured five Move Replace → Gather OneDelivery Append scenarios; timers cover only submission of these commands and individual `World::tick` calls. No build or other tests ran during measurement; the earlier 0.6 game process remained open. [CSV](evidence/manual-benchmark.csv), [independent review of the scenario and numbers](evidence/manual-benchmark-review.json).

| Residents | Accepted / rejected because of capacity | Completion tick | Command submission, ms | p95 tick, ms | p99 tick, ms | max tick, ms |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 20 / 0 | 201 | 0,0158 | 0,0137 | 0,0607 | 0,5303 |
| 100 | 200 / 0 | 203 | 0,0909 | 0,1014 | 0,6795 | 2,4630 |
| 500 | 1000 / 0 | 213 | 0,4068 | 2,6610 | 2,9842 | 3,4981 |
| 1500 | 3000 / 0 | 239 | 1,8676 | 4,0096 | 4,2049 | 4,4980 |
| 4×1500 | 8192 / 3808 | 296 | 6,2271 | 5,5051 | 6,7883 | 7,6996 |

In the 6000-resident scenario, every resident physically arrived, and 2192 accepted gathering orders produced exactly 6576 Wood in addition to the starting 320. The final state had 6896 Wood in storage and zero cargo, active orders, pending paths, or Failed outcomes; peaks were 1500 manual visits, 1 shared batch admission, 4096 path work, and 42 391 336 logical planner bytes. The 8192 limit rejected the remaining 3808 Gather commands without partial creation.

Group-reservation optimization and avoidance of repeated empty scans reduced command submission in this one scenario from 343,2010 to 6,2271 ms. This is one seed and one run on a controlled plane with a shared source, residents at one starting point, enlarged storage, and sufficient food. It does not measure GameServer/leases, network, UI, saving, RAM/VRAM, complex terrain, or collisions. Repeated Replace against a genuinely large history and Stop with active Work have different costs; the complete performance gate remains open.

### Package and CI 0.10

The extracted `dist/Ribterra-Windows-x64-0.10.0.zip` passed **4/4 smoke with exit 0**: menu, mine, manual orders, and a dedicated server with two probes and a graphical client. Additional probes were copied only into the validation extraction. All 16 package order frames are byte-identical to the reviewed frames; three more menu/mine/network frames were reviewed separately. [Smoke](evidence/manual-package-smoke.json), [16-PNG comparison](evidence/manual-package-visual-comparison.json), [three-scene review](evidence/manual-package-visual-review.json), [package GPU](evidence/manual-package-device.json).

At tick 180, both network clients see the completed building, 50 Wood, zero reserved, and consistent chat; the duplicate build attempt produced one Applied and one Rejected. This is loopback on one PC. The mine scenario exhausted 24 ore and preserved the restored projection; 88 Stone in the report is the total, while the delivery frame shows 86 in storage and 2 carried. [Network](evidence/manual-package-network.json), [mine](evidence/manual-package-mine.json).

The package is **30 229 861 bytes**, SHA-256 `9C4BB264CD7689831478A31450A354F3E084089B50108194E93AFD56D41EB5DB`. [Metadata](evidence/manual-package.json). Runtime comes from source **`5476c90676dc3152ef044e80fc0d12884d994f99`**. After smoke, only README changed: 49 of 50 files, including every executable, asset, and license, remained byte-identical to the validated copy. [Comparison](evidence/manual-package-readme-update.json). `Play Ribterra.cmd` reads `dist/current-build.txt` and launches `dist/releases/0.10.0/Ribterra/Ribterra.exe`; the earlier open 0.6 process was not closed.

[GitHub CI for 5476c90](https://github.com/fipsyatina/ribterra/actions/runs/34260695987) succeeded: **Windows 50/50 in 230,21 s, Linux 33/33 in 126,36 s, UBSan 12/12 in 120,56 s**. Windows had zero C++ warnings/errors; Linux and UBSan together reported ten GCC warnings and zero runtime errors. Two warnings concern capture of a type from an anonymous namespace, and eight concern incomplete aggregate initialization in tests. [Run metadata](evidence/manual-ci-5476c90.json), [Windows](evidence/manual-ci-windows-5476c90.txt), [Linux](evidence/manual-ci-linux-5476c90.txt), [UBSan](evidence/manual-ci-ubsan-5476c90.txt), [Linux quality](evidence/manual-ci-linux-5476c90-quality.json).

The next source, **`a4dd9a3b8c09dd9d7c6941c30dddd4d522d38ed8`**, captures only the scalar pending batch ID and explicitly sets earlier values after standard initialization of test commands. Versions, gameplay hash, save/wire, and intended behavior are unchanged. The local Release build had zero C++ warnings/errors; targeted manual/save/server/protocol/network/UI tests passed **6/6 in 64,61 s**. [Build](evidence/manual-warning-build.json), [CTest](evidence/manual-warning-ctest.txt). The Windows package remains on validated runtime `5476c90`.

**Final CI `a4dd9a3` succeeded:** [run 34262343935](https://github.com/fipsyatina/ribterra/actions/runs/34262343935), **Windows 50/50 in 217,37 s, Linux 33/33 in 124,09 s, UBSan 12/12 in 118,12 s**. Both jobs had zero C++ warnings/errors and UBSan errors. [Metadata](evidence/manual-ci-a4dd9a3.json), [Windows](evidence/manual-ci-windows-a4dd9a3.txt), [Linux](evidence/manual-ci-linux-a4dd9a3.txt), [UBSan](evidence/manual-ci-ubsan-a4dd9a3.txt), [Windows quality](evidence/manual-ci-windows-a4dd9a3-quality.json), [Linux quality](evidence/manual-ci-linux-a4dd9a3-quality.json). A separate GitHub service warning about Node.js 20 in Actions v4 remains; it is not a C++ or sanitizer diagnostic. This milestone does not complete the full engine or V01–V03.

### Genuine 0.10 saves for future migrations

Before any schema change, 24 files from writer `5476c90` were frozen: public headers, two libraries, two generated headers, and the schema. [Provenance snapshot](evidence/manual-v8-writer-snapshot.json). The unchanged writer created **27 genuine v8 fixtures** through ordinary commands; a separate verifier confirmed exact re-encoding, unchanged originals, and all **65 checkpoints / 64 subsequent ticks** for each snapshot while preserving undrained outcomes.

The set covers manual queues, shared Search/Reconstruct/partially applied Stage, movement, cargo, WaitingBoundary, pending Gather continuation, Cancelled/Released/Stopped/Deferred, a migrated legacy atomic path, and neutral cargo. For Farm it records Planned, Delivery, paid Work, completion, the 399→400 boundary, actual Food8 cargo and delivery, and the current Farmer NoCompatibleWork. [Catalog](../tests/fixtures/WORLD_SAVE_V8.md), [results](evidence/manual-v8-fixtures.json), [independent review](evidence/manual-v8-fixture-review.json).

There is no genuine historical opaque executor; this remains an explicit gap, and the test with a substituted manual flag is not presented as a genuine fixture. The set also does not cover every ability, every phase combination, or unlimited future histories. This records fixed 0.10 behavior, not validation of unimplemented production or migration 0.11. The [next-stage plan](FARM_PRODUCTION_PLAN_UA.md) retains all 41 requirements of the complete target.

## Historical professions and skills: validated 0.9 snapshot

Current-code contract: **protocol 6 / rules 6 / generator 2 / save 7**. Profession is separate from Job; server Assign/Stop produces separate Applied/Deferred/Rejected outcomes for each resident and preserves physical cargo to a safe boundary. Eight independent XP/practice/ability scales and stored fractional credit use one balance table and one skill+ability+familiarity limit of **−15…+15%**. Changing profession does not erase XP. The owner-only snapshot transmits ready skill breakdowns, visible assignment, and a waiting reason, but not internal practice timestamps/debt or a hidden worksite. Migration of genuine format 6 preserves old XP, planner frontier, and work/logistics state and creates `.v6.bak`. Detailed contracts: [SKILLS.md](SKILLS.md), [SAVE_FORMAT.md](SAVE_FORMAT.md).

### Local tests and UI fixes

The complete local build and CTest passed after repeated Enter/Space protection and localized-result refinements: **46/46 in 126,97 s** ([log](evidence/profession-ctest.txt)). The first run had 44/46 in 122,11 s; only `profession_ui` and `profession_client_rendered` failed (`.deps/profession-first-full-ctest.log`). The CSS fix `tab-index: auto` produced targeted 2/2 in 11,58 s (`.deps/profession-ui-keyboard.log`) before the successful complete run.

Fixes covered an empty optional ICU argument while retaining template validation, a stale worksite after a new Assign/Stop, selection/draft/captured IDs when replacing the world, and an immediate-rejection message. The dropdown contains only executable professions; Enter/Space neither duplicates an order nor toggles pause beneath the HUD. After separating muted hover from gold focus/active, repeated UI tests passed **11/11 in 40,06 s** ([log](evidence/profession-ui-final.txt)).

### Final GPU scenario

`ProfessionVisualProbe` passed EN/UK/RU/PL/ES on **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false: eight skill rows, pointer hover/Tab focus while paused, captured assignment, Miner, Stop, 128-tick save/resume for each scene, and preserved Deferred cargo. [Device](evidence/profession-gpu-device.json), [results](evidence/profession-gpu-results.json). 23 frames were reviewed: labels, XP, and breakdowns are readable, and explanations wrap without blocking truncation. Reviews/SHA-256: [UK, 7](evidence/profession-visual-uk.json), [EN/RU, 8](evidence/profession-visual-en-ru.json), [PL/ES, 8](evidence/profession-visual-pl-es.json); examples: [PL](screenshots/professions/pl-miner-skills.png), [ES](screenshots/professions/es-miner-skills.png). Technical world art is not concept acceptance.

### Profession benchmark

`SimRunner --benchmark-professions --seed 70421 --ticks 10000` produced **25 completed rows**: five sequential stages for 10/100/500/1500 and 4×1500 residents. The [CSV](evidence/profession-benchmark.csv) and [independent review of the CSV and code](evidence/profession-benchmark-review.json) contain no violations of the checked invariants. The stages are lumberjack → miner → return to lumberjack → assignment to an exhausted source → Stop.

For 6000 residents after 1015 ticks, all were alive and each had 2 Wood XP and 1 Mining XP; exactly 12000 wood and 6000 stone results produced **36000 Wood / 12000 Stone**. At the end of each stage there was no cargo, no ground item, no active Work, and no pending path. The code separately checks each nation’s XP and physical items across all storage locations, plus exact food consumption. An empty source returns WorksiteUnavailable; the final Stop returns Stopped.

The times below are milliseconds of `World::tick`, shown separately for the three productive stages with 6000 residents:

| Stage | Ticks | p95 tick | p99 tick | max tick | Initial assignment, ms |
|---|---:|---:|---:|---:|---:|
| Lumberjack | 299 | 6,4730 | 7,7741 | 9,7824 | 2,002 |
| Miner | 336 | 6,6212 | 7,4738 | 8,1751 | 9,554 |
| Lumberjack again | 340 | 5,9921 | 7,0170 | 9,0130 | 3,782 |

The highest p95 among the stages is **6,6212 ms**, p99 is **7,7741 ms**, and max is **9,7824 ms**; these are not quantiles calculated over a combined run. The five initial Assign/Stop commands took 22,481 ms in total, and all additional Stop barriers took 2,909 ms. Those commands, diagnostic reads, resident-cache updates, accounting checks, and CSV writes are outside tick timing. Maximum selector visits/admissions were 1024/32, Work 128/32, and path work 4096; the planner high-water mark of 4,731 MiB and 7 staged cells includes earlier stages.

The limitations are material: one seed and one run; a flat controlled world, residents starting together, two shared sources, nearby home camps, enlarged storage, and sufficient food. After one productive gather, every resident receives an ordinary Stop that permits delivery to finish; this equalizes access to the final stock and does not prove fairness of unlimited autonomous gathering. The `candidate_checks ≤ 2×visits` bound applies to this fixture, not to selector complexity in general. Dense resources, obstacles, portals, builder/hauler switching, long forgetting, save/network/UI, collisions, RAM/VRAM, and complete Utility AI were not validated by this benchmark.

### Windows package 0.9

The extracted `dist/Ribterra-Windows-x64-0.9.0.zip` passed **4/4 smoke with exit 0**: menu, mine, professions, and network. Helper probes were copied only into the extracted directory. All 23 package profession PNG files are byte-identical to the reviewed GPU set; three menu/mine/network frames were reviewed separately. [Archive metadata with current SHA-256](evidence/profession-package.json), [smoke](evidence/profession-package-smoke.json), [PNG comparison](evidence/profession-package-visual-comparison.json), [package GPU](evidence/profession-package-device.json). A dedicated server and two separate clients synchronized construction at tick 176: one Applied, one Rejected, Wood 50 / reserved 0, with no duplicated materials. This is loopback, not two physical PCs. [Network](evidence/profession-package-network.json). `Play Ribterra.cmd` reads `dist/current-build.txt` and now launches `dist/releases/0.9.0/Ribterra/Ribterra.exe`.

The package, listed GPU frames, and benchmark belong to source `d3bc78530ab2f03d8116bb9ef4c09ba71ca0232d`. After package smoke, the archive received only an updated README: **49 of 50 files are unchanged**, and every runtime, asset, and license file is byte-identical to the validated extracted copy. [Comparison at the README-only update](evidence/profession-package-readme-update.json). Later documentation edits do not mean that the executable package was republished.

### CI and current source

[CI `d3bc785`](https://github.com/fipsyatina/ribterra/actions/runs/34248830961) succeeded: **Windows 46/46 in 164,01 s**, **Linux 30/30 in 68,64 s**, **UBSan 10/10 in 49,14 s**. Windows had no C++ warnings; the Linux builds together reported **60 `-Wmissing-field-initializers`** for `Resident::profession`, `GameCommand::assignment`, and `CommandReply::residents`. There were no UBSan errors. [Windows log](evidence/profession-ci-windows-d3bc785.txt), [Linux log](evidence/profession-ci-linux-d3bc785.txt), [UBSan log](evidence/profession-ci-ubsan-d3bc785.txt), [Windows quality](evidence/profession-ci-windows-d3bc785-quality.json), [Linux quality](evidence/profession-ci-linux-d3bc785-quality.json).

Current source **`42036ffc3e08bc967b4b477f8a599ac0629e5658`** adds `{}` only to these three fields, preserving their earlier values, layout, save/wire behavior, gameplay, and graphics. The local MSVC Release build had zero warnings; targeted skills/professions/persistence/server/protocol/network_session tests passed **6/6 in 56,21 s**. [Build evidence](evidence/profession-defaults-build.json), [CTest](evidence/profession-defaults-ctest.txt). The Windows ZIP and 0.9 launcher from `d3bc785` are unchanged; the GCC-diagnostic fix is not a newly published Windows archive.

**Final CI `42036ff` succeeded:** [run 34250688287](https://github.com/fipsyatina/ribterra/actions/runs/34250688287), **Windows 46/46 in 171,96 s**, **Linux 30/30 in 79,14 s**, **UBSan 10/10 in 59,83 s**. Both jobs had zero C++ warnings/errors and UBSan errors. [Run metadata](evidence/profession-ci-42036ff.json), [Windows](evidence/profession-ci-windows-42036ff.txt), [Linux](evidence/profession-ci-linux-42036ff.txt), [UBSan](evidence/profession-ci-ubsan-42036ff.txt), [Windows quality](evidence/profession-ci-windows-42036ff-quality.json), [Linux quality](evidence/profession-ci-linux-42036ff-quality.json). Package 0.8 and CI `f62cea0` below are historical. These checks do not close the complete engine or V01–V03.

### Genuine v7 saves for the next migration

Unchanged 0.9 headers/libraries from `d3bc785` created **25 format-7 files** through ordinary commands and ticks. A separate verifier confirmed byte-exact resaving and identical checksums on each of the **64 subsequent ticks for every file**, without clearing undrained path outcomes. This is 1600 continuation comparisons plus 25 initial states. [Catalog and reproduction method](../tests/fixtures/WORLD_SAVE_V7.md), [provenance and SHA-256](../tests/fixtures/world_save_v7.provenance.json), [complete verification sequences](evidence/profession-v7-fixtures.json), [independent review of sources and results](evidence/profession-v7-fixture-review.json). Frozen schema `world_save_v7.fbs` was added. Format 8 and migration 7→8 had not yet been implemented; these fixtures are not acceptance of the next manual-control stage or the entire engine.

## Work continuation and builders 0.8

Contract: protocol 5 / rules 5 / generator 2 / save 6. The final local Windows Release build passed **40/40 CTest in 98,65 s** and installed a separate package at `dist/releases/0.8.0/Ribterra`. [CTest](evidence/work-ctest.txt), complete log: `.deps/work-final-build.log`. The builds for this milestone had no MSVC C++ warnings. CI source commit `f62cea0` also passed; exact results and logs appear below.

New regressions cover backoff under a full queue, a preserved fair cursor for 256 residents, manual replacement before and during search, normal depletion, a truly invalid target, builder death/reassignment, exactly-once payment, retry after topology changes, deadline/ID overflow, duplicates, and old sites with multiple builders. Genuine v5 cargo/continuation/paid-Work fixtures migrate with exact planner state and `.v5.bak`; all earlier migrations pass. Server/protocol/client/GNS checks cover owner-only status, full/delta/removal, 6000 multipart records, late join/reconnect, and absence of external CommandReply messages for internal routes.

The first run found an incorrect ICU argument for waiting status and a delivery stall after cancellation of the preceding order between cell centers. The first defect was fixed by separating the text from the formatted quantity. The second was fixed by a physical final step for a zero-length route: a rounded coordinate no longer substitutes for exact arrival. A separate regression checks a 0,125-cell step and save/resume halfway through it; the repeated-construction network scenario passes. Site Planning is now distinct from actual Travelling.

`WorkVisualProbe` passed on **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver 32.0.15.9282 / software=false**. In the ordinary-order scenario, builder No. 7 replaced No. 1, reached site `(20,20)`, and completed House at tick 569; Wood 50 / Stone 25 were not charged a second time. All 12 tree units at `(30,22)` produced exactly 36 Wood, a stockpile of 116, and normal Exhausted at tick 1720. A separate natural scenario with two residents and 512 undrained external replies preserved the delivered 83 Wood and showed automatic retry at tick 681.

EN/UK/RU/PL/ES, 31 scene loads with comparison of each following 8 ticks, and real RmlUi pointer events were validated. **21 PNG files** were reviewed, including Ukrainian at 1100×700. After the Planning clarification, six changed frames were reviewed again; the other 15 are byte-identical to frames already reviewed. [GPU device](evidence/work-gpu-device.json), [results](evidence/work-gpu-results.json), [SHA and review](evidence/work-visual-review.json), [retry waiting](screenshots/work/uk-compact-retry-waiting.png), [builder planning](screenshots/work/uk-builder-planning.png). The final `LogisticsVisualProbe` also passed: [results](evidence/work-logistics-gpu-results.json); all [17 PNG files are identical to reviewed 0.7 frames](evidence/work-logistics-comparison.json).

### Scale and exact completion accounting

[SimRunner CSV](evidence/work-benchmark.csv), seed 70421, Release x64, ran without a concurrent build or tests; the earlier user game 0.6 remained running. [Machine](evidence/work-test-machine.json). Times in the table are milliseconds; the fixture has a short physical delivery through a portal and one shared exhaustible vein.

| Residents | Ticks | p95 tick | p99 tick | max tick | Stone delivered | Complete / Failed deliveries |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 69 | 0,019 | 0,426 | 0,426 | 20 | 10 / 0 |
| 100 | 70 | 0,228 | 3,454 | 3,454 | 200 | 100 / 0 |
| 500 | 75 | 4,021 | 5,116 | 5,116 | 1000 | 500 / 0 |
| 1500 | 87 | 5,810 | 6,422 | 6,422 | 3000 | 1500 / 0 |
| 4×1500 | 220 | 7,322 | 7,977 | 15,072 | 12000 | 6000 / 0 |

Every resident gained XP and remained alive; at the end, each was Idle and cargo, active intents, and pending paths were zero. For 6000 residents, 6826 Work Intents were created: **826 Started + 6000 Exhausted, zero Failed/Superseded/Cancelled**. Started completes route handoff; later exhaustion can create a new separate intent. There were 1088 admission attempts in total, peak Waiting 1130, and at most 128 visits / 32 admissions per tick. When reading only truncated history, the early benchmark missed terminal transitions in the same tick; final counters record every first transition before truncation and verify that their sum equals the number of created intents. They are not saved and do not affect the gameplay checksum; a 640-intent regression validates correct accounting with history size 256.

This closes the work-continuation failures seen in the 0.7 fixture (943 Failed after delivery), but not the complete performance gate. Hierarchical pathfinding, complete AI, crowd collisions, long adverse routes, RAM/VRAM/rendering, and the external network remain unvalidated.

### Windows package 0.8

`dist/Ribterra-Windows-x64-0.8.0.zip`: **29 944 245 bytes**, SHA-256 `4A2433F3FDECB2F896D25AAC3C6CF31F2910AF0B946AA98E092A16767B7C3601`. [Metadata](evidence/work-package.json). It was extracted to `artifacts/package-0.8.0-final`. The packaged client completed separate smoke and menu capture with exit 0; MineVisualProbe used DLL/assets from the extracted package and saved/restored the view at tick 3036, total Stone 88 (86 available, 2 still carried in the frame). The helper probe was copied only into the extracted directory; the ZIP does not contain it.

The server, two separate probes, and graphical client from the same archive passed a loopback scenario: distinct player IDs, one Applied and one Rejected for competing construction, a shared completed House at tick 168, Wood 50 / reserved 0, chat, and server save. This is not a two-physical-PC test. [Smoke](evidence/work-package-smoke.json), [device](evidence/work-package-device.json), [mine](evidence/work-package-mine.json), [network](evidence/work-package-network.json). Three package frames were also reviewed: [menu](screenshots/work/package-menu.png), [mine](screenshots/work/package-mine-resumed.png), [network](screenshots/work/package-network.png).

[CI for source commit `f62cea0`](https://github.com/fipsyatina/ribterra/actions/runs/34238060438) succeeded: **Windows 40/40 in 138,43 s**, **Linux 26/26 in 56,79 s**, **UBSan 8/8 in 36,21 s**. There were no C++ warnings or UBSan runtime errors. [Metadata](evidence/work-ci-f62cea0.json), [Windows](evidence/work-ci-windows-f62cea0.txt), [Linux](evidence/work-ci-linux-f62cea0.txt), [UBSan](evidence/work-ci-ubsan-f62cea0.txt), [Windows quality](evidence/work-ci-windows-quality.json), [Linux quality](evidence/work-ci-linux-quality.json). The following report commit with `[skip ci]` changes only documentation and this evidence; the validated code and package belong to `f62cea0b34ad8495d8ce7a598e79f3f2d3f0e290`. These results close the 0.8 milestone, not the complete engine.

## Physical inventories and construction delivery 0.7

Current contract: protocol 4, rules 4, generator 2, save 5. Items have an owner and a Ground/Storage/Carried/Construction location; available + reserved = total, and one resident carries no more than 10 units of one kind. Construction reserves materials but begins Work and consumes them only after complete physical delivery. Exact rules and open requirements: [LOGISTICS_PLAN.md](LOGISTICS_PLAN.md).

The final `scripts/build.ps1 -SkipBootstrap` successfully completed the Release build, **37/37 CTest in 82,62 s**, and installation; the C++ code had no MSVC warnings. [CTest](evidence/logistics-ctest.txt), [machine](evidence/logistics-test-machine.json). Regressions cover competition for the last item, a route through a portal, cancel/death/storage full, absence of loss and duplication, unchanged state after failed import, stack/task/ID/INT_MAX limits, storage-filter revalidation, a fractional coordinate near the map boundary, and typed work-continuation failures. UI validation uses real pointer down/up with delivery markup changing between them: cancellation sends exactly one correct building ID. A later successful haul clears the earlier Failed state from the resident’s server projection; a separate one-carrier regression confirms this.

The final `LogisticsVisualProbe` passed on **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. In every EN/UK/RU/PL/ES scene, cargo appeared at tick 18 and the house completed at tick 199; save/resume during carry produces identical later states. Delivery-before-work, capacity, quantity conservation, and pointer cancellation during transport were checked. 16 HUD frames were reviewed, including 1100×700; after the final projection fix, they are byte-identical to the reviewed set. An additional frame 17 with the panel closed shows the physical gray cargo in the resident’s hands. [GPU](evidence/logistics-gpu-device.json), [results](evidence/logistics-gpu-results.json), [comparison](evidence/logistics-visual-comparison.json), [Ukrainian inventory](screenshots/logistics/uk-carrying.png), [cargo in the world](screenshots/logistics/uk-carrying-world.png).

Archive `dist/Ribterra-Windows-x64-0.7.0.zip`: **29 877 117 bytes**, SHA-256 `2F86CCAAA602C762471D0E826C38328008C0358E504662BDFA5839E430FE92CF`. [Metadata](evidence/logistics-package.json). The extracted copy at `artifacts/package-0.7.0-final/Ribterra` passed the GPU menu, MineVisualProbe, and a separate dedicated server + two probes + graphical client. Both clients observed reservation, cargo, and delivery and reached a completed House at tick 172 with Wood 50 / available 50 / reserved 0. One competing Build was Applied and the other Rejected; materials were not duplicated. [Network](evidence/logistics-package-network.json), [mine](evidence/logistics-package-mine.json), [package GPU](evidence/logistics-package-gpu-device.json). Reviewed frames: [menu](screenshots/logistics/package-menu.png), [mine](screenshots/logistics/package-mine-resumed.png), and [network](screenshots/logistics/package-network.png). Installation uses `dist/releases/0.7.0/Ribterra`, and `Play Ribterra.cmd` reads `dist/current-build.txt`; the earlier open 0.6 game was not interrupted.

[CI for `8f23e51`](https://github.com/fipsyatina/ribterra/actions/runs/34228038088) succeeded: **Windows 37/37 in 93,34 s**, **Linux 24/24 in 54,23 s**, **UBSan 7/7 in 34,36 s**. There were no C++ warnings or UBSan runtime errors. [Metadata](evidence/logistics-ci-8f23e51.json), [Windows](evidence/logistics-ci-windows-8f23e51.txt), [Linux](evidence/logistics-ci-linux-8f23e51.txt), [UBSan](evidence/logistics-ci-ubsan-8f23e51.txt), [Linux counters](evidence/logistics-ci-linux-quality.json). The later report commit with `[skip ci]` does not change validated code. This evidence closes the 0.7 milestone, not the complete engine.

The [genuine 0.6 fixture](../tests/fixtures/world_save_v4_builds.ribsave) was created by the unchanged old writer before the schema change: 526972 bytes, SHA-256 `394268f0e8e3efe8a31b22db99821ad1ccfce200939088ecb5697f465e35923a`. It contains both an already paid unfinished House and a pending Farm. Migration preserves the pending ID, creates physical reservations, does not pay for the House again, and brings both buildings to completion. Corrupted semantic payloads are tested after recomputing FlatBuffer/zstd/container checksums, so the tests reach world validation. [Contract and provenance](SAVE_FORMAT.md).

[SimRunner CSV](evidence/logistics-benchmark.csv) measures useful extraction by every resident and return of Stone through a portal to that nation’s Camp. Times below are milliseconds of `World::tick` only; report aggregation is outside the timer. The run had no concurrent build or other tests; the earlier user-opened 0.6 game remained running.

| Residents | Ticks | p95 tick | p99 tick | max tick | Stone delivered | Final Complete / Failed |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 68 | 0,022 | 0,927 | 0,927 | 20 | 10 / 0 |
| 100 | 69 | 0,094 | 3,502 | 3,502 | 200 | 100 / 0 |
| 500 | 73 | 4,220 | 5,165 | 5,165 | 1000 | 500 / 0 |
| 1500 | 85 | 5,505 | 7,765 | 7,765 | 3000 | 1500 / 0 |
| 4×1500 | 236 | 7,649 | 9,385 | 14,532 | 12000 | 5057 / 943 |

Every resident gained XP and remained alive, and all extracted Stone reached storage; pending paths were 0 at the end. For 6000 residents, peaks were 4096 active tasks and 8192 cargo units. **943 tasks ended Failed after delivery** when gather continuation received LimitExceeded (540) or TargetInvalid (403). Counters use the final phase of each ID, so Complete → Failed is not counted twice. Delivery of all material does not mean every continuation succeeded. Automatic retry after queue capacity becomes available remains open.

The fixture starts work near a portal and uses a short route and one exhaustible shared vein. It does not validate crowd collisions, long complex routes, complete AI, RAM/VRAM, render budget, or an adverse network. These results are not the complete performance gate from the original plan.

## Route queue 0.6

Current versions: protocol 3, rules 3, generator 2, save 4. The final local Windows Release build passed **33/33 CTest in 37,32 s** and installed the package; there were no MSVC warnings. [CTest log](evidence/path-ctest.txt), complete build log: `.deps/path-warning-clean-build.log`. Coverage includes shared search for 1500 residents, atomicity, queue bounds and recovery after drain, topology restart/timeout, food reservation, rejection of substituted state, and save/resume in Queued/Search/Reconstruct/Stage.

The initial run had 32/33: a blocked camp starting cell could incorrectly become a transit cell for other residents. The fix leaves such a node as a terminal leaf of the search while allowing only its owner to exit. The next failure in the new limit test was a fixture error because its target was the camp itself; the test now uses a validated passable cell. Linear resident lookup by ID, coordinate validation before indexing, gather-target revalidation before commit, and accounting for messages within the planner-state bound were also fixed.

`PathVisualProbe` passed again on the final SimCore: D3D12, **NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. Seed 70421, 1500 residents, target `(159,159,Surface)`: after saving on the first search tick, the original and restored clients both completed after the same 109 subsequent ticks; all 1500 received an active order. EN/UK/RU/PL/ES, waiting state, typed Queued/Applied/Rejected, and localized rejection for an isolated target were checked. 11 distinct frames were reviewed, including compact 1100×700; five resume PNG files are identical to pending. All 16 final PNG files are byte-identical to the reviewed set. [GPU](evidence/path-gpu-device.json), [results](evidence/path-gpu-results.json), [comparison](evidence/path-visual-comparison.json), [Ukrainian HUD](screenshots/paths/uk-pending.png).

[SimRunner CSV](evidence/path-benchmark.csv) separately measures queueing orders and every tick until the terminal result for those submitted batch IDs. Seed 70421 uses a remote passable target in Mine, meaning a route through a portal; times in the table are milliseconds.

| Residents | Order submission | Completion tick | p95 tick | p99 tick | max tick | Applied / Rejected |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 0,004 | 8 | 2,721 | 2,721 | 2,721 | 1 / 0 |
| 100 | 0,020 | 15 | 3,262 | 3,262 | 3,262 | 1 / 0 |
| 500 | 0,061 | 41 | 2,614 | 2,775 | 2,775 | 1 / 0 |
| 1500 | 0,077 | 109 | 1,462 | 1,660 | 2,250 | 1 / 0 |
| 4×1500 | 0,721 | 434 | 3,069 | 4,701 | 6,145 | 4 / 0 |

Peak work was 4096 per tick. For 4×1500, peak planner state was 43 482 904 bytes and staged cells 409 255. `planner_bytes` is a deterministic estimate of state, not peak process RAM. [Machine specifications](evidence/path-test-machine.json) are preserved. The fixtures do not cover complex AI, crowd collisions, the complete render budget, RAM/VRAM, an adverse network, or the external Steam gate; these requirements remain open.

Archive `dist/Ribterra-Windows-x64-0.6.0.zip`: **29 713 761 bytes**, SHA-256 `50395326E199448431FCC783A4DB3172680FEC1824DECA96AF009502D862E2B0`. [Metadata](evidence/path-package.json). It was extracted to `artifacts/package-0.6.0-warning-clean`; the GPU menu, MineVisualProbe with terminal replies and save/resume, and a separate server + two probes + graphical client passed. [Network result](evidence/path-package-network.json), [mine](evidence/path-package-mine.json). Three package frames were reviewed: [menu](screenshots/paths/package-menu.png), [mine](screenshots/paths/package-mine-resumed.png), [network](screenshots/paths/package-network.png). CI for current code appears below; historical 0.5 results are separate.

The first [0.6 CI for `c710cf2`](https://github.com/fipsyatina/ribterra/actions/runs/34220670669) recorded Linux 21/21 in 16,02 s and UBSan 5/5 in 2,93 s. GCC reported implicit grouping of `&&/||` and partial initialization of test ClientConfig. Added parentheses preserve behavior, and the test now explicitly sets the name after standard initialization. The local run and smoke test of the updated archive above were repeated after this change. The follow-up CI after warning cleanup succeeded.

[Final CI for `1ef93e9`](https://github.com/fipsyatina/ribterra/actions/runs/34221544539) succeeded: **Windows 33/33 in 34,13 s**, **Linux 21/21 in 15,76 s**, **UBSan 5/5 in 2,63 s**. There were no C++ warnings or UBSan runtime errors. [Metadata](evidence/path-ci-1ef93e9.json), [Windows](evidence/path-ci-windows-1ef93e9.txt), [Linux](evidence/path-ci-linux-1ef93e9.txt), [UBSan](evidence/path-ci-ubsan-1ef93e9.txt), [Linux counters](evidence/path-ci-linux-quality.json). This evidence confirms the 0.6 milestone, not completion of every engine requirement.

## Chunks, EnTT and the mine 0.5

After the generator-overflow fix, `scripts/build.ps1 -SkipBootstrap` completed the Release build, **30/30 CTest in 32,60 s**, and package installation without MSVC warnings. Log: `.deps/chunks-overflow-fix-build.log`; [CTest result](evidence/chunks-ctest.txt). Current versions: protocol 2, rules 2, generator 2, save 3. EnTT 3.16 actually stores resident components in SimCore. The control command `SimRunner --ticks 160 --population 10 --seed 70421` has identical checksum `9495560223965183407` before and after the fix.

The world contains 128 pre-created 32×32 chunks: surface and mine are each 256×256 cells at coordinates `[-96,160)`. This is a bounded world without streaming/unload or a complete biome generator. Tests cover negative coordinates, chunk boundaries, reads without generation, explicit portal transit, solid ore, and conversion to floor after depletion.

| Check | Executed scenario |
|---|---|
| `world_validation` | NaN/Infinity before coordinate conversion, enums/UTF-8/limits, duplicates, route and portal continuity; rejection preserves the checksum; reordering resident/route DTOs does not change later ticks; import and tick for 4×1500 |
| `persistence` | Format 3 round trip and continuation; a genuine unchanged 0.4 save with format 2/rules 1/generator 1/exact content hash; overlay of old 0…47 surface data; `.v2.bak`, `.legacy.bak`; corrupted and mixed formats are rejected |
| `protocol` | Full/delta chunk snapshots, 256 KiB wire bound, reordering and identical/conflicting duplicates, 16 MiB assembly bound, timeout, invalid portals, rejection of stale full; incomplete assembly does not change visible state |
| `chunk_network` | Real GNS transfers all 128 chunks in **19 parts**; late join, movement into the mine, rejection of control over another nation, reconnect, save/resume; dropping parts and timeout/resync restore the world without reconnect; a premature ACK disconnects only the offender |
| `server` | Initially exactly one mine cell is known: the portal exit; other ore is redacted to an empty hidden value; authoritative chunk revisions do not enter the projection |
| `ui` / `client_input` | Surface/Mine and Passage intents, active tab, resident layer, five languages, camera/view without simulation mutation; legacy HUD hit regions do not activate over RmlUi |

Schemas pass `flatc --conform` against the baseline save and separately frozen format 2, network payload, and frame. A real 0.4 server created [world_save_v2_current.ribsave](../tests/fixtures/world_save_v2_current.ribsave): seed 424242, 6 residents, 37 ticks. SHA256: `a84c8db20201f00590cb3e5b312ac5b0a05371b78f8036313a2e7f5c166d360c`; expectations are stored beside it. Migration details: [SAVE_FORMAT.md](SAVE_FORMAT.md).

`MineVisualProbe.exe artifacts/chunks-0.5-fixed` passed on a physical RTX 3050 Laptop through D3D12 after the generator fix. A native event selected resident 1; the server moved that resident into the mine; ore at `(27,22)` fell from 24 to 0; the cell became MineFloor; Stone stock became 88. At tick 1058, save/resume preserved every visible chunk, portal, resident, building, resource, and tick. The frame SHA256 matches the previously reviewed 0.5 frame. [Result](evidence/chunks-mine.json), [frame after load](screenshots/chunks/mine-resumed.png), [device](screenshots/chunks/device.json). Unknown space is opaque black. `RenderFrameTests.exe --gpu` also passed again, including controlled text-upload failures: [result](evidence/chunks-gpu-contract.txt).

A separate `process_session_test.py` again checked a dedicated server, two independent probes, and a graphical D3D12 client with a temporary Unicode password: distinct player IDs, shared 50 Wood, 10 residents, two buildings, a reply, and chat. [JSON](evidence/chunks-network.json), [network frame](screenshots/chunks/network.png). This is loopback on one PC.

`UiVisualProbe` produced menu/settings/help/HUD for EN/UK/RU/PL/ES: EN at 100%, the others at 130%, plus a Ukrainian HUD at 1100×700. All 21 UI PNG files were reviewed; with mine and network, the total is 23 frames. Layer and passage buttons, status, help, and long translations fit; Cyrillic and diacritics are readable. [Ukrainian HUD](screenshots/chunks/uk-hud.png), [compact HUD](screenshots/chunks/uk-compact-hud.png), [help](screenshots/chunks/uk-help.png), [Spanish settings](screenshots/chunks/es-settings.png). These are automated native events and framebuffer review, without a claim of a complete keyboard-only/IME/4K gate.

### Windows package

`dist/Ribterra-Windows-x64-0.5.0.zip`: **29 596 671 bytes**, SHA256 `65052A3727502C897B5DD569D36CC2770009FE225D339C46F491C776014D3E3C`. [Metadata](evidence/chunks-package.json). The archive contains the client, dedicated server, probe, SimRunner, DLL, validated assets, and notices; the source DOCX and concepts are absent from the runtime package.

The archive was extracted to a new directory, `artifacts/package-0.5.0-fixed`. Its `Ribterra.exe` successfully opened the D3D12 menu; the PNG has the same SHA256 as the reviewed EN menu capture: `2AB2C19696D29FE17BD2EC3E08BF71A96E7C92620E5B916A449674949B74BF82`. The server, two probes, and graphical client **from the extracted archive** then passed the complete process scenario with saving. [Packaged device](evidence/chunks-package-device.json), [packaged network result](evidence/chunks-package-network.json).

### Initial measurements of scale

`SimRunner.exe --benchmark-chunks --ticks 160 --seed 70421` measures each tick separately with a steady clock; p50/p95/p99 use nearest rank, without averaging batches of ticks. Initial route construction is measured **separately** and is not included in the listed tick percentiles. [All CSV results](evidence/chunks-benchmark.csv).

| Scenario | Residents | p95 tick, ms | p99 tick, ms | Initial command, ms |
|---|---:|---:|---:|---:|
| Move across a chunk boundary | 10 | 0,0105 | 0,0120 | 1,533 |
| Move across a chunk boundary | 100 | 0,0841 | 0,1179 | 10,352 |
| Move across a chunk boundary | 500 | 0,2095 | 0,4030 | 51,765 |
| Move across a chunk boundary | 1500 | 0,6620 | 0,9367 | 144,000 |
| Move through a portal | 4×1500 | 1,4478 | 3,7717 | 407,870 |

The mass command exceeds the tick budget: BFS is still synchronous, so budgeted requests and group planning are needed. The 6000 residents in this fixture have four nation IDs, but the co-op client and resources still represent one shared nation; this is not evidence of four complete economies or networked nations. The run measured 160 simple ticks without complete AI, logistics, crowd collisions, or combat. RAM/VRAM limits, render time, long scenarios, and the final performance gate remain open.

### CI

Initial 0.5 code, commit `036b08b98422cb3ccbb48bf7675998d7611404fd`, [run 34213349737](https://github.com/fipsyatina/ribterra/actions/runs/34213349737), had **Windows success: 30/30 tests in 26.88 s**, job `102019260338`. [Preserved Windows CI result](evidence/chunks-ci-windows.txt). Linux job `102019260354` completed **10/19, with nine failures** caused by `std::bad_alloc` during world creation; `asset_tool` already passed. [Preserved Linux failure](evidence/chunks-ci-linux-036b08b.txt).

In commit `d9e35cca06e02f7014fef902664b9d2d25c8084f`, generator coordinates are converted to `uint32_t` before multiplication, removing signed-overflow UB. Three terrain checks were added at negative and upper boundaries, and a GCC warning about copying a structured binding was removed. [New run 34214848608](https://github.com/fipsyatina/ribterra/actions/runs/34214848608) completed with **success**. Windows job `102024081957` had **30/30 in 27.86 s**; Linux job `102024082232` had **19/19 in 15.06 s** in the ordinary Release build and **4/4 in 1.68 s** under UBSan (`sim_core`, `chunks`, `world_validation`, `persistence`) through preset `headless-ubsan`. [Windows CTest](evidence/chunks-ci-windows-d9e35cc.txt), [Linux CTest](evidence/chunks-ci-linux-d9e35cc.txt), [UBSan CTest](evidence/chunks-ci-ubsan-d9e35cc.txt), [run status](evidence/chunks-ci-d9e35cc.json). The builds contain no compiler warnings or UBSan runtime errors. A later documentation-and-evidence-only commit does not change the validated executable code. The complete ASan/UBSan gate for all subsystems remains open.

For the preceding 0.4 commit, [run 34185301168](https://github.com/fipsyatina/ribterra/actions/runs/34185301168) had Windows success and Linux failure: 14/15 tests, only `asset_tool` failed. Linux found an uppercase-`UI` metadata error before the collision with `ui`. In 0.5, the tool first validates every case-folded path; the test removes the collision fixture before the independent symlink check. A GCC false positive about a dangling reference in a test helper was also removed by using `string_view` and an explicit missing-entry check. Read the 0.5 CI result from the separate current run after push.

## Historical UI and AssetPipeline 0.4

The 0.4 client used RmlUi 6.3, ICU 78.3, and FreeType 2.14.3. Menu, HUD, selected-resident panel, chat, and help loaded from RML/RCSS; language and scale were stored in user settings. Simulation was server-side, and the world was a 48×48 vector prototype.

`scripts/build.ps1 -SkipBootstrap` completed the Release build, installation, and **26/26 CTest in 26.58 s**, with no MSVC warnings. Complete local log: `.deps/ui-build-final.log`; [preserved CTest result](evidence/ui-ctest.txt). New dependencies were pinned in the manifest; this UI milestone did not change rules/protocol/generator/save.

Additional checks:

| Test | What is checked |
|---|---|
| `platform` | Unicode environment names/values, missing/empty values, rejection of invalid names, and Windows Unicode |
| `asset_catalog` | Registered paths, manifest identity, dimensions, SHA256, corruption, and traversal outside the catalog |
| `texture_atlas` | Deterministic packing, padding/extrusion, UV/pivot, limits, and invalid inputs |
| `asset_tool` | JSON/references/five catalogs/provenance/licenses/path collisions |
| `localization` | ICU plurals for five languages, argument names and types, invalid templates/UTF-8, pseudo expansion |
| `atlas_tool` | Real PNG export, Unicode paths, provenance notices, hashes, rejection without partial output |
| `asset_staging` | Native ICU/RmlUi validation candidate; an invalid template or missing UI ID preserves the preceding catalog |
| `ui` | Menu, settings, localization, HUD/selection, focus/input, and chat intents |
| `ui_lifecycle` | Rejection of invalid RML/RCSS and an unsupported filter, followed by successful creation of valid UI in the same process |

`LocalizationCheck` also validates FreeType glyph coverage in all five shipped catalogs. `RenderFrameTests` was extended with generation-ID textures, premultiplied alpha, slot reuse, and rejection of stale handles. The network process fixture uses a temporary Unicode password without printing its value.

On a physical NVIDIA GeForce RTX 3050 Laptop GPU with D3D12, driver 32.0.15.9282, `RenderFrameTests.exe --gpu` passed: two forced text-texture upload failures discard the incomplete cache, and the third frame succeeds. [GPU contract result](evidence/ui-gpu-contract.txt), [device](screenshots/ui/device.json).

`UiVisualProbe.exe artifacts/ui` produced 15 menu/settings/HUD frames for EN/UK/RU/PL/ES plus one compact 1100×700 HUD. EN uses 100%; the other languages use 130%. All 16 PNG files were reviewed: text and controls fit, diacritics and Cyrillic are readable, and panels do not overlap. Examples: [menu](screenshots/ui/en-menu.png), [Ukrainian HUD](screenshots/ui/uk-hud.png), [compact](screenshots/ui/uk-compact-hud.png), [Polish settings](screenshots/ui/pl-settings.png), [Spanish HUD](screenshots/ui/es-hud.png).

Separately, `tests/process_session_test.py` launched a dedicated server, two independent probes, and a graphical D3D12 client with a Unicode password. Both players received distinct IDs, shared 50 Wood, 10 residents, two buildings, a reply, and chat; the server saved the world. [JSON result](evidence/ui-network.json), [network chat frame](screenshots/ui/network.png). Unknown terrain is opaque black. This is loopback on one PC, not a physical LAN session between two machines.

`scripts/package.ps1 -SkipBuild` created `dist/Ribterra-Windows-x64-0.4.0.zip`: **29 383 357 bytes**, SHA256 `EA67BE3D7F71CE69BDE39FA81CE43C51145C7C578E4B1A8FF9689B9CBE6B2735`. The archive was extracted to a separate directory, `artifacts/package-0.4.0-smoke`; its `Ribterra.exe --menu-capture ... --renderer-info ...` exited with code 0 on D3D12. The menu PNG has the same SHA256 as the reviewed build capture. [Package metadata](evidence/ui-package.json), [packaged device](evidence/ui-package-device.json). The archive does not contain the source DOCX or concepts.

These are automated native checks and framebuffer reviews. Complete human keyboard-only/IME testing, 1440p/4K, video settings, and every planned screen remain unconfirmed. AtlasTool is an offline tool; production artwork and runtime hot reload are not yet connected.

## Historical local build 0.3

The command `scripts/build.ps1 -SkipBootstrap` completed configuration, the Release build, CTest, and package installation. CMake output directory: `build/engine-windows`; package: `dist/Ribterra`; log: `.deps/gpu-build.log`.

CTest: **17/17 passed**, 0 failed, total time 19.67 s in the final run after fixing the Linux ABI and snapshot waiting in the test. The MSVC log contains no compiler warnings. In addition to the 14 foundation tests below, the new milestone adds:

| Test | What was actually checked |
|---|---|
| `shader_contract` | DXIL, JSON reflection and manifest correspond to HLSL entry points and expected bindings |
| `shader_negative` | A broken shader fixture is rejected by the validator without replacing valid output |
| `render_frame` | Explicit software backend, painter-order geometry/text, alpha, clip, resize capture, and letterbox coordinate round trip |

## Historical baseline 0.2

The command `scripts/build.ps1` completed pinned bootstrap, configuration, the Release build, CTest, and package installation. CMake output directory: `build/engine-windows`; package: `dist/Ribterra`. Preserved log: `.deps/engine-build.log`.

CTest: **14/14 passed**, 0 failed, total time 20.81 s. The MSVC log contains no compiler warnings.

| Test | What was actually checked |
|---|---|
| `sim_core` | Deterministic world, IDs, movement, gathering, construction, food/season, and current simulation boundaries |
| `persistence` | FlatBuffers+zstd format 2, exact round trip/resume, bounds/checksum/semantic rejection, backup rotation, missing-primary recovery and one-way legacy migration |
| `server` | Authenticated player source, observer/spoof rejection, request dedupe/conflict, command order, snapshot rights, reconnect without duplicate residents, chat limits/history/mute |
| `protocol` | FlatBuffers frame round trip, invalid/truncated/oversized data, full/delta, apply, removals, wrong-baseline resync and password-proof binding |
| `transport` | Two real standalone GameNetworkingSockets endpoints in loopback, reliable/unreliable delivery, lanes, disconnect, message limit and busy port |
| `save_schema_conform` | `flatc --conform` for the current and baseline save schemas |
| `network_conform` | `flatc --conform` for the network-protocol payload schema |
| `network_frame_conform` | `flatc --conform` for the outer compressed-frame schema |
| `sim_boundary` | Recursive absence of platform, presentation, transport, and server dependencies in public SimCore headers and implementation |
| `network_session` | Password success/failure, two clients, one authoritative world state, full/delta sync, one in-flight baseline, reconnect cursor and replay reply to batched ack |
| `dedicated_processes` | Separate `RibterraServer` and two independent `RibterraNetProbe` processes; distinct player IDs, shared resources, two buildings, chat, and server save in a temporary path containing Cyrillic |
| `client_input` | SDL-window 1100×700, logical-coordinate input, UI-commands through embedded GameServer, black unknown area and save/resume |
| `client_smoke` | Launch and framebuffer capture local client from the SDL dummy/software backend |
| `network_client_rendered` | A separate dedicated server and graphics client, remote full/delta sync and framebuffer 1100×700 |

Commands executed directly or included in a successful build/test run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
.\dist\Ribterra\RibterraServer.exe --help
.\build\engine-windows\SimRunner.exe --ticks 1 --population 10 --seed 70421
```

The `tests/process_session_test.py` scenario sets a random password only in child-process environments and does not print it. It reserves a local UDP port, launches server and clients as separate processes, and checks the JSON result, saved world, and server termination.

Import inspection for `RibterraServer.exe` showed only system Windows DLLs (`SHELL32`, `WS2_32`, `CRYPT32`, `WINMM`, `IPHLPAPI`, `KERNEL32`, `USER32`, `ADVAPI32`, `dbghelp`). The dedicated executable does not import SDL, GPU, or Steam client runtime.

Remote-session frame: [network-session.png](screenshots/network-session.png). It comes from the framebuffer of the real `Ribterra.exe` after synchronization with a separate `RibterraServer`; reviewers checked 1100×700, the opaque black unknown region, and shared state after commands from two CLI clients. Input events and the environment were automated. This is not a manual human-input test or a session on two physical PCs.

GitHub Actions run `34177929620` for the preceding 0.2 commit had a Linux failure. The next run below confirms its fix.

Cause of the Linux failure: `std::int64_t*` is `long*`, while `ISteamNetworkingSockets::SendMessages` requires the library’s `int64*` (`long long*`). Version 0.3 uses the exact API type. Both jobs in [run 34180370872](https://github.com/fipsyatina/ribterra/actions/runs/34180370872) for commit `46df17f555ce98697b331ecc4377e98f5287e719` succeeded: Windows `101918059824`, Linux `101918059974` (11/11 tests, 9.51 s), 2026-09-08. This is a 0.3 result, not CI evidence for current 0.4 changes. The network test also continues server ticks while waiting for the snapshot after ack; its separate repeat passed 5/5 runs without changing the timeout or final assertions.

## SDL_GPU/D3D12 milestone 0.3

The code contains backend-neutral `Frame2D`, `Renderer2D`, an explicit software test backend, D3D12 selection without fallback, offscreen capture, letterbox coordinate conversion, and a build-time shadercross pipeline. The package contains `assets/shaders/sprite.vert.dxil`, `sprite.frag.dxil`, corresponding JSON reflection files, and `manifest.json`; `shadercross`, DXC, and SPIRV-Cross are build tools only.

`RenderFrameTests.exe --gpu` passed on a physical NVIDIA GeForce RTX 3050 Laptop GPU, backend `direct3d12`, driver 32.0.15.9282. It checked geometry/text painter order, alpha, clip, letterbox round trip, and offscreen captures at 1440×900 and 1100×700. A separate dedicated-server + two `RibterraNetProbe` + graphical D3D12-client scenario also passed. The first settlement, compact frame, developed settlement, and network frame were reviewed visually; unknown terrain remains opaque black.

Artifacts: [GPU device](screenshots/gpu-device.json), [first settlement](screenshots/gpu-first.png), [compact 1100×700](screenshots/gpu-compact.png), [developed settlement](screenshots/gpu-developed.png), [network session](screenshots/gpu-network.png). `software:false` in DeviceInfo describes the selected backend; the device name and actual D3D12 run also confirm the physical GPU. `--gpu-debug` remains a diagnostic flag, not separate performance evidence.

Software-capture cropping was fixed; the final local 0.3 CTest includes successful `render_frame` and is part of the 17/17 result above.

## Verified data security behavior

An unauthorized or disconnected ID receives an empty snapshot. A cell unknown to the client is serialized as empty and rendered as solid opaque black; hidden buildings are absent from the snapshot. The network does not serialize complete `WorldState`, the seed, or server routes.

Decoding validates FlatBuffers, versions, content hash, compressed and decompressed sizes, collections, and semantics before applying state. Tests alter headers, checksum, baseline, world ID, lengths, enum/range values, and truncate data.

A save is written to `.tmp`, read back, and compared before replacement. The preceding valid slot moves to `.bak`; a missing primary is restored from it. An existing corrupt primary does not trigger silent rollback. Physical power loss during writing was not tested.

## Historical M0 load test

Version 0.1.0, seed 70421, 300 measured ticks after warmup. This measures only the simplified SimCore of that time, without rendering, complete AI, networking, combat, or four nations.

| Residents | Scenario | p95 tick, µs | p99 tick, µs | Mass-order creation, ms |
|---|---|---:|---:|---:|
| 10 | Movement | 0.240 | 0.260 | 0.248 |
| 100 | Movement | 2.160 | 2.160 | 2.742 |
| 500 | Movement | 11.260 | 11.380 | 12.185 |
| 1500 | Movement | 65.520 | 66.080 | 41.411 |

Synchronous route creation for 1500 residents cost about 41 ms. These historical numbers do not establish 4×1500, final AI, 60 FPS, or the stated system requirements.

Historical local M0 frames: [first settlement](screenshots/first-settlement.png) and [developed settlement](screenshots/developed-settlement.png).

## Unchecked boundaries

Not validated: SDL_GPU/D3D12 performance budget, shader hot reload/fallback, the complete UI/IME/keyboard-only gate, audio, chunk streaming and the complete generator, complete AI/logistics/combat/society, complete game content in five languages, LAN discovery, Steam, durable reconnect after server restart, autosave ring, crash/power-cut fault injection, macOS, 2–4 physical PCs, latency/loss/long-soak, and final performance/memory gates.

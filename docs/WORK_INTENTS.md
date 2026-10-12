# Work continuation and builder assignment in 0.8

> English translation of a historical project document. Results apply to the named versions and snapshots. The original wording is retained in import commit `7388a2f`; translation does not validate the current build.

Status: local 40/40 CTest, GPU, benchmark, and package smoke for 0.8 passed. CI source commit `f62cea0` passed Windows 40/40, Linux 26/26, and UBSan 8/8. The complete engine scope remains in [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md); actual evidence is in [VALIDATION.md](VALIDATION.md). This document does not claim complete Utility AI readiness.

This is the historical **0.8** module contract; the versions and acceptance below apply to that slice. **0.9: protocol 6/rules 6/generator 2/save 7** adds professions, automatic selection of compatible work, partial Assign/Stop, and independent skills on top of Work Intents. Changes accepted during protected delivery wait for its completion. See [professions](PROFESSIONS_PLAN_UA.md), [SKILLS.md](SKILLS.md), and [SAVE_FORMAT.md](SAVE_FORMAT.md). Local/GPU/package evidence is in [VALIDATION.md](VALIDATION.md). CI 0.9 for `d3bc785` succeeded; repeated CI for `42036ff` passed without C++ warnings or UBSan errors. VALIDATION distinguishes source and package. Complete AI and the manual queue/command catalog remain open.

## Behavior

After physical dropoff, the delivery remains Complete. A separate WorkIntent retains the intent to return to gathering. If admission to the path queue is temporarily unavailable, the resident waits and retries. Exhaustion of the expected resource before route admission or application completes gathering as Exhausted; an incompatible target remains a typed failure. Retrying creates no extra items or XP.

A paid building in Work retains progress when its last worker dies or receives another order. The scheduler selects an idle resident of the same nation, reserves that resident through BuildAssignment, and requests an ordinary route. Work resumes only after route completion and arrival at an adjacent cell; materials are not charged again. Existing workers at other Work buildings are not taken away.

Rejected admission of a new manual order leaves the old intent unchanged. An accepted manual order ends its corresponding automatic intent and internal pending path. If the new route later fails, the old intent is not restored automatically. Reserved physical cargo follows its own logistics rules and is not cancelled by this mechanism.

## Persistent contract

WorkIntentState contains intent ID, GatherContinuation/BuildAssignment kind, owner, resident/building ID, target/job/expected terrain, phase, failure/completion, creation/next-attempt/deadline times, attempt count, path correlation, and the attempt's last topology revision. Phases are Waiting, PathPending, Complete, and Failed. Normal completion reasons are Started, Exhausted, Superseded, and Cancelled. **Started means that the route was handed to the resident, not that the resident physically arrived.**

WorldState stores the next intent ID, all records, and separate fair-traversal cursors for intents and Work buildings. A resident has at most one active intent; a building without a worker has at most one active BuildAssignment. A building with no idle candidate displays Waiting without creating a resident ID 0 record.

| Limit | Value |
|---|---|
| WorkIntent records | 8192 |
| Records/buildings visited per tick | up to 128 combined |
| Admission attempts per tick | up to 32 |
| Retry intervals | 2, 4, 8, 16, then 32 ticks |
| Maximum attempts / admission deadline | 64 / 1200 ticks |
| Ordinary completed history | up to 256 records |

Visit limits do not establish a CPU budget: resident indexing, workforce collection, history trimming, and path callbacks have their own costs. Measure them together with the complete tick.

Failed BuildAssignment retains its blocking reason rather than starting another 64 attempts every tick. An unavailable worker can be replaced. Unreachable/TopologyChanged is reconsidered after a topology revision change; the new attempt obeys the same limits. Other exhausted deadlines/attempt limits retain an explicit failure until another accepted manual order. Version 0.8 does not select a new profession or resource. Version 0.9 separately selects compatible work for the assigned profession but does not change the profession itself.

Save 6 / rules 5 / generator 2 explicitly migrate genuine 0.7 format-5 saves with `.v5.bak`. A pending continuation from a Complete LogisticsTask becomes a WorkIntent with the same path batch and search state. Completed old failures without a lost intent are not revived. Physical items, reservations, and paid Work are preserved. See [SAVE_FORMAT.md](SAVE_FORMAT.md) for byte-level checks.

Protocol 5 sends only permitted resident work statuses and the workforce of visible owned sites. Internal Work results are not replies to player commands. Old Exhausted/Failed display history is retired after a new order or the next delivery. The UI distinguishes automatic waiting from terminal failure; inventory is not duplicated in the new DTO.

Workforce distinguishes Waiting, Planning, Travelling, and Working. A pending path means Planning; Travelling requires an installed route, and Working requires actual arrival. If an earlier order stopped a resident between cell centers, even a zero-edge route contains a final physical step to the target cell center. Position rounding does not grant delivery or construction progress early.

Diagnostic counters for creation and first terminal transitions accumulate within the current World instance independently of history trimming. They are not simulation state, do not enter saves/checksums, and restart from zero after loading. SimRunner sums these transitions and admission attempts instead of trying to reconstruct lost phases from the last 256 records.

## Acceptance

Locally executed scenarios, with evidence in VALIDATION: a full queue with actual dropoff and save/load during backoff; normal exhaustion and a truly invalid target; manual orders before and during search; builder death/reassignment; opening a previously blocked passage; malformed cross-reference rejection without mutation; genuine v5 migration; owner-only full/delta/removal and reconnect; localized GPU frames; 10/100/500/1500/4×1500 with completion of all intents and separate retry/failure metrics.

Professions and priorities were implemented in 0.9 under the separate contract above. Complete AI, remaining profession execution scenarios, hierarchical pathfinding, crowd avoidance, production, roads/danger, society, combat, and other original-plan requirements remain in scope.

# Professions, assignments, and experience: the 0.9 slice

> English translation of a historical project document. Results apply to the named versions and snapshots. The original wording is retained in import commit `7388a2f`; translation does not validate the current build.

Status: the 0.9 slice is implemented. Local, GPU, package, and CI checks for `d3bc785` passed. After initializer corrections, repeated CI for `42036ff` passed without C++ warnings or UBSan errors. Current rules are described in [SKILLS.md](SKILLS.md); the source and unchanged package are distinguished in [VALIDATION.md](VALIDATION.md). The source is the Skills, Control, and AI sections of the [original plan](source/development-plan-ua.txt), lines 311–349. The complete S04–S07 and T01 requirements in the [registry](ENGINE_REQUIREMENTS.md) remain open.

## Player-facing behavior

The player assigns a resident's profession and workplace, changes priorities, and sees the reason for waiting and the actual work speed. Profession is separate from the current Job. A manual Move temporarily interrupts the schedule; after arrival, the resident returns to the assignment. Stop disables automatic work until another explicit assignment. Experience from previous professions is retained.

## Implementation

- Profession, workplace, permitted work, priorities, and temporary manual override state are stored separately. The first executable jobs use the existing Woodcut/Mine/Forage/Build/Haul paths; the complete profession catalog remains a required extension.
- Authoritative assignment and Stop commands check rights, deduplicate requests, return per-resident group outcomes, and define the ordering of conflicts between two clients. Reassigning a physical carrier does not remove cargo/reservations; the change takes effect at a safe delivery boundary.
- Work selection considers player priority, profession, availability, and distance; stable ID breaks ties. Resource indexing and pruning distant buckets reduce ordinary search, but a complete scan of candidates in a dense bucket remains possible. Visit/admission limits do not bound the total number of candidates examined. Worst-case behavior and the complete AI/performance gate remain open. Work search is spread across ticks with a target frequency of 1–2 Hz. Execution uses Work Intents and logistics states.
- The server ModifierSystem uses independent XP, separate familiarity, slow familiarity decay of at most 2 percentage points, one combined −15…+15% cap, and an explainable ModifierBreakdown. Curves, time parameters, and limits must be explicit and included in the gameplay hash before a format change. Permanent XP is never erased.
- The resident card shows profession, assignment, selection/waiting reason, practice, and speed. “+15% speed” does not mean “−15% time.” The projection carries the ready server calculation without foreign professions, hidden targets, or internal task/path IDs.

## Acceptance criteria

1. Woodcutter → miner → woodcutter: independent XP persists; permitted work and actual speed change. Assignment itself grants no XP.
2. XP and items are granted once for useful work. Queue pressure, full storage, retry, cancellation, repeated commands, and save/load produce no extra result.
3. Manual Move and Stop have different durations of effect; loading preserves the correct state. Current cargo reaches a safe boundary without material loss.
4. Work selection is deterministic and respects profession restrictions and priorities. No candidate produces a specific waiting reason.
5. Extreme modifier combinations remain within −15…+15%; the displayed breakdown matches simulation progress. A long break does not erase XP.
6. Compare every tick after save/resume during a manual override, Waiting/PathPending, carrying, and a profession change.
7. Check ownership, retries, simultaneous changes, late join/reconnect, keyed full/delta/removal, and absence of hidden-data leakage. Internal assignment creates no player CommandReply.
8. Check real frames and interactions in five languages; measure selection, routing, and complete ticks for 10/100/500/1500 and 4×1500 residents. Earlier 0.8 results do not replace these measurements.

## Format migration

Before the schema changed, the unchanged, verified 0.8 runtime captured [eight genuine format-6 fixtures](../tests/fixtures/WORLD_SAVE_V6.md): cargo, Waiting, queued and partially searched Work paths, travel/work beside a paid building, Failed, and Exhausted. Each has a byte-identical resave and 64 subsequent ticks with identical checksums in the old loader. Implementation 0.9 adds format 7 and checks migration of each file followed by 128 ticks of continuation.

Migration adds a neutral assignment without inventing a workplace, transfers XP exactly, and does not revive completed intents. Planner frontier, IDs, deadlines, cursors, and physical item accounting are preserved. A strict old hash, `.v6.bak`, append-only schema/conformance, and all previous migrations are required.

Complete Utility AI, needs, social relationships, the broader profession catalog, physical farm work, and other engine requirements remain in scope. The full engine is followed by the user-supplied assets and large terrain based on concepts, then the coordinate indicator. This slice does not complete the art work.

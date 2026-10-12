# The archival plan for the management of the inhabitants: S07

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The plan below is preserved as a history of decisions until implementation 0.10. Currently executed contracts, actual limits and changes to network recovery are given in [MANUAL_ORDERS.md](MANUAL_ORDERS.md), and checks — in [VALIDATION.md](VALIDATION.md). The future formulations below are not the current status of these mechanics.

**Status: plan for the next release, not the possibility of the game being released.** The document is based on the fixed 0.9.0 / `d3bc785`; actual receipt and further correction of warnings GCC described in [VALIDATION.md](VALIDATION.md). The plan does not change the code, rules, format of the save or the results of the acceptance 0.9. The new types and numerical limits below are design propositions; The implementation requires a joint contract DTO and measurement. The corrections made after the review of the code for compatibility decisions are given separately.

The next small limit's goal is — a line of available manuals Move/Gather, partial execution of the group order, and temporary server rights for inhabitant management. All 38 requirements of the output plan and the addition V01–V03 remain in the [ registry ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md). This stage also does not completely close S07.

## What a Source Requires

Authoritative source  [development-plan-ua.txt](source/development-plan-ua.txt), rows of an unaltered text copy of the original DOCX:

| Rare | The need and impact for this stage |
|---|---|
| L311, L315–318 | The profession, skill and current activity are separated; Handwriting or re-designation doesn't erase experience. |
| L341–342 | Orders to a resident or group: move/take/drop/interact/talk/profession/work/defend/stop. All types remain mandatory; This stage extends the implementation of the existing Move/Gather/Assign/Stop. |
| L343 | Key modifier sequence and clear results for each participant; Partial success is not hidden by general rejection. The allocation frame, Shift-add, saved groups, and context menu also remain in scope. |
| L344 | The server checks control, visibility, passworthiness and occupancy; The reservation doesn't duplicate the items. The manual command temporarily overrides the automatic operation; And then, after the end, the decay is back. |
| L345 | The choice of mouse itself doesn't block anyone. Temporary rights have a visible leader, a transfer, a group name check, and a dismissal after the timeout is disabled. The world is following a single, coordinated order. |
| L346 | Interacting with NPC is a separate game action, not a network chat of players; And it's the next thing to do. |
| L347–349 | Performance through a clear state automaton, reservation, timeout/replan and failure explanation; The algorithmic value is controlled, LAN is not dependent on the cloud model. |

The exit plan does not specify the capacity of the line 8/8192, the budget 1024/32, the length of the control lease or a separate single collection mode. These are the technical solutions proposed below, not the author's new requirements.

## The starting limit 0.9

[World](../include/ribterra/world.hpp) has atomic group Move/Gather, named results Assign/Stop, and conserved `ProfessionState`. Move under protected load still deflects. Deferred Assign/Stop maintains the new policy and applies it after delivery or the boundary of the external pending path; Further  [SKILLS.md](SKILLS.md).

[PathBatchState](../include/ribterra/world_state.hpp) already retains a common search, frontier, drafts and reconstruction stage. But `PathBatchOutcome` has one result for the whole batch, and `finish_failed` in [world.cpp](../src/sim/world.cpp) rejects it completely. This is the main dependence of the particle Move/Gather.

[GameServer](../src/server/game_server.cpp) already has session authorization, observer verification, nation1 ownership, dedupe, FIFO application and correlation of the external path batch with the request ID. There is no transfer of individual rights. `GameServer::load` clears the session and request/path correlation after the world has been successfully loaded.

Current versions are save format 7 in [world_save.cpp](../src/sim/world_save.cpp), protocol 6/rules 6/generator 2 in [foundation.hpp](../include/ribterra/foundation.hpp). They don't change by making this plan.

## The proposed contract of the line

Keep in SimCore `ManualOrderState`: stable `order_id`, `resident_id`, type of action occurring, goal, Gather execution mode, phase, cause of completion, and correlation with occurring path/Work/logistics performers. These correlations do not create a second owner of objects. The next row ID and fairness cursor are in export/import/checksum; The finished story has a separate limited capacity.

The proposed phases are: Queued, WaitingBoundary, PathPending, Running, Delivering, Complete, Failed, Cancelled. The remaining names and fields should be frozen together for core/save/network/UI before the parallel changes. `ProfessionState.manual_override` becomes a collaborative hand-crafted projection, not an independent second machine that can contradict rows.

The usual command uses Replace, a command with the modifier — Append. Replace first checks the right, purpose and possibility of a new order; Only then does the previous one go away. A refusal does not alter the order in force. The Append accepts or rejects each participant separately; The overload doesn't eliminate the orders already passed.

Move ends upon physical arrival, not after the route is set. For Gather, a clear mode is proposed: the current work to exhaustion» is maintained; One collection with delivery gives the final step to the line. UI should call this choice, not inadvertently change the old command. The line for Gather to exhaustion is normally waiting for the completion of this work; The limitation of the capacity is not a promise of short-term performance.

During a secure physical delivery Replace/Append is accepted as a safe limit expectation. The current load remains in the same stack as the task; The old gather continuation doesn't start over a new line. The stop clears the manual line, turns off the automatic operation and completes the secure delivery before the stop. The resource drain completes Gather normally; An invalid target or a failed route gives a typical failure. After a blank line, the professionally permitted automatic execution returns unless it is turned off Stop.

## Private routes and answers

Results and cancellations are required for individual participants of the shared path batch. Residents with a common goal use an existing search tree; An improper start or unavailability of one participant does not discourage the rest. Don't replace this 1500 with individual full searches. A globally invalid goal may dismiss everyone, but the answer still explains the result of everyone ID.

The correlation must undergo partial completion, cancellation of one actor, Search/Reconstruct/Stage and save/load. The release of draft/route reservation of one resident does not harm others. The order of participants, admission and final results are determined.

Confirm confirmation of acceptance of an order from its execution: the response contains a nominal result and a stable order ID; projection shows expectation, movement, delivery, completion or failure. Do not issue the artificial path batch ID for Deferred and do not call the accepted Move in-line already executed. The final evolution `CommandReply`/receipt should be agreed to by changing the wire schema. Dedupe repeats the same namesake results; The internal Work/Logistics does not complete the request of another player.

## Temporary law and session epoch

A separate server control lease is offered per resident: holder, session epoch, revision and a set release date after disconnection. The separation of the mouse does not create it. The first hand command accepted can be freely assigned; The other player's conflict team returns the cause and the authorized name of the leader. Assign/Stop are going through the same check. The transfer is only possible under the applicable law, to an eligible connected player, via a general server line.

The transmission itself does not change XP, the profession, the cargo or the orders received; A new manager can replace them. After disconnect grace the right is released, unauthorized steps of the old manager are cancelled, the active action completes the protected delivery and returns AI. It's a separate operation Release/Cancel, not a Stop that turns off AI. A short reconnect in the same epoch keeps the right and dedupe; An obsolete reconnect won't restore it.

Time lease requires a solution to be implemented: sim ticks are easy to replay, but the pause stops timeout; The monotonous server time is running on pause, but the expiry should be converted to a clear ordered event, not read the wall clock at SimCore. A server clock with Release event at the specified command application limit is suggested. The exact length of grace is not frozen and does not automatically equal the current reconnect timeout.

The secret tokens and old access rights are not kept in the world. The line is the game state, the lease — the live session authority. For a new epoch after `GameServer::load`, it is recommended to keep the game commands already accepted but not to revive their old commander: show unencrypted controls, allow a new authorized player to get the right and change the order. This keeps the extension of World save/load. The alternative — is the open release of all manual commands on the download — is simpler with respect to return AI, but changes the stored extension. The policy should be selected and verified separately from the usual disconnect; Do not make hidden cancellations inside the import. Durable session/request replay after crash remains a requirement G02.

## Boundaries and variants

For the first implementation, 8 orders per resident, 8192 active/expected world orders, 1024 visits and 32 admissions per tick are offered. The global limit can work earlier than the local one: the group gets fair partial answers. These are the initial design choices that should be measured on 10/100/500/1500/4×1500; They're not benchmarks or numbers on the outcome plan. The path planner's existing limits are kept until a separate measurement justifies their change.

- One active hand performer per resident; The automatic profession doesn't get stuck between the steps of the line.
- One authoritative stack of the subject; queue/control changes do not create second cargo, reserve or delivery credit.
- All IDs, enum, time limits, sizes, cross-references and ownership are verified before transactional import; The arithmetic tick/ID is not overlapping.
- Acceptance, execution, expiry, transfer and cancellation have clear results without re-effects after retry/reconnect.
- The projection allows you to understand the state, without transmitting hidden targets/objects, tokens, internal frontier planner or other private data.
- Budget visits are not declared as a budget of the entire work: individually measured indexing, path work, memory, group commands and tick time.

## Preparing genuine v7 fixtures and migration

Initial preparation is done: [25 real v7 fixtures](../tests/fixtures/WORLD_SAVE_V7.md) and frozen schema created by immutable writer `d3bc785`; Each file has been accurately retrieved and 64 ticks compared. Below are the rules for further capture and the very migration that has not yet been implemented.

Before any change DTO/schema copy the actual writer 0.9, headers/libs, generated schema header, `world_save.fbs`, version and gameplay hash. Set source commit and SHA256. Create fixtures by the old writer through real commands; The new serial, registered with the number v7, is not evidence of migration.

Required genuine conditions:

1. Neutral Unassigned and designated profession with independent XP/remainder/practice and non-zero scheduler cursor.
2. Hand Move in physical motion; group external Move/Gather in Search, Reconstruct and Stage with actual pending ID.
3. Manual Gather with cargo/delivery; its Waiting or PathPending Work continuation; A terminal failure without automatic regeneration.
4. Deferred Assign and Deferred Stop under load; partial change in the policy of one external participant pending group path.
5. Automatic professional work without manual override and a paid Work-Buddy with an employee on the road. These actions should not be manual after migration.

For each to maintain the old move stepwise control states or checksum sequence, XP, physical quantities and correlations. After changing the rules compare the retained semantics, rather than requiring the numerical equivalence of the old and new checksum, which includes different fields.

Migration creates manual commands only by confirming `manual_override` and route/path/Work/logistics links; It doesn't start the search again, it doesn't change the frontier, and it doesn's not re-paying for resources. **Do not define manual control only by `Job`**: and the action itself may belong to the profession or to the internal performer. Legacy correlation validation precedes conversion; The damaged state is turned away until the world is replaced. The absence of a recorded origin in a valid old save does not equate to damage. The old session rights are not synthesized.

After [separate correlation review 0.9](MANUAL_QUEUE_MIGRATION_UA.md) the following compatibility requirements have been adopted for implementation:

- Neutral performers in v7 continue to work with the previous semantics after the v6 is loaded, without a fabricated manual origin or a new control lease.
- Deferred Assign/Stop retains a previously recorded policy and an old physical limit. Old Gather is not becoming the new active order of the line.
- Already accepted group batches v7 are completed by the previous atomic rule and with the previous path budget. Partial results are applied to new commands.
- For a valid non-typical manual performer, a compatible connection with its existing Work/logistics/route is maintained, without a second owner of the items and without a fictional user command type. It needs to be completed and replaced separately.
- New ID migrants are assigned by ID resident. The unsigned author, request ID and time of publication remain unknown; They don't recover from current time or terminal history.

This is the solution for the next code; v7→v8 migration has not yet been implemented and tested.

The next set of versions is expected — save 8, protocol 7, rules 7; the generator 2 remains if the generation is not changed. It's a reservation of direction, not a promotion already done. It requires frozen v7 schema, `flatc --conform`, strict old hash, `.v7.bak`, support for all previous migrations, and atomic verification save/load.

## Four main reception scenarios

1. **Mixed group:** available, unavailable, foreign, incapacitated and carrying participants. The available routes are shared searched; The rest have the correct names for reasons/WaitingBoundary. The refusal of one does not annul the other.
2. **Round and physical boundary:** Move → one Gather with delivery → Move; Replace/Append/Stop between the road and cargo. Save/resume at each stage with the same new checksum at each tick; The unchanged XP and the objects in the cancellation, the correct return to the profession.
3. **Two clients:** simultaneous conflicting commands, transmission, observer/foreign rejection, short and delayed reconnect, repeat uncertain request, and new session epoch. One effect, visible control, the absence of old rights or hidden data.
4. **Limit and scale:** 10/100/500/1500/4×1500, saturated line, unavailable section of routes, increase/release of capacity and fairness. The p50/p95/p99, the command time, the actual visits/admissions, the path work, the memory and all the terminal results; The GPU/UI keystrokes, Shift rows, partial reasons and five languages.

Successful results in these scenarios will confirm the exact milestone described. Take/drop/interact/talk/defend, fully persisted groups and a context menu, tools/danger/panic, full Utility AI, durable sessions, and every other registry row remain part of the full engine target and require their own acceptance evidence.

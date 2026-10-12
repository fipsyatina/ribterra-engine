# Farm production: cutting offer 0.11

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Status: plan for the next stage of full movement; production has not yet been implemented.** The code 0.11, new schematics and fixtures are not created by this document. The migration decision below is made within the framework of technical planning; All DTO and numerical budgets need to be frozen before they can be implemented. The full purpose and all 41 requirements [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md), as well as the supplement V01–V03 remain open for own acceptance.

## Source and choice of next stage

The letters L represent the rows of unchanged copy [development-plan-ua.txt](source/development-plan-ua.txt).

| The Need | The reason for the cut |
|---|---|
| S03 Logistics | L264–266: food and supplies; L282: location, property, reservation and delivery; L305: work separately from delivery/simplification, queue, pause and continuation after save. |
| S04 People, S05 AI | L311,315: a profession does not wipe away skill; L243: distributed choice of work; L348–349: intent, reservation, arrival, work, delivery and explained failure. |
| S06 Skills | L315–340: useful practice of Farming, independent XP, familiarity and one common cap −15...+15%; UI receives the server ModifierBreakdown. |
| S07 Control, G01 Server | L342–345: address actions, manual priority, checking each resident, server rights to the unit. |
| P01/P02, U01/U02, T01 | L494–512: authoritative server and storage; L513–543: UI/words; L215–248: measured limits of simulation. |

The current `Profession::Farmer` receives `NoCompatibleWork` in [world_professions.inc](../src/sim/world_professions.inc), while the completed Farm without an employee creates 8 Food each 400 ticks in [world.cpp](../src/sim/world.cpp). The next cut replaces this simplified economy with farmer's work.

A full resource catalogue and a forging is required for L256–307, but now `Resources`/`ItemKind` only contain Wood/Stone/Food, and the extraction MineOre is given by Stone. Metallurgy requires individual minerals, alloys, recipes, fuels, stations, tools and technological thresholds. Routes/cash hierarchy over L244 and local unit variation over L372 also remain mandatory at individual stages. A stream-controlled benchmark of the hand line does not prove these possibilities. The farm was chosen as the smallest next extension of useful work, not a replacement for these requirements.

## The smallest score of the game

The player builds a Farm, adds a final order for one existing Food product, assigns a Farmer, and sees separately the worker's expectations, path, work, output, and delivery. After the actual arrival, one worker completes the cycle, earns Farming XP for the added work, and the result is delivered by the available logistics. Pauses, Stop, Move, replacement of the worker and save/load do not start the cycle again.

There are no new grains/seeds, soils, seasonal growths, or metal recipe in this cut. One recipe for growing abstract food does not require material input; This is a clear temporary rule of content, not a teleportation of reserved raw materials. Catalogue, material inputs, weather and agronomy remain further expansion S02/S03/S13. The initial output 8 Food and 400 standard working ticks per cycle can only be used as a test balance to compare to the old pace; Now it's an active work, not a global calendar trigger. The numbers should be in the verified data and gameplay hash.

Three station operations are required: add the final order, pause and cancel the selected order balance. Constant repetition, upgrades, and several recipes are not in the least. The Farmer designation itself does not create an order or issue Food/XP.

## Proposed state and limits of modules

The names below are project contracts for clarification before implementation, not yet new API.

- `FarmProductionState`: stable `building_id`, Worked mode, pause, phase, revision configuration, maintained current cycle work, single worker binding, link to current physical output/delivery. `ProductionOrderState`: monotone `order_id`, station, recipe, finished cycle residue and terminal reason. The queue is ordered by the server's acceptance; ID are not repeated.
- The phases are to distinguish `AwaitingOrders`, `WaitingWorker`, `Travelling`, `Working`, `OutputBlocked`, `WaitingDelivery`, and the pause. `paused=false` with a blank line means `AwaitingOrders`, not the beginning of the work. Absent, dead or employed workers are not productive progress.
- Add `Job::Farm` and `WorkIntentKind::FarmAssignment` after the old enum values. [world_work.inc](../src/sim/world_work.inc) provides bounded admission/retry and arrival; [world_professions.inc](../src/sim/world_professions.inc) indexes suitable stations and selects them by default. The equation is solved by priority, distance, and stable ID; worker/station reservation is mutually unambiguous.
- The recommended minimum priority — is to add Production sixth after unchanged Woodcut/Mine/Forage/Build/Haul. Migration retains five old meanings and clearly initiates a new one. Not replacing Forage with a new Farm: Shared Farming skills do not make these jobs the same.
- The new private production module has a cycle state. [world_logistics.inc](../src/sim/world_logistics.inc) remains the sole owner of physical stacks, reservations and transportation. Production refers to them, does not contain the second volume of Food itself. One output cycle has one start delivery and one confirmed result.
- The output buffer of one cycle is limited. Until the previous result is delivered, the next cycle does not begin. Full stack/stack limit/carrier absence does not erase the output and do not repeat its creation. The unsuccessful delivery shall retain the physical object and an obvious reason; The recovery does not produce new crops.

The work is measured in whole units with a clear pulmonary residue if it is needed for the modifier. XP/familiarity are calculated only for actual increase in labour due to the available `record_skill_work`; Arrivals, waiting, retrying, placing an order and delivery itself Farming XP doesn't give. One server `skill_modifier` is used by the work and UI; The speed of the station is not second to the skill cap.

Cancellation of the order removes its future cycles but does not zero out the work of the current cycle on the Farm. Unfinished work is stopped without a new order and may be continued under the next order of the same single recipe; previously published XP is not re-calculated. The output already created completes the physical delivery regardless of the cancelled residue. The final result of the order and the fact of delivery shall be separate statuses, without designating the undelivered product as delivered.

The hand line [world_manual.inc](../src/sim/world_manual.inc) maintains priority. Move temporarily fires the Farm employee; The return of automation is possible after the manual turn. Stop disables automation, Assign changes policy, Release keeps its choices. Already protected cargo reaches a safe limit. The replacement employee continues the overall work of the station, but charges new units only to the new actual performer.

## Server, rights and UI

The station command is addressed `building_id` and the expected revision configuration. The server checks the authorized player, his rights to the nation/building, Farm completion, allowed visibility, recipe, count, and capacity. The proposed typed rejection are: `UnknownStation`, `NotOwner`, `NotReady`, `UnknownRecipe`, `InvalidQuantity`, `QueueFull`, `StaleRevision`, `OrderUnavailable`. There should be rules in authority and in wire semantic validation. The text of the cause is not a protocol.

A separate player lease per station is not required: co-owners of the cooperative nation submit commands in the same server sequence, revision rejects outdated editing, request dedupe does not add a repeat cycle. The right to change the station line does not give the right to re-designate or stop a resident of another person's command. Assign/Stop/The manual orders of the employee are subject to the name lease check. Automatic selection uses an already accepted profession, `automation_enabled`, employment and `manual_override`; He doesn't create/transmit leases. The fact of retaining the lease after the end of the manual line does not exclude the automatic profession already permitted.

UI shows the line, the number of cycles, the pause, the worker phase, the actual progress, the ready food and the delivery/cause simple. `Applied` means accepting a configuration, not a finished crop. Snapshot/delta transmits only authorized stations and a ready-made Farming breakdown server, with no hidden targets and internal planner/task IDs. Configuration operations run on a break at the same server boundary; The work and delivery on the break are not progressing. keyboard/focus and GPU- checks EN/UK/RU/PL/ES are required.

Tutorial/Guide It's a must-see: the old "build a farm" step no longer means food security. Please provide explanations, add an order, mark the Farmer, ensure delivery, check the actual Food in the warehouse and see a notice for the empty Farm load. The trial rate 8/400 does not prove sufficient nutrition for the startup population: separate checks on food costs, initial stocks and availability of the first cycle for L252. This is a review of the relevant steps, not a statement of completion of the whole U03 TutorialDirector.

## Budget: projected values for measurement

Initial proposal: to 8 Orders on the farm, to 1024 global order records including from terminal history, to 100 cycles in one order, terminal tail to 128; One worker and one unfinished output per station. This is not the original document number. The overlay ID/count is deviated until the current line changes; history can be dismissed for a clear rule.

The manufacturer's scheduler has a persisted cursor and initial budget 128 station visits per tick. FarmAssignment uses already shared Work admission and path budgets, does not receive hidden additional 32 admissions or own 4096 path units. Physical delivery uses the current task/reservation limits. Measure the total tick, number of verified candidates, group commands, pending paths and peak save/network memory: visits do not automatically limit the cost of indexing or reconciliation. Repeated scans for each employee cannot be hidden from the task.

## Migration v8: a deliberate change in the economy

**There is no LegacyPassive and no convert-on-assignment.** The finished Farm from v8 goes to the same Worked-station as the new Farm: `AwaitingOrders`, empty/unset line, `paused=false`, production progress and credit equal zero, worker/output/order are not created. The designation that Profession Farmer already had is not a matter of order.

Unfinished paid Farm stores ID, owner, construction progress, delivery and reservations; After the actual completion, she's also waiting for orders. Existing Food/other batches, cargo, reservations, XP/practice, professions, manual queue, Work, routes and pending atomic/partial batches are not re-registered as farming. In particular, Food created by the old Farm before migration is not excreted and is not created again.

The passive rule `8 Food / 400 ticks` is removed. Therefore **the future economy of the old and new movements is deliberately not cohesive**, including the transition from 399 → 400. The absence of old-fashioned next crop can continue to alter hunger, health, employment, and behaviors. Do not promise the same old checksums throughout the extension; not to mask the expected discrepancies by the wide ignorance of fields. Initial projection checks the accuracy of all old fields; The individual oracle checks the deleted passive output, and new-load/new-resave compares each tick determined. For non-changing scenarios, the old control sequences remain useful.

The version guidelines for this change are save9, rules8, protocol8; Generator2 is stored if the generation is unchanged. The new enum append-only, recipe/balance is included in the content hash, the old payloads cannot clearly contain new fields even with default settings. The old condition of the goods shall be checked before conversion; New correlations to transactional replacement world. It requires a baseline schema, conform, `.v8.bak` and a strict legacy content gate, without changing old fixtures.

## Real v8 fixtures to change the circuit

Writer frozen locally at `.deps/v8-capture-5476c90`; [description of the copy and hash](evidence/manual-v8-writer-snapshot.json): source `5476c90676dc3152ef044e80fc0d12884d994f99`, engine0.10.0/save8/rules7/protocol7/generator2, content SHA-256 `5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606`. The manifest is stored in 24 files headers/schema/generated data/libs. The fields `fixtures_generated=false` and CI in this snapshot describe the historical copying moment. Later, unchanged writer created and tested [27 genuine v8 fixtures](../tests/fixtures/WORLD_SAVE_V8.md). This is the basic story 0.10, not proof of an unrealized migration 0.11; The uncovered cases are listed in the catalog.

Captures required through unchanged writer and regular commands, without `WorldState` injection:

1. Farm in Planned/Delivery/paid Work, then completed without an employee. Apart from Farm is ready before and after the real old trigger 400 ticks and Food on Ground cargo Storage.
2. Farmer from worksite in `NoCompatibleWork`; Stop and already accept a manual redefinition of this resident. Migration should not turn them into production orders.
3. New v8 manual line Queued/WaitingBoundary, physical Move, OneDelivery cargo, UntilExhausted, from Work Waiting/PathPending; Append-tail, Cancelled=8, Released and Stopped as different states.
4. Shared Manual Search/Reconstruct/partially applied Stage, renamed cancellation with preserved peers; separately the old atomic and neutral legacy executor, obtained by the actual loading of an unchanged previous fixture. The opaque manual executor captures only in the presence of a natural source. The current opaque persistence test changes the flag manual_override of the old carrier: it's a validator test, not genuine origin. If there's no natural source, obviously record the gap, not make such a capture.
5. Deferred Assign/Stop under cargo, reservations retained and a small skill credit. Limits and malformed-states are verified by individual negative tests; They're not called genuine captures.

For each capture save the initial bytes, metadata/hash, unread `path_results` and at least 64 the next bits of the old writer. For Farm boundary, it's the next old harvest, not just the name of the fixture. Re-encode and old-writer reload must match migration; The source files and the snapshot remain unchanged. Later the new writer is compared to the weathered migration oracle above. Leases/control epoch is not World-save: they are verified by a separate server command trace after load/reconnect.

## Acceptance of the Next Realization

1. New and migrated Farm without a contract/employee will not produce new Food; The old preserved food remains accurate. The transition of the old tick399 → the new tick400 does not cause a passive output. The unfinished farm is not making any progress.
2. The real Farmer physically arrives; until progress is zero. Two candidates do not occupy the same station. Death, manual Move or Stop release worker binding, and replacement continues left work.
3. Just one crop and one delivery receipt for the completed cycle. Competitive delivery, full stock/output, stack limit, repeat command, cancel/retry/save do not duplicate or lose Food. Without confirmed delivery UI does not show delivered result.
4. Farming XP/practice is changed only by the added work units, including with an unfinished long cycle. Pause/resume, replacement/cancellation of the order and replacement of the employee do not count the old units second. The modifier and the actual progress are matched within one cap.
5. Move/Append/Stop/Assign/Release under work and cargo retain the actual semantics 0.10. The cancelled production order does not recover after manual route or load; The protected physical delivery is completed without loss of items.
6. Save/resume of each phase, current-format malformed matrices and preservation of atomic legacy frontier/budgets. The new total checksum is matched between the new extensions; old/new economic divergence is limited and explained by an agreed change in rules.
7. Two clients: repeat and stale revision, foreign/unbuilt station, hidden target, worker lease conflict, pause/load/reconnect. The configuration is taken once, the rights of others are not inherited from the station, the permissible full/delta/removal projections do not reveal internal correlations.
8. GPU in five key-focused languages: queue, pause, traveling, working, blocked output, delivery, AwaitingOrders after migration. The updated training step tests the actual outcome. The benchmark 10/100/500/1500/4×1500 performs useful work and transportation; separately recorded fixture setup, command/tick costs, budgets and uncovered terrain/crowd/network/RAM/GPU limits.

The success of this cut will mean a basic farming and a single production chain. It doesn't complete the entire S03, the entire catalog of professions, the full Utility AI, the hierarchy/cash/collixion, health, the combat system or the rest of the movement.

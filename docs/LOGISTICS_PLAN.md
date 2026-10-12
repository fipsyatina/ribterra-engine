# Physical Logistics and Work 0.8: Contract and Remaining Requirements

> English translation of the historical document; snapshot-specific evidence and scope are preserved from the original wording imported in commit `7388a2f`.

The historical 0.8 contract integrates separate work continuation and builder replacement on top of 0.7 physical logistics. The 0.8 runtime, GPU, package, and CI checks passed: local and CI Windows 40/40, Linux 26/26, and UBSan 8/8. The preceding 0.7 release passed local 37/37 CTest, GPU checks in five languages, carrying save/resume, package smoke, and CI Windows 37/37, Linux 24/24, and UBSan 7/7. Its 4×1500 benchmark delivered materials but found 943 gather-continuation failures. That is a historical result, not a result from the new work scheduler. The full performance gate remains open. The 0.8 continuation contract is documented in [WORK_INTENTS.md](WORK_INTENTS.md); actual evidence is recorded in [VALIDATION.md](VALIDATION.md).

The current **0.9: protocol 6/rules 6/generator 2/save 7** adds professions, priority-based work selection, partial Assign/Stop, and independent skills. The rules below and the save 6/protocol 5 labels describe the 0.8 foundation. An accepted reassignment or Stop during a protected delivery is deferred until that delivery physically completes. The 0.9 contracts are [professions](PROFESSIONS_PLAN_UA.md), [SKILLS.md](SKILLS.md), and [SAVE_FORMAT.md](SAVE_FORMAT.md). Local/GPU/package checks passed, CI 0.9 for `d3bc785` succeeded, and the repeated CI run `42036ff` passed without C++ warnings or UBSan errors; evidence is in VALIDATION. The full logistics, AI, and manual-command catalogs are still required.

The original document requires a physical resource location, owner, available and reserved quantities, delivery that accounts for distance, capacity, priority, and danger, and construction that consumes only delivered material ([source, lines 249–287](source/development-plan-ua.txt)). The complete scope remains in [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md).

## Single source of truth for quantities

`ItemStackState` contains a monotonic `stack_id`, `ItemKind` (Wood, Stone, Food), quantity, owner_nation, and one location:

- Ground — a specific cell, entity_id = 0.
- Storage — the ID of a completed Camp or Stockpile.
- Carried — a resident ID, one item kind, and no more than 10 units.
- Construction — the ID of a site in the delivery phase.

For a location identified by entity ID, the cell has the canonical zero value; coordinates are resolved through the entity. There is no separate authoritative CarryState: a resident's cargo is derived from stacks and tasks.

`InventorySummary` aggregates one nation. total covers all of its unconsumed items; available covers unreserved items on the ground and in storage; reserved includes source reservations and all cargo in hands or at construction sites. For each item kind, total = available + reserved. stored and carried are additional views of those same items and are not added to total. The current `World::resources()` is a derived total for nation 1.

Each ItemReservationState belongs to one task and reserves up to 10 units from a source stack. Pickup atomically decreases the source, creates a carried stack, and removes the reservation. Dropoff transfers cargo to storage or a construction site. Compatible stacks in storage merge without changing the total quantity.

## Work cycle

One Forest unit yields 3 Wood, Rock/MineOre yields 2 Stone, and forage yields 2 Food. A completed work step immediately creates cargo and transport to storage. After dropoff, the delivery remains Complete while a separate GatherContinuation waits for admission of the return route to the resource. Temporary queue saturation causes a bounded retry. An expected resource that has been depleted ends the work as Exhausted; an incompatible target or final path error produces WorkIntent Failed. This does not change the successful delivery and does not create duplicate yield or XP. If no storage accepts the initial yield, it remains on the ground with StorageFull. If the item-count or total-quantity limit is exhausted, the source tile and XP remain unchanged.

An unreserved ground stack can receive its own reservation and HaulToStorage task. Storage capacity and its filter are rechecked at dropoff. On rejection, the cargo remains on the ground and the task reports StorageFull or TargetInvalid.

Build atomically creates a Planned order and reserves the full cost through DeliverConstruction tasks. Insufficient material or queue capacity leaves no partial order. An external Build progresses through Queued → Applied/Rejected: Applied means that the validated site entered Delivery, not that the building completed. A new external Build path batch does not assign workers; internal tasks route carriers to the source and destination.

Material leaves storage only on pickup. Building.progress remains 0 until delivered == required. Site stacks are then consumed exactly once, the order enters Work, and the last carrier begins construction. If the worker becomes unavailable or receives another order, progress remains and BuildAssignment selects an available resident of the same nation. Work resumes after physical arrival without charging again. Completion moves to Complete; a Stockpile receives storage. The delivery planner uses Idle residents and builders waiting specifically in Delivery. It does not take current Work builders or owners of active work intents.

Source/storage selection uses Manhattan distance, adds an estimate of +64 for the other layer, then considers storage priority and stable ID. This is a distance estimate, not the cost of a computed route. Available carriers are assigned by stable ID separately for each nation; availability is computed in one pass per tick.

## Failures, cancellation, and balance

CancelConstruction releases reservations and removes the unfinished site. Delivered or carried items remain on the ground. In the Work phase, the consumed cost is returned as ground stacks; every quantity, slot, and ID limit is checked before mutation. Repeated cancellation is rejected. Cancelling Planned also completes the original external Build as Rejected.

Death or a path error releases an uncollected reservation and places a carried stack in the current cell. A delivery failure cancels its entire construction order. Released ground cargo can receive an independent haul; nonzero reservations after the cancellation tick do not necessarily belong to the cancelled construction. A cancelled, unfinished Logistics planner leg remains PathPending until its callback, so an external command cannot overwrite its correlation. Work pending follows a separate rule: an accepted manual order atomically cancels that specific batch, while rejected admission retains the previous intent and route. Assigning a new physical transport task also clears stale Exhausted/Failed status.

| Parameter | Current value |
| --- | --- |
| Carrier capacity | 10 units of one item kind |
| Camp / Stockpile | 10000 / 5000 total units |
| Stacks / tasks / reservations | 65536 / 4096 / 8192 per world |
| Scheduled logistics transitions | Up to 128 per tick; atomic cancellation is separate |
| Task deadline | 10000 ticks |
| Completed history | Up to 256 records without an unfinished planner leg |
| Planner | 4096 work units/tick, deadline 1200 ticks, up to 3 topology restarts |
| Total quantity of an item kind for one nation | No more than INT_MAX |

These limits do not prove tick duration: scans over items, workers, and storage, plus atomic cleanup, must be measured. `SimRunner --benchmark-logistics` models productive gathering by all 10/100/500/1500 and 4×1500 residents and the return of cargo through a portal. It reports delivery, the final phase of every task ID, individual failure reasons, pending paths, peak active transports, living residents, and tick percentiles. Scenario completion also waits for every internal route. This fixture does not model crowd collision.

A Farm creates 8 Food on the ground every 400 ticks; ordinary logistics collects it. Upkeep consumes only unreserved Food from the nation's own storage every 200 ticks. There is not yet personal food delivery to residents.

## Persistence, networking, and evidence

Save format 6 stores every counter, stack, storage, reservation, task, construction order, planner, work intent, and scheduler cursor; every field participates in the checksum. Before replacing the world, import validates IDs, enums, ownership, quantities, capacity, a single carrier, phase/correlation, reservation completeness, and conservation of site materials. Carrier coordinates must round to a cell inside the world so death cannot create an item outside the grid. The v5 migration with `.v5.bak` transfers a pending continuation from a Complete LogisticsTask into a WorkIntent with the same path batch and search state; old terminal failures are not restored. Genuine v5 carry/continuation/Work fixtures are recorded; current checks belong to [SAVE_FORMAT.md](SAVE_FORMAT.md) and [VALIDATION.md](VALIDATION.md).

The v4 migration converts old resource totals into stacks in the completed Camp of nation 1. For an old oversized stock, Camp capacity expands to the required amount. Already paid, unfinished buildings receive Work orders with required == delivered and are not charged again. A pending Build preserves its ID, cell, and path correlation, and receives physical reservations in FIFO order. v3/v2/text pass through the existing migrations before this step; originals are retained with their corresponding backup suffixes. An unrepresentable state is rejected without replacing the world.

Protocol 5 / rules 5 / generator 2 transmits permitted owner aggregates, cargo/phase/typed failure for the owner's residents, and required/delivered/work for the owner's explored sites. Owner-only ResidentWorkView adds kind/phase/failure/completion/attempts without quantities or internal intent IDs; a site adds worker phase/failure/count. Full/delta/removal and multipart processing remain atomic and bounded. This slice does not transmit the complete list of stack coordinates over the network. Internal Logistics and Work path results do not become external CommandReply records. The server validates CancelConstruction ownership; repeated requests use the existing idempotency behavior.

Code regressions cover competition for the last material, portal traversal, prohibition of Work before complete delivery, exact resume, cancel/death/full storage, quantity and ID overflow, storage-filter changes, the coordinate boundary, orphan reservations, and visibility of gather-continuation failures under a full queue. The presence of these tests does not replace their execution result for the current revision.

## Full plan: remaining work

Persisted work-continuation waiting and replacement of an unavailable builder passed local 0.8 acceptance: 40/40 CTest, GPU, benchmark, and package smoke. CI source commit `f62cea0` passed Windows 40/40, Linux 26/26, and UBSan 8/8. Limits, phase/failure/completion, retry/backoff, manual replacement rules, workforce behavior, and migration are documented in [WORK_INTENTS.md](WORK_INTENTS.md). A separate 0.9 selector adds professions, priorities, and selection of a new compatible resource; that does not complete the full Utility AI.

Completed local 0.8 scenarios (see VALIDATION) include a full queue with save/load during backoff; exhaustion before retry and during search; death/reassignment of the final builder; genuine v5 migration; owner-only full/delta/removal/reconnect; localized GPU and package smoke; and a scale of 6000 with all new intents completed, separate retry/failure metrics, and verification that yield, XP, and payment were not duplicated. Results and limits are preserved in VALIDATION; they do not close the full-engine scope.

A complete material catalog, recipes, weight/volume, mixed containers, spoilage, tools, fuel, technology thresholds, repair, and upgrades require later stages. Roads, real danger effects, cost-based routing, complex ownership rights, caravans, storage-filter and priority UI, broader AI for builder assignment/replacement, and a pinned HUD with consumption forecasts also remain. Separate measurements are required for RAM during save/load, VRAM, network deltas, and full AI/crowd behavior at the specified scales. This vertical slice does not close those requirements.

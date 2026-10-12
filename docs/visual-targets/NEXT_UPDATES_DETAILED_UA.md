# Ribterra — detailed update roadmap and target frames

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Current base-graphics sequence:** [V00–V05 — old view → UI → ground → objects → rain → winter](GRAPHICS_PROGRESS_UA.md). This is the main sequence of visible changes after Dynamic was deferred. The older dev/GW/DL entries are technical packages, not finished screenshots for every version. [Progress gallery](index.html); [older 13 public targets](public-versions.html).

**Latest decision, 27.09:** Full dynamic lighting and its toggle are deferred. The code currently contains only safe groundwork. [Future plan DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) takes precedence: Dynamic does not block the base world/UI update; its light/occlusion gates apply only to the future enabled mode.

Updated 27 September 2026 after the renderer capability audit. Status: **plan; versions 0.26.1–0.27.0 are not implemented**.

This document combines the current [small-release roadmap](../ALPHA_DEMO_SMALL_RELEASES_UA.md) with a series of 11 generated target frames, `S01…S11`. The frames serve as art and composition specifications for the agent after each public update. They do not prove that the game already contains the depicted feature.

The current technical package for 0.26.1 comprises the [V2 “Living World” plan](GRAPHICS_ENGINE_PLAN_V2_UA.md), [render core](RENDER_CORE_SPEC_UA.md), and [world FX](WORLD_FX_SPEC_UA.md). The canonical V2 table is the sole source of the internal development sequence; this document does not duplicate its counts. Implemented versions 0.25.x and 0.26.0, their numbers, and their verification reports remain unchanged. The first internal alpha is 0.26.7. Version 0.27.0 remains a target marker for the external demo, not a date.

## How to Use Targeted Frames

Each file must contain a visible **“TARGET MOCKUP”** label. If a frame requires numbers that the simulation has not produced, the image caption or its sidecar/manifest must contain **“ILLUSTRATIVE DATA”**; the image generator is not required to reproduce this second label reliably inside the bitmap. The image must not be included in a verification report as a gameplay screenshot or compared with a real build unless seed, camera, zoom, resolution, time, and season are identical.

The text contract for each card takes precedence over minor variations in layout, labels, and decoration produced by the image generator. If a PNG conflicts with the described UI state, behavior, or scope boundary, the PNG is corrected or rejected; the text is not retroactively rewritten to match a random generation result.

A common visual contract of all scenes:

- Ribterra's isometric medieval world, with a camera and scale suitable for actual play and no cinematic perspective;
- compact dark-blue panels with brass borders at the edges; the center of the world remains open;
- warm window and fire light, with readable ground, shorelines, entrances, cargo, and work activity;
- no manually built Road in the world or catalog; decorative ground near entrances provides no bonus;
- the Bridge remains; the available set after 0.26.1 is the Camp plus 11 buildable types;
- not to show new mines, castle, army, monsters, ice, temperature mortality, heating, fuel for heating or warming;
- the storage card does not mix settlement-wide, available, reserved, carried, and storage-owned stock;
- a generated image specifies visual appearance and UI state. Only a build, tests, and unedited runtime recording prove functionality.

### Types of Evidence

| The card | What Does It Prove | What is not proved |
|---|---|---|
| **V — visual** | Composition, readability, style, lack of overlays in the recorded scene | Server rules, save/load, clicks, performance |
| **F — functional** | Tests, runtime scenarios, logs and snapshots confirm the behavior and data | Work on another PC or via an external network |
| **P — physical** | Launch and scenario on two physical Windows PCs and, where specified, across different networks | General quality for all configurations |

## Frame map

| ID | Version | The file | Main content |
|---|---|---|---|
| S01 | 0.26.1 | [target-0261.png](images/target-0261.png) | The living world, the weather, the temperature, the concept HUD |
| S02 | 0.26.2 | [target-0262.png](images/target-0262.png) | Housing and safe resident admission |
| S03 | 0.26.3 | [target-0263.png](images/target-0263.png) | Two mutually exclusive farm modes |
| S04 | 0.26.4 | [target-0264.png](images/target-0264.png) | A stable diet and a normal daily cycle |
| S05 | 0.26.5 | [target-0265.png](images/target-0265.png) | The useful chain of mills and kitchens |
| S06 | 0.26.6 | [target-0266.png](images/target-0266.png) | Understanding logistics, storage, bridges and mining |
| S07 | 0.26.7 | [target-0267.png](images/target-0267.png) | Self-guided onboarding and the first internal alpha |
| S08 | 0.26.8 | [target-0268.png](images/target-0268.png) | Autosave and recovery |
| S09 | 0.26.9 | [target-0269.png](images/target-0269.png) | Resilient two-player co-op |
| S10 | 0.26.10 | [target-02610.png](images/target-02610.png) | Verified Windows candidate |
| S11 | 0.27.0 | [target-0270.png](images/target-0270.png) | First external demo without new mechanics |

## S01 — 0.26.1 “Living World”

**The result for the player.** The new game begins in a readable valley with a river, slopes, forest and available resources. Weather, temperature, snow and forecast are the server state. Snow weather is changing the pace of collection and construction on the Surface under one clear rule. The world and HUD are approaching the accepted concepts by composition, and the 2.5D renderer first demonstrates the required light, shade, water, and atmosphere in a small native cut.

**Added/removed/improved.** Adds a versioned generation profile, weather calendar, today plus the next three forecast days, temperature, precipitation, wind, storms, snow accumulation/melting, compact HUD, accurate storage card, and minimap. The base graphics branch follows V01–V05; a small kit is verified in the renderer before mass export. Full light/occlusion lighting and the UI toggle move to future DL-01–08. The manual Road, its server build command, and its movement bonus are removed; `BuildingKind::Road` remains a reserved ID.

| The effect V2 | Asset/data contract | Runtime cost contract | Native verification |
|---|---|---|---|
| Future DL: projected light + emission | emissive/mask atlas, projection anchor, color/radius, time of day | bounded visible-source quads/pass and overdraw cap | paired day/night video, clipping, UI isolation |
| Contact/directional shadows | shadow mask/shape, ground anchor, height class, light direction | bounded draw/overdraw per visible object | residents/buildings/trees on slopes, overlap and depth sorting |
| Wetness | wet masks/variants, WeatherState, drying transition | visible-material layer only, measured GPU/frame cost | rain→wet→dry, pause/save/load/reconnect, no gameplay topology change |
| Real river render + bounded reflection | normal/foam/reflection masks, bank clip, presentation flow | dedicated bounded water pass or low-res/sprite projection; visible water only | bridge/banks/camera; no foam/reflection over land or UI; fail-safe off |
| Haze distinct from FoW | depth bands/color LUT, visibility mask | 3–4 bounded world-space bands, no volumetric solver | unexplored world stays hidden; no grey fullscreen rectangle |
| Actual rain/snow | particle atlases, spawn/wind data, authoritative period/tick | resolution/quality particle caps; no particle serialization | transition, pause, late join, save/load, reconnect, host/client video |
| Footprints/compressed snow | decal atlas, movement cadence, bounded history/lifetime | bounded decals/memory/batches with expiry | cosmetic persistence only; no elevation, collision, pathing or buildability change |

**Render boundary.** The plan selects a controlled 2.5D architecture with separate bounded passes and fail-safe effect disabling. A move to another engine is allowed only after native evidence establishes a specific insurmountable limit, not automatically because the concept is ambitious. No rigid-body, cloth, or fluid physics, physical snow deformation/collision, or waterfall simulation are added.

**Exact frame state.** A winter valley during the first-storm warning: compact real resources at the top left; calendar and four-day forecast at the top center; a separate current temperature; one actionable warning at the top right. The selected Warehouse at the bottom left shows its own actual stock/capacity and a button to open details. Contextual actions sit at the bottom center; the right side contains a minimap with a camera frame. Do not show “fuel for N days” or “insulation %.”

**Contract of behavior and data.** The host determines the weather; save, full snapshot, delta and reconnect restore the same sequence. The temperature is not generated by the renderer itself. The surface modifier is only applicable to certain assembly/construction operations; It doesn't add hunger, damage, death or water freezing. `FirstStorm` has a separate Food buffer for at least 20 minutes; It's a storm preparedness, not evidence of a stable economy 0.26.4. The mini-map shows only the explored. The old world is not being regenerated by a new silent algorithm.

**Sequence and files.** **Verified in 0.26.0:** the main HUD uses RmlUi; the old minimap lives in the legacy HUD; the starting clearing is forced flat; height is not a movement rule; no weather snapshot exists; Road is still allowed; water highlights, ground mist, smoke/night glow, winter variants, and simple ellipse shadows are approximations rather than V2 systems. **Proposed:** follow only the canonical development table in the V2 plan: baseline/contracts → native renderer proof → measured stop/go → mass art → world/weather/UI/network/package integration. Existing change locations: `include/ribterra/world_grid.hpp`, `include/ribterra/biomes.hpp`, `include/ribterra/seasons.hpp`, `include/ribterra/world.hpp`, `include/ribterra/game_server.hpp`, `include/ribterra/foundation.hpp`; `schemas/world_save.fbs`, `schemas/network.fbs`; `src/sim/world.cpp`, `src/sim/world_save.cpp`, `src/server/game_server.cpp`, `src/net/protocol.cpp`; `include/ribterra/terrain_surface.hpp`, `include/ribterra/terrain_appearance.hpp`, `src/client/view.cpp`, `src/client/game_art.cpp`, renderer/shader/material paths; `src/ui/game_ui.cpp`; `assets/ui`, `assets/art`, `assets/locales`.

New files, if decomposition requires them, are **proposed** to be created and should be added separately to master/manifest; This document does not give an inexistent path to verification.

**Dependencies and negative cases.** Depends on 0.26.0 and the fixed compatibility policy. Check chunk seams, unreachable starting resources, a building across an elevation change, passage through a painted rock, preview/server divergence, hidden entities on the minimap, a weather jump after load/reconnect, a repeated bonus from the old Road, long localized strings, and dragging across a panel boundary. Also check reflection/foam on land, haze as a fullscreen rectangle, precipitation inside FoW, shadows without a ground anchor, footprint memory leaks, and any effect of cosmetic snow on collision/pathing/building placement.

**Proof and gate.** V: matching summer/winter and day/night frames at 720p, 1080p, and in the V2-profile 1440p scene, plus unedited native video for every row of the effects table. Before mass art, frame-time/GPU/CPU/memory measurements, caps, fallbacks, and native failure cases are mandatory; the software probe and generated S01 do not prove them. F: generator/path/build/weather/save/network/UI tests, a 10-seed matrix, `FirstStorm`, full configured CTest, and package launch. P: synchronized weather state on two physical PCs in the final scenario. Ready only after the final gate in the canonical V2 table; S01 alone closes nothing.

**Scene brief for generation.** Isometric winter valley with 5–7 buildings, 8–12 residents, a flat river and bridge, clipped shoreline foam, bounded reflections, wet stone, warm projected window light/emission, contact/directional shadows, world-space haze, actual snowfall, and cosmetic footprints in visually compressed snow. The concept HUD occupies only the edges. The selected Warehouse, minimap, and forecast are visible. No Road, castle, army, ice, fuel or insulation UI, physical snow deformation, or fluid physics. Label: “S01 · TARGET MOCKUP.”

## S02 — 0.26.2 “Housing and Recruitment”

**The result for the player.** A new person can only be accepted if there is a real free space. The house and the House II have a clear practical role.

**Added/removed/improved.** An authoritative account of occupied, reserved and common housing, the state of expected arrival and specific reasons for refusal are added. There is no admission fee, double reservation of last place and payment of refused application. The choice between the new House and House II is improving.

**Exact frame state.** Clear summer day after 0.26.1. House II is selected; the compact card shows the level, occupied/free spaces, and one pending arrival, while the details reveal the sources of capacity. The settlement panel offers “Admit resident” only in an allowed state. The frame contains no families, births, furniture, heating, or insulation. The general caption/manifest rule “ILLUSTRATIVE DATA” applies to every number.

**Behavior and data contract.** The server atomically checks and reserves a place before charging the cost. A living adult, a young person, and a pending arrival each occupy one place; becoming an adult does not count a person twice; a dead person occupies no place. The reservation survives save/load and appears identically for both clients.

**Sequence and files.** **Verified:** House II already increases capacity and has UI in 0.26.0; recruitment has not yet been proven as a housing gate. **Proposed:** lock the formula → add a pending reservation → make the server command atomic → serialize and project the snapshot → connect RmlUi and localization → test races. Locations: `include/ribterra/settlement.hpp`, `include/ribterra/world.hpp`, `include/ribterra/game_server.hpp`; `src/sim/world.cpp`, `src/sim/world_settlement.inc`; `src/server/game_server.cpp`; `src/ui/game_ui_settlement.inc`; protocol/save schemas; settlement/network/UI tests.

**Dependencies and negative cases.** Version 0.26.1 is accepted; House II from 0.26.0 remains intact. Refusal at full capacity must not deduct Food; two simultaneous requests must not create two people; death, youth→adult, cancellation, saving while waiting, and reconnecting must not distort the count.

**Proof and gate.** V: house card, pending state, disabled/reason state at 720p/1080p. F: unit/save/network all transition scenarios and two local clients. P: is not a separate gate of the inner 0.26.2; It's repeated on the physical PC in 0.26.9. It's ready when House II really opens the door, and the last place can't be used twice.

**Scene brief.** A cozy summer settlement, House II highlighted with a blue outline, a compact housing card at the bottom left, and one clear recruitment action; several residents stand near the house. No babies, family system, heating smoke, or roads. “S02 · TARGET MOCKUP.”

## S03 — 0.26.3 “Farm Modes”

**Result for the player.** A farm either provides basic ready-to-eat Food through permanent farmers or produces raw materials on demand. The two paths do not run silently at the same time.

**Added/removed/improved.** Adds an explicit mode selector, visible output, workers, and safe switching status. Removes duplicate output, the unnoticed assignment of one worker to both paths, and silent profession removal. Improves explanations of finished Food and raw materials.

**Exact frame state.** An autumn or summer farm with visible workers. The farm is selected; the compact card names the active mode, while details compare “Basic Food” and “Raw materials for the kitchen,” show current output, and show the safe “finishing delivery” state before switching. No new cook profession or farming engine.

**Behavior and data contract.** The mode belongs to a specific farm and is authoritative on the server. A transition completes or returns cargo under the current 0.25.9 contract; it does not convert existing raw material into Food. Automatic selection takes a suitable free adult without a permanent assignment; an assigned farmer is released explicitly.

**Sequence and files.** **Verified:** a permanent-farmer path and a recipe path exist; 0.25.9 provides safe worker control. **Proposed:** formalize the mode enum/state → server transitions → save/network → UI/details → cancellation/delivery tests. Locations: `src/sim/world.cpp`, `src/sim/world_economy_api.inc`, `src/sim/world_economy.inc`; `include/ribterra/settlement.hpp`, `include/ribterra/economy.hpp`; `src/ui/game_ui_buildings.inc`; `src/client/client_production_group.inc`; economy/settlement/network tests.

**Dependencies and negative cases.** Depends on 0.25.9 and housing in 0.26.2. Check switching before work, during work, during carrying, after output is ready, after save/load, and under simultaneous client commands. No person remains assigned without work.

**Proof and gate.** V: both mode states and transition state. F: Each mode only yields its own result, objects and employees are not duplicated, both clients see the same state. P: deferred to the cooperative transit 0.26.9. Gate — controlled movement without loss of cargo or occupation.

**Scene brief.** A farm with fields and two workers; the lower card shows the active mode and two large mutually exclusive options, with one disabled during delivery. Visible baskets/cargo match the action. No Road and no additional crops absent from the game. “S03 · TARGET MOCKUP.”

## S04 — 0.26.4 “Sustainable Basic Food”

**Result for the player.** The starting 10 people have time to understand the controls, and an operating basic farm supports them throughout the control cycle without emergency manual gathering.

**Added/removed/improved.** Adds an honest explanation of available Food, consumption rate, or approximate reserve time. Removes the opening race, balance that only consumes the starting stock, and excessive overnight waiting. Improves only fixed numbers: starting stock, consumption, output, calendar, and recruitment.

**Exact frame state.** Evening→night transition; the basic farm is operating and a carrier is delivering Food to the Warehouse. The HUD shows available Food separately from cargo, estimated reserve time with an explanation of the method, and day/night progress. Weather fields from 0.26.1 remain, but the frame does not imply that storms add a new food penalty.

**Behavior and data contract.** Cargo carried by a worker does not count as available Food. Target: a starting buffer of at least 5 minutes without incoming supplies; a farm with two adults supports 10 people for two full day/night cycles; in the second cycle, delivery is at least equal to consumption, stock is no lower than at the start of the cycle, and hunger/HP have not worsened. This is separate from the 20-minute `FirstStorm` buffer in 0.26.1. A population of 16–20 requires expanded food production.

**Sequence and files.** **Verified:** the current calendar and consumption exist; previous numbers were inputs, not an accepted balance. **Proposed:** delivered/consumed telemetry → two control seeds → tune one parameter at a time → repeat weather/night → UI estimate → regression. Locations: `include/ribterra/seasons.hpp`, `include/ribterra/economy.hpp`, `include/ribterra/settlement.hpp`; `src/sim/world.cpp`; `src/ui/game_ui.cpp`; integration balance fixtures (**proposed**).

**Dependencies and negative cases.** Depends on farm modes in 0.26.3 and weather rules in 0.26.1. Check a distant farm, night, a carrier with cargo, save/load in the middle of a cycle, two maps, and the absence of hidden ready-made resources. Do not merely increase timers to create artificial duration.

**Proof and gate.** V: food status is understandable without a journal. F: journal of two complete cycles with separate delivered/consumed/available/hunger/HP and repeat after changing calendar. P: not required for the internal gate; The balance is repeated in the final physical session. Gate — Both seeds pass the criteria without manual food gathering.

**Scene brief.** An evening settlement, with farm and Warehouse on one readable route, a carrier with a basket, and a compact HUD showing Food and day/night progress. Warm windows, no famine, and no false exact numbers. “S04 · TARGET MOCKUP.”

## S05 — 0.26.5 “Mill and Kitchen with Proven Value”

**Result for the player.** Investment in a mill/kitchen has a measurable benefit for a growing settlement but is not a mandatory barrier in the opening minutes.

**Added/removed/improved.** Added a clear raw material route→processing→ready Food and a full account of labor, wood and delivery. The impression is that the raw grain/fish is already ready Food or that the output without cost is equal to profit. The available lengths and outputs after measurement are improved.

**Exact frame state.** The Kitchen is selected; the compact card shows the active batch, worker, and state, while details show real inputs, output, remaining order, and the reason for idling. The world shows a farm, mill, Kitchen, Warehouse, and physical delivery. Do not show new products, quality, vitamins, or spoilage.

**Behavior and data contract.** Comparing the basic and complex routes uses the same seed, people, skills, stock, location, and worker budget. The initial target for the steady cycle is at least +20% delivered finished Food; separately, a full startup must catch up with the basic route in a controlled 30–60-minute session. These numbers are tuning criteria, not proof of readiness before measurement.

**Sequence and files.** **Verified:** mill/Kitchen recipes, physical inputs/outputs, and production orders exist. **Proposed:** select one reference route → measure the baseline → include all labor/wood/delivery → tune → check the remaining retained recipes → UI and regression. Locations: `include/ribterra/economy.hpp`; `src/sim/world_economy.inc`; `src/ui/game_ui_economy.inc`, `src/ui/game_ui_buildings.inc`; `tests/economy_tests.cpp`; balance fixtures (**proposed**).

**Dependencies and negative cases.** Depends on sustainable basic food in 0.26.4. Check a shortage of firewood, missing input, a full Warehouse, cancellation during delivery, save/load, and resumed supply. Do not provide free finished inputs or hide construction labor.

**Proof and gate.** V: chain and cause are read in plain language in the map. F: pair logs baseline/complex with full work and materials; The item conservation and save/load. P: the result is repeated in 0.26.9, but does not block a separate internal gate. It's ready after you pass the default and startup criteria, not after the button pops up.

**Scene brief.** A dense summer production yard: farm, mill, Kitchen, Warehouse; residents carry real sacks/baskets. The Kitchen is selected and the production card is expanded, leaving the center unobstructed. “S05 · TARGET MOCKUP.”

## S06 — 0.26.6 Logistics and expansion»

**Result for the player.** The player can see exactly where goods are, why a building is waiting, and why a nearby Warehouse, Bridge, and Mine are useful.

**Added/removed/improved.** Clear levels are added: total settlement stock, own stock of selected storage, reserved, carried/in-transit and available. The order shall show the delivery point when it has already been determined by the simulation. A general stockpile is removed with the name of the specific composition, promises of mines/expeditions and any return of the manual Road. The reasons unreachable and StorageFull are improving.

**Exact frame state.** A Warehouse near production is selected; its card shows its own fill level, while details show the stock breakdown. In the world, a carrier crosses the Bridge with cargo; the Mine appears as the existing transition to supported underground stone. The minimap shows the explored route. No Road button.

**Behavior and data contract.** Per-storage data comes from an authoritative projection, not from global inventory. `available != total != stored != carried != reserved`. A nearby Warehouse shortens physical delivery without a new route planner. The Bridge opens a supported passage across water; the Mine does not promise metal.

**Sequence and files.** **Verified:** physical goods, StorageState, aggregate InventorySummary, the Bridge, and the Mine portal exist; per-storage projection is planned in the canonical 0.26.1 data/UI slice. **Proposed:** establish names for the quantities → confirm the snapshot → connect destination/reasons → compare near/distant storage → bridge/mine/save tests → UI polish. Locations: `include/ribterra/world.hpp`, `include/ribterra/game_server.hpp`; `src/sim/world.cpp`, `src/sim/world_economy.inc`; `src/ui/game_ui_buildings.inc`; `schemas/network.fbs`, `schemas/world_save.fbs`; logistics tests.

**Dependency and negative cases.** Depends on 0.26.1 per-storage data and world height contract, as well as food chain 0.26.5. Check carried item during save, two warehouses, rejected resource mask, full warehouse, destroyed/unbuilt bridge, unavailable resource and change in visible layer.

**Proof and gate.** V: One item can be traced from source to selected item. F: item conservation, near/far comparison, bridge/mine path, save/load, and two clients. P: the full route is repeated in 0.26.9. Gate — UI never calls the aggregate a building stock, and the main route has no dead end.

**Scene brief.** River, Bridge, Warehouse beside a workshop, and a Mine on a rocky slope; one carrier has a visible load. An expanded storage card shows separate categories, with no invented numbers and no Road. “S06 · TARGET MOCKUP.”

## S07 — 0.26.7 “Onboarding and the First Internal Alpha”

**The result for the player.** A person without knowledge of the code starts the main cycle on his own: collection → construction → worker → delivery → food → housing → new dwelling → useful Level II → save/load.

**Added/removed/improved.** Adds short contextual steps completed by world events and the option to hide help. Removes instructions for deleted actions, an incorrect winter description, and untranslated lines on the main path. Improves the first session without requiring a long encyclopedia.

**Exact frame state.** Start of an independent playthrough: a small unobtrusive tutorial card indicates one next action, an object in the world has a soft highlight, and the HUD remains fully functional. The panel shows progress through the cycle but no false “quest complete” message. The frame includes the new world and 0.26.1 weather HUD.

**Behavior and data contract.** A step completes on a confirmed server/world event, not on a button click. After load/reconnect, the tutorial restores the correct step. Skip does not change the simulation. Steps do not require Road, Expeditions, Research, or hidden buildings.

**Sequence and files.** **Verified:** the required commands and inspectors exist incrementally through 0.26.6; the old tutorial is not an active RmlUi path. **Proposed:** event checklist → minimal text for every locale → RmlUi component → persistence → two-seed scripted check → two independent playtests → fixes for specific blockers. Locations: `src/client/view.cpp`, `src/client/main.cpp`; `src/ui/game_ui.cpp` and inspectors; `assets/ui`, `assets/locales`; tutorial state/save and UI scenarios (**proposed**).

**Dependencies and negative cases.** All 0.26.0–0.26.6 outcomes are accepted. Check a step completed by another player, a repeated event, skip, load at every boundary, an unavailable resource, bad weather, long locale strings, 720p, and the absence of voice guidance.

**Proof and gate.** V: the tutorial does not obscure the world and has focus/keyboard state. F: two control maps, progress restoration, and no crashes/softlocks. P: a short physical co-op repeat is useful, but at least two independent new players without verbal guidance determine first-internal-alpha status. Gate: both complete the defined cycle and continue after save/load.

**Scene brief.** A lively medium-sized settlement, one subtle tutorial callout, and a highlight on the required building, with an open center and no modal wall of text. “S07 · TARGET MOCKUP · FIRST INTERNAL ALPHA.”

## S08 — 0.26.8 “Autosave and Recovery”

**Result for the player.** The game regularly saves progress and honestly reports success or failure; the last valid backup can be restored.

**Added/removed/improved.** Adds local periodic saving through the existing writer/backup, the time of the last success, and an unobtrusive error state. Removes “saved” before actual success, parallel write queues, and replacement of a valid copy with a failed result. Manual Save/Load remains.

**Exact frame state.** A normal gameplay scene without a pause; a compact “Saved” toast with a time, or a separate target variant with a non-modal error and an “Open details” action. The system menu shows the last successful save and the available backup. Do not show cloud storage or multiple slots.

**Contract of behavior and data.** Initial interval 60 seconds — tuning value, which is checked at load. No more than one recording is performed at a time. Success comes after the writer's completion. The main and backup files have a defined atomic/rotation contract; The refusal does not destroy the last valid copy.

**Sequence and files.** **Verified:** the manual writer/backup and host save path exist; the result of some periodic save cycles still needs to be shown. **Proposed:** state machine idle/writing/success/error → rotation contract → UI status → fault injection → recovery → performance measurement. Locations: `src/client/main.cpp`; `src/net/network_session.cpp`; `include/ribterra/network_session.hpp`; `src/sim/world_save.cpp`; `assets/ui/hud.rml`; persistence tests.

**Dependencies and negative cases.** Depends on save compatibility and internal alpha 0.26.7. Check a read-only directory, disk error, damaged main file, interrupted write, repeated timer, saving during production/weather/pending arrival, and exit during a write.

**Proof and gate.** V: success/error/recovery states do not obscure the game. F: fault injection, main/backup validity, restored people/property/production/cargo/weather, and measured pause time. P: repeated with the Windows package in 0.26.10; a separate second PC is not required here. Gate: the error is not hidden, and the last valid copy actually loads.

**Scene brief.** A night settlement under light snow, a small save-success toast in the corner, and an open compact system menu with the last successful save. The world keeps moving; no cloud icon or fake progress bar. “S08 · TARGET MOCKUP.”

## S09 — 0.26.9 “Resilient Co-op”

**Result for the player.** The host and guest see the same world, and a brief guest disconnect and return do not duplicate resources, people, weather, or commands.

**Added/removed/improved.** Adds complete connecting, waiting for world, reconnecting, resumed, and connection lost states. Removes stuck controls, repeated payment/commands, and snapshot divergence. Improves messages for version mismatch, timeout, and an impossible resume. Host migration, relay, Steam, and matchmaking are not added.

**Exact frame state.** Cooperative settlement after a successful reconnect: compact non-modal “Player returned” status, two selection/ownership colors without clutter, and unchanged resources and weather. The alternative target state shows “Restoring world” until a complete snapshot arrives, with commands unavailable during restoration.

**Behavior and data contract.** Gameplay begins after a full snapshot. Session token/resume does not repeat a confirmed command. While the guest is absent, the host remains authoritative. Returning restores ownership/control, weather forecast, pending housing, production, and physical cargo. If the host exits, the session becomes unavailable.

**Sequence and files.** **Verified:** handshake/snapshot/reconnect/resume and the shared settlement have a foundation; the physical two-PC criterion remains unproven. **Proposed:** state diagram → full snapshot gate → idempotency check → UI statuses → disconnect in each critical phase → host restart from save → physical/network runs. Locations: `src/server/game_server.cpp`; `src/net/network_session.cpp`, `src/net/protocol.cpp`; `src/client/main.cpp`; `src/ui/game_ui_lobby.inc`; network/lobby/ownership tests.

**Dependencies and negative cases.** Depends on 0.26.8 and all shared states from 0.26.1–0.26.6. Disconnect the guest during delivery, recruitment reservation, farm transition, a storm, and autosave; check duplicate packets/commands, wrong version, timeout, host exit, and invalid resume.

**Proof and gate.** V: all network states and restored control are clear. F: two local clients, fault injection, and equality of items/population/weather. P: two physical Windows PCs, 60 minutes of play, disconnect/reconnect, host restart from a save, and one verified session across different networks; loopback does not replace this. Gate: all P criteria have artifacts, or the version remains unconfirmed.

**Scene brief.** A developed settlement, two differently colored player selections, a compact reconnect-success banner, and identical winter or rainy weather for both. No competitive nations, bot armies, or host migration. “S09 · TARGET MOCKUP.”

## S10 — 0.26.10 «Windows candidate»

**Result for the player.** One ZIP runs in a clean Windows environment without development tools and has an unambiguous build ID, instructions, known limitations, and a path for bug reports.

**Added/removed/improved.** Adds a package manifest, version/build/commit, SHA-256, launcher instructions, and log location. Removes missing assets, mixed version numbers, old Expeditions instructions, and package-only crashes. Improves 720p/1080p, UI scale, focus, keyboard/mouse, and all supported locales. No new content is added.

**Exact frame state.** The game runs from an unpacked candidate; compact About/Diagnostics in the system menu shows version/build (a placeholder in the target), renderer, a log action, and a known-limitations link. Behind it is a normal gameplay scene, not a development showcase. There is no console, editor overlay, or absolute development path.

**Behavior and data contract.** The package contains exactly the required executable/assets/fonts/shaders/locales. The version in the UI, network handshake, filename, and manifest agree. The hash is calculated for the ZIP that was actually verified. A new package or version number requires package verification again.

**Sequence and files.** **Verified:** `scripts/package.ps1` and `packaging/README.md` provide a foundation and verify required files. **Proposed:** clean build → package → manifest/hash → unpack into a separate Ukrainian path → launch/save/continue/host/join → resolutions/locales → full configured CTest → package report (**proposed**). Locations: `scripts/package.ps1`; `packaging/README.md`; launcher artifact (**proposed packaging output**); `assets/ui`, `assets/locales`, fonts/shaders/art; build/CTest configuration.

**Dependencies and negatives.** Depends on the accepted 0.26.9. Check path with spaces/Ukrainian characters, missing DLL/asset, read-only location, second launch, incompatible client, 720p overflow, DPI scale, save permissions and packaged log path.

**Proof and gate.** V: a real packaged build at 720p/1080p and all UI states; target S10 specifies only the appearance of diagnostics. F: SHA-256, manifest, clean-machine launch, full configured CTest, package-only smoke/save/host/join, and a performance run with 20–30 residents using specified PCs/seed/resolution. P: install/run and co-op on two physical Windows PCs. Gate: one verified ZIP; any change after hashing invalidates candidate status.

**Scene brief.** Final gameplay scene with an open compact diagnostics panel: version/build placeholders, renderer, log button, and known limitations. The UI is clear at 16:9, with no development console and no “released” marketing badge. “S10 · TARGET MOCKUP · WINDOWS CANDIDATE.”

## S11 — 0.27.0 “First External Demo”

**Result for the player.** External players install the game and complete the same defined cycle without verbal guidance. Version 0.27.0 adds no further mechanic; it marks an accepted candidate.

**Added/removed/improved.** Adds the final package number/identifier and external acceptance report. Removes only blockers: crashes, lost/duplicated progress, inability to connect, and unsolvable food/development dead ends. Improves only what prevents independent completion; no new building, resource, enemy, or system is allowed as a pre-release bonus.

**Exact frame state.** Normal final gameplay that a tester should actually see: a settlement of 16–20 people, one useful Level II, working food and material chains, compact HUD, minimap, real weather, and two co-op selection markers. No “successfully tested” claim appears inside the game. The version in the system menu remains a placeholder until the actual build exists.

**Behavior and data contract.** The final package/hash/commit/protocol/rules/generator match the evidence. Any functional fix repeats the affected scenarios; a simulation/network change repeats the full co-op playthrough. Missing testers or a second PC leaves the criterion unmet.

**Sequence and files.** **Verified:** 0.27.0 is a roadmap marker, not an implemented release. **Proposed:** freeze candidate → give written instructions to two independent pairs and one solo tester → collect logs/bugs → fix only blockers → rebuild/rehash/retest → one final two-hour session → release report. Locations: package 0.26.10; verification report; tester instructions; logs/crash reports; no game-code change is required by default.

Package 0.26.10, the verification report, tester instructions, and the release report are **proposed release artifacts** until they are created and verified against the exact final build.

**Dependencies and negative cases.** Depends on an unchanged accepted 0.26.10. Check fresh install, solo, host/join, different network, save/continue, disconnect/reconnect, food expansion, House/Workshop II, weather, a Ukrainian path, and the bug-report path. Do not replace tester completion with an automated script run.

**Proof and gate.** V: final package screenshot/video without a development overlay; S11 provides only target composition. F: exact logs, hashes, and repetition of failed steps. P: two independent co-op pairs on physical Windows PCs, one solo tester, a recorded 60-minute network run, a cross-network run, and a final two-hour session without a new blocker. Gate: only this evidence allows 0.27.0 to be called the first external demo.

**Scene brief.** The most developed but still realistic settlement of 16–20 people in the valley, in summer after rain or a clear winter, with a working farm/mill/Kitchen and material buildings, a visible Bridge, compact HUD, and two player markers. No castle, army, monsters, manual roads, or decorative survival metrics. “S11 · TARGET MOCKUP · EXTERNAL DEMO TARGET.”

## Stop rule for series

After each version, the agent generates the next target frame separately from the runtime evidence. If the target conflicts with the available data or scope, the target is corrected, not the hidden mechanics are added. If the actual set does not match the target for the composition, a specific gap in layout, material, scale, or condition is first fixed. The image generated is not a reason to call the implementation ready.

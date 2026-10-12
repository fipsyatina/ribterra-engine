# Ribterra: detailed plan of small versions to the first alpha and demo

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Graphic cycle — update 28.09:** [ details of the cards V01–V05 and deferred DL](../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md), [ verified asset/engine audit](visual-targets/ASSET_ENGINE_AUDIT_2026_09_28_UA.md). It's a plan, not a version that's been released.

**Solution 27.09.2026:** Full Dynamic lighting and graphics switch are deferred. Now only the engine API configuration is added, effective mode remains Legacy. [Future DL-etaps](visual-targets/FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) do not block the basic world/UI update and have priority over early requirements to include Dynamic in 0.26.1.

Updated 28 September 2026. The **0.25.8** inspector and **0.25.9 production management were implemented locally**; see the [0.25.8 checks](verification-0.25.8-uk.md) and [0.25.9 checks](verification-0.25.9-uk.md). **0.25.10 was implemented and verified locally**; see the [report](verification-0.25.10-uk.md). The two-physical-Windows-PC check was not performed because the second PC was unavailable. **0.25.11, 0.25.12, and 0.26.0 were implemented locally**. **V01 was implemented locally as 0.26.1**, **V02 as 0.26.2**, and **V03 as 0.26.3**. See the [V03 description](update-0.26.3-uk.md) and [checks](verification-0.26.3-uk.md). V04–V05 remain planned as separate stages.

This document expands [ALPHA_DEMO_PLAN_UA.md](ALPHA_DEMO_PLAN_UA.md) and is the current source **for the numbers, order, and scope of the upcoming versions**. The plan contains 17 separate validation versions from 0.25.8 through 0.27.0. The first internal alpha is now labeled **0.26.7**; the external Windows demo remains the target marker **0.27.0**, not a date promise. The implemented 0.25.x and 0.26.0 releases and their verification files are not renumbered.

Facts about the implementation and evidence of previous inspections: [FEATURE_AUDIT_UA.md](FEATURE_AUDIT_UA.md). The initial draft of the plan was completed 22 September without any changes to the mechanics. The results of the 0.25.8 implementation from 23 September are given in a separate audit report.

## Limit of product

The player starts with 10 people, arranges food, takes in new residents, makes boards and bricks, gets useful improvements, and continues after preservation. For the demo, the two players run a shared settlement on Windows. The target size of the control settlement — 16–20 people, performance check — approximately 20–30. This is not a new hard line.

The Camp and 11 buildable types are preserved: House, Farm, Warehouse, Mine, Workshop, Fishing Station, Bridge, Sawmill, Mill, Kitchen, and Clay Quarry. Manual Road construction is removed from the UI and server rules together with its speed bonus; the Bridge remains a complete structure. The old Road ID is not renumbered or reused, which preserves unambiguous reading of supported old data. Physical deliveries and manual orders remain.

The graph, surface generation, fog and weather at 0.26.1 are consciously changing. The concepts set the composition and the artistic goal, but none of the forecasts, temperatures, composition capacity, or warnings shown in them can be a decorative guess: UI only shows the state that actually calculates and conveys the simulation.

Seven special buildings are temporarily hidden only for new construction. Enum, art, executed recipes, old objects, and finished buildings are not removed. Creating systems for these seven buildings — after the demo. The Expedition panel and the suggestions to cut portions/select hunters are not coming back.

In this plan, “added” often means exposing an already implemented mechanic. “Removed” means eliminating a specific defect or access to an incomplete function, not automatically deleting its code.

## Map of the versions

| Version | One major result | Build status |
|---|---|---|
| 0.25.8 | A building explains its work and idle state | Internal |
| 0.25.9 | Production can be managed without losing a worker or cargo | Internal |
| 0.25.10 | Shared settlement and correct ownership | Internal, early two-PC test |
| 0.25.11 | A new game has no empty promises or deferred attacks | Internal |
| 0.25.12 | Defined safe compatibility for old worlds | Internal |
| 0.26.0 | Boards and bricks provide an accessible upgrade | Internal |
| 0.26.1 | V01: compact concept HUD on the data and commands in place | Internal, locally implemented |
| 0.26.2 | V02: valley, river, shared surface rules, and worn paths | Internal, locally implemented |
| 0.26.3 | V03: aligned buildings, real levels II, occupants and cargo | Internal, locally implemented |
| 0.26.4 | The starting settlement can feed itself reliably | Internal |
| 0.26.5 | Complex food production justifies the investment | Internal |
| 0.26.6 | Supply and expansion are understandable to the player | Internal |
| 0.26.7 | A newcomer can complete the development cycle unaided | **First internal alpha** |
| 0.26.8 | Autosave and understandable recovery | Closed demo check |
| 0.26.9 | The Co-op is experiencing a breakdown and recovery | Concealed check of the demo |
| 0.26.10 | Verified Windows package | The candidate in the demo |
| 0.27.0 | The same cycle was passed by external testers. | **First external demo** |

The sequence of versions is not a calendar. A small version — one result that can be checked separately; It's not a promise to do it in one day. Parts UI, texts and test scenarios can be prepared in parallel. Do not delay the loss of a backup or connection to the end just because the correct number is there.

## 0.25.8 — Clear building inspector

Performed: [ description](update-0.25.8-uk.md), [ verification](verification-0.25.8-uk.md).

**Research for the player:** After clicking on the building, it becomes clear what it is doing, or working now, and what is interfering.

**Added/connected:**

- A single minimum set of information for existing buildings: name, role, owner, construction/work status, assigned worker or absence thereof.
- For the production facility — input materials, output of one batch, number of ordered/finished batches. Units and signatures should distinguish parties from objects.
- The reasons are simple, based on real life: no order, no staff, no raw materials, no available route or delivery location; night rest, if that's what he stopped working on.
- Selection and rejection messages are transmitted to the real RmlUi, rather than remaining only in the deactivated legacy HUD.

**Removes:** silent click, wrong impression of the building doing nothing, mixing expectations, work and delivery. The prescriptions that are now available are not being re-written for the new inspector.

**Improved:** display panel readability, timing and data updating when selection changes. The text of the new elements is immediately passed through the existing localization. This completed stage did not include a major redesign; A separate redesign of the world and HUD is planned for 0.26.1.

**References:** in the Woodworking, Workshops and Kitchens recreate the work and every applicable cause in simplicity; It's a reflection of the state of the world. The product manufactured but not yet delivered is not considered available in stock. Start building, finish it, change the choice — panel does not leave the old value. The rejected command gives an apparent specific reason. This is an inspector's check, not a complete gameplay review.

**Where: ** src/ui/game_ui_buildings.inc, src/ui/game_ui_economy.inc; src/client/view.cpp:1196–1211,1256–1259; src/client/main.cpp:575; assets/ui, assets/locales. Dependence: current 0.25.7.

## 0.25.9  Safe management of production and workers

Performed: [ description](update-0.25.9-uk.md), [ verification](verification-0.25.9-uk.md). Rules 15; The file format and protocol are unchanged, the file readings 0.25.8 are preserved.

**Result:** order is launched through the building; It's clear who's doing it and how to free a person without losing their cargo.

**Added/connected:**

- The agreed path building → available recipe → number of parties → employee → start». The list of companies remains a review of the same orders.
- The automatic selection takes the appropriate adult free with no permanent appointment; It's not the same as the reason for the absence. For the chosen employed specialist, the player clearly exempts him from his current assignment.
- The semantics of existing cancellation: not to start the following actions after a safe limit, to complete the required behavior with the physical load. Indicate the intermediate status of the delivery termination if the cancellation is not immediate.
- The inspector sees a busy worker and a remaining order. Not to create a separate professional manager.
- For group orders, no one worker shall be appointed twice and no duplication of the same work; show which items have received orders, which have been missed and why.

**Removed:** silent erasure of the profession by prescription; the appointment of one resident for incompatible work; the disappearance or duplication of raw materials during disposal if this is repeated in the verification. The interface and simulation check the same conditions, the server remains the source of the solution.

**Improves:** reports of employment, shortages of staff and materials. The lack of resources does not seem like a completed or depending order.

**Reference:** perform two sets of races; cancel the order before the raw material is taken, during the transfer and after production; Save/download at these stages. Count items without loss/doubling. The start of production does not silence the assigned farmer. Two customers don't get the same employee at the same time and they don't double the effect of repeatedly sending the same order. Do not add new roles, professions or standalone AI.

**Where:** src/client/client_production_group.inc:24–38; src/sim/world_economy_api.inc:16–35; src/sim/world_economy.inc; src/ui/game_ui_economy.inc, game_ui_professions.inc; economy and settlement tests. Dependency: 0.25.8.

## 0.25.10  Common settlement and proper ownership

Locally performed: [ description](update-0.25.10-uk.md), [ verification](verification-0.25.10-uk.md). Joint commands No1 and No2 and protection of other facilities are verified by two clients on one PC. **The criterion of two physical PCs is not met** following user confirmation of the unavailability of the second PC; [scenario for future launch](coop-two-pc-0.25.10-uk.md).

**Result:** Host and guest can start the cooperative themselves and manage their own facilities regardless of the number of the nation.

**Added/connected:** a new demo session by default puts two people in a joint team; The lobby explains the community of settlements. The existing control and transfer of control over the inhabitants is maintained, no new governance is devised.

**Removes:** verification UI and group orders where only nation == 1 is considered to be theirs. Ownership is determined by the current authorized player. Access to other people and buildings from this is not open. Old teams/nations in the safe don't merge automatically.

**Improves:** message about a resident hired by another player, rejected order and different versions of clients. The team, production and inventory results are the same for both clients.

**References:** separate check the joint command No1 and No2; Both players start production and see the result. In two different commands, an attempt to order a foreign structure is rejected by the server. Conduct the first real-world connection of two physical Windows PCs and a short build/delivery scenario. If the second PC is unavailable, indicate that the verification has not been completed; Local customers don't replace it. The dependent economy can be prepared further, but the collection has not yet been confirmed by the cooperative.

**Where: ** src/ui/game_ui_buildings.inc:29,51,135; game_ui_economy.inc:21; src/client/client_production_group.inc:22,26; src/server/game_server.cpp:94–104,584–598,643–645. Dependence: 0.25.9. Own network relay, Steam and diplomacy are not included.

## 0.25.11  Alpha's honest stock and deferred threats

Locally performed: [ description](update-0.25.11-uk.md), [ verification](verification-0.25.11-uk.md). Rules 17; Secure old backups are maintained, active deferred states are deleted without changing the file.

**Result:** The new game leaves the settlement's development supporting features, and there is no uncontrolled threat of war.

**Added/connected:** one harmonized alpha rule set for single game, host, and supported server run. The restrictions apply to simulations/commands, not just buttons. The inspector of an existing deferred facility can honestly explain its status.

**Temporary hidden:**

- The new construction of the House of Religion, the Pub, the House of the Cartographer, the House for Research, the Swimming Room, the Library and the Military House; Research preview and deactivated actions without a performer.
- New orders for tools and scripts until they have a role available. An old inventory of these items is preserved.
- Seven resources that exist only in HUD; a constant of +18 °C; And the price of these decisions is not having any real effect.
- Inactivated skills bonuses and career choices that promise no automatic jobs. Manual orders to collect wood, stone and food remain. Old records of professions are not renamed silently.

**Removed:** the start of new expeditions and attacks by animals under alpha rules, including the unloaded Attacking. Passive animals can be left behind. The Expedition panel is not being restored. The material branch of the workshop/career should not be hidden along with the deferred systems.

**Improves:** the upper panel and inventory list show the actual materials needed for current production. Raw materials, ready food, and road cargo are not mixed. Zero future minerals are not being looked at.

**Note:** new world has equal to 12 permissible building types plus the starting table. The stored recipes of materials and food are launched. UI and direct team forwarding do not allow a new delayed action. Animals do not cause damage to the local or host. To the next version, old worlds with dangerous/unattended states are deleted without a rewrite: do not open them in simplified rules of inference.

**Where: ** include/ribterra/building_catalog.hpp; src/ui/game_ui.cpp:950–990,1093–1152; game_ui_buildings.inc:125–131; src/sim/world_wilderness.inc:63–119, world_wilderness_api.inc; src/client/main.cpp:301,581; src/server/main.cpp:22–24. Dependence: 0.25.10. It's a reduction in the available volume, not a removal of the base.

## 0.25.12  Limited and secure compatibility of the saves

Locally performed: [ description](update-0.25.12-uk.md), [ verification](verification-0.25.12-uk.md). Control states written with code 0.25.7 of 0fe2454; The active expeditions are retreating.

**Result:** the game honestly accepts the supported old world or rejects it with explanation; The original is intact.

**Added/connected:** supported state 0.25.7 is transferred to a new copy; The source is preserved. The mismatch report has a version and a reason. Periodic car savings are not yet added in this version.

| The condition of the test | The result |
|---|---|
| A world without active expedition, with old buildings/Level II/hidden items | Owners, people, objects, levels and quantities of goods preserved |
| Same thing with the shortened portions. | The following table summarizes the results of the following calculations: an explanation of the transition; The stock is not changing on its own. |
| The same with the animal attacking | The old state does not return the attacks or damage |
| The same with the normal production delivery cargo | Delivery continues exactly once |
| Any active expedition: departure, hunting, return, self-defense | Alpha download rejected, file saved |
| Unknown or damaged format | Clearly refused without a transcript |

**Removed:** available to download worlds with uncontrolled shortened portions or an active old expedition that can no longer be managed to complete via UI.

**Improves:** predictability of compatibility. Universal transportation of all cargo expeditions is not a condition for the release of the alpha from the new world.

**Reception is performed locally:** nine control files are written in the old code 0.25.7 and have a manifest SHA256 in `tests/fixtures/alpha-0257`. Five supported states are saved/loaded twice without changing full state; The normal cargo is delivered once. Four phases of the active expedition have been rejected. Originals after acceptance or rejection are unchanged. The interface explains the reason and leaves the new game available; The new game uses a separate file. The proofs and limits — in [world](verification-0.25.12-uk.md).

**Where:** Available save/import The Ways SimCore, src/sim/world.cpp:1572, world_wilderness.inc, world_economy.inc; real fixtures in tests/fixtures. Dependence: 0.25.11. Further changes to the rules also clearly indicate compatibility; It's not a promise of lifelong support for all the old versions.

## 0.26.0  Materials and first useful improvements

Locally performed: [ description](update-0.26.0-uk.md), [ verification](verification-0.26.0-uk.md). From the usual start through UI the materials are made, the Building and Workshop improved. The price is fixed; The II deposit is hidden.

**Result:** produced boards and bricks become a means of development available to the average player.

**Added/connected:**

- The Home and Workshop upgrade button to II level thanks to the existing Upgrade command. Research houses are not needed.
- You can see the current level, the price, the worker, the progress and the specific effect. The available base price is  6 brick and 4 brick; Any change to fix as a balance, not a hidden new rule.
- Description of the completed chain: wood → board/wood; clay + wood → brick; The board + brick → improvement.
- Workshop II halves the production work itself. The path to resources and delivery from them is not automatically accelerated.

**Removes:** unavailability of an already implemented upgrade in UI; referral to a non-existent Research; Repeat the resource with a re-order if it is found.

**Improves:** The need for a Woodworking, Craft and Clay Career is evident. House II is already showing additional seats; their practical testing in human admissions is added to 0.26.2. In this built-in version, Workshop II already provides a tangible benefit.

**Reception:** in the New World through the usual UI, with no additional supplies, get brick/board and Workshop II; measuring the working phase of a single prescription before/after. Through the same UI to improve the building and check the change in capacity. The lack of materials is the cause; The re-command is not paid twice. Verify cancellation and save/load in the application phases of preparation, work and completion of materials; The returned, spent and transferred items together correspond to the cancellation contract in force. Do not issue a full refund of spent materials as a new bonus and do not write them off again when renewed.

Stockpile II remains in code, but the button is hidden to the StorageFull proof due to the capacity in the control settlement 20–30 of people. Warehouse proximity is already useful without level II; do not invent a new bonus just to justify the button.

**Where: ** src/sim/world_economy_api.inc:26; src/sim/world_economy.inc:40–95; src/ui/game_ui_buildings.inc:33–63; tests/economy_tests.cpp:51–64. Dependence: 0.25.9–0.25.12.

## 0.26.1 — V01: compact concept HUD

Locally performed: [ description](update-0.26.1-uk.md), [ verification](verification-0.26.1-uk.md). This release only covers UI-stage V01. The following large description of the “Living World” is preserved as a historical decomposition of the future V02–V05 and is not a component of the finished 0.26.1.

The actual decomposition of the implementation, dependencies and checks: [V2-plan 0.26.1 «Live light»](visual-targets/GRAPHICS_ENGINE_PLAN_V2_UA.md), [core of the render](visual-targets/RENDER_CORE_SPEC_UA.md) and [world FX](visual-targets/WORLD_FX_SPEC_UA.md). The numbers and number of internal dev-sections are determined only by the canonical table V2; They are not separate public versions and do not provide a readiness. This entire section has **planned status; runtime is not proven**.

**Result:** The new game opens as a holistic living world, close to the established concepts behind the composition: distinct relief, natural beaches, dense settlement, seasonal atmosphere, readable dwellings, and compact interface at the edges. Weather and temperature are a server state with real consequences, not drawn numbers.

**Added/connected:**

- A new version of the determined surface generator with a separate profile: valley, river, pass, shoreline, elevation, biome, resources, safe launch site and guaranteed achievable launch targets. The profile, seed and generator version are recorded in the safe; The old world is not being quietly regenerated by a new algorithm.
- The only contract height is for generation, rendering, path finding, facility placement, bridges and customer preview. A curved inclination cannot look transitable, and a dweller cannot pass through a drawn rock. The slope limits are fixed after the control cards are measured, rather than being guessed from the concept.
- Server weather calendar: current state, temperature, transition between clear weather, cloud, snow and storm, as well as limited true forecast. The same seed and state give the same development after save/load; The client receives the condition from the host and does not predict it on its own.
- Significant but limited impact of weather, temperature and snow: a server-fixed modifier acts on resource collection and construction on the Surface, and UI explains its size, cause, and completion. The true forecast is; Fuel and heating values from the concept in 0.26.1 are not shown.
- Prior to mass production of art — technical 2.5D-cut in native renderer: projected light from windows to the ground and emissive-balls, contact and directional shadows, wet materials, true flat river render with limited image illusion, atmospheric haze separated from fog-of-war, actual rain/snow particles, and cosmetic traces in visually compressed snow. Only after native proof, measured price and fail-safe behavior does mass production of final assets begin.
- New RmlUi HUD with the winter concept composition: real resources at the top left; Calendar, weather and temperature above; the active warning on the top right; the compact card of the selected object at the bottom left; contextual actions from the bottom to the center; The mini map and the system buttons on the right. The center of the world remains open. Full production, logistics and improvement are available through deployment, not lost to beauty.
- The minimap is active in RmlUi mode: it shows only explored areas, does not reveal hidden entities, indicates the current camera viewport, and lets the player move the camera safely without issuing an order.

Each complex effect has a separate contract; The name of the effect without these four parts is not a realisation task:

| Effect | Assets and data | The cost to be measured | Compulsory inspection |
|---|---|---|---|
| Window light + emission | emissive/mask atlas, ground projection anchor, color/radius, authoritative time of day | Additional quads/pass and overdraw only for visible sources, with hard cap | native day/night video; Light does not pass through unsuitable surfaces and does not pollute UI |
| Contacts and direct shadows | shadow mask/shape, ground anchor, height class, direction of light | Additional draw/overdraw to the visible object | Residents, buildings, trees, slopes, overlap and depth sort in motion |
| Wet materials | wetness mask/variant for soil, rock, roofs and wood; WeatherState and the drying transition | Another layer of material only for visible surfaces | rain → wet → dry, save/load/reconnect; without change collision/pathing |
| River and limited reflection | water normal/foam/reflection masks, coastal clip, authored presentation flow | a single water pass or bounded low-resolution/sprite projection; Only visible water | the bridge/coast/camera; no foam or reflection on land/UI; The effect is turned off without the artifact. |
| Haze, separate from FoW | depth bands/color LUT and visibility mask | 3–4 bounded world-space bands, without volumetric solver | unknown territory does not illuminate; haze is not a gray fullscreen rectangle |
| Rain and Snow | particle atlases, spawn/wind parameters, authoritative weather period/tick | the cap of the particle per resolution/quality tier; The individual particles are not serialized. | pause, save/load, late join, reconnect and host/client transition video |
| Traces and snow | footprint/decal atlas, cadence from real motion, bounded per-resident history/lifetime | limited decals, memory and batches with expiry | Cosmetic traces: do not change elevation, collision, pathing, buildability or network resolution |

**Removed: **

- A flat guaranteed start, a grey rectangular edge of fog, sharp seams of shore/ground, conflicting depth of spraying and UI covering the world with large permanent panels.
- Manual construction of the Road: the element is removed from the catalog and training, the server rejects the new command, the speed bonus road stops working. `BuildingKind::Road` and its serialized ID are reserved; The other ID do not move and do not inherit the old value.
- Decorative forecasts, temperatures, anxieties and saturation that don't come from the actual snapshot. The example `86/200` from the concept is not presented as a true stock of the selected composition.
- Visual cuts on the surface of cells that the simulation considers to be usually transient, and hidden changes in the map rules without a new version/hash and explicit compatibility policy.

**Implementation order:** record the basic frameworks and metrics → define renderer/material/asset contracts → implement a small native technical cut of all the declared lighting/shadow/wetness/water/haze/weather/footprint classes → measure it and pass stop/go → only then approve the art contract and scale the final assets → align height with movement/building → add versioned-profile generation and server weather → connect true data to HUD/minicards → complete integration, networking, save/load, productivity and package checks. The dev-cut sequence is given by the canonical table V2.

**References:**

- On the canonical 10 seed matrix, the new world has a passing start, accessible wood/stone/food, suitable places for mandatory construction, a correct bridge through the water, and no seams between the seams. Check the weather at least in summer and winter, day and night.
- The same weather and temperature sequence matches the host and the client, survives save/load and does not jump after reconnection. The warning appears to be a storm following the actual forecast; And the result can be reproduced and explained by UI.
- A separate scenario `FirstStorm` starts with a verified food buffer for at least 20 minutes and proves preparation/survival of the first storm. This does not replace the evidence of a stable food economy: its adoption remains in 0.26.4.
- The 1280×720 and 1920×1080 HUD do not overlap the central work area, all the necessary actions are available to the mouse and keyboard, long rows of supported locations are uncircumcised. The empty region HUD misses the control of the world; drag across the edge of the panel does not leave a dependent state.
- The selected composition shows its own stock/location only from the individual data transmitted; the common, reserved, transferred and available stocks are not mixed. A mini-map does not reveal fog.
- Comparison frames are made with the same seed, camera, scale, time and season; A short unattended record of movement, selection, construction, weather and temperature changes is added. A good static showcase doesn't replace a regular game check.
- The native technical slice separately proves each row of the effects table, contains GPU/CPU/frame-time/ memory and caps, shows fail-safe shutdowns, and only then opens up mass production of art. The target generation and the software probe don't prove that.
- The manual path is missing in UI and training, the new command is rejected by the server, the bonus does not affect the movement, the old numbers ID are not moved. The bridge is being built and operated as before under updated elevation rules.
- The embedded tests of generation, movement, construction, fog, render, RmlUi, protocol and save/load have been performed; At the end of the day, you've got a full CTest configuration and a Windows package. Physical verification of two PCs remains a separate unmet criterion until it is actually done.

**Range:** 0.26.1 adds weather, temperature, snow, a Surface modifier to the collection/building and the listed presentation effects. The selected 2.5D architecture with individual bounded passes remains targeted until native proof shows a specific boundary; There's no automatic transcription of the engine. Excludes rigid-body/cloth/fluid physics, detailed physical deformation or collision of snow, heating fuel, heating of homes, heating, temperature mortality, ice, castle, army, monsters, global hydrology, waterfalls, dynamic tracks and new mines.

**where: ** `include/ribterra/world_grid.hpp`, `include/ribterra/biomes.hpp`, `include/ribterra/seasons.hpp`, `include/ribterra/world.hpp`, `include/ribterra/game_server.hpp`, `include/ribterra/terrain_surface.hpp`, `include/ribterra/terrain_appearance.hpp`; `src/sim/world.cpp`, `src/server/game_server.cpp`, `src/net/protocol.cpp`, `src/client/view.cpp`, `src/client/game_art.cpp`, renderer/shader/material paths, `src/ui/game_ui.cpp`, `assets/ui`, art/FX manifests; The first is the generation, rendering, atmosphere, UI, protocol and save/load tests. Dependence: 0.26.0 and fixed compatibility rules 0.25.12. Details and budgets — are only in the three V2- documents above.

## The next gameplay stage after V02 — The real role of housing and population replenishment

Previously, this stage had the number 0.26.2. After the V02 is implemented under this number, its release number shall be designated separately; The requirements below are not considered to be fulfilled in 0.26.2.

**Result: ** new person needs a place; The house is the reason why the settlement can grow.

**Added/connected:**

- Before you pay for admission, check your nation's free housing. The seats are filled by live adults, live young residents and reserved future arrivals; Young people do not count a second among the total number of living.
- Indicate the occupancy/capacity and the individual state of expected arrival. Example: The tent gives 10 places, the building further 6, its II level raises the contribution to 10.
- The reasons for refusing are clear: there is no room, no food, or there are restrictions on admission. If you refuse, you don't write anything down.
- The existing adulthood and the prohibition on normal work for young people are maintained; The arrival and maturity statements are clearly signed.

**Removed:** receipt without adequate housing; died in the counting room; Double reservation of one last seat by two customers. Do not add family, birth, furniture or heating.

**Improves:** the choice of either building a new home or improving an existing one. Population produces more labor, but it increases real consumption.

**Admission:** at 10/10 places of admission rejected without payment; after completion of the building, admission is permitted; A young man takes one place and when he's grown up, he doesn't take another. Two simultaneous requests for last place do not create an excess of arrivals. The state of waiting and reservation is experiencing save/load. House II allows for admission to additional seats.

**Where:** src/sim/world.cpp:1139–1164; src/sim/world_settlement.inc:23–48; include/ribterra/settlement.hpp; src/ui/game_ui_settlement.inc. Dependency: 0.26.0.

## Future gameplay stage  Harmonized farm modes

The previous number 0.26.3 occupied by the implemented graph V03; The new release number for this gameplay-episode is determined separately.

**Result:** The player understands when the farm is giving ready food and when the raw materials are for the kitchen, and can be directed to switch jobs.

**Add/connect:** an obvious choice for a particular farm between basic food with permanent farmers and raw material orders on current recipes. The output signature distinguishes the finished Food from the grain/sheep/meat. The workers shown, the active action and the status of the move.

**Removes:** the simultaneous use of two executive paths of one farm is not noticeable; the discontinuation of permanent work by prescription; double output from the same action. A change in diet does not transform existing grains into ready-to-eat foods.

**Improved:** moving across the safe delivery limit. The transition to raw materials frees permanent employment clearly; The transition to basic nutrition requires a clear farmer's purpose. Choosing a regime shows which people are going to be released. The designation mechanism with 0.25.9 is reused, not rewritten.

**Reception:** the base mode only gives its result; raw  is the result of a chosen recipe. Change the mode during work and transfer, check the goods and occupancy after save/load and in both clients. No one is forever unemployed. It's the control version; The new productivity values are set to the following.

**Where:** src/sim/world.cpp:1500–1517,1589; world_economy_api.inc, world_economy.inc; src/ui/game_ui_buildings.inc; permanent assignments in settlement. Dependencies: 0.25.9 and 0.26.2. This adds neither a new farming engine nor a cook profession.

## 0.26.4  Steady start and normal day/night

**Result:** the newcomer has time to unravel, and the initial settlement can survive without an endless emergency food collection.

**Added/connected:** visible food supply and understandable consumption or approximate time of supply. If this is a valuation without future revenue, sign it. The food in the cargo is no longer considered available for consumption.

**Removed:** start race due to too small a buffer; the taking of the balance by the consumption of the initial stocks; It's a night of excessive waiting.

**Improves only by the numbers shown:** original Food, farmer's produce, consumption, calendar and, if necessary, price/time replenishment. The flow rate 100 Food is approximately equal to 100 seconds without inputs; day and night now in 12 minutes. This is the original audit data, not the target.

**Aims for verification, not a balance already reached:**

- The starting meal is sufficient for at least 5 minutes without replenishment for the starting 10 people.
- The base farm with two adults after launch supports these 10 people for two full cycles day/night without manual food collection. The second cycle shall be delivered at least as much as consumed; the stock at the end is not lower than the beginning of the cycle; There is no worsening of hunger/HP.
- The night shift setting does not create more than 2 minutes of mandatory inactivity at 1×. Raids and night events are not included.
- On 16–20 inhabitants the player needs to expand the feed; The start-up farm should not be randomly providing unlimited growth.

**References:** separate the food delivered and consumed, the initial/final supply, hunger and rest time. Check two controls with real distances, no hidden ready resources. After the change of calendar, repeat the check; long timers are not to be treated as added content. Changes in rules/hash and the compatibility limit should be obvious.

**Where:** include/ribterra/seasons.hpp:10–14; economy.hpp, settlement.hpp; src/sim/world.cpp:1572–1604; stockpile RmlUi. Dependency: 0.26.3.

## 0.26.5 — Kitchen and mills with proven use

**Result:** more complex nutrition helps to provide a growing population and pays for additional work.

**Added/connected:** brief explanation of available routes: grain → flour → kitchen → food; vegetables/meat/fish → the appropriate recipe for the kitchen. For recipes that require fuel, wood is shown. The fish station is giving raw materials, not ready-made food.

**Removes:** lack of a clear reason to build a kitchen; a mixture of raw materials with prepared food; It's a false claim that the benefit of a prescription is the same as its expense without the cost of supply.

**Improves:** the origin and duration of the current recipes. First, test one complete route, such as bread, and then check that the other recipes left behind are also meaningful and working properly. Do not add new foods, food quality, vitamins, decay or hunger to a particular type of food.

**Benefit acceptance:** run paired comparisons with the same start, seed, skills, worker budget, available area, and initial stockpile. Count all labor spent on raw materials, fuel, and delivery; report fuel separately. The initial target is at least +20% finished food delivered over one complete stable cycle. Separately measure the result from the start of construction: including building materials and assembly, cooked food must catch up with and exceed the basic option in the 30–60 minute validation window. This is a test target, not a demo promise.

**Behavioral perceptions:** to the chain can go from a normal start, survive a lack of timber, restore supply, preserve and continue. The kitchen does not become a necessity for survival in the first few minutes. If the balance is not passed, change the actual numbers and repeat the measurement; The new food quality system is not an inevitable solution.

**Where:** include/ribterra/economy.hpp; src/sim/world_economy.inc; src/ui/game_ui_economy.inc; tests/economy_tests.cpp; the 15-recipe matrix in FEATURE_AUDIT_UA.md. Dependency: 0.26.4.

## 0.26.6 — Understanding logistics and expansion

**Result:** The player sees why the building is waiting, where the goods are and why the warehouse, bridge and mine are needed.

**Add/connect:** in the available panels distinguish the total stock of the settlement, the stock of the selected storage and the employee's cargo. For active ordering, indicate the delivery point if it is already identified by simulation. Do not add a manual route planning screen or a new logistics algorithm.

**Remove:** the signature of the common stock as a stock of a particular stock; false indications that the mine is already mining metals or is launching an expedition; The impression that moving the visible layer is an order to the inhabitants to go underground.

**Improved:** describes the functions available. The nearby warehouse shrinks the delivery, the completed bridge opens the passage, the mine provides the available path between the layers and access to the stone. The cause of the failure must be visible. Planned volume — UI and verification, without second transcription of the movement/delivery algorithms after the height contract 0.26.1. If a detected main route defect still requires a change in behavior, correct it and repeat the attached food measurements 0.26.4–0.26.5; Old numbers are not proof of a new balance.

**Note:** on the same scheme compare delivery to a distant and nearby warehouse without manual roads. Build bridges and get the people to the resource through the water. Through the mine to make a supported transition and obtain the stone by the rules in force; Check the cargo, the layers and save/load. On the control settlement 20–30 people record the filling of inventory and the availability of StorageFull: without stressing the capacity of the base inventory, the Stockpile II button cannot be opened. Not to turn the mine into a new mine system. If a specific map of cities is not needed, that's no excuse to force it to be built in training.

**Where: ** src/sim/world.cpp:1403,1545; world_economy.inc; src/ui/game_ui_buildings.inc:51–63,124; the available stocks/movements/transitions; docs/LOGISTICS_PLAN.md as proof of the validity of the basis. Dependence: 0.26.5. New caravans, inventory priorities and automated trading have been postponed.

## 0.26.7 — Training and the first inner alpha

**Result:** a person who hasn't written code starts an economy himself and sees the sense of developing a settlement.

**Add/connect:** Compact cues in real RmlUi: select a resident → collect resources → put in a building → assign work → wait for delivery → arrange food → provide housing → take in a person → make a beneficial improvement. The step ends with the world happening, not just the click. The clues can be hidden; The story windows and the expedition panel are not added.

**Removes:** incorrect description of winter farm stops; instructions for deleted actions; unchanged lines in the abandoned game path; messages that require an explanation from the developer.

**Improved:** first session and manual saving/loading. It was clear to me before the exit that this was an internal alpha with manual storage. The local auto-save is still ahead. If the testers are stuck, the specific action/explanation is repaired rather than the long encyclopedia added.

**References:** two control cards and at least two independent new players. Each without voice guidance starts feeding, gets a new adult, makes building materials, makes a useful Level II, and continues after save/load. Record stops, uncertain actions, famine crises and expansion decisions. Technical passing and interest to evaluate separately: the closed task itself does not prove what was interesting.

In addition to a single pass, a short cooperative repeat covers added after 0.25.10 improvements, population adoption, and farm regimes. There are no known crashes, loss of progress or blockage of the main cycle. The detected blocker is corrected to the alpha designation; This is not a list of promises for the future.

**Restrictions:** is the first suitable internal alpha, not a publicity promise of an external co-op. After its adoption, no new game systems are added to the demo.

**Where: ** src/client/view.cpp, main.cpp; src/ui/game_ui.cpp and inspectors; assets/locales; UI-scenarios and the available save/load. Dependence: 0.26.0–0.26.6 and accepted property 0.25.10.

## 0.26.8 — Self-preservation and recovery of progress

**Result:** a regular session is kept regularly, and a recording error cannot be confused with success.

**Add/connect:** local periodic storage through the existing writer/backup; the initial interval of 60 seconds to reconcile with the current host, with load checking. The time of the last successful recording and the specific error if the recording failed. Successful preservation does not require a window of opportunity every minute.

**Removes:** ignoring the host save result; a copy of the correct backup with an incorrect result if it is found to be in fact verified; The label is preserved for actual success. Continuing the game after a failed attempt does not mask the error.

**Improves:** recovery from the existing backup and message of invalid file. The hands Save/Load are left. A separate cloud service, a story version for dozens of slots or a new safe manager are not needed.

**References:** local autosave and save host create a suitable file; After the process is completed, the population, owners, production and cargo are restored. The rejection of the record and the damaged main file give a message; The last working copy is available for recovery. During periodic recording, no line of simultaneous recordings accumulates and no noticeable game stoppage occurs at the control settlement; record the time measured.

**Where:** src/client/main.cpp:246–262; src/net/network_session.cpp:40–52,297–301; include/ribterra/network_session.hpp:7–13; the existing writer. Dependency: 0.26.7 and the 0.25.12 save contract.

## 0.26.9  Sustainable co-operative for two

**Result:** Both participants see the same world, and a short break or restart of the host does not spoil progress.

**Added/connected:** complete “connecting,” “waiting for world,” “restoring,” and “connection lost” states, followed by a clear return of control. Use the existing handshake, snapshot, reconnect, and resume mechanisms; do not create a second network architecture.

**Removes:** deprecated inventory/workers discrepancies, repayment of the order, dependent control after cutting, incorrect production continuation. The host's exit honorably ends the session availability; There's no automatic host transfer to the demo.

**Improves:** messages for incompatible version, incorrect address, timeout and inability to reset the session. The lobby of the first demo focuses on the joint settlement; But the boots and the competing nations are not the main scenario.

**References:** two physical Windows PCs, 60 minutes of joint play with construction, recipes, House/Workshop, II and refill. Turn off the guest during delivery, return it, check the check and the quantity of goods. Restart the host with the saved world and return the guest. Check the same last-place reception and conflicting orders from the employee. Run a single session from different networks through a simulated method: direct access or VPN with short instructions. Local loopback doesn't prove that.

**Where:** src/server/game_server.cpp:245–415,836–874; src/net/network_session.cpp; src/client/main.cpp:299–329,377–398; the current lobby/network/ownership tests. Dependency: 0.26.8 and the early 0.25.10 network results. Relay, Steam invitations, matchmaking, and host migration are deferred.

## 0.26.10 — Windows candidate in the demo

**Result:** is a single understandable package that runs outside the developer environment.

**Add/connect:** ZIP with candidate number, build ID/commit, SHA-256, short start and connection instructions, known constraints, by way of log and bagport format. No new game content.

**Removes:** old expedition instructions, mixed version numbers, missing required package files, lost run/scale/focus errors, and cut key elements.

**Improved:** readability on 1280×720 and 1920×1080, scale UI, mouse/keyboard navigation, compatibility of all remaining texts in supported languages. The obstacles are specific, not the whole art style.

**Recommendations:** unpack and run on Windows without the dev environment; check the path with the Ukrainian characters, restart, Save/Continue, create and join the session. Measure frame time/stability and save recording on 20–30 settings with specified PC, map, and resolution. The minimum requirements shall be determined by the dimensions; 1500 people's support is not being declared. Perform full customized CTest and check the package itself; If the test is not available, it's obviously an open-ended test, not a green result.

**Where: ** scripts/package.ps1; packaging/README.md; assets/ui, assets/locales; The current build/CTest/package scenarios. Dependence: 0.26.9. Do not change the mechanics at all.

## 0.27.0  First external demo

**Result:** limited but completed game cycle confirmed by people who did not develop it.

**Added:** external demo status and final package with the corresponding number/identifier. This isn't another content update.

**Remove:** blockers found during candidate admission: crash, loss/doubling of progress, inability to connect to the claimed instruction, indissoluble food or developmental blockage. For each correction, repeat a failed scenario and stuck regressions.

**Improves:** only what prevents the already fixed cycle from going through itself. A new building, enemy or resource is not allowed as a small bonus before release.

**Reception:** two independent pairs of new testers and one single testers set up the game, start the session, provide food, increase population, make beneficial improvements, and continue to maintain the written instructions. Co-operative pairs go through the stated scenario without verbal control by the developer. On the final revision, one 2-hour session without new blockers; preserve the results of the 60-minute check and the connection across networks.

The evidence must match the final package. Even a number change means a new package and requires a run-up check and a compatible connection; functional changes require replication of embedded scenarios, network/simulation — of the corresponding full cooperative passage. Enter commit, a version of the rules/protocol, a hash packet and a verification environment.

No testers or two PCs — criteria remains unfulfilled. The functionality of the simulation script does not allow such a set to be called a confirmed external demo. Public promise of content duration only after actual passes; 60 min of network testing is not equal to 60 min of unique content.

**Where:** final package, verification report, logs, and 0.26.7–0.26.10 scenarios. Dependency: accepted 0.26.10. The document itself does not upload the package or send messages to testers.

## The Fate of All Buildings

| Building | Role and Required Work | Versions |
|---|---|---|
| Camp | Start, supplies, delivery point, 10 accommodation | 0.25.8; 0.26.2; 0.26.6 |
| The house | The actual restriction of admission; II level increases capacity | 0.26.0; 0.26.2 |
| Farm | Clear regimes, enough basic food, raw materials | 0.26.3–0.26.5 |
| The collection | Physical goods and short delivery; II while hidden without needing to be in the container | 0.26.6 |
| Mining | A foundation underground and rock; The correct explanation | 0.26.6 |
| Workshop | The brake and the useful acceleration II; without ordering unnecessary tools/lists | 0.25.9; 0.25.11; 0.26.0 |
| Fishery station | Raw materials for the kitchen and alternative food production location | 0.26.5 |
| Bridge | Access to water and access to resources | 0.26.6 |
| The Forest | Boards and wood for material and food chains | 0.25.9; 0.26.0; 0.26.5 |
| Mill | Grain → flour with proven full-chain benefits | 0.26.5 |
| The kitchen | Preparation for a Real Advantage in Growing Up | 0.26.5 |
| Clay Carrier | Clay for bricks and reinforcements | 0.26.0 |
| Road (old ID reserved) | New build, server command and traffic bonus are excluded; The numeric ID is not overused | 0.26.1 |
| Houses of Religion | Hide the new building; faith/service after the demo | 0.25.11 |
| The pub | Hide; The social impact of the demo | 0.25.11 |
| The house of the cartographer | Hide; individual map/intelligence updates | 0.25.11 |
| Research Houses | Hide; research after the demo | 0.25.11 |
| The shipping works | Hide; ore → metal → useful commodity after demo | 0.25.11 |
| The library | Hide; training/research after the demo | 0.25.11 |
| The Military House | Hide; Controlled battle/defense after demo | 0.25.11 |

## Deferred updates

Monsters come back with defense, combat control and consequences. Minerals — together with extraction, processing and useful product; The tree of nations  together with the tangible differences in development. The postponed campaign, diplomacy, full AI professions, medicine, religion, complex discontent, new biographies and a strategic map are all out. For them, there are no promises yet: the order depends on the demo result.

## The only rule of small version readiness

1. Its result is available through normal control; The software challenge only proves the bottom layer is working.
2. The scenarios described have been completed and regressions are needed. For pure text/storage, sufficient interface verification is required; Simulation tests are needed where the account, rules or status have changed.
3. The change in the world is verified after save/load; change in the shared status of the host and client. Full CTest and long network lines are not repeated after each text correction, but are mandatory at the specified edges.
4. It says that it was actually checked, what was left and what collection it contains. The new room is not a proof of anything.
5. The known blockers of its main result are not carried forward under the guise of a completed version. Independent work may take time, but the unmet criteria do not disappear.

Make the first calendar estimate after 0.25.9, when the cost of the inspector, worker, queue, and delivery is known; update it after the early 0.25.10 connection test. If a version proves too difficult, split its internal tasks while keeping the same result instead of adding new content. The plan details the work; it does not increase the number of required game systems.

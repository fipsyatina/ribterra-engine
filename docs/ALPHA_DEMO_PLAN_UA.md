# Ribterra: from the current game to the first alpha and demo

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Graphic cycle — update 28.09:** [ details of the cards V01–V05 and deferred DL](../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md), [ verified asset/engine audit](visual-targets/ASSET_ENGINE_AUDIT_2026_09_28_UA.md). It's a plan, not a version that's been released.

**Solution 27.09.2026:** Full Dynamic lighting and graphics switch are deferred. Now only the engine API configuration is added, effective mode remains Legacy. [Future DL-etaps](visual-targets/FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) do not block the basic world/UI update and have priority over early requirements to include Dynamic in 0.26.1.

Edit 22 September 2026 after re-analysis and rejection by the author. This is a strategy for the next development. It replaces the previous offer to postpone the workshop, the clay career and the improvement. Update of the execution on 28 September 2026: versions 0.25.8–0.26.0 are implemented locally, and UI-stage V01 is implemented as 0.26.1. New generation, weather, temperature and the rest of the chart cycle are divided into future V02–V05; They're not part of the finished 0.26.1. Current evidence and unverified checks are given in the detailed plan of small versions.

Detailed at the request of the author: [17 small versions — added, removed, improved and checked](ALPHA_DEMO_SMALL_RELEASES_UA.md). This document defines a specific numbering and order: the new direction of the world **0.26.1**, the first inner alpha **0.26.7**And the external demo is the target marker. **0.27.0**, not a promise to give. The previously scheduled 0.26.1–0.26.9 has been moved to 0.26.2–0.26.10; They are implemented in 0.25.x, 0.26.0 and their verification files are not renumbered. The four generalized stages as version ranges are preserved below.

Verified document database: Desktop/ribterra, branch AInora/first-playable; V01 is formatted as 0.26.1. Extended current state tables — [FEATURE_AUDIT_UA.md](FEATURE_AUDIT_UA.md). The resulting idea is a cooperative strategy for settlement, survival and people, rather than a mandatory execution of the entire old list to the demo: [source](source/development-plan-ua.txt), rows 35–38.

## 1. What Was Wrong With the First Plan

Workshops and careers were mistakenly equated with empty buildings. They're already making bricks, and the boards and bricks are already being spent on three improvements. The gap — in the inaccessible UI and weak practical role of some of the results. To remove this entire chain would mean to take away the development of the settlement, leaving primarily food supply.

Now let's divide the four cases:

| The State | Example | The Decision |
|---|---|---|
| It Works and Is Beneficial | Delivery, kitchen, woodland, bridge | Leave it alone; Check from a normal start |
| Works in code, but not in game | Level II, housing, part of the production interface | Connect with Alpha |
| Has only a shell or no consumer | Research, foundry, tools | temporarily hide access to an unfinished function; to preserve the realization/data/art |
| The Great System to Come | Monsters, minerals, the tree of nations | Separate updates after demo |

Holding until next update» means removing from the catalog new games, buttons, and hints that promise available action. Do not remove enum, assets, buildings of old coffers or implemented production mechanisms. The return will require the completion of the function and its activation, not the re-creation of the building. It's not enough to simulate a dangerous one-stop shop: it needs to be turned off by alpha rules.

## 2. New test of normal start

A separate diagnostic scenario created the usual World(70421, 10), enabled the wildlife available and used public simulation commands. No imported inventory, completed buildings or world editing.

| Verified result | Simulation time |
|---|---:|
| The forest is built | 9,70 c |
| Boards delivered, then wood | 44,10 / 69,30 c |
| Carrier built, clay delivered | 78,50 / 125,30 c |
| Built workshop, delivered brick | 143,45 / 185,85 c |
| Built Building, completed its Level II | 199,25 / 242,45 c |
| Built Farm, Mill, Kitchen; Four batches of bread were delivered. | Up to 477,60 s |
| Save/load and 40 are the same following ticks | 479,60 c |

At the end of the day, all 10 residents are alive, with minimal health 100. This literally contradicts the claim that only a farm simulated. But the scenario automatically issued 66 collection orders and caused unavailability due to UI improvements through API. It doesn't prove convenience, self-driving, stable nightlife, an external co-op or an hour of interesting content. Don't use these 8 minutes as an estimate of the length of the demo or the labor costs of the development.

The evidence is .deps/alpha_fresh_start_probe.cpp, .deps/alpha-fresh-start-build.cmd, build/alpha-fresh-start-build.log, build/alpha-fresh-start-run.log, artifacts/alpha-audit/fresh-start.json and fresh-start.save. The old 20 panels and 15 recipes are audited; This is another set that's been done before. Full CTest didn't start this time.

## 3. Why play the first alpha?

The player transforms a camp of 10 residents into a small settlement that feeds itself, takes in new people, makes building materials, and improves structures. It distributes a limited number of workers between today's food and future development. Distance affects delivery; More people are giving up their hands to work, but it's increasing the cost of food and housing.

Three connected branches:

1. Harvest/base farm → food supply → new residents → more workers and consumption.
2. Wood → logging → board and wood; clay → workshop + wood → brick; The tablets + bricks → improvements → more housing or faster production.
3. Farmer's raw materials / fish → kitchen; grain → mill → kitchen → more efficient feeding of more population.

The warehouse is closer to the enterprises, the passing railway, the bridge and the mine allow to organize supplies and to get the following resources. No session is required to use all facilities.

Real solutions: leave the worker at the table or move to the construction site; Invite someone now or stock up on something. Build a new home or improve an old one; Start a simple farm or invest in cooking. Spending materials on nearby warehouses or expansion. These decisions don't need a story, monsters, or a tree of nations.

The first alpha control result: from a new camp to stable feeding, at least one new adult, House II or Workshop II and continuation after conservation. It's a test scenario and a short-sighted orientation, not a mandatory mission with a final screen.

## 4. What Buildings Are Left and What Are Lacking

In the current future scope remain the starting Tab and **11 building types**. Their code doesn't need to be rewritten. The bridge remains a building structure; The manual road is designated in 0.26.1.

| Building | Role in the first alpha |
|---|---|
| Camp | Starting stocks, warehouse, 10 seats for initial 10 residents |
| The house | +6 places become a real condition of filling. House II gives more +4, so camp + one house II allows 20 living residents |
| Farm | Reliable basic food; It's a very clear choice of ingredients for the kitchen. Explain and agree on these regimes |
| The collection | The nearest point of receipt of materials, the shortest route of delivery. Don't sell huge capacity as a useful improvement without checking the need |
| Mining | Access to the ground and rocks. No need for new mines; Clean up the promises of metals and expeditions |
| Workshop | The brick for the improvement; Workshop II speeds up the production work. Tools and records temporarily unavailable for new orders |
| Fishery station | Alternative feedstock for water |
| Bridge | Physical access to resources and places to expand through water |
| The Forest | Improvement boards, wood for bricks and kitchens |
| Mill | Wheat → flour; Leave as a ready-made option of the food branch, not a mandatory first minute build |
| The kitchen | Food preparation should have a measured advantage when expanding |
| Clay Carrier | Clay → brick → improvement; We keep it in the alpha. |

In the current 0.26.0 Road, there is still a traffic bonus ×1,35. Plan 0.26.1 removes it from the catalog and training, bans the new server command and disables the bonus. The old serialized ID is reserved and not reused to avoid confusing the values of other devices and distorting the supported old data.

The base is: include/ribterra/economy.hpp:16–23; src/sim/world_economy_api.inc:26; src/sim/world_economy.inc:40–41,85–95; src/sim/world.cpp:1403,1545,1589; tests/economy_tests.cpp:51–64.

Seven buildings are temporarily unavailable for the new building: the House of Religion, the Pub, the House of Cartography, the Research House, the Shipping House, the Library, the Military House. Their basic systems are not implemented, and the specific cycle above is not dependent on them. They remain in the code, the art, and the load of the old worlds. The actual silences are  src/ui/game_ui_buildings.inc:125–131.

## 5. Resolutions for Unfinished Systems

| Area | We need the first alpha. | Deferred |
|---|---|---|
| Progress | Direct Upgrade buttons for supported types, price of 6 dashboard + 4 bricks, result and working condition; House II and Workshop II are definitely useful | Research, technology tree, third level, new family of buildings |
| II | Save the implemented upgrade. The button remains unavailable until the control session on 20–30 residents shows StorageFull due to the capacity of the base composition. The benefits of close-up from short routes to check separately | Don't add an artificial bonus or new mechanics for the button |
| Crops/population | A vacancy before the recruitment fee; The young man is the expected stranger. No double subscription in the cooperative | Families, births, aging, heating and heating of houses; They're not in the temperature cycle 0.26.1 |
| Food | The base farm supports the start; The preparation justifies the development costs. Normal nighttime stocks and reasonable consumption | Food quality, vitamins, dozens of products, seasonal agronomy |
| Employment | A prominent worker and reason simple; They're working for farmers, builders, carriers. Manufacturing does not wipe out the permanent profession of silence | New full AI professions, carpentry/mining, medicine and military professions |
| Habits | Save the four acting effects and XP; do not show inappropriate bonuses as active | Transmission/capture/medicine/beans, generated capabilities, binding of each prescription to XP |
| The following resources are available: | The only clear inventory of real materials; tablets, clay and bricks have an accessible finish | Seven HUD-only minerals; the recipes of the instruments/lists; Skin without a consumer |
| The Community | Normal breakdown and maturation; actual health/hunger conditions | Cosmetic dissatisfaction and related forced decisions; religion, politics, protests, treatment |
| The World | In 0.26.1  new versioned-profile surface generation, coordinated height/movement/building, natural shores and relief, server weather and temperature with save/load and network synchronization | Strategic map, state boundaries, global hydrology, disasters outside the weather cycle 0.26.1 |
| Render and the world FX | In front of the mass art — native 2.5D proof: projected window light/emission, contact/directional shadows, wetness, flat-river material and bounded reflections, scenic haze apart from FoW, rain/snow particles, cosmetic footprints and compressed-snow shading | Automatic transcription of movements, rigid-body/cloth/fluid physics, physical snow deformation/collision, waterfalls |
| Animals | For alphas, there is no attack on humans; passive animals can remain | Hunting, expeditions, wolves/cabinets as a threat, monsters, bosses, defense |
| Cooperative | Two people rule the same settlement by default; Resources and results are the same. | A few competing nations, boots as strategic rivals, diplomacy, a tree of nations |
| What is the presentation? | Short real-world training, a clear inspector and localization; in 0.26.1 — compact concept HUD, miniature map, weather and art, only scaled after the technical renderer-gate | A campaign plot, a full audio/video package, unlimited processing of all sprites to native proof |

The action limits and canonical dev-table are defined in the [V2- plan 0.26.1 «Live world»](visual-targets/GRAPHICS_ENGINE_PLAN_V2_UA.md), [ render core specification](visual-targets/RENDER_CORE_SPEC_UA.md) and [ world specification FX](visual-targets/WORLD_FX_SPEC_UA.md). The amount of internal collection is not duplicated here. Each advanced effect has asset/data contract, measured runtime value, fallback, and native verification; The name itself or the generated target is not enough to implement. The concept defines the composition. The temperature and forecast indicate the server's condition; Fuel and heating dates in 0.26.1 are not shown, as heating, heating/heating, temperature mortality and ice are not included in the scope.

Hidden resources from old safe houses are not deleted. If they already exist, leave a correct account and the available description in this alpha is not used. In the new world, they don't have HUD. Do not change the old record of a profession to another profession without a real name.

## 6. The following is a list of the most recent versions

### 0.25.8–0.25.12 — Clear start and control of what is already running

The result: a person can find and run the production that is available; It's a good foundation for development. These are internal controls for control checking, not alpha for distribution: the material chain will get an available finish in 0.26.0. The removal of empty elements is a collaborative effort. Inspector 0.25.8, team/employee — 0.25.9, property 0.25.10, alpha set — 0.25.11, compatibility 0.25.12.

1. These already implemented versions retain the then 12 building types. Give each available facility a brief description of the role, inputs and outputs. Save the workshop, the career, the boards and the bricks. The indicator for improvement is shown only after the upgrade is connected to 0.26.0. The actual future scope becomes 11 types after the manual route is removed in 0.26.1.
2. The main way to control is to click on the building, see the worker, the recipe, the number of parties and the state. The Enterprise panel remains a review, not a secret mandatory entry. Expectations are different from work and a finished result.
3. The GameView choice/error message is transmitted to the real RmlUi. Add the first visible steps: allocation → collection → construction → worker → first delivery. The existing tutorial lives only in the old HUD: view.cpp:1196, and main.cpp:575 turns it off. There is no need for a new learning movement.
4. Fix the nation == 1 checks in the panels/group commands; The server checks the property and then continues. The new default co-op start brings together two players in a team. The old nation is not to be confused.
5. Temporarily hide seven empty buildings, Research-preview, seven nonexistent resources, constant +18 °C, excess buttons turned off and prescriptions of items unused. The corresponding systems and art cannot be removed.
6. For the rules of the alpha, authoritative prohibitions on animal attacks and expeditionary launches, including unloaded Attacking. The usual animals can stay. The absence of the button does not turn off process_animals.
7. Limit compatibility with the finite set of states 0.25.7 below. Supported worlds migrate to a new copy; The original file is saved. Active old expeditions at this stage do not migrate: explain the incompatibility without re-recording and propose a new world. It doesn't stop the alpha from starting again; The universal transfer of expeditions is not included in the next version.

A safe matrix for creating test fixtures during implementation:

| The input state 0.25.7 | The result is a must |
|---|---|
| A world without active expedition; including old buildings, Level II and temporarily hidden goods | The people, owners, buildings, levels and quantities of each item preserved |
| The same plus reduced_rations | A normal feeding schedule is recorded, explanations of the transition; stocks not changed |
| The same plus animal in the attacking state | Attack is lifted by the rules of the alpha, the old state does not renew the damage |
| The same plus the usual production delivery with cargo | Delivery continues without loss or duplication of goods after repeat save/load |
| Any active expedition: road to destination, hunting, cargo return or self-defense | Alpha rule loading is off with an explanation. The old file is unchanged |

These names describe the required test states, not the files already created. For supported combinations of the first four rows, recharging is required and the same amount of property. The unsupported format is also rejected without a rewrite. Completed expeditions without an active order shall not be grounds for removing the goods already delivered.

Where: src/ui/game_ui_buildings.inc, game_ui_economy.inc, game_ui_professions.inc; src/client/client_production_group.inc, view.cpp, main.cpp; src/server/game_server.cpp; src/sim/world_wilderness.inc, world_wilderness_api.inc; assets/ui, assets/locales; save/load and the rules of the versions.

Ready: In the internal check of the installed set, the new player finds the set/builder within the first 5 minutes and launches one recipe through the console-less building; both Joint Teams No1/No2 are in check; There are no hidden attacks in the supported worlds. The Matrix of the Saves is passing, the incompatible worlds are not rewritten. A special inspection confirms the refusal to control the foreign objects. The first test connection between two physical PCs is done right here.

### 0.26.0–0.26.3 — The living world and real progress of settlement

The result: the finished materials open up the next tangible step; after 0.26.1 it happens in a world with a valid visual direction and true weather. Improvements 0.26.0♪ The living world ♪ 0.26.1, housing/refurbishment 0.26.2, farm regimes — 0.26.3. The safe appointment of an employee under 3 begins earlier, in 0.25.9, and is here reused.

0.26.1 is a separate stop/go-direct between ready-made material improvement and further balance: versioned-generation of valley and river, aligned heights for render/movement/building, server weather, temperature and snow, limited collection/building modifier on Surface, compact concept HUD and mini map. Before mass art, the native technical cut must prove projected window-ground light/emission, contact/directional shadows, wetness, real flat-river rendering with bounded reflections, haze separately from FoW, rain/snow particles and cosmetic footprints/compressed snow. Everyone needs assets/data, budget/caps, fallback and native capture. 2.5D was chosen as the target architecture for concrete evidence of its deficiency; There's no automatic transcription of the engine. The Handway and its bonus are excluded, the Bridge remains. The full sequence and the receipt of — in the three V2- documents above. The `FirstStorm` scenario requires a food buffer for at least 20 minutes as a separate preparation for the first storm; The proof of the consistent food economy is the result 0.26.4. This is **planned scope; runtime has not yet been proven**.

1. Connect the House/Workshop upgrade directly to the existing Upgrade team. Show the cost, the workforce, the progress and the difference in output. The cancellation/missing of materials uses the existing production logic. Don't demand the House of Research. API Stockpile II to save; the button is to be connected only after the StorageFull criterion from 5 is executed.
2. Link housing with recruitment: no place no payment; The new building allows for the first expansion; House II adds the capacity of the same settlement. Count the survivors of his nation, the young and already awaited arrival. Two customers don't make two arrivals for one payment.
3. Arranging permanent appointments and one-off orders. The minimum rule: automatic selection takes a free adult without a permanent appointment. The employee must be able to obtain a reasonable amount of work and a reasonable amount to be paid. No silent wiping of the trade.
4. The farm clearly shows basic food and raw material production as different goals. Do not allow two executive routes to take the same job/worker unnoticed. The switch shall have a clear end of delivery; Not to introduce another manufacturing movement or a new cooking profession.
5. Check both branches from the New World: wood/clay → board/brick → House II; farm/forest → kitchen → food delivered. Apart from checking the actual acceleration of the Workshop II and the benefits of nearby equipment without manual roads.

Where: src/sim/world.cpp:1139–1164,1500–1517,1589; src/sim/world_economy_api.inc:16–35; world_economy.inc:40–95; src/ui/game_ui_buildings.inc:33–63; game_ui_economy.inc:20–55; client_production_group.inc:24–38; tests/economy_tests.cpp and settlement_tests.cpp.

Ready: through the usual UI made House II from materials collected/made in this world; the housing actually allowed for a new person to come in; Workshop II will double the same working time without reimbursement; save/load in the middle of the materials, labor and delivery don't waste the result. The housing bill itself is not a receipt.

### 0.26.4–0.26.7 — Balance and the first inner alpha

The result: a small group can play in a settlement rather than just call up individual teams. Base balance 0.26.4, the utility of the kitchen 0.26.5, explanation/logistics check 0.26.6, learning and receiving the first inner alpha. 0.26.7. Includes local gameplay and cooperative trail checking; This isn't a statement about a finished external demo yet.

1. Balance only the numbers available: food costs, prescription costs, starting stock, day/night length, recruitment. Current 100 Food gives about 100 seconds without filling; The current day and night are going on 12 The simulation minutes. It's not a ready balance for the first session.
2. Starting goal: food buffer for at least 5 minutes without new arrivals; The base farm with two designated adults supports the initial 10 people for two full cycles of day/night without manual food collection. After the start of the farm, at least the amount of food delivered is consumed during the second full cycle; The final supply is not lower than the supply at the beginning of this cycle, hunger and health have not deteriorated. Supplies and consumption shall be recorded separately: consumption of the starting stock shall not be counted. Rest, routes, and skills are all part of the measure. That's the goal of the new balance sheet, not a description of the current farm.
3. Complex foods should justify investing. Compare pair scenarios with the same seed, initial people/skills, stock, available area, and location of the original composition. For a single employee budget, a ready route shall deliver at least 20% more food in the full steady cycle; to take into account all the raw materials, wood and supplies, and to show the wood separately. Apart from the constant rate, the investment is calculated: from the start of construction, the accumulated delivered Food must match and exceed the base option within the control 30–60-minute session. This dimension includes work on building materials collection, construction and commissioning; Don't give out free ready-made entries. 20% and 30–60 min — are the initial balancing criteria, not a result or promise of content duration. The player can choose an additional simple farm or invest in cooking.
4. Shorten the mandatory empty wait: adjust the available night to not require more than 2 minutes of inactivity on 1×. Night raids/new events not to be developed. Repeat the food calculations after changing the calendar, update the version of the rules and compatibility.
5. Supplementing short RmlUi instructions with first food, housing, new housing, and improvement; They can be hidden. Select the wrong winter descriptions, foreign English lines and instructions for the deleted panel. The total food supply should be visible alongside the rate of consumption or the time supply.
6. Two verified maps/seeds: a real progression from the Camp to a new adult and a useful level II upgrade. The first playthrough is completed without spoken hints by at least two people who did not write the code. Record where they stopped, what they did not understand, and whether they had a reason to expand. If they are bored, do not extend timers merely to increase duration; review the decisions and the benefit of development.
7. A clear manual save and recovery at this stage is mandatory. For the internal alpha, marked restrictions are allowed without a local auto-save; The absence should be visible before the exit. Fixed crashes, loss of progress and major cycle stoppage.

Where: include/ribterra/seasons.hpp:10–14, economy.hpp, settlement.hpp; src/sim/world.cpp:1572–1604; src/ui/game_ui.cpp, game_ui_buildings.inc; assets/locales; tools and testing of integrated scenarios. The numbers and gameplay hash must match the fixed packet.

Ready: can provide 10 people, grow settlements in the direction 16–20, produce building materials and get useful improvements, preserve and continue; The new tester goes through this UI. 16–20 — The size of the control session, not the new global population limit. Don't promise the length of content before these sessions.

### 0.26.8–0.27.0 — Reliability and first external demo

The result: a self-installed Windows package with the same completed cycle and confirmed co-op. Car safety 0.26.8, a sustainable cooperative 0.26.9, Windows candidate 0.26.10, adopted the external demo 0.27.0. The content is frozen at this point.

1. Two people by default in a joint settlement. The selection of competing nations and bots does not distract the demo lobby. Ownership, people transfer, competing orders, and resource expenditure remain authoritative on the server.
2. On two physical Windows PCs, complete the full loop, a 60 minute session, a brief disconnect and guest reconnect, and a host restart with the saved world. Then perform one 2 hour run on the same final revision. Loopback does not replace two PCs.
3. At least one successful session from different networks through a tested for testers method: direct access or VPN with short instructions. Steam invitation, own relay and automatic host transfer are delayed.
4. Add local periodic saving to the existing writer/backup system. Online host autosave is already called once every 60 s; show success or failure because this cycle currently ignores the save() result. Verify recovery from backup and the message shown when writing is impossible. Do not create new cloud storage or a complex save manager.
5. One ZIP with the unambiguous version/build ID, SHA-256, short instruction, known limitations and a way to provide a bagreport. Package instructions to clear expeditions and mixed numbers. Launch on Windows without dev environment, Ukrainian path, reboot and extension.
6. Check the readability of UI to 1280×720 and 1920×1080, scale/focus, keyboard and mouse; all the remaining texts of the supported languages. Productivity is measured at a controlled settlement of approximately 20–30 people, indicate PC/seed/unit, not advertise 1500 residents as proven support. The minimum requirements are to be determined from dimensions, not to be guessed.
7. Full customized CTest, packet launch and two independent pairs of new testers plus one local tester. They connect themselves, get new food, expand their population, improve their building, and continue to save on written instructions. Repeat the failed steps after correcting. It's the acceptance of the candidate, not a statistical guarantee of quality.

Where: src/server/game_server.cpp:94–104,245–415,836–874; src/net/network_session.cpp:40–52,297–301; include/ribterra/network_session.hpp:7–13; src/client/main.cpp:246–262,299–329,377–398; scripts/package.ps1; packaging/README.md; the available network/lobby/UI tests.

Ready: no known blockers of the main cycle, loss of safe and connection in the stated scenario; Two pairs completed it without the developer's guidance. The evidence matches the package. Without available physical PCs/testers, this remains an unmet criteria, and no reason to call a local collection a ready-to-play cooperative demo.

## 7. First specific tasks and dependencies

After the completed inspector/communications → secure production teams → ownership/early co-operative → set of alpha and supported safes → direct upgrades next 0.26.1: baseline → renderer/material contracts → small native technical slice of light/shadow/wetness/water/haze/precipitation/footprints → performance/fallback stop-go → only then mass art → coordinated altitude/movement/building → versioned-generation → server weather, temperature, snow and Surface-modifier → truthful-data HUD and a miniature map → save/network/performance/package regression. After receiving it: housing and recruitment → farm regimes → start-up balance and development benefits → explanation/logistics check → independent passage → reliability and external package. Hiding the empty UI, turning off the dangerous fauna, the texts and the verification scenarios can be prepared in parallel, they do not replace this sequence.

Version number  is a control result, not an estimate of the number of days. The first calendar estimate is made after integration 0.25.9 and specified after the first physical connection 0.25.10. The four generalized stages are divided into 17 smaller versions; None of these numbers mean as many short development sessions.

## 8. What to Develop After the Demo

- **Monsters and defense:** combat/retreat management, equipment, consequences of wounds, then opponents; The role of the military house is here. Do not turn off the threat to the available means of reaction.
- **Minerals and metallurgy:** useful fields → extraction → smelting → material → tool/equipment. The new resource name shall be displayed together with the completed application.
- **Research and tree of nations:** tangible unlocking and differentiation; This is where the Research House and the Library are. No empty trees to demo.
- **Mapping and world:** strategic map, signs, broader intelligence; Then we'll go back to the House of Cartographers.
- **Society:** services of the Pope, faith, discontent with real consequences, medicine. These are self-contained systems, not decorative calculators.

These are postponed directions, no announced calendar or obligation to implement all immediately after the demo. The first one after the demo is a confirmed player request; The code/art of the deferred building is already saved.

## 9. Risks and rules of stopping growth

- **Again, only food left:** the material branch and the useful Level II are required for the alpha, not an optional post-demo promise.
- **Make the kitchen an over or forced start barrier:** basic food available; The benefits of a complex road are measured by the full production, including timber and supplies.
- **New systems outside the approved scope:** 0.26.1 deliberately adds generation, weather, temperature and new input to a separate master plan. Beyond that, do not demo equipment/strength tools, Research, combat AI or complete reworking of the professions; to make connections that close the cycle.
- **Hidden hazardous state after concealment UI:** check the authoritative simulation, save and reconnect old; Keep the physical items and the original spare.
- **Step the game by automatic test:** new script-box — proof of code execution. The receiver is a real UI and independent player. If a person can't find the button, the opportunity for a demo isn't ready.
- **Endlessly polishing:** after passing the criteria, release the internal alpha, then the limited external candidate. New ideas don't move these criteria backwards.

In the original 22 edition of September, only the documents were changed and a separate diagnostic scenario was added. The September plan update 26 also does not implement 0.26.1 itself: the catalog, generation, weather, temperature, schedule, UI and road rules change only with future implementation and individual verification.

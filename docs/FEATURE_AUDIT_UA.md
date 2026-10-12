# Ribterra: real-world functionality audit before the demo

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 22 September 2026. Checkout `C:/Users/fipst/Desktop/ribterra`, branch `AInora/first-playable`, HEAD `0fe2454` and current unannounced changes after removing the «Expedition» panel have been verified. The version remains 0.25.7. It's an audit and a plan solution; The corrections listed below have not yet been executed.

Clarified after the author's denial: The previous recommendation to postpone the Workshop, the career, the board, the brick and all improvements was wrong. This almost ready branch needs to be connected to the first alpha. The action plan is  [ALPHA_DEMO_PLAN_UA.md](ALPHA_DEMO_PLAN_UA.md); It's dividing the inner alpha and the outer demos. The facts about the current code are kept below, the recommendations reviewed.

The specific details and numbering — [ALPHA_DEMO_SMALL_RELEASES_UA.md](ALPHA_DEMO_SMALL_RELEASES_UA.md): 16 small versions, internal alpha 0.26.6, external demo 0.27.0. This detail is not a new runtime audit and does not change the status of the evidence below.

## The Conclusion

The main shortcoming is not the amount of content. Part of the content that is available is not working, and part of the systems that are implemented are not connected. Adding a story, new buildings or professions will increase this problem.

- 20 types of buildings: Starting Tent and 19 available for construction.
- 7 special facilities do not provide practical use after completion. The building — Eight problem buildings: it changes the housing meter that does not affect population or behavior.
- By technical effect: 12 types do something, 3 are partial (House, House of Religion, Pub), and 5 have no special function. “Does something” does not mean “has a useful place in the economy”: Workshops and Clay Quarries produce goods for unavailable or absent consumers.
- 15 real resources and 15 recipes. Brick, tools and writing are made, but in the current standard UI they have no useful application. The skins have no consumer and after the hunt is removed they have no new source available.
- Another 7 resource in the top panel is completely absent in the economic model.
- 10 enum-profession; UI is showing 7. The choice of logger, miner or collector does not start the appropriate automatic work. 4 of the 8 shown skills have no performer or effect.
- All 15 recipes are about professions/crafts. The order discards the employee's assigned occupation.
- The study is just a screen. Three real simulation building enhancements are not available through UI.
- The manufacturing boards mistakenly consider only the No1 nation as their own. It's a flaw, not a delayed opportunity.

## Method and extent of evidence

For each type, the ads, visibility in the catalog/panel, team path, simulator performer, end result and its consumer were checked. Separately, the tests were compared to what they actually prove.

This audit rebuilt `BuildingPanelVisualProbe`, `EconomyVisualProbe`, and `EconomyTests`. The probe of all 20 panels passed; it also checked selecting a building on the map, two group orders, save/load, and two network clients on one PC with a shared nation. `economy` and `economy_ui` passed 2/2 in 49,75 s; the recipe test executes all 15 recipes with physical delivery on prepared fixtures. Captures of the House of Religion and Research were reviewed.

It's not a new world without prepared supplies, not a verification of all the 20 post-construction effects, and not an external co-op. The nation's mistakes, occupations, housing and discontent are established by the analysis of executive paths; Some new runtime tests for their reproduction in this audit were not added. The full CTest didn't start.

The evidence is: `build/audit-content-build.log`, `build/audit-building-panels.log`, `build/audit-economy-tests.log`, `artifacts/feature-audit/buildings/building-render-report.json`; All the images of the 20 panels and `choose-research.png` are in the same artifact directory.

Further verification after this audit: new World(70421,10), without importing finished buildings or supplies, through public teams passed Forestry → board/wood, Career → clay, Workshop → brick, Building → II, Farm → grain, Mill → flour, Kitchen → bread. Save/load and the following 40 ticks gave the same checksum; All 10 people are alive from 100 HP. It's about 8 minutes of simulation with automatic 66 collection commands. The enhancement is caused by API, so this result does not prove accessibility because of UI or convenience to the novice. The evidence is: `.deps/alpha_fresh_start_probe.cpp`, `build/alpha-fresh-start-run.log`, `artifacts/alpha-audit/fresh-start.json`. He proves that the useful foundation of these buildings does not need to be rebuilt.

## All 20 buildings

Catalogue and prices: `include/ribterra/building_catalog.hpp:11`. Permission to build 19 types: `src/sim/world.cpp:1190`. The UI cycle does not replace the unfinished types: `src/ui/game_ui.cpp:950–990`. The message is not implemented. The player already sees it in the inspector after the resource is spent.

| Building | The actual function | Gap / conclusion for the demo |
|---|---|---|
| Camp | Automatic start, 10 000-capacity stockpile, starting supplies, and resident arrival point; also the 10-seat counter | Retain. It provides real value independently of housing |
| The house | Adds 6 to the housing index | It has no effect on people's reception, health, employment, or homelessness. One real consequence is that housing limits the replenishment |
| Farm | The assigned farmer directly creates the food. Individual orders create grain, vegetables and meat | Two parallel ways of working without a clear role. Save the basic food; The more complicated path should give a measured advantage in the expansion |
| Stockpile | Physical storage with 5 000 capacity; delivery point | Retain. The panel shows the settlement's total inventory, not the inventory of one specific stockpile |
| Mine | Passage between layers, clearing a 3×3 underground area, and revealing territory | Retain the existing function. It produces ordinary stone, not metal |
| Workshop | Recipes for bricks, tools, and spears | Retain; connect the existing upgrades as a consumer of bricks. Defer only tool and spear recipes that have no available use |
| Houses of Religion | Technical exception for night work in his cell | No services, visitors, faith or influence on people; The exception is not a useful function. Delete from the demo |
| The pub | Such a nightly exception; the total food supply | There's no attendance, no food budget, no social impact. Delete from the demo |
| The house of the cartographer | Decorated building and inspector | There are no maps, minicards, markers or bonus reviews. Delete from the demo |
| Research Houses | Opening the Research card II | There is no launch, progress, result or unlock. Delete from the demo |
| The shipping works | The following is a list of the activities that are planned: | There are no minerals, metals, and recipes. Delete from the demo |
| Fishery station | By the water, on order and with the worker gives 6 fish | It works; Fish needs a kitchen. Non-passive food generator |
| The library | Decorated building and inspector | There's no training, no training or a skill bonus. Delete from the demo |
| The Military House | Decorated building and inspector | There's no training, no formation of convoys, no delivery of weapons. Delete from the demo |
| Bridge | The finished sections make the water flowing | Leave it alone; There are separate transition and retention checks. |
| The Forest | Wood → board or wood | Leave both recipes: wood for the kitchen/ bricks, improvement tablets |
| Mill | Grain → flour | Leave if the main cycle uses cooking |
| The kitchen | Four types of raw materials + wood → ready food | It works. Its necessity undermines direct food production by the farmer |
| Clay Quarry | A worker produces clay on request | Retain: clay → brick → existing upgrades. Their available UI closes the gap |
| The road | On the ready-made road cell motion ×1,35 | It works by code; The individual test bonus speed was not found |

The main effects: `src/sim/world.cpp:907,1403,1500,1545,1589`; `src/sim/world_settlement.inc:18`; `src/sim/world_economy.inc:17–95`. The missing functions are directly described in `src/ui/game_ui_buildings.inc:125–131`.

The building is particularly important: `World::housing()` counts places, but runtime-consumers — snapshot and UI (`src/server/game_server.cpp:771`, `src/ui/game_ui_buildings.inc:121–122`, `src/client/view.cpp:1167`. Recruitment in `src/sim/world.cpp:1139–1153` doesn't read this number. The `+6 housing` test does not prove the benefits of housing.

## Resources: sources and consumers

The full list: `include/ribterra/world.hpp:209`, `include/ribterra/economy.hpp:9–23`. This is a current state of affairs, not a proposed shortened catalog.

| The following is the list of resources: | Where It Comes From | Where is it being spent now? |
|---|---|---|
| The tree | Starting cutting the forest | Building, board, wood |
| The Stone | Starting surface rock and underground mining | Building, tools, records |
| Ready food | Starting harvesting on the grass; automatic farmer; The kitchen | Hunger, population growth, farm building/pub |
| Boards | The Forest | The tools/the lists. Improvement — only not available through UI additional consumer |
| Clay | Clay Quarry | Brick; the entire branch currently ends at an unavailable upgrade |
| The brick | Workshop: clay + wood | Only the simulation enhancements that the player can't run now |
| Grain | Order at the farm | It's a little bit of a mess. meat through animal husbandry |
| Fruits | Order at the farm | Food in the Kitchen |
| Fish | Fishery station | Food in the Kitchen |
| Meat | Farming on a farm; old hunting | Food in the kitchen. After removing the panel, the source through the farm remains. |
| The Boxer | Mill | Bread in the Kitchen |
| Instruments | Workshop: boards + stones | No crew, consumption, bonus or other use |
| Wood | The Forest | All dishes are kitchen and brick. Building burnings not realized |
| Leather | Old active hunting; new hunting with UI not launched | No consumer |
| Listings | Workshop | The simulations are to organize expeditions and influence animal combat. No new order available to order/start the expedition after the panel is removed |

Coal, iron, copper, gold, silver, steel, crystal are seven more names on HUD with a constant zero; They're not in `ItemKind`. The evidence acknowledges that production is unavailable (`assets/ui/hud.rml:2`, `src/ui/game_ui.cpp:1093–1102`). These are not hidden deposits to be found.

The inventory panel shows three old materials, and all 15 are available in a separate production panel (`src/ui/game_ui.cpp:1128–1152`, `src/ui/game_ui_economy.inc:14–17`). So the player can make an item and not find it in the usual inventory list.

## All the 15 recipes

All are available at the inspector of the respective completed building for the nation No1. Time — only work at Level I, 20 ticks/s; No roads, no waiting, no nights, no shipping. Without raw materials, it still requires a worker.

| Building / recipe | The entrance | The Exit | Time is now |
|---|---|---|---:|
| Farm: grain | No raw materials | 8 grain | 20 c |
| Farm: vegetables | No raw materials | 8 vegetables | 20 c |
| Fishery station | No raw materials | 6 fish | 20 c |
| Sawmill: boards | 3 wood | 6 boards | 15 s |
| Mill | 4 grain | 4 flour | 15 c |
| The kitchen: bread | 3 flour + 1 wood | 8 food | 15 c |
| Kitchen: fish dishes | 3 fish + 1 wood | 8 food | 15 c |
| The kitchen: meat dishes | 3 meat + 1 wood | 8 food | 15 c |
| Workshop: brick | 4 clay + 1 wood | 4 bricks | 20 c |
| Workshop: tools | 2 boards + 2 stone | 2 tools | 20 s |
| Wood: wood | 2 the tree | 6 Wood | 10 c |
| Clay Quarry | No raw materials | 6 clay | 20 s |
| Farm: livestock | 6 grain | 4 meat | 30 c |
| The kitchen: vegetable dishes | 4 vegetables + 1 wood | 8 food | 15 c |
| Workshop: spear | 2 boards + 1 stone | 1 spear | 15 s |

Recipes really consume materials and create a physical burden (`src/sim/world_economy.inc:43–95`); `tests/economy_tests.cpp:28–41` checks every one. This test doesn't test whether a result is needed in the game.

Wood, stone and gathered in the place of the food of the slaughter; No recovery was found. Grain, vegetables, fish, clay, and ready-to-eat food from the old farming routes are being restored. It's not a requirement to add regeneration to the demo: for the first session, you just need to check the available inventory and the pace.

## The Food Cycle: The Main Contradiction

Automatic Farmer creates 8 ready-made Food without mill, kitchen, and wood. Orders on the same farm create raw material for the long chain. So there's a technically working production chain that the player can go through in a different way of working the same structure.

In the standard new game 10 Residents and 100 Food. For full portions they consume 10 Food every 200 ticks, that is, every 10 second. Without new arrival, it's about 100 seconds before the stock runs out, not death. One farm has a maximum of two automatic farmers: nominal 16 Food per 20 seconds versus consumption 20. It's about adjusting to skills, road, delivery and night. So the assumption that he set up a farm provided the start-up village was wrong.

The evidence is: `include/ribterra/world.hpp:16,31–33,348`, `src/sim/world_professions.inc:326–341`, `src/sim/world.cpp:1500–1517,1572–1583`. For the demo, first select one food model, then balance. It is not necessary to add new products to these rules.

## Professions and skills

Professions: `include/ribterra/world.hpp:47`, visibility UI — `src/ui/game_ui_professions.inc:110`, performers `src/sim/world_professions.inc`.

| The profession | Available in select | What Really Makes a Purpose |
|---|---|---|
| No assignment | Yes | Supports automatic construction and hauling; allows manual orders |
| Woodcutter | Yes | Does not start automatic cutting; manual cutting works without this profession |
| Miner | Yes | Does not start automatic mining; manual mining works without this profession |
| Forager | Yes | Does not start automatic gathering; manual gathering works without this profession |
| Builder | Yes | Automatically works at a specifically selected construction site; may remain idle without a workplace |
| Carrier | Yes | Performs logistics tasks; the workplace restriction applies to the cargo source cell |
| Farmer | Yes | Automatically works at a farm, up to two residents; currently produces finished food |
| Smith | No | The value is preserved, but no worker behavior exists |
| Medic | No | The value is preserved, but treatment does not exist |
| Guard | No | The value is preserved, but no worker behavior exists; it does not control self-defense against animals |

Hand movements and assemblies do not require a suitable profession. The ban on the manufacture of cars is clear in `world_professions.inc:67`, leisure for three visible professions — `:383`, absent performers of the rest `:390`. This is not to say that a resident is incapable of cutting down a tree.

| The pattern, all 8 shown UI | Effect and experience in a regular game |
|---|---|
| The cut | They work in hand-held machines |
| Mining | Working in a hand-held mining operation |
| Agriculture | Working in food harvesting and old automated Farmer |
| Construction | They work in physical construction |
| Transportation | No accounting XP or impact on transportation |
| Coal mining | There is no recipe mastery, in particular. |
| Medicine | There is no |
| Fight | There is no; The damage caused by the war is determined by other rules. |

The evidence is: `include/ribterra/skills.hpp:8–19`, `src/ui/game_ui_professions.inc:60`, `src/sim/world.cpp:1403,1432,1460,1509,1518`. The ability to break down is retained, but normal human creation is zero. The game's power generator is missing.

Receptive economics — separate path: `world_economy_api.inc:29–31` erases the purpose of the profession; `world_economy.inc:84–87` adds exactly one tick of work and does not cause either a modifier or useful_work. UI selects the first free worker (`client_production_group.inc:24–38`). So a farmer who is a farmer is not given a bonus for ordering grain. A demo should not promise a full professional economy or develop it all to fill in the blanks.

## Other systems shown

| The system | Current state | Action in front of the demo |
|---|---|---|
| Research | 20 card II, start off; There's no simulation. | Hiding the whole preview, not building a tech tree |
| Improvements | API has House II, Stockpile II, Workshop II; UI does not create data upgrade buttons | Connect to Alpha without Research. House II to connect with the population; The workshop II gives speed. Stockpile II to show after the yield yield is proven |
| Dissatisfaction | Calculator 0–100, has no effect on productivity, reception of people or events | Select the indicator and the corresponding forced decisions from the demo; Not adding a riot/new social system |
| Population growth | 50 Food → through 2 minute young resident → still through 3 minute adult | It works, but it requires housing restrictions and a proper count of the living |
| Youth | The age timer and the ban work. Forced labor adds only cosmetic dissatisfaction | Forced labor and adulthood |
| The night | Normal work is stopped; There is a permit for forced change. | To maintain natural decomposition; Do not advertise the high price of forced decisions |
| Health and Hunger | Food consumption, accumulation of hunger, loss HP/death work; There is no cure. | Check the pace. Do not promise to the doctor/physician; Count the living apart from the records of the dead |
| Seasons | The calendar and some of the rules work; In winter, hunger without food grows faster. | Not to call the whole system a silent one; to check specific promises |
| Winter on the Farm | UI promises to stop growth; Both farming routes work without this seasonal check. | Select the wrong text; The complex agronomy of postponing |
| The temperature | Constant +18 °C, without affecting the game | Select a decorative indicator |
| Resize Field / Storage Rules / Recall | The elements in the panels are turned off | Select inaccessible actions, not implement them to complete UI |
| Building inspectors | Strictly written English names/descriptions regardless of the chosen locality | Translate only left in the demo interface due to the existing mechanism |
| Evidence/Teaching | The evidence explains the movement/separation/ball. The three-step tutorial exists only in legacy HUD, which the real client turns off | Short steps to move into RmlUi, continue to delivery, housing and improvement; Don't create a plot line. |
| Lobby, chat, pause, speed, storage | They have processors and execution paths | These are not empty buttons; We need a new world and two physical PCs. |
| Boots | There's a limited logic: one house/farm, a collection of resources, a farmer; in the joint command control limit | Not to be confused with full strategic AI; Not including boots in the first demo criteria |
| The setting | Language and scale UI work | There is no full menu of the graphics/audio/key re-designation; It's the lack of capabilities, not the broken buttons that are available. |

The dissatisfaction is read in runtime only UI: `include/ribterra/settlement.hpp:23`, `src/ui/game_ui_settlement.inc:21–22`. The number of people: `src/sim/world.cpp:1139–1164`; age: `src/sim/world_settlement.inc:23–48`. The dead record remains in the mass of the inhabitants; Its size is used as population/limit. Boots: `src/server/game_server_lobby.inc:64–103`.

The "Enter Underground" button switches the visible layer. It does not order residents to change layers. Its “mining expedition” text should match the actual controls (`game_ui_buildings.inc:51–63,124`). The remote expedition panel should not be returned to correct this text.

Real tutorial: `src/client/view.cpp:1196–1211` draws it only with legacy HUD, and `src/client/main.cpp:575` causes compose with false. RmlUi tutorial_step does not use. Part GameView::set_status is also only drawn by the old HUD (`view.cpp:1256–1259`), so the result of a single UI- action may not receive a clear message. These are integration gaps that you can't see from enum checking or tutorial status testing.

Note: the local game has a hand Save/Load; The automatic host save is every 60 s (`include/ribterra/network_session.hpp:7–13`), but the result of the call in `src/net/network_session.cpp:297–301` is ignored. An external demo requires local periodic storage and a visible error in the host's storage. `packaging/README.md` still contains instructions for the already deleted panel; It's a problem that needs to be fixed in a package, not just in the developer's documents.

## Co-operative interface defect

`game_ui_buildings.inc:29,51,135` checks `nation == 1`, not the property of the current player. `game_ui_economy.inc:21` rejects all other nations. `client_production_group.inc:22,26` also rejects their buildings and people.

It is not normalized ID in snapshot: the server stores actual objects of the current nation without renaming (`src/server/game_server.cpp:584–598,643–645`; `include/ribterra/game_server.hpp:260`). So the inspector calls the own construction of the team No2 a foreign one, the list of its enterprises is empty, the group order does not start.

This applies to any player in the No2–6 nation, not necessarily the second connected». Two in Team No1 miss this mistake. After correction, check the joint command No1, the joint command 2 and two different commands as authorization regression. The first demo scenario could still remain a one-nation cooperative.

## The results of the already deleted panel

Animal and simulation data are preserved. New hunting/expeditions through the client UI are not available; It's a deliberate reduction. Meat is still available because of livestock. Old active expeditions may end and bring items.

An additional significant gap: the local client automatically turns off the animals (`main.cpp:581`), the online host also turns them off. Wolves and coyotes can attack, and the old expedition can go into self-defense (`world_wilderness.inc:63–119`). So to postpone the combat content, you need to authoritatively disable the attacks in alpha rules, including the old Attacking mode. Removing the panel alone is not enough. Residents have limited automatic self-defense. It's not a replacement for a regulated, complete combat regime.

The old reduced portions still double the consumption interval from 200 to 400 ticks, but they can no longer be disabled in the UI (`world.cpp:1572`). Supported states require a controlled migration of the save into a new copy without losing cargo. For the first alpha, active old expeditions should be rejected with an explanation while preserving the source file; universal migration is not included in the next version. The final compatibility matrix is in [ALPHA_DEMO_PLAN_UA.md](ALPHA_DEMO_PLAN_UA.md), stage 0.25.8. Hiding new buildings or recipes does not permit removing old buildings, goods, or upgrades from saves.

## Solutions for upcoming releases

1. **0.25.8–0.25.12: internal assemblies for control checks.** Inspector, worker/prescription/delivery, message RmlUi, ownership, alpha safety rules and supported safes. Until the upgrade is connected, it's not an alpha to distribute. Hidden empty UI — is a side effect.
2. **0.26.0–0.26.2: the progression of the settlement.** Improvements available without Research; And the tablets and the bricks shall be needed; the housing is restricted to replenishment; The farm's harmonized regimes. The craft and the career remain.
3. **0.26.3–0.26.6: balance and first inner alpha.** Start and day/night; basic and more profitable food; understanding logistics and learning; independent transition to the new adult and useful Level II; save/load. Alpha is accepted in 0.26.6.
4. **0.26.7–0.27.0: reliability and external demo.** Automatic storage with visible errors, two on physical PCs, reconnect/host recovery, package and independent testers. No new systems to be added to the stabilization.

The proposed directory: Automatically set; The building types are: **12 Building types**  Housing, Farm, Warehouse, Mine, Workshop, Fishery Station, Bridge, Mill, Kitchen, Clay quarry, Road. The grain and the improvement must have a practical effect before the external appearance. The seven future buildings are only hidden for new construction; Their code, their art, and their old objects are not deleted.

Monsters, new minerals, the tree of nations, research, medicine and social systems — later individual updates. A full review plan, choice between basic/complex feeding, criteria and limits of evidence  [ALPHA_DEMO_PLAN_UA.md](ALPHA_DEMO_PLAN_UA.md).

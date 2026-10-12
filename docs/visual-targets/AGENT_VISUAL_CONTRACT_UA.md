# Ribterra — Contract for the use of visual purposes for agents

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**The order of the basic chart:** [V00–V05 — old view → UI → earth → objects → rain → winter](GRAPHICS_PROGRESS_UA.md). This is the main sequence of visible changes after the Dynamic delay. Old dev/GW/DL — technical packages, not ready images of each version. [Gallery of progress](index.html); [old 13 public targets](public-versions.html).

**Final solution 27.09:** Full dynamic lighting and switch is deferred. Now the code is just a safe preparation. [Future plan DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) has priority: Dynamic is not a world-based update blocker/UI; its light/occlusion gates are only applicable to future enabled mode.

**V2 / 27.09:** [plan of action](GRAPHICS_ENGINE_PLAN_V2_UA.md), [Core](RENDER_CORE_SPEC_UA.md), [FX](WORLD_FX_SPEC_UA.md) and [Asset spec](ART_PRODUCTION_SPEC_UA.md) have priority in technical details and order. 24 The dev-etap.

The date is 26 September 2026. Status: **Normal contract for future implementation and verification**.

This document applies to the generated targeted illustrations of public versions `0.26.1`...`0.26.10`, `0.27.0` and internal collections `0.26.1-dev.01`...`0.26.1-dev.24`. It complements the main plan Live Light», the world/UI technical specification and the weather specification. If the illustration is contrary to the authoritative simulation, the actions available, or these documents, the status of the game and the written contract are correct, and the illustration needs to be reviewed separately.

## 1. Time limits and levels of evidence

| Term | The Meaning | What is not |
|---|---|---|
| **AI target** | A generated objective illustration of the composition, atmosphere, scale, materials, and approximate hierarchy UI. | Not a game frame, not a protocol data model, not proof of implementation, not a license to show fictitious systems. |
| **UI overlay target** | A clearly painted surface illustrates the reference of the panel's location, their proportions, typographic hierarchy and states. | Not a source of numbers for HUD and not a raster that can be displayed instead of RmlUi. |
| **Native capture** | Uninstalled PNG from a running Windows executable and actual D3D12 renderer for a precisely identified set. | Not a software probe, not a Photoshop composition, not a shot of a targeted illustration. |
| **Native video** | Unassembled record of actual runtime showing input, motion, shutdown, change of state and time transitions. | Not a slide show of target images, not an accelerated animation, not a simulation. |
| **Machine manifest** | JSON or text recording build/commit, versions rules/protocol/generator/save, seed/profile/scenario, save/fixture, cameras, zoom, calendar, weather, locale, UI, scale, resolution, renderer, GPU, and time capture. | Not a free description of the scene itself. |
| **Visual acceptance** | The result is native capture/video, manifest, overlay-equation, functional and render/UI tests. | Not subjective has become more beautiful and not one target side-by-side with another scene. |

Priority of evidence:

1. Authentic state of the simulation, save/network snapshot and machine testing.
2. Native capture/video is the verified manifest collection.
3. Instrumental render/UI probes if they use the same runtime path.
4. AI target as the direction.

A lower level cannot resist a higher one. A good AI target doesn't make a false prediction true; The green UI test doesn't prove that the world is visually responding to the target.

## 2. The Rules of the Agency

### 2.1. How to use a target

- Read the target along with the scene ID, the version, the written description, and the stage outline.
- Draw on the relationship: where the center is left open, what masses of light/dark, how the buildings are grouped, where the warm accents, which visual priority has a warning.
- Reproduce the target using the actual renderer, RmlUi, runtime assets, simulations, and permissible presentation FX.
- If the target accidentally displays an unforeseen system, do not implement its silent. Record the difference and leave the element hidden or replace it with a true analog in native capture.
- The pseudocate or error letters in AI- illustrations are not copied. In the game, the entire text is created by RmlUi with state fonts and localization.

### 2.2. What is Strictly Prohibited

- Don't use the target or its large fragment as the static ground of the game world.
- Do not cut from the entire target building, people, river or reef as ready runtime sprites without a separate manufacturing passport, closed surfaces, pivot, footprint, seasonal conditions and overhang checks.
- Do not paint HUD in the background texture. Panels, text, icons, focus, hover, disabled and errors should remain true interactive components.
- Do not change the target, baseline or comparison manifest to make the implementation pass. Changing the reference requires a separate versioned file, cause, author decision, and re-reception; The old file is not being rewritten.
- Don't pick up the camera, seed, season or resolution just in the after-scene. If the parameter is changed, the baseline is retrieved with the same parameter and the reason is recorded.
- Do not release a manual setting scene for a normal start, but a diagnostic fixture — for a completed game cycle.
- Do not tamper with progress, stock, weather, population, capacity, route, cargo or work regardless of status.
- Not to be confused with generated illustration, software probe or test JSON game screenshots.
- Don't just close the visual criteria with unit tests. And don't just close the functional criteria with a frame.

## 3. Fixed art and screen language

### 3.1. Camera, projection and scale

- The actual isometric projection 58×29 is retained until a single native proof shows that it is the cause of the unacceptable result.
- The camera doesn't rotate. All production assets have the same direction of isometry and light at the top left.
- For each scene, the manifest fixes the world center, zoom, and logical resolution. The equation before/after has a center deviation of no more than about a quarter of the visible tail on the control zoom.
- For V01: 1280×720, 1920×1080, 2560×1440; The current UI scale 85/100/115/130%. OS DPI 100/125/150% is checked separately. 150% UI — future accessibility/viewport work, not an option.
- V01 stores the current logical 1440×900, letterbox and input mapping; The layout is adjusted to the UI scale. Drawable-native UI/broader world viewport with Core — future offer, not condition V01. Details in the plan 28.09.
- Traffic video checks the sorting on both sides of the house, the trees, the slope, the bridge and the cargo. A long static perspective is not enough.

### 3.2. Normalized geometry HUD

The observed ranges derived from the initial 1280×720 master plan diagram are given below. This is **not a statement of a pixel-measured concept**. The ranges are needed so that the agent does not turn the compact HUD into large dashboard panels.

| Area | Starting part of the screen | Allowable starting range | The Yucca |
|---|---:|---:|---|
| External withdrawal | about 1% of the width | 0,7–1,5% width | From the nearest end |
| The following resources are available: | about 32% W × 8% H | 30–34% W × 7–10% H | Up to left |
| Forecast | about 27% W × 12,5% H | 24–29% W × 10–14% H | Top to center |
| Current temperature | about 10,5% W × 8% H | 9–12% W × 7–10% H | Next to the Forecast |
| Warning | to 24% W × 16% H | 22–26% W × 13–17% H | Up to the right |
| Compact inspector | about 32% W × 24% H | 30–34% W × 21–26% H | From the bottom left |
| Four Steps | about 32% W × 11% H | 30–34% W × 10–13% H | Down to center |
| Minimum with control | about 20% W × 27% H | 18–22% W × 24–29% H | Down to the right |

The permanent panels do not fit into the central workspace approximately `x=34…76%`, `y=16…82%`, except for the temporary tooltip or modal that the user has opened himself. A deviation of the position from the coordinated anchor to approximately 1,5% of the screen size is permissible if it eliminates localization circumference. It's the same color that doesn't make up for the wrong composition.

The 1280×720 start orientations are left with 12 dp margin; the resources are approximately 412×58; the forecast 340×90; the temperature 134×58; the warning to 308×116; the card 410×172; the action 410×82; The minimum with the control around 260×192. It's layout constraints, not a command to copy each pixel.

### 3.3. Materials, lighting and ashes

- Large shapes should be read before the small decor: valley, river, forest, stone slope, settlement grove.
- The tree has a distinct fiber direction and a warm brown range. the stone is cooler, without the same noise level; The sun is dark and matt; tissue  is blurred blue; windows and fire are local brass accents.
- The contact of the object with the ground is a separate layer: foundation, contact shadow, entrance, small yard/trace of work. The square floor, constant smoke, and night glow do not burn into the building.
- The pivot of the foot, foundation, or entrance on the control zoom does not deviate visually more than about one-eighth of the height of the visible tail. This is the observed start-up. The logical footprint always determines the simulation.
- The roof and crown can be visually hung; collision, picking and route not expanding through the drawn curve.
- There is no visible chroma halo, rectangular substrate, scale jump between frames or conflicting shadow direction.
- Each asset has a passport: source/provenance and rights, master, export, runtime ID, size, pivot, footprint, state, season, atlas, control native capture.
- Mass export of other buildings does not begin until a small set of house/stockpile/workshop/resident/conifer/rock/ground/shore/UI frames has passed DEV03.

### 3.4. UI palette, typography and icons

The initial tokens are panel `#101D27`, depth `#0B151E`, inner `#172734`, brass `#B79256`, highlight rim `#DEC27F`, primary text `#F2E6C9`, active blue `#285D92`. These are coordinated tokens, not a color sample from random JPEG.

- One nine-patch family for card/button: normal, hover, focus, active, disabled, danger.
- Cormorant is used for large names and short commands from 18 dp. The note is used for numbers, explanations, and dense Ukrainian rows approximately 15–16 dp.
- Interactive zone — at least 44 dp. Focus is visible in the light snow and in the dark.
- The hazard has an icon, a title and text; The color red itself is not a message.
- The object icons have a common light and material. are checked in the final size 28–36 dp.
- If the AI target contains unread letters, runtime capture is still evaluated by sharp native text. The RmlUi is not allowed to justify the visual style of the target.

## 4. Authoritative data UI and weather

### 4.1. Stocks, population and stock

- The upper HUD shows `inventory.available` for wood, stone and ready food and the actual number of inhabitants.
- The details are divided into `stored`, `reserved`, `carried`, `ground`. The manufactured item does not become available until the actual life cycle confirms it.
- Up to 0.26.2 population is not shown as fictitious `48/70`. In 0.26.2 capacity is derived only from the housing contract executed.
- The filling of the selected storage is shown only from the per-storage server projection: storage ID, capacity, physically stored items, occupied units. The total population, carried and ground do not replace this indicator.
- `reserved` is a marker over a physical reserve if defined by the model; It's not added to the volume twice.
- Unknown or unreceived data is displayed as «Data unavailable», rather than `0`, `200` or the placeholder with the target.
- Prior to the actual priority command, the compact card has the action of opening the inventory rather than changing the priority.

### 4.2. Weather, Forecast and Snow

- Until DEV07 production HUD does not show temperature, four-day forecast or countdown. DEV06 may have a separate diagnostic overlay, clearly signed as diagnostic, but not production UI.
- After DEV07 all weather values come only from the host-authoritative `WeatherView`/snapshot. ClientSession does not generate its own weather.
- The forecast consists of the current day and the next three calendar days. Min/max, dominant kind, precipitation summary, storm interval and countdown come from the same canonical schedule as actual weather.
- Until the full snapshot state is unknown. UI does not show fake `0°C`, zero forecast or negative countdown.
- Rain/Snow is determined by the authoritative temperature; The presentation renderer doesn't adjust the positive temperature of the snow for the sake of the target.
- The snow cover accumulates and melts through the simulation ticks, survives save/load and reconnects. FX/quality changes the number of particles, but not the condition of the roof or the effect on the work.
- Weather only affects the agreed upon Surface gather/build work. Mine, Farm, Haul, Move, recipes, health and the hunger effect do not get a hidden new modifier.
- A storm warning appears only for the actual interval, showing an accurate countdown and actual work-rate effect.
- Fuel, heating, warming, temperature death, ice and freezing routes are missing. Do not show fuel on 3 days» or warming 62%».

### 4.3. A world without man-made roads and imaginary scale

- Road is missing in the available directory, tutorial, hotkey and new commands. The server rejects the new request; The historical enum/ID is not renumbered.
- Decorative courtyards and floor tiles do not give speed bonus, accumulate traffic, or create a hidden automatic road system.
- Bridge is left. Its approaches, passing, preview and pathfinding use the same height/edge contract.
- Fortresses, walls, sieges, armies, enemies, giant, multi-level mines, waterfalls and new mines are not the target of this cycle.
- Target may imitate the density and dramatic composition of the concept, but it should not promise these systems in the playable region.

## 5. Protocol of the same comparison

### 5.1. The following is the list of the official languages of the European Union:

Each pair baseline/current and each release capture record:

- scene ID and destination: ordinary start, played settlement, staged art fixture, diagnostic or network scenario;
- build version, commit, package hash if available;
- rules/protocol/generator/save versions;
- world seed, profile, scenario, save/fixture hash;
- camera center, zoom, active layer;
- simulation tick, calendar day/time, season, weather, temperature, cover;
- resolution, UI scale, system DPI, locale;
- renderer/backend, quality, GPU/driver;
- the capture tool and the UTC/local timestamp.

If at least one parameter that changes the image or state is unknown, frames are not called accurate before/after. They can be illustrative comparisons to a clear note.

### 5.2. Comparison with target

1. Open target and native capture in the same aspect ratio.
2. First, we evaluate the large masses and the open central zone.
3. Then evaluate the anchors and the HUD fractions from the 3.2 section.
4. Next — the scale of the occupants/buildings, contact with the ground, direction of light, snow, fog and materials.
5. Separately, check the authenticity of text and numbers for snapshot/manifest, not for target.
6. Use side-by-side, 50/50 wipe and semipermeable overlay. Pixel diff is a diagnostic, not an automatic verdict: particles, water, foliage sway, and animation can legally differ.
7. For animated or time systems, add native video; One frame doesn't take weather, logistics, picking or minimums.

### 5.3. What Is Considered a Passage

| Category of the product | The Passage | Failure |
|---|---|---|
| Composition | The permanent modules are held close to their edges; The central work area is open. | A great inspector or catalog is constantly covering the world. HUD becomes a full-width table again. |
| Geometry | The relief, water, building, pivot and picking describe the same surface. | A person walks through a painted rock, a house is hanging, or a click selects an object behind a visible surface. |
| Materials | Wood/stone/snow/land is read by different masses and the same light. | The only noisy bay, conflicting shades, square bases or chroma halo. |
| Snow/weather | Cover and precipitation correspond to snapshot and change over time. | Snow is painted at Rain/+°C, the storm has no authoritative interval or UI shows a different state. |
| UI data | Any number can be traced to snapshot/model; Unknown remains unknown. | A constant from target, local random, invented capacity, or manufactured=available. |
| Typography | The native localized text is sharp, uncut, has focus/contrast. | Rust or AI- text, reduced to unreadability, danger only by color. |
| The dynamics | The unedited video confirms the movement, depth, state transition and inward. | The screenshot hides pivot leaps, incorrect drag, stale data, or fake progress bar. |
| The Evidence | Capture comes from the declared build/package and has a manifest. | Reference changed, evidence from another set or scene was released for gameplay. |

## 6. Catalogue of public target scenes

References to the generated files are added separately. Scene IDs are stable; The name or file format is not part of the gameplay contract.

| Scene ID | Version | A Target Seen | What remains unchanged | Required for Real Evidence |
|---|---|---|---|---|
| **S01** | 0.26.1 | A living valley, true forecast/°C, snow, compact HUD, compact inspector, four actions, real minimum, settlement without manual roads. | No fuel/heating, fortresses, armies, enemies, new mines; The bridge and the supported demo loop are left. | 720p/1080p/1440p native captures; summer/winter day/night; storm transition video; same-seed before/after; save/net/reconnect, UI/render/weather tests; packaged-like D3D12 launch. |
| **S02** | 0.26.2 | Housing occupancy/capacity (`зайнято / місткість`), reservation of expected arrival, House/House II as the visible cause of growth. | The art world S01; No families, no births, no furniture, no heating. | The frames 10/10, after House, after House II, pending arrival; the video/log of two simultaneous requests; settlement/save/network/UI tests. |
| **S03** | 0.26.3 | The farm inspector sees the mode of preparing food or raw materials, workers, exit and safe movement. | There's no new cook profession or new farming engine; art direction S01. | Native footage of both modes, short video of switching during the work/transfer, save/load and host/guest, economy/UI tests. |
| **S04** | 0.26.4 | A clear supply of ready-to-eat food and an honest estimate of time without future revenue; It's a balanced day/night. | Carried does not become available; No raids or new night events. | The start and end frames of two cycle runs, unassembled time-lapse/video key moments, delivered/consumed/hunger metrics, balance tests on two control cards. |
| **S05** | 0.26.5 | In UI the route reads grain → flour → kitchen → food; the wood and the full work are counted; The kitchen/mill makes sense. | There is no quality food, no vitamins, no spoilage, no new products. | Full production chain frame, video shipments and recovery after wood loss, paired benchmark +20% target or honest failed result, economy/save tests. |
| **S06** | 0.26.6 | You can see the general stock, the specific warehouse, carried/ground/reserved, the delivery point, the bridges and the maintained mine passage. | No manual routes, caravans, priorities for inventory, new metals and roads. | The two components have different content, bridge/shaft, video delivery near/far, save/load, path/logistics/storage UI tests. |
| **S07** | 0.26.7 | The short RmlUi tutorial prompts run through collection, construction, work, food, housing, reception and Level II; The world remains visible. | No modal windows, expeditions or encyclopedias. | Full unassembled first-session recording of two new players, capture each type of prompt, event-completion logs, localization/UI tests; A separate record of interest and blockers. |
| **S08** | 0.26.8 | Unobtrusive last successful save indicator (`останнє успішне збереження`), clear persistent recording error and backup recovery. | Manual Save/Load are left; without cloud saves and a great slot manager. | Capture success/error/recovery, video without a visible stall, measured save time, corruption/write-denied tests, packaged save/load. |
| **S09** | 0.26.9 | The clear connection/waiting/recovering/lost/version mismatch of the surface of the same world; I've got control back. | No host migration, relay, matchmaking and Steam invites. | Synchronized frames of two physical Windows PCs, 60-minute video/log, disconnect during delivery, reconnect, host restart, cross-network evidence, network regressions. |
| **S10** | 0.26.10 | The same accepted world without new content; The final readability is 720p/1080p, a clean menu and package identity. | The mechanics are frozen; There's no last little feature. | Capture only from unpacked ZIP/launcher, build/commit/hash, Ukrainian path, Save/Continue/Host/Join, full configured CTest and package checks. |
| **S11** | 0.27.0 | The same accepted demo loop and visual language S10; Just fixed release blockers. | No new building, enemy, resource or redesign before release. | The final package hash; two independent coop-pairs and one solo tester; Two hours without a new blocker; repeat all the stitched scenarios. Generated target does not support external demo. |

If S10 and S11 are visually identical, this is the correct result: 0.27.0 is a confirmation of the candidate, not a mandatory redesign.

## 7. Currently visible stages V00–V05
The order and evidence is given by [GRAPHICS_PROGRESS_UA.md](GRAPHICS_PROGRESS_UA.md). V00 — actual baseline; V01 changes to UI; V02 terrain/generation; V03 building/resident art; V04 adds rain and weather data; V05 — snow/winter. The previous table DEV01–24 is moved to the technical map V2 and is not an order to make Dynamic to UI.

## 8. Stop/go current branch
- V01 does not pass if the new HUD has lost the command in force, covers the world with a transparent panel or displays fabricated data.
- V02 needs a coordinated path/build/picking, generated and compatible; The new coastline cannot make the available resources unavailable.
- V03: before mass export, check the small kit in the current renderer. Full Dynamic proof is no longer a prerequisite for basic art; lit variants — separate future production.
- V04/V05: the numbers and FX come from authoritative weather. Screenshot does not prove time, accumulation, meltdown, pause or reconnect.
- Dynamic disabled does not block the basic V- steps. Its enabled mode in the future is accepted under the [DL- plan](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md), with moving sources and measurements.
- The demo also needs economy/tutorial/save/Windows/co-op gates; The new PNG does not mean completion0.27.0.

For each step: build/seed/camera/zoom/resolution manifest, native before/after, video of timing interactions, relevant tests and a list of rejections. Unconfirmed remains unconfirmed.

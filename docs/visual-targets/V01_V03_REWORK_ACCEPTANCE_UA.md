# Ribterra — acceptance checklist of the complete recycling V01–V03

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Date of establishment and verification: 30.09.2026. The document status: ** periodic verification; The total score is PARTIAL**.

This document defines one cumulative final state after V03: **HUD V01 + world V02 + objects V03**. The `[ ]` symbol means «not yet accepted», even if the code, test, or old screenshot already exists. The mark can only become `[x]` after a specific evidence path, command, or record is entered.

The proofs of **T/M/A/R/P** are decoded in [ light of re-reception](../verification-rework-0.26.3-uk.md). `PASS` is only for a particular row; `PARTIAL` means the result available with the indicated unfulfilled residual; `NOT RUN` is not replaced by the assumption of workability.

## 1. Source and extent of the interpretation

- The main artistic direction: the original reference image of the user; It sets the composition, scale, hierarchy, materials, and readability, but it doesn't invent the gameplay.
- Local step directions are: [V01 HUD](progress/v01-ui.png), [V02 terrain](progress/v02-terrain.png), [V03 objects](progress/v03-objects.png). These are generated targets, not runtime evidence and not pixel clarity requirements.
- The technical contract is `GRAPHICS_PROGRESS_UA.md` and `.omx/plans/2026-09-28-graphics-updates-implementation-ua.md`.
- Actual data, available commands, collision, pathfinding, save, and network authority prioritize over the decorative similarity.
- V01–V03 does not include temperature, forecast, rain, wet, snow, snow-cover simulation or Dynamic Lighting; These are the next individual stages.
- Old public targets **S01 weather / S02 housing / S03 farm** are not renamed and are not considered part of this V01–V03 rework. Their relationship to versions is waiting for individual clarification from the user.

## 2. The top layer cumulative gate

| Gate | The final state criterion after V03 | The status | Evidence |
|---|---|---|---|
| G1 | HUD corresponds to the compositional hierarchy of reference, leaves the center of the world open and shows only true data | [ ] PARTIAL | M/P: the composition HUD is reworked; The artistic correspondence and full UI acceptance are still open. |
| G2 | F1 completely hides and returns HUD without changing selection, pause, simulation or camera | [ ] PARTIAL | M/P + ui: hide/restore and repeat verified; The full invariant placement/command stream is not separately derived. |
| G3 | The world reads like a valley: one river, one bank, one passage, one vast material area, and a grouped nature. | [ ] PARTIAL | A/M + terrain_v02: valley and connection; The great winter material limits are still visible. |
| G4 | The real routes of the units gradually form visible protoplated paths; They're stored and synced, but they don't give a speed bonus. | [ ] PARTIAL | terrain_v02/save: wear from real movement; There's no native video storage and a separate dual-client wear comparison. |
| G5 | Camp + all 11 available buildable types have a harmonised construction/ready/winter state; House II and Workshop II have a true level-II look | [ ] PARTIAL | A + v03_asset_passport: coverage/II/winter is; The final artistic consistency is not accepted. |
| G6 | The beam is readable in 8 directions, carries actual load and is in correct contact with the surface | [ ] PARTIAL | R + unit_motion: 256 frames/actual deliveries; all-facing/painter-order The video is missing. |
| G7 | The same certified seed/save leads the cumulative scene V03 in native D3D12 rather than imagegen or software probe | [x] PASS | M/A/R: certified native D3D12, seed/save/camera/zoom in manifest; The staged fixture is clearly marked. |
| G8 | There are no gameplay regressions, save/load, local host/join, picking, UI input, package staging and old supported saves | [x] PASS | T: 91/91 on the final set + A/R/P; the result is limited to the verified local set, not physical/cross-network. |

**Stop condition:** V01, V02 or V03 is not called `PASS`, until all the mandatory points of the relevant section and its evidence rows have `[x]` and a reproduced proof.

## 3. V01 — HUD and control

| ID | Extreme or behavioral criteria | The status | Evidence |
|---|---|---|---|
| V01-01 | Upper left is the available wood/stone/food and population; tooltip explains available» | [x] PASS | ui + P: available resource counts and population are taken from ClientSession; Accessibility explains stocks. |
| V01-02 | A resource click opens stocks with split stored/reserved/carried/ground/manufactured; population opens settlement | [x] PASS | ui/settlement_ui/building_inspector + P: resource click, authority stock categories; population routing. |
| V01-03 | Upper right is at most one effective warning with the real cause and the next action; No imagined temperature or forecast | [ ] PARTIAL | M + game_ui.cpp: one real warning, transient feedback; The longest localized warnings are not covered by the entire matrix. |
| V01-04 | Calendar and Surface/Mine remain available and do not override resources or warning | [x] PASS | M: calendar/Surface/Mine in all 60 captures; The Polish Surface is fixed. |
| V01-05 | The lower left compact inspector shows type/name/level, the condition, the main cause and the next step; `Деталі` (Details) opens the full inspector | [x] PASS | building_inspector + M: summary, level/status/next step, Details; It's a real thumbnail resolver. |
| V01-06 | Mixed/multi-selection shows count and only joint actions, not copying the state of the first object | [x] PASS | building_inspector: group/mixed selection and common actions; No invented common resources. |
| V01-07 | The following action strip contains Build (`Будувати`), Gather (`Збирати`), Assign (`Призначити`), and Cancel (`Скасувати`); disabled state has a visible specific reason | [x] PASS | ui/input_feedback + M: four actions, disabled reason for keyboard focus. |
| V01-08 | `Скасувати` (Cancel) only ends placement/worksite/drag; does not destroy the building, production queue or resident order and does not emulate escape | [x] PASS | ui/client_input: typed CancelInteraction, restore selection to drag; No delete/queue reset. |
| V01-09 | Pause, 1×/2×/ 4×, stocks, enterprises, save/load, help, menu, chat, layer and ownership/order controls are available | [x] PASS | ui/profession_ui/production_control_ui/settlement_ui + P: the existing controls/routing are preserved. |
| V01-10 | F1 hide/show works on the edge-trigger: the hold does not switch off again; return restores the full HUD | [x] PASS | ui + M/P: F1 repeat=true does not switch over again, hide/restore confirmed. |
| V01-11 | Hide/show HUD does not change selection, placement state, camera, pause/speed or authoritative command stream | [ ] PARTIAL | P: pause/time/resources/camera is the constant in the manual scenario; Not all placement/selection/authority invariants are automated. |
| V01-12 | Navy/nine-patch panels, ivory text, gold 1–2 px border, blue active and red warning are read; hit area ≥36 dp | [ ] PARTIAL | M: navy/ivory/brass and focus; ornate nine-patch and substantial ≥36 dp verification not completed. |
| V01-13 | Hover/focus/pressed/disabled are not just different in color; keyboard focus and IME/chat do not break | [ ] PARTIAL | ui: keyboard focus, disabled reason, text/IME/chat; The full visual state matrix is missing. |
| V01-14 | In the normal state, union HUD panels occupy no more than 30% of the logical viewport; The center of the scene is open. | [ ] NOT RUN | The center is open in M; union area of all panels not measured — number ≤30% not confirmed. |
| V01-15 | Click/drag/wheel over HUD do not rule the world; Escape first closes the top panel | [x] PASS | ui/client_input + P: UI consumes pointer; stocks→Escape closes the panel and does not change the pause. |
| V01-16 | 50 cycles open/close details after warm-up do not increase texture count | [ ] NOT RUN | UiLifecycleTests checks other lifecycle errors, not 50 cycles texture-count inspector. |

## 4. V02 — valley, surface, river and protoplated trails

| ID | Extreme or behavioral criteria | The status | Evidence |
|---|---|---|---|
| V02-01 | The new profile creates one continuous river with one water level; No cracks, insulated sludge or water on land | [ ] PARTIAL | terrain_v02: the relationship of the river to 10 seeds; A full review of all water-level transitions has not been completed. |
| V02-02 | The shores have seamless transitions to chunk edges and negative coordinates; foam/waves not painted over the bridge deck | [ ] PARTIAL | terrain_v02: shared UV edges/negative coordinates; bridge foam mask in code; native bridge/shore video is missing. |
| V02-03 | The earth has wide readable grass/soil/rock zones; The contrast is slightly lower than the contrast of buildings and people. | [ ] PARTIAL | M: new calm meadow and bounded mips; The winter material boundaries remain visible. |
| V02-04 | Trees, rocks, bushes, and wild food form groups, not a uniform noise and not a visual impervious wall | [ ] PARTIAL | M: combined nature and jitter; The density/biomatic stylisation is still different from the concept. |
| V02-05 | There is a level starting point, ≥80 reachable buildable cells and reachable wood/stone/food/clay/mine for 10 fixed seeds | [ ] PARTIAL | terrain_v02: 10 seeds, ≥80 cells, wood/stone/food/mine PASS; There is no separate clay assertion. |
| V02-06 | Renderer, movement, building preview/server validation, picking, resident feet, entry cells and bridge approaches use common surface geometry | [x] PASS | A: 1089 terrain picks, 3 zoom checks, uphill movement; terrain_v02/client_input: shared geometry. |
| V02-07 | The unit does not pass through a visible cliff, does not hang over the ramp, and does not collapse under the surface | [ ] PARTIAL | A: uphill click and movement PASS; All cliff/ramp/bridge occlusion cases are not shown in the video. |
| V02-08 | Real traversed cells accumulate bounded wear and gradually become a visible pathway. Unused cells don't get dark on their own. | [ ] PARTIAL | terrain_v02: real movement creates bounded wear; The time-lapse without motion is not recorded. |
| V02-09 | Wear is the authoritative state: deterministic, save/load roundtrip, checksum and host/guest snapshot/delta coincide | [ ] PARTIAL | terrain_v02/persistence/protocol: field/save/checksum; dedicated wear host/guest delta comparison is still needed. |
| V02-10 | A protopted track does not change speed, collision, path cost or buildability | [x] PASS | terrain_v02: arrival_tick the same, trail wear presentation-only; path planner regression. |
| V02-11 | New handheld Road is missing at catalog/hotkey/help/command; legacy Road ID and old-save object saved without speed bonus | [x] PASS | terrain_v02/alpha_rules/ui: Road ID=19 is preserved; new Road rejected, Bridge available. |
| V02-12 | F1 and all accepted HUD V01 work without regression in the new world | [x] PASS | M/P/ui: V01 HUD and F1 are running on V02 generation; The return doesn't regenerate the world. |
| V02-13 | The old save does not reproduce the recorded chunks of the new profile and keeps the old terrain hash | [x] PASS | terrain_v02/save_compatibility: the generator 5 roundtrip stores checksum, recorded chunks not re-generated. |
| V02-14 | Native video shows movement along the coast, across the bridge and the buildup of trails; The screenshot itself does not count | [ ] NOT RUN | There is no uninstalled native video bridge/shore/desire-path accumulation; screenshots are not recorded. |

## 5. V03 — small art kit, all available buildings and residents

| ID | Extreme or behavioral criteria | The status | Evidence |
|---|---|---|---|
| V03-01 | Initially adopted small kit: House, Stockpile, Workshop and one resident on flat and slope V02 in the same calibration scene | [ ] PARTIAL | A: calibration/slope/zoom the frames of a small kit are; The separate artistic reception kit is not closed until expansion. |
| V03-02 | For the small kit fixed pivot/feet anchor, entry reference, silhouette bounds, projected size and scale on zoom 0.62/1.30/3.00. | [x] PASS | A + passport/building_visual: anchors/entry/bounds, zoom 0.62/1.30/3.00, and picking. |
| V03-03 | After a small kit the style spread to Camp + 11 buildable types; Temple/Barracks and other locked types remain locked | [ ] PARTIAL | v03_asset_passport/alpha_rules: Camp+11 covered, locked types not open; the gate of the little kit yet PARTIAL. |
| V03-04 | Each available family has stage1/stage2/stage3/ready/winter without missing sprite, magenta fringe or anchor jump | [ ] PARTIAL | A/v03_asset_passport: 5 states without missing IDs; all-family alpha-fringe/anchor visual acceptance is incomplete. |
| V03-05 | `.level2` only select completed House II and Workshop II; Upgrade to completion shows current level and progress | [x] PASS | building_visual/upgrades/upgrade_flow: only completed House II/Workshop II select level2. |
| V03-06 | Winter does not return House II/Workshop II to level I; a separate plate or documented overlay/tint retains the correct silhouette | [x] PASS | M winter + A/building_visual/passport: individual House II/Workshop II winter plates are stored II. |
| V03-07 | The visual scale does not alter the simulation footprint, entry cell, collision or isometric projection 58×29 | [x] PASS | building_visual/client_input/A: scale 1.35 presentation-only, bridge/Road exceptions, footprint/projection not changed. |
| V03-08 | Renderer, inspector thumbnail and picking use one visual resolver and the same actual level/scale | [x] PASS | A/M/building_inspector: single resolver, exact stockpile picks, thumbnail/level state. |
| V03-09 | Resident coverage: 2 looks × 4 load states × 8 directions × 4 phases; anchor stable at turn and motion | [ ] PARTIAL | A/R/unit_motion: 256 frames and anchors; All spins in the continuous native video are unverified. |
| V03-10 | `walk/logs/sack/cart` is derived from the actual item+quantity; After delivery/cancel the load disappears | [ ] PARTIAL | R: actual freight/two deliveries/save-resume PASS; cancellation and all item-to-cart options separately are not taken into account. |
| V03-11 | Contact shadow world-anchored under feet, painted before the sprite; The cart has a wider contact, but it's not directional sunlight. | [x] PASS | unit_motion/R: feet contact before sprite, wider cart shadow; It's not directional lighting. |
| V03-12 | Resident goes in/out building/tree/bridge/cliff without the wrong painter-order overlap | [ ] PARTIAL | A/R: slope and representative objects; before/after all tree/bridge/cliff unverified video. |
| V03-13 | Smoke/fire/tool signal only appears when the actual active job is in the Working phase | [ ] PARTIAL | work_client_rendered/code: Working gating is saved; There is no complete native matrix inactive/paused/job phase. |
| V03-14 | Stockpile piles are displayed only in the presence of bounded authoritative binding; Otherwise, no false stock indicator is added | [x] PASS | M/code: no false quantitative piles have been added; static art is not declared as inventory. |
| V03-15 | Atlas/passport contains stable ID, source/license, dimensions, pivot, alpha mode, family/state/season, hash and notice | [x] PASS | v03_asset_passport/asset_catalog/asset_staging: IDs/dimensions/pivot/hash/license/source/notice; The provenance prompts are saved. |
| V03-16 | Native video shows 8 facing, each cargo type, delivery, pause, 4× and movement from both sides of the building | [ ] NOT RUN | 256-frame sheets and delivery PNG are available, but this is not unassembled video facing/cargo/pause/4×/occlusion. |

## 6. The required UI visual matrix

Each cell represents a separate native capture with an open representative inspector and the longest localized text; No cell inherits PASS from another cell.

| Resolution \ UI scale | 0.85 | 1.00 | 1.15 | 1.30 |
|---|---|---|---|---|
| 1280×720 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |
| 1920×1080 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |
| 2560×1440 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |

| Locale | Resource strip | Warning | Inspector/details | Actions/controls | The status |
|---|---|---|---|---|---|
| uk | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| en | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| ru | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| pl | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures, Surface fit fixed | [ ] PARTIAL |
| es | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |

- [ ] NOT RUN — OS DPI 100/125/150% input mapping separately from the UI scale.
- [ ] PARTIAL — representative visible rects examined; There's no automatic hit-rect overlap test for each cell.
- [ ] PARTIAL — RenderFrameTests software/GPU checks the resize and letterbox coordinate roundtrip; the full set click/drag/wheel/tooltip in each partition is not complete.

M contains 60 ordinary + 60 staged captures, one winter and two F1 — together 123 PNG. `CAPTURED` is not an automatic `PASS` of all visual/input conditions.

## 7. Regression and evidence matrix

| Area | Minimum evidence | The status | Evidence path / command |
|---|---|---|---|
| Source hygiene | scoped diff, `git diff --check`, without random mixing weather/DL | [x] PASS | git diff --check exit0; only CRLF warnings; unrelated dirty-tree changes are saved. |
| Focused tests | UI/input/localization; terrain/path/build/save/network; art/resolver/animation/atlas/staging | [x] PASS | T: all the tests listed have been passed; Additionally RenderFrameTests --gpu, A/R/M native D3D12. |
| Full suite | Full configured CTest on the final executable/assets | [x] PASS | ctest-certified.xml: 91/91, 0 failures, 674.79 s. The previous postfix with 2 failures is stored as history. |
| GPU | D3D12 hardware metadata, `software=false`, exact executable hash | [x] PASS | A/R/M: D3D12 RTX3050, software=false; executable hash in build-evidence.json. |
| Native appearance | same seed/state/camera/zoom/resolution before/after for each V-step | [ ] PARTIAL | M: equal to seed/state/camera in certified fixtures; There are no complete before/after single V1/V2/V3 binaries. |
| Native interaction | Unassembled V01 action/F1 V02 bridge/path wear, V03 movement/cargo videos | [ ] PARTIAL | P: real native control; The mandatory unedited videos are not recorded. |
| Save compatibility | current roundtrip + supported frozen old-save copy; The original is not transcribed | [x] PASS | save_compatibility/terrain_v02 + R: frozen/current copies; The user originals are unchanged. |
| Network | local host/join see wear, cargo, levels and authoritative UI reasons alike | [ ] PARTIAL | local process/protocol/adverse tests in T; special native comparison wear/levels/cargo of two clients not closed. |
| Package | fresh output directory, manifest/hash, package-only launch and staging validation | [x] PASS | P: fresh rework package, 158 staged assets, exact match exe hash, manual new-world/F1/Escape smoke. |
| Physical scope | two physical Windows PC/cross-network marked separately; Local loopback doesn't replace them. | [ ] NOT RUN | Two physical Windows PCs and cross-network have not been launched; The loopback is not equal to them. |
| Performance | cold generation, frame/compose, texture bytes and memory compared to baseline on the same scene | [ ] PARTIAL | Cold generation <5s test; 310.01/411.68 MiB texel payload, budget not accepted; p95/p99/VRAM baseline is missing. |

## 8. The Final Solution

- **PASS V01:** all V01 rows, UI matrix and V01 evidence completed; The world pixels/commands is not changed outside the permissible HUD.
- **PASS V02:** PASS V01 is preserved; All V02 rows and native river/bridge/path-wear evidence completed.
- **PASS V03:** PASS V01+V02 is preserved; All V03 rows, Camp+11 coverage, cargo/motion video, package and regressions are complete.
- **PARTIAL:** is code or separate tests/captures, but at least one mandatory row remains `PENDING`.
- **FAIL:** runtime contradicts authority/scope, regressions the previous stage accepted or evidence reproduces the defect.
- **NOT IMPLEMENTED:** there is no runtime diff and no native proof; The generated target or document does not change that status.

The current verdict of this document: **PARTIAL — full artwork has not yet been accepted**. Specific remnants: artist/style/detail parity, winter material boundaries, longest-text/DPI/hit-rect matrix, native videos, texture lifecycle and performance/VRAM budget. The document does not announce a public release or change status S01/S02/S03.

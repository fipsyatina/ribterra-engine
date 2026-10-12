# Full graphics engine first — mandatory acceptance threshold

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Date: 05.10.2026. Status: **ENGINE IN PROGRESS / NOT ACCEPTED**.

CPU follow-up 09.10: the [neutral XYZ draft](GAMEVIEW_NEUTRAL_EXPORT_CHECK_2026_10_09_UA.md) passed two actual exporter runs and an independent review. Four neutral collections are byte-identical and the old mode is preserved; 19 649 pixels of the old house are still not covered by the draft geometry. Reviewed C++ changes were applied with exact archives; fresh strict/native outline, picking, and cold-boundary evidence remains pending. This is part of E13; all E01–E17/P01 and the complete target remain OPEN/ACTIVE.

Newer bounded checkpoint08.10: [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md), fresh build02 and native01–02 actual exit0; 42GPU PNG+raw+JSON (44/44 exact). Diagnostic hour/cloud/haze jointly control Sun/Moon/ambient/atmosphere; source-specific shadows, horizon on/off, and causal local/emission were verified. Normal integration, authoritative weather, native input/video, concept fidelity, and ≥120FPS are not accepted. **E06 and all E01–E17 OPEN.**

Partial checkpoint 08.10: [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) was checked by two native GPU runs in the technical GameView scene. This does not close E09 or any E01–E17 item; normal startup, the shared scene, raw coverage/AA, input/video, concept art, and 120 FPS remain open.

Newer follow-up: [raw binary carrier / odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md) passed native05–06; binary packing and the specific rollback were proven. This is not intermediate AA or a complete E09. AllE01–E17 remain OPEN.

Next bounded checkpoint: [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_CHECK_2026_10_08_UA.md), fresh build03 and native02–03 exit0, 26 PNG + two raw downloads + JSON (29/29 derived outputs byte-exact). Camera-relative depth, height falloff, coast proximity, diagnostic time/weather, disabled identity, UI/reveal, and the 32/33-region transaction were verified. This is not authoritative weather, full-world/video/performance evidence, or concept art. **E10 and all E01–E17 OPEN.**

The user's direct requirement is to continue game development only after everything required by the engine is fully implemented and the smallest visible details are checked against the concepts. This order takes precedence over older plans that deferred Dynamic or allowed the HUD/art catalog to expand first. The earlier V01–V03, F1, and movement-worn path requirements are not cancelled; their further implementation waits for this threshold.

## Scope of the current work

Allowed changes: renderer, GPU resources, shaders, materials, verification tools, a minimum control asset set, presentation adapters, and integration of this engine with the real GameView. The integration is required to prove that the engine works, not to add game mechanics.

Until the engine is accepted, do not continue game mechanics, generator/resource balancing, save/network expansion, the new building/character catalog, or further HUD design. Preserve existing changes and previews. Do not switch the launcher to an intermediate shader probe. The user's next direct addition—practical optimization, graphics settings, high FPS, and smoothness—belongs in the engine phase (E16–E17), rather than being deferred to game development.

Comparison basis: [winter](images/target-0261.png), [rain](images/target-0261-rain.png), [storm](images/target-0261-storm.png), [summer world](images/target-0262.png), [farm/residents](images/target-0263.png). Later direct requirements add causal resident lighting, dawn/water fog, and moonlight. Static concepts define the artistic appearance; motion, weather changes, and lighting response are checked separately in video.

## Engine acceptance checklist

New partial checkpoint08.10: [automatic local lighting of windows](GAMEVIEW_WINDOW_LIGHT_CHECK_2026_10_08_UA.md). Strict GameView build10 exit0,27zero-command-sidecars/96source/8protected/69shader/52kit bindings; native06/07 each root-run exit0,13PNG1920×1080+JSON,14/14files byte-exact. Source on/off causes both emission and lighting changes in real ground/typed resident; independent red/blue blocker controls,6CPUcamera cases and7+6transactional cases passed. Native exit sidecar not preserved; GPU timing/FPS/video is not measured. It's neutral opt-in control, not concept art or normal startup. **All E01–E17 OPEN.**

Next partial checkpoint08.10: [bounded real GameView control](GAMEVIEW_ENGINE_CONTROL_CHECK_2026_10_08_UA.md), fresh strict build05 exit0,21productionTUs+probe,96source/8protected hashes, native01/02 each exit0,5PNG1920×1080+JSON,6/6repeat files byte-exact. 289realterraincells/8neutralcontrols,53 850caster-dependent pixels,Sun/Moon, hidden-object filtering, and 8cell-picks. Normal GameView/launcher was not switched; fullcamera/worldobjects/localpixelproof/concept/FPS/CTest were not accepted. All E01–E17 remain OPEN.

Current authored native checkpoint 08.10: [real authored GPU path](AUTHORED_SKY_NATIVE_CHECK_2026_10_08_UA.md) passed two runs with exit0,13 checks/32PNG each,32/32repeat PNG and 54/54derived files byte-exact. Fresh strict build10 and focused verifier — exit0;104 source entries/12 protected anchors unchanged until the separate next GameView integration. Previous native01–04 were unsuccessful diagnostic attempts, not accepted checkpoints. It is a limited fixed-camera component, **not** the full engine, concept, or closure of any E-item. Clearing 82,79GB of historic runtime copies does not change engine acceptance; the current evidence/launcher is preserved.

Previous authored checkpoint08.10: [loader/adapter proof](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md),41 exporter negatives,13 focused groups×2/19 schema invalids/13 transactional failures,22 material regression PNG,103 source hashes, and12 prior evidence files unchanged. Authored GPU shadow pixels were NOT_RUN at that point; the new limited native evidence appears above. All E01–E17 remain OPEN, and game development did not resume.

Partial directional-shadow checkpoint08.10: [independent D32 Sun/Moon maps](SKY_SHADOW_CHECK_2026_10_08_UA.md),110CPUchecks,12nativegroups×2,27/27repeatPNG,26Sky/31Contact/56shader byte-exact regression, transactional targets, and actual cross-slot controls. This is only a fixed-camera prerequisite; the authored adapter was started separately and is not yet accepted. **E01–E17 OPEN; concept fidelity, full GameView, and ≥120FPS are not confirmed; game development waits.**

Partial Sky direct checkpoint07.10: [independent moon channel](SKY_DIRECT_CHECK_2026_10_07_UA.md) in an explicit opt-in graph, with native causal sun/moon pixels on ground/typed fixtures and unchanged old shaders/contact captures. This is not yet directional shadows/time/weather/art or full-engine/FPS acceptance. **All E01–E17 remain OPEN; game development waits.**

Partial Contact GPU checkpoint07.10: [versioned contact graph + immutable R8 registry](CONTACT_GPU_CHECK_2026_10_07_UA.md),211CPUassertions,actual shader positives/repaired negatives,53nativechecks×2 and31/31 byte-exact repeated PNG, fresh selected-renderer software/D3D12 regression, and independent partial approval. This is not raw-W/video/art/concept/fullGameView/performance acceptance. **All E01–E17 remain open; game development did not resume.** The older CPU-only records below are historical.

Every item below remains **OPEN**. Partial code or earlier test reports do not mean that an item is complete.

First partial step: [native verification of the bounded surface-lighting core](SURFACE_LIGHTING_CHECK_2026_10_05_UA.md), 4/4 targeted tests, D3D12 proof, and legacy regression. This does not fully close E01/E02/E04 and is not performance proof.

Next partial step: [presentation settings, GPU-resource reuse, and local profiling](PRESENTATION_OPTIMIZATION_CHECK_2026_10_05_UA.md), 10/10 targeted tests, native failure/visual checks, and a separate ETW trace. The 145.16 CPU-loop FPS result without tracing applies only to the static legacy scene; in the Immediate/HUD trace, valid display intervals occurred at 118.72 Hz in a mode that allowed tearing (`AllowsTearing=1`, not evidence of actual tearing), with explicit methodology limits. This **does not** close E16/E17, replace the full Dynamic scene, or change any status below.

New partial step 05–06.10: [linear HDR world / Composite / unchanged UI](HDR_COLOR_CHECK_2026_10_06_UA.md), 10/10 targeted tests, 34 native HDR captures, pixel-checked resize rollback of both frame slots, 3/3 supported presentation modes, and 15/15 byte-identical Legacy regression images. This is only the color/resource foundation in the production renderer; it is not the complete six-pass graph, Dynamic materials, shadows/atmosphere, or ≥120 FPS proof. **E01–E17 remain open; game development waits for engine acceptance.**

New partial step 06.10: [typed material bridge / schema 2 / shared projection](MATERIAL_BRIDGE_CHECK_2026_10_06_UA.md), 13/13 targeted tests, 22 native analytical material captures, verification of sRGB filtering, normal/depth/emission, atlas edges, and candidate rollback. This is a prerequisite for a real art kit, not a substitute for one: the current GameView remains BakedCompat, and the full graph/shadows/atmosphere/120 FPS are not accepted. **All E01–E17 remain open.**

Next partial step 06.10: [editable geometry / deterministic exporter / direct GPU P/N](MATERIAL_GEOMETRY_CALIBRATION_CHECK_2026_10_06_UA.md), 15/15 selected CTest, 72 interior geometry samples, and 180 production color samples. Maximum native reprojection error is 0.3121 px in this 1280×720 control matrix. The diagnostic cabin/slope is not a concept-ready house or a complete neutral kit; the MRT probe is not a production render graph. **No E01–E17 item is closed.**

New partial step 07.10: [production GroundGBuffer → GroundLighting → WorldForward → Composite → UI](GROUND_FORWARD_CHECK_2026_10_07_UA.md), 16/16 selected CTest, 34 native PNG, causal sun/point/spot/emission controls, ±32 precision, two-set cross-slot equality, strict 4:3 resize, and three transactional allocation faults. The Legacy 15/15 PNG remained byte-identical. This is an opt-in production slice, not Reflection/FoW/shadows/atmosphere or a concept-ready kit; normal GameView remains BakedCompat. **All E01–E17 remain open; ≥120 FPS at 1080p was not measured.**

New partial step 07.10: [owner-aware local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md),16/16 selected CTest,49 Local PNG,34 Ground regression PNG, independent review, and15/15 byte-exact Legacy PNG. Authored OBB/capsule prisms block only the corresponding local source; owner/reveal/mask and two-slot buffer recovery were verified. This is not sun/moon/contact shadows, a complete graph, or concept-ready GameView. **All E01–E17 remain open; ≥120 FPS was not measured; game development waits.**

Next partial CPU checkpoint07.10: [Contact CPU contract](CONTACT_CPU_CONTRACT_CHECK_2026_10_07_UA.md), clean standalone MSVC C++20/W4/WX,157CPUassertions, and bounded independent approval. The typed metadata/geometry validator and versioned receiver design are not connected to the GPU. Registry/art/adapter/projection/shader/renderer/native pixels/motion/performance are still missing. **This closes no E-item; the full-engine gate and 120FPS remain open.**

| ID | What must actually work | Observable criterion |
|---|---|---|
| E01 | One coordinate contract and safe GPU resources | Consistent world XYZ, normal, projection, viewport/DPI, and input; verified shader ABI and formats. Resize/minimize/restore, frame fences, and allocation/upload failures do not leave stale or partially created resources. |
| E02 | Production render graph and color | Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite/FoW → UI passes follow the Core spec. Linear HDR lighting; one sRGB decode/encode; correct alpha; no reading from the active render target or repeated attenuation of emission. |
| E03 | Real terrain surfaces | Flat ground, slope, cliff, shore, and bridge have correct heights/normals. Lighting, shadows, feet/picking/build-preview use the same surface. Pan/zoom does not shift them relative to the world. Map-generation quality will be checked later. |
| E04 | Lighting for every visible category | Ground, facades, roofs, trees, residents, and cargo respond per pixel to ambient, directional sun, and point/spot sources. Moving a lamp changes the lit side without changing albedo. A uniform screen tint does not count. |
| E05 | Windows, fire, and forge | A separate emission mask and world-anchored source. Switching it on/off changes both the source and its lighting on suitable adjacent surfaces. Radius, cone, falloff, and occlusion by objects work; the light patch is not painted into the PNG. |
| E06 | Time, Sun, Moon, and weather | A smooth day → sunset → night → dawn sequence changes light and shadow direction/intensity/color. Clouds/storms weaken daylight. The Moon provides a separate controllable cool contribution; windows remain warm and the HUD is not tinted. |
| E07 | Contact, directional, and local shadows | Buildings, trees, residents, and cargo have appropriate shadows. Movement and light direction change the silhouette. A moved blocker occludes only its corresponding source; two differently colored sources are checked independently. No leakage reaches roof/water/UI/unknown areas beyond the permitted AA edge. |
| E08 | Materials and wet surfaces | Normal/material/depth/emission channels are actually used. Dry → rain → drying changes roughness/specular only on suitable surfaces; highlights depend on light rather than painted spots. No highlight appears on the UI or an inappropriate receiver. |
| E09 | Water, shores, and reflections | World-anchored flow/ripples, foam near the shore, and a consistent lighting highlight. Limited, weaker/distorted reflections disappear with their caster and do not cover the shore, bridge, or unknown areas. Pan/zoom/pause does not break anchoring. River shape is the generator's next phase. |
| E10 | Spatial atmosphere | Haze separates near and distant planes; morning and coastal fog are distinct. Density/color match time and weather inputs. This is not a grey curtain or a replacement for Fog of War. Hidden-object silhouettes do not leak. |
| E11 | Rain, snow, wind, and storm | Particles, gusts, and their interaction with receiver/reveal work in motion. Positive rain does not generate snow. Pause stops effects, 2× changes time consistently, and reset/epoch does not cause a burst of accumulated particles. Smoke and vegetation have a consistent wind response. |
| E12 | Snow materials and tracks | Visual coverage appears on the correct surfaces; L/R footprints follow the movement actually shown, including turns, fade/burial/melt. No stamps appear while stationary/teleporting or on forbidden receivers. No hidden changes to path, elevation, or speed. |
| E13 | Material pipeline and minimum art kit | Control terrain/water/bridge, house, warehouse, workshop, tree, resident, and cargo have consistent neutral channels, pivots/anchors/occluders. The atlas and passport verify projection, alpha, color space, and required maps; an incomplete Dynamic material is rejected transactionally. The mass catalog is not produced yet. |
| E14 | UI subsystem without lighting damage | A separate unattenuated UI pass; nine-slice, icons, Ukrainian font, scissor/clip, DPI/hit mapping; readable at 720p. F1 hides/restores the entire HUD without changing the world. Further HUD composition/game content comes after the threshold. |
| E15 | Shared production verification | All listed effects run together in the same production renderer, not in a separate showcase image. Raw captures and unedited video, side-by-side concept comparison, independent review, counters, frame-time/VRAM, and failure checks. No mandatory category is silently disabled. |
| E16 | Real graphics settings | Low / Medium / High and Custom; available manual controls change real GPU effects. VSync, frame limit/uncapped, screen mode, resolution/render scale, and shadow/light/water/fog/particle/detail quality. Requested/effective values are shown; unsupported options are not presented as enabled; apply/revert/persistence/restart/resize work without a broken frame. |
| E17 | Optimization and smoothness | Full control scene, 1080p, RTX 3050: target ≥120 FPS without disabling mandatory categories. Measured CPU/frame/GPU budgets, p50/p95/p99/max, VRAM, stalls, and warmup. Pan/zoom, new chunks/assets/effects, and a long run do not hide hitch/stutter behind high average FPS. |

## Implementation order and evidence

1. **Contracts and lighting core:** E01–E02, per-pixel P/N proof, gamma/alpha/ABI, lifetime. The current four-source opaque-surface kernel is only an initial component; it is not Dynamic or the final HDR pass.
2. **Surfaces and materials:** E03–E05/E08/E13, a minimum scene on the production path, and every receiver category.
3. **Time and shadows:** E06–E07, a real lunar contribution, blockers/silhouettes, and verification of independent sources.
4. **Water and world effects:** E09–E12, added gradually with a shared repeat run after each integration.
5. **Overall threshold:** E14–E17, settings, quality, load, smoothness, and fault cases. Introduce telemetry and performance regression from early steps instead of optimizing only at the end. Only then continue game development under the original V01–V03 requirements.

Control scene: a supported slope/cliff/plain, two shores and a bridge, windows/forge/fire, a tree/stone blocker, and residents carrying nothing/logs/a sack. It is marked diagnostic and does not replace normal startup. Prototype time/weather comes through an explicit reproducible fixture input: this proves renderer response, not readiness of host-authoritative weather or forecasting.

The compulsory matrix is day/night/morning; summer/winter; clear/rain/wet/snow/storm; pan and zoom 0.62/1.30/3.00; before/behind objects, shore/bridge, reveal frontier; pause/2×/epoch reset; 720p/1080p/1440p, 100/150% DPI, resize/restore; Low/Medium/High. Record the exe/shader/asset hashes, backend/GPU/driver/resolution, camera/seed/fixture inputs. PNG does not replace temporal video, software does not replace native D3D12, and a green test suite does not replace visual acceptance.

Spatial tolerances proposed in the existing specs (do not report them as measured before a run): anchor drift ≤2 physical px, reprojection ≤1 px and ≤0.02 tile, and leakage only within the ≤1 px AA edge.

**The latest direct user requirement: at least 120 FPS at 1920×1080 on RTX 3050.** It replaces the previous 60 FPS answer. On the current laptop, verify the RTX 3050 Laptop actually selected, driver, power mode, and resolution. The frame-time target is ≤8.33 ms; record p50/p95/p99/max and over-budget frames, not only average FPS. Do not count a result with mandatory effects disabled, a reduced control scene, or a lower internal resolution that is not disclosed in the report. Report loading/compilation separately from the sustained gameplay run; keep GPU captures/readback outside performance sampling as well. **The target has not yet been achieved or verified.**

## Open decisions

- The canonical world/render metric must match the real projection. The current surface header documents `renderZ = legacyElevation * 2.4 / 29`; the older Core spec text using `/29` cannot be applied mechanically. A shared adapter and reprojection test are required, without a hidden change to game elevations.
- Agree on and verify GPU format/usage support, swapchain gamma, depth precision/basis, slope-shadow handling, GPU timing, caps, and ownership of UI composition. These are open Core spec decisions, not completed features.
- Define a separate Sun/Moon contract: curves, direction, intensity, color, cloud coverage, and interaction with ambient. A blue fullscreen tint proves nothing.
- Reproducibly verify the agreed performance baseline: ≥120 FPS, 1920×1080, RTX 3050, and every mandatory effect. The final VRAM/caps and GPU-timing method still require implementation and measurement; average FPS must not hide slowdowns.

## Settings and optimization — user addition

- **Quality profiles:** initial Low/Medium/High profiles from the World FX spec plus Custom. They define explicit effect budgets, LOD/decor density, shadow/reflection resolution, particle/haze caps, and permitted material quality. A preset name is not performance evidence. In the mandatory control scene, every profile retains all required effect categories; silent disabling does not count.
- **Manual controls:** lighting/shadows, reflections/water, haze, particles, detail, and render scale; display mode/resolution, VSync, and an FPS limiter with an uncapped mode. The UI remains at native drawable resolution when only the world render scale changes. Do not present nonfunctional switches for unimplemented capabilities as working controls.
- **Safe application:** settings are local and do not change simulation/save/network state. Validate ranges and GPU capabilities, expose clear requested/effective state, provide Apply/Cancel/defaults, and recover after failed display/resource changes. Screen-mode changes need a safe revert so the user is not left without an image.
- **Measure first:** separate simulation/update, scene composition, RmlUi, command preparation/upload, GPU execution, fence/present wait, pacing, and capture overhead. Do not label CPU submit duration as GPU time. Record the supported effective present mode and monitor refresh; VSYNC-limited or hidden-window submit throughput is not evidence of displayed FPS.
- **Optimize measured bottlenecks:** visibility/chunk culling, LOD, and stable effect budgets; batching only where it preserves painter order/alpha; reuse of buffers/atlases/caches; removal of duplicate work and unnecessary allocations/uploads; asynchronous loading/warmup only with verified lifetime and explicit diagnostics. These are profiling candidates, not claims that every item is already implemented.
- **Smoothness:** a long steady-state run with pan/zoom and new objects appearing; frame-time distribution, over-budget frames, stalls, and 1%/0.1% lows with documented methodology. Test cold start/loading, pause/resume, and focus/minimize/restore separately. High FPS without consistent frame pacing does not close E17.
- **Two different results:** concept-faithful baseline on the 100% render scale with all required effects and separately the maximum measured performance of other obvious settings. A low-scale/density profile cannot secretly replace the baseline 1080p in the report.
- **Unlimited promises are prohibited:** “maximum possible” means consistent measured improvement without quality/smoothness regression. Do not claim an absolute maximum for any scene, configuration, or unit count. The agreed numerical threshold remains at least 120 FPS; the expected upper bound is established by the actual benchmark.

## What the requirement does not necessarily mandate

Do not promise an arbitrary 3D camera, GI/ray tracing, a fluid solver, physically volumetric atmosphere, snow-geometry deformation, or rigid-body/cloth simulation solely because the requirement says “detailed.” A limited 2.5D implementation is allowed only if its result passes the checks above and matches the concept appearance. Otherwise, revisit the specific limitation instead of declaring the effect complete.

## Condition for returning to game development

**All E01–E17 must be closed by current evidence, mandatory defects must be absent, and a shared native run plus independent comparison against the concepts must pass.** None of these items is currently marked fully accepted. Until then, further game development waits; the overall goal is not complete.

Technical sources: [Render Core](RENDER_CORE_SPEC_UA.md), [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md), [World FX](WORLD_FX_SPEC_UA.md), [Engine plan](GRAPHICS_ENGINE_PLAN_V2_UA.md), [Art production](ART_PRODUCTION_SPEC_UA.md). The direct newer requirement takes precedence over their older phase order.

# Ribterra — corrected graphics plan: engine, assets, world, and verification

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Current base-graphics order:** [V00–V05 — old appearance → UI → ground → objects → rain → winter](GRAPHICS_PROGRESS_UA.md). This is the main sequence of visible changes after Dynamic was deferred. The old dev/GW/DL items are technical packages, not finished screenshots of each version. [Progress gallery](index.html); [older 13 public targets](public-versions.html).

**Latest decision 27.09:** full dynamic lighting and its switch are deferred. The code currently contains only safe groundwork. The [future DL-01–08 plan](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) takes precedence: Dynamic does not block the base world/UI update; its light/occlusion gates apply only to the future enabled mode.
Date: 27 September 2026. Revision: **V2**. Status: **implementation plan; the new effects are neither implemented nor measured**.
Basis: checkout `C:\Users\fipst\Desktop\ribterra`, HEAD `bdc53e8`, 0.26.0. This document corrects insufficient technical detail in the 26 September plan.

## 1. What this corrects and which document is authoritative
Earlier concepts showed light patches, wet reflections, shadows, haze, and water, but phrases such as “align the lighting” and “review the water” did not define their implementation. That specification was insufficient. This document preserves the audit and technical packages; **the current scope and order are defined by the [28.09 plan](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md)**. [Render Core](RENDER_CORE_SPEC_UA.md) and [World FX](WORLD_FX_SPEC_UA.md) define algorithms and contracts; [Art production](ART_PRODUCTION_SPEC_UA.md) defines required content.

[The old world/UI plan](../../.omx/plans/2026-09-26-graphics-world-ui-update-ua.md) preserves game requirements and GW packages; the [world/UI specification](../../.omx/plans/2026-09-26-world-ui-implementation-spec-ua.md) covers generation, path/build/UI; the [weather specification](../../.omx/plans/2026-09-26-weather-implementation-spec-ua.md) covers authoritative simulation. The **24 technical packages for the full original scope** remain below as references; they are not a queue of mandatory releases. Public versions 0.26.1–0.27.0 are not renumbered.

The S01–S11/W01/W02 images remain unchanged artistic references. Native measurements and written boundaries take precedence over their inaccuracies. Pixel-perfect reproduction of an AI image is not guaranteed. Acceptance applies to the specifically described in-game appearance and behavior.

## 2. Confirmed engine baseline
These statements were verified in code; they are not new runtime tests.

| ID | Existing capability | Actual boundary |
|---|---|---|
| E01 | [Frame2D](../../include/ribterra/render_frame.hpp:15) has a 2D position, UV, color, texture handle and painter order. | There are no material/light/receiver contracts. |
| E02 | The [fragment shader](../../shaders/sprite.frag.hlsl:11) returns texture × color; the [pipeline](../../src/render/renderer2d.cpp:437) has one color target and no depth target. | Normals, wet specular, reflections, and lighting visibility cannot appear merely by replacing PNG files. |
| E03 | [Night grade](../../src/client/view.cpp:1625) darkens vertex colors; [fx.3](../../src/client/view.cpp:1121) then draws warm patches. | This is not a projection from a specific window or a material response to light. |
| E04 | [Buildings](../../src/client/view.cpp:688) have a dark contact overlay; [sprite residents](../../src/client/view.cpp:796) do not receive separate automatic shadows. | There is no dynamic directional shadow silhouette. Do not confuse the rock fallback on line 623 with a building. |
| E05 | [Water](../../src/client/view.cpp:1032) uses animated sprite overlays; the [surface](../../include/ribterra/terrain_surface.hpp:8) shares corner data. | There is no flow material, reflection, or water physics. A single water level remains. |
| E06 | [Terrain appearance](../../include/ribterra/terrain_appearance.hpp:62) and ground mist conceal the frontier. | Fog of War is not artistic haze separating depth planes. |
| E07 | [UnitAnimation](../../include/ribterra/unit_animation.hpp:15) has 8 facings and 4 walking phases; [cargo](../../include/ribterra/resident_art.hpp:8) comes from the real carried stack. | There is no foot IK, footstep history, or deformation. The existing Cart is a cargo-carrying appearance, not a new vehicle. |
| E08 | [GameArt loader](../../src/client/game_art.cpp:70) reads schema 1 and atlas pages up to 4096²; [shader contract](../../tests/shader_contract.py:31) strictly expects current bindings. | Loader, shader build, reflection tests and packaging need to be changed along with new materials. |
| E09 | [Compose](../../src/client/view.cpp:1610) uses logical 1440×900; [main](../../src/client/main.cpp:589) composites RmlUi separately. | A single viewport/DPI contract is required; 16:9 images do not prove that the existing layout is 16:9. |
| E10 | [Seasons](../../include/ribterra/seasons.hpp:8) and winter art states already exist. | Rain/snow/storm, temperature and accumulation — future weather systems. |

On 27.09, this machine reported an NVIDIA GeForce RTX 3050 Laptop GPU and AMD Radeon Graphics. This is hardware inventory, **not an FPS test or automatic minimum requirements**. Dev.01 must record the GPU actually selected, driver, power mode, executable, and resolution.

## 3. Architecture decision
**Working decision: extend the current fixed-camera 2.5D SDL_GPU/D3D12 renderer.** The world remains a tile simulation; buildings and residents remain controlled sprites with separate metadata. The new light/material/decal/water passes are real engine work.

| Option | Benefit | Cost/risk | Decision |
|---|---|---|---|
| Only new PNG files and old fx.3 | Small code change | Does not provide lighting visibility, wet materials, reflections, or tracks | Rejected as insufficient for the target |
| 2.5D + surface materials, projected lights/shadows, bounded FX | Preserves sim/net/UI/camera and provides the required visible effects within a bounded design | Requires authored masks/occluders rather than arbitrary 3D geometry | Selected, subject to dev.09 verification |
| Full 3D scene/different engine | Enables general 3D lighting/physics systems | Requires new models/animations/integration/picking/rendering and creates major demo risk | Do not begin unless the evidence-driven 2.5D slice fails |

Using 3D master models for **offline export of 2D assets** does not move the game to 3D. It is a way to align scale, camera direction, masks, and animation.

Dev.09 verifies the chosen decision; it is not a ceremony. If lighting/shadows/reflections are visibly implausible on a supported slope or the budget is not met, mass export stops. Record the concrete problem, fix the model, or separately reconsider scope/architecture. Do not call a disabled mandatory effect a success.

## 4. Included effects and their cost
| Effect | Required implementation | Required content/data | What is NOT promised |
|---|---|---|---|
| Window light | A ground receiver is lit from position/radius/cone/intensity; objects block rays; window emission is separate | Window anchors, cone, emission mask, occluder footprint/height | GI, ray tracing; facade/resident lighting belongs to future Dynamic under the separate DL plan |
| Shadows | Contact shadow + directional silhouette projection for buildings/trees/residents; responds to the agreed light direction | Caster silhouettes/height, receiver triangles, sunlight direction | Exact self-shadowing of every detail or arbitrary 3D shadows on every surface |
| Wet ground | Rain-driven wetness, normal/roughness response, and lighting highlights only on suitable surfaces | Ground normal/material mask, dry/wet response | Mirror reflection of the entire scene in every puddle |
| River | Flow UV, ripples, shore foam, lighting highlight; bounded projection of reflected sprites | Water/shore/flow masks, eligible reflection casters | Fluid solver, SSR, waterfalls, flooding, buoyancy |
| Haze | World-anchored depth bands with gradual tint/extinction that separate planes | World depth, revealed mask, bounded noise | Physical volumetric scattering and light shafts across the whole world |
| Rain/snow/wind | Authoritative weather controls particles and trails/gusts; roof/light/UI/FoW use correct masks | WeatherFxFrame, roof/ground receivers, particle atlases | Atmospheric physics, temperature damage, insulation |
| Footprints | Left/right cosmetic decals follow real movement; they persist, age, and are covered/melt | Snow receiver, movement distance, bounded local cache | Changes to elevation/collision or a network log for every footprint |
| Compressed snow | A local footprint shade/normal mask creates the appearance of shallow relief | Footprint normal/alpha, global snow cover | Real geometric deformation of snow |
| “Living physics” appearance | Synchronized steps/cargo/work animations, smoke, and vegetation with wind response | Consistent frames, pivots, job/cargo state | Rigid bodies, cloth solver, ragdoll, destruction |

The phrase “detailed physics” is not an acceptance criterion. If real physical interaction between objects is required, it is a separate system rather than a hidden part of the graphics update.

## 5. Shared integration contracts
1. SimCore/server owns temperature, precipitation, wind, cover, work modifier, and calendar. The renderer does not change rules for work, food, HP, or routing.
2. One presentation clock uses simulation-tick interpolation with explicit pause/speed/epoch. Until dev.14, test weather is allowed only in a marked diagnostic fixture. Weather values remain hidden in the production HUD until the authoritative path exists.
3. Ground receivers use the same surface as the feet/picking/build preview. Ground light, shadow, wetness and decals are not projected by an arbitrary screen oval.
4. Light/occluder/reflection/particle/decal data is culled by revealed state **before** effect buffers are prepared. Final FoW must not merely paint over reflections that already leaked.
5. There is no manual road, no road bonus and no hidden auto-road. Footprints are short-lived and do not change speed.
6. Blend/linear/sRGB/alpha, render-target formats, ping-pong, and resource lifetime are defined in the Core spec. Do not sample a target in a shader while the same subresource is a render target.
7. Minimum graphics quality reduces counts/resolution while retaining every mandatory effect category. GPU allocation/shader failure produces a diagnosed error/safe recovery, not a hidden “low mode” that still counts as accepted.
8. Old fx.3/night darkening is removed for migrated objects in the corresponding branch to avoid double lighting/shadows. The old software diagnostic is not presented as a verified D3D12 path.
9. Production RmlUi, resource-lifetime, scissor/clip, and GPU shader-interface tests expand with the renderer; a green old texture×color shader test proves nothing about the new passes.

## 6. Control scene and measurable target
**Proposed fixture** `visual_slice_v2`: a house with 2 windows, a warehouse, a workshop, 1 tree, 1 stone occluder, 3 moving residents (empty/logs/sack), a river with two shores and a bridge, a flat area, and one supported slope. Use 10 residents for the gameplay run and 20–30 for load. This is a diagnostic scene, not normal startup.

Dev.01 creates it through the current World/save lifecycle and records seed/save hash, world origin, layout, camera center, zoom, and native/render/logical sizes. If the fixture requires a test helper, keep it separate; do not hide changes to rules or persistence. Materials/objects use the production renderer, not a separate showcase shader.

Control states: summer day/night; dry→rain→drying; winter without precipitation→snow→storm→clear; unit movement in front of/behind house/tree/bridge; terrain slope; revealed frontier; pause/2×/load/reconnect. At dev.09 these are render fixtures; after dev.18, the same appearance comes from real weather.

| Gate | Concrete evidence |
|---|---|
| LIGHT | Turn on one window: its emission and light patch appear together; turn it off and both disappear. Move the occluder: the shadow within the patch moves; there is no lighting outside radius/cone. Camera pan/zoom does not move the light relative to the ground. |
| SHADOW | A resident and cargo move: each shadow remains anchored, changing silhouette/direction; a blue selection circle alone does not count. A building/tree shadows the correct receiver without leaking onto roof/UI/unknown areas. |
| WATER | Video shows ripples and foam moving; a reflection moves/fades with its caster and does not cover the shore/bridge; disabling the caster removes the reflection. There are no waterfalls. |
| WET | The same ground is dry/wet under the same lights: wet areas have a controlled specular response; the UI and dry roof do not become shiny. |
| HAZE | Two identical objects at different world depths have different controlled attenuation; pan doesn't attach haze to the screen, the nearest unit remains readable. The hidden object is not silhouetted. |
| SNOW | After a resident passes, sequential L/R footprints remain; standing/pause/teleport does not stamp new ones. Tracks disappear through aging/covering/melting and do not appear on water/roof/bridge without a snow receiver. |
| INPUT | Visual FX does not change picking or commands; particles and shadows receive no hit target. The UI remains readable and outside the world grade. |
| ART | 5–7 buildings read as one set in projection, scale, contact, detail, materials, and light. One attractive PNG is not accepted as a substitute for the scene. |

For spatial checks at the native 1080p control zoom, use debug receiver/occluder overlays and control points: the initial tolerance for anchor drift is ≤2 physical pixels on a stationary surface; leakage outside the mask, except for a ≤1 pixel AA boundary, fails. These are **proposed proof thresholds** and must not be reported as measured.

Artistic quality is scored separately from correctness: 0/1/2 for composition, scale, materials, contact, light, water, atmosphere, motion, UI, and legibility. Dev.09 requires ≥16/20 with no 0; final 0.26.1 requires ≥18/20 with no 0. The evaluator is not the asset author. A score does not replace video or conceal a failed functional criterion.

## 7. Reference: 24 technical packages for the full graphics direction
This details the previous full scope, including deferred lighting. The current base-branch order is V00–V05; the future Dynamic order is DL-01–08. The numbers below remain as cross-reference work packages; they no longer mean 24 mandatory sequential builds 0.26.1.

| Dev | Result and change | Main locations | Required output/dependency |
|---|---|---|---|
| 01 | Baseline, fixture, camera/scale/perf, capability inventory | view, renderer, existing capture/probe tools | Manifest + real baseline video; nothing is mocked as “improved” |
| 02 | Material/pipeline/resource contracts, shader reflection build, linear/alpha path | render_frame, renderer2d, shaders, cmake, shader tests | Offscreen sample/composite, resize/minimize/device/allocation failure; E01/E02/E08 |
| 03 | Small asset kit + metadata/windows/casters/receivers | GameArt, world.json, art exporters | House/store/workshop/unit/tree/ground/water masks; no mass export |
| 04 | Consistent receiver triangles, occluders, viewport, and reveal masks | terrain_surface, GameView, Core spec | Surface/picking/masks/debug fixtures agree; 02–03 |
| 05 | Window ground lighting, emission, contact/directional shadows, wet ground | Core materials/passes | LIGHT/SHADOW/WET native videos; 04 |
| 06 | Flow water, shore foam, bounded reflection | World FX + water material | WATER video with shore/bridge/hidden caster; 04 |
| 07 | Haze, smoke/wind, rain/snow prototype | World FX | HAZE/precipitation clips, pause/quality; diagnostic weather input |
| 08 | Local footsteps and compressed snow appearance | World FX + movement observation | SNOW edge cases and bounded cache; 04/07 |
| 09 | All effects in one small real scene | Full production rendering path | **G1: appearance + correctness + perf. Mass assets remain blocked until this passes** |
| 10 | Compact concept HUD and input skeleton | RmlUi assets/GameUi/GameView | 720p/1080p/1440p, scale/locales; weather fields still hidden |
| 11 | Terrain edge/path/build rules, removal of Road and bonus | Sim/world_grid/path/server/UI | Canonical preview/server + compatibility tests; Bridge preserved |
| 12 | Deterministic valley generation | world/biomes/generation profile | One candidate demo seed + 10 regression seeds, reachability; 11 |
| 13 | WT-01 authoritative weather/temp/cover/work, calendar offset | Weather spec sim/rules | Golden schedule, affected/unaffected work matrix, FirstStorm food buffer |
| 14 | WT-02 save/net/reconnect/forecast/current °C | schemas/protocol/ClientSession/UI | Full/delta/split/latejoin/persistence, no client-random numbers; 13 |
| 15 | Final shores/cliffs/vegetation/ground and receivers | terrain assets/generation/render | The new world surface preserves LIGHT/WATER/SHADOW correctness; 09/12 |
| 16 | Full available building kit + states/II | Asset spec/export/loader | Camp + 11 types, construction/ready/winter/selection, runtime contact sheet; 09/15 |
| 17 | Full resident/cargo/job art + coherent motion | UnitAnimation/resident_art/GameView | 8 facing × 4 walk frames, cargo truth, footstep cadence, depth; 09/15 |
| 18 | Full atmosphere/material integration with real weather | Core + FX + WeatherView | Day/night/rain/storm/cover/melt, with no diagnostic weather in normal gameplay; 14–17 |
| 19 | Accurate compact/full inspector, per-storage projection | GameUi/snapshot/save/net | Two warehouses with different stocks, reservation/cargo, current upgrade/production |
| 20 | Minimap and final input/DPI | RmlUi/Minimap/GameView | Revealed map/viewport/click/drag/keyboard, without legacy HUD |
| 21 | Culling/quality/performance and resource limits | Renderer/assets/FX | 20–30 residents + storm + lights, profiles of each tier; 18–20 |
| 22 | End-to-end gameplay regression run | Sim/net/UI/render/tests | 10 seeds, normal start, FirstStorm, food/logistics/noRoad, full configured CTest |
| 23 | RC package | Packaging/assets/shaders/launcher | Clean extract/launch/save/load, hashes/manifest, all shader/material assets included |
| 24 | Acceptance of 0.26.1 on Windows and two PCs | The same RC | Weather/world state synchronized, two physical PCs, report; if unavailable, only a local candidate |

A static diagnostic weather fixture is allowed in dev.01–09 for graphics experiments. It does not bypass WT-01/02 or enable a fake forecast. Any change to lower-level contracts after dev.09 repeats the corresponding proof; for example, new shore geometry in dev.15 repeats water/receiver tests.

For the deferred full graphics direction, the original dependency path is 01→02→03→04→05/06/07/08→09→10/11→12→13→14→15/16/17→18→19/20→21→22→23→24. Parallel work is allowed only after the shared contract is fixed; one renderer owner integrates the passes.

## 8. Production asset pipeline
The detailed passport is defined in [ART_PRODUCTION_SPEC_UA.md](ART_PRODUCTION_SPEC_UA.md). The engine cannot produce the required style from inconsistent textures. Each asset has a source/master, camera/export settings, neutral base, alpha, emission/snow/shadow or ground normal/material maps, anchors, bounds, and a native contact sheet. Legacy/BakedCompat is preserved; future Dynamic2_5D requires facade and resident normal/depth maps under a separate specification.

Only a small kit is produced before dev.09. After acceptance, produce the full available catalog. Existing PNG files may remain only if they pass the same passport/scale/material requirements; “the file already exists” is not acceptance.

## 9. Performance, compatibility, and budget
Initial objectives, **not measurement results**:
- 1920×1080 Medium on the recorded RTX 3050 Laptop configuration: p95 frame ≤16.7 ms, p99 ≤25 ms during a 120-second scene run after warmup; no regular gameplay frame >50 ms because of FX.
- GPU world rendering working budget ≤10 ms; CPU FX preparation ≤2 ms/frame; measure the actual sim/UI share separately. If GPU timestamps are unavailable, do not substitute CPU submit for GPU time—mark the measurement unavailable.
- 1280×720 Low on the separately fixed integrated GPU: initial budget 33.3 ms p95, p99 ≤50 ms. This is a test target, not yet published min specs.
- Additional render targets/caches and atlas residency have a numerical ledger in the Core/Asset specs. Total peak use matters more than the size of one PNG. Allocation must not repeat every frame.
- For each tier: screen resolution, internal scale, lights/occluders/water/reflection/particles/decals caps, active GPU/driver, sample count, p50/p95/p99, memorypeak, overflow counts.
- A mandatory effect cannot disappear on Low. If the scope does not fit, record an explicit revised decision instead of silently weakening acceptance.

An existing flat-quad shadow or disabled reflection is an acceptable safe failure during development, but not final delivery of the promised effect.

## 10. Risks and early checks
| Risk | How to detect it early | Response |
|---|---|---|
| Sprite silhouette does not produce a convincing shadow/reflection | One unit + house near water, different heights/zoom, dev.05/06 | Add authored caster/height slices; repeat the proof; do not conceal the issue with blur |
| Light passes through a facade or a hill cuts the patch | Receiver debug + moving occluder/slope/cliff fixture | Fix the receiver/visibility model before mass asset production |
| Double baked/runtime lighting | Neutral-lit asset with lighting off/on | Separate authored AO, emission, and ground illumination; remove legacy duplicates |
| Glow/particles reveal a hidden building | Two identical visible scenes with different hidden worlds | Visible pixels must remain identical outside the AA boundary |
| PNG/mask cost grows unnoticed | Byte ledger and runtime peak for the small kit | Reuse atlases, control lifetime/mips/format; do not multiply every map for every variant |
| Cosmetic time affects simulation or pause | Compare simulation hashes with FX off/on and pause/2×/resume | Use one clock adapter; no GPU-to-simulation callbacks |
| Footprints create new roads | Verify identical speed/path/hash with and without footprints | Local decals only; finite lifetime; no traffic accumulator |
| The plan again substitutes for results | Every “done” claim includes build+native video+tests+failed criteria | Independent reviewer, not the concept author |

## 11. Evidence and completion
Future implementation artifacts go under `artifacts/graphics-v2/dev-XX/` with a capture manifest, raw screenshot, continuous video, machine/perf JSON, and test log. The manifest records commit/build, shader+asset hashes, renderer/backend, GPU/driver, native/internal/logical resolution, UI scale, seed/save hash, world/camera/zoom, tick/calendar/epoch/weather, quality, test ID, expected/actual, and unresolved issues.

A software probe, generated image, JPEG collage, or “green tests” does not replace D3D12 video. A screenshot does not prove movement of water, shadows, trails, snowfall, or recovery. Full CTest does not prove two physical PCs. Unavailable external evidence is marked as not run.

**The current sequence to execute is V01–V05.** The full material/light graph begins only in the future DL branch. This revision corrects the plan; it does not create a new executable or permit 0.26.1 to be called ready.

## 12. Primary technical sources
SDL supports separate render targets/passes and defines their lifecycle; support for a specific format is checked with the required usage flags. This supports the proposed API path but does not prove Ribterra's speed or artistic quality:
- [SDL GPU overview](https://wiki.libsdl.org/SDL3/CategoryGPU)
- [Render pass: load/store and boundaries](https://wiki.libsdl.org/SDL3/SDL_BeginGPURenderPass)
- [Texture format capability](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat)
- [Shader bindings contract](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)

## 13. Changes from V1
Added the missing renderer/material/resource plan; projected lighting and shadows; wetness; bounded reflections; separate haze; footprints; asset production; and early performance/correctness gates. Replaced 15 development stages with 24. Preserved weather/generator/roads/UI/demo scope. Explicitly separated graphics illusions from physical simulations. No new effect is marked implemented.


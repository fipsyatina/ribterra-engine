# Ribterra — World FX contract for 0.26.1

> English translation of a historical project document. Technical literals and recorded results are preserved; these records describe their original snapshots and do not validate the current build. Original wording is available in import commit `7388a2f`.

**Bounded Environment checkpoint 08.10:** [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md) synchronizes diagnostic hour/cloud/haze across Sun, Moon, ambient light, and atmosphere. Fresh build02 and native01–02 actually exited with code 0; 44/44 derived outputs were exact. Checks causally covered cloud/haze, source-specific shadows, and unchanged local light/emission. Normal startup, authoritative WeatherFxFrame, precipitation, wind, video, performance, and full E01–E17 remain OPEN.

**Bounded native E10 checkpoint 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_CHECK_2026_10_08_UA.md) passed build03/native02–03 on RTX 3050/D3D12; 29/29 derived outputs were exact, with causal depth/height/coast/time/weather behavior and an unchanged raw carrier. This is a diagnostic layer, not full-world, authority, video, performance, or full E10 acceptance.

**Specified E10 algorithm 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md) for the new `WaterAtmosphereSlice` replaces the proposed six fog quads/HazeBandCache with analytic world-space distance using separate far/morning/coastal terms. This does not remove visual requirements or accept the older cache/quad checks or full E10. b0=1024 is only a new proposal; separate native proof must establish that the existing 384-byte prefix and attachment-memory totals remain unchanged after actual implementation.

**New bounded follow-up 08.10:** [binary GPU carrier and odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md) was checked with fresh build02/native05–06. Raw binary packing is proven only in this control scene; intermediate AA, every recovery failure, and all full-engine gates remain open. The checkpoint describes a previous source cohort.

**Current status 08.10:** [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) has a bounded native checkpoint in the shared production renderer, not merely an overlay. Its [contract](WATER_REFLECTION_V1_CONTRACT_UA.md) defines the coverage carrier, Land/Water/Reflection typing, world-anchored flow, shoreline foam, and half-resolution Reflection. Normal startup, raw coverage/AA, input/video, all World FX, concept art, and ≥120 FPS have not been accepted.

**Historical scope boundary 27.09:** The scaffold/deferred decision below does not override the newer [full engine first](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md) requirement. Game and generator integration wait for E01–E17. The work packages and old-runtime description below are specification/history, not current readiness evidence.

**Status:** technical specification for `0.26.1-dev.06…dev.09`; every selected design is **PROPOSED** and still must be implemented and measured.

**Goal:** combine water, reflections, haze, weather, and footprints into one convincing 2.5D image without letting decorative effects override simulation.
**Related documents:** [future update plan](NEXT_UPDATES_DETAILED_UA.md), [visual test contract](AGENT_VISUAL_CONTRACT_UA.md), [render core](RENDER_CORE_SPEC_UA.md).

This document does not promise physical water, full 3D lighting, or snow deformation. It defines a 2.5D system that supports camera movement, zoom, pause, 2× speed, reconnect, and Fog of War. Do not accept the work if an agent tries to close this stage with new sprites over the old flat water, a full-screen dark-gray overlay, or randomness outside the diagnostic fixture or future authoritative `WeatherView`.

## 1. Verified state and change boundaries

### 1.1. Current state

- `src/client/view.cpp` places the world in fixed isometry: cell width `58 × zoom`, height `29 × zoom`; terrain height is subtracted from screen `y` in logical pixels.
- Water in the presentation snapshot has one height: `z = 0`. Anchored effects include up to 96 animated `fx.4`/`fx.5` items and 12 `fx.2` links; these are sparkles and foam, not a water material.
- `render::Frame2D` has passes `Terrain`, `WorldOrdered`, `Atmosphere`, `Fog`, `Ui`. GPU-path uses one texture × vertex-color fragment shader; surface metadata, a material-specific water pipeline, and a reflection target are missing.
- `UnitMotion` interpolates the displayed position between authoritative samples. `UnitAnimation` has eight directions and four walking frames; there are no bones, feet, or IK.
- In winter, trees and buildings have seasonal variants, but they do not prove current snow cover. `WeatherView` and `snow_cover_permille` currently exist only in the weather specification; they are absent from runtime headers/snapshots.
- Fog of War controls cell discovery; atmospheric effects must not reveal hidden cells.

These statements are **CURRENT / VERIFIED IN SOURCE**. All new structures, shaders, caps, and paths below are **PROPOSED** until code, tests, and native evidence show otherwise.

### 1.2. Hard 0.26.1 boundary

Included:

- one water level on Surface;
- world-anchored UVs, a normal map, foam, and controlled sparkle;
- limited sprite reflection for visible objects;
- local haze bands, rain, snow, and wind streaks: in dev.06–09 from explicitly selected diagnostic input, and after dev.14/18 only from authoritative `WeatherView`;
- cosmetic left/right tracks derived from actual displayed movement;
- quality presets, fault diagnostics, culling and native video checks.

Excluded:

- SSR, full-frame planar capture, or ray tracing;
- simulation of water waves, currents, level, or freezing;
- waterfalls, cascades, or multiple water planes;
- wet physics, slip, collision/path changes;
- terrain-mesh deformation or saved footprints;
- constructed roads, road lines, or flora footprints;
- fortresses, armies, sieges, fuel, or heating merely because they appear in the concept.

## 2. Coordinate Systems and Time

### 2.1. Coordinates

The contracts below use four spaces:

| Space | Unit | Purpose |
|---|---|---|
| `renderXY` | tile unit, `float2` | material quality, particles, traces, culling; one cell = `1×1` |
| `renderZ` | tile-height unit, `float` | `legacyElevation / 29`; water always `0` |
| `drawable_px` | physical drawable pixel | quad, streak, scissor, native capture; not window points and not old logical 1440×900 |
| `uv` | normalized texture coordinates | albedo/normal/foam/mask samples |

Every new FX object must use one render-core helper rather than duplicating the formulas:

```text
effective_zoom = user_zoom * drawable_height / 900
tile_w = 58 * effective_zoom
tile_h = 29 * effective_zoom

screen_x = origin_x + (renderX - renderY - cam_x + cam_y) * tile_w / 2
screen_y_at_z0 = origin_y + (renderX + renderY - cam_x - cam_y) * tile_h / 2
screen_y = screen_y_at_z0 - 29 * renderZ * effective_zoom
```

`origin_x/origin_y` belong to the render core's drawable camera/viewport state. World FX receives a prepared `RenderToDrawable` and does not read `GameView` camera globals. Subtract the camera origin on the CPU before float32 upload. All radii, prism heights, reflection proximity, lateral footprint offsets, and culling distances use render-tile units. Do not mix `legacyElevation`, drawable pixels, and `renderZ` in one spatial test.

### 2.2. One presentation clock

**PROPOSED interface:** once per frame, render core supplies `presentation_time_s`, `shown_simulation_tick`, `control_epoch`, `paused`, and `speed`. Water, reflections, haze, weather particles, and footprints must not create independent wall-clock generators.

- While paused, `shown_simulation_tick` and every weather-particle TTL stop. The camera may move, but only projection is recalculated.
- At 2× speed, FX moves twice as fast with the displayed simulation; the active-particle count does not double.
- If the tick moves backward, jumps by more than two seconds of simulation, `control_epoch` changes, the session/layer changes, or the client returns after reconnect, clear transient haze/particle/footprint pools rather than fast-forwarding through missed storm time.
- The global water phase is a pure function of `shown_simulation_tick` and versioned material constants. Teleporting/resetting a local emitter does not restart water; reconnect reconstructs the phase from the new displayed tick without accumulated backlog.
- Save/load and reconnect can drop haze, particles and footprints. This is an allowed cosmetic difference and cannot get into save, checksum, snapshot or replay authority.

## 3. Compatible render-core contract

The names in this section align with `RENDER_CORE_SPEC_UA.md` as the boundary between world data and renderer, but remain **PROPOSED** until corresponding headers and implementation exist.

```cpp
struct SurfaceInstance {
    MaterialId material_id;
    std::uint32_t receiver_flags; // Water, Snow, Wetness, Footprint, Reflection
    std::uint8_t wetness_u8;
    std::uint8_t reveal_u8;
};

struct Occluder2D {
    WorldPoint world_anchor;
    AuthoredPrismShape shape; // OBB or capsule; CPU converts to bounded convex vertices
    float base_render_z;
    float top_render_z;
    ShadowClass shadow_class;
    std::uint8_t reveal_u8;
};

struct WeatherFxFrame {
    double presentation_time_s;
    std::uint64_t epoch;
    float rain01;
    float snow01;
    float haze01; // artistic atmosphere, never secrecy FoW
    Vec2 wind_world;
    float temperature_c;
    float accumulation01;
    RenderQuality quality;
};
```

`Occluder2D` does not expose an arbitrary polygon API to the GPU. The original authored polygon may remain as a CPU validation/debug source, but frame preparation reduces it to an OBB or capsule prism in `renderXY/renderZ`. The loader rejects invalid, self-intersecting, or excessively complex shapes instead of allowing the shader to truncate them arbitrarily.

`WeatherFxFrame` is a presentation projection, not a new source of truth. Two sources are allowed at different development stages:

1. **dev.06–09 diagnostic only:** versioned `DiagnosticWeatherFxInput` inside `visual_slice_v2`, with values recorded in the capture manifest. It is unavailable to the normal world, save, network, or HUD and must not be called a forecast.
2. **after dev.14, shipping integration in dev.18:** future authoritative `WeatherView` from the host snapshot through `ClientSession`. Source inspection confirms that this runtime type does not yet exist; the field names below come from the **PROPOSED** weather specification, not an implemented API.

The final adapter builds a frame from `WeatherView.current`, `snow_cover_permille` and one interpolated clock:

```text
rain01 = precipitation == Rain ? precipitation_permille / 1000 : 0
snow01 = precipitation == Snow ? precipitation_permille / 1000 : 0
haze01 = kind == Storm ? clamp(0.20 + 0.55 * precipitation01, 0, 0.75)
         : kind == Cloudy ? 0.12 : 0
temperature_c = temperature_deci_c / 10
accumulation01 = snow_cover_permille / 1000
```

`wind_world` derives from eight planned `WindDirection` values and is scaled by `wind_permille / 1000`. One global weather state means all players receive the same values. A quality preset may change only density and caps. A normal game must not replace missing/unknown `WeatherView` with client RNG: precipitation remains zero, a diagnostic status is shown, and the delivery gate remains failed.

### 3.1. Future render graph
The canonical graph has six passes and eight attachments defined in Core: Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite → UI. GroundGBuffer contains albedo/world/material/flags; GroundLighting reads them and reflection, then writes scene/meta; WorldForward writes scene/meta without reading meta; Composite handles output/FoW. Haze is applied once before alpha blending in GroundLighting/WorldForward.

Water/reflection compositing runs in GroundLighting only for water receivers. Bridge/bank surfaces have their own flags and receive no reflection. Footprints are applied in GroundGBuffer before WorldForward and do not change position/reveal/receiver flags. Metadata packing comes only from Core: surface_flags B = shore for Water or snow cover for land; world_meta = depth/reveal, not water coverage.

The old half-resolution LocalLight target is removed; full lighting uses sharp material/normal data. Reflection remains half-resolution, with no separate water-mask attachment. All shader reads and writes remain separated.

## 4. Water material

### 4.1. Geometry and receiver mask

- Every revealed Surface cell with `Terrain::Water` writes a water bit to `surface_flags` in the Ground pass at `renderZ=0`. Unrevealed cells do not enter effect preparation.
- Neighboring water cells may be batched into a chunk mesh, but the boundary from authored water/shore data must match receiver triangles without rectangular seams.
- GroundGBuffer first establishes water/bank/bridge receivers. GroundLighting samples reflection only for water; bank/deck flags receive no reflection, and water beneath a bridge is visible only through open gaps.
- Terrain elevation, picking, pathfinding, and collision remain unchanged. Water vertex displacement is zero.

### 4.2. Stable UV and visual flow

UVs are calculated from `renderXY`, so the picture is not "glued" to the screen:

```text
uv_base = renderXY * 0.18
t = shown_simulation_tick / 20.0
flow = valley_flow_world * (0.018 + 0.022 * wind01)
uv0 = uv_base + flow * t
uv1 = rotate_37deg(uv_base * 1.73) - flow * 0.61 * t
```

For 0.26.1, `valley_flow_world` is a normalized presentation vector from versioned map/generator visual metadata. If the metadata does not exist yet, use one explicitly specified demo-profile vector; do not derive it from container order, frame RNG, or pathfinding. This is decorative flow and does not move objects.

Water fragment shader (**PROPOSED**) reads:

- base water albedo/gradient texture;
- a tileable two-channel normal map sampled twice (`uv0`, `uv1`);
- authored water/shore atlas data and material flags;
- frame constants: `t`, `wind01`, tint, fixed camera light direction.

```text
n0 = unpack_normal(sample(normal, uv0))
n1 = unpack_normal(sample(normal, uv1))
n  = normalize(float3(n0.xy + 0.55*n1.xy, 1.35))
fresnel = pow(1 - saturate(n.z), 3)
sparkle = pow(saturate(dot(n, half_vector)), 32)
color = lerp(deep_tint, shallow_tint, shore01)
color += sparkle * (0.04 + 0.10*wind01)
alpha = 1
```

This is stylized 2.5D lighting. There is no scene refraction, depth reconstruction, or SSR. Normal-distortion amplitude must remain small: the shoreline must not move by more than one tenth of a tile height at 100% zoom.

### 4.3. Shoreline foam

`shore01` derives from four orthogonal water/non-water neighbors and, if needed, four diagonals for smoothing. Foam appears only in the water-side band:

```text
foam = smoothstep(0.62, 0.92, shore01 + normal_noise*0.12)
foam *= 0.35 + 0.35*wind01 + 0.30*max(rain01, snow01)
foam *= reveal01
```

It does not pass through the bridge deck, paint unrevealed areas, or turn every internal chunk boundary into a shoreline. `fx.4/fx.5` may serve as temporary fallback, but must not be layered over the new material in an accepted frame.

## 5. Bounded sprite reflections

### 5.1. Eligible reflections

In dev.06, candidates are visible buildings, trees, bridge supports, and large static props with `Reflectable2D`. Medium/High may add residents and animals if caps hold. Particles, UI, Fog of War, selection rings, progress bars, smoke, and reflections themselves do not reflect.

Candidates are accepted only when:

- `reveal_u8 > 0`, the object is on the Surface layer, and the sprite/atlas handle is valid;
- its ground anchor is no farther than 2.5 `renderXY` tile units from a visible water receiver;
- its projected bounds intersect the drawable viewport + 32 reference px, scaled by `drawable_height/900`;
- base/top height lies inside the accepted 2.5D range; an enormous decoration cannot fill the lake.

### 5.2. Bounded flipped billboard

For each source, let `A = world_anchor` in `renderXY`, `H = max(0, top_render_z - base_render_z)`, and `B = project(A.x, A.y, renderZ=0)`. `B.y` is the local waterline for that sprite. Reflect every source-quad vertex in drawable space:

```text
x_ref = x_source
y_ref = 2 * B.y - y_source
```

This approximates a billboard reflection rather than the correct projection of a 3D shape. Use the same shaped atlas region with a vertical-flip shader flag/UV; a dynamic depth map alone cannot produce the correct perspective warp for a single quad. Shift the quad downward by `0.06 * projected_height` so the foundation does not leave a bright gap. Alpha:

```text
distance01 = saturate(abs(y_ref - B.y) / max(1, projected_height))
alpha = source_alpha * reveal01 * (1 - distance01)^1.7 * reflection_strength
reflection_strength = clamp(0.16 + 0.18*(1-wind01), 0.12, 0.34)
```

Water normals shift the reflection sample within `±1.5` reference px at `drawable_height=900`, scaled with the viewport; a larger shift makes the reflection shimmer. This is a subdraw in the `Reflection` pass before Ground/World. It is not a screen capture and must not read an already completed framebuffer. The reflection target may contain pixels outside future water, but they never enter `scene_linear` because only the GroundLighting water branch composites them using water/shore inputs.

### 5.3. Clipping, banks, bridges, Fog of War

Reflection pixels enter `scene_linear` only through an authored water receiver, reveal, the reflection bit, and nonzero antialiased coverage in a **carrier explicitly defined by the full-engine graph version**. The old roadmap assumed coverage in SurfaceWorld.W; Contact V1 forbids that interpretation because W is discrete 0/1/2, point-sampled/opaque, and not coverage. The [Contact GPU partial proof](CONTACT_GPU_CHECK_2026_10_07_UA.md) is implemented but **does not support Reflection/Water**. The full-engine coverage carrier remains OPEN; Reflection requires an explicit ABI/memory/resize amendment and native edge proof. The Reflection requirement is not removed. Ground bank/bridge deck overwrite water output; supports/foreground sprites render later. Local OBB/capsule prisms do not replace authored shoreline/receiver geometry.

Zero tolerance: no reflected pixel may appear in unrevealed space, on land, on the bridge deck, or over the foreground bank. If exact clipping fails, reflections turn off with a diagnostic fault; that does not pass release acceptance.

### 5.4. Cache and culling

`ReflectionCandidateCache` keys entries by stable object ID + atlas generation + terrain/surface/reveal revision. Static candidates recheck proximity/bounds at least once every 8 rendered frames and immediately after camera, zoom, reveal, layer, or terrain revision changes. Dynamic candidates recheck every frame. Clear the cache on epoch/session reset; do not serialize or transfer it. When over cap, discard the candidate farthest from viewport center, with stable ID as the tie-breaker.

## 6. Haze and scene depth

Use local world-anchored layers instead of a gray full-screen rectangle.

### 6.1. Depth bands

World depth is `d = renderX + renderY`; `screen_y_at_z0` is its projection. Divide the visible range into `haze_bands`. Each band contains 2–4 deterministic soft quads with render-space anchors and low-contrast noise. Anchor seed:

```text
hash(world_seed, band_index, floor(shown_simulation_tick / 1200), fx_algorithm_version)
```

Positions do not reshuffle every frame. Quads drift along `wind_world` at 0.02–0.06 cell/s and wrap within their band. Default opacity is 0.03–0.12; Storm may reach 0.20, but black or gray curtains are forbidden.

### 6.2. Difference from secrecy fog

- Haze can reduce local contrast, but the silhouette of a revealed building remains readable.
- Haze is not created for unrevealed anchors and is multiplied by receiver/source `reveal01`.
- Fog of War is drawn after atmosphere and finally masks unrevealed areas.
- No haze texture, reflection, or particle may derive color or shape from a hidden background.

`HazeBandCache` retains a band for 60 presentation ticks after it exits the expanded viewport so camera panning does not cause popping. Epoch/layer/algorithm reset clears the cache. TTL has no effect on simulation.

## 7. Rain, snow and wind

### 7.1. Diagnostic input and future authority input

In dev.06–09, use only `DiagnosticWeatherFxInput` in the control scene with the same normalized fields as `WeatherFxFrame`. This validates the shader, pool, pause, and quality behavior before weather exists, but does not prove simulation, forecast, save, network, or reconnect authority.

After dev.14/18, use the planned `WeatherView`, where:

- `precipitation_permille` and `wind_permille` — integers in `0…1000`, not m/s;
- `temperature_deci_c` — tenths of a degree Celsius;
- `PrecipitationKind` defines Rain/Snow; the renderer does not infer precipitation type from the season;
- `snow_cover_permille` is global valley cover, not the number of particle sprites;
- `evaluated_simulation_tick` records the authoritative tick to which the sample corresponds.

Positive temperature + Rain never displays snow. A winter material without current snow does not produce snowfall. A forecast does not activate an effect before its actual boundary. In a shipping build, the diagnostic provider is not connected and cannot replace unknown network weather.

### 7.2. Wind direction

Eight directions map to a unit world vector. For drawable screen:

```text
wind_screen = normalize(project_vector(wind_world, z=0))
wind01 = wind_permille / 1000
```

A rain streak has a horizontal drift `80 * wind_screen.x * wind01` reference px/s and vertical speed 520–760 reference px/s. Snow flake: 55–120 reference px/s down, 18–65 reference px/s lateral drift, sin wobble from stable slot phase. All reference pixels given for drawable height 900 and multiply by `drawable_height/900`. The numbers are **PROPOSED** and must be calibrated with native video, but the units and dependencies are fixed.

### 7.3. Pool, spawn and TTL

Particle slot seed = hash(`control_epoch`, weather-period start, slot ID, algorithm version). This is presentation RNG and does not enter the simulation hash.

```text
target_rain = round(cap_rain * rain01)
target_snow = round(cap_snow * snow01)
spawn_budget_per_frame = min(missing, ceil(cap * 0.08))
```

- Rain TTL: 0.45–0.85 s; kill below the viewport + 32 px.
- Snow TTL: 3–7 s; kill below the ground receiver or viewport + 48 px.
- Wind-gust pixel/ribbon TTL: 0.8–1.8 s; gusts are allowed only at `wind01 >= 0.55` and do not replace Rain/Snow.
- The spawn area is the expanded drawable viewport. Rain is screen-oriented; snow has world anchors for parallax. Both pass reveal filtering before GPU upload and receiver clipping in the production graph.
- The camera does not teleport old snow: world-anchored flakes reproject. Screen-oriented rain may be cleared after a camera jump greater than 1 tile.
- Pause freezes TTL. At 2×, TTL and movement advance twice as fast. Reconnect resets instead of replaying backlog.

Particle drawing does not change accumulation. In dev.06–09, cover/melt is a field in the diagnostic fixture; after dev.18 it comes only from authoritative simulation through `snow_cover_permille`.

## 8. Footprints in snow

### 8.1. Motion source

Each displayed resident receives a `FootprintEmitter`. It uses the actual displayed `resident_position()` / `UnitMotion::sample(animation_tick)`, not a future path, packet arrival, or tile command. Four-frame animation does not provide exact foot-contact events, so footfall is an honest 2.5D approximation based on distance.

```cpp
struct FootprintEmitter { // PROPOSED, cosmetic only
    WorldPoint previous_displayed;
    float accumulated_distance;
    FootSide next_side; // Left / Right
    std::uint64_t epoch;
    bool initialized;
};
```

On each frame:

1. Calculate `delta` between previous and current displayed `renderXY` position.
2. If the layer changed, distance exceeds the discontinuity bound, or the clock reset, reset the emitter without a stamp.
3. Accumulate Euclidean distance in render-tile units. At every `stride = 0.34` for adults (`0.28` for the young visual scale), interpolate a point inside the current movement segment.
4. Normalize direction `dir`; lateral `perp = (-dir.y, dir.x)`. Stamp anchor = point `± 0.085 cell * perp`, one-by-one L/R.
5. Orientation = `atan2(dir.y, dir.x)` or nearest of 8 atlas directions. Do not take random angle.

If one frame contains enough distance for several stride intervals, the loop may place several stamps, capped at 4 per emitter/frame. More indicates a discontinuity and resets the emitter so a teleport does not draw a straight trail.

### 8.2. Receiver eligibility

A stamp is created only when:

```text
Surface layer
AND receiver_flags contains Snow & Footprint
AND accumulation01 >= 0.20
AND revealed
AND not Water
AND not bridge deck / building footprint / exposed rock
```

The stamp uses the existing `ground_height()` and does not change the height field, mesh, collision, path cost, or movement bonus. There are no manually built roads; do not add a road receiver or road tracks.

### 8.3. Lifetime, burial, and melting

`FootprintStamp` stores anchor, side, orientation, displayed birth tick, cover at birth, and opacity seed in a local ring buffer.

```text
age_s = (shown_tick - birth_tick) / 20
age_fade = 1 - smoothstep(35, 90, age_s)
burial = saturate((current_cover - cover_at_birth) / 0.18)
melt = current_cover < cover_at_birth ?
       saturate((cover_at_birth - current_cover) / 0.22) : 0
opacity = base * age_fade * (1 - 0.85*burial) * (1 - melt) * reveal01
```

The numbers are **PROPOSED** and must be calibrated in dev.08. Rain at temperature >0 does not delete stamps through a local formula: the fixture/authoritative adapter changes accumulation, and the renderer responds to that decrease. In dev.18, this value comes only from host cover. Evict stamps with opacity < 0.02 or age > 90 s. Camera culling does not remove a stamp before TTL, but the ring cap may evict the oldest.

Footprints are not serialized, networked, or checksummed. They may disappear after load/reconnect. UI or release notes must not call them persisted snow state.

## 9. Quality presets and budgets

These are **PROPOSED, UNMEASURED** caps. Validate Low at 1280×720 and Medium at 1920×1080; High is a separate initial proposal. This is a dev.06–09 starting point, not FPS proof. Caps scale effect counts, not `WeatherFxFrame`, reveal, water geometry, or snow cover.

| Resource | Low | Medium | High |
|---|---:|---:|---:|
| reflection target scale | 0.50 | 0.50 | 0.50 |
| local lights | 24 | 48 | 72 |
| global shadow occluders | 128 | 256 | 384 |
| occluders per accepted light | 16 | 32 | 48 |
| reflection casters | 8 | 16 | 32 |
| haze bands | 3 | 6 | 8 |
| haze quads | 24 | 48 | 72 |
| weather particles together | 1000 | 2000 | 3000 |
| footprint ring capacity | 2048 | 4096 | 6144 |

Medium is the architectural baseline. Rain and Snow are mutually exclusive in one authoritative/diagnostic sample and therefore share the weather-particle pool. Resolution scaling for particle caps is allowed only after measurement; a hidden FPS-based multiplier is forbidden because it makes captures non-reproducible. Stable culling order: visibility → expanded viewport → distance to viewport center → stable ID/slot.

The global occluder cap does not allow silently discarding a blocker that actually affects an accepted light. First cull lights by reveal/viewport, then gather all blockers for the remaining lights. If an accepted light needs more than the per-light cap or the population exceeds the global cap, a counter/diagnostic is mandatory and the control scene and release acceptance fail. Reduce overlap, simplify authored prisms, or deliberately change the budget and repeat the proof; a frame with light leaking due to stable culling does not pass.

Required telemetry counters in the debug overlay/log: submitted/drawn/culled water chunks, reflection casters, haze quads, rain, snow, footprints, lights, and occluders; dimensions/formats of all eight attachments; active quality preset; overflow/reset reason. Telemetry is not shown to the player in release UI.

## 10. Fault policy

| Failure | Allowed runtime fallback | Delivery status |
|---|---|---|
| water shader/normal missing | flat/current water + diagnostic | **FAIL**, do not accept dev.06 |
| `surface_flags`/shore metadata invalid | reflections off + diagnostic | **FAIL**, do not accept dev.06 |
| reflection clip leak | reflections off | **FAIL** until corrected native proof exists |
| haze asset missing | haze off | **FAIL** dev.07 |
| weather particle asset/pipeline missing | particles off, diagnostic/authoritative frame remains available for debugging | **FAIL** dev.07 |
| footprint atlas/pool failure | footprints off | **FAIL** dev.08 |
| non-blocking particle/reflection/decal overflow | stable cull to cap, counter increments | allowed only outside the required control scene and without losing required rubric items |
| blocker/required-category overflow | diagnostic; frame may continue without crash | **FAIL**, silent drop does not accept |

A fallback is not a path to a completed delivery. Silent disabling is forbidden. The software test backend may verify command generation but does not prove D3D12 shader output.

## 11. Files and ownership boundaries

### 11.1. Existing integration points (**CURRENT**)

- `src/client/view.cpp` — projection, terrain/water overlays, displayed residents, atmosphere;
- `include/ribterra/render_frame.hpp`, `include/ribterra/renderer2d.hpp`, `src/render/renderer2d.cpp` — the existing Frame2D and D3D12/software paths;
- `shaders/sprite.vert.hlsl`, `shaders/sprite.frag.hlsl` — valid texture × color;
- `include/ribterra/unit_motion.hpp`, `include/ribterra/unit_animation.hpp` — displayed movement and 4-frame walk;
- `include/ribterra/weather.hpp` — **PROPOSED weather-plan path**; authoritative types must arrive through the weather task, not an FX shortcut.

### 11.2. Recommended split (**PROPOSED**, coordinate creation with render core)

- `include/ribterra/world_fx.hpp` — POD frame inputs, quality, counters, reset reasons;
- `src/client/world_fx.cpp` — diagnostic/future-WeatherView adapters, pools/caches/culling, no simulation mutations;
- `include/ribterra/render_materials.hpp` — `SurfaceInstance`, receiver flags, and material IDs if the render core approves this shared path;
- `src/render/world_fx_renderer.cpp` — water/reflection/haze/particle GPU passes;
- `shaders/water.vert.hlsl`, `shaders/water.frag.hlsl`, `shaders/reflection.frag.hlsl`, `shaders/world_fx.frag.hlsl`;
- `assets/art/fx/water_normal.*`, `water_foam.*`, `rain.*`, `snow.*`, `footprint_left.*`, `footprint_right.*`;
- `tests/world_fx_tests.cpp` — pure projection/pool/reset/culling contracts;
- `tools/world_fx_visual_probe.*` — an isolated probe if needed, but it does not replace native game capture.

Do not edit generated shader binaries manually; build/package must compile or copy them through the same mechanism as sprite shaders. Canonical pass names and attachment ownership come from `RENDER_CORE_SPEC_UA.md`. World FX uses them without aliases and does not create a parallel renderer.

## 12. Implementation order dev.06–dev.09

### `0.26.1-dev.06` — water proof

Visible changes: world-anchored water normals, shoreline foam, bounded reflections, and correct bank/bridge/FoW clipping. Terrain height, picking, water level, paths, and gameplay remain unchanged.

Required evidence:

- native D3D12 video 20–30 s: slow pan + zoom over the shore and bridge;
- second video with moving reflectable unit on Medium/High;
- frames same seed/view in pause and after resume;
- automated mask/projection/culling tests and counters showing no cap overflow in the control scene.

### `0.26.1-dev.07` — haze and diagnostic weather prototype

Visible changes: depth-banded haze and Rain/Snow/Wind driven only by versioned `DiagnosticWeatherFxInput` in `visual_slice_v2`; Storm does not become a gray curtain. Reveal and water proof remain intact. Forecast, work modifiers, and runtime weather are still absent; host-authoritative wiring belongs to dev.13–14/18.

Required evidence:

- native video Clear → Rain, Snow/Storm, pause → resume, 1× → 2×;
- diagnostic epoch reset equivalent to future reconnect: no backlog burst and no rain appearing from nothing;
- positive-temperature Rain case no snow particle;
- the same diagnostic frame on two clients: identical normalized weather values, allowing for preset-based particle-density differences; this is not network-authority proof.

### `0.26.1-dev.08` — displayed-motion footprints

Visible change: alternating L/R footprints with correct direction, fade/burial/melt on snow receivers. Movement, speed, collision, and save schema remain unchanged. Before dev.14, cover in the fixture is diagnostic input; after dev.18, the same formula reads authoritative cover.

Required evidence:

- native video of one resident walking through snow with a 90° turn;
- transition from snow → rock/bridge/water edge with no stamps on forbidden surfaces;
- pause/2×, teleport/discontinuity and load/reconnect reset;
- automated tests for stride interpolation, the maximum of 4 stamps/frame, L/R alternation, cap eviction, and the receiver mask.

### `0.26.1-dev.09` — combined slice

Visible change: water, reflections, haze, diagnostic weather, and footprints work simultaneously in a control scene before mass production of art assets. UI readability, Fog of War, and bridge occlusion remain stable. This is G1 render proof, not proof of the future authoritative weather state.

Required evidence:

- 60–90 s native video same seed/view on Low/Medium/High;
- GPU timing/counters and frame-time capture; specific test machine/driver/resolution;
- storm by water from bridge, buildings, resident movement and camera pan;
- failure-injection runs for missing shader/asset with explicit diagnostic;
- side-by-side comparison with the previous native build; an AI-only mockup may be shown as a reference, never as proof.

## 13. Checks and acceptance IDs

### 13.1. Automated contracts

| ID | Check |
|---|---|
| `WFX-WAT-01` | camera pan/zoom does not change world UV phase in the same world point |
| `WFX-WAT-02` | GroundLighting water branch/`surface_flags` prevents reflection on land, unrevealed areas, and the bridge deck |
| `WFX-REF-01` | quad mirror executes `y'=2B.y-y`, `x'=x` for fixed iso camera |
| `WFX-REF-02` | stable culling gives the same list for the same frame input |
| `WFX-REF-03` | Source or receiver reveal 0 produces zero reflected pixels/commands |
| `WFX-HAZ-01` | haze anchors remain stable between frames and reset when the epoch changes |
| `WFX-HAZ-02` | unrevealed tiles produce no quads; secrecy fog remains last |
| `WFX-WEA-01` | dev.07 diagnostic input, and `WeatherView` after dev.18, map exactly into the same `WeatherFxFrame` units |
| `WFX-WEA-02` | pause neither moves nor fades particles; 2× scales presentation delta |
| `WFX-WEA-03` | tick rollback/jump/reconnect clears the pool without backlog spawn |
| `WFX-FPT-01` | stamps appear by displayed distance, alternate L/R, and use the correct lateral sign |
| `WFX-FPT-02` | receiver/reveal/cover gates block water, rock, deck, and unrevealed areas |
| `WFX-FPT-03` | Footprint pool does not enter save/network/checksum, and reset does not mutate the world |
| `WFX-BUD-01` | Every preset enforces hard caps and stable eviction |

Pixel tests cover masks and the projection probe. They do not replace native video because they cannot show temporal shimmer, particle cadence, camera anchoring, or bridge occlusion in motion.

### 13.2. Visual rubric

Accept only when an observer confirms:

- water reads as one surface rather than a set of identical stickers; UVs do not follow the camera;
- shoreline foam and reflection clipping do not enter land, bridge, or unrevealed space, even at extreme zoom levels;
- a reflected sprite remains weaker than its source and fades downward, without mirroring UI or fog;
- haze adds depth between scene layers but does not obscure tasks, selection indicators, or the HUD;
- Rain/Snow matches the fixed diagnostic frame in dev.07–09 and the current authoritative state after dev.18; pause/2×/epoch reset behave coherently;
- footprints clearly alternate L/R, turn without drawing a line during teleport, and disappear through cover/age;
- the Medium baseline does not exceed its caps; any culling is visible in counters rather than hidden;
- none of these effects changes terrain, movement, paths, cover simulation, save data, or network authority.

## 14. Stop conditions

The phase is not allowed to be completed if:

- only a generated target or isolated shader probe exists, with no native game video;
- water/reflection only works in one static frame;
- weather in dev.06–09 does not come from the versioned diagnostic fixture, or after dev.18 does not come from host `WeatherView`;
- haze appears as a gray overlay;
- footprint stamps are based on command paths instead of displayed motion;
- a fallback silently disables a required effect;
- mass production of new buildings / characters began before combined dev.09 proof;
- performance numbers omit the machine, backend, resolution, preset, or capture method.

After dev.09, the art-asset library may scale. Until then, the main risk is not “missing beautiful sprites,” but that water, atmosphere, masks, and time do not yet form a stable system.

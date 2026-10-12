# AnalyticAtmosphere V1 — Space fog in the common array

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. This is a new `WaterAtmosphereSlice` contract, not a E10 contract,
the whole engine, the normal game start, the concept art or 120 FPS.

## Algorithm replacement, not visual requirement reduction

For this graph, the analytic density ** replaces the ** proposed in World FX §6
soft-quad bands / `HazeBandCache`. They remain obliged to name far,
The morning low fog, the fog at the open water, the individual colors, the reaction to the
time/weather, motion and scale of the camera, correct reveal/alpha and unchanged HUD.
The old cache/quad/asset-missing criteria cannot be defined by this
I'm not going to use analytical evidence. There's no new texture, fullscreen overlay or temporal cache.

The composition is applied to the linear HDR RGB of each visible contribution once:

- GroundLighting: after illumination, ** to ** mixing Reflection.
- Reflection: after P/N the original source, after lighting/emission,
  ** to ** alpha, fade and premultiply. It's the source, not the depth of the water.
- WorldForward: after lighting/emission, to alpha blend. In a new way
  binary `reveal=0` is cut off from the lighting, not publishing scene/meta.
- Composite and UI remain unchanged. The already fogged Reflection is not fogging up again.

## Space and Time

`view_far = -(P_camera.x + P_camera.y)`; near/far — in camera-relative tiles.
Panorama changes the depth term accordingly. The absolute XY is recovered through
camera origin **except** for noise and distance to water; These components are not bound
to the screen. Height — canonical render Z, not screen Y.

`far = smoothstep(near, far, view_far)`.
`height = exp(-max(P.z-baseZ,0)*heightFalloff)`.
`morning = morningDensity*morning01*height`.
`coastal = coastalDensity*waterProximity*height`.
The distance to water — is the exact external distance to AABB; The proximity is falling smoothly.
to zero on shoreRadius. The noise is low contrast, world-anchored, drifting only for
Clearly shown clock and wind direction. Stopping this clock doesn't add random/wall time.

Color — is a weighted combination ** of three distinct linear RGB**: far/morning/coastal.
Opacity is equal to `min(maxOpacity,1-exp(-opticalStrength*sumWeights*noise))`, maximum
0.20; opacity multiplies by reveal, only changes RGB, not alpha. There's no fog
a self-contained issuing card; The night's color and density need to be shared.
visual imaging with night lighting.

`EngineAtmosphereSettings` has one `time_hour` [0,24) and `weather_haze01` [0,1].
The adapter itself calculates morning01 through smoothstep 4→6 and 6→9, daylight through
5→9 and 16→21, optical/densities/colors through weather/daylight. You can't.
independently file the conflicting hour and morning. This is **the diagnostic presentation
inputs**, not ready host-authoritative weather, weather, forecast or save/network.

## Revealed-water carrier and transactional

GameView is passing through unknown cells and sources. AABB are formed only from actual
revealed `Terrain::Water` in the control window, the boundary of the tile center ±0.5. Rare
are joined only by the same X- boundary and the immediately adjacent Y:
So no right angle is covering land/unknown/hole. Maximum 32; I need 33
The field rejects the candidate to the publication frame/camera/picks. Disabled atmosphere
does not build areas, count=0 and tail=0, does not impose active-capacity on the old world.

The renderer checks the finite/ranges, ABI version, enabled, unit/zero wind,
ordered world bounds and zero inactive tail **until GPU acquisition**. Public
`Frame2D` does not prove the origin of the revealed water areas by itself;
For GameView this is separately correlated with ClientSession. Loop dynamic to actual
count; 32 — provisional carrier capacity, not measured performance budget.

## ABI and memory

| Field AtmosphereGpu | Offset | Content |
|---|---:|---|
| origin_time | 0 | absolute camera XY, shown seconds, morning01 |
| depth | 16 | camera-relative near/far, optical, maxOpacity |
| density | 32 | morning/coastal, baseZ, heightFalloff |
| far_color_noise | 48 | far linear RGB, noise amplitude |
| morning_color | 64 | morning linear RGB, reserved zero |
| coastal_color | 80 | coastal linear RGB, reserved zero |
| wind_shore | 96 | wind XY, tiles/sec, shoreRadius |
| counts_version | 112 | patchCount, ABI1, enabled0/1, zero |
| water_aabbs[32] | 128 | absolute minXY/maxXY, stride16 |

AtmosphereGpu = **640 bytes**, alignment16. New b0 wrapper = **1024 bytes**:
the old SurfaceLighting384 prefix + Atmosphere640, patches offset512.
The old graphs have an unchanging b0=384. b1–b3, sampler/storage/MRT slots are unchanged;
So the fifth UBO is not added. Three new programs: `atmosphere_ground_lighting`,
`atmosphere_material_forward`, `atmosphere_water_reflection`.

There are nine world attachments/two frame sets, **84WH bytes**, i.e.
174 182 400 nominal bytes for 1080p, plus individual available sky targets. Uniform
upload: 1024 bytes on GroundLighting and each lit forward/reflection draw;
This is not a new VRAM attachment, nor is it a measure of the overall/top driver allocation.

## What a Native Witch Should Prove

Real D3D12/RTX 3050, GameView/ClientSession, old graph vs disabled PNG/raw
carrier, morning/noon/night, clear/haze, paused/drift, UI exact, unknown source
non-submission, hidden surface emission, stored Reflection/deck/shore, pan and
zoom0.62/1.30/3.00. Separate really designed terrain panels with the same
The materials are the causal near/far, low/high and coastal-distance gates.
The 32-patch scene must be performed; 33-patch — save the previous rendered frame.
The incorrect inputs should deviate to GPU, the next valid frame — being accurate.

PNG — final GPU output, **not raw HDR**. Raws are called only loaded
WaterCoverage bytes. A separate hidden-surface test does not test disappearance anymore
the complex local light/caster; actual GameView unknown-source check is testing
The non-submission of an entire hidden source. Without specific evidence, not accepting
the video/temporal smoothness, reprojection tolerances, DPI/input, combined weather,
full night/weather authority, E10/E15–E17 or concept-faithful chart.

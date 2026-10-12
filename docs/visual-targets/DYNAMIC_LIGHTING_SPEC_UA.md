# Ribterra — mandatory dynamic lighting

> English translation of a historical project document. Technical literals and recorded results are preserved; these records describe their original snapshots and do not validate the current build. Original wording is available in import commit `7388a2f`.

**New bounded checkpoint 08.10:** [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md) changes Sun, Moon, ambient light, and atmosphere together from diagnostic hour/cloud/haze inputs. Fresh build02 and two actual native runs exited with code 0; 42 PNG+raw+JSON outputs were checked (44/44 exact). The checks causally covered solar and lunar shadow movement, cloud/haze isolation, horizon source on/off, and unchanged authored local light/emission. This is not authoritative time/weather, normal startup, concept art, or ≥120 FPS evidence; **E06 and all E01–E17 remain OPEN**.

**Partial authored checkpoint 08.10:** The [schema-2 importer/camera-relative adapter](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md) was checked with a fresh C++20 W4/WX build and two focused native loader runs. World triangles are independent of the raster caster mask; directional shadow pixels through the authored path remain NOT_RUN. Full Dynamic/GameView, concept fidelity, and ≥120 FPS remain OPEN.

**Partial engine-first checkpoint 08.10:** [SkyShadowSlice](SKY_SHADOW_CHECK_2026_10_08_UA.md) adds independent opaque-triangle D32 depth maps for the Sun and Moon and source-specific visibility on ground/typed P. This is fixed-camera diagnostic proof, not full Dynamic/GameView, art, or ≥120 FPS evidence. The new material b3 has an explicit SkyMoonLighting192 block (Moon32+Shadow160), not five uniform buffers. The later engine-first requirement takes precedence over the historical deferred status below; E01–E17 remain OPEN.

**Active engine-first checkpoint 07.10:** The user's later direct requirement takes
priority over the historical deferred/scaffold status below. [Independent direct Sun/Moon](SKY_DIRECT_CHECK_2026_10_07_UA.md)
is implemented only in explicit SkyContactSlice; directional silhouettes, time/weather,
all art categories, GameView, full Dynamic, and ≥120 FPS have not been accepted. The previous 384-byte sun/local
ABI was not expanded with hidden fields; a separate Moon32 block was added.

**Deferred by the user's latest decision.** The current work includes only the engine scaffold. The full system and switch belong to [future DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md), with no assigned release. The mandatory criteria below apply to the future enabled mode, not to 0.26.1 as a whole.
Date: 27.09.2026. Status: **PROPOSED / NOT IMPLEMENTED / NOT BENCHMARKED**.

This clarifies the user's requirement for [V2](GRAPHICS_ENGINE_PLAN_V2_UA.md). Lighting must change **terrain, facades, roofs, trees, and residents** when a light source or object position changes. An emissive PNG, a global tint, or a light spot confined to the ground does not satisfy this requirement. The algorithms below are a proposed engineering path whose suitability still must be proven in native dev.05/09.

## 1. Visible contract
Required controllable source parameters: position XYZ, direction, cone/radius, linear color, intensity, and enabled. Light and shadow update as the camera and objects move without re-exporting PNG files.

| Scene | Required behavior |
|---|---|
| Window off→on | Only the emission mask glows; terrain and eligible neighboring surfaces inside the cone are illuminated |
| A resident passes the source | The lit side, intensity, and color change according to normal/position; carried cargo responds too |
| A test light moves from left to right | The opposite side of the visible facade becomes lit; a painted highlight does not remain permanently |
| A rock or resident crosses the beam | Visibility from that specific source changes; the other source continues lighting the area |
| Day→night | Directional and ambient contributions decrease while local sources remain legible |
| Dry→wet | Terrain roughness/specular response changes while light position and causal occlusion remain intact |

The moving source in the proof is a diagnostic control. It does not add a new torch mechanic, inventory item, or network command to the demo. Production lights derive from visual profiles and the object's actual permitted state.

## 2. Selected path: lit 2.5D with shape data
Keep the fixed camera, painter order, SimCore, and RmlUi. Terrain uses geometry/world position; building, unit, and tree sprites receive **neutral albedo + normal/material map + camera-depth map + emission**. This dynamically calculates lighting on the visible surface using sprite shape data. It is not a full 3D scene and does not support arbitrary camera rotation.

If this method cannot produce acceptable lighting on a small kit or fit within a measured budget, dev.09 fails. The algorithm or geometry path must change before mass art production. Do not move facades back to baked-only materials and call that completed dynamic lighting.

## 3. Geometry for each sprite pixel
Core render metric: X/Y in cells, Z = legacyElevation × 2.4 / 29. Canonical constants and CPU helpers are defined in `include/ribterra/world_projection.hpp`. Affine projection before zoom:

```text
sx = 29 * (x - y)
sy = 14.5 * (x + y) - 29 * z
d  = dot(normalize(1,1,1), float3(x,y,z))

M = rows((29,-29,0), (14.5,14.5,-29), normalize(1,1,1))
local_xyz = inverse(M) * float3(local_sx, local_sy, local_d)
world_xyz = object_world_anchor + local_xyz
```

`local_sx/local_sy` come from the source pixel relative to the pivot and are converted through `export_pixels_per_reference_pixel`; do not use scaled framebuffer coordinates without reversing zoom. `local_d` is linear camera depth unpacked from R16_UNORM using per-asset `depth_min/depth_max`. This is **texture data**, not a runtime depth target. For a source with a different basis, the exporter applies the full affine transform into the render metric before writing the maps. The normal is transformed by the inverse transpose and then normalized; simply swapping normal coordinates is invalid under nonuniform scale.

The sprite does not rotate arbitrarily at runtime. Every facing/animation frame exports normal/depth data in the shared world basis of the same camera. The data covers both the body and actual cargo. A seasonal mask that does not change geometry can reuse the maps; a new roof or changed silhouette requires new depth/normal maps.

Calibration test: offline XYZ points → export → shader reconstruction → reproject; error ≤1 native pixel at the control zoom and world error ≤0.02 tile in valid opaque pixels. These are the initial acceptance tolerances. Check house corners, the roof ridge, feet, heads, and log ends; do not evaluate only the center of the PNG.

## 4. Material contract
| Map | Initial format | Data |
|---|---|---|
| Albedo | RGBA8, sRGB source | neutral color + alpha; no directional/local light or ground shadow |
| NormalMaterial | RGBA8 linear | RG: octahedral normal; B: roughness; A: bounded authored AO |
| CameraDepth | R16_UNORM linear | depth range metadata; invalid alpha is not used for reconstruction |
| EmissionMask | R8 linear | window/fire coverage; emitter color/intensity comes from the profile |

These are the initial GPU formats; dev.01–02 verifies their support and usage. The internal loader must support the data formats; do not pass R16 through the old 8-bit RGBA image loader. If the source is a 16-bit PNG or EXR, the offline exporter converts it into a verified runtime data blob with width, height, format, byte count, and hash. The shipping client does not need a full EXR decoder.

Do not derive authoritative normal/depth data solely from the brightness of an arbitrary AI-generated PNG: a dark window does not imply a geometric cavity. Such reconstructions may serve as drafts, but they must pass the same geometry tests. The recommended source is a coordinated offline master.

World materials return linear radiance; emission is added after lighting. The UI remains outside world lighting. Do not apply a second night tint over the final light.

## 5. Light calculation
For every visible point P and normal N:

```text
L = light.position - P
distance = length(L)
dir = L / max(distance, epsilon)
range_fade = saturate(1 - distance*distance / (radius*radius))^2
cone = smoothstep(outer_cos, inner_cos, dot(-dir, outward_direction))
visibility = ray_visibility(P + N*bias, light.position, candidates)
diffuse = albedo * max(dot(N, dir), 0) * color * intensity
local = diffuse * range_fade * cone * visibility
result = albedo * ambient * authored_AO + sun_term + sum(local) + specular + emission
```

Point lights use cone=1. Spotlights use normalized XYZ direction; a ground-only XY direction is insufficient for lighting a roof or person. The future LightGpu in Core contains float4 directionXYZ/outerCos and innerCos in the fifth float4, with an 80-byte stride. Before DL-01, verify C++/HLSL offsets; the current scaffold does not yet contain these GPU structs. Use one shared declaration/offset test, not separate structs in Core and FX.

For ground/water and rough lit sprite materials, bounded Blinn-Phong specular is acceptable: H=normalize(Ldir+V), exponent=lerp(8,96,1-roughness), strength from the material family, multiplied by N·L, visibility, and range/cone. This is a controlled model for a stylized result, not full physical PBR. Soil must not look metallic. Wetness lowers roughness within defined limits and increases specular strength; water uses its own flow normals. Do not multiply ambient light by the shadow of a specific local light.

The Sun is a directional source with a versioned azimuth/elevation curve and linear color/intensity supplied by the calendar/presentation adapter. The initial production elevation range is 20°–65°; below that, fade to zero without creating an infinitely long shadow. Record azimuth and approved control states in dev.03; diagnostics control direction independently to validate normals. This is an artistic game rule, not a geographic solar simulation.

## 6. Occlusion and shadow limits
The CPU gathers candidates by light influence, visibility, and layer; moving units add updated capsule prisms, while buildings and trees use author-validated OBB/capsule sets. For segment P→light:

1. Transform XY into the OBB basis; intersect the X/Y slab intervals and Z height slab.
2. An intersection exists when a nonempty t interval lies inside (bias, 1-bias).
3. A capsule uses the XY segment/capsule intersection plus the Z interval. Contact within epsilon must not cause random flicker.
4. Exclude the receiver owner from this approximation's self-occlusion. This does not produce true self-shadowing between details of a single facade; accept that limitation separately and explicitly.
5. Do not disable one object's occlusion of another because the buffer lacks space. Record overflow and fail acceptance.

Sun shadows on terrain use authored silhouette/height and receiver projection; local shadows use visibility from a specific source. Shadow precision is limited by authored geometry: a capsule does not reproduce every finger. Roof shadows on their own facade, light through complex interior rooms, GI, ray tracing, volumetric light shafts, and arbitrary 3D self-shadows are not promised. These are quality limits, not permission to disable lighting on every facade.

Demonstrate ground shadow and lighting on another object separately: a silhouette cannot simultaneously impersonate a ray blocker for every source. Move a window anchor outside the owner prism and direct its cone outward; do not remove the owner from every other light's lighting query.

## 7. Integration, cost, and order
The canonical render graph and formats are defined in [Core](RENDER_CORE_SPEC_UA.md). Ground lighting consumes albedo, normal, world position, and flags; the world sprite shader calculates light on the reconstructed surface itself. A finished ground-light buffer is not overlaid on a resident or roof as a screen-space tint.

Light lists are built for ground tile/receiver batches and sprite bounds. Initial per-receiver caps are 4/8/12 local lights for Low/Medium/High; per-light occluder caps are 16/32/48, with global caps of 128/256/384. These are **unmeasured** numbers. Distance/range culling must prove that a discarded source does not affect the receiver. If more sources actually overlap, do not call their disappearance correct: record the overflow and revise the budget or content density before acceptance.

Additional data maps are charged to the asset ledger. Example without reuse: 256 frames ×128² × (4 albedo +4 normal +2 depth +1 emission) bytes =44 MiB without mips, ≈58.67 MiB with a full mip chain. Geometry and cargo variants can increase the frame count. The initial 64 MiB resident budget passes only if the actual set fits; arithmetic alone is not proof.

Dev.02 fixes the GPU/material contracts; dev.03 creates a neutral-lit kit and geometry maps; dev.04 adds position reconstruction and receiver/occluder validation; dev.05 proves dynamic light on terrain and sprites; dev.09 combines all effects with measurement. Dev.16/17 extend already accepted lit materials across the available catalog.

## 8. Mandatory tests for dev.05/09
| ID | Check and criterion |
|---|---|
| DL-01 | Light off/on: diffuse/emission vary separately; albedo asset hash unchanged |
| DL-02 | A source moves around the control cube/house: normals produce the correct lit side |
| DL-03 | A resident and cargo pass through a light cone: lighting and shadow change without baked tint |
| DL-04 | An occluder blocks one of two differently colored sources: only that source's contribution disappears |
| DL-05 | Roof/facade/terrain receive light from their distinct N/P; uniform screen glow fails |
| DL-06 | Reproject depth/normal data at three zoom levels; no drift or frame/facing discontinuity |
| DL-07 | Hidden source/caster does not change revealed output; pan/zoom does not change light in world coordinates |
| DL-08 | Sun/night/wetness transitions, pause/2×/reload; emission does not darken a second time |
| DL-09 | Low and Medium with 20–30 residents and weather: raw frame-time/VRAM/overflow logs, with all required categories preserved |
| DL-10 | Missing normal/depth, bad range, or NaN: reject the asset transaction; albedo-only legacy does not count as lit proof |

Readiness requires native unedited video and measurements from the same build. This is a specification update, not a claim that dynamic lighting is already implemented.

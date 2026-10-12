# Ribterra — graphics production for Renderer V2

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Dynamic lighting is deferred:** the normal/depth profiles below are required for the future lit mode; the current scaffold does not load them. Do not start mass lit export until DL-01–08 resumes. [Plan](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md).
Date: 27.09.2026. Status: **future production specification; the kit has not been created yet**.

The [V2 plan](GRAPHICS_ENGINE_PLAN_V2_UA.md) defines the work order, [Render Core](RENDER_CORE_SPEC_UA.md) defines the GPU and coordinate contracts, and [World FX](WORLD_FX_SPEC_UA.md) defines effect behavior. This document specifies the source materials those algorithms require. A generated image does not contain production-ready masks, geometry, attachment points, animation, or runtime data.

## 1. Production method and scope
The main method uses one set of master models for buildings and characters, orthographic offline rendering to sprites, controlled art processing, and repeatable export. 3D is used to produce assets; the game remains 2.5D. Manual drawing is allowed when it passes the same asset-profile and geometry checks. Image generation is suitable for exploring shape and palette, but not as an automatic source of compatible animation frames or material maps.

Initial kit: house, warehouse, workshop, tree, stone, one resident with cargo, ground/shore/water, and bridge. Produce only what dev.03–09 requires. Start the full catalog only after the shared native proof; otherwise, a camera or lighting error will be multiplied across the entire kit.

Art direction: dark wood, cold stone, blue cloth, and warm local lights; large readable silhouettes and secondary details. Detail contrast decreases with distance. Do not compensate for a resident's small size by oversharpening the entire screen. Settlement density comes from composition and real work areas, not random decoration in every cell.

## 2. Camera, size, and asset profile
The renderer's output projection uses a 58×29 diamond before zoom. Actual footprints come from [building_catalog.hpp](../../include/ribterra/building_catalog.hpp), while sprite projection and selection come from [view.cpp](../../src/client/view.cpp). No export changes the simulation footprint.

Initial offline camera: orthographic, azimuth 45°, elevation about 30° for a 2:1 diamond. This starts calibration; it is **not proof of conformity**. Verify axis direction, handedness, vertical unit, and scale against the render calibration grid. Use a cube labelled +X/+Y/+Z, a flat footprint, and four contact points. Compare the native fixture at all three zoom levels before exporting the first building; store the accepted camera matrix and export settings in source control.

`VisualAssetProfile` schema 2 is a proposed new contract, not an existing JSON field:

| Field | Data and validation |
|---|---|
| `asset_id`, `revision`, `source_hash` | Stable semantic ID, export version, link to the edited master |
| `albedo`, `emissive`, `caster_mask`, `material_mask` | References to separate atlas regions; a missing required channel fails kit validation |
| `normal` | Required normal/material and camera-depth data for future Dynamic2_5D facades and residents; not required for Legacy/BakedCompat |
| `pivot_px`, `ground_anchor`, `bounds` | One ground anchor for all seasons/phases/cargo states; bounds cover the alpha silhouette |
| `footprint_id` | Reference to the current footprint definition, without a separate copy of the rules in the PNG |
| `base_height`, `caster_height` | Render tile-equivalent units defined by Core; one adapter converts legacy elevation |
| `occluders` | Authored footprint polygons as source data; the exporter validates and approximates them with bounded OBB/capsule prisms |
| `window_lights` | ID, anchor XYZ, outward direction/cone, range, linear color/intensity, owner, allowed receiver kinds |
| `reflection_class` | None/Building/Vegetation/Unit, anchor, opacity, and eligibility; objects do not qualify automatically |
| `surface_class` | Land/Water/Bridge/None; the deck mask is distinct from a decorative roof |
| `snow_mask`, `wet_response` | Where snow/wetness may appear; not windows or an arbitrary alpha rectangle |
| `state`, `season`, `facing`, `frame`, `cargo` | Keys only for supported game/animation modes |

A window must have an emission mask, an external anchor, and a cone on the correct side of the building. Do not paint the light patch into the ground albedo. Initial kit: two reference windows per house and up to four emitters per complex structure; anything more requires a separate measured budget. Do not place a light origin inside its owner's occluder, or the building will occlude its own source.

## 3. Maps to export
| Channel | Data | What not to bake |
|---|---|---|
| Albedo RGBA, sRGB | Material color, alpha silhouette; soft authored AO for readability | Ground shadow, window-light patch on the ground, or night darkening across the entire PNG |
| Emissive, linear mask | Windows/forges/lights; intensity and color come from the profile | Square glow around the building or all light-colored plaster |
| Ground normal, linear | Consistent tangent/world basis for ground and water | sRGB gamma or a normal from a different camera |
| Material mask, linear | Roughness/wet response/snow eligibility/coverage for family layout | Four different semantics of a single channel without material family |
| Caster alpha | Silhouette for directed shadow, separate from selection | Blue selection disc or old baked ellipse |
| Snow top mask | Roofs, horizontal surfaces, suitable ground | A white window or snow on the river without ice mechanics |
| Water flow + shore mask | Flow direction/magnitude, smooth shore transition | A stationary-building reflection painted into the tile texture |
| Footprint alpha/normal | L/R imprint and the edge of compressed snow | A large dark oval beneath every step |

Channels use documented packing within each material family. Color maps and data maps do not share an atlas under ambiguous color spaces. Linear masks do not receive automatic sRGB decoding. Do not premultiply an already premultiplied export; Core defines the boundary conversion. Dynamic2_5D albedo may contain only neutral material variation and soft AO, with no baked directional or local light. The Dynamic Lighting specification defines normal/depth reconstruction. Future Dynamic sun elevation and intensity may change within agreed limits; the base V01–V05 stages do not add a new sunlight renderer.

Roughness does not imply a full PBR renderer: only the Core ground/water specular formula is used. A facade receives dynamic diffuse from P/N, but not full physical reflection. Depth requires an R16_UNORM data texture with range metadata; the loader must validate format and byte count separately.

## 4. Resolution and atlas budget
The source master's size is not its visible in-game size. For each family in dev.03, record projected bounds at zoom 0.62/1.30/3.0. Initial export buckets: buildings at 256 or 512 px on the longer side, residents at 96–128 px per frame, and terrain/FX at 128–512 px depending on repetition. These numbers remain hypotheses until native capture; do not increase everything to 2048 px “for quality.”

The [loader](../../src/client/game_art.cpp:70) limits each atlas page to 4096×4096; this is an upper bound, not a recommendation to fill every page. Plan for 2048×2048 pages, with separate pages for different sampler/color-space families. Packaging requires at least a 4 px extruded gutter at the base level; the mip generator must preserve alpha coverage without sampling a neighboring region. If a mip level cannot preserve isolation, set a maximum LOD or add more padding. Verify native separation, not only the atlas preview.

Uncompressed-cost example: 2 resident types × 8 directions × 4 walking frames × 4 cargo types = 256 frames. 128²×256×4 bytes = 16 MiB for an RGBA8 map without mips; the full mip chain is ≈21.33 MiB. Three independent maps of this size are ≈64 MiB. This is an arithmetic example, not a requirement to create all 256 frames: compatible masks and cargo overlays should be reused.

The initial **target** resident atlas budget is ≤64 MiB, buildings/environment/FX ≤192 MiB, and total content textures ≤256 MiB on Medium. This excludes render attachments, UI, upload buffers, and old atlases retained during reload. Maintain a byte ledger: width×height×bytes×mips×pages; measure resident and peak memory during load/resize. If the budget does not fit, reduce duplicated variants and decorative forms; retain the emission/caster metadata required by accepted effects.

## 5. Buildings and environment
The full matrix comes from the available catalog after Road is removed: **11 types + Camp**, including the current House II/Workshop II. Record the current list in dev.01 instead of copying unavailable buildings from the concept. Selected/hover state uses a runtime outline/marker rather than a separate full PNG copy. Ghost/invalid preview uses the same footprint/anchor and a clear color treatment.

For each type:
1. Ground anchor, opaque base, entrance, and work area match the game data.
2. Construction stages are readable without the inspector; their count matches current progress, without invented separate jobs.
3. Summer/winter variants use identical pivot, footprint, and camera; a snow mask may replace a second albedo if the native result passes.
4. Level II differs in silhouette and functional detail while retaining the current entrance location.
5. Forge/smoke/fire reflects the real active state when it signals work. Decorative lighting is marked separately in the profile.
6. Storage piles use discrete fill bands from actual stored stock; carried/reserved quantities are not added twice.

Terrain kit: dry/wet/snow surface families, shore transitions water→ground→grass, accessible ramp, cliff face, and bridge approaches. Seam masks and shared vertices must match the topology. The bridge masks the water receiver only with its deck mask, leaving water visible beneath open sections. A distant backdrop does not make terrain playable.

Vegetation: several coordinated silhouettes and scales, groups driven by the world seed, repetition control, and a local wind pivot. A tree must not sway from its roots. Do not stretch a stone with an accidental nonuniform scale that breaks its lighting. Do not place opaque decoration over a real entrance or passage.

## 6. Residents, cargo, and footprints
The current [animation](../../include/ribterra/unit_animation.hpp) and [cargo selection](../../include/ribterra/resident_art.hpp) define the source semantics. Start with one character, 8 facings, 4 walking frames, idle, and empty/logs/sack/current-cart cargo states. New horses and physical carts are out of scope.

All frames share one ground anchor between the feet; pelvis/head scale does not change between facings. The `left_foot_local/right_foot_local` and `contact_phase` metadata are required by the presentation emitter, but authoritative movement does not depend on animation. Step events are bounded by accumulated movement distance: idle/teleport leaves no tracks. If 4 walking frames produce visible sliding in native proof, first correct timing/poses; increasing to 6–8 frames is a measured art-budget change, not a hidden promise.

The cargo overlay has an attachment point and caster silhouette; logs must not pass through the head, float when facing changes, or remain after actual delivery. Produce the character shadow for the art-ready branch as well. Otherwise, even a good-looking resident will appear to hang above the ground.

Footprint stamps use a separate small atlas, two feet, a depth/normal appearance, and alpha fade. Prints are not part of the walking PNG; their positions remain in the world after the character leaves. [World FX](WORLD_FX_SPEC_UA.md) defines their data and lifetime.

## 7. HUD from the concept
Frames, icons, and decorative corners remain separate from text. Text remains real RmlUi/localization text: do not bake Ukrainian labels or numbers into PNG files. The nine-slice frame defines padding/minimum height and remains readable at 720p/1080p/1440p and 100/125/150% UI scale. Check small icons at their actual displayed size, not only in an enlarged export.

Preserve the main zones from the preferred concept: resources at the upper left; forecast and temperature at the top; contextual hazard at the right; compact inspector at the lower left; four primary actions at the center; minimap at the right. Weather/fuel/insulation shown in a mockup does not imply available data. Weather appears after dev.14; fuel and insulation are not part of this version.

The [UI contract](AGENT_VISUAL_CONTRACT_UA.md) defines the real hierarchy and adaptation. Decorative ornaments must not displace the reason an action is blocked or the player's next action. The base HUD is implemented in V01 on the current renderer. Full Dynamic proof is not a prerequisite; lit assets are accepted later in the DL branch.

## 8. Export integration and validation
Extend [register_reference_art.py](../../tools/register_reference_art.py), [register_resident_art.py](../../tools/register_resident_art.py), [atlas_tool.cpp](../../tools/atlas_tool.cpp), and the schema loader. A separate validator checks the profile, channel presence, dimensions, premultiplication/color space, gutters, bounds, pivots, positive ranges, finite floats, winding/degenerate occluders, and permitted emitter count. Core validates HLSL contracts; a PNG validator cannot replace those checks.

Export must be repeatable: master revision → camera/settings hash → channels → atlas → manifest → package. The manifest includes source/license provenance for external assets. Generated assets record their origin. Do not replace the source master with a final PNG that lacks reproduction parameters.

Negative tests: missing mask, invalid atlas region, bad pivot, NaN light, negative height, unsupported schema, allocation failure. The loader rejects the entire candidate and retains the last valid set. Do not treat a missing emission mask as a successful “light off” state. The production package must contain the asset manifest checked during acceptance.

## 9. Acceptance and effort
| Gate | Required result |
|---|---|
| ART-A / dev.03 | Calibration cube/grid; a small kit with all the necessary masks/anchors; valid schema 2 export |
| ART-B / dev.05–08 | Effects use the specific metadata; native on/off/occlusion/water/footstep video; no double baked lighting |
| ART-C / dev.09 | Kit accepted in the shared scene: day/night/rain/snow, zoom 0.62/1.30/3.0, camera pan, slope/bridge/FoW; target budgets measured |
| ART-D / dev.15–17 | The full available catalog, terrain, and directional residents match the accepted kit; runtime contact sheets cover all available states |
| ART-E / dev.21–23 | Native package, alpha/mips/VRAM/resize/quality checks; no missing regions or debug fallback |

At dev.03, record the actual time required for one accepted building, seasonal state, character, export, and correction. After dev.09, estimate the rest of the catalog as uncovered state families × measured effort + integration + validation. Any earlier calendar estimate would be fiction. An agent cannot credibly call a kit accepted merely because that agent generated it.

Required evidence: source/extracted atlas contact sheet, byte ledger, validator log, native scene captures/video, and a deviation list. The art reviewer checks consistency of scale/materials; the engineer checks contracts/occlusion/memory. These are responsibility roles, not a claim that such a team has already been hired.

## 10. Primary sources for the method
An orthographic camera and separate render passes provide reproducible output channels, but those channels still require integration into Ribterra's renderer:
- [Blender: camera projection](https://docs.blender.org/manual/en/4.1/render/cameras.html)
- [Blender: EEVEE render passes](https://developer.blender.org/docs/features/eevee/render_passes/)

These sources justify the offline workflow; they do not prove that Ribterra already supports these effects.

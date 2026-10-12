# Ribterra — source art set 2026-09-09

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Update 0.11.1: Runtime copies of these sheets are made with the user's direct permission. The originals in this folder remained unchanged. The current integration is described in `docs/ART_0_11_1_UA.md` from the root of the project; The technical status below describes the source PNG files specifically.

Painted with built-in image_gen without sub-agents. The images are created and edited exclusively by the generator. PNG copied without changing pixels; scripts were used only for storing, reading headers/hashes, and creating a text gallery. The starting concepts have not changed.

## What's in the set

10 sheets, 189 images/states. This counts the drawn cells, rather than individually sliced PNG files.

| File | Contents | Count |
|---|---|---:|
| buildings-core.png | House, camp, farm, warehouse | 20 |
| buildings-civic.png | Tavern, barracks, temple, town hall | 20 |
| buildings-production.png | Sawmill, smithy, mine | 15 |
| units.png | 12 types of units in the front and rear views | 24 |
| nature.png | 4 trees and winter variants, bushes, stump, log, grasses | 16 |
| rocks-mountains.png | 4 rock types, ores, crystals, cave, mountains and winter variants | 16 |
| terrain-materials.png | Grass, soil, road, water, ice, snow, rocks, floors | 16 |
| ui-kit.png | Buttons, panels, frames, indicators, flag, divider | 16 |
| icons-cursors.png | 24 icons + 6 options of the gold cursor | 30 |
| fog-masks.png | Open/unknown area, transitions and atmospheric noise | 16 |

11 building types: 10 ordinary buildings plus Townhall. Read building sheets by row; the five columns show foundation, frame, construction completion, completed summer building, and completed snow-covered building.

## Viewing

Open index.html in a browser. The categories are on the left, and the unchanged authored concept is shown next to the sheet. The “Original size” button displays the image without fitting it to the available width. The gallery runs locally without external libraries or network services.

## Actual technical status

- All 10 saved files are — PNG RGB (color type 2), without alpha. For the object sheets, the generator created a dark background. It's part of the picture.
- These are source art sheets, rather than ready runtime atlases with verified transparency and coordinates.
- The generator failed to meet the exact dimensions requested. Actual sizes, bytes and SHA-256 are given in files.json.
- The grid in the prompts describes the layout. The exact rectangles, common pivots, padding/extrusion and footprint matches are not calibrated. Automatic uniform slicing without inspection is not recommended.
- The summer and winter versions are painted as the same building; The pixel stability of the silhouettes between the states is not guaranteed.
- Units have static positions and two angles; walking, working, fighting, and a full set of directions are not shown.
- The cursors are drawn in sheets, but separate cursor files, hotspots, and system integration are not created.
- Surfaces are asked as tileable, but seamlessness is not verified. Water is not animated.
- In fog masks white means opaque fog, black means open area. This is RGB-masks; The game should read brightness like a mask, not alpha. The layout and intersection of the passes still need to be checked.
- UI is the art of components; 9-slice, interactive states and localized text are not integrated. The generator drew the indicator fills together with their frames.
- The existing game code and runtime asset registry are unchanged by this set. Previously existing local changes are preserved.

## Visual inspection

All original results were reviewed after generation. The gallery was also checked in the connected browser: switching to civic buildings, displaying a sheet beside the concept, switching to icons, and scrolling to all six cursors. This checks the artwork; it does not validate game rendering or establish complete fidelity to the concept.

Vegetation and icons received targeted background corrections from the generator; the overly thick icon outlines were also softened. Scripted image processing was not used, as explicitly requested by the user.

## Reproduction and sources

production-plan.json contains the original prompts and requested layout. generation-edits.json records the prompts for two refinements. files.json records the files actually saved. references/ contains unchanged copies of five authored concepts for standalone comparison.

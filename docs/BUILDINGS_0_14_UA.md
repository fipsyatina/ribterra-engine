# Ribterra 0.14.0 — construction and mining

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Ten buildings were added: a mine, workshop, a house of religion, a pub, a cartograph house, a research house, a smelting house, a fishing station, a library, and a military house. Along with the house, farm, and composition, the catalog contains 13 available for compilation.

## How to Build

The **«All buildings[** button opens a directory with icons, names and prices. Select the building and place it on a cleared plot. The previous view shows the admissibility of the accommodation. The materials are stored in physical reserves, delivered by the residents, and then the construction begins. The two lanes represent the delivery and construction work. Unfinished orders can be cancelled due to the available delivery interface.

| Building | The tree | The Stone | Food |
|---|---:|---:|---:|
| Mining | 40 | 30 | 0 |
| Workshop | 45 | 25 | 0 |
| Houses of Religion | 50 | 60 | 0 |
| The pub | 60 | 35 | 10 |
| The house of the cartographer | 40 | 25 | 0 |
| Research Houses | 65 | 65 | 0 |
| The shipping works | 45 | 80 | 0 |
| Fishery station | 30 | 15 | 0 |
| The library | 70 | 55 | 0 |
| The Military House | 75 | 65 | 0 |

## Mining and fishing

New worlds begin without a mine and underground passageway. The mine can be placed on a free-rooted or rocky cell. Only completion opens the transition between the spheres, clears the underground terrain 3×3 and opens up the nearest territory. Residents can come down and back through the mine. The button for the underground layer and the Page Down also explain the need for construction.

The fishing station needs a grassy beach with clear water on one of the four adjacent cells. The feed station creates 6 units of food for every 400 touch in a physical stool near the building.

Workshops, a house of religion, a pub, a cartographic and research house, a smelter, a library and a military house in this version have a full construction cycle, preservation, network support and their own look. Manufacturing recipes, research trees, religious effects, map discoveries and training troops for them have not yet been implemented.

## Drawings and integration

The original sheets were created by 10 through **built-in imagegen**: three stages of construction, a completed summer and winter building, and a separate icon. These are 50 game footage and 10 new icons; The three existing structures also received catalog icons from their existing textures.

- Originals: `art-source/production-v2/*.png`.
- Full queries and origin: `art-source/production-v2/prompts.json`.
- Reproduced preparation: `tools/prepare_building_art.py` — mechanical separation of transparent frames, common scale and bottom binding point, removal of small fragments of adjacent cells, packing through `AtlasTool` into a new export catalog.
- Active atlases: `assets/art/buildings-014-0.png`, `buildings-014-1.png`, records in `world.json`.
- Icon: `assets/ui/art/building-*.png`.

The winter look is used in both winter and in the ever-snowing biomes. Buildings take into account the surface of the relief from the 0.13 version.

## Compatibility and verification

The storage format remains 8; the rules — 8, generator 5, protocol 10. Old preserves migrate with their own new passages to keep the inhabitants from being trapped underground. For a new beginning, without a mine ready, create a new settlement. Both participants in the network game must use 0.14.0.

The full scope: **55/55 tests**, `artifacts/buildings-tests.log`. After the final changes, a separate scene check was added: overall **56 of the various tests** covered with a full run and purposeful repeat checks. The latest results are — `artifacts/buildings-ship-tests.log` and `artifacts/buildings-final-ui-tests.log`.

The new inspections cover the delivery and installation of all 13 types, storage in different phases, command and building in a network protocol, mine clearance without creating passage, movement to both sides, placement on the rock, coastal condition and food production. Verified loading of historical records and the immutability of their controls.

The images of the actual render Direct3D 12 on the NVIDIA GeForce RTX 3050 Laptop GPU: `artifacts/buildings-release/new-buildings-summer.png`, `new-buildings-winter.png`, `new-buildings-catalog.png`, `new-buildings-compact.png`. The spot in the pictures  is a specially laid test scene for inspection of all buildings; The new game starts with a regular camp. The device report: `building-render-report.json`.

The packaging is ready: `dist/Ribterra-Windows-x64-0.14.0.zip`. The completed file: `dist/releases/0.14.0/Ribterra/Ribterra.exe`. The package 0.13.0 is kept separate.

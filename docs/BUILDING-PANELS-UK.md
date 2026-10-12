# Building panels by reference

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Update from 15.09.2026. It's a game interface with English subtitles, not a new implementation of all the mechanics in the concept art.

## Launch

Close the previous game and open **Ribterra - New Graphics** on your desktop. The updated collection is at `dist/building-panels/Ribterra/Ribterra.exe`. The old process already running does not automatically update.

Press the device, and the card opens on the right. Shift + press adds or removes the structures of choice. The x button closes the card and takes the choice.

## Declaration and Action

All 20 types have a common layout: dark floor, gold frame, name, current building miniature, level, condition, main action and improvement block. There's no deposit of residents and researchers.

The farm, fishing station, workshop, logging plant, mill, kitchen and clay quarry show the available recipes, quantity, materials, worker and actual ordering progress. The quantity is rounded to the entire production batch; The exact result is seen in `Order per building` before dispatch. The time of work does not include the journey and the waiting for materials.

`Apply to` specifies the area of the new production order: this building, selected structures of this type or all of its own structures of this kind. Each receives a given volume. The settlement uses free adult residents without taking participants from active assignments or people under the control of another player. Occupied buildings and shortages of personnel are reported as missed orders. There are no new endless tasks.

Group orders are sent in a sequence with the server's confirmation and current world status. Repeated building ID does not create duplicates. `Stop Order` refers to the current order for a particular building; the server may reject cancellation during the transfer of load — then it should be repeated after the transfer is completed.

The camp opens up the existing settlement rules, the stockpile, the mine, the underground layer if there is an entrance, the road, the extension of construction. An unfinished building can be cancelled. Foreign buildings are only available for viewing.

## What is Still Unavailable

The research cards can be chosen at will. The research itself, its costs and time have not yet been realized, so `Start Research` is turned off; There's no such thing as a fictitious progress. The existing improvements in the old archives are reflected, but the interface does not offer new improvements in research bypass.

The following mechanics are also marked unavailable: heating individual homes, changing field size, rules for a separate warehouse, recalling a squad assigned to a mine, temple services, tavern visitors and budgets, a strategic map and minimap, smelting, library education, and squad training. Global indicators are labeled as settlement indicators, not as values for a specific building.

## Validation

- Real frames Direct3D 12 for all 20 types and screen of choice for the study: `artifacts/building-panels/`.
- Pressing a building on a map, closing a card, selecting a command area from the keyboard, two production tasks with different occupants.
- Two network customers see the same order; repeated buildings do not duplicate them; The storage and loading keeps the task.
- Tests `ui`, `client_input`, `economy` have been passed.

The frames show specially arranged structures for the inspection of the panels. This is not an example of an automatic ready-made city.

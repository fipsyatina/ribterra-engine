# Ribterra 0.26.1 — compact HUD V01

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

0.26.1 only realizes the first graphical stage V01: the active RmlUi HUD became compact and released the center of the screen. Land, camera, projection, lighting, buildings, plants and inhabitants have not been replaced; V02–V05 remain separate future stages.

## What the Player Sees

- F1 hides the whole game HUD; Repeatedly pressing it back without pausing, dropping a choice or changing the state of the world.
- The top left shows the available trees, stones, and food and population. A click on the resource opens a full inventory panel, a click on the population — settlement.
- A good food warning is shown at the top right when food is not available or when the inhabitants are hungry. It reveals the details of the inventory, not imitates the new mechanics.
- Below on the left is a brief inspector of a resident or building. The  Details» button opens up the available full order, health, cargo, production and improvement data. For group selection, only the number and joint actions are shown.
- Below the center are Build», Collect,  Name» and Delete». The first three open up the current directories and dashboards. The unavailable action has a real reason: there is no choice, no choice out of control, or construction is only possible on the surface.
- Below on the right is a pause, 1×/2×/ 4×, stocks, enterprise, save/load, help and menu. Balls, calendars, chat and control of ownership remained available.
- On the 1.15 and 1.30 scales, the resource signatures are reduced, and the inspector is raised above the bottom row to avoid overshadowing the action.

## The behavior of the victim

The button only deletes current customer interactions: location of the facility, location selection, or drag-selection. It does not cancel an existing construction, production line, or a resident's order, nor does it emulate Escape.

## Compatibility

The rules **17**, protocol **19**, format of the file **8** and generator **5** are not changed. The version window for this format is extended to 0.26.1; No new simulation field, safe or network message is added.

[Local checks 0.26.1](verification-0.26.1-uk.md).

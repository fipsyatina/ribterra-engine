# Ribterra 0.26.3 — V03: buildings and people

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

0.26.3 connects to the game a pre-prepared detailed object-art as a whole system. Map, river, surface and compact HUD from V02 are preserved; Rain, temperature, snow simulation and dynamic lighting are not included.

## What changed

- The only `building_visual` resolver chooses `stage1/2/3`, `ready`, `winter`, and the real `level2` from the actual condition of the building.
- A separate version of the II level is available only for implemented improvements: the House and Workshops. In winter, their silhouette does not return to level I.
- In the 30.09 rework, the visual scale of buildings is increased by 35% from the catalog size without changing the simulation footprint, entry cell or isometric projection 58×29. The bridge and the legacy road are not scalable.
- Directional residents have become more readable; All 8 directions, 4 phases and two images are preserved. The bars, bags, and bags depend on the actual item and the quantity carried.
- Before each art-ready inhabitant is painted a small world-anchored contact shadow. The vein has a wider base; It's not a sun projection shadow.
- Manufacturing smoke and fire appear only for real active job in phase `Working`. The night decorative accent remains a distinct presentation mark, not a proof of production.
- The Inspector uses the level-II preview for House II and Workshop II. Picking uses the same resolver and scale as rendering.
- Added art passport with hashes, license notice, size and runtime policy: `visual-targets/v03-asset-passport.json`.

## Compatibility

Engine: `0.26.3`. Protocol `20`, rules `18`, generator `6` and save format `8` remain unchanged because V03 does not change the simulation, network state or world generation. The old worlds are being loaded in the same way; The type of building is determined by the existing upgrade data.

## Known Limits

- The 30.09 rework adds separate `level2-winter` window plates for House II and Workshop II. This is a seasonal appearance, not the future V05 snow-cover simulation.
- Buys on the inventory are not tied to actual inventories at this stage: a separate bounded binding is not yet available, so a new fake indicator is not added.
- Temple/Barracks and other deferred types are unlocked. The Hand Path is not back.
- Dynamic lighting, rain and temperature remain separate after the next stage.

## Reception after remarks

A prior realization does not mean achieving an artistic orientation. The cumulative transformation V01–V03 is checked by [with a separate list of end results](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md). Unfulfilled or unverified items are not considered PASS.

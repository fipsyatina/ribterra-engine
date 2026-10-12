# Ribterra — step-by-step graphics updates

> English translation of a historical project document. Technical literals and recorded results are preserved; these records describe their original snapshots and do not validate the current build. Original wording is available in import commit `7388a2f`.

**Detailed current plan, 28.09:** [complete technical cards for V01–V05 and DL](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md), [asset and engine audit](ASSET_ENGINE_AUDIT_2026_09_28_UA.md).
Updated 27.09.2026. **Current order: V00 → V01 UI → V02 world → V03 objects → V04 rain → V05 winter. Full Dynamic mode comes later.**

## Main files
| File | Purpose |
|---|---|
| [New gallery](index.html) | Actual previous capture and five sequential mockups, with comparison against the preceding stage |
| [V00–V05 plan](GRAPHICS_PROGRESS_UA.md) | What changes, what remains, where to implement it, and what evidence accepts each stage |
| [Future lighting](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) | Existing API scaffold, deferred switch, and future DL-01–08 |
| [Agent contract](AGENT_VISUAL_CONTRACT_UA.md) | Scale, HUD, truthful data, and native evidence |
| [Earlier 13 targets](public-versions.html) | Mockups for public gameplay versions 0.26.1–0.27.0, not a graphics-progression scale |
| [Gameplay-version cards](NEXT_UPDATES_DETAILED_UA.md) | Future housing, food, logistics, tutorial, cooperative play, and demo acceptance |

V00 is an unchanged preserved 0.26.0 capture. V01–V05 were created with built-in image generation through sequential edits. Geometry preservation is approximate; the game must prove exact pivots, picking, and text correctness. Displayed weather values are illustrative. A generated PNG cannot establish readiness.

## Technical supplements
- [V2 graphics audit and work map](GRAPHICS_ENGINE_PLAN_V2_UA.md): current capabilities, the gap to the concepts, and work packages. The earlier 24 development packages do not define a new mandatory sequence for the 0.26.1 baseline.
- [Future Render Core](RENDER_CORE_SPEC_UA.md) and [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md): proposal for the full mode, not an implementation yet.
- [World FX](WORLD_FX_SPEC_UA.md): water, haze, precipitation, and footprints. Effects require separate native proof; an image does not implement them.
- [Asset production](ART_PRODUCTION_SPEC_UA.md): art masters, export, materials, and budgets; normal and depth maps are required only for the future Dynamic2_5D mode.
- [World/UI](../../.omx/plans/2026-09-26-world-ui-implementation-spec-ua.md), [weather](../../.omx/plans/2026-09-26-weather-implementation-spec-ua.md): simulation/path/build/save/network dependencies.

## Provenance
New scenes: [data](progress/scenes.json), [prompts](progress/prompts.json), [manifest](progress/manifest.json).
Earlier 13 images: [original prompts](generation-prompts.json), [original manifest](manifest.json). Their PNG files and hashes remain unchanged.

## Start implementation
Take one V-stage at a time instead of changing all graphics at once. Check `HEAD` and other contributors' changes. Implement through the actual renderer, RmlUi, and simulation interfaces, then capture the same scene before and after. For weather, establish authority/save/network behavior first; for objects, calibrate a small asset kit first. Do not add roads, castles, new physics, or hidden dynamic lighting.

Dynamic lighting is currently unavailable: the API truthfully keeps `effective=Legacy` or rejects an explicitly requested unsupported startup mode. There is no UI switch. When the DL stage begins, its introduction must not change simulation, save, or protocol behavior.

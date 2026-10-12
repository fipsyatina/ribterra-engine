# Check the detailed plan — 28.09.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Object: `.omx/plans/2026-09-28-graphics-updates-implementation-ua.md` and `ASSET_ENGINE_AUDIT_2026_09_28_UA.md`.

The independent critic originally returned REVISE with three blockers:

1. The draw_world/atmosphere function already creates Fog commands, so adding Atmosphere after them violates the renderer nondecreasing-pass contract.
2. The uncertainty is whether V04 implements snow accumulation or only reserves fields for V05.
3. Lack of an exact canonical formula for the working weather coefficient and golden vectors.

Corrected in V04:

- Clear division terrain/objects, atmosphere and world interaction overlays; the transfer smoke/fire; FoW is described as current reveal filtering, not a fictional final mask pass.
- The full authoritative climate/cover/wetness implementation and testing belong to V04; V05 adds winter visuals, scripts and footprints.
- Added integer snow/wet rates, work caps, remainder lifecycle/reset, affected jobs, tick ordering and control examples.

The verdict was re-examined: **APPROVED**. There are no blockers in the corrections that have been tested. It's a review plan, not a review of the renderer implemented or a new weather system.

A specific document verification: all links to the new plan/copy exist, file:line does not go beyond the file; Six cards and ten illustrations are opened in the Desktop HTML at widths 390/1280/1920, with no horizontal overflow and JavaScript errors. Details in `detailed-plan-check.json`, `detailed-plan-render-check.json`; screenshot `detailed-plan-preview.png`.

The source code of the motion and runtime assets in this step remained unchanged. The diff LightingMode/RenderFrameTests — of the previous work. Native game tests at this stage were not launched, as the result was documentation and inventory.

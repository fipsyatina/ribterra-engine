# Ribterra — Future of dynamic lighting and switch

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Base update before DL:** [ the detailed plan V01–V05 from 28.09](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md).
The user's decision: 27.09.2026. **Now it's just a build in progress. Full lighting and setup are delayed. A release date and version have not yet been determined.**

This document takes precedence over the prior requirement to make full dynamic lighting mandatory in 0.26.1. It doesn't block basic update UI, the world, texture, buildings and weather. Dynamic lighting references — future target; It's not a criterion for a finished product.

## What's done now
- [Renderer2D API](../../include/ribterra/renderer2d.hpp): `LightingMode::Legacy/Dynamic`, `LightingStatus` from requested/effective/availability/reason, request `set_lighting_mode()`.
- By default, the current renderer works as before. `dynamic_available=false`.
- The Dynamic runtime query returns false, leaves effective=Legacy and reports NotImplemented. The unknown enum value deviates without changing state.
- The direct start through RendererOptions from Dynamic deviates to the GPU allocation. There is no silent activation or hidden replacement.
- There's no new shader, graphics resources, UI- switch or setup record. There's no new lighting on the screen.
- [Render contract tests](../../tests/render_frame_tests.cpp) check the rejection, status, unchanged frame and return to Legacy on the software and Direct3D 12.

## Future small steps
Numbers DL — development packages, not previously released versions of the game.

| Stage | Adding/altering | Acceptance |
|---|---|---|
| DL-01 — possibilities GPU | Forms, material/shader contracts, capability/status; Legacy remains a separate path | Native query + offscreen test; Unsupported has a good reason. |
| DL-02 — small lit kit | Neutral albedo, normal/roughness, depth, emission; a house, a tree, a dwelling, land | Loader/atlas/basis validation, the checkpoints do not move on different zooms |
| DL-03 — dynamic light | Sun + local point/spot sources; The earth, the facade, the roof and the inhabitants react to the movement of the source | Uninstalled native video source off/on/move, warm/cold, actor crossing |
| DL-04 — Cover and materials | Moving occluders, per-light shadows, wet response; reveal filtering | Two different sources: blocker switches off only one; The light doesn't reveal the fog. |
| DL-05 — productivity | Light/occluder culling, quality caps, atlas/target memory ledger | Measured p95/p99 CPU/GPU/VRAM on specified PCs; There's no hidden effect switch off. |
| DL-06 — setting | Switch «Dynamic lighting: Off/Off», local storage, cause of unavailability | The values requested/effective are not confused; unsupported does not show active mode |
| DL-07 Life cycle | Restart/apply, safe failure, resize/minimize, load/reconnect; assets from the package | The operating mode is available after the error; The resources are running out. |
| DL-08 — reception | Real scene with 20–30 residents, rain/snow, light and UI | Both modes have gone through the same gameplay/save/co-op scenarios; Only publish after proof. |

## The behavior of the future movers
The layout  setting of the chart RmlUi. The initial value — is turned off. In the first implementation, the change is applied after the game restarts: clearly write it to the settings, save the desired mode locally. Safely switching the GPU graph between frames is not a hidden requirement of the first version.

Now `ui-settings.json` only keeps the existing parameters of the locale/UI scale. The DL-06 needs a versioned local graphics settings contract with migration/defaults/atomic write. Don't add this field to World save, protocol or rules hash. Host and guest may have different lighting modes with the same simulation.

On the unsupported GPU parameter, it is inactive and explains the cause. If the user already has Dynamic stored on another PC, the startup checks the capability to create the renderer, displays the message, and explicitly selects Legacy; It doesn't transmit consciously unsupported mode to ctor. Active state is always read from `LightingStatus::effective`, not from the old options object.

The Legacy name remains the internal name API. In UI — Dynamic lighting: Off. The shutdown does not remove weather, buildings, gameplay or seasonal textures. In both modes, there is no impact on food, HP, routes and resources.

## Architectural appliances
- [Algorithm of dynamic lighting](DYNAMIC_LIGHTING_SPEC_UA.md): normal/depth reconstruction, source, occlusion, limitation and testing.
- [Future Render Core](RENDER_CORE_SPEC_UA.md): GPU graph, formats, resource lifetime, alpha/color space, viewport.
- [Manufacturing of assets](ART_PRODUCTION_SPEC_UA.md): edited masters, material maps, pivots, atlases and budget.
- [Effects of the world](WORLD_FX_SPEC_UA.md): water/smoke/stains not to be confused with physical solver.

These are technical proposals, not protests about architecture. After DL-03–05 they may need clarification. Full 3D physics, GI, ray tracing and the solitude of every detail are not included in this proposition.

## Basic graphical update to this stage
Separate visual steps: current frame → new UI on the old world → land/coast/generation → new buildings and inhabitants → weather. Each layout shows only the addition of a corresponding step. This visual order does not allow the contract numbers to be switched to server/save/network contract and does not replace technical dependencies.

The new artwork is not a set of native screenshots. Compare the same scene and camera angle; accept each implementation with a separate real capture and video. A new UI does not automatically imply new textures, and rain does not imply finished dynamic lighting.

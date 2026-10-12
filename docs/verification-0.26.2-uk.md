# Ribterra verification 0.26.2 V02

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

> The historical account of a particular collection is below, not of visual conformity. After user comments 30.09 the cumulative processing [ is started V01–V03](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md); Its status is prioritized for the current outcome.

The date of the local verification: 28.09.2026. The configuration is Windows x64, `RelWithDebInfo`, Direct3D 12 for GPU smoke. This is a local checkout report; It doesn't prove a published release, two physical PCs or WAN/NAT.

## Confirmed

- The complete clean-up of the Ninja and final linking of all dependent executables were completed successfully.
- Full CTest: **89/89**, 0 falls, 1153,03 s. It includes simulation, save/schema compatibility, networking, dedicated processes, UI, all rendered probes and client smoke.
- `terrain_v02`: 10 fixed seed, a bound river, accessible startup resources/shaft, minimum 80 building cells, surface rib symmetry, stable Road ID, absence of a new Road and its bonus, prototyping and save/load trail, preservation of legacy generator profile. Final single launch: **169 ms** cold generation.
- `network_adverse_1pct` and `network_adverse_3pct` passed in the final suite for 102,79 s and 113,31 s.
- Fixed detected V02- regression: the normal passing start cell no longer bypasses the cliff edge check when searching for the route; The exception is left only to exit the actually blocked cell. `lobby`, `path_planner`, `world_validation` and save roundtrip passed after the correction.
- The old frozen save 0.25.7 migrates in a separate copy; Repeat roundtrip and do not change the state or rewrite the original.
- Staging checked the 155 assets; content hash: `7e38f5a5621f4be1c247e4c2d14b6d513c4b8809bea68415e2662d30779c74a8`.
- GPU contract: `direct3d12`, `NVIDIA GeForce RTX 3050 Laptop GPU`, driver `32.0.15.9282`, `software=false`, engine/window title `0.26.2`.

## The visual evidence

- Native capture: [`../artifacts/graphics-v02-0.26.2/v02-native-1440x900.png`](../artifacts/graphics-v02-0.26.2/v02-native-1440x900.png)
- Renderer metadata: [`../artifacts/graphics-v02-0.26.2/renderer-info.json`](../artifacts/graphics-v02-0.26.2/renderer-info.json)
- Orientation V02, not runtime proof: [`visual-targets/progress/v02-terrain.png`](visual-targets/progress/v02-terrain.png)

The frame should prove one notable river, shoreline without rifts, wide quiet material zones, group of trees/stones and preserved HUD V01. The tracks are tested by a state/roundtrip test; The individual long gameplay capture of their accumulation was not recorded.

## Not Confirmed by This Report

- Two physical Windows PCs, different local networks, WAN/NAT and a real router.
- Published ZIP/release and installation on a pure PC.
- It supported memory costs relative to 0.26.1 and the long-time soak.

# Session time for light, water and fog  08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**The interval is passed:** opt-in `SessionShown` uses the same presentation `MotionClock` as the resident movement. Light, water, and fog get a coordinated phase. It's a local native verification of two neutral categories; **full E06, E01–E17 and reference fidelity remain OPEN / NOT_ACCEPTED**.

## Implementation

`EngineControlSettings::time_source` is default to `ExplicitDiagnostic`. `SessionShown` requires a ready world, water and environment V1. It just replaces environment hour and water/fog time. Cloud, haze, wind and flow are still obvious diagnostic inputs, not the weather magnitude of the host.

The prepared `MotionClock` is copied and checked together with the candidate. The frame, camera, picking and contact queries are posted before commit clock. A rejected candidate does not alter the next phase of time. Normal composition re-uses this prepare/commit and one-time update of motion tracks after reset.

The calendar has a beginning 06:00: ticks 0/7200/14400/21600/28800 correspond to 06/12/18/00/06. Directly before midnight, converting double to float does not give an invalid clock 24. The shown seconds limit ≤1 000 000 is clearly saved; long-term clock rebasing has not yet been implemented.

## Actual execution and source binding

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; The available dirty source is preserved.
- Fresh build `build/gameview-session-time-2026-10-08-attempt02`: MSVC C++20 `/O2 /MT /W4 /WX`, 21 production translation units and a new probe; 158 source bindings, 120 commands with actual zero exit. 23 shader programs / 46 stages; root 528 files / 119 150 652 bytes.
- Probe SHA256 `1B67C317295870723D992461C509F0BC314D1A9774A71CA6290DB9537F27B7A4`.
- Two actual native runs: `build/gameview-session-time-native-2026-10-08-attempt01` and `attempt02`, each exit 0. Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Each run: 17 GPU PNG, raw WaterCoverage RGBA8, JSON and five current-schema world saves. A cumulative eight-pass graph is executed without contact rows.
- Parent verifier actual exit 0: `session time current evidence checks passed; sources=158, bound_files=599, zero_commands=120, repeat_outputs=19`.
- All 19 derived outputs — 17 PNG, raw carrier and JSON — byte-exact between actual runs. Only five accurately named saves are excluded from cross-run comparison because of their own random World identities; Each save separately SHA256-bound to its execution.
- Independent source and actual-evidence review: CLEAR. The reviewer individually checked snapshot/current hashes, both actual subprocess executions, all 19 derived SHA256 and four calendar PNG.

The current cohort check command:

```powershell
C:\Python314\python.exe tests/gameview_session_time_evidence_tests.py build/gameview-session-time-2026-10-08-attempt02 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-session-time-native-2026-10-08-attempt01 build/gameview-session-time-native-2026-10-08-attempt02
```

After the next bound source change, this checkpoint becomes historic. The old cumulative build04 also remains a historic proof of contact; It doesn't appear to be verified by a later source.

## Causal controls

The four calendar landmarks have the exact equivalent of full frame payload and native PNG with the equivalent `ExplicitDiagnostic`. Native changed pixels from the morning frame: noon 375 018, evening 375 018, midnight 374 613. It's a change in the diagnostic scene, not a measurement of the reference quality.

Eight clock cases include actual ClientSession advancement, interpolation/catch-up without extrapolation, starvation, 1×/2×/ 4×, pause/resume, repeated-epoch production load/reset, pointer replacement, wall/authority rollback and actual changed epoch. The driver applies the normal main loop policy pause/speed: `ClientSession::advance()` does not apply it itself. In ten frame deltas per 0.1 second, authority ticks form 20/40/80; shown endpoints 19/38/76. Each speed is individually checked through pause/resume without wall-time backlog. A separate paused native repeat byte-exact.

Repeated-epoch load/reset is invariable: pointer, epoch 2 and authority tick 10 are unchanged, wall time is monotonous; shown tick to load 2.2, after production `reset_world_state()` — 10. The pointer replacement is tested for the same tick 14 / epoch 2; change epoch 1→2 — for the unchanged authority tick 10. A large tick gap or rollback does not replace these reset controls.

All 18 invalid candidates are rejected: enum, missing environment/water, conflict manual atmosphere, version, cloud/haze bounds and NaN, camera, sprite/owner/occluder IDs, instance NaN and wall NaN. The retained frame/camera/pick and subsequent shown clock coincide with the independently loaded GameView oracle. The recovery tick 2.2 is smaller than the authority 4 and differs from the snap that would cause an incorrect early commit.

In all captures exact raw carrier and UI marker RGBA `[31,189,78,255]`; local-light rows and emission/material metadata are saved. The absence of invalid WaterCoverage tuples; The bridge deck 1 865, water 70 657 and land 302 496 pixels. Known UI marker is not a verification of RmlUi HUD.

## Mistakes and Limits

Build01 saved: actual compiler exit 2, probe `constexpr`-mass from `std::string` required persistent heap allocation under C++20. Correction only `constexpr`→`const`; The new build02 is freshly built. A failed build doesn't seem to be a successful launch.

The test uses a zero-population authored terrain fixture, two neutral sprites and three authored completed Bridge records. She doesn't build bridges. Weather authority/save/wire, rain/snow/wind particles, wetness/drying, footprints, normal startup, reference art, RmlUi/DPI, physical input/video and two physical PCs still need work.

FPS and GPU execution time were not measured (`null`). 174 182 400 attachment bytes for two frame sets — nominal volume of targets, not peak VRAM and not confirmation 120 FPS. The goal remains ACTIVE; It's direct execution evidence, without automatic gate-check certification.

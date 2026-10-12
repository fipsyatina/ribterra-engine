# Transparency in typed HDR-way — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**The native gap between the two is passed:** `GameArt::lit_sprite` maintains transparency through the alpha vertex. The coverage, radiation and reflection in the water shall be separately verified in Direct3D12; The independent source and actual-evidence review  **CLEAR**. Full motion, weather FX, normal startup and three references remain **OPEN / NOT_ACCEPTED**.

To API is added trailing `float opacity = 1.f`. All entries are checked before the change of crew; asset search remains mandatory even with zero transparency. Alpha is rounded to the nearest value `round(255 * opacity) / 255`; the maximum error of this function is — 1/510. The zero quantum alpha does not add vertices, indices, materials or commands. By default, payload and PNG exactly equal explicit opacity 1. RGB remains white; The change does not introduce tint, new uniforms, bindings or shader ABI. The Opaque ground/surface requirements are preserved.

This property only governs the velocity and its reflection path. Separate authored lights, shadow blockers and contacts have their own contracts; The total shutdown GameView or weather system is not connected yet.

## Actual execution and attachment of files

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; The previous dirty source is saved.
- Fresh build `build/gameview-opacity-2026-10-08-attempt01`: C++20 `/O2 /MT /W4 /WX`, 21 production translation units and new probe; 160 source bindings, 120 actual zero-exit commands, 23 shader programs / 46 stages. Root: 530 files / 118 618 058 bytes.
- Two actual subprocess runs, exit 0: `build/gameview-opacity-native-2026-10-08-attempt01` and `attempt02`. Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080, eight-pass `WaterAtmosphereSlice`.
- Each run: 19 PNG, 3 qualification masks, raw WaterCoverage RGBA8, JSON, one current-schema world save and two runner evidence files; 27 files, about 20.1 MB.
- Parent verifier actual exit 0: `opacity current evidence checks passed; sources=160, bound_files=601, zero_commands=120, repeat_outputs=24`.
- All 24 derived outputs exactly match the actual runs. The only named exception — `opacity-world-current.save` with the random World ID; Both save separate hash-bound to their executions. Wrapper timings are not issued for deterministic outputs.
- The reviewer independently checked all 160 current/snapshot bindings, runtime origins/destinations, 25 probe outputs/run, 120 zero-exit sidecars and 24 derived comparisons. Additional revisions include coverage half/one, emission half and reflection half/one PNG. Mismatches: 0.

The current source cohort check command:

```powershell
C:\Python314\python.exe tests/gameview_opacity_evidence_tests.py build/gameview-opacity-2026-10-08-attempt01 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-opacity-native-2026-10-08-attempt01 build/gameview-opacity-native-2026-10-08-attempt02
```

After the next bound source changes, this cohort becomes historic. Previous cumulative-contact and SessionShown cohorts are already historical and preserved as separate evidence of their source versions.

## Causal Controls

The three groups have absent, zero API, real submitted zero-alpha geometry, half alpha 128, explicit one alpha 255, and default captures. The Reflection group only paints reflections, without a regular spray. Fog and water distortion off, exposure 1, environment clear and still. House fixture does not add individual local emitters/casters, which could replace the spray result.

| The group | Qualified pixels | Qualified components | Strict intermediate components | Half changed from zero / one |
|---|---:|---:|---:|---:|
| Coverage | 17 344 | 49 954 | 49 954 | 17 395 / 17 402 |
| Emission | 1 067 | 3 196 | 3 196 | 1 067 / 1 067 |
| Reflection | 1 116 | 2 069 | 2 069 | 9 634 / 11 204 |

The numerical oracle uses the inverse-sRGB intervals of the final SDR with the quantum endpoints; This is **inferred linear evidence, without raw HDR readback **. The FP16 is clearly set to relative 3/1024, absolute 1/65536; endpoint byte delta ≥8, saturated channels excluded. In all three groups, the maximum interval gap 0. Set zero or one instead of half deviates; The oracle is not vacuumed. Qualification masks for reflection are additionally limited raw water carrier.

In each group zero API and submitted zero-alpha geometry give PNG, exactly equal to absent; default PNG/frame is exactly equal to explicit one. Material/emission/receiver/reveal metadata is constant except alpha. The three CPU controls include quantized zero 0.001 and alpha 128/129/255. All 10 API and 15 renderer invalid controls are rejected to submit with the frame preserved and without creating new targets. Opaque ground alpha rejection is tested separately.

Known UI marker RGBA `[31,189,78,255]` and raw WaterCoverage remain byte-exact; There are no invalid carrier tuples. This is not a RmlUi HUD check. The carrier has 1 865 bridge-deck, 70 657 water and 302 496 land pixels; Bridge records authored, actual construction was not completed.

## Acceptance limitations

Parent reviewed actual coverage-half and reflection-one PNG: they show a neutral diagnostic scene. Her appearance is not a reception of reference art. Checked one category house; storm/rain/snow/fire/smoke particles, weather authority, save/wire weather, general fade, normal startup, physical input/video/DPI and two physical PCs need further work.

FPS and GPU execution time have `null`. 174 182 400 nominal attachment bytes for two frame sets are not peak VRAM or confirmation E17 ≥120 FPS. Native goal remains ACTIVE; The results are direct execution evidence, without automatic gate-check certification. There was no remote in this test.

# Water / Reflection V1 — bounded native checkpoint 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: **BOUNDED GPU CHECKPOINT PASS / Full release date has not been released**.
All E01–E17 are open. This is an isolated technical scene from the same renderer,
Not a normal launch, a new release, or a ready-made concept art.
The contract is [Water / Reflection V1](WATER_REFLECTION_V1_CONTRACT_UA.md).

The most current bounded follow-up: [raw binary WaterCoverage and odd-size resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md), fresh build02/native05–06. Below is the unchanging in content of the evidence of the preceding build01/native01–02; Their SHA/numbers are not replaced by new ones. Intermediate AA, full engine and FPS open.

## What Has Been Done and Checked

- A separate cumulative `WaterReflectionSlice` in a common SDL_GPU renderer;
  The old graphs are not switched. Saved sun/moon shadow paths,
  Contact V1 ABI, material and local lighting.
- The fifth MRT RGBA8 contains water/class/coast/dwell Reflection.
  `SurfaceWorld.W` remains the exact Contact tag 0/1/2.
- Two world-anchored normal flows, shown time and wind, bounded distortion;
  The coastal pine is lit up as a diffuse, not lit up at night.
- Half-resolution HDR Reflection has source lighting, premultiplied alpha
  and one strength; The final receiver gate excludes dry, unknown and unknown.
- The opt-in `GameView::compose_engine_control` uses actual ClientSession
  Water, complete the bridge and reveal. The new normal startup is not connected.
- Technical art kit: neutral house/resident and periodic normal card.
  It's a minimal control material, not a copy of the final concept assets.

## Fresh collection

`build/gameview-water-2026-10-08-attempt01` — actual exit 0.
22 translation units, 142 frozen source bindings; 108/108 command exit-sidecars
So they have 0. Fresh 20 shader programs / 40 stages, 44 source dependencies,
81 shader artifacts; 52/52 kit files and 50 manifest assets verified.
5 protected startup/launcher anchors are unchanged.

Size: 476 files, 114 888 094 B (109,566 MiB); executable 42 599 424 B.

| The Evidence | SHA-256 |
| --- | --- |
| `GameViewWaterProbe.exe` | `A3E59C9491B30E3E06B83281BE0FDB3DCC3A250B6DC653DE76007F96F2CD6B96` |
| `build-report.json` | `2E70EB153C9E388F6E287FBF4F75584C26B9FE29925FF86C92A34BCB5B22939A` |
| Shader `manifest.json` | `F00F4F4C4F542C086761F717BEA8B07B5FA86CC4A987A52188D3BF170D543B78` |
| Native report (both) | `F77E657752CD6485F2513625AA9F1A2CFB21A695ECEC1B538817B048A27B1CE5` |
| Root evidence ledger | `BBFA3E9B262FDCE4AD7E49C4499A9E6F422BF106629977C5B38DBF4F43FF8FE0` |

An independent audit confirmed the frozen build cohort. Didn't run the general
collection/CTest/install/package, did not change release version or dist pointer.

## Two real native persecutions

`build/gameview-water-native-2026-10-08-attempt01` and `...attempt02`:
the actual exit 0 of each. Viewable SDL window, native Direct3D 12,
NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, not a software renderer.
Each output is 14 GPU PNG 1920×1080, JSON report and current-schema save.
The dimensions are respectively 6 426 405 and 6 426 410 B.

- 51 water cells, 3 completed narrow E–W bridge decks, 2 reflected controls.
  `water_draws=49` — prepared GPU batches after merging, not the number of cells.
- 8 submitted GPU passes: SunShadow → MoonShadow → Reflection → GroundGBuffer
  → GroundLighting → WorldForward → Composite → UI.
  The contact pass is not triggered by an empty input in this scene; It's not full.
  It's a nine-pass scene with all the effects.
- Reflection toggle: 9 885 of the pixels changed; the time shown: 54 318;
  shore toggle: 5 098. Changes restricted by water for diagnostic CPU classifier
  with the physical pixel 1 on the edge. **This is not raw carrier/AA proof.**
- 6 actual GPU camera captures: zoom 0,62 / 1,3 / 2, and two pan for each.
  Zoom 3 and native keyboard/mouse inputs are not verified by this.
- Same clock and after-reject frames exactly match the baseline.
  The hidden receiver from Reflection on/off is also byte-exact.
  Final PNG alpha is verified as 255 in all pixels.
- 6 invalid inputs are actually rejected: non-unit flow, excess distortion,
  fractional ground/caster reveal, unsupported authored AA coverage,
  invalid reflection material index.
- Rebooting the already existing output actually returned the exit 1
  `Native output already exists`; failure JSON is not created.

14/14 PNG and the original JSON byte-exact between the probes.
Save files are different through fresh secure random `world_id`; We're not saying
the determinism of the entire 16 file tree.

## The Limits of Evidence and Memory

[`water-native-root-evidence-oct8.json`](../../.omx/plans/water-native-root-evidence-oct8.json)
It connects build report, executable, shader/kit manifest, probe source and all.
native outputs by path/bytes/SHA. Independently verified 37/37 bindings,
No extra files in two native roots. Exits recorded by the main agent for
the actual results of the launch; These are **non-binary-generated exit sidecars,
not signed independent execution transcript**. The original reports are not reproduced.

For 1080p ground graph: 174 182 400 B; sky maps: 16 777 216 B;
tracked steady nominal texture payload: 190 959 616 B (~182,11 MiB).
This is **not total VRAM**: no swapchain, buffers, art, driver overhead,
the same old+candidate at the resizing. Odd-size allocation/rollback is not proven yet.

Raw baseline and zoom-2 capture visually reviewed and compared to summer
target-0262: the gray technical scene clearly does not match the finished art scene.
GPU readback is not a desktop scanout, video or manual input verification.
Native computer automation is not available on this tool.

## What else can you open?

Raw coverage/classes/AA-exit; odd resize and allocation failure rollback;
native pan/zoom/pause/video; the whole weather and atmosphere stack together;
Concept art, normal startup, graphical presets; GPU timing, displayed FPS,
minimum 120 FPS in 1080p and smoothness; It's a full suite.
Only after E01–E17 — the next game, the generator and the final HUD.

Storage: no removal of this checkpoint. New roots are not replacement
launcher; Furthermore, the latest + verified rollback + compact evidence for
[the conservation policy](../ARTIFACT_RETENTION_UA.md). Before anyone else
There are specific ways and reasons to delete old files.

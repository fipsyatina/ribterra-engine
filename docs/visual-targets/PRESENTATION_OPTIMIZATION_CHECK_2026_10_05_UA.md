# The process of extracting and optimizing the images — verified partial step

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date of the stage: 05.10.2026. Status: **LOCAL FOUNDATION CHECKED / ENGINE NOT ACCEPTED **.

Real presentation settings are implemented, reuse of GPU upload buffers, cache of the immutable mini map, and shortening of repeated GPU state binds. The local inspections are complete. **It's not Dynamic readiness, it's not concept compliance, and it's a lack of acceptance of the 120 FPS minimum in the full scene. All E01–E17 remain open.** Further work on the game is expected [ engine threshold](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

## Changes made

- **Real withdrawal control:** VSync / Immediate / Mailbox, uncapped or FPS cap 0–1000, 1–3 frames in flight. The Renderer checks capability and range, displays requested/effective/reason, applies full configuration after GPU idle. Unsupported does not appear to be running. The software allows its own FPS limiter, but does not simulate GPU- control.
- **Local settings:** typed UI action → explicit application of main cycle → acknowledgment. The UI designer does not change the renderer itself. The accepted configuration is maintained; custom cap 77 and unknown JSON- keys are not lost. An incorrect presentation block does not drop the correct locale/scale. The explicit CLI presentation argument turns off the automatic recovery of the entire presentation configuration, rather than arbitrarily mixing the fields.
- **Apply / Reset:** Apply transmits the full request; Reset rejects unused permissions to active values, there is no display revert. A failed recording of settings does not cancel the already applied mode and does not leave UI pending: it shows localized «applied but not saved», a repeat attempt is available. A successful verified record clears the warning. Atomic/fsync durability is not claimed.
- **Pacing:** absolute time deadlines, fractional carry, late checks wakeup/clock rollback/overflow; The mandatory delay 1 MS in the normal uncapped cycle is removed. Minimize and remote polling have separate rules. The FPS cap is not a guarantee of the lower limit FPS or the absence of OS scheduling jitter.
- **GPU lifetime:** upload transfer buffers belong to the frame slot and are only reused after its fence. The size of the vertex/index/surface/upload buffers and offscreen target transaction set. Fault tests check the retention of the previous frame and the successful next attempt. When minimizing normal GPU- work is omitted; Clear offscreen capture remains available.
- **Less repeating work:** only adjacent compatible geometry draws are combined, without sorting or changing painter order. Pipeline/sampler/scissor binds are cached within one render pass. The mini-map is re-designed as required by the change terrain/chunk/reveal/trail/season/session/viewport; The live markers and camera border are drawn separately. Compose scratch vectors and part of the containers reused; The total absence of allocations is not declared.
- **True telemetry:** CPU update/compose/prepare/encoding/submit/wait/pacing are separated. CPU submit is not called GPU execution. The built-in `gpu_execution_ms` and `displayed_fps` remain `null`; The external ETW- measurements are stored separately.

Profiles Low/Medium/High/Custom, screen mode/resolution/render scale and the quality of future light/shadows/waters/fumes/particles ** have not yet been fully realized**. The three presentation controls do not close E16.

## Actual measurement conditions

- NVIDIA GeForce RTX 3050 Laptop GPU; SDL driver `32.0.15.9282` (NVIDIA 592.82), D3D12; Ryzen 5 5600H, 6 cores / 12 threads.
- It will display ** window**, client drawable **1920×1080**, monitor **144 Hz**, pixel density / display scale / render scale **1.0**. It's not an exclusive fullscreen or a hidden-window throughput.
- Networked livelihoods, balanced power plan; The food plan was unchanged. 4096 MiB — capacity VRAM, **unmeasured peak usage** during play.
- True GameView + RmlUi, common legacy world seed **70421**, frozen state, **0 simulation ticks** during sampling. `Dynamic_available=false`. Not a full control scene, no pan/zoom/ new chunks/all future effects.
- Each case: **750 ms warmup**, then sampling. PNG readback, file recording and minimize/restore checks are performed outside the performance interval. CPU quantiles — nearest-rank; recorded p50/p95/p99/max and the number of intervals over **1000/120 ms**.

## Profiling-free log: CPU-loop, 8 seconds per case

Source: [full native report](../../artifacts/presentation-2026-10-05/native-unprofiled-finalized/presentation-report.json). The same final executable, without the parallel PresentMon/build/test. The probe was completed successfully, including with minimize → offscreen capture → restore.

| Mode | CPU-loop FPS | p50, ms | p95, ms | p99, ms | max, ms | Over budget / intervals |
|---|---:|---:|---:|---:|---:|---:|
| VSync, HUD | 138.62 | 6.973 | 8.947 | 10.271 | 12.379 | 97 / 1107 |
| Immediate, uncapped, HUD | **145.16** | **6.665** | **8.245** | **9.999** | **12.694** | **50 / 1161** |
| Immediate, HUD hidden | 159.07 | 6.092 | 7.489 | 9.447 | 10.930 | 31 / 1271 |
| Immediate, cap 120, HUD | 119.52 | 8.333 | 8.862 | 11.113 | 12.205 | 474 / 955 |
| Mailbox, cap 120, HUD | 117.88 | 8.336 | 10.158 | 11.896 | 13.042 | 491 / 942 |

**145.16 — Average speed CPU- cycle of static legacy-scene, unconfirmed displayed FPS full engine.** p99/max exceed budget. HUD-hidden row — is just a diagnosis, not a replacement for the baseline. White cap 120 counter over budget precision sensitive and to very little jitter; Not every such interval is a big hitch.

In Immediate/HUD the main CPU mean: world composition **4.704 ms**, UI update **0.938 ms**, UI compose **0.055 ms**, renderer total **1.184 ms**, in particular encoding **0.379 ms**. The total renderer includes its subphases — they do not need to be added a second time. The largest CPU- component behind this short run is — scene composition. 3717 draws, 57960 vertices, 87468 indexes, 1509072 upload bytes on average; The geometry is not cut.

The earlier 5-second baseline/8-second interval runs are saved separately. Because of the different lengths, heating and power condition, no controlled accurate percentage of growth is claimed from each optimization.

## Unique allowed PresentMon: single 10-second ETW-short

One **** approved portable launch PresentMon 2.6.0 from UAC was used, with no driver/service installation or permanent change permissions/groups. Monitor PID 14124, target probe PID 3376; Both ended with exit 0. Repeat elevated launch is not performed; This license is not permanent.

The evidence: [raw CSV](../../artifacts/presentation-2026-10-05/presentmon-finalized/presentmon.csv), [QPC-matched analysis](../../artifacts/presentation-2026-10-05/presentmon-finalized/analysis.json), [profiled probe](../../artifacts/presentation-2026-10-05/native-profiled-finalized/presentation-report.json), [tool provenance](../../artifacts/presentation-2026-10-05/presentmon-tool/provenance.json). The analyzer is: [summarize_presentmon.ps1](../../tools/summarize_presentmon.ps1).

Select ETW for `CPUStartQPC` in `[sample_begin_qpc, sample_end_qpc)`; frequency 10000000. One target process; Each case has a swapchain. Warmup/capture/contracts does not include the selected CPU intervals; within CPU/present intervals, a possible error is possible. VSync has 1219 selected CSV rows against 1220 queued presentations, rather than hidden equation counts.

| The Regime | ETW rows | GPU elapsed mean / p95, ms | GPU busy mean, ms | Frequency of valid display intervals | Display p95, ms | Missed interval samples | Tearing allowed rows |
|---|---:|---:|---:|---:|---:|---:|---:|
| VSync, HUD | 1219 | 4.032 / 4.934 | 3.868 | 122.07 Hz | 13.907 | 0 | 0 |
| Immediate, uncapped, HUD | 1190 | **3.621 / 4.130** | **3.556** | **118.72 Hz** | **15.000** | **3** | **1190** |
| Immediate, HUD hidden | 1371 | 3.464 / 3.907 | 3.375 | 135.64 Hz | 13.985 | 14 | 1371 |
| Immediate, cap 120, HUD | 1162 | 3.743 / 4.107 | 3.681 | 115.90 Hz | 15.212 | 4 | 1162 |
| Mailbox, cap 120, HUD | 1175 | 3.507 / 3.984 | 3.421 | 117.50 Hz | 13.916 | 0 | 0 |

The display column — **1000 / mean valid `MsBetweenDisplayChange`** method, does not guarantee the frequency of full scanout frames, especially in a mode that allows tearing. `AllowsTearing=1` is not evidence of visible tearing or its amount. `NA` is passed, not replaced by zero. The missing display interval ** is not called the dropped frame**; without adequate evidence no ETW losses are declared. In all selected cases `MsUntilDisplayed` is added for each row, but this does not remove the specified constraints interval/scanout.

GPU elapsed (`MsGPUTime`) includes GPU busy + wait; GPU busy is shown separately. This is an external ETW- dimension, not renamed CPU submit. In this loop Immediate/HUD CPU-loop FPS ** 119.00 **, p95 ** 11.656 ms**; It doesn't mix with 145.16 in a no-trace run. The difference is consistent with the impact of profiling/flow conditions, but the exact contribution of overhead is not established. The full GPU/display p50/p95/p99/max/coverage is in the analysis JSON.

The primary sources of the methodology are: [PresentMon 2.6.0 console documentation](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md), [CSV output implementation](https://raw.githubusercontent.com/GameTechDev/PresentMon/v2.6.0/PresentMon/CsvOutput.cpp). The own numerical results — are from local CSV, not from documentation.

## Current local checks

| The verification | The actual result / limit of the evidence |
|---|---|
| Release build | The current Ribterra/probes/tests is collected; MSVC 14.51, Release `/O2 /Ob2 /DNDEBUG`. Not release publication. |
| Targeted CTest | The following is a list of the most commonly used names for the name of the product: **10/10**, 53.37 c: ui, ui_lifecycle, presentation_ui, render_frame, frame_timing, render_batch, minimap_cache, presentation_contract, client_input, surface_lighting_contract. **Not all 98 tests**. [LastTest.log](../../artifacts/presentation-2026-10-05/check-finalized/runtime/Testing/Temporary/LastTest.log). |
| Native GPU resource/text recovery | `RenderFrameTests --gpu`, exit 0; old small red frame decoded after injected buffer-growth failure to retry; glyph/upload recovery verified. |
| Native draw barriers | `RenderBatchTests --gpu`, exit 0; lit/legacy/order barriers are preserved. [captures](../../artifacts/presentation-2026-10-05/native-batch-finalized). |
| Surface kernel regression | **18 captures**, exit 0, [report](../../artifacts/presentation-2026-10-05/native-surface-finalized/surface-lighting-report.json). Only bounded opaque surface kernel; not full Dynamic/HDR. |
| Native settings UI | **3/3 applied-and-captured cases**, 4 PNG; Ukrainian 720p/scale 130%, 1080p/scal 100%, actual D3D12. 720p after two SDL wheel events and 14 animation-settling frames has available Apply/Reset/Back. [report](../../artifacts/presentation-2026-10-05/native-settings-finalized/presentation-settings-report.json). |
| Legacy visual regression | **6/6 configurations, 15 PNG**, all 15 SHA256 are matched to the previous outcrop-HUD matrix; F1 hide/restore, winter, 720p/1080p/1440p, 5 locales. [report](../../artifacts/presentation-2026-10-05/legacy-finalized/report.json). It is the preservation of the existing appearance, ** not the similarity of concepts**. |
| Benchmark capture parity | **5/5 PNG** is the same in profiled and unprofiled runs; The sampling didn't change fixture/art. |
| Actual CLI process | Software cap 120 / frames 2 and D3D12 Immediate uncapped / frames1: exit 0; [renderer info](../../artifacts/presentation-2026-10-05/cli-finalized/gpu.json). Three incorrect cap `120oops`, frames `0`, mode `made-up` queries are deviated from exit 1. ExitCode is obtained through the awaited process, not the stale `$LASTEXITCODE` GUI-launch. |
| Source review | The code-reviewer is the final UI/persistence review **APPROVE**; It's not visual conceptual acceptance. `git diff --check` passed, the existing dirty tree is preserved. |
| ETW analysis review | Independently listed source hashes, selected rows / NA counts, means, nearest-rank quantiles, and display interval rates — **APPROVE**. The review of claims separated the permitted tearing from the actually observed; The wording is corrected. |

Examples of actually reviewed native images are: [720p settings bottom](../../artifacts/presentation-2026-10-05/native-settings-finalized/uk-1280x720-scale130-immediate-cap120-frames1-settings-bottom.png), [1080p settings](../../artifacts/presentation-2026-10-05/native-settings-finalized/uk-1920x1080-scale100-vsync-uncapped-frames2-settings-top.png), [, common legacy world](../../artifacts/presentation-2026-10-05/native-unprofiled-finalized/immediate-hud.png). There's no claim that this is a concept-faithful new scene. Native computer-control API is not available in this session; The PNG from the real SDL/D3D12 renderer and SDL input probes are not output by manual desktop automation.

Recently intermittent fixture/scroll/tool-wrapper runs are preserved: SDL driver-hint conflict, minimap fixture revisions, wheel loop without time RmlUi animation and stale GUI `$LASTEXITCODE` corrected and re-checked. They're not hidden by the removal of artifacts and they're not used as green proof.

## Link to verified files

Final [snapshot](../../artifacts/presentation-2026-10-05/check-finalized/snapshot.json): **294 hash-verified runtime files**. This is an output-isolated runtime, **no hermetic source snapshot**: Python tests/repository inputs remain live.

| File / set | SHA256 / content hash |
|---|---|
| Ribterra.exe | `CE44B626EA1213861443648F9F2E9245EEA35635CB1621BBEC1AB77B69FD2D9B` |
| PresentationProbe.exe | `4E6234E8365AAF9708FC5A65B00796FD3DCF95D70EF4E6BECBF1A90EA343A835` |
| compiled-shaders/manifest.json | `3A4F827AC1789F3254B8002306C74D36882C71C572DD60A19EE1909A4220F81A` |
| Asset content | `fde76d5bb99180a47b59c046e60a7cff023a48a8ee689da2f3659a59d7976abd` |
| PresentMon executable | `B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF` |
| Raw PresentMon CSV | `7A9E2219398F3AD95F2338E343A9E9FD76CD93916D70149E8FD7F364DEF8BE14` |

Launcher ** is not updated**: `Play Ribterra Rework.cmd` still points to `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2/Ribterra/Ribterra.exe`. This stage is — source/local validation, not a new version installed, not Git commit/push/release. The files were not deleted; Prior asset-staging and failed runs are saved.

Ponytail/ultrawork helped separate implementation and independent review; karpathy-guidelines/cpp-coding-standards helped keep scope local, state typed, lifetime transactional, and failure contracts explicit. They do not allow incomplete effects or average FPS to be presented as a finished engine.

## What to Do Next and When to Take

The following work remains **over the engine**: production HDR/render graph, surface/material adapters for all receiver categories, emission, causal shadows, sun/moon/time/weather, water and space atmosphere. After connecting — a shared concept matrix, a long pan/zoom/cold-load run, GPU/display pacing and peak VRAM, a full suite and independent visual review.

**The goal ≥120 FPS in 1920×1080 on RTX 3050 with all the required effects has not yet been proven.** Neither the 145.16 CPU-loop FPS in the static legacy world nor the individual 3.62 ms GPU measurement closes it.

# Linear HDR world — verified partial engine step

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The test results are: 05–06.10.2026, Europe/Warsaw. Status: **SOURCE / LOCAL NATIVE PROOF, NOT accepted as a full member of the **.

An optional color path is added in the same `Renderer2D` that the game uses: the world is accumulated in RGBA16F, the separate Composite uses exposure and one sRGB encode, followed by UI. The collection, 10/10 target CTest, fresh D3D12 HDR-prong and native regression checks were passed. **All E01–E17 remain open. A goal ≥120 FPS with a full scene is unproven.**

## Implemented behavior

The color sequence of this step:

`World RGBA16F → Composite / RGBA8 SDR → unchanged UI → SDR UNORM swapchain`

- `Frame2D::linear_hdr_world` — an explicit opt-in; Legacy is unchanged by default. `world_exposure` must be finite at `(0,64]`; non-unit exposure without HDR deviates. HDR requires a clear clear; `clear_commands()` drops to opt-in/exposure.
- The opaque surface-lighting kernel can record linear brightness above 1 in RGBA16F, without an early SDR clamp/encode. The opaque order of lit/unlit commands is saved.
- The worlds Opaque/Alpha/Additive/PremultipliedAlpha are passing through a linear target. Encoded premultiplied input originally unpremultiply → decode → repremultiply; alpha=0 has a separate safe branch.
- This is **BakedCompat color bridge**: the existing encoded texture×vertex-color product decodes as a compatible result. It is not a new neutral Dynamic material and does not create missing normal/depth/emission channels.
- Composite performs the exposure + explicit sRGB encode + SDR clamp. The final artistic tone curve is not yet adopted. UI uses previous byte-authored shader/blend/text paths and does not include world exposure.
- The format is checked for aggregate `COLOR_TARGET|SAMPLER` usage. Actual swapchain format is re-read after changing presentation parameters. Native result is here — `B8G8R8A8_UNORM`, SDL enum 12; final target — `R8G8B8A8_UNORM`. Blit happens after encode, not replacing the HDR conversion.
- World/final targets — RAII, a separate pair for each active frame in flight (1–3). Resize creates a full candidate; It's only after success that GPU idle replaces the old resources. The allocation error does not leave a half-created active set.
- Shader interface/register/constant-buffer contracts and source/include hashes for new programs were added. Negative tests have a clean baseline and a separate pristine source/output tree for each mutation; All the generated fixtures are stored.

`LightingMode::Dynamic` is still unavailable, and `dynamic_mode_available=false` is being tested by a native probe. Full Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite/FoW → UI graph **not yet implemented**.

## The current review

Final [runtime snapshot](../../artifacts/hdr-color-2026-10-06/check-finalized-v3/snapshot.json): **319 hash-verified files**. This is an output-isolated runtime, not a hermetic source snapshot: Python tests and repository inputs remain live; Transitive source inputs are not frozen.

10/10 CTest, 7.37 c: `shader_contract`, `shader_negative`, `hdr_color_contract`, `surface_lighting_contract`, `render_frame`, `render_batch`, `frame_timing`, `minimap_cache`, `presentation_contract`, `presentation_ui`. [Full log of this selected drive](../../artifacts/hdr-color-2026-10-06/check-finalized-v3/runtime/Testing/Temporary/LastTest.log). This ** is not a complete suite**. Registered `hdr_color_contract` checks the software capability/rejection, not native HDR; native proof is given separately below.

The most recent [HdrColorProbe report](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/hdr-color-report.json): **actual process exit 0**, 34 final RGBA8 PNG. D3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, build label `0.26.3`. `complete=true` in JSON means completion of **'s probe**, not receipt of the engine or game version.

| Controlled entry | The actual readings RGB |
|---|---|
| sRGB gray128, exposure 1 | 128 / 128 / 128 |
| Colored clear 128 / 64 / 200, exposure 1 | 128 / 64 / 200 |
| Straight white, alpha128, above black | 188 / 188 / 188, not encoded-space 128 |
| Encoded premult white RGB128 / alpha128 | 188 / 188 / 188 |
| Premult input from alpha0 | 0 / 0 / 0 |
| Gray128 + additive white alpha64 | 182 / 182 / 182 |
| White albedo × ambient4, exposure0.125 | 188 / 188 / 188 — over-range light stored to Composite |
| The world of gray200: exposure1 → 0.25 | 200 → 106 |
| UI RGB31 / 157 / 219 with both exposure | 31 / 157 / 219 without change |

The samples have alpha255. The control UI quad and the empty glyph region are pixel-shaped when the exposure changes. The exceptional results were sun/local surface-kernel response, lit → unlit → lit painter order, empty/world-only/UI-only frame, invalid exposure/clear/pass order and explicit rejection of the software backend.

Raw examples: [HDR ambient4](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/hdr-ambient4-exposure0125.png), [exposure1/UI](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/exposure1-ui.png), [exposure0.25/UI](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/exposure025-ui.png), [additive](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/additive-white-alpha64-gray128.png).

## Lifetime, presentation and restoration

- 40 sequential HDR frames **without capture/readback** submitted/queued; The sequence is monotonous. It is an exercise frame ownership, **not a benchmark or proof scan-out/FPS**.
- Active frame sets 1/2/3 verified status and decoded known-color captures; Then it's restored to 2.
- VSync, Immediate and Mailbox are supported in this native run: 3/3 applied, requested=effective, queued non-capture frame and known-color capture passed. After that, it was restored VSync/2. Unsupported cases in code are referred to as skipped, not successful; JSON does not declare FPS-limit proof.
- Resize 640×480 → 800×600; Minimize without capture not submitted/queued; Clear minimized capture submitted offscreen without presentation; The restore is over.
- Fault injections 5/6/8 — first world, first final and second final texture of new pair sets. The test accepts only the exact injected error/context, not the random runtime error. The old status/footprint is preserved; **both ** old slots returned to 640×480, captured, decoded and verified quad RGB180/120/80 and opaque black clear. The resizer-retry 800×600 is also pixel-checked.
- `attachment_bytes` — the logical dimension is only world RGBA16F + final RGBA8: `width × height × active_sets × 12`. For two sets: 7,372,800 bytes at 640×480 and 11,520,000 at 800×600. This is **not a driver allocation, not a general/top VRAM** and not a budget of the future eight-target graph.

Additional native `RenderFrameTests --gpu`, `RenderBatchTests --gpu …`, `SurfaceLightingProbe …` have completed actual exit0: text upload recovery, buffer growth rollback, batch barriers, previous bounded surface core (18 PNG). Their executable/shader hashes from `check-finalized` are re-created with the final `check-finalized-v3`: identical. [Surface report](../../artifacts/hdr-color-2026-10-06/surface-finalized/surface-lighting-report.json), [batch capture](../../artifacts/hdr-color-2026-10-06/batch-finalized/surface-batch-barriers.png).

## Real GameView and comparison with concepts

`ReworkVisualProbe --linear-hdr-world` is only a diagnostic opt-in after the usual `GameView`/RmlUi compose; the normal game path and launcher were not switched to an incomplete Dynamic path. [HDR view manifest](../../artifacts/hdr-color-2026-10-06/hdr-view-02/manifest.json): 6 configurations, 15 PNG, ordinary/staged/winter, native D3D12. It is not a painted background substituted for the game.

F1 is presented as SDL key event in fixture: whole HUD/annotations is hidden, then returned; restored HDR PNG byte matches the frame to F1. [Without HUD](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-hud-hidden.png), [ is reversed HUD](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-hud-restored.png). It's native rendering/UI event proof, not manual key pressing through OS automation.

A separate Legacy matrix also went through 6/6 configurations: **15/15 PNG byte identical to ** the previous `artifacts/presentation-2026-10-05/legacy-finalized`. It's proof of the absence of visual regression of the old path, not accepting the old schedule as targeted.

The raw [ annual HDR frame ](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-staged-selected-stockpile.png), [ winter HDR frame ](../../artifacts/hdr-color-2026-10-06/hdr-view-02/uk-85-1280x720-staged-winter-selected-stockpile.png), F1-hidden and control color/rollback captures have been reviewed; The winter frame is set to the [ concept 0.26.1](images/target-0261.png).

**Visual acceptance has not been passed.** Significant differences remain: too light/flat winter lighting scene, different density and scale of composition, baked windows without proven impact on the adjacent surface, no causal shadows of inhabitants/trees/winds, peripheral reveal-vault is not a spatial morning/water fog. HUD/forecast also not yet a complete concept; They're still waiting for a full engine to be built.

These PNG — direct native GPU readback of the final target; **no photo/imaging of the scan-out monitor**, no temporal video. Native desktop API computer control in the current tool is not available; The launch and verification of native SDL probes is done, and the images are reviewed without issuing them through OS automation. The new administrative PresentMon/UAC launch at this step was not executed.

## The attachment to the files

| Final input | SHA256 / content hash |
|---|---|
| Ribterra.exe | `66F469F1BDC716D50BFA59DB074BEC44B8A4882FD9F76BA9CC7D35E6B23E8120` |
| HdrColorProbe.exe | `0DD1E5DEFD926E3FF4BBEF1D98DD9FC7936C9A93730FFC72A1C77ABD092ADAA8` |
| ReworkVisualProbe.exe | `213AA0BA165D9D5A88DAC60E252054FE83809C1DB623B9960313A8D387FA030E` |
| compiled-shaders/manifest.json | `A122A404FB3E3C757837794DB557C6EFD3AB92E0EC20F80553A73DFE3DB05D81` |
| Asset content | `f08ed26b1989791ec7d24109ae9ec865b01c7f65a947ca619e97b3df18f4329b` |

GameView matrix binaries/shaders/assets in the `check-02` hash-identical corresponding final inputs; There were no more recent changes to the production renderer after its matrix. A normal launcher still points to `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2/Ribterra/Ribterra.exe`. This is source/local validation, **not a new version, commit/push, or release**.

Previous staging directories, failed `check-01` copy and intermediate probes/snapshots are saved; The files were not deleted. Snapshot-copy failure during linking is not used as green proof; The final snapshot is created after the collection is complete. Build warnings in previously modified `view.cpp`/`game_ui.cpp` are not declared corrected by this step.

Independent code review checked the pass/lifetime/gamma/alpha/rollback scope. The detected probe gaps  pixel proof both retained slots, build provenance and diagnostic reading sequence after `set_presentation`  corrected to the final native probes. Ponytail/ultrawork divided by implementation/evidence/review; karpathy-guidelines/cpp-coding-standards affected local scope, typed state, and transactional RAII. It doesn't replace full intake.

## The next step is a must

The 2 material scheme and minimum control art kit: neutral albedo, normal/material, camera-depth, emission, receiver/reveal masks; aligned world anchor/projection/reconstruction for terrain and single house/habitant. The missing channel is deviated transactionally; The schema-1 remains the obvious BakedCompat. Then  the full graph, emission/transparency/shadow, sun/moon/weather, water and spatial atmosphere, a common temporal concept matrix, and full scene profiling. Until the engine is adopted, the generator, mechanics and mass catalog/HUD are not continued.

Technical resistance: SDL [combined format/usage query](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat), [SDR swapchain composition](https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition), [actual swapchain format after parameter changes](https://wiki.libsdl.org/SDL3/SDL_GetGPUSwapchainTextureFormat). Internal requirements: [Render Core](RENDER_CORE_SPEC_UA.md) and [full range E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

# First pixel light core — local verification 05.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: **restricted surface-lighting core tested; engine is NOT complete**. The general [barrier E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md) remains open, including with added graphics settings/flow/optimization. User goal — **minimum 120 FPS in 1920×1080 to RTX 3050**; This is not a measurement or confirmation of the truth.

## Fulfilled Right Now

- A separate 24-byte vertex stream from the camera-relative XYZ and normal, without changing the old 20-byte Vertex for sprites/UI.
- The Renderer2D/D3D12 has an opt-in opaque world draw with a pixel diffuse: ambient, directed sun and up to four point/spot sources. Distance/radius, direction, cone, falloff and reveal are involved in the calculation.
- Fixed CPU/HLSL ABI: LightGpu 80 bytes, SurfaceLightingFrame 384 bytes; Completed bindings, offsets, interfaces and hashes are checked.
- sRGB albedo is decoded for linear light computation; The result is clearly encoded back into the current opaque RGBA8 UNORM. **This is not yet a complete HDR render graph.**
- Verify the finite/bounded positions/radians, the single normals, cone/radius/count, the index-aligned stream, the opaque world passages. Unsupported occluder ranges/masks is deflected rather than ignored. The software clearly rejects surface-lit draw.
- A blank draw does not bind to a missing surface buffer. Frame clear is dropping new data. The surface buffer grows transactionally; Expecting a texture-upload fence no longer ignores the error. Double full frame validation is removed.
- Package required-file guard now includes both shader pairs and all four reflection files. It's a statically checked guard; packaging/installation/release not started.

`surface_lighting_available=true` means just this core. `LightingMode::Dynamic` all still fairly returns **NotImplemented**, `dynamic_available=false`.

## Current evidence

Machine: Windows, **NVIDIA GeForce RTX 3050 Laptop GPU**, D3D12, driver **32.0.15.9282**, 12 logical CPU cores, 15713 MiB system RAM. Calibration — 640×480; It's not a 1080p performance scene.

| The verification | The result | The Limit of Evidence |
|---|---|---|
| Targeted build | Ribterra, ReworkVisualProbe, SurfaceLightingProbe, RenderFrameTests — exit 0 | The previous warnings in Presentation/GameUI did not correct in this step |
| [Fresh isolated CTest](../../artifacts/surface-lighting-2026-10-05/check-final/focused.xml) | **4/4**: shader_contract, shader_negative, render_frame, surface_lighting_contract | Not a full suite |
| Shader negative | **8** register/space/output/ABI mutations are rejected | Not injection of all GPU allocation/fence failures |
| Native surface proof | exit 0; **18 PNG**, [raw report](../../artifacts/surface-lighting-2026-10-05/native-final/surface-lighting-report.json) | Production Renderer2D kernel, but diagnostic geometry, not GameView integration |
| RenderFrameTests `--gpu` | exit 0, D3D12 RTX 3050 Laptop | Legacy geometry/text/alpha/clip/mipmap/resize/input/texture lifetime and available text-upload rollback |
| [Legacy GameView regression](../../artifacts/surface-lighting-2026-10-05/legacy-game-check/report.json) | **6/6 configurations, 15 PNG, F1 hide/restore** | New light is not yet connected to the playground |
| The legacy comparison PNG | **15/15 SHA256 identical** `artifacts/outcrop-hud-2026-10-05/matrix-outcrops` | Evidence of the immutability of these fixed fixtures, not of the artistic reception |
| Package guard | PowerShell AST parse + all **9** shader/manifest requirements are present in the runtime snapshot | The package script itself didn't work, no new release was created. |

Runtime snapshots have 288 verified files. [Final snapshot manifest](../../artifacts/surface-lighting-2026-10-05/check-final/snapshot.json) fixes the hashes; Python/repository inputs remained live, there is no hermetic snapshot.

## What you see in the frames and what you see in numbers

- [Local pixel light](../../artifacts/surface-lighting-2026-10-05/native-final/per-fragment-local-light.png): all four peaks outside the radius of the source, the center bright **255/255/255**, edge **25/25/25**. A significant draw tint or just the lighting of the tops does not yield this result.
- [Gerelo left](../../artifacts/surface-lighting-2026-10-05/native-final/same-light-source-left.png) / [ right](../../artifacts/surface-lighting-2026-10-05/native-final/same-light-source-right.png): the same point light moved between opposite norms; The bright **255/246/201** and ambient **39/39/39** are alternating places.
- [Anti-normal under the sun](../../artifacts/surface-lighting-2026-10-05/native-final/sun-opposing-normals.png): **255/236/194** against **56/56/56**.
- Gamma probe: albedo **128** with a linear ambient **0.25** gives **66**, rather than an erroneously encoded multiplication **32**.
- Point radius: inside **209**, outside **39**; spot inside **247/211/124**, outside **39/39/39**.
- The equivalent camera pan/zoom: and the sample itself **163/219/255**. Moving the receiver from the source changes the lighting. Lit → legacy → lit gives the same verified sample.
- UI on/off: the exact unchanged **31/157/219/255**. It's a pair of pixel assertions; The visual examination of a single image does not prove it.
- [Rectified geometrical calibration](../../artifacts/surface-lighting-2026-10-05/native-final/geometry-calibration-not-game-art.png): the ground, two visible walls, and sloping roof converge in the world coordinates. An independent review of the six native frames found no anomaly within kernel proof. **It's purposely simple control areas, not game art and not approaching concept.**

The first `native-first` series with failing-oriented roof planes is preserved; For geometric visual verification only `native-final` is valid. The first set also stopped at parser names in DXIL reflection; Parser is corrected under actual shadercross prefixes with the preservation of hard ABI offsets and repeated negative proof.

## What's not here

There is no full Dynamic, light-responsive materials for play buildings/trees/houses, emission masks, real light for windows in the world, shadows/occlusion, moon/time/weather lighting, HDR/MRT graph, wet materials, water reflections, morning/water fog or new particle pipeline. The full shader/material asset kit has not yet been implemented. There is no receipt of the V01–V03 chart, new full-suite result, temporal video or FPS/VRAM proof.

The fields `elapsed_ms` in the probe contain capture/readback and startup; **do not divide the number PNG at this time to announce the game FPS**.

The permission to run the laptop is but the current computer-use tool has been completed again `failed to write kernel assets ... os error 3`. Desktop control failed. The real visual proof in this step is  GPU captures from the running native executable and their viewing, not a screenshot of someone else's window or a generated layout.

## Identification of the verified result

| The artifact | SHA256 |
|---|---|
| Ribterra.exe | `C36C5315D96FDEA3644E5C774C6F7A8E35D97FA4B69BA6008183706683938B7B` |
| SurfaceLightingProbe.exe | `C0DD21467C07C86A0A3B56CCF416D1C549A0CB75F8FCD8A67C30DF398BFC55D8` |
| ReworkVisualProbe.exe | `694AA6826893BD1F1725222821471ABF27D6AB509988EEA5614281D2D5CC44F0` |
| RenderFrameTests.exe | `A725A96F3132BBD99AD3549523E566FE7ED8370BFE6EB87FAE832495BCD0131C` |
| surface_lit.vert.dxil | `2D6D7B6C9BB26BFDF35525E5F83597FEDD4A081FA19756437568F0823A9C70BA` |
| surface_lit.frag.dxil | `7135D27CA7B896BC89753A19773573776285B34D96146DCE1BDB18C67ABC4A56` |
| shader manifest | `3A4F827AC1789F3254B8002306C74D36882C71C572DD60A19EE1909A4220F81A` |

Staged content: **189 assets**, `f9c6abf944c0867dfd6f6329c6c96f2b8f8b2e03b544c21b1a845c0cedbe97ae`. Legacy regression used `check/runtime`, where Ribterra/Rework binaries are identical to `check-final/runtime`; The only thing that changed was the geometrical calibration SurfaceLightingProbe.

Launcher and installed r2 are unchanged. Its exe is left with `DCF9514876AD73ED0B4EDA8E6DFBE1C452835A4E478B6AF007DB657E58706B35`. Old assets in staging are retained in `.ribterra-assets-retained-*`; The shader-negative clearance reported before launch only deleted its new temporary copies in `check/temp/ribterra-shader-contract-*`.

## Next job is just the engine.

Finish E01/E02: shared metric/projection adapter, capability/lifetime contracts, production HDR/MRT graph and material channels. Then connect the receiver surfaces/minimum assets, causal shading/time/moon and World FX, with separate proof of each step. A successful current probe **does not open** a return to the game.

Approaches used: ponytail directed reuse of the current SDL_GPU; Karpathy — verified narrow contract; C++ standards — clear ABI/type/resource boundaries; ultrawork — independent shader/probe/ABI/pixel/consumer checks. None of them replace engine-first demand or native acceptance.

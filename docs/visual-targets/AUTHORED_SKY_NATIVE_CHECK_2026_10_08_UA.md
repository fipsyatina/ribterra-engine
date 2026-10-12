# Authored sky — validation of the real GPU path

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **A limited component is working; Full engine NOT accepted.**

It's not a new version of the game, it's not final art, and it does not establish fidelity to the concepts. The test transmits the geometry through the real `AssetCatalog` → schema-2 `GameArt` → `Renderer2D/SkyShadowSlice`, but uses the static technical scene 640×480. All E01–E17 remain OPEN; GameView is integrated separately.

## Actual run

- Fresh `build/sky-authored-2026-10-08-attempt10`: nine C++ translation units, `/W4 /WX`, three executables, 14 successful command sidecars. 104 source/snapshot entries and 12 protected previous evidence inputs were unchanged until the start of the next separate integration GameView.
- 69 shader files reused after checking the current source closure and hashes of the accepted shadow build. **This is not a fresh compilation of shaders or new shader-negative tests.**
- `AuthoredSkyNativeProbe.exe` actually runs twice: `build/sky-authored-native-2026-10-08-attempt05` and `attempt06`. Both exit0, by 13 named checks, all nine control obligations are fulfilled.
- By 32 PNG: 32/32 hashes coincide between the probes and with the reports. 54/54 files of six derived catalogues are also byte-exact. JSON reports contain different absolute output paths, so their full byte equality is not declared.
- Native Direct3D12, `NVIDIA GeForce RTX 3050 Laptop GPU`, driver `32.0.15.9282`, software=false.
- The separate focused verifier of this set: exit0; 41 exporter negatives, 22 native MaterialBridge PNG, bridge 13 focused groups ×2, 19 schema invalids, 13 transactional adapter failures. This verifier itself does not launch an authored native probe; The two named native runs were performed separately.

## What the frames and controlled changes establish

1. Masks 1/2/3 address individual solar and lunar sources; Independent ground and typed-material receivers have causal darkening. CPU samples are taken from the actual descriptor, with a margin of ≥5 shadow texels from the edge.
2. The change of world anchor carries the shadow, but not the screen rectangle of the sprite. The old area returns exactly to the open control, while the new area changes to the shadow control.
3. Changing only the screen rectangle of the sprite changes the visible pixels of the sprite, but retains the ground shadow ROI byte-exact. Rectangles fully framed; Nonempty visible content is checked in both locations.
4. The single camera+anchor shift leaves the adapted caster rows and the byte-exact frame.
5. Moving only the camera changes caster vertices by exactly minus the camera delta and causally shifts the shadow; The receiver rows are unchanged.
6. Authored caster works without the very sprite draw; Its ground pixels match the sprite case.
7. The raster caster mask 0/255 does not change the independent world-triangle sky geometry or frame.
8. Changing one authored vertex changes the CPU-predicted membership and GPU shadow pixels. The minimum margin 5.960032 of texels; The visible sprite remains nonempty.
9. Separate controls verified opposite Sun/Moon directions, two frame slots, exact UI, disabling directional lights without losing local/emission contributions, and target-candidate rollback.

Shadows: independent D32 maps 1024², two sets of frame resources; 16,777,216 attachment bytes. GPU execution time in these captures is null. Capture/readback is not performance sampling.

## The Limits of Visual Evidence

Reviewed unchanged native PNG `anchor-base-shadow.png` and `geometry-one-vertex-moved.png`: technical grey receivers, control sprite and red UI marker. This confirms the availability of real-world capture pixels, **not** artistic similarity. PNG does not prove desktop scanout, temporal video or laptop control. Native computer-control API is not available on this device.

Unchecked by this component: normal GameView, all final art categories, full pan/zoom, low-horizon shadows, alpha foliage, camera-following shadow windows, water/reflection/mist/weather transitions, graphic presets, smoothness, 120 FPS in 1920×1080, full CTest or release/package. Launcher not changed; Dynamic in a regular game is not declared ready.

## Evidence binding

- Source native probe: `BC757BE347FF83924BCB97C84B15FD468454F3E1824FF0F0779A221513BD30DE`.
- Native executable: `5107A54A762F7643159C75CF23FB0DBF52356DC4C677E9C0AE31D13C2F4EDA4F`.
- Build report: `313A42CB21A63C443321E00254BD5129D304078E7ACB12498CDCA2FB554EEBE9`.
- Shader manifest: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`.
- Focused verification ledger: `849701069434107D3896A599C1856E4EDB7428562E0071DB0FC3992910CBC3E3`.
- Native05 report: `88ADEA99C86802C824AF307E17B2B43E48B189B5BBB1608B15E32672C048147E`.
- Native06 report: `14131511AB1FA1F611125EDFFCC8A53EE113485E555DE9F4DD8D46B0E254D1BC`.

Logical sizes after the runs: build10 — 75,275,248 bytes; exporter-checks03 — 70,892,693; native05/native06 — 60,374,364 each. Every root is below 128 MiB. Unaccepted earlier copies are subject to a separate, precise cleanup after notifying the user; they are not retained indefinitely. See `docs/ARTIFACT_RETENTION_UA.md`.

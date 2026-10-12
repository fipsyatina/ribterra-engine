# AnalyticAtmosphere V1 — limited native verification

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **BOUNDED NATIVE PASS; E10 and E01–E17 OPEN.**
This is evidence of a new atmospheric layer in joint production Renderer2D through
real GameView/ClientSession, no game acceptance, concept art or 120 FPS.
[Contract and replacement algorithm](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md).

## The Persecution

- Fresh strict `/W4 /WX` collection:
  `build/gameview-atmosphere-2026-10-08-attempt03`, the actual exit 0.
  22 translation units, 151 source bindings, 120 commands with zero exit,
  23 shader programs / 46 stages / 51 source closure / 93 artifacts.
  521 files, 116 710 960 logical bytes (111.30 MiB).
- Two launches of the ** of itself ** EXE: native `attempt02` and `attempt03`;
  runner and actual subprocess exit 0 in each, D3D12,
  NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Each native root has 34 files: 26 GPU PNG, two packed WaterCoverage
  downloads, three current test saves, JSON result, execution log and ledger.
  Root sizes 34 577 439 / 34 577 436 bytes. Full runtime was not repeated.
- **29/29 derived byte-exact outputs** between the protons: 26 PNG + two raw
  downloads + JSON. Secure world IDs in saves and timing execution
  the ledger is logically different; They're not called byte-exact.
- Independent read-only audit of the collection checked source snapshot/current hashes,
  command exits, shader closures and real DXIL reflection. The old 40 DXIL and
  40 reflection JSON byte-identical to Water build02.

The name `GameViewWaterProbe.exe` and build helper remain historical:
`-ProbeSource tools/gameview_atmosphere_probe.cpp` selects the atmospheric test.
Build report separately says execution=false; execution is provided by native ledgers,
And not the very fact of creation EXE. Ledgers unsigned, not independent GPU timer.

## Causal checks

| The verification | The Actual Result |
|---|---|
| Old Water graph → atmosphere disabled | PNG and raw carrier byte-exact |
| The early atmosphere against disabled | 369 257 changed pixels |
| Pause / same shown clock | PNG byte-exact |
| Shown clock 10→11, water phase fixed | 35 412 changed pixels, isolated atmospheric drift |
| The diagnostic clock 12 / 22 | 364 200 / 364 350 changed pixels against morning; morning01=0 |
| Diagnostic haze 0→1 | 361 656 changed pixels |
| Similar terrain panels near / mid / far | RGB change means 0 / 73.2 / 78 |
| Equal depth, low/high | RGB change means 78 / 8 |
| Equal altitude/depth, coast inside / near / outside | RGB change means 78 / 64.48 / 0 |
| Reflection off | 9 841 changed pixels only in water classifier + 1px margin |
| Opaque UI and final alpha | UI exact; PNG alpha=255 |
| Hidden surface reveal=0, emission on/off | Scene exact, without self-emission outflow |
| Actual GameView unknown source | The target source is not submitted; scene and water AABB exact |
| Pan and zoom.62 / 1.30 / 3.00 | Three GPU captures, origin/clock and actual-water provenance verified |
| 32 actual individual water regions | Accepted, GPU capture |
| 33 active regions | Candidate rejected to frame/camera/pick publication; The next frame is exact |
| Disabled in the 33-region scene | Count=0, 33 water cells are rendered; Active capacity not charged |
| Seven invalid renderer inputs | Rejected to GPU; the next valid PNG/raw exact |

Raw RGBA8 downloads: 1920×1080, 8 294 400 packed bytes, GPU row pitch 7680.
In each: clear 1 698 582, land 302 496, water 70 657, bridge deck 1 865,
shore-water 42 832, invalid tuples **0**. This is the exact **binary ** carrier,
I'm not AA coverage 1... 254. Height/depth/coast panels — separate typed shader fixture;
His hand AABB is not shown for water ClientSession. Origin of the regions of
revealed Water is separately verified in the GameView and 32-region fixture.

33-region rejection keeps the same published frame/camera/picks and GPU result.
Terrain cache is synced earlier; The full internal nonmutation is untested.

The Hidden-surface test does not remove an already complex local light/caster. Separate
The actual unknown-source test checks the non-submission of the entire source. It's different.
the statement; They don't replace full FoW/composite acceptance.

## ABI, resources and consistency

AtmosphereGpu 640 bytes; new b0 wrapper 1024 bytes with unchanged old
384-byte prefix. Only three new fragment variants have b0=1024;
The old ones have b0=384. Water AABBs offset512, array stride16. b1–b3 and slots
It's not expanded. No new render targets are available: 174 182 400 nominal world attachment
bytes in 1080p + available separate sky targets. This is not total/peak VRAM.

Gameplay, terrain authority, save/network schema, launchers and current-build
The pointer is unchanged. The window SDL/D3D12 is launched visibly; The final GPU
PNG morning/noon/night/capacity. This is a **not ** desktop scanout, mouse control,
DPI/input proof or unedited video. Technical control building and land
Visual is not the ultimate concept art.

## Evidence identity

- EXE SHA256 `8C28D8CDBA8F9849E1CDCD9DC3D380D4927CB68D867E5465637BF3FC2F32BFC3`.
- Build report `4D4A7E7BE82464403FBE80AD7A8798D7652601F1AD31CECDEA4E455F916C024C`.
- Shader manifest `213DA2A019D152134D5BD75088C3FED794BD879CE34851B825FCC16B14B0CBA8`.
- Native result `091A0286FDFB46F40B86268B57CBEC3D922872D42702F9E7E6E06B53CEE17A11`.
- Old and disabled raw `56BD855A4941255641A7F503FAE75C4826F0C6FEF479422548A3A209DF632310`.
- Runner `E2BC7B4D7ED25EABBFC95D20B995708250516C2EE4686F132FFB473B60817A9C`.
- Native02 execution `DAE71474E0910B7533160D5663AE30A7CAC63E9106BE7098380B884E3FEC305B`.
- Native03 execution `B3CF42C7A083B252FAECE97036ED3896B5C2112B5F7010815B1374D3899679D9`.

Execution ledgers store actual argv, UTC, exit, input before/after hashes,
The output inventory is accurate. The source frozen during build03/native02–03.

## Historical rejections and open borders

Build01 exit1: guarded atmosphere include was absent in old declaration
closures. Dependencies and contract corrected; The old binaries haven't changed.
Build02 exit0, but native01 exit1 after 22 captures: procedural terrain for
The 32-region fixture violated the upward supported normal requirement. It's fixed.
only flat diagnostic normal halo; The renderer validation is not impaired.
These attempts are historical failures, not current PASS.

After confirmation of replacement and notification the exact plan is executed separately
compiled-only clearing of 27 build02 files (112 939 136 logical bytes).
Its old EXE is no longer preserved; The report remains historic and has not
all of your binary inputs on the drive. Source/shaders/assets/logs/native evidence,
latest build03 and verified water rollback02 protected; It's not a repeat of the ordeal.
It's an old collection and not an GitHub archive.

**OPEN:** host-authoritative time/weather, full world without provisional 32-region
cap, common night/weather/shadows/precipitation/wetness/snow/trails, normal
startup, native video and temporal smoothness/reprojection tolerance, DPI/input,
AA/repeated-failure recovery, presets, full scene ≥120 FPS and the entire CTest suite.
The current `fps` / `gpu_execution_ms` — null, not zero and not a benchmark result.
Do not switch to generator, mechanic, or mass art/HUD to E01–E17 acceptance.

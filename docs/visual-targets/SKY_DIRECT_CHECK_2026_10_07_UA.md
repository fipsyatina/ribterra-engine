# Independent direct sunlight and moonlight — partial engine proof

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Date:07.10.2026. **Part opt-in GPU step, not ready engine/play/release.**
All of them E01–E17 OPEN; GameView=BakedCompat, Dynamic unavailable. ≥120FPS in 1920×1080
On the RTX3050Laptop, artistic fidelity to the concepts and the full scene are not yet confirmed.
Gameplay/generator/new HUD/catalog/launcher were not changed at this step.

## Implemented

`WorldGraph::SkyContactSlice` clearly expands GroundContactSlice. The Sun/ambient/four
The locals remain unchanged `SurfaceLightingFrame`384. Separate `MoonLightingGpu`
has 32bytes/align16: direction float4 offset0 (toward source, w0, length² tolerance.001),
normalized linear RGB/intensity float4 offset16 (RGB[0,1], intensity[0,64]).
The intensity of the input turns off the input, but not the validation. DefaultRGB.55/.68/1 — diagnostic,
NO approved paint colors or calendar/time curves.

Frame has explicit optional Moon: Sky requires presence, all other graphs deviate
It's up to the command acquisition/frame-slot release; `clear_commands` dropped it. Software
It doesn't support Sky Graph. The shader variant missing is a construction failure, not a fallback.

Two new programs: ground_sky_lighting (b0lighting384,b1Moon32) and material_sky_forward
(b0lighting384,b1Sprite160,b2ForwardMetadata32,b3Moon32). Separate moon diffuse+bounded
Specular does not use sun_direction/color, ground sun_visibility, contact/AO,
local visibility/receiver mask or emission. The old solar equation hasn't changed.
Emission remains after illumination; UI — after Composite and beyond sky light.
Surface_hdr is not expanded: this graph prohibits surface_lit compatibility geometry.

Contact registry/pass/GBuffer tags remain the same. Seven attachments,
5empty-contact/6populated passes; There are no new render targets. This is NOT a measurement.
VRAM/peak or productivity. The moon is still here ** without authored directional shadows **.

## Current evidence

Capsule:`build/sky-direct-2026-10-07-attempt03/`, snapshot/directMSVC
C++20/O2/MT/W4/WX/permissive/UTF8, fresh renderer/contact/probe objects; Not old
Ribterra/Presentation/GameArt libraries. No CMake/fullasset staging/install/delete.

- MoonLightingTests:51checks PASS; ContactShadowTests:211checks PASS.
- Shader positive: exact DXIL bindings/ABIs/include closure/manifest PASS.
- The old 24DXIL+24reflection files:48/48 byte-for-byte unchanged.
- Native-01/02: 9 groups of actual checks,11pre-GPU invalid payload cases,26PNG each;
  D3D12/NVIDIA GeForce RTX3050Laptop GPU,driver32.0.15.9282,software=false.
- Sun/Moon reversed illuminated two normal-surface earths and a typed sprite fixture.
  Moon direction -X→+X causes a change in the illuminated side. Sun visibility1→0 has
  the positive difference; moon visibility1/0 byte-exact. Contact is positive darkening
  ambient, but sun-only/moon-only pair byte-exact. The controls local-only/emission-only
  have positive signals for the same exposure, UI bytes are constant.
- Twilight combined is tested in linear space after the inverse sRGB with tolerance.025;
  A/B/B/A binary frames and resize800×600→640×480 are restored byte-exact.
- Repeated SkyPNG:26/26byte-exact; old ContactPNG:31/31byte-exact.
- Fresh selected RenderFrameTests:software/D3D12PASS, Frameclear Moon/contact reset;
  All 57stagedshader inputs are constant. It's not full. CTest/GameView suite.

`shader-negative-fixed-02.log.exit.txt`: actual exit0. Rejected38register/space/output/ABI
mutations,14direct+1two-levelincludehash mutations,2noncanonicalincludeforms,
owner/capsule-axis/stride,2numerical and3moon-independence mutations. Final independent
source/oracle/evidence audit: **APPROVE only bounded direct-light slice**.
Native build label honest `unversioned`, not 0.26release; So the provenance is capsule/hash.

## Provenance and preserved failures

Attempt01 is rejected due to C4324 intentional CPU-only LightState padding; suppression
local only around this wrapper, not wireABI/flags. Attempt02 compile is over,
But the helper had PowerShell `/out:` array-precedence link bug. Both roots are preserved.
Attempt03 production build/CPU/shaderpositive/native is over, but the initial whole
the recipeexit1 on the nested-include test fixture.
Fixed01 negativeexit1 is renamed to semantic-fixture directory. It's all right.
deleted/not re-recorded and not renamed FAIL→PASS. Each repair script/log
It has a name of its own. Protected old5EXE/manifest hashes after each failure are unchanged.

## Visual Checking and the Next Step

Unmodified native readbacks sun-a0.png/moon-b1.png reviewed: warm/cold
It's a contribution to the opposite normals and the same red UI control. It's a simple geometry.
and1×1schema2fixtures, **not copies of concept artwork and not verification of house categories,
The tree, the person/load**. Native SDL diagnostic window/readback is not a desktop scanout,
laptop control, video movement or FPS acceptance.

Next you need an independent authored sun/moon silhouettes/visibility carrier and
time/weather curves, the minimum real editable art kit, atmosphere/water/FoW,
presets/profiling and GameView. ContactAO cannot be packed in sun/moonvisibility;
GroundMaterial reserved lanes, local receiver bits and World.W are not allowed
hidden by the moonshadow channel. The game is waiting for the full engine gate.

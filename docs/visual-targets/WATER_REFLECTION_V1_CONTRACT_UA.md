# Water / Reflection V1 — separate cumulative contract

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 2026-10-08. Status: Code and bounded native GPU checkpoint verified;
The water intake and engine are open. Evidence and limits:
[Water / Reflection check](WATER_REFLECTION_CHECK_2026_10_08_UA.md).
This is a specification of the Render Core/World FX contract, not the E09 or the entire engine.

## Diagnostic raw readback — separate follow-up

`Renderer2D::read_water_coverage_v1()` returns tightly packed RGBA8,
actual GPU row pitch, size, frame slot and generation of last
Successfully submitted Water to the staff. This is synchronous diagnostic: own
transfer/fence, not per-frame path and not GPU timing. The Call is Serialised
from the renderer on the main thread; During the rendering, to the successful frame, to the other
graph or stale generation — reject. Separate transfer is not touched
normal capture `FrameResources::transfers` or render telemetry.

Committing new targets disables old readback; rejected candidate
leaves the previous slot/generation fit. Partial recovery submission,
It's the one that could rewrite that slot, disable it, not release the old bytes.
Raw tuples of the current binary implementation: clear=(0,0,0,0), Land=(0,1,0,0),
BridgeDeck=(0,3,0,0), Water=(255,2,shore*17,255).

Recent follow-up build02 and native05/06 have passed: actual 1279×719 drawable
→ fault36 on the last reflection candidate → old raw bytes exact
→ retry54 from ceil-half640×360 → restore72 from 1080p and baseline exact.
[Current evidence](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md).
This is a binary packing check and a specific rollback sequence, not
intermediate coverage AA, all recovery failures or total/peak VRAM.
Recovery conservatively disables readback when reused
the same slot; The random series of failures on this test is not accepted.
The previous native01/02 checkpoint uses the old source cohort
And it's preserved as historic.

## ABI and the Count

`WorldGraph::WaterReflectionSlice` keeps SkyShadow, an independent Moon,
Contact V1 and local lighting. Old graphs don't get new passes.
This is the order: SunShadow → MoonShadow → Reflection → GroundGBuffer →
GroundContact (if there is an input) → GroundLighting → WorldForward → Composite → UI.

The first four GBuffers and all their channels are stored. SurfaceWorld.W remains
The exact Contact V1 tag 0/1/2: it's not a cover, it's a mask, it' s not a mix weight.
GroundMaterialGpu remains the 48 byte. CPU-shutterbox GroundMaterialDraw — 80;
Its former tail contains the class @68, coastal bits @72, cover @76.

The fifth MRT `WaterCoverageV1`, full-resolution RGBA8 UNORM, clear=(0,0,0,0:

| Channel | The Meaning |
| --- | --- |
| R | The geometrical coverage of water; The drought and the mud write 0 |
| G | Class/255: Land=1, Water=2, BridgeDeck=3 |
| B | Beware/15: N=1, E=2, S=4, W=8; Unknown neighbor not shore |
| A | Display authorisation: 1 only for water |

This carrier point/clamp- is simplified. Reveal is left alone in SurfaceFlags.A.
RGBA8 is chosen instead of R8 because class, shores, and permits are also required; I'm busy
The old canals don't repackage. It's not a minimum number of bytes, but
the individual verification contract without prejudice to the existing ABI.

Current producer — opaque, sample count 1. CPU coverage/reveal in the new graph
They only accept 0 or 1. The presence of RGBA8 is not evidence of AA. Wood coating
edge, its raw readback and permissible AA- contours remain open.

WaterSurfaceGpu — 64 bytes: absolute origin XY/indicated time/wind;
the normalized flow/strength/normal strength; the linear tint/roughness;
distortion ≤1.5 reference pixels at reference height=900, reserved zero.
Normal — is a distinctly typed linear tileable texture. Two normative UV- phases
bound to the absolute XY, with a rotation of 37°, at speed .018+.022·wind;
the water geometry remains render Z=0. The pin changes the diffuse albedo before BRDF,
It's not adding brightness at night. The coastal phase uses origin XY and +.5 for
It's a cell that's not moving with the camera.

Reflection — separate half-resolution RGBA16F texture, clear transparent.
The reflected geometry uses the fixed atlas UV, source P/N and
The light source. RGB premultiplied; blending ONE / ONE_MINUS_SRC_ALPHA.
Strength and fade are applied once. GroundLighting mixing the display
only by the current full-resolution receiver: Water, coverage>0, reveal=1,
allow=1. Dry/natural/unopened territory is not obtained regardless of the
The result is a linear sample or distortion. The final scene/final alpha — 1.

## Memory and Size

The first seven target records keep order; recordings 7/8 — Coverage and Reflection.
The old graphs have zeros in the last two records. New Count — 9 attachments
on each of the two frame sets. Reflection dimensions: ceil(W/2), ceil(H/2).

Nominal active payload: `2 × [40WH + 8ceil(W/2)ceil(H/2)]` bytes.
For pairs — 84WH: 1920×1080 = 174182400 B / 166.11 MiB;
2560×1440 = 309657600 B / 295.31 MiB. This is not a driver allocation/VRAM measurement.
Resize creates a full candidate before publication; old+candidate temporarily
are co-located (1080p→1440p ≈461.43 MiB only these targets). Sky maps, sampled art,
Upload buffers and driver overhead don't go in here. Capability/pipeline failure
shall not turn off the water or change the beaker silently; resize failure keeps the old set.

## The limits of the current checkpoint

`compose_engine_control` now reads actual Terrain::Water, revealed neighbours
and completed the bridge from ClientSession. The technical narrow layers write BridgeDeck
to GroundLighting, rather than enclosing the entire cell; The connected sections align the axis.
Hidden cells do not create receiver/shore/reflection data. The Geometry of the Earth
And the picks are from the same terrain mesh. Normal GameView/startup is unchanged.

Native probe creates a separate current-schema diagnostic world with an uneven
river, two banks, three really built bridges and reveal frontier.
It's not evidence of a random generator or a new finite art. Clock/camera
the controls of the software; They don't prove keyboard/mouse/pause interaction/video.
CPU raster classifier with boundary 1 physical pixel — leakage diagnosis, not raw
GBuffer readback. Concepts, AA, full input/video, normal scene, weather,
all E01–E17, full suite and ≥120 FPS remain OPEN to their own factual proofs.

## File preservation

The new build/probe uses separate bounded roots ≤128 MiB. Old latest
and the verified rollback is not removed. The compiler can clean their own
transient files only in the pre-named new compiler-temp. Atomic save
fixture can clean up its failures `.tmp`/`.bak.tmp`; These are the exact ways we need to
name the user before launching. There's no change launcher/pointer/install,
GitHub or clearing old roots this checkpoint doesn't do.

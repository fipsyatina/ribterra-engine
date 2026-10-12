# Environment V1 — Diagnostic common input of lighting and atmosphere

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. Source implementation + [restricted native GPU PASS](ENVIRONMENT_CHECK_2026_10_08_UA.md).
**E06/E10/E01–E17 OPEN.** Not host-authoritative calendar/weather, not normal
It's not a final art curve, it's not the geographic model of the sun.

`EngineEnvironmentSettingsV1`: version=1, hour [0,24), cloud01/haze01 [0,1],
atmosphere_enabled. All numbers are finite. Input exclusive with manual atmosphere;
requires water and renderer **WaterAtmosphereSlice**, even when fog disabled.
GameView cannot inspect the selected graph; The wrong renderer is rejecting
frame your contract. Normal/legacy/manual composition is not switched.

Environment ** has** ambient, sun, moon and atmosphere. Manual sky inputs
are not inputs to this mode and are rewritten; only external local-light
counts/rows is stored byte-exact. Authored windows are added after this
individually; The emission mask/scale/local profiles do not dim together with the sun.

## Versioned art curve

Solar phase = pi*(hour-6)/12. Day/night height = positive/negative sin phase;
abs(sin)<1e-6 is set to zero so that sin(pi) residue does not activate the shadow source.
Sun azimuth = 45° + 180°*(hour-6)/12; elevation20→65° by day height.
Sun intensity=.9*smoothstep(0,.2,dayHeight)*(1-.75*cloud).
Sun linear RGB: warm(.95,.55,.30) → high(1,.95,.82) followed by smoothstep(0,.5,height).
The lower artistic 20° floor contribution is smoothly disappearing, infinitely long
There are no active shadows. Directions normalized, toward-source, reserved W=0.

Moon opposite azimuth, elevation20→55° by nightHeight;
intensity=.18*smoothstep(0,.2,nightHeight)*(1-.85*cloud), coldRGB(.55,.68,1).
This is the opposite of the V1 orbital curve, not lunar phases/astronomy.

Daylight=smoothstep ((5,9,hour)*(1-smoothstap(16,21,hours)), the same as in fog.
Ambient lerp night(.025,.04,.07) → day(.22,.24,.25)*(1-.45cloud).
Haze is transmitted by an atmosphere adapter with the ** hour itself**; only haze input
It doesn't change lighting/moon. Cloud and haze — independent clearly defined diagnostic
numbers, not inflated season/prognosis/client RNG. Shown seconds for water/fog
remains the only transmitted clock, not wall time; Paused input repeats.

## Limited Evidence Needed

Real GameView/ClientSession + native D3D12, shared HDR/shadow/reflection graph:

- Resolved manual equivalence, repeated frame, raw water classes and UI exact.
- Hour sequence0/6/9/12/15/18/21/3; cloud weakening and haze-only isolation.
- Sun9/15 and Moon21/3: caster-on minus caster-off masks, nonempty and relocation
  The centroid on the exact land carrier. Another sky source has zero intensity.
- Midnight nextafter(24,0)→0 numeric tolerance1e-5, not byte-exact claim.
- Horizon5.9/6/6.1 and 17.9/18/18.1: zero source is equal to 6/18, successful render,
  same-hour sky-on/off delta: equal to6/18 exact, next to positive. Ambient,
  The local and the emission in this pair are unchanged. Without the statement monotone total energy
  or exact shadow topology handoff.
- Incoming active local rows and appended authored rows exact; emission metadata
  unchanged; The native emission-only night delta is warm. Hue test has
  zero other lighting and emission scale.125, so that the SDR clamp does not reverse
  The difference in the number of people. It's not a hue measurement of saturated full night frames.
- Invalid version/hour/cloud/haze/conflict/no-water rejected; published
  frame/picks/camera and the next GPU result is exact, not all-internal-cache proof.

Don't take this adapter as a full E06 without normal integration, motion/video,
all artistic categories, authoritative time/weather and performance.
There are no new shader variants, uniforms ABI or attachments for this step.

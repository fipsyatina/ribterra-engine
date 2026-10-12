# Joint GameView-scene and authored contacts — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Part stage is completed:** real session terrain, water, three completed bridge decks, eight neutral categories, contact/Sun/Moon shadows, local lights, emission, reflection and atmosphere work together in the current production renderer. It's a bounded opt-in scene. **All are full E01–E17 OPEN; reference fidelity NOT_ACCEPTED.**

## Changes

Schema 2 received the non-binding `contact_arts` and `contact_shadows`. Ten separate tight R8 `.rtex` masks have a verified hashes/region, transparent outer ring and a non-zero internal footprint. Registry ownership is separate from sprite caster textures. The loader and adapter publish the full validated candidate transactionally; It's a startup-only directory.

Sprite feet and contact anchors use a single interpolated terrain triangle, its exact normal and tangent basis. The finished panel has the same height `.08 renderZ` for feet, contacts and screen queries. Water terrain semantics is separate from water FX activation: ordinary water does not become a contact receiver after the effect is turned off, and the physical bridge deck is retained. Unsuitable steep surfaces do not receive contact rows.

All-category lighting is measured on authored face patches. For each category, only its usual sprite draw is removed; And the individual measurements are category-removal-qualified pixels. The illuminated land behind the inhabitants cannot replace this control.

SDL_GPU, WaterAtmosphereSlice, HDR/alpha, contact/shadow, and GPU ABI registries are reused. This step does not add production translation units, graph or shader programs.

## Current source cohort and actual execution

- The repository `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; The available dirty source didn't go away.
- Kit `build/engine-cumulative-kit-2026-10-08-check01/export-a`: 62 files / 9 474 614 bytes, eight categories / ten contact arts. Actual Python exporter/negative checks exit 0; retained `export-a`/`export-b` byte-exact.
- Fresh build `build/gameview-engine-cumulative-2026-10-08-attempt04`: strict MSVC C++20 `/O2 /MT /W4 /WX`, 21 production TUs + probe, 157 source bindings, 120 actual commands with zero exit sidecars. Fresh DXIL: 23 programs / 46 stages / 93 assets with verification of interfaces/bindings/hashes. Root: 527 files / 123 417 234 bytes.
- Probe SHA256 `4B6F7F39D7D23A36D307F9916EC85374B2D75E0B2DA180EC46FEE2838783A155`.
- Native roots `build/gameview-engine-cumulative-native-2026-10-08-attempt04` and `attempt05`: actual exit 0, hardware Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Start-up: 22 C++ loader checks and 11 adapter checks. In each run 39 GPU PNG, the raw carrier, JSON, retained diagnostic catalogs and saves.
- Parent read-only verifier actual exit 0: `cumulative current evidence checks passed; sources=157, bound_files=1059, zero_commands=120, repeat_outputs=252`.
- All 252 derived byte-exact outputs between actual runs. Two save have their own random World identities, so excluded only from cross-run comparison; Each individual SHA256-bound to his execution. Runner ledger/transcript is not given for deterministic GPU output.

Repeat the creature of this cohort:

```powershell
C:\Python314\python.exe tests/gameview_cumulative_evidence_tests.py build/gameview-engine-cumulative-2026-10-08-attempt04 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-engine-cumulative-native-2026-10-08-attempt04 build/gameview-engine-cumulative-native-2026-10-08-attempt05
```

After further changes to the source, this result becomes a historic checkpoint. Verifier intentionally rejects source drift; The next current statement needs new binding and actual execution.

## Causal Effects

| Control | The Actual Result |
|---|---:|
| Ambient contact on/off: land | 2 224 changed pixels |
| Ambient contact on/off: bridge deck | 464 changed pixels |
| Contact on/off, ambient=0, direct Sun | byte-exact frame |
| Sun sky-caster on/off: land | 69 522 pixels |
| Moon sky-caster on/off: land | 81 604 pixels |
| Authored local light on/off: land | 33 535 pixels |
| Other heat window emission | 894 pixels |
| Reflection on/off | 7 655 pixels |
| Hidden unknown object / fixed shown-time repeat | byte-exact frames |
| Raw water carrier / opaque diagnostic UI | unchanged in the relevant causal controls |

All eight category patches have positive independent ambient/Sun/Moon/local responses. The minimum mean RGB deltas: ambient 31; Sun 55.83; Moon 23.67; local 72.375. The following is a list of the most commonly used names for the name of the company: These are representative faces, not every texel or the entire production catalog.

Three camera cases — zoom `.62`, `1.3`, `2.0` from the pan, in each of the ten contacts and eight checked feet/picks. Zoom `3.0`, continuous motion, DPI/input is not accepted. Duplicate/zero contact ID and cargo ID overflow deviate with the preservation published frame/camera/picks. It's not a full rollback private caches.

The WaterFX-off CPU control confirms three instances, three physical settings, three contacts and one filtered ordinary-water contact. Steep fixture separately checked on GPU: sprite retained, contact missing. CPU WaterFX-off is not called a native WaterAtmosphere frame.

Nominal attachment ledger **174 182 400 bytes** for two frame sets, including with coverage 16 588 800 and half-resolution reflections 8 294 400. This is not a measured peak VRAM. FPS and GPU execution time not measured; the field `null`.

## The Differences and Limits

Native01 stopped at the first frame: the steep fixture changed altitudes under active routes; The error message was also formulated early. Native02 detected unclean water bounds in the off-air. Native03 correctly showed zero resident local light: the test inherited the cone house spotlight instead of a separate Point source. Fixed fixture/diagnostics, full native04/05 repeated. Renderer contracts and thresholds have not weakened.

The frames — are neutral geometric controls, technical terrain/river and green UI marker. They do not accept the artistic detail of the three winter/summer references. Marker does not prove RmlUi/Ukrainian script/interaction. Fixed time: `paused-repeat` checks the same explicit inputs, not the session pause adapter.

Next, you need the session shown-time, lifecycle/input/video, typed smoke/fire/weather/wind, wetness/snow/footprints, art kit, effective settings, performance and normal startup. Full engine goal active; The game is not declared over.

During these checks, the files were not deleted. Clearing obsolete compiled copies after verified replacement has a separate exact manifest and advance notice. Source snapshots, logs, captures, catalogues, saves, current runnable and rollback are stored.

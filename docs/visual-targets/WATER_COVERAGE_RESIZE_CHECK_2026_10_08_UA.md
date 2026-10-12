# WaterCoverage V1 / odd-size resize — bounded native follow-up

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The date is 08.10.2026. **BOUNDED BINARY CARRIER / RESIZE CHECKPOINT PASS**.
This does not constitute acceptance of E09, the whole engine, concept art, normal startup, or FPS.
The original cut: [Water / Reflection](WATER_REFLECTION_CHECK_2026_10_08_UA.md);
[the contract of the deed ](WATER_REFLECTION_V1_CONTRACT_UA.md).

## Real Change

`Renderer2D::read_water_coverage_v1()` reads the fifth MRT of the same production
The renderer, not the CPU raster approximation. Owned transfer/fence, expecting GPU,
256-byte row pitch and tight RGBA8 repack; the use of conventional
capture transfer. The data is attached to the last successfully submitted slot/generation.
To the first frame API is actually rejected. Scheme, shader and color output
not modified; Fractional authored coverage remains prohibited.

## Fresh build02

`build/gameview-water-2026-10-08-attempt02`: actual exit0; 22 TUs,
142 source bindings, 108/108 command exit-sidecars0, 216 log/exit files;
20 shader programs/40 stages/44 sources/81 artifacts; 52 kit files,
50 manifest assets, 5 protected inputs. Independent build audit: 0 mismatches.
476 files, 115 928 623 B (110,558 MiB); EXE42 629 120 B.

An independent read-only native audit confirmed all 5 input bindings,
18 native rows in each ledger, final closure20files, actual argv/exit0,
17/17 repeat-derived hashes and 14/14 old PNG; direct raw parse gave
The exact report counts and0 invalid tuples. Scope is limited to the bounded checkpoint.

| The Evidence | SHA-256 |
| --- | --- |
| Build02 EXE | `72587E5894B4F508E4483BB87EE5FFE15DAB1F2EB75A3B6E04B91F400FB44172` |
| Build02 report | `7A80BD154A8E635FA6178A4302C3A313FE4FF843134791FAE3E4A28961BD3014` |
| Runner | `E2BC7B4D7ED25EABBFC95D20B995708250516C2EE4686F132FFB473B60817A9C` |
| Native05/06 report | `3BA389A002AD444D19E6C92D67FDC6158232101643CF9DAE9B276BCD2B7C8DB6` |
| Raw odd carrier | `B664F4BFA223787B1BD63D18D3BDC91235F46F01188824E14ABA3DC4713CFB34` |
| Native05 execution | `6DF22E0D313919A9380547359F9F90A22F9BD6F600D215DEF90B3D5B86A3A9AF` |
| Native06 execution | `F8521334AA5FCB5A5D78F6FD54F0593A1DACA31F5FF068B79CEDC24779328D25` |

## Actual native05 and native06

You can see SDL/D3D12 windows on the RTX3050Laptop, not the backend software.
The actual wrapper exit0 and subprocess exit0 of each; each root20files/
10 208 477 B. Native outputs to wrapper:18files/10 197 042 B;
Next up is the write-once execution log and ledger.

15 PNG + raw carrier + original native JSON — **17/17 byte-exact** between
runs. The old 14 PNG from native01 are also **14/14 byte-exact** with the new ones:
no output regression from the diagnostic API was observed.
Saves and execution timestamps/hash are different; We're not saying whole-root determinism.

Raw GPU carrier:

| Field | 1920×1080 to the size | 1279×719 after retry |
| --- | ---: | ---: |
| GPU pitch | 7680 | 5120 |
| Tight bytes | 8 294 400 | 3 678 404 |
| Clear pixels | 1 698 582 | 753 256 |
| Land pixels | 302 496 | 134 286 |
| Water pixels | 70 657 | 31 235 |
| Bridge deck pixels | 1 865 | 824 |
| Water from shore bits | 42 832 | 18 922 |
| Invalid tuples | 0 | 0 |
| Last submitted slot / generation | 1 / 1 | 1 / 2 |

Each pixel has only the permissible clear/Land/Water/BridgeDeck tuple.
Odd raw file — `water-coverage-odd.rgba8`; the size and counts are related JSON.
This is proof of the **binary** carrier packing/classification, not the AA values1...254.

The sequence of sizes:

1. Initial:18 targets, generation1, IDs1/2, 1080p.
2. Actual1279×719 verified directly by SDL logical/pixel queries and renderer
   display_info; density/scale1. Injection36 failed on the final
   `reflection640×360 frame-set1`. The old dimensions/IDs/formats/bytes/generation
   The candidate live0, submittedfalse, previous raw bytes exact.
3. Retry54: generation2/IDs3/4; full1279×719, Reflection ceil-half640×360.
   Coverage7 356 808 B + Reflection3 686 400 B + previous attachments =
   **77 254 480 B** nominal target payload.
4. Restore72: generation3/IDs5/6;1080p/half960×540,
   **174 182 400 B** ground targets. Final PNG is exactly equal to baseline.

Readback runtime compares sequence/submitted/slot/pass array/cpu_total and
stable targets; The source review confirms the absence of telemetry mutation.
It's not a complete runtime equality of all telemetry fields.
Verified the specific sequence of alternating slots; conservative recovery
disables the readback on the same slot. All possible failures have not been proven.

Previous causal checks have also been done: Reflection9885/clock54318/shore5098
the pixel changes, 6 GPU camera cases, 6 rejected inputs, hidden/same-clock/
after-reject exact, final alpha255. Location of these ** color changes** and so on
uses the CPU classifier1pxmargin; The raw carrier doesn't turn it into AA proof.

## Execution provenance and preservation

`tools/run_gameview_water_native.ps1` records the actual argv, UTC begin/end,
Subprocess exit, combined output hash, 5 before/after input bindings and all
18 native outputs. Self-bound wrapper separately: it is not in build02 snapshot.
This is a wrapper-recorded actual subprocess, **not signed/independent execution
transcript, not GPU timing and not displayed FPS**.

Runner does not rewrite the existing output: actual preflight exit1 to native05,
20/20 byte/hash/paths is unchanged; binary didn't start.
Proof: [negative ledger](../../.omx/plans/water-runner-existing-output-negative-oct8.json),
SHA `189C045DEE99A430D891369B7ECAB4D44A72F7E5FA758F869A1C97ACA7EA417F`.

Native03/04 previously had actual nativepass, but the wrapper misrepresents
`total_bytes=0` through the Measure-Object over the ordered dictionaries. These originals
stored as history, **not current storage ledger**. Repaired wrapper
Obviously missing the integer bytes and betting the final row total with the real file lengths,
count and projected bytes. Nov 05/06 has passed; renderer/EXE not picked up by
I'm working on a magazine. None of the files in this follow-up have been deleted.

## Open boundaries

Intermediate AA; an arbitrary repeated failure/recovery; normal startup,
native keyboard/mouse/pause/video; All the effects together, atmosphere/weather,
Concept art, presets, up to date full suite, GPU time/total peakVRAM,
120 FPS/flow. Reviewed GPU PNG — neutral gray control scene,
It's not a final chart, it's not an desktop scanout. AllE01–E17 OPEN; game/generator/HUD waiting.

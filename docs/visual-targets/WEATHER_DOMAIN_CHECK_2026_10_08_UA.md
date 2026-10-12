# Determined weather component — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**The component has gone through strict build, two actual CPU runs, and independent source/evidence review CLEAR.** This is the foundation for E06, E08, E11 and E12. World/save/wire/client/HUD/FX not yet connected; full motion reception and tuning remain **OPEN**.

Typed `CalendarTick`/`CalendarView`, `ClimateConfig`, host runtime, seedless `WeatherView` and fixed-capacity cache were added. In the calendar of the beginning 06:00; simulation ticks, recruitment/youth/path/logistics deadlines keep their own scale. `FirstWinterStorm` adds 864 000 calendar ticks — day 31, winter 06:00, snow cover 650, without losing previous ticks.

The candidate weather algorithm 1 uses a versioned unsigned64 mixer, seed/profile/scenario/day/purpose/period keys, integer temperature interpolation, transitions from the reached value, and bounded event schedules. New numeric defaults marked candidate; It's not a previously accepted balance. The cache has six schedule days and four forecast cards, a maximum of eight wire intervals; The generator generates no more than seven. Min/max and Rain/Snow summary is obtained from all 28 800 samples of each date.

Snow/wetness have signed int64 remainders in strict limits (-28 800, 28 800) and a clamp without outward debt. The work gate has rates 7500...10000 bp and remainder 0...9999. So long as it doesn't affect the game Activity, production or XP. Forecast/cache derivatives; authoritative cover/history/work remainders should be saved and supported by future integration.

## Actual build and executions

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`.
- Fresh `build/weather-domain-2026-10-08-attempt02`: two translation units, C++20 `/O2 /MT /W4 /WX`, three actual zero-exit compile/link commands. 106 source bindings include all project headers, installed nlohmann headers, helpers/verifier and two separate mathematical derivation JSON. Root: 117 files / 9 439 561 bytes.
- Weather source SHA256 `DD985BF116C2D3D31AACFC16F27C9C2E396B7D0373543F4E72F7C9DF2F1C268C`; probe `7D6D876425B542E663756D398F5607F3E03A3FF9E1B9B9F89630DF7C7BEE3612`.
- SAME EXE SHA256 `05B8EBD6269D007CCD862C969B97F2C6E0CA74466B22F1623977BA1CD5F230BF`, 493 056 bytes. Actual distinct output roots `build/weather-domain-native-2026-10-08-attempt01` and `attempt02`, both actual exit 0.
- Each run created one component JSON, transcript and wrapper evidence; 3 files / about 229 KB. They're all inputs/outputs hash-bound individually. Deterministic object exactly matches between runs; The measured object is a fairly different object.
- Parent verifier actual exit 0: `weather domain checks passed; sources=106, bound_files=229, zero_commands=3`.
- The reviewer independently checked all 229 bound paths, sources/current/snapshots, compiler/linker, launcher before/after, actual argv/EXE/reports/transcripts and p95 with raw arrays. Mismatches 0.

```powershell
C:\Python314\python.exe tests/weather_domain_evidence_tests.py build/weather-domain-2026-10-08-attempt02 build/weather-domain-native-2026-10-08-attempt01 build/weather-domain-native-2026-10-08-attempt02
```

After bound headers/source is changed, this result is historical; It doesn't seem to be evidence of later World integration. Build01 actual exit 1 was a preflight failure before the output root was created: the parent helper had the wrong name `Ribterra.cmd`. Corrected to actual `Play Ribterra.cmd`; attempt02 was performed fresh. There was no distance.

## Reproduced controls

68 exact rejection controls check for version/enums/calendar overflow/horizon, host canonical sample, wire forecast/period/storm pair, work records and cover bounds. Invalid advance/jump keeps the previous state/cache. Wire current period now limited the scenario barrier and the four-day horizon; Inactive slots and sorted work records checked.

There are literal mixer/calendars/cover/work goldens, nine independently derived folded keys, 126 boundary samples for seeds 70421/0/UINT32_MAX and jump/rollback equality. Exhaustive 30-day candidate run has 864 000 observed ticks; Each forecast min/max and precipitation summary coincide with the actual yield. Domain state reconstruction verified by extension; The actual World save round-trip was not done.

The source review found that two identical intensity generic Storm events could alter metadata across the north. Connected equal-kind/intensity events are now merged into ** to ** day clipping with the preservation of the early wind key. Nonvacuous fixture seed 286479: original events 891173...897792 and 893382...900539, intensity 837; union 891173…900539, NW wind 951. Ten literal samples and one-tick advance 892799→892800 confirm the unchanged countdown/period. Parent separately calculated mixer/event bounds and five settled temperatures, stored in `build/weather-carryover-derivation-2026-10-08.json` and `build/weather-folded-key-derivation-2026-10-08.json`.

Candidate Standard seed 70421 for 30 ticks: Clear 556 890, Cloudy 249 895, Precipitation 40 432, Storm 16 783 ticks; Rain 41 602 and Snow 15 613 ticks. Temperature -9.4…24.6°C. It's the size of the candidate, not the accepted balance.

**FirstWinterStorm seed 70421 not certified:** start 876050, end 882343, duration 6293; The storm starts at +4.5°C Rain/8000 bp, before ending -6.8°CSnow/7500 bp. Maximum storm temperature +4.5°C, `all_ticks_snow=false`. Selection and actual certification of winter demo seed remains mandatory; The snow phase will not be forced to change the temperature.

## The productivity and the limits

| Pure CPU measurement | Run01 | Run02 |
|---|---:|---:|
| Four-card cold initialization p95, 100 samples | 13.4202 ms | 14.3095 ms |
| Next forecast-day rollover p95, 100 samples | 4.0174 ms | 3.9492 ms |
| Steady tick, 10 000 ticks | 320.05 ns | 253.98 ns |
| Instrumented allocations / allocated bytes, steady and 100 rollovers | 0 / 0 | 0 / 0 |

One-day forecast goal ≤10 ms passed. Four-card total <10 ms — diagnostic `false`, not a reason to hide timing. Renderer FX preparation ≤3 ms was not measured here. Allocation scope — global `operator new` instrumentation, not platform heap profiling; steady fixture contains one valid work record.

6000-resident World overhead ≤1%, full GPU graph/FPS/VRAM/stalls, actual save/full/delta/split/reconnect, native UI and reference appearance still need to be checked on their own. Native goal ACTIVE; E01–E17/P01 not closed. Automatic gate-check certification is not claimed.

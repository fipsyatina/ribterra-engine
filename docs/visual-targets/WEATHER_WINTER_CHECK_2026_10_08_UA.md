# Certification of the first snowstorm and food supply  08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Seed12 has undergone an exhaustive domain scan and an actual World trace in two launches with independent actual review CLEAR. All 6913 ticks of the first storm — Snow, temperature −13.3...−1.7°C. It's a certification of the weather model and the physical ration horizon; main hasn't selected this seed yet, and the graph, FX, the references and the full motion remain open.

The authority tree used is `C:\Users\fipst\Desktop\ribterra`, `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`, weather algorithm1/profile1/scenarioFirstWinterStorm1, save9/protocol23/rules19/generator8. After further bound header changes the exact cohort is historical; Production weather in this wave has not changed.

## The Execution

- Build `build/weather-winter-2026-10-08-attempt01`:actual exit0,15 translation units/20 zero commands/420 source rows, C++20 `/O2 /MT /W4 /WX`.
- `WeatherWinterProbe.exe`:7 966 208 bytes, SHA256 `A8F4D767F2A9204BC9A470C20D46CB50981B29AAB701CD6ED848D4F2FD21984C`.
- Native `build/weather-winter-native-2026-10-08-attempt01` and `...attempt02`:actual exit0. Each has three boundary saves, a report and two runner evidence files. Stable JSON fields are repeated exactly; Only three save UUID payloads and four measured diagnostics can differ.
- Parent read-only verifier actual exit0:420 sources/1020 bound paths/20 zero commands. Independent reviewer rehashed1020 unique files without any discrepancies and also actually performed verifier with exit0. No formal automatic certification has been announced.

```powershell
C:\Python314\python.exe -B tests/weather_winter_evidence_tests.py build/weather-winter-2026-10-08-attempt01 build/weather-winter-native-2026-10-08-attempt01 build/weather-winter-native-2026-10-08-attempt02
```

## Seed and real simulation

Diagnostic ascending search0...4095 rejected seeds0... 11 and accepted 12 for 13 attempts. This is a search cap, not a new rule of thumb. Storm simulation interval `[14035,20948)`, calendar `[878035,884948)`, duration6913; start is in 9600...14400, duration is in 4800... 7200. All ticks have one canonical Storm period and Snow; Rain0. Domain and actual World storm digests are the same:1222428438975540385. Exact start−1/start/end−1/end samples have been verified.

Negative 70421 remains negative: it starts at +4.5°C Rain, with a total of 420Rain and 5873Snow ticks. The snow at the end does not replace the all-snow demand.

The main World was created at simulation tick0/calendar864000/day31/06:00 without replaying previous days, with snow650 and signed remainder0.10 residents have1200 physical Food, Campcapacity10000; Standard remains Food100. There is only one public stop for ten actors, with no import/time/inventory/actor mutation in the main trace.

Exactly24000 actual World ticks were run, with every current/forecast/cover compared against an independently advanced domain runtime. Exactly120 normal ration checks every200 ticks actually consume10Food each:1200→0, with no other food output/reserved/carried. All10 actors remain healthy with hunger0 and automation disabled. Final snowcover734, signed snow remainder24493. This is20 **simulation** minutes at1×;20 wall-clock minutes of play were not verified.

The actual simulation ticks14034/14035/20947 is written as `winter-before.save`, `winter-start.save`, `winter-end.save`. Each load is kept private ClimateRuntime/current/full WeatherView/checksum. Separate pairs direct/loaded move 200 ticks with equal checksum/current/forecast and final private climate; The main trace is still standing. The first tick next definitely enters the storm or exits the end-exclusive boundary. An additional 1200 pair-world ticks are separated from the main24000.

Total4.526/4.163s and world-trace4.319/3.973s — diagnostic timings, not proof≤1% overhead or FPS. There were no new deletions. We still need shipping startup, renderer/HUD, precipitation/wind/ materials/snow/footprints, reference/video/input, broad suite, two-PC/WAN, full simulation/GPU performance and E01–E17/P01 acceptance.

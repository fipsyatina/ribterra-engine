# Детермінований погодний компонент — 08.10.2026

**Компонент пройшов strict build, два actual CPU runs і незалежний source/evidence review CLEAR.** Це foundation для E06, E08, E11 та E12. World/save/wire/client/HUD/FX ще не під'єднані; повне приймання рушія та tuning залишаються **OPEN**.

Додано typed `CalendarTick`/`CalendarView`, `ClimateConfig`, host runtime, seedless `WeatherView` і fixed-capacity cache. У календаря початок 06:00; simulation ticks, recruitment/youth/path/logistics deadlines зберігають власну шкалу. `FirstWinterStorm` додає 864 000 calendar ticks — день 31, winter 06:00, snow cover 650, без програвання попередніх діб.

Candidate weather algorithm 1 використовує versioned unsigned64 mixer, seed/profile/scenario/day/purpose/period keys, integer temperature interpolation, transitions з досягнутого значення й bounded event schedules. Нові числові defaults позначено candidate; це не раніше прийнятий баланс. Cache має шість schedule days і чотири forecast cards, максимум вісім wire intervals; генератор створює не більше семи. Min/max та Rain/Snow summary отримано з усіх 28 800 samples кожної доби.

Snow/wetness мають signed int64 remainders у строгих межах (-28 800, 28 800) і clamp без outward debt. Work gate має rates 7500…10000 bp і remainder 0…9999. Поки він не впливає на ігрові Activity, виробництво чи XP. Forecast/cache похідні; authoritative cover/history/work remainders мають бути збережені та захешовані майбутньою інтеграцією.

## Actual build і executions

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`.
- Fresh `build/weather-domain-2026-10-08-attempt02`: два translation units, C++20 `/O2 /MT /W4 /WX`, три actual zero-exit compile/link commands. 106 source bindings включають усі project headers, installed nlohmann headers, helpers/verifier і два окремі mathematical derivation JSON. Root: 117 файлів / 9 439 561 bytes.
- Weather source SHA256 `DD985BF116C2D3D31AACFC16F27C9C2E396B7D0373543F4E72F7C9DF2F1C268C`; probe `7D6D876425B542E663756D398F5607F3E03A3FF9E1B9B9F89630DF7C7BEE3612`.
- SAME EXE SHA256 `05B8EBD6269D007CCD862C969B97F2C6E0CA74466B22F1623977BA1CD5F230BF`, 493 056 bytes. Actual distinct output roots `build/weather-domain-native-2026-10-08-attempt01` і `attempt02`, обидва actual exit 0.
- Кожен run створив один component JSON, transcript і wrapper evidence; 3 файли / приблизно 229 KB. Усі inputs/outputs індивідуально hash-bound. Deterministic object точно збігається між runs; measured object закономірно різниться.
- Parent verifier actual exit 0: `weather domain checks passed; sources=106, bound_files=229, zero_commands=3`.
- Reviewer незалежно перевірив усі 229 bound paths, sources/current/snapshots, compiler/linker, launcher before/after, actual argv/EXE/reports/transcripts та p95 із raw arrays. Mismatches 0.

```powershell
C:\Python314\python.exe tests/weather_domain_evidence_tests.py build/weather-domain-2026-10-08-attempt02 build/weather-domain-native-2026-10-08-attempt01 build/weather-domain-native-2026-10-08-attempt02
```

Після зміни bound headers/source цей результат історичний; він не видається за доказ пізнішої World інтеграції. Build01 actual exit 1 був preflight failure до створення output root: у parent helper була неправильна назва `Ribterra.cmd`. Виправлено на фактичний `Play Ribterra.cmd`; attempt02 виконувався свіжо. Видалень не було.

## Відтворювані контроли

68 exact rejection controls перевіряють version/enums/calendar overflow/horizon, host canonical sample, wire forecast/period/storm pair, work records та cover bounds. Invalid advance/jump зберігає попередній state/cache. Wire current period тепер обмежений scenario barrier і чотириденним horizon; inactive slots та sorted work records перевірені.

Є literal mixer/calendars/cover/work goldens, дев'ять independently derived folded keys, 126 boundary samples для seeds 70421/0/UINT32_MAX та jump/rollback equality. Exhaustive 30-day candidate run має 864 000 observed ticks; кожна forecast min/max і precipitation summary збігається з реалізованою добою. Domain state reconstruction перевірено продовженням; фактичний World save round-trip не виконувався.

Source review виявив, що дві однакові за intensity generic Storm events могли змінювати metadata через північ. Connected equal-kind/intensity events тепер об'єднуються **до** day clipping зі збереженням раннього wind key. Nonvacuous fixture seed 286479: original events 891173…897792 і 893382…900539, intensity 837; union 891173…900539, NW wind 951. Десять literal samples і one-tick advance 892799→892800 підтверджують незмінний countdown/period. Parent окремо розрахував mixer/event bounds і п'ять settled temperatures, збережені у `build/weather-carryover-derivation-2026-10-08.json` та `build/weather-folded-key-derivation-2026-10-08.json`.

Candidate Standard seed 70421 за 30 діб: Clear 556 890, Cloudy 249 895, Precipitation 40 432, Storm 16 783 ticks; Rain 41 602 і Snow 15 613 ticks. Temperature -9.4…24.6°C. Це вимір кандидата, не прийнятий баланс.

**FirstWinterStorm seed 70421 не сертифікований:** start 876050, end 882343, duration 6293; storm починається при +4.5°C Rain/8000 bp, перед кінцем -6.8°C Snow/7500 bp. Maximum storm temperature +4.5°C, `all_ticks_snow=false`. Підбір і actual certification зимового demo seed залишаються обов'язковими; фаза Snow не буде примусово підміняти температуру.

## Вимір продуктивності та межі

| Pure CPU вимір | Run01 | Run02 |
|---|---:|---:|
| Four-card cold initialization p95, 100 samples | 13.4202 ms | 14.3095 ms |
| Next forecast-day rollover p95, 100 samples | 4.0174 ms | 3.9492 ms |
| Steady tick, 10 000 ticks | 320.05 ns | 253.98 ns |
| Instrumented allocations / allocated bytes, steady та 100 rollovers | 0 / 0 | 0 / 0 |

One-day forecast goal ≤10 ms пройдено. Four-card total <10 ms — diagnostic `false`, а не підстава приховати timing. Renderer FX preparation ≤3 ms тут не вимірювалась. Allocation scope — global `operator new` instrumentation, не platform heap profiling; steady fixture містить один valid work record.

6000-resident World overhead ≤1%, full GPU graph/FPS/VRAM/stalls, actual save/full/delta/split/reconnect, native UI та reference appearance ще потребують власних перевірок. Native goal ACTIVE; E01–E17/P01 не закриті. Автоматична gate-check certification не заявляється.

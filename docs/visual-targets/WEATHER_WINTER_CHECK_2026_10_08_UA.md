# Сертифікація першої снігової бурі й запасу їжі — 08.10.2026

Seed12 пройшов exhaustive domain scan і фактичний World trace у двох запусках із незалежним actual review CLEAR. Усі6913 ticks першої бурі — Snow, температура−13.3…−1.7°C. Це сертифікація погодної моделі та фізичного ration horizon; main ще не обирає цей seed, а графіка, FX, референси та повний рушій залишаються відкритими.

Використано авторитетне дерево `C:\Users\fipst\Desktop\ribterra`, `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`, weather algorithm1/profile1/scenarioFirstWinterStorm1, save9/protocol23/rules19/generator8. Після подальших bound header змін exact cohort історична; production погоди в цій хвилі не змінено.

## Виконання

- Build `build/weather-winter-2026-10-08-attempt01`:actual exit0,15 translation units/20 zero commands/420 source rows, C++20 `/O2 /MT /W4 /WX`.
- `WeatherWinterProbe.exe`:7 966 208 bytes, SHA256 `A8F4D767F2A9204BC9A470C20D46CB50981B29AAB701CD6ED848D4F2FD21984C`.
- Native `build/weather-winter-native-2026-10-08-attempt01` та `...attempt02`:actual exit0. Кожен має три boundary saves, report і два runner evidence files. Stable JSON fields повторюються точно; лише три save UUID payloads і чотири measured diagnostics можуть відрізнятися.
- Parent read-only verifier actual exit0:420 sources/1020 bound paths/20 zero commands. Independent reviewer rehashed1020 unique files без розбіжностей і також фактично виконав verifier з exit0. Формальної автоматичної certification не оголошено.

```powershell
C:\Python314\python.exe -B tests/weather_winter_evidence_tests.py build/weather-winter-2026-10-08-attempt01 build/weather-winter-native-2026-10-08-attempt01 build/weather-winter-native-2026-10-08-attempt02
```

## Seed і справжня симуляція

Діагностичний ascending search0…4095 відхилив seeds0…11 і прийняв12 на13-й спробі. Це search cap, не нове правило балансу. Storm simulation interval `[14035,20948)`, calendar `[878035,884948)`, duration6913; start входить у9600…14400, duration у4800…7200. Усі ticks мають один canonical Storm period і Snow; Rain0. Domain та actual World storm digests збігаються:1222428438975540385. Exact start−1/start/end−1/end samples перевірено.

Негативний70421 залишається відхиленим: починається при+4.5°C Rain, загалом420Rain і5873Snow ticks. Сніг у кінці не підміняє all-Snow вимогу.

Основний World створено в simulation tick0/calendar864000/day31/06:00 без програвання попередніх діб, snow650 і signed remainders0.10 мешканців мають1200 фізичного Food, Campcapacity10000; Standard лишаєтьсяFood100. Є лише один public stop для десяти actors, без import/time/inventory/actor mutation у main trace.

Виконано24000 actual World ticks, кожен current/forecast/cover порівняно з незалежно просунутим domain runtime. Рівно120 нормальних ration checks через200 ticks реально споживають по10Food:1200→0, без іншого food output/reserved/carried. Усі10 actors залишаються здоровими з hunger0 і вимкненою автоматизацією. Final snowcover734, signed snow remainder24493. Це20 **simulation** minutes at1×;20 wall-clock minutes of play не перевірялися.

На реально досягнутих simulation ticks14034/14035/20947 записано `winter-before.save`, `winter-start.save`, `winter-end.save`. Кожен load зберігає private ClimateRuntime/current/full WeatherView/checksum. Окремі direct/loaded pairs просуваються200 ticks із рівними checksum/current/forecast і final private climate; main trace тим часом стоїть. Перший наступний tick точно входить у бурю або виходить із end-exclusive boundary. Додаткові1200 pair-world ticks відокремлено від main24000.

Total4.526/4.163s і world-trace4.319/3.973s — diagnostic timings, не доказ≤1% overhead або FPS. Нових видалень не було. Ще потрібні shipping startup, renderer/HUD, precipitation/wind/матеріали/snow/footprints, reference/video/input, широкий suite, two-PC/WAN, full simulation/GPU performance та E01–E17/P01 acceptance.

# WaterCoverage V1 / odd-size resize — bounded native follow-up

Дата: 08.10.2026. **BOUNDED BINARY CARRIER / RESIZE CHECKPOINT PASS**.
Не приймання E09, всього двигуна, концепт-арту, normal startup або FPS.
Початковий зріз: [Water / Reflection](WATER_REFLECTION_CHECK_2026_10_08_UA.md);
[чинний контракт](WATER_REFLECTION_V1_CONTRACT_UA.md).

## Реальна зміна

`Renderer2D::read_water_coverage_v1()` читає п'ятий MRT того самого production
renderer, не CPU raster approximation. Власні transfer/fence, очікування GPU,
256-byte row pitch і tight RGBA8 repack; жодного використання звичайного
capture transfer. Дані прив'язані до останнього успішно submitted slot/generation.
До першого кадру API фактично відхилено. Схема, шейдери й кольоровий output
не змінені; fractional authored coverage лишається забороненим.

## Свіжа build02

`build/gameview-water-2026-10-08-attempt02`: actual exit0; 22 TUs,
142 source bindings, 108/108 command exit-sidecars0, 216 log/exit files;
20 shader programs/40 stages/44 sources/81 artifacts; 52 kit files,
50 manifest assets, 5 protected inputs. Незалежний build audit: 0 mismatches.
476 файлів, 115 928 623 B (110,558 MiB); EXE42 629 120 B.

Незалежний read-only native audit підтвердив усі5 input bindings,
18 native rows у кожному ledger, final closure20files, actual argv/exit0,
17/17 repeat-derived hashes і 14/14 старих PNG; direct raw parse дав
точні report counts та0 invalid tuples. Scope — лише bounded checkpoint.

| Доказ | SHA-256 |
| --- | --- |
| Build02 EXE | `72587E5894B4F508E4483BB87EE5FFE15DAB1F2EB75A3B6E04B91F400FB44172` |
| Build02 report | `7A80BD154A8E635FA6178A4302C3A313FE4FF843134791FAE3E4A28961BD3014` |
| Runner | `E2BC7B4D7ED25EABBFC95D20B995708250516C2EE4686F132FFB473B60817A9C` |
| Native05/06 report | `3BA389A002AD444D19E6C92D67FDC6158232101643CF9DAE9B276BCD2B7C8DB6` |
| Raw odd carrier | `B664F4BFA223787B1BD63D18D3BDC91235F46F01188824E14ABA3DC4713CFB34` |
| Native05 execution | `6DF22E0D313919A9380547359F9F90A22F9BD6F600D215DEF90B3D5B86A3A9AF` |
| Native06 execution | `F8521334AA5FCB5A5D78F6FD54F0593A1DACA31F5FF068B79CEDC24779328D25` |

## Actual native05 і native06

Видимі SDL/D3D12 вікна на RTX3050Laptop, не software backend.
Фактичний wrapper exit0 і subprocess exit0 кожного; кожен root20files/
10 208 477 B. Native outputs до wrapper:18files/10 197 042 B;
далі лише write-once execution log і ledger.

15 PNG + raw carrier + original native JSON — **17/17 byte-exact** між
прогонами. Старі 14 PNG із native01 також **14/14 byte-exact** з новими:
немає спостереженої output-регресії від діагностичного API.
Saves і execution timestamps/hash різні; не заявляємо whole-root determinism.

Raw GPU carrier:

| Поле | 1920×1080 до resize | 1279×719 після retry |
| --- | ---: | ---: |
| GPU pitch | 7680 | 5120 |
| Tight bytes | 8 294 400 | 3 678 404 |
| Clear pixels | 1 698 582 | 753 256 |
| Land pixels | 302 496 | 134 286 |
| Water pixels | 70 657 | 31 235 |
| Bridge deck pixels | 1 865 | 824 |
| Water з shore bits | 42 832 | 18 922 |
| Invalid tuples | 0 | 0 |
| Last submitted slot / generation | 1 / 1 | 1 / 2 |

Кожен піксель має лише дозволений clear/Land/Water/BridgeDeck tuple.
Odd raw файл — `water-coverage-odd.rgba8`; розмір і counts зв'язані JSON.
Це доказ **binary** carrier packing/classification, не AA значень1…254.

Послідовність resize:

1. Initial:18 targets, generation1, IDs1/2, 1080p.
2. Actual1279×719 перевірено прямими SDL logical/pixel queries і renderer
   display_info; density/scale1. Ін'єкція36 зірвала останній
   `reflection640×360 frame-set1`. Старі dimensions/IDs/formats/bytes/generation
   збереглися, candidate live0, submittedfalse, попередні raw bytes exact.
3. Retry54: generation2/IDs3/4; full1279×719, Reflection ceil-half640×360.
   Coverage7 356 808 B + Reflection3 686 400 B + попередні attachments =
   **77 254 480 B** nominal target payload.
4. Restore72: generation3/IDs5/6;1080p/half960×540,
   **174 182 400 B** ground targets. Final PNG точно дорівнює baseline.

Readback runtime порівнює sequence/submitted/slot/pass array/cpu_total і
stable targets; source review підтверджує відсутність telemetry mutation.
Це не повна runtime equality усіх telemetry fields.
Перевірено конкретну послідовність alternating slots; recovery консервативно
інвалідовує readback на same-slot. Всі можливі серії failures ще не доказані.

Попередні causal checks також пройшли: Reflection9885/clock54318/shore5098
змінених пікселів, 6 GPU camera cases, 6 rejected inputs, hidden/same-clock/
after-reject exact, final alpha255. Локалізація цих **кольорових змін** і далі
використовує CPU classifier1pxmargin; raw carrier не перетворює її на AA proof.

## Execution provenance та збереження

`tools/run_gameview_water_native.ps1` записує actual argv, UTC begin/end,
subprocess exit, combined output hash, 5 before/after input bindings і всі
18 native outputs. Wrapper self-bound окремо: його немає в build02 snapshot.
Це wrapper-recorded actual subprocess, **не підписаний/незалежний execution
transcript, не GPU timing і не displayed FPS**.

Runner не перезаписує існуючий output: actual preflight exit1 на native05,
20/20 byte/hash/paths незмінні; binary не запускали.
Доказ: [negative ledger](../../.omx/plans/water-runner-existing-output-negative-oct8.json),
SHA `189C045DEE99A430D891369B7ECAB4D44A72F7E5FA758F869A1C97ACA7EA417F`.

Native03/04 раніше мали actual nativePASS, але wrapper помилково записав
`total_bytes=0` через Measure-Object над ordered dictionaries. Ці оригінали
збережені як історія, **не актуальний storage ledger**. Виправлений wrapper
явно сумує integer bytes і звіряє final row total з реальними file lengths,
count і projected bytes. Нові05/06 пройшли; renderer/EXE не перезбирали через
виправлення журналу. Жодних файлів цього follow-up не видалено.

## Відкриті межі

Intermediate AA; довільні repeated failure/recovery; normal startup,
native keyboard/mouse/pause/відео; всі ефекти разом, атмосфера/погода,
концепт-арт, presets, актуальна full suite, GPU time/total peakVRAM,
120 FPS/плавність. Переглянутий GPU PNG — нейтральна сіра контрольна сцена,
не фінальна графіка, не desktop scanout. AllE01–E17 OPEN; гра/генератор/HUD чекають.

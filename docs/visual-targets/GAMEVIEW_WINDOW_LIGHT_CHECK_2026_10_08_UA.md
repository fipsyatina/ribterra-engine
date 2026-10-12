# GameView Window Light — обмежений доказ локального світла з вікон

08.10.2026. **Вузький компонент схвалено; повний двигун, звичайний запуск гри та художній вигляд не прийнято.**

Цей checkpoint перевіряє лише opt-in шлях `GameArt` → `GameView` → native D3D12 для двох локальних джерел світла нейтрального діагностичного будинку. Контрольний kit потрібен для вимірюваного технічного доказу й **не є концепт-артом або production-каталогом**.

Основні roots:

- strict build: `build/gameview-engine-2026-10-08-attempt10`;
- два native прогони: `build/gameview-engine-native-2026-10-08-attempt06` і `attempt07`;
- kit: `build/engine-control-kit-2026-10-08-check05/export-a`.

## Рішення

**APPROVE лише bounded window-light lane.** Підстава: свіжий strict build, два повторні native прогони, точні source/artifact bindings, причинні on/off і blocker fixtures, transactional negative paths та незалежний read-only аудит. Main checkpoint і ширше приймання залишаються окремим наступним рішенням.

## Контракт компонента

- Каталог — schema 2. `lit_sprites.local_emitters` необов'язковий для старих описів; у контрольному kit лише `control.house.sprite` має рівно два emitters, решта семи категорій мають `[]`.
- Числові поля emitter-дескриптора канонізуються у float32 до запису JSON; однакові точні дескриптори записані в `world.json` і `control-kit.json`.
- House anchors мають local render Y=`0.9300000071525574`. Мінімальний зовнішній clearance від owner occluder — float32 `0.0020000000949949026`; anchor всередині, на межі або ближче/рівно до цієї відстані від 3D owner volume відхиляється.
- Колір та інтенсивність повторно беруться з material emission. У emitter metadata немає дубльованого кольору й немає baked ground glow.
- Межі: максимум 4 surface lights, максимум 32 blocker rows для одного світла та максимум 256 глобальних local-occluder rows. Діапазони збираються атомарно: переповнення або некоректний range не публікує частковий кадр.
- Source-off вимикає і material emission scale, і автоматичні локальні джерела, не змінюючи geometry fingerprint.
- Збереження зовнішнього slot0 доведене лише для попередньо наявних external lights із нульовими blocker ranges. Ширший external-light merge контракт не заявляється.

## Strict build і прив'язка джерел

Attempt10 зібрано MSVC C++20 із `/W4 /WX`. Збережено 27 command exit sidecars; кожний містить фактичний `0`.

Незалежний аудит повторно обчислив:

- **96/96** унікальних repository/snapshot source bindings — hash і bytes збігаються;
- **8/8** захищених paths — before/after/current збігаються;
- 22 object files, 4 tool binaries, 4 generated headers, 3 runtime files і 54 command-evidence files — ledger точний;
- **69/69** shader files у accepted origin і staged copy — byte-exact; 34 binary/reflection manifest pairs валідні;
- **52/52** файли kit; 50 manifest assets валідні, export-a/export-b byte-exact;
- kit check root: 18 878 600 байтів, запас до 128 MiB — 115 339 128 байтів.

Shader cohort повторно використано з прийнятого authored-sky attempt10. Це не нова shader compilation або нове загальне shader acceptance.

## Native докази

`GameViewEngineProbe.exe` root-run запущено для native06 і native07; обидва запуски мали фактичний exit0. Окремого native exit sidecar не збережено, тому exit0 є засвідченим результатом root-run, а не висновком із JSON.

Кожний native root містить рівно **13 PNG + 1 report = 14 файлів**. Усі **14/14** файлів byte-exact між native06 і native07, включно зі звітом. Для кожного root:

- усі 13 PNG мають 1920×1080, правильний PNG header і hash зі звіту;
- PNG size range: 18 440–77 858 байтів;
- root size: 693 157 байтів; запас до 128 MiB — 133 524 571 байт;
- backend `direct3d12`, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, `software=false`;
- кожний capture має `submitted=true`, `capture=true`, exact graph order `[6,7,0,1,2,3,4]`;
- `gpu_execution_ms=null`, тому це не FPS/performance evidence.

Raw `gameview-window-radiance-on.png` і `gameview-window-radiance-off.png` переглянуті окремо. On-кадр показує тепле локальне освітлення ground і typed receiver біля нейтрального будинку; off-кадр зберігає geometry, але без цього поширення. Це GPU readback із контрольного native вікна, не desktop scanout, відео чи доказ художньої відповідності.

## Причинні pixel assertions

- Actual GameView ground: `[8,8,6,255]` → `[54,41,22,255]`.
- Typed resident receiver: `[8,8,6,255]` → `[52,40,22,255]`.
- Window source: off `[24,17,4,255]`; self-only і authored-lights-on `[255,255,196,255]`.
- Self-only кадр залишає незалежні ground і typed-receiver pixels точними до off; отже emission surface не підміняє доказ поширення локального світла.
- Disabled source + external light із non-matching receiver mask дає PNG byte-exact до source-off.
- Red fixture: red `160→14`, blue лишається `14`.
- Blue fixture: blue `157→14`, red лишається `14`.

Ці fixtures доводять відповідне blocker-range і receiver-mask розділення у вузькій сцені. Вони не доводять усі можливі перекриття, матеріали, масштаби або world objects.

## Camera і transactional negative scope

CPU composition перевірено у **6 випадках**: 3 zoom (`0.62`, `1.3`, `2.0`) × 2 camera positions. Кожний випадок зберіг 2 authored lights, 0 lights у source-off, 2 точні terrain picks, world-anchor сталість і незмінний geometry fingerprint.

Це **CPU camera/composition matrix**, а не шість окремих GPU zoom captures. Повна camera/zoom/reveal матриця не доведена.

Сім GameView rejection cases перевіряють точну незмінність frame, lighting, camera projection і picking:

1. unsupported cell radius;
2. finite coordinate поза world;
3. NaN coordinate;
4. external-light count понад чотири;
5. external light із nonzero blocker range;
6. collision другого authored stable identity;
7. authored blocker range понад 32.

Шість direct-emitter cases зберігають повний `SurfaceLightingFrame` byte-exact:

1. stable ID zero;
2. stable ID overflow;
3. collision другого emitter;
4. capacity понад чотири;
5. scaled exterior clearance нижче `0.002`;
6. disabled invalid ID/anchor відфільтровано без append.

Окремий existing-output negative був root-run із фактичним exit1 до створення нового evidence. Наявний root лишився **14/14 файлів незмінним**, failure JSON у чужому каталозі не створено. Окремого failure JSON як позитивного доказу немає і не заявляється.

## Hash anchors

| Артефакт | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/gameview-engine-2026-10-08-attempt10/build-report.json` | 199 854 | `0A93F603BC7A54C19D0E46687A7426A530CD6B8C3A8E2867E255E2CC1B60E06D` |
| `tools/gameview_engine_probe.cpp` | 59 681 | `1D102D56ACD12891A005540AFB528273DDCBB4254D30508DE04BCA32095D9711` |
| `GameViewEngineProbe.exe` | 41 962 496 | `9B2408E77E52851A49ED2A6327618A5DE8935C1803916EBC691C2EB6CBEB0A08` |
| обидва `gameview-engine-report.json` | 12 062 | `1DB08269820E59FC6AB5B9275EF63F16624029F25A226903A30188D591769A56` |
| shader `manifest.json` | — | `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1` |
| kit `manifest.json` | — | `98B48D003C1DEFD1C6A68EE3F9A36FB1798E8B5BFA59AE04D4B9F4B495263017` |

## Вплив робочих навичок

- `parallel review` — реалізація, критика контракту й незалежний evidence audit були розділені;
- `minimal C++ ABI-stable changes` — emitter metadata/adaptation додано без розширення GPU ABI або зміни старих raster channels;
- `measured bounded proof` — рішення спирається на exact hashes, bytes, pixels, caps і transactional negatives, а не на загальне враження від кадру.

## Що НЕ доведено і залишається OPEN

- E01–E17 та всі ширші engine acceptance gates;
- full engine, звичайний GameView, launcher або startup гри;
- production catalog, concept-art match, фінальний HUD чи фінальний вигляд;
- FPS, frame pacing, GPU timing або ціль 120 FPS;
- відео, desktop scanout або інтерактивний довгий play session;
- full suite/CTest;
- повна camera/zoom/reveal, weather, water, fog, particles і всі world objects;
- runtime invalid-catalog reload rollback/GPU-upload counter proof. Є exporter negatives і source-level transactional checks, але вони не замінюють native reload evidence.

Нейтральні контролі залишаються діагностикою, **не концепт-артом**. Окреме [повідомлене compiled-only очищення](../GAMEVIEW_BUILD_COMPACTION_2026_10_08_UA.md) завершене після цього bounded approval: старі GameView06–09 compiled-копії прибрані, але GameView10/latest,05/rollback, kit і всі native PNG/звіти збережені. Воно не розширює engine acceptance.

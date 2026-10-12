# GameView: авторитетна погода та мокрі матеріали — 08.10.2026

**Статус:** strict GPU build01, native01–02, parent та незалежний read-only verifier завершилися з actual exit0; незалежний actual evidence review **CLEAR**. Source review CLEAR і фактичний wet-kit test PASS також підтверджено. Цей bounded checkpoint прийнято для його конкретного source snapshot; повні E06/E08 і весь рушій не прийнято.

Авторитетне дерево — `C:\Users\fipst\Desktop\ribterra`, `AInora/first-playable`, unreleased0.26.3, weather algorithm1/save9/protocol23/rules19/generator8. Звичайний launcher і shipping startup, включно з вибором certified seed12, не оновлено. Попередні CPU/GPU cohorts зберігають доказ для своїх exact source snapshots; нову whole-include closure прив'язано до цього fresh build і двох actual repeats. На момент приймання bound код, tools, headers, kit і shaders frozen. Подальші зміни `render_frame.hpp`/`renderer2d.cpp` зроблять цю exact cohort історичною для live current-source перевірки, не скасовуючи результату збереженого snapshot.

## Уже підтверджено

- Leaf1.15 — pure seedless `EngineWeatherProjectionV1`: незалежне SOURCE CLEAR. Валідований WeatherView задає cloud/haze, wind, precipitation та cover; hour походить із staged SessionShown. Ефективна мокрість — `clamp((wet_permille + signed_remainder/28800)/1000, 0, 1)`.
- Leaf1.16 — opt-in GameView `SessionAuthority`, strict prerequisites та staged publication: незалежне SOURCE CLEAR. Ground/bridge і sprite отримують мокрість; water зберігає wetness1. Local lights, emission і river flow збережено. Snow scalar є даними, не доказом snow shader або FX.
- Leaf1.18 — фактичний `engine_control_kit_tests.py` PASS. Збережений root [weather-material-kit](../../build/weather-material-kit-2026-10-08-attempt01) має **125 files / 18 949 382 bytes**. [Export-a manifest](../../build/weather-material-kit-2026-10-08-attempt01/export-a/manifest.json) та [export-b manifest](../../build/weather-material-kit-2026-10-08-attempt01/export-b/manifest.json): кожен **62 files / 9 474 685 bytes**, file-byte exact. Решта — збережений 12-byte sentinel для overwrite-rejection control. Exterior wet masks проходять authored eligibility; emissive windows і resident skin лишаються сухими. Це діагностичний kit, його відповідність референсам не прийнято.
- Leaf1.17 probe, builder, runner і semantic verifier пройшли SOURCE CLEAR. Python3.14 AST parse перевіряє синтаксис verifier; це не запуск його actual evidence checks.

## Frozen source для поточної спроби

| Файл | SHA256 |
|---|---|
| `tools/gameview_weather_probe.cpp` | `DB35970379E670509819BFCF7EB7DD6E511604ED11B1214005FF0E1AD9CC6E47` |
| `tests/gameview_weather_evidence_tests.py` | `3495D78694C637DD378B5F4A6AC21E04850D2AAFE25278E1390281F135B34328` |
| `tools/build_gameview_weather_isolated.ps1` | `58C324BFC808FE7F9C4BE7BD1D0DA2BF83817A9918ADE685FEF6528417EE999D` |
| `tools/run_gameview_weather_native.ps1` | `C7D93BED0B0E2199B51A464451A0D78A516BD85F57B9ECBEA0CB9502069B0B29` |

## Actual виконання та прив'язки

| Перевірка | Поточний стан |
|---|---|
| [Build01 report](../../build/gameview-weather-2026-10-08-attempt01/build-report.json), strict C++20 `/O2 /MT /W4 /WX`, fresh shaders | **Actual exit0**; 164 source bindings / 23 TUs / 121 actual argv commands із zero exits; 537 files / 121 638 124 bytes |
| [Native01 ledger](../../build/gameview-weather-native-2026-10-08-attempt01/native-execution.json) і [native02 ledger](../../build/gameview-weather-native-2026-10-08-attempt02/native-execution.json), той самий EXE | Обидва **actual exit0**; D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver32.0.15.9282 / 1920×1080 |
| Current source/snapshot/tool/runtime/shader/kit/output verifier | Parent **actual exit0**: 164 sources / 921 unique bound files / 121 zero-exit actual argv commands / 16 byte-exact derived files |
| Незалежне read-only actual evidence review | **Actual CLEAR**; 921 unique bound files / 1 613 hash occurrences, 0 mismatches; власний verifier actual exit0 із 164/921/121/16 |

Зібраний `GameViewWaterProbe.exe`: **43 148 800 bytes**, SHA256 `05BDD22E7133A85F51FBE7535EBBE01633216CA76723F367C73BADD62897466B`. Кожен native root містить рівно 20 probe files — 14 PNG, 3 saves, 2 raw readbacks, report — та 2 runner evidence files: **22 files / 27 614 867 bytes** у native01 і **22 files / 27 614 864 bytes** у native02. Всі 16 derived PNG/raw files повторюються byte-exact; весь стабільний report збігається, відрізняється лише `measured.total_ms`. Кожен із трьох UUID-bearing saves окремо прив'язаний до свого run.

Перша actual read-only спроба verifier через system Python3.14 завершилася `ModuleNotFoundError: No module named 'PIL'`; цей tool output збережено. Незмінений verifier повторено через вже встановлений bundled Python3.12.14 / Pillow12.3.0 із workspace dependencies, з exit0. Інсталяцій або source workaround не виконувалося.

## Фактичний результат

Обидва runs пройшли bounded набір: **12 positive controls / 21 transactional rejects / 6 component rejects / 4 clock cases / 14 PNG / 3 saves / 2 raw files**. Нейтральна сцена має house та resident.empty, реальну terrain water і три bridge records; вона не доводить мокрі матеріали всіх восьми категорій у звичайному світі. Contacts тут OFF, graph має 8 проходів; contact/shadow proof з попередніх компонентів не видається за нову перевірку. Host-only fixtures явно авторять ticks7200/7200/7210 та wetness0/600.5 permille без програвання пропущеної cover history.

Authority та exact explicit-equivalent frames/pixels збігаються. Пройшли локальні 1×/2×/4×/pause/load-reset переходи, ready/unknown/reconnecting та реально отримана GNS localhost authority. Actual incomplete-recovery GameView transition у цьому GPU executable лишається OPEN; getter mask уже має окремий CPU-доказ. Disconnect тут означає реальне знищення host/закриття GNS transport; `NetworkServer::shutdown` із повторним save не викликається. Source saves збережено byte-exact протягом кожного run; backup files не створено.

Окремі причинні material controls тримають geometry/clock/lighting незмінними й змінюють diagnostic wetness0→0.8. Ground змінюється у **269 987 із 273 074** qualified pixels; exterior house sprite — у **6 020 із 14 928**. Усі **700** qualified emissive-window pixels лишаються незмінними й nonsaturated. Виявлено **17 981** disjoint inverse-sRGB component intervals. Pause повторюється точно; actual resume змінює **13 901** pixels. Це видима причинна реакція конкретного ground/house матеріалу, не raw HDR measurement або FPS.

Raw carrier і class mask разом із PNG дозволяють verifier незалежно перерахувати class counts, causal wet ground/sprite pixels, dry-window equality, resume delta, opaque UI marker та inverse-sRGB quantization intervals; parent actual verifier це виконав. HDR scalar не зчитується безпосередньо. Source UV ownership і 4×4 authored footprint залишаються bound C++/kit evidence; raw mask сам їх не відновлює. `total_ms` 13 769.8287 / 13 154.0843 — elapsed усього діагностичного набору; це не GPU time або simulation overhead. FPS та GPU execution time лишаються null. Тільки три буквально названі `.save` звільнено від між-run byte equality через random World UUID; кожен save усе одно має власний hash binding.

## Межа приймання

У native report передбачено 13 OPEN scope entries: normal startup, coherent production art, precipitation FX, snow materials/FX, footprints, full suite, physical two-PC, reference visuals, video, FPS, GameView incomplete-recovery transition, contact/shadow validation за попередніми компонентними доказами та fine-grained remote interpolation за попередніми компонентними доказами. WAN і повне simulation/GPU performance також не доведено цим bounded checkpoint.

Звичайний main лишається на legacy path; production schema1 art не має потрібного typed material набору. Наступна інтеграція потребує окремої перевірки UI/menu/Mine та production authored subset разом із world renderer; одного перемикання graph недостатньо.

Повні **E01–E17/P01 і reference acceptance залишаються OPEN**, goal активний. Незалежно прийнятий bounded checkpoint не підміняє приймання всього engine чи звичайного startup. Формальних gate checkbox або автоматичної OMX certification не змінено; нових видалень немає.

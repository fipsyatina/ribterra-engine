# Виведення кадрів і оптимізація — перевірений частковий крок

Дата етапу: 05.10.2026. Статус: **LOCAL FOUNDATION CHECKED / ENGINE NOT ACCEPTED**.

Реалізовані справжні presentation settings, повторне використання GPU upload buffers, кешування незмінної мінікарти та скорочення повторних GPU state binds. Поточні локальні перевірки пройдені. **Це не готовність Dynamic, не відповідність концептам і не приймання мінімуму 120 FPS у повній сцені. Усі E01–E17 залишаються відкритими.** Подальша робота над грою чекає [порога двигуна](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

## Що змінено

- **Справжнє керування виведенням:** VSync / Immediate / Mailbox, uncapped або FPS cap 0–1000, 1–3 кадри у польоті. Renderer перевіряє capability та діапазони, показує requested/effective/reason, застосовує повну конфігурацію після GPU idle. Unsupported не видається за працюючий режим. Software допускає власний FPS limiter, але не симулює GPU-керування.
- **Локальні налаштування:** typed UI action → явне застосування головним циклом → acknowledgment. Конструктор UI сам не змінює renderer. Прийнята конфігурація зберігається; custom cap 77 та невідомі JSON-ключі не губляться. Некоректний presentation block не скидає коректні locale/scale. Явний CLI presentation argument вимикає автоматичне відновлення всієї presentation-конфігурації, а не довільно змішує поля.
- **Apply / Reset:** Apply передає повний запит; Reset відкидає незастосовані правки до активних значень, не є display revert. Невдалий запис налаштувань не скасовує вже застосований режим і не залишає UI в pending: показується локалізоване «застосовано, але не збережено», доступна повторна спроба. Успішний перевірений запис очищає попередження. Atomic/fsync durability не заявляється.
- **Pacing:** абсолютні часові deadlines, fractional carry, перевірки late wakeup/clock rollback/overflow; прибрано обов'язкову затримку 1 мс у звичайному uncapped-циклі. Minimize та remote polling мають окремі правила. FPS cap не є гарантією нижньої межі FPS або відсутності OS scheduling jitter.
- **GPU lifetime:** upload transfer buffers належать frame slot і повторно використовуються лише після його fence. Ріст комплекту vertex/index/surface/upload buffers та offscreen target транзакційний. Fault tests перевіряють збереження попереднього кадру й успішну наступну спробу. При minimize нормальна GPU-робота пропускається; явний offscreen capture залишається доступним.
- **Менше повторної роботи:** лише сусідні сумісні geometry draws об'єднуються, без сортування чи зміни painter order. Pipeline/sampler/scissor bind кешуються в межах одного render pass. Мінікарта перебудовується за потрібною зміною terrain/chunk/reveal/trail/season/session/viewport; живі markers і camera border малюються окремо. Compose scratch vectors та частина контейнерів повторно використовуються; повна відсутність allocations не заявляється.
- **Чесна telemetry:** CPU update/compose/prepare/encoding/submit/wait/pacing відокремлені. CPU submit не називається GPU execution. Вбудовані `gpu_execution_ms` і `displayed_fps` залишаються `null`; зовнішні ETW-вимірювання збережені окремо.

Профілі Low/Medium/High/Custom, screen mode/resolution/render scale та якість майбутніх світла/тіней/води/туману/частинок **ще не реалізовані повністю**. Наявні три presentation controls не закривають E16.

## Фактичні умови вимірювання

- NVIDIA GeForce RTX 3050 Laptop GPU; SDL driver `32.0.15.9282` (NVIDIA 592.82), D3D12; Ryzen 5 5600H, 6 cores / 12 threads.
- Видиме **вікно**, client drawable **1920×1080**, monitor **144 Hz**, pixel density / display scale / render scale **1.0**. Це не exclusive fullscreen і не hidden-window throughput.
- Живлення від мережі, Balanced power plan; план живлення не змінювався. 4096 MiB — місткість VRAM, **не виміряне пікове використання** під час гри.
- Справжні GameView + RmlUi, звичайний legacy world seed **70421**, заморожений стан, **0 simulation ticks** під час sampling. `Dynamic_available=false`. Не повна контрольна сцена, немає pan/zoom/нових chunks/усіх майбутніх ефектів.
- Кожен case: **750 мс warmup**, потім sampling. PNG readback, запис файлів і minimize/restore checks виконуються поза performance interval. CPU quantiles — nearest-rank; записані p50/p95/p99/max та кількість інтервалів понад **1000/120 мс**.

## Прогін без профілювальника: CPU-loop, 8 секунд на case

Джерело: [повний native report](../../artifacts/presentation-2026-10-05/native-unprofiled-finalized/presentation-report.json). Той самий фінальний executable, без паралельного PresentMon/build/test. Probe завершився успішно, включно з minimize → offscreen capture → restore.

| Режим | CPU-loop FPS | p50, мс | p95, мс | p99, мс | max, мс | Понад бюджет / інтервалів |
|---|---:|---:|---:|---:|---:|---:|
| VSync, HUD | 138.62 | 6.973 | 8.947 | 10.271 | 12.379 | 97 / 1107 |
| Immediate, uncapped, HUD | **145.16** | **6.665** | **8.245** | **9.999** | **12.694** | **50 / 1161** |
| Immediate, HUD приховано | 159.07 | 6.092 | 7.489 | 9.447 | 10.930 | 31 / 1271 |
| Immediate, cap 120, HUD | 119.52 | 8.333 | 8.862 | 11.113 | 12.205 | 474 / 955 |
| Mailbox, cap 120, HUD | 117.88 | 8.336 | 10.158 | 11.896 | 13.042 | 491 / 942 |

**145.16 — середня швидкість CPU-циклу статичної legacy-сцени, не підтверджений displayed FPS повного двигуна.** p99/max перевищують бюджет. HUD-hidden рядок — лише діагностика, не заміна baseline. Біля cap 120 лічильник понад точний бюджет чутливий і до дуже малого jitter; не кожен такий інтервал є великим hitch.

В Immediate/HUD основні CPU mean: world composition **4.704 мс**, UI update **0.938 мс**, UI compose **0.055 мс**, renderer total **1.184 мс**, зокрема encoding **0.379 мс**. Renderer total включає свої підфази — їх не треба додавати вдруге. За цим коротким прогоном найбільший CPU-компонент — scene composition. 3717 draws, 57960 vertices, 87468 indices, 1509072 upload bytes у середньому; геометрія не урізана.

Раніші 5-секундні baseline/8-секундні проміжні runs збережені окремо. Через різну тривалість, прогрівання й стан живлення не заявляється контрольований точний відсоток приросту від кожної оптимізації.

## Одноразовий дозволений PresentMon: окремий 10-секундний ETW-прогін

Використано **один** схвалений запуск portable PresentMon 2.6.0 з UAC, без інсталяції драйвера/служби чи постійної зміни permissions/groups. Monitor PID 14124, target probe PID 3376; обидва завершилися з exit 0. Повторний elevated запуск не виконано; цей дозвіл не є постійним.

Докази: [raw CSV](../../artifacts/presentation-2026-10-05/presentmon-finalized/presentmon.csv), [QPC-matched analysis](../../artifacts/presentation-2026-10-05/presentmon-finalized/analysis.json), [profiled probe](../../artifacts/presentation-2026-10-05/native-profiled-finalized/presentation-report.json), [tool provenance](../../artifacts/presentation-2026-10-05/presentmon-tool/provenance.json). Аналізатор: [summarize_presentmon.ps1](../../tools/summarize_presentmon.ps1).

Відбір ETW за `CPUStartQPC` у `[sample_begin_qpc, sample_end_qpc)`; frequency 10000000. Один target process; кожен case має один swapchain. Warmup/capture/contracts не входять у вибрані CPU intervals; на межах CPU/present intervals можлива похибка. VSync має 1219 вибраних CSV rows проти 1220 queued presentations, а не приховане прирівнювання counts.

| Режим | ETW rows | GPU elapsed mean / p95, мс | GPU busy mean, мс | Частота валідних display intervals | Display p95, мс | Пропущені interval samples | Tearing allowed rows |
|---|---:|---:|---:|---:|---:|---:|---:|
| VSync, HUD | 1219 | 4.032 / 4.934 | 3.868 | 122.07 Hz | 13.907 | 0 | 0 |
| Immediate, uncapped, HUD | 1190 | **3.621 / 4.130** | **3.556** | **118.72 Hz** | **15.000** | **3** | **1190** |
| Immediate, HUD приховано | 1371 | 3.464 / 3.907 | 3.375 | 135.64 Hz | 13.985 | 14 | 1371 |
| Immediate, cap 120, HUD | 1162 | 3.743 / 4.107 | 3.681 | 115.90 Hz | 15.212 | 4 | 1162 |
| Mailbox, cap 120, HUD | 1175 | 3.507 / 3.984 | 3.421 | 117.50 Hz | 13.916 | 0 | 0 |

Метод display column — **1000 / mean валідних `MsBetweenDisplayChange`**, не гарантована частота повних кадрів scanout, особливо в режимі, що дозволяє tearing. `AllowsTearing=1` не є доказом видимого tearing чи його кількості. `NA` пропускаються, а не замінюються нулем. Пропущений display interval **не називається dropped frame**; без відповідного доказу не заявляється нуль ETW losses. В усіх вибраних cases `MsUntilDisplayed` додатний для кожного row, але це не усуває вказаних обмежень interval/scanout.

GPU elapsed (`MsGPUTime`) включає GPU busy + wait; GPU busy показано окремо. Це зовнішній ETW-вимір, не перейменований CPU submit. В цьому прогоні Immediate/HUD CPU-loop FPS **119.00**, p95 **11.656 мс**; він не змішується з 145.16 у прогоні без trace. Різниця сумісна з впливом profiling/поточних умов, але точний внесок overhead не встановлено. Повні GPU/display p50/p95/p99/max/coverage є в analysis JSON.

Первинні джерела методики: [PresentMon 2.6.0 console documentation](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md), [CSV output implementation](https://raw.githubusercontent.com/GameTechDev/PresentMon/v2.6.0/PresentMon/CsvOutput.cpp). Власні числові результати — з локального CSV, не з документації.

## Поточні локальні перевірки

| Перевірка | Фактичний результат / межа доказу |
|---|---|
| Release build | Поточні Ribterra/probes/tests зібрані; MSVC 14.51, Release `/O2 /Ob2 /DNDEBUG`. Не release publication. |
| Targeted CTest | **10/10**, 53.37 с: ui, ui_lifecycle, presentation_ui, render_frame, frame_timing, render_batch, minimap_cache, presentation_contract, client_input, surface_lighting_contract. **Не всі 98 тестів**. [LastTest.log](../../artifacts/presentation-2026-10-05/check-finalized/runtime/Testing/Temporary/LastTest.log). |
| Native GPU resource/text recovery | `RenderFrameTests --gpu`, exit 0; старий small red frame декодовано після injected buffer-growth failure до retry; glyph/upload recovery перевірено. |
| Native draw barriers | `RenderBatchTests --gpu`, exit 0; lit/legacy/order barriers збережені. [captures](../../artifacts/presentation-2026-10-05/native-batch-finalized). |
| Surface kernel regression | **18 captures**, exit 0, [report](../../artifacts/presentation-2026-10-05/native-surface-finalized/surface-lighting-report.json). Лише bounded opaque surface kernel; не full Dynamic/HDR. |
| Native settings UI | **3/3 applied-and-captured cases**, 4 PNG; Ukrainian 720p / scale 130%, 1080p / scale 100%, фактичний D3D12. 720p після двох SDL wheel events і 14 animation-settling frames має доступні Apply/Reset/Back. [report](../../artifacts/presentation-2026-10-05/native-settings-finalized/presentation-settings-report.json). |
| Legacy visual regression | **6/6 configurations, 15 PNG**, всі 15 SHA256 збігаються з попередньою outcrop-HUD matrix; F1 hide/restore, winter, 720p/1080p/1440p, 5 locales. [report](../../artifacts/presentation-2026-10-05/legacy-finalized/report.json). Це збереження наявного вигляду, **не подібність концептам**. |
| Benchmark capture parity | **5/5 PNG** однакові в profiled та unprofiled runs; sampling не змінював fixture/art. |
| Actual CLI process | Software cap 120 / frames 2 та D3D12 Immediate uncapped / frames 1: exit 0; [renderer info](../../artifacts/presentation-2026-10-05/cli-finalized/gpu.json). Три некоректні запити cap `120oops`, frames `0`, mode `made-up` відхилені з exit 1. ExitCode отримано через awaited Process, не stale `$LASTEXITCODE` GUI-launch. |
| Source review | Окремий code-reviewer: фінальний UI/persistence review **APPROVE**; не візуальне приймання концептів. `git diff --check` пройдено, наявний dirty tree збережено. |
| ETW analysis review | Незалежно перераховано source hashes, selected rows / NA counts, means, nearest-rank quantiles та display interval rates — **APPROVE**. Огляд claims відокремив дозволений tearing від фактично спостереженого; формулювання виправлено. |

Приклади фактично переглянутих native зображень: [720p settings bottom](../../artifacts/presentation-2026-10-05/native-settings-finalized/uk-1280x720-scale130-immediate-cap120-frames1-settings-bottom.png), [1080p settings](../../artifacts/presentation-2026-10-05/native-settings-finalized/uk-1920x1080-scale100-vsync-uncapped-frames2-settings-top.png), [звичайний legacy світ](../../artifacts/presentation-2026-10-05/native-unprofiled-finalized/immediate-hud.png). Немає заяви, що це концепт-faithful нова сцена. Native computer-control API у цій сесії недоступний; PNG з реального SDL/D3D12 renderer та SDL input probes не видаються за ручну desktop automation.

Невдалі проміжні fixture/scroll/tool-wrapper runs збережено: SDL driver-hint conflict, minimap fixture revisions, wheel loop без часу RmlUi animation та stale GUI `$LASTEXITCODE` виправлені й перевірені повторно. Вони не приховані видаленням artifacts і не використовуються як green proof.

## Прив'язка до перевірених файлів

Фінальний [snapshot](../../artifacts/presentation-2026-10-05/check-finalized/snapshot.json): **294 hash-verified runtime files**. Це output-isolated runtime, **не hermetic source snapshot**: Python tests/repository inputs залишаються live.

| Файл / набір | SHA256 / content hash |
|---|---|
| Ribterra.exe | `CE44B626EA1213861443648F9F2E9245EEA35635CB1621BBEC1AB77B69FD2D9B` |
| PresentationProbe.exe | `4E6234E8365AAF9708FC5A65B00796FD3DCF95D70EF4E6BECBF1A90EA343A835` |
| compiled-shaders/manifest.json | `3A4F827AC1789F3254B8002306C74D36882C71C572DD60A19EE1909A4220F81A` |
| Asset content | `fde76d5bb99180a47b59c046e60a7cff023a48a8ee689da2f3659a59d7976abd` |
| PresentMon executable | `B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF` |
| Raw PresentMon CSV | `7A9E2219398F3AD95F2338E343A9E9FD76CD93916D70149E8FD7F364DEF8BE14` |

Launcher **не оновлено**: `Play Ribterra Rework.cmd` досі вказує на `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2/Ribterra/Ribterra.exe`. Цей етап — source/local validation, не встановлена нова версія, не Git commit/push/release. Файли не видалялися; prior asset-staging та failed runs збережені.

Застосовані ponytail/ultrawork допомогли розділити реалізацію й незалежний review; karpathy-guidelines/cpp-coding-standards — втримати локальний scope, typed state, transactional lifetime та явні failure contracts. Вони не дозволяють зарахувати неповні ефекти або середнє FPS як готовий двигун.

## Що далі і коли можна приймати

Наступна робота залишається **над двигуном**: виробничий HDR/render graph, surface/material adapters для всіх receiver-категорій, emission, причинні тіні, sun/moon/time/weather, вода й просторова атмосфера. Після підключення — спільна concept matrix, довгий pan/zoom/cold-load run, GPU/display pacing і піковий VRAM, повний suite та незалежний visual review.

**Ціль ≥120 FPS у 1920×1080 на RTX 3050 із усіма потрібними ефектами ще не доведена.** Ані 145.16 CPU-loop FPS у статичному legacy world, ані окреме 3.62 мс GPU measurement її не закривають.

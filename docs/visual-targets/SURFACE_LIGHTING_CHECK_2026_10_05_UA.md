# Перше попіксельне світлове ядро — локальна перевірка 05.10.2026

Статус: **обмежене surface-lighting ядро перевірено; двигун НЕ завершений**. Загальний [поріг E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md) залишається відкритим, включно з доданими graphics settings/плавністю/оптимізацією. Ціль користувача — **мінімум 120 FPS у 1920×1080 на RTX 3050**; цей прогін її не вимірює і не підтверджує.

## Реалізовано саме зараз

- Окремий 24-byte vertex stream із camera-relative XYZ і normal, без зміни старого 20-byte Vertex для спрайтів/UI.
- У Renderer2D/D3D12 є opt-in opaque world draw із попіксельним diffuse: ambient, напрямлене сонце та до чотирьох point/spot джерел. Відстань/радіус, напрям, cone, falloff і reveal беруть участь у розрахунку.
- Фіксований CPU/HLSL ABI: LightGpu 80 bytes, SurfaceLightingFrame 384 bytes; перевіряються скомпільовані bindings, offsets, interfaces та hashes.
- sRGB albedo декодується для лінійного світлового розрахунку; результат явно кодується назад у поточний opaque RGBA8 UNORM. **Це ще не повний HDR render graph.**
- Перевіряються finite/bounded позиції/радіанси, одиничні нормалі, cone/radius/count, index-aligned stream, opaque світові проходи. Непідтримувані occluder ranges/masks відхиляються, а не ігноруються. Software явно відхиляє surface-lit draw.
- Порожній draw не зв'язує відсутній surface buffer. Frame clear скидає нові дані. Surface buffer зростає транзакційно; очікування texture-upload fence більше не ігнорує помилку. Подвійну повну валідацію кадру усунуто.
- Package required-file guard тепер включає обидві shader-пари та всі чотири reflection files. Це статично перевірений guard; упаковку/інсталяцію/реліз не запускали.

`surface_lighting_available=true` означає лише це ядро. `LightingMode::Dynamic` усе ще чесно повертає **NotImplemented**, `dynamic_available=false`.

## Актуальні докази

Machine: Windows, **NVIDIA GeForce RTX 3050 Laptop GPU**, D3D12, driver **32.0.15.9282**, 12 logical CPU cores, 15713 MiB system RAM. Calibration — 640×480; це не performance scene 1080p.

| Перевірка | Результат | Межа доказу |
|---|---|---|
| Targeted build | Ribterra, ReworkVisualProbe, SurfaceLightingProbe, RenderFrameTests — exit 0 | Наявні попередні warnings у Presentation/GameUI не виправляли в цьому кроці |
| [Свіжий isolated CTest](../../artifacts/surface-lighting-2026-10-05/check-final/focused.xml) | **4/4**: shader_contract, shader_negative, render_frame, surface_lighting_contract | Не повний suite |
| Shader negative | **8** register/space/output/ABI mutations відхилено | Не injection усіх GPU allocation/fence failures |
| Native surface proof | exit 0; **18 PNG**, [raw report](../../artifacts/surface-lighting-2026-10-05/native-final/surface-lighting-report.json) | Production Renderer2D kernel, але diagnostic geometry, не GameView integration |
| RenderFrameTests `--gpu` | exit 0, D3D12 RTX 3050 Laptop | Legacy geometry/text/alpha/clip/mipmap/resize/input/texture lifetime та наявний text-upload rollback |
| [Legacy GameView regression](../../artifacts/surface-lighting-2026-10-05/legacy-game-check/report.json) | **6/6 конфігурацій, 15 PNG, F1 hide/restore** | Нове світло ще не підключене до ігрової сцени |
| Порівняння legacy PNG | **15/15 SHA256 ідентичні** `artifacts/outcrop-hud-2026-10-05/matrix-outcrops` | Доказ незмінності цих fixed fixtures, не художнього приймання |
| Package guard | PowerShell AST parse + усі **9** shader/manifest requirements присутні у runtime snapshot | Сам package script не виконувався, новий release не створено |

Runtime snapshots мають по 288 перевірених файлів. [Final snapshot manifest](../../artifacts/surface-lighting-2026-10-05/check-final/snapshot.json) фіксує hashes; Python/repository inputs залишалися live, не є hermetic snapshot.

## Що видно в кадрах і що перевірено числами

- [Локальне попіксельне світло](../../artifacts/surface-lighting-2026-10-05/native-final/per-fragment-local-light.png): усі чотири вершини поза радіусом джерела, центр яскравий **255/255/255**, край **25/25/25**. Суцільний draw tint або лише освітлення вершин цього результату не дає.
- [Джерело ліворуч](../../artifacts/surface-lighting-2026-10-05/native-final/same-light-source-left.png) / [праворуч](../../artifacts/surface-lighting-2026-10-05/native-final/same-light-source-right.png): той самий point light переміщений між протилежними нормалями; яскравий **255/246/201** і ambient **39/39/39** міняються місцями.
- [Протилежні нормалі під сонцем](../../artifacts/surface-lighting-2026-10-05/native-final/sun-opposing-normals.png): **255/236/194** проти **56/56/56**.
- Gamma probe: albedo **128** при linear ambient **0.25** дає **66**, а не помилкове encoded multiplication **32**.
- Point radius: inside **209**, outside **39**; spot inside **247/211/124**, outside **39/39/39**.
- Еквівалентні camera pan/zoom: та сама sample **163/219/255**. Зміщення receiver від джерела змінює освітлення. Lit → legacy → lit дає ту саму перевірену sample.
- UI on/off: точні незмінні **31/157/219/255**. Це парна pixel assertion; візуальний огляд одного кадру сам по собі її не доводить.
- [Виправлена геометрична калібрація](../../artifacts/surface-lighting-2026-10-05/native-final/geometry-calibration-not-game-art.png): земля, дві видимі стіни і похилий дах сходяться у світових координатах. Незалежний огляд шести native кадрів не знайшов аномалій у межах kernel proof. **Це навмисно прості контрольні площини, не ігровий арт і не наближення до концепту.**

Першу `native-first` серію з невдало орієнтованими площинами даху збережено; для геометричної візуальної перевірки чинною є лише `native-final`. Перша збірка також зупинилася на parser names у DXIL reflection; parser виправлено під фактичні shadercross prefixes зі збереженням жорстких ABI offsets і повторним negative proof.

## Чого тут немає

Немає full Dynamic, light-responsive матеріалів ігрових будівель/дерев/мешканців, emission masks, реального світла вікон у світі, тіней/occlusion, moon/time/weather lighting, HDR/MRT graph, мокрих матеріалів, водних reflections, ранкового/водного туману чи нового particle pipeline. Повний shader/material asset kit ще не реалізований. Немає приймання графіки V01–V03, нового full-suite результату, temporal video або FPS/VRAM proof.

Поля `elapsed_ms` у probe містять capture/readback та startup; **не ділити кількість PNG на цей час для оголошення ігрового FPS**.

Дозвіл керувати ноутбуком є, але поточний computer-use інструмент повторно завершився `failed to write kernel assets ... os error 3`. Desktop control не відбувся. Реальний візуальний доказ у цьому кроці — GPU captures із запущених native executable та їх перегляд, не screenshot чужого вікна й не згенерований макет.

## Ідентичність перевіреного результату

| Артефакт | SHA256 |
|---|---|
| Ribterra.exe | `C36C5315D96FDEA3644E5C774C6F7A8E35D97FA4B69BA6008183706683938B7B` |
| SurfaceLightingProbe.exe | `C0DD21467C07C86A0A3B56CCF416D1C549A0CB75F8FCD8A67C30DF398BFC55D8` |
| ReworkVisualProbe.exe | `694AA6826893BD1F1725222821471ABF27D6AB509988EEA5614281D2D5CC44F0` |
| RenderFrameTests.exe | `A725A96F3132BBD99AD3549523E566FE7ED8370BFE6EB87FAE832495BCD0131C` |
| surface_lit.vert.dxil | `2D6D7B6C9BB26BFDF35525E5F83597FEDD4A081FA19756437568F0823A9C70BA` |
| surface_lit.frag.dxil | `7135D27CA7B896BC89753A19773573776285B34D96146DCE1BDB18C67ABC4A56` |
| shader manifest | `3A4F827AC1789F3254B8002306C74D36882C71C572DD60A19EE1909A4220F81A` |

Staged content: **189 assets**, `f9c6abf944c0867dfd6f6329c6c96f2b8f8b2e03b544c21b1a845c0cedbe97ae`. Legacy regression використовував `check/runtime`, де Ribterra/Rework binaries ідентичні `check-final/runtime`; змінювалася тільки геометрична калібрація SurfaceLightingProbe.

Launcher і встановлений r2 не змінені. Його exe лишається `DCF9514876AD73ED0B4EDA8E6DFBE1C452835A4E478B6AF007DB657E58706B35`. Старі ассети при staging збережені в `.ribterra-assets-retained-*`; повідомлене перед запуском очищення shader-negative видалило лише його нові тимчасові copies у `check/temp/ribterra-shader-contract-*`.

## Наступна робота — лише двигун

Завершувати E01/E02: спільний metric/projection adapter, capability/lifetime contracts, production HDR/MRT graph і material channels. Після цього підключати receiver surfaces/мінімальні ассети, причинне затінення/час/місяць і World FX, з окремим доказом кожного кроку. Успішний поточний probe **не відкриває** повернення до гри.

Використані підходи: ponytail спрямовував повторне використання чинного SDL_GPU; Karpathy — перевірюваний вузький контракт; C++ standards — явний ABI/типи/ресурсні межі; ultrawork — незалежні shader/probe/ABI/pixel/consumer перевірки. Жоден із них не замінює engine-first вимогу або native acceptance.

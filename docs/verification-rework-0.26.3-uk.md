# Переробка Ribterra V01–V03 — перевірка 30.09.2026

## Рішення

**PARTIAL — суттєва локальна переробка реалізована, але повна художня відповідність референсам не прийнята.** Це не опублікований реліз і не твердження, що всі V01–V03 готові. Построковий результат: [acceptance checklist](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md).

Виконаний спільний графічний обсяг: **V01 HUD → V02 земля/річка/стежки → V03 будівлі/мешканці**. Старі `target-0261/0262/0263` містять також іншу gameplay-послідовність: погоду, житло, режими ферми. Вона не видана за реалізовану; питання про об'єднання цих двох обсягів залишається відкритим. Прогноз, температура, дощ, snow-cover simulation, мінімепа з public targets і Dynamic Lighting не додані цією переробкою.

## Що реально змінено

### V01

- Компактні ресурсні лічильники, navy/ivory/brass палітра, нижній inspector, чотири основні дії з іконками. Дані та команди лишаються з реальної симуляції.
- Прибрано постійне внутрішнє повідомлення початкової генерації wilderness; звичайний feedback зникає через п'ять секунд.
- F1 приховує і повертає не тільки RmlUi-панелі, а й selection markers, progress annotations та social bubbles над світом. Утримання клавіші не перемикає HUD повторно.
- Escape закриває відкриту панель до передавання події у GameView. Чат має пріоритет. Видимість перераховується одразу, не лише на наступному кадрі.
- Виправлені обрізане «Продовжити» на паузі й довга польська назва Surface.

### V02

- Спокійніший meadow material, широкі матеріальні зони, менш контрастний дрібний шум, природніше розміщення декору й берегові деталі.
- Виправлена спільна межа mirrored UV, зокрема для від'ємних координат. Береговий колір визначається спільними вершинами, а не типом окремої клітинки.
- Для world textures увімкнені максимум чотири mip-рівні; це зменшує мерехтіння й зерно при віддаленні. UI і текст лишаються на базовому рівні.
- Збережено authoritative протоптування від реальних переміщень, save/load і legacy Road без бонусу швидкості. Нової ручної дороги немає.

### V03

- Масштаб споруд — 1.35 від каталожного (крім мосту й legacy Road); більші мешканці та contact shadows. Simulation footprint не збільшено.
- House II і Workshop II отримали окремі зимові sprites: зима більше не підміняє їх силуетом I рівня.
- Нові джерела, точні prompts, pivots, hashes та notices записані в `art-source/rework-v03-winter/`, `assets/art/world.json` і паспорті.
- Renderer, picking і thumbnail використовують той самий resolver. Візуальні вантажі лишаються прив'язаними до фактичного item/quantity.

## Відтворювані докази

Усі шляхи нижче — від кореня репозиторію. Остаточна директорія доказів: `artifacts/rework-v01-v03-2026-09-30/`.

| ID | Доказ | Межа твердження |
|---|---|---|
| T | `ctest-certified.xml` | PASS: 91/91, 0 failures, 674.79 s, остаточні executables/assets; local/software/network checks не підміняють окремі native докази |
| M | `matrix-certified/report.json` | PASS запуску: 60 комбінацій, 123 PNG; uk/en/ru/pl/es × scale 0.85/1.00/1.15/1.30 × 1280×720/1920×1080/2560×1440; це captures, не повна longest-text/input acceptance |
| A | `art-certified/art-report.json` | Native D3D12, 20 catalog families × 5 states, 2 level-II families, 256 resident sprites, 1089 terrain picks, 3 zoom picks, uphill movement |
| R | `residents-certified/report.json` | Native D3D12: 256 кадрів; фактичні delivery до camp на tick 212 і stockpile на tick 252, обидва з save/resume |
| P | `build-evidence.json`, `package-certified-*.png` | PASS локального запуску: точні hashes, свіжий пакет, stocks→Escape без unpause, F1 hide/restore зі сталими 06:42/80 wood/40 stone/60 food/10 people |

GPU: **NVIDIA GeForce RTX 3050 Laptop GPU**, backend `direct3d12`, без software fallback. Окремо `RenderFrameTests.exe --gpu` пройшов після остаточних змін: minification, blending, clipping, painter order, Unicode, resize/input mapping. Програмний renderer перевіряється незалежно і не називається GPU-доказом.

У M ordinary-start — реальний новий світ; staged-v03 — явно підготовлена контрольна сцена з будівлями. Обидві проходять через `World` save/load, `ClientSession`, `GameView`, RmlUi й D3D12. Staged-сцена не видається за природно виросле поселення. Seed 70421, camera (24,24), zoom 1.30; tick/state/resolution збережені в manifest кожного кадру.

Ручне керування вікном гри виявило обрізану кнопку паузи та Escape, який раніше доходив до pause. Перевірені запуск нового поселення, ресурсна панель, закриття Escape, F1 hide/restore та груповий наказ 10 мешканцям. F12-кадри не є записом відео; тестова гра не перезаписувала старі користувацькі saves.

Попередні `pass-01`, `pass-02`, `final`, `matrix-final`, `matrix-final-v2`, `art-final`, `residents-final`, `ctest-final.xml`, `ctest-postfix.xml` збережені як історія ітерацій. Вони **не замінюють** остаточні `*-certified` докази. `ctest-postfix.xml` містить дві знайдені й виправлені помилки, а не green run.

## Що ще не прийнято

1. **Художній результат:** світ усе ще значно менш насичений деталями й відрізняється від композиції та стилізації надісланих концептів. Взимку помітні великі межі матеріальних зон. Перевірка UV доводить безперервність координат, але не художню якість усієї поверхні.
2. **Повна UI-матриця:** кадри не доводять автоматично найдовші warning/details, всі hit rectangles, ≥36 dp, OS DPI 100/125/150%, union HUD ≤30% або відсутність витоку текстур після 50 циклів inspector.
3. **Рух:** немає обов'язкових немонтованих відео всіх facing/cargo, переходу мостом і накопичення стежки. Послідовності доставки підтверджені окремими native кадрами та simulation assertions.
4. **Пам'ять:** розрахунковий RGBA8 payload 45 world pages становить 310.01 MiB на базовому рівні та 411.68 MiB з чотирма рівнями. Попередні 292.01 MiB не збережені. Це арифметика texels, **не виміряне виділення VRAM**; немає контрольного p95/p99/peak memory порівняння. Потрібні repack/lazy loading і профілювання, а не приховане зменшення якості.
5. **Мережа:** локальні тести на одному ПК не є двома фізичними Windows-ПК або cross-network acceptance.
6. **Production art:** додані PNG — підготовлені растрові sprites, не 3D masters і не набір normal/depth/material maps для майбутнього Dynamic Lighting.

Через ці відкриті пункти ані V01, ані V02, ані V03 тут не отримують фінальну позначку PASS.

## Запуск

Із кореня репозиторію відкрити **`Play Ribterra Rework.cmd`**. Він запускає тільки `dist/rework-0.26.3-2026-09-30/Ribterra/Ribterra.exe`, нічого не завантажує і не підміняє результат старим публічним пакетом. F1 — приховати/повернути HUD; F12 — зберегти кадр.

Старий `Play Ribterra.cmd` залишений без зміни release-маршруту. Commit, push, GitHub release і зовнішня публікація не виконувалися.

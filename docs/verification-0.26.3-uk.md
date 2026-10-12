# Перевірка Ribterra 0.26.3 V03

> Історичний звіт конкретної збірки нижче, не приймання візуальної відповідності. Після зауважень користувача 30.09 розпочато [кумулятивну переробку V01–V03](visual-targets/V01_V03_REWORK_ACCEPTANCE_UA.md); її статус має пріоритет для поточного результату. Хеш EXE і твердження про відсутність winter-II plates нижче стосуються тільки збірки 29.09.

Перевірено 29 вересня 2026 у `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`. Це локальний доказ реалізації V03, а не твердження про опублікований реліз або зовнішнє приймання.

## Результат

- Повна Release-збірка завершилась: 137 build-кроків після інкрементальної конфігурації, asset staging — 155 валідованих файлів.
- Повний послідовний CTest: **91/91 passed**, 0 failed, `1066.83 s`.
- Попередній паралельний прогін дав 90/91: `coop_start` перевищив timeout під навантаженням. Той самий тест одразу пройшов окремо за `9.01 s`, а потім пройшов у повному послідовному suite за `8.92 s`. Це зафіксовано як load-sensitive test behavior, не приховано.
- Focused V03: **11/11** — `building_visual`, `unit_motion`, `art_alpha`, `art_client_rendered`, `texture_atlas`, `atlas_tool`, `asset_staging`, `upgrades`, `upgrade_flow`, `v03_asset_passport`, `render_frame`.
- Додаткові UI/input regression checks: **4/4** — `ui`, `ui_lifecycle`, `client_input`, `building_inspector`.
- `git diff --check` — без помилок. Impeccable detector для V03 presentation-файлів — `[]`.

## D3D12 та нативні докази

- `RenderFrameTests.exe --gpu`: `direct3d12`, `NVIDIA GeForce RTX 3050 Laptop GPU` — renderer contract passed.
- Native ArtVisualProbe: 20 building families × 5 базових станів, 2 дозволені level-II сім'ї, 256 directional resident sprites, summer/winter, 1089 terrain-pick checks, 3 object zoom/pick checks, uphill movement.
- Native ResidentVisualProbe: 256 sprite frames; реальний вантаж доставлений у Табір на tick 212 і в Склад на tick 252; cargo зникає після доставки; обидва сценарії пережили save/resume.

Основні файли доказу:

- [Summer V03 world + HUD](../artifacts/graphics-v03-0.26.3/native/summer.png)
- [Object calibration: stages, ready, II, winter, 8 facing/load samples](../artifacts/graphics-v03-0.26.3/native/v03-object-calibration.png)
- [Zoom 0.62](../artifacts/graphics-v03-0.26.3/native/v03-zoom-062.png), [zoom 1.30](../artifacts/graphics-v03-0.26.3/native/v03-zoom-130.png), [zoom 3.00](../artifacts/graphics-v03-0.26.3/native/v03-zoom-300.png)
- [Winter state](../artifacts/graphics-v03-0.26.3/native/winter.png)
- [Real cargo before delivery](../artifacts/graphics-v03-0.26.3/residents/stockpile-carrying.png) і [after delivery](../artifacts/graphics-v03-0.26.3/residents/stockpile-delivered.png)
- [Native art metadata](../artifacts/graphics-v03-0.26.3/native/art-report.json), [resident metadata](../artifacts/graphics-v03-0.26.3/residents/report.json), [SHA256 manifest](../artifacts/graphics-v03-0.26.3/manifest.json)

Imagegen [V03 target](visual-targets/progress/v03-objects.png) використано лише як художній орієнтир. Він не є runtime-доказом і не підмінює нативні кадри вище.

## Підтверджені контракти

- Renderer і picking використовують один `building_visual` resolver і однаковий width.
- `stage1/2/3` залежать від construction progress; завершені I рівні вибирають `ready/winter`.
- Тільки House II та Workshop II вибирають `.level2`; під час upgrade лишається поточний рівень, доки authoritative upgrade не завершено.
- Winter не понижує II рівень: level-II plate зберігається з холодним tint.
- Simulation footprint, entry cell, projection 58×29, protocol, rules, generator і save format не змінені.
- Directional resident: 2 образи × 4 load states × 8 напрямків × 4 фази = 256 перевірених IDs.
- Cargo походить із фактичних `ResidentLogisticsView.item/quantity`; `quantity == 0` вибирає порожній walk.
- Contact shadow малюється до art-ready resident sprite й має ширший ground contact для cart.
- Production smoke/fire потребує реального active order у фазі `Working`.
- Inspector preview показує level-II art лише для House II/Workshop II.

## Версії й межі

Engine `0.26.3`; protocol `20`; rules `18`; generator `6`; save format `8`. Нового schema/network bump немає, бо V03 presentation-only.

Не закрито цим локальним прогоном:

- незмонтований native video руху/паузи/4× не записано; замість нього є runtime motion tests, 256-frame GPU probe та before/after delivery captures;
- clean install/package запуск з окремої свіжої папки не виконувався;
- два фізичні Windows-ПК і cross-network не перевірялися;
- `level2-winter` PNG немає: використано стабільний холодний tint, а повний snow-cover лишається V05;
- купи Складу не прив'язані до фактичного stock у V03, бо bounded inventory-art binding відсутній;
- нових PNG не пакували, тому AtlasTool не запускав production export у новий каталог; чинні atlas/manifest/tool/staging контракти пройшли тести.

Файл `Ribterra.exe`: 47 082 496 bytes, SHA256 `04d96f4ff2e1895f671f8a62e3f393150fa1f25022bfb9cf222983c8b320f939`.

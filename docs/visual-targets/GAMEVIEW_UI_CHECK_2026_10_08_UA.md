# Native RmlUi, меню та typed World — перевірка 2026-10-08

Статус: цей обмежений UI checkpoint прийнято після свіжої strict збірки, двох фактичних GPU-запусків та незалежного огляду. Повна ціль рушія залишається активною. Звичайний запуск гри, референсний арт, усі панелі та FPS ще OPEN.

Build `gameview-ui-2026-10-08-attempt03`: 178 sources, 24 translation units, 122 фактичні команди з exit 0; 554 файли / 132 383 967 байтів. EXE: 47 034 880 байтів, SHA-256 `E3AED475E84A97B7C753F789843AB961FBBFE9C423090DAD81F642D8AFD56618`. Native `attempt02` і `attempt03` завершились exit 0 на Direct3D12 / NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080. Кожен запуск має 20 файлів: 31 112 220 / 31 112 219 байтів.

Parent та незалежний read-only verifier пройшли: 178 sources / 1245 bound files / 122 zero commands / 16 точних derived outputs. Незалежний повторний hash-огляд: 3032 рядки прив'язки, 1245 унікальних файлів, 0 розбіжностей. Усі 15 PNG та raw WaterCoverage повторилися байт у байт; actual save із UUID прив'язаний окремо. Report повторився за винятком виміряного часу всього діагностичного запуску.

`Frame2D::content` явно розрізняє `World` та `UiOnly`, за замовчуванням `World`. Renderer перевіряє відсутність World-параметрів у `UiOnly` і використовує наявний SDR-шлях UI, зберігаючи обраний cumulative graph для наступного World-кадру. Порожній неправильний World-кадр не стає UI автоматично. `clear_commands()` повертає початковий `World`.

RmlUi тепер додає канонічні `SurfaceVertex{}` до власних вершин, якщо World-префікс уже має aligned typed/HDR stream. Неправильний префікс відхиляється, попередні дані зберігаються. Legacy-префікс без companion stream зберігає свою конвенцію. Буфер проміжних вершин повторно використовує місткість між draw-викликами.

## Ресурси та джерела

Створено два окремі UI kits: `build/engine-ui-kit-2026-10-08-attempt01` і `attempt02`. Кожен містить 50 оригінальних ресурсів, новий manifest та окремий звіт — 52 файли, 26 580 243 байти. Усі 52 файли повторилися байт у байт. Перевірені початкові джерела, metadata, чотири license notices, RML/RCSS references та 11 ready building portraits; parent oracle перевірив 164 bound files. Спроба повторного експорту в існуючу папку відхилена з фактичним Python exit 2, після неї файли залишились незмінними.

GameUi використовує власний AssetCatalog цього UI kit; typed GameView використовує окремий diagnostic control kit. Повний production catalog не копіюється. Оригінальний `art/world.json` у UI kit містить також сторінки поза цією підмножиною, тому kit призначений для меню/HUD та зазначених portraits, не для всіх панелей чи повного світу.

У збірці фактично використано 24 translation units, installed RmlUi 6.3 Core з FreeType та їхні статичні залежності, 23 shader programs / 46 stages / 93 свіжі shader artifacts. SDK headers/configs і всі link libraries перевірені за шляхом та hash до/після збірки; великі SDK-бібліотеки не копіювались. Кожна свіжа папка обмежена 128 MiB, мінімальний запас вільного місця — 384 MiB.

## Фактична перевірка

Probe створив рівно 15 PNG, один actual World save, один raw WaterCoverage і `ui-report.json`. Runner додав два файли фактичного виконання. Пройшли 14 positive controls, 25 renderer negatives і чотири RmlUi prefix negatives. Неправильні кадри не змінили targets чи completed submission; наступний правильний кадр відрендерився. Окремого acquisition counter API немає, тому така перевірка не заявляється.

Перевірено справжні українські fonts/menu/HUD, premultiplied textured UI/scissor/text, optional neutral companions, повний World-префікс та neutral UI suffix, World → UiOnly → World, retained targets. Presentation depths 1/2/3 підтримані фактично, усі відповідні ring slots відвідані. Odd resize 1601×901 та повернення 1920×1080 пройшли. UI-only odd resize перевірено direct UI контролем; усі UI panels/DPI/F1 ще не прийняті цим probe.

Меню: 648 вершин, 22 UI commands, 8 textures, 2 073 596 змінених пікселів. HUD зберіг World-префікс із 1624 вершин / 270 commands, додав 47 UI commands з нейтральними companions; змінив 145 328 пікселів. Premultiplied контроль дав `[74,47,36,255]`, clip і text coverage пройшли.

Окремий read-only `tests/gameview_ui_scene_evidence_tests.py` перевірив видимий World: 375 018 ненульових RGB-пікселів / 5818 RGBA-кольорів у кожному повторі; before/after/recovered точні. Центральна ділянка `[280,100,1590,850]` містить всі ці scene pixels і точно збереглась під HUD. Чорний negative control відхилено лише в пам'яті, без запису файлів. Цей додатковий verifier прив'язано в receipt, але його немає в попередньо frozen Build03 snapshot. Початкове враження чорного preview спростовано прямим читанням PNG; production renderer через це не змінювався.

Actual RmlUi має один lifecycle на configured renderer. Порівняння Legacy/cumulative використовує окремий direct UI oracle; це не доказ реального RmlUi на другому Legacy renderer. Старі RmlUi atlas handles не повторно рендеряться після наступної compose/retirement.

## Межі прийняття

Звичайний `main`, production material art, відповідність трьом референсам, інші UI panels/settings actions, опади/snow/smoke/fire FX, повний набір тестів та повний performance baseline залишаються OPEN. Diagnostic World містить нейтральні контрольні матеріали. Час усієї перевірки не є FPS чи GPU duration; відповідні поля залишаються `null`.

Перша C++-спроба відхилена через C5054 shortcut expression; його виправлено явними integer casts. Build02 пройшла, але Native01 відхилив пізнє встановлення schema-2 art після UI submit. Незмінений cold-load block перенесено в probe перед першим submit; production GameArt guard залишено. Build03 і Native02–03 перевірили це виправлення. Усі невдалі outputs збережені.

Попередній accepted weather GPU snapshot збережений; зміни `render_frame.hpp`/renderer зробили його exact cohort історичною. UI Build03 підтверджує власний frozen source snapshot. Наступні зміни bound sources зроблять і цей cohort історичним для live checks. Actual receipt: `.unlazy/engine-completion-2026-10-08/ui-gpu-actual-2026-10-08.json`. Формальна automatic gate certification не заявляється. Файли не видалялися.

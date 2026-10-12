# Перевірки Ribterra 0.10: локальні, GPU, пакет і CI

Оновлено: 9 вересня 2026 року (Europe/Warsaw). Середовище: Windows 11 Pro 10.0.26200; AMD Ryzen 5 5600H, 6 ядер / 12 потоків; AMD Radeon Graphics і NVIDIA GeForce RTX 3050 Laptop GPU; MSVC 19.50; Release x64; C++20.

Цей документ розділяє поточні перевірки ручних наказів 0.10 та історичні результати 0.2–0.9/M0. Повний gate рушія й доповнення користувача визначено в [ENGINE_REQUIREMENTS.md](ENGINE_REQUIREMENTS.md) і ще не пройдено.

## Завершальне виправлення лінійки 0.10: 0.10.1

Джерело runtime — `27da15308cb3f9f9a1c824decde26d6cd4f80a15`. Локальний повний набір пройшов **52/52 за 296,46 с**, без C++ warnings/errors. Після цього фінальний тест мережі посилено від graceful reconnect до автоматичного повернення після справжнього транспортного timeout: обидва остаточні профілі **1% і 3%** також пройшли. Код runtime між цими перевірками не змінювався. [Локальний звіт](evidence/release0101-local.json), [CTest](evidence/release0101-ctest.txt). Фінальний [CI `13ed5c2`](https://github.com/fipsyatina/ribterra/actions/runs/34285248451) успішний: **Windows 52/52 за 360.27 с, Linux 35/35 за 251.01 с, UBSan 13/13 за 135.18 с**. Він містить остаточні посилені тести обриву та сумісну конфігурацію санітайзера; C++ warnings/errors та UBSan errors — 0. [Метадані](evidence/manual-ci-13ed5c2.json), [Windows](evidence/manual-ci-windows-13ed5c2.txt), [Linux](evidence/manual-ci-linux-13ed5c2.txt), [UBSan](evidence/manual-ci-ubsan-13ed5c2.txt), [якість Windows](evidence/manual-ci-windows-13ed5c2-quality.json), [якість Linux](evidence/manual-ci-linux-13ed5c2-quality.json).

CI `13ed5c2` відрізняється від runtime `27da153` лише налаштуванням санітайзера та його документацією. Перший запуск нового network_session під UBSan виявив несумісність vptr із GNS без RTTI: [невдалий прогін](evidence/release0101-ubsan-initial-failure.txt). Лише адаптер NetTransport вимикає vptr; інші перевірки залишені. [Точне покриття й обмеження](SANITIZERS.md). Windows runtime не змінено.

Виявлено й відтворено помилку: якщо чергова межа 5 Hz наставала під час очікування ACK старого snapshot, пауза могла залишити клієнта зі старим станом. [Невдалий прогін до виправлення](evidence/release0101-before-snapshot-fix.json). Сервер тепер пам'ятає запит на оновлення до його виконання; pending transition залишається незмінним до ACK.

Графічний клієнт автоматично повертається до тієї самої сесії після транспортного обриву. Повтор непідтверджених команд обмежений 30 секундами, зберігає весь payload та ID й допускається лише за незмінних endpoint/world/player/token/epoch. Перевірені переривання до прийняття й після прийняття до terminal receipt: рівно один наказ та один видимий terminal result. Застарілий намір і змінений epoch не запускають replay; явний disconnect не перепідключається. Історія чату не дублюється. [Тести](../tests/network_recovery_tests.inc), [контракт](MANUAL_ORDERS.md).

| Мережевий профіль | Фактичні результати |
|---|---|
| 150 мс базового RTT, 1% втрат у кожен бік | 19 частин initial snapshot; 12 terminal replies; автоматичне повернення після обриву; точна кінцева проєкція обох клієнтів; Wood 83 у сховищі, вантаж 0 |
| 150 мс базового RTT, 3% втрат у кожен бік | Ті самі інваріанти; конфлікт має одного переможця; transfer/cancel/release працюють; справжні 30 секунд grace спливають без руху sim time; старе право не повертається |

Профілі також задають 2% дублювання UDP datagrams та 5% додаткової затримки на 90 мс. Обидва фактично втрачали й дублювали пакети; лічильники — у [1%](evidence/release0101-adverse-1pct.json) і [3%](evidence/release0101-adverse-3pct.json). Це два клієнти на одному ПК через справжній зашифрований UDP і контрольовані relay. Початковий generated world у тесті навмисно відкрито повністю для multipart; подальші накази звичайні. Це не перевірка фізичних ПК, пропускної здатності чи всього мережевого навантаження гри.

GPU D3D12 / RTX 3050 Laptop: **36 кадрів**. Усі 16 попередніх кадрів наказів байт-ідентичні раніше переглянутим; 20 нових кадрів показують чотири повідомлення відновлення в п'яти мовах. Переглянуто найдовше повідомлення в кожній мові та всі чотири українські повідомлення — 8 нових кадрів. Вони читаються при 1100×700 без блокувального обрізання. [Device](evidence/release0101-gpu-device.json), [виконані UI assertions](evidence/release0101-gpu-results.json), [візуальний огляд](evidence/release0101-gpu-review.json). Тексти в HUD у цьому probe подаються явно; поведінку реального відновлення підтверджують окремі виконувані тести вище. Це не приймання concept art або native OS automation.

Розпакований Windows-пакет пройшов **4/4**: меню, шахта/save-resume, панель наказів і dedicated server + два probes + графічний клієнт. Усі 36 пакетних кадрів наказів байт-ідентичні source probe. Окремо переглянуто три пакетні сцени. [Smoke](evidence/release0101-package-smoke.json), [мережа](evidence/release0101-package-network.json), [візуальне порівняння](evidence/release0101-package-visual.json), [metadata архіву](evidence/release0101-package.json).

Фінальний архів: `dist/Ribterra-Windows-x64-0.10.1.zip`, **30,238,278 байтів**, SHA-256 `B3C6C057CA71CD72ECC43CB5DB38640E4A59B7E6AF0EBA2E6B9479B35B681EBE`. Після запису фінального CI оновлено лише README: **49 із 50 файлів** байт-ідентичні архіву, який пройшов smoke. Усі EXE, DLL, assets і ліцензії незмінні; додаткові probe EXE існують тільки в перевірочному розпакуванні. [Порівняння з перевіреним пакетом](evidence/release0101-package-readme-comparison.json). Launcher указує на `dist/releases/0.10.1/Ribterra/Ribterra.exe`.

Збереження **8**, protocol/rules **7**, generator **2** і gameplay hash `5cace4260fbbaf80c55177f5b78e6356e1d1f996dfd99dce9caf818c25360606` незмінні. Старі genuine v7/v8 inputs збережено. Runtime 0.10.0 та старі перевірки нижче залишаються історичними доказами. Реалізацію 0.11 у цьому завершенні не починали; повний рушій і V01–V03 зберігають окремі критерії приймання.

## Історична основа ручних наказів: 0.10.0

Контракт: **protocol 7 / rules 7 / generator 2 / save 8**. Поіменні Move/Gather дають стабільні ID прийнятих наказів; Shift додає до черги, звичайна команда замінює її після перевірки. Збір має явне завершення OneDelivery або UntilExhausted. Скасування одного наказу не відпускає керування; Release та Transfer є окремими серверними діями. Відключення має 30 секунд монотонного grace, який спливає і на паузі. Вантаж завершує фізичну доставку перед зміною діяльності. [Контракт і обмеження](MANUAL_ORDERS.md).

### Локальний прогін

Фінальний MSVC Release build і **50/50 CTest за 191,09 с** пройдено ([журнал](evidence/manual-ctest.txt), [версії, хеші й binary metadata](evidence/manual-local-validation.json)). Нові `manual_orders` і `manual_order_persistence` тривали 17,00 та 34,54 с; повний попередній `persistence` — 43,58 с. Перевірено часткові результати груп, ліміти 8/8192, переповнення ID, неприйняту Replace без зміни старої черги, фізичні межі, захоплення/передачу/відпускання й reconnect, pause-boundary snapshots, 64-бітні ID, full/delta/removal/multipart, підміни зв'язків та причин завершення.

Перед фінальним прогоном виправлено окрему причину `Cancelled=8`, помилку RmlUi dropdown, який накопичував старі option після SetInnerRML, і скасування внутрішнього Work PathPending після доставки. Останній випадок міг поновити скасований збір або залишити наступний наказ із неправильним executor; регресія перевіряє чергу з хвостом і без нього, точні 83 Wood / 1 XP та валідний стан після збереження. Попередній повний прогін до цього останнього виправлення мав 50/50 за 195,27 с; його не підмінено фінальним доказом.

Міграція порівнює всі старі поля **25 genuine v7 fixtures** і frozen v7 checksum на кожній із 65 історичних точок, а також подальше детерміноване відновлення нового світу. `.v7.bak` зберігає точні старі байти; nullable issuer/tick не вигадують походження команди. Підтримано попередні явні міграції. Це скінченні історії з нульовими innate abilities, а не всі можливі світи або повний fault-injection gate. [Формат і межі](SAVE_FORMAT.md).

### GPU та введення

Фінальний `ManualOrderVisualProbe` пройшов на **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. Реальні SDL pointer/key events перевірили Shift-чергу, вибір Gather, скасування середнього ID, груповий Release, захист від повторного Space, reset, фізичний Wood3 cargo → доставка → наступний Move та save/resume. Два loopback-клієнти передали контроль на tick 0; попередній holder отримав типізовану відмову з ID мешканця. [GPU](evidence/manual-gpu-device.json), [результати](evidence/manual-gpu-results.json).

Переглянуто **16 кадрів EN/UK/RU/PL/ES**, включно з 1100×700. Черга, кнопки й переклади читаються; Close лишається доступним під час прокрутки. Скасований наказ показує «Скасовано гравцем», а holder залишається «Ви»; після Release інші накази мають окрему причину «Керування відпущено». [UK](evidence/manual-visual-uk-final.json), [EN/RU](evidence/manual-visual-en-ru-final.json), [PL/ES](evidence/manual-visual-pl-es-final.json), [вантаж і наступний наказ](screenshots/manual-orders/uk-cargo-and-next-order.png), [компактна панель](screenshots/manual-orders/uk-compact-control.png). Початкові кадри зі знайденою неточністю збережено окремо в `screenshots/manual-orders-initial`.

Це огляд фактичного SDL_GPU framebuffer та введення подій через SDL. Нативне керування екраном ОС у середовищі недоступне. Технічний world art не є прийманням концептів; повні keyboard-only/IME/4K, інші GPU та 2–4 фізичні ПК залишаються відкритими.

### Контрольований benchmark

`ManualOrderBench` послідовно виміряв п'ять сценаріїв Move Replace → Gather OneDelivery Append; таймери охоплюють лише подання цих команд і окремі `World::tick`. Під час заміру збірки й інші тести не виконувалися; попередній процес гри 0.6 лишався відкритим. [CSV](evidence/manual-benchmark.csv), [незалежний огляд сценарію й чисел](evidence/manual-benchmark-review.json).

| Мешканці | Прийнято / відхилено через місткість | Завершення, tick | Подання команд, ms | p95 tick, ms | p99 tick, ms | max tick, ms |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 20 / 0 | 201 | 0,0158 | 0,0137 | 0,0607 | 0,5303 |
| 100 | 200 / 0 | 203 | 0,0909 | 0,1014 | 0,6795 | 2,4630 |
| 500 | 1000 / 0 | 213 | 0,4068 | 2,6610 | 2,9842 | 3,4981 |
| 1500 | 3000 / 0 | 239 | 1,8676 | 4,0096 | 4,2049 | 4,4980 |
| 4×1500 | 8192 / 3808 | 296 | 6,2271 | 5,5051 | 6,7883 | 7,6996 |

У сценарії 6000 усі мешканці фізично прибули, 2192 прийняті збори дали рівно 6576 Wood додатково до 320 стартових. Наприкінці — 6896 Wood у сховищах, нуль cargo/активних наказів/pending paths/Failed; піки — 1500 manual visits, 1 shared batch admission, 4096 path work та 42 391 336 логічних planner bytes. Межа 8192 відхилила решту 3808 Gather без часткового створення.

Оптимізація групового резервування й уникнення повторних порожніх сканувань скоротила подання команд у цьому одному сценарії з 343,2010 до 6,2271 ms. Це один seed і прогін на контрольованій площині зі спільним джерелом, мешканцями в одній стартовій точці, збільшеними складами й їжею. Вимірювання не охоплює GameServer/leases, мережу, UI, збереження, RAM/VRAM або складний рельєф і колізії. Повторні Replace з реальною великою історією та Stop з активним Work мають інші витрати; повний performance gate залишається відкритим.

### Пакет і CI 0.10

Розпакований `dist/Ribterra-Windows-x64-0.10.0.zip` пройшов **4/4 smoke з exit 0**: меню, шахта, ручні накази та dedicated server із двома probes і графічним клієнтом. Додаткові probes скопійовано лише в перевірочну розпаковану директорію. Усі 16 пакетних кадрів наказів байт-ідентичні переглянутим; ще три кадри меню/шахти/мережі переглянуто окремо. [Smoke](evidence/manual-package-smoke.json), [порівняння 16 PNG](evidence/manual-package-visual-comparison.json), [огляд трьох сцен](evidence/manual-package-visual-review.json), [GPU пакета](evidence/manual-package-device.json).

На tick 180 обидва мережеві клієнти бачать завершену споруду, 50 Wood, нуль reserved та узгоджений чат; дубльована спроба будівництва дала один Applied і один Rejected. Це loopback на одному ПК. Шахтний сценарій вичерпав 24 ore й зберіг відновлену проєкцію; 88 Stone у звіті — загальна кількість, а кадр у момент доставки показує 86 у сховищі та 2 в руках. [Мережа](evidence/manual-package-network.json), [шахта](evidence/manual-package-mine.json).

Пакет має **30 229 861 байти**, SHA-256 `9C4BB264CD7689831478A31450A354F3E084089B50108194E93AFD56D41EB5DB`. [Metadata](evidence/manual-package.json). Runtime походить із source **`5476c90676dc3152ef044e80fc0d12884d994f99`**. Після smoke оновлено лише README: 49 із 50 файлів, включно з усіма виконуваними файлами, assets і licenses, лишилися байт-ідентичними перевіреній копії. [Порівняння](evidence/manual-package-readme-update.json). `Play Ribterra.cmd` через `dist/current-build.txt` запускає `dist/releases/0.10.0/Ribterra/Ribterra.exe`; попередній відкритий процес 0.6 не закривався.

[GitHub CI для 5476c90](https://github.com/fipsyatina/ribterra/actions/runs/34260695987) успішний: **Windows 50/50 за 230,21 с, Linux 33/33 за 126,36 с, UBSan 12/12 за 120,56 с**. Windows має нуль C++ warnings/errors; Linux і UBSan разом — десять попереджень GCC та нуль runtime errors. Два попередження стосуються захоплення типу з anonymous namespace, вісім — неповних агрегатних ініціалізацій тестів. [Run metadata](evidence/manual-ci-5476c90.json), [Windows](evidence/manual-ci-windows-5476c90.txt), [Linux](evidence/manual-ci-linux-5476c90.txt), [UBSan](evidence/manual-ci-ubsan-5476c90.txt), [Linux quality](evidence/manual-ci-linux-5476c90-quality.json).

Наступний source **`a4dd9a3b8c09dd9d7c6941c30dddd4d522d38ed8`** захоплює лише scalar pending batch ID та явно встановлює попередні значення після стандартної ініціалізації тестових команд. Версії, gameplay hash, save/wire і задумана поведінка незмінні. Локальний Release build має нуль C++ warnings/errors; цільові manual/save/server/protocol/network/UI тести пройшли **6/6 за 64,61 с**. [Build](evidence/manual-warning-build.json), [CTest](evidence/manual-warning-ctest.txt). Windows-пакет залишається на перевіреному runtime `5476c90`.

**Фінальний CI `a4dd9a3` успішний:** [run 34262343935](https://github.com/fipsyatina/ribterra/actions/runs/34262343935), **Windows 50/50 за 217,37 с, Linux 33/33 за 124,09 с, UBSan 12/12 за 118,12 с**. Обидва jobs мають нуль C++ warnings/errors і UBSan errors. [Metadata](evidence/manual-ci-a4dd9a3.json), [Windows](evidence/manual-ci-windows-a4dd9a3.txt), [Linux](evidence/manual-ci-linux-a4dd9a3.txt), [UBSan](evidence/manual-ci-ubsan-a4dd9a3.txt), [Windows quality](evidence/manual-ci-windows-a4dd9a3-quality.json), [Linux quality](evidence/manual-ci-linux-a4dd9a3-quality.json). Окреме службове попередження GitHub про Node.js 20 у Actions v4 залишається; воно не є C++ або sanitizer-діагностикою. Повний рушій та V01–V03 цим рубежем не завершені.

### Справжні сейви 0.10 для наступних міграцій

До зміни будь-якої схеми заморожено 24 файли writer `5476c90`: public headers, дві бібліотеки, два generated headers і schema. [Provenance snapshot](evidence/manual-v8-writer-snapshot.json). Незмінний writer створив **27 genuine v8 fixtures** звичайними командами; окремий verifier підтвердив точне повторне кодування, незмінні оригінали й усі **65 контрольних точок / 64 наступні тики** для кожного знімка, зі збереженням невичитаних outcomes.

Набір охоплює ручні черги, shared Search/Reconstruct/частково застосований Stage, рух, cargo, WaitingBoundary, pending Gather continuation, Cancelled/Released/Stopped/Deferred, мігрований legacy atomic шлях та neutral cargo. Для Farm зафіксовано Planned, Delivery, оплачену Work, завершення, межу 399→400, фактичні Food8 cargo й доставку та нинішній Farmer NoCompatibleWork. [Каталог](../tests/fixtures/WORLD_SAVE_V8.md), [результати](evidence/manual-v8-fixtures.json), [незалежний огляд](evidence/manual-v8-fixture-review.json).

Природного історичного opaque executor немає; цей пункт лишається явною прогалиною, а тест із підміненим manual flag не видається за genuine fixture. Набір також не охоплює всі здібності, всі комбінації фаз або необмежені майбутні історії. Це зафіксована поведінка 0.10, а не перевірка ще не реалізованого виробництва чи міграції 0.11. [План наступного етапу](FARM_PRODUCTION_PLAN_UA.md) зберігає всі 41 вимогу повної цілі.

## Історичні професії та навички: зріз 0.9 перевірено

Контракт поточного коду: **protocol 6 / rules 6 / generator 2 / save 7**. Професія відокремлена від Job; серверні Assign/Stop мають окремі Applied/Deferred/Rejected для кожного мешканця й зберігають фізичний вантаж до безпечної межі. Вісім незалежних шкал XP/practice/abilities та збережуваний дробовий credit використовують одну таблицю балансу й один ліміт skill+ability+familiarity **−15…+15%**. Зміна професії не стирає XP. Owner-only snapshot передає готові skill breakdown, видиме призначення та причину очікування, але не внутрішні practice timestamps/debt чи приховані worksite. Міграція genuine format 6 зберігає старі XP, planner frontier та work/logistics state й створює `.v6.bak`. Детальні контракти: [SKILLS.md](SKILLS.md), [SAVE_FORMAT.md](SAVE_FORMAT.md).

### Локальні тести та виправлення UI

Повна локальна збірка й CTest після захисту від повторних Enter/Space та уточнення локалізованих результатів пройшли: **46/46 за 126,97 с** ([журнал](evidence/profession-ctest.txt)). Перший прогін мав 44/46 за 122,11 с: падали лише `profession_ui` і `profession_client_rendered` (`.deps/profession-first-full-ctest.log`). Виправлення CSS `tab-index: auto` дало цільові 2/2 за 11,58 с (`.deps/profession-ui-keyboard.log`) перед успішним повним прогоном.

Виправлено порожній необов'язковий ICU-аргумент зі збереженням перевірки шаблонів, старий worksite після нового Assign/Stop, вибір/чернетку/захоплені ID при заміні світу та повідомлення негайної відмови. Dropdown містить лише виконувані професії; Enter/Space не дублюють наказ і не перемикають паузу під HUD. Після розділення приглушеного hover і золотого focus/active повторні UI-тести пройшли **11/11 за 40,06 с** ([журнал](evidence/profession-ui-final.txt)).

### Фінальний GPU-сценарій

`ProfessionVisualProbe` пройшов EN/UK/RU/PL/ES на **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false: вісім skill rows, pointer hover/Tab focus на паузі, захоплене призначення, шахтар, Stop, 128 ticks save/resume кожної сцени й збережений вантаж Deferred. [Device](evidence/profession-gpu-device.json), [результати](evidence/profession-gpu-results.json). Переглянуто 23 кадри: підписи, XP й breakdown читаються, пояснення переносяться без блокувального обрізання. Огляди/SHA-256: [UK, 7](evidence/profession-visual-uk.json), [EN/RU, 8](evidence/profession-visual-en-ru.json), [PL/ES, 8](evidence/profession-visual-pl-es.json); приклади [PL](screenshots/professions/pl-miner-skills.png), [ES](screenshots/professions/es-miner-skills.png). Технічний world art не є прийманням концептів.

### Benchmark професій

`SimRunner --benchmark-professions --seed 70421 --ticks 10000` створив **25 завершених рядків**: п'ять послідовних етапів для 10/100/500/1500 і 4×1500 мешканців. [CSV](evidence/profession-benchmark.csv) та [незалежний огляд CSV і коду](evidence/profession-benchmark-review.json) не містять порушень перевірених інваріантів. Етапи — лісоруб → шахтар → повернення до лісоруба → призначення на вичерпане джерело → Stop.

Для 6000 мешканців після 1015 ticks усі живі, кожен має 2 Wood XP і 1 Mining XP; рівно 12000 деревних і 6000 кам'яних результатів дали **36000 Wood / 12000 Stone**. Наприкінці кожного етапу немає cargo, предметів на землі, активних Work або pending paths. Код окремо перевіряє відповідність XP і фізичних предметів кожної нації, включно з усіма місцями зберігання, та точне споживання провізії. Порожнє джерело дає WorksiteUnavailable; фінальний Stop — Stopped.

Час нижче — мілісекунди `World::tick`, окремо для трьох продуктивних етапів 6000 мешканців:

| Етап | Тактів | p95 tick | p99 tick | max tick | Початкове призначення, ms |
|---|---:|---:|---:|---:|---:|
| Лісоруб | 299 | 6,4730 | 7,7741 | 9,7824 | 2,002 |
| Шахтар | 336 | 6,6212 | 7,4738 | 8,1751 | 9,554 |
| Знову лісоруб | 340 | 5,9921 | 7,0170 | 9,0130 | 3,782 |

Найбільший p95 серед етапів — **6,6212 ms**, p99 — **7,7741 ms**, max — **9,7824 ms**; це не обчислені квантилі об'єднаного прогону. П'ять початкових Assign/Stop сумарно займали 22,481 ms, усі додаткові Stop-бар'єри — 2,909 ms; ці команди, читання діагностики, оновлення resident cache, перевірка обліку й запис CSV не входять у tick timing. Максимум selector visits/admissions — 1024/32, Work — 128/32, path work — 4096; planner high-water mark 4,731 MiB і 7 staged cells включає попередні етапи.

Обмеження суттєві: один seed і один прогін; плаский контрольований світ, мешканці починають разом, два спільні джерела, близькі власні табори, збільшені склади й достатньо їжі. Після одного корисного збору кожному надсилається звичайний Stop, який дає завершити доставку; це вирівнює доступ до кінцевого запасу й не доводить справедливості необмеженого автономного збору. Межа `candidate_checks ≤ 2×visits` стосується цього fixture, не загальної складності selector. Щільні ресурси, перешкоди, портали, перемикання builder/hauler, довге забування, save/network/UI, колізії, RAM/VRAM і повний Utility AI цим benchmark не перевірені.

### Windows-пакет 0.9

Розпакований `dist/Ribterra-Windows-x64-0.9.0.zip` пройшов **4/4 smoke з exit 0**: меню, шахта, професії та мережа. Допоміжні probes скопійовано лише до розпакованої директорії. Усі 23 пакетні profession PNG байт-ідентичні переглянутому GPU-набору; окремо переглянуто три кадри меню/шахти/мережі. [Metadata архіву з актуальним SHA-256](evidence/profession-package.json), [smoke](evidence/profession-package-smoke.json), [порівняння PNG](evidence/profession-package-visual-comparison.json), [GPU пакета](evidence/profession-package-device.json). Dedicated server і два окремі клієнти синхронізували будівництво на tick 176: один Applied, один Rejected, Wood 50 / reserved 0, без дублювання матеріалів; це loopback, не два фізичні ПК. [Мережа](evidence/profession-package-network.json). `Play Ribterra.cmd` через `dist/current-build.txt` тепер запускає `dist/releases/0.9.0/Ribterra/Ribterra.exe`.

Пакет, наведені GPU-кадри й benchmark належать до source `d3bc78530ab2f03d8116bb9ef4c09ba71ca0232d`. Після package smoke архів отримав лише оновлений README: **49 із 50 файлів незмінні**, усі runtime/assets/licenses байт-ідентичні перевіреній розпакованій копії. [Порівняння на момент README-only оновлення](evidence/profession-package-readme-update.json). Подальше редагування документації не означає повторної публікації виконуваного пакета.

### CI та поточне джерело

[CI `d3bc785`](https://github.com/fipsyatina/ribterra/actions/runs/34248830961) завершився успішно: **Windows 46/46 за 164,01 с**, **Linux 30/30 за 68,64 с**, **UBSan 10/10 за 49,14 с**. Windows не мав C++ warnings; Linux-збірки разом повідомили про **60 `-Wmissing-field-initializers`** для `Resident::profession`, `GameCommand::assignment` і `CommandReply::residents`. UBSan errors немає. [Windows log](evidence/profession-ci-windows-d3bc785.txt), [Linux log](evidence/profession-ci-linux-d3bc785.txt), [UBSan log](evidence/profession-ci-ubsan-d3bc785.txt), [Windows quality](evidence/profession-ci-windows-d3bc785-quality.json), [Linux quality](evidence/profession-ci-linux-d3bc785-quality.json).

Поточний source **`42036ffc3e08bc967b4b477f8a599ac0629e5658`** додає `{}` лише цим трьом полям, зберігаючи попередні значення, layout, save/wire, gameplay і графіку. Локальна MSVC Release-збірка має нуль warnings; цільові skills/professions/persistence/server/protocol/network_session пройшли **6/6 за 56,21 с**. [Build evidence](evidence/profession-defaults-build.json), [CTest](evidence/profession-defaults-ctest.txt). Windows ZIP та launcher 0.9 з `d3bc785` незмінні; виправлення GCC-діагностики не є новим опублікованим Windows-архівом.

**Фінальний CI `42036ff` успішний:** [run 34250688287](https://github.com/fipsyatina/ribterra/actions/runs/34250688287), **Windows 46/46 за 171,96 с**, **Linux 30/30 за 79,14 с**, **UBSan 10/10 за 59,83 с**. Обидва jobs мають нуль C++ warnings/errors та UBSan errors. [Run metadata](evidence/profession-ci-42036ff.json), [Windows](evidence/profession-ci-windows-42036ff.txt), [Linux](evidence/profession-ci-linux-42036ff.txt), [UBSan](evidence/profession-ci-ubsan-42036ff.txt), [Windows quality](evidence/profession-ci-windows-42036ff-quality.json), [Linux quality](evidence/profession-ci-linux-42036ff-quality.json). Пакет 0.8 і CI `f62cea0` нижче історичні. Ці перевірки не закривають повний рушій або V01–V03.

### Справжні збереження v7 для наступної міграції

Незмінними headers/libs 0.9 з `d3bc785` створено **25 файлів формату 7** через звичайні команди й ticks. Окремий verifier підтвердив побайтове повторне збереження та однакові checksum на кожному з **64 наступних ticks для кожного файлу**, без очищення undrained path outcomes. Це 1600 порівнянь продовження плюс 25 початкових станів. [Каталог і спосіб відтворення](../tests/fixtures/WORLD_SAVE_V7.md), [provenance та SHA-256](../tests/fixtures/world_save_v7.provenance.json), [повні послідовності перевірки](evidence/profession-v7-fixtures.json), [незалежний огляд джерел і результатів](evidence/profession-v7-fixture-review.json). Додано frozen schema `world_save_v7.fbs`. Формат 8 і міграцію 7→8 ще не реалізовано; ці fixtures не є прийманням наступного ручного керування або всього рушія.

## Продовження роботи та будівельники 0.8

Контракт: protocol 5 / rules 5 / generator 2 / save 6. Фінальна локальна Windows Release-збірка пройшла **40/40 CTest за 98,65 с** і встановила окремий пакет `dist/releases/0.8.0/Ribterra`. [CTest](evidence/work-ctest.txt), повний журнал — `.deps/work-final-build.log`. C++-попереджень MSVC у збірках цього рубежу немає. CI source commit `f62cea0` також пройдено; точні результати й журнали наведено нижче.

Нові регресії охоплюють backoff під заповненою чергою, збережений справедливий курсор для 256 мешканців, ручне заміщення до й під час пошуку, нормальне вичерпання, справді недопустиму ціль, смерть/перепризначення будівельника, оплату рівно один раз, retry після зміни topology, переповнення deadline/ID, дублікати й старі майданчики з кількома будівельниками. Genuine v5 cargo/continuation/paid Work fixtures мігрують із точним planner state та `.v5.bak`; усі попередні міграції проходять. Server/protocol/client/GNS перевіряють owner-only status, full/delta/removal, 6000 multipart записів, late join/reconnect і відсутність зовнішніх CommandReply від внутрішніх маршрутів.

Перший прогін виявив неправильний ICU-аргумент для статусу очікування та зависання доставки після скасування попереднього наказу між центрами клітинок. Перший дефект виправлено розділенням тексту й форматованої кількості. Другий — фізичним останнім кроком нульового маршруту: округлена координата більше не підміняє точне прибуття. Окремий regression перевіряє крок 0,125 клітинки та save/resume посеред нього; мережевий сценарій повторного будівництва проходить. Planning майданчика тепер відрізняється від фактичного Travelling.

`WorkVisualProbe` пройшов на **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver 32.0.15.9282 / software=false**. У сценарії звичайних наказів будівельник №7 замінив №1, дійшов до майданчика `(20,20)` і завершив House на tick 569; Wood 50 / Stone 25 не списувалися вдруге. Усі 12 одиниць дерева `(30,22)` дали рівно 36 Wood, запас 116 і нормальний Exhausted на tick 1720. Окремий природний сценарій із двома мешканцями та 512 незабраними зовнішніми відповідями зберіг доставлені 83 Wood й показав автоматичний retry на tick 681.

Перевірено EN/UK/RU/PL/ES, 31 завантаження сцен із порівнянням кожного з наступних 8 ticks і реальні pointer-події RmlUi. Переглянуто **21 PNG**, включно з українськими 1100×700. Після уточнення Planning повторно переглянуто шість змінених кадрів; решта 15 байт-ідентичні вже переглянутим. [GPU device](evidence/work-gpu-device.json), [результати](evidence/work-gpu-results.json), [SHA та огляд](evidence/work-visual-review.json), [очікування retry](screenshots/work/uk-compact-retry-waiting.png), [планування будівельника](screenshots/work/uk-builder-planning.png). Фінальний `LogisticsVisualProbe` також пройшов: [результати](evidence/work-logistics-gpu-results.json), усі [17 PNG ідентичні переглянутим кадрам 0.7](evidence/work-logistics-comparison.json).

### Масштаб і точний облік завершень

[SimRunner CSV](evidence/work-benchmark.csv), seed 70421, Release x64, виконано без паралельної збірки чи тестів; попередня користувацька гра 0.6 залишалася запущеною. [Машина](evidence/work-test-machine.json). Час у таблиці — мілісекунди; fixture має коротку фізичну доставку через портал і спільну вичерпувану жилу.

| Мешканці | Тактів | p95 tick | p99 tick | max tick | Доставлено Stone | Complete / Failed доставок |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 69 | 0,019 | 0,426 | 0,426 | 20 | 10 / 0 |
| 100 | 70 | 0,228 | 3,454 | 3,454 | 200 | 100 / 0 |
| 500 | 75 | 4,021 | 5,116 | 5,116 | 1000 | 500 / 0 |
| 1500 | 87 | 5,810 | 6,422 | 6,422 | 3000 | 1500 / 0 |
| 4×1500 | 220 | 7,322 | 7,977 | 15,072 | 12000 | 6000 / 0 |

Усі мешканці отримали XP та залишилися живими; наприкінці вони Idle, cargo, active intents і pending paths дорівнюють нулю. Для 6000 створено 6826 Work Intents: **826 Started + 6000 Exhausted, нуль Failed/Superseded/Cancelled**. Started завершує передачу маршруту; наступне вичерпання може створити новий окремий intent. Усього 1088 admission attempts, peak Waiting 1130, максимум 128 visits / 32 admissions за tick. При зчитуванні лише обрізаної історії ранній benchmark пропускав terminal transitions у тому самому tick; фінальні лічильники рахують кожен перший перехід до обрізання та перевіряють, що сума дорівнює кількості створених intents. Вони не зберігаються у save й не впливають на gameplay checksum; 640-intent regression перевіряє правильний облік при історії 256.

Це закриває помилки продовження роботи у цьому fixture 0.7 (943 Failed після доставки), але не повний performance gate. Hierarchical pathfinding, повний AI, колізії натовпу, довгі несприятливі маршрути, RAM/VRAM/render і зовнішня мережа залишаються неперевіреними.

### Windows-пакет 0.8

`dist/Ribterra-Windows-x64-0.8.0.zip`: **29 944 245 байт**, SHA-256 `4A2433F3FDECB2F896D25AAC3C6CF31F2910AF0B946AA98E092A16767B7C3601`. [Metadata](evidence/work-package.json). Розпаковано в `artifacts/package-0.8.0-final`. Пакетний клієнт завершив окремі smoke та menu capture із exit 0; MineVisualProbe використав DLL/assets розпакованого пакета й зберіг/відновив вид на tick 3036, total Stone 88 (86 доступно, 2 ще у вантажі на кадрі). Допоміжний probe скопійовано лише до розпакованої директорії; ZIP його не містить.

Сервер, два окремі probes і графічний клієнт із цього ж архіву пройшли loopback-сценарій: різні player ID, один Applied і один Rejected для конкурентного будівництва, спільний завершений House на tick 168, Wood 50 / reserved 0, чат та серверне збереження. Це не тест двох фізичних ПК. [Smoke](evidence/work-package-smoke.json), [device](evidence/work-package-device.json), [шахта](evidence/work-package-mine.json), [мережа](evidence/work-package-network.json). Додатково переглянуто три кадри пакета: [меню](screenshots/work/package-menu.png), [шахта](screenshots/work/package-mine-resumed.png), [мережа](screenshots/work/package-network.png).

[CI для source commit `f62cea0`](https://github.com/fipsyatina/ribterra/actions/runs/34238060438) успішний: **Windows 40/40 за 138,43 с**, **Linux 26/26 за 56,79 с**, **UBSan 8/8 за 36,21 с**. Попереджень C++ та UBSan runtime errors немає. [Metadata](evidence/work-ci-f62cea0.json), [Windows](evidence/work-ci-windows-f62cea0.txt), [Linux](evidence/work-ci-linux-f62cea0.txt), [UBSan](evidence/work-ci-ubsan-f62cea0.txt), [Windows quality](evidence/work-ci-windows-quality.json), [Linux quality](evidence/work-ci-linux-quality.json). Наступний коміт звіту з `[skip ci]` змінює лише документацію та ці докази; перевірений код і пакет належать до `f62cea0b34ad8495d8ce7a598e79f3f2d3f0e290`. Ці результати закривають рубіж 0.8, а не повний рушій.

## Фізичні запаси й будівельна доставка 0.7

Поточний контракт: protocol 4, rules 4, generator 2, save 5. Предмети мають власника та місце Ground/Storage/Carried/Construction; available + reserved = total, вантаж одного мешканця не перевищує 10 одиниць одного виду. Будівництво резервує матеріали, але починає Work і споживає їх лише після повної фізичної доставки. Точні правила й незакриті вимоги: [LOGISTICS_PLAN.md](LOGISTICS_PLAN.md).

Фінальна `scripts/build.ps1 -SkipBootstrap` успішно виконала Release build, **37/37 CTest за 82,62 с** та інсталяцію; попереджень MSVC у C++ коді немає. [CTest](evidence/logistics-ctest.txt), [машина](evidence/logistics-test-machine.json). Регресії охоплюють конкуренцію за останній предмет, шлях через портал, cancel/death/storage full, відсутність втрат і дублювання, незмінний стан після невдалого імпорту, ліміти stack/task/ID/INT_MAX, повторну перевірку фільтра сховища, дробову координату біля межі карти та типізовані помилки продовження роботи. UI перевіряє реальні pointer down/up зі зміною delivery markup між ними: скасування надсилає рівно один правильний building ID. Пізніше успішне перевезення прибирає попередній Failed із серверної проєкції мешканця; окрема регресія з одним носієм підтверджує це.

Фінальний `LogisticsVisualProbe` пройшов на **D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. У всіх EN/UK/RU/PL/ES вантаж з'явився на tick 18, будинок завершився на tick 199; save/resume під час carry дає однакові подальші стани. Перевірено delivery-before-work, місткість, збереження кількості та pointer-скасування під час перевезення. Переглянуто 16 кадрів HUD, включно з 1100×700; після останнього виправлення проєкції вони байт-ідентичні переглянутим. Додатковий 17-й кадр із закритою панеллю показує фізичний сірий вантаж у руках мешканця. [GPU](evidence/logistics-gpu-device.json), [результати](evidence/logistics-gpu-results.json), [порівняння](evidence/logistics-visual-comparison.json), [українські запаси](screenshots/logistics/uk-carrying.png), [вантаж у світі](screenshots/logistics/uk-carrying-world.png).

Архів `dist/Ribterra-Windows-x64-0.7.0.zip`: **29 877 117 байт**, SHA-256 `2F86CCAAA602C762471D0E826C38328008C0358E504662BDFA5839E430FE92CF`. [Metadata](evidence/logistics-package.json). Розпакована копія `artifacts/package-0.7.0-final/Ribterra` пройшла GPU menu, MineVisualProbe і окремий dedicated server + два probes + графічний клієнт. Обидва клієнти спостерігали резервування, вантаж і доставку, дійшли до завершеного House на tick 172 із Wood 50 / available 50 / reserved 0. Один конкуруючий Build Applied, другий Rejected; матеріали не дублюються. [Мережа](evidence/logistics-package-network.json), [шахта](evidence/logistics-package-mine.json), [GPU пакета](evidence/logistics-package-gpu-device.json). Переглянуто кадри [меню](screenshots/logistics/package-menu.png), [шахти](screenshots/logistics/package-mine-resumed.png) та [мережі](screenshots/logistics/package-network.png). Інсталяція використовує `dist/releases/0.7.0/Ribterra`, а `Play Ribterra.cmd` читає `dist/current-build.txt`; попередню відкриту гру 0.6 не перервано.

[CI для `8f23e51`](https://github.com/fipsyatina/ribterra/actions/runs/34228038088) успішний: **Windows 37/37 за 93,34 с**, **Linux 24/24 за 54,23 с**, **UBSan 7/7 за 34,36 с**. Попереджень у C++ коді та UBSan runtime errors немає. [Metadata](evidence/logistics-ci-8f23e51.json), [Windows](evidence/logistics-ci-windows-8f23e51.txt), [Linux](evidence/logistics-ci-linux-8f23e51.txt), [UBSan](evidence/logistics-ci-ubsan-8f23e51.txt), [лічильники Linux](evidence/logistics-ci-linux-quality.json). Подальший коміт звіту з `[skip ci]` не змінює перевірений код. Ці докази закривають рубіж 0.7, а не весь рушій.

[Справжній fixture 0.6](../tests/fixtures/world_save_v4_builds.ribsave) отримано незмінним старим writer до зміни схеми: 526972 байти, SHA-256 `394268f0e8e3efe8a31b22db99821ad1ccfce200939088ecb5697f465e35923a`. Він одночасно містить уже оплачений незавершений House і pending Farm. Міграція зберігає pending ID, створює фізичні резерви, не оплачує House повторно й доводить обидві споруди до завершення. Пошкоджені семантичні payloads перевіряються після повторного FlatBuffer/zstd/container checksum, тому тести доходять до перевірки світу. [Контракт і provenance](SAVE_FORMAT.md).

[SimRunner CSV](evidence/logistics-benchmark.csv) вимірює корисний видобуток усіма мешканцями та повернення каменю через портал до Camp своєї нації. Час нижче — мілісекунди лише `World::tick`; агрегування звіту поза таймером. Запуск виконано без одночасної збірки чи інших тестів; раніше відкрита користувачем гра 0.6 залишалася запущеною.

| Мешканці | Тактів | p95 tick | p99 tick | max tick | Доставлено Stone | Останні Complete / Failed |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 68 | 0,022 | 0,927 | 0,927 | 20 | 10 / 0 |
| 100 | 69 | 0,094 | 3,502 | 3,502 | 200 | 100 / 0 |
| 500 | 73 | 4,220 | 5,165 | 5,165 | 1000 | 500 / 0 |
| 1500 | 85 | 5,505 | 7,765 | 7,765 | 3000 | 1500 / 0 |
| 4×1500 | 236 | 7,649 | 9,385 | 14,532 | 12000 | 5057 / 943 |

Усі мешканці отримали XP, залишились живими, весь видобутий камінь потрапив у сховища; наприкінці pending paths = 0. Для 6000 піки становили 4096 активних завдань і 8192 одиниці вантажу. **943 завдання завершились Failed після доставки**, коли продовження збору отримало LimitExceeded (540) або TargetInvalid (403). Лічильники враховують останню фазу кожного ID, тому Complete → Failed не рахується двічі. Доставка всього матеріалу не означає успіх усіх продовжень роботи. Автоматичне повторення після звільнення черги залишається відкритим.

Fixture починає роботу біля порталу та використовує короткий маршрут і вичерпувану спільну жилу. Він не перевіряє колізії натовпу, довгі складні маршрути, повний AI, RAM/VRAM, render budget або несприятливу мережу. Ці результати не є повним performance gate з вихідного плану.

## Черга маршрутів 0.6

Поточні версії: protocol 3, rules 3, generator 2, save 4. Фінальна локальна Windows Release-збірка пройшла **33/33 CTest за 37,32 с** та встановила пакет; попереджень MSVC немає. [Журнал CTest](evidence/path-ctest.txt), повний build log — `.deps/path-warning-clean-build.log`. Перевірено спільний пошук для 1500 осіб, атомарність, обмеження черги та її відновлення після drain, topology restart/timeout, food reservation, відхилення підміненого стану та save/resume у Queued/Search/Reconstruct/Stage.

Початковий прогін мав 32/33: заблокована стартова клітинка табору помилково могла стати транзитною для інших мешканців. Виправлення залишає такий вузол кінцевим листом пошуку, дозволяючи лише вихід його власника. Наступний збій нового тесту ліміту був помилкою fixture: його ціллю був сам табір; тест використовує перевірену прохідну клітинку. Також виправлені лінійний пошук юніта за ID, перевірка координат перед їх індексацією, повторна валідація цілі збору перед commit і облік повідомлень у межі planner state.

`PathVisualProbe` повторно пройшов на фінальному SimCore: D3D12, **NVIDIA GeForce RTX 3050 Laptop GPU**, driver `32.0.15.9282`, software=false. Seed 70421, 1500 мешканців, ціль `(159,159,Surface)`: після збереження на першому такті пошуку оригінал і відновлений клієнт завершили його через однакові 109 наступних тактів; усі 1500 отримали активний наказ. Перевірено EN/UK/RU/PL/ES, стан очікування, типізовані Queued/Applied/Rejected і локалізовану відмову для ізольованої цілі. Переглянуто 11 різних кадрів, включно з компактним 1100×700; п'ять resume PNG ідентичні pending. Усі 16 фінальних PNG байт-ідентичні переглянутому набору. [GPU](evidence/path-gpu-device.json), [результати](evidence/path-gpu-results.json), [порівняння](evidence/path-visual-comparison.json), [український HUD](screenshots/paths/uk-pending.png).

[SimRunner CSV](evidence/path-benchmark.csv) окремо вимірює додавання наказів у чергу та всі такти до термінального результату саме поданих batch ID. Seed 70421, віддалена прохідна ціль в Mine, тобто маршрут через портал; час у таблиці — мілісекунди.

| Мешканці | Додавання наказів | Завершення, такт | p95 tick | p99 tick | max tick | Applied / Rejected |
|---|---:|---:|---:|---:|---:|---:|
| 10 | 0,004 | 8 | 2,721 | 2,721 | 2,721 | 1 / 0 |
| 100 | 0,020 | 15 | 3,262 | 3,262 | 3,262 | 1 / 0 |
| 500 | 0,061 | 41 | 2,614 | 2,775 | 2,775 | 1 / 0 |
| 1500 | 0,077 | 109 | 1,462 | 1,660 | 2,250 | 1 / 0 |
| 4×1500 | 0,721 | 434 | 3,069 | 4,701 | 6,145 | 4 / 0 |

Піковий лічильник роботи — 4096 за такт. Для 4×1500 peak planner state — 43 482 904 байти, staged cells — 409 255. `planner_bytes` є детермінованою оцінкою стану, а не піковим RAM процесу. Збережено [характеристики машини](evidence/path-test-machine.json). Fixtures не охоплюють складний AI, колізії натовпу, повний render budget, RAM/VRAM, несприятливу мережу або зовнішній Steam gate; ці вимоги залишаються відкритими.

Архів `dist/Ribterra-Windows-x64-0.6.0.zip`: **29 713 761 байт**, SHA-256 `50395326E199448431FCC783A4DB3172680FEC1824DECA96AF009502D862E2B0`. [Metadata](evidence/path-package.json). Його розпаковано в `artifacts/package-0.6.0-warning-clean`; GPU menu, MineVisualProbe з термінальними відповідями й save/resume, окремий server + два probes + графічний клієнт пройшли. [Мережевий результат](evidence/path-package-network.json), [шахта](evidence/path-package-mine.json). Три кадри пакета переглянуті: [меню](screenshots/paths/package-menu.png), [шахта](screenshots/paths/package-mine-resumed.png), [мережа](screenshots/paths/package-network.png). CI поточного коду наведено нижче; історичні результати 0.5 відокремлені.

Перший [CI 0.6 для `c710cf2`](https://github.com/fipsyatina/ribterra/actions/runs/34220670669) має Linux 21/21 за 16,02 с і UBSan 5/5 за 2,93 с; GCC повідомив про неявне групування `&&/||` та часткову ініціалізацію тестового ClientConfig. Уточнення дужок зберігає поведінку, а тест тепер явно задає ім'я після стандартної ініціалізації. Локальний прогін і smoke оновленого архіву вище виконані повторно після цієї зміни. Повторний CI після очищення попереджень завершився успішно.

[Фінальний CI для `1ef93e9`](https://github.com/fipsyatina/ribterra/actions/runs/34221544539) успішний: **Windows 33/33 за 34,13 с**, **Linux 21/21 за 15,76 с**, **UBSan 5/5 за 2,63 с**. Попереджень у C++ коді та UBSan runtime errors немає. [Metadata](evidence/path-ci-1ef93e9.json), [Windows](evidence/path-ci-windows-1ef93e9.txt), [Linux](evidence/path-ci-linux-1ef93e9.txt), [UBSan](evidence/path-ci-ubsan-1ef93e9.txt), [лічильники Linux](evidence/path-ci-linux-quality.json). Ці докази підтверджують рубіж 0.6, а не завершення всіх вимог рушія.

## Chunks, EnTT і шахта 0.5

`scripts/build.ps1 -SkipBootstrap` після виправлення переповнення в генераторі завершив Release build, **30/30 CTest за 32,60 с** і встановлення пакета без попереджень MSVC. Журнал: `.deps/chunks-overflow-fix-build.log`; [результат CTest](evidence/chunks-ctest.txt). Поточні версії: protocol 2, rules 2, generator 2, save 3. EnTT 3.16 реально зберігає компоненти мешканців у SimCore. Контрольний `SimRunner --ticks 160 --population 10 --seed 70421` до й після виправлення має однаковий checksum `9495560223965183407`.

Світ містить 128 наперед створених чанків 32×32: поверхня та шахта по 256×256 клітинок у координатах `[-96,160)`. Це обмежений світ, без streaming/unload та повного біомного генератора. Тести перевіряють від'ємні координати, межі чанків, читання без генерації, явний перехід через портал, тверду руду й перетворення в підлогу після вичерпання.

| Перевірка | Виконаний сценарій |
|---|---|
| `world_validation` | NaN/Infinity до перетворення координат, enums/UTF-8/ліміти, дублікати, неперервність маршрутів і порталів; відмова зберігає checksum; перестановка resident/route DTO не змінює наступні ticks; імпорт і tick для 4×1500 |
| `persistence` | Format 3 round trip і продовження; реальний незмінний сейв 0.4, format 2/rules 1/generator 1/точний content hash; overlay старих 0…47 на поверхню; `.v2.bak`, `.legacy.bak`; пошкоджені та змішані формати відхиляються |
| `protocol` | Full/delta chunk-знімки, 256 KiB wire bound, перестановки й однакові/конфліктні дублікати, 16 MiB assembly bound, timeout, неправильні портали, відхилення застарілого full; незавершена збірка не змінює видимий стан |
| `chunk_network` | Реальний GNS передає всі 128 чанків у **19 частинах**; late join, рух у шахту, відмова керувати чужою нацією, reconnect, save/resume; відкидання частин і timeout/resync відновлюють світ без reconnect; завчасний ACK розриває лише порушника |
| `server` | Початково відома рівно одна клітинка шахти — вихід порталу; інша руда редагується до порожнього hidden-значення; authority chunk revisions не потрапляють у проєкцію |
| `ui` / `client_input` | Surface/Mine та Passage intents, active tab, шар мешканця, п'ять мов, camera/view без мутації симуляції; старі HUD hit regions не працюють поверх RmlUi |

Схеми проходять `flatc --conform` проти базового сейва та окремо frozen format 2, network payload і frame. Файл [world_save_v2_current.ribsave](../tests/fixtures/world_save_v2_current.ribsave) створив справжній сервер 0.4: seed 424242, 6 мешканців, 37 ticks. SHA256: `a84c8db20201f00590cb3e5b312ac5b0a05371b78f8036313a2e7f5c166d360c`; очікування збережено поруч. Деталі міграцій — [SAVE_FORMAT.md](SAVE_FORMAT.md).

`MineVisualProbe.exe artifacts/chunks-0.5-fixed` пройшов на фізичному RTX 3050 Laptop через D3D12 після виправлення генератора. Нативна подія вибрала мешканця 1, сервер провів його в шахту, руда `(27,22)` зменшилася з 24 до 0, клітинка стала MineFloor, запас каменю став 88. На tick 1058 save/resume зберіг усі видимі chunks/portals/residents/buildings/resources і tick. SHA256 кадру збігається з раніше оглянутим кадром 0.5. [Результат](evidence/chunks-mine.json), [кадр після завантаження](screenshots/chunks/mine-resumed.png), [device](screenshots/chunks/device.json). Невідомий простір непрозоро чорний. `RenderFrameTests.exe --gpu` також повторно пройшов, включно з контрольованими відмовами text upload: [результат](evidence/chunks-gpu-contract.txt).

Окремий `process_session_test.py` знову перевірив dedicated server, два незалежні probes й графічний D3D12-клієнт з тимчасовим Unicode password: різні player ID, спільні 50 одиниць дерева, 10 мешканців, дві споруди, reply та чат. [JSON](evidence/chunks-network.json), [мережевий кадр](screenshots/chunks/network.png). Це loopback на одному ПК.

`UiVisualProbe` створив menu/settings/help/HUD для EN/UK/RU/PL/ES: EN 100%, інші 130%, додатково український HUD 1100×700. Усі 21 UI PNG оглянуто, разом із шахтою та мережею — 23 кадри. Кнопки шарів і проходу, статус, довідка й довгі переклади вміщуються; кирилиця й діакритики читаються. [Український HUD](screenshots/chunks/uk-hud.png), [компактний HUD](screenshots/chunks/uk-compact-hud.png), [довідка](screenshots/chunks/uk-help.png), [іспанські налаштування](screenshots/chunks/es-settings.png). Це автоматизовані нативні події та огляд framebuffer, без заяви про повний keyboard-only/IME/4K gate.

### Пакет Windows

`dist/Ribterra-Windows-x64-0.5.0.zip`: **29 596 671 bytes**, SHA256 `65052A3727502C897B5DD569D36CC2770009FE225D339C46F491C776014D3E3C`. [Metadata](evidence/chunks-package.json). До архіву входять клієнт, dedicated server, probe, SimRunner, DLL, перевірені assets та notices; вихідний DOCX і концепти не входять до runtime-пакета.

Архів розпаковано в новий каталог `artifacts/package-0.5.0-fixed`. Саме його `Ribterra.exe` успішно відкрив D3D12-меню; PNG має той самий SHA256, що оглянутий EN menu capture: `2AB2C19696D29FE17BD2EC3E08BF71A96E7C92620E5B916A449674949B74BF82`. Після цього сервер, два probes та графічний клієнт **із розпакованого архіву** пройшли повний process-сценарій зі збереженням. [Packaged device](evidence/chunks-package-device.json), [packaged network result](evidence/chunks-package-network.json).

### Початкові виміри масштабу

`SimRunner.exe --benchmark-chunks --ticks 160 --seed 70421` вимірює кожен tick окремо через steady clock; p50/p95/p99 — nearest-rank, без усереднення пакетів tick. Початкова побудова маршруту виміряна **окремо** й не входить у наведені tick percentiles. [Усі результати CSV](evidence/chunks-benchmark.csv).

| Сценарій | Мешканці | p95 tick, мс | p99 tick, мс | Початкова команда, мс |
|---|---:|---:|---:|---:|
| Рух через межу чанка | 10 | 0,0105 | 0,0120 | 1,533 |
| Рух через межу чанка | 100 | 0,0841 | 0,1179 | 10,352 |
| Рух через межу чанка | 500 | 0,2095 | 0,4030 | 51,765 |
| Рух через межу чанка | 1500 | 0,6620 | 0,9367 | 144,000 |
| Рух через портал | 4×1500 | 1,4478 | 3,7717 | 407,870 |

Масова команда перевищує бюджет тіку: BFS поки синхронний, потрібні бюджетовані запити та групове планування. 6000 осіб у цьому fixture мають чотири nation ID, але кооперативний клієнт і ресурси ще представляють одну спільну націю; це не доказ чотирьох повних економік або мережевих націй. Виміряно 160 простих ticks без повного AI, логістики, колізій натовпу чи бою. Ліміти RAM/VRAM, render time, довгі сценарії й фінальний performance gate не закриті.

### CI

Початковий код 0.5, commit `036b08b98422cb3ccbb48bf7675998d7611404fd`, [run 34213349737](https://github.com/fipsyatina/ribterra/actions/runs/34213349737) має **Windows success: 30/30 тестів за 26.88 с**, job `102019260338`. [Збережений результат Windows CI](evidence/chunks-ci-windows.txt). Linux job `102019260354` завершив **10/19, дев'ять збоїв** через `std::bad_alloc` під час створення світу; asset_tool уже проходить. [Збережений Linux failure](evidence/chunks-ci-linux-036b08b.txt).

У commit `d9e35cca06e02f7014fef902664b9d2d25c8084f` координати генератора перетворюються на `uint32_t` перед множенням, усуваючи signed-overflow UB. Додано три перевірки terrain на від'ємній і верхній межах, прибрано GCC warning про копіювання structured binding. [Новий run 34214848608](https://github.com/fipsyatina/ribterra/actions/runs/34214848608) завершився **success**. Windows job `102024081957` має **30/30 за 27.86 с**, Linux job `102024082232` — **19/19 за 15.06 с** у звичайній Release-збірці та **4/4 за 1.68 с** з UBSan (`sim_core`, `chunks`, `world_validation`, `persistence`) через preset `headless-ubsan`. [Windows CTest](evidence/chunks-ci-windows-d9e35cc.txt), [Linux CTest](evidence/chunks-ci-linux-d9e35cc.txt), [UBSan CTest](evidence/chunks-ci-ubsan-d9e35cc.txt), [статус run](evidence/chunks-ci-d9e35cc.json). У збірках немає compiler warnings або UBSan runtime errors. Подальший commit лише документації та доказів не змінює перевірений виконуваний код. Повний ASan/UBSan gate усіх підсистем залишається відкритим.

Для попереднього commit 0.4 [run 34185301168](https://github.com/fipsyatina/ribterra/actions/runs/34185301168) має Windows success і Linux failure: 14/15 тестів, лише `asset_tool`. Linux знаходив metadata-помилку uppercase `UI` раніше за колізію з `ui`. У 0.5 інструмент спочатку перевіряє всі casefold-шляхи; тест прибирає collision fixture перед незалежною перевіркою symlink. GCC false-positive про dangling reference у тестовому helper також усунуто через `string_view` і явний missing-entry check. Результат CI 0.5 слід читати з окремого актуального run після push.

## Історичні UI та AssetPipeline 0.4

Клієнт 0.4 використовував RmlUi 6.3, ICU 78.3 і FreeType 2.14.3. Меню, HUD, панель вибраного мешканця, чат і допомога завантажувалися з RML/RCSS; мова та масштаб зберігалися у налаштуваннях користувача. Симуляція була серверною, а світ — векторним прототипом 48×48.

`scripts/build.ps1 -SkipBootstrap` завершив Release build, встановлення та **26/26 CTest за 26.58 с**, без попереджень MSVC. Повний локальний журнал: `.deps/ui-build-final.log`; [збережений результат CTest](evidence/ui-ctest.txt). Нові залежності зафіксовано в manifest, правила/протокол/генератор/сейв не змінювалися цим UI milestone.

Додані перевірки:

| Тест | Що перевіряється |
|---|---|
| `platform` | Unicode імен/значень середовища, missing/empty, відхилення некоректних імен і Windows Unicode |
| `asset_catalog` | Зареєстровані шляхи, manifest identity, розміри, SHA256, пошкодження та вихід за межі каталогу |
| `texture_atlas` | Детерміноване пакування, padding/extrusion, UV/pivot, ліміти та неправильні входи |
| `asset_tool` | JSON/посилання/п'ять каталогів/походження/ліцензії/колізії шляхів |
| `localization` | ICU plural для п'яти мов, імена й типи аргументів, неправильні шаблони/UTF-8, pseudo expansion |
| `atlas_tool` | Реальний PNG export, Unicode paths, provenance notices, hashes, відмова без часткового output |
| `asset_staging` | Native ICU/RmlUi validation candidate; неправильний шаблон або відсутній UI ID зберігає попередній каталог |
| `ui` | Меню, налаштування, локалізація, HUD/вибір, focus/input та chat intents |
| `ui_lifecycle` | Відхилення неправильного RML/RCSS, unsupported filter та створення валідного UI після невдачі в тому самому процесі |

`LocalizationCheck` також перевіряє FreeType glyph coverage у всіх п'яти відвантажених каталогах. `RenderFrameTests` розширено текстурами з generation IDs, premultiplied alpha, повторним використанням слота й відхиленням застарілих handles. Мережевий process fixture використовує тимчасовий пароль з Unicode без виведення його значення.

На фізичному NVIDIA GeForce RTX 3050 Laptop GPU, D3D12, драйвер 32.0.15.9282, пройшов `RenderFrameTests.exe --gpu`: два примусові збої завантаження text texture відкидають незавершений cache, третій frame успішний. [Результат GPU contract](evidence/ui-gpu-contract.txt), [device](screenshots/ui/device.json).

`UiVisualProbe.exe artifacts/ui` створив 15 кадрів menu/settings/HUD для EN/UK/RU/PL/ES та додатковий compact HUD 1100×700. EN використовує 100%, інші мови — 130%. Усі 16 PNG оглянуто: тексти й контроли вміщуються, діакритики/кирилиця читаються, панелі не перекривають одна одну. Приклади: [меню](screenshots/ui/en-menu.png), [український HUD](screenshots/ui/uk-hud.png), [compact](screenshots/ui/uk-compact-hud.png), [польські налаштування](screenshots/ui/pl-settings.png), [іспанський HUD](screenshots/ui/es-hud.png).

Окремо `tests/process_session_test.py` запустив dedicated server, два незалежні probes та графічний D3D12-клієнт з Unicode password. Обидва гравці отримали різні ID, спільні 50 одиниць дерева, 10 мешканців, дві споруди, reply і чат; сервер зберіг світ. [JSON результат](evidence/ui-network.json), [кадр мережевого чату](screenshots/ui/network.png). Невідома територія непрозоро чорна. Це loopback на одному ПК, не фізична LAN-сесія між двома машинами.

`scripts/package.ps1 -SkipBuild` створив `dist/Ribterra-Windows-x64-0.4.0.zip`: **29 383 357 bytes**, SHA256 `EA67BE3D7F71CE69BDE39FA81CE43C51145C7C578E4B1A8FF9689B9CBE6B2735`. Архів розпаковано в окремий каталог `artifacts/package-0.4.0-smoke`; його `Ribterra.exe --menu-capture ... --renderer-info ...` завершився з кодом 0 на D3D12. PNG меню має той самий SHA256, що оглянутий build capture. [Package metadata](evidence/ui-package.json), [packaged device](evidence/ui-package-device.json). Archive не містить вихідного DOCX або концептів.

Це автоматизовані native-перевірки та перегляд framebuffer. Повний keyboard-only/IME тест людиною, 1440p/4K, відеоналаштування й усі заплановані екрани ще не підтверджені. AtlasTool є offline інструментом; production artwork і runtime hot reload ще не підключені.

## Історична локальна збірка 0.3

Команда `scripts/build.ps1 -SkipBootstrap` завершила конфігурацію, Release-збірку, CTest та встановлення пакета. Вихідний каталог CMake: `build/engine-windows`; пакет: `dist/Ribterra`; журнал: `.deps/gpu-build.log`.

CTest: **17/17 пройдено**, 0 невдалих, загальний час 19.67 с у фінальному запуску після виправлення Linux ABI та очікування snapshot у тесті. У журналі MSVC немає попереджень компілятора. Крім 14 тестів фундаменту нижче, новий milestone додає:

| Тест | Що фактично перевірено |
|---|---|
| `shader_contract` | DXIL, JSON reflection і manifest відповідають HLSL entry points та очікуваним bindings |
| `shader_negative` | Зламаний shader fixture відхиляється validator без підміни валідного output |
| `render_frame` | Явний software backend, painter order geometry/text, alpha, clip, resize capture та letterbox coordinate round trip |

## Історична основа 0.2

Команда `scripts/build.ps1` завершила pinned bootstrap, конфігурацію, Release-збірку, CTest та встановлення пакета. Вихідний каталог CMake: `build/engine-windows`; пакет: `dist/Ribterra`. Зафіксований журнал: `.deps/engine-build.log`.

CTest: **14/14 пройдено**, 0 невдалих, загальний час 20.81 с. У журналі MSVC немає попереджень компілятора.

| Тест | Що фактично перевірено |
|---|---|
| `sim_core` | Детермінований світ, IDs, рух, збір, будівництво, їжа/сезон і межі поточної симуляції |
| `persistence` | FlatBuffers+zstd format 2, точний round trip/resume, bounds/checksum/semantic rejection, backup rotation, missing-primary recovery і one-way legacy migration |
| `server` | Authenticated player source, observer/spoof rejection, request dedupe/conflict, порядок команд, права на snapshot, reconnect без дубля мешканців, chat limits/history/mute |
| `protocol` | FlatBuffers frame round trip, invalid/truncated/oversized data, full/delta apply, removals, wrong-baseline resync і password-proof binding |
| `transport` | Два реальні standalone GameNetworkingSockets endpoints у loopback, reliable/unreliable delivery, lanes, disconnect, message limit і зайнятий порт |
| `save_schema_conform` | `flatc --conform` для поточної й baseline-схеми сейва |
| `network_conform` | `flatc --conform` для payload-схеми мережевого протоколу |
| `network_frame_conform` | `flatc --conform` для зовнішньої compressed-frame схеми |
| `sim_boundary` | Рекурсивна відсутність platform, presentation, transport і server залежностей у публічних заголовках та реалізації SimCore |
| `network_session` | Password success/failure, двоє клієнтів, одна authoritative world state, full/delta sync, один in-flight baseline, reconnect cursor і replay reply до batched ack |
| `dedicated_processes` | Окремий `RibterraServer` і два незалежні `RibterraNetProbe` процеси; різні player IDs, спільні ресурси, дві споруди, чат і server save у тимчасовому шляху з кирилицею |
| `client_input` | SDL-вікно 1100×700, logical-coordinate input, UI-команди через embedded GameServer, чорна невідома область і save/resume |
| `client_smoke` | Запуск і framebuffer capture локального клієнта з SDL dummy/software backend |
| `network_client_rendered` | Окремий dedicated server і графічний клієнт, remote full/delta sync та framebuffer 1100×700 |

Команди, які були виконані або входили в успішний build/test run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
.\dist\Ribterra\RibterraServer.exe --help
.\build\engine-windows\SimRunner.exe --ticks 1 --population 10 --seed 70421
```

Сценарій `tests/process_session_test.py` задає випадковий пароль лише в середовищі дочірніх процесів і не друкує його. Він резервує локальний UDP-порт, запускає server і clients як окремі процеси, перевіряє JSON-результат, збережений світ та завершення сервера.

Перевірка imports для `RibterraServer.exe` показала лише системні Windows DLL (`SHELL32`, `WS2_32`, `CRYPT32`, `WINMM`, `IPHLPAPI`, `KERNEL32`, `USER32`, `ADVAPI32`, `dbghelp`). Dedicated executable не імпортує SDL, GPU або Steam client runtime.

Кадр віддаленої сесії: [network-session.png](screenshots/network-session.png). Він отриманий із framebuffer реального `Ribterra.exe` після синхронізації з окремим `RibterraServer`; оглянуто 1100×700, непрозору чорну невідому область і спільний стан після команд двох CLI-клієнтів. Вхідні події та середовище були автоматизовані. Це не є ручним тестом людського вводу або сесією на двох фізичних ПК.

GitHub Actions run `34177929620` для попереднього 0.2 commit мав Linux failure. Його виправлення підтверджено наступним run нижче.

Причина Linux failure: `std::int64_t*` має тип `long*`, тоді як `ISteamNetworkingSockets::SendMessages` вимагає бібліотечний `int64*` (`long long*`). У 0.3 використано точний тип API. Обидва jobs [run 34180370872](https://github.com/fipsyatina/ribterra/actions/runs/34180370872) для commit `46df17f555ce98697b331ecc4377e98f5287e719` успішні: Windows `101918059824`, Linux `101918059974` (11/11 тестів, 9.51 с), 2026-09-08. Це результат 0.3, а не CI-доказ поточних змін 0.4. Мережевий тест також продовжує server ticks під час очікування snapshot після ack; його окрема повторна перевірка пройшла 5/5 запусків без зміни timeout або кінцевих assertions.

## SDL_GPU/D3D12 milestone 0.3

У коді є backend-neutral `Frame2D`, `Renderer2D`, явний software test backend, D3D12 selection без fallback, offscreen capture, letterbox coordinate conversion і build-time shadercross pipeline. Пакет містить `assets/shaders/sprite.vert.dxil`, `sprite.frag.dxil`, відповідні JSON reflection-файли та `manifest.json`; `shadercross`, DXC і SPIRV-Cross є лише build tools.

`RenderFrameTests.exe --gpu` пройшов на фізичному NVIDIA GeForce RTX 3050 Laptop GPU, backend `direct3d12`, драйвер 32.0.15.9282. Перевірено geometry/text painter order, alpha, clip, letterbox round trip і offscreen captures 1440×900 та 1100×700. Окремо пройшов сценарій dedicated server + два `RibterraNetProbe` + графічний D3D12-клієнт. Візуально оглянуто перше поселення, compact frame, розвинене поселення й мережевий кадр; невідома область лишається непрозоро чорною.

Артефакти: [GPU device](screenshots/gpu-device.json), [перше поселення](screenshots/gpu-first.png), [compact 1100×700](screenshots/gpu-compact.png), [розвинене поселення](screenshots/gpu-developed.png), [мережева сесія](screenshots/gpu-network.png). Поле `software:false` у DeviceInfo описує обраний backend; фізичний GPU тут підтверджено також назвою пристрою та фактичним D3D12-запуском. `--gpu-debug` лишається діагностичним прапорцем, а не окремим performance доказом.

Software capture cropping виправлено; фінальний локальний CTest 0.3 включає успішний `render_frame` і входить до результату 17/17 вище.

## Перевірена поведінка безпеки даних

Неавторизований або відключений ID отримує порожній snapshot. Невідома клієнту клітинка серіалізується як порожня й відображається суцільним непрозорим чорним; приховані будівлі не входять до snapshot. Мережа не серіалізує повний `WorldState`, seed або серверні маршрути.

Декодування перевіряє FlatBuffers, версії, content hash, розміри стиснених і розпакованих даних, колекції та семантику до застосування стану. Тести змінюють заголовки, checksum, baseline, world ID, довжини, enum/range значення та обрізають дані.

Сейв записується в `.tmp`, повторно читається й порівнюється перед заміною. Попередній валідний слот переходить у `.bak`; missing primary відновлюється з нього. Наявний пошкоджений primary не викликає тихого rollback. Фізичне вимкнення живлення під час запису не виконувалось.

## Історична M0-проба навантаження

Версія 0.1.0, seed 70421, 300 вимірюваних ticks після прогріву. Це вимірювання лише спрощеного тодішнього SimCore без рендера, повного AI, мережі, бою та чотирьох націй.

| Мешканці | Сценарій | p95 tick, мкс | p99 tick, мкс | Формування масового наказу, мс |
|---|---|---:|---:|---:|
| 10 | Рух | 0.240 | 0.260 | 0.248 |
| 100 | Рух | 2.160 | 2.160 | 2.742 |
| 500 | Рух | 11.260 | 11.380 | 12.185 |
| 1500 | Рух | 65.520 | 66.080 | 41.411 |

Синхронне формування маршрутів на 1500 мешканців коштувало близько 41 мс. Ці історичні числа не підтверджують 4×1500, фінальний AI, 60 FPS або заявлені системні вимоги.

Історичні локальні кадри M0: [перше поселення](screenshots/first-settlement.png) і [побудоване поселення](screenshots/developed-settlement.png).

## Неперевірені межі

Не перевірено SDL_GPU/D3D12 performance budget, shader hot reload/fallback, повний UI/IME/keyboard-only gate, аудіо, chunk streaming і повний генератор, повний AI/логістику/бій/суспільство, повний ігровий контент п'ятьма мовами, LAN discovery, Steam, durable reconnect після server restart, autosave ring, crash/power-cut fault injection, macOS, 2–4 фізичні ПК, latency/loss/long-soak і фінальні performance/memory gates.

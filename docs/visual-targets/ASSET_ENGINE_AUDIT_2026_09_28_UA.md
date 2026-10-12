# Ribterra — інвентар графіки й можливості рушія

Аудит 28.09.2026: `bdc53e8`, версія 0.26.0, робоче дерево містить незакомічену заготовку LightingMode та документи. Це перевірка коду, JSON і PNG-заголовків; новий графічний executable у цьому аудиті не створювався.

[Повний машинний реєстр із SHA256, розмірами й кожним sprite ID](verification/assets-audit-2026-09-28.json). Відтворення: виконати Node-скрипт [.omx/plans/audit-graphics-assets-2026-09-28.cjs](../../.omx/plans/audit-graphics-assets-2026-09-28.cjs) із checkout. Скрипт читає ассети, перевіряє прямокутники та записує тільки цей звіт. Він не перевіряє якість декодування всіх пікселів або вигляд у грі.

## Наявність не дорівнює використанню

[GameArt::load](../../src/client/game_art.cpp:72) читає всі сторінки `assets/art/world.json`, декодує в RGBA, застосовує magenta/luminance alpha та реєструє спрайти. Кандидат замінює старий комплект після успішного завантаження; дубль ID — помилка. Це не готовий інтерфейс гарячого перезавантаження всього контенту гри.

| Група | Фактичний обсяг | Як використовується / що врахувати |
|---|---|---|
| Світовий runtime комплект | 42 PNG-сторінки; 622 записи, 622 унікальні ID | Усі завантажуються; не всі вибираються під час гри. Source world.json і build/engine-windows/assets/art/world.json зараз мають однаковий SHA256 |
| Старі atlas-0…3 | 4 × 2048²; 110 записів | Серед них 10 станів townhall/blacksmith, яких немає в чинному каталозі; інші групи не можна видаляти разом із ними |
| refresh будівель | 20 × 1536×1024; 120 записів | По stage1/2/3, ready, winter, level2. `.level2` не вибирає чинний [selector](../../src/client/view.cpp:661). Це 20 завантажених, але не підключених станів |
| refresh-materials | 1254²; 16 матеріалів | Земля/камінь/вода; не normal/roughness maps |
| refresh-grass / trees / rocks / detail | 6 / 12 / 16 / 14 записів | Варіанти декору; поточний view використовує лише частину detail. Наявність текстури скелі не створює непрохідний схил |
| refresh-effects | 8 luminance-записів | М'які накладки, дим, вогонь, відблиски; не фізичні світло й вода |
| Старі refresh-walk | 2 × 16 записів | Резервна анімація; основний мешканець використовує directional family |
| Тварини | 32 записи | Літо/зима, чотири види × чотири фази; потрібні тільки наявні тварини |
| Directional residents | 8 PNG × 1774×887; 256 записів | man/woman × walk/logs/sack/cart × 8 напрямків × 4 фази. Вантаж бере [resident_art](../../include/ribterra/resident_art.hpp:16) зі справжніх carried items |
| UI PNG | 83 файли | 19 building icons, 14 glyphs, 30 icons/cursors, 16 ui-kit, 4 menu/lobby images; це окрема група від світових атласів |
| UI шрифти | NotoSans-Regular.ttf, CormorantGaramond.ttf | [Завантаження](../../src/ui/game_ui.cpp:687); цифри й довгі пояснення Noto, заголовки Cormorant |

20 family будівель: camp, house, farm, stockpile, mine, workshop, temple, tavern, cartographer, research, smelter, fishing, library, barracks, bridge, lumbermill, mill, kitchen, claypit, road. Чинні [alpha rules](../../include/ribterra/alpha_rules.hpp:12) дозволяють збудувати 12 типів; після вилучення Road — 11. Camp — окремий стартовий об'єкт. Наявність temple/barracks art не дозволяє повертати заблоковану механіку в демо.

### Пам'ять

Сума `width × height × 4` світових сторінок: **306 197 408 bytes = 292,01 MiB**. UI PNG: **31,52 MiB**, якщо всі декодовані одночасно. Це арифметичний розмір повних RGBA8-зображень, **не виміряний VRAM процесу**: реальна пам'ять також залежить від UI crop-копій, upload buffers, render targets, driver alignment та одночасного старого/нового покоління. GameArt декодує сторінки послідовно; не стверджуємо, що всі CPU-копії одночасно живуть у RAM.

Плановий бюджет 256 MiB із попереднього production spec уже менший за базовий світовий комплект. Отже, це ціль оптимізації, а не доведена властивість. Перед normal/depth/wet/snow layers потрібні ledger і вимірювання піку. Не множити весь комплект на 3–4 карти матеріалів автоматично.

### Уже є потрібні рамки

`ui-kit.0.png` і `ui-kit.4.png` візуально перевірені: темна синя середина, металева золота рамка й декоративні кути. [theme.rcss](../../assets/ui/art/theme.rcss:1) визначає six nine-patch decorators. Але підключений пізніше [concept.rcss](../../assets/ui/concept.rcss:2) задає `decorator: none` для карток і кнопок. Спочатку відновити рамки **для нового HUD через scoped selectors**, потім оцінювати потребу в нових зображеннях. Глобально змінювати весь menu/lobby каскад небезпечно.

### Production sources

Папки `art-source/production-v1…v4`, `reference-refresh`, `runtime-0.11.1/export-*` — вихідні листи, промпти й експорти. Вони не є доказом наявності редагованих 3D-моделей, анімаційного рига чи синхронного експорту матеріальних карт. Кожний master потребує паспорта походження й перевірки камери. Копія файлу в `art-source` не означає, що гра його читає: джерело runtime — asset registry та world.json.

## Рушій: підтверджена межа

| Область | Наявне | Потреба наступних оновлень |
|---|---|---|
| Renderer | C++ / SDL_GPU, [Frame2D](../../include/ribterra/render_frame.hpp:15), painter order, текстури, scissor, чотири blend modes | V01–V03 можна будувати на поточному шляху. `Pass` — логічна категорія, не автоматичний GPU render graph |
| Шейдер | [texture × vertex color](../../shaders/sprite.frag.hlsl:13), [один color target без depth](../../src/render/renderer2d.cpp:473) | Немає багатоканального матеріалу, normal lighting чи depth-aware mist |
| UI | [RmlUi 6.3](../../CMakeLists.txt:238), справжні controls, font glyphs, nine-patch | Новий HUD потребує компонування, подій і контролю каскаду; нового рушія не потребує |
| Геометрія | [1440×900 logical](../../src/client/view.cpp:1611), shared terrain corners, height sampling | Для V01 зберегти current projection/letterbox. Новий адаптивний world viewport не вважати готовим |
| Освітлення | [нічний color grade](../../src/client/view.cpp:1625), теплі sprite overlays | Світло із намальованого вікна не освітлює підлогу/мешканця; full Dynamic відкладений |
| Тіні | Контактна будівельна накладка; [fallback тінь мешканця](../../src/client/view.cpp:829) | Art-ready branch повертається раніше. V03 додає окрему contact shadow, не обіцяє рухомої тіні від джерела |
| Вода | [анімація fx.4 і серпанок](../../src/client/view.cpp:1032) | V02 — читабельне русло/береги/накладки. Flow/reflections/physics не виникають від нового atlas |
| Погода | [календар сезонів](../../include/ribterra/seasons.hpp:10); winter sprite; температура зараз [порожня](../../src/ui/game_ui.cpp:1240) | V04 — weather state, forecast, save/net; V05 — покрив/танення. Готових температури й прогнозу немає |
| Снігові сліди | У world.json немає footprint family | Окремі stamps + bounded cache + walking events; не деформація геометрії |
| Фізика | Ігрові рух/прохідність/робота | Повний rigid-body/cloth/fluid solver не входить у ці оновлення |
| Dynamic scaffold | [API/status](../../include/ribterra/renderer2d.hpp:18), [відхилення Dynamic](../../src/render/renderer2d.cpp:260) | `effective=Legacy`, `dynamic_available=false`; нового світла й перемикача немає |

У manifest немає окремих normal/depth/roughness/emission/material/snow-mask/flow/footprint map contracts. Пікселі теплого вікна в кольоровому PNG не замінюють emission mask. Це вимоги до майбутнього контенту й loader, не «вже наявні текстури».

## Що робити з результатом

Детальні картки V01–V05 і відкладеного DL: [поточний план](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md). Кожний етап використовує тільки потрібну частину інвентарю. Старі atlas/orphan IDs видаляти лише після повного аналізу посилань і package-тестів, окремим diff; не чистити їх наосліп під час зміни UI.

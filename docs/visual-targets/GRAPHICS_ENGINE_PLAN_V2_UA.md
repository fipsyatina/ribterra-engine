# Ribterra — виправлений план графіки: рушій, ассети, світ і перевірка

**Поточний порядок базової графіки:** [V00–V05 — старий вигляд → UI → земля → об’єкти → дощ → зима](GRAPHICS_PROGRESS_UA.md). Це головна послідовність видимих змін після відкладення Dynamic. Старі dev/GW/DL — технічні пакети, не готові знімки кожної версії. [Галерея прогресу](index.html); [старі 13 публічних targets](public-versions.html).

**Останнє рішення 27.09:** повне динамічне освітлення та перемикач відкладені. Зараз у коді лише безпечна заготовка. [Майбутній план DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) має пріоритет: Dynamic не є блокером базового оновлення світу/UI; його light/occlusion gates застосовуються лише до майбутнього ввімкненого режиму.
Дата: 27 вересня 2026. Редакція: **V2**. Статус: **план реалізації; нові ефекти не реалізовані й не виміряні**.
Основа: checkout `C:\Users\fipst\Desktop\ribterra`, HEAD `bdc53e8`, 0.26.0. Документ виправляє недостатню технічну деталізацію плану від 26 вересня.

## 1. Що виправлено і який документ головний
Попередні макети показували світлові плями, мокрі відблиски, тіні, серпанок і воду, але формулювання «узгодити світло/переглянути воду» не визначали їх реалізацію. Це було недостатньою специфікацією. Цей документ зберігає аудит і технічні пакети; **поточний обсяг і порядок задає [план 28.09](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md)**, [ядро рендеру](RENDER_CORE_SPEC_UA.md) та [ефекти світу](WORLD_FX_SPEC_UA.md) — алгоритми й контракти, [виробництво ассетів](ART_PRODUCTION_SPEC_UA.md) — необхідний контент.

[Старий план світу/UI](../../.omx/plans/2026-09-26-graphics-world-ui-update-ua.md) зберігає ігрові вимоги та пакети GW; [специфікація світу/UI](../../.omx/plans/2026-09-26-world-ui-implementation-spec-ua.md) — генерацію, path/build/UI; [погода](../../.omx/plans/2026-09-26-weather-implementation-spec-ua.md) — авторитетну симуляцію. Нижче збережені **24 технічні пакети повного початкового обсягу**; це не черга обов’язкових релізів. Публічні версії 0.26.1–0.27.0 не перенумеровуються.

Картинки S01–S11/W01/W02 залишаються незмінними художніми референсами. Нативні вимірювання й письмові межі мають пріоритет над їхніми неточностями. Немає гарантії попіксельного відтворення AI-зображення. Приймається конкретно описаний вигляд і поведінка у грі.

## 2. Підтверджена база рушія
Ці твердження перевірені в коді; вони не є новими runtime-тестами.

| ID | Наявне | Реальна межа |
|---|---|---|
| E01 | [Frame2D](../../include/ribterra/render_frame.hpp:15) має позицію 2D, UV, колір, texture handle і painter order. | Немає material/light/receiver контрактів. |
| E02 | [Фрагментний шейдер](../../shaders/sprite.frag.hlsl:11) повертає texture × color; [pipeline](../../src/render/renderer2d.cpp:437) має один color target без depth target. | Нормалі, wet specular, відбиття, світлова видимість не виникнуть від заміни PNG. |
| E03 | [Night grade](../../src/client/view.cpp:1625) затемнює vertex colors, після нього [fx.3](../../src/client/view.cpp:1121) малює теплі плями. | Це не проєкція конкретного вікна й не реакція матеріалу на світло. |
| E04 | [Будівлі](../../src/client/view.cpp:688) мають темну контактну накладку; [спрайтові мешканці](../../src/client/view.cpp:796) не отримують окремої автоматичної тіні. | Немає динамічного напрямленого силуету тіні. Не плутати rock fallback на рядку 623 з будівлею. |
| E05 | [Вода](../../src/client/view.cpp:1032) використовує анімовані спрайтові накладки; [поверхня](../../include/ribterra/terrain_surface.hpp:8) має спільні кути. | Немає flow material, відбиттів і фізики води. Один рівень води зберігається. |
| E06 | [Terrain appearance](../../include/ribterra/terrain_appearance.hpp:62) і ground mist приховують frontier. | Fog of war не дорівнює художньому серпанку між планами. |
| E07 | [UnitAnimation](../../include/ribterra/unit_animation.hpp:15) має 8 facing і 4 фази ходьби; [вантаж](../../include/ribterra/resident_art.hpp:8) визначається справжнім carried stack. | Немає foot IK/footstep history/deformation. Наявний Cart — вид перенесення вантажу, не новий транспорт. |
| E08 | [GameArt loader](../../src/client/game_art.cpp:70) читає schema 1 і atlas pages до 4096²; [shader contract](../../tests/shader_contract.py:31) жорстко очікує поточні bindings. | Loader, shader build, reflection tests і packaging потрібно змінити разом із новими матеріалами. |
| E09 | [Compose](../../src/client/view.cpp:1610) задає logical 1440×900; [main](../../src/client/main.cpp:589) компонує RmlUi окремо. | Потрібен єдиний viewport/DPI contract; 16:9 картинки не доводять існуючий 16:9 layout. |
| E10 | [Сезони](../../include/ribterra/seasons.hpp:8) і зимові art states уже існують. | Дощ/снігопад/буря, температура й accumulation — майбутні погодні системи. |

На цій машині 27.09 виявлені NVIDIA GeForce RTX 3050 Laptop GPU та AMD Radeon Graphics. Це інвентар обладнання, **не FPS-тест і не автоматично мінімальні вимоги**. Dev.01 має зафіксувати фактично обраний GPU, driver, power mode, executable і resolution.

## 3. Рішення щодо архітектури
**Робоче рішення: розвинути чинний 2.5D renderer SDL_GPU/D3D12 із фіксованою камерою.** Світ лишається тайловою симуляцією; будівлі й мешканці — керовані спрайти з окремими метаданими. Нові light/material/decal/water passes є реальною роботою в рушії.

| Варіант | Перевага | Втрата/ризик | Рішення |
|---|---|---|---|
| Лише нові PNG і старі fx.3 | Мала зміна коду | Не дає світлової видимості, мокрих матеріалів, відбиттів і слідів | Відхилено як недостатній для цілі |
| 2.5D + surface materials, projected lights/shadows, bounded FX | Зберігає sim/net/UI/камеру, дає потрібні видимі ефекти у вузькій постановці | Потрібні authored masks/occluders, не довільна 3D-геометрія | Обрано з перевіркою dev.09 |
| Повна 3D-сцена/інший рушій | Дозволяє загальні 3D light/physics системи | Нові моделі/анімації/інтеграція/picking/render, великий ризик для демо | Не починати без провалу доказового 2.5D зрізу |

Використання 3D master-моделей для **офлайн експорту 2D ассетів** не є переходом гри на 3D. Це спосіб узгодити масштаб, напрям камери, маски й анімацію.

Dev.09 — перевірка обраного рішення, а не церемонія. Якщо світло/тіні/відбиття помітно неправдиві на допустимому схилі або бюджет не досягається, масовий export зупиняється. Записати конкретну проблему; виправити модель, або окремо переглянути обсяг/архітектуру. Не називати вимкнений обов'язковий ефект успіхом.

## 4. Які ефекти точно входять і якою ціною
| Ефект | Обов'язкова реалізація | Потрібний контент/дані | Що НЕ обіцяється |
|---|---|---|---|
| Світло з вікна | Ground receiver освітлюється за position/radius/cone/intensity; предмет перекриває промені; emission вікна окремо | Window anchors, cone, emission mask, occluder footprint/height | GI, ray tracing; фасади/мешканці — майбутній Dynamic за окремим DL-планом |
| Тіні | Контактна тінь + напрямлена силуетна проєкція для будівель/дерев/мешканців; реагує на погоджений light direction | Caster silhouettes/height, receiver triangles, sunlight direction | Точні самотіні всіх деталей, довільна 3D тінь на будь-якій поверхні |
| Мокра земля | Rain-driven wetness, normal/roughness response, світлові відблиски лише на придатній поверхні | Ground normal/material mask, dry/wet response | Дзеркальне відображення всієї сцени в кожній калюжі |
| Річка | Flow UV, рябь, shore foam, світловий блиск; обмежена проєкція відображених спрайтів | Water/shore/flow masks, eligible reflection casters | Fluid solver, SSR, водоспади, повінь, плавучість |
| Серпанок | World-anchored depth bands з поступовим tint/extinction, розділення планів | World depth, revealed mask, bounded noise | Фізичне об'ємне розсіювання та світлові промені крізь весь світ |
| Дощ/сніг/вітер | Авторитетна погода керує частинками, шлейфом/поривами; дах/світ/UI/FoW мають коректні маски | WeatherFxFrame, roof/ground receivers, particle atlases | Фізика атмосфери, температурна шкода, утеплення |
| Сліди кроків | Лівий/правий cosmetic decal за реальним переміщенням; залишається, старіє, засипається/тане | Snow receiver, movement distance, bounded local cache | Зміна elevation/collision, мережевий журнал кожного сліду |
| Втиснутий сніг | Локальна shade/normal-маска сліду створює неглибокий рельєф на вигляд | Footprint normal/alpha, global snow cover | Реальна геометрична деформація снігу |
| «Жива фізика» вигляду | Синхронні кроки/вантаж/робочі рухи, дим і рослинність із wind response | Узгоджені кадри, pivots, job/cargo state | Rigid bodies, cloth solver, ragdoll, руйнування |

Фраза «детальна фізика» не є acceptance criterion. Якщо потрібна справжня фізична взаємодія предметів, це окрема система, не прихована частина графічного оновлення.

## 5. Загальні контракти інтеграції
1. SimCore/сервер володіє temperature, precipitation, wind, cover, work modifier, calendar. Renderer не змінює правила роботи, їжі, HP чи маршруту.
2. Один presentation clock із simulation tick interpolation, явними pause/speed/epoch. До dev.14 тестова погода дозволена тільки у позначеній diagnostic fixture. У production HUD до authoritative path погодні числа приховані.
3. Ground receivers використовують ту саму поверхню, що feet/picking/build preview. Ground light, shadow, wetness і decals не проєктуються довільним екранним овалом.
4. Light/occluder/reflection/particle/decals відсікаються за revealed state **до** підготовки effect buffers. Фінальний FoW не повинен лише замальовувати вже витеклі відбиття.
5. Ручної Road, road bonus і прихованої auto-road немає. Footprints короткочасні й не змінюють швидкість.
6. Blend/linear/sRGB/alpha, render target formats, ping-pong і ресурсна тривалість визначені у Core spec. Не читати target у shader, поки той самий subresource є render target.
7. Мінімальна графічна якість зменшує кількість/роздільність, зберігаючи всі обов'язкові категорії ефектів. Відмова GPU allocation/shader — діагностована помилка/безпечне відновлення, не прихований «low mode» із зарахованим acceptance.
8. Старі fx.3/нічне затемнення для переведених об'єктів прибираються з відповідної гілки, щоб не отримати подвійне світло/тіні. Старий software diagnostic не видається за перевірений D3D12 шлях.
9. Виробничі тести RmlUi, resource lifetime, scissor/clip і GPU shader interface розширюються разом із renderer; зелений старий shader test для texture×color нічого не доводить про нові passes.

## 6. Контрольна сцена і вимірювана ціль
**Proposed fixture** `visual_slice_v2`: будинок із 2 вікнами, склад, майстерня, 1 дерево, 1 камінь-occluder, 3 рухомі мешканці (порожній/колоди/мішок), річка з двома берегами і мостом, рівна ділянка та один підтриманий схил. 10 мешканців для ігрового прогона; 20–30 для навантаження. Це diagnostic scene, не звичайний старт.

Dev.01 створює її через чинний World/save lifecycle і зберігає seed/save hash, world origin, layout, camera center, zoom, native/render/logical sizes. Якщо fixture потребує test helper, він окремий; зміна правил або збереження не приховується. Матеріали/об'єкти проходять production renderer, а не окремий showcase shader.

Контрольні стани: літо день/ніч; сухо→дощ→висихання; зима без опадів→сніг→буря→ясно; рух unit перед/за house/tree/bridge; terrain slope; revealed frontier; pause/2×/load/reconnect. На dev.09 це render fixtures; після dev.18 — той самий вигляд від справжньої погоди.

| Gate | Конкретний доказ |
|---|---|
| LIGHT | Увімкнути одне вікно: його emission і пляма з'являються разом; вимкнути — зникають. Посунути occluder: тінь у плямі рухається; поза radius/cone немає освітлення. Камера pan/zoom не пересуває світло відносно землі. |
| SHADOW | Мешканець і вантаж рухаються: окрема тінь прив'язана до їхнього anchor, змінює силует/напрям; не залишається тільки блакитне selection circle. Будівля/дерево затінюють правильний receiver без виходу на дах/UI/невідоме. |
| WATER | Відео показує рух ряби й піни; reflection рухається/гасне з caster, не накриває берег/міст; відключення caster видаляє відображення. Водоспадів немає. |
| WET | Та сама земля суха/мокра при тих самих lights: мокрі ділянки мають контрольований specular response; UI й сухий дах не стають блискучими. |
| HAZE | Два однакові об'єкти на різній world depth мають різну контрольовану attenuation; pan не прив'язує haze до екрана, ближній unit залишається читабельним. Hidden object не видається силуетом. |
| SNOW | Після проходу мешканця залишаються послідовні L/R сліди; standing/pause/teleport не штампують нові; сліди зникають від віку/засипання/танення й не з'являються на воді/даху/мості без snow receiver. |
| INPUT | Візуальний FX не змінює picking або commands; частинки й тіні не отримують hit target. UI читається й не потрапляє в world grade. |
| ART | 5–7 споруд читаються як один комплект: проєкція, масштаб, контакт, деталізація, матеріали й світло. Окремий красивий PNG не приймається замість сцени. |

Для spatial checks на native 1080p контрольному zoom використовувати debug receiver/occluder overlays й контрольні точки: стартовий допуск anchor drift ≤2 physical pixels на нерухомій поверхні; leakage поза маскою, крім ≤1 pixel AA boundary, — fail. Це **запропоновані пороги для proof**, їх не називати виміряними.

Художній критерій оцінюється окремо від correctness: 0/1/2 за композицію, масштаб, матеріали, контакт, світло, воду, атмосферу, рух, UI, читабельність. Dev.09: ≥16/20 і жодного 0; фінальна 0.26.1: ≥18/20 і жодного 0. Оцінювач не автор ассетів. Бал не замінює відео й не приховує проваленого функціонального критерію.

## 7. Довідкові24 технічні пакети повного графічного напряму
Це деталізація попереднього повного обсягу, включно з відкладеним освітленням. Поточний порядок базової гілки — V00–V05, майбутнього Dynamic — DL-01–08. Номери нижче збережені як cross-reference work packages; вони більше не означають24 обов’язкові послідовні збірки0.26.1.

| Dev | Результат і що змінюється | Основні місця | Обов'язковий вихід / залежність |
|---|---|---|---|
| 01 | Baseline, fixture, camera/scale/perf, capability inventory | view, renderer, існуючі capture/probe tools | Manifest + реальний відеобейзлайн; нічого не «покращено» |
| 02 | Material/pipeline/resource contracts, shader reflection build, linear/alpha path | render_frame, renderer2d, shaders, cmake, shader tests | Offscreen sample/composite, resize/minimize/device/allocation failure; E01/E02/E08 |
| 03 | Малий asset kit + metadata/windows/casters/receivers | GameArt, world.json, art exporters | House/store/workshop/unit/tree/ground/water masks; без масового export |
| 04 | Узгоджені receiver triangles, occluders, viewport, reveal masks | terrain_surface, GameView, Core spec | Surface/picking/masks/debug fixtures збігаються; 02–03 |
| 05 | Window ground lighting, emission, contact/directional shadows, wet ground | Core materials/passes | LIGHT/SHADOW/WET native videos; 04 |
| 06 | Flow water, shore foam, bounded reflection | World FX + water material | WATER video з берегом/мостом/hidden caster; 04 |
| 07 | Haze, smoke/wind, rain/snow prototype | World FX | HAZE/precipitation clips, pause/quality; diagnostic погодний ввід |
| 08 | Local footsteps і compressed snow appearance | World FX + movement observation | SNOW edge cases й bounded cache; 04/07 |
| 09 | Усі ефекти в одній малій справжній сцені | Повний production rendering path | **G1: вигляд + correctness + perf. Без проходу масові ассети заблоковані** |
| 10 | Compact concept HUD і input skeleton | RmlUi assets/GameUi/GameView | 720p/1080p/1440p, scale/locales; weather fields ще hidden |
| 11 | Terrain edge/path/build rules, видалення Road і bonus | Sim/world_grid/path/server/UI | Canonical preview/server + compatibility tests; збережений Bridge |
| 12 | Детермінована valley generation | world/biomes/generation profile | Один кандидат demo seed + 10 regression seeds, reachability; 11 |
| 13 | WT-01 authoritative weather/temp/cover/work, calendar offset | Weather spec sim/rules | Golden schedule, affected/unaffected work matrix, FirstStorm food buffer |
| 14 | WT-02 save/net/reconnect/forecast/current °C | schemas/protocol/ClientSession/UI | Full/delta/split/latejoin/persistence, жодних client-random цифр; 13 |
| 15 | Фінальні береги/скелі/рослинність/земля і receivers | terrain assets/generation/render | Новий world surface зберігає LIGHT/WATER/SHADOW correctness; 09/12 |
| 16 | Повний доступний building kit + states/II | Asset spec/export/loader | Camp + 11 типів, construction/ready/winter/selection, runtime contact sheet; 09/15 |
| 17 | Повний resident/cargo/job art + coherent motion | UnitAnimation/resident_art/GameView | 8 facing × 4 walk frames, cargo truth, footstep cadence, depth; 09/15 |
| 18 | Повна інтеграція atmosphere/materials зі справжньою погодою | Core + FX + WeatherView | Day/night/rain/storm/cover/melt, без diagnostic weather у звичайній грі; 14–17 |
| 19 | Правдивий компактний/повний inspector, per-storage projection | GameUi/snapshot/save/net | Два склади з різним запасом, reservation/cargo, чинні upgrade/production |
| 20 | Мінімапа й остаточний input/DPI | RmlUi/Minimap/GameView | Revealed map/viewport/click/drag/keyboard, без legacy HUD |
| 21 | Culling/quality/performance і ресурсні межі | Renderer/assets/FX | 20–30 residents + storm + lights, profiles кожного tier; 18–20 |
| 22 | Наскрізний ігровий регресійний прогін | Sim/net/UI/render/tests | 10 seeds, normal start, FirstStorm, food/logistics/noRoad, full configured CTest |
| 23 | RC package | Packaging/assets/shaders/launcher | Clean extract/launch/save/load, hashes/manifest, усі shader/material assets включені |
| 24 | Приймання 0.26.1 на Windows і двох ПК | Той самий RC | Погода/стан світу синхронні, два фізичні ПК, звіт; якщо недоступні — лише локальний кандидат |

У dev.01–09 допускається static diagnostic weather fixture для графічного експерименту. Це не обходить WT-01/02 і не вмикає фальшивий forecast. Зміна нижчих контрактів після dev.09 повторює відповідний proof; наприклад нова геометрія берега у dev.15 повторює water/receiver tests.

Для відкладеного повного графічного напряму первісний dependency шлях: 01→02→03→04→05/06/07/08→09→10/11→12→13→14→15/16/17→18→19/20→21→22→23→24. Паралельність дозволена лише після фіксації спільного contract; один renderer owner інтегрує passes.

## 8. Production asset pipeline
Докладний паспорт у [ART_PRODUCTION_SPEC_UA.md](ART_PRODUCTION_SPEC_UA.md). Рушій не створить потрібний стиль із неузгоджених текстур. Кожен asset має source/master, camera/export settings, neutral base, alpha, emission/snow/shadow або ground normal/material maps, anchors, bounds і native contact sheet. Legacy/BakedCompat зберігається; майбутній Dynamic2_5D потребує normal/depth карт фасадів і мешканців за окремою специфікацією.

До dev.09 виготовляється лише малий kit. Після приймання — весь доступний каталог. Наявні PNG можуть лишитися, тільки якщо проходять той самий паспорт/масштаб/матеріали; «вже є файл» не є acceptance.

## 9. Продуктивність, сумісність і бюджет
Початкові цілі, **не результати вимірювання**:
- 1920×1080 Medium на зафіксованій RTX 3050 Laptop конфігурації: p95 frame ≤16.7 ms, p99 ≤25 ms у 120-секундному сценічному прогоні після warmup; жоден regular gameplay frame >50 ms через FX.
- GPU world rendering як робочий бюджет ≤10 ms; CPU preparation FX ≤2 ms/frame; фактична sim/UI частка вимірюється окремо. Якщо GPU timestamps недоступні, не підміняти GPU час CPU submit — позначити відсутність виміру.
- 1280×720 Low на окремо зафіксованій інтегрованій GPU: початковий budget 33.3 ms p95, p99 ≤50 ms. Це тестова ціль, не вже опубліковані min specs.
- Additional render targets/caches та atlas residency мають числовий ledger у Core/Asset specs. Загальний пік важливіший за розмір одного PNG. Потік allocation не повторюється кожен frame.
- Для кожного tier: screen resolution, internal scale, lights/occluders/water/reflection/particles/decals caps, активний GPU/driver, sample count, p50/p95/p99, memorypeak, overflow counts.
- Обов'язковий ефект не може зникнути у Low. Якщо обсяг не вкладається, потрібне явне переглянуте рішення, а не тихе спрощення acceptance.

Наявна flat-quad тінь чи disabled reflection є допустимим safe failure під час розробки, але не фінальним виконанням обіцяного ефекту.

## 10. Ризики і ранні перевірки
| Ризик | Як виявити рано | Що робити |
|---|---|---|
| Sprite silhouette не дає переконливої тіні/відбиття | Один unit + house біля води, різні heights/zoom, dev.05/06 | Додати authored caster/height slices; повторити proof; не маскувати за blur |
| Світло проходить крізь фасад/гора ріже пляму | Receiver debug + moving occluder/slope/cliff fixture | Виправити receiver/visibility model до масових карт |
| Подвійне baked/runtime освітлення | Neutral-lit asset vs світло off/on | Розділити authored AO, emission і ground illumination, вилучити legacy duplicates |
| Glow/particles видають приховану будівлю | Дві однакові видимі сцени з різним hidden world | Видимі pixels мають лишитися однаковими поза AA boundary |
| Вартість PNG/масок виростає непомітно | Byte ledger та runtime peak на малому kit | Atlas reuse/lifetime/mips/format; не множити всі карти на кожен variant |
| Cosmetic time впливає на sim або pause | Hash sim при FX off/on, pause/2×/resume | Єдиний clock adapter; жодних callbacks із GPU в sim |
| Сліди створюють нові дороги | Перевірити speed/path/hash однаково зі слідами й без | Лише локальні decals; finite lifetime; жодного traffic accumulator |
| План знову підміняє результат | До кожного «готово» build+native video+tests+провалені критерії | Незалежний reviewer, не автор макета |

## 11. Докази і завершення
Артефакти майбутньої реалізації: `artifacts/graphics-v2/dev-XX/` з capture manifest, raw screenshot, безперервним video, machine/perf JSON і журналом тестів. Manifest: commit/build, shader+asset hashes, renderer/backend, GPU/driver, native/internal/logical resolution, UI scale, seed/save hash, world/camera/zoom, tick/calendar/epoch/weather, quality, test ID, expected/actual, unresolved issues.

Software probe, generated image, JPEG collage або «тести зелені» не замінює D3D12 відео. Screenshot не доводить рух води, тіней, появу слідів, снігопад або відновлення стану. Full CTest не доводить двох фізичних ПК. Недоступний зовнішній доказ позначається невиконаним.

**Поточний виконуваний порядок — V01–V05**. Повний material/light graph починається тільки в майбутній DL-гілці. Ця редакція виправляє план; не створює новий executable і не дозволяє називати 0.26.1 готовою.

## 12. Первинні технічні джерела
SDL дозволяє окремі render targets/passes і визначає їхній lifecycle; capability конкретного формату перевіряється з потрібними usage flags. Це обґрунтовує запропонований шлях API, але не доводить швидкість чи художню якість Ribterra:
- [SDL GPU overview](https://wiki.libsdl.org/SDL3/CategoryGPU)
- [Render pass: load/store і межі](https://wiki.libsdl.org/SDL3/SDL_BeginGPURenderPass)
- [Texture format capability](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat)
- [Shader bindings contract](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)

## 13. Зміни від V1
Додано відсутній renderer/material/resource план; проєктоване світло й тіні; wetness; bounded reflections; окремий haze; footprints; asset production; ранні performance/correctness gates. 15 dev-етапів замінено 24. Погода/генератор/дороги/UI/демо scope збережені. Графічні ілюзії прямо відділено від фізичних симуляцій. Жоден новий ефект не позначено реалізованим.


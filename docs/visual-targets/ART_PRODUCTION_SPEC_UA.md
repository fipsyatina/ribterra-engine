# Ribterra — виробництво графіки для renderer V2

**Dynamic lighting відкладено:** паспорти normal/depth нижче потрібні для майбутнього lit mode; поточна заготовка їх не завантажує. До продовження DL-01–08 не починати масовий lit export. [План](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md).
Дата: 27.09.2026. Статус: **специфікація майбутнього виробництва; комплект ще не створений**.

Порядок робіт визначає [план V2](GRAPHICS_ENGINE_PLAN_V2_UA.md), GPU/координати — [Render Core](RENDER_CORE_SPEC_UA.md), поведінку ефектів — [World FX](WORLD_FX_SPEC_UA.md). Цей документ визначає, які вихідні матеріали потрібні цим алгоритмам. Згенерована картинка не містить готових масок, геометрії, прив'язок, анімацій або runtime-даних.

## 1. Метод виробництва і межі
Основний спосіб — один набір master-моделей будівель і персонажів, ортографічний offline render у спрайти, контрольована художня обробка та повторюваний export. 3D використовується у виробництві ассетів; гра залишається 2.5D. Ручне малювання дозволене, якщо проходить той самий паспорт і контроль геометрії. Imagegen придатний для пошуку форми/палiтри, але не як автоматичне джерело сумісних animation frames чи material maps.

Початковий kit: будинок, склад, майстерня, дерево, камінь, один мешканець із вантажем, земля/берег/вода, міст. Робити лише те, що потрібне для dev.03–09. Повний каталог починається після спільного native proof, інакше помилка камери або освітлення помножиться на весь комплект.

Художня мета: темне дерево, холодний камінь, сині тканини, теплі локальні вогні; великі читабельні силуети й деталі другого плану. Контраст деталей зменшується з відстанню. Малий розмір мешканця не виправляти надмірною різкістю всього екрану. Густина поселення виникає з композиції та справжніх робочих зон, а не випадкового декору в кожній клітині.

## 2. Камера, розмір і паспорт ассета
Вихідна проєкція renderer — ромб 58×29 до zoom; справжні footprints беруться з [building_catalog.hpp](../../include/ribterra/building_catalog.hpp), проєкція й відбір спрайта — з [view.cpp](../../src/client/view.cpp). Жоден export не змінює simulation footprint.

Початкова offline камера: orthographic, azimuth 45°, elevation близько 30° для ромба 2:1. Це старт калібрування, **не доказ відповідності**. Напрям осей, handedness, вертикальна одиниця та масштаб звіряються через render calibration grid. Використати куб із підписаними +X/+Y/+Z, плаский footprint і чотири точки контакту. Порівняти native fixture на трьох zoom до експорту першої будівлі; прийняті camera matrix/export settings зберегти у source control.

`VisualAssetProfile` schema 2 — запропонований новий контракт, не наявне поле JSON:

| Поле | Семантика і перевірка |
|---|---|
| `asset_id`, `revision`, `source_hash` | Стабільний semantic ID, версія експорту, зв'язок із редагованим master |
| `albedo`, `emissive`, `caster_mask`, `material_mask` | Посилання на окремі atlas regions; відсутній обов'язковий канал — помилка kit validation |
| `normal` | Обов’язкові normal/material + camera-depth для майбутніх Dynamic2_5D фасадів і мешканців; для Legacy/BakedCompat не потрібні |
| `pivot_px`, `ground_anchor`, `bounds` | Один ground anchor для всіх сезонів/фаз/вантажів; bounds охоплює alpha silhouette |
| `footprint_id` | Посилання на чинну footprint definition, без незалежної копії правил у PNG |
| `base_height`, `caster_height` | У render tile-equivalent units за Core; legacy elevation переводиться в одному adapter |
| `occluders` | Авторські footprint polygons як source; exporter перевіряє й апроксимує обмеженими OBB/capsule-призмами |
| `window_lights` | ID, anchor XYZ, outward direction/cone, range, linear color/intensity, owner, allowed receiver kinds |
| `reflection_class` | None/Building/Vegetation/Unit, anchor, opacity і eligibility; не автоматично всі об'єкти |
| `surface_class` | Land/Water/Bridge/None; deck mask відрізняється від декоративного даху |
| `snow_mask`, `wet_response` | Де може з'явитися сніг/волога; не вікна, не довільний прямокутник альфа |
| `state`, `season`, `facing`, `frame`, `cargo` | Ключі тільки підтриманих ігрових/animation станів |

Вікно повинно мати emission mask, зовнішній anchor і конус на правильному боці будівлі. Пляма світла не малюється в albedo землі. Початковий kit: два контрольні вікна на будинок, до чотирьох emitters на складну споруду; більше — окремий measured budget. Не ставити light origin усередині owner occluder: тоді будинок затінить власне джерело.

## 3. Які карти експортувати
| Канал | Дані | Чого не запікати |
|---|---|---|
| Albedo RGBA, sRGB | Колір матеріалу, alpha silhouette; м'який authored AO для читабельності | Ground shadow, світлову пляму вікна на землі, нічне затемнення всього PNG |
| Emissive, linear mask | Вікна/горни/вогні; intensity і color задає profile | Квадратний glow навколо будівлі, усю світлу штукатурку |
| Ground normal, linear | Узгоджений tangent/world basis для ґрунту й води | sRGB gamma, normal від іншої камери |
| Material mask, linear | Roughness/wet response/snow eligibility/coverage за family layout | Чотири різні семантики одного каналу без material family |
| Caster alpha | Силует для напрямленої тіні, окремо від selection | Блакитний selection disc або стару baked ellipse |
| Snow top mask | Дахи, горизонтальні поверхні, придатна земля | Біле вікно, сніг на річці без механіки льоду |
| Water flow + shore mask | Напрям течії/модуль, плавний береговий перехід | Відбиття нерухомої будівлі, намальоване у tile texture |
| Footprint alpha/normal | L/R відбиток, край втиснутого снігу | Великий темний овал під кожним кроком |

Канали мають documented packing у material family. Колір і data maps не використовують один atlas із неоднозначним color space. Linear masks не отримують автоматичного sRGB decode. Premultiplied export не premultiply вдруге; boundary conversion визначає Core. У Dynamic2_5D albedo допускається тільки neutral material variation/soft AO, без запеченого directional/local light. Normal/depth reconstruction задає Dynamic Lighting spec. У майбутньому Dynamic sun elevation/інтенсивність змінюються в погоджених межах; для базових V01–V05 нового sun-light renderer немає.

Roughness не означає повний PBR renderer: застосовується лише формула ground/water specular із Core. Фасад отримує dynamic diffuse за P/N, але не повний physical reflection. Для depth потрібна R16_UNORM data texture з range metadata; loader має перевіряти формат і byte count окремо.

## 4. Роздільність і atlas budget
Розмір вихідного master не дорівнює видимому розміру в грі. Для кожної family у dev.03 записати projected bounds на zoom 0.62/1.30/3.0. Стартові export buckets: споруди 256 або 512 px по більшій стороні, мешканці 96–128 px на кадр, terrain/FX 128–512 px залежно від повторення. Ці числа — гіпотеза до native capture; не збільшувати все до 2048 px «для якості».

Чинний [loader](../../src/client/game_art.cpp:70) обмежує atlas page 4096×4096; це верхня межа, не рекомендація завжди її заповнювати. Планувати сторінки 2048×2048, окремі pages для різних sampler/color-space families. Упакування: мінімум 4 px extruded gutter на базовому рівні; mip generator зберігає alpha coverage, не підмішує сусідній region. Якщо mip рівень не може зберегти isolation, задавати max LOD або більше padding; перевіряти native віддалення, а не лише atlas preview.

Приклад вартості без стиснення: 2 типи мешканців × 8 напрямків × 4 walking frames × 4 види вантажу = 256 кадрів. 128²×256×4 bytes = 16 MiB на RGBA8 map без mips; повний mip chain ≈21.33 MiB. Три незалежні такі карти ≈64 MiB. Це арифметичний приклад, не обов'язок створити всі 256 кадрів: сумісні маски й cargo overlays потрібно повторно використовувати.

Початковий **цільовий** resident atlas budget ≤64 MiB, buildings/environment/FX ≤192 MiB, разом ≤256 MiB на Medium. Це лише content textures, окремо від render attachments, UI, upload buffers, старих atlas під час reload. Вести byte ledger: width×height×bytes×mips×pages; вимірювати resident і peak під час load/resize. Якщо бюджет не сходиться, зменшувати дублювання варіантів і кількість декоративних форм; не прибирати emission/caster metadata, потрібні прийнятому ефекту.

## 5. Будівлі й середовище
Повна матриця береться з доступного каталогу після вилучення Road: **11 типів + Camp**, чинні House II/Workshop II. Зафіксувати актуальний список у dev.01, а не копіювати недоступні споруди з концепту. Стан selected/hover — runtime outline/marker, а не окрема повна PNG-копія. Ghost/invalid preview використовує той самий footprint/anchor і зрозумілий color treatment.

Для кожного типу:
1. Ground anchor, непрозора основа, вхід і робоча зона збігаються з ігровими даними.
2. Construction stages читаються без inspector; кількість стадій узгоджена з чинним progress, без вигаданих окремих jobs.
3. Summer/winter варіанти мають ідентичний pivot, footprint і camera; можливий snow mask замість подвійного albedo, якщо native результат проходить.
4. II рівень відрізняється силуетом і функціональною деталлю, зберігає чинне місце входу.
5. Горн/дим/вогонь відображає справжній active state, коли це сигнал роботи. Декоративне світло окремо позначене в profile.
6. Складські купи мають дискретні fill bands зі справжнього stored stock; carried/reserved не додаються двічі.

Terrain kit: dry/wet/snow surface families, берег вода→ґрунт→трава, доступний ramp, cliff face, bridge approaches. Маски швів і спільні вершини відповідають topology. Міст перекриває water receiver тільки маскою deck, залишає воду видимою під відкритими частинами. Далекий backdrop не підробляє доступну для гри територію.

Рослинність: кілька узгоджених силуетів і масштабів, групи за world seed, контроль повторів, локальний wind pivot. Дерево не хитається разом із коренем. Камінь не розтягується випадковим nonuniform scale, який руйнує його освітлення. Не ставити непрозорий декор поверх реального входу/переходу.

## 6. Мешканці, вантажі та стопи
Чинні [animation](../../include/ribterra/unit_animation.hpp) і [cargo selection](../../include/ribterra/resident_art.hpp) — вихідна семантика. Спершу один персонаж, 8 facing, 4 walking frames, idle, empty/logs/sack/чинний cart. Нові коні й фізичні вози не входять.

Усі кадри мають один ground anchor між стопами; pelvis/head не змінюють масштаб між facing. Метадані `left_foot_local/right_foot_local` і `contact_phase` потрібні presentation emitter, але authoritative movement не залежить від animation. Step events обмежуються накопиченою відстанню руху: idle/teleport не залишають сліди. Якщо 4 walking frames дають помітне ковзання у native proof, спершу виправити timing/poses; збільшення до 6–8 кадрів — measured art-budget change, не прихована обіцянка.

Cargo overlay має attachment point і caster silhouette; колоди не проходять крізь голову, не плавають при зміні facing, не залишаються після реальної доставки. Тінь персонажа створюється для art-ready гілки теж. Інакше гарний мешканець знову висітиме над землею.

Footprint stamps — окремий малий atlas, дві ноги, depth/normal look, alpha fade. Відбитки не є частиною walking PNG; їхня позиція залишається у світі після виходу персонажа. Дані й час життя — [World FX](WORLD_FX_SPEC_UA.md).

## 7. HUD із концепту
Рамки, іконки й декоративні кути відокремлені від тексту. Текст залишається справжнім RmlUi/localization text: не запікати українські написи й числа в PNG. Nine-slice frame має padding/мінімальну висоту й читається при 720p/1080p/1440p, 100/125/150% UI scale. Невеликі іконки перевіряти у фактичному розмірі, а не лише у збільшеному export.

Основні зони з улюбленого концепту зберігаються: ресурси ліворуч угорі; прогноз і температура згори; контекстна небезпека праворуч; компактний inspector зліва внизу; чотири основні дії по центру; мінімапа справа. Weather/fuel/insulation макета не означає наявних даних. Погода з'являється після dev.14; паливо/утеплення не входять у цю версію.

Справжня ієрархія й адаптація задані в [контракті UI](AGENT_VISUAL_CONTRACT_UA.md). Красиві орнаменти не повинні займати місце причини блокування та наступної дії гравця. Базовий HUD виконується на V01 у чинному renderer. Повний Dynamic proof не є його передумовою; lit assets приймаються пізніше в DL-гілці.

## 8. Інтеграція і перевірка експорту
Розширити [register_reference_art.py](../../tools/register_reference_art.py), [register_resident_art.py](../../tools/register_resident_art.py), [atlas_tool.cpp](../../tools/atlas_tool.cpp) та schema loader. Окремий validator перевіряє passport, channel presence, dimensions, premultiply/color space, gutters, bounds, pivots, positive ranges, finite floats, winding/degenerate occluders, допустиму кількість emitters. HLSL-контракти перевіряє Core; PNG validator не може їх замінити.

Експорт має бути повторюваним: master revision → camera/settings hash → channels → atlas → manifest → package. Manifest включає source/license provenance для зовнішніх ассетів. Generated assets позначають походження. Не замінювати source master одним фінальним PNG без параметрів відтворення.

Негативні тести: missing mask, invalid atlas region, bad pivot, NaN light, negative height, unsupported schema, allocation failure. Loader відхиляє кандидат цілком і зберігає останній валідний комплект. Не перетворювати missing emission mask на «успішне» вимкнення світла. У production package має бути asset manifest, який перевірявся в acceptance.

## 9. Приймання і трудомісткість
| Gate | Потрібний результат |
|---|---|
| ART-A / dev.03 | Calibration cube/grid; малий kit з усіма потрібними masks/anchors; валідний schema 2 export |
| ART-B / dev.05–08 | Ефекти використовують конкретні metadata; on/off/occlusion/water/footstep native video; немає подвійного baked light |
| ART-C / dev.09 | Kit прийнятий у спільній сцені: день/ніч/дощ/сніг, zoom 0.62/1.30/3.0, camera pan, slope/bridge/FoW; target budgets measured |
| ART-D / dev.15–17 | Увесь доступний каталог, terrain і directional residents відповідають прийнятому kit; runtime contact sheets усіх доступних станів |
| ART-E / dev.21–23 | Native package, alpha/mips/VRAM/resize/quality checks; немає missing regions або debug fallback |

На dev.03 записати фактичний час на одну прийняту споруду, сезонний стан, персонажа, export і виправлення. Після dev.09 оцінити решту каталогу: кількість непокритих state families × виміряна трудомісткість + інтеграція + перевірка. Календарний строк до цього був би вигадкою. Один агент не може достовірно назвати kit прийнятим тільки тому, що сам згенерував його.

Необхідні докази: source/extracted atlas contact sheet, byte ledger, validator log, native scene captures/video, список відхилень. Художній reviewer перевіряє однаковість масштабу/матеріалів, інженер — контракт/перекриття/пам'ять. Це ролі відповідальності, не твердження, що така команда вже найнята.

## 10. Первинні джерела методу
Ортографічна камера й окремі render passes дають відтворювані вихідні канали, але їх ще потрібно інтегрувати у власний renderer:
- [Blender: camera projection](https://docs.blender.org/manual/en/4.1/render/cameras.html)
- [Blender: EEVEE render passes](https://developer.blender.org/docs/features/eevee/render_passes/)

Ці джерела обґрунтовують offline workflow; вони не доводять, що Ribterra вже підтримує ці ефекти.

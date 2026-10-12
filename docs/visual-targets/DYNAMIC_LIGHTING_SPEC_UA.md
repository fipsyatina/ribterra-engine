# Ribterra — обов'язкове динамічне освітлення

**Новий bounded checkpoint08.10:** [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md) спільно змінює Sun/Moon/ambient/atmosphere за діагностичними hour/cloud/haze. Fresh build02 і два actual native runs exit0; 42PNG+raw+JSON (44/44 exact). Причинно перевірені рух сонячних/місячних тіней, cloud/haze isolation, horizon source-on/off і незмінні authored local/emission. Це не authoritative time/weather, normal startup, концепт-арт або ≥120FPS; **E06 і всі E01–E17 OPEN**.

**Частковий authored checkpoint08.10:** [schema-2 importer/camera-relative adapter](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md) перевірено свіжою C++20 W4/WX збіркою і двома focused native loader runs. Світові трикутники незалежні від raster caster mask; directional shadow pixels через authored шлях ще NOT_RUN. Full Dynamic/GameView, concept fidelity та≥120FPS лишаються OPEN.

**Частковий engine-first checkpoint08.10:** [SkyShadowSlice](SKY_SHADOW_CHECK_2026_10_08_UA.md) додає незалежні opaque-triangle D32 depth maps Sun/Moon і source-specific visibility на ground/typed P. Це fixed-camera diagnostic proof, не повний Dynamic/GameView, art або120FPS. Новий material b3 має явний SkyMoonLighting192 (Moon32+Shadow160), не п'ять uniform buffers. Пізніша engine-first вимога має пріоритет над історичним deferred статусом нижче; E01–E17 OPEN.

**Актуальний engine-first checkpoint07.10:** пізніша пряма вимога користувача має
пріоритет над історичним deferred/scaffold статусом нижче. [Незалежне direct Sun/Moon](SKY_DIRECT_CHECK_2026_10_07_UA.md)
реалізовано лише в explicit SkyContactSlice; directional silhouettes, time/weather,
всі художні категорії/GameView/full Dynamic/120FPS ще не прийняті. Старий384 sun/local
ABI не розширювали прихованими полями — додано separateMoon32.

**Відкладено за останнім рішенням користувача.** У поточній роботі лише engine scaffold. Повна система та перемикач — [майбутні DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md), без призначеного release. Обов’язкові критерії нижче стосуються майбутнього ввімкненого режиму, не 0.26.1 загалом.
Дата: 27.09.2026. Статус: **PROPOSED / NOT IMPLEMENTED / NOT BENCHMARKED**.

Це уточнення вимоги користувача до [V2](GRAPHICS_ENGINE_PLAN_V2_UA.md). Освітлення повинно змінювати **землю, фасади, дахи, дерева і мешканців**, коли змінюється джерело або положення об'єкта. Emissive PNG, загальний tint і світлова пляма лише на землі не закривають цю вимогу. Алгоритми нижче — запропонований інженерний шлях, придатність якого ще треба довести у native dev.05/09.

## 1. Видимий контракт
Обов'язкові керовані параметри джерела: position XYZ, direction, cone/radius, linear color, intensity, enabled. Світло/тінь оновлюються під час руху камери та об'єктів без повторного експорту PNG.

| Сцена | Що повинно відбутися |
|---|---|
| Вікно off→on | Світиться тільки emission mask; освітлюються земля і придатні сусідні поверхні в конусі |
| Мешканець проходить повз джерело | Освітлений бік, інтенсивність і колір змінюються відповідно до normal/position; вантаж реагує теж |
| Тестове світло пересувається зліва направо | Освітлюється інший бік видимого фасаду; намальований highlight не залишається назавжди |
| Камінь/мешканець перетинає промінь | Видимість конкретного джерела змінюється; інше джерело продовжує освітлювати ділянку |
| День→ніч | Зменшується directional/ambient contribution, локальні джерела залишаються читабельними |
| Dry→wet | Змінюється roughness/specular землі, але light position і causal occlusion зберігаються |

Рухоме джерело у proof — diagnostic control. Воно не додає у demo нової механіки факела, предмета інвентарю або мережевої команди. Production lights походять із visual profiles і справжнього дозволеного стану об'єкта.

## 2. Обраний шлях: lit 2.5D із даними форми
Зберігаємо фіксовану камеру, painter order, SimCore і RmlUi. Terrain використовує geometry/world position; building/unit/tree sprites отримують **neutral albedo + normal/material map + camera-depth map + emission**. Це динамічний розрахунок освітлення видимої поверхні зі спрайтовими даними форми. Це не повна 3D-сцена і не підтримка довільного обертання камери.

Якщо цей спосіб не дає прийнятного світла на малому kit або не вкладається у виміряний бюджет, dev.09 провалений. До масового арту потрібно змінити алгоритм/геометричний шлях. Не переводити фасади назад на baked-only material і не називати це виконаним dynamic lighting.

## 3. Геометрія кожного пікселя спрайта
Render metric із Core: X/Y у клітинах, Z = legacyElevation × 2.4 / 29. Канонічні constants і CPU helpers визначені в `include/ribterra/world_projection.hpp`. Affine projection до zoom:

```text
sx = 29 * (x - y)
sy = 14.5 * (x + y) - 29 * z
d  = dot(normalize(1,1,1), float3(x,y,z))

M = rows((29,-29,0), (14.5,14.5,-29), normalize(1,1,1))
local_xyz = inverse(M) * float3(local_sx, local_sy, local_d)
world_xyz = object_world_anchor + local_xyz
```

`local_sx/local_sy` беруться з source pixel відносно pivot і переводяться через `export_pixels_per_reference_pixel`; не використовувати масштабовані framebuffer coordinates без зворотного zoom. `local_d` — linear camera-depth, розпакований із R16_UNORM за per-asset `depth_min/depth_max`. Це **texture data**, не runtime depth target. Для source іншого basis exporter застосовує повний affine transform у render metric до запису карт. Normal перетворюється inverse-transpose, потім normalizes; проста заміна координат normal некоректна при nonuniform scale.

Спрайт не обертається довільно в runtime. Кожний facing/animation frame експортує normal/depth у спільному world basis тієї самої камери. Ці дані охоплюють тіло й реальний cargo. Сезонна маска без зміни геометрії може повторно використовувати карти; новий дах або змінений силует потребує нового depth/normal.

Calibration test: offline points XYZ → export → shader reconstruction → reproject; помилка ≤1 native pixel на контрольному zoom і world error ≤0.02 tile у валідних opaque pixels. Це початкові acceptance tolerances. Перевірити кути будинку, коник даху, стопи, голову і кінці колод; не оцінювати лише центр PNG.

## 4. Material contract
| Карта | Початковий формат | Дані |
|---|---|---|
| Albedo | RGBA8, sRGB source | neutral color + alpha; без directional/local light і ground shadow |
| NormalMaterial | RGBA8 linear | RG: octahedral normal; B: roughness; A: bounded authored AO |
| CameraDepth | R16_UNORM linear | depth range metadata; invalid alpha не використовується для reconstruction |
| EmissionMask | R8 linear | window/fire coverage; emitter color/intensity із profile |

Це стартові GPU formats, їх support/usage перевіряє dev.01–02. Внутрішній loader повинен підтримати data formats; не пропускати R16 через старий 8-bit RGBA image loader. Якщо source має 16-bit PNG/EXR, offline exporter конвертує у перевірений runtime data blob із width/height/format/byte count/hash. Повний EXR decoder у shipping client не потрібний.

Заборонено виводити справжню normal/depth просто з яскравості довільного AI PNG: темне вікно не означає геометричну яму. Такі реконструкції можуть бути чернеткою, але проходять ті самі geometry tests. Рекомендований source — узгоджений offline master.

Світові матеріали повертають linear radiance; emission додається після освітлення. UI лишається поза світовим освітленням. Жодного повторного night tint над кінцевим світлом.

## 5. Розрахунок світла
Для кожної видимої точки P та normal N:

```text
L = light.position - P
distance = length(L)
dir = L / max(distance, epsilon)
range_fade = saturate(1 - distance*distance / (radius*radius))^2
cone = smoothstep(outer_cos, inner_cos, dot(-dir, outward_direction))
visibility = ray_visibility(P + N*bias, light.position, candidates)
diffuse = albedo * max(dot(N, dir), 0) * color * intensity
local = diffuse * range_fade * cone * visibility
result = albedo * ambient * authored_AO + sun_term + sum(local) + specular + emission
```

Point light використовує cone=1. Spot використовує нормалізований direction XYZ; ground-only direction XY недостатній для світла на даху або людині. Майбутній LightGpu у Core містить float4 directionXYZ/outerCos та innerCos у п’ятому float4, з 80-byte stride. Перед DL-01 звірити C++/HLSL offsets; поточна заготовка цих GPU structs ще не має. Одна спільна declaration/offset test, не дві різні structs у Core і FX.

Для ground/water і грубих lit sprite materials допустимий bounded Blinn-Phong specular: H=normalize(Ldir+V), exponent=lerp(8,96,1-roughness), strength із family; помножити на N·L, visibility і range/cone. Це керована модель для стилізованого результату, не повний physical PBR. Soil не стає металом. Wetness зменшує roughness у заданих межах і збільшує specular strength; water використовує власні flow normals. Ambient не множиться на shadow конкретного local light.

Sun — directional source із versioned azimuth/elevation curve, linear color/intensity за calendar/presentation adapter. Початковий production діапазон elevation 20°–65°, нижче — fade to zero без нескінченної довгої тіні. Azimuth і погоджені контрольні стани фіксуються у dev.03; diagnostic контролює напрям незалежно для перевірки normals. Це художнє правило гри, не географічна симуляція сонця.

## 6. Occlusion і межі тіней
CPU збирає кандидати за light influence, видимістю і layer; moving units додають оновлювані capsule-призми. Будівлі/дерева — author-validated OBB/capsule sets. Для відрізка P→light:

1. Перевести XY у basis OBB; перетнути slab intervals X/Y і height slab Z.
2. Перетин існує, якщо непорожній t-інтервал лежить усередині (bias, 1-bias).
3. Capsule використовує XY segment/capsule intersection плюс Z interval. Дотик у межах epsilon не створює випадкового миготіння.
4. Виключити receiver owner із self-occlusion цього approximation. Це не дає справжніх самотіней між деталями одного фасаду; межа явно приймається окремо.
5. Для інших об'єктів не вимикати occlusion через нестачу місця у buffer. Overflow реєструється і провалює acceptance.

Sun shadows на землі використовують authored silhouette/height і receiver projection, local shadows — visibility конкретного джерела. Точність тіні обмежена author geometry: capsule не відтворює кожен палець. Тіні даху на власний фасад, світло через складні внутрішні кімнати, GI, ray tracing, volumetric light shafts і довільні 3D self-shadows не обіцяються. Це межі якості, а не право вимкнути світло на всіх фасадах.

Показати ground shadow і освітлення на іншому об'єкті окремо: silhouette не може одночасно вдавати ray blocker для всіх джерел. Window anchor змістити назовні owner prism і направити cone назовні; не прибирати owner із усіх чужих lighting queries.

## 7. Інтеграція, вартість і порядок
Canonical render graph і formats — [Core](RENDER_CORE_SPEC_UA.md). Ground lighting споживає albedo/normal/world position/flags; World sprite shader сам обчислює світло на реконструйованій поверхні. Готовий ground light buffer не накладається поверх мешканця або даху як screen-space tint.

Light lists будуються для ground tile/receiver batch і sprite bounds. Початковий per-receiver cap 4/8/12 local lights для Low/Medium/High; per-light occluders 16/32/48, global 128/256/384. Це **не виміряні** числа. Culling за distance/range має довести, що відкинуте джерело не впливає на receiver. Якщо справді перекривається більше джерел, не називати їхнє зникнення коректним: зафіксувати overflow і переглянути budget або content density перед прийманням.

Додаткові data maps оплачуються в asset ledger. Приклад без reuse: 256 кадрів ×128² × (4 albedo +4 normal +2 depth +1 emission) bytes =44 MiB без mips, ≈58.67 MiB із повним mip chain. Geometry/cargo variants можуть збільшити число кадрів. Стартовий resident budget64 MiB проходить тільки якщо фактичний комплект поміщається; числа не є доказом.

Dev.02 фіксує GPU/material contracts; dev.03 створює neutral-lit kit і geometry maps; dev.04 додає position reconstruction/receiver/occluder validation; dev.05 доводить dynamic light на землі та спрайтах; dev.09 — всі ефекти разом із вимірюванням. Dev.16/17 розширюють вже прийняті lit materials на весь доступний каталог.

## 8. Обов'язкові тести dev.05/09
| ID | Перевірка і критерій |
|---|---|
| DL-01 | Світло off/on: diffuse/emission змінюються окремо; albedo asset hash незмінний |
| DL-02 | Рух джерела навколо контрольного куба/будинку: нормалі дають правильний освітлений бік |
| DL-03 | Мешканець + вантаж проходять світловий конус: змінюються освітлення і тінь, без baked tint |
| DL-04 | Occluder закриває одне з двох різнокольорових джерел: гасне тільки його внесок |
| DL-05 | Roof/фасад/земля отримують світло за різними N/P; uniform screen glow не проходить |
| DL-06 | Reproject depth/normal data на трьох zoom; немає drift і frame/facing discontinuity |
| DL-07 | Hidden source/caster не змінює revealed output; pan/zoom не змінює світло у world coordinates |
| DL-08 | Sun/night/wetness переходи, pause/2×/reload; emission не темніє вдруге |
| DL-09 | Low і Medium із 20–30 мешканцями та погодою: raw frame-time/VRAM/overflow logs, усі обов'язкові категорії збережені |
| DL-10 | Missing normal/depth/bad range/NaN: asset transaction відхиляється; albedo-only legacy не зараховується за lit proof |

Критерій готовності — нативне немонтоване відео та вимірювання на тому самому build. Це оновлення специфікації, а не заява, що динамічне освітлення вже реалізоване.

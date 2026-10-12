# Water / Reflection V1 — окремий кумулятивний контракт

Дата: 2026-10-08. Стан: код і bounded native GPU checkpoint перевірені;
повне приймання води та двигуна відкрите. Докази й межі:
[Water / Reflection check](WATER_REFLECTION_CHECK_2026_10_08_UA.md).
Це уточнення контракту Render Core / World FX, а не приймання E09 або всього двигуна.

## Діагностичний raw readback — окремий follow-up

`Renderer2D::read_water_coverage_v1()` повертає tightly packed RGBA8,
фактичний GPU row pitch, розмір, frame slot і generation останнього
успішно submitted Water кадру. Це synchronous diagnostic: власні
transfer/fence, не per-frame шлях і не GPU timing. Виклик серіалізований
із renderer на main thread; під час render, до успішного кадру, на іншому
графі або stale generation — reject. Окремий transfer не торкається
normal capture `FrameResources::transfers` або render telemetry.

Commit нових targets інвалідовує старий readback; відхилений candidate
залишає попередній slot/generation придатним. Часткове recovery submission,
яке могло перезаписати той slot, інвалідовує його, а не видає старі bytes.
Raw tuples поточної binary реалізації: clear=(0,0,0,0), Land=(0,1,0,0),
BridgeDeck=(0,3,0,0), Water=(255,2,shore*17,255).

Свіжа follow-up build02 та native05/06 пройшли: actual 1279×719 drawable
→ fault36 на останньому reflection candidate → old raw bytes exact
→ retry54 з ceil-half640×360 → restore72 у1080p і baseline exact.
[Актуальні докази](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md).
Це перевірка binary packing і конкретної послідовності rollback, не
intermediate coverage AA, усіх recovery failures або total/peak VRAM.
Recovery консервативно інвалідовує readback при повторному використанні
того самого slot; довільні серії failures цим тестом не прийняті.
Попередній native01/02 checkpoint використовує стару source cohort
і зберігається як історичний.

## ABI і граф

`WorldGraph::WaterReflectionSlice` зберігає SkyShadow, незалежний Moon,
Contact V1 і локальне освітлення. Старі графи не отримують нові проходи.
Порядок: SunShadow → MoonShadow → Reflection → GroundGBuffer →
GroundContact (якщо є його вхід) → GroundLighting → WorldForward → Composite → UI.

Перші чотири GBuffer і всі їх канали зберігаються. SurfaceWorld.W залишається
точним Contact V1 тегом 0/1/2: це не покриття, не маска, не вага змішування.
GroundMaterialGpu залишається 48 байт. CPU-обгортка GroundMaterialDraw — 80;
її колишній хвіст містить клас @68, берегові біти @72, покриття @76.

П'ятий MRT `WaterCoverageV1`, full-resolution RGBA8 UNORM, clear=(0,0,0,0):

| Канал | Значення |
| --- | --- |
| R | Геометричне покриття води; суша й настил пишуть 0 |
| G | Клас/255: Land=1, Water=2, BridgeDeck=3 |
| B | Береги/15: N=1, E=2, S=4, W=8; невідомий сусід не берег |
| A | Дозвіл відображення: 1 лише для води |

Цей носій point/clamp-семплюється. Reveal лишається окремо в SurfaceFlags.A.
RGBA8 обрано замість R8, бо потрібні також клас, береги й дозвіл; зайняті
старі канали не перепаковуються. Це не мінімальна кількість байтів, але
окремий перевірний контракт без пошкодження наявних ABI.

Поточний producer — opaque, sample count 1. CPU coverage/reveal у новому графі
приймають тільки 0 або 1. Наявність RGBA8 не є доказом AA. Дробове покриття
краю, його raw readback і допустимий AA-контур залишаються відкритими.

WaterSurfaceGpu — 64 байти: абсолютний origin XY/показаний час/wind;
нормалізований flow/strength/normal strength; лінійний tint/roughness;
distortion ≤1.5 reference pixels при reference height=900, reserved zero.
Нормаль — окрема typed linear tileable текстура. Дві нормативні UV-фази
прив'язані до абсолютних XY, із поворотом 37°, швидкістю .018+.022·wind;
геометрія води лишається render Z=0. Піна змінює diffuse albedo перед BRDF,
а не додає нічне світіння. Берегова фаза використовує origin XY і +.5 для
цілоцентричних клітин, тому не рухається разом із камерою.

Reflection — окрема half-resolution RGBA16F texture, clear transparent.
Віддзеркалена геометрія використовує незмінні atlas UV, source P/N та
освітлення джерела. RGB premultiplied; blending ONE / ONE_MINUS_SRC_ALPHA.
Strength і fade застосовуються один раз. GroundLighting змішує відображення
лише за актуальним full-resolution receiver: Water, coverage>0, reveal=1,
allow=1. Суша/настил/невідкрита територія його не отримують незалежно від
білінійного семплу чи distortion. Фінальна scene/final alpha — 1.

## Пам'ять і resize

Перші сім target records зберігають порядок; записи 7/8 — Coverage і Reflection.
Старі графи мають нулі в останніх двох записах. Новий граф — 9 attachments
на кожен із двох frame sets. Reflection dimensions: ceil(W/2), ceil(H/2).

Номінальний active payload: `2 × [40WH + 8ceil(W/2)ceil(H/2)]` bytes.
Для парних розмірів — 84WH: 1920×1080 = 174182400 B / 166.11 MiB;
2560×1440 = 309657600 B / 295.31 MiB. Це не driver allocation/VRAM measurement.
Resize створює повний candidate перед publication; old+candidate тимчасово
співіснують (1080p→1440p ≈461.43 MiB лише цих targets). Sky maps, sampled art,
upload buffers і driver overhead сюди не входять. Capability/pipeline failure
не має тихо вимикати воду чи міняти бекенд; resize failure зберігає старий set.

## Межі поточного checkpoint

`compose_engine_control` тепер читає фактичні Terrain::Water, revealed neighbours
і завершені Bridge із ClientSession. Технічні вузькі настили пишуть BridgeDeck
до GroundLighting, а не закривають цілу клітину; з'єднані секції узгоджують вісь.
Приховані клітини не створюють receiver/shore/reflection data. Геометрія землі
й picks походять із того самого terrain mesh. Normal GameView/startup незмінені.

Native probe створює окремий current-schema diagnostic world із нерівною
річкою, двома берегами, трьома справді побудованими Bridge і reveal frontier.
Це не доказ випадкового генератора або нового фінального арту. Clock/camera
контроли програмні; вони не доводять keyboard/mouse/pause interaction/video.
CPU raster classifier з межею 1 physical pixel — діагностика leakage, не raw
GBuffer readback. Концепти, AA, повний input/video, нормальна сцена, погода,
усі E01–E17, full suite і ≥120 FPS залишаються OPEN до власних фактичних доказів.

## Збереження файлів

Новий build/probe використовує окремі bounded roots ≤128 MiB. Старий latest
і перевірений rollback не видаляються. Компілятор може прибирати власні
transient files тільки в заздалегідь названому новому compiler-temp. Atomic save
fixture може прибрати свої failed `.tmp`/`.bak.tmp`; ці точні шляхи потрібно
назвати користувачу перед запуском. Ніякої зміни launcher/pointer/install,
публікації GitHub або очищення старих roots цей checkpoint не виконує.

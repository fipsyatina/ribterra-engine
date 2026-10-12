# Ribterra — послідовні видимі оновлення графіки

**08.10.2026 — partial authored sky loader/adapter:** [свіжий bounded bridge proof](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md),41 exporter negatives,13 groups×2/19 schema invalids/13 transactional adapter failures,22 material regression PNG. Реальний GameArt шлях перевірено; authored shadow GPU pixels ще NOT_RUN. **Усі E01–E17 OPEN; це не новий вигляд гри, не concept acceptance або120FPS; gameplay/HUD/catalog чекають.**

**08.10.2026 — partial Sun/Moon directional depth:** [SkyShadowSlice native checkpoint](SKY_SHADOW_CHECK_2026_10_08_UA.md),110CPUchecks,12nativegroups×2/27repeatPNG,oldSky26/Contact31/shaders56byte-exact. Це fixed-camera opaque-triangle prerequisite, не новий вигляд гри. Наступний authored asset adapter лише в роботі. **Усі E01–E17 OPEN; concepts/GameView/presets/≥120FPS не прийняті; gameplay/HUD/catalog чекають.**

**07.10.2026 — partial independent sky direct:** [SkyContactSlice + окремий Moon32](SKY_DIRECT_CHECK_2026_10_07_UA.md), causal native ground/typed-fixture sun/moon, old shader/contact byte equality. Directional silhouettes/time/weather, art/concepts/GameView та≥120FPS ще OPEN. Попередні записи про відсутність moon стосуються історичних graph checkpoints, не нового opt-in; до гри не переходили.

**07.10.2026 — partial Contact GPU V1:** [ізольований shader/registry/native proof](CONTACT_GPU_CHECK_2026_10_07_UA.md),211CPUassertions,shader positives/repaired negatives,53nativechecks×2 та31/31 repeatPNG; fresh selected renderer software/D3D12 regression. Ambient-only маски працюють у новому opt-in graph; GameView лишається BakedCompat. **Усі E01–E17 OPEN, concept fidelity та ≥120FPS не підтверджені; гра чекає.** Нижчий CPU-only checkpoint — історія до цього кроку.

**07.10.2026 — CPU prerequisite, не contact GPU implementation:** [Contact CPU contract](CONTACT_CPU_CONTRACT_CHECK_2026_10_07_UA.md),157CPUassertions/standalone W4WX build/independent bounded approval. Art registry, adapter, projection/scissor, GPU pass, native contact pixels і performance ще не реалізовані. Усі E01–E17OPEN; GameView/launcher незмінні, до гри не переходили. Core/WFX explicitly separate contact Wtags from future antialiased coverage; full-engine coverage carrier remains OPEN.

**07.10.2026 — local-light visibility, не художнє приймання.** [Owner-aware OBB/capsule production крок](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md):16/16 selected tests,49 Local PNG,34 Ground regression PNG,15/15 Legacy PNG незмінні й незалежний review пройдено. Ground/фасад/дах реагують на окреме перекриття red/blue sources; UI незмінний, buffer recovery/fail-closed bounds перевірені. Напрямлені/контактні тіні, місяць, Reflection/FoW/FX, GameView, концепти й120FPS лишаються відкритими; gameplay/HUD/catalog не продовжуються.

**07.10.2026 — production ground/forward slice, не новий вигляд гри.** [Фінальна native перевірка](GROUND_FORWARD_CHECK_2026_10_07_UA.md): 16/16 selected tests, 34 PNG, causal ground sun/point/spot, schema-2 emission, два frame sets, rollback і 4:3. 15/15 Legacy GameView кадрів незмінні. Reflection/FoW, тіні, час/погода/атмосфера, concept-ready kit, Dynamic integration та ≥120 FPS відкриті. До повного engine-first приймання gameplay/HUD/catalog не продовжуються.

**06.10.2026 — engine-first має пріоритет за останньою прямою вимогою користувача.** [Повний поріг E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md) відкритий; генератор, нові механіки, масовий каталог і подальший HUD чекають. [Typed material bridge](MATERIAL_BRIDGE_CHECK_2026_10_06_UA.md) — частковий інженерний крок із native доказами, не нова прийнята версія чи відповідність концептам. Нижче збережено історичні плани й checkpoints; старий deferred-Dynamic порядок не переважає цю вимогу.

**06.10.2026 — геометричне калібрування матеріального шляху.** [Exporter та прямий GPU P/N-контроль](MATERIAL_GEOMETRY_CALIBRATION_CHECK_2026_10_06_UA.md): 15/15 selected tests, 72 geometry samples, 15 production lighting PNG. Це діагностична геометрія, не новий вигляд гри: concept-art kit, повний graph/ефекти й ≥120 FPS ще не прийняті.

**05.10.2026 — наступний прохід у роботі.** [Мінікарта, підлісся та окрема перевірка генератора](MAP_GROUND_CHECK_2026_10_05_UA.md). Наведені нижче 92/92 стосуються збереженого попереднього checkpoint/r2, не поточного дерева після нових змін.

**05.10.2026 — фоновий checkpoint, REBUILD / не прийнято.** Повний suite **92/92**, native matrix **60/60**, звичайні fixed-seed старти **8/8**. [Актуальні виправлення й построкова звірка](BACKGROUND_CHECK_2026_10_05_UA.md), [концепт і native кадри поруч](BACKGROUND_COMPARE_2026_10_05.html). Нижчі записи є історією; технічний PASS не замінює поточне художнє приймання.

**30.09.2026 — попередній rework відхилено користувачем.** Поточне повторне відтворення: [CONCEPT_REBUILD_2026_09_30_UA.md](CONCEPT_REBUILD_2026_09_30_UA.md). Наведені нижче 91/91 та 60 captures стосуються старої збірки й не є прийманням нинішнього арту/генератора. Новий результат ще **IN PROGRESS / NOT ACCEPTED**.

**30.09.2026 — повторне приймання: PARTIAL.** Позначки «реалізовано локально» нижче описують наявність коду, а не підтверджену відповідність орієнтирам. V01–V03 отримали кумулятивну переробку: HUD/F1/Escape, calm terrain/shared UV/bounded mips, масштаби об'єктів і зимові II-level sprites. Остаточні 91/91 CTests та 60 native D3D12 matrix cases пройшли, але повна художня відповідність ще не прийнята. [Построковий acceptance checklist](V01_V03_REWORK_ACCEPTANCE_UA.md) і [звіт із залишками та запуском](../verification-rework-0.26.3-uk.md) мають пріоритет над старими загальними позначками. Старі звіти збережено як історію.

**Реалізація по сторінці на оновлення:** [V01–V05 / DL, 28.09](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md). Цей детальний план уточнює залежності, assets, UI scale, compatibility та native acceptance.
27.09.2026. Цей порядок виправляє попередню галерею, де майже всі картинки одразу показували завершений новий стиль.

**Починати з [нової галереї прогресу](index.html).** Старі 13 макетів описують публічні gameplay версії і збережені в [окремій галереї](public-versions.html); вони більше не є основною шкалою зміни графіки.

V00 — справжній збережений кадр 0.26.0. V01 реалізовано локально як 0.26.1; його native evidence і точні межі наведено у [звіті перевірки](../verification-0.26.1-uk.md). V02 реалізовано локально як 0.26.2; [звіт перевірки](../verification-0.26.2-uk.md) відділяє runtime-доказ від макета. V03 реалізовано локально як 0.26.3; [звіт](../verification-0.26.3-uk.md) відділяє runtime-кадри від imagegen-орієнтира. V04–V05 залишаються послідовно відредагованими imagegen-макетами. Вони зберігають приблизно одну сцену і демонструють накопичувані зміни; AI не гарантує незмінності кожного пікселя.

## Що змінюється після кожного кроку
V-номери — візуальні внутрішні етапи, не нова нумерація публічних релізів 0.26.x. Для базової графічної гілки цей порядок має пріоритет над старою послідовністю «повний Dynamic proof перед HUD»: користувач відклав Dynamic.

| Крок | Видима різниця від попереднього | Що лишається | Реалізація і критерій |
|---|---|---|---|
| V00 — база | Нічого; поточні старі UI/земля/спрайти | Незмінений реальний PNG | Capture provenance, commit/версія; не видавати generated image за baseline |
| V01 — UI | Компактні ресурси; нижній inspector; горизонтальні дії; прибраний великий правий блок | Старі земля/рослини/будівлі/мешканці, денне світло | RmlUi/RCSS/GameUi, справжні дані/команди, 720p/locales/input; прогноз ще прихований |
| V02 — світ і поверхні | Земля з матеріальними переходами, групи природи, річка і береги | UI V01 та старі building/unit assets | Спільні terrain/path/build/picking rules, generator/version/save, no Road; 10 seed reachability, берег без швів |
| V03 — будівлі та люди | Узгоджені силуети споруд, основи, матеріали, читабельні мешканці/вантаж | Карта/береги/UI попередніх кроків | Малий kit → runtime proof → доступний каталог/8 facing; реальні jobs/cargo, без нових непередбачених споруд |
| V04 — дощ | Опади, мокрі темніші матеріали, погода/температура/прогноз | Ті самі UI-зони, шість споруд, карта й видиме населення | Спершу WT simulation/save/net/current/forecast; потім FX; pause/2×/reconnect, тільки правдиві цифри |
| V05 — зима | Snow cover, снігопад, зимова палітра й погодне попередження | Розташування об'єктів і попередні системи | Authority cover/melt, material masks, bounded FX, native transitions; сліди — окреме приймання, не автоматично готові |
| DL — пізніше | Динамічна реакція землі/фасадів/мешканців на джерела + перемикач | Обидва режими мають однакові правила гри | [Майбутній план](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md). Зараз тільки scaffold; окремого готового screenshot немає |

Дощ/зима — два видимі milestones, але спільні authoritative weather dependencies можна реалізувати одним попереднім технічним пакетом. Технічні зміни без видимої різниці не отримують вигаданого «нового красивого кадру».

## Конкретні завдання для агентів
**V01:** переносити HUD у справжній RmlUi, а не малювати його PNG. Використати чинні resource/selection/command дані. Зберегти доступ до деталізації складу, будівництва й меню; перевірити Escape/focus/mouse capture. Графіку світу в цьому diff не замінювати.

**V02:** узгодити renderer surface із path/build rules до нової долини. Прибрати ручну Road/bonus за server/client compatibility contract. Не міняти footprint будівель заради намальованого берега. Вибрати demo seed, перевірити доступність їжі/дерева/каменю/шахти; після цього оформлювати material transitions і рослинність.

**V03:** калібрувати export camera/pivot/scale на одній споруді; зробити house/store/workshop kit і мешканця, перевірити рух на V02. Потім поширити стиль на доступні типи, House II/Workshop II, construction/winter/selection. «Моделі» в цій гілці означають вигляд об'єктів і production masters; перехід runtime на повний 3D не замовлений.

**V04:** пройти [weather spec](../../.omx/plans/2026-09-26-weather-implementation-spec-ua.md) до показу °C і прогнозу. Rain береться з host state, wetness не вигадується клієнтом. Постійні калюжі, фізичне затоплення і dynamic reflections не додаються приховано. Дощ у макеті показує бажаний вигляд, відео має довести справжній рух.

**V05:** та сама сцена взимку, сніг на придатних поверхнях, узгоджений покрив/танення. Не заморожувати воду, не вводити утеплення/паливо/HP damage. Footprint cache є окремою роботою з [World FX](WORLD_FX_SPEC_UA.md); наявність снігової текстури її не реалізує.

## Правило порівняння
Для кожного кроку зняти попередній і новий executable на тому самому seed/save, camera center/zoom, resolution, UI scale і simulation state. Під кадром записати тільки фактичний diff. Якщо V01 раптом містить нову річку/моделі — це неправильний доказ; розділити реалізації й captures. Для V04/V05 погода є контрольованою відмінністю.

Шість UI-зон і загальний художній напрям походять із концепту користувача. Випуск не зобов'язаний точно повторити AI-пікселі; він зобов'язаний довести описані матеріали, масштаб, функції та стан.

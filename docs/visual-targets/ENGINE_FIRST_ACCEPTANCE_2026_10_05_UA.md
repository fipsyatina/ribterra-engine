# Спочатку повний графічний двигун — обов'язковий поріг приймання

Дата: 05.10.2026. Статус: **ENGINE IN PROGRESS / НЕ ПРИЙНЯТО**.

CPU follow-up 09.10: [нейтральний XYZ draft](GAMEVIEW_NEUTRAL_EXPORT_CHECK_2026_10_09_UA.md) пройшов два фактичні експортерні прогони та незалежний review. Чотири neutral collections побайтно однакові, старий режим збережено; 19 649 пікселів старого будинку ще не покрито draft геометрією. Перевірені C++ зміни застосовано з точними архівами, fresh strict/native outline/picking/cold-boundary докази ще pending. Це частина E13, усі E01–E17/P01 і повна ціль лишаються OPEN/ACTIVE.

Новіший bounded checkpoint08.10: [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md), fresh build02 та native01–02 actual exit0; 42GPU PNG+raw+JSON (44/44 exact). Діагностичні hour/cloud/haze керують спільно Sun/Moon/ambient/atmosphere; source-specific shadows, horizon on/off та causal local/emission перевірені. Normal integration, authoritative weather, native input/video, concept fidelity й ≥120FPS не прийняті. **E06 і всі E01–E17 OPEN.**

Частковий checkpoint 08.10: [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) перевірено двома native GPU прогонами в технічній GameView сцені. Це не закриває E09 або будь-який E01–E17; normal startup, спільна сцена, raw coverage/AA, ввід/відео, концепт-арт і 120 FPS лишаються відкритими.

Новіший follow-up: [raw binary carrier / odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md) пройшов native05–06; binary packing і конкретний rollback доказані. Це не intermediate AA або повний E09. AllE01–E17 лишаються OPEN.

Наступний bounded checkpoint: [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_CHECK_2026_10_08_UA.md), fresh build03 і native02–03 exit0, 26 PNG + два raw downloads + JSON (29/29 derived outputs byte-exact). Перевірені camera-relative depth, height falloff, coast proximity, diagnostic time/weather, disabled identity, UI/reveal і 32/33-region transaction. Це не authoritative weather, full-world/video/performance або концепт-арт. **E10 і всі E01–E17 OPEN.**

Пряма вимога користувача: після повної реалізації всього потрібного двигуну, з перевіркою найменших видимих деталей за концептами, і тільки тоді продовжувати гру. Цей порядок має пріоритет над старими планами, що відкладали Dynamic або дозволяли спочатку нарощувати HUD/каталог арту. Попередні вимоги V01–V03, F1 та протоптаних рухом стежок не скасовані; їхня подальша реалізація чекає цього порога.

## Межа поточної роботи

Дозволені зміни: renderer, GPU-ресурси, шейдери, матеріали, засоби перевірки, мінімальний контрольний набір ассетів, presentation-адаптери та підключення цього двигуна до справжнього GameView. Останнє потрібне для доказу роботи двигуна, а не для додавання механік гри.

До приймання двигуна не продовжувати ігрові механіки, генератор/баланс ресурсів, save/network expansion, новий каталог будівель/персонажів або подальше оформлення HUD. Уже наявні зміни й preview зберегти. Не змінювати launcher на проміжний shader probe. Пряме наступне доповнення користувача — максимально практична оптимізація, графічні налаштування, високий FPS та плавність — входить саме до фази двигуна (E16–E17), а не відкладається до гри.

Основа порівняння: [зима](images/target-0261.png), [дощ](images/target-0261-rain.png), [буря](images/target-0261-storm.png), [літній світ](images/target-0262.png), [ферма/мешканці](images/target-0263.png). Прямі пізніші вимоги додають причинне освітлення мешканців, світанковий/водний туман і місячне світло. Статичні концепти визначають художній вигляд; рух, зміна погоди й реакція світла перевіряються окремо відео.

## Перелік приймання двигуна

Новий частковий checkpoint08.10: [автоматичне локальне світло вікон](GAMEVIEW_WINDOW_LIGHT_CHECK_2026_10_08_UA.md). Strict GameView build10 exit0,27zero-command-sidecars/96source/8protected/69shader/52kit bindings; native06/07 кожний root-run exit0,13PNG1920×1080+JSON,14/14files byte-exact. Source on/off причинно змінює і emission, і освітлення real ground/typed resident; незалежні red/blue blocker controls,6CPUcamera cases і7+6transactional cases пройдені. Native exit sidecar не збережений; GPU timing/FPS/відео не виміряні. Це нейтральний opt-in контроль, не concept art або normal startup. **Усі E01–E17 OPEN.**

Наступний частковий checkpoint08.10: [bounded real GameView control](GAMEVIEW_ENGINE_CONTROL_CHECK_2026_10_08_UA.md), свіжа strict build05 exit0,21productionTUs+probe,96source/8protected hashes, native01/02 кожний exit0,5PNG1920×1080+JSON,6/6repeat files byte-exact. 289realterraincells/8neutralcontrols,53 850caster-dependent pixels,Sun/Moon,hidden-object filtering і8cell-picks. Нормальний GameView/launcher не переключений, fullcamera/worldobjects/localpixelproof/concept/FPS/CTest не прийняті; усі E01–E17 OPEN.

Поточний authored native checkpoint 08.10: [реальний authored GPU path](AUTHORED_SKY_NATIVE_CHECK_2026_10_08_UA.md) пройшов два прогони exit0,13 checks/32PNG кожний,32/32repeat PNG і54/54derived files byte-exact. Свіжа strict build10 та focused verifier — exit0;104 source entries/12 protected anchors unchanged до окремої наступної інтеграції GameView. Попередні native01–04 були невдалими діагностованими спробами, не прийнятими checkpoints. Це обмежений fixed-camera компонент, **не** повний двигун, концепт або закриття E-пунктів. Очищення82,79GB історичних runtime-копій не змінює приймання двигуна; чинні докази/launcher збережені.

Попередній authored checkpoint08.10: [loader/adapter proof](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md),41 exporter negatives,13 focused groups×2/19 schema invalids/13 transactional failures,22 material regression PNG,103 source hashes і12 prior evidence files unchanged. Тоді authored GPU shadow pixels були NOT_RUN; новий обмежений native доказ наведений вище. Усі E01–E17 OPEN, до розробки гри не переходили.

Частковий directional-shadow checkpoint08.10: [незалежні D32 Sun/Moon maps](SKY_SHADOW_CHECK_2026_10_08_UA.md),110CPUchecks,12nativegroups×2,27/27repeatPNG,26Sky/31Contact/56shader byte-exact regression, transactional targets і actual cross-slot controls. Тільки fixed-camera prerequisite; authored adapter почато окремо і ще не прийнято. **E01–E17 OPEN; concept fidelity, full GameView і≥120FPS не підтверджені; гра чекає.**

Частковий Sky direct checkpoint07.10: [незалежний moon channel](SKY_DIRECT_CHECK_2026_10_07_UA.md) у явному opt-in graph, native causal sun/moon pixels на ground/typed fixture, незмінні старі shaders/contact captures. Це ще НЕ directional shadows/time/weather/art або full-engine/FPS acceptance. **Усі E01–E17 OPEN; гра чекає.**

Частковий Contact GPU checkpoint07.10: [versioned contact graph + immutable R8 registry](CONTACT_GPU_CHECK_2026_10_07_UA.md),211CPUassertions,actual shader positives/repaired negatives,53nativechecks×2 і31/31 byte-exact repeated PNG, fresh selected renderer software/D3D12 regression та independent partial approval. Це не raw-W/video/art/concept/fullGameView/performance acceptance. **Усі E01–E17 залишаються відкритими; до гри не переходили.** Старі CPU-only записи нижче — історичні.

Кожен пункт нижче поки **ВІДКРИТИЙ**. Частково наявний код або попередні тестові звіти не означають його завершення.

Перший частковий крок: [native перевірка bounded surface-lighting ядра](SURFACE_LIGHTING_CHECK_2026_10_05_UA.md), 4/4 targeted tests, D3D12 proof і legacy regression. Це не закриває E01/E02/E04 повністю й не є performance proof.

Наступний частковий крок: [presentation settings, GPU-resource reuse та локальне профілювання](PRESENTATION_OPTIMIZATION_CHECK_2026_10_05_UA.md), 10/10 targeted tests, native failure/visual checks і окремий ETW trace. 145.16 CPU-loop FPS без trace отримано лише у статичній legacy-сцені; у trace Immediate/HUD частота валідних display intervals — 118.72 Hz у режимі, де tearing було дозволено (`AllowsTearing=1`, не доказ фактичного tearing), з явними обмеженнями методики. Це **не** закриває E16/E17, не підмінює full Dynamic scene та не змінює жоден статус нижче.

Новий частковий крок 05–06.10: [лінійний HDR світу / Composite / незмінний UI](HDR_COLOR_CHECK_2026_10_06_UA.md), 10/10 targeted tests, 34 native HDR captures, pixel-checked resize rollback обох frame slots, 3/3 supported presentation modes і 15/15 byte-identical Legacy regression images. Це лише color/resource foundation у production renderer; не повний шестипрохідний graph, не Dynamic-матеріали, тіні/атмосфера або ≥120 FPS proof. **E01–E17 лишаються відкритими; гра чекає приймання двигуна.**

Новий частковий крок 06.10: [typed material bridge / schema 2 / спільна проєкція](MATERIAL_BRIDGE_CHECK_2026_10_06_UA.md), 13/13 targeted tests, 22 native analytical material captures, перевірка sRGB filtering, normal/depth/emission, atlas edges і candidate rollback. Це prerequisite для справжнього art kit, не його заміна: поточний GameView лишається BakedCompat, повний graph/shadows/atmosphere/120 FPS не прийняті. **Усі E01–E17 відкриті.**

Наступний частковий крок 06.10: [редагована геометрія / детермінований exporter / прямий GPU P/N](MATERIAL_GEOMETRY_CALIBRATION_CHECK_2026_10_06_UA.md), 15/15 selected CTest, 72 interior geometry samples і 180 production color samples. Максимальна native reprojection похибка 0.3121 px у цьому 1280×720 control matrix. Diagnostic cabin/slope не є concept-ready будинком чи повним neutral kit; probe MRT не є production render graph. **Жоден E01–E17 не закрито.**

Новий частковий крок 07.10: [production GroundGBuffer → GroundLighting → WorldForward → Composite → UI](GROUND_FORWARD_CHECK_2026_10_07_UA.md), 16/16 selected CTest, 34 native PNG, causal sun/point/spot/emission controls, ±32 precision, two-set cross-slot equality, strict 4:3 resize і три transactional allocation faults. Legacy 15/15 PNG лишилися byte-identical. Це opt-in production slice, не Reflection/FoW/shadows/atmosphere чи concept-ready kit; звичайний GameView лишається BakedCompat. **E01–E17 усі відкриті; ≥120 FPS у 1080p не виміряно.**

Новий частковий крок 07.10: [owner-aware local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md),16/16 selected CTest,49 Local PNG,34 Ground regression PNG, незалежний review та15/15 byte-exact Legacy PNG. Authored OBB/capsule prisms перекривають лише відповідне local джерело; owner/reveal/mask і двослотове buffer recovery перевірені. Це не sun/moon/contact shadows, complete graph або concept-ready GameView. **Усі E01–E17 відкриті; ≥120 FPS не виміряно; гра чекає.**

Наступний частковий CPU checkpoint07.10: [Contact CPU contract](CONTACT_CPU_CONTRACT_CHECK_2026_10_07_UA.md), clean standalone MSVC C++20/W4/WX,157CPUassertions та bounded independent approval. Typed metadata/geometry validator і versioned receiver design не підключені до GPU. Registry/art/adapter/projection/shader/renderer/native pixels/motion/performance ще відсутні. **Жодного E-пункту цим не закрито; full-engine gate і120FPS лишаються відкритими.**

| ID | Що має реально працювати | Спостережуваний критерій |
|---|---|---|
| E01 | Один координатний контракт і безпечні GPU-ресурси | Узгоджені world XYZ, normal, projection, viewport/DPI, input; перевірені shader ABI й формати. Resize/minimize/restore, frame fences і збої allocation/upload не залишають старих або напівстворених ресурсів. |
| E02 | Виробничий render graph і колір | Проходи Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite/FoW → UI за Core spec. Лінійне HDR-освітлення; одноразове sRGB decode/encode; правильна alpha; немає читання активного render target або повторного затемнення emission. |
| E03 | Справжні поверхні рельєфу | Рівна земля, схил, скеля, берег і міст мають правильні висоти/нормалі. Світло, тіні, feet/picking/build-preview спираються на одну поверхню. Pan/zoom не зрушує їх відносно світу. Якість генерації карти перевірятиметься пізніше. |
| E04 | Освітлення всіх видимих категорій | Земля, фасади, дахи, дерева, мешканці та вантаж реагують попіксельно на ambient, напрямлене сонце, point/spot джерела. Переміщення лампи змінює освітлену сторону без зміни albedo. Суцільне екранне підфарбування не зараховується. |
| E05 | Вікна, вогонь, кузня | Окрема emission-маска та world-anchored джерело. Увімкнення/вимкнення змінює і джерело, і його освітлення придатних сусідніх поверхонь. Радіус, конус, falloff і перекриття об'єктом працюють; пляма не намальована в PNG. |
| E06 | Час, сонце, місяць і погода | Плавна послідовність день → захід → ніч → світанок змінює напрям/інтенсивність/колір світла та тіні. Хмари/буря послаблюють денне світло. Місяць дає окремий керований холодний внесок; вікна залишаються теплими, а HUD не тонується. |
| E07 | Контактні, напрямлені та локальні тіні | Будівлі, дерева, мешканці та вантаж мають доречні тіні. Рух і напрям світла змінюють силует. Пересунутий blocker перекриває тільки відповідне джерело; два різноколірні джерела перевіряються незалежно. Немає протікання на дах/воду/UI/невідоме поза допустимим AA-краєм. |
| E08 | Матеріали й мокрі поверхні | Normal/material/depth/emission канали справді використовуються. Сухо → дощ → висихання змінює roughness/specular лише придатних поверхонь; відблиск залежить від світла, не від намальованої плями. Немає блиску на UI чи невідповідних receiver. |
| E09 | Вода, береги та відображення | World-anchored flow/ripples, піна біля берега, узгоджений світловий блиск. Обмежені слабші/спотворені відображення зникають разом із caster та не накривають берег, міст або невідоме. Pan/zoom/pause не змінює прив'язку. Форма річки — наступна фаза генератора. |
| E10 | Просторова атмосфера | Серпанок розділяє близькі/далекі плани; окремі ранковий і прибережний тумани. Щільність/колір відповідають часу й погодному входу. Це не сіра завіса і не підміна Fog of War. Контури прихованих об'єктів не витікають. |
| E11 | Дощ, сніг, вітер, буря | Частинки, пориви й їхня взаємодія з receiver/reveal працюють у русі. Позитивний дощ не породжує сніг. Pause зупиняє ефекти, 2× узгоджено змінює час, reset/epoch не створює сплеску накопичених частинок. Дим та рослинність мають погоджений wind response. |
| E12 | Снігові матеріали й сліди | Візуальне покриття правильних поверхонь; L/R footprints за фактично показаним рухом, повороти, fade/burial/melt. Немає штампів при стоянні/teleport чи на заборонених receiver. Без прихованих змін path, elevation або швидкості. |
| E13 | Матеріальний pipeline і мінімальний art kit | Контрольні terrain/water/bridge, будинок, склад, майстерня, дерево, мешканець і вантаж мають узгоджені нейтральні канали, pivots/anchors/occluders. Атлас і паспорт перевіряють проєкцію, alpha, колірний простір та необхідні карти; неповний Dynamic-матеріал відхиляється транзакційно. Масовий каталог ще не виробляється. |
| E14 | UI-підсистема без світлових пошкоджень | Окремий незатемнений UI-прохід; nine-slice, іконки, український шрифт, scissor/clip, DPI/hit mapping; читабельність на 720p. F1 приховує/повертає весь HUD без зміни світу. Подальша композиція/ігрове наповнення HUD — після порога. |
| E15 | Спільна виробнича перевірка | Усі вказані ефекти разом у тому самому production renderer, не в окремій showcase-картинці. Raw captures і немонтоване відео, side-by-side з концептами, незалежний огляд, counters, frame-time/VRAM та failure checks. Жодна обов'язкова категорія не вимкнена мовчки. |
| E16 | Справжні налаштування графіки | Low / Medium / High та Custom; доступні ручні параметри змінюють реальні GPU-ефекти. VSync, frame limit/uncapped, режим екрана, resolution/render scale, якість тіней/світла/води/туману/частинок/деталізації. Показані requested/effective значення; unsupported не видається за ввімкнене; apply/revert/persistence/restart/resize без зламаного кадру. |
| E17 | Оптимізація та плавність | Повна контрольна сцена, 1080p, RTX 3050: ціль ≥120 FPS без відключення обов'язкових категорій. Виміряні CPU/frame/GPU budgets, p50/p95/p99/max, VRAM, stalls і прогрівання. Pan/zoom, нові chunks/ассети/ефекти, тривалий прогін не приховують hitch/stutter за високим average FPS. |

## Порядок реалізації і докази

1. **Контракти та світлове ядро:** E01–E02, попіксельний доказ P/N, gamma/alpha/ABI, lifetime. Поточний чотириджерельний opaque surface kernel — тільки початкова частина; він не є Dynamic і не є кінцевим HDR-проходом.
2. **Поверхні й матеріали:** E03–E05/E08/E13, мінімальна сцена на виробничому шляху, усі категорії receiver.
3. **Час і затінення:** E06–E07, реальний місячний внесок, blockers/силуети, перевірка незалежних джерел.
4. **Вода й світові ефекти:** E09–E12; поступово, зі спільним повторним прогоном після кожного підключення.
5. **Загальний поріг:** E14–E17, налаштування, якість, навантаження, smoothness та fault cases. Telemetry й performance regression вводити вже з ранніх кроків, не лише оптимізувати наприкінці. Лише потім продовжувати гру за початковими вимогами V01–V03.

Контрольна сцена: підтриманий схил/скеля/рівнина, два береги та міст, вікна/кузня/вогонь, дерево/камінь-blocker, мешканці без вантажу/з колодами/мішком. Вона позначається diagnostic, не підмінює звичайний старт. Прототипні час/погода подаються через явний відтворюваний fixture input: це доказ renderer response, не готовності host-authoritative погоди чи forecast.

Обов'язкова матриця: день/ніч/ранок; літо/зима; clear/rain/wet/snow/storm; pan і zoom 0.62/1.30/3.00; перед/за об'єктами, берег/міст, reveal frontier; pause/2×/epoch reset; 720p/1080p/1440p, 100/150% DPI, resize/restore; Low/Medium/High. Записати exe/shader/asset hashes, backend/GPU/driver/resolution, camera/seed/fixture inputs. PNG не замінює temporal video, software — native D3D12, green test suite — візуальне приймання.

Пропоновані в наявних spec spatial tolerances (не оголошувати виміряними до прогону): anchor drift ≤2 physical px, reprojection ≤1 px і ≤0.02 tile, leakage лише в межах ≤1 px AA-краю.

**Остання пряма вимога користувача: мінімум 120 FPS у 1920×1080 на RTX 3050.** Вона замінює попередню відповідь про 60 FPS. Для поточного ноутбука перевірити фактично обрану RTX 3050 Laptop, driver, power mode та resolution. Орієнтир часу кадру — ≤8.33 ms; записувати p50/p95/p99/max і кадри понад бюджет, а не лише середнє FPS. Не зараховувати результат з вимкненими обов'язковими ефектами, урізаною контрольною сценою або зниженою внутрішньою роздільністю, не названою у звіті. Завантаження/компіляцію показувати окремо від сталого ігрового прогону; GPU captures/readback також окремо від performance sampling. **Ціль ще не досягнута й не перевірена.**

## Рішення, які ще треба закрити

- Канонічний world/render metric повинен збігатися з реальною проєкцією. Поточний surface header документує `renderZ = legacyElevation * 2.4 / 29`; старий текст Core spec із `/29` не можна механічно застосувати. Потрібен спільний adapter і reprojection test, не прихована зміна висот гри.
- Узгодити та перевірити GPU format/usage support, swapchain gamma, depth precision/basis, slope-shadow handling, GPU timing, caps і власника UI composition. Це відкриті рішення Core spec, не виконані фічі.
- Задати окремий контракт сонця/місяця: криві, напрям, інтенсивність, колір, хмарність, взаємодія з ambient. Синій fullscreen tint нічого не доводить.
- Відтворювано перевірити погоджений performance baseline: ≥120 FPS, 1920×1080, RTX 3050, усі обов'язкові ефекти. Остаточні VRAM/caps і метод GPU timing ще потребують реалізації та вимірювання; average FPS не приховує просідання.

## Налаштування й оптимізація — доповнення користувача

- **Профілі якості:** початкові Low/Medium/High за World FX spec плюс Custom. Вони мають явні effect budgets, LOD/щільність декору, роздільність тіней/відбиттів, particle/haze caps і допустиму якість матеріалів. Назва preset не є доказом швидкодії. В обов'язковій контрольній сцені кожен профіль зберігає всі потрібні категорії ефектів; silent disable не зараховується.
- **Ручне керування:** світло/тіні, reflections/water, haze, particles, деталізація, render scale; display mode/resolution, VSync і FPS limiter з uncapped режимом. UI лишається у native drawable resolution при зміні лише world render scale. Не показувати фіктивні перемикачі ще не реалізованих можливостей як працездатні.
- **Безпечне застосування:** settings локальні, не змінюють simulation/save/network state. Перевірка діапазонів і GPU capability, чіткий requested/effective стан, Apply/Cancel/defaults, відновлення після невдалого display/resource change. Зміни режиму екрана потребують безпечного revert, щоб користувач не залишився без зображення.
- **Спочатку вимірювання:** відокремлювати simulation/update, scene composition, RmlUi, command preparation/upload, GPU execution, fence/present wait, pacing і capture overhead. Не називати CPU submit duration часом GPU. Записати підтримуваний фактичний present mode і monitor refresh; VSYNC-limited або hidden-window submit throughput не є доказом displayed FPS.
- **Оптимізувати виявлені вузькі місця:** visibility/chunk culling, LOD і stable effect budgets; batching лише там, де він не змінює painter order/alpha; повторне використання buffers/atlases/caches; усунення повторної роботи та зайвих allocations/uploads; асинхронне завантаження/прогрівання лише з перевіреним lifetime і явним diagnostic. Це кандидати для профілювання, не твердження, що всі вже реалізовані.
- **Плавність:** тривалий steady-state прогін із pan/zoom і появою нових об'єктів; frame-time distribution, over-budget frames, stalls, 1%/0.1% lows із задокументованою методикою. Окремо тестувати cold start/loading, pause/resume, focus/minimize/restore. Високий FPS без рівного frame pacing не закриває E17.
- **Два різні результати:** concept-faithful baseline на 100% render scale із усіма обов'язковими ефектами та окремо максимальна виміряна продуктивність інших явних налаштувань. Профіль зі зниженим масштабом/щільністю не може потай замінити baseline 1080p у звіті.
- **Необмежені обіцянки заборонені:** «максимально можлива» означає послідовні виміряні покращення без регресій якості/плавності. Не заявляти абсолютний максимум будь-якої сцени, конфігурації чи кількості юнітів. Погоджений числовий поріг лишається мінімум 120 FPS; очікувана верхня межа встановлюється фактичним benchmark.

## Що не є необхідною заміною вимоги

Не обіцяти довільну 3D-камеру, GI/ray tracing, fluid solver, фізично об'ємну атмосферу, деформацію геометрії снігу чи rigid-body/cloth лише через слово «детально». Обмежена 2.5D реалізація дозволена тільки якщо її результат витримує наведені перевірки і вигляд концептів. Якщо ні — переглянути конкретне обмеження, а не оголосити ефект готовим.

## Умова переходу до гри

**Усі E01–E17 закриті актуальними доказами, обов'язкові дефекти відсутні, спільний native прогін і незалежне порівняння з концептами пройдені.** Ні один із цих пунктів зараз не позначений повністю прийнятим. До того подальша гра чекає; загальну ціль ще не завершено.

Технічні джерела: [Render Core](RENDER_CORE_SPEC_UA.md), [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md), [World FX](WORLD_FX_SPEC_UA.md), [Engine plan](GRAPHICS_ENGINE_PLAN_V2_UA.md), [Art production](ART_PRODUCTION_SPEC_UA.md). Пряма нова вимога має пріоритет над їхнім старим порядком фаз.

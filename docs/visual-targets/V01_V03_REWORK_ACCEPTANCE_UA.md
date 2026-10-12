# Ribterra — acceptance checklist повної переробки V01–V03

Дата створення й перевірки: 30.09.2026. Статус документа: **построкова перевірка; загальний результат PARTIAL**.

Цей документ визначає один кумулятивний кінцевий стан після V03: **HUD V01 + світ V02 + об'єкти V03**. Позначка `[ ]` означає «ще не прийнято», навіть якщо код, тест або старий screenshot уже існує. Позначка може стати `[x]` лише після внесення конкретного evidence path, команди або запису.

Позначення доказів **T/M/A/R/P** розшифровані у [звіті повторного приймання](../verification-rework-0.26.3-uk.md). `PASS` стосується лише конкретного рядка; `PARTIAL` означає наявний результат із зазначеним недоведеним залишком; `NOT RUN` не замінюється припущенням про працездатність.

## 1. Джерела і межа трактування

- Основний художній напрям: оригінальне референсне зображення користувача; воно задає композицію, масштаб, ієрархію, матеріали й читабельність, але не вигадує gameplay-стан.
- Локальні покрокові орієнтири: [V01 HUD](progress/v01-ui.png), [V02 terrain](progress/v02-terrain.png), [V03 objects](progress/v03-objects.png). Це generated targets, не runtime evidence і не вимога піксельної тотожності.
- Технічний контракт: `GRAPHICS_PROGRESS_UA.md` і `.omx/plans/2026-09-28-graphics-updates-implementation-ua.md`.
- Фактичні дані, доступні команди, collision, pathfinding, save і network authority мають пріоритет над декоративною схожістю.
- V01–V03 не додають температуру, прогноз, дощ, мокрість, снігопад, snow-cover simulation або Dynamic Lighting; це наступні окремі етапи.
- Старі public targets **S01 weather / S02 housing / S03 farm** не перейменовуються і не вважаються частиною цієї V01–V03 переробки. Їхній зв'язок із версіями очікує окремого уточнення користувача.

## 2. Верхньорівневий кумулятивний gate

| Gate | Критерій кінцевого стану після V03 | Статус | Evidence |
|---|---|---|---|
| G1 | HUD відповідає композиційній ієрархії референсу, лишає центр світу відкритим і показує тільки правдиві дані | [ ] PARTIAL | M/P: композиція HUD перероблена; художня відповідність і повна UI acceptance ще відкриті. |
| G2 | F1 повністю приховує і повертає HUD без зміни selection, pause, simulation або camera | [ ] PARTIAL | M/P + ui: hide/restore і repeat перевірені; повний invariant placement/command stream окремо не доведений. |
| G3 | Світ читається як долина: одна зв'язна річка, береги, проходи, широкі матеріальні зони й згрупована природа | [ ] PARTIAL | A/M + terrain_v02: долина й зв'язність; великі зимові матеріальні межі ще помітні. |
| G4 | Реальні маршрути юнітів поступово утворюють видимі протоптані стежки; вони зберігаються й синхронізуються, але не дають speed bonus | [ ] PARTIAL | terrain_v02/save: wear від реального руху; немає native відео накопичення та окремого двоклієнтного wear comparison. |
| G5 | Camp + усі 11 доступних buildable типів мають узгоджені construction/ready/winter стани; House II і Workshop II мають правдивий level-II вигляд | [ ] PARTIAL | A + v03_asset_passport: coverage/II/winter є; остаточна художня узгодженість не прийнята. |
| G6 | Мешканці читабельні у 8 напрямках, несуть фактичний вантаж і правильно контактують із поверхнею | [ ] PARTIAL | R + unit_motion: 256 frames/actual deliveries; all-facing/painter-order video відсутнє. |
| G7 | Той самий certified seed/save доводить кумулятивну сцену V03 у native D3D12, а не в imagegen або software probe | [x] PASS | M/A/R: certified native D3D12, seed/save/camera/zoom у manifest; staged fixture явно позначена. |
| G8 | Немає регресій gameplay, save/load, local host/join, picking, UI input, package staging і старих підтриманих saves | [x] PASS | T: 91/91 на остаточній збірці + A/R/P; висновок обмежений перевіреним локальним набором, не physical/cross-network. |

**Стоп-умова:** V01, V02 або V03 не називається `PASS`, доки всі обов'язкові пункти відповідного розділу та його evidence rows не мають `[x]` і відтворюваного доказу.

## 3. V01 — HUD і керування

| ID | Видимий або поведінковий критерій | Статус | Evidence |
|---|---|---|---|
| V01-01 | Зверху ліворуч компактно показані available wood/stone/food і population; tooltip пояснює «доступно» | [x] PASS | ui + P: available resource counts і population беруться з ClientSession; доступність пояснює stocks. |
| V01-02 | Клік ресурсу відкриває stocks із розділеними stored/reserved/carried/ground/manufactured; population відкриває settlement | [x] PASS | ui/settlement_ui/building_inspector + P: resource click, authority stock categories; population routing. |
| V01-03 | Зверху праворуч є максимум одне дієве попередження з реальною причиною та наступною дією; без вигаданих температури чи прогнозу | [ ] PARTIAL | M + game_ui.cpp: одне real warning, transient feedback; longest localized warnings не охоплені всією матрицею. |
| V01-04 | Календар і Surface/Mine лишаються доступними та не перекривають resources або warning | [x] PASS | M: календар/Surface/Mine у всіх 60 captures; польський Surface виправлений. |
| V01-05 | Нижній лівий compact inspector показує type/name/level, стан, головну причину й наступний крок; `Деталі` відкриває повний inspector | [x] PASS | building_inspector + M: summary, level/status/next step, Details; реальний resolver thumbnail. |
| V01-06 | Mixed/multi-selection показує count і тільки спільні дії, не копіює стан першого об'єкта | [x] PASS | building_inspector: group/mixed selection і common actions; жодних вигаданих загальних ресурсів. |
| V01-07 | Нижній action strip містить `Будувати`, `Збирати`, `Призначити`, `Скасувати`; disabled state має видиму конкретну причину | [x] PASS | ui/input_feedback + M: чотири дії, disabled reason для keyboard focus. |
| V01-08 | `Скасувати` завершує лише placement/worksite/drag; не знищує будівлю, production queue або resident order і не емулює Escape | [x] PASS | ui/client_input: типізований CancelInteraction, відновлення selection до drag; жодного delete/queue reset. |
| V01-09 | Pause, 1×/2×/4×, stocks, enterprises, save/load, help, menu, chat, layer та ownership/order controls доступні | [x] PASS | ui/profession_ui/production_control_ui/settlement_ui + P: існуючі controls/routing збережені. |
| V01-10 | F1 hide/show працює по edge-trigger: утримання не перемикає повторно; повернення відновлює повний HUD | [x] PASS | ui + M/P: F1 repeat=true не перемикає повторно, hide/restore підтверджено. |
| V01-11 | Hide/show HUD не змінює selection, placement state, camera, pause/speed або authoritative command stream | [ ] PARTIAL | P: pause/time/resources/camera незмінні в ручному сценарії; не всі placement/selection/authority invariants автоматизовано. |
| V01-12 | Navy/nine-patch panels, ivory text, gold 1–2 px border, blue active і red warning читаються; hit area ≥36 dp | [ ] PARTIAL | M: navy/ivory/brass та focus; ornate nine-patch і суцільна ≥36 dp перевірка не завершені. |
| V01-13 | Hover/focus/pressed/disabled відрізняються не лише кольором; keyboard focus і IME/chat не ламаються | [ ] PARTIAL | ui: keyboard focus, disabled reason, text/IME/chat; повна visual state matrix відсутня. |
| V01-14 | У звичайному стані union HUD panels займає не більш як 30% logical viewport; центр сцени відкритий | [ ] NOT RUN | Центр відкритий у M; union площі всіх панелей не виміряно — число ≤30% не підтверджене. |
| V01-15 | Click/drag/wheel над HUD не керують світом; Escape спочатку закриває верхню panel | [x] PASS | ui/client_input + P: UI consumes pointer; stocks→Escape закриває panel і не змінює pause. |
| V01-16 | 50 циклів open/close details після warm-up не збільшують texture count | [ ] NOT RUN | UiLifecycleTests перевіряє інші lifecycle помилки, не 50 циклів texture-count inspector. |

## 4. V02 — долина, поверхні, річка і протоптані стежки

| ID | Видимий або поведінковий критерій | Статус | Evidence |
|---|---|---|---|
| V02-01 | Новий profile створює одну безперервну річку з одним water level; немає розривів, ізольованих калюж або води на суші | [ ] PARTIAL | terrain_v02: зв'язність річки на 10 seeds; повний огляд усіх water-level transitions не виконано. |
| V02-02 | Береги мають безшовні переходи на chunk edges і negative coordinates; foam/waves не малюються поверх Bridge deck | [ ] PARTIAL | terrain_v02: shared UV edges/negative coordinates; bridge foam mask у code; native bridge/shore video відсутнє. |
| V02-03 | Земля має широкі читабельні grass/soil/rock zones; дрібний контраст нижчий за контраст будівель і людей | [ ] PARTIAL | M: новий calm meadow і bounded mips; winter material boundaries лишаються видимими. |
| V02-04 | Дерева, валуни, кущі й дика їжа утворюють групи, не рівномірний шум і не візуальну непрохідну стіну | [ ] PARTIAL | M: згрупована природа і jitter; щільність/біомна стилізація ще відрізняються від концепту. |
| V02-05 | Є рівна стартова галявина, ≥80 reachable buildable cells і досяжні wood/stone/food/clay/mine для 10 fixed seeds | [ ] PARTIAL | terrain_v02: 10 seeds, ≥80 cells, wood/stone/food/mine PASS; окремої clay assertion немає. |
| V02-06 | Renderer, movement, building preview/server validation, picking, resident feet, entry cells і bridge approaches використовують спільну surface geometry | [x] PASS | A: 1089 terrain picks, 3 zoom checks, uphill movement; terrain_v02/client_input: shared geometry. |
| V02-07 | Юніт не проходить крізь видимий cliff, не зависає над ramp і не провалюється під surface | [ ] PARTIAL | A: uphill click and movement PASS; усі cliff/ramp/bridge occlusion cases не показані у video. |
| V02-08 | Реальні traversed cells накопичують bounded wear і поступово стають видимою стежкою; невикористані клітинки не темнішають самі | [ ] PARTIAL | terrain_v02: реальний movement створює bounded wear; видимий time-lapse без руху/з рухом не знятий. |
| V02-09 | Wear є authoritative state: deterministic, save/load roundtrip, checksum і host/guest snapshot/delta збігаються | [ ] PARTIAL | terrain_v02/persistence/protocol: field/save/checksum; dedicated wear host/guest delta comparison ще потрібний. |
| V02-10 | Протоптана стежка не змінює speed, collision, path cost або buildability | [x] PASS | terrain_v02: arrival_tick однаковий, trail wear presentation-only; path planner regression. |
| V02-11 | Нова ручна Road відсутня в catalog/hotkey/help/command; legacy Road ID і old-save object збережені без speed bonus | [x] PASS | terrain_v02/alpha_rules/ui: Road ID=19 збережено; new Road rejected, Bridge available. |
| V02-12 | F1 і весь прийнятий HUD V01 працюють без регресій у новому світі | [x] PASS | M/P/ui: V01 HUD і F1 працюють на V02 generation; return не перегенеровує світ. |
| V02-13 | Старий save не перегенеровує recorded chunks новим profile і зберігає старий terrain hash | [x] PASS | terrain_v02/save_compatibility: generator 5 roundtrip зберігає checksum, recorded chunks не перегенеровано. |
| V02-14 | Native video показує рух берегом, через міст і накопичення стежки; screenshot сам не зараховується | [ ] NOT RUN | Немає немонтованого native відео bridge/shore/desire-path accumulation; screenshots не зараховані. |

## 5. V03 — малий art kit, усі доступні споруди й мешканці

| ID | Видимий або поведінковий критерій | Статус | Evidence |
|---|---|---|---|
| V03-01 | Спочатку прийнятий малий kit: House, Stockpile, Workshop і один resident на flat та slope V02 в однаковій calibration scene | [ ] PARTIAL | A: calibration/slope/zoom кадри малого kit є; окреме художнє приймання kit до розширення не закрите. |
| V03-02 | Для малого kit зафіксовано pivot/feet anchor, entry reference, silhouette bounds, projected size і scale на zoom 0.62/1.30/3.00 | [x] PASS | A + passport/building_visual: anchors/entry/bounds, zoom 0.62/1.30/3.00 і picking. |
| V03-03 | Після малого kit стиль поширений на Camp + 11 buildable types; Temple/Barracks та інші locked types не розблоковані | [ ] PARTIAL | v03_asset_passport/alpha_rules: Camp+11 covered, locked types не відкриті; gate малого kit ще PARTIAL. |
| V03-04 | Кожна доступна family має stage1/stage2/stage3/ready/winter без missing sprite, magenta fringe або anchor jump | [ ] PARTIAL | A/v03_asset_passport: 5 states без missing IDs; all-family alpha-fringe/anchor visual acceptance не повна. |
| V03-05 | `.level2` вибирають тільки завершені House II і Workshop II; upgrade до завершення показує чинний рівень і progress | [x] PASS | building_visual/upgrades/upgrade_flow: тільки завершені House II/Workshop II вибирають level2. |
| V03-06 | Winter не повертає House II/Workshop II до level I; окремий plate або документований overlay/tint зберігає правильний silhouette | [x] PASS | M winter + A/building_visual/passport: окремі House II/Workshop II winter plates зберігають II. |
| V03-07 | Візуальний scale не змінює simulation footprint, entry cell, collision або ізометричну проєкцію 58×29 | [x] PASS | building_visual/client_input/A: scale 1.35 presentation-only, bridge/Road винятки, footprint/projection не змінено. |
| V03-08 | Renderer, inspector thumbnail і picking використовують один visual resolver та однаковий фактичний level/scale | [x] PASS | A/M/building_inspector: єдиний resolver, exact stockpile picks, thumbnail/level state. |
| V03-09 | Resident coverage: 2 looks × 4 load states × 8 directions × 4 phases; anchor стабільний при русі й повороті | [ ] PARTIAL | A/R/unit_motion: 256 frames та anchors; усі повороти у безперервному native відео не перевірено. |
| V03-10 | `walk/logs/sack/cart` походить із фактичних item+quantity; після delivery/cancel вантаж зникає | [ ] PARTIAL | R: фактичні вантажі/дві доставки/save-resume PASS; cancellation і всі item-to-cart варіанти окремо не відзняті. |
| V03-11 | Contact shadow world-anchored під feet, намальована перед sprite; cart має ширший contact, але це не directional sunlight | [x] PASS | unit_motion/R: feet contact перед sprite, ширший cart shadow; не directional lighting. |
| V03-12 | Resident проходить перед/за building/tree/bridge/cliff без неправильного painter-order перекриття | [ ] PARTIAL | A/R: slope і representative objects; перед/за всіма tree/bridge/cliff не перевірено відео. |
| V03-13 | Smoke/fire/tool signal з'являється лише при фактичному active job у фазі Working | [ ] PARTIAL | work_client_rendered/code: Working gating збережено; повної native матриці inactive/paused/job фаз немає. |
| V03-14 | Stockpile piles показуються лише за наявності bounded authoritative binding; інакше не додається фальшивий stock indicator | [x] PASS | M/code: не додані фальшиві кількісні piles; static art не заявляється як облік запасів. |
| V03-15 | Atlas/passport містять stable ID, source/license, dimensions, pivot, alpha mode, family/state/season, hash і notice | [x] PASS | v03_asset_passport/asset_catalog/asset_staging: IDs/dimensions/pivot/hash/license/source/notice; provenance prompts збережені. |
| V03-16 | Native video показує 8 facing, кожен cargo type, delivery, pause, 4× і рух з обох боків споруди | [ ] NOT RUN | 256-frame sheets і delivery PNG є, але це не немонтоване відео facing/cargo/pause/4×/occlusion. |

## 6. Обов'язкова UI visual matrix

Кожна клітинка означає окремий native capture з відкритим representative inspector і найдовшим локалізованим текстом; жодна клітинка не успадковує PASS від іншої.

| Resolution \ UI scale | 0.85 | 1.00 | 1.15 | 1.30 |
|---|---|---|---|---|
| 1280×720 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |
| 1920×1080 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |
| 2560×1440 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 | CAPTURED ×5 |

| Locale | Resource strip | Warning | Inspector/details | Actions/controls | Статус |
|---|---|---|---|---|---|
| uk | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| en | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| ru | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |
| pl | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures, Surface fit fixed | [ ] PARTIAL |
| es | M: 12 captures | longest warning NOT RUN | representative stockpile; Details NOT RUN | M: 12 captures | [ ] PARTIAL |

- [ ] NOT RUN — OS DPI 100/125/150% input mapping окремо від UI scale.
- [ ] PARTIAL — representative visible rects оглянуті; автоматичного hit-rect overlap тесту кожної клітинки немає.
- [ ] PARTIAL — RenderFrameTests software/GPU перевіряє resize і letterbox coordinate roundtrip; повний click/drag/wheel/tooltip набір у кожній роздільності не виконаний.

M містить 60 ordinary + 60 staged captures, один winter та два F1 — разом 123 PNG. `CAPTURED` не є автоматичним `PASS` усіх visual/input умов.

## 7. Regression та evidence matrix

| Область | Мінімальний доказ | Статус | Evidence path / command |
|---|---|---|---|
| Source hygiene | scoped diff, `git diff --check`, без випадкового змішування weather/DL | [x] PASS | git diff --check exit0; лише CRLF warnings; unrelated dirty-tree changes збережені. |
| Focused tests | UI/input/localization; terrain/path/build/save/network; art/resolver/animation/atlas/staging | [x] PASS | T: усі перелічені тести пройшли; додатково RenderFrameTests --gpu, A/R/M native D3D12. |
| Full suite | повний configured CTest на фінальному executable/assets | [x] PASS | ctest-certified.xml: 91/91, 0 failures, 674.79 s. Попередній postfix із 2 failures збережено як історію. |
| GPU | D3D12 hardware metadata, `software=false`, exact executable hash | [x] PASS | A/R/M: D3D12 RTX3050, software=false; executable hash у build-evidence.json. |
| Native appearance | same seed/state/camera/zoom/resolution before/after для кожного V-step | [ ] PARTIAL | M: однакові seed/state/camera у certified fixtures; немає повного before/after окремих V1/V2/V3 binaries. |
| Native interaction | немонтовані V01 action/F1, V02 bridge/path wear, V03 movement/cargo videos | [ ] PARTIAL | P: реальне native керування; обов'язкові немонтовані videos не записані. |
| Save compatibility | current roundtrip + supported frozen old-save copy; оригінал не перезаписаний | [x] PASS | save_compatibility/terrain_v02 + R: frozen/current copies; користувацькі originals не змінено. |
| Network | local host/join однаково бачать wear, cargo, levels і authoritative UI reasons | [ ] PARTIAL | local process/protocol/adverse tests у T; спеціальне native порівняння wear/levels/cargo двох клієнтів не закрите. |
| Package | fresh output directory, manifest/hash, package-only launch і staging validation | [x] PASS | P: fresh rework package, 158 staged assets, точний збіг exe hash, ручний new-world/F1/Escape smoke. |
| Physical scope | два фізичні Windows-ПК/cross-network позначені окремо; local loopback їх не замінює | [ ] NOT RUN | Два фізичні Windows-ПК і cross-network не запускались; loopback не прирівняний до них. |
| Performance | cold generation, frame/compose, texture bytes і memory порівняні з baseline на тій самій сцені | [ ] PARTIAL | Cold generation <5s test; 310.01/411.68 MiB texel payload, бюджет не прийнятий; p95/p99/VRAM baseline відсутній. |

## 8. Фінальне рішення

- **PASS V01:** усі V01 rows, UI matrix та V01 evidence завершені; світові pixels/commands не змінені поза дозволеним HUD.
- **PASS V02:** PASS V01 збережено; усі V02 rows і native river/bridge/path-wear evidence завершені.
- **PASS V03:** PASS V01+V02 збережено; усі V03 rows, Camp+11 coverage, cargo/motion video, package й regressions завершені.
- **PARTIAL:** є код або окремі тести/captures, але хоча б один обов'язковий row лишається `PENDING`.
- **FAIL:** runtime суперечить authority/scope, регресує прийнятий попередній етап або evidence відтворює дефект.
- **NOT IMPLEMENTED:** немає runtime diff та нативного доказу; generated target або документ не змінює цей статус.

Поточний verdict цього документа: **PARTIAL — повна художня переробка ще не прийнята**. Конкретні залишки: artist/style/detail parity, зимові матеріальні межі, longest-text/DPI/hit-rect matrix, native videos, texture lifecycle і performance/VRAM budget. Документ не оголошує публічний реліз і не змінює статус S01/S02/S03.

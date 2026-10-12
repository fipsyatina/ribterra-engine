# Water / Reflection V1 — bounded native checkpoint 08.10.2026

Статус: **BOUNDED GPU CHECKPOINT PASS / ПОВНИЙ ДВИГУН НЕ ПРИЙНЯТО**.
Всі E01–E17 відкриті. Це ізольована технічна сцена того самого renderer,
а не нормальний запуск гри, новий реліз чи готовий концепт-арт.
Контракт: [Water / Reflection V1](WATER_REFLECTION_V1_CONTRACT_UA.md).

Актуальніший bounded follow-up: [raw binary WaterCoverage та odd-size resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md), fresh build02/native05–06. Нижче незмінні за змістом докази попередньої build01/native01–02; їхні SHA/числа не підмінено новими. Intermediate AA, повний двигун і FPS відкриті.

## Що реалізовано й перевірено

- Окремий кумулятивний `WaterReflectionSlice` у спільному SDL_GPU renderer;
  старі графи не переключені. Збережені sun/moon shadow paths,
  Contact V1 ABI, матеріальне та локальне освітлення.
- П'ятий MRT RGBA8 містить воду/клас/берег/дозвіл Reflection.
  `SurfaceWorld.W` лишається точним Contact-тегом 0/1/2.
- Два world-anchored normal потоки, показаний час і вітер, bounded distortion;
  берегова піна освітлюється як diffuse, а не світиться сама вночі.
- Half-resolution HDR Reflection має source lighting, premultiplied alpha
  і одну strength; фінальний receiver gate виключає сушу, настил та невідоме.
- Opt-in `GameView::compose_engine_control` використовує actual ClientSession
  Water, завершені Bridge і reveal. Новий нормальний startup не підключений.
- Technical art kit: нейтральний будинок/мешканець і періодична normal-карта.
  Це мінімальний контрольний матеріал, не копії фінальних концепт-ассетів.

## Свіжа збірка

`build/gameview-water-2026-10-08-attempt01` — фактичний exit 0.
22 translation units, 142 frozen source bindings; 108/108 command exit-sidecars
мають 0. Свіжі 20 shader programs / 40 stages, 44 source dependencies,
81 shader artifacts; 52/52 kit files та 50 manifest assets перевірені.
5 protected startup/launcher anchors не змінені.

Розмір: 476 файлів, 114 888 094 B (109,566 MiB); executable 42 599 424 B.

| Доказ | SHA-256 |
| --- | --- |
| `GameViewWaterProbe.exe` | `A3E59C9491B30E3E06B83281BE0FDB3DCC3A250B6DC653DE76007F96F2CD6B96` |
| `build-report.json` | `2E70EB153C9E388F6E287FBF4F75584C26B9FE29925FF86C92A34BCB5B22939A` |
| Shader `manifest.json` | `F00F4F4C4F542C086761F717BEA8B07B5FA86CC4A987A52188D3BF170D543B78` |
| Native report (обидва) | `F77E657752CD6485F2513625AA9F1A2CFB21A695ECEC1B538817B048A27B1CE5` |
| Root evidence ledger | `BBFA3E9B262FDCE4AD7E49C4499A9E6F422BF106629977C5B38DBF4F43FF8FE0` |

Незалежний аудит підтвердив frozen build cohort. Не запускали загальну
збірку/CTest/install/package, не міняли release version або dist pointer.

## Два реальні native прогони

`build/gameview-water-native-2026-10-08-attempt01` і `...attempt02`:
фактичний exit 0 кожного. Видимі SDL вікна, native Direct3D 12,
NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, не software renderer.
Кожен output: 14 GPU PNG 1920×1080, JSON report і current-schema save.
Розміри відповідно 6 426 405 та 6 426 410 B.

- 51 water cells, 3 завершені вузькі E–W bridge decks, 2 reflected controls.
  `water_draws=49` — prepared GPU batches після merge, не кількість клітин.
- 8 submitted GPU passes: SunShadow → MoonShadow → Reflection → GroundGBuffer
  → GroundLighting → WorldForward → Composite → UI.
  Contact pass не викликано через порожній input у цій сцені; це не повна
  спільна дев'ятипрохідна сцена з усіма ефектами.
- Reflection toggle: 9 885 змінених пікселів; показаний час: 54 318;
  shore toggle: 5 098. Зміни обмежені водою за diagnostic CPU classifier
  з допуском 1 physical pixel на межі. **Це не raw carrier/AA proof.**
- 6 actual GPU camera captures: zoom 0,62 / 1,3 / 2 та два pan для кожного.
  Zoom 3 і native keyboard/mouse input цим не перевірені.
- Same clock і after-reject кадри точно дорівнюють baseline.
  Hidden receiver з Reflection on/off також byte-exact.
  Final PNG alpha перевірена як 255 в усіх пікселях.
- 6 invalid inputs фактично відхилені: non-unit flow, excess distortion,
  fractional ground/caster reveal, unsupported authored AA coverage,
  invalid reflection material index.
- Повторний запуск у вже наявний output фактично повернув exit 1
  `Native output already exists`; failure JSON не створено.

14/14 PNG і оригінальний JSON byte-exact між прогонами.
Save-файли різні через fresh secure random `world_id`; не заявляємо
детермінізм цілого 16-файлового дерева.

## Межа доказів і пам'яті

[`water-native-root-evidence-oct8.json`](../../.omx/plans/water-native-root-evidence-oct8.json)
зв'язує build report, executable, shader/kit manifest, probe source і всі
native outputs шляхом path/bytes/SHA. Незалежно перевірені 37/37 bindings,
без зайвих файлів у двох native roots. Exits записані основним агентом за
реальними результатами запуску; це **не binary-generated exit sidecars,
не підписаний незалежний execution transcript**. Оригінальні звіти не переписані.

На 1080p ground graph: 174 182 400 B; sky maps: 16 777 216 B;
tracked steady nominal texture payload: 190 959 616 B (~182,11 MiB).
Це **не total VRAM**: без swapchain, buffers, art, driver overhead,
одночасного old+candidate при resize. Odd-size allocation/rollback ще не доказані.

Raw baseline та zoom-2 capture візуально оглянуті й зіставлені з літнім
target-0262: сіра технічна сцена явно не відповідає готовій художній сцені.
GPU readback не є desktop scanout, відео або перевіркою ручного вводу.
Native computer automation на цьому інструменті недоступна.

## Що ще відкрите

Raw coverage/класи/AA-край; odd resize і allocation failure rollback;
native pan/zoom/pause/video; весь погодний і атмосферний стек разом;
концепт-арт, normal startup, графічні presets; GPU timing, displayed FPS,
мінімум 120 FPS у 1080p і плавність; актуальна повна suite.
Лише після E01–E17 — подальша гра, генератор і фінальний HUD.

Сховище: жодного видалення цього checkpoint. Нові roots не замінюють
launcher; надалі latest + verified rollback + компактні докази за
[політикою зберігання](../ARTIFACT_RETENTION_UA.md). Перед будь-яким
видаленням старих файлів потрібні конкретні шляхи й причина.

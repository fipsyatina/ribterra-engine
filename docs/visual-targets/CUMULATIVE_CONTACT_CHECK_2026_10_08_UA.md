# Спільна GameView-сцена та authored contacts — 08.10.2026

**Частковий поріг пройдено:** real session terrain, вода, три завершені bridge decks, вісім нейтральних категорій, contact/Sun/Moon shadows, local lights, emission, reflection і atmosphere працюють разом у чинному production renderer. Це bounded opt-in сцена. **Усі повні E01–E17 OPEN; reference fidelity NOT_ACCEPTED.**

## Зміни

Schema 2 отримала необов'язкові `contact_arts` і `contact_shadows`. Десять окремих tight R8 `.rtex` masks мають перевірені hashes/region, прозоре зовнішнє кільце і ненульовий внутрішній footprint. Registry ownership окремий від sprite caster textures. Loader і adapter публікують повний валідований candidate транзакційно; каталог startup-only.

Sprite feet і contact anchors використовують один інтерпольований terrain triangle, його точний normal і tangent basis. Завершений настил має одну висоту `.08 renderZ` для feet, contacts і screen queries. Семантика water terrain відокремлена від увімкнення water FX: ordinary water не стає contact receiver після вимкнення ефекту, а фізичний bridge deck зберігається. Непридатні круті поверхні не отримують contact rows.

All-category lighting вимірюється на authored face patches. Для кожної категорії видаляється тільки її звичайний sprite draw; до окремого вимірювання потрапляють category-removal-qualified pixels. Освітлена земля позаду мешканця не може підмінити цей контроль.

SDL_GPU, WaterAtmosphereSlice, HDR/alpha, contact/shadow registry і GPU ABI повторно використано. Цей крок не додає production translation units, graph або shader programs.

## Поточна source cohort і actual execution

- Репозиторій `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; наявний dirty source не відкидався.
- Kit `build/engine-cumulative-kit-2026-10-08-check01/export-a`: 62 файли / 9 474 614 bytes, вісім категорій / десять contact arts. Actual Python exporter/negative checks exit 0; retained `export-a`/`export-b` byte-exact.
- Fresh build `build/gameview-engine-cumulative-2026-10-08-attempt04`: strict MSVC C++20 `/O2 /MT /W4 /WX`, 21 production TUs + probe, 157 source bindings, 120 фактичних команд із zero exit sidecars. Fresh DXIL: 23 програми / 46 stages / 93 assets із перевіркою interfaces/bindings/hashes. Root: 527 файлів / 123 417 234 bytes.
- Probe SHA256 `4B6F7F39D7D23A36D307F9916EC85374B2D75E0B2DA180EC46FEE2838783A155`.
- Native roots `build/gameview-engine-cumulative-native-2026-10-08-attempt04` та `attempt05`: actual exit 0, hardware Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Startup: 22 C++ loader checks і 11 adapter checks. У кожному run 39 GPU PNG, raw carrier, JSON, retained diagnostic catalogs і saves.
- Parent read-only verifier actual exit 0: `cumulative current evidence checks passed; sources=157, bound_files=1059, zero_commands=120, repeat_outputs=252`.
- Усі 252 derived outputs byte-exact між actual runs. Два save мають випадкові власні World identities, тому виключені лише з cross-run comparison; кожен окремо SHA256-bound до свого execution. Runner ledger/transcript не видаються за deterministic GPU output.

Повторна звірка цієї cohort:

```powershell
C:\Python314\python.exe tests/gameview_cumulative_evidence_tests.py build/gameview-engine-cumulative-2026-10-08-attempt04 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-engine-cumulative-native-2026-10-08-attempt04 build/gameview-engine-cumulative-native-2026-10-08-attempt05
```

Після подальших змін source цей результат стає історичним checkpoint. Verifier навмисно відхиляє source drift; наступна current заява потребує нового binding і actual execution.

## Причинні результати

| Контроль | Фактичний результат |
|---|---:|
| Ambient contact on/off: land | 2 224 changed pixels |
| Ambient contact on/off: bridge deck | 464 changed pixels |
| Contact on/off, ambient=0, direct Sun | byte-exact frame |
| Sun sky-caster on/off: land | 69 522 pixels |
| Moon sky-caster on/off: land | 81 604 pixels |
| Authored local light on/off: land | 33 535 pixels |
| Окремий warm window emission | 894 pixels |
| Reflection on/off | 7 655 pixels |
| Hidden unknown object / fixed shown-time repeat | byte-exact frames |
| Raw water carrier / opaque diagnostic UI | unchanged у відповідних causal controls |

Усі вісім category patches мають позитивні independent ambient/Sun/Moon/local responses. Мінімальні mean RGB deltas: ambient 31; Sun 55.83; Moon 23.67; local 72.375. Перевірені house, stockpile, workshop, tree, rock, resident.empty, resident.log, resident.sack; це representative faces, не кожен texel або весь production каталог.

Три camera cases — zoom `.62`, `1.3`, `2.0` із pan, у кожній десять contacts і вісім checked feet/picks. Zoom `3.0`, continuous motion, DPI/input цим не прийняті. Duplicate/zero contact ID та cargo ID overflow відхиляються зі збереженням published frame/camera/picks. Це не повний rollback private caches.

WaterFX-off CPU control підтверджує три інстанси, три фізичні настили, три контакти й один filtered ordinary-water contact. Steep fixture окремо перевірена на GPU: sprite збережено, contact відсутній. CPU WaterFX-off не називається native WaterAtmosphere кадром.

Nominal attachment ledger **174 182 400 bytes** на два frame sets, включно з coverage 16 588 800 і half-resolution reflections 8 294 400. Це не measured peak VRAM. FPS та GPU execution time не виміряно; поля `null`.

## Відмови й межі

Native01 зупинився до першого кадру: steep fixture змінювала висоти під активними маршрутами; error message також формувалося передчасно. Native02 виявив неочищені water bounds у вимкненій atmosphere. Native03 correctly показав нульове resident local light: тест успадкував cone house spotlight замість окремого Point source. Виправлено fixture/diagnostics, повний native04/05 повторено. Renderer contracts та thresholds не послаблювалися.

Кадри — нейтральні геометричні controls, технічний terrain/river і зелений UI marker. Вони не приймають художню деталізацію трьох зимових/літніх референсів. Marker не доводить RmlUi/український шрифт/interaction. Час фіксований: `paused-repeat` перевіряє однакові explicit inputs, не session pause adapter.

Далі потрібні session shown-time, lifecycle/input/video, typed smoke/fire/weather/wind, wetness/snow/footprints, art kit, effective settings, performance та normal startup. Повна engine goal активна; гра не оголошена завершеною.

Під час цих перевірок файли не видалялися. Очищення obsolete compiled copies після verified replacement має окремий exact manifest і advance notice. Source snapshots, logs, captures, catalogs, saves, поточний runnable та rollback зберігаються.

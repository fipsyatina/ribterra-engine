# Нейтральне albedo з XYZ: CPU checkpoint 09.10.2026

Два фактичні CPU прогони та незалежна перевірка пройшли. Експортер створює непрозоре albedo будинку з 114 авторських XYZ-трикутників і палітри, без копіювання мальованого кольору в цьому явно вибраному режимі. Це перевірений експорт тимчасового draft, а фізична достовірність матеріалів, художня відповідність трьом референсам і весь рушій ще потребують приймання.

| Фактичний прогін | CLI команди | Очікувані відмови | Файли тестового root | Байти тестового root |
|---|---:|---:|---:|---:|
| Parent attempt01, exit 0 | 70 | 66 із exit 2 | 272 | 12 467 958 |
| Independent attempt02, exit 0 | 70 | 66 із exit 2 | 272 | 12 467 955 |

Кожен прогін включає чотири успішні експорти: старий calibration, сумісну мальовану collection та два повтори нейтральної collection. Усі чотири нейтральні результати між двома прогонами побайтно однакові: кожен має 20 файлів / 4 171 748 байтів, 15 manifest rows і 12 raw texture streams / 4 096 304 байти. Root не перевищує 128 MiB; мінімум вільного місця становить 384 MiB. Збережений тестовий junction не обходиться при підрахунку і не подвоює файли.

Режим вибирає `albedo_source.kind = authored-xyz-triangle-materials-v1` у новому [house-neutral-collection.json](../../art-source/production-materials-v1/house-neutral-collection.json). Палітра, notice, provenance і master зв'язуються фактичними SHA. Це перевіряє цілісність вхідних даних; самі SHA не встановлюють авторства, ліцензії або фізичної якості draft. Інший елемент collection — старий двоплощинний `resident.man.walk.2.1` — залишається мальованим діагностичним контролем.

Для будинку отримано 127 605 видимих пікселів: 127 563 перекривають стару alpha, 42 належать тільки новій геометрії. Старий малюнок має 147 212 видимих пікселів, із них 19 649 залишаються поза draft геометрією. Експортер не заповнює цю прогалину мальованим albedo. Геометрія й художня форма залишаються окремою незавершеною роботою. RGB задають 14 тимчасових пласких кольорів; emission дорівнює нулю.

Нормалі та R16 camera depth походять із XYZ переможного трикутника. Перевірено двійкову alpha, нульові RGBA поза геометрією, surface/caster/emission masks і 32 числові probes двох елементів. Зміна тільки RGB зберігає решту каналів; зміна XYZ змінює normal/depth. Для wetness 0.30 отримано byte 76 відповідно до round-to-even; перевірено 713 кваліфікованих пікселів.

У кожному прогоні відхилено 66 хибних CLI входів без публікації результатів. Окремі прямі перевірки охоплюють 11 відхилених merge calls у дев'яти collision groups і шість пошкоджених raw oracle inputs. Перевіряються зіткнення імен файлів/директорій, зарезервований report path, палітра, геометрія, provenance, канали та межі читання. Вхідні файли і збережені попередні результати незмінні.

Сумісність старого режиму підтверджена фактично: calibration зберігає вісім non-report файлів, мальована collection — 17 non-report файлів побайтно. Відмінності report обмежені явно задекларованими bindings нового exporter. Старий source record і формат результатів без нового режиму збережено.

Windows post-exit process counters доступні для всіх 70 subprocesses кожного прогону. У parent нейтральний exporter має peak working set 73 363 456 / 73 285 632 байти, приблизно 70 MiB. Найбільший child peak з усіх режимів — 92 659 712 / 91 525 120 байтів; сам тестовий Python — 179 134 464 / 181 596 160. Це фактичні CPU-вимірювання цих fixtures; вони не визначають GPU memory, FPS або пам'ять усіх восьми production masters.

Незалежна перевірка повторно зв'язала 676 унікальних файлів і 728 спостережень без mismatch. Її [receipt](../../.unlazy/engine-completion-2026-10-08/neutral-export-independent-2026-10-09.json), [parent receipt](../../.unlazy/engine-completion-2026-10-08/neutral-export-parent-2026-10-09.json) і незмінні [raw attempt01](../../build/neutral-material-export-tests-2026-10-09-attempt01/neutral-material-export-test-report.json) / [raw attempt02](../../build/neutral-material-export-tests-2026-10-09-attempt02/neutral-material-export-test-report.json) є прямими unsigned доказами. Вони не замінюють формальну автоматичну сертифікацію skill gates.

Перевірені C++ зміни GameArt/View/AssetCatalog/main застосовано після збереження точних оригіналів. Для MaterialCollection профіль обмежує палітру 512 KiB до allocation, а authored View вимагає цей профіль. Це SOURCE review; фактичне native приймання ліміту, reparse відмов, typed outline, picking і звичайного main ще попереду. Попередні GPU/main результати залишаються прийнятими для їхніх збережених snapshots.

Наступний обов'язковий checkpoint — свіжа strict C++ збірка, два позитивні й два cold native прогони та незалежний verifier. Повна ціль активна: production eight masters, FullFX, три референси, звичайний startup/package та ≥120 FPS залишаються OPEN.

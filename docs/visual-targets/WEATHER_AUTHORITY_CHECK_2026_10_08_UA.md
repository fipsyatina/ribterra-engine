# Погода у World, збереженнях і snapshots — 08.10.2026

Проміжна CPU-інтеграція пройшла strict build і два фактичні запуски того самого executable. Це підтверджує базову authority, persistence та transport; повні E01–E17/P01 і приймання E06 залишаються відкритими. Launcher та установлене прев’ю не оновлювалися.

Авторитетне дерево: `C:\Users\fipst\Desktop\ribterra`, `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`. Поточна несумісна allocation: save 9, protocol 23, rules 19, generator 8; engine залишається unreleased 0.26.3. Попередні GPU/weather cohorts після зміни bound headers є історичними.

## Actual build та виконання

- Build: `build/weather-authority-2026-10-08-attempt03`; фактичний exit 0, 15 translation units, 20 команд із нульовими exit sidecars. C++20 `/O2 /MT /W4 /WX`; свіжа генерація skill balance і трьох FlatBuffers schemas. 419 source/SDK snapshot rows; без shader/GPU build, installer або запуску звичайної гри.
- `WeatherAuthorityProbe.exe`: SHA256 `B070FDEF07DA5F62DE5C63F259ABFA5E892163D121AC04E7CF32AD0E693C8465`, 8 488 960 bytes.
- Native: `build/weather-authority-native-2026-10-08-attempt01` та `...attempt02`; обидва фактичні exit 0, JSON результати byte-exact. Кожен save/staging artifact окремо прив’язаний до свого run; рівно 15 названих payload exceptions через random World UUID. Два collision-файли по 3 bytes повторюються точно.
- Parent verifier фактично exit 0: `sources=419, bound_files=1046, zero_commands=20`. Він перевіряє current/snapshot sources, SDK, tools, libraries, generated headers, objects, executable, argv/logs/outputs, точний inventory і negative hash control.

Відтворення read-only verifier:

```powershell
C:\Python314\python.exe tests/weather_authority_evidence_tests.py build/weather-authority-2026-10-08-attempt03 build/weather-authority-native-2026-10-08-attempt01 build/weather-authority-native-2026-10-08-attempt02
```

Build01 зупинився на generate-network: default 0 відсутній у ClimateProfileId; явно встановлено TemperateValley. Build02 зупинився на compile-world_save: helper load_cell був локальним в іншій функції; замінено прямою конверсією вже перевіреного target. Обидва failed roots, логи й snapshot sources збережені. Нові видалення не виконувалися.

## Що саме перевірено

World має один authoritative climate runtime/cache; calendar offset додається до simulation tick лише через CalendarTick. Standard tick 0 — day 1/06:00/100 Food. FirstWinterStorm tick 0 — day 31/06:00/snow 650 і незмінний simulation tick 0. Для 10 початкових мешканців фізичні food stacks містять 1200. Перевірено population/capacity межі: 0→100/10000; 83→9960/10080; 1500→180000/180120.

1201 фактичний World tick збігається з незалежно просунутим domain runtime на кожному кроці. Save на 1201, другий save на 1202 з byte-exact попереднім primary у backup, restore і 200 tick продовження до 1402 з рівними checksum/WeatherView. Окремо actual world досяг бурі на tick 12650: snow remainder `26109`, wet remainder `-21669`; full ClimateRuntimeState/checksum збереглися після load, ще 200 фактичних tick до 12850 не розійшлися. Цей контроль має ненульові залишки обох знаків, без синтетичного replay.

Вісім пошкоджених climate imports відхилено зі збереженням live checksum і WeatherView: tick0 cover/history, evaluated tick, offset, version, current temperature, snow bounds, wet remainder bounds і work row на tick0. Останній контроль відхиляється initial-history guard; він не доводить exact live Activity binding. Зміна валідної wetness змінює checksum. Ненульові work remainders тут ще не перевірено.

Save 9 має required climate/current/present work vector; старі version/text не проходять header preflight до full read/ZSTD. Actual old primary, old backup, пошкоджений backup із поточним header та обидві staging collisions збереглися byte-exact. Реальний Windows handle без delete sharing заблокував primary promotion: save повернув помилку, обидва оригінали й обидва stages залишилися. Source review також перевірив primary-first rotation: у разі пізнішої backup failure залишаються новий primary, старий backup і prior-primary stage; окремого runtime контролю цієї другої failure branch поки немає.

WeatherView обов’язковий у full/delta; decode обмежує чотири cards та 1–8 intervals перед копіюванням. Actual full/delta roundtrips замінюють усю weather metadata. Diagnostic 40-chunk multipart має 6 parts і максимальний encoded packet 19 160 bytes. Легально валідна зміна snow лише одного part відхиляється за full header equality; pending не змінює показаний стан. Після 11 s спостережено assembly expiry та незмінний weather; наступний accept/rejection після timeout тут ще не виконано. Waiting lobby також має authoritative weather.

Actual GNS loopback host/client узгодили WeatherView. ClientSession перед full snapshot і після disconnect повертає unknown та відсутній calendar; local session читає свій authoritative snapshot. Це один комп’ютер, а не two-PC/WAN тест.

## Джерела й межі приймання

Незалежні source reviews CLEAR охопили World/key/reset/candidate/checksum, persistence/retention, builder include closure та transport/client/main/HUD. Legacy integer calendar overloads прибрано; youth/recruit/path/logistics timers залишилися simulation ticks. Main використовує нову `climate-v1` папку save slots/pointers, залишаючи стару namespace. HUD бере temperature/day/season/clock із клієнтського authoritative calendar та має явний waiting state. Ці HUD/main/View зміни ще потребують fresh production renderer/UI build і native captures; CPU executable їх не компілює.

Незалежний огляд actual cohort CLEAR: reviewer особисто rehashed усі 1046 unique bound files без розбіжностей і окремо фактично запустив read-only verifier з exit 0. JSON обох native runs byte-exact, SHA256 `579524BC6267FA7CBD6A2246CF857A9939E4AFC4CD96EF5CF24FEF30F0AC645F`; також перевірено exact retained locked primary/backup/stages. Це manual direct evidence; автоматичної OMX/gate-check certification не оголошено.

Ще OPEN: actual gather/build work gates та XP/waits/underground/reassignment/failed Replan; ненульові keyed work save/restore; malformed weather wire controls; localhost latejoin/reconnect/pause/speed/calendar boundaries; backup rotation failure execution; актуальний широкий suite; renderer/HUD/FX integration; calibrated/certified winter seed; повна simulation overhead, GPU/FPS і референси.

Seed 70421 не прийнятий для shipping FirstWinterStorm: буря починається при +4.5 °C дощем. Snow після початку бурі не доводить snow-only старт. Domain forecast/cover тести не замінюють цю сертифікацію або visual acceptance.

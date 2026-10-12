# AnalyticAtmosphere V1 — обмежена нативна перевірка

08.10.2026. **BOUNDED NATIVE PASS; E10 і E01–E17 OPEN.**
Це доказ нового атмосферного шару у спільному production Renderer2D через
справжні GameView/ClientSession, не приймання гри, концепт-арту або 120 FPS.
[Контракт і заміна алгоритму](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md).

## Виконаний прогін

- Свіжа strict `/W4 /WX` збірка:
  `build/gameview-atmosphere-2026-10-08-attempt03`, фактичний exit 0.
  22 translation units, 151 source bindings, 120 команд із zero exit,
  23 shader programs / 46 stages / 51 source closure / 93 artifacts.
  521 файлів, 116 710 960 логічних байтів (111.30 MiB).
- Два запуски **того самого** EXE: native `attempt02` та `attempt03`;
  runner і actual subprocess exit 0 в кожному, D3D12,
  NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Кожний native root має 34 файли: 26 GPU PNG, два packed WaterCoverage
  downloads, три поточні test saves, JSON результату, execution log і ledger.
  Root sizes 34 577 439 / 34 577 436 bytes. Повний runtime не дублювався.
- **29/29 derived outputs byte-exact** між прогонами: 26 PNG + два raw
  downloads + JSON. Secure world IDs у saves та часові записи execution
  ledger закономірно відрізняються; їх не називати byte-exact.
- Незалежний read-only аудит збірки перевірив source snapshot/current hashes,
  command exits, shader closures і реальну DXIL reflection. Старі 40 DXIL та
  40 reflection JSON byte-identical до Water build02.

Назва `GameViewWaterProbe.exe` і build helper залишилася історичною:
`-ProbeSource tools/gameview_atmosphere_probe.cpp` вибирає атмосферну перевірку.
Build report окремо каже execution=false; виконання доводять native ledgers,
а не сам факт створення EXE. Ledgers unsigned, не незалежний GPU timer.

## Причинні перевірки

| Перевірка | Фактичний результат |
|---|---|
| Old Water graph → atmosphere disabled | PNG і raw carrier byte-exact |
| Ранкова атмосфера проти disabled | 369 257 changed pixels |
| Пауза / той самий shown clock | PNG byte-exact |
| Shown clock 10→11, water phase зафіксована | 35 412 changed pixels, ізольований атмосферний drift |
| Діагностична година 12 / 22 | 364 200 / 364 350 changed pixels проти morning; morning01=0 |
| Діагностичний haze 0→1 | 361 656 changed pixels |
| Однакові terrain panels near / mid / far | RGB change means 0 / 73.2 / 78 |
| Однакова глибина, low / high | RGB change means 78 / 8 |
| Однакова висота/глибина, coast inside / near / outside | RGB change means 78 / 64.48 / 0 |
| Reflection off | 9 841 changed pixels тільки у water classifier + 1px margin |
| Оpaque UI і final alpha | UI exact; PNG alpha=255 |
| Hidden surface reveal=0, emission on/off | Scene exact, без self-emission витоку |
| Actual GameView unknown source | Ціле джерело не submitted; scene та water AABB exact |
| Pan і zoom .62 / 1.30 / 3.00 | Три GPU captures, origin/clock і actual-water provenance перевірені |
| 32 фактичні окремі water regions | Прийняті, GPU capture |
| 33 active regions | Candidate відхилено до frame/camera/pick publication; наступний кадр exact |
| Disabled у 33-region scene | Count=0, 33 water cells рендеряться; active capacity не нав'язується |
| Сім invalid renderer inputs | Відхилені до GPU; наступний valid PNG/raw exact |

Raw RGBA8 downloads: 1920×1080, 8 294 400 packed bytes, GPU row pitch 7680.
У кожному: clear 1 698 582, land 302 496, water 70 657, bridge deck 1 865,
shore-water 42 832, invalid tuples **0**. Це exact **binary** carrier,
не AA coverage 1…254. Height/depth/coast panels — окремий typed shader fixture;
його ручний AABB не видається за воду ClientSession. Походження областей з
revealed Water окремо перевірено у GameView і 32-region fixture.

33-region rejection зберігає саме published frame/camera/picks і GPU result.
Terrain cache синхронізується раніше; повну внутрішню nonmutation не перевірено.

Hidden-surface test не видаляє вже складений local light/caster. Окремий
actual unknown-source test перевіряє non-submission цілого джерела. Це різні
твердження; вони не замінюють повний FoW/composite acceptance.

## ABI, ресурси і незмінність

AtmosphereGpu 640 bytes; новий b0 wrapper 1024 bytes із незмінним старим
384-byte prefix. Тільки три нові fragment variants мають b0=1024;
старі мають b0=384. Water AABBs offset512, array stride16. b1–b3 і slots
не розширені. Нових render targets немає: 174 182 400 nominal world attachment
bytes у 1080p + наявні окремі sky targets. Це не total/peak VRAM.

Gameplay, terrain authority, save/network schema, launchers і current-build
pointer не змінено. Вікно SDL/D3D12 запускалося видимо; переглянуті final GPU
PNG morning/noon/night/capacity. Це **не** desktop scanout, керування мишею,
DPI/input proof або немонтоване відео. Технічний контрольний будинок і земля
візуально не є кінцевим концепт-артом.

## Ідентичність доказів

- EXE SHA256 `8C28D8CDBA8F9849E1CDCD9DC3D380D4927CB68D867E5465637BF3FC2F32BFC3`.
- Build report `4D4A7E7BE82464403FBE80AD7A8798D7652601F1AD31CECDEA4E455F916C024C`.
- Shader manifest `213DA2A019D152134D5BD75088C3FED794BD879CE34851B825FCC16B14B0CBA8`.
- Native result `091A0286FDFB46F40B86268B57CBEC3D922872D42702F9E7E6E06B53CEE17A11`.
- Old і disabled raw `56BD855A4941255641A7F503FAE75C4826F0C6FEF479422548A3A209DF632310`.
- Runner `E2BC7B4D7ED25EABBFC95D20B995708250516C2EE4686F132FFB473B60817A9C`.
- Native02 execution `DAE71474E0910B7533160D5663AE30A7CAC63E9106BE7098380B884E3FEC305B`.
- Native03 execution `B3CF42C7A083B252FAECE97036ED3896B5C2112B5F7010815B1374D3899679D9`.

Execution ledgers зберігають actual argv, UTC, exit, input before/after hashes,
точний output inventory. Джерела frozen під час build03/native02–03.

## Історичні відмови й відкриті межі

Build01 exit1: guarded atmosphere include був відсутній у старих declaration
closures. Dependencies і контракт виправлені; старі binaries не змінилися.
Build02 exit0, але native01 exit1 після 22 captures: procedural terrain за
межами 32-region fixture порушував вимогу upward supported normal. Виправлено
тільки flat diagnostic normal halo; renderer validation не послаблено.
Ці спроби — історичні невдачі, не нинішній PASS.

Після підтвердження заміни та повідомлення exact plan виконано окреме
compiled-only очищення 27 файлів build02 (112 939 136 logical bytes).
Її старий EXE більше не збережений; report лишається історичним і не має
всіх своїх binary inputs на диску. Source/shaders/assets/logs/native evidence,
latest build03 і verified Water rollback02 захищені; це не повторний прогін
старої збірки та не GitHub archive.

**OPEN:** host-authoritative час/погода, повний world без provisional 32-region
cap, спільні night/weather/shadows/precipitation/wetness/snow/trails, нормальний
startup, native video і temporal smoothness/reprojection tolerance, DPI/input,
AA/repeated-failure recovery, presets, повна сцена ≥120 FPS і вся CTest suite.
Поточні `fps` / `gpu_execution_ms` — null, не нуль і не результат benchmark.
Не переходити до генератора, механік чи масового art/HUD до E01–E17 acceptance.

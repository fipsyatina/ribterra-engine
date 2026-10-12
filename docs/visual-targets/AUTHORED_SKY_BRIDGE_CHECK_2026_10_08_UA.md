# Authored Sky Bridge — перевірений importer/adapter, не приймання двигуна

## Статус

Прийнято вузький importer/adapter компонент: опис авторських трикутників у schema-2 ассеті проходить через справжні `AssetCatalog` і `GameArt` у camera-relative `Frame2D::sky_shadow_triangles`. Verifier attempt05 завершився з фактичним exit0. На той момент GPU-кадри були `NOT_RUN`; native01–04 згодом стали діагностованими невдалими спробами. **Актуальний наступний bounded native checkpoint:** [native05/06](AUTHORED_SKY_NATIVE_CHECK_2026_10_08_UA.md), кожний exit0,13checks/32PNG,32/32repeatPNG byte-exact та незалежне схвалення. Це не повний двигун або звичайний GameView; окремий контрольний шлях GameView перевіряється наступним кроком.

Усі E01–E17, повний Dynamic/GameView, відповідність концептам, налаштування графіки, ≥120 FPS у 1920×1080 і повне приймання двигуна залишаються OPEN. Гру, генератор, HUD, каталог художніх ассетів і launcher у цьому кроці не змінювали. Загальна ціль ACTIVE.

## Реалізований контракт

- Editable master має незалежний `sky_shadow_triangles`, не виведений із raster `caster_mask`. Діагностичний набір містить шість асиметричних facade/gable/roof трикутників із Sun/Moon/shared масками 1/2/3; це не фінальний art kit.
- Exporter канонізує координати float32, перевіряє точну форму записів, невиродженість і ліміти та додає масив до обох lit-sprite описів. Відсутній master field експортується як `[]`; старий schema-2 descriptor без field імпортується як порожній список.
- Importer повторно перевіряє дані незалежно від exporter/manifest. Опис має рівно `id`, `vertices`, `source_mask`; ID непорожній, унікальний, до 256 UTF-8 байтів. Це непрозорий ідентифікатор, не display text. Маски — лише integer 1/2/3, не bool/float. Координати скінченні, точно представлені float32, у ±8192; double bound перевіряється **до** narrowing.
- Raw ліміт 4096 і ліміт 1024 трикутники на джерело збережені. Некоректний кандидат відхиляється до sampled upload і не публікує новий registry.
- `append_lit_sky_casters` застосовує `world_anchor + local_vertex * geometry_scale`, потім явне віднімання camera world через `camera_relative_render_xyz`. Маска зберігається; reveal=0 нічого не додає, позитивний reveal лишається opaque/filter-only, не alpha coverage.
- Усі існуючі та staged rows перевіряються до вставки: скінченність, ±32 camera-relative cube, невиродженість, reveal/masks/capacities. Помилка залишає frame byte-exact незмінним. `clear_commands` очищає sky rows.

## Свіжий source-frozen proof

Основний root: `build/sky-authored-2026-10-08-attempt05`.
Окремий exporter root: `build/sky-authored-2026-10-08-export-checks01`.

- Fresh MSVC C++20 /O2 /MT /W4 /WX /permissive- /utf-8: вісім translation units, два нові EXE, фактичний helper exit 0. Старі production .obj/.lib не використовували. `skill_balance_generated.h` згенеровано зі snapshotted generator/data.
- Shader cohort — **перевірене повторне використання**, не нова компіляція/negative run: 68 artifacts + manifest із прийнятого SkyShadow attempt09, unchanged source closure і hash equality origin/copy.
- 103 authoritative/snapshot source entries збіглися до та після перевірок. 12 захищених попередніх EXE/manifest/report/log файлів не змінено.
- Незалежний reviewer повторно перевірив реальні звіти, exit sidecars, 103 source pairs, EXE та 12 prior paths і схвалив **лише цей bounded loader/adapter компонент**. Жоден ширший engine/art/FPS статус не підвищено.
- Exporter: 10 deterministic files, 12 probes, **41 негативний випадок** із фактичним exit 2 та потрібною діагностикою; invalid inputs не створили output, existing-output sentinel збережено.
- Fresh MaterialBridge software contract: exit 0. Native D3D12 material regression: exit 0, **22 PNG**, hardware sRGB/linear channels і candidate rollback controls. Main переглянув незмінені `game-art-schema2-lit-sprite.png` та `material-world-ui-order.png`; це сірі/кольорові контрольні fixtures, не concept-ready art і не desktop scanout/video.
- Authored bridge: **два фактичні запуски exit 0**, кожен має рівно **13 named groups**, **19 schema invalids** і **13 transactional adapter failures**. Пройшли exact transform, camera delta, reveal, ID boundary/control identity, raw/per-source caps, malformed existing rows, nonfinite/out-of-cube/float-collapse і clearing.
- Parser/adapter semantics та повні compact-fixture/derived-kit file ledgers збіглися між повторними запусками. Authored bridge не submit-ив жодного GPU frame й не породжував PNG.
- Backend: native `direct3d12`, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, software=false, однаковий у material/обох bridge runs.
- Після verifier: основний root 60,024,495 logical bytes; exporter root 70,892,693 bytes, обидва ≤128 MiB. Ledger вимірює байти до власного запису, тому його поля чесно названі `*_before_ledger`; final guard виконується після protected-final.

## Що доводить sentinel і що не доводить

Для негативних parser fixtures використовуються явно позначені валідні 1×1 six-channel catalogs із тією самою authored геометрією. Фінальна adapter перевірка завантажує **оригінальний повнорозмірний fresh export**. Production parser не послаблювали.

Baseline load споживає рівно шість sampled creation attempts. Fault на N+1=7 доводить, що кожен invalid descriptor спожив нуль sampled uploads, а старий callable registry не був замінений. Це не доказ partial-upload RAII cleanup або live-resource counters — вони не заявлені. Окрема MaterialBridge регресія має власні candidate-failure checks.

## Основні hash anchors

- MaterialBridgeProbe.exe: `0641A8451F56AE25D20F3439A454EC7F7D25A861B60D9D11244CFE2A1D55A645`
- AuthoredSkyBridgeProbe.exe: `D4AF8BFA11A432247030298F1CDF40432B3F69A287AEBB868A1B4877970CE85F`
- Shader manifest: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`
- Editable master: `6b362340248c1b8eef3a6ebe850fb01d1962485d910d9605a6d25b087b923359`
- Exporter: `0616a51fc385ee0ed3d9aa7028f7ae6d6c23b03438c8786d91fc55c9a8cac8f4`
- Canonical authored descriptor: `ca6eaf077d02207939147ebe00cc6e0c15d2f3cdced2c8a2d05b69a5d2af773f`
- Verifier: `4BAA115C0D299B7C8591C607DB3AA690DACCC9520F48EAF5AEF6EED6A3209F47`

Descriptor hash перевірено повторним обчисленням із loaded `world.json`: ordered-object compact UTF-8 JSON + один LF. Окремий no-newline nlohmann hash у звіті явно названий і не підмінює exporter hash. Це перевірена рівність поточного набору, не загальна гарантія канонічності різних JSON serializers.

## Збережені невдалі спроби

1. Attempt01: helper exit 1 до компіляції, PowerShell 5.1 не має `Path.GetRelativePath`; тепер PS7 guard до створення root.
2. Attempt02: helper exit 1 після шести production TUs, MSVC C7772/C1903 через escaping build-label argument; виправлено аргумент helper.
3. Attempt03: build і focused bridge exit 0, але logical root 275,574,337 bytes через full-size fixture hardlinks; **не прийнятий** через бюджет. Fixtures зменшено, докази збережено.
4. Attempt04: compact build exit 0; verifier exit 1 у preflight `Compare-Object` binding, до тестів. Виправлено named operands, зібрано новий frozen attempt05.

На момент bridge attempt05 файлів не видаляли. Backups і ці authored outputs наразі збережені. Пізніше, за прямим проханням користувача, [очищено окремі історичні runtime-копії](../ARTIFACT_CLEANUP_2026_10_08_UA.md); цей proof до очищення не входив. Компілятор використовував лише оголошені нові `compiler-temp` каталоги для власних службових файлів. Надалі діє [правило обмеженого зберігання](../ARTIFACT_RETENTION_UA.md), не нескінченне накопичення невдалих збірок.

## Наступний незакритий поріг

Окремий native causal probe має виконати дев'ять груп authored-path плану: nonrectangular source masks, рух world anchor, ізоляція sprite rectangle, camera lockstep/translation, caster без sprite, raster-mask незалежність, vertex mutation, actual frame slots/UI/target rollback. Поки цей matrix не пройде повністю (latest run03 FAILED), повний authored prerequisite не завершено. Full suite/CTest, усі художні категорії, camera-pan/cascades, alpha/self filtering, time/weather, вода/туман/частинки, presets, 1080p performance та порівняння з концептами залишаються OPEN.

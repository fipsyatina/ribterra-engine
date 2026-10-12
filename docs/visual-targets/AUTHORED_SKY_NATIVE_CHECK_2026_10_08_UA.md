# Authored sky — перевірка справжнього GPU-шляху

08.10.2026. **Обмежений компонент працює; повний двигун НЕ прийнято.**

Це не нова версія гри, не фінальний арт і не доказ відповідності концептам. Перевірка передає геометрію через справжні `AssetCatalog` → schema-2 `GameArt` → `Renderer2D/SkyShadowSlice`, але використовує нерухому технічну сцену 640×480. Усі E01–E17 залишаються OPEN; GameView інтегрується окремо.

## Фактичний прогін

- Свіжа `build/sky-authored-2026-10-08-attempt10`: дев'ять C++ translation units, `/W4 /WX`, три executable, 14 успішних command sidecars. 104 source/snapshot entries і 12 захищених попередніх доказів були незмінні до початку наступної окремої інтеграції GameView.
- 69 shader files повторно використано після перевірки поточного source closure і hashes прийнятої shadow збірки. **Це не свіжа компіляція шейдерів або нові shader-negative tests.**
- `AuthoredSkyNativeProbe.exe` фактично запущено двічі: `build/sky-authored-native-2026-10-08-attempt05` і `attempt06`. Обидва exit0, по 13 named checks, усі дев'ять контрольних зобов'язань виконані.
- По 32 PNG: 32/32 hashes збігаються між прогонами та зі звітами. 54/54 файли шести derived catalogs також byte-exact. JSON звіти містять різні абсолютні output paths, тому їхня повна byte equality не заявляється.
- Native Direct3D12, `NVIDIA GeForce RTX 3050 Laptop GPU`, driver `32.0.15.9282`, software=false.
- Окремий focused verifier цієї збірки: exit0; 41 exporter negatives, 22 native MaterialBridge PNG, bridge 13 focused groups ×2, 19 schema invalids, 13 transactional adapter failures. Цей verifier сам не запускає authored native probe; два наведені native прогони виконані окремо.

## Що доведено кадрами та контрольними змінами

1. Маски 1/2/3 адресують окремі сонячне й місячне джерела; незалежні ground і typed-material receivers мають причинне затемнення. CPU samples беруться з actual descriptor, з запасом ≥5 shadow texels від краю.
2. Зміна world anchor переносить тінь, але не екранний прямокутник спрайта. Стара ділянка точно повертається до open control, нова переходить у shadow control.
3. Зміна лише екранного прямокутника спрайта змінює видимі pixels спрайта, але зберігає ground shadow ROI byte-exact. Rectangles повністю в кадрі; невакуумність перевіряється в обох місцях.
4. Однаковий зсув camera+anchor лишає adapted caster rows і кадр byte-exact.
5. Зсув лише камери змінює caster vertices рівно на мінус camera delta та причинно переносить тінь; receiver rows незмінні.
6. Authored caster працює без самого sprite draw; його ground pixels збігаються зі sprite case.
7. Raster caster mask 0/255 не змінює незалежну world-triangle sky геометрію або кадр.
8. Зміна однієї authored vertex змінює CPU-predicted membership і GPU shadow pixels. Мінімальний margin 5.960032 texels; видимий спрайт лишається невакуумним.
9. Протилежні напрямки Sun/Moon, два frame slots, точний UI, вимкнення directional lights без втрати local/emission та target-candidate rollback перевірені окремими controls.

Тіні: незалежні D32 maps 1024², два набори frame resources; 16,777,216 attachment bytes. GPU execution time у цих captures — null. Захоплення/readback не є performance sampling.

## Межі візуального доказу

Переглянуті незмінені native PNG `anchor-base-shadow.png` і `geometry-one-vertex-moved.png`: технічні сірі receivers, контрольний спрайт і червоний UI marker. Це підтверджує доступність реальних capture pixels, **не** художню подібність. PNG не доводить desktop scanout, temporal video або керування ноутбуком. Native computer-control API на цьому пристрої недоступний.

Не перевірено цим компонентом: normal GameView, всі категорії фінального арту, повний pan/zoom, low-horizon shadows, alpha foliage, camera-following shadow windows, вода/відбиття/туман/погодні переходи, графічні presets, плавність, 120 FPS у 1920×1080, full CTest або release/package. Launcher не змінено; Dynamic у звичайній грі не оголошено готовим.

## Прив'язка доказів

- Source native probe: `BC757BE347FF83924BCB97C84B15FD468454F3E1824FF0F0779A221513BD30DE`.
- Native executable: `5107A54A762F7643159C75CF23FB0DBF52356DC4C677E9C0AE31D13C2F4EDA4F`.
- Build report: `313A42CB21A63C443321E00254BD5129D304078E7ACB12498CDCA2FB554EEBE9`.
- Shader manifest: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`.
- Focused verification ledger: `849701069434107D3896A599C1856E4EDB7428562E0071DB0FC3992910CBC3E3`.
- Native05 report: `88ADEA99C86802C824AF307E17B2B43E48B189B5BBB1608B15E32672C048147E`.
- Native06 report: `14131511AB1FA1F611125EDFFCC8A53EE113485E555DE9F4DD8D46B0E254D1BC`.

Логічні обсяги після прогонів: build10 — 75,275,248 bytes; exporter-checks03 — 70,892,693; native05/native06 — по 60,374,364. Кожний root нижче 128 MiB. Неприйняті попередні копії підлягають окремому точному очищенню після повідомлення користувача, не безмежному зберіганню; див. `docs/ARTIFACT_RETENTION_UA.md`.

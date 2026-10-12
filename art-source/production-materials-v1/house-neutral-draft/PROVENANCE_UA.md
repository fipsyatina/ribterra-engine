# Neutral house palette — авторська чернетка

DRAFT / SOURCE ONLY / NOT_ACCEPTED. Ці чотири файли не зареєстровані в collection чи live assets. Export, native render, production materials, reference fidelity та FPS не перевірені й не прийняті.

`master.json` відділяє нові flat material colours від збереженої XYZ-геометрії будинку. `palette.json` містить 14 матеріалів і рівно 114 однозначних triangle assignments. RGB — авторські provisional sRGB byte values, не виміряний reflectance і не sampled paint. Однаковий матеріал має однакові RGB та response на всіх своїх поверхнях: окремий sun-facing/night/contact grade у кольори не записано. Наприклад, усі chimney facets, rim та внутрішня floor мають один матеріал; темрява порожнини не імітується окремим baked RGB shadow.

Для кожного triangle `master.albedo` дорівнює assigned `palette.albedo_srgb` плюс alpha255; roughness/emission_mask/wet_mask/caster_mask копіюються з того самого palette material. Це чернеткові response values, не виміряні властивості slate/plaster/wood/stone/glass/metal. AO з оригінального master збережено окремо без зміни. Glass — opaque coloured proxy; transmission/refraction OPEN. Усі emission masks і emission_radiance нульові; window/lantern schedules, radiance та local-light anchors потребують окремого source/causal review.

## Збережена геометрія

Джерело: `art-source/production-materials-v1/house/master.json`, SHA256 `0e602fa94a406ee3aefd9c62550679434f2a75a87e37030312b2170d77299d9b`.

Збережено exact XYZ numbers, 114 triangle IDs/order/winding/topology, кожне triangle AO, 10 coarse sky-shadow triangles з vertices/source_mask, порожні local_occluders, 20 numeric probe IDs/triangle IDs/barycentrics, coordinate system та export settings: eppp4, padding0. Нові purpose/IDs, RGBA й material scalars не змінюють геометрію чи її нормалі. Нормалі й depth утворюються authored XYZ/winding; RGB не використовується для їхнього відновлення.

Це та сама faceted proxy geometry, вручну номінована за screen landmarks, з виправленими coplanar fence posts і bucket ring/facets. Вона не стала фізично точною моделлю через заміну palette. Старі припущення skewed facades/roof planes/тонких joints і відсутні приховані частини збережені.

Original registration target зберігає crop529×428, rect[106,37,529,428], normalized pivot[.5,.98], pixel pivot[264.5,419.44000244140625], reference_width529 та eppp4. `palette.source_registration_target` копіює стару original-catalog declaration лише як джерело/майбутню ціль; active_registration=false. Її старий painted-alpha caster policy не є прийнятою policy нового neutral mode. Unregistered default master bounds не можна мовчки оголосити новою registered collection.

## Alpha та silhouette

Стара read-only карта original paint: nonzero alpha147212, supported127563, unsupported19649. Джерело — bound `house/landmarks.json`; це успадковане вимірювання старого source paint, не новий export результат.

Ця чернетка не додає геометрію для baked glow/halo чи missing edges/props. Palette alpha255 означає opaque authored triangles, а не примусове перетворення original fractional paint alpha на255. Новий raster alpha/silhouette, pixels поза old paint support і actual picks не виміряні. Future geometry-derived neutral output може втратити старі baked halos; це не reference acceptance. Original PNG, landmarks/RLE-map, old master та collection залишаються незмінними.

## Походження

Original visual art: Ribterra project / OpenAI image generation, sourceproject, LicenseRef-Proprietary, notice`licenses/Ribterra-Assets.txt`. Source page SHA256: `c232e87e5891e37b3ebfd90f214c70bea6628b2ce6c2c6521e086d947a0af7f9`; exact original catalog entry SHA256: `45d5827d29a0eef1ed8c49824e20126f28cfe408cf34d53b0df363e2dd5ce0d5`.

Source geometry: Ribterra project / Codex authored registered proxy draft. New palette: Codex-authored provisional flat material palette for Ribterra. Це окремі attribution roles; original visual art не оголошено нейтральним albedo, а proxy не приписана автору reference 3D model.

`palette.json` зберігає original license/source/author/notice metadata та hashes, original prompt і target0261/0262 hashes з visual-only purpose. Окремі full-file source_bindings фіксують 11 входів: old master, landmarks, assumptions, collection, original PNG/catalog, asset_sources, original notice, prompt і два references. Додатково bound новий `NOTICE.txt`. References не дають права на redistribution. Новий notice не змінює original license.

| Збережений source | SHA256 |
|---|---|
| house/landmarks.json | 2ca26a33c09e4b52275161afabcbd53212e67a8a68c02cb8c7647aa995dde2b7 |
| house/GEOMETRY_ASSUMPTIONS_UA.md | d1c949159fc35a08be58ffe491ccb0e2507022c960e98f20946eadf172abde59 |
| house-collection.json | cd37d0b9936fa938213bdb26a151c2d4795141c0fab287cdd9d0c6cc636937a1 |
| licenses/Ribterra-Assets.txt | ca14f2c54c511542928e26d3903b8acbc1f7f5d48410f440787cec87271479e9 |

## Наступний обмежений крок

Потрібен явний mode `authored-xyz-triangle-materials-v1` в існуючому exporter/loader: mode-specific source_record/hash та alpha validation, exact extent/pivot/eppp, пряма palette→geometry raster відповідність, збереження original PNG/catalog/notice bindings. Нині painted collection exporter замінює triangle albedo на original RGBA; normal loader вимагає exact original cropped RGBA і відхилить цей інший RGB. Тому підключити новий master через старий painted mode означало б втратити задум neutral palette.

Після source acceptance нового mode потрібні retained channel exports, незалежні geometry/palette/alpha controls, actual lighting/anchor/occlusion на zoom0.62/1.30/3.0, новий CPU alpha-mask/selection і typed geometry outline. Окремо залишаються model silhouette completion, artists/reference review, emission/glass/local occluders/contacts, інші сім masters та animation/cargo/tier/season families. Жоден root engine/reference/performance gate ця data-only чернетка не закриває.

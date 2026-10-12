# Авторська XYZ-чернетка оригінального house

DRAFT. Production, neutral albedo, reference fidelity, export і native acceptance не прийняті. Оригінальні PNG/catalog/provenance/notices збережені. Чернетка не підключається до normal GameView.

## Джерело та спостереження

building.house.ready: art/concept-houses-low.png, rect[106,37,529,428], normalized pivot[.5,.98], pixel pivot[264.5, 419.44000244140625]. PNG має очевидні baked directional/ambient shadows, amber window/lantern glow та навколишній ореол. Це legacy-painted RGBA, не нейтральний albedo. Нормалі/depth не виводилися з RGB.

Вручну номіновано 206 screen landmarks, 53 поверхонь і 114 XYZ-трикутників roof/ridge/eaves, gable/two facades/foundation, chimney, door/three windows/lantern, steps/barrel/bucket/bench/частини fence. Screen tolerance здебільшого±3–5px — припущення номінації, не виконаний acceptance measurement. Master SHA256: 0e602fa94a406ee3aefd9c62550679434f2a75a87e37030312b2170d77299d9b. Faceted render proxy не є відновленою з одного PNG точною фізичною моделлю; тонкі joints, приховані деталі й деякі краї OPEN.

## Камера, масштаб і ground anchor

Існуючий basis: u=29(X−Y), v=14.5(X+Y)−29Z; depth=dot(XYZ,(1,1,1)/sqrt(3)). eppp4 — явне авторське припущення render units. Для nominated screen sx/sy задано Z, u=(sx−pivot_x)/4, v=(sy−pivot_y)/4; X=v/29+Z+u/58, Y=v/29+Z−u/58. Normals утворюються winding XYZ, а не painted brightness.

Reference width529 і crop збережені. Чинний house width76×2.1=159.6 до zoom; майбутній registered adapter очікувано застосовує scale159.6×4/529≈1.2068053. Native підтвердження цього очікування відсутнє. Ground origin лишається original pivot/Z0; gameplay footprint/placement/traversal не змінюються.

Vertical wall128px номіновано як height128/(29×4), foundation52px як52/(29×4). Painted ground-edge slopes не доводять ортогональну коробку в цьому basis, отже plan draft помірно skewed. Два roof triangles мають можливу невелику різницю площин: painted quadrilateral не доведений планарним. Це явні геометричні припущення, не physical dimensions/camera acceptance.

Door/frame/housing patches мають offset0.018, а inner glass0.024 уздовж camera ray для bounded layer separation без screen shift; glass має окремий frontmost layer над frame, тому emission не губиться в coplanar tie. Внутрішню floor chimney номіновано на0.15 нижче rim — візуальне припущення порожнини, не RGB estimator. Steps/props — грубі видимі facets.

Після незалежного draft review виправлено twisted post faces і bucket fan. Чотири post faces збережуть nominated screen outlines, але тепер кожна є вертикальною площиною через її два ground-edge endpoints. Верхній Z кожного краю дорівнює його власній вертикальній source-height/(29×4): верхні контури можуть бути похилі, cap рівним фізично не доведений і не змодельований. Це явний bounded draft компроміс registration, не відновлена конструкція стовпчика. Body bucket замінено справжньою зв'язаною циліндричною proxy geometry: source center202/411.5, radius0.085 render units, height24/116; одна спільна eight-vertex top ring та чотири vertical body facets. Нормалі між сусідніми body facets змінюються на45°, усередині кожної площини узгоджені. Flat cap має normal+Z. Rim/cylinder fit — ручне припущення до контурів original small bucket, не brightness estimator.

## Матеріали та shadow metadata

Collection копіює оригінальний cropRGBA без resampling/recolor/normalization. Triangle albedo — редагований placeholder, його замінює paintedRGBA. Roughness/wetness bounded, але не виміряні властивості slate/plaster/timber/stone/glass. AO1 не прибирає baked AO/shadows із RGB.

EmissionMask1 лише на номінованих glass трьох вікон і ліхтаря; radiance[1,.35,.08,1] provisional. Для майбутнього legacy-painted diagnostic preview emission scale має бути явно0 до neutral/source/causal review, щоб не приймати подвійне освітлення. External emitters, schedules та contacts відсутні/OPEN: unsupported metadata не вставлялися в collection schema.

CasterMask1 — чернеткова policy; collection явно painted-alpha-times-authored-mask, тому fractional alpha не примусово255. Sky triangles — тільки coarse opaque roof/main facades/chimney; fine-alpha shadows OPEN. Local occluders поки порожні: потрібні узгоджений plan, справжні bounds і owner-safe emitter anchors.

## Alpha support та probes

Максимальний within-post-face normal angle після repair: 1.47877933347e-06°. Planarity residual максимум: 2.26304525762e-16 render units. Точні normals/angles/residuals та cylinder parameters наведено в landmarks.coherence_repair. Це in-memory authoring calculation, не exporter/native acceptance.

Read-only in-memory authoring calculation на source pixel centers: nonzero alpha147212; supported by projected draft triangles127563; uncovered19649. landmarks.json містить точну RLE-map та32px tile counts. Це не export/test/native run і не незалежне художнє приймання. Missing pixels охоплюють навколишній baked halo та ще не змодельовані edges/props; автоматичного per-pixel фізичного розрізнення немає.

Не створено full-canvas/two-plane/billboard/nearest-fill підтримки для halo. Поточний collection exporter має відхилити цей incomplete draft через alpha>0 без XYZ. Export PENDING. Наступний art крок: розглянути карту missing pixels, доповнити лише справжню object geometry, а для baked glow створити окремий neutral paint/render source зі збереженням оригіналів.

Master має 20 face-qualified numeric probes; read-only nomination використовувала frontmost authored triangle, source alpha>=64 і clearance>=8px. Smaller window/ground/prop samples у landmarks.face_samples явно мають numeric_probe_8px_eligible false, якщо не проходять clearance. Співставлення з власними assumed faces саме по собі не доводить source-form correspondence. Потрібні artist/source review, actual reconstruction/lighting і native anchor/occlusion на zoom0.62/1.30/3.0.

## Походження й межі

Original paint provenance: LicenseRef-Proprietary; Ribterra project / OpenAI image generation; sourceproject; noticelicenses/Ribterra-Assets.txt. Original prompt і target0261/0262 bound їхніми поточними hashes. Нова геометрія — Ribterra/Codex authored proxy, не reference author's 3D model. References visual-only, не licence grant чи geometry ground truth.

Інші сім masters, tiers/seasons/construction, residents/facings/poses/cargo, neutral albedo, reference fidelity, normal startup, full engine та FPS OPEN. Root goal активний, formal checkboxes не закриваються.

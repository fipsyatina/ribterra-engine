# Нейтральні texture details: наступний контракт

Статус 09.10.2026: **SOURCE design only; реалізацію, export і native proof ще не почато**. Поточний flat-palette cohort лишається окремим. Повна ціль рушія, E01–E17/P01, три референси й ≥120 FPS активні/OPEN.

Незалежний огляд чинного `tools/export_material_master.py` підтвердив: XYZ визначає normal/depth/coverage, але RGB кожного triangle сталий. Triangle UV та per-material textures не підтримуються. Великі дошки/блоки можна авторити окремими triangles; дрібні grain/weave/stone variations потребують компактнішого джерела. Референси задають вигляд, а не достовірні фізичні coefficients.

Майбутня реалізація має додати явно новий `albedo_source.kind` і palette version. Старі painted та flat branches зберігають свої exact schemas і non-report bytes; нові поля в старому режимі відхиляються. Material color є one-of: сталий RGB або bounded neutral texture reference. Texture brightness не створює emission чи roughness.

Початковий запропонований texture contract: tight RGB8 sRGB, dimensions/byte count/SHA, author/license/notice/source identity. Raw RGB не потребує додаткового PNG decoder contract. Перед реалізацією зафіксувати максимальні dimensions, texture count, combined decoded/source bytes та source-record count у leaf contract; наведена форма не є вже підтриманим API. RGBA/texture alpha не керує geometry coverage.

По три finite normalized UV на triangle у порядку XYZ vertices; top-left origin, V вниз. UV можна тримати у palette assignment sidecar, зберігши geometry master schema. Перша версія має явний nearest/clamp sampler: interpolation UV використовує вже обчислені barycentric weights winning XYZ triangle. Bilinear/repeat/color-space rounding потребуватимуть окремих правил.

Texture sampling змінює тільки RGB. Normal походить із winding XYZ, depth із XYZ fragment position; чинні AO/material/emission/caster правила й geometry-derived binary alpha0/255 зберігаються. Поза geometry всі канали залишаються нульовими. Освітлений старий sprite crop не є нейтральним texture source.

Offline export далі створює ті самі шість кінцевих channels, тому новий GPU UV stream/RHI/shader ABI для цієї функції не потрібен. Runtime View має отримати окрему bounded textured-mode validation branch; загальне послаблення flat-palette tuple/field перевірок неприпустиме. Palette512KiB/notice128KiB, canonical paths/no-reparse, source/texture/UV/generated-albedo hashes, transactional publication та published hit-mask16MiB/outline20MiB limits залишаються явними.

Texture bytes, UV assignments, palette, provenance і notices входять до producer source bindings, manifest/source record та before/after/repeat ledger. Нинішні helper counts20/14 не можна мовчки застосувати до нового набору: нова versioned closure має вказати точну membership/count і всі inputs. GPU/runtime/object/export/capture roots мають власні128MiB caps, free384MiB; aggregate — чесна сума, без підміни індивідуальних лімітів спільним.

Потрібні фактичні перевірки: RGB-only texture change зберігає інші п’ять channels; UV-only change змінює тільки RGB; XYZ change впливає на normal/depth; спільні UV edges не породжують шов. Malformed dimensions/bytes/SHA/UV/one-of/provenance, oversize і reparse відхиляються до publication. Старі exports зберігають свої non-report bytes; report/source SHA deltas чесно відображають змінений producer. Два actual exports/native repeats і незалежний source-derived reader потрібні до приймання.

Цей контракт лише надає інструмент для деталей. Production masters складу/майстерні/дерева/каменю/мешканця з вантажем, terrain/water/bridge materials, завершення house silhouette/emitters/contacts та порівняння з трьома референсами ще потребують окремої роботи. Художню або фізичну достовірність не можна вивести з texture SHA.

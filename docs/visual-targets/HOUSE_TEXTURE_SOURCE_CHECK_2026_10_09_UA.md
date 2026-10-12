# House: нейтральні текстури, source/CPU checkpoint

09.10.2026 дві фактичні генерації0A і два фактичні exports820F завершилися exit0. Parent та незалежний read-only reviewer прийняли джерела й CPU-канали. GPU, ordinary main, художню відповідність референсам та FPS ще не прийнято.

Source roots: `art-source/production-materials-v1/house-textured-detail-2026-10-09-a` та `-b`, кожен10 файлів /546835 B. Три tight RGB8 textures займають405504 B: timber192×256, slate192×192, fieldstone192×256. Незалежно звірено всі процедурні RGB bytes, 23 source bindings, точний master, 14 scalar tuples,114 triangles,20 probes і10 sky triangles. XYZ, winding та emission0 збережені. Авторські UV перевірені за незалежною XYZ-проєкцією:102 shared corners/51 shared edges.

Export roots: `build/house-textured-detail-export-2026-10-09-a` та `-b`, кожен23 файли /4598809 B. Кожен має35 source origins,18 manifest roles та12 channel payloads: текстурований House і незмінний мальований resident. Змішані режими перевірено окремо; resident не приписували вигадану texture palette.

Незалежний actual reviewer18820 exit0 перерахував усі226412 canvas pixels House,127605 covered samples і20 probes за pinned7DDD rational oracle. Підтверджено48278 sampled RGB bins. Порівняно з flat draft змінилися89091 RGB pixels; alpha та інші п’ять House-каналів побайтно однакові. Шість resident-каналів, raw original RGBA,12 probes і metadata незмінні. Усі12 каналів двох exports повторилися побайтно. Source-prefix paths і залежні palette/source hashes звірені окремо за вісьмома явно переліченими групами; manifests/reports не оголошено побайтно однаковими між різними source roots.

Незалежна receipt: `.unlazy/engine-completion-2026-10-08/house-texture-source-review-2026-10-09/receipt.json`,65361 B, SHA256 `76679BF389E6A4BC41A066BCD016DFC829E491E8041BD2DF4D22144E3589B510`. Accepted=true, complete=true, errors=[]. Незмінні119 guarded inputs та п’ять retained inventories. Їхній чесний сумарний розмір14463036 B; кожен root окремо вкладається у128 MiB.

Parent повторно звірив119 inputs. Direct acceptance receipt: `.unlazy/engine-completion-2026-10-08/house-texture-source-CPU-accepted-2026-10-09.json`,1759 B, SHA256 `AE44394E7C75D2B38730A5204D4921E93498E35FAF6B207F746585529D1DBBB6`. Parent equality/rehash receipt:14316 B, SHA256 `C6BDDA85FCA4ECF83462FC23C2686EECAF85DE80C32FB6AA649DEA1E68A4CFA9`.

Це preview саме експортованого CPU albedo та трьох RGB джерел, без освітлення:

![House CPU albedo](C:/Users/fipst/Desktop/ribterra/.unlazy/engine-completion-2026-10-08/house-texture-preview-2026-10-09/cpu-albedo-preview.png)

Геометричний draft усе ще залишає19649 old-painted alpha samples непокритими й42 generated-only. Структурна деталізація силуету, віконне/ліхтарне світло, contacts/casters, інші production masters та референси залишаються відкритими. Збережений0194 — джерело формату авторинга; фактично виконаний producer — окремий accepted820F. Усі оригінали, невдалі діагностики й preimages збережено; файлів не видаляли. Automatic gate certification не заявляється. Повна ціль активна.

# Ribterra: native checkpoint нейтрального матеріалу, 09.10.2026

Parent і незалежна actual-перевірка reader9A прийняли конкретний snapshot Build06/Object06, positive05–06 та cold01–02. Це normal GameView::compose у LegacyCombined з provisional XYZ/flat-palette House і незмінним diagnostic painted Resident. Приймання стосується цього snapshot; повний рушій та production art ще OPEN. Формальні checkbox у [leaf1.39](../../.unlazy/engine-completion-2026-10-08/gates/leaf-1.39.md) і [leaf1.46](../../.unlazy/engine-completion-2026-10-08/gates/leaf-1.46.md) не позначено автоматично.

Перевірена checkout: AInora/first-playable, HEAD bdc53e82c3d65eee4a5d8b13497aea67dff4a33b. Checkpoint включає робочі зміни й точні source bindings, тому HEAD сам по собі його не описує. Реальний backend — Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver32.0.15.9282, 1920×1080; FPS і GPU execution time — null.

| Збережений root у build/ | Actual результат | Регулярні файли / bytes |
| --- | --- | --- |
| gameview-neutral-material-2026-10-09-attempt06 | 183 source bindings; 122 strict commands exit0; 93 shaders, 4 generated headers | 536 / 58 279 114 |
| gameview-neutral-material-objects-2026-10-09-attempt06 | 24 objects: 23 production + probe | 24 / 80 295 429 |
| neutral-material-native-positive-2026-10-09-attempt05 | native + wrapper exit0; 20 PNG, 6 fresh saves | 30 / 116 403 712 |
| neutral-material-native-positive-2026-10-09-attempt06 | native + wrapper exit0; 20 PNG, 6 fresh saves | 30 / 116 403 719 |
| neutral-material-native-cold-2026-10-09-attempt01 | native + wrapper exit0; 24 відмови, 20 candidate catalogs, 0 PNG | 386 / 88 213 065 |
| neutral-material-native-cold-2026-10-09-attempt02 | native + wrapper exit0; 24 відмови, 20 candidate catalogs, 0 PNG | 386 / 88 213 066 |

Runtime та Object roots окремо вкладаються у 128 MiB; їхня сума — **138 574 543 B**, без aggregate≤128MiB заяви. EXE — 47 809 024 B, SHA256 A7FA78FAE7677981DECE52BD58CABF1F8AB35370BFE37E835D4981F8164BD0B8. Кожен cold root також зберігає три власні aliases/junctions, які inventory не обходить. Після всіх 24 відмов кожен cold виконав один actual all-painted Default compatibility render; нуль PNG не означає відсутність render.

Усі чотири wrapper перевіряють незмінність 2 860 inputs до/після та 36 звичайних preferences. Повторні семантичні дані й пікселі звірено окремо від measured.total_ms: positive05/06 — 72 845.479 / 75 381.3997 ms; cold01/02 — 45 678.021 / 46 508.2438 ms. Це повний час діагностичного запуску, а не FPS чи час GPU pass.

Прийняті source-derived й actual controls:

- Шість typed house/resident queries у трьох zoom, спільні draw/query anchors та picking за pooled alpha mask; вісім реальних outline offsets.
- Два фактично вибрані House IDs **2+4**, Workshop **5** між ними, повторне використання texture/cache handle, точні 64 додані vertices / 96 indices для двох outlines.
- Same-frame paired render видаляє лише визначену шестиіндексну typed House body command. Решту published vertices, materials, companions, UI та environment збережено; кваліфікована зміна пікселів доводить видимий внесок саме body.
- П'ятнадцять direct API controls, дві відмови invalid cache limits, transactional cold catalogs і post-submit rejection; реальні menu/HUD/World переходи, prefix та canonical companions, успадковані animation/variant/Mine/unavailable fallback.
- Валідний початковий cache містить 388 hit masks і 132 outlines. Логічні caps 16/20 MiB збережено; ці відмови не доводять фізичний bad_alloc або GPU OOM.

Generated House має 127 605 alpha samples: 127 563 перетинаються зі старим малюнком, 42 є generated-only; зі 147 212 old-paint samples **19 649** лишаються поза draft. Це прийнята CPU/native реєстрація provisional draft; художня прогалина лишається OPEN. Palette є авторським provisional припущенням; reference fidelity, фізичні коефіцієнти, neutral production albedo та всі вісім production masters не прийнято.

Compiled reader tests/gameview_neutral_material_evidence_tests.py лишається незмінним live/Build06 provenance: **102 610 B**, SHA256 8A8622782B960F676E2F324A04B17EF1B2161CAF84DD08DE7E6BF7BDE2E2866F. Окремий **POSTBUILD** reader tests/gameview_neutral_material_evidence_review_tests.py — **106 187 B**, SHA256 9A134D1A23CDF986E1CB6D20F229B9427AE21ADB483325341B191435DFB4A1DD. Reader9A виправляє порядок/keyed inventories, float32 projection і valid-cache expectation без послаблення caps, numerical tolerance або заміни C++/raw artifacts. Його не записано як compiled provenance.

Parent actual reader9A positive77172 і cold20422 завершилися exit0; [parent receipt](../../.unlazy/engine-completion-2026-10-08/neutral-material-parent-9a-review-2026-10-09.json) — **9 450 B**, SHA256 8B0024EC83DE65CA061D55B036B7948A4C5F35A70AEEEE784030F90249E3F1E3. [Незалежна actual receipt](../../.unlazy/engine-completion-2026-10-08/neutral-material-independent-9a-review-2026-10-09.json) має complete=true, обидва profiles exit0 і guarded_bytes_unchanged=true; SHA256 71FAF4D6CCC723AA2E696BB009C019B528D26A8F07A222B4DCF1C57C3A57E055, **11 128 B**. Незалежний reader знову виконав всі семантичні перевірки; positive witness також має вісім in-memory negative controls.

Невдалі Build01–05, positive01–04, original8A/741/E94/659 reader attempts, кандидати й live preimages збережено. Positive04 перевищив ліміт — **149 077 979 B** — і не прийнятий; відсутність його точного wrapper-after ledger зафіксована, а не заповнена припущенням.

Наступні хвилі мають окремий статус. Textured producer successor0194FD40… / 111 797 B і CPU test D712108531… мали незалежний SOURCE CLEAR, але actual attempt01 завершився **exit1/HOLD**: AssertionError: Independent twelve numeric probes/5/source_pixel/1. Три old-compat CLI та новий export-a мали exit0, dense six-channel oracle PASS, inputs незмінні. Причина — порядок обчислення diagnostic target: authored XYZ перед projection проти зважених projected screens, три ULP на Y71 tie; це не встановлена помилка RGB producer. Test-only successor5F406053… / 108 931 B незалежно пройшов SOURCE review, зберіг точний 12-probe contract і D712 preimage; actual attempt02 зараз виконується, **не прийнятий**.

Runtime1.45 candidates BBBE38DC/23BFF6A1/FBACE825/95D8580D мають SOURCE CLEAR і ще не застосовані/зібрані/native-перевірені. Main1.44 E0F4, helperE6A65203, runner7E165832, readerC94C6927 мають незалежний SOURCE CLEAR; parent опублікував чотири reviewed файли зі збереженням preimages (receiptA9AE2619/source-release7D4C51A4) і розпочав свіжий strict main helper. Actual main build/native-приймання ще OPEN.

Повна ціль E01–E17/P01 лишається активною: normal main/package, FullFX/Cumulative, all8 production assets, три референси, усі resident directions/phases/cargo, normal mirror branch, remote recovery, physical two-PC/co-op, full suite цього cohort, physical OOM і ≥120 FPS ще OPEN. Цей документ не сертифікує ці наступні роботи.

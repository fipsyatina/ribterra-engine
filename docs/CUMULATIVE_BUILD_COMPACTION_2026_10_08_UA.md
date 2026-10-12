# Bounded cumulative compiled copies: очищення 08.10.2026

Після fresh build04, actual native04/05, parent verifier exit0 та незалежного CLEAR видалено лише **81 повідомлений compiled-файл / 357 978 433 логічні байти** з failed cumulative build01/02/03 (по27 файлів). Каталогів, source/art/saves/logs/PNG/shader assets не видалено.

Exact plan: [plan02](../build/cumulative-compiled-prune-2026-10-08-plan02.json), SHA256 `86F5B7433458B022711479B8925F43A2B66539BDAA87BDBF32729C8DC521D99E`. Helper SHA256 `4859DC220A32DF65E0B04E97FF80AF11E51E2E65B546B597BF0FD0CC48752928`. [Actual result02](../build/cumulative-compiled-prune-2026-10-08-result02.json): complete true, remaining0, protected3654 unchanged. Current evidence verifier actual exit0 до й після видалення.

Target roots під `C:\Users\fipst\Desktop\ribterra\build`:

- `gameview-engine-cumulative-2026-10-08-attempt01`: 27 файлів / 119 294 043 bytes.
- `gameview-engine-cumulative-2026-10-08-attempt02`: 27 файлів / 119 342 053 bytes.
- `gameview-engine-cumulative-2026-10-08-attempt03`: 27 файлів / 119 342 337 bytes.

Збережені повний replacement build04, native01–05, актуальний kit, verified environment02 rollback, launcher targets та всі non-target файли failed roots. Snapshot/logs дозволяють дослідити помилки; старі EXE не запускаються. Архів цих binaries не створювався, відновлення потребує перекомпіляції.

Початковий dry-run plan01 мав коректні target hashes, але помилково нульовий загальний byte ledger через `Measure-Object` над OrderedDictionary. До Apply це виявлено й виправлено explicit integer summation. Plan01 збережено як superseded; новий plan02 прив'язаний до виправленого helper. Перед Apply користувачу повідомлено точний manifest, шляхи, обсяг і recovery limits.

Фактичний C: free snapshot змінився з98 292 613 120 до98 652 995 584 bytes (+360 382 464). Це системний free-space delta за інтервал, а не точний незалежний physical-size ledger видалених файлів. Доказ target обсягу — їхні measured logical bytes та hashes.

# Зберігання SessionShown checkpoint — 08.10.2026

Після fresh time build02 і двох actual native runs збережено актуальний runnable `build/gameview-session-time-2026-10-08-attempt02` та один перевірений renderer rollback `build/gameview-engine-cumulative-2026-10-08-attempt04`. Це ізольовані перевірки, не нові повні пакети гри; активні launchers не змінювалися.

Після advance exact-path notification виконано лише новий allowlist: 27 compiled EXE/DLL/LIB/EXP/OBJ у `build/gameview-environment-2026-10-08-attempt02` та 21 production OBJ невдалого time build01. Видалено **48 файлів / 173 144 494 логічні байти**; directories, source snapshots, assets, reports, saves і PNG не видалялися.

- Helper `build/session-time-compiled-prune-2026-10-08-01.ps1`, SHA256 `4C6C59DD88DEAD025AE4685055768D70965108EEB47FD3413C6DEEF35E8504E4`.
- Exact manifest `build/session-time-compiled-prune-2026-10-08-plan01.json`, SHA256 `C2E9DDAEB7B5F021879E21ED0F8D35A831A96319DFCEC0CC9988B52D7150F018`.
- Result `build/session-time-compiled-prune-2026-10-08-result01.json`: actual exit 0, complete true, remaining 0, **3 253 protected rows unchanged**. Independent read-only helper/plan review CLEAR до Apply.
- Current SessionShown verifier actual exit 0 до й після видалення: sources158 / bound599 / commands120 / repeat19.

Захист охоплював latest time02, cumulative04 rollback, обидва time native runs, retained environment/cumulative native evidence, kit exports, нецільові файли очищених roots, runtime origins та current launcher bindings. Застарілий environment02 тепер не runnable; failed time01 objects більше не доступні. Програмні копії не архівовано; відновлення потребує повторної компіляції. Після наступних source edits перевірена time cohort є історичним checkpoint; це не змінює факт збереження її executable і actual evidence.

Цей plan вже виконаний і не є cleaner для наступних roots. Новий scope потребує нового exact manifest і advance notice.

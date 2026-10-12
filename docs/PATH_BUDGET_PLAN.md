# Бюджетована черга маршрутів: контракт і перевірка

> Версія 0.6 реалізує спільний пошук, повний стан черги у сейві та двофазні мережеві відповіді. Локальні 33/33 CTest, 1500/4×1500 planner fixtures, D3D12 UI та розпакований пакет пройшли перевірку; докази й обмеження наведено в `VALIDATION.md`. Версія 0.5 лишається історичною базою порівняння.

## Чому зміна потрібна

План вимагає 20 ticks/s, p95 tick до 10 ms, p99 до 20 ms, ієрархічного пошуку `region → chunk → cell`, черги з бюджетом на tick та спільних маршрутів для групового руху ([source, рядки 221–245](source/development-plan-ua.txt)). Окремо вимагаються обмежені черги, детерміноване застосування результатів на межі tick та зрозумілий стан очікування масового наказу ([source, рядки 897–906](source/development-plan-ua.txt), [984–999](source/development-plan-ua.txt)).

У 0.5 BFS синхронний. Точні production callsites:

- `src/sim/world.cpp`, `World::Impl::set_route` — створює й повністю обходить масив із 131072 клітин двох шарів.
- `World::move` — викликає `set_route` для кожного ID, перед цим копіює весь `WorldState` для rollback.
- `World::gather` — повторює ту саму схему для маршруту до сусідньої клітини ресурсу.
- `World::build` — синхронно шукає маршрути для трьох будівельників.
- `World::tick` — синхронно перебудовує маршрут, якщо наступна клітина стала непрохідною.
- `src/server/game_server.cpp`, `GameServer::tick` → `apply` — спочатку вичерпує всю чергу команд; кожна Move/Gather може виконати всі зазначені BFS до початку `World::tick`.

`ClientSession::Impl::queue` і transport лише ставлять серверну команду в чергу; шляхів вони не будують. Виміряний `SimRunner --benchmark-chunks` підтверджує проблему саме у початковій побудові маршрутів.

## Контракт черги

1. Додати до `World::Impl` глобальну FIFO `PathBatchQueue`, один `ActivePathSearch`, монотонний `next_path_batch_id` і `topology_revision`. Максимум — 512 batch-ів та 1500 унікальних resident ID у batch, відповідно до чинних server limits.
2. `World::move` і `World::gather` після дешевої перевірки резервують мешканців і повертають розширений `CommandResult { accepted, message, path_batch_id, pending }`. Резервування на межі tick зупиняє лише locomotion старого Route, фіксує поточні float Position і стартову клітину, але не зупиняє hunger/health та інші simulation systems. До commit Position не змінюється; commit встановлює новий Route без запису координат, тому немає teleport. Другий наказ для вже зарезервованого мешканця на цьому етапі відхиляється; скасування/заміна є окремою функцією.
3. Один batch із спільною ціллю використовує один reverse BFS: для Move — від цілі, для Gather/Build — від усіх прохідних сусідів цілі. Стартові resident ID канонічно сортуються. Порядок сусідів лишається фіксованим `N, W, E, S`, після них портал. Це прибирає 1500 однакових обходів і зберігає відтворюваність.
4. На початку `World::tick` planner витрачає не більше `path_work_units_per_tick = 4096`. Одна одиниця — BFS expansion, один parent-step реконструкції або один `CellCoord`, доданий/скопійований у staged Route; отже завершальний tick не копіює 1500 маршрутів без бюджету. Початкові межі: `max_path_cells = 2048`, `max_staged_route_cells = 3'072'000` (1500×2048) і `max_planner_bytes = 64 MiB`; перевищення термінально відхиляє весь batch. Це детерміновані лічильники роботи й пам'яті, а не wall-clock deadline. FIFO batch повністю готується або повністю відхиляється; вже staged маршрути всіх його мешканців застосовуються одночасно на межі tick лише через swap/move, у порядку resident ID. Рух виконується після цієї фази.
5. Будівництво й автоматичний replan також створюють внутрішні batch-и. Жодного прямого виклику повного BFS поза planner phase не залишається.
6. `topology_revision` змінюється при появі/зникненні блокера, зміні порталу та переході `MineOre → MineFloor`. Якщо revision активного пошуку застарів, його прогрес детерміновано скидається до початку до витрати бюджету поточного tick. Batch зберігає `enqueued_tick`, має `deadline_tick = enqueued_tick + 1200` (60 s при 20 Hz) і максимум три topology restarts. Deadline або четверта invalidation дає термінальний failure, звільняє всі reservations і дозволяє наступному FIFO batch працювати; частковий або застарілий шлях ніколи не потрапляє в `Route`.

Окремий ECS-компонент `OrderStatus` зберігає фазу, типізовану причину відмови та ID очікуваного batch. Видимий DTO мешканця містить `OrderPhase { Idle, PathPending, Active, PathFailed }`, `PathFailure` і `pending_path_batch`. Пошуковий frontier, parent map і повні маршрути у `ViewSnapshot` не передаються. HUD показує кількість вибраних мешканців у черзі та причину відмови мовою інтерфейсу.

## Відповіді команд, save і network

Move/Gather потребують двофазної відповіді: `Queued` після прийняття batch і одна термінальна `Applied` або `Rejected` після атомарного commit. У `schemas/network.fbs` поле `Reply` слід розширити enum-станом; dedup і ACK мають ідентифікувати `(request_id, phase)`, бо поточний `NetworkClient` відкидає другу відповідь з тим самим request ID. `GameServer` тримає `path_batch_id → (PlayerId, GameCommand)` і переносить команду з pending до completed лише після термінальної відповіді. Повтор ідентичного request ID повертає поточну фазу; змінене повторення лишається конфліктом.

`WorldState` і `schemas/world_save.fbs` зберігають FIFO batch-и, `next_path_batch_id`, `topology_revision`, planner phase, `work_units_done`, frontier/cursor, parent nodes і частково підготовлені маршрути; ці поля входять до checksum. Формат 4 зберігає повний прогрес без повторного виконання пошуку при load. Тимчасові індекси відновлюються з перевірених вузлів. Міграція зафіксованого v3 створює порожню чергу та зберігає оригінал як `.v3.bak`; старі v2 і текстові v1 лишаються підтриманими.

Server/session queues не входять до world save. Успішний `GameServer::load` інвалідовує стару transport-сесію та очищує її кореляції; завантажені simulation batch-и продовжують роботу без старих мережевих відповідей. Невдалий load зберігає чинний світ і сесію. Звичайний disconnect не скасовує вже прийнятий наказ: його термінальний результат лишається доступним при reconnect у той самий серверний процес. Durable replies після повного restart залишаються окремим незакритим контрактом.

## Acceptance tests

1. **Жорсткий бюджет:** довгий маршрут займає кілька ticks; сума expansion/reconstruction/copy work за tick не перевищує 4096, staged routes і planner memory не перевищують установлених меж, а виклик Move не будує маршрут синхронно.
2. **Групова атомарність:** 1500 мешканців з однією ціллю мають один shared search; під час очікування їхні Position не змінюються, hunger/health продовжують оновлюватися, а commit не телепортує fractional position. Після завершення всі 1500 переходять `PathPending → Active` на одному tick.
3. **Детермінізм і save/resume:** два однакові світи дають ті самі batch IDs, completion ticks, routes і checksum; save/load посеред пошуку збігається з безперервним прогоном на кожному наступному tick.
4. **Topology invalidation:** блокування клітини під час пошуку збільшує revision і перезапускає пошук; четверта invalidation або tick 1200 термінально завершує batch і пропускає наступний. Жоден застосований шлях не проходить через новий блокер. Відкриття MineFloor після виснаження руди дає маршрут лише через звичайну чергу.
5. **Reply/idempotency:** мережевий клієнт отримує рівно `Queued`, потім одну terminal phase; повтор пакета не створює batch, altered reuse відхиляється, ACK кожної фази не губить наступну.
6. **Межі та privacy:** 513-й batch і overlapping resident order відхиляються; тривалий headless run не збільшує чергу без меж; snapshot не містить frontier, прихованих клітин або чужих target/route даних.

## Неперевірені рішення

- Значення 4096 є стартовим детермінованим бюджетом, а не підтвердженим performance optimum; його слід затвердити benchmark-ами 10/100/500/1500 і 4×1500.
- `planner_bytes` — детермінована оцінка стану за розмірами елементів і фіксованими накладними витратами індексів. Межа 64 MiB не є вимірюванням фізичної пікової пам'яті процесу: capacity контейнерів, allocator і тимчасові буфери save/load перевіряються окремо.
- Ієрархічний region/chunk cache та локальне уникнення заторів виходять за мінімальний queue milestone. Shared reverse BFS можна впровадити зараз, а hierarchy — після профілю.
- Потрібно виміряти розмір pending-search save, час load і пікову пам'ять save/load. Frontier і parent state зберігаються напряму; повного replay алгоритму немає.

# Виправлення запуску 0.10.2

9 вересня 2026 року.

У 0.10.1 заголовок SDL-вікна залишався жорстко записаним як «pre-alpha 0.4», хоча engine_version і runtime були новими. Це вводило в оману. Крім того, Play Ribterra працював лише у checkout з dist/current-build.txt; GitHub source ZIP не містив EXE, а готовий Windows ZIP узагалі не містив цього launcher. Пакетна README лишалася від 0.8. Попередній огляд framebuffer не перевіряв заголовок OS-вікна та запуск через CMD.

0.10.2 бере заголовок із engine_version; renderer-info записує actual SDL window title та engine version. Launcher входить до готового ZIP, працює від власної папки, передає аргументи й залишає помилку видимою. У source checkout перевіряється саме версія vcpkg.json: за відсутності відповідної локальної збірки завантажується приватний GitHub release ZIP + SHA-256 через авторизований gh. Якщо gh недоступний, показується сторінка готового пакета. Припущення про старий dist/Ribterra вилучене; встановлені попередні версії збережені.

Пройдено запуск через repository Play Ribterra зі старим pointer на 0.10.1 та через Play Ribterra із розпакованого ZIP у папці з пробілами, з іншого cwd. Обидва дали exit 0, D3D12/RTX 3050 Laptop, engine_version=0.10.2 і actual window_title=Ribterra 0.10.2. Окремо новий пакет пройшов dedicated server + два probes + graphical client. [Результати](evidence/launch0102-results.json), [metadata ZIP](evidence/launch0102-package.json).

Ігрові правила, симуляція та мережевий код у цьому виправленні не змінювалися. Save 8 / protocol 7 / rules 7 / generator 2 збережено. Попередні Windows 52/52, Linux 35/35 і UBSan 13/13 у VALIDATION стосуються 0.10.1; цей документ не підміняє їх результатами нової повної CI-збірки. Окремий CI нової ревізії запускається під час push.

Додатково перевірено фактичний GitHub source ZIP: launcher сам завантажив приватний реліз, перевірив SHA-256, установив пакет і відкрив 0.10.2. Перший такий прогін виявив несумісність Get-FileHash/Expand-Archive з успадкованим PowerShell 7 module path у Windows PowerShell 5.1; installer переведено на стандартні .NET SHA256 і ZipFile, остаточний прогін успішний. Також виправлено й перевірено Play Ribterra у вже завантаженій користувачем папці Downloads; оригінальний launcher збережено як .bak.

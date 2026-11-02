# Very Light RGB 1.2.0 — native Windows x64

Мова інтерфейсу за замовчуванням — **English**. У головному меню **Language / Мова** можна вибрати **English** або **Українська**; вибір зберігається між запусками. Написи меню, палітри, підказки, прогрес reload і tray-меню оновлюються без перезапуску анімації.

C++20 / Win32. Один фоновий tray-процес, окремий процес текстового меню лише на час користування. .NET, WinForms, Qt, Python та встановлення драйверів не потрібні. Потрібна Windows 10/11 x64 та штатні драйвери обладнання.

Native Auto додатково запускає включений `native-drivers/native-driver-core.exe` лише для вибраних сумісних контролерів. Це локальний JSONL worker через приватні pipes, без SDK-сервера. Transport DLL зібрані з вихідного коду й не потребують окремого Visual C++ runtime. Новий worker зберігається й на статичному кольорі для апаратного keepalive; при втраті процесу/USB endpoint engine перепідключає кешовані виходи. Фізичний rescan виконує тільки Reload devices.

`driver.cpp` відповідає за прихований процес, bounded I/O, kill-on-close job, каталог зон і маршрутизацію кадрів. Відомі MSI/Gainward виключаються з додаткової авто-детекції. Для збирання корпусу драйверів потрібен Node.js; runtime від Node не залежить. Оригінальні архіви, зміни bridge й ліцензії включені до `native-drivers/sources` та `native-drivers/licenses`.

## Збірка

```powershell
.\scripts\build.ps1
.\scripts\install.ps1
```

Перший build завантажує офіційний переносний LLVM-MinGW 20261020 UCRT x64 у `local/toolchain`, перевіряє закріплений SHA-256, компілює з `-O2 -Wall -Wextra -Werror` та статично лінкує C++ runtime. Системні бібліотеки Windows залишаються DLL. Інкрементальний кеш враховує вихідні файли, заголовки та прапорці. Бібліотека JSON 3.12.0 уже в `vendor` разом із ліцензією.

Результат: `artifacts/VeryLightRGB/VeryLightRGB.exe`, `native-drivers` та тексти ліцензій. Основний EXE приблизно 2 МіБ. Не переносити `tests.exe` у користувацьку інсталяцію. Старий C# проєкт збережений у `src`; `build.ps1 -Legacy` збирає його окремо.

Фіолетова V вбудована в EXE та використовується в треї й ярлику. Редагований оригінал — `assets/app-icon.svg`; `scripts/build-icon.ps1` відтворює PNG та ICO з прозорістю й розмірами 16–256 px через штатний System.Drawing. Готовий ICO вже в репозиторії, тому звичайна збірка не потребує графічних інструментів. Класична консоль також отримує іконку; Windows Terminal може показувати власну іконку вкладки.

`scripts/build-installer.ps1` створює Inno Setup installer. `install.ps1` запускає його; `-NoLaunch` залишає програму зупиненою, але перевіряє child-driver на контролері в пам’яті. Перед оновленням helper штатно завершує агент і закриває тільки меню цільового EXE; кожна підтверджена зміна конфігурації вже збережена атомарно через IPC. Існуючі settings мають пріоритет над legacy-міграцією. Setup пропонує автозапуск і ярлик, не змінює SignalRGB чи інші сторонні записи. Uninstaller видаляє лише свою startup task і зберігає користувацькі settings. Права адміністратора не потрібні. `scripts/verify-clean-install.ps1` — opt-in реальна перевірка зі збереженням і відновленням усієї поточної інсталяції.

## Модулі

| Файл | Відповідальність |
|---|---|
| `core.cpp` | Конфігурація schema 3, сумісність із schema 2, каталог, план виходів, дев’ять ефектів |
| `hardware.cpp` | Перевірені MSI HID / Gainward NVAPI, перевірка USB/PCI ID та конфліктів |
| `sdk.cpp` | Локальний OpenRGB SDK, формат 3, обмеження пакетів, стабільне зіставлення зон |
| `driver.cpp` | Приховане native-ядро без сервера, авто-детекція, кеш зон і передача кадрів |
| `engine.cpp` | Один worker, таймер кадрів, повторні підключення, окремі стилі, статичні виходи |
| `ipc.cpp` | Named pipe поточного SID, ACL користувач + SYSTEM, bounded I/O |
| `startup.cpp` | Interactive logon task, подієве відновлення після збою, ярлик |
| `tui.cpp` | Символьне меню, палітра / HEX, параметри, вибір виходів |
| `localization.cpp` | English / Українська: написи, підказки, повідомлення про стан |
| `main.cpp` | Приховане tray-вікно, CLI, події Windows / живлення / USB |

Каталог оновлюється при першому налаштуванні, зміні backend / SDK-порту та `Reload devices`. Звичайний native запуск відкриває відомі вибрані контролери. Актуальний HID interface шукається за перевіреними ID. SDK при новій TCP-сесії зіставляє зони з поточними індексами сервера; запитів фізичного rescan, зміни firmware або запису flash клієнт не виконує.

На статичному кольорі кадри й сесії прямих MSI/Gainward-модулів звільняються; NVAPI звільняється після статичного запису. Для вибраних додаткових контролерів native-ядро лишається відкритим для keepalive й перевіряється кожні дві секунди. Анімація використовує waitable timer, пропускає однакові кадри, не переписує статичну GPU через анімацію хедера і не змінює глобальний системний таймер. Нативні HID-буфери перевикористовуються.

## CLI

GUI subsystem EXE може не показувати stdout у PowerShell; для машинного результату використовуй `--output`.

```powershell
$rgb = "$env:LOCALAPPDATA\Programs\VeryLightRGB\VeryLightRGB.exe"
Start-Process $rgb --menu
Start-Process $rgb -ArgumentList '--off' -WindowStyle Hidden -Wait
Start-Process $rgb -ArgumentList '--on' -WindowStyle Hidden -Wait
Start-Process $rgb -ArgumentList '--reload-devices' -WindowStyle Hidden -Wait
Start-Process $rgb -ArgumentList '--status --output "C:\Temp\rgb-status.json"' -WindowStyle Hidden -Wait
Start-Process $rgb -ArgumentList '--exit' -WindowStyle Hidden -Wait
```

Також: `--background`, `--enable-startup`, `--disable-startup`, `--startup-info`, `--request FILE --output FILE`, `--data-dir ABSOLUTE_PATH`. CLI та меню керують одним агентом поточного користувача через pipe; повторний background запуск завершується без другого агента.

`--sdk-list --sdk-port 6742 --output FILE` читає реальні контролери, зони (також нульової довжини), режими й кеш кольорів OpenRGB без апаратних записів. `--sdk-resize --sdk-port 6742 --id STABLE_ZONE_ID --led-count 12 --output FILE` — явне setup-налаштування довжини з перевіркою меж; після нього потрібен Reload devices. Переприв’язка робиться за ID, а не збереженим порядковим індексом. Довжина SDK-зони в звичайному меню залишається read-only.

У [third_party/openrgb](../third_party/openrgb/README.md) збережені офіційний portable та вихідний архів релізу 1.1, manifest / SHA-256, ліцензія та скрипти запуску. `scripts/package-native.ps1 -WithOpenRgb` включає їх в окремий ZIP.

RPC: `status`, `diagnostics`, `set-config` із `Revision` та `Config`, `reload-devices`, `reconnect`, `identify` із `Id`, `startup` із `Enabled`, `menu`, `quit`. Застаріла revision відхиляється. `diagnostics` читає налаштування й обмежений хвіст журналу саме у контексті агента.

Для змін освітлення меню використовує `edit-lighting` із `Revision`, `Scope` (порожній — усі вибрані виходи), `Field` (`color`, `secondary`, `brightness`, `effect`, `speed`, `width`, `power`) та `Style`. Агент атомарно перевіряє revision при збереженні та змінює тільки обране поле. Глобальні зміни кольору / режиму охоплюють незалежні стилі, не зачіпаючи їхніх інших параметрів; master power зберігає локальні power-прапорці. Локальна зміна пов’язаного виходу від’єднує його від шаблону, копіюючи поточні ефективні параметри. `set-config` лишається точним записом конфігурації для міграції / відновлення.

`status.Reload` містить `Id`, `State` (`idle`, `queued`, `running`, `done`, `warning`, `error`), `Stage`, `Completed`, `Total`, `Found`, `Notes`. Кроки — реальні етапи, не оцінка часу / ETA. Вони не зберігаються в конфігурації; повторний Reload під час активного скану не створює ще один скан. TUI читає стан раз на 250 мс лише під час reload, інакше раз на секунду. Коли TUI закрите, UI polling немає.

Навігація TUI зберігає екран, вибраний рядок та scope у bounded history. Esc повертає назад, Q закриває тільки меню. Кольоровий текст форматується після розрахунку ширини рядка, тому ANSI-послідовності не ламають рамки.

Новий процес TUI стартує на головному меню. Reload і його shortcut R доступні у Devices / Пристрої та System / Система; головний екран не містить дубльованих гарячих клавіш розділів / reload. Ширина рамки адаптується до термінала до 112 колонок; великий банер має потовщені штрихи, кольоровий фон і компактні варіанти для вузьких вікон. Тема береться з першого керованого виходу поточного backend (`Style.Color` або спільний колір для linked-зони), не з яскравості / другого кольору. Ця логіка працює лише у процесі меню.

Дані: `%LOCALAPPDATA%\VeryLightRGB\settings.json`, `light.log` (ротація після 512 КіБ). Каталог зберігається разом із налаштуваннями, профілі не зберігаються. Журнал не містить покадрових записів.

Задача `VeryLightRGB-<SID>` працює після входу користувача з затримкою 10 секунд, без elevation / обмеження часу виконання. Події Application Error 1000 для точного EXE-шляху та власна fatal-подія 2001 запускають відновлення із затримкою 5 секунд. Звичайний `quit` / вихід із tray не перезапускає програму. Примусове завершення Task Manager без crash-події не покривається. Recovery не працює, якщо користувач вимкнув автозапуск.

## Тести

```powershell
.\scripts\build.ps1
& .\artifacts\VeryLightRGB\tests.exe
# OPT-IN: реальні зміни світла; агент та інші RGB-програми мають бути закриті
& .\artifacts\VeryLightRGB\tests.exe --hardware
# Реальний запущений SDK-сервер, лише читання каталогу:
& .\artifacts\VeryLightRGB\tests.exe --openrgb 6742
# OPT-IN: точні перевірені MSI/Gainward, JARGB 1 має бути налаштований на 12 LED:
& .\artifacts\VeryLightRGB\tests.exe --openrgb-hardware 6742
.\scripts\measure-resources.ps1 -Seconds 60
```

Звичайні тести не відкривають RGB-обладнання. 432 еталонні кадри попередньої C# версії перевіряють усі ефекти, межі циклів, довжини, напрямок і зміщення. TCP-емулятор перевіряє OpenRGB framing / перепідключення / перетасовані індекси / пошкоджені пакети та ізоляцію статичних виходів. Є перевірки конфігурації, IPC, UI-тексту й точного збереження невибраних MSI-зон. Hardware-набір робить readback після різних кольорів, 24 кадрів темного блоку, перевіряє black/off і відновлює попередній стан.

`--menu-self-test` — короткий вбудований console smoke test: рендерить реальне меню, читає власний console buffer і записує `menu-self-test.json`, потім закривається. RPC `menu` із `SelfTest:true` використовує той самий шлях створення консолі, що й double-click tray. Зовнішня автоматизація вводу клавіш не потрібна.

Обмеження та результати вимірювань: [рев’ю](../docs/review-native-v1.md).

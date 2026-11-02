# База пристроїв і native-протоколів

База та модулі Very Light RGB для роботи без OpenRGB/SignalRGB-сервера. Починаючи з **1.1.0**, provider інтегровано в головну програму: Native Auto / Offline, Reload devices, вибір зон, кольори й ефекти. [Інтеграційні перевірки](../docs/native-driver-integration-v1.1.md).

Поточна база: **2204 записи, 1968 різних назв, 198 груп контролерів**. Це каталог вихідних реалізацій, а не обіцянка фізично перевіреної підтримки 1968 девайсів. Точні числа й розподіл транспорту — у [summary.json](summary.json).

Додатково зібрано **повне native-ядро з 2144 реальними реєстраціями детекторів**, без Qt та SDK-сервера. Воно вже має виконуваний JSONL API для прямого керування. [Інструкція ядра](core/README.md) · [зіставлення provider → записи бази](providers.json).

Пошук, match та inventory у light EXE підмішують цей provider-index: 2130 зіставлених записів показуються як `native-experimental` із маршрутом до driver-core. Вихідний `catalog.json` зберігає первинний статус імпорту; ID/endpoint-обмеження від додавання provider не послаблюються.

## Що збережено

| Файл | Вміст |
|---|---|
| `catalog.json` | USB/PCI ID, interface/usage page/usage, тип транспорту, upstream-детектор, невирішені вирази, ревізія, SHA-256 і посилання на точний рядок джерела |
| `protocols.json` | 15 566 статичних констант, 1869 місць HID/USB/I²C-обміну з аргументами та посилання на реалізації |
| `local/device-db-research` | Приватний дослідницький імпорт із SignalRGB; не входить у публічну базу або релізи |
| `source-files.json` | Хеші 1702 файлів ліцензованих джерел |
| `import-issues.json` | Вирази та метадані, які потребують ручного аналізу |
| `sources.lock.json`, `upstream/` | Закріплені завантаження та контрольні суми; архіви завантажуються локально й ігноруються Git |

Джерела, ліцензії, межі автоматичного витягування та конкретні файли протоколів описані в [PROVENANCE.md](PROVENANCE.md).

## Що вже виконується нативно

`artifacts/device-db/native-device-db.exe` — окремий Windows x64 EXE без Node/.NET/Qt/OpenRGB у runtime. Він читає базу, визначає кандидатів через штатний Windows HID і має два незалежні модулі:

* **Corsair Lighting Node:** Core, Pro, Commander Pro, LS100, 1000D Obsidian, SPEC OMEGA RGB, LT100. Точний VID `1B1C` і сім PID; вибраний канал, RGB-кадри до 204 LED, keepalive кожні 5 секунд.
* **QMK OpenRGB:** протоколи 9/B/C/D, лише після відповіді firmware на handshake та перевірки щільного LED-layout. Наявність VIA не є перевіркою цього протоколу. Revision E та sparse-layout відхиляються. Кольори йдуть у фізичному порядку діодів; зонування клавіш/underglow потребує окремого mapping.

Статуси:

* `protocol-source` — є джерело й витягнуті дані, виконуваного модуля ще немає.
* `native-experimental` — є виконуваний модуль у цьому окремому інструменті, фізично ще не перевірений.
* `hardwareVerified: false` — фізичну перевірку не підмінено збігом ID чи software-тестом.

Існуючі MSI/Gainward залишаються у головній програмі. База не вмикає модулі автоматично й не надсилає довільні USB-пакети з JSON.

## Використання без сервера

```powershell
$tool = '.\artifacts\device-db\native-device-db.exe'
& $tool summary
& $tool find 'Razer'
& $tool protocol 'openrgb:CorsairLightingNodeController'
& $tool match .\device-db\examples\corsair-match.json
& $tool inventory
# Лише побудова пакетів, без відкриття обладнання:
& $tool plan .\device-db\examples\corsair-plan.json
& $tool plan .\device-db\examples\qmk-plan.json
```

`inventory` виконує тільки HID enumeration / читання дескрипторів, без GetFeature/SetFeature чи lighting-команд. Повертає `path` для явного вибору. `signatureMatched` описує збіг витягнутих умов; усі результати лишаються `identificationOnly`.

Для реального запису створи власний JSON із `adapter`, точним `path` із inventory, `colors` та для Corsair `channel` (0 або 1). `apply FILE.json` надсилає один кадр. Необов'язковий `holdMs` підтримує сесію до заданого часу (0–3 600 000 мс). Після закриття Corsair може повернути апаратний ефект, тому постійне керування використовує `stream`.

`probe FILE.json` відкриває вибраний контролер; для QMK надсилає запити версії/кількості LED, не команди кольору. `apply` і `stream` відмовляють при виявленні OpenRGB/SignalRGB/iCUE. Інструмент сам не завершує сторонні процеси й не встановлює драйверів.

`stream` приймає JSON Lines через stdin, одна відповідь JSON на запит:

```json
{"op":"open","adapter":"corsair-lighting-node","path":"EXACT_PATH_FROM_INVENTORY","channel":0}
{"op":"frame","colors":[[64,0,0],[0,64,0],[0,0,64]]}
{"op":"close"}
```

Для QMK `open` має `adapter: "qmk-openrgb"`; ревізію та кількість LED отримуємо з контролера, а не довіряємо JSON. Кадр повинен містити точну отриману кількість LED. Поки stdin відкритий, worker підтримує Corsair keepalive. EOF закриває сесію. Це локальний stdio-процес, TCP-сервера немає.

## Перебудова бази й перевірки

```powershell
# Звіряє SHA-256 перед розпакуванням; можна додати -Offline:
.\device-db\tools\fetch.ps1
node .\device-db\tools\import.mjs
node --test .\device-db\tests\import.test.mjs
.\device-db\tools\build-native.ps1 -Tests
& .\artifacts\device-db\native-device-db-tests.exe
.\device-db\tools\build-native.ps1
.\device-db\tools\package.ps1
```

Node потрібен тільки для розробки/імпорту. Native-збірка використовує вже наявний переносний LLVM-MinGW, вихідні файли й артефакти тримає окремо від головної програми. Скрипт не запускає її build/install і не перезапускає RGB-процеси.

## Підключення до engine

`native/database.hpp` надає `candidates(catalog, device)` і `find(catalog, query)`. `native/windows_hid.hpp` надає `Session(request)`, `info()`, `frame(request)`, `keepalive()`. Усі методи сесії викликаються одним worker або під спільним mutex. `keepalive()` викликається щонайменше раз на 5 секунд для Corsair; для QMK це no-op. Для ізоляції можна використати `stream` замість включення заголовків.

Каталог застосовується для ідентифікації й вибору перевіреного модуля. Передавати йому право автоматично виконувати невідомий плагін не потрібно. Для нового family слід реалізувати initialization/frame/cleanup з відповідного джерела, перевірити транспорт і firmware, додати пакетні fixtures, потім фізично перевірити конкретний контролер. Поле `hardwareVerified` змінюється лише після такої перевірки.

Головна програма використовує включений корпус драйверів для фактично виявлених сумісних виходів. Повний вихідний каталог не означає працездатність кожного запису на будь-якій прошивці; динамічні/непідтримувані протоколи залишаються матеріалом для окремих модулів.

Публічний імпорт використовує тільки OpenRGB. 
ode device-db/tools/import.mjs --research записує повний дослідницький варіант (3042 записи) в ігнорований каталог local/device-db-research; публічні файли не змінюються. Для джерел SignalRGB у snapshot немає загальної ліцензії.

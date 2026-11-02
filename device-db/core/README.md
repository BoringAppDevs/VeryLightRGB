# Повне native-ядро драйверів без SDK-сервера

`artifacts/device-db-core/native-driver-core.exe` зібраний із **1083 C/C++ одиниць**, включно з корпусом контролерів Qt-free OpenRGB fork. У виконуваній збірці є **2144 реєстрації / 1928 різних назв детекторів**. З основною базою точно зіставлено 2130 записів: [providers.json](../providers.json).

Це окремий процес із JSON Lines через stdin/stdout. Він **не запускає OpenRGB SDK**, не приймає SDK-з'єднань і не потребує Qt, Node чи .NET у runtime. Поряд потрібні три включені transport DLL; У версії 1.1.1 вони зібрані з вихідного коду без залежності від окремого MSVC runtime. Установка драйверів, служби, firmware, fan-control та збереження flash не є операціями нашого API. Деякі upstream-контролери можуть мати власні побічні дії під час ініціалізації. Реальна інтеграційна перевірка охоплює Razer Basilisk V3 Pro Wireless; решта корпусу не має загальної hardware-гарантії.

## Запуск

```powershell
$core = '.\artifacts\device-db-core\native-driver-core.exe'
& $core detectors       # Реальні скомпільовані реєстрації, без сканування обладнання
& $core --self-test     # Тільки контролер у пам'яті
& $core stream 'C:\absolute\isolated-driver-data'
```

`stream` приймає запити по одному рядку. Приклад відкриття конкретного HID family:

```json
{"op":"open","detectors":["Corsair Lighting Node Pro"]}
{"op":"devices"}
```

Цей `open` **може відкривати й ініціалізувати обладнання**. Виконується лише явно вибраний детектор; інші вимкнені. Для обмеження конкретним HID endpoint додай `"paths":["exact HIDAPI path"]`. Шляхи можна отримати через окремий `native-device-db inventory`. Відкриття відхиляється, поки працюють VeryLightRGB/OpenRGB/SignalRGB/iCUE чи інший відомий конкурент. Один worker володіє ядром до завершення процесу.

Каталог повертає стабільний для цієї сесії `id`, повну кількість LED, зони й `directCapable`. Кадр надсилається **цілому вибраному контролеру** й має точну кількість кольорів:

```json
{"op":"frame","id":"EXACT_ID_FROM_DEVICES","colors":[[64,0,0],[0,64,0]]}
```

Це приклад для контролера з двома LED. `queued:true` підтверджує передачу до драйверного worker, не фізичний readback. Per-LED frame приймається лише для апаратно заявленого direct-mode. Індекси сервера не використовуються.

Кількість LED зони з ручною довжиною задається явно:

```json
{"op":"resize","id":"EXACT_ID_FROM_DEVICES","zone":0,"ledCount":12}
```

Для I²C/SMBus-детекторів потрібне `"allowBusAccess":true`; для custom/мережевих/серійних детекторів — `"allowCustomDetection":true`. Це поля запиту розробника, а не автоматичні перемикачі. Наявність реєстрації не гарантує доступу до RAM/SMBus без відповідного штатного/upstream драйвера й прав. Системні драйвери цією роботою не встановлювалися.

SDK initialization, загальний `BeginDetection` і глобальні pre-detection hooks не викликаються. API підтримує `zone-frame`, перевірку HID endpoint через `check` і авто-детекцію відповідних HID/display PCI ID через `open-auto`. Основний агент виконує повторне підключення кешованих виходів; пошук нових пристроїв запускає Reload devices. Dynamic user-configured QMK реєстрації й збереження hardware-профілів не підключені. Для QMK є також окремий легкий модуль із handshake у `device-db/native`.

## Джерела й відтворення

Ревізії та SHA-256 — у [sources.lock.json](sources.lock.json). Оригінальні архіви збережені в `device-db/upstream`. MbedTLS 3.2.1 зібрано з вихідного коду тим самим компілятором, без змішування MSVC static runtime з MinGW.

```powershell
.\device-db\tools\fetch.ps1 -Core
node .\device-db\tools\build-core.mjs
node .\device-db\tools\index-core.mjs
node --test .\device-db\tests\core.test.mjs
```

`build-core.mjs` застосовує до окремого cached fork чотири вузькі зміни: friend-доступ для нашого bridge; виправлення upstream `#include .cpp` на `.h`; ініціалізацію atomic frame flags перед стартом thread; окрему конфігураційну директорію через `VLR_DEVICE_DB_CORE_DATA` із процесним TEMP fallback. Windows/MinGW compatibility для Nanoleaf і hueplusplus задана окремими compile options / `mingw_compat.hpp`. Код контролерів у головній програмі не змінюється.

Перевірено: повну компіляцію, JSONL API, відмову для некоректних запитів, кадри/межі на контролері в пам'яті, відсутність TCP connections і Qt DLL. На встановленому агенті 1.1.0 перевірено реальне відкриття Razer, кадри трьох зон, статичну сесію 22 секунди, off/on та відновлення після завершення worker із поверненням налаштувань користувача. Це не є optical readback кольорів миші.

Починаючи з Very Light RGB 1.1.0, worker інтегровано в Native Auto / Offline. Агент запускає його приховано, передає перевірений parent PID, виключає власні MSI/Gainward і використовує `open-auto`, зональні кадри та health-check. Завершення stdin звільняє контролери. Деталі й актуальні hardware-перевірки — у `docs/native-driver-integration-v1.1.md` головного репозиторію.

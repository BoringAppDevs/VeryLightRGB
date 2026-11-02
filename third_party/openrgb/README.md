# OpenRGB 1.1 — Windows x64, SDK 6

Офіційний немодифікований portable-реліз з [OpenRGB Releases](https://openrgb.org/releases.html). Повний commit: `8e16de27858cf2be6af54a33ce003df74cb9fd3e` (release_1.1, 2026-10-16).

Manifests та ліцензії збережені у Git. Архіви в `dist/` завантажує `scripts/setup-openrgb.ps1` із перевіркою SHA-256; вони ігноруються Git:

- `dist/OpenRGB_1.1_Windows_64_8e16de2.zip` — 22 019 605 байтів, офіційні EXE та DLL.
- `dist/OpenRGB-release_1.1-source.tar.gz` — 15 311 823 байти, вихідний код цього релізу із залежностями та build-файлами upstream.
- `LICENSE` — GPL v2, upstream позначає код GPL-2.0-or-later.
- `version.json` — версія, джерела та закріплені SHA-256; суми обчислені з отриманих офіційних архівів, не видаються за цифровий підпис видавця.
- `runtime-sha256.json` — інвентар усіх файлів portable; `setup-openrgb.ps1` перевіряє також розпакований runtime.
- `detector-names-1.1.json` — 1924 назви зареєстрованих детекторів Windows x64 цього релізу, включно з динамічними. Використовуються для обмеженого тестового preset.

`runtime/` розпаковується локально й ігнорується Git. Уже завантажені архіви дозволяють setup працювати без мережі. Відсутні архіви завантажуються лише з URL у manifest; неспівпадіння hash зупиняє setup.

## Запуск

```powershell
.\scripts\setup-openrgb.ps1
# SDK без GUI, наявна конфігурація OpenRGB зберігається:
.\scripts\start-openrgb.ps1
# Налаштування через вікно OpenRGB:
.\scripts\start-openrgb.ps1 -Gui
# Сервер без апаратного сканування для перевірки протоколу:
.\scripts\start-openrgb.ps1 -Detection None -Port 16742 -ConfigDirectory .\local\openrgb-smoke
.\scripts\stop-openrgb.ps1 -ConfigDirectory .\local\openrgb-smoke
```

Початкова папка конфігурації — `%LOCALAPPDATA%\VeryLightRGB\OpenRGB`; це окремо від звичайної інсталяції OpenRGB. Сервер слухає **тільки 127.0.0.1**. Скрипти не встановлюють драйвери, не запитують elevation, не додають OpenRGB в автозапуск. Stop завершує лише процес, записаний власним start-скриптом, із перевіркою PID, EXE-шляху та часу створення.

`-Detection Configured` (початковий режим) зберігає наявні перемикачі детекторів; на порожній конфігурації діють upstream defaults. `All` явно вмикає всі зареєстровані детектори. `None` додає `--nodetect`. `ThisPc` дозволяє лише **MSI Mystic Light X870**, **Gainward GeForce RTX 5080 Phoenix** і **Keychron RGB QMK/ZMK Keyboard**; це тестовий preset цього ПК. X870 — назва USB-детектора FF00/0001, він знайшов і цю B850, це не інша модель плати. Будь-який явний preset замінює попередній вибір детекторів у цій папці.

Перед повним SDK-тестом зупини native RGB-агент. Для hybrid залиш у OpenRGB тільки контролери, якими native не керує, включно з їхніми детекторами. Повний пошук обладнання виконує OpenRGB при власному запуску; кеш клієнта цього не скасовує.

На Windows OpenRGB може повідомляти про недоступні I²C/SMBus через PawnIO. У тесті драйвер / права не змінювалися: MSI USB HID і Gainward через NVIDIA NVAPI працювали, RAM/інші SMBus-контролери не перевірялися. Пакет містить upstream PawnIO-бібліотеку й модулі, але не встановлює системний драйвер.

## Ліцензії та джерела

OpenRGB запускається окремим процесом, не лінкується з Very Light RGB. Його GPL, повідомлення авторства й вихідний архів збережені. Portable також містить **Qt 6.8.3** та інші upstream runtime-компоненти; оригінальні файли не змінені. Тексти LGPL v3 / GPL v3 для Qt збережені в `licenses/`. Джерела / ліцензії Qt: [Qt 6.8.3 archive](https://download.qt.io/archive/qt/6.8/6.8.3/), [Qt licensing](https://doc.qt.io/qt-6/licensing.html). Інші upstream залежності та їх автори перелічені у README та LICENSE всередині архіву вихідного коду OpenRGB.

Типовий компактний ZIP Very Light RGB лишається native-only. `package-native.ps1 -WithOpenRgb` створює окремий великий комплект із цими незміненими архівами, manifest / ліцензіями та скриптами. Він не вмикає OpenRGB автоматично.

Результати реальних перевірок: [docs/openrgb-integration.md](../../docs/openrgb-integration.md).

#include "localization.hpp"
#include <array>
#include <utility>

namespace vlr {
using Translation = std::pair<std::wstring_view, std::wstring_view>;
static constexpr Translation phrases[] = {
    {L"  Native: пряме керування, сервер не потрібен.", L"  Native: direct device control, no server required."},
    {L"Ця довжина визначена пристроєм", L"This length is reported by the device"},
    {L"Пошук native-контролерів без сервера", L"Finding native devices without a server"},
    {L"Червоний", L"Red"},
    {L"Рожево-червоний", L"Rose"},
    {L"Кораловий", L"Coral"},
    {L"Помаранчево-червоний", L"Orange red"},
    {L"Помаранчевий", L"Orange"},
    {L"Бурштиновий", L"Amber"},
    {L"Теплий жовтий", L"Warm yellow"},
    {L"Жовтий", L"Yellow"},
    {L"Лаймовий", L"Lime"},
    {L"Зелений", L"Green"},
    {L"Неоновий зелений", L"Neon green"},
    {L"Нефритовий", L"Jade"},
    {L"М’ятний", L"Mint"},
    {L"Бірюзовий", L"Teal"},
    {L"Ціан", L"Cyan"},
    {L"Небесно-блакитний", L"Sky blue"},
    {L"Лазурний", L"Azure"},
    {L"Кобальтовий", L"Cobalt"},
    {L"Індиго", L"Indigo"},
    {L"Фіалковий", L"Violet"},
    {L"Фіолетовий", L"Purple"},
    {L"Маджента", L"Magenta"},
    {L"Яскравий рожевий", L"Hot pink"},
    {L"Пастельний рожевий", L"Pastel pink"},
    {L"Лавандовий", L"Lavender"},
    {L"Крижаний блакитний", L"Ice blue"},
    {L"Пастельний зелений", L"Pastel green"},
    {L"Персиковий", L"Peach"},
    {L"Теплий білий", L"Warm white"},
    {L"Холодний білий", L"Cool white"},
    {L"Білий", L"White"},
    {L"Чорний", L"Black"},
    {L"    Фон: ", L"    Background: "},
    {L"  · контролер знайдено", L"  · controller found"},
    {L"  · недоступний", L"  · unavailable"},
    {L"  · роз’єм; кабель невідомий", L"  · header; connection unknown"},
    {L"  ↑↓ вибір   Enter / Space   ← → змінити", L"  ↑↓ select   Enter / Space   ← → change"},
    {L"  ↑↓ вибір   Enter відкрити   [Q] Згорнути", L"  ↑↓ select   Enter open   [Q] Hide"},
    {L"  ↑↓ вибір   Enter змінити   [R] Reload devices   [F5] Стан",
     L"  ↑↓ select   Enter change   [R] Reload devices   [F5] Refresh"},
    {L"  Enter: список / значення   ← →: крок яскравості 5%",
     L"  Enter: list / value   ← →: brightness in 5% steps"},
    {L"  Esc назад   [Q] Згорнути", L"  Esc back   [Q] Hide"},
    {L"  SDK: лише 127.0.0.1. Сервер OpenRGB має працювати.",
     L"  SDK: 127.0.0.1 only. Keep the OpenRGB server running."},
    {L"  Space: вибір   I: підсвітити   1: лише цей MSI-хедер",
     L"  Space: select   I: identify   1: only this MSI header"},
    {L"  Гейській · оберт за 12 секунд · яскравість змінюється.", L"  Gay · 12-second cycle · adjustable brightness."},
    {L"  ГОЛОВНЕ МЕНЮ", L"  MAIN MENU"},
    {L"  Загальне світло вимкнено. Увімкни його в «Усе разом».",
     L"  Master power is off. Turn it on in All together."},
    {L"  Зміна параметра створить власний стиль цього виходу.",
     L"  Editing a setting creates a custom style for this output."},
    {L"  Зміни застосовуються до всіх вибраних виходів.", L"  Changes apply to all selected outputs."},
    {L"  ПІДСВІТКА", L"  LIGHTING"},
    {L"  ПРИСТРОЇ · познач лише виходи, які використовуєш", L"  DEVICES · select only the outputs you use"},
    {L"  СИСТЕМА", L"  SYSTEM"},
    {L" · визначено OpenRGB", L" · set by OpenRGB"},
    {L" ]\r\n\r\n  Введи значення. Enter зберегти; Esc назад.\x1b[J",
     L" ]\r\n\r\n  Enter a value. Enter saves; Esc goes back.\x1b[J"},
    {L" МБ    ", L" MiB    "},
    {L" мс", L" ms"},
    {L"[ ] Вимкнено", L"[ ] Off"},
    {L"[ ] Власний колір та ефект", L"[ ] Custom color and effect"},
    {L"[x] Колір та ефект як у «Усе разом»", L"[x] Same color and effect as All together"},
    {L"[x] Після входу у Windows", L"[x] After signing in to Windows"},
    {L"[x] Увімкнено", L"[x] On"},
    {L"\r\n  ↑↓ вибір   Enter підтвердити   Esc назад\x1b[J",
     L"\r\n  ↑↓ select   Enter confirm   Esc back\x1b[J"},
    {L"← Зворотний", L"← Reverse"},
    {L"→ Звичайний", L"→ Forward"},
    {L"○ Вимкнено", L"○ Off"},
    {L"● Увімкнено", L"● On"},
    {L"HEX колір #RRGGBB", L"HEX color #RRGGBB"},
    {L"Native + SDK · без спільних контролерів", L"Native + SDK · separate controllers"},
    {L"Native + SDK · контролери не повинні перетинатися", L"Native + SDK · controllers must not overlap"},
    {L"OpenRGB SDK · потрібен локальний сервер", L"OpenRGB SDK · local server required"},
    {L"SDK порт", L"SDK port"},
    {L"Автозапуск та з’єднання", L"Startup and connection"},
    {L"Автозапуск", L"Start with Windows"},
    {L"Анімація", L"Animation"},
    {L"Ввести свій HEX…", L"Enter a custom HEX…"},
    {L"Вихід", L"Output"},
    {L"Виходи та кількість діодів", L"Outputs and LED counts"},
    {L"Другий колір", L"Secondary color"},
    {L"Ефект", L"Effect"},
    {L"З’єднання", L"Connection"},
    {L"Застосовано до вибраного виходу", L"Applied to the selected output"},
    {L"Застосовано до всіх вибраних виходів", L"Applied to all selected outputs"},
    {L"Збережено", L"Saved"},
    {L"Згорнути в tray", L"Hide to tray"},
    {L"Кадрів / с", L"Frames / second"},
    {L"Керування", L"Backend"},
    {L"Кількість діодів", L"LED count"},
    {L"Колір, ефект, яскравість", L"Color, effect, brightness"},
    {L"Колір", L"Color"},
    {L"Напрямок", L"Direction"},
    {L"Недоступний вихід", L"Unavailable output"},
    {L"Підсвітка продовжить працювати", L"Lighting keeps running"},
    {L"Підсвітка", L"Lighting"},
    {L"Повторити підключення", L"Reconnect"},
    {L"Повторне підключення…", L"Reconnecting…"},
    {L"Початок зони", L"Zone offset"},
    {L"Пристрої", L"Devices"},
    {L"Пробне підсвічування вибраного виходу", L"Identifying the selected output"},
    {L"Різна ширина", L"Mixed widths"},
    {L"Різна яскравість", L"Mixed brightness"},
    {L"Різний час кроку", L"Mixed step times"},
    {L"Різні ефекти", L"Mixed effects"},
    {L"Різні кольори", L"Mixed colors"},
    {L"Світло зони", L"Zone power"},
    {L"Система", L"System"},
    {L"Спільний стиль", L"Shared style"},
    {L"Сталий колір", L"Static color"},
    {L"Темних діодів", L"Dark LEDs"},
    {L"Усе разом", L"All together"},
    {L"Уся підсвітка", L"Master power"},
    {L"Цю довжину налаштовує OpenRGB", L"Set this zone length in OpenRGB"},
    {L"Час кроку", L"Step time"},
    {L"Яскравість · крок 5%", L"Brightness · 5% steps"},
    {L"Яскравість", L"Brightness"},
    {L"Мова", L"Language"},
    {L"English / Українська", L"English / Українська"},
    {L"По черзі · заповнення", L"Sequential fill"},
    {L"Темний блок по колу", L"Orbiting dark block"},
    {L"Дихання", L"Breathing"},
    {L"Гейській", L"Gay"},
    {L"Перехід двох кольорів", L"Two-color fade"},
    {L"Комета", L"Comet"},
    {L"Хвиля", L"Wave"},
    {L"Мерехтіння", L"Twinkle"},
    {L"Невідомий ефект", L"Unknown effect"},
    {L"Відкрити меню…", L"Open menu…"},
    {L"Увімкнути / вимкнути все", L"Toggle all lights"},
    {L"Вийти", L"Exit"},
    {L"Підсвітка працює", L"Lighting is running"},
    {L"Світло вимкнене", L"Lights are off"},
    {L"Вибери виходи у меню «Пристрої»", L"Select outputs in Devices"},
    {L"Деякі виходи недоступні", L"Some outputs are unavailable"},
    {L"Призупинено до пробудження", L"Paused until resume"},
    {L"З’єднання втрачено, повторне підключення", L"Connection lost; reconnecting"},
    {L"Оновлення пристроїв у черзі", L"Device reload queued"},
    {L"Пошук доступних контролерів", L"Finding available controllers"},
    {L"Підключення до OpenRGB SDK", L"Connecting to OpenRGB SDK"},
    {L"Збереження каталогу пристроїв", L"Saving the device catalog"},
    {L"Застосування кольорів та ефектів", L"Applying colors and effects"},
    {L"Перевірка MSI · ARGB-хедери", L"Checking MSI · ARGB headers"},
    {L"Перевірка Gainward · відеокарта", L"Checking Gainward · graphics card"},
    {L"Нативні контролери перевірено", L"Native controllers checked"},
    {L"Налаштування змінилися. Натисни F5 та повтори.", L"Settings changed. Press F5 and try again."},
    {L"Вибрані контролери ще не готові; повторне підключення",
     L"Selected controllers are not ready; reconnecting"},
    {L"Параметри з’єднання змінилися; повторне оновлення", L"Connection settings changed; reloading again"},
    {L"RGB зайняте іншою операцією", L"RGB is busy with another operation"},
    {L"Колір має бути #RRGGBB", L"Color must be #RRGGBB"},
    {L"Некоректний HEX колір", L"Invalid HEX color"},
    {L"Некоректні параметри ефекту", L"Invalid effect settings"},
    {L"Некоректні налаштування системи", L"Invalid system settings"},
    {L"Некоректні налаштування виходу", L"Invalid output settings"},
    {L"Некоректний каталог пристроїв", L"Invalid device catalog"},
    {L"Невідомий нативний вихід", L"Unknown native output"},
    {L"Некоректний опис GPU", L"Invalid GPU descriptor"},
    {L"Невідомий параметр підсвітки", L"Unknown lighting setting"},
    {L"Спочатку вибери виходи в меню «Пристрої»", L"Select outputs in Devices first"},
    {L"Вихід не вибраний або недоступний для цього backend",
     L"Output is not selected or is unavailable for this backend"},
    {L"Пристрій OpenRGB недоступний; Reload devices", L"OpenRGB device unavailable; reload devices"},
    {L"Довжина SDK-зони змінилася; Reload devices", L"SDK zone length changed; reload devices"},
    {L"Немає нативного модуля для цієї материнської плати", L"No native backend for this motherboard"},
    {L"MSI RGB-контролер не знайдено", L"MSI RGB controller not found"},
    {L"NVIDIA NVAPI недоступний", L"NVIDIA NVAPI unavailable"},
    {L"Gainward RTX 5080 Phoenix не знайдено", L"Gainward RTX 5080 Phoenix not found"},
    {L"Конфлікт native / OpenRGB. Вимкни детектори MSI та Gainward в OpenRGB або вибери один backend.",
     L"Native / OpenRGB conflict. Disable MSI and Gainward detectors in OpenRGB or choose one backend."},
};

std::wstring localize(std::string_view language, std::wstring_view text) {
    if (language != "uk")
        for (auto [uk, en] : phrases)
            if (text == uk)
                return std::wstring(en);
    return std::wstring(text);
}

std::wstring localizeMessage(std::string_view language, std::wstring_view text) {
    if (language == "uk")
        return std::wstring(text);
    auto translated = localize(language, text);
    if (translated != text)
        return translated;
    // Dynamic engine/hardware messages have fixed text around numbers or diagnostics.
    static constexpr Translation fragments[] = {
        {L"Помилка оновлення: ", L"Reload failed: "},
        {L"Каталог оновлено · виходів: ", L"Catalog reloaded · outputs: "},
        {L" · деякі виходи недоступні", L" · some outputs are unavailable"},
        {L"OpenRGB · контролер ", L"OpenRGB · controller "},
        {L" з ", L" of "},
        {L"Значення має бути ", L"Value must be "},
        {L": конфігурацію не підтверджено", L": configuration not confirmed"},
        {L"GPU: колір не підтверджено; ", L"GPU: color not confirmed; "},
        {L"Закрий ", L"Close "},
        {L", щоб звільнити RGB", L" to release RGB"},
        {L"SDK від’єднано", L"SDK disconnected"},
    };
    // Long fixed phrases precede shorter fragments, so labels cannot damage messages.
    for (auto [uk, en] : phrases) {
        if (uk.size() < 12)
            continue;
        size_t offset = 0;
        while ((offset = translated.find(uk, offset)) != std::wstring::npos) {
            translated.replace(offset, uk.size(), en);
            offset += en.size();
        }
    }
    for (auto [uk, en] : fragments) {
        size_t offset = 0;
        while ((offset = translated.find(uk, offset)) != std::wstring::npos) {
            translated.replace(offset, uk.size(), en);
            offset += en.size();
        }
    }
    return translated;
}
} // namespace vlr

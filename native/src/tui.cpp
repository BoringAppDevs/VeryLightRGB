#include "tui.hpp"
#include "ipc.hpp"
#include "localization.hpp"
#include <shellapi.h>
#include <psapi.h>
#include <sstream>

namespace vlr {
static void spawn(const std::wstring &arguments, bool console) {
    auto exe = executablePath();
    std::wstring command = L"\"" + exe + L"\" " + arguments;
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    if (!console) {
        start.dwFlags = STARTF_USESHOWWINDOW;
        start.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                        console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW, nullptr,
                        std::filesystem::path(exe).parent_path().c_str(), &start, &process))
        throw std::runtime_error(winError("Launch process"));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void launchMenu(bool smokeTest) {
    spawn((smokeTest ? L"--menu-self-test --data-dir \"" : L"--menu --data-dir \"") +
              dataDirectory().wstring() + L"\"",
          true);
}

void launchBackground() {
    spawn(L"--background --data-dir \"" + dataDirectory().wstring() + L"\"", false);
}

static std::wstring clean(const std::string &text) {
    auto value = wide(text);
    std::erase_if(value, [](wchar_t c) {
        return c < 32 || (c >= 127 && c <= 159) || (c >= 0x202A && c <= 0x202E) ||
               (c >= 0x2066 && c <= 0x2069);
    });
    return value;
}

static std::wstring fit(std::wstring s, size_t count) {
    if (s.size() > count) {
        if (count > 1) {
            s.resize(count - 1);
            s += L"…";
        } else
            s.resize(count);
    }
    if (s.size() < count)
        s.append(count - s.size(), L' ');
    return s;
}

struct Row {
    std::string key;
    std::wstring label, value;
};

static std::wstring foreground(Rgb color) {
    return L"\x1b[38;2;" + std::to_wstring(color[0]) + L";" + std::to_wstring(color[1]) + L";" +
           std::to_wstring(color[2]) + L"m";
}

static std::wstring background(Rgb color) {
    return L"\x1b[48;2;" + std::to_wstring(color[0]) + L";" + std::to_wstring(color[1]) + L";" +
           std::to_wstring(color[2]) + L"m";
}

static const Style &accentStyle(const Config &config) {
    for (auto &desc : config.catalog)
        if (config.allows(desc))
            if (auto device = config.find(desc.id); device && device->managed)
                return device->useGlobal ? config.global : device->style;
    return config.global;
}

Rgb menuAccent(const Config &config) {
    return parseColor(accentStyle(config).color);
}

bool menuRainbowAccent(const Config &config) {
    return accentStyle(config).effect == 4;
}

static std::wstring rainbowText(const std::wstring &text, bool filled) {
    static constexpr std::array<Rgb, 7> colors = {{{255, 55, 95}, {255, 149, 0}, {255, 221, 0},
        {52, 199, 89}, {0, 174, 239}, {83, 108, 255}, {198, 92, 255}}};
    std::wstring out;
    for (size_t i = 0; i < text.size(); i++) {
        double position = double(i) * (colors.size() - 1) / std::max<size_t>(1, text.size() - 1);
        size_t start = std::min(size_t(position), colors.size() - 2);
        double blend = position - double(start);
        Rgb color{};
        for (size_t c = 0; c < 3; c++)
            color[c] = uint8_t(std::lround(colors[start][c] * (1 - blend) + colors[start + 1][c] * blend));
        if (filled) {
            out += background(color) + foreground({16, 18, 22});
        } else out += foreground(color);
        out += text[i];
    }
    return out;
}

struct MenuTheme {
    std::wstring edge, rightEdge, banner, selection;
    bool rainbow = false;

    explicit MenuTheme(Rgb accent, bool rainbowAccent = false) : rainbow(rainbowAccent) {
        if (rainbow) accent = {198, 92, 255};
        bool dark = (int(accent[0]) * 2126 + int(accent[1]) * 7152 + int(accent[2]) * 722) < 900000;
        Rgb panel{};
        for (int channel = 0; channel < 3; channel++)
            panel[size_t(channel)] = dark ? uint8_t(236) : uint8_t(18 + int(accent[size_t(channel)]) / 5);
        edge = foreground(accent) + (dark ? background(panel) : L"");
        rightEdge = edge;
        if (rainbow) {
            edge = foreground({255, 55, 95});
            rightEdge = foreground({198, 92, 255});
        }
        banner =
            L"\x1b[0m\x1b[1m" + background(accent) + foreground(dark ? Rgb{245, 247, 250} : Rgb{16, 18, 22});
        selection = L"\x1b[0m" + background(panel) + foreground(dark ? Rgb{20, 22, 26} : Rgb{245, 247, 250});
    }

    std::wstring heading(const std::wstring &text) const {
        return rainbow ? rainbowText(text, false) : edge + text;
    }
};

static std::array<const wchar_t *, 5> titleGlyph(wchar_t letter, bool compact) {
    if (compact) {
        switch (letter) {
        case L'V':
            return {L"█ █", L"█ █", L" █ ", L"", L""};
        case L'E':
            return {L"███", L"██ ", L"███", L"", L""};
        case L'R':
            return {L"██ ", L"███", L"█ █", L"", L""};
        case L'Y':
            return {L"█ █", L" █ ", L" █ ", L"", L""};
        case L'L':
            return {L"█  ", L"█  ", L"███", L"", L""};
        case L'I':
            return {L"███", L" █ ", L"███", L"", L""};
        case L'G':
            return {L" ██", L"█ █", L"███", L"", L""};
        case L'H':
            return {L"█ █", L"███", L"█ █", L"", L""};
        case L'T':
            return {L"███", L" █ ", L" █ ", L"", L""};
        case L'B':
            return {L"██ ", L"███", L"██ ", L"", L""};
        default:
            return {L" ", L" ", L" ", L"", L""};
        }
    }
    switch (letter) {
    case L'V':
        return {L"█   █", L"█   █", L"█   █", L" █ █ ", L"  █  "};
    case L'E':
        return {L"█████", L"█    ", L"████ ", L"█    ", L"█████"};
    case L'R':
        return {L"████ ", L"█   █", L"████ ", L"█  █ ", L"█   █"};
    case L'Y':
        return {L"█   █", L" █ █ ", L"  █  ", L"  █  ", L"  █  "};
    case L'L':
        return {L"█    ", L"█    ", L"█    ", L"█    ", L"█████"};
    case L'I':
        return {L"███", L" █ ", L" █ ", L" █ ", L"███"};
    case L'G':
        return {L" ████", L"█    ", L"█ ███", L"█   █", L" ████"};
    case L'H':
        return {L"█   █", L"█   █", L"█████", L"█   █", L"█   █"};
    case L'T':
        return {L"█████", L"  █  ", L"  █  ", L"  █  ", L"  █  "};
    case L'B':
        return {L"████ ", L"█   █", L"████ ", L"█   █", L"████ "};
    default:
        return {L" ", L" ", L" ", L" ", L" "};
    }
}

static std::vector<std::wstring> titleBanner(size_t width) {
    bool compact = width < 100;
    bool thick = width >= 67;
    size_t gap = width >= 78 ? 2 : 1;
    std::vector<std::wstring> lines(compact ? 3 : 5);
    for (wchar_t letter : std::wstring(L"VERY LIGHT RGB")) {
        auto glyph = titleGlyph(letter, compact);
        for (size_t row = 0; row < lines.size(); row++) {
            if (letter == L' ') {
                lines[row] += L"  ";
                continue;
            }
            std::wstring stroke(glyph[row]);
            auto bold = stroke;
            if (thick) {
                bold.assign(stroke.size() + 1, L' ');
                for (size_t column = 0; column < stroke.size(); column++)
                    if (stroke[column] == L'█')
                        bold[column] = bold[column + 1] = L'█';
            }
            lines[row] += bold + std::wstring(gap, L' ');
        }
    }
    for (auto &line : lines) {
        line.resize(line.size() - gap);
        line.insert(0, (width - line.size()) / 2, L' ');
    }
    return lines;
}

void MenuNavigation::navigate(int destination, std::string output) {
    if (page == destination && scope == output)
        return;
    if (history.size() >= 32)
        history.erase(history.begin() + 1);
    history.push_back({page, selected, scope});
    page = destination;
    scope = std::move(output);
    selected = 0;
}

bool MenuNavigation::back() {
    if (history.empty()) {
        bool changed = page != 3 || !scope.empty();
        page = 3;
        selected = 0;
        scope.clear();
        return changed;
    }
    auto previous = std::move(history.back());
    history.pop_back();
    page = previous.page;
    selected = previous.selected;
    scope = std::move(previous.scope);
    return true;
}

std::wstring colorMenuText(const std::wstring &text, bool selected, const std::wstring &selectedStyle,
                           size_t width) {
    constexpr std::wstring_view swatch = L"   ████████ ";
    auto colorAt = [](std::wstring_view value, size_t at) -> std::optional<Rgb> {
        if (value[at] != L'#' || at + 7 > value.size() || (at + 7 < value.size() && iswxdigit(value[at + 7])))
            return {};
        try {
            return parseColor(utf8(std::wstring(value.substr(at, 7))));
        } catch (const std::exception &) {
            return {};
        }
    };
    // Reserve visible cells for the preview before fitting, so ANSI and swatches
    // cannot shift the frame border or split a color block at the right edge.
    std::wstring decorated;
    for (size_t at = 0; at < text.size(); at++) {
        if (colorAt(text, at)) {
            decorated += text.substr(at, 7);
            if (!width || decorated.size() + swatch.size() + (at + 7 < text.size() ? 1 : 0) <= width)
                decorated += swatch;
            at += 6;
        } else
            decorated += text[at];
    }
    if (width)
        decorated = fit(std::move(decorated), width);
    std::wstring out;
    std::wstring normal =
        selected ? (selectedStyle.empty() ? L"\x1b[0m\x1b[48;2;35;72;65m\x1b[97m" : selectedStyle)
                 : L"\x1b[0m";
    for (size_t i = 0; i < decorated.size(); i++) {
        if (auto color = colorAt(decorated, i)) {
            auto rgb = *color;
            // Dark HEX text needs a light chip to remain readable, including #000000.
            if (*std::max_element(rgb.begin(), rgb.end()) < 80)
                out += L"\x1b[48;2;224;228;235m";
            out += L"\x1b[38;2;" + std::to_wstring(rgb[0]) + L";" + std::to_wstring(rgb[1]) + L";" +
                   std::to_wstring(rgb[2]) + L"m" + decorated.substr(i, 7) + normal;
            i += 6;
            if (decorated.compare(i + 1, swatch.size(), swatch) == 0) {
                out += L"   " + background(rgb) + L"        " + normal + L" ";
                i += swatch.size();
            }
            continue;
        }
        out += decorated[i];
    }
    return out;
}

std::wstring brightnessBar(int percent) {
    percent = std::clamp(percent, 0, 100);
    int filled = percent / 5;
    return L"[" + std::wstring(size_t(filled), L'█') + std::wstring(size_t(20 - filled), L'░') + L"] " +
           std::to_wstring(percent) + L" %";
}

int stepBrightness(int percent, int direction) {
    percent = std::clamp(percent, 0, 100);
    return std::clamp(direction < 0 ? ((percent + 4) / 5 - 1) * 5 : (percent / 5 + 1) * 5, 0, 100);
}

bool menuReloadAvailable(int page) {
    return page == 1 || page == 2;
}

struct Model : MenuNavigation {
    Json state;
    Config config;
    std::wstring notice;

    explicit Model(const Json &value) : state(value), config(Config::from(value.at("Config"))) {}

    std::wstring tr(const std::wstring &text) const {
        return localize(config.language, text);
    }

    Device *device() {
        if (scope.empty())
            return nullptr;
        for (auto &d : config.devices)
            if (d.id == scope)
                return &d;
        return nullptr;
    }

    const Descriptor *descriptor() const {
        for (auto &d : config.catalog)
            if (d.id == scope)
                return &d;
        return nullptr;
    }

    std::vector<const Style *> activeStyles() const {
        std::vector<const Style *> result;
        for (auto &desc : config.catalog)
            if (config.allows(desc))
                if (auto d = config.find(desc.id); d && d->managed)
                    result.push_back(d->useGlobal ? &config.global : &d->style);
        return result;
    }

    Style style() {
        auto d = device();
        if (d) {
            auto value = d->useGlobal ? config.global : d->style;
            if (d->useGlobal)
                value.enabled = true;
            return value;
        }
        auto styles = activeStyles();
        auto value = styles.empty() ? config.global : *styles.front();
        value.enabled = config.global.enabled;
        return value;
    }

    template <class T> bool mixed(T Style::*field) const {
        if (!scope.empty())
            return false;
        auto styles = activeStyles();
        return !styles.empty() && std::any_of(styles.begin(), styles.end(),
                                              [&](auto s) { return s->*field != styles.front()->*field; });
    }

    template <class F> bool anyEffect(F predicate) {
        if (!scope.empty())
            return predicate(style().effect);
        auto styles = activeStyles();
        return styles.empty()
                   ? predicate(config.global.effect)
                   : std::any_of(styles.begin(), styles.end(), [&](auto s) { return predicate(s->effect); });
    }

    std::vector<Row> rows() {
        std::vector<Row> r;
        if (page == 3) {
            return {{"page-light", tr(L"Підсвітка"), tr(L"Колір, ефект, яскравість")},
                    {"page-devices", tr(L"Пристрої"), tr(L"Виходи та кількість діодів")},
                    {"page-system", tr(L"Система"), tr(L"Автозапуск та з’єднання")},
                    {"language", tr(L"Мова"), config.language == "uk" ? L"Українська" : L"English"},
                    {"hide", tr(L"Згорнути в tray"), tr(L"Підсвітка продовжить працювати")}};
        }
        if (page == 1) {
            for (auto &desc : config.catalog) {
                auto d = config.find(desc.id);
                std::wstring suffix = desc.id.rfind("native:msi:", 0) == 0
                                          ? tr(L"  · роз’єм; кабель невідомий")
                                      : desc.present ? tr(L"  · контролер знайдено")
                                                     : tr(L"  · недоступний");
                r.push_back(
                    {desc.id, L"", (d && d->managed ? L"[x] " : L"[ ] ") + clean(desc.name) + suffix});
            }
            return r;
        }
        if (page == 2) {
            const std::wstring backend[] = {L"Native · Auto / Offline", L"OpenRGB SDK",
                                            tr(L"Native + SDK · без спільних контролерів")};
            r.push_back({"backend", tr(L"Керування"), backend[config.backend]});
            if(config.backend!=0) r.push_back({"port", tr(L"SDK порт"), std::to_wstring(config.port)});
            r.push_back({"fps", tr(L"Кадрів / с"), std::to_wstring(config.fps)});
            r.push_back({"startup", tr(L"Автозапуск"),
                         state.value("StartupEnabled", false) ? tr(L"[x] Після входу у Windows")
                                                              : tr(L"[ ] Вимкнено")});
            r.push_back({"reload", tr(L"Пристрої"), L"Reload devices"});
            r.push_back({"reconnect", tr(L"З’єднання"), tr(L"Повторити підключення")});
            return r;
        }
        auto d = device();
        auto desc = descriptor();
        auto s = style();
        r.push_back({"scope", tr(L"Вихід"),
                     scope.empty() ? tr(L"Усе разом")
                     : desc        ? clean(desc->name)
                                   : tr(L"Недоступний вихід")});
        if (d)
            r.push_back({"link", tr(L"Спільний стиль"),
                         d->useGlobal ? tr(L"[x] Колір та ефект як у «Усе разом»")
                                      : tr(L"[ ] Власний колір та ефект")});
        r.push_back({"power", d ? tr(L"Світло зони") : tr(L"Уся підсвітка"),
                     s.enabled ? tr(L"[x] Увімкнено") : tr(L"[ ] Вимкнено")});
        r.push_back(
            {"effect", tr(L"Ефект"),
             (mixed(&Style::effect) ? tr(L"Різні ефекти") : tr(clean(effectName(s.effect)))) + L"  ▾"});
        if (anyEffect([](int effect) { return effect != 4; })) {
            r.push_back({"color", tr(L"Колір"),
                         (mixed(&Style::color) ? tr(L"Різні кольори") : wide(s.color)) + L"  ▾"});
            if (anyEffect([](int effect) { return effect == 5 || effect == 7; }))
                r.push_back({"secondary", tr(L"Другий колір"),
                             (mixed(&Style::secondary) ? tr(L"Різні кольори") : wide(s.secondary)) + L"  ▾"});
            if (anyEffect([](int effect) { return effect != 0 && effect != 4; }))
                r.push_back(
                    {"speed", tr(L"Час кроку"),
                     mixed(&Style::step) ? tr(L"Різний час кроку") : std::to_wstring(s.step) + tr(L" мс")});
            if (anyEffect([](int effect) { return effect == 2; }))
                r.push_back({"width", tr(L"Темних діодів"),
                             mixed(&Style::darkWidth) ? tr(L"Різна ширина") : std::to_wstring(s.darkWidth)});
        }
        r.push_back({"brightness", tr(L"Яскравість"),
                     (mixed(&Style::brightness) ? tr(L"Різна яскравість") : brightnessBar(s.brightness)) +
                         L"  ▾"});
        if (!desc || !desc->single)
            r.push_back({"leds", tr(L"Кількість діодів"),
                         std::to_wstring(desc ? (desc->configurable ? d->leds : desc->count) : config.leds) +
                             (desc && !desc->configurable ? (desc->core()?L" · native":tr(L" · визначено OpenRGB")) : L"")});
        if (desc && !desc->single) {
            r.push_back({"reverse", tr(L"Напрямок"), d->reverse ? tr(L"← Зворотний") : tr(L"→ Звичайний")});
            r.push_back({"offset", tr(L"Початок зони"), std::to_wstring(d->offset)});
        }
        return r;
    }

    std::wstring render(size_t columns = 80) {
        size_t width = std::clamp<size_t>(columns > 4 ? columns - 4 : 58, 58, 112);
        MenuTheme theme(menuAccent(config), menuRainbowAccent(config));
        std::wstring out = L"\x1b[?25l\x1b[H";
        auto border = [&](const wchar_t *left, const wchar_t *right) {
            const auto text = std::wstring(left) + std::wstring(width, L'─') + right;
            out += theme.rainbow ? rainbowText(text, false) : theme.edge + text;
            out += L"\x1b[0m\r\n";
        };
        auto line = [&](const std::wstring &text, bool active = false, bool accent = false) {
            out += theme.edge + L"│\x1b[0m";
            out += active ? theme.selection : L"\x1b[0m";
            if (accent && theme.rainbow)
                out += L"\x1b[1m" + rainbowText(fit(text, width), true);
            else {
                if (accent) out += theme.banner;
                out += colorMenuText(text, active, theme.selection, width);
            }
            out += L"\x1b[0m" + theme.rightEdge + L"│\x1b[0m\r\n";
        };
        border(L"┌", L"┐");
        for (auto &row : titleBanner(width))
            line(row, false, true);
        line(L"  VERY LIGHT RGB                                      v" + wide(Version), false, true);
        double memory = state.contains("Process") ? state["Process"].value("WorkingSetMB", 0.0) : 0;
        std::wstringstream stats;
        stats << L"  " << (config.global.enabled ? tr(L"● Увімкнено") : tr(L"○ Вимкнено")) << tr(L"    Фон: ")
              << std::fixed << std::setprecision(1) << memory << tr(L" МБ    ")
              << (state.value("Animating", false) ? tr(L"Анімація") : tr(L"Сталий колір"));
        line(stats.str());
        border(L"├", L"┤");
        line(page == 0   ? tr(L"  ПІДСВІТКА")
             : page == 1 ? tr(L"  ПРИСТРОЇ · познач лише виходи, які використовуєш")
             : page == 2 ? tr(L"  СИСТЕМА")
                         : tr(L"  ГОЛОВНЕ МЕНЮ"));
        auto fields = rows();
        int index = 0;
        for (auto &r : fields) {
            line((index == selected ? L" › " : L"   ") +
                     (r.label.empty() ? r.value : fit(r.label, 20) + L"  " + r.value),
                 index == selected);
            index++;
        }
        line(L"");
        if (page == 0 && !config.global.enabled)
            line(tr(L"  Загальне світло вимкнено. Увімкни його в «Усе разом»."));
        else if (page == 0 && device() && device()->useGlobal)
            line(tr(L"  Зміна параметра створить власний стиль цього виходу."));
        else if (page == 0 && !anyEffect([](int effect) { return effect != 4; }))
            line(tr(L"  Гейській · оберт за 12 секунд · яскравість змінюється."));
        else if (page == 0 && scope.empty())
            line(tr(L"  Зміни застосовуються до всіх вибраних виходів."));
        else if (page == 1)
            line(tr(L"  Space: вибір   I: підсвітити   1: лише цей MSI-хедер"));
        else if (page == 2)
            line(tr(config.backend==0?L"  Native: пряме керування, сервер не потрібен.":L"  SDK: лише 127.0.0.1. Сервер OpenRGB має працювати."));
        else if (page != 3)
            line(tr(L"  Enter: список / значення   ← →: крок яскравості 5%"));
        if (menuReloadAvailable(page) && state.contains("Reload")) {
            const auto &reload = state["Reload"];
            auto status = reload.value("State", std::string("idle"));
            if (status != "idle") {
                bool busy = status == "queued" || status == "running";
                std::wstring prefix = busy ? L"↻ " : status == "done" ? L"✓ " : L"! ";
                int total = reload.value("Total", 0), complete = reload.value("Completed", 0);
                if (busy && total > 0) {
                    int cells = std::clamp(complete * 12 / total, 0, 12);
                    prefix += L"[" + std::wstring(size_t(cells), L'█') +
                              std::wstring(size_t(12 - cells), L'░') + L"] ";
                }
                line(L"  " + prefix +
                     localizeMessage(config.language, clean(reload.value("Stage", std::string()))));
            }
        }
        line(L"  " + (notice.empty()
                          ? localizeMessage(config.language, clean(state.value("Message", std::string())))
                          : notice));
        border(L"├", L"┤");
        if (page == 3)
            line(tr(L"  ↑↓ вибір   Enter відкрити   [Q] Згорнути"));
        else {
            line(menuReloadAvailable(page)
                     ? tr(L"  ↑↓ вибір   Enter змінити   [R] Reload devices   [F5] Стан")
                     : tr(L"  ↑↓ вибір   Enter / Space   ← → змінити"));
            line(tr(L"  Esc назад   [Q] Згорнути"));
        }
        border(L"└", L"┘");
        out += L"\x1b[J";
        return out;
    }
};

std::wstring menuSnapshot(const Json &state, int scope, int page) {
    Model model(state);
    model.page = page;
    if (scope > 0) {
        int i = 0;
        for (auto &d : model.config.catalog)
            if (auto c = model.config.find(d.id); c && c->managed) {
                if (++i == scope) {
                    model.scope = d.id;
                    break;
                }
            }
    }
    return model.render();
}

class Console {
    HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    HICON smallIcon = nullptr, largeIcon = nullptr;
    DWORD oldInput = 0, oldOutput = 0;
    MenuTheme theme{Rgb{100, 230, 198}};
    std::string language = "en";

  public:
    void setLanguage(const std::string &value) {
        language = value;
    }

    std::wstring tr(const std::wstring &text) const {
        return localize(language, text);
    }

    void setAccent(const Config &config) {
        theme = MenuTheme(menuAccent(config), menuRainbowAccent(config));
    }

    Console() {
        if (!GetConsoleWindow() && !AllocConsole())
            throw std::runtime_error(winError("Open console"));
        input = GetStdHandle(STD_INPUT_HANDLE);
        output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!GetConsoleMode(input, &oldInput) || !GetConsoleMode(output, &oldOutput))
            throw std::runtime_error("Console handles unavailable");
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleMode(input, (oldInput | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS) &
                                  ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE |
                                    ENABLE_MOUSE_INPUT));
        SetConsoleMode(output, oldOutput | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
        SetConsoleTitleW(L"Very Light RGB · menu");
        // Classic console windows accept these icons. Terminal hosts may use their own tab icon.
        auto module = GetModuleHandleW(nullptr);
        smallIcon =
            static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        largeIcon =
            static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
        if (auto window = GetConsoleWindow()) {
            if (smallIcon)
                SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
            if (largeIcon)
                SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        }
        write(L"\x1b[?1049h\x1b[2J\x1b[?25l");
    }

    ~Console() {
        write(L"\x1b[0m\x1b[?25h\x1b[?1049l");
        if (input != INVALID_HANDLE_VALUE)
            SetConsoleMode(input, oldInput);
        if (output != INVALID_HANDLE_VALUE)
            SetConsoleMode(output, oldOutput);
        FreeConsole();
        if (smallIcon)
            DestroyIcon(smallIcon);
        if (largeIcon)
            DestroyIcon(largeIcon);
    }

    size_t width() const {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(output, &info))
            return size_t(info.srWindow.Right - info.srWindow.Left + 1);
        return 80;
    }

    void verifyAndCapture(const wchar_t *report = L"menu-self-test.json") {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(output, &info))
            throw std::runtime_error("Console verification failed");
        Json rows = Json::array();
        Json hexSamples = Json::array();
        bool title = false;
        for (SHORT y = 0; y < std::min<SHORT>(info.dwSize.Y, 30); y++) {
            std::wstring row(size_t(info.dwSize.X), L' ');
            DWORD n = 0;
            COORD start{0, y};
            if (!ReadConsoleOutputCharacterW(output, row.data(), DWORD(row.size()), start, &n))
                throw std::runtime_error("Console buffer readback failed");
            row.resize(n);
            while (!row.empty() && row.back() == L' ')
                row.pop_back();
            if (row.find(L"VERY LIGHT RGB") != std::wstring::npos)
                title = true;
            for (size_t x = 0; x + 7 <= row.size(); x++)
                if (row[x] == L'#') {
                    try {
                        auto value = row.substr(x, 7);
                        (void)parseColor(utf8(value));
                        WORD attribute = 0;
                        DWORD read = 0;
                        COORD position{SHORT(x), y};
                        if (ReadConsoleOutputAttribute(output, &attribute, 1, position, &read) && read) {
                            WORD cells[8]{};
                            COORD swatchPosition{SHORT(x + 10), y};
                            bool swatch =
                                x + 19 <= row.size() && row.substr(x + 7, 3) == L"   " &&
                                row.substr(x + 10, 8) == L"        " && row[x + 18] == L' ' &&
                                ReadConsoleOutputAttribute(output, cells, 8, swatchPosition, &read) &&
                                read == 8 && std::all_of(std::begin(cells), std::end(cells), [&](WORD cell) {
                                    return (cell & 0xF0) == ((attribute & 0x0F) << 4);
                                });
                            hexSamples.push_back({{"Hex", utf8(value)},
                                                  {"Attributes", attribute},
                                                  {"SwatchVerified", swatch}});
                        }
                    } catch (const std::exception &) {
                    }
                }
            rows.push_back(utf8(row));
        }
        if (!title)
            throw std::runtime_error("Terminal did not render the menu title");
        DWORD host = 0;
        GetWindowThreadProcessId(GetConsoleWindow(), &host);
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        WORD bannerAttributes = 0;
        DWORD attributesRead = 0;
        ReadConsoleOutputAttribute(output, &bannerAttributes, 1, COORD{1, 1}, &attributesRead);
        std::set<WORD> bannerBackgrounds;
        const auto bannerWidth = std::clamp<int>(int(width()) - 4, 58, 112);
        for (int i = 0; i < 12; i++) {
            WORD cell = 0; DWORD count = 0;
            if (ReadConsoleOutputAttribute(output, &cell, 1, COORD{SHORT(1 + i * (bannerWidth - 1) / 11), 1}, &count) && count)
                bannerBackgrounds.insert(cell & 0xF0);
        }
        if (std::wstring(report) == L"menu-rainbow-self-test.json" && bannerBackgrounds.size() < 4)
            throw std::runtime_error("Terminal did not render the rainbow accent");
        writeAtomic(dataDirectory() / report,
                    Json({{"TitleVerified", title},
                          {"ProcessId", GetCurrentProcessId()},
                          {"ConsoleWindowOwnerProcessId", host},
                          {"WorkingSetMB", double(memory.WorkingSetSize) / 1048576},
                          {"Columns", width()},
                          {"BannerAttributes", attributesRead ? bannerAttributes : 0},
                          {"BannerBackgrounds", bannerBackgrounds},
                          {"HexSamples", hexSamples},
                          {"Rows", rows}})
                        .dump(2));
    }

    void write(const std::wstring &value) {
        DWORD n = 0;
        WriteConsoleW(output, value.data(), DWORD(value.size()), &n, nullptr);
    }

    KEY_EVENT_RECORD key(DWORD timeout = INFINITE) {
        for (;;) {
            DWORD wait = WaitForSingleObject(input, timeout);
            if (wait == WAIT_TIMEOUT)
                return {};
            if (wait != WAIT_OBJECT_0)
                throw std::runtime_error("Console closed");
            INPUT_RECORD record{};
            DWORD n = 0;
            if (!ReadConsoleInputW(input, &record, 1, &n))
                throw std::runtime_error("Console closed");
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
                return record.Event.KeyEvent;
            if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                KEY_EVENT_RECORD key{};
                key.wVirtualKeyCode = VK_F5;
                return key;
            }
        }
    }

    int choose(const std::wstring &title, const std::vector<std::wstring> &values, int selected = 0) {
        if (values.empty())
            return -1;
        selected = std::clamp(selected, 0, int(values.size()) - 1);
        int offset = std::max(0, selected - 12);
        for (;;) {
            std::wstring text = L"\x1b[H\x1b[2J" + theme.heading(L"  " + title) + L"\x1b[0m\r\n\r\n";
            if (selected < offset)
                offset = selected;
            if (selected >= offset + 18)
                offset = selected - 17;
            auto columns = width();
            size_t itemWidth = std::min<size_t>(74, columns > 5 ? columns - 5 : 1);
            for (int i = offset; i < std::min(int(values.size()), offset + 18); i++) {
                text += i == selected ? theme.selection + L" › " : L"   ";
                text += colorMenuText(values[size_t(i)], i == selected, theme.selection, itemWidth) +
                        L"\x1b[0m\r\n";
            }
            if (values.size() > 18)
                text += L"\r\n  PgUp / PgDn   Home / End     " + std::to_wstring(selected + 1) + L" / " +
                        std::to_wstring(values.size());
            text += tr(L"\r\n  ↑↓ вибір   Enter підтвердити   Esc назад\x1b[J");
            write(text);
            auto k = key();
            if (k.wVirtualKeyCode == VK_ESCAPE)
                return -1;
            if (k.wVirtualKeyCode == VK_RETURN)
                return selected;
            if (k.wVirtualKeyCode == VK_UP)
                selected = (selected + int(values.size()) - 1) % int(values.size());
            if (k.wVirtualKeyCode == VK_DOWN)
                selected = (selected + 1) % int(values.size());
            if (k.wVirtualKeyCode == VK_PRIOR)
                selected = std::max(0, selected - 18);
            if (k.wVirtualKeyCode == VK_NEXT)
                selected = std::min(int(values.size()) - 1, selected + 18);
            if (k.wVirtualKeyCode == VK_HOME)
                selected = 0;
            if (k.wVirtualKeyCode == VK_END)
                selected = int(values.size()) - 1;
        }
    }

    std::optional<std::wstring> prompt(const std::wstring &title, std::wstring value) {
        bool fresh = true;
        for (;;) {
            write(L"\x1b[H\x1b[2J" + theme.heading(L"  " + title) + L"\x1b[0m\r\n\r\n  [ " +
                  colorMenuText(value) + tr(L" ]\r\n\r\n  Введи значення. Enter зберегти; Esc назад.\x1b[J"));
            auto k = key();
            if (k.wVirtualKeyCode == VK_ESCAPE)
                return std::nullopt;
            if (k.wVirtualKeyCode == VK_RETURN)
                return value;
            if (k.wVirtualKeyCode == VK_BACK) {
                if (fresh)
                    value.clear();
                else if (!value.empty())
                    value.pop_back();
                fresh = false;
            } else if (k.uChar.UnicodeChar >= 32 && k.uChar.UnicodeChar != 127) {
                if (fresh) {
                    value.clear();
                    fresh = false;
                }
                if (value.size() < 64)
                    value += k.uChar.UnicodeChar;
            }
        }
    }
};

struct ColorPreset {
    std::string_view hex;
    std::wstring_view name;
};

static constexpr std::array<ColorPreset, 32> palette = {{{"#FF0000", L"Червоний"},
                                                         {"#FF375F", L"Рожево-червоний"},
                                                         {"#FF8040", L"Кораловий"},
                                                         {"#FF4500", L"Помаранчево-червоний"},
                                                         {"#FF9500", L"Помаранчевий"},
                                                         {"#FFBF00", L"Бурштиновий"},
                                                         {"#FFE092", L"Теплий жовтий"},
                                                         {"#FFFF00", L"Жовтий"},
                                                         {"#BFFF00", L"Лаймовий"},
                                                         {"#34C759", L"Зелений"},
                                                         {"#00FF00", L"Неоновий зелений"},
                                                         {"#00A86B", L"Нефритовий"},
                                                         {"#64E6C6", L"М’ятний"},
                                                         {"#00BFA5", L"Бірюзовий"},
                                                         {"#00FFFF", L"Ціан"},
                                                         {"#27C8FF", L"Небесно-блакитний"},
                                                         {"#0080FF", L"Лазурний"},
                                                         {"#0047AB", L"Кобальтовий"},
                                                         {"#536CFF", L"Індиго"},
                                                         {"#8B5CF6", L"Фіалковий"},
                                                         {"#C65CFF", L"Фіолетовий"},
                                                         {"#FF00FF", L"Маджента"},
                                                         {"#FF69B4", L"Яскравий рожевий"},
                                                         {"#FFB7CE", L"Пастельний рожевий"},
                                                         {"#D8B4FE", L"Лавандовий"},
                                                         {"#BFDBFE", L"Крижаний блакитний"},
                                                         {"#BBF7D0", L"Пастельний зелений"},
                                                         {"#FFDAB9", L"Персиковий"},
                                                         {"#FFF4D6", L"Теплий білий"},
                                                         {"#EAF4FF", L"Холодний білий"},
                                                         {"#FFFFFF", L"Білий"},
                                                         {"#000000", L"Чорний"}}};

static void refresh(Model &model) {
    auto state = rpc({{"Command", "status"}});
    model.state = state;
    model.config = Config::from(state["Config"]);
    if (!model.scope.empty()) {
        auto d = model.config.find(model.scope);
        if (!d || !d->managed)
            model.scope.clear();
    }
    model.selected = std::clamp(model.selected, 0, std::max(0, int(model.rows().size()) - 1));
}

static void commit(Model &model) {
    auto state = rpc(
        {{"Command", "set-config"}, {"Config", model.config.json()}, {"Revision", model.state["Revision"]}});
    model.state = state;
    model.config = Config::from(state["Config"]);
    model.notice = model.tr(L"Збережено");
}

static void reloadDevices(Model &model) {
    rpc({{"Command", "reload-devices"}});
    refresh(model);
    model.notice.clear();
}

static void change(Model &model, Console &console, const Row &row, int direction, bool enter) {
    auto &c = model.config;
    auto tr = [&](const std::wstring &text) { return model.tr(text); };
    auto d = model.device();
    auto desc = model.descriptor();
    auto s = model.style();
    if (row.key == "language") {
        int selected = c.language == "uk" ? 1 : 0;
        int value =
            enter ? console.choose(L"Language / Мова", {L"English", L"Українська"}, selected) : 1 - selected;
        if (value < 0)
            return;
        c.language = value == 1 ? "uk" : "en";
        commit(model);
        console.setLanguage(c.language);
        return;
    }
    if (row.key == "page-light" || row.key == "page-devices" || row.key == "page-system") {
        model.navigate(row.key == "page-light" ? 0 : row.key == "page-devices" ? 1 : 2);
        model.notice.clear();
        return;
    }
    if (row.key == "scope") {
        std::vector<std::wstring> names = {tr(L"Усе разом")};
        std::vector<std::string> ids = {""};
        for (auto &item : c.catalog)
            if (auto dc = c.find(item.id); dc && dc->managed && c.allows(item)) {
                names.push_back(clean(item.name));
                ids.push_back(item.id);
            }
        auto found = std::find(ids.begin(), ids.end(), model.scope);
        int selected = found == ids.end() ? 0 : int(found - ids.begin());
        int n = enter ? console.choose(tr(L"Вихід"), names, selected)
                      : (selected + direction + int(ids.size())) % int(ids.size());
        if (n >= 0)
            model.navigate(0, ids[size_t(n)]);
        return;
    }
    if (row.key == "link") {
        if (d) {
            if (d->useGlobal && !d->hasCustom) {
                d->style = c.global;
                d->hasCustom = true;
            }
            d->useGlobal = !d->useGlobal;
            commit(model);
        }
        return;
    }
    if (row.key == "startup") {
        bool enabled = !model.state.value("StartupEnabled", false);
        rpc({{"Command", "startup"}, {"Enabled", enabled}});
        refresh(model);
        return;
    }
    if (row.key == "reload") {
        reloadDevices(model);
        return;
    }
    if (row.key == "reconnect") {
        rpc({{"Command", "reconnect"}});
        model.notice = model.tr(L"Повторне підключення…");
        return;
    }
    auto number = [&](int &value, int min, int max, int step) {
        if (enter) {
            auto text = console.prompt(row.label, std::to_wstring(value));
            if (!text)
                return false;
            size_t used = 0;
            int result = std::stoi(*text, &used);
            if (used != text->size() || result < min || result > max)
                throw std::runtime_error("Значення має бути " + std::to_string(min) + "–" +
                                         std::to_string(max));
            value = result;
        } else
            value = std::clamp(value + direction * step, min, max);
        return true;
    };
    if (row.key == "backend") {
        int value =
            enter
                ? console.choose(tr(L"Керування"),
                                 {L"Native · Auto / Offline", tr(L"OpenRGB SDK · потрібен локальний сервер"),
                                  tr(L"Native + SDK · контролери не повинні перетинатися")},
                                 c.backend)
                : (c.backend + direction + 3) % 3;
        if (value < 0)
            return;
        c.backend = value;
        commit(model);
        return;
    }
    if (row.key == "port") {
        if (number(c.port, 1, 65535, 1))
            commit(model);
        return;
    }
    if (row.key == "fps") {
        if (number(c.fps, 10, 60, 5))
            commit(model);
        return;
    }
    if (row.key == "leds") {
        if (desc && !desc->configurable) {
            model.notice = model.tr(desc->core()?L"Ця довжина визначена пристроєм":L"Цю довжину налаштовує OpenRGB");
            return;
        }
        int value = d ? d->leds : c.leds;
        if (!number(value, 1, desc ? desc->maxLeds : 120, 1))
            return;
        if (d) {
            d->leds = value;
            d->offset %= value;
        } else {
            c.leds = value;
            for (auto &item : c.catalog)
                if (item.configurable) {
                    auto &dc = c.device(item);
                    if (dc.managed && c.allows(item)) {
                        dc.leds = std::min(value, item.maxLeds);
                        dc.offset %= dc.leds;
                    }
                }
        }
        commit(model);
        return;
    }
    if (row.key == "reverse") {
        if (d) {
            d->reverse = !d->reverse;
            commit(model);
        }
        return;
    }
    if (row.key == "offset") {
        if (d) {
            if (number(d->offset, 0, (desc && desc->configurable ? d->leds : desc ? desc->count : 1) - 1, 1))
                commit(model);
        }
        return;
    }
    if (row.key == "power")
        s.enabled = !s.enabled;
    else if (row.key == "effect") {
        std::vector<std::wstring> effects;
        for (int effect = 0; effect < EffectCount; effect++)
            effects.push_back(tr(clean(effectName(effect))));
        int value = enter ? console.choose(tr(L"Ефект"), effects, s.effect)
                          : (s.effect + direction + EffectCount) % EffectCount;
        if (value < 0)
            return;
        s.effect = value;
    } else if (row.key == "color" || row.key == "secondary") {
        auto &value = row.key == "color" ? s.color : s.secondary;
        if (enter) {
            std::vector<std::wstring> colors;
            for (auto &preset : palette)
                colors.push_back(wide(std::string(preset.hex)) + L"  " + tr(std::wstring(preset.name)));
            colors.push_back(tr(L"Ввести свій HEX…"));
            auto found =
                std::find_if(palette.begin(), palette.end(), [&](auto &p) { return p.hex == value; });
            int selected = found == palette.end() ? int(palette.size()) : int(found - palette.begin());
            for (;;) {
                selected = console.choose(tr(L"Колір"), colors, selected);
                if (selected < 0)
                    return;
                if (selected == int(palette.size())) {
                    auto text = console.prompt(tr(L"HEX колір #RRGGBB"), wide(value));
                    if (!text)
                        continue;
                    value = hexColor(parseColor(utf8(*text)));
                } else
                    value = palette[size_t(selected)].hex;
                break;
            }
        } else {
            auto it = std::find_if(palette.begin(), palette.end(), [&](auto &p) { return p.hex == value; });
            int index = it == palette.end() ? 0 : int(it - palette.begin());
            value = palette[size_t((index + direction + int(palette.size())) % int(palette.size()))].hex;
        }
    } else if (row.key == "brightness") {
        if (enter) {
            std::vector<std::wstring> levels;
            for (int percent = 0; percent <= 100; percent += 5)
                levels.push_back(brightnessBar(percent));
            int selected = console.choose(tr(L"Яскравість · крок 5%"), levels, (s.brightness + 2) / 5);
            if (selected < 0)
                return;
            s.brightness = selected * 5;
        } else
            s.brightness = stepBrightness(s.brightness, direction);
    } else if (row.key == "speed") {
        if (!number(s.step, 100, 1000, 20))
            return;
    } else if (row.key == "width") {
        if (!number(s.darkWidth, 1, 120, 1))
            return;
    } else
        return;
    auto state = rpc({{"Command", "edit-lighting"},
                      {"Scope", model.scope},
                      {"Field", row.key},
                      {"Style", s.json()},
                      {"Revision", model.state["Revision"]}});
    model.state = state;
    model.config = Config::from(state["Config"]);
    model.notice = model.scope.empty() ? tr(L"Застосовано до всіх вибраних виходів")
                                       : tr(L"Застосовано до вибраного виходу");
}

int runMenu(bool smokeTest) {
    auto name = L"Local\\VeryLightRGB.Menu-" + userSid();
    Handle singleton(CreateMutexW(nullptr, FALSE, name.c_str()));
    if (!singleton)
        throw std::runtime_error(winError("Menu mutex"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        auto window = FindWindowW(nullptr, L"Very Light RGB · menu");
        if (window) {
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        }
        return 0;
    }
    Console console;
    Json state;
    try {
        state = rpc({{"Command", "status"}}, 500);
    } catch (...) {
        launchBackground();
        for (int i = 0; i < 20; i++) {
            console.write(L"\x1b[H\x1b[2J  VERY LIGHT RGB\r\n\r\n  Starting the background process…");
            try {
                state = rpc({{"Command", "status"}}, 500);
                break;
            } catch (...) {
                Sleep(100);
            }
        }
        if (state.empty())
            throw std::runtime_error("Background process is not responding. Check light.log.");
    }
    Model model(state);
    bool done = false;
    DWORD host = 0;
    GetWindowThreadProcessId(GetConsoleWindow(), &host);
    log("TUI opened; pid=" + std::to_string(GetCurrentProcessId()) + "; consoleHost=" + std::to_string(host) +
        ".");
    while (!done) {
        auto fields = model.rows();
        model.selected = std::clamp(model.selected, 0, std::max(0, int(fields.size()) - 1));
        console.setAccent(model.config);
        console.setLanguage(model.config.language);
        console.write(model.render(console.width()));
        if (smokeTest) {
            console.verifyAndCapture();
            // Render a local preview without changing saved settings or hardware effects.
            auto rainbowState = model.state;
            auto previewConfig = model.config;
            previewConfig.global.effect = 4;
            for (auto &device : previewConfig.devices) device.style.effect = 4;
            rainbowState["Config"] = previewConfig.json();
            Model rainbowPreview(rainbowState);
            console.write(rainbowPreview.render(console.width()));
            console.verifyAndCapture(L"menu-rainbow-self-test.json");
            // Exercise actual color output without sending RGB commands or console input.
            std::wstring preview = L"\x1b[H\x1b[2J  VERY LIGHT RGB · " + model.tr(L"Колір") + L"\r\n\r\n";
            size_t cellWidth = std::min<size_t>(42, console.width() > 5 ? (console.width() - 5) / 2 : 1);
            for (size_t i = 0; i < palette.size(); i++) {
                preview += colorMenuText(wide(std::string(palette[i].hex)) + L"  " +
                                             model.tr(std::wstring(palette[i].name)),
                                         false, {}, cellWidth);
                preview += i % 2 ? L"\r\n" : L"   ";
            }
            console.write(preview);
            console.verifyAndCapture(L"menu-colors-self-test.json");
            Sleep(3000);
            break;
        }
        auto reload = model.state.value("Reload", Json::object()).value("State", std::string());
        auto key = console.key(
            menuReloadAvailable(model.page) && (reload == "running" || reload == "queued") ? 250 : 1000);
        try {
            auto vk = key.wVirtualKeyCode;
            if (!vk) {
                refresh(model);
                continue;
            }
            if (vk == VK_ESCAPE) {
                model.back();
                refresh(model);
                model.notice.clear();
                continue;
            }
            if (vk == L'Q') {
                done = true;
                continue;
            }
            if (vk == VK_UP) {
                model.selected = std::max(0, model.selected - 1);
                continue;
            }
            if (vk == VK_DOWN) {
                model.selected = std::min(std::max(0, int(fields.size()) - 1), model.selected + 1);
                continue;
            }
            if (vk == VK_F5) {
                refresh(model);
                model.notice.clear();
                continue;
            }
            // Virtual-key shortcuts also work with the Ukrainian keyboard layout.
            WORD c = key.wVirtualKeyCode;
            if (c == L'L' || c == L'D' || c == L'S') {
                model.navigate(c == L'L' ? 0 : c == L'D' ? 1 : 2);
                refresh(model);
                model.notice.clear();
                continue;
            }
            if (c == L'R' && menuReloadAvailable(model.page)) {
                reloadDevices(model);
                continue;
            }
            if (model.page == 1 && !fields.empty()) {
                auto id = fields[size_t(model.selected)].key;
                if (c == L'I') {
                    rpc({{"Command", "identify"}, {"Id", id}});
                    model.notice = model.tr(L"Пробне підсвічування вибраного виходу");
                } else if ((c == L'1' || c == VK_NUMPAD1) && id.rfind("native:msi:argb:", 0) == 0) {
                    for (auto &d : model.config.devices)
                        if (d.id.rfind("native:msi:argb:", 0) == 0)
                            d.managed = d.id == id;
                    commit(model);
                } else if (vk == VK_SPACE || vk == VK_RETURN) {
                    for (auto &d : model.config.devices)
                        if (d.id == id)
                            d.managed = !d.managed;
                    commit(model);
                }
                continue;
            }
            if (!fields.empty() && (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RETURN || vk == VK_SPACE)) {
                if (fields[size_t(model.selected)].key == "hide")
                    done = vk == VK_RETURN || vk == VK_SPACE;
                else
                    change(model, console, fields[size_t(model.selected)], vk == VK_LEFT ? -1 : 1,
                           vk == VK_RETURN);
            }
        } catch (const std::exception &ex) {
            model.notice = localizeMessage(model.config.language, clean(ex.what()));
            try {
                refresh(model);
            } catch (...) {
            }
        }
    }
    log("TUI closed; lighting agent remains active.");
    return 0;
}
} // namespace vlr

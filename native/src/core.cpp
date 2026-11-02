#include "core.hpp"
#include <shlobj.h>
#include <sddl.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace vlr {
static std::filesystem::path dataPath;
static std::mutex logMutex;

std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!size)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), size);
    return out;
}

std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0,
                                   nullptr, nullptr);
    if (!size)
        throw std::runtime_error("Invalid Unicode");
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

std::string winError(const std::string &operation, DWORD code) {
    return operation + " (Windows " + std::to_string(code) + ")";
}

std::wstring executablePath() {
    std::wstring value(32768, 0);
    DWORD n = GetModuleFileNameW(nullptr, value.data(), DWORD(value.size()));
    if (!n || n >= value.size())
        throw std::runtime_error(winError("Executable path"));
    value.resize(n);
    return value;
}

std::wstring absolutePath(const std::wstring &path) {
    if (path.empty())
        throw std::runtime_error("Empty path");
    std::wstring full(32768, 0);
    DWORD n = GetFullPathNameW(path.c_str(), DWORD(full.size()), full.data(), nullptr);
    if (!n || n >= full.size())
        throw std::runtime_error("Invalid absolute path");
    full.resize(n);
    return full;
}

std::wstring localAppData() {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
    if (FAILED(hr) || !path)
        throw std::runtime_error("Local AppData is unavailable");
    std::wstring result(path);
    CoTaskMemFree(path);
    return absolutePath(result);
}

std::wstring userSid() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h))
        throw std::runtime_error(winError("User token"));
    DWORD n = 0;
    GetTokenInformation(token.h, TokenUser, nullptr, 0, &n);
    Bytes data(n);
    if (!GetTokenInformation(token.h, TokenUser, data.data(), n, &n))
        throw std::runtime_error(winError("User identity"));
    LPWSTR value = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &value))
        throw std::runtime_error(winError("User SID"));
    std::wstring result(value);
    LocalFree(value);
    return result;
}

void setDataDirectory(const std::wstring &value) {
    dataPath = absolutePath(value);
}

std::filesystem::path dataDirectory() {
    if (dataPath.empty())
        dataPath = std::filesystem::path(localAppData()) / L"VeryLightRGB";
    return dataPath;
}

std::string readFile(const std::filesystem::path &path, size_t limit) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        throw std::runtime_error(winError("Read " + utf8(path.wstring())));
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.h, &size) || size.QuadPart < 0 || uint64_t(size.QuadPart) > limit)
        throw std::runtime_error("File exceeds size limit");
    std::string text(size_t(size.QuadPart), '\0');
    DWORD read = 0;
    if (!text.empty() &&
        (!ReadFile(file.h, text.data(), DWORD(text.size()), &read, nullptr) || read != text.size()))
        throw std::runtime_error(winError("Read file"));
    if (text.rfind("\xEF\xBB\xBF", 0) == 0)
        text.erase(0, 3);
    return text;
}

void writeAtomic(const std::filesystem::path &path, const std::string &text) {
    if (text.size() > MaxJson)
        throw std::runtime_error("Configuration exceeds size limit");
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += L".tmp";
    Handle file(
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        throw std::runtime_error(winError("Write temporary file"));
    DWORD n = 0;
    if (!WriteFile(file.h, text.data(), DWORD(text.size()), &n, nullptr) || n != text.size() ||
        !FlushFileBuffers(file.h))
        throw std::runtime_error(winError("Write file"));
    file.reset();
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(winError("Commit configuration"));
}

Json parseJson(const std::string &text) {
    if (text.size() > MaxJson)
        throw std::runtime_error("JSON exceeds size limit");
    return Json::parse(text, [](int depth, Json::parse_event_t, Json &) {
        if (depth > 32)
            throw std::runtime_error("JSON nesting limit exceeded");
        return true;
    });
}

void log(const std::string &message) noexcept {
    try {
        std::lock_guard lock(logMutex);
        auto path = dataDirectory() / L"light.log";
        std::filesystem::create_directories(path.parent_path());
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 512 * 1024) {
            auto old = path;
            old += L".old";
            MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond);
        std::ofstream out(path, std::ios::app | std::ios::binary);
        out << prefix << message << "\r\n";
    } catch (...) {
    }
}

std::string hash(const std::string &value) {
    std::array<uint8_t, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<char *>(value.data())), ULONG(value.size()),
                   digest.data(), ULONG(digest.size())) < 0)
        throw std::runtime_error("SHA256 failed");
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(64);
    for (auto b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

Rgb parseColor(const std::string &s) {
    if (s.size() != 7 || s[0] != '#')
        throw std::runtime_error("Колір має бути #RRGGBB");
    Rgb result{};
    for (int i = 0; i < 6; i++) {
        char c = s[size_t(i) + 1];
        int v = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                       : -1;
        if (v < 0)
            throw std::runtime_error("Некоректний HEX колір");
        result[i / 2] = uint8_t(result[i / 2] * 16 + v);
    }
    return result;
}

std::string hexColor(Rgb c) {
    char text[8];
    snprintf(text, sizeof(text), "#%02X%02X%02X", c[0], c[1], c[2]);
    return text;
}

std::string effectName(int e) {
    static const char *names[] = {"Сталий колір", "По черзі · заповнення", "Темний блок по колу", "Дихання",
                                  "Гейській", "Перехід двох кольорів", "Комета", "Хвиля", "Мерехтіння"};
    return e >= 0 && e < EffectCount ? names[e] : "Невідомий ефект";
}

void Style::validate() const {
    parseColor(color);
    parseColor(secondary);
    if (brightness < 0 || brightness > 100 || effect < 0 || effect >= EffectCount || step < 100 || step > 1000 ||
        darkWidth < 1 || darkWidth > 120)
        throw std::runtime_error("Некоректні параметри ефекту");
}

Rgb Style::output() const {
    auto c = parseColor(color);
    for (auto &b : c)
        b = enabled ? uint8_t(int(b) * brightness / 100) : 0;
    return c;
}

Json Style::json() const {
    return {{"Color", color},    {"SecondaryColor", secondary}, {"Brightness", brightness},
            {"Effect", effect},  {"StepMilliseconds", step},    {"DarkWidth", darkWidth},
            {"Enabled", enabled}};
}

Style Style::from(const Json &j) {
    if (!j.is_object())
        throw std::runtime_error("Style must be an object");
    Style s;
    s.color = j.value("Color", s.color);
    s.secondary = j.value("SecondaryColor", s.secondary);
    s.brightness = j.value("Brightness", s.brightness);
    s.effect = j.value("Effect", s.effect);
    s.step = j.value("StepMilliseconds", s.step);
    s.darkWidth = j.value("DarkWidth", s.darkWidth);
    s.enabled = j.value("Enabled", s.enabled);
    s.validate();
    return s;
}

Json Descriptor::json() const {
    Json value = {{"Id", id},
            {"Name", name},
            {"ControllerKey", controllerKey},
            {"LedCount", count},
            {"MaxLeds", maxLeds},
            {"ZoneIndex", zone},
            {"ControllerIndex", controller},
            {"ControllerType", controllerType},
            {"ConfigurableLength", configurable},
            {"SingleZone", single},
            {"Present", present}};
    if(core()) value["Driver"] = driver;
    return value;
}

Descriptor Descriptor::from(const Json &j) {
    Descriptor d;
    d.id = j.at("Id").get<std::string>();
    d.name = j.at("Name").get<std::string>();
    d.controllerKey = j.value("ControllerKey", std::string());
    d.count = j.value("LedCount", 12);
    d.maxLeds = j.value("MaxLeds", 120);
    d.zone = j.value("ZoneIndex", 0);
    d.controller = j.value("ControllerIndex", uint32_t(0));
    d.controllerType = j.value("ControllerType", -1);
    d.configurable = j.value("ConfigurableLength", true);
    d.single = j.value("SingleZone", false);
    d.present = j.value("Present", true);
    d.driver = j.value("Driver", Json::object());
    return d;
}

Descriptor headerDescriptor(int h) {
    if (h < 0 || h > 2)
        throw std::runtime_error("Unknown MSI header");
    Descriptor d;
    d.id = "native:msi:argb:" + std::to_string(h);
    d.name = "MSI · ARGB " + std::to_string(h + 1);
    d.zone = h;
    return d;
}

Descriptor gpuDescriptor() {
    Descriptor d;
    d.id = "native:gainward:5080:phoenix";
    d.name = "Gainward RTX 5080 Phoenix";
    d.count = d.maxLeds = 1;
    d.configurable = false;
    d.single = true;
    return d;
}

Json Device::json() const {
    return {{"Id", id},         {"Managed", managed}, {"UseGlobal", useGlobal}, {"HasCustomStyle", hasCustom},
            {"LedCount", leds}, {"Reverse", reverse}, {"Offset", offset},       {"Style", style.json()}};
}

Device Device::from(const Json &j) {
    Device d;
    d.id = j.at("Id").get<std::string>();
    d.managed = j.value("Managed", true);
    d.useGlobal = j.value("UseGlobal", true);
    d.hasCustom = j.value("HasCustomStyle", false);
    d.leds = j.value("LedCount", 12);
    d.reverse = j.value("Reverse", false);
    d.offset = j.value("Offset", 0);
    d.style = Style::from(j.value("Style", Json::object()));
    return d;
}

Device &Config::device(const Descriptor &d) {
    for (auto &v : devices)
        if (v.id == d.id)
            return v;
    Device v;
    v.id = d.id;
    v.managed = d.id.rfind("native:msi:argb:", 0) != 0 && (!d.core() || d.driver.value("PhysicalLeds",0)>0);
    v.leds = d.configurable ? std::min(leds, d.maxLeds) : d.count;
    v.style = global;
    devices.push_back(v);
    return devices.back();
}

const Device *Config::find(const std::string &id) const {
    for (auto &d : devices)
        if (d.id == id)
            return &d;
    return nullptr;
}

bool Config::allows(const Descriptor &d) const {
    return (backend == 2 && (d.native() || d.sdk())) || (backend == 0 && (d.native() || d.core())) || (backend == 1 && d.sdk());
}

void Config::validate() const {
    global.validate();
    if (language != "en" && language != "uk")
        throw std::runtime_error("Unsupported interface language");
    if (leds < 1 || leds > 120 || fps < 10 || fps > 60 || port < 1 || port > 65535 || backend < 0 ||
        backend > 2 || devices.size() > 256 || catalog.size() > 256)
        throw std::runtime_error("Некоректні налаштування системи");
    std::set<std::string> ids;
    for (auto &d : devices) {
        if (d.id.empty() || d.id.size() > 256 || !ids.insert(d.id).second || d.leds < 1 || d.leds > 4096 ||
            d.offset < 0 || d.offset > 4095)
            throw std::runtime_error("Некоректні налаштування виходу");
        d.style.validate();
    }
    ids.clear();
    for (auto &d : catalog) {
        if ((!d.native() && !d.sdk() && !d.core()) || d.id.size() > 256 || d.name.empty() || d.name.size() > 512 ||
            !ids.insert(d.id).second || d.count < 1 || d.count > 4096 || d.maxLeds < d.count ||
            d.maxLeds > 4096 || d.zone < 0 || d.zone > 255)
            throw std::runtime_error("Некоректний каталог пристроїв");
        if(d.core() && (!d.driver.is_object() || !d.driver.contains("Id") || !d.driver.contains("Detector") ||
           !d.driver["Id"].is_string() || !d.driver["Detector"].is_string() || d.driver["Id"].get<std::string>().size()>2048 ||
           d.driver["Detector"].get<std::string>().size()>512 || d.driver.value("Path",std::string()).size()>2048 ||
           d.controllerKey != hash(d.driver["Id"].get<std::string>()))) throw std::runtime_error("Некоректний опис native-драйвера");
        if (d.native() && d.id != gpuDescriptor().id) {
            if (d.zone > 2 || d.id != headerDescriptor(d.zone).id || d.maxLeds != 120)
                throw std::runtime_error("Невідомий нативний вихід");
        }
        if (d.id == gpuDescriptor().id && (d.count != 1 || d.configurable || !d.single))
            throw std::runtime_error("Некоректний опис GPU");
    }
}

Json Config::json() const {
    auto j = global.json();
    j["SchemaVersion"] = 3;
    j["LedCount"] = leds;
    j["FramesPerSecond"] = fps;
    j["OpenRgbPort"] = port;
    j["Backend"] = backend;
    j["Language"] = language;
    j["Devices"] = Json::array();
    j["Catalog"] = Json::array();
    for (auto &d : devices)
        j["Devices"].push_back(d.json());
    for (auto &d : catalog)
        j["Catalog"].push_back(d.json());
    return j;
}

Config Config::from(const Json &j) {
    if (!j.is_object())
        throw std::runtime_error("Configuration must be an object");
    int schema = j.value("SchemaVersion", 2);
    if (schema != 2 && schema != 3)
        throw std::runtime_error("Unsupported configuration version");
    Config c;
    c.global = Style::from(j);
    c.leds = j.value("LedCount", 12);
    c.fps = j.value("FramesPerSecond", 30);
    c.port = j.value("OpenRgbPort", 6742);
    c.backend = j.value("Backend", 0);
    c.language = j.value("Language", std::string("en"));
    auto devices = j.value("Devices", Json::array()), catalog = j.value("Catalog", Json::array());
    if (!devices.is_array() || !catalog.is_array() || devices.size() > 256 || catalog.size() > 256)
        throw std::runtime_error("Invalid device collections");
    for (auto &d : devices)
        c.devices.push_back(Device::from(d));
    for (auto &d : catalog)
        c.catalog.push_back(Descriptor::from(d));
    c.validate();
    return c;
}

Config Config::load() {
    auto path = dataDirectory() / L"settings.json";
    if (std::filesystem::exists(path))
        return from(parseJson(readFile(path)));
    auto legacy = std::filesystem::path(localAppData()) / L"RgbTray" / L"settings.json";
    if (std::filesystem::exists(legacy)) {
        auto c = from(parseJson(readFile(legacy)));
        c.save();
        log("Migrated settings from RGB Tray; original file retained.");
        return c;
    }
    return {};
}

void Config::save() const {
    validate();
    writeAtomic(dataDirectory() / L"settings.json", json().dump(2));
}

void editLighting(Config &config, const std::string &scope, const std::string &field, const Style &value) {
    value.validate();
    auto assign = [&](Style &target) {
        if (field == "color")
            target.color = value.color;
        else if (field == "secondary")
            target.secondary = value.secondary;
        else if (field == "brightness")
            target.brightness = value.brightness;
        else if (field == "effect")
            target.effect = value.effect;
        else if (field == "speed")
            target.step = value.step;
        else if (field == "width")
            target.darkWidth = value.darkWidth;
        else if (field == "power")
            target.enabled = value.enabled;
        else
            throw std::runtime_error("Невідомий параметр підсвітки");
    };
    auto selected = [&](const Device &device) {
        return device.managed && std::any_of(config.catalog.begin(), config.catalog.end(),
                                             [&](auto &d) { return d.id == device.id && config.allows(d); });
    };
    if (scope.empty()) {
        if (field != "power" && std::none_of(config.devices.begin(), config.devices.end(), selected))
            throw std::runtime_error("Спочатку вибери виходи в меню «Пристрої»");
        assign(config.global);
        // "All together" is a command to all selected outputs, including independent styles.
        // Broadcast only the edited field; retain their other parameters and link state.
        if (field != "power")
            for (auto &device : config.devices)
                if (selected(device) && !device.useGlobal) {
                    assign(device.style);
                    device.hasCustom = true;
                }
    } else {
        auto found = std::find_if(config.devices.begin(), config.devices.end(),
                                  [&](auto &d) { return d.id == scope && selected(d); });
        if (found == config.devices.end())
            throw std::runtime_error("Вихід не вибраний або недоступний для цього backend");
        auto style = found->useGlobal ? config.global : found->style;
        if (found->useGlobal)
            style.enabled = true; // Master-off is separate from the zone's own power setting.
        assign(style);
        found->style = style;
        found->useGlobal = false;
        found->hasCustom = true;
    }
    config.validate();
}

bool Plan::animated() const {
    return style.enabled && style.brightness > 0 && style.effect != 0 &&
           !(device.single && style.effect == 2);
}

std::vector<Plan> plans(const Config &c) {
    c.validate();
    std::vector<Plan> out;
    for (auto &desc : c.catalog) {
        auto d = c.find(desc.id);
        if (!c.allows(desc) || !d || !d->managed)
            continue;
        Plan p;
        p.device = desc;
        p.style = d->useGlobal ? c.global : d->style;
        if (!c.global.enabled)
            p.style.enabled = false;
        p.count = desc.configurable ? std::clamp(d->leds, 1, desc.maxLeds) : desc.count;
        p.reverse = d->reverse;
        p.offset = d->offset % p.count;
        p.virtualCount = c.leds;
        out.push_back(p);
    }
    return out;
}

Renderer::Renderer(Plan p) : plan(std::move(p)) {
    plan.style.validate();
    if (plan.count < 1 || plan.count > 4096 || plan.virtualCount < 1 || plan.virtualCount > 120 ||
        plan.offset < 0 || plan.offset >= plan.count)
        throw std::runtime_error("Invalid render plan");
    primary = plan.style.output();
    auto other = plan.style;
    other.color = other.secondary;
    secondary = other.output();
    work.resize(size_t(std::max(plan.count, plan.virtualCount)) * 3);
    frame.resize(size_t(plan.count) * 3);
}

static Rgb mix(Rgb a, Rgb b, double t) {
    Rgb out{};
    for (int i = 0; i < 3; i++)
        out[i] = uint8_t(std::nearbyint(a[i] + (double(b[i]) - a[i]) * t));
    return out;
}

const Bytes &Renderer::render(double ms) {
    auto &s = plan.style;
    int n = plan.count;
    ms = std::max(0.0, ms);
    auto put = [&](int led, Rgb c) { std::copy(c.begin(), c.end(), work.begin() + size_t(led) * 3); };
    if (!s.enabled || s.brightness == 0)
        std::fill(work.begin(), work.end(), 0);
    else if (s.effect == 1) {
        int count = plan.device.single ? plan.virtualCount : n;
        double phase = std::fmod(ms, 550.0 + 2.0 * count * s.step), drain = 200.0 + count * s.step + 350;
        bool draining = phase >= drain;
        double progress = std::max(0.0, (phase - (draining ? drain : 200)) / s.step), total = 0;
        for (int i = 0; i < count; i++) {
            double x = std::clamp(progress - i, 0.0, 1.0), level = x * x * (3 - 2 * x);
            if (draining)
                level = 1 - level;
            total += level;
            Rgb c{};
            for (int k = 0; k < 3; k++)
                c[k] = uint8_t(count > 120 ? std::nearbyint(primary[k] * level)
                                           : std::lround(primary[k] * level));
            put(i, c);
        }
        if (plan.device.single) {
            Rgb c{};
            double level = total / count;
            for (int k = 0; k < 3; k++)
                c[k] = uint8_t(std::lround(primary[k] * level));
            put(0, c);
        }
    } else if (s.effect == 2 && !plan.device.single) {
        double pos = std::fmod(ms, double(n) * s.step) / s.step;
        int start = int(pos), width = std::min(s.darkWidth, n);
        double f = pos - start, blend = f * f * (3 - 2 * f);
        Rgb edge{};
        for (int k = 0; k < 3; k++)
            edge[k] = uint8_t(n > 120 ? std::nearbyint(primary[k] * blend) : std::lround(primary[k] * blend));
        for (int i = 0; i < n; i++) {
            bool a = (i - start + n) % n < width, b = (i - start - 1 + 2 * n) % n < width;
            Rgb c{};
            for (int k = 0; k < 3; k++)
                c[k] = a == b ? (a ? 0 : primary[k]) : a ? edge[k] : uint8_t(primary[k] - edge[k]);
            put(i, c);
        }
    } else if (s.effect == 3 || s.effect == 5) {
        double t = (1 - std::cos(2 * 3.14159265358979323846 * ms / (s.step * 20.0))) / 2;
        auto c = s.effect == 3 ? mix({}, primary, t) : mix(primary, secondary, t);
        for (int i = 0; i < n; i++)
            put(i, c);
    } else if (s.effect == 4) {
        for (int i = 0; i < n; i++) {
            double h = std::fmod(ms / 12000.0 + double(i) / n, 1.0) * 6,
                   x = 1 - std::abs(std::fmod(h, 2) - 1);
            std::array<double, 3> c{};
            switch (int(h)) {
            case 0:
                c = {1, x, 0};
                break;
            case 1:
                c = {x, 1, 0};
                break;
            case 2:
                c = {0, 1, x};
                break;
            case 3:
                c = {0, x, 1};
                break;
            case 4:
                c = {x, 0, 1};
                break;
            default:
                c = {1, 0, x};
            }
            const double level = 255.0 * s.brightness / 100.0;
            put(i, {uint8_t(std::nearbyint(c[0] * level)), uint8_t(std::nearbyint(c[1] * level)),
                    uint8_t(std::nearbyint(c[2] * level))});
        }
    } else if (s.effect == 6) {
        // A bright head with a smooth trailing tail. Single-zone devices pulse.
        const int count = plan.device.single ? plan.virtualCount : n;
        const double pos = std::fmod(ms / s.step, double(count));
        const double tail = std::max(1.0, std::min(5.0, count / 2.0));
        for (int i = 0; i < n; i++) {
            const double distance = std::fmod(pos - i + count, double(count));
            const double level = std::max(0.0, 1.0 - distance / tail);
            put(i, mix({}, primary, level * level));
        }
    } else if (s.effect == 7) {
        // A moving, continuous two-color wave; also meaningful on a single LED.
        for (int i = 0; i < n; i++) {
            const double phase = ms / (s.step * 20.0) - double(i) / n;
            const double level = (1.0 - std::cos(2.0 * 3.14159265358979323846 * phase)) / 2.0;
            put(i, mix(primary, secondary, level));
        }
    } else if (s.effect == 8) {
        // Stable per-LED phases give soft independent sparkles without random state.
        for (int i = 0; i < n; i++) {
            const double offset = double((uint32_t(i) * 2654435761u) & 0xffffu) / 65536.0;
            const double phase = ms / (s.step * 20.0) + offset;
            const double pulse = (1.0 + std::cos(2.0 * 3.14159265358979323846 * phase)) / 2.0;
            put(i, mix({}, primary, std::pow(pulse, 8.0)));
        }
    } else
        for (int i = 0; i < n; i++)
            put(i, primary);
    for (int i = 0; i < n; i++) {
        int source = ((plan.reverse ? n - 1 - i : i) + plan.offset) % n;
        std::copy_n(work.begin() + size_t(source) * 3, 3, frame.begin() + size_t(i) * 3);
    }
    return frame;
}

Bytes zoneConfig(const Bytes &current, int header, Rgb color, int directCount) {
    if (current.size() != 290 || current[0] != 0x50 || header < 0 || header > 2 || directCount < 0 ||
        directCount > 120)
        throw std::runtime_error("Invalid MSI configuration");
    int o = 1 + header * 16;
    if (current[o] > 10)
        throw std::runtime_error("Unknown MSI mode; write refused");
    auto result = current;
    result[o] = directCount ? 9 : 2;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++)
            result[o + 1 + i * 3 + k] = directCount ? 0 : color[k];
    if (directCount) {
        result[o + 1] = result[o + 5] = result[o + 9] = result[o + 10] = result[o + 11] = result[o + 12] =
            255;
        result[o + 15] = uint8_t(directCount);
    }
    result[o + 13] = directCount ? 3 : 1;
    result[o + 14] = directCount ? 0x15 : 0x35;
    result.back() = 0;
    return result;
}

Bytes framePacket(int header, std::span<const uint8_t> rgb) {
    if (header < 0 || header > 2 || rgb.empty() || rgb.size() > 360 || rgb.size() % 3)
        throw std::runtime_error("Invalid MSI frame");
    Bytes out(727);
    out[0] = 0x51;
    out[1] = 9;
    out[2] = 4;
    out[3] = uint8_t(header);
    out[6] = uint8_t(rgb.size() / 3);
    std::copy(rgb.begin(), rgb.end(), out.begin() + 7);
    return out;
}
} // namespace vlr

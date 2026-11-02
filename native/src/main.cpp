#include "core.hpp"
#include "engine.hpp"
#include "ipc.hpp"
#include "startup.hpp"
#include "tui.hpp"
#include "localization.hpp"
#include <shellapi.h>
#include <psapi.h>
#include <dbt.h>
#include <hidsdi.h>

namespace vlr {
constexpr UINT TrayMessage = WM_APP + 1;

struct Application {
    Engine *engine = nullptr;
    HWND window = nullptr;
    NOTIFYICONDATAW icon{};
    HICON image = nullptr;
    UINT taskbarMessage = 0;
    HDEVNOTIFY devices = nullptr;
    int exitCode = 0;

    void tray(bool add) {
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = 1;
        icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        icon.uCallbackMessage = TrayMessage;
        icon.hIcon = image;
        wcscpy_s(icon.szTip, AppName);
        Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &icon);
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
    }
};

static HICON makeIcon() {
    auto icon =
        static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!icon)
        throw std::runtime_error(winError("Load application icon"));
    return icon;
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Application *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window = window;
    }
    if (!app)
        return DefWindowProcW(window, message, wParam, lParam);
    if (message == app->taskbarMessage && app->taskbarMessage) {
        app->tray(true);
        return 0;
    }
    try {
        if (message == TrayMessage) {
            UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONDBLCLK || event == NIN_KEYSELECT) {
                launchMenu();
                return 0;
            }
            if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                auto config = Config::from(app->engine->snapshot()["Config"]);
                auto tr = [&](const wchar_t *text) { return localize(config.language, text); };
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, tr(L"Відкрити меню…").c_str());
                AppendMenuW(menu, MF_STRING, 2, tr(L"Увімкнути / вимкнути все").c_str());
                AppendMenuW(menu, MF_STRING, 3, tr(L"Повторити підключення").c_str());
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 4, tr(L"Вийти").c_str());
                POINT point{};
                GetCursorPos(&point);
                SetForegroundWindow(window);
                int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x,
                                              point.y, 0, window, nullptr);
                DestroyMenu(menu);
                PostMessageW(window, WM_NULL, 0, 0);
                if (selected == 1)
                    launchMenu();
                else if (selected == 2) {
                    auto state = app->engine->snapshot();
                    auto c = Config::from(state["Config"]);
                    c.global.enabled = !c.global.enabled;
                    app->engine->update(c, state["Revision"].get<uint64_t>());
                } else if (selected == 3)
                    app->engine->reconnect();
                else if (selected == 4)
                    PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
        }
        if (message == WM_DEVICECHANGE &&
            (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
            SetTimer(window, 1, 1500, nullptr);
            return TRUE;
        }
        if (message == WM_TIMER && wParam == 1) {
            KillTimer(window, 1);
            app->engine->reconnect();
            return 0;
        }
        if (message == WM_POWERBROADCAST) {
            if (wParam == PBT_APMSUSPEND)
                app->engine->pause(true);
            else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
                app->engine->pause(false);
            return TRUE;
        }
        if (message == WM_QUERYENDSESSION)
            return TRUE;
        if (message == WM_ENDSESSION && wParam) {
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_CLOSE) {
            app->exitCode = wParam == 1 ? 1 : 0;
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            Shell_NotifyIconW(NIM_DELETE, &app->icon);
            PostQuitMessage(app->exitCode);
            return 0;
        }
    } catch (const std::exception &ex) {
        log(std::string("Tray action: ") + ex.what());
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static Json processStatus() {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                         sizeof(memory));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    return {{"ProcessId", GetCurrentProcessId()},
            {"WorkingSetMB", double(memory.WorkingSetSize) / 1048576},
            {"PrivateCommitMB", double(memory.PrivateUsage) / 1048576},
            {"Handles", handles}};
}

int runBackground(bool autostart) {
    auto mutexName = L"Local\\VeryLightRGB.Application-" + userSid();
    Handle singleton(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
    if (!singleton)
        throw std::runtime_error(winError("Application mutex"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log("Duplicate background launch skipped.");
        return 0;
    }
    log(std::string("Application starting; version=") + Version +
        "; pid=" + std::to_string(GetCurrentProcessId()) + "; autostart=" + std::to_string(autostart) +
        "; data=" + utf8(dataDirectory().wstring()));
    auto config = Config::load();
    DWORD mainThread = GetCurrentThreadId();
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    Engine engine(config, true, [mainThread] { PostThreadMessageW(mainThread, WM_QUIT, 1, 0); });
    Application app;
    app.engine = &engine;
    app.image = makeIcon();
    app.taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = windowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"VeryLightRGB.Tray";
    cls.hIcon = app.image;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error(winError("Tray class"));
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, AppName, WS_POPUP, 0, 0, 0, 0, nullptr,
                                  nullptr, cls.hInstance, &app);
    if (!window)
        throw std::runtime_error(winError("Tray window"));
    app.tray(true);
    GUID guid{};
    HidD_GetHidGuid(&guid);
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = guid;
    app.devices = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    PipeServer server([&](const Json &command) -> Json {
        std::string action = command.value("Command", std::string("status"));
        if (action == "status") {
            auto value = engine.snapshot();
            value["Process"] = processStatus();
            value["StartupEnabled"] = startupEnabled();
            return value;
        }
        if (action == "diagnostics") {
            Json value = {{"DataDirectory", utf8(dataDirectory().wstring())},
                          {"Executable", utf8(executablePath())},
                          {"Runtime", engine.snapshot()},
                          {"ConfigOnDisk", Config::load().json()}};
            auto logFile = dataDirectory() / L"light.log";
            if (std::filesystem::exists(logFile)) {
                auto text = readFile(logFile, 1024 * 1024);
                if (text.size() > 16384)
                    text.erase(0, text.size() - 16384);
                value["RecentLog"] = text;
            }
            auto menuReport = dataDirectory() / L"menu-self-test.json";
            if (std::filesystem::exists(menuReport))
                value["MenuSelfTest"] = parseJson(readFile(menuReport));
            auto colorReport = dataDirectory() / L"menu-colors-self-test.json";
            if (std::filesystem::exists(colorReport))
                value["MenuColorsSelfTest"] = parseJson(readFile(colorReport));
            return value;
        }
        if (action == "set-config") {
            engine.update(Config::from(command.at("Config")), command.at("Revision").get<uint64_t>());
            return engine.snapshot();
        }
        if (action == "edit-lighting") {
            engine.editLighting(command.value("Scope", std::string()), command.at("Field").get<std::string>(),
                                Style::from(command.at("Style")), command.at("Revision").get<uint64_t>());
            return engine.snapshot();
        }
        if (action == "reload-devices") {
            engine.refresh();
            return engine.snapshot();
        }
        if (action == "reconnect") {
            engine.reconnect();
            return {{"Message", "Повторне підключення"}};
        }
        if (action == "identify") {
            engine.identify(command.at("Id").get<std::string>());
            return {{"Message", "Пробне підсвічування"}};
        }
        if (action == "startup") {
            setStartup(command.at("Enabled").get<bool>());
            return {{"Enabled", startupEnabled()}};
        }
        if (action == "menu") {
            launchMenu(command.value("SelfTest", false));
            return Json::object();
        }
        if (action == "quit") {
            int exitCode = command.value("ExitCode", 0);
            if (exitCode != 0 && exitCode != 1)
                throw std::runtime_error("Unsupported exit code");
            PostMessageW(window, WM_CLOSE, WPARAM(exitCode), 0);
            return {{"Message", "Завершення"}};
        }
        throw std::runtime_error("Unknown command");
    });
    BOOL next;
    while ((next = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    app.exitCode = next < 0 ? 1 : int(message.wParam);
    server.stop();
    engine.stop();
    if (app.devices)
        UnregisterDeviceNotification(app.devices);
    Shell_NotifyIconW(NIM_DELETE, &app.icon);
    if (IsWindow(window))
        DestroyWindow(window);
    DestroyIcon(app.image);
    log(app.exitCode ? "Background failure requested." : "Application exited normally.");
    return app.exitCode;
}

static void output(const Json &result, const std::vector<std::wstring> &args) {
    auto it = std::find(args.begin(), args.end(), L"--output");
    auto text = result.dump(2);
    if (it != args.end() && it + 1 != args.end()) {
        writeAtomic(absolutePath(*(it + 1)), text);
        return;
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        out = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (out && out != INVALID_HANDLE_VALUE) {
        text += "\r\n";
        DWORD n = 0;
        WriteFile(out, text.data(), DWORD(text.size()), &n, nullptr);
    }
}

int mainEntry(const std::vector<std::wstring> &args) {
    auto has = [&](const wchar_t *s) { return std::find(args.begin(), args.end(), s) != args.end(); };
    auto argument = [&](const wchar_t *key) -> std::wstring {
        auto i = std::find(args.begin(), args.end(), key);
        if (i == args.end() || i + 1 == args.end())
            throw std::runtime_error("Missing argument");
        return *(i + 1);
    };
    if (has(L"--data-dir"))
        setDataDirectory(argument(L"--data-dir"));
    try {
        if (has(L"--installer-stop")) {
            stopInstallation(has(L"--target-exe") ? argument(L"--target-exe") : executablePath());
            return 0;
        }
        if (has(L"--installer-remove-startup")) {
            removeOwnedStartup();
            return 0;
        }
        if (has(L"--installer-check")) {
            Driver worker(true);
            std::vector<std::string> notes;
            if (worker.discover(false, false, notes).size() != 2)
                throw std::runtime_error("Native driver bundle check failed");
            output({{"Version", Version}, {"DriverBundle", "ok"}, {"HardwareAccessed", false}}, args);
            return 0;
        }
        if (has(L"--enable-startup") || has(L"--disable-startup")) {
            setStartup(has(L"--enable-startup"));
            output({{"Enabled", startupEnabled()},
                    {"TaskName", utf8(startupTaskName())},
                    {"DataDirectory", utf8(dataDirectory().wstring())}},
                   args);
            return 0;
        }
        if (has(L"--startup-info")) {
            output({{"Version", Version},
                    {"Process", processStatus()},
                    {"DataDirectory", utf8(dataDirectory().wstring())},
                    {"SettingsExist", std::filesystem::exists(dataDirectory() / L"settings.json")},
                    {"StartupEnabled", startupEnabled()},
                    {"TaskName", utf8(startupTaskName())}},
                   args);
            return 0;
        }
        if (has(L"--shortcut")) {
            createShortcut(argument(L"--shortcut"));
            return 0;
        }
        if (has(L"--status")) {
            output(rpc({{"Command", "status"}}), args);
            return 0;
        }
        if (has(L"--sdk-list")) {
            int port = has(L"--sdk-port") ? std::stoi(argument(L"--sdk-port")) : 6742;
            Sdk sdk(port);
            Json devices = Json::array();
            for (auto &device : sdk.discover())
                devices.push_back(device.json());
            output({{"Port", port}, {"Devices", devices}, {"Controllers", sdk.inspect()}}, args);
            return 0;
        }
        if (has(L"--sdk-resize")) {
            int port = has(L"--sdk-port") ? std::stoi(argument(L"--sdk-port")) : 6742;
            auto id = utf8(argument(L"--id"));
            int count = std::stoi(argument(L"--led-count"));
            Sdk sdk(port);
            sdk.resize(id, count);
            output({{"Id", id}, {"LedCount", count}, {"Message", "Zone resize sent; reload devices"}}, args);
            return 0;
        }
        if (has(L"--send")) {
            auto command = parseJson(utf8(argument(L"--send")));
            output(rpc(command), args);
            return 0;
        }
        if (has(L"--request")) {
            auto command = parseJson(readFile(argument(L"--request")));
            output(rpc(command), args);
            return 0;
        }
        if (has(L"--exit")) {
            try {
                output(rpc({{"Command", "quit"}}, 2000), args);
            } catch (...) {
            }
            return 0;
        }
        if (has(L"--reload-devices")) {
            output(rpc({{"Command", "reload-devices"}}), args);
            return 0;
        }
        if (has(L"--off") || has(L"--on")) {
            auto state = rpc({{"Command", "status"}});
            auto c = Config::from(state["Config"]);
            c.global.enabled = has(L"--on");
            output(rpc({{"Command", "set-config"}, {"Config", c.json()}, {"Revision", state["Revision"]}}),
                   args);
            return 0;
        }
        if (has(L"--diagnose")) {
            Handle lease(CreateMutexW(nullptr, FALSE, L"Local\\RgbTray.Hardware"));
            DWORD lock = WaitForSingleObject(lease.h, 0);
            if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED)
                throw std::runtime_error("Зупини анімацію перед діагностикою");
            Json result;
            try {
                checkConflicts();
                try {
                    Msi m;
                    result["Msi"] = m.diagnose();
                } catch (const std::exception &ex) {
                    result["MsiError"] = ex.what();
                }
                try {
                    Gpu g;
                    result["Gainward"] = g.diagnose();
                } catch (const std::exception &ex) {
                    result["GpuError"] = ex.what();
                }
            } catch (...) {
                ReleaseMutex(lease.h);
                throw;
            }
            ReleaseMutex(lease.h);
            output(result, args);
            return result.contains("Msi") || result.contains("Gainward") ? 0 : 2;
        }
        if (has(L"--test-effect")) {
            auto c = Config::load();
            if (c.catalog.empty()) {
                std::vector<std::string> notes;
                c.catalog = discoverNative(notes);
                for (auto &d : c.catalog)
                    c.device(d);
            }
            Engine engine(c);
            if (!engine.waitReady())
                throw std::runtime_error("Hardware startup timed out");
            Sleep(6000);
            auto state = engine.snapshot();
            engine.stop();
            output(state, args);
            auto status = state["Status"];
            return !status.empty() &&
                           std::all_of(status.begin(), status.end(),
                                       [](auto &r) { return r.at("Success").template get<bool>(); })
                       ? 0
                       : 2;
        }
        if (has(L"--background")) {
            int result = runBackground(has(L"--autostart"));
            if (result)
                reportBackgroundFailure("Background process ended with an error.");
            return result;
        }
        if (has(L"--menu-self-test"))
            return runMenu(true);
        return runMenu();
    } catch (const std::exception &ex) {
        log(ex.what());
        if (has(L"--background"))
            reportBackgroundFailure(ex.what());
        try {
            output({{"Error", ex.what()}}, args);
        } catch (...) {
        }
        if (has(L"--menu"))
            MessageBoxW(nullptr, wide(ex.what()).c_str(), AppName, MB_OK | MB_ICONERROR);
        return 1;
    }
}
} // namespace vlr

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv)
        return 1;
    std::vector<std::wstring> args;
    for (int i = 1; i < count; i++)
        args.emplace_back(argv[i]);
    LocalFree(argv);
    try {
        return vlr::mainEntry(args);
    } catch (const std::exception &ex) {
        vlr::log(ex.what());
        return 1;
    }
}

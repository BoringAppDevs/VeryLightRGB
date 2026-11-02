#include "startup.hpp"
#include "ipc.hpp"
#include <tlhelp32.h>
#include <taskschd.h>
#include <oleauto.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace vlr {
template <class T> struct Com {
    T *p = nullptr;

    ~Com() {
        if (p)
            p->Release();
    }

    T **operator&() {
        return &p;
    }

    T *operator->() const {
        return p;
    }
};

struct Bstr {
    BSTR value;

    explicit Bstr(const std::wstring &s) : value(SysAllocStringLen(s.data(), UINT(s.size()))) {
        if (!value)
            throw std::bad_alloc();
    }

    ~Bstr() {
        SysFreeString(value);
    }

    operator BSTR() const {
        return value;
    }
};

struct Variant {
    VARIANT v;

    Variant() {
        VariantInit(&v);
    }

    explicit Variant(const std::wstring &s) : Variant() {
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocStringLen(s.data(), UINT(s.size()));
        if (!v.bstrVal)
            throw std::bad_alloc();
    }

    ~Variant() {
        VariantClear(&v);
    }
};

struct ComInit {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ~ComInit() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};

static void check(HRESULT hr, const char *operation) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(operation) + " (HRESULT " + std::to_string(uint32_t(hr)) + ")");
}

static std::wstring escape(const std::wstring &s) {
    std::wstring out;
    for (auto c : s)
        switch (c) {
        case L'&':
            out += L"&amp;";
            break;
        case L'<':
            out += L"&lt;";
            break;
        case L'>':
            out += L"&gt;";
            break;
        case L'\"':
            out += L"&quot;";
            break;
        default:
            out += c;
        }
    return out;
}

std::wstring startupTaskName() {
    return L"VeryLightRGB-" + userSid();
}

std::wstring startupXml(const std::wstring &executable, const std::wstring &sid,
                        const std::wstring &directory) {
    // Event-driven recovery uses the existing Windows event service; no watchdog process.
    auto failureQuery = L"<QueryList><Query Id=\"0\" Path=\"Application\"><Select Path=\"Application\">" +
                        escape(L"*[System[Provider[@Name='Very Light RGB'] and EventID=2001]] and "
                               L"*[EventData[Data=\"" +
                               executable + L"\"]]") +
                        L"</Select><Select Path=\"Application\">" +
                        escape(L"*[System[Provider[@Name='Application Error'] and EventID=1000]] and "
                               L"*[EventData[Data[@Name='AppPath']=\"" +
                               executable + L"\"]]") +
                        L"</Select></Query></QueryList>";
    return L"<Task version=\"1.2\" "
           L"xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/"
           L"task\"><RegistrationInfo><Description>Very Light RGB — native tray "
           L"lighting</Description></RegistrationInfo>"
           L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" +
           escape(sid) +
           L"</UserId><Delay>PT10S</Delay></LogonTrigger><EventTrigger><Enabled>true</Enabled>"
           L"<Subscription>" +
           escape(failureQuery) +
           L"</Subscription><Delay>PT5S</Delay></EventTrigger></Triggers>"
           L"<Principals><Principal id=\"User\"><UserId>" +
           escape(sid) +
           L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></"
           L"Principal></Principals>"
           L"<Settings><MultipleInstancesPolicy>IgnoreNew</"
           L"MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</"
           L"DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
           L"<AllowHardTerminate>false</AllowHardTerminate><StartWhenAvailable>true</"
           L"StartWhenAvailable><RunOnlyIfNetworkAvailable>false</"
           L"RunOnlyIfNetworkAvailable><AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled>"
           L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</"
           L"Priority><RestartOnFailure><Interval>PT1M</Interval><Count>3</Count></RestartOnFailure></"
           L"Settings>"
           L"<Actions Context=\"User\"><Exec><Command>" +
           escape(executable) + L"</Command><Arguments>--background --autostart --data-dir &quot;" +
           escape(directory) + L"&quot;</Arguments><WorkingDirectory>" +
           escape(std::filesystem::path(executable).parent_path().wstring()) +
           L"</WorkingDirectory></Exec></Actions></Task>";
}

static void connect(Com<ITaskService> &service, Com<ITaskFolder> &folder) {
    check(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                           reinterpret_cast<void **>(&service.p)),
          "Task Scheduler");
    Variant empty;
    check(service->Connect(empty.v, empty.v, empty.v, empty.v), "Connect Task Scheduler");
    Bstr root(L"\\");
    check(service->GetFolder(root, &folder.p), "Task folder");
}

bool startupEnabled() {
    try {
        ComInit init;
        Com<ITaskService> service;
        Com<ITaskFolder> folder;
        connect(service, folder);
        Com<IRegisteredTask> task;
        Bstr name(startupTaskName());
        HRESULT hr = folder->GetTask(name, &task.p);
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            return false;
        check(hr, "Read startup task");
        VARIANT_BOOL enabled = VARIANT_FALSE;
        check(task->get_Enabled(&enabled), "Startup state");
        return enabled != VARIANT_FALSE;
    } catch (...) {
        return false;
    }
}

void setStartup(bool enabled) {
    ComInit init;
    Com<ITaskService> service;
    Com<ITaskFolder> folder;
    connect(service, folder);
    Bstr name(startupTaskName());
    if (enabled) {
        Bstr xml(startupXml(executablePath(), userSid(), dataDirectory().wstring()));
        Variant user(userSid()), empty;
        Com<IRegisteredTask> registered;
        check(folder->RegisterTask(name, xml, TASK_CREATE_OR_UPDATE, user.v, empty.v,
                                   TASK_LOGON_INTERACTIVE_TOKEN, empty.v, &registered.p),
              "Register startup task");
    } else {
        HRESULT hr = folder->DeleteTask(name, 0);
        if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            check(hr, "Remove startup task");
    }
    log(enabled ? "Interactive logon task enabled; 10s delay; event-driven crash recovery."
                : "Logon task disabled.");
}

void removeOwnedStartup() {
    ComInit init;
    Com<ITaskService> service;
    Com<ITaskFolder> folder;
    connect(service, folder);
    Com<IRegisteredTask> task;
    Bstr name(startupTaskName());
    auto hr = folder->GetTask(name, &task.p);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return;
    check(hr, "Read startup task");
    Com<ITaskDefinition> definition;
    Com<IActionCollection> actions;
    Com<IAction> action;
    Com<IExecAction> executable;
    check(task->get_Definition(&definition.p), "Startup definition");
    check(definition->get_Actions(&actions.p), "Startup actions");
    check(actions->get_Item(1, &action.p), "Startup action");
    check(action->QueryInterface(IID_IExecAction, reinterpret_cast<void **>(&executable.p)), "Startup executable");
    BSTR path = nullptr;
    check(executable->get_Path(&path), "Startup path");
    const bool owned = path && _wcsicmp(path, executablePath().c_str()) == 0;
    SysFreeString(path);
    if (owned) check(folder->DeleteTask(name, 0), "Remove owned startup task");
}

static std::wstring processPath(HANDLE process) {
    std::wstring path(32768, L'\0');
    DWORD length = DWORD(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
    path.resize(length);
    return path;
}

void stopInstallation(const std::wstring &target) {
    const auto expected = absolutePath(target);
    Json state;
    try { state = rpc({{"Command", "status"}}, 1000); } catch (...) {}
    if (state.contains("Process")) {
        const auto pid = state.at("Process").at("ProcessId").get<DWORD>();
        Handle agent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if (!agent || _wcsicmp(processPath(agent.h).c_str(), expected.c_str()) != 0)
            throw std::runtime_error("Close the running copy of Very Light RGB in the other folder first.");
        rpc({{"Command", "quit"}}, 3000);
        if (WaitForSingleObject(agent.h, 15000) != WAIT_OBJECT_0)
            throw std::runtime_error("Very Light RGB is still stopping; retry installation shortly.");
    }
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw std::runtime_error(winError("Installation process list"));
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.h, &entry); more; more = Process32NextW(snapshot.h, &entry)) {
        if (entry.th32ProcessID == GetCurrentProcessId() || _wcsicmp(entry.szExeFile, L"VeryLightRGB.exe")) continue;
        Handle menu(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, entry.th32ProcessID));
        if (!menu || _wcsicmp(processPath(menu.h).c_str(), expected.c_str())) continue;
        // Settings are committed by IPC on every edit. Only this installation's remaining menus are closed.
        if (WaitForSingleObject(menu.h, 200) == WAIT_TIMEOUT) {
            if (!TerminateProcess(menu.h, 0) || WaitForSingleObject(menu.h, 5000) != WAIT_OBJECT_0)
                throw std::runtime_error(winError("Close installation menu"));
        }
    }
}

void reportBackgroundFailure(const std::string &reason) noexcept {
    try {
        auto executable = executablePath();
        auto message = wide(reason);
        HANDLE source = RegisterEventSourceW(nullptr, AppName);
        if (!source)
            return;
        const wchar_t *strings[] = {executable.c_str(), message.c_str()};
        ReportEventW(source, EVENTLOG_ERROR_TYPE, 0, 2001, nullptr, 2, 0, strings, nullptr);
        DeregisterEventSource(source);
    } catch (...) {
    }
}

void createShortcut(const std::wstring &destination) {
    ComInit init;
    Com<IShellLinkW> link;
    check(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                           reinterpret_cast<void **>(&link.p)),
          "Shortcut");
    check(link->SetPath(executablePath().c_str()), "Shortcut executable");
    check(link->SetArguments(L"--menu"), "Shortcut arguments");
    check(link->SetIconLocation(executablePath().c_str(), 0), "Shortcut icon");
    check(link->SetWorkingDirectory(std::filesystem::path(executablePath()).parent_path().c_str()),
          "Shortcut directory");
    check(link->SetDescription(L"Very Light RGB — lightweight RGB controls"), "Shortcut description");
    Com<IPersistFile> file;
    check(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file.p)), "Shortcut file");
    check(file->Save(destination.c_str(), TRUE), "Save shortcut");
}
} // namespace vlr

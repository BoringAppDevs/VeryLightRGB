#include "ipc.hpp"
#include <sddl.h>

namespace vlr {
static std::wstring testPipeSuffix;

void setTestPipeNamespace(const std::wstring &suffix) {
    testPipeSuffix = suffix;
}

std::wstring pipeName() {
    return L"\\\\.\\pipe\\VeryLightRGB-" + userSid() + testPipeSuffix;
}

static DWORD operation(HANDLE file, void *data, DWORD size, bool writing, HANDLE stop, DWORD timeout) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event)
        throw std::runtime_error(winError("IPC event"));
    OVERLAPPED ov{};
    ov.hEvent = event.h;
    DWORD done = 0;
    BOOL result = writing ? WriteFile(file, data, size, &done, &ov) : ReadFile(file, data, size, &done, &ov);
    if (!result) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
            throw std::runtime_error(winError("IPC transfer", error));
        HANDLE events[] = {event.h, stop};
        DWORD wait = WaitForMultipleObjects(stop ? 2 : 1, events, FALSE, timeout);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(file, &ov);
            GetOverlappedResult(file, &ov, &done, TRUE);
            throw std::runtime_error(wait == WAIT_TIMEOUT ? "IPC timeout" : "IPC stopped");
        }
        if (!GetOverlappedResult(file, &ov, &done, FALSE))
            throw std::runtime_error(winError("IPC completion"));
    }
    if (!done)
        throw std::runtime_error("IPC disconnected");
    return done;
}

static void transfer(HANDLE file, void *data, size_t size, bool writing, HANDLE stop, DWORD timeout) {
    auto bytes = static_cast<uint8_t *>(data);
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    while (size) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0)
            throw std::runtime_error("IPC deadline exceeded");
        DWORD n = operation(file, bytes, DWORD(size), writing, stop, DWORD(left));
        bytes += n;
        size -= n;
    }
}

static void writeMessage(HANDLE file, const Json &value, HANDLE stop, DWORD timeout) {
    auto text = value.dump();
    if (text.size() > MaxJson)
        throw std::runtime_error("IPC message too large");
    uint32_t size = uint32_t(text.size());
    transfer(file, &size, 4, true, stop, timeout);
    transfer(file, text.data(), text.size(), true, stop, timeout);
}

static Json readMessage(HANDLE file, HANDLE stop, DWORD timeout) {
    uint32_t size = 0;
    transfer(file, &size, 4, false, stop, timeout);
    if (!size || size > MaxJson)
        throw std::runtime_error("Invalid IPC message size");
    std::string text(size, '\0');
    transfer(file, text.data(), size, false, stop, timeout);
    return parseJson(text);
}

Json rpc(const Json &command, DWORD timeout) {
    auto name = pipeName();
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    Handle file;
    while (!file) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0)
            throw std::runtime_error("Фоновий процес ще не готовий");
        if (!WaitNamedPipeW(name.c_str(), DWORD(left))) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_SEM_TIMEOUT) {
                Sleep(10);
                continue;
            }
            throw std::runtime_error(winError("Wait for background process", error));
        }
        file.reset(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr));
        if (!file) {
            DWORD error = GetLastError();
            if (error == ERROR_PIPE_BUSY || error == ERROR_FILE_NOT_FOUND) {
                Sleep(10);
                continue;
            }
            throw std::runtime_error(winError("Connect background process", error));
        }
    }
    writeMessage(file.h, command, nullptr, timeout);
    auto result = readMessage(file.h, nullptr, timeout);
    uint8_t ack = 1;
    transfer(file.h, &ack, 1, true, nullptr, timeout);
    if (!result.value("Ok", false))
        throw std::runtime_error(result.value("Error", std::string("IPC error")));
    return result.value("Data", Json::object());
}

PipeServer::PipeServer(std::function<Json(const Json &)> callback)
    : stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)), handler(std::move(callback)) {
    if (!stopEvent)
        throw std::runtime_error(winError("IPC stop event"));
    thread = std::thread(&PipeServer::run, this);
}

PipeServer::~PipeServer() {
    stop();
}

void PipeServer::stop() {
    if (stopEvent)
        SetEvent(stopEvent.h);
    if (thread.joinable())
        thread.join();
}

void PipeServer::run() noexcept {
    PSECURITY_DESCRIPTOR security = nullptr;
    try {
        auto sddl = L"D:P(A;;GA;;;" + userSid() + L")(A;;GA;;;SY)";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security,
                                                                  nullptr))
            throw std::runtime_error(winError("IPC ACL"));
        SECURITY_ATTRIBUTES attrs{sizeof(attrs), security, FALSE};
        auto name = pipeName();
        while (WaitForSingleObject(stopEvent.h, 0) != WAIT_OBJECT_0) {
            Handle pipe(
                CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 1, 65536, 65536, 5000, &attrs));
            if (!pipe) {
                DWORD error = GetLastError();
                if (error == ERROR_PIPE_BUSY || error == ERROR_ACCESS_DENIED) {
                    if (WaitForSingleObject(stopEvent.h, 50) == WAIT_OBJECT_0)
                        break;
                    continue;
                }
                throw std::runtime_error(winError("Create IPC pipe", error));
            }
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED ov{};
            ov.hEvent = event.h;
            BOOL connected = ConnectNamedPipe(pipe.h, &ov);
            DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (!connected && error == ERROR_IO_PENDING) {
                HANDLE waits[] = {stopEvent.h, event.h};
                DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(pipe.h, &ov);
                    DWORD done = 0;
                    GetOverlappedResult(pipe.h, &ov, &done, TRUE);
                    break;
                }
                DWORD done = 0;
                if (!GetOverlappedResult(pipe.h, &ov, &done, FALSE))
                    continue;
            } else if (!connected && error != ERROR_PIPE_CONNECTED)
                continue;
            try {
                auto command = readMessage(pipe.h, stopEvent.h, 5000);
                Json response;
                try {
                    response = {{"Ok", true}, {"Data", handler(command)}};
                } catch (const std::exception &ex) {
                    response = {{"Ok", false}, {"Error", ex.what()}};
                }
                writeMessage(pipe.h, response, stopEvent.h, 5000);
                // The client acknowledges receipt before disconnect, without an unbounded FlushFileBuffers.
                uint8_t ack = 0;
                transfer(pipe.h, &ack, 1, false, stopEvent.h, 1000);
            } catch (const std::exception &ex) {
                if (WaitForSingleObject(stopEvent.h, 0) != WAIT_OBJECT_0)
                    log(std::string("IPC request: ") + ex.what());
            }
            DisconnectNamedPipe(pipe.h);
        }
    } catch (const std::exception &ex) {
        log(std::string("IPC server stopped: ") + ex.what());
    }
    if (security)
        LocalFree(security);
}
} // namespace vlr

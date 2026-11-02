// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <tlhelp32.h>
#include <chrono>
#include <cwctype>
#include <memory>
#include "database.hpp"

namespace vrgbdb {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
inline std::string utf8(const std::wstring &s) {
    if (s.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid UTF-16");
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), size, nullptr, nullptr);
    return out;
}
inline std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), size);
    return out;
}
inline std::runtime_error error(const std::string &operation) {
    return std::runtime_error(operation + ": Windows error " + std::to_string(GetLastError()));
}

inline Json inventory() {
    GUID guid{}; HidD_GetHidGuid(&guid);
    auto list = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (list == INVALID_HANDLE_VALUE) throw error("HID enumeration");
    Json result = Json::array();
    try {
        for (DWORD i = 0;; ++i) {
            SP_DEVICE_INTERFACE_DATA data{}; data.cbSize = sizeof(data);
            if (!SetupDiEnumDeviceInterfaces(list, nullptr, &guid, i, &data)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
                throw error("HID interface enumeration");
            }
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetailW(list, &data, nullptr, 0, &size, nullptr);
            if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) continue;
            std::vector<uint8_t> buffer(size);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(list, &data, detail, size, nullptr, nullptr)) continue;
            const std::wstring devicePath(detail->DevicePath);
            Handle file(CreateFileW(devicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
            if (!file) continue;
            HIDD_ATTRIBUTES attrs{}; attrs.Size = sizeof(attrs);
            if (!HidD_GetAttributes(file.value, &attrs)) continue;
            PHIDP_PREPARSED_DATA parsed = nullptr;
            if (!HidD_GetPreparsedData(file.value, &parsed)) continue;
            HIDP_CAPS caps{}; const auto status = HidP_GetCaps(parsed, &caps); HidD_FreePreparsedData(parsed);
            if (status != HIDP_STATUS_SUCCESS) continue;
            std::wstring folded = devicePath;
            std::transform(folded.begin(), folded.end(), folded.begin(), ::towlower);
            int interfaceNumber = -1;
            const auto at = folded.find(L"&mi_");
            if (at != std::wstring::npos && at + 6 <= folded.size()) {
                wchar_t *end = nullptr; const auto parsedInterface = wcstoul(folded.c_str() + at + 4, &end, 16);
                if (end == folded.c_str() + at + 6) interfaceNumber = static_cast<int>(parsedInterface);
            }
            wchar_t product[256]{};
            HidD_GetProductString(file.value, product, sizeof(product));
            Json device{{"transport", "hid"}, {"path", utf8(devicePath)}, {"vid", attrs.VendorID}, {"pid", attrs.ProductID},
                        {"usagePage", caps.UsagePage}, {"usage", caps.Usage},
                        {"inputReportLength", caps.InputReportByteLength}, {"outputReportLength", caps.OutputReportByteLength},
                        {"featureReportLength", caps.FeatureReportByteLength}, {"product", utf8(product)}};
            if (interfaceNumber >= 0) device["interface"] = interfaceNumber;
            result.push_back(std::move(device));
        }
    } catch (...) { SetupDiDestroyDeviceInfoList(list); throw; }
    SetupDiDestroyDeviceInfoList(list);
    return result;
}

inline void checkConflicts() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw error("Process snapshot");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.value, &entry)) do {
        std::wstring name(entry.szExeFile);
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        if (name == L"native-driver-core.exe" || name == L"openrgb.exe" || name == L"signalrgb.exe" || name == L"icue.exe" || name == L"corsair.service.exe" ||
            name == L"corsairllaccessservice.exe") throw std::runtime_error("Close the competing RGB application: " + utf8(name));
    } while (Process32NextW(snapshot.value, &entry));
}

class Hid {
    Handle handle;
    size_t inputLength;

    DWORD transfer(bool write, uint8_t *bytes, DWORD length, DWORD timeout) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) throw error("I/O event");
        OVERLAPPED overlapped{}; overlapped.hEvent = event.value;
        DWORD done = 0;
        BOOL success = write ? WriteFile(handle.value, bytes, length, &done, &overlapped) : ReadFile(handle.value, bytes, length, &done, &overlapped);
        if (!success) {
            if (GetLastError() != ERROR_IO_PENDING) throw error(write ? "HID write" : "HID read");
            const DWORD waited = WaitForSingleObject(event.value, timeout);
            if (waited != WAIT_OBJECT_0) {
                CancelIoEx(handle.value, &overlapped);
                GetOverlappedResult(handle.value, &overlapped, &done, TRUE); // Buffer cannot outlive pending I/O.
                if (!write && waited == WAIT_TIMEOUT) return 0;
                throw std::runtime_error("HID I/O timeout or wait failure");
            }
            if (!GetOverlappedResult(handle.value, &overlapped, &done, FALSE)) throw error("HID I/O completion");
        }
        return done;
    }

  public:
    explicit Hid(const Json &device)
        : handle(CreateFileW(wide(device.at("path").get<std::string>()).c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)),
          inputLength(device.at("inputReportLength").get<size_t>()) {
        if (!handle) throw error("Open selected HID device");
        if (inputLength < 2 || inputLength > 4096 || device.at("outputReportLength") != 65)
            throw std::runtime_error("Unexpected HID report descriptor; writes refused");
        HIDD_ATTRIBUTES attrs{}; attrs.Size = sizeof(attrs);
        if (!HidD_GetAttributes(handle.value, &attrs) || attrs.VendorID != device.at("vid") || attrs.ProductID != device.at("pid"))
            throw std::runtime_error("Selected device changed; writes refused");
    }
    void flush() { if (!HidD_FlushQueue(handle.value)) throw error("Flush HID input"); }
    void write(const Packet &packet) {
        auto copy = packet;
        if (transfer(true, copy.data(), static_cast<DWORD>(copy.size()), 1000) != copy.size()) throw std::runtime_error("Incomplete HID write");
    }
    std::vector<uint8_t> read(DWORD timeout = 100) {
        std::vector<uint8_t> buffer(inputLength);
        const auto count = transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), timeout);
        buffer.resize(count);
        // Match HIDAPI semantics: remove the Windows zero report ID on unnumbered input reports.
        if (!buffer.empty() && buffer[0] == 0) buffer.erase(buffer.begin());
        return buffer;
    }
    std::vector<uint8_t> query(const Packet &packet) {
        flush(); write(packet);
        auto reply = read(250);
        if (reply.size() < 3 || reply[0] != packet[1]) throw std::runtime_error("Unexpected or missing QMK protocol response");
        return reply;
    }
};

inline Json selectedDevice(const std::string &devicePath) {
    for (const auto &device : inventory()) if (device.at("path") == devicePath) return device;
    throw std::runtime_error("Selected HID path is no longer present");
}

class Session {
    Json device;
    std::string adapter;
    Hid hid;
    std::unique_ptr<Handle> lease;
    int revision = 0, ledCount = 0;
    unsigned int channel = 0;
    std::vector<uint8_t> indices;
    bool initialized = false;
    std::chrono::steady_clock::time_point lastCommit{};
    std::chrono::steady_clock::time_point lastConflictCheck{};

  public:
    explicit Session(const Json &request)
        : device(selectedDevice(request.at("path").get<std::string>())), adapter(request.at("adapter").get<std::string>()), hid(device) {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : lower(device.at("path").get<std::string>())) { hash ^= c; hash *= 1099511628211ull; }
        const auto name = L"Local\\VeryLightRGB-device-db-" + std::to_wstring(hash);
        // Object lifetime is the lease; no thread owns this mutex.
        lease = std::make_unique<Handle>(CreateMutexW(nullptr, FALSE, name.c_str()));
        if (!*lease) throw error("Device lease");
        if (GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("Selected device is already owned by another native database worker");
        checkConflicts(); lastConflictCheck = std::chrono::steady_clock::now();
        if (adapter == "corsair-lighting-node") {
            if (device.at("vid") != 0x1b1c || !corsairChannels(device.at("pid").get<unsigned int>()))
                throw std::runtime_error("Not an allowlisted Corsair Lighting Node controller");
            channel = static_cast<unsigned int>(integer(request, "channel", 0, corsairChannels(device.at("pid").get<unsigned int>()) - 1));
        } else if (adapter == "qmk-openrgb") {
            if (device.at("usagePage") != 0xff60 || device.at("usage") != 0x61 || device.at("inputReportLength") != 65)
                throw std::runtime_error("Not a QMK raw-HID interface");
            Packet query{}; query[1] = 1;
            revision = hid.query(query)[1];
            if (!qmkRevision(revision)) throw std::runtime_error("Unsupported QMK OpenRGB firmware revision; no lighting writes sent");
            query = {}; query[1] = 3;
            const auto info = hid.query(query);
            ledCount = info[1];
            if (ledCount < 1 || info[2] != ledCount)
                throw std::runtime_error("Sparse or unknown QMK layout requires a model-specific adapter");
            if (revision == 13) for (int i = 0; i < ledCount; ++i) indices.push_back(static_cast<uint8_t>(i));
        } else throw std::runtime_error("No executable adapter for this record");
    }
    Json info() const {
        return {{"adapter", adapter}, {"device", device}, {"revision", revision}, {"ledCount", ledCount},
                {"channel", channel}, {"hardwareVerified", false}, {"serverRequired", false}};
    }
    void frame(const Json &request) {
        if (std::chrono::steady_clock::now() - lastConflictCheck >= std::chrono::seconds(5)) {
            checkConflicts(); lastConflictCheck = std::chrono::steady_clock::now();
        }
        std::vector<Packet> packets;
        if (adapter == "corsair-lighting-node") {
            const auto rgb = colors(request, 204);
            packets = corsairFrame(device.at("pid").get<unsigned int>(), channel, rgb);
        } else {
            const auto rgb = colors(request, 255);
            if (static_cast<int>(rgb.size()) != ledCount) throw std::runtime_error("Frame must match the probed LED count");
            packets = qmkFrame(revision, rgb, indices);
            if (!initialized) packets.insert(packets.begin(), qmkDirectMode());
        }
        for (const auto &packet : packets) {
            hid.write(packet);
            if (adapter == "corsair-lighting-node") hid.read(15);
            else Sleep(1);
        }
        initialized = true; lastCommit = std::chrono::steady_clock::now();
    }
    void keepalive() {
        if (adapter == "corsair-lighting-node" && initialized && std::chrono::steady_clock::now() - lastCommit >= std::chrono::seconds(5)) {
            checkConflicts();
            hid.write(corsairCommit()); hid.read(15); lastCommit = std::chrono::steady_clock::now();
        }
    }
};
}

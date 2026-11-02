#include "hardware.hpp"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <tlhelp32.h>

namespace vlr {
bool supportedBoard() {
    wchar_t value[256]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct",
                        RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS &&
           std::wstring(value) == L"MAG B850M MORTAR WIFI (MS-7E61)";
}

bool supportedGpu(uint32_t device, uint32_t subsystem) {
    return device == 0x2C0210DE && subsystem == 0xF32310B0;
}

void checkConflicts(bool openRgbConflict) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        throw std::runtime_error(winError("Process snapshot"));
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.h, &entry))
        do {
            std::wstring name(entry.szExeFile);
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name == L"rgbtray.exe" || name == L"signalrgb.exe" || name == L"ledkeeper.exe" ||
                name == L"ledkeeper2.exe" || name == L"lightkeeperservice.exe" ||
                name == L"lightkeeperservice2.exe" || (openRgbConflict && name == L"openrgb.exe"))
                throw std::runtime_error("Закрий " + utf8(name) + ", щоб звільнити RGB");
        } while (Process32NextW(snapshot.h, &entry));
}

Msi::Msi() {
    if (!supportedBoard())
        throw std::runtime_error("Немає нативного модуля для цієї материнської плати");
    GUID guid{};
    HidD_GetHidGuid(&guid);
    HDEVINFO list = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (list == INVALID_HANDLE_VALUE)
        throw std::runtime_error(winError("HID enumeration"));
    try {
        for (DWORD index = 0;; index++) {
            SP_DEVICE_INTERFACE_DATA data{};
            data.cbSize = sizeof(data);
            if (!SetupDiEnumDeviceInterfaces(list, nullptr, &guid, index, &data)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                throw std::runtime_error(winError("HID interface"));
            }
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetailW(list, &data, nullptr, 0, &size, nullptr);
            if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536)
                continue;
            Bytes detailBytes(size);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailBytes.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(list, &data, detail, size, nullptr, nullptr))
                throw std::runtime_error(winError("HID path"));
            std::wstring path(detail->DevicePath);
            std::transform(path.begin(), path.end(), path.begin(), ::towlower);
            if (path.find(L"vid_0db0&pid_0076") == std::wstring::npos)
                continue;
            Handle file(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr));
            if (!file)
                throw std::runtime_error(winError("Open MSI HID"));
            HIDD_ATTRIBUTES attrs{};
            attrs.Size = sizeof(attrs);
            if (!HidD_GetAttributes(file.h, &attrs) || attrs.VendorID != 0x0DB0 || attrs.ProductID != 0x0076)
                continue;
            wchar_t serial[128]{};
            if (!HidD_GetSerialNumberString(file.h, serial, sizeof(serial)))
                throw std::runtime_error(winError("MSI serial"));
            if (std::wstring(serial).rfind(L"7E61", 0) != 0)
                continue;
            PHIDP_PREPARSED_DATA parsed = nullptr;
            if (!HidD_GetPreparsedData(file.h, &parsed))
                throw std::runtime_error(winError("HID descriptor"));
            HIDP_CAPS caps{};
            auto result = HidP_GetCaps(parsed, &caps);
            HidD_FreePreparsedData(parsed);
            if (result != HIDP_STATUS_SUCCESS || caps.FeatureReportByteLength < 761)
                throw std::runtime_error("Unknown MSI HID protocol; writes refused");
            handle = std::move(file);
            break;
        }
    } catch (...) {
        SetupDiDestroyDeviceInfoList(list);
        throw;
    }
    SetupDiDestroyDeviceInfoList(list);
    if (!handle)
        throw std::runtime_error("MSI RGB-контролер не знайдено");
}

Bytes Msi::read(uint8_t report, size_t length, bool allow) {
    Bytes out(length);
    out[0] = report;
    if (!HidD_GetFeature(handle.h, out.data(), ULONG(out.size())))
        throw std::runtime_error(winError("MSI GetFeature"));
    if (out[0] != report && !(allow && report == 0x51 && out[0] == 0x50))
        throw std::runtime_error("Unexpected MSI response");
    return out;
}

void Msi::write(const Bytes &data) {
    if (!HidD_SetFeature(handle.h, const_cast<uint8_t *>(data.data()), ULONG(data.size())))
        throw std::runtime_error(winError("MSI SetFeature"));
}

void Msi::zone(int header, Rgb color, int count) {
    auto desired = zoneConfig(read(0x50, 290), header, color, count);
    write(desired);
    for (int i = 0; i < 6; i++) {
        if (i)
            Sleep(15);
        auto actual = read(0x50, 290);
        int offset = 1 + header * 16;
        if (std::equal(actual.begin() + offset, actual.begin() + offset + 16, desired.begin() + offset))
            return;
    }
    throw std::runtime_error("MSI ARGB " + std::to_string(header + 1) + ": конфігурацію не підтверджено");
}

void Msi::frame(int header, std::span<const uint8_t> rgb, Bytes &packet) {
    if (packet.size() != 727 || rgb.empty() || rgb.size() > 360 || rgb.size() % 3 || header < 0 || header > 2)
        throw std::runtime_error("Invalid MSI output frame");
    packet[0] = 0x51;
    packet[1] = 9;
    packet[2] = 4;
    packet[3] = uint8_t(header);
    packet[6] = uint8_t(rgb.size() / 3);
    std::copy(rgb.begin(), rgb.end(), packet.begin() + 7);
    write(packet);
    frameReply[0] = 0x51;
    if (!HidD_GetFeature(handle.h, frameReply.data(), ULONG(frameReply.size())))
        throw std::runtime_error(winError("MSI post-frame read"));
    if (frameReply[0] != 0x51 && frameReply[0] != 0x50)
        throw std::runtime_error("Unexpected MSI frame response");
}

Json Msi::diagnose() {
    auto bytes = read(0x50, 290);
    static const char *hex = "0123456789ABCDEF";
    std::string text;
    for (auto b : bytes) {
        text += hex[b >> 4];
        text += hex[b & 15];
    }
    return {{"Config", text},
            {"Headers", Json::array({{{"Header", 1}, {"Mode", bytes[1]}, {"Leds", bytes[16]}},
                                     {{"Header", 2}, {"Mode", bytes[17]}, {"Leds", bytes[32]}},
                                     {{"Header", 3}, {"Mode", bytes[33]}, {"Leds", bytes[48]}}})}};
}

static void nvcheck(int status, const char *operation) {
    if (status)
        throw std::runtime_error(std::string("NVIDIA ") + operation + ": " + std::to_string(status));
}

Gpu::Gpu() {
    wchar_t directory[MAX_PATH]{};
    if (!GetSystemDirectoryW(directory, MAX_PATH))
        throw std::runtime_error("System directory unavailable");
    auto path = std::filesystem::path(directory) / L"nvapi64.dll";
    library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
        throw std::runtime_error("NVIDIA NVAPI недоступний");
    try {
        using Query = void *(__cdecl *)(uint32_t);
        auto query = reinterpret_cast<Query>(GetProcAddress(library, "nvapi_QueryInterface"));
        if (!query)
            throw std::runtime_error("NVAPI QueryInterface unavailable");
        auto get = [&](uint32_t id) {
            auto p = query(id);
            if (!p)
                throw std::runtime_error("Required NVAPI interface unavailable");
            return p;
        };
        unload = reinterpret_cast<Simple>(get(0xD22BDD7E));
        nvcheck(reinterpret_cast<Simple>(get(0x0150E828))(), "Initialize");
        initialized = true;
        read = reinterpret_cast<I2c>(get(0x4D7B0709));
        write = reinterpret_cast<I2c>(get(0x283AC65A));
        using Enumerate = int(__cdecl *)(void **, int *);
        using Pci = int(__cdecl *)(void *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
        void *handles[64]{};
        int count = 0;
        nvcheck(reinterpret_cast<Enumerate>(get(0xE5AC921F))(handles, &count), "EnumPhysicalGPUs");
        if (count < 0 || count > 64)
            throw std::runtime_error("Invalid GPU count");
        auto pci = reinterpret_cast<Pci>(get(0x2DDFB66E));
        for (int i = 0; i < count; i++) {
            uint32_t device = 0, subsystem = 0, revision = 0, ext = 0;
            nvcheck(pci(handles[i], &device, &subsystem, &revision, &ext), "PCI identifiers");
            if (supportedGpu(device, subsystem)) {
                gpu = handles[i];
                break;
            }
        }
        if (!gpu)
            throw std::runtime_error("Gainward RTX 5080 Phoenix не знайдено");
    } catch (...) {
        if (initialized && unload)
            unload();
        FreeLibrary(library);
        library = nullptr;
        throw;
    }
}

Gpu::~Gpu() {
    if (initialized && unload)
        unload();
    if (library)
        FreeLibrary(library);
}

uint8_t Gpu::reg(uint8_t address, int value) {
    uint8_t data = value < 0 ? 0 : uint8_t(value);
    Info info{};
    info.version = uint32_t(sizeof(info)) | (3u << 16);
    info.address = 0x92;
    info.reg = &address;
    info.registerSize = 1;
    info.data = &data;
    info.size = 1;
    info.deprecatedSpeed = 0xFFFF;
    info.port = 1;
    info.portSet = 1;
    uint32_t extra = 0;
    nvcheck((value < 0 ? read : write)(gpu, &info, &extra), value < 0 ? "RGB read" : "RGB write");
    return data;
}

void Gpu::frame(Rgb c) {
    reg(0x6C, c[0]);
    reg(0x6D, c[1]);
    reg(0x6E, c[2]);
}

void Gpu::apply(Rgb c) {
    reg(0xE0, 0);
    reg(0x60, 1);
    reg(0x6F, 100);
    frame(c);
    Rgb actual{};
    uint8_t mode = 0;
    // A cold NVAPI/controller session may expose the previous values briefly.
    // Poll confirmation without sending duplicate writes or scanning other registers.
    for (int attempt = 0; attempt < 6; attempt++) {
        if (attempt)
            Sleep(15);
        actual = {reg(0x6C), reg(0x6D), reg(0x6E)};
        mode = reg(0x60);
        if (actual == c && mode == 1)
            return;
    }
    throw std::runtime_error("GPU: колір не підтверджено; expected=" + hexColor(c) +
                             ", actual=" + hexColor(actual) + ", mode=" + std::to_string(mode));
}

Json Gpu::diagnose() {
    return {{"Device", "10DE:2C02 / 10B0:F323"},
            {"Registers",
             {{"0xE0", reg(0xE0)},
              {"0x60", reg(0x60)},
              {"0x6C", reg(0x6C)},
              {"0x6D", reg(0x6D)},
              {"0x6E", reg(0x6E)},
              {"0x6F", reg(0x6F)}}}};
}

std::vector<Descriptor> discoverNative(std::vector<std::string> &notes,
                                       std::function<void(int, const std::string &)> progress) {
    std::vector<Descriptor> found;
    if (progress)
        progress(0, "Перевірка MSI · ARGB-хедери");
    try {
        Msi probe;
        for (int i = 0; i < 3; i++)
            found.push_back(headerDescriptor(i));
    } catch (const std::exception &ex) {
        notes.push_back(ex.what());
    }
    if (progress)
        progress(1, "Перевірка Gainward · відеокарта");
    try {
        Gpu probe;
        found.push_back(gpuDescriptor());
    } catch (const std::exception &ex) {
        notes.push_back(ex.what());
    }
    if (progress)
        progress(2, "Нативні контролери перевірено");
    return found;
}
} // namespace vlr

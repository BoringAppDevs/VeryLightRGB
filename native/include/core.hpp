#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"

namespace vlr {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Rgb = std::array<uint8_t, 3>;
using Clock = std::chrono::steady_clock;
constexpr size_t MaxJson = 2 * 1024 * 1024;
constexpr wchar_t AppName[] = L"Very Light RGB";
constexpr char Version[] = "1.2.0";
constexpr int EffectCount = 9;

struct Handle {
    HANDLE h = nullptr;
    Handle() = default;

    explicit Handle(HANDLE value) : h(value) {}

    ~Handle() {
        reset();
    }

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    Handle(Handle &&other) noexcept : h(other.h) {
        other.h = nullptr;
    }

    Handle &operator=(Handle &&other) noexcept {
        if (this != &other) {
            reset();
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }

    void reset(HANDLE value = nullptr) {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        h = value;
    }

    explicit operator bool() const {
        return h && h != INVALID_HANDLE_VALUE;
    }
};

std::wstring wide(const std::string &value);
std::string utf8(const std::wstring &value);
std::string winError(const std::string &operation, DWORD code = GetLastError());
std::wstring executablePath();
std::wstring localAppData();
std::wstring userSid();
std::wstring absolutePath(const std::wstring &path);
std::string readFile(const std::filesystem::path &path, size_t limit = MaxJson);
void writeAtomic(const std::filesystem::path &path, const std::string &text);
Json parseJson(const std::string &text);
void setDataDirectory(const std::wstring &path);
std::filesystem::path dataDirectory();
void log(const std::string &message) noexcept;
std::string hash(const std::string &value);
std::string effectName(int effect);
Rgb parseColor(const std::string &hex);
std::string hexColor(Rgb color);

struct Style {
    std::string color = "#FF8040", secondary = "#536CFF";
    int brightness = 50, effect = 0, step = 180, darkWidth = 4;
    bool enabled = true;
    void validate() const;
    Rgb output() const;
    Json json() const;
    static Style from(const Json &j);
};

struct Descriptor {
    std::string id, name, controllerKey;
    int count = 12, maxLeds = 120, zone = 0, controllerType = -1;
    uint32_t controller = 0;
    bool configurable = true, single = false, present = true;
    Json driver = Json::object();
    Json json() const;
    static Descriptor from(const Json &j);

    bool native() const {
        return id.rfind("native:", 0) == 0;
    }

    bool sdk() const {
        return id.rfind("openrgb:", 0) == 0;
    }
    bool core() const { return id.rfind("driver:", 0) == 0; }
};

Descriptor headerDescriptor(int index);
Descriptor gpuDescriptor();

struct Device {
    std::string id;
    bool managed = true, useGlobal = true, hasCustom = false, reverse = false;
    int leds = 12, offset = 0;
    Style style;
    Json json() const;
    static Device from(const Json &j);
};

struct Config {
    Style global;
    std::string language = "en";
    int leds = 12, fps = 30, port = 6742, backend = 0; // native / SDK / hybrid
    std::vector<Device> devices;
    std::vector<Descriptor> catalog;
    void validate() const;
    bool allows(const Descriptor &d) const;
    Device &device(const Descriptor &d);
    const Device *find(const std::string &id) const;
    Json json() const;
    static Config from(const Json &j);
    static Config load();
    void save() const;
};

void editLighting(Config &config, const std::string &scope, const std::string &field, const Style &value);

struct Plan {
    Descriptor device;
    Style style;
    int count = 12, offset = 0, virtualCount = 12;
    bool reverse = false;
    bool animated() const;
};

std::vector<Plan> plans(const Config &config);

class Renderer {
    Plan plan;
    Rgb primary{}, secondary{};
    Bytes work, frame;

  public:
    explicit Renderer(Plan value);
    const Bytes &render(double ms);
};

Bytes zoneConfig(const Bytes &current, int header, Rgb color, int directCount = 0);
Bytes framePacket(int header, std::span<const uint8_t> rgb);
} // namespace vlr

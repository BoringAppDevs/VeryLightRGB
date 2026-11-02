#pragma once
#include "core.hpp"

namespace vlr {
bool supportedBoard();
bool supportedGpu(uint32_t device, uint32_t subsystem);
void checkConflicts(bool openRgbConflict = true);

class Msi {
    Handle handle;
    Bytes frameReply = Bytes(727);

  public:
    Msi();
    Bytes read(uint8_t report, size_t length, bool allowZoneResponse = false);
    void write(const Bytes &data);
    void zone(int header, Rgb color, int directCount = 0);
    void frame(int header, std::span<const uint8_t> rgb, Bytes &packet);
    Json diagnose();
};

class Gpu {
    struct Info {
        uint32_t version, displayMask;
        uint8_t ddc, address;
        void *reg;
        uint32_t registerSize;
        void *data;
        uint32_t size, deprecatedSpeed, speed;
        uint8_t port;
        uint32_t portSet;
    };

    static_assert(sizeof(Info) == 64);
    using Simple = int(__cdecl *)();
    using I2c = int(__cdecl *)(void *, Info *, uint32_t *);
    HMODULE library = nullptr;
    void *gpu = nullptr;
    Simple unload = nullptr;
    I2c read = nullptr, write = nullptr;
    bool initialized = false;

  public:
    Gpu();
    ~Gpu();
    Gpu(const Gpu &) = delete;
    Gpu &operator=(const Gpu &) = delete;
    uint8_t reg(uint8_t address, int value = -1);
    void apply(Rgb color);
    void frame(Rgb color);
    Json diagnose();
};

std::vector<Descriptor> discoverNative(std::vector<std::string> &notes,
                                       std::function<void(int, const std::string &)> progress = {});
} // namespace vlr

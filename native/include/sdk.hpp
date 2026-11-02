#pragma once
#include "core.hpp"

namespace vlr {
class Sdk {
    SOCKET socket = INVALID_SOCKET;
    bool wsa = false, invalid = false;
    std::set<uint32_t> customControllers;

    struct Message {
        uint32_t controller, id;
        Bytes body;
    };

    void send(uint32_t controller, uint32_t id, std::span<const uint8_t> body);
    Message receive();
    Bytes response(uint32_t controller, uint32_t id);

  public:
    explicit Sdk(int port);
    ~Sdk();
    Sdk(const Sdk &) = delete;
    Sdk &operator=(const Sdk &) = delete;
    std::vector<Descriptor> discover(std::function<void(uint32_t, uint32_t)> progress = {});
    Json inspect();
    Json controllerInfo(uint32_t index);
    void resize(const std::string &zoneId, int count);
    void check();
    void custom(uint32_t controller);
    void frame(const Descriptor &device, std::span<const uint8_t> rgb);
    static std::vector<Descriptor> parseController(const Bytes &data, uint32_t index);
    static Bytes zonePacket(int zone, std::span<const uint8_t> rgb);
};
} // namespace vlr

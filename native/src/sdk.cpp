#include "sdk.hpp"
#include <cstring>

namespace vlr {
static uint32_t u32(std::span<const uint8_t> data, size_t i = 0) {
    if (i + 4 > data.size())
        throw std::runtime_error("Truncated SDK integer");
    return uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) | (uint32_t(data[i + 2]) << 16) |
           (uint32_t(data[i + 3]) << 24);
}

static void put32(std::span<uint8_t> b, size_t i, uint32_t v) {
    for (int k = 0; k < 4; k++)
        b[i + size_t(k)] = uint8_t(v >> (8 * k));
}

static Bytes integer(uint32_t v) {
    Bytes b(4);
    put32(b, 0, v);
    return b;
}

static std::string socketError(const char *op) {
    return std::string(op) + " (SDK " + std::to_string(WSAGetLastError()) + ")";
}

static void transfer(SOCKET socket, uint8_t *bytes, size_t size, bool sending) {
    auto deadline = Clock::now() + std::chrono::seconds(4);
    while (size) {
        if (Clock::now() > deadline)
            throw std::runtime_error("OpenRGB response deadline exceeded");
        int n = sending ? ::send(socket, reinterpret_cast<const char *>(bytes), int(size), 0)
                        : recv(socket, reinterpret_cast<char *>(bytes), int(size), 0);
        if (n <= 0)
            throw std::runtime_error(n == 0 ? "OpenRGB SDK від’єднано"
                                            : socketError(sending ? "Send" : "Receive"));
        bytes += n;
        size -= size_t(n);
    }
}

Sdk::Sdk(int port) {
    if (port < 1 || port > 65535)
        throw std::runtime_error("Invalid SDK port");
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        throw std::runtime_error("Winsock unavailable");
    wsa = true;
    try {
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
            throw std::runtime_error(socketError("Socket"));
        u_long one = 1;
        ioctlsocket(socket, FIONBIO, &one);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(uint16_t(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int connected = connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        if (connected == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            throw std::runtime_error("OpenRGB SDK не запущений на 127.0.0.1:" + std::to_string(port));
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(socket, &ready);
        timeval timeout{2, 0};
        if (select(0, nullptr, &ready, nullptr, &timeout) <= 0)
            throw std::runtime_error("OpenRGB connect timeout");
        int error = 0, n = sizeof(error);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &n);
        if (error)
            throw std::runtime_error("OpenRGB connection failed: " + std::to_string(error));
        one = 0;
        ioctlsocket(socket, FIONBIO, &one);
        DWORD ms = 2000;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&ms), sizeof(ms));
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&ms), sizeof(ms));
        BOOL noDelay = TRUE;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&noDelay), sizeof(noDelay));
        send(0, 40, integer(3));
        auto version = response(0, 40);
        if (version.size() != 4 || u32(version) < 3)
            throw std::runtime_error("Потрібен OpenRGB SDK protocol 3+");
        log("OpenRGB SDK connected; server protocol=" + std::to_string(u32(version)) +
            "; negotiated format=3.");
        std::string name = "Very Light RGB";
        name += '\0';
        send(0, 50, std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(name.data()), name.size()));
    } catch (...) {
        if (socket != INVALID_SOCKET)
            closesocket(socket);
        socket = INVALID_SOCKET;
        WSACleanup();
        wsa = false;
        throw;
    }
}

Sdk::~Sdk() {
    if (socket != INVALID_SOCKET)
        closesocket(socket);
    if (wsa)
        WSACleanup();
}

void Sdk::send(uint32_t controller, uint32_t id, std::span<const uint8_t> body) {
    if (invalid)
        throw std::runtime_error("SDK session invalidated; reconnect first");
    std::array<uint8_t, 16> header{'O', 'R', 'G', 'B'};
    put32(header, 4, controller);
    put32(header, 8, id);
    put32(header, 12, uint32_t(body.size()));
    transfer(socket, header.data(), header.size(), true);
    if (!body.empty())
        transfer(socket, const_cast<uint8_t *>(body.data()), body.size(), true);
}

Sdk::Message Sdk::receive() {
    std::array<uint8_t, 16> h{};
    transfer(socket, h.data(), h.size(), false);
    if (h[0] != 'O' || h[1] != 'R' || h[2] != 'G' || h[3] != 'B')
        throw std::runtime_error("Invalid OpenRGB signature");
    uint32_t n = u32(h, 12);
    if (n > 4 * 1024 * 1024)
        throw std::runtime_error("SDK packet exceeds size limit");
    Message m{u32(h, 4), u32(h, 8), Bytes(n)};
    if (n)
        transfer(socket, m.body.data(), n, false);
    return m;
}

Bytes Sdk::response(uint32_t controller, uint32_t id) {
    auto m = receive();
    if (m.id == 100) {
        invalid = true;
        throw std::runtime_error("OpenRGB device list changed during connection");
    }
    if (m.controller != controller || m.id != id)
        throw std::runtime_error("Unexpected SDK response");
    return std::move(m.body);
}

void Sdk::check() {
    if (invalid)
        throw std::runtime_error("OpenRGB: список пристроїв змінився; потрібне зіставлення");
    fd_set read;
    FD_ZERO(&read);
    FD_SET(socket, &read);
    timeval timeout{0, 0};
    int ready = select(0, &read, nullptr, nullptr, &timeout);
    if (ready < 0)
        throw std::runtime_error(socketError("Socket state"));
    if (!ready)
        return;
    u_long available = 0;
    ioctlsocket(socket, FIONREAD, &available);
    if (!available) {
        invalid = true;
        throw std::runtime_error("OpenRGB SDK від’єднано");
    }
    auto m = receive();
    invalid = true;
    throw std::runtime_error(m.id == 100 ? "OpenRGB: пристрої змінилися, виконується повторне зіставлення"
                                         : "Unexpected SDK notification");
}

class Reader {
    const Bytes &bytes;
    size_t pos = 0;

  public:
    explicit Reader(const Bytes &b) : bytes(b) {}

    void skip(size_t n) {
        if (n > bytes.size() - pos)
            throw std::runtime_error("Truncated controller data");
        pos += n;
    }

    uint32_t number() {
        auto result = u32(bytes, pos);
        skip(4);
        return result;
    }

    size_t count(size_t max) {
        if (pos + 2 > bytes.size())
            throw std::runtime_error("Truncated SDK count");
        size_t n = size_t(bytes[pos]) | (size_t(bytes[pos + 1]) << 8);
        skip(2);
        if (n > max)
            throw std::runtime_error("SDK count exceeds limit");
        return n;
    }

    std::string text() {
        size_t n = count(4096);
        if (!n)
            throw std::runtime_error("Empty SDK string envelope");
        size_t start = pos;
        skip(n);
        if (bytes[pos - 1] != 0)
            throw std::runtime_error("Unterminated SDK string");
        std::string out(reinterpret_cast<const char *>(bytes.data() + start), n - 1);
        (void)wide(out);
        return out;
    }

    bool end() const {
        return pos == bytes.size();
    }
};

static std::pair<std::vector<Descriptor>, Json> decodeController(const Bytes &bytes, uint32_t index) {
    Reader r(bytes);
    if (r.number() != bytes.size())
        throw std::runtime_error("SDK controller length mismatch");
    uint32_t type = r.number();
    std::string name = r.text(), vendor = r.text();
    std::string description = r.text(), version = r.text();
    std::string serial = r.text(), location = r.text();
    size_t modes = r.count(512);
    auto activeMode = r.number();
    Json modeInfo = Json::array();
    for (size_t i = 0; i < modes; i++) {
        auto modeName = r.text();
        auto value = r.number(), flags = r.number();
        r.skip(40);
        r.skip(r.count(4096) * 4);
        modeInfo.push_back({{"Name", modeName}, {"Value", value}, {"Flags", flags}});
    }
    nlohmann::ordered_json identity = {
        {"type", type}, {"vendor", vendor}, {"name", name}, {"serial", serial}, {"location", location}};
    std::string key = hash(identity.dump(-1, ' ', true)).substr(0, 24);
    std::vector<Descriptor> result;
    Json zoneInfo = Json::array();
    size_t zones = r.count(256);
    for (size_t z = 0; z < zones; z++) {
        std::string zone = r.text();
        uint32_t zoneType = r.number();
        auto min = r.number(), max = r.number();
        uint32_t count = r.number();
        if (count > 4096)
            throw std::runtime_error("SDK zone exceeds 4096 LEDs");
        r.skip(r.count(65535));
        nlohmann::ordered_json zoneIdentity = {{"zone", zone}, {"zoneType", zoneType}};
        auto zoneId = "openrgb:" + key + ":" + hash(zoneIdentity.dump(-1, ' ', true)).substr(0, 16);
        zoneInfo.push_back({{"Id", zoneId},
                            {"Index", z},
                            {"Name", zone},
                            {"Type", zoneType},
                            {"MinLeds", min},
                            {"MaxLeds", max},
                            {"LedCount", count}});
        if (!count)
            continue;
        Descriptor d;
        d.controllerKey = key;
        d.id = zoneId;
        d.name = name + " · " + zone;
        d.count = d.maxLeds = int(count);
        d.controller = index;
        d.controllerType = int(type);
        d.zone = int(z);
        d.configurable = false;
        d.single = count == 1;
        result.push_back(d);
    }
    size_t leds = r.count(4096);
    for (size_t i = 0; i < leds; i++) {
        r.text();
        r.number();
    }
    auto colorCount = r.count(4096);
    Json colors = Json::array();
    for (size_t i = 0; i < colorCount; i++) {
        auto color = r.number();
        colors.push_back(hexColor({uint8_t(color), uint8_t(color >> 8), uint8_t(color >> 16)}));
    }
    if (!r.end())
        throw std::runtime_error("Unexpected data after SDK v3 description");
    std::set<std::string> ids;
    for (auto &d : result)
        if (!ids.insert(d.id).second)
            throw std::runtime_error("OpenRGB zones have ambiguous identities");
    return {result,
            {{"Index", index},
             {"Name", name},
             {"Vendor", vendor},
             {"Type", type},
             {"Description", description},
             {"Version", version},
             {"Serial", serial},
             {"Location", location},
             {"ActiveMode", activeMode},
             {"Modes", modeInfo},
             {"Zones", zoneInfo},
             {"Colors", colors}}};
}

std::vector<Descriptor> Sdk::parseController(const Bytes &bytes, uint32_t index) {
    return decodeController(bytes, index).first;
}

Json Sdk::controllerInfo(uint32_t index) {
    send(index, 1, integer(3));
    return decodeController(response(index, 1), index).second;
}

Json Sdk::inspect() {
    send(0, 0, {});
    auto data = response(0, 0);
    if (data.size() != 4 || u32(data) > 128)
        throw std::runtime_error("Invalid SDK controller count");
    Json controllers = Json::array();
    auto deadline = Clock::now() + std::chrono::seconds(15);
    for (uint32_t i = 0; i < u32(data); i++) {
        if (Clock::now() > deadline)
            throw std::runtime_error("SDK inspection deadline exceeded");
        controllers.push_back(controllerInfo(i));
    }
    return controllers;
}

void Sdk::resize(const std::string &zoneId, int count) {
    // Setup-only operation: resolve the current controller by identity, never a cached index.
    auto controllers = inspect();
    const Json *selectedController = nullptr, *selectedZone = nullptr;
    for (auto &controller : controllers)
        for (auto &zone : controller["Zones"])
            if (zone["Id"] == zoneId) {
                if (selectedZone)
                    throw std::runtime_error("Ambiguous OpenRGB zone identity");
                selectedController = &controller;
                selectedZone = &zone;
            }
    if (!selectedZone)
        throw std::runtime_error("OpenRGB zone not found");
    if (count < 0 || count > 4096 || count < selectedZone->at("MinLeds").get<int>() ||
        count > selectedZone->at("MaxLeds").get<int>() ||
        selectedZone->at("MinLeds") == selectedZone->at("MaxLeds"))
        throw std::runtime_error("OpenRGB LED count outside configurable zone limits");
    Bytes body(8);
    put32(body, 0, selectedZone->at("Index").get<uint32_t>());
    put32(body, 4, uint32_t(count));
    check();
    send(selectedController->at("Index").get<uint32_t>(), 1000, body);
    // Resize broadcasts a list-change notification. All indices and zone lengths must be rebound.
    invalid = true;
}

std::vector<Descriptor> Sdk::discover(std::function<void(uint32_t, uint32_t)> progress) {
    send(0, 0, {});
    auto data = response(0, 0);
    if (data.size() != 4 || u32(data) > 128)
        throw std::runtime_error("Invalid SDK controller count");
    uint32_t count = u32(data);
    if (progress)
        progress(0, count);
    std::vector<Descriptor> out;
    auto deadline = Clock::now() + std::chrono::seconds(15);
    for (uint32_t i = 0; i < count; i++) {
        if (Clock::now() > deadline)
            throw std::runtime_error("SDK discovery deadline exceeded");
        send(i, 1, integer(3));
        auto zones = parseController(response(i, 1), i);
        out.insert(out.end(), zones.begin(), zones.end());
        if (progress)
            progress(i + 1, count);
        if (out.size() > 256)
            throw std::runtime_error("Too many SDK zones");
    }
    std::set<std::string> ids;
    for (auto &d : out)
        if (!ids.insert(d.id).second)
            throw std::runtime_error("OpenRGB devices have ambiguous identities");
    return out;
}

void Sdk::custom(uint32_t controller) {
    if (customControllers.contains(controller))
        return;
    check();
    send(controller, 1100, {});
    customControllers.insert(controller);
}

Bytes Sdk::zonePacket(int zone, std::span<const uint8_t> rgb) {
    if (zone < 0 || rgb.empty() || rgb.size() > 12288 || rgb.size() % 3)
        throw std::runtime_error("Invalid SDK zone frame");
    Bytes body(10 + rgb.size() / 3 * 4);
    put32(body, 0, uint32_t(body.size()));
    put32(body, 4, uint32_t(zone));
    uint16_t count = uint16_t(rgb.size() / 3);
    body[8] = uint8_t(count);
    body[9] = uint8_t(count >> 8);
    for (size_t i = 0; i < count; i++)
        std::copy_n(rgb.begin() + i * 3, 3, body.begin() + 10 + i * 4);
    return body;
}

void Sdk::frame(const Descriptor &d, std::span<const uint8_t> rgb) {
    if (rgb.size() != size_t(d.count) * 3)
        throw std::runtime_error("OpenRGB zone length changed");
    check();
    send(d.controller, 1051, zonePacket(d.zone, rgb));
}
} // namespace vlr

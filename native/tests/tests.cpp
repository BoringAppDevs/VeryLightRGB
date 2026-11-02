#include "core.hpp"
#include "sdk.hpp"
#include "engine.hpp"
#include "ipc.hpp"
#include "startup.hpp"
#include "tui.hpp"
#include "localization.hpp"
#include <iostream>
#include <cstring>
using namespace vlr;

namespace vlr {
int hardwareTests();
int openRgbTests(int port, bool hardware);
int driverTests();
} // namespace vlr

static int checks = 0;

static void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
    checks++;
}

template <class F> static void reject(F f, const std::string &message) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}

static uint32_t get32(std::span<const uint8_t> b, size_t offset = 0) {
    if (offset + 4 > b.size())
        throw std::runtime_error("Short integer");
    return uint32_t(b[offset]) | (uint32_t(b[offset + 1]) << 8) | (uint32_t(b[offset + 2]) << 16) |
           (uint32_t(b[offset + 3]) << 24);
}

static void add32(Bytes &b, uint32_t n) {
    for (int i = 0; i < 4; i++)
        b.push_back(uint8_t(n >> (i * 8)));
}

static void add16(Bytes &b, uint16_t n) {
    b.push_back(uint8_t(n));
    b.push_back(uint8_t(n >> 8));
}

static void text(Bytes &b, const std::string &s) {
    add16(b, uint16_t(s.size() + 1));
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

static Bytes controller(bool reverse = false) {
    Bytes b;
    add32(b, 0);
    add32(b, 0);
    text(b, "Fixture controller");
    text(b, "Fixture vendor");
    text(b, "Test only");
    text(b, "1");
    text(b, "abc123");
    text(b, "USB:fixture");
    add16(b, 1);
    add32(b, 0);
    text(b, "Direct");
    b.resize(b.size() + 48);
    add16(b, 0);
    add16(b, 2);
    for (int z = 0; z < 2; z++) {
        int i = reverse ? 1 - z : z;
        text(b, i == 0 ? "ARGB" : "GPU");
        add32(b, 1);
        add32(b, 1);
        add32(b, 120);
        add32(b, i == 0 ? 12 : 1);
        add16(b, 0);
    }
    add16(b, 13);
    for (int i = 0; i < 13; i++) {
        text(b, "LED " + std::to_string(i));
        add32(b, uint32_t(i));
    }
    add16(b, 13);
    b.resize(b.size() + 13 * 4);
    uint32_t size = uint32_t(b.size());
    for (int i = 0; i < 4; i++)
        b[size_t(i)] = uint8_t(size >> (8 * i));
    return b;
}

class Server {
    SOCKET listener = INVALID_SOCKET;
    std::atomic<SOCKET> peer{INVALID_SOCKET};
    std::atomic<bool> stopping{false};
    std::thread worker;
    mutable std::mutex mutex;
    std::vector<std::pair<int, Bytes>> frames;
    Bytes description;
    bool notify = false, bad = false;
    int descriptionDelay = 0;

    static void io(SOCKET s, uint8_t *data, size_t n, bool write) {
        while (n) {
            int done = write ? send(s, reinterpret_cast<char *>(data), int(n), 0)
                             : recv(s, reinterpret_cast<char *>(data), int(n), 0);
            if (done <= 0)
                throw std::runtime_error("Test peer disconnected");
            n -= size_t(done);
            data += done;
        }
    }

    void packet(SOCKET s, uint32_t device, uint32_t id, const Bytes &bytes) {
        Bytes header = {'O', 'R', 'G', 'B'};
        if (bad)
            header[0] = 'X';
        add32(header, device);
        add32(header, id);
        add32(header, uint32_t(bytes.size()));
        for (size_t start = 0; start < header.size(); start += 3)
            io(s, header.data() + start, std::min<size_t>(3, header.size() - start), true);
        for (size_t start = 0; start < bytes.size(); start += 7)
            io(s, const_cast<uint8_t *>(bytes.data()) + start, std::min<size_t>(7, bytes.size() - start),
               true);
    }

    void run() noexcept {
        while (!stopping) {
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET)
                break;
            peer = s;
            DWORD timeout = 2000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
            BOOL delay = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&delay), sizeof(delay));
            try {
                while (!stopping) {
                    Bytes header(16);
                    io(s, header.data(), header.size(), false);
                    uint32_t device = get32(header, 4), id = get32(header, 8), size = get32(header, 12);
                    if (size > 1024 * 1024)
                        throw std::runtime_error("Oversized test message");
                    Bytes body(size);
                    if (size)
                        io(s, body.data(), size, false);
                    if (id == 40) {
                        Bytes version;
                        add32(version, 6);
                        packet(s, device, id, version);
                    } else if (id == 0) {
                        Bytes count;
                        add32(count, 1);
                        packet(s, device, id, count);
                    } else if (id == 1) {
                        if (descriptionDelay)
                            Sleep(DWORD(descriptionDelay));
                        packet(s, device, id, description);
                    } else if (id == 50) {
                    } else if (id == 1100)
                        customs++;
                    else if (id == 1051) {
                        {
                            std::lock_guard lock(mutex);
                            frames.emplace_back(int(get32(body, 4)), body);
                        }
                        if (notify) {
                            packet(s, 0, 100, {});
                            notify = false;
                        }
                    } else
                        otherWrites++;
                }
            } catch (const std::exception &) {
            }
            peer = INVALID_SOCKET;
            closesocket(s);
        }
    }

  public:
    int port = 0;
    std::atomic<int> customs{0}, otherWrites{0};

    explicit Server(bool notification = false, bool malformed = false, int delay = 0)
        : description(controller()), notify(notification), bad(malformed), descriptionDelay(delay) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Test Winsock failed");
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(listener, 4))
            throw std::runtime_error("Test listen failed");
        int size = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size);
        port = ntohs(address.sin_port);
        worker = std::thread(&Server::run, this);
    }

    ~Server() {
        stopping = true;
        shutdown(listener, SD_BOTH);
        closesocket(listener);
        auto s = peer.load();
        if (s != INVALID_SOCKET)
            shutdown(s, SD_BOTH);
        if (worker.joinable())
            worker.join();
        WSACleanup();
    }

    int count(int zone = -1) const {
        std::lock_guard lock(mutex);
        return int(std::count_if(frames.begin(), frames.end(),
                                 [&](auto &f) { return zone < 0 || f.first == zone; }));
    }

    Bytes last(int zone) const {
        std::lock_guard lock(mutex);
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (it->first == zone)
                return it->second;
        return {};
    }

    bool waitFrames(int n, int zone = -1) const {
        auto deadline = Clock::now() + std::chrono::seconds(3);
        while (Clock::now() < deadline) {
            if (count(zone) >= n)
                return true;
            Sleep(5);
        }
        return false;
    }
};

static void models() {
    check(hash("abc") == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
          "SHA256 mismatch");
    check(parseColor("#aB12FF") == Rgb({171, 18, 255}), "HEX parsing mismatch");
    reject([] { parseColor("#00000g"); }, "Invalid HEX accepted");
    reject([] { parseColor("1234567"); }, "Missing hash accepted");
    auto c = Config::from(parseJson(
        R"({"Color":"#27C8FF","Brightness":80,"Effect":2,"LedCount":12,"StepMilliseconds":200,"Profiles":[]})"));
    auto h = headerDescriptor(0), g = gpuDescriptor();
    c.catalog = {h, g};
    c.device(h).managed = true;
    c.device(g).useGlobal = false;
    c.device(g).style.color = "#FF0000";
    auto p = plans(c);
    check(p.size() == 2 && p[0].style.color == "#27C8FF" && p[1].style.color == "#FF0000",
          "Independent styles lost");
    c.global.color = "#FFFFFF";
    check(p[0].style.color == "#27C8FF", "Render plan is mutable");
    c.global.enabled = false;
    p = plans(c);
    check(std::all_of(p.begin(), p.end(), [](auto &x) { return !x.style.enabled && !x.animated(); }),
          "Master off did not disable independent output");
    c.global.enabled = true;
    c.device(g).managed = false;
    check(plans(c).size() == 1, "Unmanaged output remains in plan");
    check(!c.device(headerDescriptor(1)).managed, "An empty available header was automatically selected");
    check(c.json()["SchemaVersion"] == 3 && !c.json().contains("Profiles"),
          "Profile removal or schema migration failed");
    auto roundtrip = Config::from(c.json());
    check(c.language == "en" && roundtrip.language == "en", "Default interface language is not English");
    auto translated = c.json();
    translated["Language"] = "uk";
    check(Config::from(translated).json()["Language"] == "uk", "Ukrainian language did not roundtrip");
    translated["Language"] = "xx";
    reject([&] { Config::from(translated); }, "Unknown interface language accepted");
    check(roundtrip.catalog[0].id == h.id && roundtrip.find(g.id)->style.color == "#FF0000",
          "Catalog/settings roundtrip failed");
    reject([] { Config::from(parseJson(R"({"Devices":null})")); }, "Null devices accepted");
    reject([] { Config::from(parseJson(R"({"Brightness":101})")); }, "Bad brightness accepted");
    reject([] { Config::from(parseJson(R"({"FramesPerSecond":1000})")); }, "Bad frame rate accepted");
    reject([] { parseJson(std::string(MaxJson + 1, ' ')); }, "Oversized JSON accepted");
    auto invalid = c;
    invalid.catalog[0].id = "native:other";
    reject([&] { invalid.validate(); }, "Unknown native driver accepted");
    check(supportedGpu(0x2C0210DE, 0xF32310B0) && !supportedGpu(0x2C0210DE, 0x111110B0),
          "GPU allowlist too broad");
    auto xml = startupXml(L"C:\\Apps & Tools\\VeryLightRGB.exe", L"S-1-5-21-1", L"C:\\Data & Settings");
    check(xml.find(L"Apps &amp; Tools") != std::wstring::npos &&
              xml.find(L"--data-dir &quot;") != std::wstring::npos,
          "Startup XML escaping failed");
    check(xml.find(L"<LogonType>InteractiveToken</LogonType>") != std::wstring::npos &&
              xml.find(L"<RunLevel>LeastPrivilege</RunLevel>") != std::wstring::npos,
          "Startup lost interactive least-privilege session");
    check(xml.find(L"<Interval>PT1M</Interval><Count>3</Count>") != std::wstring::npos &&
              xml.find(L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::wstring::npos,
          "Startup watchdog/runtime limit incorrect");
    check(xml.find(L"<EventTrigger>") != std::wstring::npos &&
              xml.find(L"EventID=2001") != std::wstring::npos &&
              xml.find(L"EventID=1000") != std::wstring::npos &&
              xml.find(L"Apps &amp;amp; Tools") != std::wstring::npos &&
              xml.find(L"<Delay>PT5S</Delay>") != std::wstring::npos,
          "Crash recovery query missing or incorrectly escaped");
}

static void reports() {
    Bytes before(290);
    before[0] = 0x50;
    before[1] = before[17] = before[33] = 9;
    before[16] = 12;
    before[32] = 16;
    before[48] = 24;
    before[170] = 123;
    auto steady = zoneConfig(before, 1, {10, 20, 30});
    for (int i = 0; i < 289; i++)
        if (i < 17 || i > 32)
            check(steady[size_t(i)] == before[size_t(i)], "Unselected MSI zone modified");
    check(steady[17] == 2 && steady[32] == 16 && steady[289] == 0, "Static mode length/flash mismatch");
    auto direct = zoneConfig(before, 0, {}, 12);
    check(direct[1] == 9 && direct[16] == 12 && direct[14] == 3 && direct[15] == 0x15 && direct[32] == 16,
          "Direct mode encoding mismatch");
    auto packet = framePacket(0, Bytes(36, 5));
    check(packet.size() == 727 && packet[0] == 0x51 && packet[3] == 0 && packet[6] == 12 && packet[7] == 5 &&
              packet[43] == 0,
          "MSI frame envelope mismatch");
    reject([&] { zoneConfig(before, 3, {}); }, "Invalid header accepted");
    reject([&] { framePacket(1, Bytes(361)); }, "Invalid native LED count accepted");
}

static void effects() {
    for (int n : {1, 12, 16, 200, 4096})
        for (int effect = 0; effect < EffectCount; effect++) {
            auto d = headerDescriptor(0);
            d.count = n;
            d.maxLeds = 4096;
            d.single = n == 1;
            d.configurable = false;
            Plan p;
            p.device = d;
            p.count = n;
            p.style.color = "#C65CFF";
            p.style.secondary = "#27C8FF";
            p.style.brightness = 80;
            p.style.effect = effect;
            p.style.step = 200;
            Renderer renderer(p);
            for (double ms : {0.0, 100.0, 200.0, 300.0, 500.0, 10000.0})
                check(renderer.render(ms).size() == size_t(n) * 3, "Invalid rendered frame size");
            p.style.enabled = false;
            Renderer off(p);
            auto bytes = off.render(500);
            check(std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0; }),
                  "Disabled renderer still emits light");
        }
    Plan p;
    p.device = headerDescriptor(0);
    p.style.color = "#FFFFFF";
    p.style.brightness = 100;
    p.style.effect = 2;
    p.style.step = 200;
    Renderer r(p);
    auto initial = r.render(0);
    check(std::count(initial.begin(), initial.end(), uint8_t(0)) == 12,
          "Band width must initially be four LEDs");
    for (double ms = 0; ms < 2400; ms += 25) {
        auto f = r.render(ms);
        for (int k = 0; k < 3; k++) {
            int total = 0;
            for (int i = 0; i < 12; i++)
                total += f[size_t(i) * 3 + size_t(k)];
            check(total == 8 * 255, "Circular transition changes total brightness");
        }
    }
    check(r.render(2400) == initial, "Circular wrap is discontinuous");
    p.style.effect = 4;
    p.style.brightness = 0;
    p.style.color = "#FF0000";
    p.style.step = 100;
    Renderer a(p);
    auto dark = a.render(1200);
    check(!p.animated() && std::all_of(dark.begin(), dark.end(), [](auto v) { return v == 0; }),
          "Zero-brightness rainbow emits light or keeps animating");
    p.style.brightness = 100;
    Renderer full(p);
    auto rainbow = full.render(1200);
    p.style.color = "#00FF00";
    p.style.step = 1000;
    Renderer b(p);
    check(b.render(1200) == rainbow, "Rainbow uses hidden style parameters");
    p.style.brightness = 25;
    Renderer dim(p);
    auto quarter = dim.render(1200);
    for (size_t i = 0; i < quarter.size(); i++)
        check(std::abs(int(quarter[i]) * 4 - int(rainbow[i])) <= 2,
              "Rainbow brightness does not scale its color channels");
    for (int effect = 6; effect < EffectCount; effect++) {
        p.style.effect = effect;
        p.style.step = 200;
        p.style.brightness = 100;
        p.style.color = "#FFFFFF";
        p.style.secondary = "#000000";
        Renderer animated(p);
        const auto start = animated.render(0);
        check(animated.render(700) != start, "New effect does not change over time");
        const double period = effect == 6 ? p.count * p.style.step : 20.0 * p.style.step;
        const auto wrapped = animated.render(period);
        check(std::equal(start.begin(), start.end(), wrapped.begin(),
                         [](auto a, auto b) { return std::abs(int(a) - int(b)) <= 1; }),
              "New effect does not wrap continuously");
        p.style.brightness = 0;
        Renderer zero(p);
        auto black = zero.render(700);
        check(!p.animated() && std::all_of(black.begin(), black.end(), [](auto v) { return v == 0; }),
              "New effect ignores zero brightness");
        check(Style::from(p.style.json()).effect == effect, "New effect ID does not round-trip");
    }
    auto fixtures = parseJson(readFile("native/tests/render-fixtures.json"));
    for (auto &f : fixtures) {
        Plan plan;
        plan.device = headerDescriptor(0);
        plan.count = f["Count"];
        plan.device.single = plan.count == 1;
        plan.style.color = "#C65CFF";
        plan.style.secondary = "#27C8FF";
        plan.style.brightness = 80;
        plan.style.effect = f["Effect"];
        // Legacy rainbow fixtures encode its former fixed 50% intensity.
        if (plan.style.effect == 4) plan.style.brightness = 50;
        plan.style.step = 200;
        plan.reverse = f["Reverse"];
        plan.offset = f["Offset"];
        Renderer renderer(plan);
        auto actual = renderer.render(f["Ms"].get<double>());
        std::string expected = f["Rgb"];
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        for (auto x : actual) {
            out += hex[x >> 4];
            out += hex[x & 15];
        }
        check(out == expected, "C# frame mismatch: effect=" + std::to_string(plan.style.effect) +
                                   ", count=" + std::to_string(plan.count) + ", ms=" + f["Ms"].dump() +
                                   ", expected=" + expected.substr(0, 72) + ", actual=" + out.substr(0, 72));
    }
}

static void sdk() {
    auto parsed = Sdk::parseController(controller(), 0),
         reordered = Sdk::parseController(controller(true), 7);
    check(parsed.size() == 2 && parsed[0].count == 12 && parsed[1].count == 1, "SDK v3 parsing failed");
    check(parsed[0].id == reordered[1].id && parsed[1].id == reordered[0].id && reordered[1].controller == 7,
          "Cached identities depend on live indexes");
    auto truncated = controller();
    truncated.pop_back();
    reject([&] { Sdk::parseController(truncated, 0); }, "Truncated SDK data accepted");
    auto body = Sdk::zonePacket(2, Bytes{10, 20, 30, 40, 50, 60});
    check(body.size() == 18 && get32(body) == 18 && get32(body, 4) == 2 && body[10] == 10 && body[13] == 0 &&
              body[14] == 40,
          "SDK zone RGBx encoding failed");
    reject([] { Sdk::zonePacket(0, Bytes(4)); }, "Partial SDK RGB tuple accepted");
    {
        Server server;
        Sdk client(server.port);
        auto devices = client.discover();
        auto info = client.inspect();
        check(info.size() == 1 && info[0]["Zones"].size() == 2 && info[0]["Colors"].size() == 13 &&
                  info[0]["Zones"][0]["Id"] == devices[0].id,
              "SDK inspection lost controller, zone identity or colors");
        client.custom(0);
        client.frame(devices[0], Bytes(36));
        client.frame(devices[1], Bytes(3));
        check(server.waitFrames(2) && server.customs == 1 && server.otherWrites == 0,
              "SDK transmitted unexpected commands or lost fragmented data");
    }
    {
        Server server;
        Sdk client(server.port);
        auto devices = client.discover();
        reject([&] { client.resize(devices[0].id, 121); }, "Oversized SDK resize accepted");
        reject([&] { client.resize("openrgb:missing", 12); }, "Unknown SDK resize identity accepted");
        check(server.otherWrites == 0, "Invalid resize wrote to controller");
        client.resize(devices[0].id, 16);
        reject([&] { client.frame(devices[0], Bytes(36)); }, "SDK session reused after resize");
    }
    {
        Server server(true);
        Sdk client(server.port);
        auto devices = client.discover();
        client.frame(devices[0], Bytes(36));
        check(server.waitFrames(1), "Fake server missed frame");
        Sleep(50);
        reject([&] { client.frame(devices[0], Bytes(36)); }, "Hotplug did not invalidate SDK indexes");
        reject([&] { client.frame(devices[0], Bytes(36)); }, "Invalid SDK session reused stale indexes");
        check(server.count() == 1, "Frames written after SDK invalidation");
    }
    {
        Server server(false, true);
        reject([&] { Sdk client(server.port); }, "Invalid SDK signature accepted");
    }
}

static void engine() {
    Server server;
    Config c;
    c.backend = 1;
    c.port = server.port;
    c.catalog = Sdk::parseController(controller(), 0);
    for (auto &d : c.catalog)
        c.device(d);
    c.global.color = "#FF0000";
    c.global.brightness = 100;
    c.global.effect = 0;
    c.devices[1].useGlobal = false;
    c.devices[1].style.color = "#0000FF";
    c.devices[1].style.brightness = 100;
    c.devices[1].style.effect = 3;
    c.devices[1].style.step = 100;
    Engine engine(c, false);
    check(engine.waitReady(3000), "SDK test engine failed to initialize");
    check(server.waitFrames(4, 1), "Independent animation did not run");
    check(server.count(0) == 1, "Static device was rewritten by animated neighbor");
    auto state = engine.snapshot();
    check(state["Animating"] == true && state["Status"].size() == 2, "Incorrect live status");
    c.global.enabled = false;
    engine.update(c, state["Revision"]);
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < deadline && engine.snapshot()["Animating"].get<bool>())
        Sleep(5);
    check(!engine.snapshot()["Animating"].get<bool>(), "Master off did not stop independent animation");
    check(server.waitFrames(2, 0), "Master off missed static zone");
    auto black = server.last(1);
    check(black.size() == 14 && black[10] == 0 && black[11] == 0 && black[12] == 0,
          "Master off left independent device illuminated");
    reject([&] { engine.update(c, state["Revision"]); }, "Stale UI revision overwrote settings");
    auto before = Clock::now();
    engine.stop();
    engine.stop();
    check(Clock::now() - before < std::chrono::seconds(1), "Repeated stop is unsafe or slow");
    Server partialServer;
    auto partial = c;
    partial.port = partialServer.port;
    partial.global.enabled = true;
    partial.devices[0].id = "openrgb:missing";
    partial.catalog[0].id = "openrgb:missing";
    Engine running(partial, false);
    check(running.waitReady(3000), "Partial test startup timeout");
    check(partialServer.waitFrames(3, 1), "A missing device stopped a healthy output");
    auto status = running.snapshot()["Status"];
    check(std::count_if(status.begin(), status.end(),
                        [](auto &s) { return s.at("Success").template get<bool>(); }) == 1,
          "Partial failure status incorrect");
    running.stop();
}

static void menus() {
    Config c;
    c.language = "uk";
    c.catalog = {headerDescriptor(0), gpuDescriptor()};
    for (auto &d : c.catalog)
        c.device(d);
    c.devices[0].managed = true;
    Json status = {{"Config", c.json()}, {"Revision", 0}, {"Message", "Ready"}, {"Animating", false}};
    auto normal = menuSnapshot(status, 1);
    check(normal.find(L"MSI · ARGB 1") != std::wstring::npos &&
              normal.find(L"Кількість діодів") != std::wstring::npos,
          "Header label or visible LED count missing");
    check(normal.find(L"TL-K12") == std::wstring::npos && normal.find(L"Профіл") == std::wstring::npos,
          "Removed hardcoded fan names/profiles returned");
    check(normal.find(L"██████████") != std::wstring::npos && normal.find(L"50 %") != std::wstring::npos,
          "Brightness bar missing");
    check(normal.find(L"Esc назад") != std::wstring::npos &&
              normal.find(L"Esc закрити") == std::wstring::npos &&
              normal.find(L"[Q] Згорнути") != std::wstring::npos,
          "Back/hide hints are incorrect");
    auto red = colorMenuText(L"  #FF0000  ", true);
    check(red.find(L"\x1b[38;2;255;0;0m#FF0000") != std::wstring::npos &&
              red.find(L"\x1b[48;2;35;72;65m\x1b[97m") != std::wstring::npos,
          "HEX palette color or selected-row restoration incorrect");
    check(colorMenuText(L"#000000").find(L"\x1b[48;2;224;228;235m") != std::wstring::npos,
          "Black HEX text has no readable background");
    check(colorMenuText(L"HEX #RRGGBB") == L"HEX #RRGGBB", "HEX placeholder was colorized");
    check(colorMenuText(L"#000000").find(L"   \x1b[48;2;0;0;0m        \x1b[0m ") != std::wstring::npos,
          "Black swatch is not shown at its actual color with plain spacing");
    auto plain = [](const std::wstring &styled) {
        std::wstring result;
        for (size_t i = 0; i < styled.size(); i++) {
            if (styled[i] == L'\x1b' && i + 1 < styled.size() && styled[i + 1] == L'[') {
                while (i < styled.size() && styled[i] != L'm')
                    i++;
            } else
                result += styled[i];
        }
        return result;
    };
    for (size_t width : {12, 19, 24, 58, 76, 112}) {
        auto swatches = plain(colorMenuText(L"#FF0000  Red / #00FFFF  Cyan", true, {}, width));
        check(swatches.size() == width && swatches.find(L"██") == std::wstring::npos,
              "Color swatches overflowed the row or were split by truncation");
    }
    check(brightnessBar(0).find(L'█') == std::wstring::npos &&
              brightnessBar(100).find(L'░') == std::wstring::npos,
          "Brightness endpoint bars are incorrect");
    check(stepBrightness(0, -1) == 0 && stepBrightness(100, 1) == 100 && stepBrightness(17, 1) == 20 &&
              stepBrightness(17, -1) == 15,
          "Brightness did not advance to the adjacent 5% preset");
    MenuNavigation navigation;
    check(navigation.page == 3 && !navigation.back(), "Menu does not open at the root screen");
    navigation.navigate(0);
    navigation.selected = 4;
    navigation.navigate(0, "native:msi:argb:0");
    navigation.selected = 6;
    navigation.navigate(2);
    check(navigation.back() && navigation.page == 0 && navigation.scope == "native:msi:argb:0" &&
              navigation.selected == 6,
          "Esc lost the previous device screen or selection");
    check(navigation.back() && navigation.scope.empty() && navigation.selected == 4,
          "Esc did not return from device to shared lighting");
    check(navigation.back() && navigation.page == 3 && !navigation.back(),
          "Esc at the top level does not remain in the root menu");
    auto home = menuSnapshot(status, 0, 3);
    check(home.find(L"ГОЛОВНЕ МЕНЮ") != std::wstring::npos &&
              home.find(L"Згорнути в tray") != std::wstring::npos,
          "Root navigation screen missing");
    check(home.find(L"Reload") == std::wstring::npos && home.find(L"[F5]") == std::wstring::npos &&
              home.find(L"[L]") == std::wstring::npos && home.find(L"[S]") == std::wstring::npos,
          "Root menu exposes redundant settings hotkeys");
    auto system = menuSnapshot(status, 0, 2);
    check(system.find(L"Reload devices") != std::wstring::npos && system.find(L"[R]") != std::wstring::npos,
          "Reload action or shortcut disappeared from settings");
    check(menuReloadAvailable(1) && menuReloadAvailable(2) && !menuReloadAvailable(0) &&
              !menuReloadAvailable(3),
          "Reload key routing includes the wrong pages");
    check(menuSnapshot(status, 0, 1).find(L"[R] Reload devices") != std::wstring::npos,
          "Devices page does not advertise the reload shortcut");
    status["Reload"] = {
        {"State", "running"}, {"Stage", "OpenRGB · контролер 1 з 3"}, {"Completed", 2}, {"Total", 4}};
    auto progress = menuSnapshot(status, 0, 2);
    check(progress.find(L"OpenRGB · контролер 1 з 3") != std::wstring::npos &&
              progress.find(L"██████░░░░░░") != std::wstring::npos,
          "Reload stage/progress missing");
    check(menuSnapshot(status, 0, 1).find(L"OpenRGB · контролер 1 з 3") != std::wstring::npos,
          "Devices page hides reload progress");
    status["Reload"] = {{"State", "error"}, {"Stage", "Помилка оновлення: SDK від’єднано"}};
    check(menuSnapshot(status, 0, 2).find(L"Помилка оновлення") != std::wstring::npos,
          "Reload error hidden in settings");
    auto gpu = menuSnapshot(status, 2);
    check(gpu.find(L"Кількість діодів") == std::wstring::npos, "GPU exposes fictional LED configuration");
    c.global.effect = 4;
    status["Config"] = c.json();
    auto rainbow = menuSnapshot(status);
    check(rainbow.find(L"Яскравість") != std::wstring::npos &&
              rainbow.find(L"Час кроку") == std::wstring::npos,
          "Rainbow brightness control is missing or unused speed is exposed");
    c.global.effect = 7;
    status["Config"] = c.json();
    check(menuSnapshot(status).find(L"Другий колір") != std::wstring::npos,
          "Wave does not expose its second color");
    c.catalog[0].name = "Keyboard\x1b[31m";
    status["Config"] = c.json();
    check(menuSnapshot(status, 1).find(L"\x1b[31m") == std::wstring::npos,
          "Device label injected terminal control sequences");
    c.catalog[0] = headerDescriptor(0);
    c.devices[0].useGlobal = c.devices[1].useGlobal = false;
    c.devices[0].style.effect = 1;
    c.devices[1].style.effect = 0;
    c.devices[0].style.color = "#64E6C6";
    c.devices[1].style.color = "#FFFFFF";
    status["Config"] = c.json();
    auto mixed = menuSnapshot(status);
    check(mixed.find(L"Різні ефекти") != std::wstring::npos &&
              mixed.find(L"Різні кольори") != std::wstring::npos &&
              mixed.find(L"Яскравість") != std::wstring::npos,
          "All-together screen displays an unused rainbow template instead of actual independent styles");
    check(menuAccent(c) == Rgb{100, 230, 198},
          "Accent does not use the first selected output's primary color");
    c.devices[0].managed = false;
    check(menuAccent(c) == Rgb{255, 255, 255}, "Unselected output still controls the accent");
    c.devices[1].managed = false;
    check(menuAccent(c) == parseColor(c.global.color), "Accent fallback differs from primary shared color");

    c.language = "en";
    c.catalog[0].name = "Мій ARGB header";
    status["Config"] = c.json();
    status["Message"] = "Підсвітка працює";
    auto english = menuSnapshot(status, 0, 3);
    check(english.find(L"MAIN MENU") != std::wstring::npos &&
              english.find(L"Language") != std::wstring::npos &&
              english.find(L"English") != std::wstring::npos &&
              english.find(L"Lighting is running") != std::wstring::npos &&
              english.find(L"ГОЛОВНЕ МЕНЮ") == std::wstring::npos,
          "English home or language switch is not translated");
    check(home.find(L"Мова") != std::wstring::npos && home.find(L"Українська") != std::wstring::npos,
          "Ukrainian home or language switch is not translated");
    check(menuSnapshot(status, 0, 1).find(L"Мій ARGB header") != std::wstring::npos,
          "Localization changed a hardware/user name");
    status["Reload"] = {{"State", "running"}, {"Stage", "OpenRGB · контролер 1 з 3"}};
    check(menuSnapshot(status, 0, 1).find(L"OpenRGB · controller 1 of 3") != std::wstring::npos,
          "English reload stage is not translated on Devices");
    status["Reload"] = {{"State", "error"}, {"Stage", "Помилка оновлення: SDK від’єднано"}};
    check(menuSnapshot(status, 0, 2).find(L"Reload failed: SDK disconnected") != std::wstring::npos,
          "English reload error is not translated");
    check(localize("en", L"Ввести свій HEX…") == L"Enter a custom HEX…" &&
              localize("uk", L"Ввести свій HEX…") == L"Ввести свій HEX…" &&
              localize("en", L"Гейській") == L"Gay" && localize("en", L"Відкрити меню…") == L"Open menu…",
          "Modal/effect/tray translations are incorrect");
    c.catalog[0] = headerDescriptor(0);
    for (auto &device : c.devices) {
        device.managed = true;
        device.useGlobal = true;
    }
    for (int effect = 0; effect < EffectCount; effect++) {
        c.global.effect = effect;
        status["Config"] = c.json();
        for (int page = 0; page < 4; page++) {
            auto screen = menuSnapshot(status, 0, page);
            check(std::none_of(screen.begin(), screen.end(),
                               [](wchar_t ch) { return ch >= 0x0400 && ch <= 0x04FF; }),
                  "English screen contains an untranslated application label");
        }
    }
}

static void lightingEditTests() {
    Config c;
    c.catalog = {headerDescriptor(0), headerDescriptor(1), gpuDescriptor()};
    for (auto &desc : c.catalog)
        c.device(desc);
    c.devices[0].managed = true;
    c.devices[0].useGlobal = false;
    c.devices[0].style.color = "#64E6C6";
    c.devices[0].style.effect = 1;
    c.devices[2].useGlobal = false;
    c.devices[2].style.color = "#FFFFFF";
    c.devices[2].style.effect = 0;
    auto untouched = c.devices[1].json();
    auto value = c.global;
    value.color = "#FF0000";
    editLighting(c, "", "color", value);
    auto output = plans(c);
    check(output.size() == 2 && output[0].style.color == "#FF0000" && output[1].style.color == "#FF0000",
          "All-together color ignored independently styled outputs");
    check(output[0].style.effect == 1 && output[1].style.effect == 0 && !c.devices[0].useGlobal &&
              !c.devices[2].useGlobal,
          "Color broadcast overwrote independent effects or inheritance");
    check(c.devices[1].json() == untouched, "All-together command changed an unselected header");
    value.effect = 3;
    editLighting(c, "", "effect", value);
    check(plans(c)[0].style.effect == 3 && plans(c)[1].style.effect == 3,
          "All-together effect ignored independent outputs");
    value.brightness = 25;
    editLighting(c, "", "brightness", value);
    check(plans(c)[0].style.brightness == 25 && plans(c)[1].style.brightness == 25,
          "All-together brightness ignored independent outputs");
    auto card = c.devices[2].json();
    c.devices[0].useGlobal = true;
    value.color = "#0000FF";
    editLighting(c, c.devices[0].id, "color", value);
    check(!c.devices[0].useGlobal && c.devices[0].hasCustom && plans(c)[0].style.color == "#0000FF" &&
              plans(c)[0].style.effect == c.global.effect && c.devices[2].json() == card,
          "Editing a linked zone did not detach it while preserving its effective style and neighbor");
    value.enabled = false;
    auto localEnabled = c.devices[0].style.enabled;
    editLighting(c, "", "power", value);
    check(!plans(c)[0].style.enabled && !plans(c)[1].style.enabled &&
              c.devices[0].style.enabled == localEnabled,
          "Master off changed local power settings or left an independent output on");
    c.devices[0].useGlobal = true;
    editLighting(c, c.devices[0].id, "color", value);
    check(c.devices[0].style.enabled && !c.global.enabled,
          "Editing a linked zone while master-off permanently disabled its local power");
    value.enabled = true;
    editLighting(c, "", "power", value);
    check(plans(c)[0].style.enabled && plans(c)[1].style.enabled, "Master-on did not restore edited zones");
    reject([&] { editLighting(c, c.devices[1].id, "color", value); },
           "Unselected header accepted scoped edit");
    reject([&] { editLighting(c, "", "unknown", value); }, "Unknown lighting edit accepted");

    Server server;
    Config live;
    live.backend = 1;
    live.port = server.port;
    live.catalog = Sdk::parseController(controller(), 0);
    for (auto &desc : live.catalog) {
        auto &device = live.device(desc);
        device.useGlobal = false;
        device.style.effect = 0;
        device.style.brightness = 100;
        device.style.color = "#0000FF";
    }
    Engine engine(live, false);
    check(engine.waitReady(3000), "Lighting edit transport test failed to start");
    auto state = engine.snapshot();
    auto edit = live.global;
    edit.color = "#FF0000";
    engine.editLighting("", "color", edit, state["Revision"]);
    check(server.waitFrames(2, 0) && server.waitFrames(2, 1),
          "All-together edit did not transmit to both zones");
    check(server.last(0)[10] == 255 && server.last(0)[12] == 0 && server.last(1)[10] == 255 &&
              server.last(1)[12] == 0,
          "Independent outputs did not receive changed color bytes");
    state = engine.snapshot();
    auto localized = Config::from(state["Config"]);
    localized.language = "uk";
    int sent = server.count();
    auto reloadId = state["Reload"]["Id"];
    engine.update(localized, state["Revision"]);
    Sleep(100);
    auto localizedState = engine.snapshot();
    check(localizedState["Config"]["Language"] == "uk" && server.count() == sent &&
              localizedState["Reload"]["Id"] == reloadId,
          "Changing language rewrote RGB frames or reloaded hardware");
    check(Config::load().language == "uk", "Language selection was not saved to disk");
    state = engine.snapshot();
    edit.effect = 1;
    engine.editLighting("", "effect", edit, state["Revision"]);
    check(server.waitFrames(4, 0) && server.waitFrames(4, 1),
          "Changed effect did not produce animated frames on both zones");
    check(engine.snapshot()["Animating"] == true, "Effect change did not start animation");
    reject([&] { engine.editLighting("", "color", edit, state["Revision"]); },
           "Stale lighting edit overwrote newer state");
    engine.stop();
}

static void reloadProgressTests() {
    Server server(false, false, 120);
    Config config;
    config.backend = 1;
    config.port = server.port;
    Engine running(config, false);
    check(running.waitReady(3000), "Initial SDK catalog refresh timed out");
    auto first = running.snapshot();
    check(first["Reload"]["State"] == "done" && first["Reload"]["Found"] == 2 &&
              first["Reload"]["Completed"] == first["Reload"]["Total"],
          "Initial refresh did not finish with the actual output count");
    auto id = first["Reload"]["Id"].get<uint64_t>();
    running.refresh();
    running.refresh();
    running.refresh();
    check(running.snapshot()["Reload"]["Id"] == id + 1, "Repeated reload queued duplicate scans");
    bool stageSeen = false, complete = false;
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline) {
        auto state = running.snapshot();
        auto reload = state["Reload"];
        if (reload["State"] == "running" &&
            reload["Stage"].get<std::string>().find("OpenRGB") != std::string::npos)
            stageSeen = true;
        if (reload["State"] == "done" && reload["Id"].get<uint64_t>() == id + 1) {
            complete = true;
            check(state["Config"] == first["Config"],
                  "Device reload overwrote existing styles or selections");
            break;
        }
        Sleep(5);
    }
    check(stageSeen, "Intermediate SDK reload stage was not observable");
    check(complete, "Reload progress remained busy after completion");
    running.stop();
    Server malformed(false, true);
    config.port = malformed.port;
    Engine failed(config, false);
    check(failed.waitReady(3000), "Failed reload did not become inspectable");
    auto error = failed.snapshot()["Reload"];
    check(error["State"] == "error" &&
              error["Stage"].get<std::string>().find("Помилка оновлення") != std::string::npos,
          "SDK failure was displayed as a successful or unfinished refresh");
    failed.stop();
}

static void pipes() {
    PipeServer server([](const Json &j) -> Json {
        if (j.value("fail", false))
            throw std::runtime_error("Expected rejection");
        return {{"Echo", j.at("Value")}};
    });
    check(rpc({{"Value", "Привіт"}})["Echo"] == "Привіт", "IPC Unicode roundtrip failed");
    reject([] { rpc({{"fail", true}}); }, "IPC server error was swallowed");
    check(rpc({{"Value", 42}})["Echo"] == 42, "IPC did not recover after rejected request");
    auto before = Clock::now();
    server.stop();
    check(Clock::now() - before < std::chrono::seconds(1), "Idle pipe server did not stop promptly");
}

int main(int argc, char **argv) {
    try {
        setDataDirectory((std::filesystem::current_path() / L"local" / L"native-test-state").wstring());
        setTestPipeNamespace(L"-Test-" + std::to_wstring(GetCurrentProcessId()));
        models();
        reports();
        effects();
        sdk();
        engine();
        menus();
        lightingEditTests();
        reloadProgressTests();
        pipes();
        checks += driverTests();
        bool hardware = argc > 1 && std::string(argv[1]) == "--hardware";
        if (hardware)
            checks += hardwareTests();
        bool realSdk =
            argc > 2 && (std::string(argv[1]) == "--openrgb" || std::string(argv[1]) == "--openrgb-hardware");
        if (realSdk)
            checks += openRgbTests(std::stoi(argv[2]), std::string(argv[1]) == "--openrgb-hardware");
        std::cout << "PASS: " << checks << " native assertions. "
                  << (hardware  ? "Including opt-in native hardware tests."
                      : realSdk ? "Including real OpenRGB SDK tests."
                                : "No RGB hardware accessed.")
                  << "\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "FAIL after " << checks << ": " << ex.what() << "\n";
        return 1;
    }
}

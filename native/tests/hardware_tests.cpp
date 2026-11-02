#include "hardware.hpp"
#include <iostream>

namespace vlr {
int hardwareTests() {
    int count = 0;
    auto check = [&](bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
        count++;
    };
    Handle agent(OpenMutexW(SYNCHRONIZE, FALSE, (L"Local\\VeryLightRGB.Application-" + userSid()).c_str()));
    if (agent)
        throw std::runtime_error("Exit the native background agent before hardware tests");
    checkConflicts();
    Handle lease(CreateMutexW(nullptr, FALSE, L"Local\\RgbTray.Hardware"));
    DWORD result = WaitForSingleObject(lease.h, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        throw std::runtime_error("RGB hardware is busy");

    struct Release {
        HANDLE h;

        ~Release() {
            ReleaseMutex(h);
        }
    } release{lease.h};

    Msi m;
    Gpu g;
    auto before = m.read(0x50, 290);
    Rgb original = {g.reg(0x6C), g.reg(0x6D), g.reg(0x6E)};
    auto originalMode = g.reg(0x60), originalControl = g.reg(0xE0), originalBrightness = g.reg(0x6F);
    auto restore = [&] {
        auto report = before;
        report.back() = 0;
        m.write(report);
        bool confirmed = false;
        for (int i = 0; i < 6 && !confirmed; i++) {
            Sleep(15);
            auto actual = m.read(0x50, 290);
            confirmed = std::equal(actual.begin() + 1, actual.begin() + 289, report.begin() + 1);
        }
        if (!confirmed)
            throw std::runtime_error("Could not restore MSI configuration");
        g.apply(original);
        if (originalMode != 1)
            g.reg(0x60, originalMode);
        if (originalControl != 0)
            g.reg(0xE0, originalControl);
        if (originalBrightness != 100)
            g.reg(0x6F, originalBrightness);
    };
    try {
        m.zone(0, {32, 64, 96});
        g.apply({96, 48, 32});
        auto report = m.read(0x50, 290);
        check(report[1] == 2 && report[2] == 32 && report[3] == 64 && report[4] == 96,
              "Native header static RGB mismatch");
        check(std::equal(report.begin() + 17, report.begin() + 289, before.begin() + 17),
              "Native static write changed unselected zones");
        check(g.reg(0x6C) == 96 && g.reg(0x6D) == 48 && g.reg(0x6E) == 32, "GPU independent RGB mismatch");
        m.zone(0, {});
        m.zone(0, {}, 12);
        Plan p;
        p.device = headerDescriptor(0);
        p.count = 12;
        p.style.color = "#1E3246";
        p.style.brightness = 100;
        p.style.effect = 2;
        p.style.step = 100;
        Renderer renderer(p);
        Bytes packet(727);
        for (int i = 0; i < 24; i++) {
            m.frame(0, renderer.render(i * 20), packet);
            Sleep(20);
            check(true, "Frame transfer failed");
        }
        report = m.read(0x50, 290);
        check(report[1] == 9 && report[16] == 12, "Native streaming mode/length mismatch");
        check(std::equal(report.begin() + 17, report.begin() + 289, before.begin() + 17),
              "Streaming modified unselected MSI zones");
        check(report.back() == 0, "Streaming enabled flash persistence");
        check(g.reg(0x6C) == 96 && g.reg(0x6D) == 48 && g.reg(0x6E) == 32,
              "Header animation changed static GPU color");
        m.zone(0, {});
        g.apply({});
        report = m.read(0x50, 290);
        check(report[1] == 2 &&
                  std::all_of(report.begin() + 2, report.begin() + 14, [](auto b) { return b == 0; }),
              "MSI off did not use steady black");
        check(g.reg(0x6C) == 0 && g.reg(0x6D) == 0 && g.reg(0x6E) == 0, "GPU off failed");
        restore();
        check(true, "Configuration restore failed");
    } catch (...) {
        try {
            restore();
        } catch (const std::exception &ex) {
            log(std::string("Hardware test restoration: ") + ex.what());
        }
        throw;
    }
    return count;
}
} // namespace vlr

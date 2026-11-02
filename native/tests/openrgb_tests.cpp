#include "sdk.hpp"
#include "hardware.hpp"
#include <iostream>

namespace vlr {
int openRgbTests(int port, bool hardware) {
    int checks = 0;
    auto check = [&](bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
        checks++;
    };
    Sdk sdk(port);
    auto controllers = sdk.inspect();
    auto devices = sdk.discover();
    check(controllers.is_array(), "Real OpenRGB controller enumeration failed");
    for (auto &d : devices) {
        auto current = sdk.controllerInfo(d.controller);
        check(current["Zones"].at(size_t(d.zone))["Id"] == d.id, "Real SDK identity binding failed");
        check(current["Zones"].at(size_t(d.zone))["LedCount"] == d.count, "Real SDK zone length changed");
    }
    Json report = {{"Controllers", controllers}, {"HardwareWrites", hardware}, {"Port", port}};
    if (hardware) {
        Handle app(OpenMutexW(SYNCHRONIZE, FALSE, (L"Local\\VeryLightRGB.Application-" + userSid()).c_str()));
        if (app)
            throw std::runtime_error("Stop the RGB agent before real SDK hardware tests");
        Handle lease(CreateMutexW(nullptr, FALSE, L"Local\\RgbTray.Hardware"));
        auto status = WaitForSingleObject(lease.h, 0);
        if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED)
            throw std::runtime_error("Hardware lease is busy");

        struct Release {
            HANDLE value;

            ~Release() {
                ReleaseMutex(value);
            }
        } release{lease.h};

        auto gpu = std::find_if(devices.begin(), devices.end(), [](auto &d) {
            return d.controllerType == 2 && d.name == "Gainward GeForce RTX 5080 Phoenix · GPU Zone" &&
                   d.count == 1;
        });
        auto board = std::find_if(devices.begin(), devices.end(), [](auto &d) {
            return d.controllerType == 0 && d.name == "MSI MYSTIC LIGHT · JARGB 1" && d.count == 12;
        });
        check(gpu != devices.end(), "Expected exact Gainward SDK controller missing");
        check(board != devices.end(), "Configure the verified MSI JARGB 1 SDK zone to 12 LEDs first");
        // These constructors independently validate the actual motherboard and PCI identities.
        Msi msi;
        Gpu card;
        auto before = msi.read(0x50, 290);
        Rgb original = {card.reg(0x6C), card.reg(0x6D), card.reg(0x6E)};
        auto mode = card.reg(0x60), control = card.reg(0xE0), brightness = card.reg(0x6F);
        auto restore = [&] {
            auto copy = before;
            copy.back() = 0;
            msi.write(copy);
            card.apply(original);
            card.reg(0x60, mode);
            card.reg(0xE0, control);
            card.reg(0x6F, brightness);
        };
        try {
            sdk.custom(gpu->controller);
            sdk.frame(*gpu, Bytes{32, 64, 96});
            Sleep(100);
            Rgb actual = {card.reg(0x6C), card.reg(0x6D), card.reg(0x6E)};
            check(actual == Rgb{32, 64, 96}, "OpenRGB GPU send did not reach physical RGB registers");
            report["GpuPhysicalColor"] = hexColor(actual);
            check(sdk.controllerInfo(gpu->controller)["Colors"][0] == "#204060",
                  "GPU SDK color cache mismatch");
            sdk.custom(board->controller);
            sdk.frame(*board, Bytes(36, 24));
            Sleep(100);
            auto config = msi.read(0x50, 290);
            report["MsiPhysicalMode"] = config[1];
            report["MsiPhysicalLength"] = config[16];
            check(config[1] == 9 && config[16] == 12, "OpenRGB MSI direct mode/length not confirmed");
            auto cached = sdk.controllerInfo(board->controller);
            check(cached["Colors"].size() == 12 && cached["Colors"][0] == "#181818",
                  "MSI SDK colors mismatch");
            Plan plan;
            plan.device = *board;
            plan.count = 12;
            plan.style.effect = 2;
            plan.style.color = "#FFFFFF";
            plan.style.brightness = 15;
            Renderer renderer(plan);
            for (int frame = 0; frame < 24; frame++) {
                sdk.frame(*board, renderer.render(frame * 30));
                Sleep(20);
            }
            check(card.reg(0x6C) == 32 && card.reg(0x6D) == 64 && card.reg(0x6E) == 96,
                  "SDK header animation changed the independent static GPU");
            report["AnimationFrames"] = 24;
            sdk.frame(*gpu, Bytes(3));
            sdk.frame(*board, Bytes(36));
            Sleep(100);
            check(card.reg(0x6C) == 0 && card.reg(0x6D) == 0 && card.reg(0x6E) == 0,
                  "Physical SDK GPU off failed");
            report["GpuPhysicalOff"] = true;
            restore();
            Sleep(80);
            auto after = msi.read(0x50, 290);
            check(std::equal(before.begin() + 1, before.end() - 1, after.begin() + 1),
                  "MSI config was not restored after SDK tests");
            report["Restored"] = true;
        } catch (...) {
            try {
                restore();
            } catch (...) {
            }
            throw;
        }
    }
    report["Assertions"] = checks;
    writeAtomic(dataDirectory() / L"openrgb-integration.json", report.dump(2));
    std::cout << "Real OpenRGB SDK: " << controllers.size() << " controllers, " << devices.size()
              << " non-empty zones; " << checks << " assertions.\n";
    return checks;
}
} // namespace vlr

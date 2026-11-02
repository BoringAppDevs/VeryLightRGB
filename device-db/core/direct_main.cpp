// SPDX-License-Identifier: GPL-2.0-or-later
// Server-free stdio entry point for the pinned Qt-free OpenRGB driver core.
#include <iostream>
#include <fstream>
#include <set>
#include <memory>
#include <stdexcept>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <io.h>
#include "ResourceManager.h"
#include "DetectionManager.h"
#include "SettingsManager.h"
#include "LogManager.h"
#include "RGBController.h"

class MemoryController : public RGBController {
  public:
    std::atomic<int> frames{0};
    explicit MemoryController(bool twoZones=false) {
        name = "Device DB memory test"; location = "memory";
        mode m{}; m.name = "Direct"; m.value = 1; m.flags = MODE_FLAG_HAS_PER_LED_COLOR; m.color_mode = MODE_COLORS_PER_LED;
        modes.push_back(m);
        zone z{}; z.name = "Test"; z.type = ZONE_TYPE_LINEAR; z.leds_count = 2; z.leds_min = 2; z.leds_max = 2;
        zones.push_back(z);
        if(twoZones) { z.name="Second"; zones.push_back(z); }
        leds.resize(twoZones?4:2); SetupColors();
    }
    ~MemoryController() { Shutdown(); }
    void DeviceUpdateLEDs() override { frames++; }
    void DeviceUpdateZoneLEDs(int) override {}
    void DeviceUpdateSingleLED(int) override {}
    void DeviceUpdateMode() override {}
};

class DeviceDbCore {
    DetectionManager *dm = DetectionManager::get();
    bool opened = false;
    bool hidStarted = false;
    HANDLE lease = nullptr;
    DWORD host = 0;
    json warnings = json::array();
    std::set<std::string> directInitialized;
    static std::string identity(RGBController *c) { return json::array({c->GetName(), c->GetLocation(), c->GetSerial()}).dump(); }
    static int directMode(RGBController *c) {
        for (unsigned int i = 0; i < c->GetModeCount(); i++)
            if ((c->GetModeFlags(i) & MODE_FLAG_HAS_PER_LED_COLOR) && c->GetModeColorMode(i) == MODE_COLORS_PER_LED) return static_cast<int>(i);
        return -1;
    }
    static bool direct(RGBController *c) { return directMode(c) >= 0; }
    static unsigned int number(const json &r, const char *key, unsigned int max) {
        const auto &v = r.at(key);
        if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<uint64_t>() > max) throw std::runtime_error(std::string("Invalid ") + key);
        return v.get<unsigned int>();
    }
    RGBController *controller(const std::string &id) {
        RGBController *found = nullptr;
        for (auto *c : dm->rgb_controllers) if (identity(c) == id) {
            if (found) throw std::runtime_error("Ambiguous controller identity; no writes sent");
            found = c;
        }
        if (!found) throw std::runtime_error("Controller absent; reopen explicitly");
        return found;
    }
    void conflicts() {
        const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot check competing RGB processes");
        std::string conflict;
        auto image=[](HANDLE p) {
            std::wstring result(32768,0); DWORD count=static_cast<DWORD>(result.size());
            if(!QueryFullProcessImageNameW(p,0,result.data(),&count)) return std::wstring();
            result.resize(count); return result;
        };
        const auto parentProcess=host?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,host):nullptr;
        const auto parentImage=parentProcess?image(parentProcess):std::wstring();
        if(parentProcess) CloseHandle(parentProcess);
        PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Process32First(snapshot, &entry)) do {
            if (entry.th32ProcessID == host) continue;
            std::string name(entry.szExeFile);
            for (char &c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (name == "verylightrgb.exe" || name == "native-device-db.exe" || name == "rgbtray.exe" || name == "signalrgb.exe" || name == "openrgb.exe" ||
                name == "icue.exe" || name == "ledkeeper2.exe" || name == "corsair.service.exe") {
                // Toolhelp is a snapshot: a short-lived status CLI may already have
                // exited. It must not become a phantom hardware-owner conflict.
                const auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
                if(!p && GetLastError()==ERROR_INVALID_PARAMETER) continue;
                if(p) {
                    const bool exited=WaitForSingleObject(p,0)==WAIT_OBJECT_0;
                    const auto otherImage=image(p); CloseHandle(p);
                    if(exited || (!parentImage.empty() && !otherImage.empty() && _wcsicmp(parentImage.c_str(),otherImage.c_str())==0)) continue;
                }
                conflict = name; break;
            }
        } while (Process32Next(snapshot, &entry));
        CloseHandle(snapshot);
        if (!conflict.empty()) throw std::runtime_error("Close competing RGB process before direct driver detection: " + conflict);
    }
  public:
    explicit DeviceDbCore(DWORD hostPid = 0) {
        if (hostPid) {
            const auto processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (processes == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot validate RGB host");
            PROCESSENTRY32 p{}; p.dwSize = sizeof(p); bool valid = false;
            if (Process32First(processes, &p)) do {
                if (p.th32ProcessID == GetCurrentProcessId() && p.th32ParentProcessID == hostPid) valid = true;
            } while (Process32Next(processes, &p));
            CloseHandle(processes);
            if (!valid) throw std::runtime_error("RGB host is not the direct parent process");
            host = hostPid;
        }
        // The selected-phase API bypasses BackgroundDetectDevices, including its progress setup.
        dm->detection_percent_hid_count = 0;
        dm->detection_percent_i2c_count = static_cast<unsigned int>(dm->i2c_device_detectors.size());
        dm->detection_percent_i2c_dram_count = static_cast<unsigned int>(dm->i2c_dram_device_detectors.size());
        dm->detection_percent_i2c_pci_count = static_cast<unsigned int>(dm->i2c_pci_device_detectors.size());
        dm->detection_percent_other_count = static_cast<unsigned int>(dm->device_detectors.size());
        dm->detection_percent_denominator = 1u + dm->detection_percent_i2c_count + dm->detection_percent_i2c_dram_count + dm->detection_percent_i2c_pci_count + dm->detection_percent_other_count;
    }
    json manifest() {
        json out = json::array();
        auto hid = [&](const auto &items, const char *transport) {
            for (const auto &d : items) out.push_back({{"name", d.name}, {"transport", transport}, {"vid", d.vid}, {"pid", d.pid},
                {"interface", d.interface}, {"usagePage", d.usage_page}, {"usage", d.usage}, {"bus", d.bus}, {"hardwareVerified", false}});
        };
        hid(dm->hid_specific_detectors, "hid"); hid(dm->hid_generic_detectors, "hid");
        hid(dm->hid_wrapped_specific_detectors, "hid-wrapped"); hid(dm->hid_wrapped_generic_detectors, "hid-wrapped");
        for (const auto &d : dm->i2c_pci_device_detectors) out.push_back({{"name", d.name}, {"transport", "i2c-pci"},
            {"vendor", d.ven_id}, {"device", d.dev_id}, {"subvendor", d.subven_id}, {"subdevice", d.subdev_id}, {"address", d.i2c_addr}, {"hardwareVerified", false}});
        for (const auto &d : dm->i2c_dram_device_detectors) out.push_back({{"name", d.name}, {"transport", "smbus-dram"}, {"hardwareVerified", false}});
        for (const auto &name : dm->i2c_device_detector_strings) out.push_back({{"name", name}, {"transport", "smbus"}, {"hardwareVerified", false}});
        for (const auto &name : dm->device_detector_strings) out.push_back({{"name", name}, {"transport", "custom"}, {"hardwareVerified", false}});
        return out;
    }
    json devices() {
        json out = json::array();
        for (auto *c : dm->rgb_controllers) {
            json zones = json::array();
            for (unsigned int z = 0; z < c->GetZoneCount(); z++) zones.push_back({{"index", z}, {"name", c->GetZoneName(z)},
                {"ledCount", c->GetZoneLEDsCount(z)}, {"minLeds", c->GetZoneLEDsMin(z)}, {"maxLeds", c->GetZoneLEDsMax(z)}, {"start", c->GetZoneStartIndex(z)}});
            out.push_back({{"id", identity(c)}, {"name", c->GetName()}, {"location", c->GetLocation()}, {"vendor", c->GetVendor()},
                {"ledCount", c->GetLEDCount()}, {"zones", zones}, {"directCapable", direct(c)}, {"detector", c->detector_name}, {"path", c->detection_path}, {"hardwareVerified", false}});
        }
        return out;
    }
    void open(const json &r) {
        if (opened) throw std::runtime_error("Open once per worker process");
        const auto &list = r.at("detectors");
        if (!list.is_array() || list.empty() || list.size() > 64) throw std::runtime_error("Select 1..64 exact detector names");
        std::set<std::string> allowed, known;
        json settings{{"detectors", json::object()}};
        for (const auto &record : manifest()) { const std::string name = record["name"]; known.insert(name); settings["detectors"][name] = false; }
        for (const auto &v : list) {
            const auto name = v.get<std::string>();
            if (!known.count(name)) throw std::runtime_error("Unknown compiled detector: " + name);
            allowed.insert(name); settings["detectors"][name] = true;
        }
        std::set<std::string> paths;
        if (r.contains("paths")) {
            if (!r["paths"].is_array() || r["paths"].size() > 128) throw std::runtime_error("Invalid HID path selection");
            for (const auto &p : r["paths"]) paths.insert(p.get<std::string>());
        }
        const bool bus = r.value("allowBusAccess", false), custom = r.value("allowCustomDetection", false);
        for (const auto &record : manifest()) if (allowed.count(record["name"])) {
            const auto transport = record["transport"].get<std::string>();
            if ((transport == "i2c-pci" || transport == "smbus" || transport == "smbus-dram") && !bus)
                throw std::runtime_error("Selected detector needs explicit allowBusAccess:true");
            if (transport == "custom" && !custom) throw std::runtime_error("Selected detector needs explicit allowCustomDetection:true");
        }
        conflicts();
        lease = CreateMutexW(nullptr, FALSE, L"Local\\VeryLightRGB-device-db-driver-core");
        if (!lease) throw std::runtime_error("Cannot create driver-core lease");
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(lease); lease = nullptr; throw std::runtime_error("Another driver-core worker is active"); }
        auto *rm = ResourceManager::get();
        rm->GetSettingsManager()->SetSettings("Client", json{{"clients", json::array()}});
        rm->GetSettingsManager()->SetSettings("Detectors", settings);
        rm->GetSettingsManager()->SetSettings("LogManager", json{{"log_console", false}, {"log_file", false}, {"loglevel", 0}});
        LogManager::get()->SetLogLevel(LL_FATAL, true);
        // No Initialize/BeginDetection: no SDK, broad scan, or global pre-detection hooks.
        opened = true;
        if (hid_init() != 0) throw std::runtime_error("HID initialization failed");
        hidStarted = true;
        hid_device_info *head = hid_enumerate(0, 0);
        try {
            hidapi_wrapper wrapper{nullptr, hid_send_feature_report, hid_get_feature_report, hid_get_serial_number_string,
                hid_open_path, hid_enumerate, hid_free_enumeration, hid_close, hid_error,
                hid_hotplug_register_callback, hid_hotplug_deregister_callback};
            for (auto *info = head; info; info = info->next) {
                if (!paths.empty() && !paths.count(info->path)) continue;
                try {
                    dm->RunHIDDetector(info, settings);
                    dm->RunHIDWrappedDetector(&wrapper, info, settings);
                } catch (const std::exception &e) { warnings.push_back(e.what()); }
            }
        } catch (...) { hid_free_enumeration(head); throw; }
        hid_free_enumeration(head);
        if (bus) {
            dm->BackgroundDetectI2CBuses(); dm->BackgroundDetectI2CDevices(settings);
            dm->BackgroundDetectI2CDRAMDevices(settings); dm->BackgroundDetectI2CPCIDevices(settings);
        }
        if (custom) dm->BackgroundDetectOtherDevices(settings);
    }
    json automatic(const json &r) {
        std::set<std::pair<unsigned int,unsigned int>> excluded;
        for(const auto &id:r.value("excludeHid",json::array())) excluded.insert({id.at(0).get<unsigned int>(),id.at(1).get<unsigned int>()});
        std::set<std::string> names,paths;
        const auto compiled=manifest();
        if(hid_init()!=0) throw std::runtime_error("HID initialization failed");
        auto *head=hid_enumerate(0,0);
        for(auto *p=head;p;p=p->next) {
            if(excluded.count({p->vendor_id,p->product_id})) continue;
            for(const auto &d:compiled) {
                if(d["transport"]!="hid"&&d["transport"]!="hid-wrapped") continue;
                const auto matches=[&](const char *key,int actual){return d[key].get<int>()<0||d[key]==actual;};
                if(matches("vid",p->vendor_id)&&matches("pid",p->product_id)&&matches("interface",p->interface_number)&&matches("usagePage",p->usage_page)&&matches("usage",p->usage)&&matches("bus",p->bus_type)) {
                    names.insert(d["name"].get<std::string>()); paths.insert(p->path);
                }
            }
        }
        hid_free_enumeration(head); hid_exit();
        bool bus=false;
        if(!r.value("excludeGpu",false)) for(DWORD i=0;;i++) {
            DISPLAY_DEVICEA display{}; display.cb=sizeof(display);
            if(!EnumDisplayDevicesA(nullptr,i,&display,0)) break;
            const std::string id=display.DeviceID;
            auto part=[&](const char *key,size_t length)->unsigned long {
                const auto at=id.find(key); if(at==std::string::npos) return 0;
                return strtoul(id.substr(at+strlen(key),length).c_str(),nullptr,16);
            };
            const auto vendor=part("VEN_",4),device=part("DEV_",4),sub=part("SUBSYS_",8);
            if(!vendor||!device) continue;
            for(const auto &d:compiled) if(d["transport"]=="i2c-pci"&&d["vendor"]==vendor&&d["device"]==device&&d["subvendor"]==(sub&0xffff)&&d["subdevice"]==(sub>>16)) {
                names.insert(d["name"].get<std::string>()); bus=true;
            }
        }
        if(names.empty()) return {{"ok",true},{"devices",json::array()},{"warnings",warnings},{"serverRequired",false}};
        open({{"detectors",names},{"paths",paths},{"allowBusAccess",bus}});
        return {{"ok",true},{"devices",devices()},{"warnings",warnings},{"serverRequired",false}};
    }
    json request(const json &r) {
        const auto op = r.at("op").get<std::string>();
        if (op == "open-auto") return automatic(r);
        if (op == "open") { open(r); return {{"ok", true}, {"devices", devices()}, {"warnings",warnings},{"serverRequired", false}}; }
        if (op == "devices") return {{"ok", true}, {"devices", devices()}};
        if(op=="check") {
            std::set<std::string> required;
            for(auto *c:dm->rgb_controllers) if(!c->detection_path.empty()&&c->detection_path!="memory") required.insert(c->detection_path);
            if(!required.empty()) {
                auto *head=hid_enumerate(0,0);
                for(auto *p=head;p;p=p->next) required.erase(p->path);
                hid_free_enumeration(head);
                if(!required.empty()) throw std::runtime_error("Native HID device disconnected");
            }
            return {{"ok",true}};
        }
        if (!opened) throw std::runtime_error("Open selected detectors first");
        auto *c = controller(r.at("id").get<std::string>());
        if(op=="cached-colors") {
            json colors=json::array();
            for(unsigned int i=0;i<c->GetLEDCount();i++) {const auto rgb=c->GetColor(i); colors.push_back({RGBGetRValue(rgb),RGBGetGValue(rgb),RGBGetBValue(rgb)});}
            return {{"ok",true},{"colors",colors},{"hardwareReadback",false}};
        }
        if (op == "resize") {
            const auto z = number(r, "zone", c->GetZoneCount() ? c->GetZoneCount() - 1 : 0);
            if (z >= c->GetZoneCount()) throw std::runtime_error("No such zone");
            const auto count = number(r, "ledCount", 2048);
            if (count < c->GetZoneLEDsMin(z) || count > c->GetZoneLEDsMax(z)) throw std::runtime_error("LED count outside device limits");
            c->ResizeZone(static_cast<int>(z), static_cast<int>(count));
            directInitialized.erase(identity(c));
            return {{"ok", true}, {"devices", devices()}};
        }
        if (op != "frame" && op != "zone-frame") throw std::runtime_error("Unknown operation");
        if (!direct(c)) throw std::runtime_error("Controller has no advertised per-LED direct mode");
        const auto zone=op=="zone-frame" ? number(r,"zone",c->GetZoneCount()?c->GetZoneCount()-1:0) : 0;
        if(op=="zone-frame"&&zone>=c->GetZoneCount()) throw std::runtime_error("No such zone");
        const auto expected=op=="zone-frame"?c->GetZoneLEDsCount(zone):c->GetLEDCount();
        const auto &colors = r.at("colors");
        if (!colors.is_array() || colors.empty() || colors.size() != expected || colors.size() > 16384)
            throw std::runtime_error("Frame must contain the exact full-controller LED count");
        std::vector<RGBColor> validated;
        for (const auto &rgb : colors) {
            if (!rgb.is_array() || rgb.size() != 3) throw std::runtime_error("Expected RGB triplets");
            unsigned int values[3];
            for (size_t j = 0; j < 3; j++) {
                if (!rgb[j].is_number_integer() || rgb[j].get<int64_t>() < 0 || rgb[j].get<int64_t>() > 255)
                    throw std::runtime_error("RGB components must be integer bytes");
                values[j] = rgb[j].get<unsigned int>();
            }
            validated.push_back(ToRGBColor(values[0], values[1], values[2]));
        }
        for (unsigned int i = 0; i < validated.size(); i++) {
            if(op=="zone-frame") c->SetZoneColor(zone,i,validated[i]); else c->SetColor(i,validated[i]);
        }
        if (!directInitialized.count(identity(c))) { c->SetActiveMode(directMode(c)); c->UpdateMode(); directInitialized.insert(identity(c)); }
        if(op=="zone-frame") c->UpdateZoneLEDs(static_cast<int>(zone)); else c->UpdateLEDs();
        return {{"ok", true}, {"queued", true}, {"hardwareReadback", false}};
    }
    json selfTest() {
        auto *memory = new MemoryController();
        dm->rgb_controllers.push_back(memory); opened = true;
        unsigned int checks = 0;
        auto expect = [&](bool value) { checks++; if (!value) throw std::runtime_error("Memory-controller check failed: " + std::to_string(checks)); };
        auto reject = [&](json r) { checks++; try { request(r); } catch (const std::exception &) { return; } throw std::runtime_error("Invalid frame accepted"); };
        const auto id = identity(memory);
        expect(dm->detection_percent_denominator > 0);
        expect(devices().size() == 1 && devices()[0]["directCapable"] == true);
        expect(request({{"op", "frame"}, {"id", id}, {"colors", {{18,52,86}, {154,188,222}}}})["ok"] == true);
        for (int i = 0; i < 500 && !memory->frames.load(); i++) Sleep(1);
        expect(memory->frames.load() > 0);
        expect(memory->GetColor(0) == ToRGBColor(18,52,86) && memory->GetColor(1) == ToRGBColor(154,188,222));
        reject({{"op", "frame"}, {"id", id}, {"colors", {{1,2,3}}}});
        reject({{"op", "frame"}, {"id", id}, {"colors", {{-1,2,3},{1,2,3}}}});
        reject({{"op", "frame"}, {"id", id}, {"colors", {{256,2,3},{1,2,3}}}});
        reject({{"op", "frame"}, {"id", id}, {"colors", {{1.5,2,3},{1,2,3}}}});
        reject({{"op", "frame"}, {"id", "stale-id"}, {"colors", {{1,2,3},{1,2,3}}}});
        reject({{"op", "resize"}, {"id", id}, {"zone", 0}, {"ledCount", 3}});
        expect(memory->GetColor(0) == ToRGBColor(18,52,86));
        return {{"ok", true}, {"checks", checks}, {"hardwareDetectionStarted", false}, {"serverRequired", false}};
    }
    json memoryOpen() {
        if(!opened) {
            auto *memory=new MemoryController(true);
            memory->detector_name="Memory fixture";
            memory->detection_path="memory";
            dm->rgb_controllers.push_back(memory); opened=true;
        }
        return {{"ok",true},{"devices",devices()},{"warnings",json::array()},{"serverRequired",false}};
    }
    ~DeviceDbCore() {
        if (opened) {
            // Avoid global cleanup hooks; delete only this worker's controllers and buses.
            // Destructors can unregister themselves; clear the live list before invoking them.
            std::vector<RGBController*> controllers;
            { std::lock_guard<std::mutex> guard(dm->RGBControllersMutex); controllers.swap(dm->rgb_controllers); }
            for (auto *c : controllers) delete c;
            std::vector<i2c_smbus_interface*> buses;
            buses.swap(dm->i2c_buses);
            for (auto *bus : buses) delete bus;
        }
        if (hidStarted) hid_exit();
        if (lease) CloseHandle(lease);
    }
};

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        DWORD host=0;
        if(argc==5 && std::string(argv[3])=="--host-pid") host=static_cast<DWORD>(std::stoul(argv[4]));
        DeviceDbCore core(host);
        if (argc == 2 && std::string(argv[1]) == "--self-test") { std::cout << core.selfTest().dump(2) << '\n'; return 0; }
        if (argc == 2 && std::string(argv[1]) == "detectors") {
            std::cout << json{{"serverRequired", false}, {"hardwareDetectionStarted", false}, {"detectors", core.manifest()}}.dump(2) << '\n';
            return 0;
        }
        const bool memoryMode=argc>1&&std::string(argv[1])=="memory-stream";
        if ((argc != 3 && argc != 5) || (std::string(argv[1]) != "stream" && !memoryMode)) {
            std::cout << "native-driver-core detectors | stream ABSOLUTE_DATA_DIRECTORY\n"; return 0;
        }
        int wideCount = 0;
        auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (!wideArgs || (wideCount != 3 && wideCount != 5)) throw std::runtime_error("Cannot parse data directory");
        const auto data = filesystem::path(wideArgs[2]); LocalFree(wideArgs);
        if (!data.is_absolute()) throw std::runtime_error("An absolute isolated data directory is required");
        filesystem::create_directories(data);
        _wputenv_s(L"VLR_DEVICE_DB_CORE_DATA", data.wstring().c_str());
        HANDLE protocolOutput = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(), &protocolOutput, 0, FALSE, DUPLICATE_SAME_ACCESS))
            throw std::runtime_error("Cannot open protocol output");
        // Drivers may use printf/cout. Keep their diagnostics on stderr, separate from JSONL.
        if (_dup2(_fileno(stderr), _fileno(stdout)) != 0) { CloseHandle(protocolOutput); throw std::runtime_error("Cannot isolate driver diagnostics"); }
        const auto respond = [&](const json &value) {
            const auto output = value.dump() + "\n";
            size_t offset = 0;
            while (offset < output.size()) {
                DWORD done = 0;
                if (!WriteFile(protocolOutput, output.data() + offset, static_cast<DWORD>(output.size() - offset), &done, nullptr) || !done)
                    throw std::runtime_error("Protocol output closed");
                offset += done;
            }
        };
        LogManager::get()->SetLogLevel(LL_FATAL, true);
        std::string line; char ch;
        while (std::cin.get(ch)) {
            if (ch != '\n') {
                if (line.size() >= 2 * 1024 * 1024) throw std::runtime_error("Request exceeds 2 MiB");
                line.push_back(ch); continue;
            }
            if (line.empty()) continue;
            try {
                const auto r=json::parse(line);
                if(memoryMode && (r.value("op","")=="open" || r.value("op","")=="open-auto")) respond(core.memoryOpen());
                else respond(core.request(r));
            }
            catch (const std::exception &e) { respond(json{{"ok", false}, {"error", e.what()}}); }
            line.clear();
        }
        CloseHandle(protocolOutput); return 0;
    } catch (const std::exception &e) { std::cerr << json{{"ok", false}, {"error", e.what()}}.dump() << '\n'; return 1; }
}

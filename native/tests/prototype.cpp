#include "hardware.hpp"
#include <psapi.h>
#include <iostream>
using namespace vlr;

int wmain(int argc, wchar_t **argv) {
    try {
        auto config = Config::load();
        std::vector<std::string> notes;
        auto found = discoverNative(notes);
        Json result = {{"Version", Version},
                       {"DataDirectory", utf8(dataDirectory().wstring())},
                       {"Catalog", Json::array()},
                       {"Notes", notes}};
        for (auto &d : found)
            result["Catalog"].push_back(d.json());
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        result["WorkingSetMB"] = double(memory.WorkingSetSize) / (1024 * 1024);
        result["PrivateCommitMB"] = double(memory.PrivateUsage) / (1024 * 1024);
        auto path = argc > 1 ? std::filesystem::path(argv[1]) : dataDirectory() / L"prototype.json";
        writeAtomic(path, result.dump(2));
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

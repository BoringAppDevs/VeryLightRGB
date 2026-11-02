// SPDX-License-Identifier: GPL-2.0-or-later
#include "windows_hid.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

using namespace vrgbdb;

static std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw error("Executable path");
    return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
}

static bool readLine(std::string &line) {
    line.clear(); char c;
    while (std::cin.get(c)) {
        if (c == '\n') return true;
        if (line.size() == 1024 * 1024) throw std::runtime_error("Input line exceeds 1 MiB");
        line.push_back(c);
    }
    return !line.empty();
}

static void stream() {
    // The caller owns exactly one selected device. This is a local stdio worker, not a TCP server.
    std::unique_ptr<Session> session;
    std::mutex lock;
    std::atomic<bool> failed = false;
    std::jthread keeper([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard guard(lock);
            if (!session || failed) continue;
            try { session->keepalive(); }
            catch (const std::exception &e) {
                failed = true;
                std::cout << Json{{"ok", false}, {"error", e.what()}}.dump() << std::endl;
            }
        }
    });
    std::string line;
    while (readLine(line)) {
        if (line.empty()) continue;
        const auto request = Json::parse(line);
        std::lock_guard guard(lock);
        if (failed) throw std::runtime_error("Device session failed; reconnect explicitly");
        const auto op = request.at("op").get<std::string>();
        if (op == "close") { session.reset(); std::cout << Json{{"ok", true}}.dump() << std::endl; continue; }
        if (op == "open") {
            if (session) throw std::runtime_error("Close the existing session first");
            checkConflicts(); session = std::make_unique<Session>(request);
            std::cout << Json{{"ok", true}, {"session", session->info()}}.dump() << std::endl;
        } else if (op == "frame") {
            if (!session) throw std::runtime_error("Open a session first");
            session->frame(request);
            std::cout << Json{{"ok", true}, {"hardwareReadback", false}}.dump() << std::endl;
        } else throw std::runtime_error("Unknown stream operation");
    }
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        if (argc < 2) {
            std::cout << "native-device-db summary|find TEXT|protocol FAMILY|match REQUEST.json|inventory|plan REQUEST.json|probe REQUEST.json|apply REQUEST.json|stream [--db DIRECTORY]\n";
            return 0;
        }
        std::vector<std::string> args;
        auto directory = executableDirectory();
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--db") {
                if (++i >= argc) throw std::runtime_error("Missing database directory");
                directory = std::filesystem::path(argv[i]);
            } else args.push_back(utf8(argv[i]));
        }
        if (args.empty()) throw std::runtime_error("Missing command");
        const auto &command = args[0];
        Json result;
        if (command == "summary") result = load(directory / "summary.json");
        else if (command == "protocol") {
            if (args.size() != 2) throw std::runtime_error("Expected a protocol family ID");
            result = Json::array();
            const auto protocols = load(directory / "protocols.json");
            for (const auto &family : protocols.at("families"))
                if (family.at("id") == args[1]) result.push_back(family);
            if (result.empty()) throw std::runtime_error("Unknown protocol family");
        }
        else if (command == "find" || command == "match") {
            if (args.size() != 2) throw std::runtime_error("Expected a search string or JSON request file");
            const auto catalog = loadCatalog(directory);
            result = command == "find" ? find(catalog, args[1]) : candidates(catalog, load(wide(args[1]), 1024 * 1024));
        } else if (command == "inventory") {
            const auto catalog = loadCatalog(directory);
            result = inventory();
            for (auto &device : result) device["candidates"] = candidates(catalog, device);
        } else if (command == "plan" || command == "probe" || command == "apply") {
            if (args.size() != 2) throw std::runtime_error("Expected a JSON request file");
            const auto request = load(wide(args[1]), 1024 * 1024);
            if (command == "plan") {
                result = {{"hardwareAccess", false}, {"packets", plan(request)}, {"hardwareVerified", false}};
            } else {
                const auto hold = request.contains("holdMs") ? integer(request, "holdMs", 0, 3600000) : 0;
                checkConflicts(); Session session(request);
                result = session.info();
                if (command == "apply") {
                    session.frame(request);
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(hold);
                    while (std::chrono::steady_clock::now() < deadline) { Sleep(100); session.keepalive(); }
                    result["applied"] = true; result["hardwareReadback"] = false;
                }
            }
        } else if (command == "stream") { stream(); return 0; }
        else throw std::runtime_error("Unknown command");
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << Json{{"ok", false}, {"error", e.what()}}.dump() << '\n';
        return 1;
    }
}

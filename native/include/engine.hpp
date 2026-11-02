#pragma once
#include "hardware.hpp"
#include "sdk.hpp"
#include "driver.hpp"

namespace vlr {
class Engine {
    mutable std::mutex mutex;
    Config config;
    Json outputStatus = Json::array();
    std::string message = "Запуск…";
    uint64_t revision = 0;
    uint64_t appliedRevision = 0;
    uint64_t reloadId = 0;
    Json reloadState = {{"Id", 0}, {"State", "idle"}, {"Stage", ""}, {"Completed", 0}, {"Total", 0}};
    bool forceRefresh = false, paused = false;
    std::string identifyId;
    Handle stopEvent, updateEvent, readyEvent;
    std::thread worker;
    std::atomic<uint64_t> frames{0};
    std::atomic<bool> live{false};
    bool sharedLease = true;
    std::function<void()> onFatal;
    void run() noexcept;
    void statuses(Json value, const std::string &summary);
    void queueReload(); // Caller holds mutex.
    void reloadProgress(uint64_t id, const std::string &state, const std::string &stage, int completed,
                        int total, int found = 0, const std::vector<std::string> &notes = {});

  public:
    explicit Engine(Config value, bool useSharedLease = true, std::function<void()> fatal = {});
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Json snapshot() const;
    void update(Config value, uint64_t expectedRevision);
    void editLighting(const std::string &scope, const std::string &field, const Style &value,
                      uint64_t expectedRevision);
    void refresh();
    void reconnect();
    void pause(bool value);
    void identify(const std::string &id);
    bool waitReady(DWORD milliseconds = 20000) const;
    void stop();
};
} // namespace vlr

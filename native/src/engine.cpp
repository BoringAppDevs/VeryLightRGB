#include "engine.hpp"

namespace vlr {
class Session {
  public:
    std::unique_ptr<Msi> msi;
    std::unique_ptr<Gpu> gpu;
    std::unique_ptr<Sdk> sdk;
    std::unique_ptr<Driver> driver;
    std::vector<Descriptor> actualSdk;
    std::vector<Descriptor> actualDriver;
    std::vector<Plan> driverTargets;
    std::string driverError;

    explicit Session(const Config &c, const std::vector<Plan> &targets) {
        bool native = std::any_of(targets.begin(), targets.end(), [](auto &p) { return p.device.native(); });
        if (native)
            checkConflicts(c.backend != 2);
        for (const auto &p : targets) if (p.device.core()) driverTargets.push_back(p);
        bool hasSdk = std::any_of(targets.begin(), targets.end(), [](auto &p) { return p.device.sdk(); });
        if (hasSdk || c.backend == 2) {
            sdk = std::make_unique<Sdk>(c.port);
            actualSdk = sdk->discover();
        }
        if (native && c.backend == 2) {
            bool board = std::any_of(targets.begin(), targets.end(),
                                     [](auto &p) { return p.device.id.rfind("native:msi:", 0) == 0; }),
                 card = std::any_of(targets.begin(), targets.end(),
                                    [](auto &p) { return p.device.id == gpuDescriptor().id; });
            for (auto &d : actualSdk) {
                auto name = d.name;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                // SDK controller types 0=motherboard, 2=GPU. Names can be user-edited.
                if ((board && d.controllerType == 0) || (card && d.controllerType == 2) ||
                    (board &&
                     (name.find("msi") != std::string::npos || name.find("mortar") != std::string::npos ||
                      name.find("7e61") != std::string::npos)) ||
                    (card &&
                     (name.find("nvidia") != std::string::npos ||
                      name.find("gainward") != std::string::npos ||
                      name.find("geforce") != std::string::npos || name.find("5080") != std::string::npos ||
                      name.find("phoenix") != std::string::npos || name.find("pny") != std::string::npos)))
                    throw std::runtime_error("Конфлікт native / OpenRGB. Вимкни детектори MSI та Gainward в "
                                             "OpenRGB або вибери один backend.");
            }
        }
    }

    Msi &motherboard() {
        if (!msi)
            msi = std::make_unique<Msi>();
        return *msi;
    }

    Gpu &card() {
        if (!gpu)
            gpu = std::make_unique<Gpu>();
        return *gpu;
    }

    Descriptor resolve(const Plan &p) {
        if(p.device.core()) {
            if (!driverError.empty()) throw std::runtime_error(driverError);
            if (!driver) {
                try {
                    driver = std::make_unique<Driver>();
                    actualDriver = driver->open(driverTargets);
                } catch (const std::exception &ex) {
                    driver.reset();
                    driverError = ex.what();
                    throw;
                }
            }
            const auto found=std::find_if(actualDriver.begin(),actualDriver.end(),[&](const auto &d){return d.id==p.device.id;});
            if(found==actualDriver.end()) throw std::runtime_error("Native output unavailable; Reload devices");
            driver->prepare(p);
            return *found;
        }
        if (!p.device.sdk())
            return p.device;
        auto found =
            std::find_if(actualSdk.begin(), actualSdk.end(), [&](auto &d) { return d.id == p.device.id; });
        if (found == actualSdk.end())
            throw std::runtime_error("Пристрій OpenRGB недоступний; Reload devices");
        if (found->count != p.count)
            throw std::runtime_error("Довжина SDK-зони змінилася; Reload devices");
        return *found;
    }

    void open(const Plan &p, const Descriptor &actual, const Bytes &initial, Bytes &packet) {
        if(p.device.core()) {
            driver->frame(actual,initial);
        } else if (p.device.id == gpuDescriptor().id) {
            card().apply({initial[0], initial[1], initial[2]});
            if (!p.animated())
                gpu.reset();
        } else if (p.device.native()) {
            auto &m = motherboard();
            if (p.animated()) {
                try {
                    m.zone(p.device.zone, {});
                    m.zone(p.device.zone, {}, p.count);
                    packet = Bytes(727);
                    m.frame(p.device.zone, initial, packet);
                } catch (...) {
                    try {
                        m.zone(p.device.zone, {});
                    } catch (...) {
                    }
                    throw;
                }
            } else
                m.zone(p.device.zone, {initial[0], initial[1], initial[2]});
        } else {
            if (!sdk)
                throw std::runtime_error("SDK session unavailable");
            sdk->custom(actual.controller);
            sdk->frame(actual, initial);
        }
    }

    void send(const Plan &p, const Descriptor &actual, const Bytes &rgb, Bytes &packet) {
        if(p.device.core()) driver->frame(actual,rgb);
        else if (p.device.id == gpuDescriptor().id)
            card().frame({rgb[0], rgb[1], rgb[2]});
        else if (p.device.native())
            motherboard().frame(p.device.zone, rgb, packet);
        else
            sdk->frame(actual, rgb);
    }

    void restore(const Plan &p, const Descriptor &actual, Rgb color) {
        if(p.device.core()) {
            Bytes rgb(size_t(p.count)*3);
            for(int i=0;i<p.count;i++) std::copy(color.begin(),color.end(),rgb.begin()+size_t(i)*3);
            driver->frame(actual,rgb);
        } else if (p.device.id == gpuDescriptor().id) {
            card().apply(color);
            if (!p.animated())
                gpu.reset();
        } else if (p.device.native())
            motherboard().zone(p.device.zone, color);
        else {
            Bytes rgb(size_t(p.count) * 3);
            for (int i = 0; i < p.count; i++)
                std::copy(color.begin(), color.end(), rgb.begin() + size_t(i) * 3);
            sdk->frame(actual, rgb);
        }
    }

    void check(const Config &c, bool native) {
        if (native)
            checkConflicts(c.backend != 2);
        if (sdk)
            sdk->check();
        if(driver) driver->check();
    }
};

struct State {
    Plan plan;
    Descriptor actual;
    Renderer renderer;
    Bytes previous, packet;
    bool active = true;

    State(Plan p, Descriptor d) : plan(std::move(p)), actual(std::move(d)), renderer(plan) {}
};

Engine::Engine(Config value, bool useSharedLease, std::function<void()> fatal)
    : config(std::move(value)), stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      updateEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
      readyEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr)), sharedLease(useSharedLease),
      onFatal(std::move(fatal)) {
    config.validate();
    if (!sharedLease && (config.backend != 1 || std::any_of(config.catalog.begin(), config.catalog.end(),
                                                            [](auto &d) { return d.native(); })))
        throw std::runtime_error("Lease-free mode is restricted to SDK transport tests");
    if (!stopEvent || !updateEvent || !readyEvent)
        throw std::runtime_error(winError("Engine events"));
    worker = std::thread(&Engine::run, this);
}

Engine::~Engine() {
    stop();
}

void Engine::stop() {
    if (stopEvent)
        SetEvent(stopEvent.h);
    if (worker.joinable())
        worker.join();
}

bool Engine::waitReady(DWORD ms) const {
    return WaitForSingleObject(readyEvent.h, ms) == WAIT_OBJECT_0;
}

void Engine::statuses(Json value, const std::string &text) {
    std::lock_guard lock(mutex);
    outputStatus = std::move(value);
    message = text;
}

Json Engine::snapshot() const {
    std::lock_guard lock(mutex);
    return {{"Config", config.json()}, {"Revision", revision}, {"AppliedRevision",appliedRevision}, {"Status", outputStatus},
            {"Message", message},      {"Frames", frames.load()}, {"Animating", live.load()},
            {"Paused", paused},        {"Version", Version},      {"Reload", reloadState}};
}

void Engine::queueReload() {
    forceRefresh = true;
    reloadState = {{"Id", ++reloadId},      {"State", "queued"}, {"Stage", "Оновлення пристроїв у черзі"},
                   {"Completed", 0},        {"Total", 0},        {"Found", 0},
                   {"Notes", Json::array()}};
}

void Engine::reloadProgress(uint64_t id, const std::string &state, const std::string &stage, int completed,
                            int total, int found, const std::vector<std::string> &notes) {
    std::lock_guard lock(mutex);
    if (id != reloadId)
        return;
    reloadState = {{"Id", id},       {"State", state}, {"Stage", stage}, {"Completed", completed},
                   {"Total", total}, {"Found", found}, {"Notes", notes}};
}

void Engine::update(Config value, uint64_t expected) {
    std::lock_guard lock(mutex);
    if (expected != revision)
        throw std::runtime_error("Налаштування змінилися. Натисни F5 та повтори.");
    value.catalog = config.catalog;
    value.validate();
    value.save();
    auto previous = config.json();
    previous["Language"] = value.language;
    bool lightingChanged = previous != value.json();
    if (value.backend != config.backend || value.port != config.port)
        queueReload();
    config = std::move(value);
    const bool wasApplied = appliedRevision == revision;
    revision++;
    if(!lightingChanged && wasApplied) appliedRevision=revision;
    // A language change must not restart an animation or reopen hardware sessions.
    if (lightingChanged || !wasApplied)
        SetEvent(updateEvent.h);
}

void Engine::editLighting(const std::string &scope, const std::string &field, const Style &value,
                          uint64_t expectedRevision) {
    Config next;
    {
        std::lock_guard lock(mutex);
        if (expectedRevision != revision)
            throw std::runtime_error("Налаштування змінилися. Натисни F5 та повтори.");
        next = config;
    }
    vlr::editLighting(next, scope, field, value);
    update(std::move(next), expectedRevision);
}

void Engine::refresh() {
    std::lock_guard lock(mutex);
    if (reloadState["State"] == "queued" || reloadState["State"] == "running")
        return;
    queueReload();
    SetEvent(updateEvent.h);
}

void Engine::reconnect() {
    SetEvent(updateEvent.h);
}

void Engine::pause(bool value) {
    std::lock_guard lock(mutex);
    paused = value;
    SetEvent(updateEvent.h);
}

void Engine::identify(const std::string &id) {
    std::lock_guard lock(mutex);
    if (std::none_of(config.catalog.begin(), config.catalog.end(), [&](auto &d) { return d.id == id; }))
        throw std::runtime_error("Unknown output");
    identifyId = id;
    SetEvent(updateEvent.h);
}

void Engine::run() noexcept {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    Handle lease(CreateMutexW(nullptr, FALSE, L"Local\\RgbTray.Hardware"));
    Handle timer(CreateWaitableTimerW(nullptr, FALSE, nullptr));
    bool owned = false, dirty = true;
    int retries = 0;
    std::optional<Clock::time_point> retryAt;
    Config current;
    std::unique_ptr<Session> session;
    std::vector<State> outputs;
    Json results = Json::array();
    auto began = Clock::now(), checkAt = Clock::now();
    auto release = [&] {
        session.reset();
        outputs.clear();
        live = false;
        if (owned) {
            ReleaseMutex(lease.h);
            owned = false;
        }
    };
    auto schedule = [&] {
        static constexpr int delays[] = {2, 5, 10, 20, 30, 30};
        if (retries < 6) {
            retryAt = Clock::now() + std::chrono::seconds(delays[retries++]);
            log("Connection retry " + std::to_string(retries) + " queued; cached devices only.");
        } else
            retryAt.reset();
    };
    auto interrupted = [&] { return WaitForSingleObject(stopEvent.h, 0) == WAIT_OBJECT_0; };
    try {
        while (!interrupted()) {
            if (dirty) {
                release();
                dirty = false;
                retryAt.reset();
                bool refresh = false, suspended = false;
                uint64_t activeReload = 0;
                uint64_t applyingRevision = 0;
                int reloadTotal = 0, reloadFound = 0;
                std::vector<std::string> reloadNotes;
                std::string identify;
                {
                    std::lock_guard lock(mutex);
                    current = config;
                    applyingRevision = revision;
                    refresh = forceRefresh || config.catalog.empty();
                    if (refresh) {
                        if (reloadState["State"] != "queued")
                            queueReload();
                        activeReload = reloadId;
                        reloadTotal = (config.backend != 1 ? 2 : 0) + (config.backend != 0 ? 2 : 0) + (config.backend==0?1:0) + 2;
                        reloadState["State"] = "running";
                        reloadState["Stage"] = "Пошук доступних контролерів";
                        reloadState["Total"] = reloadTotal;
                    }
                    forceRefresh = false;
                    suspended = paused;
                    identify = std::move(identifyId);
                    identifyId.clear();
                }
                results = Json::array();
                bool failed = false;
                try {
                    if (refresh) {
                        std::vector<Descriptor> catalog;
                        auto &notes = reloadNotes;
                        int completed = 0;
                        if (current.backend != 1)
                            catalog = discoverNative(notes, [&](int step, const std::string &stage) {
                                reloadProgress(activeReload, "running", stage, step, reloadTotal);
                            });
                        completed = current.backend != 1 ? 2 : 0;
                        if(current.backend==0) {
                            reloadProgress(activeReload,"running","Пошук native-контролерів без сервера",completed,reloadTotal,int(catalog.size()));
                            auto extra=discoverDrivers(catalog,notes);
                            catalog.insert(catalog.end(),extra.begin(),extra.end());
                            completed++;
                        }
                        if (current.backend != 0)
                            try {
                                reloadProgress(activeReload, "running", "Підключення до OpenRGB SDK",
                                               completed, reloadTotal, int(catalog.size()));
                                Sdk probe(current.port);
                                auto sdkCatalog = probe.discover([&](uint32_t done, uint32_t total) {
                                    reloadProgress(activeReload, "running",
                                                   "OpenRGB · контролер " + std::to_string(done) + " з " +
                                                       std::to_string(total),
                                                   completed + 1, reloadTotal, int(catalog.size()));
                                });
                                catalog.insert(catalog.end(), sdkCatalog.begin(), sdkCatalog.end());
                            } catch (const std::exception &ex) {
                                notes.push_back(ex.what());
                                {
                                    std::lock_guard lock(mutex);
                                    forceRefresh = true;
                                }
                                if (current.backend == 1)
                                    throw;
                                failed = true;
                            }
                        reloadFound = int(catalog.size());
                        reloadProgress(activeReload, "running", "Збереження каталогу пристроїв",
                                       reloadTotal - 2, reloadTotal, reloadFound);
                        {
                            std::lock_guard lock(mutex);
                            if (config.backend != current.backend || config.port != current.port)
                                throw std::runtime_error("Параметри з’єднання змінилися; повторне оновлення");
                            // Keep known missing outputs so reconnect can safely target their verified
                            // driver.
                            for (auto d : config.catalog)
                                if (std::none_of(catalog.begin(), catalog.end(),
                                                 [&](auto &x) { return x.id == d.id; })) {
                                    if (config.allows(d))
                                        d.present = false;
                                    catalog.push_back(d);
                                }
                            config.catalog = std::move(catalog);
                            for (auto &d : config.catalog)
                                config.device(d);
                            config.save();
                            current = config;
                            revision++;
                            applyingRevision = revision;
                        }
                        log("Catalog refresh completed: " + std::to_string(current.catalog.size()) +
                            " outputs; " + std::to_string(notes.size()) + " notes.");
                        for (auto &note : notes)
                            log(note);
                        reloadProgress(activeReload, "running", "Застосування кольорів та ефектів",
                                       reloadTotal - 1, reloadTotal, reloadFound);
                    }
                    auto targets = plans(current);
                    if (targets.empty() &&
                        std::any_of(current.devices.begin(), current.devices.end(), [&](const Device &d) {
                            return d.managed && ((current.backend==0 && d.id.rfind("driver:",0)==0) || (current.backend != 1 && d.id.rfind("native:", 0) == 0) ||
                                                 (current.backend != 0 && d.id.rfind("openrgb:", 0) == 0));
                        }))
                        throw std::runtime_error("Вибрані контролери ще не готові; повторне підключення");
                    if (!identify.empty()) {
                        auto it = std::find_if(current.catalog.begin(), current.catalog.end(),
                                               [&](auto &d) { return d.id == identify; });
                        if (it != current.catalog.end()) {
                            auto d = current.device(*it);
                            Plan p;
                            p.device = *it;
                            p.count = it->configurable ? std::clamp(d.leds, 1, it->maxLeds) : it->count;
                            p.style.color = "#FFFFFF";
                            p.style.brightness = 25;
                            p.style.effect = 3;
                            p.style.step = 100;
                            targets = {p};
                        }
                    }
                    if (suspended)
                        for (auto &p : targets) {
                            p.style.enabled = false;
                            p.style.effect = 0;
                        }
                    if (!targets.empty()) {
                        if (sharedLease) {
                            DWORD status = WaitForSingleObject(lease.h, 0);
                            if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED)
                                throw std::runtime_error("RGB зайняте іншою операцією");
                            owned = true;
                        }
                        session = std::make_unique<Session>(current, targets);
                        outputs.reserve(targets.size());
                        for (auto &p : targets) {
                            if (interrupted())
                                break;
                            try {
                                auto actual = session->resolve(p);
                                State state(p, actual);
                                auto rgb = state.renderer.render(0);
                                session->open(p, actual, rgb, state.packet);
                                state.previous = rgb;
                                outputs.push_back(std::move(state));
                                results.push_back({{"Id", p.device.id},
                                                   {"Name", p.device.name},
                                                   {"Success", true},
                                                   {"Message", effectName(p.style.effect)}});
                            } catch (const std::exception &ex) {
                                failed = true;
                                results.push_back({{"Id", p.device.id},
                                                   {"Name", p.device.name},
                                                   {"Success", false},
                                                   {"Message", ex.what()}});
                                log(p.device.id + ": " + ex.what());
                            }
                        }
                    }
                    bool animation = std::any_of(outputs.begin(), outputs.end(),
                                                 [](auto &s) { return s.plan.animated(); });
                    bool boardAnimation = std::any_of(outputs.begin(), outputs.end(), [](auto &s) {
                        return s.plan.animated() && s.plan.device.id.rfind("native:msi:", 0) == 0;
                    });
                    if (session && !boardAnimation)
                        session->msi.reset();
                    live = animation;
                    began = Clock::now();
                    checkAt = began + std::chrono::seconds(2);
                    statuses(results, suspended                 ? "Призупинено до пробудження"
                                      : targets.empty()         ? "Вибери виходи у меню «Пристрої»"
                                      : failed                  ? "Деякі виходи недоступні"
                                      : !current.global.enabled ? "Світло вимкнене"
                                                                : "Підсвітка працює");
                    if (activeReload) {
                        auto summary = "Каталог оновлено · виходів: " + std::to_string(reloadFound);
                        if (!reloadNotes.empty())
                            summary += " · " + reloadNotes.front();
                        else if (failed)
                            summary += " · деякі виходи недоступні";
                        reloadProgress(activeReload, failed || !reloadNotes.empty() ? "warning" : "done",
                                       summary, reloadTotal, reloadTotal, reloadFound, reloadNotes);
                    }
                    log("Applied cached configuration: " + std::to_string(outputs.size()) +
                        " outputs; animated=" + std::to_string(animation) +
                        "; errors=" + std::to_string(failed) + ".");
                    {std::lock_guard lock(mutex); appliedRevision=applyingRevision;}
                    SetEvent(readyEvent.h);
                    if (!identify.empty() && session) {
                        while (!interrupted() && Clock::now() - began < std::chrono::milliseconds(1200)) {
                            double ms =
                                std::chrono::duration<double, std::milli>(Clock::now() - began).count();
                            for (auto &s : outputs) {
                                auto &rgb = s.renderer.render(ms);
                                session->send(s.plan, s.actual, rgb, s.packet);
                            }
                            if (WaitForSingleObject(updateEvent.h, 33) == WAIT_OBJECT_0)
                                break;
                        }
                        for (auto &s : outputs)
                            try {
                                session->restore(s.plan, s.actual, {});
                            } catch (...) {
                            }
                        dirty = true;
                        continue;
                    }
                    if (!animation && (!session || !session->driver))
                        release();
                    if (failed && !suspended)
                        schedule();
                    else
                        retries = 0;
                } catch (const std::exception &ex) {
                    release();
                    if (activeReload)
                        reloadProgress(activeReload, "error", std::string("Помилка оновлення: ") + ex.what(),
                                       0, reloadTotal, reloadFound, reloadNotes);
                    statuses(results, ex.what());
                    log(std::string("Apply failed: ") + ex.what());
                    SetEvent(readyEvent.h);
                    if (!suspended)
                        schedule();
                }
            }
            if (interrupted())
                break;
            if (retryAt && Clock::now() >= *retryAt) {
                dirty = true;
                continue;
            }
            DWORD timeout = INFINITE;
            if(session && session->driver) timeout=2000;
            if (retryAt) {
                auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(*retryAt - Clock::now()).count();
                timeout = std::min(timeout,DWORD(std::clamp<int64_t>(ms, 1, 30000)));
            }
            DWORD count = 2;
            HANDLE waits[] = {stopEvent.h, updateEvent.h, timer.h};
            if (live && timer) {
                LARGE_INTEGER due{};
                due.QuadPart = -std::max<int64_t>(10000, 10000000 / current.fps);
                if (!SetWaitableTimer(timer.h, &due, 0, nullptr, nullptr, FALSE))
                    throw std::runtime_error(winError("Frame timer"));
                count = 3;
            }
            DWORD event = WaitForMultipleObjects(count, waits, FALSE, timeout);
            if (event == WAIT_OBJECT_0)
                break;
            if (event == WAIT_OBJECT_0 + 1) {
                retries = 0;
                dirty = true;
                continue;
            }
            if (event == WAIT_TIMEOUT) {
                if(session && session->driver) try { session->driver->check(); }
                catch(const std::exception &e) {
                    for(auto &r:results) if(r["Id"].get<std::string>().rfind("driver:",0)==0) { r["Success"]=false;r["Message"]=e.what(); }
                    statuses(results,"З’єднання втрачено, повторне підключення");
                    release();schedule();
                }
                continue;
            }
            if (event != WAIT_OBJECT_0 + 2)
                throw std::runtime_error(winError("Engine wait"));
            if (!session)
                continue;
            bool failed = false, sent = false;
            try {
                if (Clock::now() >= checkAt) {
                    bool native = std::any_of(outputs.begin(), outputs.end(),
                                              [](auto &s) { return s.active && s.plan.device.native(); });
                    session->check(current, native);
                    checkAt = Clock::now() + std::chrono::seconds(2);
                }
                double ms = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
                for (auto &s : outputs)
                    if (s.active && s.plan.animated())
                        try {
                            auto &rgb = s.renderer.render(ms);
                            if (rgb != s.previous) {
                                session->send(s.plan, s.actual, rgb, s.packet);
                                std::copy(rgb.begin(), rgb.end(), s.previous.begin());
                                sent = true;
                            }
                        } catch (const std::exception &ex) {
                            s.active = false;
                            failed = true;
                            for (auto &r : results)
                                if (r["Id"] == s.plan.device.id) {
                                    r["Success"] = false;
                                    r["Message"] = ex.what();
                                }
                            log(s.plan.device.id + ": " + ex.what());
                        }
            } catch (const std::exception &ex) {
                failed = true;
                for (auto &r : results) {
                    r["Success"] = false;
                    r["Message"] = ex.what();
                }
                for (auto &s : outputs)
                    s.active = false;
                log(std::string("Session invalidated: ") + ex.what());
            }
            if (sent)
                frames++;
            if (failed) {
                statuses(results, "З’єднання втрачено, повторне підключення");
                if (!retryAt)
                    schedule();
            }
            if ((!session || !session->driver) && std::none_of(outputs.begin(), outputs.end(),
                             [](auto &s) { return s.active && s.plan.animated(); }))
                release();
        }
        if (session) {
            try {
                bool native = std::any_of(outputs.begin(), outputs.end(),
                                          [](auto &s) { return s.active && s.plan.device.native(); });
                session->check(current, native);
                for (auto &s : outputs)
                    if (s.active && s.plan.animated())
                        try {
                            session->restore(s.plan, s.actual, s.plan.style.output());
                        } catch (const std::exception &ex) {
                            log(std::string("Restore failed: ") + ex.what());
                        }
            } catch (const std::exception &ex) {
                log(std::string("Restore skipped: ") + ex.what());
            }
        }
    } catch (const std::exception &ex) {
        statuses(results, ex.what());
        log(std::string("Fatal engine error: ") + ex.what());
        SetEvent(readyEvent.h);
        if (onFatal)
            try {
                onFatal();
            } catch (...) {
            }
    }
    release();
    log("Lighting worker stopped; frames=" + std::to_string(frames.load()) + ".");
}
} // namespace vlr

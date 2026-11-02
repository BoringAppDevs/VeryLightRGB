#pragma once
#include "core.hpp"

namespace vlr {
// Private child worker, bounded JSONL over pipes. No TCP/SDK server.
class Driver {
    Handle process, job, input, output;
    std::string buffered;
    Json devices = Json::array();
    Json request(const Json &value, DWORD timeout = 10000);
    static std::vector<Descriptor> descriptors(const Json &devices);
  public:
    explicit Driver(bool memoryTest = false);
    ~Driver();
    Driver(const Driver &) = delete;
    Driver &operator=(const Driver &) = delete;
    std::vector<Descriptor> discover(bool excludeBoard, bool excludeGpu, std::vector<std::string> &notes);
    std::vector<Descriptor> open(const std::vector<Plan> &targets);
    void prepare(const Plan &target);
    void frame(const Descriptor &target, const Bytes &rgb);
    void check();
    Json testRequest(const Json &value) { return request(value); }
};
std::vector<Descriptor> discoverDrivers(const std::vector<Descriptor> &native, std::vector<std::string> &notes);
}

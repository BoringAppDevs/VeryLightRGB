#pragma once
#include "core.hpp"

namespace vlr {
std::wstring pipeName();
void setTestPipeNamespace(const std::wstring &suffix);
Json rpc(const Json &command, DWORD timeout = 5000);

class PipeServer {
    Handle stopEvent;
    std::thread thread;
    std::function<Json(const Json &)> handler;
    void run() noexcept;

  public:
    explicit PipeServer(std::function<Json(const Json &)> callback);
    ~PipeServer();
    void stop();
};
} // namespace vlr

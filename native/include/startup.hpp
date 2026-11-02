#pragma once
#include "core.hpp"

namespace vlr {
std::wstring startupTaskName();
std::wstring startupXml(const std::wstring &executable, const std::wstring &sid,
                        const std::wstring &directory);
bool startupEnabled();
void setStartup(bool enabled);
void removeOwnedStartup();
void stopInstallation(const std::wstring &target);
void reportBackgroundFailure(const std::string &reason) noexcept;
void createShortcut(const std::wstring &destination);
} // namespace vlr

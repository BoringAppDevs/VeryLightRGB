// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
// hueplusplus selects its POSIX branch under MinGW. These wrappers only update this process's TZ.
inline int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name, value);
}
inline int unsetenv(const char *name) { return _putenv_s(name, ""); }

# Copyright and licensing

Copyright (C) 2026 Very Light RGB contributors.

Very Light RGB's original application, installer, scripts, documentation and assets
are free software: you may redistribute and modify them under the GNU General
Public License, version 3 or (at your option) any later version
(`SPDX-License-Identifier: GPL-3.0-or-later`). The complete license is in `LICENSE`.
The software is provided WITHOUT ANY WARRANTY. See that license for details.

This grant covers original project work, not third-party code or trademarks.
Existing per-file licenses and copyright notices take precedence for those files.
In particular:

* OpenRGB-derived code and the original `device-db` bridge/experimental adapters
  retain their GPL-2.0-or-later notices; the combined driver build is distributed
  under GPLv3, compatible with its LGPLv3 and Apache-2.0 dependencies.
* `native/vendor`, `device-db/licenses`, the pinned upstream source archives and
  `third_party/openrgb` retain their own notices and licenses. See
  `native/THIRD-PARTY-NOTICES.txt` and `device-db/PROVENANCE.md`.
* The public release catalog is derived from licensed OpenRGB sources. SignalRGB
  research snapshots and source-derived layouts/predicates remain in ignored
  `local/` storage; no license for them is claimed and they are not released.

Releases include corresponding source and build scripts in `SOURCE.zip`, plus
the native core's original dependency archives under `native-drivers/sources`.
You may rebuild or replace the separately loaded transport DLLs. No signing key
is needed to run a modified Very Light RGB application or driver-core build.
Hardware firmware and optional kernel drivers remain subject to their own terms.

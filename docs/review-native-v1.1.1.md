# Review and installation verification — 1.1.1

Reviewed the native worker bridge, engine application/retry paths, startup task
ownership, installer lifecycle, release contents and source/license provenance.
The issues below were fixed before the verified release was packaged.

| Priority | Finding | Resolution |
|---|---|---|
| P1 | The old installer stopped SignalRGB and removed its startup value. | Replaced with a per-user Setup.exe; unrelated processes and Run values are left alone. |
| P1 | First-migration logic could override current settings with an older legacy profile. | Removed that installer override. Every confirmed edit is saved atomically by the agent; existing current settings take precedence in `Config::load`. Upgrade preserves the complete settings file. |
| P1 | Opening all additional driver targets in the session constructor could prevent working MSI/Gainward outputs from applying when a new driver failed. | Driver initialization is deferred to that provider's outputs; its initialization error stays local to those outputs. A deliberately invalid detector was tested alongside MSI/GPU. |
| P1 | The conflict guard could treat an already-exited status CLI in a Toolhelp snapshot as another RGB owner, and compared Windows image paths with case sensitivity. | Inspect only candidate processes, skip verified exited PIDs, and compare the host image without case sensitivity. Real install/upgrade checks now pass while status clients poll. |
| P1 | The research catalog included source-derived SignalRGB expressions/layouts without a repository-wide license grant. | Public import now contains licensed OpenRGB material only: 2204 records. The full 3042-record research import stays under ignored `local/device-db-research`. No executable driver coverage was removed. |
| P2 | Shipped HIDAPI/libusb DLLs depended on a separately installed MSVC runtime; transport DLL corresponding sources were incomplete. | Rebuild all three transport DLLs from pinned source with LLVM-MinGW. Release contains original source archives, build instructions and license texts. Import-table tests reject MSVC/C++ runtime DLL dependencies. |
| P2 | A language-only edit could prematurely advance `AppliedRevision` while a lighting revision was still pending. | Advance immediately only after the previous revision completed; otherwise queue application of the latest revision. |
| P2 | There was no standard uninstall path; blindly removing the named task could affect a portable copy's startup task. | Setup registers Windows uninstall support. The uninstall helper checks the task executable before removing it; settings remain on disk. |
| P2 | An upgrade could restore an old Setup startup choice after the user disabled startup in the app. | Setup reads current startup state for its initial checkbox; explicit unattended task arguments still take precedence. |
| P2 | Driver pipe ownership relied on default Windows permissions. | Explicit current-user/SYSTEM ACL plus first-instance pipe creation. |

## Results

* Native C++ suite: **1370 assertions passed**, including the real child process
  with memory-only controllers and two independent zones.
* Database/core suite: **12 tests passed**, covering source hashes, matching,
  protocol constants, malformed requests, frame bounds, no TCP listener and DLL
  import dependencies.
* `scripts/verify-clean-install.ps1`: **21 checks passed** for absence of previous
  files/settings/task; install; runtime bundle check; least-privilege startup;
  first discovery of seven outputs; successful default selected outputs; failure
  isolation for MSI/GPU; upgrade with a running agent/menu; startup opt-out;
  retained color/language; startup opt-out retained by an unattended upgrade;
  real menu rendering; foreign-task ownership;
  uninstall while running; retained settings; unchanged unrelated Run values;
  and exact restoration of the original live configuration.
* The installed 1.1.1 Razer integration also passed 22 seconds of static keepalive,
  off/on and recovery after terminating its owned worker, with no TCP connections
  and restoration of the original configuration. Report:
  `local/native-live-check-c4345d26f2d448ca9060381d655a737a/report.json`.
* Successful host report:
  `local/clean-install-8907573de17e41de86f624f4309035fa/report.json`.
  Logs and user settings are local evidence and are excluded from releases.

The clean-install test temporarily moved the complete existing installation,
current/legacy data directories and startup task out of the way, then ran the
actual installer. It restored them afterward and installed 1.1.1 using the
original configuration. This is a **clean per-user application state on the
existing Windows host**, not a freshly provisioned Windows VM. It does not
claim reboot, sleep/resume or every hardware model was tested.

## Release and source

`scripts/build-installer.ps1` produces the per-user Setup.exe and corresponding
source ZIP; `scripts/package-native.ps1` produces the portable archive. Each
artifact has a SHA-256 sidecar. Setup is currently unsigned by this project.
The build-tool download is independently SHA-256 and Authenticode checked.

Project-owned work defaults to **GPL-3.0-or-later**, with original per-file and
third-party exceptions described in `COPYRIGHT.md`. GPL-2.0-or-later OpenRGB
notices, LGPL dependencies, MIT/BSD/Apache notices and original authorship are
preserved. `SOURCE.zip` and the driver source directory accompany binaries.

All six shipped PawnIO modules were byte-for-byte matched against official
PawnIO.Modules **0.2.11** release binaries. The corresponding source is pinned at
`52a7e536dff3e53c96917a28caac5e0fa6510696`; hashes are in
`device-db/core/module-provenance.json`. No kernel driver is installed.

GitHub publication has not been performed. `local/`, build outputs and personal
settings/logs remain excluded by `.gitignore`.

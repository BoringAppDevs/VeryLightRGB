# Source provenance

The exact inputs and SHA-256 digests are in `sources.lock.json`. Importing does not execute, dynamically import, or evaluate upstream code. Native operation does not load JavaScript or C++ from the database.

* OpenRGB: official Codeberg repository, revision `f81fd32259fdd1cdcf506c9797c4be3730dfa0b3`. Full original archive is preserved in `upstream/`. Its notices and GPL text remain in that archive; `licenses/OpenRGB-GPL-2.0.txt` is a convenience copy. Every extracted source file has its own hash in `source-files.json`.
* SignalRGB: public `signalrgb/signal-plugins` repository, `Development` revision `2343f7f7c305d592132a380a7ecc6821154a26c2`. `master` was older at the time of import. The full original archive is retained in the local research cache `local/device-db-sources/signalrgb.tar.gz`. No repository-wide license file was present in this snapshot; original per-file notices are preserved in the cache. No SignalRGB plugin is embedded in or executed by the native EXE.
* The native tool uses the existing nlohmann/json header from `native/vendor`; its MIT notice is copied into the standalone output directory.

Code under `device-db/native`, `device-db/tools`, and `device-db/tests` is provided under GPL-2.0-or-later. Do not infer a license grant for upstream SignalRGB source from this statement.

## Executable wire formats

The following pinned OpenRGB files were inspected when implementing `native/protocol.hpp`:

* `Controllers/CorsairLightingNodeController/CorsairLightingNodeController.h`: packet IDs, report size, software/hardware port state.
* `Controllers/CorsairLightingNodeController/CorsairLightingNodeController.cpp`: planar RGB chunks of 50 LEDs, commit `0x33/0xFF`, port state `0x38`, bounded reply reads, 5-second keepalive.
* `Controllers/CorsairLightingNodeController/CorsairLightingNodeControllerDetect.cpp`: the seven exact USB IDs.
* `Controllers/CorsairLightingNodeController/RGBController_CorsairLightingNode.cpp`: the upstream 204-LED zone limit.
* `Controllers/QMKController/QMKOpenRGBController/QMKOpenRGBController.h`: 65-byte output reports, commands, mode and response offsets.
* `Controllers/QMKController/QMKOpenRGBController/QMKOpenRGBBaseController.cpp`: protocol/device metadata handshake and non-persistent mode selection.
* `Controllers/QMKController/QMKOpenRGBController/QMKOpenRGBRev9Controller/QMKOpenRGBRev9Controller.cpp` and `QMKOpenRGBRevBController/QMKOpenRGBRevBController.cpp`: sequential RGB chunks, 20 LEDs per packet.
* `Controllers/QMKController/QMKOpenRGBController/QMKOpenRGBRevDController/QMKOpenRGBRevDController.cpp`: indexed RGB chunks, 15 LEDs per packet.
* `Controllers/QMKController/QMKOpenRGBController/QMKOpenRGBControllerDetect.cpp`: firmware dispatch for 9/B/C/D. Revision E deliberately remains unsupported because its direct-mode index is dynamic.

These are executable experimental modules with software tests. No physical validation of the new Corsair/QMK modules has been performed. The existing application's MSI/Gainward validation does not transfer to these modules.

## Extraction boundaries

Registrations, variants, and generic detector templates are counted separately from distinct display names. Shared IDs can appear in several source implementations. Wooting registrations expand to three explicit emulation variants. Dynamic QMK templates keep their unresolved user-configured IDs.

Numeric constants use static symbol resolution with local includes and unambiguous global fallback. Function-like macros other than the explicitly implemented Wooting registration expansion are not evaluated. Conditional compilation branches are retained as source context; this is not a full C++ preprocessor. SignalRGB endpoint predicates are retained verbatim for review rather than simplified into guessed matches. Dynamic layouts and identifiers remain unresolved. Custom detection, firmware negotiation, privileged transports, and initialization can require additional logic beyond the extracted IDs.

`nativeAdapter` is a route to the standalone experimental implementation. It never activates a device automatically. A catalog match alone is never permission for a hardware write. No database entry is marked `hardwareVerified`.

## Full driver core

The server-free provider is built from `hello-nexus/openrgb-headless` revision `cc71485c803842f065353e65426e3a28dca1e6f3` (GPL-2.0-or-later), with MbedTLS 3.2.1 (Apache-2.0) rebuilt from its original source. Exact archives and hashes are in `core/sources.lock.json`; originals are preserved under `upstream/`. Reproducible local patches and the bridge are in `tools/build-core.mjs`, `core/direct_main.cpp`, and `core/mingw_compat.hpp`. See `core/README.md` for the API and verification limits.

The native core includes the original controllers and their dependencies: nlohmann/json (MIT), CRC++ (BSD-style notice), hueplusplus (LGPL-3.0), cpp-httplib (MIT), libe131 (Apache-2.0), mdns (public domain), stb (upstream notices in headers), NVIDIA/AMD API interface definitions and interop code. Original per-file authorship/notices and dependency headers are preserved in the complete source archive. Extra convenience copies of applicable license texts are in `licenses/`.

All three transport DLLs are rebuilt by `tools/build-transports.mjs` from the exact additional archives in `core/sources.lock.json`: HIDAPI hotplug (BSD-3-Clause alternative selected), libusb 1.0.27 (LGPL-2.1-or-later), and PawnIOLib (LGPL-2.1-or-later). The generated import libraries and DLLs use LLVM-MinGW/UCRT without a separate MSVC runtime. The unchanged PawnIO module binaries from the pinned headless archive are accompanied by upstream module source and LGPL notices. No PawnIO kernel driver is installed. The original driver archive preserves its upstream binary copies as historical input; those copies are not the deployed transport DLLs.

From 1.1.1, the public catalog/protocol index contains only licensed OpenRGB-derived material (2204 records). SignalRGB source-derived predicates, layouts and protocol expressions are confined to ignored `local/device-db-research` output generated with `import.mjs --research`. The public source lock retains a provenance reference, not a grant to redistribute those plugins.
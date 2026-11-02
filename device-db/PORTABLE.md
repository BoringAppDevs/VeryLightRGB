# Very Light RGB — serverless device database and native tools

Windows 10/11 x64. The transport DLLs are rebuilt from source without a separate MSVC runtime. Extract this archive into a new folder. Nothing is installed or started automatically.

* `database/`: 2204 licensed source-backed records / 1968 display names; provider mapping and protocol metadata. Unlicensed research inputs are excluded.
* `light/native-device-db.exe`: offline search, read-only HID inventory, packet planning and focused Corsair/QMK modules.
* `core/native-driver-core.exe`: Qt-free driver corpus with 2144 compiled detector registrations and a local JSONL API. It never starts the OpenRGB SDK server.
* `source/`: tool/bridge sources, tests, pinned original OpenRGB/headless/MbedTLS archives and build instructions.
* `licenses/`: license texts and original component notices. See `source/device-db/PROVENANCE.md` and `source/device-db/core/README.md`.

```powershell
.\light\native-device-db.exe summary --db .\database
.\light\native-device-db.exe find Razer --db .\database
.\light\native-device-db.exe inventory --db .\database
.\light\native-device-db.exe plan .\examples\corsair-plan.json
.\core\native-driver-core.exe detectors
.\core\native-driver-core.exe --self-test
```

These commands do not change RGB. `inventory` reads HID descriptors only. `detectors` lists compiled registrations only.

The direct driver bridge is started with `core/native-driver-core.exe stream ABSOLUTE_ISOLATED_DATA_DIRECTORY`. It accepts exact detector selections, controller IDs and frames via stdin; the complete API is documented in `source/device-db/core/README.md`. Open/init can write hardware configuration, so it is distinct from read-only inventory. New drivers are experimental and have not been physically verified here. No catalog entry is marked hardware-verified.

This standalone database package does not install or update the main application. Very Light RGB 1.1.0 separately integrates the native provider in Native Auto / Offline and includes it in the main portable ZIP. Real RGB operation needs the matching device protocol/firmware and any required existing system drivers. The tools do not install drivers, update firmware or start other RGB applications.

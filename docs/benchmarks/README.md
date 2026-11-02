# Windows resource measurements

Measured on November 2, 2026 using Very Light RGB 1.2.0 and the unmodified OpenRGB 1.1 Windows x64 release. The purpose is to compare Native mode with OpenRGB SDK mode in Very Light RGB, rather than to establish a universal memory requirement or rank all features of either project.

## Workload and method

- Windows 11 x64; 16 logical processors.
- One MSI motherboard ARGB header, manually configured for 12 LEDs, plus one Gainward RTX 5080 Phoenix lighting zone.
- ARGB: Sequential fill, white, 50% brightness, 200 ms step time, 30 FPS. GPU: static `#64E6C6` at 100% brightness.
- The menu was closed during both runs. No additional native-driver devices were selected, so no driver-core worker was running in the native case.
- Approximately 60 seconds per run, with working set and private commit sampled once per second. CPU is the increase in process CPU time divided by elapsed wall time and the logical-processor count.
- SDK client and server were sampled concurrently. OpenRGB ran as a CLI SDK server with two detected controllers, no desktop UI, and no Effects Plugin. Other lighting controllers were not selected.
- No working-set trimming was requested. Test configurations were isolated from the user's settings. Temporary processes were stopped and the original lighting configuration and running/stopped state were restored afterward.

The measurement implementation is [measure-resources.ps1](../../scripts/measure-resources.ps1). The [aggregate data](windows-memory.json) deliberately omits PIDs, computer names, account identifiers, serial numbers, personal paths, and configuration dumps.

## Results

| Process/configuration | Mean working set | Mean private commit | CPU, whole machine |
| --- | ---: | ---: | ---: |
| Very Light RGB, native | 14.8 MiB | 2.4 MiB | 0.013% |
| Very Light RGB, SDK client | 13.8 MiB | 2.2 MiB | 0.011% |
| OpenRGB 1.1, CLI SDK server | 30.7 MiB | 9.5 MiB | 0.047% |
| SDK client + server, arithmetic sum | 44.5 MiB | 11.7 MiB | 0.058% |

Native sampling lasted 60.41 seconds; concurrent SDK sampling lasted 60.43 seconds. MiB means 1,048,576 bytes, despite the historical `MB` field names in the measuring script.

Working sets can share memory pages, so summing process working sets can count shared pages more than once. Small CPU percentages are sensitive to scheduling, timer resolution, and sample duration. A different machine, device set, effect, UI state, or firmware can change these figures. Native devices that need the separate driver-core worker add its memory and CPU usage; this measurement does not include that configuration.

## What the comparison does and does not cover

The two runs used the same app-level lighting workload through different transports. This is not a measurement of standalone OpenRGB's software effects or GUI. OpenRGB is itself designed to be lightweight; its broader platform support, SDK, and plugin ecosystem serve use cases beyond this benchmark.

Feature references: [OpenRGB overview](https://openrgb.org/), [OpenRGB Effects Plugin](https://openrgb.org/plugin_effects.html). No unsourced RAM numbers for other applications are included.

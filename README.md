<div align="center">
  <img src="native/assets/app-icon.png" width="80" alt="Very Light RGB logo">
  <h1>Very Light RGB</h1>
  <p><strong>Your lighting. Your way. A lighter Windows tray.</strong></p>
  <p>Make your setup yours with a free, open-source RGB app built to stay small.</p>
  <p>
    <a href="https://github.com/BoringAppDevs/VeryLightRGB/releases/latest/download/VeryLightRGB-Setup-win-x64.exe"><strong>Download for Windows x64</strong></a>
    &nbsp; · &nbsp;
    <a href="https://github.com/BoringAppDevs/VeryLightRGB/releases/latest">Portable &amp; other downloads</a>
    &nbsp; · &nbsp;
    <a href="https://github.com/BoringAppDevs/VeryLightRGB/issues">Feedback &amp; device requests</a>
  </p>
  <p>
    <a href="https://github.com/BoringAppDevs/VeryLightRGB/actions/workflows/release.yml"><img src="https://github.com/BoringAppDevs/VeryLightRGB/actions/workflows/release.yml/badge.svg" alt="Windows build"></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-purple" alt="GPL-3.0-or-later"></a>
    <img src="https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-blue" alt="Windows 10 and 11">
  </p>
</div>

**Nine built-in effects. 2,000+ device detection entries. Just 14.8 MiB of background RAM in our Native mode benchmark.**

Pick your devices, choose your colors, and let your setup shine. Very Light RGB puts everyday lighting controls in a compact, keyboard-friendly menu, then slips back into your system tray. No account. No cloud. No subscription.

![Very Light RGB's compact English lighting menu](docs/images/main-menu.png)

## Why Very Light RGB?

- **Keep your resources for what you love.** Native C++ and Win32 keep the app compact. In our measured workload, Native mode used about one-third of the RAM of running the same app through an OpenRGB SDK server.
- **Get effects out of the box.** Choose Solid color, Sequential fill, Orbiting dark block, Breathing, Rainbow ("Gay" in the menu), Two-color fade, Comet, Wave, or Twinkle. No effects plugin to install.
- **Make every output your own.** Set colors, brightness, and effects together or independently. Match your fans to your GPU, or give each its own look.
- **Dial in the details.** Pick from 32 colors or enter a HEX value. Adjust brightness from 0–100%, including Rainbow. Tune speed, direction, and a second color where the effect supports them.
- **Set it once. Enjoy it every day.** Settings save automatically. Optional Windows startup, reconnect handling, and crash recovery help keep your lighting ready.
- **Stay in control.** Lighting works offline. The installer includes the app and native drivers, with no separate OpenRGB installation, Node.js, or .NET runtime needed for Native Auto mode.

## Up and glowing in minutes

1. **[Download the Windows installer](https://github.com/BoringAppDevs/VeryLightRGB/releases/latest/download/VeryLightRGB-Setup-win-x64.exe).** Choose English or Ukrainian and optionally enable startup with Windows. Leave **Launch Very Light RGB** checked to start right away.
2. Open **Devices** and select the outputs you want to control.
3. Open **Lighting**, find your favorite look, and press **Q** to hide the menu. Your lights keep running.

Reopen the menu from the tray icon whenever you want a change. Prefer a portable app? [Download the ZIP](https://github.com/BoringAppDevs/VeryLightRGB/releases/latest/download/VeryLightRGB-win-x64.zip), extract it, and keep the included `native-drivers` folder beside the EXE.

**Requirements:** Windows 10 or 11, x64, and compatible RGB hardware.

## Hardware compatibility

**A broad device catalog, built in.** Very Light RGB includes **2,204 catalog entries across 198 controller families**, backed by an OpenRGB-derived native driver core with **2,144 compiled detector registrations**. Explore lighting across motherboards, graphics cards, keyboards, mice, and RGB controllers from one app.

Open **Devices** to discover your setup and choose what to control. For supported ARGB headers, you can set the strip length manually from **1–120 LEDs**.

Compatibility depends on the model, firmware, and connection mode; catalog entries are detection rules, not a count of individually verified devices. Missing a device or seeing unexpected behavior? **[Open a compatibility issue](https://github.com/BoringAppDevs/VeryLightRGB/issues)** with its model and connection type to help improve support. Use one lighting app per device at a time.

## Less overhead. More room for everything else.

In our Windows benchmark, Native mode averaged **14.8 MiB of RAM** and **0.013% CPU** while running an ARGB animation alongside a static GPU color, with the menu closed.

| Same lighting workload | Average RAM | Average CPU |
| --- | ---: | ---: |
| **Very Light RGB · Native mode** | **14.8 MiB** | **0.013%** |
| Very Light RGB + OpenRGB SDK server | 44.5 MiB | 0.058% |
| **Native mode advantage in this test** | **About 67% less RAM** | **About 78% less CPU time** |

Native mode lets supported devices run directly, without a separate SDK server. Additional devices use the bundled native driver worker when needed.

*Measured for 60 seconds on Windows 11 with 16 logical processors. RAM is mean working set; CPU is the share of the whole machine. The SDK total includes the app and OpenRGB 1.1 CLI server. Results depend on hardware and selected devices; the Native run did not need the extra driver worker. This is a comparison of this app's two connection modes, not a benchmark of OpenRGB's desktop UI. [Method and results](docs/benchmarks/README.md).*

## An easy way to enjoy the OpenRGB ecosystem

[OpenRGB](https://openrgb.org/) provides the open-source driver foundation behind much of our device coverage. Very Light RGB brings that work into a focused Windows experience:

| What you want | What Very Light RGB gives you |
| --- | --- |
| Install and start changing your lights | One Windows installer with native drivers included |
| Animated lighting without extra setup | Nine effects built into the app |
| A small app you can leave running | Tray controls, an on-demand menu, and direct Native mode |
| Different looks across your setup | Independent colors, brightness, and effects per output |
| Keep your existing OpenRGB setup | Optional SDK mode connects to your OpenRGB server |

Already use OpenRGB? Keep your existing server and try Very Light RGB as a compact lighting front end. Starting fresh? Native Auto mode is ready out of the box.

## Download once. Keep it fresh.

**[Get the latest release](https://github.com/BoringAppDevs/VeryLightRGB/releases/latest)** for the Windows installer, portable ZIP, source archive, and SHA-256 checksums. Install a newer build over the existing app to keep your settings.

Every push to `main` automatically builds and tests the app, then publishes the downloads to GitHub Releases. The installer is currently unsigned; `SHA256SUMS.txt` is included with each release.

## Help make it better

Found a bug, have an effect idea, or want to help a device work better? **[We'd love your feedback](https://github.com/BoringAppDevs/VeryLightRGB/issues).** Include the device model, connection mode, and what happened. Remove serial numbers and personal information from any diagnostics you share.

Code contributions are welcome too. See [Building and releases](docs/BUILDING.md) to get started.

## Open source, with thanks

Very Light RGB is free software under **[GPL-3.0-or-later](LICENSE)**. Thanks to OpenRGB and its contributors for the driver work that helps make broad device coverage possible. Third-party components retain their own licenses; see [COPYRIGHT.md](COPYRIGHT.md). Corresponding source and dependency notices ship with the release packages.

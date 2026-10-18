# Development environment

Initialized 2026-10-18 on macOS arm64. Repository: `/Users/floo/Documents/Coding/Waveshare/rsvpnano`; branch `dev/library-planning`. Fork: https://github.com/floo-one/rsvpnano. `origin` points to the fork, `upstream` to https://github.com/ionutdecebal/rsvpnano. Full upstream history and all four pinned submodules retained.

## Tooling and reproducible entry points

- Git/GitHub CLI authenticated as `floo-one` (network access required in sandbox).
- Python 3.13 isolated at `.local/venv`; PlatformIO Core 6.2.0 plus upstream `freetype-py`, `fonttools`, `uharfbuzz`, `lz4`. `.local/` is ignored. CI uses Python 3.12; local version differs but checks ran on 3.13.
- PlatformIO downloads pioarduino 55.03.39, Arduino-ESP32 3.3.9, Xtensa GCC 14.2 and GDB 17.1 as specified by upstream. Framework and tool caches are in `~/.platformio`.
- Apple Clang 21 / Xcode 26.6 for native C++23 tests. One upstream board fixture has a Clang enum-range incompatibility; see validation.
- Homebrew JDK 17.0.20.1 at `/opt/homebrew/opt/openjdk@17`; no system Java symlink or shell-profile edits. Repository Gradle wrapper 8.14.4, Kotlin 2.4.10 and Compose 1.11.1 manage companion dependencies.
- Node 26.11.0 and Bun 1.3.14 already present. G-Stack accepts this Bun but warns that 1.4.2+ is its tested version. Existing setup/build succeeded; no unrelated global Bun upgrade.
- Android SDK was not found in standard locations; Android build not validated. Xcode exists; iOS companion build/simulator checks not run in this firmware/library initialization. Web companion development is installed and checked. Neither mobile environment is reported ready.
- CMake/Ninja are not required as standalone tools by the commands validated here; PlatformIO/Gradle own their needed build tooling. No separate ESP-IDF or Arduino IDE installation is needed.

```sh
cd /Users/floo/Documents/Coding/Waveshare/rsvpnano
source .local/venv/bin/activate
pio test -e native_test
pio run -e waveshare_esp32s3_touch_lcd_349_rev2
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home ./gradlew checkWeb --no-daemon
```

Fresh machine: clone the fork with `--recurse-submodules`, create a virtual environment, install the Python dependencies above, install JDK 17 and use the committed wrapper. Follow upstream [README](../../README.md) for normal setup and the existing Android/iOS guides for mobile-specific tooling. Do not copy local build outputs into Git.

## Hardware, flashing and debugging

User confirmed **revision 2** in this chat. Upstream defaults to rev1: always supply the rev2 environment explicitly. Source revision headers differ in backlight (rev1 GPIO 8; rev2 GPIO 42), touch IRQ (42 vs 8) and display reset (21 vs expander-controlled). Manufacturer identification: V2 case QC label or PCB `Rev1.1` marking. See [Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49). Its feature summary has a 172×320 typo; the LCD parameter table and upstream board source agree on 172×640.

No device was probed, reset, flashed or connected to a serial monitor in this task. Confirmation is user-reported, not electrical validation. Future upload requires explicit authorization and backup of the SD card; keep the correct target and fork artifact identity. PlatformIO/esptool provide upload; the monitor is 115200 baud with exception decoding. GDB/OpenOCD support is toolchain capability, not a verified debug connection. Debugger pin/probe availability must be checked on the real revision before use.

Compile outputs remain ignored under `.pio/build/<environment>/`. Web distribution is `build/webSite`. See [validation](validation.md) for actual outcomes, not merely commands that exist.

## License and dependency notes

Upstream `LICENSE` is MIT, copyright 2026 RSVP Nano contributors; it remains unchanged. Retain its notice and bundled library notices. Submodules: HarfBuzz `4c2aa804…` (`COPYING`), PDFio `b468d164…` (`LICENSE`), SheenBidi `9c048a32…` (`LICENSE`), zlib `da607da7…` (`LICENSE`). G-Stack is separately MIT and remains a global development tool, not embedded firmware. A release still needs normal dependency/license accounting; this setup does not relicense third-party assets.

Most core versions are pinned upstream; XPowersLib remains a moving Git dependency and tooling resolves transitive packages. Do not claim full reproducibility from the Git SHA alone. Preserve logs/package manifests when reproducing a release.

## Library foundation tests

Run `.local/venv/bin/pio test -e native_library_test` for the real organization codec/recovery and fault-injection suite. The focused native environment also compiles the installed ESP32 SHA implementation through `tools/pio_native_library.py`; install the project framework with a board build first. See [foundation validation](library-foundation.md). This command does not upload firmware.

Run `.local/venv/bin/pio test -e native_reading_status_test` for reader lifecycle, status commands and Start again recovery. See [reader integration validation](reading-status.md) for coverage boundaries and sanitizer commands. It uses the same installed framework prerequisite and does not upload firmware.

Run `.local/venv/bin/pio test -e native_feed_organization_test` for registry, matching, shared progress, fair scans and import recovery. See [feed integration validation](feed-organization.md). The same installed ESP32 framework prerequisite applies; no firmware upload is performed.

Run `.local/venv/bin/pio test -e native_library_view_test` for collection projections, sorting, remembered context, source-safe selection and the actual shared screen-data builder. See [library view validation](library-views.md) for coverage and remaining UI/device boundaries. It uses the installed ESP32 framework's SHA implementation; no firmware upload is performed.

Run `.local/venv/bin/pio test -e native_library_ui_test -e native_library_watch_ui_test` for the real library/action renderer and touch tests. These environments use the same installed ESP32 framework prerequisite and host inventory/filesystem adapters as the foundation tests. See [library UI validation](library-ui.md) for command, sanitizer and coverage details. Keep PlatformIO invocations using the same build directory sequential; no firmware upload is performed.

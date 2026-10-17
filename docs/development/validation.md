# Initialization validation

Run date: 2026-10-18 Asia/Bangkok. Firmware/companion baseline: upstream `f9d2a800ed38ce9ff64c6711e8ef2eca5db699fb`, with only setup/docs added. Full command logs are retained locally in ignored `.local/validation/`; this table is the committable evidence summary.

| Check | Result | Evidence / limitation |
|---|---|---|
| Fork/remotes/history/submodules | PASS | GitHub API identifies fork parent; origin/upstream main match baseline; full clone, four submodules clean |
| `pio test -e native_test` | PASS | 222 cases in 16 selected suites; exit 0 |
| Additional native environments | PARTIAL | 57 cases passed across watch/light-sleep/AMOLED suites; board-wiring suite compile error; combined exit 1 |
| `python -m unittest discover -s tools -p test_firmware_targets.py` | PASS | 4 tests |
| Both localization generators `--check` | PASS | Existing generated files current, no changes |
| `python -m unittest localization/test_locale_packs.py` | PASS | 9 tests |
| Font test discovery | PASS | 15 tests |
| `python test/native_canvas/run.py --fetch-library` | PASS | 1,536 text-strip cases, zero failures; newline/prepared-ink checks also zero failures |
| `pio run -e waveshare_esp32s3_touch_lcd_349_rev2` | PASS | exit 0; firmware and factory images created locally in ~191s after command start |
| `JAVA_HOME=... ./gradlew checkWeb --no-daemon` | PASS | 46 browser cases (23 web, 23 conversion), zero failures/errors/skips; staged website; exit 0, ~5min |
| G-Stack official installation | PASS | Claude + Codex current 1.91.125.0; routers/section links resolve; all eight requested skills readable |
| Team hook | PASS | Node syntax and installed-root success execution return `{}` |
| Local independent design-document review | PASS | One sealed/validated round, 9/10, no blocking/minor findings; not implementation approval |
| Local planning sketch | PASS | Rendered and visually inspected; placeholder browser fonts, no device QA |

## Baseline failure: native board-wiring suite on Apple Clang

`native_board_test:test_board_wiring` fails compiling `src/platforms/waveshare_c6_touch_lcd_147/WaveshareC6TouchLcd147.h:57`. The test shim `test/support/driver/gpio.h` defines enum values only through GPIO 3, while the C6 boot pin is 9. Apple Clang 21 rejects the out-of-range enum cast as a constant expression; the subsequent static assertion also fails. This was reproduced on unchanged upstream source. It is not a rev2 firmware compilation failure and does not establish that CI's Linux compiler fails.

No warning was suppressed, test altered or source patched to force a pass. Follow-up: separately verify/fix the host GPIO shim or reproduce with CI's compiler. Preserve this baseline until a focused maintenance change is authorized.

## Build sizing and warnings

Rev2 link report: RAM 124,468 / 327,680 bytes (38.0%); flash 2,948,890 / 6,553,600 bytes (45.0%, app partition). These are static linker figures, **not** runtime heap headroom, PSRAM consumption or whole-device flash usage. Generated images are ignored and were not uploaded.

Firmware build emits upstream C compilation warnings about `-fno-rtti`; companion build emits webpack critical-dependency/peer/deprecation warnings. Builds pass, but are not warning-free. Package resolution includes a moving XPowersLib Git dependency; logs preserve the resolved build. G-Stack Bun 1.3.14 is accepted but below its documented tested 1.4.2+ recommendation.

## Not performed / still required

- Physical revision verification beyond the user's confirmation, serial connection, debug probe validation, any flash/OTA/reset operation.
- Real-device scrolling, touch targets, RTL/CJK fonts, screen/gesture orientation, SD speed/heap/PSRAM, battery and sleep/wake measurements.
- Library-specific native coverage: baseline `native_test` does not include the library renderers/item builder. Add coverage with feature work.
- Rev1 or the full firmware board build matrix; no product code changed, rev2 was the confirmed build target.
- Android/iOS builds and mobile device tests. Android SDK is absent in standard locations; iOS tooling exists but was not exercised.
- Product implementation, deployment, push, PR, merge or release.
- External Claude review: automatic export approval rejected it; user then explicitly chose local-only.

## Setup issues resolved or retained

Restricted-network GitHub auth initially appeared invalid; approved network access verified the existing login and created the fork. Reads during cloning were retried after checkout completed. G-Stack review preparation required creating its output directory first. The old Waveshare wiki URL returned 403; current official docs were used. Sandbox blocked Chromium's macOS process rendezvous; local-only rendering succeeded with approved execution. Native board-shim error remains recorded above. No failed check is counted as a pass.

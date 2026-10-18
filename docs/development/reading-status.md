# Reader status and Start again integration (T02)

Implemented locally following the owner's 2026-10-18 authorization. Hardware remains unflashed. This receipt supersedes T01's statement that the organization module has no application callers; the original foundation receipt remains historical.

## Behavior

- Opening/indexing alone does not label an older item Unread or Reading. Explicit playback or paragraph-browsing activity records Reading. Read remains sticky on ordinary reopening/playback.
- Timed consumption of the final word records Read once. Seeking, companion position updates, final-page rendering and opening at the last word do not complete an item. One-word content requires its full duration; empty content cannot complete.
- Mark read/unread pause the active session, cancel earlier activity events and preserve its bookmark. A fresh explicit start can move Unread to Reading.
- Start again targets a successfully opened item. It validates source identity and the existing bookmark, checkpoints current progress, saves a durable reset intent, atomically writes position zero with typography preserved, commits Reading and publishes the beginning paused. An intent that committed before an interruption is rolled forward on opening, before ordinary bookmark writers can run.

`App::requestReadingAction` and `screens::Action::{MarkRead,MarkUnread,StartAgain}` are integrated command boundaries. **There is no new visible action button yet.** Title-hold access, the shared Item actions screen, the Read-menu entry, selected-library-item opening and collection return contexts belong to T04/T05. This slice does not implement feed matching or the new library presentation.

## Ownership and recovery

`ReadingLoop` emits memory-only activity. App serializes commands in its existing SD-owning background job, validates path/source/index/session generation/activity, and publishes results on the app thread. Full-source checks read bounded chunks and yield periodically. USB/HTTP/serial sessions gate job creation; serial processing also waits while a background job owns storage. Benchmark mode skips organization preparation/dispatch. Startup/remount reconstructs the durable order maximum from available valid item records.

`ReadingOrganization` implements status commands and intent replay. `ReadingStateStore` extracts the existing bookmark codec, retains its public TOML shape, bounds reads to 2048 bytes and uses staged atomic replacement with exact reread verification. Ordinary bookmark reads can use a backup when final is missing. Public position, font and session-mirror writers refuse unresolved organization/restart records. A failed post-commit publication also fences the stale in-memory session.

Failed status commands stay in session memory and retry with the same operation ID on explicit new reading activity; they are not retried every frame. A manual correction supersedes that pending event. Closing/replacing the session clears it; an uncommitted event cannot survive a reboot. A restart failure requires reopening to recover/revalidate before further playback or writes.

First-record creation retains T01's conservative policy: a sole temporary organization/registry record has no committed create intent and is **not promoted automatically**. Interrupted first creation can therefore report corruption/recovery required and block organization changes, while preserving the old bookmark. No reset is inferred from those bytes. T03/T06 recovery tooling must retain this distinction; this implementation does not offer a user-facing repair command for that case. Unsupported schemas, malformed reset snapshots and changed source bytes likewise never authorize overwriting metadata or resetting progress. Content remains available for static viewing when bookmark writes/playback are fenced.

Orders are reserved durably from the existing registry sequence; gaps are harmless. Restart intents retain their reserved order across retries. `last_operation` and intent `order` are additive fields; missing fields retain defaults. Older foundation-only firmware may reject the added fields for mutation, preserving its read-only compatibility behavior. No database, network endpoint or dependency was added.

## Verification

The new native suite compiles the actual `ReadingLoop`, `ReadingProgress`, `ReadingOrganization`, `ReadingStateStore`, organization codec/recovery, filesystem helpers and installed ESP32 SHA implementation. Small adapters supply the indexed word source, preferences, board filesystem and inventory-title boundary. It does not execute the complete App scheduler, ReaderScreen renderer or physical input.

Coverage includes status/bookmark truth tables; timed/one-word/empty completion; seek clocks; chapter rewind after catch-up; identity replacement, session/activity guards; manual correction; paused reset publication; font preservation; failed checkpoint before intent; repeated recovery and unchanged-content reindexing; public position/font/mirror write guards; failed completion retry; cancellation; and interruption/failure schedules. Tests enumerate every mutating reset boundary and every filesystem failure in the existing-record reset path, plus first-record and ordinary bookmark-write interruptions.

Commands:

```sh
.local/venv/bin/pio test -e native_reading_status_test
.local/venv/bin/pio test -e native_library_test -e native_test -e native_watch_test
.local/venv/bin/pio run -e waveshare_esp32s3_touch_lcd_349_rev1 -e waveshare_esp32s3_touch_lcd_349_rev2
PLATFORMIO_BUILD_DIR=.pio/build-reading-sanitized \
PLATFORMIO_BUILD_FLAGS='-DGLZ_DEFAULT_OPTIMIZATION_SIZE -DGLZ_DISABLE_ALWAYS_INLINE -std=gnu++23 -Itest/test_library_organization/support -Itest/support -Isrc -lz -fsanitize=address,undefined -fno-omit-frame-pointer' \
.local/venv/bin/pio test -e native_reading_status_test -e native_library_test
```

Results on 2026-10-18:

| Check | Result |
|---|---|
| New reader status suite | 28/28 passed |
| Metadata foundation | 33/33 passed |
| Existing native regression | 222/222 passed |
| Watch regression | 12/12 passed |
| Reader + foundation under ASan/UBSan | 61/61 passed; no findings; linked ASan runtime verified |
| Final LCD rev1/rev2 builds | Both passed; no upload |
| Whitespace check | Passed |

Local review fixed chapter rewind completion, failed-event retention, strict checkpoint validation before rewriting an older bookmark, and serial acquisition during an SD job. Review was local and in-host, not an independent outside review. One intermediate board build linked mixed objects while the callback signature was being revised; final source rebuild results are recorded above.

Remaining verification: real SD/controller interruption, rev2 touch behavior and performance, complete App/input integration, shared action-screen routes and the broader T06 companion/delete/USB matrix. Firmware compilation and simulated filesystem schedules do not establish physical durability or latency. No flashing, external review/export, push or release was performed.

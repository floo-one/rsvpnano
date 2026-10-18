# Library layout and Item actions (T05)

This slice implements the approved collection UI on top of T01–T04. Hardware remains unflashed. The main target is the user-confirmed Waveshare LCD 3.49 revision 2; native rendering tests and compilation do not establish physical usability or SD durability.

## Visible behavior

The 640×172 layout retains the 136-pixel rail, 12-pixel gap and right power inset. Content is 444×156 pixels: a 26-pixel heading and 130-pixel list viewport containing 52-pixel rows. With three or more items, two full rows and half of the next row are visible. The partial row includes actual title text. Long titles use up to two lines; unknown status and unavailable metadata are identified without inventing progress or publication dates. Only visible rows are prepared/painted; whole text lines are clipped at the viewport boundary.

Library opens with Books, Newsletters and conditional Needs review. Newsletters opens the stable feed chooser, including saved archived feeds and Other articles. Collection views use Back / Unread / Reading / Read. Needs review has Back and an unfiltered list. Existing filtering, sorting, first-visit defaults and remembered per-tab anchors come from T04. List-position save failures are visible and dirty state remains available for a later save retry.

Back from a feed or Other articles returns to the feed chooser; from Books, Needs review or the chooser it returns to Library. Root hardware Back returns to Read. An item opened from a collection records its collection/tab; reader Back restores that context even after other browsing. The root retains global Read / Settings / Device / Focus navigation and power. The watch presentation uses the shared list/input controller with its existing content bounds and dock.

A tap opens only a fully visible row. A partial row can start a drag but cannot open an item or show actions. Movement beyond the existing 20-pixel tolerance permanently cancels activation for that gesture, even after moving back. Scroll follows displacement and clamps to real content without inertia, snapping or empty trailing pages. A 600 ms hold must begin and remain in the title area of a fully visible row. Refresh invalidates the old gesture and canonical selection.

The shared Item actions screen is reachable by title hold and from the Read menu, alongside Chapters and Library. Resume and the font/language control remain. Mark read and Mark unread carry “Keep position”; Start again carries “Reset position to beginning.” Redundant marks are disabled. The opening hold/release cannot operate an action: a fresh press is required, including after a failed attempt. Back returns to the entry context. Marking returns only after persistence succeeds; Start again ends at the beginning, paused.

## Command and recovery boundary

Library actions retain canonical path, full source identity and inventory generation. The App owns serialized jobs for loading action state and applying a mark to an unopened item. Those jobs verify inventory and source bytes. The action-state job presents Checking and supports hardware Back/Open Menu cancellation. It loads explicit status, retains a prepared conservative legacy classification when available, and exposes recovery/unavailable state. The screen renderer and gesture handler perform no filesystem operations.

An unopened item can be marked without conversion, opening a reader, creating a bookmark, or replacing another active book. `ReadingOrganization::apply` accepts a source-only target for Mark read/unread and preserves any existing index identity, memberships and bookmark bytes. Activity and Start again still require a valid indexed identity. Missing, replaced, unsupported or corrupt organization records cannot be silently overwritten. Pending restart intent blocks marking until recovery.

Start again on the active item uses the existing reader command/checkpoint path. For another item, the App first successfully opens/indexes it and checks that the prepared reader still matches the selected source; only then does it enqueue Start again. Reset failures retain the action screen for retry/Back. A blocked or pending restart reopens through the existing recovery path before another reset. The durable intent, legacy-writer guard and source/session checks from T02 remain authoritative. The existing open boundary checkpoints the prior reader outside the UI frame.

Opening or holding an item never starts playback or emits a Reading event. Explicit marking clears pending activity through the existing reader command boundary when acting on the active reader. Inactive-item marking does not disturb that reader. Shared feed memberships still reference one canonical status/bookmark.

New UI strings are generated through the existing localization pipeline. English fallback is available for all new keys; this slice does not invent translations for installed language packs.

## Test scope and local review

`native_library_ui_test` compiles the real regular Library renderer, shared presentation/input code, Item actions, Read menu, immediate UI controls, localization, model, metadata/bookmark stores and reading commands. Its graphics/font metrics, contact samples, inventory and filesystem are host adapters. Tests drive `Context::pollTouch` and actual frame rendering rather than a duplicate input algorithm. The inventory adapter does not emulate SD enumeration, conversion or App scheduling.

The 19 regular UI cases cover 2.5-row geometry and title preview; full/partial/last-row activation; scrolling and out-and-back gestures; title-only holds and consumed release; collection/tab/feed/Needs review navigation; remembered reader origin; same-count refresh; zero/small lists; long Unicode titles; disabled/redundant actions; retries and pending recovery; visible view-save failure/retry; and the retained Read-menu controls. A separate watch case checks the shared library/actions against nine existing watch dimensions.

Five additional reading-command cases cover source-only marking with undecodable bookmark preservation, retained indexed identity and shared memberships, missing-index rejection for reset/activity, replacement/removal guards, and pending-restart recovery. The earlier crash-interruption, stale-identity, sorting, saved-anchor and feed suites remain in the regression set.

The in-host engineering review traced App dispatch, job completion, source validation, bookmark/reset order, fresh-press consumption, collection restoration, generation invalidation, storage ownership and write-failure handling. It fixed a missing visible view-save error, title-origin hold eligibility, and recovery/return-context paths. Sanitizers also found a temporary-theme lifetime bug in the new test fixture; the fixture now owns the theme for the entire UI context lifetime. This is local review, not an independent external review.

App/FreeRTOS scheduling and the real conversion/SD stack are compiled in both firmware builds but are not executed by the native UI harness. Physical fonts, handedness, edge gestures, touch feel, power/USB ownership transitions, large-card latency and heap/PSRAM high-water marks remain integration/device validation work (T06–T08). No release readiness is claimed.

## Validation

Commands (none upload firmware):

```sh
.local/venv/bin/pio test -e native_library_ui_test -e native_library_watch_ui_test \
  -e native_library_view_test -e native_reading_status_test
.local/venv/bin/pio test -e native_library_test -e native_feed_organization_test \
  -e native_test -e native_watch_test
PLATFORMIO_BUILD_DIR=.pio/build-library-ui-sanitized \
PLATFORMIO_BUILD_FLAGS='-DGLZ_DEFAULT_OPTIMIZATION_SIZE -DGLZ_DISABLE_ALWAYS_INLINE -std=gnu++23 -Itest/test_library_organization/support -Itest/support -Isrc -lz -fsanitize=address,undefined -fno-omit-frame-pointer' \
.local/venv/bin/pio test -e native_library_ui_test -e native_library_view_test -e native_reading_status_test
.local/venv/bin/python localization/generate_localization.py --check
.local/venv/bin/pio run -e waveshare_esp32s3_touch_lcd_349_rev1 -e waveshare_esp32s3_touch_lcd_349_rev2
```

Run PlatformIO build/test invocations sequentially when using the same build directory. The first overlapping native/firmware invocation lost dependency-output directories; a clean sequential build resolves that tooling collision. The sanitizer invocation uses a separate build directory.

Results on 2026-10-18:

| Check | Result |
|---|---|
| Final regular library/action renderer/input suite | 19/19 passed |
| Shared watch library/actions across nine dimensions | 1/1 passed |
| Final saved-view/shared-builder + reading-command suites | 53/53 passed |
| Metadata + feed organization suites | 60/60 passed |
| Existing regular native + watch regressions | 234/234 passed |
| Combined distinct regression cases | 367/367 passed |
| Final regular UI + view + status ASan/UBSan suites | 72/72 passed; no findings; linked ASan runtime verified |
| Localization generation and whitespace checks | Passed |
| Final LCD rev1 + rev2 builds | Both passed; no upload |

No hardware probing, flashing, serial monitor, live feed request, external review/export, push or release was performed.

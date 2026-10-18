# Library collection model and preparation (T04)

Implemented locally on 2026-10-18 following T01–T03. Hardware remains unflashed. This slice implements the collection data/controller boundary and integrates preparation and safe opening into App. The existing shelf/watch presentation remains in place: T05 connects the new collection APIs to the 2.5-row list, contextual rail, Back hierarchy and Item actions. Passing these tests does not certify those future renderers or physical touch behavior.

## Behavior

`LibraryView` owns snapshots identified by canonical path, full source identity and inventory generation. Books, individual feeds, Other articles and conditional Needs review project the same canonical rows. Per-feed membership does not duplicate progress or aggregate counts. Archived feeds with saved items remain available. Orphan memberships remain accessible if the registry cannot be read; they do not acquire an invented subscription URL/title. Known feeds follow stable subscription order. Unknown and loading are distinct from Unread; metadata failure categories remain available to the UI.

Valid explicit status wins. An older item without organization metadata is Reading only with an unambiguous, identity-matching intermediate bookmark. Missing/stale/corrupt bookmarks, zero/final positions, one-word content and shared legacy stems stay Unknown. Preparation never rewrites bookmarks or infers completion. Source replacement invalidates old status/membership. A pending restart remains unavailable for classification. Valid bookmark percentage and chapter are retained independently of status.

Books first opens Reading when populated, otherwise Unread. Feeds and Other articles first open Unread. Needs review has one internal projection without selectable status tabs. Books/Other articles Unread sort by title; feed Unread sorts by descending download batch, then original position within that batch. Reading/Read use descending activity/completion order. Unknown activity follows known activity. Titles use the existing supported Latin case folding and codepoint fallback, with canonical paths breaking ties. Needs review is alphabetical. No publication dates are inferred.

Each collection stores its selected tab and each visited tab's canonical top anchor plus intra-row pixel offset. Re-entry keeps an empty selected tab. Refresh resolves a removed/filtered anchor to the next surviving item in its previous ordering. Restored offsets clamp to actual content height; 52-pixel rows and a 130-pixel viewport have no artificial trailing space. Across a restart, only the persisted anchor is available: if it disappeared, restoration falls back to the top of that same tab. Missing inactive collections are evicted; supported live contexts are bounded by the existing 259-view codec limit. Future view formats are read-only. Browsing context allocation is separately capped at 64 KiB; exceeding it returns a resource error without silently evicting a live collection.

## Integration and ownership

The shared `LibraryScreen` builder now runs in an App-owned Library background job, outside paint/touch. It verifies sources in 512-byte chunks, yields between chunks/items, prioritizes the prior top item, reports progress and publishes a complete snapshot. Hardware Back/Open Menu can cancel preparation. No partial counts or partial digest are published. Existing inventory scanning remains unchanged; the added legacy-stem ambiguity check uses sorted indices rather than quadratic path allocation.

Paint receives cached adapters. Inventory refresh/removal/remount changes `StorageManager::generation()`, even with an unchanged count. The builder cancels its gesture and drops inventory pointers on invalidation. A fresh press is required before selection. App opens a canonical selection, resolves its inventory index, and verifies source bytes again in the open job. Queued typography opens carry the same identity/generation, so delayed work cannot open a neighbor or replaced source.

`openCollection`, `selectTab`, `scrollTo`, `collections` and `order` are the T05 presentation boundary. Current shelf/watch adapters retain all inventory items until those controls are connected. Active-reader progress/chapter overlays are memory-only. Views save on library exit, standby, power-off and before USB ownership transfer, rather than each frame. A failed save remains dirty and returns/logs an error; unsupported loaded records block writes. The visible retry/error treatment in the new collection UI belongs to T05. A failed initial creation with only a damaged/staged record still follows T01's conservative recovery rules; it is never silently promoted without intent.

Preparation and replacement account for retained/new row capacities, strings, memberships, view/registry state and a fixed codec/UI workspace allowance against a 1 MiB policy ceiling. Resource overflow returns an error without truncating items; preparation releases retained snapshots on this failure so a later attempt can rebuild. This is logical allocation accounting, not a measurement of total live heap, allocator overhead, old indexed-book metadata, or guaranteed recovery from global heap exhaustion in the exception-disabled firmware. Actual heap/PSRAM high-water marks and large/slow-card timing remain T08 release gates. No device performance numbers are claimed.

## Verification

The new `native_library_view_test` compiles the actual `LibraryView`, organization/bookmark stores, reader helpers and shared `LibraryScreen.cpp`. Its in-memory inventory adapter stands in for SD enumeration/index metadata; its filesystem fixture records I/O and injects failures. It does **not** compile App dispatch, the real inventory scanner, or either library renderer. Board builds cover their compilation; T05/T07 and device checks cover remaining integration/input behavior.

The 20 cases cover defaults, empty remembered tabs, filtering/loading/counts, shared article progress, archive/orphan collections, title/activity/arrival sorting, anchors after insert/filter/removal, per-tab/per-collection restart restoration, source-index mapping, same-count replacement, partial-row eligibility/clamping, conservative legacy classification, source identity/errors, ambiguous stems, preparation priority/cancellation, I/O-free cached frames, future-record protection, failed save/retry and every simulated interrupted view-update mutation. Fixtures include 0, 1, 2, 3, 25, 100, 500 and 1000 items, plus explicit memory/identity-limit errors and excessive saved-context growth.

```sh
.local/venv/bin/pio test -e native_library_view_test -e native_library_test -e native_reading_status_test -e native_feed_organization_test
.local/venv/bin/pio test -e native_test -e native_watch_test
.local/venv/bin/pio run -e waveshare_esp32s3_touch_lcd_349_rev1 -e waveshare_esp32s3_touch_lcd_349_rev2
PLATFORMIO_BUILD_DIR=.pio/build-library-view-sanitized \
PLATFORMIO_BUILD_FLAGS='-DGLZ_DEFAULT_OPTIMIZATION_SIZE -DGLZ_DISABLE_ALWAYS_INLINE -std=gnu++23 -Itest/test_library_organization/support -Itest/support -Isrc -lz -fsanitize=address,undefined -fno-omit-frame-pointer' \
.local/venv/bin/pio test -e native_library_view_test
```

Results on 2026-10-18:

| Check | Result |
|---|---|
| Library views/shared preparation, final source | 20/20 passed |
| Metadata, reading status, feed organization | 88/88 passed |
| Existing native + watch regression | 234/234 passed |
| Combined distinct regression cases | 342/342 passed |
| Final library suite under ASan/UBSan | 20/20 passed; no findings; linked ASan runtime verified |
| Final LCD rev1 + rev2 builds | Both passed; no upload |
| Whitespace check | Passed |

The in-host local review addressed same-count invalidation, source-index mapping, deferred identity verification, fresh-press activation, chapter/progress preservation, cancellation, interrupted context saves and saved-context allocation growth. It is not an independent outside review. Native inventory adapters and logical memory bounds do not establish physical SD durability, touch usability or PSRAM headroom. No live feeds, external review service, push, upload or flashing were used.

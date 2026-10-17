# Library redesign: readable list first

Date: 2026-10-18 (Asia/Bangkok)
Branch: `dev/library-planning` • Repository: `floo-one/rsvpnano`
Status: **DRAFT FOR USER REVIEW — no implementation authorized**
Discovery: G-Stack office-hours, builder mode; user selected approach A.

## Problem and premise

A horizontal shelf hides titles and makes a mixed book/article collection hard to browse. The real goal is to reduce time and effort before reading during a 2–10 minute gap. New categories are useful only if their metadata and navigation are trustworthy. The user accepted readable-list-first with richer metadata later. We have source evidence and the owner's report, not measured demand or usability results.

The enjoyable moment is opening a recognizable title immediately and returning to the same place in both the text and library. Preserve the reading engine. If we do nothing, shelf ambiguity remains and RSS volume increases navigation work. A list addresses recognizability; it does not by itself solve deep navigation in a thousand-item collection.

## Chosen approach and alternatives

A — replace the target shelf with a readable list over existing inventory/open workflows. Smallest useful change; low persistence risk, moderate touch/shared-state risk. Selected by user. It may need a second, bounded metadata-preparation slice to meet responsiveness targets.

B — categories plus a persistent metadata index immediately. Deferred because it adds migration, synchronization and taxonomy decisions before the browsing improvement is proven.

C — companion-curated small read-next queue. Deferred because choosing what to read would depend more on the companion and introduce queue semantics.

No new library framework, database, reading engine, board driver or feed downloader is proposed.

## Proposed geometry

Target is 640×172 landscape, not portrait. Preserve the existing 136 px navigation rail and 48 px right gutter. `tabContent` is `(148,8,444,156)`.

Candidate: 28 px informational header and **two 64 px rows**. Each row has a readable title, author/source text if present, and a right-aligned progress label/bar. A title may use two lines; secondary metadata yields space rather than overlap. Entire visible row is the activation target (about 8.4 mm tall at this panel density). Header is informational, not a tiny tap control. No new action in the power gutter. Final font metrics must use the firmware's real fonts.

```text
640 x 172 physical UI, illustrative only
+--------------+----------------------------------------------+-----+
| Read         | Library                         1–2 of 500   | PWR |
|              | The title you can actually recognize         |     |
| Settings     | Author / source                         32%  |     |
|              |----------------------------------------------|     |
| Device       | A second title, readable at a glance         |     |
| Focus        | Author / source                     unknown  |     |
+--------------+----------------------------------------------+-----+
```

These are proposed dimensions, not an approved visual design or device screenshot. More rows may look denser but make touch targets and long titles worse; test two versus three rows on the physical device before freezing layout. Keep theme colors/localization and RTL/CJK text shaping. The normal library must not use uppercase ASCII-only abbreviations.

## Interaction contract proposed for MVP

- Vertical finger movement scrolls by displacement, not frame count. Clamp to first/last item; no inertial animation in the first slice.
- A tap activates only the row under both a valid press/release sequence with no drag. Release after movement/cancellation never opens. Empty space and clipped, non-activatable fragments are inert. Existing global edge gestures take precedence when they own a gesture.
- Keep the same underlying inventory order for the first slice. The selected result remains a **source inventory index**, not a future filtered-view index. No automatic sorting by title introduced incidentally.
- Preserve an anchor path plus pixel offset when returning from reading; reconcile it when inventory changes. Clear old entry pointers/caches on every refresh, including same-count replacement. If the anchor disappears, select the nearest surviving neighbor, then clamp; do not open automatically.
- Use existing `App::runBookOpen` and progress save/restore. Opening shows the upstream conversion/indexing status if needed. Errors return to a usable library with a retry path, without discarding the active reader state prematurely.
- Show progress from validated source identity. If absent, stale or unsupported, render unknown/pending rather than declare unread. Do not read sidecars from paint callbacks.

## Architecture and data flow

```text
SD inventory -> existing BookLibrary/StorageManager -> library view state -> visible rows
                                      |                        |
                              identity/progress lookup    tap -> source index
                                      |                        |
                              cache outside paint       App::runBookOpen -> existing reader
```

Keep canonical storage ownership. `LibraryItem` points into an inventory vector, so any cache invalidation must precede pointer use after `refreshBooks`. Filtering in V1 will need explicit visible-to-source mapping; path reconciliation handles reordering between inventories. Reuse `ui::Context`, `TouchTiming`, theme/localization and chapter-screen drag cancellation patterns. Shared library metadata and header changes require watch regression checks.

Current `LibraryScreen::items()` loads metadata/progress for all entries before drawing. First measure it; replace this with budgeted preparation if it prevents R6 targets. Budget one item or a measured time slice per cooperative update, prioritize visible rows, redraw only when data changes, retain inventory generation checks, and serialize with existing SD/background-job ownership. Do not add an independent reader/indexer task or new persistent catalog in MVP. This follow-up is conditional release work, not a claim that the existing implementation is bounded.

## States and rescue behavior

| State/failure | User sees / action | Safety boundary |
|---|---|---|
| No card / mount failed | Clear storage message and existing Device/check-card route | No list-pointer dereference or open attempt |
| Mounted, empty inventory | “No library items” and add-content hint | No fabricated default item |
| Initial/metadata loading | Titles/fallbacks when available; progress pending | Do not label unknown as unread; no long work in touch handler |
| Corrupt/stale index or sidecar | Unknown progress; opening uses existing rebuild/recovery | Preserve original content/state; log a specific failure |
| Pending raw-document conversion | Existing conversion label/status | No false word count or completion |
| Open failure | Specific existing status, then recoverable library | No silent switch to another title |
| Refresh/removal during interaction | Cancel gesture and re-resolve anchor | Never use a dangling pointer or stale source index |
| USB storage session | Existing transfer screen; refresh after remount | No simultaneous firmware/host SD ownership |

## Acceptance and test map

Read [requirements](../product/requirements.md) for targets and fixtures. Add library behavioral coverage: tap vs vertical/horizontal movement, release-only movement, cancelled/edge gesture, blank space, one item, last item, long/multilingual titles, known/unknown progress, returning to the anchor, and same-count refresh. Verify correct canonical item is opened after any mapping changes.

Existing `native_test` excludes the library screen, so extend its test source selection deliberately or add a focused native suite with storage/UI fakes. Avoid tests that merely duplicate row arithmetic; assert user-visible activation, bounds, data correctness and absence of filesystem work during paint. Keep the RSVP, feed, touch and watch tests passing. Compile both LCD revisions after source changes. Hardware: rev2 corner taps, both handedness modes, menu/quick-settings edge gestures, power/sleep/wake, mixed SD library, repeated article/book switching, reader position and battery comparison. Initialization never flashes.

## Scope and release sequence

MVP slice 1: list + safe gestures + existing open/resume + context retention + honest metadata states. Slice 2 if baseline requires it: bounded item preparation for 100–500-entry responsiveness. This conditional work is required before making the scalable-MVP claim; slice 1 can be reviewed independently.

V1: Books/Articles and a single Continue shortcut; status filters only with agreed semantics and unknown handling; explicit coarse navigation for hundreds of entries. Keep category control on a separate adequately sized selector if four tabs do not fit. Do not label a single last book as a most-recent list.

V2: additive source/date metadata and migrations, publication/feed grouping, explicit newsletter classification, optional favorites/archive/delete and feed management. Each is separately scoped after evidence; not a promised bundle.

Distribution for later releases: preserve existing PlatformIO targets and upstream release packaging/companion manifests, publish clearly named fork artifacts only after review, rev2 testing and license checks. No releases or upstream-directed PRs in this initialization. Rollback of MVP source is a normal revert; no content migration means existing firmware still reads the same card.

## Open decisions for plan review

1. Two large rows or three denser rows after testing real fonts/touch (recommend two).
2. Exact status semantics: untouched, position-zero reread, reached-last-word, completed then restarted, one-word text and unknown index (defer filter implementation until chosen).
3. Continue as the last book or an in-progress collection (recommend last book first; recency history needs new metadata).
4. V1 coarse navigation: proportional scrubber, page jump or source selector; test a known-target search in 500 items.
5. Whether new feed/date metadata belongs in a content header or additive sidecar, and how the companion preserves it (V2 decision).
6. Actual performance/battery limits, test-card class and release hardware matrix; current thresholds are proposed.

## Assignment before implementation

Choose a representative set of 20 real book/article titles and use the current device to time three find-and-open tasks plus one resume. Then inspect the two-row sketch at physical size and try a paper tap/scroll sequence. Record mis-opens, truncation and reachability; this establishes a useful baseline without flashing or changing code. The next engineering task, after plan approval, is the isolated list slice and behavioral tests described above.

## What I noticed in the brief

- “Build on the existing architecture, not rewrite everything” makes path/progress compatibility a design constraint rather than cleanup to do later.
- “Short reading sessions” means we should measure time to reading, not only how many organization features exist.

## Decision ledger

| ID / owner | Evidence | Current | Proposed | Status | Approval scope |
|---|---|---|---|---|---|
| D1 / owner | User reply 2026-10-18 | Rev2 | — | approved | Build target only; no flash |
| D2 / owner | User chose A | Readable list first | — | approved | Planning direction, not implementation |
| D3 / design | 444×156 content area | Shelf | Two 64px rows | unresolved | Final layout awaits physical trial/review |
| D4 / engineering | Eager `items()` + raw inventory pointers | Existing storage | Bound preparation if measured necessary | proposed | Part of draft release criteria, no code approved |
| D5 / product | Missing metadata evidence | No new schema | Rich feed/status features later | deferred | Revisit with separate schema/semantics decision |

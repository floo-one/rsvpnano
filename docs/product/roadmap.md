# Prioritized roadmap

All implementation below is proposed, not started. Owner chose readable list first. Milestones are gated by behavior and measurements, not dates. No time estimate can substitute for hardware validation.

## MVP — recognizable list and reliable opening

**User-facing:** full recognizable titles, author/source where available, honest progress, vertical browsing, tap-to-open/resume, remembered browse position, clear empty/loading/error states. Keep existing navigation and reading modes. No new taxonomy.

**Dependencies:** owner plan approval; representative SD fixtures; rev2 hardware; current test baseline; library draw/state tests added because upstream's native suite omits them. Keep source ordering, file formats and open/save workflow.

**Likely files:** `src/ui/screens/regular/LibraryScreen.cpp`, `src/ui/screens/LibraryScreen.h`, shared `src/ui/screens/LibraryScreen.cpp`; `src/app/App.cpp` only if reset/invalidation coordination requires it; `platformio.ini` and a focused `test/test_library_ui/` suite to include the omitted screen. Conditional bounded-preparation work may touch `StorageManager` metadata access. Do not alter `ReadingLoop` for a library view.

**Acceptance:** R1–R10 in [requirements](requirements.md). A row tap opens exactly its canonical item; dragging never opens; return preserves context; unknown remains unknown; no filesystem calls in paint/touch; no crashes/stale selection for 0/1/500 entries or same-count refresh. New tests plus baseline suites and rev2 build pass, or unrelated baseline failures are clearly isolated and dispositioned. Device trial proves legibility and touch in both handedness modes.

**Tests:** pure gesture/view-state unit tests; screen harness with fake inventory/UI; integration tests around actual App open dispatch and refresh invalidation; rev2 manual SD/remount/sleep/read-switch checks. Measure cold/warm latency, SD reads and heap. Host tests do not prove physical tap accuracy, battery use or successful flashing.

**Risks:** eager metadata loads, raw pointers into refreshed inventory, small physical targets, multilingual font clipping, regular/watch coupling. A renderer-only patch is the first review slice, not proof of large-library performance.

**Sequence:**
1. Baseline + focused list slice: preserve source ordering and engine; add gesture/activation/anchor tests. Estimated 5–7 touched files, no new service.
2. Measure on real 100/500-item cards. If R6 responsiveness fails, add bounded visible-first metadata preparation and generation invalidation before declaring the MVP complete. No arbitrary delay budget or endless background retry.
3. Validate hardware and compare with shelf on the same tasks. Only then consider a fork release; no publish action is part of this task.

## V1 — organization and faster long-distance navigation

**User-facing:** Books and Articles views; a single Continue Reading shortcut; status filters once semantics are chosen; a large-target coarse-navigation method to reach distant entries. Avoid four permanent category tabs crowding the display. Remember per-view anchors during the session.

**Dependencies:** reliable MVP view model; explicit path/source-index mapping; status semantics for never-opened/position-zero/completed/restarted/one-word/unknown; known-target tests for hundreds of items. A most-recent list is not promised without timestamps.

**Relevant files:** `BookLibrary.h/.cpp` (reuse `isArticle`, not relocation), `LibraryScreen.h/.cpp`, regular renderer, `App.cpp`, `ReadingProgress.h/.cpp` and `ReadingState.h` only if an approved status design needs additive fields; localization sources/generation; matching native tests. Companion changes only if a persistence contract changes.

**Acceptance:** each view includes exactly the expected fixtures; same-title entries remain distinct; filtered row opens correct source; refresh/removal cannot open a neighbor; unknown state does not disappear as unread/completed; coarse navigation measurably improves far-target selection with no increase in accidental opening. Agree the target after baseline trials.

**Testing:** classification/status truth table and boundary cases; filtered/reordered/removal dispatch; return and empty-filter states; refresh race schedules; all MVP regressions; physical long-list seek and localization checks. Test old and new state formats if fields change.

**Risks/assumptions:** directory type can reflect how content was imported rather than its literary type. Completion cannot be inferred reliably from displayed integer percent. Persistent recency or explicit read status is a separate migration decision, not an incidental list change.

## V2 — richer source metadata and optional management

**Candidate functionality, not a committed bundle:** feed/source grouping, publication-date sort, explicit newsletter classification, favorites, archive, deletion, estimated remaining time, on-device feed editing and statistics. Prioritize source/date correctness before discovery/recommendation features.

**Dependencies:** choose additive metadata format and version/unknown-field policy; store stable subscription identity and optional dates during ingestion; preserve legacy files/progress and companion round trips; decide dedupe/retention/deletion semantics and recovery after interrupted writes. No evidence supports an immediate database rewrite.

**Relevant files:** `feeds/FeedParser.h/.cpp`, `RssFeeds.cpp`, `RssConfig*`, `conversion/rsvp/RsvpWriter*`, `text/RsvpDirectives*`, `library/BookLibrary*`, `ReadingState` if needed, `storage/fs/StoragePaths*`, companion API/model/persistence/conversion code and protocol documentation.

**Acceptance:** source identity remains correct when two feeds share a domain or author; absent/invalid dates are shown as unknown; old content and progress survive migration/rollback; interrupted saves leave recoverable content; deletion cannot silently recreate or permanently suppress content contrary to the chosen policy; full/corrupt/missing cards recover explicitly. Every optional feature needs its own approved acceptance contract.

**Testing:** RSS/Atom fixtures (missing author/date/link, multiple feeds per host, unicode, duplicate/update cases); backward/forward metadata fixtures across C++/Kotlin; crash/interrupted-write and SD-full tests; integration with companion; memory/SD/battery stress on rev2. Management actions need explicit recovery/confirmation appropriate to their destructiveness.

**Risks:** NVS seen-state growth, 32-bit hash collisions, sync limits, clock/timezone ambiguity, mixed provenance, compatibility with upstream companions and increased write/battery load.

## First development task after review

**Replace the wide regular library shelf with a clipped vertical list while preserving canonical storage selection.** Implement the smallest screen/state change, test tap/drag and return/refresh mapping, keep the existing reader/storage APIs and build both LCD targets. Include no categories, new metadata, deletion or feed changes. Use the two-row sketch as a candidate until physically checked. Deliver a reviewable commit before the bounded-preparation follow-up.

A future PR split is optional: one PR for the initial list, one only if measurements justify the metadata-preparation follow-up. Each adds reviewer/CI cost; do not split merely to increase activity. Initialization itself is one local documentation/setup commit and opens no PR.

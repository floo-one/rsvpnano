# Library implementation findings

Baseline: `f9d2a800ed38ce9ff64c6711e8ef2eca5db699fb`. File/line references describe that snapshot.

## Current path

1. `BookLibrary::refresh` scans `/library`, `/library/books`, `/library/articles` non-recursively, excludes hidden/sidecar files, handles document/cache pairs, sorts by case-insensitive filename (`src/library/BookLibrary.cpp:42–210`). Fallback display titles derive from filenames. Metadata may be deferred on startup.
2. `LibraryScreen::items` (`src/ui/screens/LibraryScreen.cpp:23`) loops over **every** inventory entry when invalid. It calls `StorageManager::readBookMetadata`, reads identity and chapter metadata from indexes, then loads saved progress. Missing/stale index or state becomes zero progress in today's view model. The cached fast path updates only the active book when the count matches.
3. `LibraryScreen::draw` (`src/ui/screens/regular/LibraryScreen.cpp:137`) uses the bookshelf for wide, shallow displays. Its spine label renderer keeps at most seven ASCII letters, making multilingual/full-title browsing particularly weak. Only visible spines are drawn, but item preparation is still eager.
4. Drag moves the horizontal shelf; release snaps. A tap on empty shelf space can open the selected item (`:193–201`). The detail area also opens it. Fallback regular layouts show only the first few 32-pixel buttons without a scrolling path in that renderer. Watch has a separate implementation.
5. `App.cpp:192–200` sends `selectedIndex()` directly to `runBookOpen`. `runBookOpen` (`:758`) validates the storage index and delegates to the existing background workflow.

## Geometry and controls

`regular/ScreenCommon.cpp` reserves a 136 px navigation rail, 12 px gap, 48 px right inset and 8 px vertical margins. At 640×172, `tabContent` is **444×156 at (148,8)**. Existing navigation offers Read, Settings, Device, Focus; power remains in the right gutter. Do not assume a 640-pixel-wide content list, a portrait device or four tall category rows.

`ui::TouchTiming` and input processing already distinguish tap/drag. The chapter screen offers displacement-based vertical movement and suppresses release activation after dragging. Those patterns are reusable. Exact touch-target size and font readability need a physical trial: 44 px is only about 5.8 mm on this panel, not automatically a comfortable finger target.

## Reuse versus change

| Area | Classification | Reason |
|---|---|---|
| Reader/open/save workflow | Reuse unchanged | Protect working RSVP/page mode and progress |
| Regular shelf drawing | Change incrementally | Replace only target geometry with clipped visible rows |
| Navigation and board input | Reuse | Avoid gesture, rotation and power regressions |
| Item preparation | Measure, then bound work | Eager metadata across hundreds of entries can dominate first display |
| Title/author extraction | Reuse with fallbacks | Existing directive support; unknown source must remain unknown |
| View state | Small modification | Preserve scroll/selection when returning; clamp after refresh |
| Filter/sort mapping | Explicit later change | Must map visible rows to canonical inventory paths/indices |
| Feed/date taxonomy | Later schema work | Not represented in current library metadata |

## Risks to cover in the plan

Pointer lifetime after same-count refresh; selection mismatch after sort; long or RTL titles; touch-release opening the wrong row; SD removal; absent index/progress; no content; one entry; last row; returning from reader; USB remount; sync adding entries; regular/watch shared-state coupling. `test_ui` currently compiles neither library draw implementation nor `src/ui/screens/LibraryScreen.cpp`; existing passing tests cannot certify the redesign.

Worst-case inventory matching uses linear searches per entry (document/cache and sibling probes), potentially quadratic before sorting. This is source-based analysis, not an observed bottleneck. Benchmark 0/1/25/100/500/1000 entries, mixed formats and cold/warm metadata before choosing a replacement inventory algorithm.

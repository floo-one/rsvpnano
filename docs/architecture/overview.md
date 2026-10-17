# Architecture overview

Audited 2026-10-18 against upstream `f9d2a800ed38ce9ff64c6711e8ef2eca5db699fb`. This is a map of existing behavior, not a rewrite proposal. See upstream [repository overview](../../README.md), [UI layouts](../ui-layouts.md), [configuration](../configuration.md) and [companion protocol](../companion-api.md) for their authoritative details.

```text
Touch + buttons -> Input sampler -> App -> screen draw -> ui::Context -> Arduino_GFX
                                    |                                 -> RowPrefixCanvas -> QSPI LCD
                                    +-> ReaderScreen / ReadingSession / ReadingLoop
                                    +-> StorageManager -> BookLibrary inventory
                                    |                  -> IndexedBook / IndexedBookStore -> SD
                                    |                  -> ReadingProgress -> SD state + NVS last book
                                    +-> background jobs (book open, RSS, storage repair)
                                    +-> companion HTTP/serial <-> Kotlin shared clients
RSS config -> RssFeeds HTTP -> FeedParser -> RsvpWriter -> /library/articles
```

## Execution and ownership

`src/main.cpp` enters the application. `src/app/App.cpp:192` prepares library items, draws and dispatches `Action::OpenBook` using `selectedIndex()`. Its background-job code (`runBackgroundJob`, `runBookOpen`) owns expensive book opening, RSS sync and storage repair. Readers retain `ReadingSession` and `IndexedBookStore`; the library must not create a second reading session or bypass open/save transitions.

`src/input/Input.cpp` samples input separately; `src/ui/Touch.h` defines start/move/release/tap timing. `src/ui/Ui.h`/`.cpp` provide the immediate UI, invalidation signatures, font shaping, clipping and controls. Regular screen layout is in `src/ui/screens/regular/`; watch presentation is selected by PlatformIO source filters, not runtime screen size alone. Both use shared declarations. Existing edge menus and handedness transforms remain application/board concerns.

## Reusable components

| Need | Existing implementation | Boundary to preserve |
|---|---|---|
| Open/resume | `App::runBookOpen`, `ReaderScreen`, `ReadingProgress` | Source index must match inventory; save current session before changing books |
| Inventory/title/author | `BookLibrary`, `StorageManager` | Canonical paths and pointers invalidated by inventory refresh |
| Indexed content | `IndexedBook`, `IndexedBookStore` | Rebuildable index, source identity validation, bounded reading |
| UI text/themes | `ui::Context`, localization, `Theme` | Unicode shaping and theme roles; no hand-rolled ASCII title renderer |
| Vertical touch pattern | `regular/ChaptersScreen.cpp` | Reuse displacement/tap cancellation ideas, not the curved chapter-selection UX |
| Article ingestion | `RssFeeds`, `FeedParser`, `RsvpWriter` | Existing sync/download and content conversion behavior |
| Management | `companion/http/CompanionLibraryApi.cpp`, `CompanionFeedsApi.cpp` | Existing upload/delete/progress/feed contracts; no API change in MVP |

## Resource constraints

The board configuration selects a 240 MHz ESP32-S3, 16 MB flash and 8 MB OPI PSRAM (`boards/esp32-s3-r8-opi.json`). The vendor lists 512 KB SRAM; PlatformIO's upload RAM budget is 327,680 bytes. Neither equals usable runtime heap. Obtain free internal heap, largest block and PSRAM high-water measurements on hardware before claiming capacity.

Native panel is 172×640; board rotation produces 640×172 (`WaveshareLcd349.h:39`, `BoardConfig.h`). A RGB565 framebuffer alone is 220,160 bytes (~215 KiB). `RowPrefixCanvas` requests PSRAM, with internal-memory fallback, and flushes a prefix of native rows through a 40 MHz QSPI panel bus. A small logical invalidation can still cause substantial transfer after rotation. Do not promise frame rate from dirty rectangles alone. No extra framebuffer is proposed.

SD uses one-bit SD_MMC (`BoardStorage.cpp`). Library metadata and progress cause separate file reads; scan/read latency depends on card and library contents. RSS permits 4 MiB feed bodies and 512 KiB article text, with temporary string copies. RSS/typography/content conversion share memory pressure with UI: stress on real hardware, avoid a new concurrent indexer.

Battery effects remain unmeasured. Reuse idle/sleep and event-driven redraw; no continuous animation, background polling or new Wi-Fi work for library browsing. Browser mocks and host tests cannot establish battery life or display/touch behavior.

## Coupling and risks

- `App` interprets selected library indices as storage indices. A filter or reorder cannot pass a visible-row index directly.
- `LibraryItem` holds raw pointers into inventory and cached data. Refresh/removal requires invalidation before reuse, even when item count stays the same.
- Regular and watch screens share `LibraryScreen.h` and shared cache code. Keep changes isolated or exercise both presentations.
- Progress uses source identity and path-derived storage. Renaming/moving files to organize them risks progress continuity.
- Companion and firmware share file/reading contracts conceptually, across C++ and Kotlin. New metadata needs compatibility fixtures on both sides.

These are architectural risks inferred from source, not reproduced runtime failures. Performance needs measurement before changing ingestion, indexing or rendering architecture.

# Storage, metadata and progress

This document supplements [upstream configuration](../configuration.md) and [companion API](../companion-api.md); it does not redefine their formats.

## Existing files and identity

Content lives at `/library/books`, `/library/articles`, and legacy-compatible files in `/library` (`src/storage/fs/StoragePaths.h`). Raw documents may be converted to RSVP caches. `.ridx`/`.rdat` are rebuildable; hidden `.rstate.toml` sidecars hold durable reading position and typography. `BookLibrary::id` hashes the **path** with FNV-1a. `reading::BookIdentity` uses source size, fingerprint and word count. Preserve both path identity and validation when designing organization; use a view rather than moving files.

`ReadingProgress.cpp` validates sidecars (maximum 2048 bytes), clamps positions and preserves reading overrides. `State` has `wordIndex` and overrides, **no opened/completed timestamps or explicit read flag** (`src/reader/ReadingState.h`). NVS stores the last book path; the 15-second `save()` path updates that cache, while `mirror()` writes the SD sidecar at lifecycle events. Do not describe NVS as a per-book progress database. One-word content returns 0% from `percent()`; missing/stale state also often displays 0%, so percentages alone cannot define read status.

## Metadata audit

| Proposed capability | Persisted/exposed today | Consequence |
|---|---|---|
| Books / Articles | Article path prefix; no type enum in `Entry` | Can derive a view, with imported/misfiled content caveat |
| Title / author | RSVP directives, `Entry.title/author` | Available; filename fallback; author may actually be source host |
| Source article URL | `RsvpWriter::Metadata.source`, written as header by RSS | In file but **not exposed** by `RsvpDirectiveValues`/library `Entry` |
| Stable subscription/source ID | RSS config is URL list only; saved article has no association | Domain from URL is not a feed ID; new metadata needed |
| Publication / fetched date | Absent from `FeedItem`, writer metadata and library entry | Cannot accurately sort by publication time without schema/ingestion work |
| Newsletter | No newsletter type or ingestion distinction | Treat as articles for now; later explicit classification needed |
| Position / percent | Sidecar + validated index identity; active session | Reusable, but unknown must be distinct from unread |
| Unread / in-progress / completed | No explicit state; position can be derived cautiously | Needs product semantics for last word, one-word texts, restart and missing index |
| Continue reading | Active session and NVS last-book path | Single resume is possible; multi-item most-recent list needs timestamps |
| Estimated remaining time | Word count + remaining position + WPM where known | Derivable later; not actual elapsed reading time |
| Favorites / archive / history | No library fields | Requires persistence, UI and compatibility decisions |

## RSS behavior

`RssConfig.h`: maximum 24 configured feed URLs. `RssFeeds.cpp`: processes up to 8 per check, body cap 4 MiB, request/total/idle timeouts 30/90/15 seconds. `FeedSync.h`: at most 12 newly saved articles per check. `FeedParser::FeedItem` contains title/link/author/body, no GUID/date/feed identity. Source label falls back to the article host.

Seen flags in NVS are `rss<32-bit hash>` of article link or title. They mean **downloaded**, never **read**. Filename includes sanitized title plus the same hash. `saveItem` writes a `.tmp`, finalizes and renames, then marks seen. No archive/read-status database or retention policy is implemented there. Deleting an article may leave its seen flag, affecting re-download; device deletion planning must define this explicitly. Hash collisions and dedupe growth warrant later investigation, not an MVP ingestion rewrite.

## Schema evolution guardrails

MVP makes no persistence/API changes. Later metadata must distinguish optional missing fields from false values, keep legacy content readable, preserve canonical path/progress, tolerate interrupted writes, and round-trip firmware/companion fixtures. Prefer an additive sidecar/header decision after validating real sources; do not introduce SQLite or a mandatory full-library database spec now. A disposable in-memory view cache must never become the only durable state.

Additional RSS scaling observation: `RssFeeds.cpp:445–459` takes the first min(configured, 8) feeds each check; no rotation cursor is visible in that path. Subscriptions beyond the first eight therefore need investigation before promising fair all-feed refresh. This existing limitation is outside the list MVP.

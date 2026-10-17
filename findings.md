# Investigation index

Canonical findings are in docs/architecture/overview.md, library.md and storage.md. Product output is docs/plans/library-redesign.md, docs/product/requirements.md and roadmap.md. Review evidence is docs/plans/planning-reviews.md; environment and validation live in docs/development/.

## Key evidence
- Upstream f9d2a800ed38ce9ff64c6711e8ef2eca5db699fb, MIT; fork floo-one/rsvpnano, history and four submodules retained.
- Library Entry: path, bytes, title, author, metadataLoaded. Article type comes from directory. No publication date, stable feed identity or newsletter type.
- @source URL is written into RSS content but not exposed by the library directive parser. Downloaded NVS flag is not read state.
- Shared library item preparation loads metadata/progress for every entry; visible-only rendering alone does not guarantee responsiveness.
- App opens by source inventory index; LibraryItem pointers require invalidation after refresh, including same-count replacement.
- Display 640×172 logical; existing content area 444×156. Two 64px rows are proposed, unvalidated on hardware.
- RSS config allows 24 feeds, but check loops through first eight without apparent rotation; 12 new articles/check. Rich RSS UX needs more than a list redesign.
- Reader progress has no explicit completion/recency history. Unknown/indexless and one-word cases defeat percent-only status classification.
- Hardware rev2 confirmed by user; no hardware operations performed.
- G-Stack 1.91.125.0 official setup: Claude and Codex current; all eight requested skills verified. Local independent draft review PASS, 9/10. External Claude review skipped by user after export rejection.
- Passing build/tests and retained native board-shim failure are documented in validation; no product files modified.

# RSVP Nano fork: agent guide

## Scope and product
Build on RSVP Nano to make a pocket reader pleasant for 2–10 minute sessions. The first priority is the library. Initialization is planning-only: **do not implement product features, flash hardware, merge, or release until the user authorizes that next phase**. The user selected a readable-list-first direction on 2026-10-18; the detailed plan remains for review.

## Architecture
- `src/app/App.cpp` coordinates screens, jobs, storage and reading.
- `src/ui/screens/regular/LibraryScreen.cpp` renders the target library; `src/ui/screens/LibraryScreen.cpp` prepares shared item data; `watch/` is a separate presentation.
- `src/library/BookLibrary.cpp`, `StorageManager.cpp`, `IndexedBook*` and `ReadingProgress.cpp` own inventory, content and progress.
- `src/feeds/` fetches/parses RSS into `/library/articles`. Companions share Kotlin contracts under `companion/`.
- Read `docs/architecture/overview.md`, `library.md`, `storage.md` and existing `docs/ui-layouts.md` before proposing changes. Keep upstream source structure intact.

## Conventions and constraints
Use existing C++23, `std::expected` error handling, Arduino_GFX/immediate UI, theme roles and localization. No new UI framework/database for the MVP. Preserve RSVP/page-reading, file identity and progress formats. Avoid per-frame filesystem access and unbounded metadata parsing. Do not equate unknown progress with unread or 0% with never opened.

Target: Waveshare LCD 3.49 **rev2, user-confirmed**. Use explicit `waveshare_esp32s3_touch_lcd_349_rev2`; upstream default is rev1. Logical UI is 640×172 with existing rail and edge gestures. Preserve handedness/touch mapping, power and USB storage ownership. Compile does not verify hardware. No flashing in initialization.

## Testing
Use `docs/development/environment.md` for setup and exact commands. Baseline outcomes are in `docs/development/validation.md`. For future library changes, add behavioral tests for tap-vs-drag, first/last items, refresh/reorder/removal and source-index mapping. Native tests currently do not compile either library renderer or its shared item builder; do not claim coverage they lack. Check regular/watch regressions and both LCD revision builds before release, with physical rev2 validation separately required.

## Git workflow
`origin` is `floo-one/rsvpnano`; `upstream` is `ionutdecebal/rsvpnano`. Preserve history and licenses. Work on a named development branch, review scope before implementation, commit coherent changes. Do not reset or overwrite user work. No push, merge or release is implied by a local commit request. Do not vendor generated SDKs, build outputs or global skills.

## G-Stack
The official setup generates Codex skills in `~/.codex/skills/gstack-*` and the runtime in `~/.codex/skills/gstack`. Verify with the checkout's `./setup --status`; restart the agent to refresh skill discovery after installation. Codex support is experimental and hooks are advisory. `CLAUDE.md` contains the official team integration; do not manually copy skill templates.

Use installed `gstack-office-hours` for discovery, `gstack-plan-ceo-review` for scope, `gstack-plan-design-review` for interaction planning, and `gstack-plan-eng-review` for architecture/tests. `gstack-review`, `gstack-qa`, `gstack-investigate`, `gstack-ship` are installed for later phases. QA browser tests cannot validate a physical ESP32 touchscreen. Shipping skills must respect the current no-push/no-release scope. User instructions override generic workflow steps. Cite pending questions and unavailable independent reviews honestly.

## Legacy library follow-up — 2026-10-27

The user requested better newsletter/book organization and explicitly chose **Treat older items as unread**, superseding the earlier unknown-status default for otherwise valid legacy items. Preserve valid in-progress bookmarks and explicit statuses; damaged/stale/unverifiable records still require review. Offline publisher groups may use saved article source hosts as display hints, without inventing confirmed feed memberships or shared progress. See `docs/development/legacy-library-classification.md` for behavior and validation. Keep backups local. The user subsequently authorized committing and pushing these improvements to the development branch. No merge or release is authorized.

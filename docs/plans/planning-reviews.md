# Planning reviews

2026-10-18 • `dev/library-planning` • target: [library redesign](library-redesign.md).
G-Stack 1.91.125.0 workflows were read and started from installed Codex skills. These are **preliminary planning assessments with open decisions**, not approved implementation gates. The owner's original request says to stop for plan review. No product implementation, ship review, live device QA, or external-Claude review is claimed.

## Office-hours outcome

Builder mode: the detailed brief allowed smart-skipping already answered discovery questions. Premise: reduce friction before short reading sessions; organization is useful only when metadata is reliable. Alternatives were a list using current data, metadata/index-first, and companion-curated queue. The user chose list-first (A). Rev2 was separately confirmed. The assignment and reflection on the user's brief are in the design document.

Independent local spec review: the official helper prepared the full prompt, a fresh native subagent read it and wrote/sealed its verdict, and `check` returned PASS. See [verdict](library-redesign-review/round-1.json). The helper-generated Spec Review section below is authoritative. Its 9/10 document score is not usability, firmware correctness or launch approval.

The optional outside Claude invocation was blocked by automatic export approval review. The user then selected local-only review. External coverage is **skipped by user choice for this task**; global preferences were not changed.

## CEO review — HOLD SCOPE, issues open

Mode provenance: original brief's focused first release and user-selected A. No new approach choice needed. Current scope is a readable list retaining existing storage/reader, with measured preparation improvements if required. Rich categories/metadata remain later proposals. Estimated initial implementation: 5–7 touched files, no new service. No scope expansion is approved.

| Section | Assessment and disposition |
|---|---|
| 1. Architecture | Reuse App dispatch, inventory and reader; avoid a new database/UI framework. Pointer lifetime/source-index mapping is the main integration risk. Plan describes safe boundaries; implementation must prove them. |
| 2. Error & rescue | Draft covers no-card, empty, loading, corrupt index/state, conversion, open failure and refresh. Unknown data must not become unread. Error paths use existing status/retry UI; no C++ exceptions added. |
| 3. Security | No new network endpoints, credentials or mutations in MVP. Treat imported titles as untrusted UTF-8, bound layout/text work. No deletion or new remote fetch surface. |
| 4. Data & interaction | Critical invariant: the touched canonical item is the one opened. Refresh must cancel a captured gesture before replacing inventory; see schedules below. Blank/partial rows must not open a nearby item. |
| 5. Code quality | Keep target presentation isolated; shared header/cache changes affect watch screens. Reuse drag cancellation concepts, not a chapter-carousel abstraction or reader rewrite. |
| 6. Tests | Existing native suite omits the library. Required plan includes actual screen/App dispatch coverage, not only helper arithmetic. Physical targets and memory remain unverified. |
| 7. Performance | Eager N-item sidecar/index loading and potentially quadratic inventory matching are likely costs; ranking is source inference, timings unknown. Conditional bounded-preparation slice prevents renderer success being mistaken for scalable performance. |
| 8. Observability | Record first-list, gesture feedback, open latency, SD opens and heap during development. Reuse serial logging; no analytics service or continuous battery-consuming polling proposed. |
| 9. Rollout | No schema migration in MVP; revert source/firmware is feasible after backing up card. Validate correct rev2 target first, release fork-specific artifacts later. No deploy in initialization. |
| 10. Trajectory | Source/date/status features can layer on views later. Preserve canonical files instead of rearranging directories; maintain upstream mergeability. Reversibility 5/5 for draft screen-only change, lower if later persistence is approved. |
| 11. UX | Recognizable titles and one-tap rows directly address the reported problem. Two-row geometry is plausible, but far-target browsing needs V1 coarse navigation and row density needs a physical trial. |

Current -> proposed -> possible future: ambiguous spines -> readable/reliable list -> truthful source/status organization with bounded navigation. A large feature count is not the success metric. No new commercial/demand claims were inferred from an open-source owner request.

## Design review — seven passes, issues open

Reference: [local sketch](library-wireframe.html) / [rendered sketch](library-wireframe.png). This is locally rendered HTML with placeholder browser fonts, not image generation, live firmware output, a hardware test or an approved mockup. No separate design system is invented; existing theme/text primitives are the basis.

| Pass | Draft score | Finding / what would make it complete |
|---|---:|---|
| Information architecture | 8/10 | Title first, author/source second, progress third; retained navigation. Need physical choice of two versus three rows and V1 category entry. |
| Interaction states | 8/10 | Normal/empty sketch; plan names loading/no-card/error/unknown/conversion. Render the remaining states with actual fonts before implementation sign-off. |
| Journey | 8/10 | Wake -> existing Read route -> Library -> recognize -> tap -> read -> return to anchor. Measure actual wake-to-reading and frustration points. |
| Generic-design risk | 9/10 | Device-specific rail/content sizes, restrained typography, no decorative shelf/gradients/animation. No marketing layout or new card system. |
| Design-system alignment | 7/10 | Reuses theme roles and localized text; browser colors/fonts are illustrative. Verify exact firmware text metrics, contrast and truncation; no DESIGN.md token system exists to certify. |
| Accessibility / variants | 7/10 | 64px rows ~8.4mm, text labels instead of color alone, preserve handedness. Need RTL/CJK/large-font/bright-light trials. ARIA/keyboard/screen readers do not apply to this native ESP32 view; companion is unchanged. |
| Decisions | 6/10 | Row density, progress semantics and V1 coarse-navigation mechanism remain open by design. These are owner choices, not automatic approvals. |

A 10/10 here means every relevant design choice and device state has evidence, not that a browser sketch looks polished. Ratings are reviewer judgment, not benchmark measurements.

Journey storyboard:

| Step | Reader action | Desired experience | Evidence still needed |
|---|---|---|---|
| First 5 seconds | Wake or enter library | Recognize where to resume | Existing wake flow + measured first-list latency |
| Browse | Read titles and scroll | Feel in control; no accidental opens | Real font/touch trial and cancellation tests |
| Open | Tap chosen row | Exact item opens, position retained | Canonical mapping integration test |
| Next 5 minutes | Read in either mode | Reader behaves exactly as before | Upstream regressions + physical switching test |
| Return / repeat | Re-enter list | No loss of place | Anchor/refresh cases, repeated daily use |
| Long-term | Large mixed collection | Still trustworthy | V1 navigation and later metadata compatibility |

Unanswered design questions remain in the plan's ledger. This assessment does not mark the skill's interactive decision loop complete; owner review comes next. The original request's final-review gate is honored instead of silently accepting these details.

## Engineering review — architecture, quality, tests, performance

Estimated initial slice is below eight files and two new classes/services, so no large-architecture structure choice is needed. [Layer 1] reuse existing platform abstractions; no new infrastructure/concurrency model. Scope accepted as proposed for review, not implementation. No tests are retired or weakened.

### Architecture and ownership

**High risk, high confidence (source):** `App.cpp:198` opens by selected storage index; `LibraryScreen.h` stores pointers into `BookLibrary::Listing`. Any later filtering or refresh can invalidate naive row indices/pointers. Draft requires canonical selection, inventory invalidation and cancellation. Implementation owner must verify the real refresh completion boundary in App, not merely increment a view counter.

```text
Order A: touch start(generation G) -> refresh completes(G+1) -> release
         cancel gesture at refresh; release opens nothing; next draw re-resolves anchor
Order B: touch start(G) -> release(valid G) -> open dispatch -> refresh attempted
         existing App job ownership serializes storage work; if not provable, defer open/refresh
         until one completes; never retain a raw row pointer across the job boundary
```

These are required test schedules, not proof today's App already implements the proposed protection. Same-count replacement, removal of selected item, USB remount and sync completion must trigger the boundary. No extra background task is proposed.

Happy: inventory -> visible row -> canonical index -> existing open -> reader.
Nil: no card -> storage message; no open.
Empty: mounted/zero entries -> empty guidance; no fabricated item.
Error: invalid metadata -> unknown; open failure -> existing status then usable library.

### Code quality and maintainability

**Medium risk, high confidence:** shared `LibraryScreen.h`/items cache serves regular and watch implementations. Keep wide-layout changes localized and retain baseline fallback behavior until separately reviewed. Do not extract a general scrolling framework solely because chapters also drag. Use `ui::Context` text/theme/clipping; avoid duplicating ASCII rendering or progress formulas.

**Medium risk, high confidence:** source identities and sidecars must remain untouched. A view filter is reversible; moving files or redefining 0% introduces hidden compatibility effects. Formal read-state semantics are intentionally not an MVP implementation requirement.

### Test contracts

| Requirement | Observable assertion | Wrong outcome rejected | What green does not prove |
|---|---|---|---|
| Tap opens intended item | App receives exact source index/path under tapped row | Visible index opens different book | Physical touch accuracy; fake events only |
| Drag does not open | Every moved/cancelled release emits no open action | Scroll release starts reading | Real sampler noise; device test needed |
| Return preserves context | Anchor path/offset restored then clamped after removal | Reset to first row or stale pointer | Power-cycle recency; no new durable history |
| Honest metadata | Missing identity/state renders unknown | False unread/complete labels | Corrupt-media recovery beyond fixtures |
| Efficient redraw | Painted rows bounded, no FS reads in draw/touch | N sidecar reads on every frame | QSPI throughput/heap on actual hardware |
| Reader preserved | Existing pacing/progress cases unchanged + switch/resume integration | Lost reading state or changed RSVP speed | Full hardware correctness or battery life |

Cover touch release-only movement, edge ownership, partial rows, duplicate/RTL/CJK titles, 0/1/500/1000 items, same-count refresh and both refresh/open orders. A strong hostile case is repeatedly replacing the selected file while finishing a scroll. Manual system testing is essential; no fake UI test should claim it.

### Performance and feasibility

**High risk, source-inferred, timing unknown:** cold `items()` loads every indexed metadata/sidecar before first draw (`LibraryScreen.cpp:23`). Rendering only two rows does not fix this. Measure baseline; if thresholds fail, add bounded visible-first preparation under existing storage ownership before release. Keep unknown data visible and cancellable; no new full book parses in the draw path.

**Medium risk, source-inferred:** inventory pair matching can be O(N²) for mixed formats; profile rather than automatically rewrite. Existing panel prefix flush can transfer much more than the changed row. Framebuffer is 172×640×2 = 220,160 bytes [derived from board constants]; runtime free memory is unknown. Linker RAM/flash results are in validation and are not heap/PSRAM measurements.

Draft numeric targets (250ms warm list, 100ms p95 feedback, 500/1000 fixtures) are [estimated acceptance proposals]. No measured latency, SD budget, battery life or capacity limit is claimed. At 10×/100× inventory, SD preparation and inventory matching are expected to fail before visible-row draw complexity; verify this ranking.

## Reality check receipts

| Premise | Receipt | Verdict |
|---|---|---|
| Shelf truncates browse labels | `src/ui/screens/regular/LibraryScreen.cpp:108` | verified source |
| Existing content viewport is 444×156 | `src/ui/screens/regular/ScreenCommon.cpp:49`, board 640×172 | derived source |
| All-entry item preparation | `src/ui/screens/LibraryScreen.cpp:23` | verified source |
| Canonical index is dispatch contract | `src/app/App.cpp:198` | verified source |
| Publication/feed/newsletter metadata missing | `src/feeds/FeedParser.h:16`, `src/library/BookLibrary.h:11` | verified model definitions |
| User owns rev2 | user reply, `docs/decisions/README.md:8` | user-confirmed; not hardware-probed |
| Proposed timings are achievable | `docs/product/requirements.md:22` | unverified until device baseline |

No linked GitHub issue was provided. Upstream/fork main were fetched and both resolve to the audited commit; no later change on the named paths was returned for the ask date. CLI surfaces were exercised (pio test/run, wrapper checkWeb, G-Stack setup/status/team-init). Binding decisions were checked in local G-Stack branch search (no records returned) and the actual user replies/plan ledger. Default one future PR per wave, with a second only if measured preparation work needs separation; none opened here.

REALITY: premise-table finding unverified=1 timing feasibility awaits hardware docs/product/requirements.md:22
REALITY: already-done pass fetched origin/main and upstream/main match baseline; no later named-path hit docs/architecture/overview.md:3
REALITY: surface-check pass documented build/test and setup commands exercised docs/development/validation.md:1
REALITY: binding-decisions pass owner selected A and rev2; no external review docs/decisions/README.md:1
REALITY: numbers finding geometry derived; performance thresholds estimated docs/product/requirements.md:22
REALITY: deferred-asks pass richer taxonomy and management deferred with reasons docs/product/roadmap.md:1
REALITY: pr-count pass one local initialization commit; no PR; future split conditional docs/product/roadmap.md:1

## Review readiness

| Review | Status | Remaining gate |
|---|---|---|
| Office-hours independent spec | PASS (draft quality, 9/10) | Owner approval of final draft still pending |
| CEO planning | Issues open | Confirm release acceptance and scope boundaries |
| Design planning | Issues open | Physical row/font choice, remaining states and semantics |
| Engineering planning | Issues open | Baseline hardware measurements and actual integration-test design |
| Outside Claude | Skipped by user | No result claimed |
| Product code review / QA / ship | Not run | No implementation in this task |

**Not cleared for implementation or release.** This is the requested reviewable planning handoff. Proposed tasks live in the roadmap; none has started. After owner review, finalize the relevant G-Stack gates before feature work. One independent office-hours reviewer is not being represented as three separate independent specialist reviews.

<!-- gstack:office-hours:report:start -->
## Spec Review

Disposition: COMPLETED

Stop: PASS

| Round | Blocking findings | Minor findings | Prior findings confirmed resolved | Quality score |
|---|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 9/10 |

Findings reported across rounds: 0 (sum of round inventories; recurrences count again).

Confirmed resolutions: 0 (sum of explicit later-reviewer resolved statuses).

Unresolved findings in the last completed inventory: 0 (0 blocking, 0 minor).

Completed fix-and-review transitions: 0 (rounds, not edits).

No unresolved findings.
<!-- gstack:office-hours:report:end -->

# Requirements and evidence

Status: proposed acceptance criteria for review, not implementation authorization. Approach A and rev2 hardware are user-confirmed. Numeric performance targets below are hypotheses until baselined.

| ID | Requirement | Evidence / proposed acceptance |
|---|---|---|
| R1 | Recognizable content | Whole title strings through Unicode-aware text drawing; up to two lines or safe ellipsis, no seven-letter spine encoding; fallback filename |
| R2 | Touch browse/open | Vertical displacement scroll, release after drag never opens; one tap within a fully visible row invokes existing open/resume; outside-row tap is inert |
| R3 | Reading continuity | Existing open/save/restore workflow; same canonical content opens after return, refresh and later filter/sort; RSVP/page pacing unchanged |
| R4 | Meaningful metadata | Author/source display when present, progress only when known; no inferred publication date or newsletter identity |
| R5 | Restore browsing context | Return from reader retains anchor/offset; refresh re-resolves anchor by path and clamps safely; removal selects next neighbor or empty state |
| R6 | Scalable preparation | No SD reads in the list's paint or touch handler; draw work proportional to visible rows; bounded metadata work separated from rendering if measurements require it |
| R7 | Usable states | Empty, no-card, loading, unavailable progress, conversion-required and open error all explicit; UI remains navigable |
| R8 | Hardware consistency | Existing rail/power/edge gestures and 180° handedness retain behavior; no display-driver or reader rewrite |
| R9 | Compatibility | Legacy content and sidecars intact; regular non-target boards and watch presentation retain baseline behavior |
| R10 | Practical short sessions | Candidate already on screen opens in one tap; resume from existing Read flow remains fast; measure time from wake and library separately |

## Proposed measurement protocol

Use 0, 1, 25, 100, 500 and 1000 items, including long/duplicate/RTL/CJK titles, articles and unindexed documents. Record cold and warm entry-to-first-list, touch-to-feedback, open-to-first-word, maximum draw/frame time, SD operations, free/largest internal heap and PSRAM. Include missing/corrupt sidecars, full/missing card and refresh with unchanged count.

Initial targets to validate: warm library visible within 250 ms for 100 indexed entries; visual response p95 under 100 ms during drag; cold 500-item load shows feedback within 250 ms and remains interruptible. These are proposed targets, not achieved benchmarks. If current eager preparation prevents them, include the bounded metadata follow-up before calling the MVP release-ready. Large unindexed books can need conversion: show the existing progress workflow rather than promise instant opening.

A full 500-item seek task needs coarse navigation in V1; a two-row list alone does not solve every long-distance browse. Compare a named-target task with the shelf using the same collection; capture gesture count and mis-opens rather than just visual preference. Do not set an arbitrary battery-life claim; compare a fixed browse/idle/sleep session at equal brightness and networking conditions.

## Non-goals for MVP

No publication-date sort, newsletter type, favorites, archive/deletion UI, on-device feed editing, statistics, recommendations, new companion UI, or content schema migration. Preserve existing API management functions. No product code is changed in initialization.

# Decision log

Record only consequential decisions here. Use a small dated Markdown ADR when implementation introduces a new contract. A proposal is not an accepted decision.

| Date | Decision | Status / reason |
|---|---|---|
| 2026-10-18 | Use existing RSVP Nano architecture and history | User instruction; preserve functionality and maintenance path |
| 2026-10-18 | Target Waveshare LCD 3.49 revision 2 | User-confirmed; no flash authorized in initialization |
| 2026-10-18 | Readable list first (approach A) | User-selected; metadata/index-first and companion queue deferred |
| 2026-10-18 | Official G-Stack team bootstrap + generated Codex install | User instruction; avoid vendored/copied skill drift |
| 2026-10-18 | Keep external Claude review off for this task | User selected local review after export approval was blocked; global setting unchanged |

Pending: physical row density, read-state semantics, long-list navigation, future source/date schema and measured performance targets. See the [design ledger](../plans/library-redesign.md#decision-ledger). No implementation or release approval is recorded.

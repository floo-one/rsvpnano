# Feed registry, matching and shared progress (T03)

Implemented locally after the owner's authorization on 2026-10-18. This builds on T01 metadata/recovery and T02 reader state. Hardware remains unflashed. The separate feed collection screens remain T04/T05.

## Implemented behavior

- Exact configured URLs retain stable local feed IDs through reorder, removal and re-add. URL replacement creates a new subscription; old records remain inactive so saved memberships retain their origin. RSS channel/Atom feed titles are separate from article authors; fallback labels include the host/path and are bounded on UTF-8 boundaries.
- Config save keeps the existing URL-list TOML/API shape and reconciles the registry. Startup/remount also reconciles it. If config commits but registry persistence fails, the error is returned; the saved config remains authoritative for retry. No new endpoint or companion payload is required.
- Matching requires the same resolved article URL, normalized-body SHA-256 and matching scope, plus validation that the existing local file still has its recorded full-source identity. Atom alternate links are preferred over self/enclosure links. Opaque/non-permalink GUIDs are not article URLs. Relative links use the effective fetched URL after redirects; query parameters are retained.
- A confident match adds a feed membership to the same canonical file. Its bookmark, typography, read status and recency/completion orders remain unchanged. A changed body, different URL or replaced local file cannot inherit that membership. Linkless/incomplete/unsupported-XML-base cases use feed-scoped matching and cannot merge across feeds. Existing duplicate files are preserved; matching selects a deterministic valid candidate rather than merging their bookmarks.
- An old downloader file can gain a membership only when its generated filename exists and its complete bytes exactly match the newly rendered article, with a complete, representable source URL. It keeps its explicit status, or remains Unknown for Needs review. This is conservative backfill, not a complete legacy migration; changed titles/ambiguous historical files can remain unassociated.
- New content becomes Unread after content and organization commit. Filenames use full identity digests plus checked collision suffixes. Existing content, metadata and bookmark final/backup/stage paths are never overwritten to make room.

## Scheduling and limits

A persisted cursor rotates across at most eight active feed attempts per check. It advances before the request, so network failure or a check exhausting its twelve-new-save budget cannot keep the first subscriptions permanently ahead. Removed subscriptions stay in the registry but are skipped. With eight attempts/check all 24 can be attempted in three completed checks; with one save-heavy feed ending each check, all can be attempted within 24 checks. A power interruption before a reserved request can skip that turn until the next rotation.

Each fetched feed processes at most 128 usable entries per pass. Associations and already-present entries do not consume the new-save budget. A digest-bound byte cursor and original entry position resume later passes; a changed feed body starts a new arrival batch. Deferred/failed entries are revisited after the scan wraps, rather than blocking every later entry. Membership batch/position remains its first confirmed arrival. Partial fetches, deferred work and failures are reported as pending work. Existing 4-MiB feed, 512-KiB parsed-body and network timeout limits remain.

The candidate catalog budgets its vector capacity and string storage to 1 MiB and fails explicitly on overflow. Registry/record/membership limits remain the foundation's bounds; inactive feeds are retained up to the explicit registry limit, without silently pruning memberships. This is an allocation guard, not a measured total heap/PSRAM claim: the existing inventory, feed body, parser and writer also consume memory. Real 1000-item/card/network measurements remain a release gate.

## Durable imports and recovery

`FeedOrganization` owns registry operations, catalog matching, bounded scans and imports. The real `RsvpWriter` still creates reader content. `LibraryOrganization` adds a bounded/checksummed import record, matching scope and scan fields using its existing version/revision/replacement contract.

- Journal: `/config/library-import.toml`, with adjacent `.tmp` and `.bak`.
- Content stage: `/library/articles/.feed-import.tmp`, hidden from inventory.
- Journal contains the chosen canonical path, full staged-content identity, proposed organization, and a completion flag. An existing-content flag distinguishes legacy backfill, which never installs replacement content.

The serialized flow is: recover prior intent → write/flush/close stage → verify length and hash → commit intent → install without overwriting a canonical path → validate content → commit organization → retain a completed receipt → mark seen. Startup, remount and each later ingestion recover unfinished work. Recovery preserves an intervening reader status change instead of replacing it with Unread. A pending reader restart blocks membership changes.

A validated initial journal stage can itself authorize import recovery; a partial first journal cannot have installed content yet and may be discarded for retry. The durable configured URL list similarly authorizes first registry creation/reconciliation. Unsupported/newer/unreadable records remain blocked. Existing-file first membership creation also uses a journal, so interruption cannot leave a sole organization stage without its recovery intent. A completed receipt is retained; ordinary recovery does not recreate deleted content. Missing/replaced content behind an unfinished intent produces an explicit recovery error.

New seen markers use `rs2` plus a short NVS bucket key **and compare the full stored digest**; the short key alone is never a match. They are written only after a successful content/membership operation. An NVS marker failure does not invalidate a durable import: the next pass can rediscover its metadata. Historical URL/title/GUID inputs are retained separately even when the new parser selects a better article link. Historical `rss<32-bit hash>` booleans are read conservatively to preserve the old intentional-deletion behavior, and are not rewritten as proof of identity. Their pre-existing ambiguity cannot be removed without additional evidence. Matching existing content is attempted before seen suppression, so a second feed can acquire its membership even when the article was already downloaded.

App's existing background/USB/HTTP/serial ownership boundaries remain in force. Startup recovery occurs before opening the saved reader item. Benchmark startup excludes the new organization operations. The implementation does not add background network polling or filesystem work to painting/touch callbacks.

## Verification

`native_feed_organization_test` compiles production registry/matching/import/scan code, parser, RSVP writer, bookmark/status stores, codecs, filesystem helpers and installed ESP32 SHA code. The fixture provides persistent filesystem nodes, failure/power-loss injection, and the existing small reader/preferences adapters. HTTP transport, the full App scheduler and physical SD/touch are not simulated by this suite.

The 27 feed tests cover stable/archive/re-add identity; 24-feed rotation; RSS/Atom titles and links; cursor invalidation; shared canonical progress/status; different URL/body and uncertain scopes; replaced content; filename/orphan-bookmark collisions; legacy backfill and explicit status preservation; deleted seen content; limits; unchanged config payloads; and commit-before-seen behavior. They enumerate all simulated mutation boundaries for first/subsequent imports, first legacy membership and existing membership updates, and all filesystem-call failures for an import following a committed receipt. Repeated recovery is checked for idempotence and preservation of unrelated content/bookmarks.

```sh
.local/venv/bin/pio test -e native_feed_organization_test -e native_library_test -e native_reading_status_test
.local/venv/bin/pio test -e native_test -e native_watch_test
.local/venv/bin/pio run -e waveshare_esp32s3_touch_lcd_349_rev1 -e waveshare_esp32s3_touch_lcd_349_rev2
PLATFORMIO_BUILD_DIR=.pio/build-feed-sanitized \
PLATFORMIO_BUILD_FLAGS='-DGLZ_DEFAULT_OPTIMIZATION_SIZE -DGLZ_DISABLE_ALWAYS_INLINE -std=gnu++23 -Itest/test_library_organization/support -Itest/support -Isrc -lz -fsanitize=address,undefined -fno-omit-frame-pointer' \
.local/venv/bin/pio test -e native_feed_organization_test -e native_library_test -e native_reading_status_test
```

Results on 2026-10-18:

| Check | Result |
|---|---|
| Feed organization | 27/27 passed |
| Metadata + reader | 61/61 passed |
| Existing native + watch | 234/234 passed |
| Combined regression | 322/322 passed; focused final-source rerun 88/88 and final parser rerun 21/21 passed |
| Feed + metadata + reader under ASan/UBSan | 88/88 passed, no findings; linked ASan runtime verified |
| Final LCD rev1 and rev2 builds | Both passed; no upload |
| Whitespace check | Passed |

Local review addressed durable legacy backfill, preserving intervening reader status, archived feed identity, orphan bookmark backups, verifying files after flush/close, partial-fetch reporting, conservative GUID/link handling, and arrival-order reconstruction. This was an in-host review, not an independent outside service review. No live user feeds were fetched by the agent.

## Next steps and flashing

T04 adds collection projections, filtering, ordering and remembered anchors. T05 adds the 2.5-row list, per-feed/Books navigation and shared Item actions. T06 completes broader companion/delete/USB/rollback compatibility, and T07 consolidates integration checks. The first useful end-to-end rev2 flash should follow those checks, with explicit owner authorization and an SD backup. T08 then measures touch behavior, real card interruption/removal, timing, memory and power. Passing builds and injected filesystem schedules do not establish physical durability or performance. No hardware flashing, push or release was performed.

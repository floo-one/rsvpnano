# Library metadata and recovery foundation (T01)

Implemented locally on 2026-10-18 following the owner's approval to start T01. Hardware remains unflashed. This document records the implemented boundary; the prior planning review receipt is historical and does not certify this code.

## What is implemented

`src/library/LibraryOrganization.{h,cpp}` provides versioned item, feed-registry and collection-view records; bounded TOML codecs; full-source SHA-256 identity; optimistic revision checking; and validated final/temporary/backup recovery. Item data includes explicit status or an unknown reason, optional indexed identity, source/body matching keys, membership order, recency and completion order, and a data-only restart intent. View records contain per-collection tabs and per-tab anchors.

Locations:

- Item: `/library/books/.Book.epub.library.toml` (full filename, so `Book.txt` has a separate record).
- Feeds: `/config/library-feeds.toml`.
- Views: `/config/library-view.toml`.
- Each store uses adjacent `.tmp` and `.bak` files.

Existing `.rstate.toml`, content and RSS configuration formats/writers remain unchanged. Hidden organization siblings are already excluded by the existing hidden-file inventory rule. No application caller uses this module yet, so this slice does not migrate existing files or change device behavior.

## Caller contract

Operations require serialized, exclusive SD ownership. Invoke them in the existing application job/storage flow, never in paint or touch callbacks. The caller creates parent directories and preserves the USB ownership gate. Loads can repair files and therefore also require write ownership.

Use `hashSource` to derive the source byte count and full SHA-256. It uses the existing ESP32 `SHA256Builder`, reads at most 512 bytes per step and offers a callback before each step and after the final step for progress/yield/cancellation. Read failures, cancellation and detected size changes return no partial identity. The caller must prevent concurrent content mutation, including same-size replacement; a digest of a mixed read is not a snapshot. Hashing is not automatically performed by `saveItem` or `loadItem`: pass an identity validated while holding that ownership.

`saveItem`, `saveFeeds` and `saveViews` take the expected persisted revision. Zero means create; success returns the assigned revision. A conflict means reload and reconcile. An I/O failure after the final rename can mean the write committed but its verification read failed; reload before retrying. Never publish UI success based only on an attempted write.

The `Recovery::stagedCreation` policy is only for a caller with a durable import/create intent. With both final and backup absent, it can promote a validated revision-one stage, or remove a corrupt partial stage and report missing so that intent can retry. It never promotes an interrupted update over an existing commit, discards a final/backup, or clears unsupported/stale/unreadable candidates. T03 will supply durable import intents; this foundation does not invent one.

## Record validation and recovery

Every record has a schema version and nonzero revision. Records are capped at 64 KiB including their checksum comment. Paths/URLs are capped at 4096 bytes, labels at 256 UTF-8 bytes, memberships and retained feeds at 256, active feeds at 24, views at 259 and anchors at three per collection. The existing active RSS configuration's separate 4-KiB bound remains unchanged. Validation rejects traversal/non-library paths, malformed digests, invalid UTF-8/control characters in labels, duplicate keys in the domain model (feed IDs, feed URLs, memberships, collection IDs, anchor tabs), invalid statuses and inconsistent restart data. Oversized output uses a fixed-capacity heap serializer and returns an error rather than growing indefinitely or truncating identity. No limits silently remove saved content.

The first line is `# rsvpnano-library v1 <8 lowercase FNV-1a hex digits>`. It covers the exact remaining TOML bytes, so a truncated but syntactically valid prefix cannot become a valid commit. This checksum detects accidental record damage; full SHA-256 is used for content identity. It is not an authentication format. Arrays are written inline to avoid the dependency's empty-array table-scope issue. Missing optional fields retain documented struct defaults; core identity/status/revision fields must validate. Unknown fields and versions produce read-only errors, not lossy rewrites.

Recovery order:

1. Return a valid, identity-matching final unchanged.
2. If final is missing/corrupt, restore a validated matching backup, checking removal/rename and rereading it.
3. Only the explicit create-intent policy may use a sole revision-one stage. Otherwise report recovery required or corruption.
4. Unsupported versions/fields, stale identities, directories, oversized records and I/O errors block mutation rather than falling back as if absent.

Save sequence: validate/encode → load/recover current → compare revision → inspect both stale candidates before cleanup → write/flush/close stage → verify exact staged bytes → rename old final to backup → rename stage to final → decode/validate final. Retain the previous committed backup until the next save's validated preflight. The older `StorageFiles::writeFileAtomic` helper removes backups without this validation and recovery contract, so this module reuses its bounded reader and performs its own checked replacement sequence; existing callers are unchanged.

## Verification

See the test results recorded below. `native_library_test` compiles the real organization module, `StorageFiles.cpp` and `StoragePaths.cpp`. Its isolated filesystem fixture injects open/read/write/flush/remove/rename failures and simulates interruption at every mutating update, creation and backup-recovery boundary. It checks that recovery preserves a committed record, can be repeated, allows safe retry, and never edits source content or bookmarks. Other tests cover minimal/full/Unicode round trips, truncated records, future schemas/fields, stale identities, overflow, malformed input, revision conflicts and path collisions.

SHA tests compile the installed ESP32 framework's actual `SHA2Builder.cpp` and `HEXBuilder.cpp` with small host API shims, rather than a substitute digest implementation. Empty-string, `abc` and million-`a` vectors pass; chunking, cancellation, short reads and size changes are covered. The native environment requires the project's ESP32 Arduino framework already installed by a board build.

Commands:

```sh
.local/venv/bin/pio test -e native_library_test
.local/venv/bin/pio test -e native_test -e native_watch_test
.local/venv/bin/pio run -e waveshare_esp32s3_touch_lcd_349_rev1 -e waveshare_esp32s3_touch_lcd_349_rev2
```

Results on 2026-10-18:

| Check | Result |
|---|---|
| Foundation suite | 33/33 passed, including exhaustive injected I/O schedules within tests |
| Existing `native_test` | 222/222 passed |
| Existing `native_watch_test` | 12/12 passed |
| Foundation under AddressSanitizer + UndefinedBehaviorSanitizer | 33/33 passed, no sanitizer findings; linked ASan runtime verified |
| LCD 3.49 rev1 build, final source | Passed; static RAM 124,468 bytes, application flash 2,948,658 bytes |
| LCD 3.49 rev2 build, final source | Passed; static RAM 124,468 bytes, application flash 2,948,678 bytes |
| Diff whitespace checks | Passed |

Sanitizer invocation (separate build directory):

```sh
PLATFORMIO_BUILD_DIR=.pio/build-library-sanitized \
PLATFORMIO_BUILD_FLAGS='-DGLZ_DEFAULT_OPTIMIZATION_SIZE -DGLZ_DISABLE_ALWAYS_INLINE -std=gnu++23 -Itest/test_library_organization/support -Itest/support -Isrc -lz -fsanitize=address,undefined -fno-omit-frame-pointer' \
.local/venv/bin/pio test -e native_library_test
```

Local code review checked error classification, mutation ordering, ownership assumptions and preservation of existing formats. Resolved findings included empty-array TOML scope, validating both stale candidates before cleanup, treating an empty-file diagnostic reopen failure as I/O, bounding serializer allocation and avoiding repeated string copies. This was an in-host review, not an independent outside review.

The new module is compiled but not called by the application; dead-code elimination means firmware size is not a runtime memory/performance measurement for the future integrated feature. The unrelated, previously documented native GPIO board-fixture Clang failure was not rerun or changed. These results cover the listed targets, not every project test environment.

## Remaining integration

T02 adds lifecycle events, status transitions and the multi-file Start again transaction; its stored intent is data only here. T03 adds registry reconciliation, feed matching, import intents and membership writes. T04+ implement filtering, sorting, remembered anchors, UI and companion/delete/USB integration. Real SD power-loss/removal behavior and physical rev2 usability require separate hardware validation. Simulated operation boundaries and successful builds do not prove FAT/controller durability. No flashing, remote review, push or release was performed.

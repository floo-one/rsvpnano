# Legacy library classification

On 2026-10-27 the owner reported that most items appeared in Needs review and explicitly chose **Treat older items as unread**. This supersedes the earlier requirement to keep otherwise valid legacy items unknown until manually classified.

## Cause and revised behavior

The local pre-flash SD backup contains 6 books and 24 articles in the correct directories, but no organization records. Only two of six saved bookmarks are at intermediate positions. Source type was already recognized; status filters hid the remaining items from their normal collections.

- Missing organization records and identity-valid `legacy-unclassified` records default to Unread when there is no bookmark or a valid beginning/end bookmark. A valid intermediate bookmark remains Reading. Explicit statuses remain authoritative.
- Corrupt, stale, unsupported or recovery-pending organization, invalid/out-of-range bookmarks, and existing bookmarks that cannot be assigned or validated stay in Review status. An unindexed item with no bookmark can be Unread without converting it first.
- The fallback is a deterministic read-only interpretation. Browsing does not bulk-write metadata, reset bookmarks, mark items complete or move files. Existing reader actions persist explicit status as before. Item actions use the same rule even when opened from Read before visiting Library.
- Older articles without confirmed feed membership use their saved `@source` publisher hostname for offline newsletter groups. These groups are labeled Saved articles. Exact full subdomains stay separate. Confirmed feed memberships take precedence; publisher groups never establish shared progress or duplicate identity. Missing/ambiguous sources remain Other articles.
- Publisher and feed groups share the existing 256-identity limit and memory budget. Publisher collection tabs and scroll anchors use the same saved navigation as feeds. Book sorting, default tabs and the three status controls stay unchanged.

## Parsing and persistence

The old metadata reader stopped at any line longer than 80 bytes, despite the writer allowing 256-byte titles/authors and 512-byte source URLs. The reader now uses bounded buffered header parsing (4 KiB, 64 lines, 528 bytes per retained line), handles EOF/BOM/CRLF, preserves exact complete source values, and stops at body text, escaped directives, paragraph/chapter markers and the header separator. Conflicting or oversized sources do not create publisher keys.

Publisher extraction accepts HTTP(S) DNS hosts, lowercases only the host, retains subdomains and rejects credentials, ports, invalid labels, address literals, controls, backslashes and oversized values. Group labels are hints about the saved publisher, not proof of the original RSS subscription.

Saved views alone now use schema version 2. Version 1 views load unchanged and upgrade on the next successful save, retaining the prior backup. Item, feed, import and bookmark formats remain unchanged. Older firmware sees the new view schema as unsupported and leaves it intact instead of treating publisher IDs as corruption. New firmware still refuses unknown future versions.

## Verification

The behavioral fixture reproduces the backup's 6-book/24-article shape: 28 Unread, 2 Reading, no routine status review, and two publisher groups. Tests cover unchanged bookmark bytes, explicit choices, damaged/stale/ambiguous bookmarks, one-word items, fallback source parsing, confirmed memberships, source navigation/selection/restoration, collection limits, version migration and interrupted saves. Real render/input tests retain the zero-filesystem-I/O interaction checks.

A read-only replay of the production header and publisher parsers against the actual ignored SD backup recovered 6/6 book titles and authors, 24/24 article titles and authors, and 24/24 exact source URLs. Publisher group sizes were 20 and 4. Parser tests and backup replay passed AddressSanitizer/UndefinedBehaviorSanitizer. Backup contents and publisher addresses remain local and are not committed.

Final validation on 2026-10-27:

| Check | Result |
| --- | --- |
| Native regular/watch, metadata, reading status, feed organization, library views/UI and RSVP headers | 389/389 passed |
| Metadata and library view suites under AddressSanitizer/UndefinedBehaviorSanitizer | 65/65 passed, no findings |
| Header tests and actual-backup parser replay under sanitizers | 8/8 tests; all 30 files parsed successfully |
| Final Waveshare LCD 3.49 rev1 and rev2 builds | Both passed |
| Generated localization and whitespace checks | Passed |

Logs are local and ignored: `.local/validation/legacy-library-tests.log`, `legacy-library-sanitized.log`, and `legacy-library-final-build.log`. Run the normal suite with `PLATFORMIO_BUILD_DIR=.pio/build-legacy-validation .local/venv/bin/pio test -e native_test -e native_watch_test -e native_library_test -e native_reading_status_test -e native_feed_organization_test -e native_library_view_test -e native_library_ui_test -e native_library_watch_ui_test -e native_rsvp_directives_test`.

Status: **DONE_WITH_CONCERNS**. Implementation and local validation are complete. Host tests do not execute App/FreeRTOS scheduling or establish physical touchscreen/SD behavior. No connected reader port was available, so this update has not been flashed or physically checked. Nothing was pushed or released during validation; the owner subsequently authorized committing and pushing the improvements to the development branch.

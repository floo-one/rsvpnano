#include "library/LibraryOrganization.h"

#include <FS.h>
#include <SHA2Builder.h>
#include <glaze/toml.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <span>
#include <type_traits>
#include <utility>

#include "hash/Fnv1a.h"
#include "library/ArticleSource.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/Utf8Text.h"

using LibraryOrganization::Status;
using LibraryOrganization::Tab;
template<> struct glz::meta<Status> {
    static constexpr auto value = glz::enumerate("unknown", Status::unknown, "unread", Status::unread,
                                                "reading", Status::reading, "read", Status::read);
};
template<> struct glz::meta<Tab> {
    static constexpr auto value = glz::enumerate("unread", Tab::unread, "reading", Tab::reading, "read", Tab::read);
};

namespace LibraryOrganization {
    namespace {
        class ErrorCategory final : public std::error_category {
            const char* name() const noexcept override { return "library-organization"; }
            std::string message(int value) const override {
                switch (static_cast<Error>(value)) {
                case Error::corrupt: return "Invalid organization record";
                case Error::unsupported_version: return "Unsupported organization version";
                case Error::unsupported_fields: return "Unsupported organization fields";
                case Error::stale_identity: return "Organization belongs to different content";
                case Error::revision_conflict: return "Organization changed; reload before saving";
                case Error::recovery_required: return "Uncommitted organization requires recovery";
                }
                return "Unknown organization error";
            }
        };

        std::error_code ioError() {
            return errno ? std::error_code{errno, std::generic_category()} : std::make_error_code(std::errc::io_error);
        }
        bool text(std::string_view value, size_t maximum) {
            if (value.size() > maximum) return false;
            uint32_t cp;
            while (!value.empty())
                if (!Utf8Text::decode(value, cp) || cp < 0x20 || (cp >= 0x7f && cp <= 0x9f)) return false;
            return true;
        }
        bool digest(std::string_view value) {
            return value.size() == 64 && std::ranges::all_of(value, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        }
        bool id(std::string_view value) {
            return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '_' || c == '-';
            });
        }
        bool path(std::string_view value) {
            if (!text(value, kMaxPathBytes) || !value.starts_with("/library/") || value.find('\\') != value.npos)
                return false;
            value.remove_prefix(9);
            if (value.starts_with("books/")) value.remove_prefix(6);
            else if (value.starts_with("articles/")) value.remove_prefix(9);
            return !value.empty() && !value.starts_with('.') && value.find('/') == value.npos
                && !StoragePaths::isHiddenOrSidecarPath(value)
                && (StoragePaths::hasTextExtension(value) || StoragePaths::hasRsvpExtension(value)
                    || StoragePaths::hasConvertibleDocumentExtension(value));
        }
        bool source(const SourceIdentity& value) {
            return value.bytes != std::numeric_limits<uint64_t>::max() && digest(value.sha256);
        }
        bool url(std::string_view value) {
            return text(value, kMaxPathBytes) && value.find(' ') == value.npos
                && ((value.starts_with("https://") && value.size() > 8)
                    || (value.starts_with("http://") && value.size() > 7));
        }
        bool tab(Tab value) { return value == Tab::unread || value == Tab::reading || value == Tab::read; }
        template<class Range, class Projection> bool unique(const Range& values, Projection project) {
            // Records are bounded (<=259); no extra hash table or attacker-controlled hashing.
            for (size_t i = 0; i < values.size(); ++i)
                for (size_t j = 0; j < i; ++j)
                    if (project(values[i]) == project(values[j])) return false;
            return true;
        }
        bool valid(const Item& r) {
            if (!r.match_scope.empty() && !id(r.match_scope)) return false;
            if (!r.last_operation.empty() && !id(r.last_operation)) return false;
            if (!path(r.path) || !source(r.source) || r.memberships.size() > kMaxMemberships
                || !text(r.unknown_reason, kMaxLabelBytes) || (!r.source_url.empty() && !url(r.source_url))
                || (!r.body_sha256.empty() && !digest(r.body_sha256))) return false;
            if (r.status != Status::unknown && r.status != Status::unread && r.status != Status::reading
                && r.status != Status::read) return false;
            if ((r.status == Status::unknown) != !r.unknown_reason.empty()) return false;
            if (r.indexed && (r.indexed->source_size == 0 || r.indexed->word_count == 0)) return false;
            if (!std::ranges::all_of(r.memberships, [](const Membership& m) { return id(m.feed_id) && m.batch > 0; })
                || !unique(r.memberships, [](const Membership& m) -> const std::string& { return m.feed_id; })) return false;
            if (r.restart && (!id(r.restart->operation_id) || r.restart->desired_word_index != 0
                || r.restart->previous_bookmark.size() > 2048
                || r.restart->had_bookmark != !r.restart->previous_bookmark.empty())) return false;
            return true;
        }
        bool valid(const Feeds& r) {
            if (r.next_order == 0 || r.feeds.size() > kMaxFeeds
                || std::ranges::count_if(r.feeds, [](const Feed& f) { return f.active; }) > kMaxActiveFeeds)
                return false;
            if (!std::ranges::all_of(r.feeds, [&](const Feed& f) {
                    return id(f.id) && url(f.url) && text(f.title, kMaxLabelBytes) && f.subscription_order > 0
                        && f.subscription_order < r.next_order
                        && (f.scan_digest.empty() || digest(f.scan_digest)) && f.scan_cursor <= 4 * 1024 * 1024 && f.scan_position <= 4 * 1024 * 1024
                        && (f.scan_digest.empty() ? f.scan_cursor == 0 && f.scan_position == 0 && f.scan_batch == 0 : f.scan_batch > 0)
                        && f.scan_batch < r.next_order;
                }) || !unique(r.feeds, [](const Feed& f) -> const std::string& { return f.id; })
                   || !unique(r.feeds, [](const Feed& f) -> const std::string& { return f.url; })) return false;
            return r.last_attempted_feed.empty() || std::ranges::any_of(r.feeds, [&](const Feed& f) {
                return f.id == r.last_attempted_feed;
            });
        }
        bool valid(const Import& r) {
            return valid(r.item) && r.item.revision == 1
                && r.item.status == (r.existing_content ? Status::unknown : Status::unread)
                && r.item.source.bytes > 0 && r.item.source.bytes <= kMaxImportBytes
                && r.item.path.starts_with("/library/articles/") && !r.item.restart
                && r.item.memberships.size() == 1 && !r.item.body_sha256.empty();
        }
        bool valid(const Views& r) {
            if (r.views.size() > kMaxViews
                || !unique(r.views, [](const View& v) -> const std::string& { return v.collection; })) return false;
            return std::ranges::all_of(r.views, [&](const View& v) {
                const bool collection = v.collection == "books" || v.collection == "needs-review"
                    || v.collection == "other-articles"
                    || (r.schema_version == kViewsSchemaVersion && v.collection.starts_with("source:")
                        && ArticleSource::validPublisherHost(std::string_view{v.collection}.substr(7)))
                    || (v.collection.starts_with("feed:") && id(std::string_view{v.collection}.substr(5)));
                return collection && tab(v.selected) && v.anchors.size() <= 3
                    && unique(v.anchors, [](const Anchor& a) { return a.tab; })
                    && std::ranges::all_of(v.anchors, [](const Anchor& a) {
                        return tab(a.tab) && (a.path.empty() || path(a.path)) && a.offset_px >= 0 && a.offset_px < 52
                            && (!a.path.empty() || a.offset_px == 0);
                    });
            });
        }

        constexpr std::string_view kPrefix = "# rsvpnano-library v1 ";
        std::array<char, 9> checksumFor(std::string_view body) {
            std::array<char, 9> checksum{};
            std::snprintf(checksum.data(), checksum.size(), "%08lx", static_cast<unsigned long>(Fnv1a::hash(body)));
            return checksum;
        }
        std::string wrap(std::string_view body) {
            const auto checksum = checksumFor(body);
            return std::string{kPrefix} + checksum.data() + '\n' + std::string{body};
        }
        std::expected<std::string_view, std::error_code> unwrap(std::string_view input) {
            if (input.size() > kMaxRecordBytes) return std::unexpected(std::make_error_code(std::errc::value_too_large));
            if (!input.starts_with(kPrefix)) {
                return std::unexpected(make_error_code(input.starts_with("# rsvpnano-library ")
                    ? Error::unsupported_version : Error::corrupt));
            }
            if (input.size() < kPrefix.size() + 9 || input[kPrefix.size() + 8] != '\n')
                return std::unexpected(make_error_code(Error::corrupt));
            const auto body = input.substr(kPrefix.size() + 9);
            const auto checksum = checksumFor(body);
            if (input.substr(kPrefix.size(), 8) != std::string_view{checksum.data(), 8})
                return std::unexpected(make_error_code(Error::corrupt));
            return body;
        }
        struct Version {
            uint32_t schema_version = 0;
            struct glaze { static constexpr auto value = glz::object("schema_version", &Version::schema_version); };
        };
        template<class T> bool supportedVersion(uint32_t version) {
            if constexpr (std::is_same_v<T, Views>)
                return version == kSchemaVersion || version == kViewsSchemaVersion;
            return version == kSchemaVersion;
        }
        template<class T> std::expected<T, std::error_code> decode(std::string_view input) {
            auto body = unwrap(input);
            if (!body) return std::unexpected(body.error());
            Version version;
            if (glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(version, *body))
                return std::unexpected(make_error_code(Error::corrupt));
            if (!supportedVersion<T>(version.schema_version))
                return std::unexpected(make_error_code(version.schema_version == 0 ? Error::corrupt : Error::unsupported_version));
            T record;
            if (auto error = glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = true}>(record, *body))
                return std::unexpected(make_error_code(error.ec == glz::error_code::unknown_key
                    ? Error::unsupported_fields : Error::corrupt));
            if (record.revision == 0 || !valid(record)) return std::unexpected(make_error_code(Error::corrupt));
            return record;
        }
        template<class T> std::expected<std::string, std::error_code> encodeRecord(const T& record) {
            if (!supportedVersion<T>(record.schema_version)) return std::unexpected(make_error_code(Error::unsupported_version));
            if (record.revision == 0 || !valid(record)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            // Fixed-capacity heap storage prevents a large in-memory registry
            // from growing the serializer beyond the on-disk bound. Extra room
            // covers Glaze's worst-case escaping reservation for the last field.
            std::string body(kMaxRecordBytes + 2 * kMaxPathBytes + glz::write_padding_bytes, '\0');
            std::span<char> buffer{body};
            // Inline arrays keep empty memberships/anchors at their owning scope.
            // Glaze's array-of-tables writer emits empty arrays after nested tables.
            const auto result = glz::write<glz::toml_opts{true}>(record, buffer);
            if (result)
                return std::unexpected(std::make_error_code(result.ec == glz::error_code::buffer_overflow
                    ? std::errc::value_too_large : std::errc::io_error));
            body.resize(result.count);
            auto output = wrap(body);
            if (output.size() > kMaxRecordBytes) return std::unexpected(std::make_error_code(std::errc::value_too_large));
            return output;
        }

        bool missing(std::error_code e) { return e == std::errc::no_such_file_or_directory; }
        bool damaged(std::error_code e) { return e == make_error_code(Error::corrupt); }
        template<class T, class Check>
        std::expected<T, std::error_code> read(fs::FS& fs, const std::string& name, Check check) {
            auto bytes = StorageFiles::readTextFile(fs, name.c_str(), kMaxRecordBytes);
            if (!bytes) {
                if (bytes.error() == std::errc::invalid_argument) {
                    // Empty file is damaged; a directory must never be removed as a file.
                    errno = 0;
                    File file = fs.open(name.c_str(), FILE_READ);
                    if (!file) return std::unexpected(ioError());
                    if (file.isDirectory()) return std::unexpected(std::make_error_code(std::errc::is_a_directory));
                    return std::unexpected(make_error_code(Error::corrupt));
                }
                return std::unexpected(bytes.error());
            }
            auto record = decode<T>(*bytes);
            if (record) {
                if (auto result = check(*record); !result) return std::unexpected(result.error());
            }
            return record;
        }
        std::expected<void, std::error_code> remove(fs::FS& fs, const std::string& name) {
            errno = 0;
            if (!fs.remove(name.c_str())) return std::unexpected(ioError());
            return {};
        }
        std::expected<void, std::error_code> rename(fs::FS& fs, const std::string& from, const std::string& to) {
            errno = 0;
            if (!fs.rename(from.c_str(), to.c_str())) return std::unexpected(ioError());
            return {};
        }
        template<class T, class Check>
        std::expected<T, std::error_code> load(fs::FS& fs, const std::string& name, Check check, Recovery policy) {
            auto final = read<T>(fs, name, check);
            if (final || (!missing(final.error()) && !damaged(final.error()))) return final;
            auto backup = read<T>(fs, name + ".bak", check);
            if (backup) {
                if (!missing(final.error())) {
                    if (auto result = remove(fs, name); !result) return std::unexpected(result.error());
                }
                if (auto result = rename(fs, name + ".bak", name); !result) return std::unexpected(result.error());
                return read<T>(fs, name, check);
            }
            if (!missing(backup.error()) && !damaged(backup.error())) return std::unexpected(backup.error());
            auto stage = read<T>(fs, name + ".tmp", check);
            if (stage) {
                if (policy == Recovery::stagedCreation && missing(final.error()) && missing(backup.error())
                    && stage->revision == 1) {
                    if (auto result = rename(fs, name + ".tmp", name); !result) return std::unexpected(result.error());
                    return read<T>(fs, name, check);
                }
                return std::unexpected(make_error_code(Error::recovery_required));
            }
            if (!missing(stage.error()) && !damaged(stage.error())) return std::unexpected(stage.error());
            if (policy == Recovery::stagedCreation && missing(final.error()) && missing(backup.error())
                && damaged(stage.error())) {
                // The durable create intent authorizes retrying a partial first
                // write. Never discard a final, backup or unsupported candidate.
                if (auto result = remove(fs, name + ".tmp"); !result) return std::unexpected(result.error());
                return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
            }
            if (damaged(final.error()) || damaged(backup.error()) || damaged(stage.error()))
                return std::unexpected(make_error_code(Error::corrupt));
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }
        template<class T, class Check>
        std::expected<bool, std::error_code> disposable(fs::FS& fs, const std::string& name, Check check) {
            auto candidate = read<T>(fs, name, check);
            if (!candidate && missing(candidate.error())) return false;
            if (!candidate && !damaged(candidate.error())) return std::unexpected(candidate.error());
            return true;
        }
        template<class T, class Check>
        std::expected<T, std::error_code> save(fs::FS& fs, const std::string& name, T record,
                                             uint64_t expectedRevision, Check check) {
            if (expectedRevision == std::numeric_limits<uint64_t>::max())
                return std::unexpected(std::make_error_code(std::errc::value_too_large));
            record.revision = expectedRevision + 1;
            auto encoded = encodeRecord(record);
            if (!encoded) return std::unexpected(encoded.error());
            auto current = load<T>(fs, name, check, Recovery::committedOnly);
            if (!current && !missing(current.error())) return std::unexpected(current.error());
            if ((current ? current->revision : 0) != expectedRevision)
                return std::unexpected(make_error_code(Error::revision_conflict));
            const std::string temporary = name + ".tmp";
            const std::string backup = name + ".bak";
            auto cleanStage = disposable<T>(fs, temporary, check);
            if (!cleanStage) return std::unexpected(cleanStage.error());
            auto cleanBackup = disposable<T>(fs, backup, check);
            if (!cleanBackup) return std::unexpected(cleanBackup.error());
            if (*cleanStage) {
                if (auto result = remove(fs, temporary); !result) return std::unexpected(result.error());
            }
            if (*cleanBackup) {
                if (auto result = remove(fs, backup); !result) return std::unexpected(result.error());
            }
            errno = 0;
            File file = fs.open(temporary.c_str(), FILE_WRITE);
            if (!file) return std::unexpected(ioError());
            if (file.isDirectory()) return std::unexpected(std::make_error_code(std::errc::is_a_directory));
            const auto written = file.write(reinterpret_cast<const uint8_t*>(encoded->data()), encoded->size());
            file.flush();
            file.close();
            if (written != encoded->size()) return std::unexpected(std::make_error_code(std::errc::io_error));
            // Verify exact bytes before moving the last committed file. A valid
            // checksum alone is not proof that the intended revision was written.
            auto staged = StorageFiles::readTextFile(fs, temporary.c_str(), kMaxRecordBytes);
            if (!staged) return std::unexpected(staged.error());
            if (*staged != *encoded) return std::unexpected(make_error_code(Error::corrupt));
            if (current) {
                if (auto result = rename(fs, name, name + ".bak"); !result) return std::unexpected(result.error());
            }
            if (auto result = rename(fs, temporary, name); !result) return std::unexpected(result.error());
            auto verified = read<T>(fs, name, check);
            if (!verified) return std::unexpected(verified.error());
            if (*verified != record) return std::unexpected(make_error_code(Error::corrupt));
            // Keep the previous valid backup until the NEXT save preflight.
            // This also makes post-rename read failures recoverable.
            return verified;
        }
        struct AnyRecord {
            template<class T> std::expected<void, std::error_code> operator()(const T&) const { return {}; }
        };
        struct ItemCheck {
            std::string_view path;
            const SourceIdentity& source;
            std::expected<void, std::error_code> operator()(const Item& item) const {
                if (item.path != path || item.source != source) return std::unexpected(make_error_code(Error::stale_identity));
                return {};
            }
        };
    }

    std::error_code make_error_code(Error error) {
        static ErrorCategory category;
        return {static_cast<int>(error), category};
    }
    static std::expected<SourceIdentity, std::error_code> hashFile(
        fs::FS& fs, std::string_view name, const std::function<bool(uint64_t, uint64_t)>& proceed) {
        errno = 0;
        File file = fs.open(std::string{name}.c_str(), FILE_READ);
        if (!file) return std::unexpected(ioError());
        if (file.isDirectory()) return std::unexpected(std::make_error_code(std::errc::is_a_directory));
        const uint64_t total = file.size();
        uint64_t processed = 0;
        SHA256Builder hash;
        hash.begin();
        std::array<uint8_t, 512> buffer;
        do {
            if (proceed && !proceed(processed, total))
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            if (file.size() != total) return std::unexpected(make_error_code(Error::stale_identity));
            if (processed == total) break;
            const size_t requested = std::min<uint64_t>(buffer.size(), total - processed);
            if (file.read(buffer.data(), requested) != requested)
                return std::unexpected(std::make_error_code(std::errc::io_error));
            hash.add(buffer.data(), requested);
            processed += requested;
        } while (true);
        file.close();
        hash.calculate();
        std::array<char, 65> digest{};
        hash.getChars(digest.data());
        return SourceIdentity{total, digest.data()};
    }
    std::expected<SourceIdentity, std::error_code> hashSource(fs::FS& fs, std::string_view name,
        const std::function<bool(uint64_t, uint64_t)>& proceed) {
        if (!path(name)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return hashFile(fs, name, proceed);
    }
    std::expected<SourceIdentity, std::error_code> hashImportStage(fs::FS& fs) { return hashFile(fs, kImportStage, [](uint64_t, uint64_t total) { return total <= kMaxImportBytes; }); }
    std::string hashBytes(std::string_view bytes) {
        SHA256Builder hash; hash.begin();
        for (size_t at = 0; at < bytes.size(); at += 512) {
            const auto count = std::min<size_t>(512, bytes.size() - at);
            hash.add(reinterpret_cast<const uint8_t*>(bytes.data() + at), count);
        }
        hash.calculate(); std::array<char, 65> result{}; hash.getChars(result.data()); return result.data();
    }
    std::expected<std::string, std::error_code> encode(const Import& r) { return encodeRecord(r); }
    std::expected<Import, std::error_code> decodeImport(std::string_view s) { return decode<Import>(s); }
    std::expected<Import, std::error_code> loadImport(fs::FS& fs, Recovery r) { return load<Import>(fs, kImportPath, AnyRecord{}, r); }
    std::expected<Import, std::error_code> saveImport(fs::FS& fs, Import r, uint64_t revision) {
        return save(fs, kImportPath, std::move(r), revision, AnyRecord{});
    }
    std::expected<std::string, std::error_code> encode(const Item& r) { return encodeRecord(r); }
    std::expected<std::string, std::error_code> encode(const Feeds& r) { return encodeRecord(r); }
    std::expected<std::string, std::error_code> encode(const Views& r) { return encodeRecord(r); }
    std::expected<Item, std::error_code> decodeItem(std::string_view s) { return decode<Item>(s); }
    std::expected<Feeds, std::error_code> decodeFeeds(std::string_view s) { return decode<Feeds>(s); }
    std::expected<Views, std::error_code> decodeViews(std::string_view s) { return decode<Views>(s); }
    std::expected<Item, std::error_code> loadItem(fs::FS& fs, std::string_view name, const SourceIdentity& identity, Recovery r) {
        if (!path(name) || !source(identity)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return load<Item>(fs, StoragePaths::libraryOrganizationPathFor(name), ItemCheck{name, identity}, r);
    }
    std::expected<Feeds, std::error_code> loadFeeds(fs::FS& fs, Recovery r) {
        return load<Feeds>(fs, StoragePaths::kLibraryFeedsPath, AnyRecord{}, r);
    }
    std::expected<Item, std::error_code> inspectItem(fs::FS& fs, std::string_view name) {
        if (!path(name)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        const auto check = [name](const Item& item) -> std::expected<void, std::error_code> {
            if (item.path != name) return std::unexpected(make_error_code(Error::stale_identity));
            return {};
        };
        const auto final = StoragePaths::libraryOrganizationPathFor(name);
        auto record = read<Item>(fs, final, check);
        if (record || (!missing(record.error()) && !damaged(record.error()))) return record;
        auto backup = read<Item>(fs, final + ".bak", check);
        if (backup || !missing(backup.error())) return backup;
        auto stage = read<Item>(fs, final + ".tmp", check);
        if (stage) return std::unexpected(make_error_code(Error::recovery_required));
        if (!missing(stage.error())) return stage;
        return record;
    }
    std::expected<Views, std::error_code> loadViews(fs::FS& fs, Recovery r) {
        return load<Views>(fs, StoragePaths::kLibraryViewPath, AnyRecord{}, r);
    }
    std::expected<Item, std::error_code> saveItem(fs::FS& fs, Item r, uint64_t revision) {
        // Own these before moving r (argument evaluation order must not dangle them).
        const std::string ownedPath = r.path;
        const SourceIdentity identity = r.source;
        return save(fs, StoragePaths::libraryOrganizationPathFor(ownedPath), std::move(r), revision, ItemCheck{ownedPath, identity});
    }
    std::expected<Feeds, std::error_code> saveFeeds(fs::FS& fs, Feeds r, uint64_t revision) {
        return save(fs, StoragePaths::kLibraryFeedsPath, std::move(r), revision, AnyRecord{});
    }
    std::expected<Views, std::error_code> saveViews(fs::FS& fs, Views r, uint64_t revision) {
        if (!supportedVersion<Views>(r.schema_version)) return std::unexpected(make_error_code(Error::unsupported_version));
        // Upgrade only on an explicit save. Loading v1 leaves its exact bytes
        // intact, and the existing atomic writer retains it as rollback backup.
        r.schema_version = kViewsSchemaVersion;
        return save(fs, StoragePaths::kLibraryViewPath, std::move(r), revision, AnyRecord{});
    }
}

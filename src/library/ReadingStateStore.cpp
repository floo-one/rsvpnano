#include "library/ReadingStateStore.h"
#include <FS.h>
#include <glaze/toml.hpp>
#include <algorithm>
#include "settings/SettingsGlaze.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
namespace ReadingStateStore {
    std::expected<reading::StoredState, std::error_code> decode(std::string_view bytes, const reading::BookIdentity& identity, bool strict) {
        if (bytes.empty() || bytes.size() > kMaxBytes || identity.sourceSize == 0 || identity.wordCount == 0)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        reading::StoredState state;
        const auto error = strict
            ? glz::read<glz::opts{.format=glz::TOML, .error_on_unknown_keys=true}>(state, bytes)
            : glz::read<glz::opts{.format=glz::TOML, .error_on_unknown_keys=false}>(state, bytes);
        if (error) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (state.identity != identity) return std::unexpected(std::make_error_code(std::errc::state_not_recoverable));
        if (strict && state.reading.wordIndex >= identity.wordCount)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        state.reading.wordIndex = std::min(state.reading.wordIndex, identity.wordCount - 1);
        return state;
    }
    std::expected<std::string, std::error_code> encode(const reading::StoredState& state) {
        if (!state.identity.sourceSize || !state.identity.wordCount || state.reading.wordIndex >= state.identity.wordCount)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        std::string bytes;
        if (glz::write<glz::toml_opts{true}>(state, bytes)) return std::unexpected(std::make_error_code(std::errc::io_error));
        if (bytes.size() > kMaxBytes) return std::unexpected(std::make_error_code(std::errc::value_too_large));
        return bytes;
    }
    std::expected<std::optional<std::string>, std::error_code> snapshot(fs::FS& fs, std::string_view path) {
        const auto final = StoragePaths::bookStatePathFor(path);
        auto bytes = StorageFiles::readTextFile(fs, final.c_str(), kMaxBytes);
        if (!bytes && bytes.error() == std::errc::no_such_file_or_directory) {
            // A previous atomic bookmark write may have stopped after moving
            // final to backup. Prefer that commit, then a sole staged bookmark.
            bytes = StorageFiles::readTextFile(fs, (final + ".bak").c_str(), kMaxBytes);
            if (!bytes && bytes.error() == std::errc::no_such_file_or_directory)
                bytes = StorageFiles::readTextFile(fs, (final + ".tmp").c_str(), kMaxBytes);
        }
        if (!bytes) {
            if (bytes.error() == std::errc::no_such_file_or_directory) return std::nullopt;
            return std::unexpected(bytes.error());
        }
        return std::move(*bytes);
    }
    std::expected<reading::StoredState, std::error_code> read(fs::FS& fs, std::string_view path, const reading::BookIdentity& identity, bool strict) {
        auto bytes = snapshot(fs, path);
        if (!bytes) return std::unexpected(bytes.error());
        if (!*bytes) return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        return decode(**bytes, identity, strict);
    }
    std::expected<void, std::error_code> write(fs::FS& fs, std::string_view path, const reading::StoredState& state) {
        auto bytes = encode(state);
        if (!bytes) return std::unexpected(bytes.error());
        const auto final = StoragePaths::bookStatePathFor(path);
        const auto temp = final + ".tmp";
        const auto backup = final + ".bak";
        auto result = StorageFiles::writeFileAtomic(fs, final.c_str(), temp.c_str(), backup.c_str(), *bytes);
        if (!result) return result;
        auto verified = StorageFiles::readTextFile(fs, final.c_str(), kMaxBytes);
        if (!verified) return std::unexpected(verified.error());
        if (*verified != *bytes) return std::unexpected(std::make_error_code(std::errc::io_error));
        return {};
    }
}

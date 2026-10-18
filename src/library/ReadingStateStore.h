#pragma once
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include "reader/ReadingState.h"
namespace fs { class FS; }
// Legacy bookmark codec/storage. Organization transaction ownership is enforced
// by ReadingProgress/ReadingOrganization, not by this lower-level adapter.
namespace ReadingStateStore {
    constexpr size_t kMaxBytes = 2048;
    std::expected<reading::StoredState, std::error_code> decode(std::string_view, const reading::BookIdentity&, bool strict = false);
    std::expected<std::string, std::error_code> encode(const reading::StoredState&);
    std::expected<std::optional<std::string>, std::error_code> snapshot(fs::FS&, std::string_view path);
    std::expected<reading::StoredState, std::error_code> read(fs::FS&, std::string_view path, const reading::BookIdentity&);
    std::expected<void, std::error_code> write(fs::FS&, std::string_view path, const reading::StoredState&);
}

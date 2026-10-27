#pragma once

#include <string>
#include <string_view>

namespace fs { class FS; }

namespace RsvpText {

    struct RsvpDirectiveValues {
        std::string title;
        std::string author;
        // Exact header value, never normalized or truncated for matching.
        std::string source;
    };

    std::string_view stripBom(std::string_view text);
    bool prefixHasBoundary(std::string_view text, std::string_view prefix);
    bool chapterTitleFromLine(std::string_view line, std::string& title);
    std::string directiveValue(std::string_view line, std::string_view directive);
    RsvpDirectiveValues readRsvpDirectiveValues(std::string_view path);
    RsvpDirectiveValues readRsvpDirectiveValues(fs::FS& fs, std::string_view path);

} // namespace RsvpText

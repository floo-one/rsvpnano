#include "text/RsvpDirectives.h"

#include <algorithm>
#include <array>

#include "board/BoardStorage.h"

#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"
#include "text/TextNormalizer.h"
#include "text/Utf8Text.h"

namespace RsvpText {

    using namespace StoragePaths;

    constexpr size_t kMaxChapterTitleChars = 64;

    bool prefixHasBoundary(std::string_view text, std::string_view prefix) {
        const size_t prefixLength = prefix.length();
        if (text.length() < prefixLength
            || !std::ranges::equal(text.substr(0, prefixLength), prefix, {}, AsciiText::toLower, AsciiText::toLower))
            return false;
        if (text.length() == prefixLength) {
            return true;
        }

        const char next = text[prefixLength];
        return AsciiText::isWhitespace(next) || next == ':' || next == '.' || next == '-';
    }

    std::string_view stripBom(std::string_view text) {
        text = AsciiText::trim(text);
        if (text.length() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB
            && static_cast<uint8_t>(text[2]) == 0xBF) {
            text.remove_prefix(3);
            text = AsciiText::trim(text);
        }
        return text;
    }

    bool chapterTitleFromLine(std::string_view line, std::string& title) {
        const std::string trimmed = normalizeDisplayText(stripBom(line));
        if (trimmed.empty() || trimmed.length() > kMaxChapterTitleChars) {
            return false;
        }

        if (trimmed.starts_with('#')) {
            size_t prefixLength = 0;
            while (prefixLength < trimmed.length() && trimmed[prefixLength] == '#') {
                ++prefixLength;
            }
            title = AsciiText::trim(std::string_view{trimmed}.substr(prefixLength));
            return !title.empty();
        }

        if (prefixHasBoundary(trimmed, "chapter") || prefixHasBoundary(trimmed, "part")
            || prefixHasBoundary(trimmed, "book")) {
            title = trimmed;
            return true;
        }

        return false;
    }

    std::string directiveValue(std::string_view line, std::string_view directive) {
        std::string_view value = AsciiText::trim(line.substr(directive.length()));
        if (!value.empty() && (value.front() == ':' || value.front() == '-' || value.front() == '.')) {
            value.remove_prefix(1);
            value = AsciiText::trim(value);
        }
        return normalizeDisplayText(value);
    }

    RsvpDirectiveValues readRsvpDirectiveValues(std::string_view path) {
        return readRsvpDirectiveValues(Board::Storage::filesystem(), path);
    }

    RsvpDirectiveValues readRsvpDirectiveValues(fs::FS& fs, std::string_view path) {
        RsvpDirectiveValues values;
        const std::string ownedPath{path};
        if (!hasRsvpExtension(path)) {
            return values;
        }

        File file = fs.open(ownedPath.c_str());
        if (!file || file.isDirectory()) {
            if (file) {
                file.close();
            }
            return values;
        }

        // Match the writer's field limits, independently of chapter-heading
        // detection. A long source/converter line must not hide a later author.
        constexpr size_t maxDisplayBytes = 256, maxSourceBytes = 512;
        constexpr size_t maxLineBytes = maxSourceBytes + 16;
        constexpr size_t maxHeaderBytes = 4096, maxHeaderLines = 64;
        std::string line;
        line.reserve(maxLineBytes);
        bool overflow = false, sawDirective = false, sourceConflict = false;
        size_t lines = 0;
        const auto consumeLine = [&] {
            if (++lines > maxHeaderLines) return false;
            const std::string_view trimmed = stripBom(line);
            if (trimmed.empty()) return !sawDirective;
            // The first body line, including an escaped @ or chapter marker,
            // ends the header. Never discover metadata in the reading content.
            if (!trimmed.starts_with('@') || trimmed.starts_with("@@")
                || prefixHasBoundary(trimmed, "@chapter") || prefixHasBoundary(trimmed, "@para")) return false;
            sawDirective = true;
            if (!overflow) {
                if (values.title.empty() && prefixHasBoundary(trimmed, "@title")) {
                    values.title = directiveValue(trimmed, "@title");
                    values.title.resize(Utf8Text::prefix(values.title, maxDisplayBytes).size());
                } else if (values.author.empty() && prefixHasBoundary(trimmed, "@author")) {
                    values.author = directiveValue(trimmed, "@author");
                    values.author.resize(Utf8Text::prefix(values.author, maxDisplayBytes).size());
                } else if (!sourceConflict && prefixHasBoundary(trimmed, "@source")) {
                    auto source = AsciiText::trim(trimmed.substr(7));
                    if (!source.empty() && (source.front() == ':' || source.front() == '-' || source.front() == '.')) {
                        source.remove_prefix(1);
                        source = AsciiText::trim(source);
                    }
                    if (source.size() > maxSourceBytes || source.find_first_of("\r\n\t\0", 0, 4) != std::string_view::npos
                        || (!values.source.empty() && values.source != source)) {
                        values.source.clear();
                        sourceConflict = true;
                    } else {
                        values.source = source;
                    }
                }
            } else if (prefixHasBoundary(trimmed, "@source")) {
                values.source.clear();
                sourceConflict = true;
            }
            return true;
        };
        std::array<uint8_t, 128> buffer{};
        size_t bytes = 0;
        bool header = true;
        while (header && bytes < maxHeaderBytes && lines < maxHeaderLines) {
            const size_t length = file.read(buffer.data(), std::min(buffer.size(), maxHeaderBytes - bytes));
            if (!length) break;
            bytes += length;
            for (size_t i = 0; i < length && header; ++i) {
                const char c = static_cast<char>(buffer[i]);
                if (c == '\n') {
                    header = consumeLine() && lines < maxHeaderLines;
                    line.clear();
                    overflow = false;
                } else {
                    if (line.size() < maxLineBytes) line += c;
                    else overflow = true;
                }
            }
        }
        // Include a complete final line even when it ends exactly at the byte
        // limit. A failed read or exhausted budget cannot publish a partial URL.
        if (header && lines < maxHeaderLines && !line.empty() && file.position() == file.size()) consumeLine();

        file.close();
        return values;
    }

} // namespace RsvpText

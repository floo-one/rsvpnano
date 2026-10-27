#include <FS.h>
#include <unity.h>
#include "text/RsvpDirectives.h"

namespace Board::Storage {
    fs::FS& filesystem() { static fs::FS fs; return fs; }
}
void setUp() {}
void tearDown() {}

void long_writer_headers_keep_title_and_author() {
    auto& fs = Board::Storage::filesystem();
    const std::string title(256, 't'), author(256, 'a');
    fs.put("/book.rsvp", "@title " + title + "\n@source https://example.com/" + std::string(200, 's')
                           + "\n@author " + author + "\n\nBody");
    auto values = RsvpText::readRsvpDirectiveValues("/book.rsvp");
    TEST_ASSERT_EQUAL_STRING(title.c_str(), values.title.c_str());
    TEST_ASSERT_EQUAL_STRING(author.c_str(), values.author.c_str());
    TEST_ASSERT_EQUAL_STRING(("https://example.com/" + std::string(200, 's')).c_str(), values.source.c_str());
}

RsvpText::RsvpDirectiveValues parse(const std::string& bytes) {
    fs::FS fs;
    fs.put("/book.rsvp", bytes);
    return RsvpText::readRsvpDirectiveValues(fs, "/book.rsvp");
}
void sources_are_exact_and_headers_support_eof_bom_and_crlf() {
    const std::string source = "https://EXAMPLE.com/Exact?x=%2F&Y=Case#Fragment";
    const auto values = parse("\xef\xbb\xbf@rsvp 1\r\n@title Title\r\n@author Author\r\n@source " + source);
    TEST_ASSERT_EQUAL_STRING("Title", values.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Author", values.author.c_str());
    TEST_ASSERT_EQUAL_STRING(source.c_str(), values.source.c_str());
    const std::string longest = "https://example.com/" + std::string(492, 's');
    TEST_ASSERT_EQUAL(512, longest.size());
    TEST_ASSERT_EQUAL_STRING(longest.c_str(), parse("@source " + longest + "\r\n@author A\n").source.c_str());
}
void oversized_directives_do_not_hide_author_or_become_partial_sources() {
    auto values = parse("@title Good\n@converter " + std::string(900, 'x') + "\n@author Author\n\nBody");
    TEST_ASSERT_EQUAL_STRING("Good", values.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Author", values.author.c_str());
    values = parse("@source https://example.com/" + std::string(600, 'x') + "\n@author Author\n");
    TEST_ASSERT_TRUE(values.source.empty());
    TEST_ASSERT_EQUAL_STRING("Author", values.author.c_str());
    values = parse("@source https://a.example/\n@source https://b.example/\n");
    TEST_ASSERT_TRUE(values.source.empty());
    TEST_ASSERT_TRUE(parse("@source https://ex\rample.com/\n").source.empty());
}
void body_cannot_supply_metadata() {
    for (const auto* body : {"Ordinary body", "@@literal at sign", "@chapter Chapter one", "@para", ""}) {
        const auto values = parse(std::string{"@rsvp 1\n@title Real title\n"} + body
                                  + "\n@source https://not-metadata.example/\n@author Not metadata\n");
        TEST_ASSERT_EQUAL_STRING("Real title", values.title.c_str());
        TEST_ASSERT_TRUE(values.source.empty());
        TEST_ASSERT_TRUE(values.author.empty());
    }
}
void scans_and_allocations_are_bounded() {
    fs::FS fs;
    fs.put("/book.rsvp", "@converter " + std::string(10000, 'x') + "\n@author Hidden\n");
    size_t reads = 0;
    fs.disk->fail = [&](const auto& op, const auto&) { reads += op == "read"; return false; };
    TEST_ASSERT_TRUE(RsvpText::readRsvpDirectiveValues(fs, "/book.rsvp").author.empty());
    TEST_ASSERT_LESS_OR_EQUAL(32, reads);
    std::string manyLines;
    for (size_t i = 0; i < 64; ++i) manyLines += "@extension x\n";
    TEST_ASSERT_TRUE(parse(manyLines + "@author Hidden\n").author.empty());
}
void read_failure_does_not_accept_partial_source() {
    fs::FS fs;
    fs.put("/book.rsvp", "@source https://example.com/" + std::string(200, 's'));
    size_t reads = 0;
    fs.disk->fail = [&](const auto& op, const auto&) { return op == "read" && ++reads == 2; };
    TEST_ASSERT_TRUE(RsvpText::readRsvpDirectiveValues(fs, "/book.rsvp").source.empty());
}
void exact_scan_budget_preserves_complete_final_source() {
    const std::string last = "@source https://example.com/end";
    const std::string prefix = "@converter " + std::string(4096 - 12 - last.size(), 'x') + "\n";
    TEST_ASSERT_EQUAL(4096, prefix.size() + last.size());
    TEST_ASSERT_EQUAL_STRING("https://example.com/end", parse(prefix + last).source.c_str());
    TEST_ASSERT_TRUE(parse(prefix + last + "unfinished").source.empty());
}
void unicode_display_values_are_bounded_on_codepoint_boundaries() {
    std::string title;
    for (size_t i = 0; i < 90; ++i) title += "界";
    auto values = parse("@title " + title + "\n@author Author\n");
    TEST_ASSERT_EQUAL(255, values.title.size());
    TEST_ASSERT_EQUAL_STRING(title.substr(0, 255).c_str(), values.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Author", values.author.c_str());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(long_writer_headers_keep_title_and_author);
    RUN_TEST(sources_are_exact_and_headers_support_eof_bom_and_crlf);
    RUN_TEST(oversized_directives_do_not_hide_author_or_become_partial_sources);
    RUN_TEST(body_cannot_supply_metadata);
    RUN_TEST(scans_and_allocations_are_bounded);
    RUN_TEST(read_failure_does_not_accept_partial_source);
    RUN_TEST(exact_scan_budget_preserves_complete_final_source);
    RUN_TEST(unicode_display_values_are_bounded_on_codepoint_boundaries);
    return UNITY_END();
}

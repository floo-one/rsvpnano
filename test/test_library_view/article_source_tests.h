#pragma once

#include <unity.h>
#include "library/ArticleSource.h"

namespace ArticleSourceTests {
    inline void valid_urls_preserve_publisher_boundaries() {
        TEST_ASSERT_EQUAL_STRING("example.com", ArticleSource::publisherHost("http://example.com").c_str());
        TEST_ASSERT_EQUAL_STRING("news.example.com", ArticleSource::publisherHost("HTTPS://News.Example.COM/posts/one").c_str());
        TEST_ASSERT_EQUAL_STRING("www.example.com", ArticleSource::publisherHost("https://www.example.com/?next=other@example.net#@elsewhere.test").c_str());
        TEST_ASSERT_EQUAL_STRING("writer-one.example.com", ArticleSource::publisherHost("https://writer-one.example.com/p/a%20b").c_str());
        TEST_ASSERT_TRUE(ArticleSource::publisherHost("https://one.example.com") != ArticleSource::publisherHost("https://two.example.com"));
    }

    inline void ambiguous_authorities_and_schemes_are_rejected() {
        constexpr std::string_view invalid[] = {
            "", "example.com/post", "//example.com/post", "ftp://example.com", "http:/example.com", "https:///post",
            "https://user@example.com", "https://user:pass@example.com", "https://example.com:443/post",
            "http://example.com:80", "https://[2001:db8::1]/post", "https://127.0.0.1/post",
            "https://2130706433/post", "https://example%2ecom/post", "https://example.com\\@other.test/post",
            " https://example.com", "https://example.com/white space", "https://example.com/\tpost",
            "https://example.com/\npost", "https://example.com/\x7fpost", "https://example.com/\x01post"
        };
        for (const auto url : invalid) TEST_ASSERT_TRUE_MESSAGE(ArticleSource::publisherHost(url).empty(), url.data());
        const std::string embeddedNul{"https://example.com/\0post", 25};
        TEST_ASSERT_TRUE(ArticleSource::publisherHost(embeddedNul).empty());
    }

    inline void saved_hosts_require_canonical_dns_labels() {
        TEST_ASSERT_TRUE(ArticleSource::validPublisherHost("writer-2.example.com"));
        TEST_ASSERT_TRUE(ArticleSource::validPublisherHost("xn--bcher-kva.example"));
        constexpr std::string_view invalid[] = {
            "", "Example.com", ".example.com", "example..com", "example.com.", "-example.com", "example-.com",
            "example.-com", "example.com-", "ex_ample.com", "example.com:443", "a@b.com", "127.0.0.1", "[::1]",
            "example.com/path", "example.com?query", "example.com#fragment", "example.com\\other", "bücher.example"
        };
        for (const auto host : invalid) TEST_ASSERT_FALSE_MESSAGE(ArticleSource::validPublisherHost(host), host.data());
        TEST_ASSERT_TRUE(ArticleSource::publisherHost("https://-example.com").empty());
        TEST_ASSERT_TRUE(ArticleSource::publisherHost("https://example..com").empty());
    }

    inline void source_and_label_sizes_are_bounded() {
        const std::string longestLabel(63, 'a');
        const std::string longestHost = longestLabel + '.' + longestLabel + '.' + longestLabel + '.' + std::string(61, 'b');
        TEST_ASSERT_EQUAL(253, longestHost.size());
        TEST_ASSERT_TRUE(ArticleSource::validPublisherHost(longestHost));
        TEST_ASSERT_EQUAL_STRING(longestHost.c_str(), ArticleSource::publisherHost("https://" + longestHost + '/').c_str());
        TEST_ASSERT_FALSE(ArticleSource::validPublisherHost(longestHost + 'b'));
        TEST_ASSERT_FALSE(ArticleSource::validPublisherHost(std::string(64, 'a') + ".example"));

        std::string url = "https://example.com/";
        url.append(ArticleSource::kMaxSourceBytes - url.size(), 'a');
        TEST_ASSERT_EQUAL_STRING("example.com", ArticleSource::publisherHost(url).c_str());
        url += 'a';
        TEST_ASSERT_TRUE(ArticleSource::publisherHost(url).empty());
    }

    inline void run() {
        RUN_TEST(valid_urls_preserve_publisher_boundaries);
        RUN_TEST(ambiguous_authorities_and_schemes_are_rejected);
        RUN_TEST(saved_hosts_require_canonical_dns_labels);
        RUN_TEST(source_and_label_sizes_are_bounded);
    }
}

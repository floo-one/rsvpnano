#pragma once

#include <string>
#include <string_view>

// Publisher labels are presentation hints only. They must never establish feed
// membership, article identity, or shared reading progress.
namespace ArticleSource {
    constexpr size_t kMaxSourceBytes = 512;
    constexpr size_t kMaxPublisherHostBytes = 253;

    // Saved source collection IDs use this canonical, lowercase DNS form.
    inline bool validPublisherHost(std::string_view host) {
        if (host.empty() || host.size() > kMaxPublisherHostBytes) return false;
        bool hasLetter = false;
        size_t labelStart = 0;
        for (size_t i = 0; i <= host.size(); ++i) {
            if (i == host.size() || host[i] == '.') {
                const size_t length = i - labelStart;
                if (!length || length > 63 || host[labelStart] == '-' || host[i - 1] == '-') return false;
                labelStart = i + 1;
                continue;
            }
            const char c = host[i];
            const bool letter = c >= 'a' && c <= 'z';
            if (!letter && !(c >= '0' && c <= '9') && c != '-') return false;
            hasLetter = hasLetter || letter;
        }
        // Numeric addresses are not publisher names; IPv6 and escaped forms are
        // already rejected by the DNS-label character rules above.
        return hasLetter;
    }

    inline std::string publisherHost(std::string_view url) {
        if (url.empty() || url.size() > kMaxSourceBytes) return {};
        for (const unsigned char c : url)
            if (c <= 0x20 || c == 0x7f || c == '\\') return {};

        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
        const auto scheme = url.find("://");
        if (scheme != 4 && scheme != 5) return {};
        constexpr std::string_view https = "https";
        for (size_t i = 0; i < scheme; ++i)
            if (lower(url[i]) != https[i]) return {};

        const size_t start = scheme + 3;
        const auto end = url.find_first_of("/?#", start);
        const auto authority = url.substr(start, end == url.npos ? url.npos : end - start);
        if (authority.empty() || authority.size() > kMaxPublisherHostBytes) return {};
        // Do not reinterpret credentials, port numbers, or encoded hostnames.
        // Keeping all subdomains intact avoids merging separate publications.
        std::string host;
        host.reserve(authority.size());
        for (const char c : authority) host += lower(c);
        return validPublisherHost(host) ? host : std::string{};
    }
}

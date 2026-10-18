#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "library/BookMetadata.h"
#include "reader/ReadingState.h"
#include "library/IndexedBookStore.h"
#include "library/ReadingOrganization.h"

struct ReadingSession {
    BookMetadata metadata;
    size_t lastSavedWordIndex = static_cast<size_t>(-1);
    uint32_t lastAdvanceMs = 0;
    uint32_t lastSaveMs = 0;
    std::string currentWord;
    std::span<const std::string> words;
    const IndexedBookStore* bookStore = nullptr;
    bool playing = false;
    bool loadedWords = false;
    bool resetWordClock = false;
    bool readingStarted = false;
    bool naturalEnd = false;
    uint64_t generation = 0;
    uint64_t activity = 0;
    std::optional<ReadingOrganization::Target> organization;
    std::optional<ReadingOrganization::Request> failedOrganization;
    std::error_code organizationError;
    bool organizationErrorReported = false;
    bool restartBlocked = false;
    reading::State state;

    bool stored() const {
        return bookStore != nullptr && bookStore->isOpen();
    }
    std::string_view sourcePath() const {
        return stored() ? bookStore->sourcePath() : std::string_view{};
    }
};

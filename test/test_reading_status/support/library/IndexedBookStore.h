#pragma once
#include "reader/ReadingState.h"
#include <string>
#include <vector>
// Only the indexed word source is a fixture; loop, progress writers and
// organization transaction are production code in this environment.
class IndexedBookStore {
public:
    bool opened = false;
    std::string path;
    reading::BookIdentity bookIdentity;
    std::vector<std::string> words;
    bool isOpen() const { return opened; }
    std::string_view sourcePath() const { return path; }
    const reading::BookIdentity& identity() const { return bookIdentity; }
    size_t wordCount() const { return words.size(); }
    std::string_view wordAt(size_t i) const { return words.at(i); }
    void prefetchAround(size_t) const {}
};

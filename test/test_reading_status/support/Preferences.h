#pragma once
#include <string>
class Preferences {
public:
    std::string lastBook;
    size_t putString(const char*, const char* value) { lastBook = value; return lastBook.size(); }
};

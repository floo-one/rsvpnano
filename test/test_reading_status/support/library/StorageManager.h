#pragma once
#include "library/BookLibrary.h"
class StorageManager {
public:
    int findBook(std::string_view) const { return -1; }
    const BookLibrary::Entry* book(size_t) const { return nullptr; }
};

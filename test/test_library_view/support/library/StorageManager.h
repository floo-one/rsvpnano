#pragma once
#include <span>
#include <optional>
#include "library/BookLibrary.h"
#include "library/BookMetadata.h"
#include "reader/ReadingState.h"
// Inventory/metadata adapter only. Tests compile the actual shared screen builder.
class StorageManager {
public:
    BookLibrary::Listing inventory;
    uint64_t revision = 1;
    size_t metadataReads = 0;
    bool available = true;
    bool mounted() const { return available; }
    uint64_t generation() const { return revision; }
    std::span<const BookLibrary::Entry> books() const { return inventory; }
    const BookLibrary::Entry* book(size_t i) const { return i < inventory.size() ? &inventory[i] : nullptr; }
    int findBook(std::string_view path) const {
        for (size_t i=0; i<inventory.size(); ++i) if (inventory[i].path==path) return static_cast<int>(i);
        return -1;
    }
    std::optional<reading::BookIdentity> readBookMetadata(size_t, BookMetadata&) {
        ++metadataReads; return reading::BookIdentity{3,42,5};
    }
};

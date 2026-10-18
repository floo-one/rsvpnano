#pragma once
#include <cstddef>
#include <cstdint>
// SHA2Builder's unused streaming adapter needs these methods to link. The
// production wrapper exercises byte-array add(), with real ESP32 SHA code.
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual size_t readBytes(uint8_t*, size_t) = 0;
};

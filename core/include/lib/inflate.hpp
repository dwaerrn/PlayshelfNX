#pragma once

#include <cstddef>
#include <cstdint>

namespace lib::inflate {
    // Decompresses a raw DEFLATE stream (no zlib/gzip header, i.e. wbits = -15) into out.
    // Returns true on success and stores the number of bytes written in *outLen.
    bool raw(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, size_t* outLen);
}

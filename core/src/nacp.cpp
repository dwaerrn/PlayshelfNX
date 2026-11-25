#include "lib/nacp.hpp"

#include <cstring>
#include <vector>

#include "lib/inflate.hpp"

namespace lib::nacp {
namespace {

constexpr size_t kEntrySize = 0x300;
constexpr size_t kNameSize = 0x200;
constexpr size_t kFormatOffset = 0x3215;
constexpr int kClassicEntries = 16;
constexpr int kMaxEntries = 32;

// Copy of a fixed-size NACP string field. Never reads past the field and returns an empty
// string if the text is not valid UTF-8 or contains control characters.
std::string safeString(const uint8_t* field, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && field[len] != 0) {
        len++;
    }

    size_t i = 0;
    while (i < len) {
        unsigned char c = field[i];
        size_t extra;
        if (c < 0x20) {
            return "";
        } else if (c < 0x80) {
            extra = 0;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
        } else {
            return "";
        }
        for (size_t j = 1; j <= extra; j++) {
            if (i + j >= len || (field[i + j] & 0xC0) != 0x80) {
                return "";
            }
        }
        i += extra + 1;
    }
    return std::string(reinterpret_cast<const char*>(field), len);
}

}  // namespace

std::string extractName(const uint8_t* nacp, size_t size, int preferredLanguage) {
    if (nacp == nullptr || size < kNacpSize) {
        return "";
    }

    // Language entries: 16 in the classic layout, up to 32 once the compressed block is expanded
    std::vector<uint8_t> titles(kMaxEntries * kEntrySize, 0);
    int entries = kClassicEntries;
    if (nacp[kFormatOffset] == 1) {
        size_t compSize = static_cast<size_t>(nacp[0]) | (static_cast<size_t>(nacp[1]) << 8);
        size_t outLen = 0;
        if (compSize == 0 || compSize > 0x2FFE ||
            !inflate::raw(nacp + 2, compSize, titles.data(), titles.size(), &outLen) || outLen < kEntrySize) {
            return "";
        }
        entries = kMaxEntries;
    } else {
        std::memcpy(titles.data(), nacp, kClassicEntries * kEntrySize);
    }

    // Preferred language first, then every other entry in order
    if (preferredLanguage >= 0 && preferredLanguage < entries) {
        std::string name = safeString(titles.data() + preferredLanguage * kEntrySize, kNameSize);
        if (!name.empty()) {
            return name;
        }
    }
    for (int i = 0; i < entries; i++) {
        std::string name = safeString(titles.data() + i * kEntrySize, kNameSize);
        if (!name.empty()) {
            return name;
        }
    }
    return "";
}

}  // namespace lib::nacp

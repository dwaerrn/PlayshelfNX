#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lib::nacp {

constexpr size_t kNacpSize = 0x4000;

// Returns the title name stored in a control.nacp image (0x4000 bytes).
//  - Handles the compressed title block of newer firmware (TitlesDataFormat == 1, 32 languages)
//  - preferredLanguage is the NACP language index (0 = American English, 2 = Japanese, ...)
//  - Never reads outside a name field and rejects text that is not valid UTF-8
// Returns an empty string if no usable name exists.
std::string extractName(const uint8_t* nacp, size_t size, int preferredLanguage);

}  // namespace lib::nacp

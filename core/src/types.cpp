#include "lib/types.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace lib {

static bool isHex(const std::string& s) {
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return !s.empty();
}

std::string Uid::hex() const {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llX%016llX", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
    return buf;
}

bool Uid::parse(const std::string& s, Uid& out) {
    if (s.size() != 32 || !isHex(s)) {
        return false;
    }
    out.hi = std::strtoull(s.substr(0, 16).c_str(), nullptr, 16);
    out.lo = std::strtoull(s.substr(16, 16).c_str(), nullptr, 16);
    return true;
}

std::string titleHex(TitleId id) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(id));
    return buf;
}

bool parseTitleId(const std::string& str, TitleId& out) {
    std::string s = str;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s = s.substr(2);
    }
    if (s.size() != 16 || !isHex(s)) {
        return false;
    }
    out = std::strtoull(s.c_str(), nullptr, 16);
    return true;
}

}  // namespace lib

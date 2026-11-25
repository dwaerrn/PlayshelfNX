#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lib {

using TitleId = uint64_t;

// Profile id (same 128 bits as AccountUid)
struct Uid {
    uint64_t hi = 0;
    uint64_t lo = 0;

    bool valid() const { return hi != 0 || lo != 0; }
    bool operator==(const Uid& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const Uid& o) const { return !(*this == o); }
    bool operator<(const Uid& o) const { return hi != o.hi ? hi < o.hi : lo < o.lo; }

    std::string hex() const;                        // 32 hex digits
    static bool parse(const std::string&, Uid&);
};

std::string titleHex(TitleId id);                   // 16 hex digits
bool parseTitleId(const std::string&, TitleId&);

enum class EventKind : uint8_t {
    Launch,
    Exit,
    InFocus,
    OutFocus,
    AccountActive,
    AccountInactive,
};

// One entry of the system's play event log (times in seconds)
struct RawEvent {
    EventKind kind = EventKind::Launch;
    TitleId title = 0;      // 0 for account events
    Uid user;               // only for account events
    int64_t clock = 0;      // user clock (POSIX seconds)
    int64_t steady = 0;     // steady clock (seconds, unaffected by clock changes)
};

struct Profile {
    Uid uid;
    std::string name;
    std::vector<uint8_t> avatar;    // JPEG, may be empty (not stored in dumps)
};

struct TitleInfo {
    TitleId id = 0;
    std::string name;
    bool installed = true;
    std::vector<uint8_t> icon;  // JPEG from the control data, may be empty
};

// Play statistics kept by the system (per profile or for the whole console)
struct SystemStats {
    int64_t playSeconds = 0;
    uint32_t launches = 0;
    int64_t first = 0;
    int64_t last = 0;
};

}  // namespace lib

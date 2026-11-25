#pragma once

#include <array>
#include <vector>

#include "lib/types.hpp"

namespace lib {

constexpr int64_t kTimeMin = 0;
constexpr int64_t kTimeMax = INT64_MAX / 4;

// A stretch of time during which the title was in focus
struct FocusInterval {
    int64_t clockIn = 0;
    int64_t clockOut = 0;
    int64_t steadySeconds = 0;  // measured on the steady clock, so clock changes don't matter
};

// One launch of a title, from its Launch event to its Exit event (or the next launch)
struct Session {
    TitleId title = 0;
    int64_t startClock = 0;
    int64_t endClock = 0;
    bool endedCleanly = false;         // has an Exit event (false after a crash/power loss)
    std::vector<Uid> users;            // profiles with an account event inside the session
    std::vector<FocusInterval> focus;

    bool hasUser(const Uid& uid) const;
};

// Splits the event log into sessions. Same boundaries NX Activity Log uses.
std::vector<Session> buildSessions(const std::vector<RawEvent>& events);

// Seconds spent in focus inside [from, to). Intervals crossing the border are clipped using the clock.
int64_t focusSeconds(const Session& s, int64_t from, int64_t to);

// Does the session overlap [from, to)?
bool overlaps(const Session& s, int64_t from, int64_t to);

struct Stats {
    int64_t playSeconds = 0;
    uint32_t launches = 0;
    int64_t first = 0;   // clock of the first launch, 0 if none
    int64_t last = 0;    // clock of the last event, 0 if none
};

Stats computeStats(const std::vector<const Session*>& sessions, int64_t from = kTimeMin, int64_t to = kTimeMax);

// Minutes played on each of the last 7 local days (oldest first, the last entry is today)
std::array<int, 7> lastSevenDays(const std::vector<const Session*>& sessions, int64_t nowClock, int tzOffsetSeconds);

// Titles that were launched but never had a profile attached in any session (typically homebrew)
std::vector<TitleId> unlinkedTitles(const std::vector<Session>& sessions);

}  // namespace lib

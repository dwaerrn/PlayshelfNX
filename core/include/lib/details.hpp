#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lib/library.hpp"
#include "lib/sessions.hpp"

namespace lib {

// A moment on the user's local calendar
struct CivilTime {
    int year = 1970;
    int month = 1;      // 1..12
    int day = 1;        // 1..31
    int hour = 0;
    int minute = 0;
    int weekday = 3;    // 0 = Monday .. 6 = Sunday
};
CivilTime toCivil(int64_t clock, int tzOffsetSeconds);
// Clock value of the first second of a local month
int64_t monthStart(int year, int month, int tzOffsetSeconds);

std::string monthName(int month);                    // "Jan" .. "Dec"
std::string weekdayName(int weekday);                // "Mon" .. "Sun"

// One launch of the game, as the details page lists it
struct SessionInfo {
    int64_t start = 0;
    int64_t end = 0;
    int64_t playSeconds = 0;    // time in focus
    bool endedCleanly = true;
};

struct MonthTotal {
    int year = 0;
    int month = 0;
    int64_t playSeconds = 0;
};

struct GameDetails {
    std::vector<SessionInfo> sessions;      // newest first
    std::vector<MonthTotal> months;         // every month from the first session to the last, oldest first
    int64_t loggedSeconds = 0;              // sum of the sessions (the entry may add system numbers and adjustments)
    int64_t longestSeconds = 0;
    int64_t averageSeconds = 0;             // per session that had any playtime
    int weekdaySessions[7] = {0, 0, 0, 0, 0, 0, 0};
    int64_t weekdaySeconds[7] = {0, 0, 0, 0, 0, 0, 0};
};

// The sessions behind one library entry: the profile's own sessions for an ordinary title, every
// session of every member for claimed (homebrew) groups. Months use the input's time zone.
GameDetails buildDetails(const LibraryInput& in, const LibraryEntry& entry);

}  // namespace lib

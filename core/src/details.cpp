#include "lib/details.hpp"

#include <algorithm>

namespace lib {
namespace {

constexpr int64_t kDay = 86400;

int64_t floorDiv(int64_t a, int64_t b) {
    return (a >= 0 ? a : a - b + 1) / b;
}

// Howard Hinnant's civil date algorithms (days since 1970-01-01 <-> y/m/d)
int64_t daysFromCivil(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = floorDiv(y, 400);
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = floorDiv(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2));
}

}  // namespace

CivilTime toCivil(int64_t clock, int tz) {
    const int64_t local = clock + tz;
    const int64_t days = floorDiv(local, kDay);
    const int64_t secs = local - days * kDay;
    CivilTime c;
    civilFromDays(days, c.year, c.month, c.day);
    c.hour = static_cast<int>(secs / 3600);
    c.minute = static_cast<int>(secs % 3600 / 60);
    c.weekday = static_cast<int>(((days % 7) + 7 + 3) % 7);     // 1970-01-01 was a Thursday
    return c;
}

int64_t monthStart(int year, int month, int tz) {
    return daysFromCivil(year, month, 1) * kDay - tz;
}

std::string monthName(int month) {
    static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return month >= 1 && month <= 12 ? names[month - 1] : "?";
}

std::string weekdayName(int weekday) {
    static const char* names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return weekday >= 0 && weekday <= 6 ? names[weekday] : "?";
}

GameDetails buildDetails(const LibraryInput& in, const LibraryEntry& entry) {
    GameDetails out;
    std::vector<const Session*> mine;
    for (const Session& s : in.sessions) {
        if (std::find(entry.members.begin(), entry.members.end(), s.title) == entry.members.end()) continue;
        if (!entry.claimed && !s.hasUser(in.profile)) continue;
        mine.push_back(&s);
    }
    std::sort(mine.begin(), mine.end(), [](const Session* a, const Session* b) { return a->startClock > b->startClock; });

    int counted = 0;
    for (const Session* s : mine) {
        SessionInfo info;
        info.start = s->startClock;
        info.end = s->endClock;
        info.playSeconds = focusSeconds(*s, kTimeMin, kTimeMax);
        info.endedCleanly = s->endedCleanly;
        out.sessions.push_back(info);
        out.loggedSeconds += info.playSeconds;
        out.longestSeconds = std::max(out.longestSeconds, info.playSeconds);
        if (info.playSeconds > 0) {
            counted++;
            const int wd = toCivil(s->startClock, in.tzOffsetSeconds).weekday;
            out.weekdaySessions[wd]++;
            out.weekdaySeconds[wd] += info.playSeconds;
        }
    }
    if (counted > 0) out.averageSeconds = out.loggedSeconds / counted;
    if (mine.empty()) return out;

    // Months from the oldest session to the newest; play is split exactly at month borders
    CivilTime first = toCivil(mine.back()->startClock, in.tzOffsetSeconds);
    CivilTime last = toCivil(std::max(mine.front()->endClock, mine.front()->startClock), in.tzOffsetSeconds);
    int y = first.year, m = first.month;
    while (y < last.year || (y == last.year && m <= last.month)) {
        const int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
        const int64_t from = monthStart(y, m, in.tzOffsetSeconds), to = monthStart(ny, nm, in.tzOffsetSeconds);
        MonthTotal t;
        t.year = y;
        t.month = m;
        for (const Session* s : mine) {
            if (overlaps(*s, from, to)) t.playSeconds += focusSeconds(*s, from, to);
        }
        out.months.push_back(t);
        y = ny;
        m = nm;
    }
    return out;
}

}  // namespace lib

#include "lib/sessions.hpp"

#include <algorithm>

namespace lib {

bool Session::hasUser(const Uid& uid) const {
    return std::find(users.begin(), users.end(), uid) != users.end();
}

std::vector<Session> buildSessions(const std::vector<RawEvent>& ev) {
    std::vector<Session> out;
    size_t a = 0;
    while (a < ev.size()) {
        if (ev[a].kind != EventKind::Launch || ev[a].title == 0) {
            a++;
            continue;
        }

        Session s;
        s.title = ev[a].title;
        s.startClock = ev[a].clock;
        s.endClock = ev[a].clock;

        bool open = false;
        int64_t inClock = 0;
        int64_t inSteady = 0;
        a++;
        while (a < ev.size() && ev[a].kind != EventKind::Launch) {
            const RawEvent& e = ev[a];
            switch (e.kind) {
                case EventKind::AccountActive:
                case EventKind::AccountInactive:
                    if (e.user.valid() && !s.hasUser(e.user)) {
                        s.users.push_back(e.user);
                    }
                    break;
                case EventKind::InFocus:
                    open = true;  // a repeated InFocus restarts the interval
                    inClock = e.clock;
                    inSteady = e.steady;
                    break;
                case EventKind::OutFocus:
                    // The log can contain several OutFocus events in a row: only the first one counts
                    if (open) {
                        s.focus.push_back({inClock, e.clock, std::max<int64_t>(0, e.steady - inSteady)});
                        open = false;
                    }
                    break;
                case EventKind::Exit:
                    s.endedCleanly = true;
                    break;
                default:
                    break;
            }
            s.endClock = std::max(s.endClock, e.clock);
            a++;
            if (e.kind == EventKind::Exit) {
                break;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

int64_t focusSeconds(const Session& s, int64_t from, int64_t to) {
    int64_t total = 0;
    for (const FocusInterval& f : s.focus) {
        if (f.clockOut <= from || f.clockIn >= to) {
            continue;
        }
        if (f.clockIn >= from && f.clockOut <= to) {
            total += f.steadySeconds;
        } else {
            total += std::max<int64_t>(0, std::min(f.clockOut, to) - std::max(f.clockIn, from));
        }
    }
    return total;
}

bool overlaps(const Session& s, int64_t from, int64_t to) {
    return s.startClock < to && s.endClock >= from;
}

Stats computeStats(const std::vector<const Session*>& sessions, int64_t from, int64_t to) {
    Stats st;
    for (const Session* s : sessions) {
        if (!overlaps(*s, from, to)) {
            continue;
        }
        st.launches++;
        st.playSeconds += focusSeconds(*s, from, to);
        if (st.first == 0 || s->startClock < st.first) {
            st.first = s->startClock;
        }
        st.last = std::max(st.last, s->endClock);
    }
    return st;
}

std::array<int, 7> lastSevenDays(const std::vector<const Session*>& sessions, int64_t nowClock, int tz) {
    constexpr int64_t kDay = 86400;
    // Start of the current local day, expressed on the clock
    int64_t local = nowClock + tz;
    int64_t todayStart = (local - ((local % kDay) + kDay) % kDay) - tz;

    std::array<int, 7> minutes{};
    for (int k = 0; k < 7; k++) {
        int64_t from = todayStart - (6 - k) * kDay;
        int64_t to = from + kDay;
        int64_t secs = 0;
        for (const Session* s : sessions) {
            if (overlaps(*s, from, to)) {
                secs += focusSeconds(*s, from, to);
            }
        }
        minutes[k] = static_cast<int>(secs / 60);
    }
    return minutes;
}

std::vector<TitleId> unlinkedTitles(const std::vector<Session>& sessions) {
    std::vector<TitleId> unlinked;
    std::vector<TitleId> linked;
    for (const Session& s : sessions) {
        (s.users.empty() ? unlinked : linked).push_back(s.title);
    }

    std::vector<TitleId> result;
    for (TitleId id : unlinked) {
        if (std::find(linked.begin(), linked.end(), id) == linked.end() &&
            std::find(result.begin(), result.end(), id) == result.end()) {
            result.push_back(id);
        }
    }
    return result;
}

}  // namespace lib

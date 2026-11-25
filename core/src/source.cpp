#include "lib/source.hpp"

#include <algorithm>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <set>
#include <sstream>

#include "lib/sessions.hpp"

namespace lib {

std::optional<SystemStats> MemorySource::userStats(TitleId id, const Uid& uid) {
    auto it = userStatList.find({id, uid});
    if (it == userStatList.end()) return std::nullopt;
    return it->second;
}

std::optional<SystemStats> MemorySource::consoleStats(TitleId id) {
    auto it = consoleStatList.find(id);
    if (it == consoleStatList.end()) return std::nullopt;
    return it->second;
}

int64_t MemorySource::addSession(TitleId title, const Uid& user, int64_t startClock, const std::vector<int64_t>& focusSeconds, bool crash) {
    auto push = [&](EventKind kind, TitleId t, const Uid& u, int64_t clock) {
        RawEvent e;
        e.kind = kind;
        e.title = t;
        e.user = u;
        e.clock = clock;
        e.steady = clock - 1000000;   // steady clock runs on its own base
        eventList.push_back(e);
    };

    int64_t t = startClock;
    push(EventKind::Launch, title, Uid{}, t);
    if (user.valid()) push(EventKind::AccountActive, 0, user, t);
    for (int64_t secs : focusSeconds) {
        t += 5;
        push(EventKind::InFocus, title, Uid{}, t);
        t += secs;
        push(EventKind::OutFocus, title, Uid{}, t);
    }
    if (user.valid()) push(EventKind::AccountInactive, 0, user, t);
    if (!crash) {
        t += 1;
        push(EventKind::Exit, title, Uid{}, t);
    }
    return t;
}

void MemorySource::deriveStats() {
    userStatList.clear();
    consoleStatList.clear();
    std::set<TitleId> installed;
    for (const TitleInfo& t : titleList) {
        if (t.installed) installed.insert(t.id);
    }

    auto add = [](SystemStats& st, const Session& s) {
        st.playSeconds += focusSeconds(s, kTimeMin, kTimeMax);
        st.launches++;
        if (st.first == 0 || s.startClock < st.first) st.first = s.startClock;
        st.last = std::max(st.last, s.endClock);
    };
    for (const Session& s : buildSessions(eventList)) {
        for (const Uid& u : s.users) add(userStatList[{s.title, u}], s);
        if (installed.count(s.title)) add(consoleStatList[s.title], s);
    }
}

std::optional<TitleInfo> MemorySource::uninstalledTitle(TitleId id) {
    auto it = cachedNames.find(id);
    if (it == cachedNames.end() || it->second.empty()) return std::nullopt;
    TitleInfo t;
    t.id = id;
    t.name = it->second;
    t.installed = false;
    return t;
}

void writeDump(PlaySource& source, std::ostream& out) {
    out << "# library-dump 1\n";
    std::vector<Profile> profiles = source.profiles();
    for (const Profile& p : profiles) out << "profile|" << p.uid.hex() << "|" << p.name << "\n";

    std::vector<TitleInfo> titles = source.titles();
    std::set<TitleId> ids;
    for (const TitleInfo& t : titles) {
        out << "title|" << titleHex(t.id) << "|" << (t.installed ? 1 : 0) << "|" << t.name << "\n";
        ids.insert(t.id);
    }

    std::vector<RawEvent> events = source.events();
    for (const RawEvent& e : events) {
        if (e.title != 0) ids.insert(e.title);
    }
    std::set<TitleId> listed;
    for (const TitleInfo& t : titles) listed.insert(t.id);
    for (TitleId id : ids) {
        if (listed.count(id)) continue;
        if (auto t = source.uninstalledTitle(id)) out << "cached|" << titleHex(id) << "|" << t->name << "\n";
    }
    for (TitleId id : ids) {
        if (auto st = source.consoleStats(id)) {
            out << "stat|" << titleHex(id) << "|-|" << st->playSeconds << "|" << st->launches << "|" << st->first << "|" << st->last << "\n";
        }
        for (const Profile& p : profiles) {
            if (auto st = source.userStats(id, p.uid)) {
                out << "stat|" << titleHex(id) << "|" << p.uid.hex() << "|" << st->playSeconds << "|" << st->launches << "|" << st->first << "|" << st->last << "\n";
            }
        }
    }
    for (const RawEvent& e : events) {
        out << "event|" << static_cast<int>(e.kind) << "|" << titleHex(e.title) << "|" << (e.user.valid() ? e.user.hex() : "-") << "|" << e.clock << "|" << e.steady << "\n";
    }
}

bool readDump(std::istream& in, MemorySource& out) {
    std::string line;
    if (!std::getline(in, line) || line.rfind("# library-dump", 0) != 0) return false;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p;
        std::string cur;
        for (char c : line) {
            if (c == '|') { p.push_back(cur); cur.clear(); } else cur += c;
        }
        p.push_back(cur);

        if (p[0] == "profile" && p.size() >= 3) {
            Profile pr;
            if (Uid::parse(p[1], pr.uid)) { pr.name = p[2]; out.profileList.push_back(pr); }
        } else if (p[0] == "title" && p.size() >= 4) {
            TitleInfo t;
            if (parseTitleId(p[1], t.id)) { t.installed = p[2] == "1"; t.name = p[3]; out.titleList.push_back(t); }
        } else if (p[0] == "cached" && p.size() >= 3) {
            TitleId id;
            if (parseTitleId(p[1], id)) out.cachedNames[id] = p[2];
        } else if (p[0] == "stat" && p.size() >= 7) {
            TitleId id;
            if (!parseTitleId(p[1], id)) continue;
            SystemStats st;
            st.playSeconds = std::strtoll(p[3].c_str(), nullptr, 10);
            st.launches = static_cast<uint32_t>(std::strtoul(p[4].c_str(), nullptr, 10));
            st.first = std::strtoll(p[5].c_str(), nullptr, 10);
            st.last = std::strtoll(p[6].c_str(), nullptr, 10);
            Uid uid;
            if (p[2] == "-") out.consoleStatList[id] = st;
            else if (Uid::parse(p[2], uid)) out.userStatList[{id, uid}] = st;
        } else if (p[0] == "event" && p.size() >= 6) {
            RawEvent e;
            int kind = std::atoi(p[1].c_str());
            if (kind < 0 || kind > static_cast<int>(EventKind::AccountInactive) || !parseTitleId(p[2], e.title)) continue;
            e.kind = static_cast<EventKind>(kind);
            if (p[3] != "-") Uid::parse(p[3], e.user);
            e.clock = std::strtoll(p[4].c_str(), nullptr, 10);
            e.steady = std::strtoll(p[5].c_str(), nullptr, 10);
            out.eventList.push_back(e);
        }
    }
    return true;
}

namespace {

// Small deterministic random numbers, so the demo data never changes
struct Rng {
    uint64_t s;
    uint32_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<uint32_t>(s >> 33); }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

}  // namespace

MemorySource makeDemoSource(int64_t now) {
    MemorySource src;
    Uid alex{1, 0xA1};
    Uid sam{1, 0xB2};
    src.profileList = {{alex, "Alex", {}}, {sam, "Sam", {}}};

    struct Game { const char* name; int weight; int avgMin; bool sam; };
    static const Game games[] = {
        {"Starfall Odyssey", 9, 110, false}, {"Hollow Grove", 6, 90, false}, {"Crimson Tide Racing", 4, 45, true},
        {"Kitchen Chaos", 4, 40, true}, {"Pixel Dungeon", 5, 60, false}, {"Skyward Isles", 3, 70, false},
        {"Neon Drift", 4, 35, false}, {"Wildwood Tales", 2, 80, false}, {"Iron Harbor", 2, 55, false},
        {"Echoes of Dawn", 2, 75, false}, {"Rune Keeper", 1, 50, false}, {"Turbo Kart Cup", 3, 30, true},
        {"Moonlit Farm", 5, 40, true}, {"Void Runners", 2, 45, false}, {"Tiny Tactics", 3, 65, false},
        {"Lantern Quest", 2, 85, false}, {"Coral Reef Club", 1, 25, true}, {"Blade of Embers", 3, 95, false},
        {"Puzzle Parlor", 2, 20, true}, {"Skate Legends", 1, 30, true},
    };
    Rng rng{42};
    const int64_t day = 86400;
    const int kGames = static_cast<int>(sizeof(games) / sizeof(games[0]));

    // Titles: official IDs are 0x0100...000, two games were deleted (no title entry). The system
    // still has the name of one of them in its cache; the other one is forgotten.
    auto id = [](int i) { return static_cast<TitleId>(0x0100A00000000000ULL + static_cast<uint64_t>(i) * 0x1000); };
    for (int i = 0; i < kGames; i++) {
        if (i == 8 || i == 10) continue;
        src.titleList.push_back({id(i), games[i].name, true, {}});
    }
    src.cachedNames[id(8)] = games[8].name;

    // Homebrew without profile: an emulator forwarder that was reinstalled with a new ID, a save manager
    const TitleId emuOld = 0x05A1000000000001ULL;
    const TitleId emuNew = 0x05A1000000000002ULL;
    const TitleId saves = 0x05B2000000000001ULL;
    src.titleList.push_back({emuNew, "Homebrew Emulator", true, {}});
    src.titleList.push_back({saves, "Save Manager", true, {}});
    src.cachedNames[emuOld] = "Homebrew Emulator";      // the deleted forwarder is still in the system cache

    // Plan the sessions first, then lay them out one after another (the console never runs two titles at once)
    struct Planned { int64_t start; TitleId title; Uid user; std::vector<int64_t> focus; bool crash; };
    std::vector<Planned> plan;

    // Play history
    const int kDays = 130;
    for (int d = kDays; d >= 0; d--) {
        int64_t midnight = (now / day) * day - static_cast<int64_t>(d) * day;
        for (int i = 0; i < kGames; i++) {
            if (rng.range(0, 99) >= games[i].weight * 3) continue;
            int64_t start = midnight + rng.range(9, 22) * 3600 + rng.range(0, 59) * 60;
            if (start > now - 600) continue;
            int mins = std::max(5, games[i].avgMin + rng.range(-20, 40));
            std::vector<int64_t> focus = {static_cast<int64_t>(mins) * 60 / 2, static_cast<int64_t>(mins) * 60 / 2};
            bool crash = rng.range(0, 40) == 0;
            // Games with a profile; some are played by Sam
            Uid user = (games[i].sam && rng.range(0, 1)) ? sam : alex;
            plan.push_back({start, id(i), user, focus, crash});
        }
        // Homebrew: never has an account event
        if (rng.range(0, 99) < 35) {
            int64_t start = midnight + rng.range(10, 23) * 3600;
            if (start < now - 600) {
                TitleId which = (d > 60) ? emuOld : emuNew;     // the forwarder was reinstalled two months ago
                plan.push_back({start, which, Uid{}, {static_cast<int64_t>(rng.range(20, 150)) * 60}, false});
            }
        }
        if (rng.range(0, 99) < 12) {
            int64_t start = midnight + rng.range(10, 23) * 3600;
            if (start < now - 600) plan.push_back({start, saves, Uid{}, {static_cast<int64_t>(rng.range(2, 12)) * 60}, false});
        }
    }
    std::stable_sort(plan.begin(), plan.end(), [](const Planned& a, const Planned& b) { return a.start < b.start; });
    int64_t free = 0;
    for (const Planned& p : plan) {
        int64_t start = std::max(p.start, free + 60);
        if (start > now - 60) break;
        free = src.addSession(p.title, p.user, start, p.focus, p.crash);
    }
    src.deriveStats();
    return src;
}

}  // namespace lib

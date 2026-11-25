#include "lib/library.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace lib {
namespace {

void mergeSystem(LibraryEntry& e, const SystemStats& st) {
    // The system's numbers fill gaps (events the log no longer has) but never lower the history.
    // A gap shows as the system counting more launches than the log. Without one, the log's
    // playtime is used as it is (the system's own total runs a few minutes higher; NX Activity
    // Log and this app both count focus time from the events).
    if (st.launches > e.launches) {
        e.playSeconds = std::max(e.playSeconds, st.playSeconds);
        e.launches = st.launches;
    }
    if (st.first != 0 && (e.first == 0 || st.first < e.first)) e.first = st.first;
    e.last = std::max(e.last, st.last);
}

}  // namespace

std::vector<LibraryEntry> buildLibrary(const LibraryInput& in, SortBy sort) {
    std::map<TitleId, std::vector<const Session*>> byTitle;
    for (const Session& s : in.sessions) byTitle[s.title].push_back(&s);

    std::map<TitleId, const TitleInfo*> info;
    for (const TitleInfo& t : in.titles) info[t.id] = &t;

    const std::vector<TitleId> claimed = in.claims.visible(in.profile);
    auto isClaimed = [&](TitleId id) { return std::find(claimed.begin(), claimed.end(), id) != claimed.end(); };

    auto nameOf = [&](TitleId id, const std::string& fallback) {
        auto it = info.find(id);
        if (it != info.end() && !it->second->name.empty()) return it->second->name;
        if (in.names) {
            std::string n = in.names->name(id);
            if (!n.empty()) return n;
        }
        return fallback.empty() ? unknownTitleName(id) : fallback;
    };

    std::vector<LibraryEntry> out;

    // Titles with a profile: only the sessions that have this profile attached
    for (const auto& kv : byTitle) {
        if (isClaimed(kv.first)) continue;
        std::vector<const Session*> mine;
        for (const Session* s : kv.second) {
            if (s->hasUser(in.profile)) mine.push_back(s);
        }
        if (mine.empty()) continue;

        LibraryEntry e;
        e.id = kv.first;
        e.name = nameOf(kv.first, "");
        e.installed = info.count(kv.first) > 0 && info[kv.first]->installed;
        e.members = {kv.first};
        Stats st = computeStats(mine);
        e.playSeconds = st.playSeconds;
        e.launches = st.launches;
        e.first = st.first;
        e.last = st.last;
        e.week = lastSevenDays(mine, in.now, in.tzOffsetSeconds);
        if (in.source) {
            if (auto sys = in.source->userStats(kv.first, in.profile)) mergeSystem(e, *sys);
        }
        out.push_back(std::move(e));
    }

    // Claimed titles: every session counts, merged titles are one game
    for (TitleId id : claimed) {
        if (in.claims.isAlias(id, in.profile)) continue;
        std::vector<TitleId> members = in.claims.group(id, in.profile);

        std::vector<const Session*> all;
        SystemStats sys;
        bool anyInstalled = false;
        for (TitleId m : members) {
            auto it = byTitle.find(m);
            if (it != byTitle.end()) all.insert(all.end(), it->second.begin(), it->second.end());
            if (info.count(m) && info[m]->installed) anyInstalled = true;

            // The saved snapshot, summed over the group. No live call here: the console-wide query
            // costs ~110 ms on the Switch. refreshClaimSnapshots() brings the snapshots up to date
            // (once per start, and for new claims), and a claimed title can't be played while the app runs.
            SystemStats one;
            for (const ClaimEntry& c : in.claims.entries()) {
                if (c.id == m && c.hasSnapshot) one = c.snapshot;
            }
            sys.playSeconds += one.playSeconds;
            sys.launches += one.launches;
            if (one.first != 0 && (sys.first == 0 || one.first < sys.first)) sys.first = one.first;
            sys.last = std::max(sys.last, one.last);
        }

        LibraryEntry e;
        e.id = id;
        std::string claimName;
        for (const ClaimEntry& c : in.claims.entries()) {
            if (c.id == id) claimName = c.name;
        }
        e.name = nameOf(id, claimName);
        e.installed = anyInstalled;
        e.claimed = true;
        e.members = members;
        Stats st = computeStats(all);
        e.playSeconds = st.playSeconds;
        e.launches = st.launches;
        e.first = st.first;
        e.last = st.last;
        e.week = lastSevenDays(all, in.now, in.tzOffsetSeconds);
        mergeSystem(e, sys);
        out.push_back(std::move(e));
    }

    out.erase(std::remove_if(out.begin(), out.end(), [](const LibraryEntry& e) { return e.playSeconds == 0 && e.launches == 0; }), out.end());

    // Manual corrections. For merged homebrew every member counts, so a correction survives the
    // group getting a different (older) title shown.
    if (in.adjustments) {
        for (LibraryEntry& e : out) {
            for (TitleId m : e.members) e.adjustSeconds += in.adjustments->get(m, in.profile);
            e.playSeconds = std::max<int64_t>(0, e.playSeconds + e.adjustSeconds);
        }
    }

    std::stable_sort(out.begin(), out.end(), [sort](const LibraryEntry& a, const LibraryEntry& b) {
        switch (sort) {
            case SortBy::LastPlayed: return a.last > b.last;
            case SortBy::Launches: return a.launches > b.launches;
            case SortBy::Name: return normalizeName(a.name) < normalizeName(b.name);
            default: return a.playSeconds > b.playSeconds;
        }
    });
    return out;
}

std::vector<TitleId> suggestNewTitles(const std::vector<Session>& sessions, const std::vector<TitleInfo>& titles,
                                      const ClaimStore& claims, const Uid& profile) {
    std::vector<TitleId> result;
    for (TitleId id : unlinkedTitles(sessions)) {
        bool installed = std::any_of(titles.begin(), titles.end(), [id](const TitleInfo& t) { return t.id == id && t.installed; });
        if (!installed || claims.isClaimedByAnyone(id) || claims.isIgnored(id, profile)) continue;
        result.push_back(id);
    }
    return result;
}

std::vector<ClaimCandidate> claimCandidates(const std::vector<Session>& sessions, const std::vector<TitleInfo>& titles,
                                            const ClaimStore& claims, const NameCache* names, const Uid& profile) {
    std::map<TitleId, int64_t> played;
    for (const Session& s : sessions) played[s.title] += focusSeconds(s, kTimeMin, kTimeMax);

    auto installedInfo = [&](TitleId id) -> const TitleInfo* {
        for (const TitleInfo& t : titles) {
            if (t.id == id && t.installed) return &t;
        }
        return nullptr;
    };

    std::vector<ClaimCandidate> out;
    std::set<TitleId> seen;
    auto add = [&](TitleId id, const ClaimEntry* entry) {
        if (!seen.insert(id).second) return;
        ClaimCandidate c;
        c.id = id;
        const TitleInfo* info = installedInfo(id);
        c.installed = info != nullptr;
        if (info) c.name = info->name;
        if (c.name.empty() && entry) c.name = entry->name;
        if (c.name.empty() && names) c.name = names->name(id);
        if (c.name.empty()) {
            if (!c.installed && !entry) return;     // nothing to show the user
            c.name = unknownTitleName(id);
        }
        c.claimed = entry != nullptr;
        auto it = played.find(id);
        if (it != played.end()) c.playSeconds = it->second;
        if (entry && entry->hasSnapshot) c.playSeconds = std::max(c.playSeconds, entry->snapshot.playSeconds);
        out.push_back(std::move(c));
    };

    for (const ClaimEntry& e : claims.entries()) {
        if (claims.isClaimed(e.id, profile)) add(e.id, &e);
    }
    for (TitleId id : unlinkedTitles(sessions)) add(id, nullptr);

    std::sort(out.begin(), out.end(), [](const ClaimCandidate& a, const ClaimCandidate& b) {
        std::string x = normalizeName(a.name), y = normalizeName(b.name);
        return x != y ? x < y : a.id < b.id;
    });
    return out;
}

bool refreshClaimSnapshots(ClaimStore& claims, const std::vector<TitleInfo>& titles, PlaySource& source, bool onlyMissing) {
    bool changed = false;
    // Copy the entries first: updateSnapshot changes them
    std::vector<ClaimEntry> entries = claims.entries();
    for (const ClaimEntry& c : entries) {
        const TitleInfo* installed = nullptr;
        for (const TitleInfo& t : titles) {
            if (t.id == c.id && t.installed) installed = &t;
        }
        if (onlyMissing) {
            if (c.hasSnapshot) continue;
        } else if (!installed) {
            continue;
        }
        if (claims.updateSnapshot(c.id, installed ? installed->name : std::string(), source.consoleStats(c.id))) changed = true;
    }
    return changed;
}

size_t learnUninstalledNames(NameCache& names, const std::vector<RawEvent>& events, const std::vector<TitleInfo>& titles, PlaySource& source) {
    std::set<TitleId> listed;
    for (const TitleInfo& t : titles) {
        if (t.installed && !t.name.empty()) listed.insert(t.id);
    }
    std::set<TitleId> asked;
    std::vector<TitleInfo> found;
    for (const RawEvent& e : events) {
        if (e.title == 0 || listed.count(e.title) || !asked.insert(e.title).second || !names.name(e.title).empty()) continue;
        if (auto t = source.uninstalledTitle(e.title)) {
            if (!t->name.empty()) found.push_back(*t);
        }
    }
    names.update(found);
    return found.size();
}

std::string unknownTitleName(TitleId id) {
    return "Unknown title (" + titleHex(id) + ")";
}

Uid chooseProfile(const std::vector<Profile>& profiles, const Uid& preselected, const Uid& remembered) {
    for (const Uid& want : {preselected, remembered}) {
        if (!want.valid()) continue;
        for (const Profile& p : profiles) {
            if (p.uid == want) return p.uid;
        }
    }
    return profiles.empty() ? Uid{} : profiles.front().uid;
}

}  // namespace lib

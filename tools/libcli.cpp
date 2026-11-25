// Prints the library of a profile from a recorded dump or from the demo data.
//   libcli --demo [--write-dump file]
//   libcli dump.txt [--profile NAME|INDEX] [--sort playtime|last|name|launches] [--auto-claim] [--history file]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "lib/library.hpp"

using namespace lib;

static std::string playtime(int64_t secs) {
    char buf[40];
    if (secs >= 3600) std::snprintf(buf, sizeof(buf), "%lldh %02lldm", static_cast<long long>(secs / 3600), static_cast<long long>(secs % 3600 / 60));
    else std::snprintf(buf, sizeof(buf), "%lldm", static_cast<long long>(secs / 60));
    return buf;
}

static std::string ago(int64_t now, int64_t t) {
    if (t == 0) return "-";
    int64_t days = (now - t) / 86400;
    if (days <= 0) return "today";
    if (days == 1) return "yesterday";
    if (days < 60) return std::to_string(days) + " days ago";
    return std::to_string(days / 30) + " months ago";
}

int main(int argc, char** argv) {
    std::string input;
    std::string profileArg;
    std::string writeDumpPath;
    SortBy sort = SortBy::Playtime;
    bool demo = false;
    bool autoClaim = false;
    int64_t now = static_cast<int64_t>(std::time(nullptr));

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "--auto-claim") autoClaim = true;
        else if (a == "--profile" && i + 1 < argc) profileArg = argv[++i];
        else if (a == "--write-dump" && i + 1 < argc) writeDumpPath = argv[++i];
        else if (a == "--now" && i + 1 < argc) now = std::atoll(argv[++i]);
        else if (a == "--sort" && i + 1 < argc) {
            std::string v = argv[++i];
            sort = v == "last" ? SortBy::LastPlayed : v == "name" ? SortBy::Name : v == "launches" ? SortBy::Launches : SortBy::Playtime;
        } else if (a[0] != '-') input = a;
    }

    MemorySource src;
    if (demo) {
        src = makeDemoSource(now);
    } else {
        std::ifstream f(input);
        if (!f || !readDump(f, src)) {
            std::fprintf(stderr, "usage: libcli --demo | libcli <dump file> [--profile N] [--sort ...] [--auto-claim]\n");
            return 1;
        }
    }
    if (!writeDumpPath.empty()) {
        std::ofstream out(writeDumpPath);
        writeDump(src, out);
        std::printf("dump written to %s\n", writeDumpPath.c_str());
    }

    std::vector<Profile> profiles = src.profiles();
    if (profiles.empty()) { std::fprintf(stderr, "no profiles\n"); return 1; }
    Profile profile = profiles[0];
    for (size_t i = 0; i < profiles.size(); i++) {
        if (profileArg == profiles[i].name || profileArg == std::to_string(i)) profile = profiles[i];
    }

    std::vector<RawEvent> events = src.events();
    std::vector<Session> sessions = buildSessions(events);
    std::vector<TitleInfo> titles = src.titles();

    ClaimStore claims;
    if (autoClaim) {
        for (TitleId id : suggestNewTitles(sessions, titles, claims, profile.uid)) {
            std::string name;
            for (const TitleInfo& t : titles) if (t.id == id) name = t.name;
            std::printf("claiming new title %s (%s)\n", titleHex(id).c_str(), name.c_str());
            claims.add(id, name, profile.uid);
        }
        // Reinstalled forwarders that were deleted: claim them by name so they merge
        for (const Session& s : sessions) {
            if (s.users.empty() && !claims.isClaimedByAnyone(s.title)) {
                bool installed = false;
                for (const TitleInfo& t : titles) if (t.id == s.title) installed = true;
                if (!installed) claims.add(s.title, "Homebrew Emulator", profile.uid);
            }
        }
        refreshClaimSnapshots(claims, titles, src);
    }

    NameCache names;
    names.update(titles);
    size_t learned = learnUninstalledNames(names, events, titles, src);
    if (learned) std::printf("%zu names of uninstalled titles from the system cache\n", learned);

    LibraryInput in{sessions, titles, claims, &names, &src, profile.uid, now, 0};
    std::vector<LibraryEntry> library = buildLibrary(in, sort);

    std::printf("\nLibrary of %s: %zu titles, %zu sessions, %zu events\n\n", profile.name.c_str(), library.size(), sessions.size(), events.size());
    std::printf("%3s  %-26s %10s %8s  %-14s %s\n", "#", "Title", "Playtime", "Launches", "Last played", "Last 7 days (min)");
    int rank = 1;
    for (const LibraryEntry& e : library) {
        std::string flags;
        if (e.claimed) flags += " [homebrew";
        if (e.members.size() > 1) flags += ", merged x" + std::to_string(e.members.size());
        if (!flags.empty()) flags += "]";
        if (!e.installed) flags += " (deleted)";
        std::printf("%3d  %-26.26s %10s %8u  %-14s", rank++, e.name.c_str(), playtime(e.playSeconds).c_str(), e.launches, ago(now, e.last).c_str());
        for (int m : e.week) std::printf(" %3d", m);
        std::printf("%s\n", flags.c_str());
    }
    return 0;
}

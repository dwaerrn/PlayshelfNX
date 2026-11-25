#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <vector>

#include "lib/details.hpp"
#include "lib/inflate.hpp"
#include "lib/griddb.hpp"
#include "lib/library.hpp"
#include "lib/nacp.hpp"

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

using namespace lib;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const Uid A{1, 0xA1};
static const Uid B{1, 0xB2};
static const int64_t NOW = 1800000000;   // fixed "now" for all tests

// ---------- helpers ----------
static RawEvent ev(EventKind k, TitleId t, int64_t clock, int64_t steady, Uid u = {}) {
    RawEvent e; e.kind = k; e.title = t; e.clock = clock; e.steady = steady; e.user = u; return e;
}

// ---------- sessions ----------
static void test_sessions_basic() {
    std::vector<RawEvent> e = {
        ev(EventKind::Launch, 0x100, 1000, 10), ev(EventKind::AccountActive, 0, 1000, 10, A),
        ev(EventKind::InFocus, 0x100, 1010, 20), ev(EventKind::OutFocus, 0x100, 1110, 120),
        ev(EventKind::OutFocus, 0x100, 1110, 120),                    // repeated OutFocus counts once
        ev(EventKind::AccountInactive, 0, 1111, 121, A), ev(EventKind::Exit, 0x100, 1111, 121),
        // second launch crashes (no Exit), the third launch starts right after
        ev(EventKind::Launch, 0x200, 2000, 500), ev(EventKind::InFocus, 0x200, 2005, 505), ev(EventKind::OutFocus, 0x200, 2065, 565),
        ev(EventKind::Launch, 0x300, 3000, 900), ev(EventKind::InFocus, 0x300, 3002, 902),   // still running at the end of the log
    };
    std::vector<Session> s = buildSessions(e);
    CHECK(s.size() == 3);
    CHECK(s[0].title == 0x100 && s[0].endedCleanly && s[0].hasUser(A) && !s[0].hasUser(B));
    CHECK(s[0].focus.size() == 1 && focusSeconds(s[0], kTimeMin, kTimeMax) == 100);
    CHECK(s[1].title == 0x200 && !s[1].endedCleanly && s[1].users.empty() && focusSeconds(s[1], kTimeMin, kTimeMax) == 60);
    CHECK(s[2].focus.empty() && focusSeconds(s[2], kTimeMin, kTimeMax) == 0);   // focus never closed: nothing counted
    CHECK(s[0].startClock == 1000 && s[0].endClock == 1111);
}

static void test_clock_change_uses_steady_time() {
    // The user changed the clock by +10000 s in the middle of the session: steady time is right
    std::vector<RawEvent> e = {
        ev(EventKind::Launch, 0x100, 1000, 10), ev(EventKind::InFocus, 0x100, 1000, 10),
        ev(EventKind::OutFocus, 0x100, 11600, 610), ev(EventKind::Exit, 0x100, 11601, 611),
    };
    std::vector<Session> s = buildSessions(e);
    CHECK(s.size() == 1 && focusSeconds(s[0], kTimeMin, kTimeMax) == 600);
}

static void test_range_clipping_and_days() {
    MemorySource src;
    const int64_t day = 86400;
    int64_t base = (NOW / day) * day;             // midnight UTC today
    // Played 23:30 -> 00:30 across midnight (1 h), five days ago and today at 12:00 for 30 min
    int64_t start = base - 5 * day + 23 * 3600 + 1800;
    src.addSession(0x100, A, start - 5, {3600});
    src.addSession(0x100, A, base + 12 * 3600, {1800});
    std::vector<Session> s = buildSessions(src.eventList);
    std::vector<const Session*> list = {&s[0], &s[1]};

    // Seven-day chart in UTC: day index 6 = today, index 1 = 5 days ago, index 2 = 4 days ago
    std::array<int, 7> w = lastSevenDays(list, NOW, 0);
    CHECK(w[6] == 30);
    CHECK(w[1] + w[2] >= 58 && w[1] + w[2] <= 60);      // the hour is split over midnight
    CHECK(w[1] > 20 && w[2] > 20);
    // In UTC+5 the same session lands completely on one day
    std::array<int, 7> w5 = lastSevenDays(list, NOW, 5 * 3600);
    CHECK(w5[1] + w5[2] >= 58 && w5[1] + w5[2] <= 60);
    // Clipping
    Stats all = computeStats(list);
    CHECK(all.launches == 2 && all.playSeconds == 3600 + 1800);
    Stats recent = computeStats(list, base, kTimeMax);
    CHECK(recent.launches == 1 && recent.playSeconds == 1800);
    CHECK(computeStats(list, 0, 100).launches == 0);
    CHECK(all.first == s[0].startClock && all.last == s[1].endClock);
}

static void test_unlinked() {
    MemorySource src;
    src.addSession(0x100, Uid{}, 1000, {60});          // homebrew
    src.addSession(0x100, Uid{}, 2000, {60});
    src.addSession(0x200, A, 3000, {60});              // official
    src.addSession(0x300, Uid{}, 4000, {60});          // launched once with a profile -> not homebrew-like
    src.addSession(0x300, A, 5000, {60});
    src.addSession(0x400, Uid{}, 6000, {60}, true);    // crashed homebrew
    std::vector<TitleId> u = unlinkedTitles(buildSessions(src.eventList));
    CHECK(u.size() == 2 && std::count(u.begin(), u.end(), 0x100) == 1 && std::count(u.begin(), u.end(), 0x400) == 1);
}

// ---------- claims ----------
static void test_claims() {
    ClaimStore c;
    c.add(0x1, "My Game", A);
    c.add(0x2, "  my   GAME ", A);          // same name -> alias of 0x1 for A
    c.add(0x3, "Solo", A);
    c.add(0x4, "My Game", B);
    CHECK(c.isClaimed(0x1, A) && !c.isClaimed(0x1, B) && c.isClaimedByAnyone(0x4));
    CHECK(!c.isAlias(0x1, A) && c.isAlias(0x2, A) && !c.isAlias(0x3, A));
    CHECK(!c.isAlias(0x4, B) && !c.isAlias(0x2, B));
    std::vector<TitleId> g = c.group(0x1, A);
    CHECK(g.size() == 2 && g[0] == 0x1 && g[1] == 0x2);
    CHECK(c.group(0x2, A).empty() && c.group(0x1, B).empty() && c.group(0x4, B).size() == 1);
    CHECK(c.visible(A).size() == 3 && c.visible(B).size() == 1);

    // B also claims 0x1: for B the oldest is now 0x1 and 0x4 becomes the alias
    c.add(0x1, "My Game", B);
    CHECK(c.isAlias(0x4, B) && c.group(0x1, B).size() == 2);
    // A unclaims the oldest, 0x2 takes over
    c.remove(0x1, A);
    CHECK(!c.isAlias(0x2, A) && c.group(0x2, A).size() == 1 && c.isClaimed(0x1, B));
    // Last profile gone -> entry gone
    c.remove(0x1, B);
    CHECK(!c.isClaimedByAnyone(0x1));

    c.ignore(0x9, A); c.ignore(0x9, A);
    CHECK(c.isIgnored(0x9, A) && !c.isIgnored(0x9, B));
}

static void test_claim_snapshot_and_serialization() {
    ClaimStore c;
    c.add(0x1, "Game | with, separators", A);
    SystemStats live; live.playSeconds = 5000; live.launches = 3; live.first = 100; live.last = 900;
    CHECK(c.updateSnapshot(0x1, "", live));
    SystemStats lower; lower.playSeconds = 10; lower.launches = 1; lower.first = 500; lower.last = 600;
    CHECK(!c.updateSnapshot(0x1, "", lower));                   // never lowers anything
    SystemStats more; more.playSeconds = 9000; more.launches = 4; more.first = 50; more.last = 1500;
    CHECK(c.updateSnapshot(0x1, "", more));
    c.ignore(0x7, B);

    ClaimStore r = ClaimStore::parse(c.serialize());
    CHECK(r.entries().size() == 1 && r.isClaimed(0x1, A) && r.isIgnored(0x7, B));
    CHECK(r.entries()[0].hasSnapshot && r.entries()[0].snapshot.playSeconds == 9000 && r.entries()[0].snapshot.launches == 4);
    CHECK(r.entries()[0].snapshot.first == 50 && r.entries()[0].snapshot.last == 1500);
    CHECK(r.entries()[0].name == "Game   with  separators");     // separators can't corrupt the file
    CHECK(r.serialize() == c.serialize());
}

// ---------- history ----------
static void test_history() {
    MemorySource src;
    src.addSession(0x100, A, 1000, {60});
    src.addSession(0x100, A, 2000, {60});
    HistoryStore h;
    CHECK(h.merge(src.eventList) == src.eventList.size());
    CHECK(h.merge(src.eventList) == 0);                          // merging again adds nothing

    // The system forgets the oldest session and records a new one: the store keeps all three
    std::vector<RawEvent> later(src.eventList.begin() + src.eventList.size() / 2, src.eventList.end());
    MemorySource more;
    more.addSession(0x100, A, 3000, {60});
    later.insert(later.end(), more.eventList.begin(), more.eventList.end());
    size_t added = h.merge(later);
    CHECK(added == more.eventList.size());
    CHECK(buildSessions(h.events()).size() == 3);

    // Legitimately repeated events (same timestamps) are kept
    std::vector<RawEvent> dup = {ev(EventKind::OutFocus, 0x5, 10, 10), ev(EventKind::OutFocus, 0x5, 10, 10)};
    HistoryStore d;
    CHECK(d.merge(dup) == 2 && d.merge(dup) == 0);

    HistoryStore r = HistoryStore::parse(h.serialize());
    CHECK(r.events().size() == h.events().size() && r.serialize() == h.serialize());
}

static void test_name_cache() {
    NameCache n;
    n.update({{0x1, "Game | One", true, {}}, {0x2, "", true, {}}});
    CHECK(n.name(0x1) == "Game   One" && n.name(0x2).empty());
    n.update({{0x1, "Renamed", true, {}}});
    NameCache r = NameCache::parse(n.serialize());
    CHECK(r.name(0x1) == "Renamed" && r.name(0x9).empty());
}

// ---------- NACP ----------
static void putName(std::vector<uint8_t>& block, int index, const char* name) {
    std::memcpy(&block[index * 0x300], name, std::strlen(name));
}

static void test_nacp_classic() {
    std::vector<uint8_t> n(nacp::kNacpSize, 0);
    putName(n, 0, "English Name");
    putName(n, 2, "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80");           // Japanese
    CHECK(nacp::extractName(n.data(), n.size(), 0) == "English Name");
    CHECK(nacp::extractName(n.data(), n.size(), 2) == "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80");
    CHECK(nacp::extractName(n.data(), n.size(), 5) == "English Name");  // empty language -> first usable
    // invalid UTF-8 and control characters are skipped, never returned
    std::vector<uint8_t> bad(nacp::kNacpSize, 0);
    putName(bad, 0, "Bad\xFF\xFEname");
    putName(bad, 1, "Line\nBreak");
    putName(bad, 3, "Good Fallback");
    CHECK(nacp::extractName(bad.data(), bad.size(), 0) == "Good Fallback");
    // a name without terminator stays inside its field
    std::vector<uint8_t> full(nacp::kNacpSize, 0);
    std::memset(&full[0], 'A', 0x200);
    putName(full, 1, "Next");
    CHECK(nacp::extractName(full.data(), full.size(), 0) == std::string(0x200, 'A'));
    CHECK(nacp::extractName(n.data(), 100, 0).empty());                 // too small
    std::vector<uint8_t> empty(nacp::kNacpSize, 0);
    CHECK(nacp::extractName(empty.data(), empty.size(), 0).empty());
}

#ifdef HAVE_ZLIB
static std::vector<uint8_t> rawDeflate(const std::vector<uint8_t>& in) {
    z_stream zs; std::memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(deflateBound(&zs, in.size()) + 64);
    zs.next_in = const_cast<uint8_t*>(in.data()); zs.avail_in = static_cast<uInt>(in.size());
    zs.next_out = out.data(); zs.avail_out = static_cast<uInt>(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

static std::vector<uint8_t> compressedNacp(const std::vector<uint8_t>& titles) {
    std::vector<uint8_t> comp = rawDeflate(titles);
    std::vector<uint8_t> n(nacp::kNacpSize, 0);
    n[0] = comp.size() & 0xFF; n[1] = comp.size() >> 8;
    std::memcpy(&n[2], comp.data(), comp.size());
    n[0x3215] = 1;
    return n;
}

static void test_nacp_compressed() {
    std::vector<uint8_t> titles(32 * 0x300, 0);
    putName(titles, 0, "Super Mario Bros. Wonder");
    putName(titles, 2, "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80");
    std::vector<uint8_t> n = compressedNacp(titles);
    CHECK(nacp::extractName(n.data(), n.size(), 0) == "Super Mario Bros. Wonder");
    CHECK(nacp::extractName(n.data(), n.size(), 2) == "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80");

    // A name that only exists in a language slot added with the 32-language format
    std::vector<uint8_t> late(32 * 0x300, 0);
    putName(late, 20, "Only In New Slot");
    std::vector<uint8_t> n2 = compressedNacp(late);
    CHECK(nacp::extractName(n2.data(), n2.size(), 0) == "Only In New Slot");
    CHECK(nacp::extractName(n2.data(), n2.size(), 20) == "Only In New Slot");

    // Damaged blocks give no name instead of garbage
    std::vector<uint8_t> bad = n; bad[0] = 0; bad[1] = 0;
    CHECK(nacp::extractName(bad.data(), bad.size(), 0).empty());
    std::vector<uint8_t> big = n; big[0] = 0xFF; big[1] = 0x7F;
    CHECK(nacp::extractName(big.data(), big.size(), 0).empty());
    std::vector<uint8_t> garbage(nacp::kNacpSize, 0);
    for (int i = 2; i < 300; i++) garbage[i] = static_cast<uint8_t>(i * 37 + 11);
    garbage[0] = 250; garbage[1] = 0; garbage[0x3215] = 1;
    CHECK(nacp::extractName(garbage.data(), garbage.size(), 0).empty());
}
#endif

static void test_inflate_stress() {
#ifdef HAVE_ZLIB
    for (int seed = 0; seed < 40; seed++) {
        std::vector<uint8_t> in(1 + seed * 311);
        uint32_t x = 12345 + seed;
        for (auto& b : in) { x = x * 1103515245 + 12345; b = (seed % 3 == 0) ? static_cast<uint8_t>(x >> 16) : static_cast<uint8_t>((x >> 16) % 7); }
        std::vector<uint8_t> comp = rawDeflate(in);
        std::vector<uint8_t> out(in.size() + 16);
        size_t len = 0;
        CHECK(inflate::raw(comp.data(), comp.size(), out.data(), out.size(), &len) && len == in.size() && std::memcmp(in.data(), out.data(), len) == 0);
        // damaged input must fail cleanly or produce something, never crash
        for (size_t i = 0; i < comp.size() && i < 50; i++) { auto c = comp; c[i] ^= 0x5A; inflate::raw(c.data(), c.size(), out.data(), out.size(), &len); }
    }
#endif
    CHECK(true);
}

// ---------- dump + library ----------
static std::vector<LibraryEntry> libraryOf(MemorySource& src, const ClaimStore& claims, const Uid& who, SortBy sort = SortBy::Playtime) {
    std::vector<Session> sessions = buildSessions(src.events());
    std::vector<TitleInfo> titles = src.titles();
    NameCache names; names.update(titles);
    LibraryInput in{sessions, titles, claims, &names, &src, who, NOW, 0};
    return buildLibrary(in, sort);
}

static void test_dump_roundtrip() {
    MemorySource demo = makeDemoSource(NOW);
    std::stringstream ss;
    writeDump(demo, ss);
    MemorySource back;
    CHECK(readDump(ss, back));
    CHECK(back.eventList.size() == demo.eventList.size() && back.profileList.size() == 2 && back.titleList.size() == demo.titleList.size());
    CHECK(back.userStatList.size() == demo.userStatList.size() && back.consoleStatList.size() == demo.consoleStatList.size());
    ClaimStore none;
    std::vector<LibraryEntry> a = libraryOf(demo, none, A), b = libraryOf(back, none, A);
    CHECK(a.size() == b.size() && !a.empty());
    for (size_t i = 0; i < a.size() && i < b.size(); i++) CHECK(a[i].id == b[i].id && a[i].playSeconds == b[i].playSeconds);
    std::stringstream bad("not a dump");
    MemorySource x;
    CHECK(!readDump(bad, x));
    CHECK(back.cachedNames == demo.cachedNames && back.cachedNames.size() == 2);   // the system cache's answers are recorded
}

static void test_uninstalled_names() {
    MemorySource src = makeDemoSource(NOW);
    std::vector<TitleInfo> titles = src.titles();
    NameCache names; names.update(titles);
    const TitleId known = 0x0100A00000008000ULL, forgotten = 0x0100A0000000A000ULL;
    CHECK(names.name(known).empty() && names.name(forgotten).empty());
    // The system cache still knows one deleted game and the deleted forwarder
    CHECK(learnUninstalledNames(names, src.events(), titles, src) == 2);
    CHECK(names.name(0x05A1000000000001ULL) == "Homebrew Emulator");
    CHECK(!names.name(known).empty() && names.name(known) == src.cachedNames[known] && names.name(forgotten).empty());
    CHECK(learnUninstalledNames(names, src.events(), titles, src) == 0);   // already known: not asked again
    // The library shows the learned name; the forgotten title is shown as unknown, not hidden
    std::vector<Session> sessions = buildSessions(src.events());
    ClaimStore claims;
    LibraryInput in{sessions, titles, claims, &names, &src, A, NOW, 0};
    bool sawKnown = false, sawForgotten = false;
    for (const LibraryEntry& e : buildLibrary(in, SortBy::Playtime)) {
        if (e.id == known) sawKnown = e.name == src.cachedNames[known] && !e.installed;
        if (e.id == forgotten) sawForgotten = e.name == "Unknown title (0100A0000000A000)" && !e.installed;
    }
    CHECK(sawKnown && sawForgotten);
    // Installed titles are never looked up; a source without a cache learns nothing
    struct NoCache : MemorySource { std::optional<TitleInfo> uninstalledTitle(TitleId) override { return std::nullopt; } } plain;
    plain.eventList = src.eventList;
    NameCache empty;
    CHECK(learnUninstalledNames(empty, plain.events(), titles, plain) == 0 && empty.name(known).empty());
}

static void test_library_demo() {
    MemorySource src = makeDemoSource(NOW);
    ClaimStore claims;
    std::vector<LibraryEntry> alex = libraryOf(src, claims, A);
    std::vector<LibraryEntry> sam = libraryOf(src, claims, B);
    CHECK(alex.size() >= 15 && !sam.empty() && alex.size() > sam.size());
    for (size_t i = 1; i < alex.size(); i++) CHECK(alex[i - 1].playSeconds >= alex[i].playSeconds);   // sorted by playtime
    // Without claims no homebrew shows up (it never has a profile)
    for (const LibraryEntry& e : alex) CHECK(!e.claimed && e.id < 0x0500000000000000ULL);
    // Numbers of an ordinary title agree with the system's per-profile statistics (they come from the same events)
    for (const LibraryEntry& e : alex) {
        auto st = src.userStats(e.id, A);
        CHECK(st && st->playSeconds == e.playSeconds && st->launches == e.launches);
    }
    // Deleted official games keep their name only if the cache knows it
    size_t unknown = 0;
    for (const LibraryEntry& e : alex) if (e.name == unknownTitleName(e.id) && !e.installed) unknown++;
    CHECK(unknown == 2);                                    // the two deleted titles were never seen installed

    // Sorting variants
    std::vector<LibraryEntry> byName = libraryOf(src, claims, A, SortBy::Name);
    for (size_t i = 1; i < byName.size(); i++) CHECK(normalizeName(byName[i - 1].name) <= normalizeName(byName[i].name));
    std::vector<LibraryEntry> byLast = libraryOf(src, claims, A, SortBy::LastPlayed);
    for (size_t i = 1; i < byLast.size(); i++) CHECK(byLast[i - 1].last >= byLast[i].last);
}

static void test_new_titles_and_claiming() {
    MemorySource src = makeDemoSource(NOW);
    std::vector<Session> sessions = buildSessions(src.eventList);
    std::vector<TitleInfo> titles = src.titles();
    ClaimStore claims;

    std::vector<TitleId> sug = suggestNewTitles(sessions, titles, claims, A);
    // Installed homebrew is suggested; the old, deleted forwarder isn't (no name/icon to offer); official games aren't
    CHECK(sug.size() == 2);
    CHECK(std::count(sug.begin(), sug.end(), 0x05A1000000000002ULL) == 1 && std::count(sug.begin(), sug.end(), 0x05B2000000000001ULL) == 1);

    // Claim everything for Alex; the deleted forwarder is claimed under the same name so it merges
    claims.add(0x05A1000000000002ULL, "Homebrew Emulator", A);
    claims.add(0x05B2000000000001ULL, "Save Manager", A);
    claims.add(0x05A1000000000001ULL, "Homebrew Emulator", A);       // older ID, listed first would also work
    CHECK(refreshClaimSnapshots(claims, titles, src));
    CHECK(!refreshClaimSnapshots(claims, titles, src));               // second refresh: nothing new

    std::vector<LibraryEntry> alex = libraryOf(src, claims, A);
    const LibraryEntry* emu = nullptr;
    int claimedCount = 0;
    for (const LibraryEntry& e : alex) { if (e.claimed) claimedCount++; if (e.name == "Homebrew Emulator") emu = &e; }
    CHECK(claimedCount == 2 && emu != nullptr && emu->members.size() == 2);
    // The merged numbers are the sum of both forwarders' sessions
    int64_t expected = 0; uint32_t launches = 0;
    for (const Session& s : sessions) {
        if (s.title == 0x05A1000000000001ULL || s.title == 0x05A1000000000002ULL) { expected += focusSeconds(s, kTimeMin, kTimeMax); launches++; }
    }
    CHECK(emu && emu->playSeconds == expected && emu->launches == launches);
    // Sam didn't claim anything: his library has no homebrew
    for (const LibraryEntry& e : libraryOf(src, claims, B)) CHECK(!e.claimed);
    // After ignoring/claiming nothing is suggested again, for Alex; Sam is not nagged about claimed titles
    CHECK(suggestNewTitles(sessions, titles, claims, A).empty() && suggestNewTitles(sessions, titles, claims, B).empty());
}

static void test_claim_candidates() {
    MemorySource src = makeDemoSource(NOW);
    std::vector<Session> sessions = buildSessions(src.eventList);
    std::vector<TitleInfo> titles = src.titles();
    const TitleId emuOld = 0x05A1000000000001ULL, emuNew = 0x05A1000000000002ULL, saves = 0x05B2000000000001ULL;
    ClaimStore claims;
    auto find = [](const std::vector<ClaimCandidate>& v, TitleId id) -> const ClaimCandidate* {
        for (const ClaimCandidate& c : v) if (c.id == id) return &c;
        return nullptr;
    };

    // Without a name for the deleted forwarder only the installed homebrew is listed; never official games
    std::vector<ClaimCandidate> list = claimCandidates(sessions, titles, claims, nullptr, A);
    CHECK(list.size() == 2 && find(list, emuNew) && find(list, saves) && !find(list, emuOld));
    for (const ClaimCandidate& c : list) CHECK(c.installed && !c.claimed && c.playSeconds > 0 && !c.name.empty());

    // Once the system cache gave its name, the deleted forwarder can be claimed too
    NameCache names; names.update(titles);
    learnUninstalledNames(names, src.events(), titles, src);
    list = claimCandidates(sessions, titles, claims, &names, A);
    CHECK(list.size() == 3 && find(list, emuOld) && !find(list, emuOld)->installed && find(list, emuOld)->name == "Homebrew Emulator");
    for (size_t i = 1; i < list.size(); i++) CHECK(normalizeName(list[i - 1].name) <= normalizeName(list[i].name));

    // Claims are per profile
    claims.add(emuNew, "Homebrew Emulator", A);
    list = claimCandidates(sessions, titles, claims, &names, A);
    CHECK(list.size() == 3 && find(list, emuNew)->claimed && !find(list, saves)->claimed);
    std::vector<ClaimCandidate> other = claimCandidates(sessions, titles, claims, &names, B);
    CHECK(other.size() == 3 && !find(other, emuNew)->claimed);

    // A claimed title that is gone everywhere (not installed, no sessions, no cached name) stays listed from its snapshot
    const TitleId gone = 0x05C3000000000001ULL;
    claims.add(gone, "Old Tool", A);
    SystemStats st; st.playSeconds = 1234; st.launches = 3;
    claims.updateSnapshot(gone, "Old Tool", st);
    list = claimCandidates(sessions, titles, claims, &names, A);
    CHECK(list.size() == 4 && find(list, gone) && find(list, gone)->claimed && !find(list, gone)->installed);
    CHECK(find(list, gone)->name == "Old Tool" && find(list, gone)->playSeconds == 1234);
}

static void test_system_stats_only_fill_gaps() {
    // A title played twice for 10 minutes, as the log has it
    MemorySource src;
    src.profileList.push_back({A, "A"});
    src.titleList.push_back({0x0100000000010000ULL, "Game", true, {}});
    int64_t t = NOW - 5 * 86400;
    t = src.addSession(0x0100000000010000ULL, A, t, {600}) + 100;
    src.addSession(0x0100000000010000ULL, A, t, {600});
    std::vector<Session> sessions = buildSessions(src.eventList);
    std::vector<TitleInfo> titles = src.titles();
    ClaimStore claims;
    auto entry = [&]() {
        LibraryInput in{sessions, titles, claims, nullptr, &src, A, NOW, 0};
        std::vector<LibraryEntry> lib = buildLibrary(in, SortBy::Playtime);
        return lib.size() == 1 ? lib[0] : LibraryEntry{};
    };
    // The system's total is a bit higher but it saw no more launches: the log's numbers stay
    SystemStats sys;
    sys.playSeconds = 1380;
    sys.launches = 2;
    src.userStatList[{0x0100000000010000ULL, A}] = sys;
    LibraryEntry e = entry();
    CHECK(e.playSeconds == 1200 && e.launches == 2);
    // Fewer launches in the system (sessions it didn't count): still the log
    sys.launches = 1;
    src.userStatList[{0x0100000000010000ULL, A}] = sys;
    e = entry();
    CHECK(e.playSeconds == 1200 && e.launches == 2);
    // More launches in the system: the log lost events, the system's numbers fill the gap
    sys.launches = 5;
    sys.playSeconds = 7200;
    src.userStatList[{0x0100000000010000ULL, A}] = sys;
    e = entry();
    CHECK(e.playSeconds == 7200 && e.launches == 5);
    // ...but never lower the log's playtime
    sys.playSeconds = 60;
    src.userStatList[{0x0100000000010000ULL, A}] = sys;
    e = entry();
    CHECK(e.playSeconds == 1200 && e.launches == 5);
}

static void test_civil_time() {
    CivilTime c = toCivil(0, 0);
    CHECK(c.year == 1970 && c.month == 1 && c.day == 1 && c.hour == 0 && c.weekday == 3);       // a Thursday
    c = toCivil(1709214300, 0);                                                                // 2024-02-29 13:45 UTC
    CHECK(c.year == 2024 && c.month == 2 && c.day == 29 && c.hour == 13 && c.minute == 45 && c.weekday == 3);
    c = toCivil(1709214300, 3 * 3600);                                                         // UTC+3
    CHECK(c.day == 29 && c.hour == 16);
    c = toCivil(0, -3600);                                                                     // UTC-1: still 1969
    CHECK(c.year == 1969 && c.month == 12 && c.day == 31 && c.hour == 23 && c.weekday == 2);
    CHECK(monthStart(2026, 11, 0) == 1793491200 && monthStart(2026, 11, 3600) == 1793491200 - 3600);
    CHECK(monthName(1) == "Jan" && monthName(12) == "Dec" && weekdayName(6) == "Sun" && monthName(13) == "?");
}

static void test_game_details() {
    const TitleId game = 0x0100000000020000ULL, emuOld = 0x05A1000000000001ULL, emuNew = 0x05A1000000000002ULL;
    MemorySource src;
    src.profileList.push_back({A, "A"});
    src.titleList.push_back({game, "Game", true, {}});
    src.titleList.push_back({emuNew, "Emu", true, {}});
    // 2026-01-31 23:30 UTC: 60 minutes, about half in January and half in February
    int64_t t = src.addSession(game, A, 1769902200, {3600}) + 100;
    // March: 10 and 20 minutes; nothing in February otherwise
    t = src.addSession(game, A, 1772323200 + 86400, {600}) + 100;
    t = src.addSession(game, A, t + 3600, {1200}, true) + 100;          // crashed, no exit
    t = src.addSession(game, B, t + 3600, {9999}) + 100;                // another profile: not counted
    t = src.addSession(emuOld, Uid{}, t + 3600, {300}) + 100;           // homebrew, no profile
    src.addSession(emuNew, Uid{}, t + 3600, {700});
    std::vector<Session> sessions = buildSessions(src.eventList);
    std::vector<TitleInfo> titles = src.titles();
    ClaimStore claims;
    claims.add(emuNew, "Emu", A);
    claims.add(emuOld, "Emu", A);
    LibraryInput in{sessions, titles, claims, nullptr, nullptr, A, 1772323200 + 30 * 86400, 0};
    std::vector<LibraryEntry> lib = buildLibrary(in, SortBy::Name);
    const LibraryEntry* g = nullptr;
    const LibraryEntry* emu = nullptr;
    for (const LibraryEntry& e : lib) {
        if (e.id == game) g = &e;
        if (e.claimed) emu = &e;
    }
    CHECK(g && emu);
    if (!g || !emu) return;

    GameDetails d = buildDetails(in, *g);
    CHECK(d.sessions.size() == 3 && d.loggedSeconds == 3600 + 600 + 1200 && d.loggedSeconds == g->playSeconds);
    CHECK(d.sessions[0].start > d.sessions[1].start && d.sessions[1].start > d.sessions[2].start);     // newest first
    CHECK(!d.sessions[0].endedCleanly && d.sessions[1].endedCleanly);
    CHECK(d.longestSeconds == 3600 && d.averageSeconds == (3600 + 600 + 1200) / 3);
    // Jan, Feb, Mar with the first session split at midnight, February otherwise empty
    CHECK(d.months.size() == 3 && d.months[0].month == 1 && d.months[1].month == 2 && d.months[2].month == 3);
    // (focus starts 5 s after the launch, at 23:30:05)
    CHECK(d.months[0].playSeconds == 1795 && d.months[1].playSeconds == 1805 && d.months[2].playSeconds == 1800);
    // In UTC+1 the whole first session is in February
    LibraryInput plus1 = in;
    plus1.tzOffsetSeconds = 3600;
    GameDetails d1 = buildDetails(plus1, *g);
    CHECK(d1.months.size() == 2 && d1.months[0].month == 2 && d1.months[0].playSeconds == 3600);
    int weekdaySessions = 0;
    for (int i = 0; i < 7; i++) weekdaySessions += d.weekdaySessions[i];
    CHECK(weekdaySessions == 3);

    // Claimed group: every session of every member, no profile needed
    GameDetails de = buildDetails(in, *emu);
    CHECK(de.sessions.size() == 2 && de.loggedSeconds == 1000);
}

static void test_choose_profile() {
    std::vector<Profile> ps = {{A, "A", {}}, {B, "B", {}}};
    const Uid gone{9, 9};
    CHECK(chooseProfile(ps, Uid{}, Uid{}) == A);            // nothing known: the first
    CHECK(chooseProfile(ps, Uid{}, B) == B);                // last used
    CHECK(chooseProfile(ps, A, B) == A);                    // launched with a user: that one wins
    CHECK(chooseProfile(ps, gone, gone) == A);              // deleted profiles are skipped
    CHECK(chooseProfile(ps, gone, B) == B);
    CHECK(!chooseProfile({}, A, B).valid());
}

static void test_adjustments() {
    // Store: per profile and title, 0 removes, survives a save
    AdjustmentStore adj;
    adj.set(0x100, A, 3600);
    adj.set(0x100, B, -60);
    adj.set(0x200, A, 5);
    adj.set(0x200, A, 0);
    CHECK(adj.size() == 2 && adj.get(0x100, A) == 3600 && adj.get(0x100, B) == -60 && adj.get(0x200, A) == 0);
    AdjustmentStore back = AdjustmentStore::parse(adj.serialize());
    CHECK(back.size() == 2 && back.get(0x100, A) == 3600 && back.get(0x100, B) == -60);
    AdjustmentStore junk = AdjustmentStore::parse("adjust|zz|00|5\nadjust|0100000000000100|" + A.hex() + "|12x\nadjust|0100000000000100|" + A.hex() + "|\r\n");
    CHECK(junk.size() == 0);

    MemorySource src = makeDemoSource(NOW);
    std::vector<Session> sessions = buildSessions(src.eventList);
    std::vector<TitleInfo> titles = src.titles();
    ClaimStore claims;
    auto build = [&](const AdjustmentStore* a, const Uid& who) {
        LibraryInput in{sessions, titles, claims, nullptr, &src, who, NOW, 0};
        in.adjustments = a;
        return buildLibrary(in, SortBy::Playtime);
    };
    auto find = [](const std::vector<LibraryEntry>& v, TitleId id) -> const LibraryEntry* {
        for (const LibraryEntry& e : v) if (e.id == id) return &e;
        return nullptr;
    };
    std::vector<LibraryEntry> plain = build(nullptr, A);
    const LibraryEntry last = plain.back();     // the least played title

    // Adding time moves the title up the sorted list; nothing else about it changes
    AdjustmentStore more;
    more.set(last.id, A, plain.front().playSeconds);
    std::vector<LibraryEntry> adjusted = build(&more, A);
    CHECK(adjusted.front().id == last.id && adjusted.front().adjustSeconds == plain.front().playSeconds);
    CHECK(adjusted.front().playSeconds == last.playSeconds + plain.front().playSeconds && adjusted.front().launches == last.launches);
    std::vector<LibraryEntry> other = build(&more, B);
    for (const LibraryEntry& e : other) CHECK(e.adjustSeconds == 0);   // other profile untouched

    // Removing more than was played stops at zero, and the title stays listed
    AdjustmentStore less;
    less.set(plain.front().id, A, -plain.front().playSeconds - 999);
    adjusted = build(&less, A);
    const LibraryEntry* top = find(adjusted, plain.front().id);
    CHECK(top && top->playSeconds == 0 && top->launches == plain.front().launches && adjusted.size() == plain.size());

    // Merged homebrew: a correction stored on any member counts for the group
    const TitleId emuOld = 0x05A1000000000001ULL, emuNew = 0x05A1000000000002ULL;
    claims.add(emuNew, "Homebrew Emulator", A);
    claims.add(emuOld, "Homebrew Emulator", A);
    plain = build(nullptr, A);
    AdjustmentStore group;
    group.set(emuNew, A, 600);
    group.set(emuOld, A, 60);
    const LibraryEntry* before = find(plain, emuNew);   // claimed first, so shown for the group
    std::vector<LibraryEntry> grouped = build(&group, A);
    const LibraryEntry* after = find(grouped, emuNew);
    CHECK(before && after && after->adjustSeconds == 660 && after->playSeconds == before->playSeconds + 660);
}

namespace {
// Answers from a table; records what was asked
struct FakeHttp : griddb::HttpClient {
    std::map<std::string, griddb::HttpResponse> answers;
    std::vector<std::pair<std::string, std::vector<std::string>>> calls;
    griddb::HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override {
        calls.emplace_back(url, headers);
        auto it = answers.find(url);
        if (it != answers.end()) return it->second;
        griddb::HttpResponse r;
        r.status = 404;
        return r;
    }
    void set(const std::string& url, int status, const std::string& body) {
        griddb::HttpResponse r;
        r.status = status;
        r.body = body;
        answers[url] = r;
    }
};
}  // namespace

static void test_griddb() {
    using namespace griddb;
    // Names and URLs
    CHECK(cleanName("  Zelda\xE2\x84\xA2:  Breath \xC2\xAE ") == "Zelda: Breath");
    std::vector<std::string> t = searchTerms("Xenoblade Chronicles\xE2\x84\xA2 3: Future Redeemed");
    CHECK(t.size() == 2 && t[0] == "Xenoblade Chronicles 3: Future Redeemed" && t[1] == "Xenoblade Chronicles 3");
    t = searchTerms("Hades - Deluxe");
    CHECK(t.size() == 2 && t[1] == "Hades");
    CHECK(searchTerms("Celeste").size() == 1 && searchTerms("   ").empty());
    CHECK(urlEncode("Pok\xC3\xA9mon: A&B/C") == "Pok%C3%A9mon%3A%20A%26B%2FC");
    CHECK(searchUrl("B", "Hades II") == "B/search/autocomplete/Hades%20II");

    // Parsing (escapes, unicode, unknown fields, bad input)
    std::vector<Game> games;
    CHECK(parseSearch(R"({"success":true,"data":[{"id":123,"name":"Pokémon \"X\"","types":["steam"],"verified":true,"release_date":1.4e9},{"id":0,"name":"bad"}]})", games));
    CHECK(games.size() == 1 && games[0].id == 123 && games[0].name == "Pok\xC3\xA9mon \"X\"");
    CHECK(parseSearch(R"({"success":true,"data":[]})", games) && games.empty());
    CHECK(!parseSearch(R"({"success":false,"errors":["nope"]})", games));
    CHECK(!parseSearch("<html>", games) && !parseSearch(R"({"success":true,"data":[)", games) && !parseSearch("", games));
    std::vector<Grid> grids;
    CHECK(parseGrids(R"({"success":true,"page":0,"data":[
        {"id":1,"score":5,"style":"alternate","width":600,"height":900,"mime":"image/webp","url":"https:\/\/cdn\/a.webp","author":{"name":"x"}},
        {"id":2,"width":920,"height":430,"mime":"image/png","url":"https://cdn/wide.png"},
        {"id":3,"width":600,"height":900,"mime":"image/png","url":"https://cdn/b.png","nsfw":false,"tags":[]},
        {"id":4,"width":342,"height":482,"mime":"image/jpeg","url":"https://cdn/c.jpg"}]})", grids));
    CHECK(grids.size() == 2 && grids[0].url == "https://cdn/b.png" && grids[1].mime == "image/jpeg");

    // The whole lookup
    const std::string base = "https://api";
    FakeHttp http;
    http.set(searchUrl(base, "Hollow Knight: Silksong"), 200, R"({"success":true,"data":[]})");
    http.set(searchUrl(base, "Hollow Knight"), 200, R"({"success":true,"data":[{"id":42,"name":"Hollow Knight"}]})");
    http.set(gridsUrl(base, 42), 200, R"({"success":true,"data":[{"url":"https://cdn/1.png","mime":"image/png","width":600,"height":900},{"url":"https://cdn/2.jpg","mime":"image/jpeg","width":600,"height":900}]})");
    http.set("https://cdn/1.png", 200, "PNGDATA");
    http.set("https://cdn/2.jpg", 200, "JPGDATA");
    Result r = fetchPoster(http, base, "SECRET", "Hollow Knight\xE2\x84\xA2: Silksong");
    CHECK(r.outcome == Outcome::Found && r.gameId == 42 && r.image == "PNGDATA");
    CHECK(http.calls.size() == 4);
    for (const auto& c : http.calls) {
        const bool api = c.first.rfind(base, 0) == 0;
        CHECK(api ? (c.second.size() == 1 && c.second[0] == "Authorization: Bearer SECRET") : c.second.empty());   // key never sent to the CDN
    }
    CHECK(fetchPoster(http, base, "SECRET", "Hollow Knight", 1).image == "JPGDATA");    // the next poster
    CHECK(fetchPoster(http, base, "SECRET", "Hollow Knight", 2).image == "PNGDATA");    // wraps around

    FakeHttp none;
    CHECK(fetchPoster(none, base, "K", "Unknown Game").outcome == Outcome::NoMatch);
    none.set(searchUrl(base, "Tool"), 200, R"({"success":true,"data":[{"id":7,"name":"Tool"}]})");
    none.set(gridsUrl(base, 7), 200, R"({"success":true,"data":[]})");
    CHECK(fetchPoster(none, base, "K", "Tool").outcome == Outcome::NoGrid);
    FakeHttp denied;
    denied.set(searchUrl(base, "Game"), 401, R"({"success":false})");
    CHECK(fetchPoster(denied, base, "K", "Game").outcome == Outcome::BadKey);
    FakeHttp down;
    down.set(searchUrl(base, "Game"), 503, "");
    Result err = fetchPoster(down, base, "K", "Game");
    CHECK(err.outcome == Outcome::Error && err.message.find("503") != std::string::npos && err.message.find("K") == std::string::npos);

    // Memo: found titles are not asked again, misses are retried after 30 days, errors are not stored
    Memo memo;
    memo.record(0x1, Outcome::Found, 42, NOW);
    memo.record(0x2, Outcome::NoMatch, 0, NOW);
    memo.record(0x3, Outcome::Error, 0, NOW);
    memo.record(0x4, Outcome::BadKey, 0, NOW);
    CHECK(memo.size() == 2 && !memo.shouldFetch(0x1, NOW + 400 * 86400) && memo.shouldFetch(0x3, NOW) && memo.shouldFetch(0x4, NOW));
    CHECK(!memo.shouldFetch(0x2, NOW + 29 * 86400) && memo.shouldFetch(0x2, NOW + 30 * 86400));
    Memo back = Memo::parse(memo.serialize() + "garbage|x\nfound|zz|1|2\n");
    CHECK(back.size() == 2 && back.find(0x1) && back.find(0x1)->gameId == 42 && back.find(0x2)->outcome == Outcome::NoMatch);
    back.forget(0x1);
    CHECK(back.shouldFetch(0x1, NOW));
}

static void test_griddb_browse() {
    using namespace griddb;
    const std::string base = "https://api";
    FakeHttp http;
    http.set(searchUrl(base, "Zelda"), 200, R"({"success":true,"data":[{"id":1,"name":"Zelda A"},{"id":2,"name":"Zelda B"}]})");
    http.set(gridsUrl(base, 2), 200, R"({"success":true,"data":[
        {"id":71,"url":"https://cdn/71.png","thumb":"https://cdn/t71.jpg","mime":"image/png","width":600,"height":900},
        {"id":72,"url":"https://cdn/72.jpg","thumb":"","mime":"image/jpeg","width":342,"height":482}]})");
    http.set("https://cdn/t71.jpg", 200, "THUMB");

    // Every search result is offered, not only the first
    std::vector<Game> games;
    std::string msg;
    CHECK(searchGames(http, base, "K", "  Zelda\xE2\x84\xA2 ", games, msg) == Outcome::Found && games.size() == 2 && games[1].name == "Zelda B");
    CHECK(searchGames(http, base, "K", "Nothing", games, msg) == Outcome::NoMatch && games.empty());
    CHECK(searchGames(http, base, "K", "   ", games, msg) == Outcome::NoMatch);

    // Posters of the chosen game, with their IDs and previews
    std::vector<Grid> grids;
    CHECK(listGrids(http, base, "K", 2, grids, msg) == Outcome::Found && grids.size() == 2);
    CHECK(grids[0].id == 71 && grids[0].thumb == "https://cdn/t71.jpg" && grids[1].id == 72 && grids[1].thumb.empty());
    CHECK(listGrids(http, base, "K", 1, grids, msg) == Outcome::NoGrid && grids.empty());

    // Downloads never carry the key; a failed one is an error, not a refused key
    std::string body;
    http.calls.clear();
    CHECK(download(http, "https://cdn/t71.jpg", body, msg) == Outcome::Found && body == "THUMB" && http.calls[0].second.empty());
    http.set("https://cdn/gone.jpg", 403, "");
    CHECK(download(http, "https://cdn/gone.jpg", body, msg) == Outcome::Error && body.empty());

    // The automatic lookup reports which poster it took
    http.set("https://cdn/71.png", 200, "BIG");
    http.set(searchUrl(base, "Zelda B"), 200, R"({"success":true,"data":[{"id":2,"name":"Zelda B"}]})");
    Result r = fetchPoster(http, base, "K", "Zelda B");
    CHECK(r.outcome == Outcome::Found && r.gameId == 2 && r.gameName == "Zelda B" && r.gridId == 71 && r.image == "BIG");

    // Memo: the user's choice and "no poster" are never overwritten by the automatic lookup
    Memo memo;
    memo.record(0x10, Outcome::Found, 2, NOW, "Zelda | B", 72, true);
    memo.record(0x11, Outcome::Hidden, 0, NOW);
    CHECK(!memo.shouldFetch(0x10, NOW + 999 * 86400) && !memo.shouldFetch(0x11, NOW + 999 * 86400));
    Memo back = Memo::parse(memo.serialize() + "found|0100000000000099|5|123\n");   // a line from the older format
    const Memo::Entry* e = back.find(0x10);
    CHECK(back.size() == 3 && e && e->manual && e->gridId == 72 && e->gameId == 2 && e->gameName == "Zelda   B");
    CHECK(back.find(0x11)->outcome == Outcome::Hidden && back.find(0x0100000000000099ULL)->gameId == 5 && !back.find(0x0100000000000099ULL)->manual);
}

static void test_deleted_claimed_title_keeps_history() {
    MemorySource src;
    src.profileList = {{A, "Alex"}};
    src.titleList = {{0x500, "Forwarder", true, {}}};
    src.addSession(0x500, Uid{}, NOW - 5 * 86400, {3600});
    src.addSession(0x500, Uid{}, NOW - 2 * 86400, {1800});
    src.deriveStats();

    ClaimStore claims;
    claims.add(0x500, "Forwarder", A);
    CHECK(refreshClaimSnapshots(claims, src.titleList, src));

    // The title is deleted: the system drops its statistics and (later) its events
    MemorySource after;
    after.profileList = src.profileList;
    NameCache names; names.update(src.titleList);
    std::vector<Session> none;
    std::vector<TitleInfo> noTitles;
    LibraryInput in{none, noTitles, claims, &names, &after, A, NOW, 0};
    std::vector<LibraryEntry> lib = buildLibrary(in, SortBy::Playtime);
    CHECK(lib.size() == 1 && lib[0].name == "Forwarder" && !lib[0].installed);
    CHECK(lib[0].playSeconds == 5400 && lib[0].launches == 2);
}

// Counts the console-wide queries (~110 ms each on the Switch)
struct CountingSource : MemorySource {
    int consoleCalls = 0;
    std::optional<SystemStats> consoleStats(TitleId id) override { consoleCalls++; return MemorySource::consoleStats(id); }
};

static void test_console_stats_only_from_snapshots() {
    CountingSource src;
    src.profileList = {{A, "Alex"}};
    src.titleList = {{0x500, "Forwarder", true, {}}, {0x600, "Tool", true, {}}};
    src.addSession(0x500, Uid{}, NOW - 5 * 86400, {3600});
    src.addSession(0x600, Uid{}, NOW - 3 * 86400, {600});
    src.deriveStats();
    src.consoleStatList[0x500].launches = 5;                 // the system counts more: events were lost
    src.consoleStatList[0x500].playSeconds = 9000;
    src.consoleStatList[0x700] = SystemStats{1200, 2, NOW - 90 * 86400, NOW - 80 * 86400};   // deleted, stats still known

    ClaimStore claims;
    claims.add(0x500, "Forwarder", A);
    claims.add(0x600, "Tool", A);
    CHECK(refreshClaimSnapshots(claims, src.titleList, src));
    CHECK(src.consoleCalls == 2);

    // Building (and rebuilding) the library never asks the system; the snapshot's numbers are used
    std::vector<Session> sessions = buildSessions(src.eventList);
    LibraryInput in{sessions, src.titleList, claims, nullptr, &src, A, NOW, 0};
    for (int i = 0; i < 3; i++) {
        std::vector<LibraryEntry> lib = buildLibrary(in, SortBy::Playtime);
        CHECK(lib.size() == 2 && lib[0].id == 0x500 && lib[0].playSeconds == 9000 && lib[0].launches == 5);
    }
    CHECK(src.consoleCalls == 2);

    // After a claim change only titles without a snapshot are asked, deleted ones too
    claims.add(0x700, "Old forwarder", A);
    CHECK(refreshClaimSnapshots(claims, src.titleList, src, true));
    CHECK(src.consoleCalls == 3);
    CHECK(!refreshClaimSnapshots(claims, src.titleList, src, true) && src.consoleCalls == 3);
    std::vector<LibraryEntry> lib = buildLibrary(in, SortBy::Playtime);
    CHECK(lib.size() == 3 && src.consoleCalls == 3);
    bool old = false;
    for (const LibraryEntry& e : lib) if (e.id == 0x700) old = e.playSeconds == 1200 && e.launches == 2 && e.name == "Old forwarder";
    CHECK(old);
    // The start refresh asks only installed titles
    refreshClaimSnapshots(claims, src.titleList, src);
    CHECK(src.consoleCalls == 5);
}

int main() {
    struct T { const char* name; std::function<void()> fn; };
    std::vector<T> tests = {
        {"sessions_basic", test_sessions_basic}, {"clock_change_uses_steady_time", test_clock_change_uses_steady_time},
        {"range_clipping_and_days", test_range_clipping_and_days}, {"unlinked", test_unlinked},
        {"claims", test_claims}, {"claim_snapshot_and_serialization", test_claim_snapshot_and_serialization},
        {"history", test_history}, {"name_cache", test_name_cache}, {"nacp_classic", test_nacp_classic},
#ifdef HAVE_ZLIB
        {"nacp_compressed", test_nacp_compressed},
#endif
        {"inflate_stress", test_inflate_stress}, {"dump_roundtrip", test_dump_roundtrip}, {"library_demo", test_library_demo},
        {"uninstalled_names", test_uninstalled_names},
        {"new_titles_and_claiming", test_new_titles_and_claiming}, {"claim_candidates", test_claim_candidates},
        {"console_stats_only_from_snapshots", test_console_stats_only_from_snapshots},
        {"adjustments", test_adjustments}, {"system_stats_only_fill_gaps", test_system_stats_only_fill_gaps},
        {"civil_time", test_civil_time}, {"choose_profile", test_choose_profile}, {"game_details", test_game_details}, {"griddb", test_griddb}, {"griddb_browse", test_griddb_browse}, {"deleted_claimed_title_keeps_history", test_deleted_claimed_title_keeps_history},
    };
    for (const T& t : tests) {
        int before = g_failed;
        t.fn();
        std::printf("%-40s %s\n", t.name, g_failed == before ? "ok" : "FAILED");
    }
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
#ifndef HAVE_ZLIB
    std::printf("(zlib not found: compressed-NACP tests skipped)\n");
#endif
    return g_failed == 0 ? 0 : 1;
}

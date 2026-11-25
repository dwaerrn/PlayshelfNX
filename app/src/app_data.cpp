#include "app_data.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>

#include "lib/details.hpp"
#include "lib/library.hpp"
#include "poster_fetch.hpp"
#ifdef __SWITCH__
#include "switch_source.hpp"
#endif

namespace {

using Clock = std::chrono::steady_clock;

std::string dataDir() {
#ifdef __SWITCH__
    return "/config/Library/";
#else
    return "data/";
#endif
}

// Reads a file; if a crash left only the temporary copy of a save, that one is used
std::string readFile(const std::string& path) {
    std::error_code ec;
    std::string use = path;
    if (!std::filesystem::exists(path, ec) && std::filesystem::exists(path + ".tmp", ec)) {
        use = path + ".tmp";
    }
    std::ifstream f(use, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Writes to a temporary file first so an interrupted save can't destroy the old data
bool writeFile(const std::string& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            return false;
        }
        f << content;
        f.flush();
        if (!f.good()) {
            return false;
        }
    }
    std::filesystem::remove(path, ec);   // FAT can't rename over an existing file
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

void logLine(const char* fmt, long a = 0, long b = 0, long c = 0) {
    std::lock_guard<std::mutex> lock(logMutex());
    const std::string path = dataDir() + "debug.log";
    std::error_code ec;
    std::filesystem::create_directories(dataDir(), ec);
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 65536) {
        std::filesystem::remove(path, ec);
    }
    if (FILE* f = std::fopen(path.c_str(), "a")) {
        std::fprintf(f, fmt, a, b, c);
        std::fputc('\n', f);
        std::fclose(f);
    }
}

long msSince(Clock::time_point t) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count());
}

// Counts and times the console's statistics queries (for the startup analysis in debug.log)
class TimedSource : public lib::PlaySource {
  public:
    explicit TimedSource(lib::PlaySource& base) : base_(base) { }

    std::vector<lib::Profile> profiles() override { return base_.profiles(); }
    std::vector<lib::TitleInfo> titles() override { return base_.titles(); }
    std::vector<lib::RawEvent> events() override { return base_.events(); }
    std::optional<lib::SystemStats> userStats(lib::TitleId id, const lib::Uid& u) override {
        const auto t = Clock::now();
        auto r = base_.userStats(id, u);
        userCalls++;
        userUs += usSince(t);
        return r;
    }
    std::optional<lib::SystemStats> consoleStats(lib::TitleId id) override {
        const auto t = Clock::now();
        auto r = base_.consoleStats(id);
        consoleCalls++;
        consoleUs += usSince(t);
        return r;
    }
    std::optional<lib::TitleInfo> uninstalledTitle(lib::TitleId id) override {
        const auto t = Clock::now();
        auto r = base_.uninstalledTitle(id);
        cacheCalls++;
        cacheUs += usSince(t);
        return r;
    }

    // One line per phase, then the counters start again
    void report(const char* phase) {
        if (userCalls || consoleCalls || cacheCalls) {
            char line[200];
            std::snprintf(line, sizeof(line), "  stats calls during %s: %ld per-profile (%ld ms), %ld console-wide (%ld ms), %ld icon/name cache (%ld ms)", phase,
                          userCalls, userUs / 1000, consoleCalls, consoleUs / 1000, cacheCalls, cacheUs / 1000);
            logLine(line);
        }
        userCalls = consoleCalls = cacheCalls = 0;
        userUs = consoleUs = cacheUs = 0;
    }

  private:
    lib::PlaySource& base_;
    long userCalls = 0, consoleCalls = 0, cacheCalls = 0;
    long userUs = 0, consoleUs = 0, cacheUs = 0;

    static long usSince(Clock::time_point t) {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count());
    }
};

struct State {
    std::unique_ptr<lib::MemorySource> memory;
    std::unique_ptr<lib::PlaySource> console;
    std::unique_ptr<TimedSource> timed;
    lib::PlaySource* source = nullptr;
    bool demo = false;
    lib::HistoryStore history;
    lib::ClaimStore claims;
    lib::NameCache names;
    lib::AdjustmentStore adjust;

    bool loaded = false;
    bool titlePrefsLoaded = false;
    std::map<uint64_t, std::string> titleNames;     // titles.txt: name|<ID>|<name>
    std::set<uint64_t> hiddenBadges;                // titles.txt: nobadge|<ID>
    int tz = 0;                             // seconds east of UTC
    lib::Profile profile;                   // the profile whose library is shown
    bool byLastPlayed = false;              // library order (sort.txt)
    std::vector<lib::Profile> profiles;
    std::vector<lib::TitleInfo> titles;
    std::vector<lib::Session> sessions;
};

State& state() {
    static State s;
    return s;
}

// Everything from the real source, but with the app's own (longer) event history
class HistorySource : public lib::PlaySource {
  public:
    HistorySource(lib::PlaySource& base, std::vector<lib::RawEvent> events) : base_(base), events_(std::move(events)) { }
    std::vector<lib::Profile> profiles() override { return base_.profiles(); }
    std::vector<lib::TitleInfo> titles() override { return base_.titles(); }
    std::vector<lib::RawEvent> events() override { return events_; }
    std::optional<lib::SystemStats> userStats(lib::TitleId id, const lib::Uid& u) override { return base_.userStats(id, u); }
    std::optional<lib::SystemStats> consoleStats(lib::TitleId id) override { return base_.consoleStats(id); }
    std::optional<lib::TitleInfo> uninstalledTitle(lib::TitleId id) override { return base_.uninstalledTitle(id); }

  private:
    lib::PlaySource& base_;
    std::vector<lib::RawEvent> events_;
};

// Art files on the SD card, listed once per library build:
//   art/<TITLEID>.jpg|.png    the user's own poster (shown filling the card, best at 300x450)
//   grids/<TITLEID>.jpg       poster downloaded from SteamGridDB
//   icons/<TITLEID>.jpg       the system icon, saved the first time the title is seen
struct ArtFiles {
    std::map<std::string, std::string> posters;     // lower-case title ID -> path
    std::map<std::string, std::string> grids;       // posters downloaded from SteamGridDB
    std::set<std::string> icons;                    // lower-case title IDs that have an icon file

    void scan() {
        posters.clear();
        grids.clear();
        icons.clear();
        std::error_code ec;
        for (const auto& f : std::filesystem::directory_iterator(dataDir() + "art", ec)) {
            std::string ext = lower(f.path().extension().string());
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                posters[lower(f.path().stem().string())] = f.path().string();
            }
        }
        for (const auto& f : std::filesystem::directory_iterator(dataDir() + "grids", ec)) {
            // <TITLEID>.jpg (automatic) or <TITLEID>-<gridid>.jpg (picked by the user)
            const std::string stem = lower(f.path().stem().string());
            grids[stem.substr(0, stem.find('-'))] = f.path().string();
        }
        for (const auto& f : std::filesystem::directory_iterator(dataDir() + "icons", ec)) {
            icons.insert(lower(f.path().stem().string()));
        }
    }

    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

std::string iconPath(lib::TitleId id) {
    return dataDir() + "icons/" + lib::titleHex(id) + ".jpg";
}

// Poster for a library entry (any member of a merged group): the user's file, else a downloaded one
std::string findPoster(const ArtFiles& art, const lib::LibraryEntry& e) {
    std::vector<lib::TitleId> ids = {e.id};
    ids.insert(ids.end(), e.members.begin(), e.members.end());
    for (const auto* map : {&art.posters, &art.grids}) {    // the user's own files first
        for (lib::TitleId id : ids) {
            auto it = map->find(ArtFiles::lower(lib::titleHex(id)));
            if (it != map->end()) return it->second;
        }
    }
    return "";
}

// Icon file for a library entry: saved from the system's control data the first time
std::string findIcon(ArtFiles& art, const lib::LibraryEntry& e, int& written) {
    State& st = state();
    std::vector<lib::TitleId> ids = {e.id};
    ids.insert(ids.end(), e.members.begin(), e.members.end());
    for (lib::TitleId id : ids) {
        if (art.icons.count(ArtFiles::lower(lib::titleHex(id)))) return iconPath(id);
    }
    for (lib::TitleId id : ids) {
        std::vector<uint8_t> jpeg;
        for (const lib::TitleInfo& t : st.titles) {
            if (t.id == id && !t.icon.empty()) jpeg = t.icon;
        }
        if (jpeg.empty()) {
            if (auto t = st.source->uninstalledTitle(id)) jpeg = t->icon;
        }
        if (!jpeg.empty() && writeFile(iconPath(id), std::string(jpeg.begin(), jpeg.end()))) {
            art.icons.insert(ArtFiles::lower(lib::titleHex(id)));
            written++;
            return iconPath(id);
        }
    }
    return "";
}

std::string placeholderArt(const lib::LibraryEntry& e) {
    static const std::pair<const char*, const char*> known[] = {
        {"Starfall Odyssey", "starfall.png"}, {"Hollow Grove", "grove.png"}, {"Crimson Tide Racing", "racing.png"},
        {"Kitchen Chaos", "kitchen.png"}, {"Pixel Dungeon", "dungeon.png"}, {"Skyward Isles", "isles.png"},
        {"Neon Drift", "neon.png"}, {"Wildwood Tales", "wildwood.png"}, {"Iron Harbor", "harbor.png"},
        {"Echoes of Dawn", "dawn.png"}, {"Rune Keeper", "rune.png"}, {"Turbo Kart Cup", "kart.png"},
        {"Moonlit Farm", "farm.png"}, {"Homebrew Emulator", "hb_emulator.png"}, {"Save Manager", "hb_saves.png"},
    };
    for (const auto& k : known) {
        if (e.name == k.first) return k.second;
    }
    static const char* generic[] = {"starfall.png", "grove.png", "racing.png", "kitchen.png", "dungeon.png", "isles.png", "neon.png",
                                    "wildwood.png", "harbor.png", "dawn.png", "rune.png", "kart.png", "farm.png"};
    return e.claimed ? "hb_emulator.png" : generic[(e.id >> 12) % 13];
}

bool isOwnTitle(lib::TitleId id) {
#ifdef __SWITCH__
    return id == lib::SwitchSource::ownTitleId();   // the app (its forwarder) never lists itself
#else
    (void)id;
    return false;
#endif
}

// Reads the console (or dump / demo data) once and brings the app's own files up to date
void loadSources() {
    using namespace lib;
    State& st = state();
    st.loaded = true;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const std::string dir = dataDir();

#ifdef __SWITCH__
    st.console.reset(new SwitchSource());
    st.source = st.console.get();
#else
    {
        // A recorded dump of a real console, or the built-in demo library
        st.memory.reset(new MemorySource());
        std::ifstream f("library.dump");
        if (!f || !readDump(f, *st.memory)) {
            *st.memory = makeDemoSource(now);
            st.demo = true;
        }
        st.source = st.memory.get();
    }
#endif

    st.timed.reset(new TimedSource(*st.source));
    st.source = st.timed.get();
    logLine("--- start ---");

    auto t = Clock::now();
    std::vector<Profile> profiles = st.source->profiles();
    logLine("profiles: %ld (with pictures) in %ld ms", static_cast<long>(profiles.size()), msSince(t));
    if (profiles.empty()) {
        return;
    }
    st.profiles = profiles;
    {
        // Launched with a user (forwarder), else the one used last time, else the first
        lib::Uid preselected, remembered;
#ifdef __SWITCH__
        preselected = SwitchSource::preselectedUser();
#endif
        std::string text = readFile(dir + "profile.txt");
        text = text.substr(0, text.find_first_of("\r\n"));
        lib::Uid::parse(text, remembered);
        const lib::Uid uid = chooseProfile(profiles, preselected, remembered);
        for (const Profile& p : profiles) {
            if (p.uid == uid) st.profile = p;
        }
    }
    st.byLastPlayed = readFile(dir + "sort.txt").rfind("last", 0) == 0;
#ifdef __SWITCH__
    st.tz = SwitchSource::utcOffsetSeconds();
#else
    {
        const std::time_t t = std::time(nullptr);
        std::tm local = *std::localtime(&t);
        std::tm utc = *std::gmtime(&t);
        local.tm_isdst = 0;
        utc.tm_isdst = 0;
        st.tz = static_cast<int>(std::difftime(std::mktime(&local), std::mktime(&utc)));
    }
#endif
    logLine("time zone: UTC%+ld min", st.tz / 60);

    t = Clock::now();
    std::vector<RawEvent> events = st.source->events();
    logLine("read %ld events from the system in %ld ms", static_cast<long>(events.size()), msSince(t));
    t = Clock::now();
    st.titles = st.source->titles();
    logLine("read %ld installed titles (names, icons) in %ld ms", static_cast<long>(st.titles.size()), msSince(t));

    if (st.demo) {
        // Demo data is regenerated on every start, so nothing is loaded or saved: every start
        // shows the "new titles found" prompt and claims only live until the app closes.
        st.sessions = buildSessions(events);
        st.names.update(st.titles);
        learnUninstalledNames(st.names, events, st.titles, *st.source);
        return;
    }

    t = Clock::now();
    st.history = HistoryStore::parse(readFile(dir + "history.txt"));
    logLine("history.txt loaded in %ld ms", msSince(t));
    t = Clock::now();
    const size_t before = st.history.events().size();
    const size_t added = st.history.merge(events);
    logLine("history: %ld stored + %ld new events (loaded and merged in %ld ms)", static_cast<long>(before), static_cast<long>(added), msSince(t));
    if (added > 0 || before == 0) {
        t = Clock::now();
        const bool ok = writeFile(dir + "history.txt", st.history.serialize());
        logLine("history saved: %ld (in %ld ms)", ok ? 1 : 0, msSince(t));
    }

    const std::string namesBefore = readFile(dir + "names.txt");
    st.names = NameCache::parse(namesBefore);
    st.names.update(st.titles);
    t = Clock::now();
    const size_t learned = learnUninstalledNames(st.names, st.history.events(), st.titles, *st.source);
    logLine("names of uninstalled titles from the system cache: %ld (in %ld ms)", static_cast<long>(learned), msSince(t));
    if (st.names.serialize() != namesBefore) {
        writeFile(dir + "names.txt", st.names.serialize());
    }

    st.adjust = AdjustmentStore::parse(readFile(dir + "adjust.txt"));
    t = Clock::now();
    st.claims = ClaimStore::parse(readFile(dir + "claims.txt"));
    const bool snapshotsChanged = refreshClaimSnapshots(st.claims, st.titles, *st.source);
    logLine("claims: %ld entries, snapshots refreshed in %ld ms", static_cast<long>(st.claims.entries().size()), msSince(t));
    st.timed->report("names and claim snapshots");
    if (snapshotsChanged) {
        writeFile(dir + "claims.txt", st.claims.serialize());
    }
    t = Clock::now();
    st.sessions = buildSessions(st.history.events());
    logLine("%ld sessions built in %ld ms", static_cast<long>(st.sessions.size()), msSince(t));
}

// After the claims changed: snapshots of the newly claimed titles, then save. The others were
// refreshed at start and can't have changed since (the app is running, not the claimed title).
void claimsChanged() {
    State& st = state();
    lib::refreshClaimSnapshots(st.claims, st.titles, *st.source, true);
    if (!st.demo) {
        const bool ok = writeFile(dataDir() + "claims.txt", st.claims.serialize());
        logLine("claims saved: %ld (%ld entries)", ok ? 1 : 0, static_cast<long>(st.claims.entries().size()));
    }
}

ClaimItem toItem(const lib::ClaimCandidate& c, ArtFiles& art) {
    ClaimItem item;
    lib::LibraryEntry e;
    e.id = c.id;
    e.members = {c.id};
    int written = 0;
    item.iconFile = findIcon(art, e, written);      // saved now if the title was never in the library
    item.id = c.id;
    item.name = c.name;
    item.installed = c.installed;
    item.claimed = c.claimed;
    item.playSecs = static_cast<unsigned>(c.playSeconds);
    return item;
}

}  // namespace

namespace {

void loadTitlePrefs() {
    State& st = state();
    if (st.titlePrefsLoaded) return;
    st.titlePrefsLoaded = true;
    std::istringstream in(readFile(dataDir() + "titles.txt"));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t a = line.find('|');
        if (a == std::string::npos) continue;
        const size_t b = line.find('|', a + 1);
        const std::string kind = line.substr(0, a);
        const uint64_t id = std::strtoull(line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str(), nullptr, 16);
        if (id == 0) continue;
        if (kind == "name" && b != std::string::npos && b + 1 < line.size()) st.titleNames[id] = line.substr(b + 1);
        if (kind == "nobadge") st.hiddenBadges.insert(id);
    }
}

void saveTitlePrefs() {
    State& st = state();
    if (st.demo) return;
    std::string out = "# Library: own names (name|ID|name) and hidden homebrew markers (nobadge|ID)\n";
    char hex[20];
    for (const auto& kv : st.titleNames) {
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(kv.first));
        out += std::string("name|") + hex + "|" + kv.second + "\n";
    }
    for (uint64_t id : st.hiddenBadges) {
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
        out += std::string("nobadge|") + hex + "\n";
    }
    writeFile(dataDir() + "titles.txt", out);
}

}  // namespace

void setTitleName(uint64_t id, const std::string& name) {
    loadTitlePrefs();
    std::string clean;
    for (char c : name) {
        if (c != '|' && c != '\n' && c != '\r') clean += c;
    }
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    while (!clean.empty() && clean.front() == ' ') clean.erase(0, 1);
    if (clean.empty()) state().titleNames.erase(id);
    else state().titleNames[id] = clean;
    saveTitlePrefs();
}

void setHomebrewMarker(uint64_t id, bool shown) {
    loadTitlePrefs();
    if (shown) state().hiddenBadges.erase(id);
    else state().hiddenBadges.insert(id);
    saveTitlePrefs();
}

std::vector<Game> loadLibraryGames() {
    using namespace lib;
    State& st = state();
    const auto started = Clock::now();
    if (!st.loaded) {
        loadSources();
        logLine("loading done in %ld ms", msSince(started));
    }
    if (!st.source || !st.profile.uid.valid()) {
        return {};
    }
    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    LibraryInput in{st.sessions, st.titles, st.claims, &st.names, st.source, st.profile.uid, now, st.tz};
    in.adjustments = &st.adjust;
    auto t = Clock::now();
    ArtFiles art;
    art.scan();
    logLine("art folders listed in %ld ms", msSince(t));
    t = Clock::now();
    if (st.timed) st.timed->report("other work since the last build");
    const std::vector<LibraryEntry> entries = buildLibrary(in, st.byLastPlayed ? SortBy::LastPlayed : SortBy::Playtime);
    logLine("buildLibrary: %ld entries in %ld ms", static_cast<long>(entries.size()), msSince(t));
    if (st.timed) st.timed->report("buildLibrary");
    t = Clock::now();
    int iconsWritten = 0;
    std::vector<Game> games;
    loadTitlePrefs();
    // Calendar days in local time ("yesterday" = before the last local midnight)
    auto daysAgo = [&](int64_t clock) {
        auto day = [](int64_t t) { return t >= 0 ? t / 86400 : (t - 86399) / 86400; };
        return clock ? static_cast<int>(day(now + st.tz) - day(clock + st.tz)) : 9999;
    };
    for (const LibraryEntry& e : entries) {
        if (isOwnTitle(e.id)) continue;
        Game g;
        g.name = g.systemName = e.name;
        auto own = st.titleNames.find(e.id);
        if (own != st.titleNames.end()) g.name = own->second;
        g.hideBadge = st.hiddenBadges.count(e.id) > 0;
        g.playSecs = static_cast<unsigned>(e.playSeconds);
        g.launches = e.launches;
        g.lastDaysAgo = daysAgo(e.last);
        g.firstDaysAgo = daysAgo(e.first);
        g.artFile = findPoster(art, e);
        g.iconFile = findIcon(art, e, iconsWritten);     // also offered on the Change poster screen
        if (st.demo) {
            g.art = placeholderArt(e);      // the demo data has no icons
        }
        g.homebrew = e.claimed;
        g.weekMinutes = e.week;
        g.id = e.id;
        g.adjustSecs = static_cast<int>(e.adjustSeconds);
        g.members.assign(e.members.begin(), e.members.end());
        games.push_back(std::move(g));
    }
    logLine("cards prepared (posters, icons) in %ld ms", msSince(t));
    if (st.timed) st.timed->report("icons");
    logLine("library built: %ld titles, %ld sessions, in %ld ms", static_cast<long>(games.size()), static_cast<long>(st.sessions.size()), msSince(started));
    if (iconsWritten > 0) {
        logLine("icons saved: %ld", iconsWritten);
    }
    return games;
}

std::vector<ProfileItem> profileItems() {
    State& st = state();
    std::vector<ProfileItem> out;
    if (!st.source) {
        return out;
    }
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    for (const lib::Profile& p : st.profiles) {
        ProfileItem item;
        item.uid = p.uid.hex();
        item.name = p.name.empty() ? "Profile" : p.name;
        item.current = p.uid == st.profile.uid;
        if (!p.avatar.empty()) {
            // Written each time the picker opens: the user may have changed it
            item.avatarFile = dataDir() + "avatars/" + item.uid + ".jpg";
            writeFile(item.avatarFile, std::string(p.avatar.begin(), p.avatar.end()));
        }
        // Totals from the play log only (no system statistics: fast enough for every profile)
        lib::LibraryInput in{st.sessions, st.titles, st.claims, &st.names, nullptr, p.uid, now, st.tz};
        in.adjustments = &st.adjust;
        for (const lib::LibraryEntry& e : lib::buildLibrary(in, lib::SortBy::Playtime)) {
            if (isOwnTitle(e.id)) continue;
            item.games++;
            item.playSecs += static_cast<unsigned>(e.playSeconds);
        }
        out.push_back(std::move(item));
    }
    return out;
}

size_t profileCount() {
    return state().profiles.size();
}

bool sortByLastPlayed() {
    return state().byLastPlayed;
}

void setSortByLastPlayed(bool lastPlayed) {
    State& st = state();
    st.byLastPlayed = lastPlayed;
    if (!st.demo) writeFile(dataDir() + "sort.txt", lastPlayed ? "last\n" : "playtime\n");
}

bool switchProfile(const std::string& uidHex) {
    State& st = state();
    for (const lib::Profile& p : st.profiles) {
        if (p.uid.hex() == uidHex) {
            if (p.uid == st.profile.uid) return false;
            st.profile = p;
            if (!st.demo) writeFile(dataDir() + "profile.txt", uidHex + "\n");
            logLine("profile switched");
            return true;
        }
    }
    return false;
}

std::string profileName() {
    return state().profile.name;
}

std::vector<ClaimItem> newTitleSuggestions() {
    State& st = state();
    std::vector<ClaimItem> out;
    if (!st.source || !st.profile.uid.valid()) {
        return out;
    }
    const std::vector<lib::TitleId> ids = lib::suggestNewTitles(st.sessions, st.titles, st.claims, st.profile.uid);
    ArtFiles art;
    art.scan();
    for (const lib::ClaimCandidate& c : lib::claimCandidates(st.sessions, st.titles, st.claims, &st.names, st.profile.uid)) {
        if (!isOwnTitle(c.id) && std::find(ids.begin(), ids.end(), c.id) != ids.end()) {
            out.push_back(toItem(c, art));
        }
    }
    return out;
}

std::vector<ClaimItem> claimList() {
    State& st = state();
    std::vector<ClaimItem> out;
    if (!st.source || !st.profile.uid.valid()) {
        return out;
    }
    ArtFiles art;
    art.scan();
    for (const lib::ClaimCandidate& c : lib::claimCandidates(st.sessions, st.titles, st.claims, &st.names, st.profile.uid)) {
        if (!isOwnTitle(c.id)) {
            out.push_back(toItem(c, art));
        }
    }
    return out;
}

lib::GameDetails gameDetails(const Game& game) {
    State& st = state();
    if (!st.source || game.id == 0) {
        return {};
    }
    lib::LibraryEntry e;
    e.id = game.id;
    e.claimed = game.homebrew;
    e.members.assign(game.members.begin(), game.members.end());
    if (e.members.empty()) e.members.push_back(game.id);
    lib::LibraryInput in{st.sessions, st.titles, st.claims, &st.names, st.source, st.profile.uid, static_cast<int64_t>(std::time(nullptr)), st.tz};
    return lib::buildDetails(in, e);
}

int timeZoneOffset() {
    return state().tz;
}

bool setPlaytimeAdjustment(uint64_t id, int seconds) {
    State& st = state();
    if (!st.source || !st.profile.uid.valid() || id == 0) {
        return false;
    }
    // A merged homebrew group may hold corrections on several members: keep only this one
    lib::LibraryInput in{st.sessions, st.titles, st.claims, &st.names, st.source, st.profile.uid, 0, 0};
    for (const lib::LibraryEntry& e : lib::buildLibrary(in, lib::SortBy::Playtime)) {
        if (e.id == id) {
            for (lib::TitleId m : e.members) st.adjust.set(m, st.profile.uid, 0);
        }
    }
    st.adjust.set(id, st.profile.uid, seconds);
    if (st.demo) {
        return true;
    }
    const bool ok = writeFile(dataDir() + "adjust.txt", st.adjust.serialize());
    logLine("playtime adjustment saved: %ld (%ld s)", ok ? 1 : 0, static_cast<long>(seconds));
    return ok;
}

bool setClaims(const std::vector<ClaimItem>& wanted, bool ignoreOthers) {
    State& st = state();
    if (!st.source || !st.profile.uid.valid()) {
        return false;
    }
    const lib::Uid uid = st.profile.uid;
    bool libraryChanged = false, storeChanged = false;
    for (const ClaimItem& item : wanted) {
        const bool has = st.claims.isClaimed(item.id, uid);
        if (item.claimed && !has) {
            st.claims.add(item.id, item.name, uid);
            libraryChanged = true;
        } else if (!item.claimed && has) {
            st.claims.remove(item.id, uid);
            st.claims.ignore(item.id, uid);     // turned off on purpose: don't offer it as new again
            libraryChanged = true;
        } else if (!item.claimed && ignoreOthers && !st.claims.isIgnored(item.id, uid)) {
            st.claims.ignore(item.id, uid);
            storeChanged = true;
        }
    }
    if (libraryChanged || storeChanged) {
        claimsChanged();
    }
    return libraryChanged;
}

std::string appDataDir() {
    return dataDir();
}

std::string readDataFile(const std::string& path) {
    return readFile(path);
}

bool writeDataFile(const std::string& path, const std::string& content) {
    return writeFile(path, content);
}

void appLog(const std::string& line) {
    std::string safe = line;
    for (char& c : safe) {
        if (c == '%') c = '_';      // logLine is printf-style
    }
    logLine(safe.c_str());
}

std::string gridPosterPathFor(uint64_t id) {
    return dataDir() + "grids/" + lib::titleHex(id) + ".jpg";
}

std::vector<PosterJob> posterJobs() {
    std::vector<PosterJob> jobs;
    const std::vector<Game>& games = mockGames();
    if (stressActive()) {
        return jobs;
    }
    for (const Game& g : games) {
        // The user's own poster wins; titles with a stored poster are done
        if (g.id == 0 || !g.artFile.empty() || g.name.rfind("Unknown title", 0) == 0) continue;
        jobs.push_back({g.id, g.name});
    }
    return jobs;
}

bool exportDump(std::string& pathOut) {
    State& st = state();
    if (!st.source) {
        return false;
    }
    HistorySource full(*st.source, st.demo ? st.source->events() : st.history.events());
    std::ostringstream out;
    lib::writeDump(full, out);
    pathOut = dataDir() + "dump.txt";
    return writeFile(pathOut, out.str());
}

#pragma once

#include <array>
#include <vector>

#include "lib/adjust.hpp"
#include "lib/claims.hpp"
#include "lib/history.hpp"
#include "lib/sessions.hpp"
#include "lib/source.hpp"
#include "lib/types.hpp"

namespace lib {

enum class SortBy { Playtime, LastPlayed, Name, Launches };

// One row of the library as the UI shows it
struct LibraryEntry {
    TitleId id = 0;                     // for merged homebrew: the oldest claimed title
    std::string name;
    bool installed = false;
    bool claimed = false;               // claimed title without a profile (homebrew)
    int64_t playSeconds = 0;           // recorded playtime plus the manual adjustment (never below 0)
    int64_t adjustSeconds = 0;         // the manual adjustment that is included
    uint32_t launches = 0;
    int64_t first = 0;
    int64_t last = 0;
    std::array<int, 7> week{};          // minutes per day over the last 7 days
    std::vector<TitleId> members;       // title IDs the numbers are made of (more than one when merged)
};

struct LibraryInput {
    const std::vector<Session>& sessions;
    const std::vector<TitleInfo>& titles;       // installed titles
    const ClaimStore& claims;
    const NameCache* names = nullptr;           // optional: names of titles that are gone
    PlaySource* source = nullptr;               // optional: system statistics (can fill gaps in the history)
    Uid profile;
    int64_t now = 0;
    int tzOffsetSeconds = 0;
    const AdjustmentStore* adjustments = nullptr;   // optional: manual playtime corrections
};

// Builds the library of one profile: its own titles (sessions with its account events) plus the
// titles it claimed (sessions regardless of account), with merged homebrew counted as one game.
std::vector<LibraryEntry> buildLibrary(const LibraryInput& in, SortBy sort);

// Installed titles that were played without ever being linked to a profile and that nobody
// has claimed or told us to ignore: candidates for the "new titles found" prompt.
std::vector<TitleId> suggestNewTitles(const std::vector<Session>& sessions, const std::vector<TitleInfo>& titles,
                                      const ClaimStore& claims, const Uid& profile);

// One row of the claim screen
struct ClaimCandidate {
    TitleId id = 0;
    std::string name;
    bool installed = false;
    bool claimed = false;       // by this profile
    int64_t playSeconds = 0;    // console-wide, from the sessions (or the snapshot once those are gone)
};

// Everything the claim screen lists for a profile, sorted by name: the titles it claimed (deleted
// ones too, from their snapshots) and the titles that never had a profile attached and are either
// installed or still have a known name.
std::vector<ClaimCandidate> claimCandidates(const std::vector<Session>& sessions, const std::vector<TitleInfo>& titles,
                                            const ClaimStore& claims, const NameCache* names, const Uid& profile);

// The profile to show at start: the one the app was launched with (forwarder with a preselected
// user), else the one used last time, else the first. Invalid or unknown IDs are skipped.
Uid chooseProfile(const std::vector<Profile>& profiles, const Uid& preselected, const Uid& remembered);

// Saves names and statistics of installed claimed titles into the claim store. Returns true if it changed.
// Asks the system for the names of played titles that aren't installed and that the cache doesn't
// know yet, and stores them in the cache. Returns the number of names learned.
size_t learnUninstalledNames(NameCache& names, const std::vector<RawEvent>& events, const std::vector<TitleInfo>& titles, PlaySource& source);

// Shown for a title whose name nobody knows anymore
std::string unknownTitleName(TitleId id);

// Default: every installed claimed title (call once per start). onlyMissing: only claims without a
// snapshot yet, installed or not (after the claims changed). buildLibrary reads only the snapshots.
bool refreshClaimSnapshots(ClaimStore& claims, const std::vector<TitleInfo>& titles, PlaySource& source, bool onlyMissing = false);

}  // namespace lib

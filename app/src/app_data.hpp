#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lib/details.hpp"
#include "mock_data.hpp"

// Loads the library for the UI: reads the console (or a recorded dump / demo data on a PC),
// merges the system's play events into the app's own history, keeps names and claim snapshots
// and returns the games of the current profile. The console is read once; later calls rebuild
// the list from the current claims. Data files live in /config/Playshelf/ (PC: ./data/).
std::vector<Game> loadLibraryGames();

// One row of the claim screens: a title without a profile that the current profile can adopt
struct ClaimItem {
    uint64_t id = 0;
    std::string name;
    bool installed = false;
    bool claimed = false;
    unsigned playSecs = 0;
    std::string iconFile;   // system icon, empty if there is none
};

// Name of the profile whose library is shown
std::string profileName();

// The console's profiles for the picker, with their totals
struct ProfileItem {
    std::string uid;            // 32 hex digits
    std::string name;
    std::string avatarFile;     // empty if there is no picture
    bool current = false;
    size_t games = 0;
    unsigned playSecs = 0;
};
std::vector<ProfileItem> profileItems();
size_t profileCount();

// Shows another profile's library from now on (remembered in profile.txt). Returns true if it
// changed; the game list must then be rebuilt.
bool switchProfile(const std::string& uidHex);

// Order of the library: by playtime (default) or by last played. Remembered in sort.txt.
// The game list must be rebuilt after a change.
bool sortByLastPlayed();
void setSortByLastPlayed(bool lastPlayed);

// Installed titles that were played without a profile and are neither claimed nor ignored yet
std::vector<ClaimItem> newTitleSuggestions();

// Every title the profile claimed or could claim (also ones that are no longer installed)
std::vector<ClaimItem> claimList();

// Makes the claims match `wanted` (items with claimed = false are released) and saves them.
// With ignoreOthers the unclaimed items are never offered as new again. Returns true if the
// library has to be rebuilt.
bool setClaims(const std::vector<ClaimItem>& wanted, bool ignoreOthers);

// Sessions, months and records of one game of the list (details page)
lib::GameDetails gameDetails(const Game& game);
int timeZoneOffset();       // seconds east of UTC, for showing dates

// Stores a manual playtime correction (seconds, may be negative, 0 removes it) for a title of the
// current profile and saves it. The game list must be rebuilt afterwards.
bool setPlaytimeAdjustment(uint64_t id, int seconds);

// The user's own name and homebrew marker per title ID, for all profiles (titles.txt).
// An empty name goes back to the system name. The game list must be rebuilt afterwards.
void setTitleName(uint64_t id, const std::string& name);
void setHomebrewMarker(uint64_t id, bool shown);

// Shared file helpers (also used by the poster download thread)
std::string appDataDir();                                   // /config/Playshelf/ (PC: data/)
std::string readDataFile(const std::string& path);
bool writeDataFile(const std::string& path, const std::string& content);   // safe replace
void appLog(const std::string& line);                       // one line in debug.log (thread-safe)
std::string gridPosterPathFor(uint64_t id);                 // grids/<TITLEID>.jpg

// Titles of the current list that have no poster yet, for the SteamGridDB lookup
struct PosterJob;
std::vector<PosterJob> posterJobs();

// Writes everything the app knows (profiles, titles, full history, statistics) to a dump file
// that can be loaded on a PC. Returns false if it couldn't be written; pathOut says where it went.
bool exportDump(std::string& pathOut);

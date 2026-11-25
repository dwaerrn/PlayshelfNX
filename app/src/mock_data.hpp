#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Fake library entry used by the PC prototype (the real app reads play data from the console)
struct Game {
    std::string name;
    unsigned playSecs;
    unsigned launches;
    int lastDaysAgo;
    int firstDaysAgo;
    std::string art;                // file in resources/img/art (demo data only; may be empty)
    bool homebrew;                  // claimed title without a user, shown with a badge
    std::array<int, 7> weekMinutes; // minutes played per day over the last week (oldest first)
    std::string artFile;            // optional image file on disk, used instead of the bundled art
    std::string iconFile;           // square system icon on disk, used when there is no poster
    uint64_t id = 0;                // title ID (0 for the built-in sample list)
    int adjustSecs = 0;             // manual correction included in playSecs
    std::vector<uint64_t> members;  // title IDs behind the entry (several for merged homebrew)
    std::string systemName;         // the name before the user's own one (titles.txt)
    bool hideBadge = false;         // the user switched the homebrew marker off for this title
};

const std::vector<Game>& mockGames();

// Rebuilds the list (after claims changed). Lists handed out earlier stay valid.
void reloadLibrary();

// True while stress.txt multiplies the library (debug aid)
bool stressActive();

std::string formatPlaytime(unsigned secs);
std::string formatAgo(int days);

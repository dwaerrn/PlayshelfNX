#include "mock_data.hpp"

#include <cstdio>
#include <deque>
#include <fstream>

#include "app_data.hpp"

static unsigned hm(unsigned h, unsigned m) { return h * 3600 + m * 60; }

// Optional stress mode: stress.txt contains "<count>" or "<count> small" (small = 300x450 posters)
static unsigned stressCount(bool& small) {
#ifdef __SWITCH__
    std::ifstream file("/config/Library/stress.txt");
#else
    std::ifstream file("stress.txt");
#endif
    unsigned n = 0;
    std::string word;
    small = false;
    file >> n;
    if (file >> word) {
        small = (word == "small");
    }
    return n;
}

static std::vector<Game> baseGames() {
    return {
        {"Starfall Odyssey",     hm(312, 20), 148, 1,  640, "starfall.png", false, {0, 95, 120, 0, 60, 180, 140}},
        {"Homebrew Emulator",    hm(88, 15),  64,  0,  500, "hb_emulator.png", true, {30, 0, 45, 20, 0, 90, 35}},
        {"Hollow Grove",         hm(188, 5),  96,  3,  410, "grove.png", false, {0, 0, 150, 90, 0, 0, 0}},
        {"Crimson Tide Racing",  hm(74, 40),  61,  9,  300, "racing.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Kitchen Chaos",        hm(52, 10),  40,  14, 250, "kitchen.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Pixel Dungeon",        hm(41, 55),  52,  2,  200, "dungeon.png", false, {45, 0, 30, 0, 0, 75, 0}},
        {"Skyward Isles",        hm(36, 30),  22,  21, 180, "isles.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Neon Drift",           hm(29, 15),  33,  5,  150, "neon.png", false, {0, 0, 0, 55, 25, 0, 0}},
        {"Wildwood Tales",       hm(24, 50),  18,  40, 400, "wildwood.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Iron Harbor",          hm(19, 5),   12,  55, 330, "harbor.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Echoes of Dawn",       hm(14, 20),  9,   70, 120, "dawn.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Rune Keeper",          hm(9, 45),   7,   90, 100, "rune.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Turbo Kart Cup",       hm(6, 30),   11,  4,  60,  "kart.png", false, {0, 0, 0, 20, 0, 0, 40}},
        {"Moonlit Farm",         hm(3, 10),   4,   120, 130, "farm.png", false, {0, 0, 0, 0, 0, 0, 0}},
        {"Save Manager",         hm(2, 5),    20,  7,  300, "hb_saves.png", true, {0, 0, 0, 0, 0, 0, 0}},
    };
}

static bool g_stressActive = false;

bool stressActive() { return g_stressActive; }

static std::vector<Game> buildGames() {
    std::vector<Game> base = loadLibraryGames();
    if (base.empty()) {
        base = baseGames();
    }
    bool small = false;
    unsigned n = stressCount(small);
    if (n <= base.size()) {
        return base;
    }
    g_stressActive = true;
    std::vector<Game> out;
    for (unsigned i = 0; i < n; i++) {
        Game g = base[i % base.size()];
#ifdef __SWITCH__
        const std::string dir = small ? "/config/Library/stress_art_small/" : "/config/Library/stress_art/";
#else
        const std::string dir = small ? "stress_art_small/" : "stress_art/";
#endif
        char file[32];
        std::snprintf(file, sizeof(file), "s%03u.jpg", i % 600);
        if (std::ifstream(dir + file).good()) {
            g.artFile = dir + file;
        }
        unsigned round = i / base.size();
        if (round > 0) {
            g.name += " " + std::to_string(round + 1);
            g.playSecs /= (round + 1);
        }
        out.push_back(g);
    }
    return out;
}

// Every list that was ever handed out stays alive: views keep references to their games, and an
// old screen may still be around for a moment after a reload.
static std::deque<std::vector<Game>>& generations() {
    static std::deque<std::vector<Game>> all;
    return all;
}

const std::vector<Game>& mockGames() {
    if (generations().empty()) {
        generations().push_back(buildGames());
    }
    return generations().back();
}

void reloadLibrary() {
    generations().push_back(buildGames());
}

std::string formatPlaytime(unsigned secs) {
    unsigned h = secs / 3600;
    unsigned m = (secs % 3600) / 60;
    if (h == 0) {
        return std::to_string(m) + "m";
    }
    return std::to_string(h) + "h " + std::to_string(m) + "m";
}

std::string formatAgo(int days) {
    if (days <= 0) {
        return "Today";
    }
    if (days == 1) {
        return "Yesterday";
    }
    if (days < 60) {
        return std::to_string(days) + " days ago";
    }
    return std::to_string(days / 30) + " months ago";
}

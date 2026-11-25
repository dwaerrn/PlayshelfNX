#pragma once

#include <iosfwd>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "lib/types.hpp"

namespace lib {

// Where play data comes from: the console (platform/switch), a recorded dump or generated test data
class PlaySource {
  public:
    virtual ~PlaySource() = default;

    virtual std::vector<Profile> profiles() = 0;
    virtual std::vector<TitleInfo> titles() = 0;      // installed titles
    virtual std::vector<RawEvent> events() = 0;       // the system's whole play event log
    virtual std::optional<SystemStats> userStats(TitleId, const Uid&) = 0;
    virtual std::optional<SystemStats> consoleStats(TitleId) = 0;

    // Name of a title that is no longer installed, from the system's own control data cache
    // (the same data the user page's play activity uses). Nothing if the system forgot it.
    virtual std::optional<TitleInfo> uninstalledTitle(TitleId) { return std::nullopt; }
};

// Data held in memory. Used by tests, by the demo data and for recorded dumps.
class MemorySource : public PlaySource {
  public:
    std::vector<Profile> profileList;
    std::vector<TitleInfo> titleList;
    std::vector<RawEvent> eventList;
    std::map<std::pair<TitleId, Uid>, SystemStats> userStatList;
    std::map<TitleId, SystemStats> consoleStatList;
    std::map<TitleId, std::string> cachedNames;     // what uninstalledTitle() answers

    std::vector<Profile> profiles() override { return profileList; }
    std::vector<TitleInfo> titles() override { return titleList; }
    std::vector<RawEvent> events() override { return eventList; }
    std::optional<SystemStats> userStats(TitleId, const Uid&) override;
    std::optional<SystemStats> consoleStats(TitleId) override;
    std::optional<TitleInfo> uninstalledTitle(TitleId) override;

    // Appends one launch: Launch, optional account events, one focus interval per entry of
    // focusSeconds (5 s apart), then Exit (omitted when crash is true). Returns the last clock used.
    int64_t addSession(TitleId, const Uid& user, int64_t startClock, const std::vector<int64_t>& focusSeconds, bool crash = false);

    // Recomputes the system statistics from the events, like the console does: per profile for
    // sessions with an account event, console-wide for installed titles only.
    void deriveStats();
};

// Recorder format (text). A dump of a real console lets the PC build run on real data.
void writeDump(PlaySource& source, std::ostream& out);
bool readDump(std::istream& in, MemorySource& out);

// Deterministic demo library: two profiles, ~30 games, homebrew (a reinstalled forwarder,
// a deleted one) and a few months of play history ending at `now`.
MemorySource makeDemoSource(int64_t now);

}  // namespace lib

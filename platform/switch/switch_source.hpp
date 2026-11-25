#pragma once

#include "lib/source.hpp"

namespace lib {

// Reads play data from the console (libnx): profiles, installed titles with their names and icons,
// the system's play event log and its play statistics. Switch builds only.
class SwitchSource : public PlaySource {
  public:
    SwitchSource();
    ~SwitchSource() override;

    std::vector<Profile> profiles() override;
    std::vector<TitleInfo> titles() override;
    std::vector<RawEvent> events() override;
    std::optional<SystemStats> userStats(TitleId, const Uid&) override;
    std::optional<SystemStats> consoleStats(TitleId) override;
    std::optional<TitleInfo> uninstalledTitle(TitleId) override;

    // Title ID of the running program (the forwarder), so the app can leave itself out of the library
    static TitleId ownTitleId();

    // Profile that launched the app through a forwarder with a preselected user, if any
    static Uid preselectedUser();

    // The console's time zone offset from UTC in seconds (0 if it can't be read)
    static int utcOffsetSeconds();

  private:
    int language_ = 0;   // NACP language index of the system language
};

}  // namespace lib

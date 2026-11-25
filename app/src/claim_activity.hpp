#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "app_data.hpp"

// The two claim screens. Homebrew is launched without a profile, so the system never puts it
// in anybody's play activity; here the profile adopts ("claims") such titles.
//   NewTitles: shown at start when unclaimed titles were found. Claim takes the ticked ones
//              and never offers the others again; Later changes nothing.
//   Manage:    every title that is or could be claimed, as a list of switches.
class ClaimActivity : public brls::Activity {
  public:
    enum class Mode { NewTitles, Manage };

    // onClose runs after the screen is gone; libraryChanged says whether the game list is stale
    ClaimActivity(Mode mode, std::function<void(bool libraryChanged)> onClose);

    brls::View* createContentView() override;

  private:
    Mode mode_;
    std::function<void(bool)> onClose_;
    std::vector<ClaimItem> items_;
    bool closing_ = false;

    void addCells(brls::Box* list, bool installed);
    void close(bool apply);
};

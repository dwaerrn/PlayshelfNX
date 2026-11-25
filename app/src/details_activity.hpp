#pragma once

#include <borealis.hpp>
#include <functional>

#include "lib/details.hpp"
#include "mock_data.hpp"

// Details of one game: poster, totals and records, playtime per month and by weekday, and the
// most recent sessions. Y adjusts the playtime, − changes the poster.
class DetailsActivity : public brls::Activity {
  public:
    // onClose runs after the screen is gone; changed says whether the game list is stale
    DetailsActivity(const Game& game, std::function<void(bool changed)> onClose);

    brls::View* createContentView() override;

  private:
    Game game_;     // a copy: the library list may be rebuilt while this screen is open
    lib::GameDetails details_;
    int tz_;
    std::function<void(bool)> onClose_;
    bool closing_ = false;
    bool changed_ = false;              // renamed or marker switched: the library must be rebuilt
    brls::Label* nameLabel_ = nullptr;
    class GameCard* poster_ = nullptr;

    void rename();
    void toggleMarker();

    brls::View* buildHeader();
    brls::View* buildCharts();
    brls::View* buildSessions();
    void subScreenClosed(bool changed);
    void close(bool changed);
};

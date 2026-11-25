#pragma once

#include <borealis.hpp>
#include <functional>

#include "mock_data.hpp"

// "Adjust playtime": the total playtime of one title is set in hours and minutes. The difference
// to the recorded playtime is saved as a correction; the recorded data itself is never changed.
class AdjustActivity : public brls::Activity {
  public:
    // onClose runs after the screen is gone; changed says whether the game list is stale
    AdjustActivity(const Game& game, std::function<void(bool changed)> onClose);

    brls::View* createContentView() override;

  private:
    uint64_t id_;
    std::string name_;
    long recorded_;     // seconds without any correction
    long total_;        // seconds being edited
    long original_;     // total when the screen opened
    std::function<void(bool)> onClose_;
    bool closing_ = false;

    brls::Label* totalLabel_ = nullptr;
    brls::Label* diffLabel_ = nullptr;
    brls::DetailCell* hoursCell_ = nullptr;
    brls::DetailCell* minutesCell_ = nullptr;

    void step(long seconds);
    void refresh();
    void close(bool save);
};

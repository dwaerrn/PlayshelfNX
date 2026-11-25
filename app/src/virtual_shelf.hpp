#pragma once

#include <borealis.hpp>
#include <deque>
#include <functional>
#include <vector>

#include "game_card.hpp"
#include "mock_data.hpp"

// Horizontal shelf of cards that only keeps the cards near the focus alive.
// The rest of the row is represented by two empty spacers, so the scroll range and the
// positions of the live cards never change. Draw calls and texture memory stay bounded
// no matter how many games there are.
class VirtualShelf : public brls::HScrollingFrame {
  public:
    using FocusCallback = std::function<void(size_t index, const Game& game)>;

    VirtualShelf(const std::vector<Game>& games, float cardW, float cardH, float gap,
                 GameCard::Style style, FocusCallback onFocus);

    size_t liveCards() const { return cards_.size(); }
    size_t focusedIndex() const { return focused_; }

    // Moves the focus to a card (creates it if needed)
    void focusIndex(size_t index);

    // Calls fn for every card that currently exists
    void forEachCard(const std::function<void(GameCard*)>& fn) {
        for (GameCard* c : cards_) fn(c);
    }

  private:
    static constexpr int kBehind = 6; // cards kept to the left of the focus
    static constexpr int kAhead = 8;  // cards kept to the right of the focus

    const std::vector<Game>& games_;
    float cardW_, cardH_, gap_;
    GameCard::Style style_;
    FocusCallback onFocus_;

    brls::Box* row_ = nullptr;
    brls::Box* leftSpacer_ = nullptr;
    brls::Box* rightSpacer_ = nullptr;
    std::deque<GameCard*> cards_;
    int first_ = 0;
    int last_ = -1;
    size_t focused_ = 0;

    GameCard* makeCard(int index);
    void ensureWindow(int focusIndex);
    void updateSpacers();
};

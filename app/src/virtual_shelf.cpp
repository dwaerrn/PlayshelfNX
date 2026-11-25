#include "virtual_shelf.hpp"

#include <algorithm>

VirtualShelf::VirtualShelf(const std::vector<Game>& games, float cardW, float cardH, float gap,
                           GameCard::Style style, FocusCallback onFocus)
    : brls::HScrollingFrame(), games_(games), cardW_(cardW), cardH_(cardH), gap_(gap), style_(style), onFocus_(std::move(onFocus))
{
    row_ = new brls::Box(brls::Axis::ROW);
    // Room above and below for the focused card, which is drawn a little larger
    const float pad = cardH_ * (GameCard::kFocusScale - 1.0f) / 2 + 8;
    row_->setPadding(pad, 40, pad, 48);

    leftSpacer_ = new brls::Box(brls::Axis::ROW);
    leftSpacer_->setShrink(0);
    leftSpacer_->setHeight(cardH_);
    row_->addView(leftSpacer_);

    rightSpacer_ = new brls::Box(brls::Axis::ROW);
    rightSpacer_->setShrink(0);
    rightSpacer_->setHeight(cardH_);
    row_->addView(rightSpacer_);

    this->setContentView(row_);
    this->setHeight(cardH_ + 2 * pad);

    if (!games_.empty()) {
        ensureWindow(0);
    }
}

GameCard* VirtualShelf::makeCard(int index) {
    GameCard* card = new GameCard(games_[index], cardW_, cardH_, style_);
    card->setShrink(0);
    card->setMargins(0, gap_, 0, 0);
    card->getFocusEvent()->subscribe([this, index](brls::View*) {
        focused_ = index;
        ensureWindow(index);
        if (onFocus_) {
            onFocus_(index, games_[index]);
        }
    });
    return card;
}

void VirtualShelf::updateSpacers() {
    const float stride = cardW_ + gap_;
    const int total = static_cast<int>(games_.size());
    leftSpacer_->setWidth(first_ * stride);
    rightSpacer_->setWidth(std::max(0, total - 1 - last_) * stride);
}

void VirtualShelf::ensureWindow(int focusIndex) {
    const int total = static_cast<int>(games_.size());
    const int wantFirst = std::max(0, focusIndex - kBehind);
    const int wantLast = std::min(total - 1, focusIndex + kAhead);

    if (last_ < first_) {
        // First use: start the window at the wanted position
        first_ = wantFirst;
        last_ = wantFirst - 1;
    }

    // Cards entering on the right (before the right spacer)
    while (last_ < wantLast) {
        last_++;
        GameCard* card = makeCard(last_);
        row_->addView(card, 1 + cards_.size());
        cards_.push_back(card);
    }

    // Cards entering on the left (after the left spacer)
    while (first_ > wantFirst) {
        first_--;
        GameCard* card = makeCard(first_);
        row_->addView(card, 1);
        cards_.push_front(card);
    }

    // Cards leaving on the left / right (never the focused one)
    while (first_ < wantFirst && !cards_.empty()) {
        row_->removeView(cards_.front());
        cards_.pop_front();
        first_++;
    }
    while (last_ > wantLast && !cards_.empty()) {
        row_->removeView(cards_.back());
        cards_.pop_back();
        last_--;
    }

    updateSpacers();
}

void VirtualShelf::focusIndex(size_t index) {
    if (index >= games_.size()) {
        return;
    }
    ensureWindow(static_cast<int>(index));
    GameCard* card = cards_[index - first_];
    brls::Application::giveFocus(card);
}

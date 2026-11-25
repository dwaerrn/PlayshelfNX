#pragma once

#include <borealis.hpp>
#include <memory>

#include "mock_data.hpp"

// A tall poster card: boxart, optional playtime overlay and a badge for homebrew
class GameCard : public brls::Box {
  public:
    enum class Style {
        Full,    // playtime + last played
        Compact, // playtime only
        Bare,    // poster only
    };

    GameCard(const Game& game, float width, float height, Style style);

    const Game& game() const { return game_; }

    // Replaces the icon/name look with a poster that fades in (a download just finished)
    void showPoster(const std::string& path);

    // Posters downloaded while the app runs, by title ID (used for cards created later)
    static void rememberPoster(uint64_t id, const std::string& path);
    static void forgetPoster(uint64_t id);

    // Shows or hides the homebrew marker (only homebrew cards have one)
    void setBadgeVisible(bool visible);

    // Focus look: the card grows a little and gets a white border (instead of Borealis' highlight)
    static constexpr float kFocusScale = 1.06f;
    void frame(brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

  private:
    const Game& game_;
    float width_, height_;
    brls::Image* poster_ = nullptr;
    brls::Image* icon_ = nullptr;
    brls::Label* name_ = nullptr;
    brls::Box* badge_ = nullptr;
    brls::Animatable fade_ = 1.0f;
    brls::Animatable zoom_ = 0.0f;     // 0 = normal, 1 = focused
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);   // background image loads check it

    void fadeIn(brls::Image* image);

    void animateZoom(float to);

    brls::Image* makePoster(const std::string& file, bool fromRes);
};

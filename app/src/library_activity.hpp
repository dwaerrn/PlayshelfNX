#pragma once

#include <borealis.hpp>
#include <string>

class VirtualShelf;
struct Game;

// The library: a shelf of cards with a detail strip below
class LibraryActivity : public brls::Activity {
  public:
    LibraryActivity() = default;

    brls::View* createContentView() override;

    // Shown on top of the library at start if there are new titles to claim
    void showNewTitlesPrompt();

    // Starts the SteamGridDB downloads (if there is a key); cards update as posters arrive
    void startPosterDownloads();

  private:
    brls::Box* holder_ = nullptr;
    brls::AppletFrame* frame_ = nullptr;
    brls::View* content_ = nullptr;
    const Game* focused_ = nullptr;   // card with the focus (for A = adjust playtime)
    brls::Label* titleLabel_ = nullptr;
    brls::Label* subtitleLabel_ = nullptr;
    std::string totalsText_;
    VirtualShelf* shelf_ = nullptr;

    brls::View* buildContent();
    void rebuild();                 // new game list (after claims changed)
    void claimScreenClosed(bool libraryChanged);   // also used after a playtime adjustment
    void posterArrived(uint64_t id, const std::string& path);
    void updateTitle();                     // "Playshelf - <profile>" when there are several profiles
    void profileChosen(bool changed);

    brls::View* buildShelf();
};

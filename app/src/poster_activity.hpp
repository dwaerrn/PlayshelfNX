#pragma once

#include <borealis.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "mock_data.hpp"
#include "poster_fetch.hpp"

class VirtualShelf;

// "Change poster": the SteamGridDB posters of the matched game as a row of previews, plus
// "Use system icon". Y switches to another game (search results or a typed name).
class ChangePosterActivity : public brls::Activity {
  public:
    // onClose runs after the screen is gone; changed says whether the game list is stale
    ChangePosterActivity(const Game& game, std::function<void(bool changed)> onClose);

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    uint64_t titleId_;
    std::string titleName_;
    std::string iconFile_;
    bool ownArt_;                       // the user's art/ file wins anyway
    std::function<void(bool)> onClose_;
    PosterBrowser browser_;

    int64_t gameId_ = 0;
    std::string gameName_;
    int64_t currentGrid_ = 0;
    std::vector<PosterBrowser::Choice> choices_;
    std::deque<std::vector<Game>> lists_;   // cards reference these; old ones stay until the screen closes
    VirtualShelf* shelf_ = nullptr;
    brls::Box* shelfHolder_ = nullptr;
    brls::Label* gameLabel_ = nullptr;
    brls::Label* status_ = nullptr;
    bool busy_ = false;
    bool closing_ = false;

    void findGame(const std::string& term);
    void useGame(int64_t id, const std::string& name);
    void showChoices();
    void pick();
    void otherGame();
    void close(bool changed);
};

// Search results for "Other game", with a keyboard search
class GameSearchActivity : public brls::Activity {
  public:
    GameSearchActivity(PosterBrowser& browser, std::string term, std::function<void(int64_t id, const std::string& name)> onPick);

    brls::View* createContentView() override;

  private:
    PosterBrowser& browser_;
    std::string term_;
    std::function<void(int64_t, const std::string&)> onPick_;
    brls::Box* list_ = nullptr;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    void run(const std::string& term);
    void fill(bool ok, const std::vector<PosterBrowser::GameInfo>& games);
};

#include "poster_activity.hpp"
#include "ui_style.hpp"

#include "game_card.hpp"
#include "virtual_shelf.hpp"

namespace {

const NVGcolor kText = nvgRGB(200, 205, 214);
const NVGcolor kMuted = nvgRGB(150, 156, 168);
constexpr float kThumbW = 150, kThumbH = 225;

brls::Label* textLabel(const std::string& text, float size, NVGcolor color) {
    brls::Label* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    return label;
}

}  // namespace

// ---- Change poster ----

ChangePosterActivity::ChangePosterActivity(const Game& game, std::function<void(bool)> onClose)
    : titleId_(game.id), titleName_(game.name), iconFile_(game.iconFile), onClose_(std::move(onClose)) {
    ownArt_ = game.artFile.find("/art/") != std::string::npos || game.artFile.rfind("art/", 0) == 0;
    const PosterMemo memo = posterMemo(titleId_);
    gameId_ = memo.gameId;
    gameName_ = memo.gameName;
    currentGrid_ = memo.gridId;
}

brls::View* ChangePosterActivity::createContentView() {
    brls::Box* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(16, 60, 10, 60);

    brls::Label* name = textLabel(titleName_, 28, nvgRGB(255, 255, 255));
    name->setSingleLine(true);
    box->addView(name);
    // Fixed widths: these texts change after the first layout
    gameLabel_ = textLabel(" ", 17, kText);
    gameLabel_->setWidth(1160);
    gameLabel_->setSingleLine(true);
    gameLabel_->setMargins(6, 0, 0, 0);
    box->addView(gameLabel_);
    status_ = textLabel(" ", 16, kMuted);
    status_->setWidth(1160);
    status_->setSingleLine(true);
    status_->setMargins(4, 0, 6, 0);
    box->addView(status_);
    if (ownArt_) {
        brls::Label* note = textLabel("Your own file in art/ is shown for this title. Delete it to use a poster from here.", 16, nvgRGB(230, 190, 90));
        note->setMargins(0, 0, 4, 0);
        box->addView(note);
    }

    shelfHolder_ = new brls::Box(brls::Axis::COLUMN);
    shelfHolder_->setMargins(0, -60, 0, -60);   // previews run to the screen edges
    box->addView(shelfHolder_);

    brls::AppletFrame* frame = ui::frame(box);
    frame->setTitle("Change poster");
    frame->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(false); return true; });
    frame->registerAction("Other game", brls::ControllerButton::BUTTON_Y, [this](brls::View*) { otherGame(); return true; });
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Y), [this](brls::View*) { otherGame(); return true; });
    return frame;
}

void ChangePosterActivity::onContentAvailable() {
    if (steamGridKey().empty()) {
        gameLabel_->setText("SteamGridDB needs your API key in steamgriddb.txt");
        showChoices();
        return;
    }
    if (gameId_ != 0) {
        useGame(gameId_, gameName_);
    } else {
        findGame(titleName_);
    }
}

void ChangePosterActivity::findGame(const std::string& term) {
    gameLabel_->setText("Searching SteamGridDB for \"" + term + "\"...");
    status_->setText("");
    browser_.search(term, [this, term](bool ok, std::vector<PosterBrowser::GameInfo> games, std::string message) {
        if (!ok) {
            gameLabel_->setText("SteamGridDB: " + message);
            showChoices();
        } else if (games.empty()) {
            gameLabel_->setText("No game called \"" + term + "\" on SteamGridDB. Press Y to search for another name.");
            showChoices();
        } else {
            useGame(games.front().id, games.front().name);
        }
    });
}

void ChangePosterActivity::useGame(int64_t id, const std::string& name) {
    gameId_ = id;
    gameName_ = name;
    gameLabel_->setText("SteamGridDB: " + (name.empty() ? "game " + std::to_string(id) : name) + "   (Y: other game)");
    status_->setText("Loading posters...");
    browser_.cancelPreviews();
    choices_.clear();
    showChoices();
    browser_.listPosters(id, [this](bool ok, std::vector<PosterBrowser::Choice> posters, std::string message) {
        if (!ok) {
            status_->setText("SteamGridDB: " + message);
            return;
        }
        choices_ = std::move(posters);
        status_->setText(choices_.empty() ? "This game has no portrait posters." : std::to_string(choices_.size()) + " posters");
        showChoices();
    });
}

void ChangePosterActivity::showChoices() {
    // Card 0 is "Use system icon"; the others are the posters
    std::vector<Game> cards;
    Game icon{};
    icon.name = "Use system icon";
    icon.iconFile = iconFile_;
    cards.push_back(icon);
    size_t focus = 0;
    for (size_t i = 0; i < choices_.size(); i++) {
        Game g{};
        g.name = "Loading";
        cards.push_back(g);
        if (choices_[i].id == currentGrid_) focus = i + 1;
    }
    lists_.push_back(std::move(cards));
    std::vector<Game>& list = lists_.back();

    VirtualShelf* old = shelf_;
    shelf_ = new VirtualShelf(list, kThumbW, kThumbH, 18, GameCard::Style::Bare, [this](size_t index, const Game&) {
        if (index == 0) {
            status_->setText("No downloaded poster: the system icon is shown");
        } else if (index <= choices_.size()) {
            status_->setText("Poster " + std::to_string(index) + " of " + std::to_string(choices_.size()) +
                             (choices_[index - 1].id == currentGrid_ ? "  (current)" : ""));
        }
    });
    shelf_->registerAction("Use this", brls::ControllerButton::BUTTON_A, [this](brls::View*) { pick(); return true; });
    shelfHolder_->addView(shelf_);
    shelf_->focusIndex(focus);
    if (old) {
        shelfHolder_->removeView(old);
    }

    // Previews, in order, starting with the one in focus
    std::vector<size_t> order;
    for (size_t i = focus > 0 ? focus - 1 : 0; i < choices_.size(); i++) order.push_back(i);
    for (size_t i = 0; i + 1 < focus && i < choices_.size(); i++) order.push_back(i);
    VirtualShelf* shelf = shelf_;
    for (size_t i : order) {
        browser_.preview(choices_[i], [this, shelf, &list, i](std::string path) {
            if (shelf != shelf_) return;    // the list was replaced meanwhile
            Game& g = list[i + 1];
            if (path.empty()) {
                g.name = "No preview";
                return;
            }
            g.artFile = path;
            shelf_->forEachCard([&](GameCard* card) {
                if (&card->game() == &g) card->showPoster(path);
            });
        });
    }
}

void ChangePosterActivity::pick() {
    if (busy_ || !shelf_) {
        return;
    }
    const size_t index = shelf_->focusedIndex();
    if (index == 0) {
        hideDownloadedPoster(titleId_);
        GameCard::forgetPoster(titleId_);
        close(true);
        return;
    }
    if (index > choices_.size()) {
        return;
    }
    busy_ = true;
    status_->setText("Downloading the poster...");
    browser_.choose(titleId_, gameId_, gameName_, choices_[index - 1], [this](bool ok, std::string message) {
        busy_ = false;
        if (!ok) {
            status_->setText("Could not use this poster: " + message);
            return;
        }
        GameCard::forgetPoster(titleId_);
        close(true);
    });
}

void ChangePosterActivity::otherGame() {
    if (steamGridKey().empty() || busy_) {
        return;
    }
    brls::Application::pushActivity(new GameSearchActivity(browser_, gameName_.empty() ? titleName_ : gameName_,
                                                           [this](int64_t id, const std::string& name) { useGame(id, name); }));
}

void ChangePosterActivity::close(bool changed) {
    if (closing_) {
        return;
    }
    closing_ = true;
    std::function<void(bool)> done = onClose_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, changed]() {
        if (done) {
            done(changed);
        }
    });
}

// ---- Other game ----

GameSearchActivity::GameSearchActivity(PosterBrowser& browser, std::string term, std::function<void(int64_t, const std::string&)> onPick)
    : browser_(browser), term_(std::move(term)), onPick_(std::move(onPick)) { }

brls::View* GameSearchActivity::createContentView() {
    list_ = new brls::Box(brls::Axis::COLUMN);
    list_->setPadding(10, 80, 30, 80);
    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setContentView(list_);
    scroll->setGrow(1.0f);

    brls::AppletFrame* frame = ui::frame(scroll);
    frame->setTitle("Other game");
    frame->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity(brls::TransitionAnimation::FADE);
        return true;
    });
    run(term_);
    return frame;
}

void GameSearchActivity::run(const std::string& term) {
    term_ = term;
    fill(true, {});
    std::weak_ptr<int> alive = alive_;
    browser_.search(term, [this, alive](bool ok, std::vector<PosterBrowser::GameInfo> games, std::string) {
        if (alive.lock()) fill(ok, games);
    });
}

void GameSearchActivity::fill(bool ok, const std::vector<PosterBrowser::GameInfo>& games) {
    brls::View* focusTarget = nullptr;
    list_->clearViews();

    brls::DetailCell* typed = new brls::DetailCell();
    typed->setText("Search by name...");
    typed->setDetailText(term_);
    typed->registerClickAction([this](brls::View*) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            if (!text.empty()) run(text);
        }, "Search SteamGridDB", "", 64, term_);
        return true;
    });
    list_->addView(typed);
    focusTarget = typed;

    brls::Label* info = new brls::Label();
    info->setFontSize(16);
    info->setTextColor(kMuted);
    info->setMargins(14, 12, 6, 12);
    list_->addView(info);
    if (!ok) {
        info->setText("SteamGridDB could not be reached (see debug.log).");
    } else if (games.empty()) {
        info->setText("Results for \"" + term_ + "\" appear here.");
    } else {
        info->setText(std::to_string(games.size()) + " games found for \"" + term_ + "\"");
    }

    for (const PosterBrowser::GameInfo& g : games) {
        brls::DetailCell* cell = new brls::DetailCell();
        cell->setText(g.name);
        cell->setDetailText("#" + std::to_string(g.id));
        const int64_t id = g.id;
        const std::string name = g.name;
        cell->registerClickAction([this, id, name](brls::View*) {
            auto pickCb = onPick_;
            brls::Application::popActivity(brls::TransitionAnimation::FADE, [pickCb, id, name] { pickCb(id, name); });
            return true;
        });
        list_->addView(cell);
        if (focusTarget == typed) focusTarget = cell;
    }
    brls::Application::giveFocus(focusTarget);
}

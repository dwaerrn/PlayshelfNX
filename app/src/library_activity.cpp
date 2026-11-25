#include "library_activity.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <functional>
#include <numeric>

#include "app_data.hpp"
#include "adjust_activity.hpp"
#include "claim_activity.hpp"
#include "details_activity.hpp"
#include "profile_activity.hpp"
#include "poster_activity.hpp"
#include "poster_fetch.hpp"
#include "game_card.hpp"
#include "mock_data.hpp"
#include "virtual_shelf.hpp"
#include "image_loader.hpp"
#include "ui_style.hpp"

namespace {

// Bottom panel: icon, name, one line of numbers and the 7-day chart of the focused card
struct DetailPanel {
    brls::Box* root = nullptr;
    std::function<void(const Game&)> update;
};

// Day letters for the last seven days, oldest first (today last)
std::array<std::string, 7> dayLetters() {
    static const char* names[] = {"S", "M", "T", "W", "T", "F", "S"};
    const std::time_t now = std::time(nullptr) + timeZoneOffset();
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::array<std::string, 7> out;
    for (int i = 0; i < 7; i++) out[i] = names[(tm.tm_wday + 1 + i) % 7];
    return out;
}

DetailPanel buildDetailPanel() {
    DetailPanel panel;
    brls::Box* root = ui::panel(brls::Axis::ROW);
    root->setShrink(0);
    root->setHeight(128);
    root->setPadding(0, 28, 0, 20);
    root->setAlignItems(brls::AlignItems::CENTER);
    panel.root = root;

    // Square system icon (hidden when there is none)
    brls::Image* icon = new brls::Image();
    icon->setWidth(88);
    icon->setHeight(88);
    icon->setShrink(0);
    icon->setCornerRadius(ui::kCardRadius);
    icon->setScalingType(brls::ImageScalingType::FILL);
    icon->setMargins(0, 20, 0, 0);
    root->addView(icon);

    brls::Box* info = new brls::Box(brls::Axis::COLUMN);
    info->setGrow(1.0f);
    info->setShrink(1.0f);
    brls::Label* title = ui::bold("", 28);
    title->setMargins(0, 0, 6, 0);
    info->addView(title);
    brls::Box* line = new brls::Box(brls::Axis::ROW);
    line->setAlignItems(brls::AlignItems::FLEX_END);
    brls::Label* play = ui::bold("", 22, ui::kAccent);
    line->addView(play);
    brls::Label* rest = ui::label("", 17, ui::kMuted);
    rest->setMargins(0, 0, 2, 16);
    line->addView(rest);
    info->addView(line);
    root->addView(info);

    // Minutes per day over the last week, day letters below
    const float chartMax = 50.0f;
    brls::Box* chart = new brls::Box(brls::Axis::COLUMN);
    chart->setShrink(0);
    chart->setMargins(0, 0, 0, 24);
    brls::Box* bars = new brls::Box(brls::Axis::ROW);
    bars->setHeight(chartMax);
    bars->setAlignItems(brls::AlignItems::FLEX_END);
    brls::Box* letters = new brls::Box(brls::Axis::ROW);
    letters->setMargins(6, 0, 0, 0);
    std::array<brls::Box*, 7> barViews{};
    const std::array<std::string, 7> days = dayLetters();
    for (int i = 0; i < 7; i++) {
        barViews[i] = new brls::Box(brls::Axis::COLUMN);
        barViews[i]->setWidth(18);
        barViews[i]->setHeight(3);
        barViews[i]->setCornerRadius(2);
        barViews[i]->setMargins(0, i < 6 ? 8 : 0, 0, 0);
        bars->addView(barViews[i]);
        brls::Label* d = ui::label(days[i], 13, i == 6 ? ui::kText : ui::kMuted);
        d->setWidth(18);
        d->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        d->setMargins(0, i < 6 ? 8 : 0, 0, 0);
        letters->addView(d);
    }
    chart->addView(bars);
    chart->addView(letters);
    root->addView(chart);

    // A new token per focused game: a late icon of the previous one is dropped
    auto token = std::make_shared<std::shared_ptr<char>>();
    panel.update = [=](const Game& g) {
        *token = std::make_shared<char>(0);
        if (!g.iconFile.empty()) {
            icon->setVisibility(brls::Visibility::VISIBLE);
            if (!imageload::show(icon, g.iconFile, 96, 96, *token, [icon] { icon->setAlpha(1.0f); })) {
                icon->setAlpha(0.0f);
            }
        } else {
            icon->setVisibility(brls::Visibility::GONE);
        }
        title->setText(g.name);
        play->setText(formatPlaytime(g.playSecs) + (g.adjustSecs != 0 ? "*" : ""));
        std::string ago = formatAgo(g.lastDaysAgo);
        if (!ago.empty()) ago[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(ago[0])));
        rest->setText("played " + ago + ", " + std::to_string(g.launches) + (g.launches == 1 ? " launch" : " launches"));
        int maxMin = std::max(1, *std::max_element(g.weekMinutes.begin(), g.weekMinutes.end()));
        for (int i = 0; i < 7; i++) {
            const int m = g.weekMinutes[i];
            barViews[i]->setHeight(std::max(3.0f, chartMax * m / maxMin));
            barViews[i]->setBackgroundColor(m == 0 ? ui::kFaint : ui::kAccent);
        }
    };
    return panel;
}

} // namespace

brls::View* LibraryActivity::buildShelf() {
    const std::vector<Game>& games = mockGames();
    unsigned total = std::accumulate(games.begin(), games.end(), 0u, [](unsigned a, const Game& g) { return a + g.playSecs; });

    ui::Hints hints = {{brls::ControllerButton::BUTTON_A, "Details"}, {brls::ControllerButton::BUTTON_X, "Claim titles"}};
    if (profileCount() > 1) hints.push_back({brls::ControllerButton::BUTTON_LB, "Profile"});
    hints.push_back({brls::ControllerButton::BUTTON_RB, sortByLastPlayed() ? "By playtime" : "By last played"});
    brls::Box* top = ui::topBar(titleLabel_, subtitleLabel_, hints);
    totalsText_ = std::to_string(games.size()) + " games, " + std::to_string(total / 3600) + " hours";
    if (sortByLastPlayed()) totalsText_ += ", most recent first";
    if (stressActive()) totalsText_ += "   (stress test)";

    DetailPanel panel = buildDetailPanel();
    shelf_ = new VirtualShelf(games, 288, 432, 16, GameCard::Style::Bare, [this, panel](size_t, const Game& game) {
        focused_ = &game;
        panel.update(game);
    });
    panel.update(games.front());

    brls::Box* panelRow = new brls::Box(brls::Axis::COLUMN);
    panelRow->setPadding(0, ui::kGutter, 22, ui::kGutter);
    panelRow->addView(panel.root);

    brls::Box* outer = new brls::Box(brls::Axis::COLUMN);
    outer->setGrow(1.0f);
    outer->addView(top);
    outer->addView(shelf_);
    brls::Box* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    outer->addView(spacer);
    outer->addView(panelRow);
    return outer;
}

brls::View* LibraryActivity::buildContent() {
    shelf_ = nullptr;
    titleLabel_ = nullptr;
    subtitleLabel_ = nullptr;
    focused_ = mockGames().empty() ? nullptr : &mockGames().front();
    brls::View* content = buildShelf();
    updateTitle();
    return content;
}

void LibraryActivity::rebuild() {
    // Keep the focus on the same title if it is still there
    const uint64_t keepId = focused_ ? focused_->id : 0;
    const auto started = std::chrono::steady_clock::now();
    reloadLibrary();
    // Add the new content and move the focus to it before the old views go away
    brls::View* old = content_;
    content_ = buildContent();
    holder_->addView(content_);
    brls::Application::giveFocus(content_);
    if (keepId != 0 && shelf_) {
        const std::vector<Game>& games = mockGames();
        for (size_t i = 0; i < games.size(); i++) {
            if (games[i].id == keepId) {
                shelf_->focusIndex(i);
                break;
            }
        }
    }
    if (old) {
        holder_->removeView(old);
    }
    appLog("rebuild: list and shelf in " +
           std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms");
}

void LibraryActivity::claimScreenClosed(bool libraryChanged) {
    if (libraryChanged) {
        rebuild();
    } else if (brls::Application::getCurrentFocus() == nullptr || brls::Application::getCurrentFocus()->getParentActivity() != this) {
        brls::Application::giveFocus(content_);
    }
}

void LibraryActivity::updateTitle() {
    if (frame_) frame_->setTitle("Library");
    if (titleLabel_) titleLabel_->setText("Library");
    if (subtitleLabel_) subtitleLabel_->setText(profileCount() > 1 ? profileName() + "  \u2014  " + totalsText_ : totalsText_);
}

void LibraryActivity::profileChosen(bool changed) {
    if (!changed) {
        claimScreenClosed(false);   // just puts the focus back
        return;
    }
    focused_ = nullptr;             // another list: start at its first card
    rebuild();
    updateTitle();
    showNewTitlesPrompt();          // this profile may have homebrew to claim
    startPosterDownloads();         // and titles without posters
}

void LibraryActivity::startPosterDownloads() {
    startPosterFetch(posterJobs(), [this](uint64_t id, const std::string& path) { posterArrived(id, path); });
}

void LibraryActivity::posterArrived(uint64_t id, const std::string& path) {
    GameCard::rememberPoster(id, path);
    if (shelf_) {
        shelf_->forEachCard([&](GameCard* card) {
            if (card->game().id == id) card->showPoster(path);
        });
    }
}

void LibraryActivity::showNewTitlesPrompt() {
    if (stressActive() || newTitleSuggestions().empty()) {
        return;
    }
    brls::Application::pushActivity(new ClaimActivity(ClaimActivity::Mode::NewTitles, [this](bool changed) { claimScreenClosed(changed); }));
}

brls::View* LibraryActivity::createContentView() {
    // The content sits in a holder so it can be replaced when the game list changes
    content_ = buildContent();
    holder_ = new brls::Box(brls::Axis::COLUMN);
    holder_->addView(content_);
    brls::AppletFrame* frame = frame_ = ui::bareFrame(holder_);   // own top bar with hints
    updateTitle();
    auto exportAction = [](brls::View*) {
        std::string path;
        if (exportDump(path)) {
            brls::Application::notify("Data written to " + path);
        } else {
            brls::Application::notify("Could not write the data file");
        }
        return true;
    };
    frame->registerAction("Export data", brls::ControllerButton::BUTTON_Y, exportAction);
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Y), exportAction);   // PC: Y key
    auto claimAction = [this](brls::View*) {
        if (!stressActive()) {
            brls::Application::pushActivity(new ClaimActivity(ClaimActivity::Mode::Manage, [this](bool changed) { claimScreenClosed(changed); }));
        }
        return true;
    };
    frame->registerAction("Claim titles", brls::ControllerButton::BUTTON_X, claimAction);
    auto profileAction = [this](brls::View*) {
        if (profileCount() > 1 && !stressActive()) {
            brls::Application::pushActivity(new ProfileActivity([this](bool changed) { profileChosen(changed); }));
        }
        return true;
    };
    if (profileCount() > 1) {
        frame->registerAction("Profile", brls::ControllerButton::BUTTON_LB, profileAction);
        frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_L), profileAction);   // PC: L key
    }
    auto posterAction = [this](brls::View*) {
        if (!focused_ || focused_->id == 0 || stressActive()) {
            return true;
        }
        if (steamGridKey().empty()) {
            brls::Application::notify("Add your SteamGridDB API key to steamgriddb.txt");
            return true;
        }
        brls::Application::pushActivity(new ChangePosterActivity(*focused_, [this](bool changed) { claimScreenClosed(changed); }));
        return true;
    };
    auto sortAction = [this](brls::View*) {
        if (stressActive()) return true;
        setSortByLastPlayed(!sortByLastPlayed());
        focused_ = nullptr;     // the new order starts at its first card
        rebuild();
        return true;
    };
    frame->registerAction("Sort", brls::ControllerButton::BUTTON_RB, sortAction, true);
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_R), sortAction);   // PC: R key
    frame->registerAction("Change poster", brls::ControllerButton::BUTTON_BACK, posterAction);
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_P), posterAction);   // PC: P key
    frame->registerAction("Details", brls::ControllerButton::BUTTON_A, [this](brls::View*) {
        if (focused_ && focused_->id != 0 && !stressActive()) {
            brls::Application::pushActivity(new DetailsActivity(*focused_, [this](bool changed) { claimScreenClosed(changed); }));
        }
        return true;
    });
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_X), claimAction);   // PC: X key
    return frame;
}

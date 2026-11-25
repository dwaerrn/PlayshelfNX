#include "details_activity.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "adjust_activity.hpp"
#include "app_data.hpp"
#include "poster_activity.hpp"
#include "game_card.hpp"
#include "poster_fetch.hpp"
#include "ui_style.hpp"

namespace {

constexpr size_t kMaxSessionRows = 50;      // every label is drawn each frame; keep the view count bounded
constexpr float kPosterW = 240, kPosterH = 360;

std::string dateText(int64_t clock, int tz) {
    if (clock == 0) return "-";
    const lib::CivilTime c = lib::toCivil(clock, tz);
    return std::to_string(c.day) + " " + lib::monthName(c.month) + " " + std::to_string(c.year);
}

std::string dateTimeText(int64_t clock, int tz) {
    const lib::CivilTime c = lib::toCivil(clock, tz);
    char hm[8];
    std::snprintf(hm, sizeof(hm), "%02d:%02d", c.hour, c.minute);
    return lib::weekdayName(c.weekday) + " " + std::to_string(c.day) + " " + lib::monthName(c.month) + " " + std::to_string(c.year) + ", " + hm;
}

std::string duration(int64_t secs) {
    if (secs > 0 && secs < 60) return "<1m";
    return formatPlaytime(static_cast<unsigned>(std::max<int64_t>(0, secs)));
}

// Caption over a value, one cell of the numbers grid
brls::Box* stat(const std::string& caption, const std::string& value) {
    brls::Box* box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(200);
    box->setGrow(1.0f);
    brls::Label* cap = ui::label(caption, 15, ui::kMuted);
    cap->setMargins(0, 0, 2, 0);
    box->addView(cap);
    box->addView(ui::label(value, 22, ui::kText));
    return box;
}

brls::Label* panelTitle(const std::string& text) {
    brls::Label* label = ui::bold(text, 18);
    label->setMargins(0, 0, 14, 0);
    return label;
}

// Vertical bars with a caption under each and the value above
brls::Box* barChart(const std::vector<std::pair<std::string, int64_t>>& values, float barW, float gap, float maxH) {
    int64_t top = 1;
    for (const auto& v : values) top = std::max(top, v.second);
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::FLEX_END);
    for (size_t i = 0; i < values.size(); i++) {
        brls::Box* col = new brls::Box(brls::Axis::COLUMN);
        col->setWidth(barW);
        col->setMargins(0, gap, 0, 0);
        col->setAlignItems(brls::AlignItems::CENTER);
        const int64_t v = values[i].second;
        brls::Label* amount = ui::label(v >= 3600 ? std::to_string(v / 3600) + "h" : v >= 60 ? std::to_string(v / 60) + "m" : "", 12, ui::kMuted);
        amount->setMargins(0, 0, 4, 0);
        col->addView(amount);
        brls::Box* bar = new brls::Box(brls::Axis::COLUMN);
        bar->setWidth(barW - 10);
        bar->setHeight(std::max(3.0f, maxH * static_cast<float>(v) / static_cast<float>(top)));
        bar->setCornerRadius(2);
        bar->setBackgroundColor(v == 0 ? ui::kFaint : ui::kAccent);
        col->addView(bar);
        brls::Label* caption = ui::label(values[i].first, 13, ui::kMuted);
        caption->setMargins(6, 0, 0, 0);
        col->addView(caption);
        row->addView(col);
    }
    return row;
}

}  // namespace

DetailsActivity::DetailsActivity(const Game& game, std::function<void(bool)> onClose)
    : game_(game), details_(gameDetails(game)), tz_(timeZoneOffset()), onClose_(std::move(onClose)) { }

brls::View* DetailsActivity::buildHeader() {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::FLEX_START);

    // The same card as on the shelf (focus border and zoom); A changes the poster
    GameCard* poster = poster_ = new GameCard(game_, kPosterW, kPosterH, GameCard::Style::Bare);
    poster->setShrink(0);
    poster->setMargins(14, 40, 14, 0);
    poster->registerAction("Change poster", brls::ControllerButton::BUTTON_A, [this](brls::View*) {
        if (steamGridKey().empty()) {
            brls::Application::notify("Add your SteamGridDB API key to steamgriddb.txt");
        } else {
            brls::Application::pushActivity(new ChangePosterActivity(game_, [this](bool c) { subScreenClosed(c); }));
        }
        return true;
    });
    row->addView(poster);

    brls::Box* info = new brls::Box(brls::Axis::COLUMN);
    info->setGrow(1.0f);
    info->setShrink(1.0f);
    info->setMargins(12, 0, 0, 0);
    nameLabel_ = ui::bold(game_.name, 34, ui::kText, false);
    info->addView(nameLabel_);
    char hex[24] = "";
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(game_.id));
    brls::Label* id = ui::label(std::string(hex) + (game_.homebrew ? ", homebrew" : ""), 15, ui::kMuted);
    id->setMargins(4, 0, 22, 0);
    info->addView(id);

    // Total playtime, large
    brls::Box* total = new brls::Box(brls::Axis::ROW);
    total->setAlignItems(brls::AlignItems::FLEX_END);
    total->setMargins(0, 0, 20, 0);
    total->addView(ui::bold(formatPlaytime(game_.playSecs), 44, ui::kAccent));
    brls::Label* totalCap = ui::label(game_.adjustSecs ? "played (adjusted)" : "played", 17, ui::kMuted);
    totalCap->setMargins(0, 0, 8, 12);
    total->addView(totalCap);
    info->addView(total);

    const int64_t first = details_.sessions.empty() ? 0 : details_.sessions.back().start;
    const int64_t last = details_.sessions.empty() ? 0 : details_.sessions.front().start;
    brls::Box* grid = ui::panel(brls::Axis::COLUMN);
    grid->setPadding(18, 22, 4, 22);
    brls::Box* row1 = new brls::Box(brls::Axis::ROW);
    row1->setMargins(0, 0, 14, 0);
    row1->addView(stat("Launches", std::to_string(game_.launches)));
    row1->addView(stat("Average session", duration(details_.averageSeconds)));
    row1->addView(stat("Longest session", duration(details_.longestSeconds)));
    grid->addView(row1);
    brls::Box* row2 = new brls::Box(brls::Axis::ROW);
    row2->setMargins(0, 0, 14, 0);
    row2->addView(stat("First played", dateText(first, tz_)));
    row2->addView(stat("Last played", dateText(last, tz_)));
    row2->addView(stat("Sessions logged", std::to_string(details_.sessions.size())));
    grid->addView(row2);
    info->addView(grid);

    // Where the total differs from the sessions below
    std::string note;
    const int64_t fromSystem = static_cast<int64_t>(game_.playSecs) - game_.adjustSecs - details_.loggedSeconds;
    if (game_.adjustSecs != 0) {
        note += "Includes your adjustment of " + std::string(game_.adjustSecs > 0 ? "+" : "-") + duration(std::abs(game_.adjustSecs)) + ". ";
    }
    if (fromSystem >= 60) {
        note += duration(fromSystem) + " come from the console's own statistics, for sessions the play log no longer has.";
    }
    if (!note.empty()) {
        brls::Label* n = ui::label(note, 15, ui::kMuted, false);
        n->setMargins(12, 0, 0, 2);
        info->addView(n);
    }
    row->addView(info);
    return row;
}

brls::View* DetailsActivity::buildCharts() {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::STRETCH);
    row->setMargins(20, 0, 16, 0);

    // Last 12 months up to the last month played
    brls::Box* months = ui::panel(brls::Axis::COLUMN);
    months->setGrow(1.0f);
    months->setPadding(18, 22, 16, 22);
    months->setMargins(0, 16, 0, 0);
    months->addView(panelTitle("Playtime by month"));
    std::vector<std::pair<std::string, int64_t>> values;
    const size_t n = details_.months.size();
    for (size_t i = n > 12 ? n - 12 : 0; i < n; i++) {
        const lib::MonthTotal& m = details_.months[i];
        std::string label = lib::monthName(m.month);
        if (m.month == 1 || i == (n > 12 ? n - 12 : 0)) label += " " + std::to_string(m.year % 100);
        values.emplace_back(label, m.playSeconds);
    }
    if (values.empty()) {
        months->addView(ui::label("No sessions in the play log.", 16, ui::kMuted));
    } else {
        months->addView(barChart(values, 52, 2, 110));
    }
    row->addView(months);

    // Playtime by the weekday a session started
    brls::Box* week = ui::panel(brls::Axis::COLUMN);
    week->setPadding(18, 22, 16, 22);
    week->addView(panelTitle("By weekday"));
    std::vector<std::pair<std::string, int64_t>> days;
    for (int d = 0; d < 7; d++) days.emplace_back(lib::weekdayName(d), details_.weekdaySeconds[d]);
    week->addView(barChart(days, 42, 2, 110));
    row->addView(week);
    return row;
}

brls::View* DetailsActivity::buildSessions() {
    brls::Box* box = ui::panel(brls::Axis::COLUMN);
    box->setPadding(18, 14, 14, 14);
    brls::Label* title = panelTitle("Recent sessions");
    title->setMargins(0, 0, 10, 8);
    box->addView(title);
    const size_t shown = std::min(details_.sessions.size(), kMaxSessionRows);
    for (size_t i = 0; i < shown; i++) {
        const lib::SessionInfo& s = details_.sessions[i];
        brls::Box* row = new ui::CulledBox(brls::Axis::ROW);
        row->setHeight(42);
        row->setPadding(0, 12, 0, 12);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setFocusable(true);
        row->setHighlightCornerRadius(6);
        row->setCornerRadius(6);
        if (i % 2 == 0) row->setBackgroundColor(ui::kSurfaceAlt);
        brls::Label* when = ui::label(dateTimeText(s.start, tz_), 17, ui::kText);
        when->setGrow(1.0f);
        row->addView(when);
        if (!s.endedCleanly) {
            brls::Label* tag = ui::label("no exit recorded", 14, ui::kMuted);
            tag->setMargins(0, 24, 0, 0);
            row->addView(tag);
        }
        brls::Label* len = ui::label(duration(s.playSeconds), 17, ui::kAccent);
        len->setWidth(110);
        len->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        row->addView(len);
        box->addView(row);
    }
    if (details_.sessions.size() > shown) {
        brls::Label* more = ui::label(std::to_string(details_.sessions.size() - shown) + " older sessions are not listed.", 15, ui::kMuted);
        more->setMargins(10, 12, 0, 0);
        box->addView(more);
    }
    return box;
}

brls::View* DetailsActivity::createContentView() {
    brls::Box* page = new brls::Box(brls::Axis::COLUMN);
    page->setPadding(0, ui::kGutter, 28, ui::kGutter);
    page->addView(buildHeader());
    page->addView(buildCharts());
    page->addView(buildSessions());

    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setContentView(page);
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);   // one session per press, kept in view

    brls::Label *title, *subtitle;
    ui::Hints hints = {{brls::ControllerButton::BUTTON_B, "Back"}, {brls::ControllerButton::BUTTON_X, "Rename"},
                       {brls::ControllerButton::BUTTON_Y, "Adjust playtime"}};
    if (game_.homebrew) hints.push_back({brls::ControllerButton::BUTTON_RB, "Homebrew marker"});
    brls::Box* top = ui::topBar(title, subtitle, hints);
    title->setText("Details");

    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->addView(top);
    root->addView(scroll);

    brls::AppletFrame* frame = ui::bareFrame(root);
    frame->setTitle("Details");
    frame->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(changed_); return true; });
    auto adjust = [this](brls::View*) {
        brls::Application::pushActivity(new AdjustActivity(game_, [this](bool c) { subScreenClosed(c); }));
        return true;
    };
    frame->registerAction("Adjust playtime", brls::ControllerButton::BUTTON_Y, adjust);
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Y), adjust);   // PC: Y key
    auto renameAction = [this](brls::View*) { rename(); return true; };
    frame->registerAction("Rename", brls::ControllerButton::BUTTON_X, renameAction);
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_X), renameAction);   // PC: X key
    if (game_.homebrew) {
        auto markerAction = [this](brls::View*) { toggleMarker(); return true; };
        frame->registerAction("Homebrew marker", brls::ControllerButton::BUTTON_RB, markerAction);
        frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_R), markerAction);   // PC: R key
    }
    return frame;
}

void DetailsActivity::rename() {
    brls::Application::getImeManager()->openForText([this](std::string text) {
        setTitleName(game_.id, text);
        const bool reset = text.find_first_not_of(' ') == std::string::npos;
        game_.name = reset ? game_.systemName : text;
        nameLabel_->setText(game_.name);
        changed_ = true;
        if (reset) brls::Application::notify("Back to the system name");
    }, "Game name", "Leave empty to use the system name", 64, game_.name);
}

void DetailsActivity::toggleMarker() {
    game_.hideBadge = !game_.hideBadge;
    setHomebrewMarker(game_.id, !game_.hideBadge);
    if (poster_) poster_->setBadgeVisible(!game_.hideBadge);
    changed_ = true;
    brls::Application::notify(game_.hideBadge ? "Homebrew marker hidden" : "Homebrew marker shown");
}

void DetailsActivity::subScreenClosed(bool changed) {
    // The numbers on this page are stale: go back to the rebuilt library
    if (changed) close(true);
}

void DetailsActivity::close(bool changed) {
    if (closing_) return;
    closing_ = true;
    std::function<void(bool)> done = onClose_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, changed]() {
        if (done) done(changed);
    });
}

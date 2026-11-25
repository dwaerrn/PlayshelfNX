#include "claim_activity.hpp"
#include "ui_style.hpp"

#include <algorithm>

#include "mock_data.hpp"

namespace {

const NVGcolor kText = nvgRGB(200, 205, 214);

brls::Label* textLabel(const std::string& text, float size, NVGcolor color) {
    brls::Label* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    return label;
}

}  // namespace

ClaimActivity::ClaimActivity(Mode mode, std::function<void(bool)> onClose) : mode_(mode), onClose_(std::move(onClose)) {
    if (mode_ == Mode::NewTitles) {
        items_ = newTitleSuggestions();
        for (ClaimItem& item : items_) {
            item.claimed = true;    // ticked by default
        }
    } else {
        items_ = claimList();
    }
}

void ClaimActivity::addCells(brls::Box* list, bool installed) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].installed != installed) {
            continue;
        }
        std::string title = items_[i].name;
        if (items_[i].playSecs >= 60) {
            title += "  -  " + formatPlaytime(items_[i].playSecs);
        }
        brls::BooleanCell* cell = new brls::BooleanCell();
        cell->init(title, items_[i].claimed, [this, i](bool on) { items_[i].claimed = on; });
        // The system icon in front of the name (an empty square keeps the names aligned)
        brls::Image* icon = new brls::Image();
        icon->setWidth(48);
        icon->setHeight(48);
        icon->setShrink(0);
        icon->setCornerRadius(6);
        icon->setMargins(0, 16, 0, 0);
        icon->setScalingType(brls::ImageScalingType::FILL);
        if (!items_[i].iconFile.empty()) {
            icon->setImageFromFile(items_[i].iconFile);
        } else {
            icon->setBackgroundColor(nvgRGB(60, 62, 70));
        }
        cell->addView(icon, 0);
        list->addView(cell);
    }
}

void ClaimActivity::close(bool apply) {
    if (closing_) {
        return;
    }
    closing_ = true;
    const bool changed = apply && setClaims(items_, mode_ == Mode::NewTitles);
    std::function<void(bool)> done = onClose_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, changed]() {
        if (done) {
            done(changed);
        }
    });
}

brls::View* ClaimActivity::createContentView() {
    const bool prompt = mode_ == Mode::NewTitles;
    const std::string profile = profileName();

    brls::Box* list = new brls::Box(brls::Axis::COLUMN);
    list->setPadding(10, 80, 30, 80);

    std::string intro;
    if (items_.empty()) {
        intro = "No titles without a profile were found. Homebrew shows up here after it has been started once.";
    } else if (prompt) {
        intro = "These titles were played without a profile. Claim the ones that belong in " + profile +
                "'s library. Titles that are switched off will not be offered again.";
    } else {
        intro = "Titles that were played without a profile. Switched on, they are part of " + profile + "'s library.";
    }
    brls::Label* introLabel = textLabel(intro, 20, kText);
    introLabel->setMargins(14, 12, 18, 12);
    list->addView(introLabel);

    const bool anyInstalled = std::any_of(items_.begin(), items_.end(), [](const ClaimItem& c) { return c.installed; });
    const bool anyGone = std::any_of(items_.begin(), items_.end(), [](const ClaimItem& c) { return !c.installed; });
    if (anyInstalled) {
        if (anyGone) {
            brls::Header* header = new brls::Header();
            header->setTitle("Installed");
            list->addView(header);
        }
        addCells(list, true);
    }
    if (anyGone) {
        brls::Header* header = new brls::Header();
        header->setTitle("No longer installed");
        list->addView(header);
        addCells(list, false);
    }

    brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
    scroll->setContentView(list);
    scroll->setGrow(1.0f);

    brls::AppletFrame* frame = ui::frame(scroll);
    frame->setTitle(prompt ? "New titles found" : "Claim titles");

    if (prompt && !items_.empty()) {
        // B must not claim anything by accident: Claim is its own button
        frame->registerAction("Later", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(false); return true; });
        frame->registerAction("Claim", brls::ControllerButton::BUTTON_X, [this](brls::View*) { close(true); return true; });
        frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_X), [this](brls::View*) { close(true); return true; });   // PC: X key
    } else {
        frame->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(true); return true; });
    }
    return frame;
}

#include "adjust_activity.hpp"
#include "ui_style.hpp"

#include <cstdlib>

#include "app_data.hpp"

namespace {

const long kMaxSeconds = 9999L * 3600;

brls::Label* textLabel(const std::string& text, float size, NVGcolor color) {
    brls::Label* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    return label;
}

std::string signedPlaytime(long secs) {
    if (secs == 0) {
        return "none";
    }
    return (secs > 0 ? "+" : "-") + formatPlaytime(static_cast<unsigned>(std::labs(secs)));
}

// A row whose value is changed with left/right (held down: repeats)
brls::DetailCell* stepperRow(const std::string& title, std::function<void(int)> onStep) {
    brls::DetailCell* cell = new brls::DetailCell();
    cell->setText(title);
    cell->registerAction("", brls::ControllerButton::BUTTON_NAV_LEFT, [onStep](brls::View*) { onStep(-1); return true; }, true, true);
    cell->registerAction("", brls::ControllerButton::BUTTON_NAV_RIGHT, [onStep](brls::View*) { onStep(1); return true; }, true, true);
    cell->registerAction("-10", brls::ControllerButton::BUTTON_LB, [onStep](brls::View*) { onStep(-10); return true; }, false, true);
    cell->registerAction("+10", brls::ControllerButton::BUTTON_RB, [onStep](brls::View*) { onStep(10); return true; }, false, true);
    return cell;
}

}  // namespace

AdjustActivity::AdjustActivity(const Game& game, std::function<void(bool)> onClose)
    : id_(game.id), name_(game.name), onClose_(std::move(onClose)) {
    total_ = static_cast<long>(game.playSecs);
    recorded_ = total_ - game.adjustSecs;
    if (recorded_ < 0) {
        recorded_ = 0;      // the correction was clamped: treat what is shown as the starting point
    }
    original_ = total_;
}

void AdjustActivity::step(long seconds) {
    long t = total_ + seconds;
    if (t < 0) t = 0;
    if (t > kMaxSeconds) t = kMaxSeconds;
    total_ = t;
    refresh();
}

void AdjustActivity::refresh() {
    totalLabel_->setText(formatPlaytime(static_cast<unsigned>(total_)));
    diffLabel_->setText("Recorded " + formatPlaytime(static_cast<unsigned>(recorded_)) + "   |   Adjustment " + signedPlaytime(total_ - recorded_));
    hoursCell_->setDetailText("<   " + std::to_string(total_ / 3600) + "   >");
    minutesCell_->setDetailText("<   " + std::to_string((total_ % 3600) / 60) + "   >");
}

void AdjustActivity::close(bool save) {
    if (closing_) {
        return;
    }
    closing_ = true;
    bool changed = false;
    if (save && total_ != original_) {
        setPlaytimeAdjustment(id_, static_cast<int>(total_ - recorded_));
        changed = true;
    }
    std::function<void(bool)> done = onClose_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, changed]() {
        if (done) {
            done(changed);
        }
    });
}

brls::View* AdjustActivity::createContentView() {
    brls::Box* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(20, 80, 30, 80);

    brls::Label* name = textLabel(name_, 30, nvgRGB(255, 255, 255));
    name->setSingleLine(true);
    name->setMargins(0, 12, 18, 12);
    box->addView(name);

    brls::Label* caption = textLabel("TOTAL PLAYTIME", 14, nvgRGB(150, 156, 168));
    caption->setMargins(0, 12, 4, 12);
    box->addView(caption);
    totalLabel_ = textLabel("", 44, nvgRGB(51, 186, 227));
    totalLabel_->setMargins(0, 12, 6, 12);
    box->addView(totalLabel_);
    diffLabel_ = textLabel("", 18, nvgRGB(200, 205, 214));
    diffLabel_->setMargins(0, 12, 24, 12);
    box->addView(diffLabel_);

    hoursCell_ = stepperRow("Hours", [this](int n) { step(n * 3600L); });
    minutesCell_ = stepperRow("Minutes", [this](int n) { step(n * 60L); });
    for (brls::DetailCell* cell : {hoursCell_, minutesCell_}) {
        // The rows have their own A action ("OK"); replace it so A saves from anywhere
        cell->registerAction("Save", brls::ControllerButton::BUTTON_A, [this](brls::View*) { close(true); return true; });
    }
    box->addView(hoursCell_);
    box->addView(minutesCell_);

    brls::Label* help = textLabel("Left and right change the value, L and R by 10. Only this profile's number changes; "
                                  "the recorded play data is kept.", 16, nvgRGB(150, 156, 168));
    help->setMargins(20, 12, 0, 12);
    box->addView(help);

    refresh();

    brls::AppletFrame* frame = ui::frame(box);
    frame->setTitle("Adjust playtime");
    frame->registerAction("Cancel", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(false); return true; });
    frame->registerAction("Save", brls::ControllerButton::BUTTON_A, [this](brls::View*) { close(true); return true; });
    frame->registerAction("Reset", brls::ControllerButton::BUTTON_X, [this](brls::View*) { total_ = recorded_; refresh(); return true; });
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_X), [this](brls::View*) { total_ = recorded_; refresh(); return true; });
    return frame;
}

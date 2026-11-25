#include "profile_activity.hpp"
#include "ui_style.hpp"

#include "mock_data.hpp"

namespace {

const NVGcolor kMuted = nvgRGB(150, 156, 168);
const NVGcolor kAccent = nvgRGB(51, 186, 227);

brls::Label* textLabel(const std::string& text, float size, NVGcolor color) {
    brls::Label* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    label->setSingleLine(true);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    return label;
}

}  // namespace

ProfileActivity::ProfileActivity(std::function<void(bool)> onClose) : items_(profileItems()), onClose_(std::move(onClose)) { }

brls::View* ProfileActivity::createContentView() {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setGrow(1.0f);

    brls::View* focus = nullptr;
    for (size_t i = 0; i < items_.size(); i++) {
        const ProfileItem& p = items_[i];
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(220);
        card->setHeight(330);
        card->setMargins(0, 14, 0, 14);
        card->setPadding(20, 0, 16, 0);
        card->setAlignItems(brls::AlignItems::CENTER);
        card->setCornerRadius(12);
        card->setHighlightCornerRadius(14);
        card->setBackgroundColor(nvgRGB(38, 39, 44));
        card->setFocusable(true);

        if (!p.avatarFile.empty()) {
            brls::Image* avatar = new brls::Image();
            avatar->setWidth(170);
            avatar->setHeight(170);
            avatar->setCornerRadius(85);
            avatar->setScalingType(brls::ImageScalingType::FILL);
            avatar->setImageFromFile(p.avatarFile);
            card->addView(avatar);
        } else {
            // No picture: the first letter on a coloured circle
            brls::Box* circle = new brls::Box(brls::Axis::COLUMN);
            circle->setWidth(170);
            circle->setHeight(170);
            circle->setCornerRadius(85);
            circle->setJustifyContent(brls::JustifyContent::CENTER);
            circle->setAlignItems(brls::AlignItems::CENTER);
            static const NVGcolor colors[] = {nvgRGB(66, 120, 200), nvgRGB(200, 90, 90), nvgRGB(80, 170, 110), nvgRGB(190, 140, 60)};
            circle->setBackgroundColor(colors[i % 4]);
            circle->addView(textLabel(p.name.substr(0, 1), 72, nvgRGB(255, 255, 255)));
            card->addView(circle);
        }

        brls::Label* name = textLabel(p.name, 24, nvgRGB(255, 255, 255));
        name->setWidth(200);
        name->setMargins(16, 0, 4, 0);
        card->addView(name);
        card->addView(textLabel(std::to_string(p.games) + (p.games == 1 ? " game" : " games") + "  -  " + formatPlaytime(p.playSecs), 15, kMuted));
        if (p.current) {
            brls::Label* tag = textLabel("SHOWN NOW", 13, kAccent);
            tag->setMargins(8, 0, 0, 0);
            card->addView(tag);
        }

        const std::string uid = p.uid;
        card->registerAction("Choose", brls::ControllerButton::BUTTON_A, [this, uid](brls::View*) {
            close(switchProfile(uid));
            return true;
        });
        row->addView(card);
        if (p.current || !focus) focus = card;
    }

    brls::AppletFrame* frame = ui::frame(row);
    frame->setTitle("Choose a profile");
    frame->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) { close(false); return true; });
    if (focus) {
        // Start on the profile that is shown now
        brls::View* f = focus;
        brls::sync([f] { brls::Application::giveFocus(f); });
    }
    return frame;
}

void ProfileActivity::close(bool changed) {
    if (closing_) return;
    closing_ = true;
    std::function<void(bool)> done = onClose_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, changed]() {
        if (done) done(changed);
    });
}

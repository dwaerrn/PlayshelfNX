#include "game_card.hpp"

#include <map>

#include "image_loader.hpp"
#include "ui_style.hpp"

namespace {
std::map<uint64_t, std::string>& downloaded() {
    static std::map<uint64_t, std::string> m;
    return m;
}
}  // namespace

void GameCard::rememberPoster(uint64_t id, const std::string& path) {
    downloaded()[id] = path;
}

void GameCard::forgetPoster(uint64_t id) {
    downloaded().erase(id);
}

void GameCard::fadeIn(brls::Image* image) {
    image->setAlpha(0.0f);
    fade_.stop();
    fade_.reset(0.0f);
    fade_.addStep(1.0f, 220, ui::easeOut());
    fade_.setTickCallback([this, image] { image->setAlpha(fade_); });
    fade_.start();
}

brls::Image* GameCard::makePoster(const std::string& file, bool fromRes) {
    brls::Image* poster = new brls::Image();
    poster->setPositionType(brls::PositionType::ABSOLUTE);
    poster->setPositionTop(0);
    poster->setPositionLeft(0);
    poster->setWidth(width_);
    poster->setHeight(height_);
    poster->setScalingType(brls::ImageScalingType::FILL);
    poster->setCornerRadius(ui::kCardRadius);
    if (fromRes) {
        poster->setImageFromRes("img/art/" + file);     // demo art (PC)
    } else {
        // Decoded in the background; fades in when ready (the card's own colour shows meanwhile)
        const int maxW = static_cast<int>(width_ * kFocusScale + 1), maxH = static_cast<int>(height_ * kFocusScale + 1);
        if (!imageload::show(poster, file, maxW, maxH, alive_, [this, poster] { fadeIn(poster); })) {
            poster->setAlpha(0.0f);
        }
    }
    return poster;
}

void GameCard::showPoster(const std::string& path) {
    if (poster_) {
        return;     // already has one (the user's own art or an earlier download)
    }
    if (icon_) {
        this->removeView(icon_);
        icon_ = nullptr;
    }
    if (name_) {
        this->removeView(name_);
        name_ = nullptr;
    }
    poster_ = makePoster(path, false);
    this->addView(poster_, 0);
    if (poster_->getAlpha() > 0.0f) fadeIn(poster_);    // was cached: still fade, it is new
}

GameCard::GameCard(const Game& game, float width, float height, Style style)
    : brls::Box(brls::Axis::COLUMN), game_(game), width_(width), height_(height)
{
    this->setWidth(width);
    this->setHeight(height);
    this->setFocusable(true);
    this->setCornerRadius(ui::kCardRadius);
    this->setHighlightCornerRadius(ui::kCardRadius + 2);
    this->setClipsToBounds(true);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setBackgroundColor(ui::kSurfaceAlt);
    this->setHideHighlight(true);
    this->setBorderColor(ui::kText);
    this->setBorderThickness(0.0f);

    std::string file = game.artFile;
    if (file.empty() && game.id != 0) {
        auto it = downloaded().find(game.id);
        if (it != downloaded().end()) file = it->second;
    }
    if (!file.empty() || !game.art.empty()) {
        // Boxart fills the whole card
        poster_ = file.empty() ? makePoster(game.art, true) : makePoster(file, false);
        this->addView(poster_);
    } else {
        // No poster: the square system icon on top (if there is one) and the name below it
        const bool icon = !game.iconFile.empty();
        if (icon) {
            brls::Image* image = icon_ = new brls::Image();
            image->setPositionType(brls::PositionType::ABSOLUTE);
            image->setPositionTop(0);
            image->setPositionLeft(0);
            image->setWidth(width);
            image->setHeight(width);
            image->setScalingType(brls::ImageScalingType::FILL);
            if (!imageload::show(image, game.iconFile, static_cast<int>(width * kFocusScale + 1), static_cast<int>(width * kFocusScale + 1), alive_,
                                 [this, image] { fadeIn(image); })) {
                image->setAlpha(0.0f);
            }
            this->addView(image);
        }
        brls::Label* name = name_ = new brls::Label();
        name->setText(game.name);
        name->setFontSize(width > 200 ? 17 : 14);
        name->setTextColor(nvgRGB(225, 228, 235));
        name->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        name->setVerticalAlign(brls::VerticalAlign::CENTER);
        name->setPositionType(brls::PositionType::ABSOLUTE);
        name->setPositionTop(icon ? width : 0);
        name->setPositionLeft(8);
        name->setWidth(width - 16);
        name->setHeight(icon ? height - width - (style == Style::Bare ? 0 : (style == Style::Full ? 62 : 38)) : height * 0.6f);
        this->addView(name);
    }

    if (game.homebrew) {
        brls::Box* badge = badge_ = new brls::Box(brls::Axis::ROW);
        badge->setPositionType(brls::PositionType::ABSOLUTE);
        badge->setPositionTop(8);
        badge->setPositionLeft(8);
        badge->setPadding(3, 8, 4, 8);
        badge->setCornerRadius(4);
        badge->setBackgroundColor(nvgRGBA(23, 22, 21, 215));
        badge->addView(ui::bold("Homebrew", 13, ui::kAccent));
        badge->setVisibility(game.hideBadge ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        this->addView(badge);
    }

    if (style == Style::Bare) {
        return;
    }

    // Playtime overlay along the bottom edge
    brls::Box* overlay = new brls::Box(brls::Axis::COLUMN);
    overlay->setBackgroundColor(nvgRGBA(8, 8, 12, 196));
    overlay->setPadding(8, 12, 8, 12);

    brls::Label* time = new brls::Label();
    time->setText(formatPlaytime(game.playSecs));
    time->setFontSize(width > 200 ? 24 : 18);
    time->setTextColor(nvgRGB(255, 255, 255));
    time->setSingleLine(true);
    overlay->addView(time);

    if (style == Style::Full) {
        brls::Label* last = new brls::Label();
        last->setText("Played " + formatAgo(game.lastDaysAgo));
        last->setFontSize(14);
        last->setTextColor(nvgRGB(170, 175, 185));
        last->setSingleLine(true);
        overlay->addView(last);
    }
    this->addView(overlay);
}

void GameCard::animateZoom(float to) {
    const float from = zoom_;
    zoom_.stop();
    zoom_.reset(from);
    zoom_.addStep(to, 140, ui::easeOut());
    zoom_.setTickCallback([this] { this->setBorderThickness(4.0f * zoom_); });
    zoom_.start();
}

void GameCard::onFocusGained() {
    brls::Box::onFocusGained();
    animateZoom(1.0f);
}

void GameCard::onFocusLost() {
    brls::Box::onFocusLost();
    animateZoom(0.0f);
}

void GameCard::frame(brls::FrameContext* ctx) {
    // Live cards outside the screen (most of the shelf's window) cost nothing to draw
    {
        const brls::Rect r = this->getFrame();
        const float m = r.getWidth() * (kFocusScale - 1.0f) + 8;
        if (r.getMaxX() < -m || r.getMinX() > brls::Application::contentWidth + m ||
            r.getMaxY() < -m || r.getMinY() > brls::Application::contentHeight + m) {
            return;
        }
    }
    const float scale = 1.0f + (kFocusScale - 1.0f) * zoom_;
    if (scale == 1.0f) {
        brls::Box::frame(ctx);
        return;
    }
    // Scale around the card's centre; the layout (and the neighbours) stay where they are
    const brls::Rect r = this->getFrame();
    const float cx = r.getMidX(), cy = r.getMidY();
    nvgSave(ctx->vg);
    nvgTranslate(ctx->vg, cx, cy);
    nvgScale(ctx->vg, scale, scale);
    nvgTranslate(ctx->vg, -cx, -cy);
    brls::Box::frame(ctx);
    nvgRestore(ctx->vg);
}

void GameCard::setBadgeVisible(bool visible) {
    if (badge_) badge_->setVisibility(visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

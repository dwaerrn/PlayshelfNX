#include "ui_style.hpp"

namespace ui {
namespace {

int boldFont = brls::FONT_INVALID;

class FontLabel : public brls::Label {
  public:
    void useFont(int f) { if (f != brls::FONT_INVALID) this->font = f; }
};

}  // namespace

void setFontPath() {
    // Not resources/font/: the Switch build leaves that folder out of romfs
    brls::FontLoader::USER_FONT_PATH = BRLS_ASSET("fonts/plex-regular.ttf");
}

void loadFonts() {
    NVGcontext* vg = brls::Application::getNVGContext();
    const int regular = brls::Application::getDefaultFont();
#ifdef __SWITCH__
    // The custom font replaces the system font; keep the system font for what Plex lacks (Japanese etc.)
    const int system = brls::Application::getFont(brls::FONT_REGULAR);
    if (regular != brls::FONT_INVALID && system != brls::FONT_INVALID && system != regular) nvgAddFallbackFontId(vg, regular, system);
#endif
    if (brls::Application::loadFontFromFile("ui-bold", BRLS_ASSET("fonts/plex-semibold.ttf"))) {
        boldFont = brls::Application::getFont("ui-bold");
        // nanovg looks only one level deep: list every fallback
        for (const std::string& name : {std::string(brls::FONT_REGULAR), std::string(brls::FONT_SWITCH_ICONS), std::string(brls::FONT_MATERIAL_ICONS)}) {
            const int f = brls::Application::getFont(name);
            if (f != brls::FONT_INVALID && f != boldFont) nvgAddFallbackFontId(vg, boldFont, f);
        }
        if (regular != brls::FONT_INVALID) nvgAddFallbackFontId(vg, boldFont, regular);
    }
}

void applyTheme() {
    brls::Theme& t = brls::Theme::getDarkTheme();
    t.addColor("brls/background", kBackground);
    t.addColor("brls/clear", kBackground);
    t.addColor("brls/text", kText);
    t.addColor("brls/accent", kAccent);
    t.addColor("brls/highlight/background", kSurfaceAlt);
    t.addColor("brls/highlight/color1", nvgRGB(220, 214, 204));
    t.addColor("brls/highlight/color2", nvgRGB(255, 252, 246));
    t.addColor("brls/click_pulse", nvgRGBA(235, 172, 80, 38));
    t.addColor("brls/list/listItem_value_color", kAccent);
    t.addColor("brls/applet_frame/separator", kFaint);
    t.addColor("brls/header/border", kFaint);
    t.addColor("brls/sidebar/separator", kFaint);
}

brls::Label* label(const std::string& text, float size, NVGcolor color, bool singleLine) {
    brls::Label* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setSingleLine(singleLine);
    return l;
}

brls::Label* bold(const std::string& text, float size, NVGcolor color, bool singleLine) {
    FontLabel* l = new FontLabel();
    l->useFont(boldFont);
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setSingleLine(singleLine);
    return l;
}

brls::Box* panel(brls::Axis axis) {
    brls::Box* p = new brls::Box(axis);
    p->setCornerRadius(kRadius);
    p->setBackgroundColor(kSurface);
    return p;
}

brls::Box* hintRow(const Hints& hints) {
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setShrink(0);
    // The button glyphs come from the Switch's symbol font. Without it (PC builds; the repo ships no
    // Nintendo fonts) the button's letter is shown instead.
    static const bool glyphs = brls::Application::getFont(brls::FONT_SWITCH_ICONS) != brls::FONT_INVALID;
    for (const auto& h : hints) {
        std::string key = brls::Hint::getKeyIcon(h.first);
        if (!glyphs) {
            switch (h.first) {
                case brls::ControllerButton::BUTTON_A: key = "A"; break;
                case brls::ControllerButton::BUTTON_B: key = "B"; break;
                case brls::ControllerButton::BUTTON_X: key = "X"; break;
                case brls::ControllerButton::BUTTON_Y: key = "Y"; break;
                case brls::ControllerButton::BUTTON_LB: key = "L"; break;
                case brls::ControllerButton::BUTTON_RB: key = "R"; break;
                default: key = "?"; break;
            }
        }
        brls::Label* glyph = glyphs ? label(key, 21, kText) : bold(key, 17, kAccent);
        glyph->setMargins(0, 7, 0, 26);
        row->addView(glyph);
        row->addView(label(h.second, 17, kMuted));
    }
    return row;
}

brls::Box* topBar(brls::Label*& title, brls::Label*& subtitle, const Hints& hints) {
    brls::Box* bar = new brls::Box(brls::Axis::ROW);
    bar->setHeight(68);
    bar->setShrink(0);
    bar->setPadding(6, kGutter, 0, kGutter);
    bar->setAlignItems(brls::AlignItems::CENTER);
    title = bold("", 24);
    title->setShrink(0);
    bar->addView(title);
    subtitle = label("", 17, kMuted);
    subtitle->setMargins(3, 0, 0, 16);
    subtitle->setGrow(1.0f);
    subtitle->setShrink(1.0f);
    bar->addView(subtitle);
    bar->addView(hintRow(hints));
    return bar;
}

brls::AppletFrame* frame(brls::View* content) {
    brls::AppletFrame* f = new brls::AppletFrame(content);
    f->setBackgroundColor(kBackground);
    return f;
}

void CulledBox::frame(brls::FrameContext* ctx) {
    const brls::Rect r = this->getFrame();
    if (r.getMaxY() < 0 || r.getMinY() > brls::Application::contentHeight) return;
    brls::Box::frame(ctx);
}

brls::AppletFrame* bareFrame(brls::View* content) {
    brls::AppletFrame* frame = ui::frame(content);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->setFooterVisibility(brls::Visibility::GONE);
    return frame;
}

}  // namespace ui

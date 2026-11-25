#pragma once

// Shared look: palette, fonts, labels, the slim top bar and the hint row.
// Warm dark palette with one accent; sentence-case captions; few separators.

#include <borealis.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui {

inline const NVGcolor kBackground = nvgRGB(23, 22, 21);
inline const NVGcolor kSurface = nvgRGB(35, 34, 32);       // panels
inline const NVGcolor kSurfaceAlt = nvgRGB(42, 40, 38);    // striped rows, empty card
inline const NVGcolor kText = nvgRGB(237, 233, 226);
inline const NVGcolor kMuted = nvgRGB(146, 140, 132);
inline const NVGcolor kFaint = nvgRGB(64, 61, 58);         // empty bars, placeholders
inline const NVGcolor kAccent = nvgRGB(235, 172, 80);      // playtime and bars
inline const NVGcolor kAccentLight = nvgRGB(250, 210, 140);
inline const NVGcolor kWarn = nvgRGB(222, 120, 96);

constexpr float kRadius = 10;       // panels
constexpr float kCardRadius = 6;    // posters
constexpr float kGutter = 48;       // left and right screen margin

// Ease-out curve for brls::Animatable. Borealis changed EasingFunction from an enum to a
// std::function (Nov 2026); this compiles with both versions.
template <class E = brls::EasingFunction>
E easeOut() {
    if constexpr (std::is_enum_v<E>) return E::quadraticOut;
    else return tweeny::easing::quadraticOut;
}

// Font and theme. Call before Application::init (setFontPath) and after it (loadFonts, applyTheme).
void setFontPath();
void loadFonts();
void applyTheme();

// Regular or semibold label
brls::Label* label(const std::string& text, float size, NVGcolor color, bool singleLine = true);
brls::Label* bold(const std::string& text, float size, NVGcolor color = kText, bool singleLine = true);

// A rounded panel
brls::Box* panel(brls::Axis axis = brls::Axis::COLUMN);

using Hints = std::vector<std::pair<brls::ControllerButton, std::string>>;

// Glyph + text pairs, right aligned
brls::Box* hintRow(const Hints& hints);

// Slim bar at the top: bold title, muted subtitle, hints on the right
brls::Box* topBar(brls::Label*& title, brls::Label*& subtitle, const Hints& hints);

// AppletFrame with an opaque background, so the fade between screens is a clean cross-fade
// (during the fade Borealis still draws the screen below)
brls::AppletFrame* frame(brls::View* content);

// The same without Borealis' header and footer (the actions still work)
brls::AppletFrame* bareFrame(brls::View* content);

// A box that is not drawn while it is outside the screen (long scrolling lists)
class CulledBox : public brls::Box {
  public:
    using brls::Box::Box;
    void frame(brls::FrameContext* ctx) override;
};

}  // namespace ui

#include "app/Chrome.h"

namespace Loopwire {
namespace Chrome {

namespace {
bool dark_ = true;
// Invalid until somebody chooses one - see setChosenBack.
QColor chosenDark_;
QColor chosenLight_;

// What the ribbon and the panels are drawn on when nobody has said otherwise. The same figures the
// ribbon's own sheet and the application's palette have always used, in one place now that they can be
// overridden: two copies of a default is one that gets changed and one that does not.
QColor builtInBack(bool dark) { return dark ? QColor("#3a3d43") : QColor("#e9e9ed"); }
}

void setDark(bool dark) { dark_ = dark; }
bool dark() { return dark_; }

void setChosenBack(bool dark, const QColor& c) {
    (dark ? chosenDark_ : chosenLight_) = c;
}

QColor chosenBack(bool dark) { return dark ? chosenDark_ : chosenLight_; }

QColor back() {
    const QColor chosen = chosenBack(dark_);
    return chosen.isValid() ? chosen : builtInBack(dark_);
}

QColor backInk() {
    // Off the GROUND's lightness, not the scheme's. Qt's lightness is 0..255; the middle is the point
    // where near-white stops being readable and near-black starts. The two figures are the scheme's own
    // inks rather than pure black and white, so a chosen ground still looks like this application.
    return back().lightness() < 128 ? QColor("#cfd8e6") : QColor("#1e2126");
}

// The light scheme is not the dark one inverted. Text goes to near-black rather than to mid-grey, because
// what was legible as pale grey on charcoal is a smudge as pale grey on white; the bars go a shade DARKER
// than the window they sit in, where in the dark scheme they go a shade lighter; and the fly-out's ground
// stays a clear step off white so a panel held over the drawing still reads as a thing in front of it.
QColor backAlt() {
    // A step AWAY from the ground: lighter on a dark one, darker on a light one, so the stripe reads at
    // the same strength whichever way round the scheme is.
    const QColor g = back();
    return g.lightness() < 128 ? g.lighter(112) : g.darker(104);
}

QColor backHeader() {
    const QColor g = back();
    return g.lightness() < 128 ? g.darker(112) : g.darker(108);
}

QColor ink() { return dark_ ? QColor("#cfd8e6") : QColor("#1e2126"); }
QColor subInk() { return dark_ ? QColor("#b8bdc6") : QColor("#4c525b"); }

QColor bar() { return dark_ ? QColor("#34373d") : QColor("#dcdce2"); }
QColor barInk() { return dark_ ? QColor("#cfd8e6") : QColor("#1e2126"); }
QColor strip() { return dark_ ? QColor("#34373d") : QColor("#dcdce2"); }
QColor stripInk() { return dark_ ? QColor("#cfd8e6") : QColor("#23262b"); }
QColor grip() { return dark_ ? QColor("#8b97a8") : QColor("#6b7280"); }

QColor flyout() { return dark_ ? QColor("#2b2f36") : QColor("#f5f5f7"); }
QColor flyoutBorder() { return dark_ ? QColor("#5b6572") : QColor("#9aa0a8"); }
QColor edge() { return dark_ ? QColor("#3a3f47") : QColor("#c8c8ce"); }

QColor line() { return dark_ ? QColor("#b3afa6") : QColor("#b8b8be"); }

} // namespace Chrome
} // namespace Loopwire

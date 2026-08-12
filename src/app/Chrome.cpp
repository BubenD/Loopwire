#include "app/Chrome.h"

namespace Edraw_Desktop {
namespace Chrome {

namespace {
bool dark_ = true;
}

void setDark(bool dark) { dark_ = dark; }
bool dark() { return dark_; }

// The light scheme is not the dark one inverted. Text goes to near-black rather than to mid-grey, because
// what was legible as pale grey on charcoal is a smudge as pale grey on white; the bars go a shade DARKER
// than the window they sit in, where in the dark scheme they go a shade lighter; and the fly-out's ground
// stays a clear step off white so a panel held over the drawing still reads as a thing in front of it.
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
} // namespace Edraw_Desktop

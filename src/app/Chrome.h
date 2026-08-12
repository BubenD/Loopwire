#pragma once

#include <QColor>

namespace Edraw_Desktop {

// The colours of the CHROME - the ribbon, the menus, the panels, the strips a folded panel leaves behind.
// Not the drawing: model space stays dark whichever way this is set, because a drawing is read against a
// dark ground and because the colours in it are the draughtsman's, not the program's.
//
// It lives on its own because the pieces that need it are spread out: the panels and the ribbon are built in
// MainWindow.cpp, the View tab draws its own icons in ViewTab.cpp, and anything with a stylesheet of its own
// needs to be told when the scheme changes. One place to ask, and one to change.
namespace Chrome {

void setDark(bool dark);
bool dark();

// Text. `ink` is what is read, `subInk` what is only labelled - a group's name under its buttons, a hint.
// Both are the FIRST thing to get wrong in a light scheme: pale grey lettering on white says nothing at all.
QColor ink();
QColor subInk();

// The bars: a panel's title bar, and the strip a folded panel leaves at the edge of the window.
QColor bar();
QColor barInk();
QColor strip();
QColor stripInk();
QColor grip();      // the dots on a drag handle

// A fly-out: a panel held over the drawing, so it needs a ground of its own and an edge to be seen against
// whatever is under it.
QColor flyout();
QColor flyoutBorder();
QColor edge();      // the fly-out's own splitter

// Lines: the rule between ribbon groups, borders.
QColor line();

} // namespace Chrome

} // namespace Edraw_Desktop

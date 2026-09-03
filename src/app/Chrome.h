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

// ---- A GROUND OF THE OFFICE'S OWN CHOOSING ------------------------------------------------------
//
// The ribbon and the panels are drawn on one colour, and which colour that should be is a house
// decision rather than a program one: a shop with its drawings on a projector wants a flatter ground
// than one reading them on a monitor at a desk, and some places simply have a colour they use.
//
// KEPT PER SCHEME, because a ground chosen for the dark scheme is meaningless in the light one - the
// two are not a single preference with a brightness knob. An invalid colour means "the built-in one",
// which is how a fresh install and a reset both behave.
void setChosenBack(bool dark, const QColor& c);
QColor chosenBack(bool dark);

// The ground in force, and the lettering to read against it.
//
// `backInk` is not simply the scheme's own ink: somebody who sets a near-black ground while in the
// light scheme would otherwise get near-black text on it and a window that cannot be read at all. So
// the ink follows the GROUND's own lightness, and the scheme decides only what the ground is when
// nobody has chosen one.
QColor back();
QColor backInk();

// The two tones that have to go WITH the ground: every other row of a table, and a column heading.
//
// They were fixed figures, which is fine while the ground is fixed too and obviously wrong the moment it
// is not - a pale alternating row striped across a ground somebody set to charcoal. Both are the ground
// itself, stepped away from it, so a chosen colour brings its own table with it.
QColor backAlt();
QColor backHeader();

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

// Small shared pieces of the interface, drawn rather than typed.
//
// A "+" or a "−" written as TEXT is at the mercy of the font, the theme's button padding and the
// stylesheet: on a small square button it can end up clipped, mis-centred, or missing altogether if
// the glyph is not in the family the style picked. Drawn as an icon it is always there, always the
// same size, and always centred - and every add/remove button in the application looks alike, which
// is what makes them recognisable as add and remove.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <functional>

class QCoreApplication;

class QComboBox;
class QPushButton;
class QToolButton;
class QWidget;

namespace Loopwire {

// ---- Inks that carry a MEANING --------------------------------------------------------------------
//
// The parts library and the windows that hang off it colour their lettering by what it MEANS: a caption,
// a unit, "this part is yours", "attached but not on disk". Those colours were written as hex for the
// dark scheme, which costs nothing while the dark scheme is the only one - and in the light scheme it is
// pale lettering on a pale ground, #9fb4c8 captions on white being the plain case of it.
//
// So each is a PAIR, asked for by meaning rather than by colour, and kept in ONE place: the same caption
// grey was spelled out in four separate windows, which is four chances for three of them to be fixed.
//
// Plain lettering defers to Chrome, so it lands on the same ink as every other panel. The ones that carry
// a meaning keep their hue and change only their depth - making them all dark would make them readable
// and identical, and the whole point of them is that a glance tells them apart.
QColor capInk();    // a caption: what the field beside it is called
QColor faintInk();  // a unit, the "to" between two fields - present, not read
QColor ownInk();    // yours / connected and in use
QColor infoInk();   // connected, or a line of explanation
QColor idleInk();   // not connected, nothing attached
QColor missInk();   // attached, but not on disk
QColor warnInk();   // wrong, or something that has to be said

// The three signs, drawn: a plus, a minus, and a cross for "take this one out".
// A GRIP UNDER A SECTION of a column: dragged up or down, the section above it is made shorter or taller
// and everything after it is pulled up or pushed down with it. `onPress` runs as the drag starts (to fix
// the other sections' heights, say), `onReset` on a double-click. The grip hides when its section does.
QWidget* makeSectionGrip(QWidget* section, QWidget* parent, std::function<void()> onPress = {},
                         std::function<void()> onReset = {});

QIcon plusIcon(const QColor& ink = QColor("#8fd6a2"), int size = 16);
QIcon minusIcon(const QColor& ink = QColor("#e0a0a0"), int size = 16);
QIcon crossIcon(const QColor& ink = QColor("#e0a0a0"), int size = 16);

// An add button: the plus, then the words. `text` may be empty for a square one.
QPushButton* makeAddButton(QWidget* parent, const QString& text = QString(),
                           const QString& tip = QString());
// A remove button. `cross` true draws an ✕ (take this row out) rather than a − (fewer of them).
QPushButton* makeRemoveButton(QWidget* parent, const QString& text = QString(),
                              const QString& tip = QString(), bool cross = false);

// ---- The file glyphs, for the quick-access strip ------------------------------------------------
//
// New, Open, Save and Save all, drawn rather than shipped as images - for the same reason the plus and
// the minus above are drawn: they are always there, always the same size, always centred, and they
// follow the ink they are asked for instead of whatever a bitmap was baked in.
//
// The shapes are the ones everybody already knows: a sheet with a folded corner, an open folder, a
// diskette, and a diskette with another behind it. A save icon has been a diskette for forty years and
// nobody under thirty has seen one - which is exactly why it still works: it is not a picture of a
// floppy disc any more, it is the word "save".
QIcon newFileIcon(const QColor& ink = QColor("#d6dae1"), int size = 18);
QIcon openFileIcon(const QColor& ink = QColor("#d6dae1"), int size = 18);
QIcon saveFileIcon(const QColor& ink = QColor("#d6dae1"), int size = 18);
QIcon saveAllFilesIcon(const QColor& ink = QColor("#d6dae1"), int size = 18);

// ---- The eye ------------------------------------------------------------------------------------

// AN EYE, for a button that shows or hides something drawn OVER a drawing - the connection point a part is
// placed by, its ends, the faces the conductors leave by. Those marks are chrome: while the part itself is
// being looked at they are in the way of seeing it, so they go, and come back.
//
// Struck through when what it shows is hidden, because a toggle that looks the same either way is a button
// you have to press twice to find out what it did.
QIcon eyeIcon(bool shown, const QColor& ink = QColor("#d6dae1"), int size = 20);

// A checkable button carrying that eye, which keeps its own icon and words in step with its state. The
// state is REMEMBERED between sessions, and shared by every one of these: "show me the connection points"
// is one preference about how drawings are looked at, not one per window - having answered it in the
// library, nobody wants to answer it again in the model manager.
QToolButton* makeEyeToggle(QWidget* parent, const QString& shownText, const QString& hiddenText,
                           const QString& tip, const QString& settingsKey = QString());

// ---- Colours -------------------------------------------------------------------------------------

// A colour as a filled circle, for a menu or a combo entry: a wire is round, and a circle reads as a
// wire's colour where a square reads as a fill.
QIcon colorCircleIcon(const QString& hex, int size = 14);

// THE WHEEL SCROLLS, IT DOES NOT CHANGE A FIGURE. In a window of fields - a column that scrolls - rolling the
// wheel over a number box or a drop-down on the way down changed it, and nobody saw it happen. In `window`
// (and anything it opens that is a child of it) the wheel over one of those goes to what is behind it instead.
void wheelScrollsNotFields(QWidget* window);
// THE SAME RULE IN EVERY WINDOW OF THE APP: the wheel never changes a number box or a drop-down - an open drop-down
// list still scrolls. Installed once, at start.
void wheelScrollsNotFieldsAnywhere();

// The two marks a group of wires carries in the builder, drawn the way the drawing draws them: a
// twist is two conductors crossing, a screen is a dashed outline round them. On a merged cell they
// say what the block of rows is without spending a column on the word.
QIcon twistIcon(int size = 14);
QIcon shieldIcon(int size = 14);

// A colour chooser: every harness colour code as a circle and its name, the custom ones that have been
// added, and a "Custom…" entry that defines a new one - named, coloured, and remembered, so from then
// on it is offered like any other. The picked code is in the current item's data.
//
// `allowNone` adds a "none" entry at the top for a field that may be left unsaid.
QComboBox* makeColorBox(QWidget* parent, const QString& current, bool allowNone = true);
// What a colour box is set to ("" for none). Handles the Custom… entry having just been used.
QString colorBoxCode(const QComboBox* box);
// Called after the user picks: if they chose Custom…, this defines the colour and returns its new
// code (and the box is left showing it). Returns the plain code otherwise.
QString colorBoxCommit(QComboBox* box, QWidget* dialogParent);

// A field for a LIST of colours - the conductors of one cable element, in order. A line edit showing
// "WH/BU", with a swatch button whose menu appends a colour picked by eye.
QWidget* makeColorListEditor(QWidget* parent, const QStringList& codes, const QString& placeholder,
                             std::function<void(const QStringList&)> onChange);

// Makes every popup menu in the application OPAQUE. Call once, at startup, before the first window.
//
// The Windows 11 style asks for a translucent window for each menu, so that it can round the corners
// and drop a shadow. Whatever the menu then does not paint for itself is see-through - and a menu
// popped up over a crowded table showed the rows straight through its own words. Clearing that after
// the style has polished the menu (it puts the flag back if it is cleared any earlier) leaves an
// ordinary opaque popup, painted in the colours of the application stylesheet.
void installOpaqueMenus(QCoreApplication* app);

} // namespace Loopwire

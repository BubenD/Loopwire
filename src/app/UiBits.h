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

namespace Edraw_Desktop {

// The three signs, drawn: a plus, a minus, and a cross for "take this one out".
QIcon plusIcon(const QColor& ink = QColor("#8fd6a2"), int size = 16);
QIcon minusIcon(const QColor& ink = QColor("#e0a0a0"), int size = 16);
QIcon crossIcon(const QColor& ink = QColor("#e0a0a0"), int size = 16);

// An add button: the plus, then the words. `text` may be empty for a square one.
QPushButton* makeAddButton(QWidget* parent, const QString& text = QString(),
                           const QString& tip = QString());
// A remove button. `cross` true draws an ✕ (take this row out) rather than a − (fewer of them).
QPushButton* makeRemoveButton(QWidget* parent, const QString& text = QString(),
                              const QString& tip = QString(), bool cross = false);

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

} // namespace Edraw_Desktop

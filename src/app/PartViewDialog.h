// A look at a library part, without leaving what you are doing.
//
// A harness row, a wire on the sheet, a connector in a table: each of them names a part in the library,
// and the question that keeps coming up is "what IS that thing?" - what does its symbol look like, what
// does it look like in the metal, what is it made of. Opening the library, finding the part and coming
// back is four steps and loses your place.
//
// So: a small window that shows one part. Its schematic symbol, its mechanical drawing, and what the
// library knows about it. Read-only on purpose - it is for looking, and the library is for editing.
// General to every kind of part, because the question is the same whatever it is.
#pragma once

#include "harness/PartsLibrary.h"

#include <QDialog>

class QLabel;

namespace Loopwire {

class DrawingPreview;

class PartViewDialog : public QDialog {
    Q_OBJECT
public:
    // Which of the two drawings to open on. Both are always there to switch between; this only says
    // which one the question was about.
    enum class Show { Schematic, Mechanical, Data };

    explicit PartViewDialog(QWidget* parent = nullptr);

    void setPart(const LibraryPart& part, Show which = Show::Schematic);

    // The one call the rest of the application needs: show this part, in a window of its own, without
    // disturbing anything. Re-uses the window when one is already up, so a run of right-clicks does
    // not bury the screen in windows.
    static void showPart(const LibraryPart& part, Show which, QWidget* parent);
    // And by id, out of whatever libraries are connected. False when no such part is connected - the
    // caller says so rather than opening an empty window.
    static bool showPartById(const QString& partId, Show which, QWidget* parent);

private:
    DrawingPreview* schematic_ = nullptr;
    DrawingPreview* mechanical_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* data_ = nullptr;
};

} // namespace Loopwire

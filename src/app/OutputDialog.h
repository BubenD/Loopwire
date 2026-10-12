#pragma once

#include "model/Document.h"

#include <QDialog>
#include <QString>

#include <vector>

class QListWidget;
class QRadioButton;
class QCheckBox;
class QToolButton;
class QLabel;

namespace Loopwire {

// Output: what comes out of the drawing, in what order, and where it goes.
//
// A drawing is a model and a set of sheets, and putting it out is choosing among them: usually the sheets
// and not the model, sometimes one sheet, sometimes the lot for a review pack. So the list here is Model
// followed by every layout, each with a tick, and it can be ARRANGED - the order in the list is the order
// of the pages, because a pack that opens on the wrong sheet is the wrong pack.
//
// The destination is a PDF file or a printer, and the choice changes nothing else: the same pages, laid
// out the same way, painted onto whichever device. What a page IS - a sheet at its own size with its
// windows, or the model's extents - belongs to the space, not to the destination.
class OutputDialog : public QDialog {
    Q_OBJECT
public:
    // `currentSpace` is 0 for model space, 1..n for a layout: it is ticked and shown first when only one
    // thing is likely to be wanted.
    OutputDialog(const Document& doc, int currentSpace, QWidget* parent = nullptr);

    // The spaces to put out, in the order chosen: 0 = model, 1..n = layout n.
    std::vector<int> spaces() const;
    bool toPrinter() const;    // else a PDF file
    bool monochrome() const;   // black lines rather than the drawing's colours
    bool fitToPage() const;    // fit each page to the paper, rather than at its own size
    bool oneFilePerSheet() const;

private:
    void moveSelected(int delta);
    void tickAll(bool on);
    void updateSummary();

    QListWidget* list_ = nullptr;
    QRadioButton* toPdf_ = nullptr;
    QRadioButton* toPrint_ = nullptr;
    QRadioButton* black_ = nullptr;
    QRadioButton* colour_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QCheckBox* perSheet_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace Loopwire

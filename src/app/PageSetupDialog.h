#pragma once

#include "model/PageSetup.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;

namespace Loopwire {

// What a sheet is printed on: the machine, the paper, how much of the sheet goes on it, where, and in what
// ink. Our own arrangement, and our own words - a print setup is a short list of decisions and it reads
// better as one, with a picture of the page beside it so the effect of each is visible rather than
// imagined.
//
// The one decision that is not obvious is FULL BLEED. A printer will not put ink within a few millimetres
// of the paper's edge, so a plain size keeps a margin and a 420 x 297 sheet does not fit on plain A3. A
// full-bleed size has no margin: the sheet comes out at true size, which is what a plotter or a PDF can do
// and what a drawing that will be measured with a rule needs. The preview shows the margin, so the
// difference is something seen rather than a word on a list.
class PageSetupDialog : public QDialog {
    Q_OBJECT
public:
    // `sheetWidthMm`/`sheetHeightMm` are the sheet this setup is for, so the preview can show where it
    // lands on the page. Zero for a setup that belongs to no particular sheet (a named one).
    PageSetupDialog(const PageSetup& setup, const QString& title, double sheetWidthMm = 0.0,
                    double sheetHeightMm = 0.0, QWidget* parent = nullptr);

    PageSetup setup() const;

    // Closing with this instead of Accepted means: get out of the way, the plot window is to be PICKED on
    // the drawing. Whoever opened the dialog does the picking - it is the only one that can, since a modal
    // dialog blocks the canvas however well hidden it is - and then opens the dialog again on what
    // setup() gives back, with the window in it. Nothing else the user had set is lost by going round.
    static constexpr int PickWindowResult = 2;

    // The window picked on the drawing, in the sheet's own millimetres.
    void applyPickedWindow(double x1, double y1, double x2, double y2);

private:
    void refresh();          // the preview and the figures under it
    void pickWindow();       // step aside and have the corners picked on the sheet
    void typeWindow();       // or give them as figures

    PageSetup setup_;
    double sheetW_ = 0.0;
    double sheetH_ = 0.0;

    class Preview;             // the page, drawn small (defined in the .cpp)
    Preview* preview_ = nullptr;
    QComboBox* device_ = nullptr;
    QComboBox* paper_ = nullptr;
    QCheckBox* bleed_ = nullptr;
    QLabel* figures_ = nullptr;
    QComboBox* area_ = nullptr;
    QPushButton* windowBtn_ = nullptr;
    QPushButton* typeWindowBtn_ = nullptr;
    QLabel* windowLabel_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QComboBox* scale_ = nullptr;
    QDoubleSpinBox* offsetX_ = nullptr;
    QDoubleSpinBox* offsetY_ = nullptr;
    QCheckBox* centre_ = nullptr;
    QRadioButton* portrait_ = nullptr;
    QRadioButton* landscape_ = nullptr;
    QCheckBox* upsideDown_ = nullptr;
    QCheckBox* mono_ = nullptr;
    QCheckBox* lineweights_ = nullptr;
    QCheckBox* paperLast_ = nullptr;
};

} // namespace Loopwire

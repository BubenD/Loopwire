#pragma once

#include "model/PageSetup.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;

namespace Edraw_Desktop {

// Page setup: the paper a layout is plotted on, which way round, at what scale, and how the ink comes out.
//
// The one thing here that is not obvious is FULL BLEED. A printer will not put ink within a few
// millimetres of the paper's edge, so a plain size has a margin and a 420 x 297 sheet plotted on plain A3
// must shrink slightly to fit inside it. A full-bleed size has no margin: the sheet comes out at true
// size, which is what a plotter or a PDF can do and what a drawing that will be measured needs. Every size
// is offered both ways, and the printable area is shown so the difference is a figure and not a guess.
class PageSetupDialog : public QDialog {
    Q_OBJECT
public:
    // `title` names what is being set up - a layout, or a named setup.
    PageSetupDialog(const PageSetup& setup, const QString& title, QWidget* parent = nullptr);

    PageSetup setup() const;

private:
    void refreshPaperDetail();

    PageSetup setup_;
    QComboBox* device_ = nullptr;
    QComboBox* paper_ = nullptr;
    QLabel* paperDetail_ = nullptr;
    QComboBox* area_ = nullptr;
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

} // namespace Edraw_Desktop

#pragma once

#include "harness/MechanicalHarness.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Edraw_Desktop {

class Editor;

// Bundle diameter, shown as a calculation rather than as an answer.
//
//     D_bundle = √(Σ D²_wire) × k_packing
//
// The conductors go in a table - read off the drawing, or typed - and the formula comes out with
// this bundle's own numbers substituted into it, line by line, beside an explanation of where the
// figure holds and where it stops holding. k_packing is 1.15 to 1.25: worked out from how many
// conductors there are, or set by hand.
class BundleDiameterDialog : public QDialog {
    Q_OBJECT
public:
    explicit BundleDiameterDialog(Editor* editor, QWidget* parent = nullptr);

    // Take the conductors from the wires drawn on the sheet.
    void readFromDrawing();
    void recalculate();
    const BundleCalc& calculation() const { return calc_; }
    // Testing / driving.
    void setWireDiameters(const std::vector<double>& ods);
    void setPackingFactor(double k);   // <= 0 -> automatic
    std::vector<double> wireDiameters() const;

private:
    void buildUi();
    void addRow(const QString& label, const QString& gauge, double od);
    void refreshWorking();

    Editor* editor_ = nullptr;
    BundleCalc calc_;
    bool syncing_ = false;

    QTableWidget* wires_ = nullptr;
    QCheckBox* autoK_ = nullptr;
    QDoubleSpinBox* kSpin_ = nullptr;
    QLabel* kNote_ = nullptr;
    QLabel* result_ = nullptr;
    QTextBrowser* working_ = nullptr;
};

} // namespace Edraw_Desktop

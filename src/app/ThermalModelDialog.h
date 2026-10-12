#pragma once

#include "analysis/Thermal.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Loopwire {

class Editor;

// The lumped thermal model: how hot the bundle gets, and what that does back to the copper.
//
// The loop is what makes it worth having. Current through resistance makes heat; heat raises the
// temperature; a warmer conductor has more resistance, so it makes more heat. The window shows that
// loop settling round by round, the three thermal resistances that carry the heat away, and the
// formulas with this bundle's own numbers substituted into them - then says what the conductors
// could carry instead, and what size they would have to be.
class ThermalModelDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThermalModelDialog(Editor* editor, QWidget* parent = nullptr);

    void readFromDrawing();
    void recalculate();
    const ThReport& report() const { return report_; }
    // Testing / driving.
    void setWires(const std::vector<ThWire>& wires);
    void setAmbient(double c);
    void setMounting(ThMounting m);

private:
    void buildUi();
    void addRow(const ThWire& w);
    std::vector<ThWire> wiresFromTable() const;
    ThRequest buildRequest() const;
    void refreshResults();

    Editor* editor_ = nullptr;
    ThReport report_;
    bool syncing_ = false;

    QTableWidget* wires_ = nullptr;
    QDoubleSpinBox* ambientSpin_ = nullptr;
    QDoubleSpinBox* targetSpin_ = nullptr;
    QDoubleSpinBox* lengthSpin_ = nullptr;
    QComboBox* mountingBox_ = nullptr;
    QDoubleSpinBox* hSpin_ = nullptr;
    QDoubleSpinBox* kSpin_ = nullptr;
    QDoubleSpinBox* bundleSpin_ = nullptr;

    QLabel* summary_ = nullptr;
    QLabel* advice_ = nullptr;
    QTableWidget* steps_ = nullptr;
    QTextBrowser* working_ = nullptr;
};

} // namespace Loopwire

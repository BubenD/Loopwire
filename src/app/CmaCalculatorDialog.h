#pragma once

#include "analysis/Cma.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace Loopwire {

struct LibraryPart;

// THE CMA CALCULATOR: conductors listed - from the parts list (a wire by its gauge, a cable by its cores and their
// gauge) or typed in (AWG, mm2, kcmil, CMA or a diameter), so many of each - and their circular mil area each and all
// together, with the one AWG and the one metric size that hold as much. And the other way round: the area a run needs
// for a current over a length with no more than so much lost, the smallest AWG and metric size that give it, and whether
// the conductors listed do. Modeless; nothing it does changes the drawing.
class CmaCalculatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit CmaCalculatorDialog(QWidget* parent = nullptr);

    // Driving and testing: a row as it is typed, a library part as it is added, the run to size for.
    void addConductor(const CmaConductor& c);
    bool addPart(const LibraryPart& part, int count = 1);
    void setRun(double amps, double lengthM, double dropVolts, bool aluminium = false);
    const std::vector<CmaConductor>& conductors() const { return rows_; }
    double totalCma() const { return cmaTotal(rows_); }
    QString summary() const;
    QString sizing() const;

private:
    void rebuildTable();
    void refresh();
    void addFromLibrary();

    std::vector<CmaConductor> rows_;
    QTableWidget* table_ = nullptr;
    QLabel* totals_ = nullptr;
    QDoubleSpinBox* amps_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* drop_ = nullptr;
    QComboBox* material_ = nullptr;
    QLabel* sizing_ = nullptr;
    bool syncing_ = false;
};

} // namespace Loopwire

#pragma once

#include "harness/MechanicalHarness.h"

#include <QDialog>
#include <QJsonArray>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Edraw_Desktop {

class Editor;
struct LibraryPart;

// Bundle diameter, shown as a calculation rather than as an answer.
//
//     D_bundle = √(Σ D²_wire) × k_packing
//
// The conductors go in a table - read off the drawing, taken from the parts library, or typed - and
// the formula comes out with this bundle's own numbers substituted into it, line by line, beside an
// explanation of where the figure holds and where it stops holding. k_packing is 1.15 to 1.25:
// worked out from how many conductors there are, or set by hand.
//
// A ROW IS A KIND OF CONDUCTOR, NOT A CONDUCTOR. A bundle is usually so many of this and so many of
// that, so each row carries an amount and stands for that many identical conductors in the sum. The
// alternative is twenty rows saying the same thing, which is twenty places for one of them to be
// wrong.
//
// BOTH UNITS, BOTH WAYS. A cross-section is written in AWG or in mm2 and a diameter in mm or in
// inches, depending on whose drawing it is; whichever is typed, the other follows, so a bundle made
// of an American wire and a European one can be added up without anybody converting by hand.
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
    // One entry per CONDUCTOR - a row of five is five entries - which is what the sum counts.
    std::vector<double> wireDiameters() const;
    // What is in the table, a row at a time, for a test that cares about the rows themselves.
    int rowCount() const;
    int rowAmount(int row) const;
    double rowDiameterMm(int row) const;
    QString rowName(int row) const;

private:
    void buildUi();
    // One kind of conductor. `awg` picks which unit `gauge` is written in; `amount` is how many
    // identical conductors the row stands for.
    void addRow(const QString& name, const QString& gauge, bool awg, double odMm, int amount);
    void duplicateSelected();
    void removeSelected();
    void clearAll();
    // MATCH: take what is in one cell and put it in another. Armed by the button, then the next cell
    // clicked is the SOURCE and the cell that was current when it was armed is where the value goes -
    // so a size typed once can be spread down a table without retyping or re-reading it.
    void armMatch();
    void applyMatch(int sourceRow, int sourceCol);
    // The make-up of this bundle, saved into the DRAWING under a name and got back by it.
    void savePreset();
    void loadPreset();
    QJsonArray tableToJson() const;
    void tableFromJson(const QJsonArray& rows);
    // Everything the calculation came to, on one page: every conductor, how many of each, the answer.
    void showSummary();
    void takeFromLibrary();
    void fillRowFromPart(int row, const LibraryPart& part);
    // The line numbers, after anything is added or taken away.
    void renumber();
    // The two diameter cells always say the same size in their own units.
    void writeDiameter(int row, double mm);
    // The cross-section as wireOuterDiameterMm wants it: "22 AWG" or "0.5".
    QString gaugeForLookup(int row) const;
    // The diameter this row's cross-section implies, written into the diameter cells.
    void diameterFromGauge(int row);
    void refreshWorking();
    QString nextWireName();

    Editor* editor_ = nullptr;
    BundleCalc calc_;
    bool syncing_ = false;
    // Names count up and never come back: taking row 2 away and adding one must not make a second
    // "Wire 2", because the name is how a conductor is spoken about once it is off this screen.
    int nextWireNo_ = 1;
    // Where a matched value is going, and whether we are waiting for the cell to take it from. -1
    // means nothing is armed.
    int matchRow_ = -1;
    int matchCol_ = -1;

    QTableWidget* wires_ = nullptr;
    QCheckBox* autoK_ = nullptr;
    QDoubleSpinBox* kSpin_ = nullptr;
    QLabel* kNote_ = nullptr;
    QLabel* result_ = nullptr;
    QTextBrowser* working_ = nullptr;
    QPushButton* matchBtn_ = nullptr;
};

} // namespace Edraw_Desktop

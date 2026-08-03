// The cable make-up builder: what is inside the jacket, described one element at a time.
//
// A cable's make-up is a list, not a set of fields - "2 screened twisted pairs, 4 singles and a coax"
// cannot be written into boxes - so it gets a table of its own: a row per element, saying what kind it
// is, how many, what cross-section, what colours, and whether it is screened in foil or in braid. The
// overall screen and the drain wire belong to the cable rather than to any element, so they sit above.
//
// What it is for is the harness: everything described here is what the harness builder expands a placed
// cable into, so a pair is drawn twisted and a screened group is drawn screened without anyone typing a
// twist group or a shield group by hand.
#pragma once

#include "harness/CableSpec.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QComboBox;

namespace Edraw_Desktop {

class DrawingPreview;

class CableBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit CableBuilderDialog(QWidget* parent = nullptr);

    void setSpec(const CableSpec& spec);
    CableSpec spec() const { return spec_; }
    // What the cable is called, for the summary and the conductor labels.
    void setCableName(const QString& name);

    // Driving and testing.
    // The shorthand a cable is spoken about in - "3x24AWG + FTP 2x2x28AWG + coax, foil, braid,
    // black" - read into the make-up, and written back out of it.
    void setStructureText(const QString& text);
    QString structureText() const;
    void generateFromStructureText();

    void addElement(CableElementKind kind, int count, const QString& gauge, bool twisted, bool foil,
                    bool braid, const QStringList& colours);
    void removeElement(int row);
    int elementCount() const { return static_cast<int>(spec_.elements.size()); }
    QString summaryText() const;

private:
    void buildUi();
    void reloadTable();
    void readCableFields();
    void refreshSummary();

    CableSpec spec_;
    QString name_;
    bool syncing_ = false;

    QDoubleSpinBox* odSpin_ = nullptr;
    QComboBox* jacketBox_ = nullptr;
    QComboBox* jacketColourBox_ = nullptr;
    QComboBox* materialBox_ = nullptr;
    QCheckBox* overallFoil_ = nullptr;
    QCheckBox* overallBraid_ = nullptr;
    QSpinBox* foilCoverageSpin_ = nullptr;
    QSpinBox* coverageSpin_ = nullptr;
    QComboBox* drainBox_ = nullptr;
    QComboBox* standardBox_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLineEdit* structureEdit_ = nullptr;
    QLabel* structureNote_ = nullptr;
    QLabel* summary_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    DrawingPreview* schematic_ = nullptr;
};

} // namespace Edraw_Desktop

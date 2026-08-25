#pragma once

#include "harness/Bom.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace Edraw_Desktop {

class Editor;

// What the dialog asks MainWindow for: the Options dialog, opened on the BOM page.
class BomConfigOpener {
public:
    virtual ~BomConfigOpener() = default;
    virtual void openBomConfiguration() = 0;
};

// The drawing's bill of materials: line #, description, internal P/N, manufacturer P/N and name,
// quantity and the unit it is counted in.
//
// Three things fill it. The DRAWING itself contributes a row per connector and per wire (those rows
// are read off the drawing every time, so they cannot go stale). The LIBRARY contributes a row for
// a part picked from it - searched by description, P/N or manufacturer. And anything can be typed
// in by hand. Every column heading can be renamed, because a company's own wording belongs on the
// sheet, and quantities of the same part are totalled in one unit (see bomUnify).
class BomDialog : public QDialog {
    Q_OBJECT
public:
    explicit BomDialog(Editor* editor, QWidget* parent = nullptr);
    // Who opens the Options dialog when the Configuration button is pressed (the main window).
    void setConfigOpener(BomConfigOpener* opener) { opener_ = opener; }

    // Testing / driving.
    const BomTable& table() const { return table_; }
    void reload();                       // re-read the drawing and redraw the table
    // Written into the drawing as it stands (and kept, so Cancel from here on does not undo it), and
    // everything back as it was when this window opened.
    void saveNow();
    void cancelChanges();
    void markOpened();   // the bill as it stands is what Cancel will put back
    // How many lines the window is showing, for a test of "one list, two windows".
    int shownRowCount() const;
    void addManualRow();
    void addFromLibrary(const QString& partId, double qty);
    void removeCurrentRow();
    void setUnifyTotals(bool on);
    // The whole configuration at once (what the list works in, which columns reach the drawing).
    void setConfig(const BomConfig& cfg);
    const BomConfig& config() const { return table_.config; }
    std::vector<BomRow> shownRows() const; // what the table is showing, in order
    void placeOnDrawing();                 // the BOM as a table on the sheet
    bool exportCsv(const QString& path);   // the list as it reads, as a CSV file
    // Move the typed row at `from` to `to` (dragging a line in the list). The line numbers follow
    // the new order - they are the rows' places, not something a row carries.
    void moveRow(int from, int to);

signals:
    // The bill has been written to. The side panel shows the same bill, and a line added in one place has to
    // appear in the other - two views of one list that disagree is worse than one view.
    void bomChanged();

private:
    void buildUi();
    // The bill as the drawing had it when this window opened: what Cancel puts back.
    QJsonObject openedWith_;
    void refreshTable();
    void refreshLibraryList();
    int shownColumnCount() const;
    void loadControlsFromConfig(); // the configuration into the controls that show it
    void storeTable();     // manual rows + headings back onto the drawing
    void loadTable();      // ...and out of it again

    Editor* editor_ = nullptr;
    BomConfigOpener* opener_ = nullptr;
    PartsCatalog catalog_;
    PartsLibrary lib_; // every connected library merged, for adding from
    BomTable table_;
    bool unify_ = true;
    bool syncing_ = false;

    QTableWidget* view_ = nullptr;
    QTableWidget* libView_ = nullptr;
    QLineEdit* libFilter_ = nullptr;
    QCheckBox* unifyBox_ = nullptr;
    QCheckBox* fromDrawingBox_ = nullptr;
    // Configuration: what the list works in, and which columns reach the drawing.
    QComboBox* lengthUnitBox_ = nullptr;
    QDoubleSpinBox* spareSpin_ = nullptr;
    QSpinBox* decimalsSpin_ = nullptr;
    QCheckBox* roundUpBox_ = nullptr;
    QList<QCheckBox*> columnBoxes_;
    QLabel* summary_ = nullptr;
    QLabel* cfgSummary_ = nullptr;
};

} // namespace Edraw_Desktop

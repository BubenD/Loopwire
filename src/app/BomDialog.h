#pragma once

#include "harness/Bom.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace Edraw_Desktop {

class Editor;

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

    // Testing / driving.
    const BomTable& table() const { return table_; }
    void reload();                       // re-read the drawing and redraw the table
    void addManualRow();
    void addFromLibrary(const QString& partId, double qty);
    void removeCurrentRow();
    void setUnifyTotals(bool on);
    std::vector<BomRow> shownRows() const; // what the table is showing, in order
    void placeOnDrawing();                 // the BOM as a table on the sheet

private:
    void buildUi();
    void refreshTable();
    void refreshLibraryList();
    void storeTable();     // manual rows + headings back onto the drawing
    void loadTable();      // ...and out of it again

    Editor* editor_ = nullptr;
    PartsLibrary lib_;
    BomTable table_;
    bool unify_ = true;
    bool syncing_ = false;

    QTableWidget* view_ = nullptr;
    QTableWidget* libView_ = nullptr;
    QLineEdit* libFilter_ = nullptr;
    QCheckBox* unifyBox_ = nullptr;
    QCheckBox* fromDrawingBox_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace Edraw_Desktop

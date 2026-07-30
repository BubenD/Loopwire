#pragma once

#include "harness/PartsLibrary.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

class Document;

// The bill of materials of a drawing: one row per thing to buy, in the order they are listed.
//
// A row's quantity carries the UNIT it is counted in, because a harness mixes them - wire by the
// metre, heatshrink by the millimetre, connectors each. Rows that name the same part are totalled
// into the unit of the first of them (see bomUnify), so a length given in mm on one row and m on
// another still comes out as one line; quantities in units that cannot be added (metres and
// pieces) are never merged.
struct BomRow {
    QString description;
    QString internalPn;
    QString mfrPn;
    QString mfrName;
    double qty = 0.0;
    QString unit = QStringLiteral("each");
    // Where the row came from: a library part's id when it was added from the library, and the
    // drawing's own reference (a connector designator, a wire label) when the drawing implied it.
    QString partId;
    QString ref;
    bool fromDrawing = false; // true = the drawing put it there; false = typed in by hand

    QString qtyText() const;
};

// The seven columns, in order. The labels are the DEFAULTS: a drawing may rename any of them, so a
// company's own wording ends up on the sheet.
QStringList bomDefaultHeaders();
int bomColumnCount();

// A drawing's bill of materials: its column labels and its rows.
struct BomTable {
    QStringList headers = bomDefaultHeaders();
    std::vector<BomRow> rows;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
    // Header `i`, or the default when the drawing has not renamed it.
    QString header(int i) const;
    void setHeader(int i, const QString& text);
};

// One row per part, quantities added up in the unit of the first row of each part. Rows are matched
// on the part they name (its library id when it has one, else its manufacturer P/N, else its
// description), so the same part entered twice - or drawn twice - is one line. A row whose unit
// cannot be added to the one already totalled is kept as its own line rather than being forced.
std::vector<BomRow> bomUnify(const std::vector<BomRow>& rows);

// What the drawing itself calls for: every connector and every wire in it, as rows. A part id
// stamped on an entity by the harness builder is looked up in `lib`, so the row carries the
// library's description, part numbers and unit; without one, the row is filled from the entity.
std::vector<BomRow> bomFromDrawing(const Document& doc, const PartsLibrary& lib);

// A library part as a BOM row, `qty` of it.
BomRow bomRowFromPart(const LibraryPart& part, double qty);

} // namespace Edraw_Desktop

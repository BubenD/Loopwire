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
    // The row's own identity, so two rows that name nothing yet are still two rows. Set when a row
    // is created; kept in the file, so a saved BOM comes back as the rows it was.
    QString rowId;
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

    QString qtyText(int decimals = 2) const;
};

// How a drawing's bill of materials is worked out and shown. Kept with the drawing, so a sheet
// keeps the units and the columns it was set up with.
struct BomConfig {
    // The unit LENGTHS are shown and totalled in. A drawing is in millimetres, but a harness is
    // usually bought and listed by the metre; a row counted in pieces is not touched by this.
    QString lengthUnit = QStringLiteral("m");
    // Total the rows that name the same part into one line.
    bool unify = true;
    // Include what the drawing itself calls for (a row per connector and per wire).
    bool includeDrawing = true;
    // Decimals shown on a quantity. A whole figure never shows any.
    int qtyDecimals = 2;
    // Spare added to every LENGTH, as a percentage: wire is cut with some to spare.
    double sparePercent = 0.0;
    // Round a length up to the next whole unit - what a purchase order usually wants.
    bool roundUpLengths = false;
    // Which columns go onto the drawing when the table is placed. Empty = all of them.
    QList<bool> columns;

    bool columnShown(int i) const;
    void setColumnShown(int i, bool on);
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
};

// The seven columns, in order. The labels are the DEFAULTS: a drawing may rename any of them, so a
// company's own wording ends up on the sheet.
QStringList bomDefaultHeaders();
int bomColumnCount();

// A drawing's bill of materials: its column labels and its rows.
struct BomTable {
    QStringList headers = bomDefaultHeaders();
    std::vector<BomRow> rows;
    BomConfig config;

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

// The rows as the configuration says they should read: every length in the working unit, with the
// spare added and rounded up if that is asked for. Run BEFORE totalling, so the lines add up in the
// unit they are shown in.
std::vector<BomRow> bomApplyConfig(const std::vector<BomRow>& rows, const BomConfig& cfg);

// A library part as a BOM row, `qty` of it.
BomRow bomRowFromPart(const LibraryPart& part, double qty);
// An id nothing in `rows` is using, for a row about to be added.
QString bomNextRowId(const std::vector<BomRow>& rows);

} // namespace Edraw_Desktop

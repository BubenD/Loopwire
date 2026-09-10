#pragma once

#include "harness/PartsLibrary.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace Edraw_Desktop {

class Document;
struct MechHarnessDef;

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
    // MILLIMETRES, because that is what the drawing is in. A bill in metres of a harness drawn in
    // millimetres asks everybody reading it to divide by a thousand in their head, and the one place a
    // rounding creeps in is the place nobody checks.
    QString lengthUnit = QStringLiteral("mm");
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
// HOW MANY MILLIMETRES ONE OF THIS UNIT IS, or 0 for a unit that is not a length at all ("each", "off").
// A quantity in one length unit is the same quantity in another - 2 m IS 2000 mm - so changing the unit on a
// line converts what is on it rather than relabelling it, which would be the program saying something untrue
// about how much to buy.
double lengthUnitInMm(const QString& unit);
// A quantity carried from one unit to another. Between two lengths it is converted; anything else (pieces to
// metres) is left alone, because there is no answer - a hundred grommets are not any number of metres.
double convertQuantity(double qty, const QString& fromUnit, const QString& toUnit);

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

// Whether two rows name the same THING TO BUY - the very test the totalling uses (see bomUnify): the
// library id when a row came from the library, else the P/N it is ordered against, else what it says
// it is. Wanted on its own by anything that has to ask "is this already on the bill?" before adding a
// line, so that question and the totalling can never answer differently.
//
// A row that says nothing at all about what it is - no part, no P/N, no description - is the same
// thing as NOTHING, itself included: an empty line is a place to type in, not a part that the next
// empty line is one more of.
bool bomSameThing(const BomRow& a, const BomRow& b);

// For every row of `adding`, the line of `rows` that already names the same thing, or -1 when nothing
// does. One entry per row of `adding`, in its order.
//
// Asked against the WHOLE bill, hand-typed lines and lines the drawing calls for alike, because a part
// already on the bill is already on the bill whichever of them put it there.
std::vector<int> bomAlreadyListed(const std::vector<BomRow>& rows, const std::vector<BomRow>& adding);

// A mechanical harness as lines of a bill of materials: every connector, backshell, boot, heatshrink,
// braid, cable and conductor the definition calls for, in the order a drawing lists them and totalled
// per part number - which is to say `mechBom`, said in BomRows so it can go onto a drawing's own bill.
//
// The quantity is kept in the unit the harness worked it out IN rather than the unit the part would
// default to: the figure and the word beside it have to say the same thing.
std::vector<BomRow> bomRowsFromMechHarness(const MechHarnessDef& def, const PartsCatalog* catalog);

// WHICH LINE OF THIS DRAWING'S BILL A PART IS ON - 1-based, or 0 when the bill does not list it.
//
// Made for MechDrawOptions::bomItemOf, which is what a generated harness numbers its balloons from: a
// balloon saying 11 is a reference to line 11 of the bill on that sheet, so the number has to be that
// line's position in that bill. 0 comes out as "?" on the drawing, which is what a part nobody has put
// on the bill honestly deserves.
//
// The bill is read as the drawing SHOWS it - the typed lines plus what the drawing itself calls for,
// totalled if the sheet totals them - because the numbers a reader counts down the table are the
// numbers of the lines they can see.
//
// The rows are captured when this is made, so the lookup is a snapshot: balloons carry the numbering
// as it stood when the harness was generated.
std::function<int(const QString& libPart, PartKind kind, const QString& description)>
bomItemLookup(const Document& doc, const PartsLibrary& lib);
// An id nothing in `rows` is using, for a row about to be added.
QString bomNextRowId(const std::vector<BomRow>& rows);

} // namespace Edraw_Desktop

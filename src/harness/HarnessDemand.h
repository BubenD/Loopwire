#pragma once

#include "harness/HarnessBuilder.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace Loopwire {

// ---- A harness demand -------------------------------------------------------
// What a customer without Loopwire is asked for, in a workbook they fill in with Excel: a PINOUT TABLE per cable,
// the way a drawing office lays one out. A cable is a block of rows, one row per connector - its type, pins, name,
// mating part - and one column per signal; where a signal's column crosses a connector's row is the pin it lands
// on there. Above the signal columns a row says which are twisted together, shielded together, both, or a coax.
// An overview: no library parts, no contacts, no layout. Read back, a cable becomes a builder definition
// (harnessDefFromDemand) the schematic harness builder opens and generates from.
//
// Pure data and pure functions, like the builder's own model: the workbook is bytes in and bytes out.

struct DemandInfo {
    QString customer, project, partNumber, revision, date, contact, email, notes;
};

// One connector of a cable: a row of the table.
struct DemandConnector {
    QString name;           // "43-J2" - a second line, if any, is a description ("43-J2\nCOMM")
    QString type;           // its part number or type
    QString image;          // what the IMG cell says (a picture is not read)
    int maxPins = 0;        // 0: not said (N/A)
    QString length;         // the length to it, as written
    QString mating;         // the mating connector
    QStringList cells;      // per signal: the pin(s) there - "5", "21,25", a letter - or a lead's colour
    int sheetRow = 0;       // where it was read from (1-based), 0 when not read from a sheet
};

// One signal: a column of the table.
struct DemandSignal {
    QString name;
    // The group the row above it puts it in: "Twist", "Shield", "Shield+Twist" or "Coax" ("" none), and which one
    // of them (columns of one group share the number).
    QString group;
    int groupId = -1;
};

struct DemandCable {
    QString name;           // the cable's number - "HGC700150"
    QString function;       // what it does - "Tilt - GR43 to Ebox"
    std::vector<DemandConnector> connectors;
    std::vector<DemandSignal> signalColumns;
};

struct HarnessDemand {
    DemandInfo info;
    std::vector<DemandCable> cables;
};

// Something in a demand that could not be read as it was meant: where (the cable, and the sheet row when it was
// read from one) and what.
struct DemandIssue {
    QString sheet;
    int row = 0;
    QString text;
};

// THE WORKBOOK: Instructions, Info and the Pinout sheet - a block per cable, or one empty block for a template.
QByteArray harnessDemandToXlsx(const HarnessDemand& demand);
// And read back from what Excel (or anything else) saved: every block on any sheet whose heading row has a Cable
// Name, its columns found by their headings, merged cells spread over what they cover. False with `error` set
// when it is not a demand workbook at all.
bool harnessDemandFromXlsx(const QByteArray& bytes, HarnessDemand& out, QString* error = nullptr);

// A CABLE AS A DEFINITION: its first connector on the left and the rest on the right, a wire for each place a
// signal lands beyond its first (a pin on the first connector to each other one, two pins to two pins in order),
// a lead's colour as a flying lead, the twisted, shielded and coax groups. What could not be taken as written is
// reported.
HarnessDef harnessDefFromDemand(const DemandCable& cable, std::vector<DemandIssue>* issues = nullptr);
// And a definition as a cable: a builder's harness sent out to be checked or finished by the customer.
DemandCable harnessDemandFromDef(const HarnessDef& def, const QString& name = QString());

// THE DEMAND ON PAPER, as a PDF: a page per cable - the details, the harness drawn by the builder, and the
// pinout table as the customer wrote it. For sending back to confirm what was understood.
bool writeHarnessDemandPdf(const HarnessDemand& demand, const QString& path, QString* error = nullptr);

} // namespace Loopwire

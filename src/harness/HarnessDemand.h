#pragma once

#include "harness/HarnessBuilder.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace Loopwire {

// ---- A harness demand -------------------------------------------------------
// What a customer without Loopwire is asked for, in a workbook they fill in with Excel: who wants the harness,
// its connectors and which pin goes to which, and how the wires are made up - twisted together, shielded, coax.
// An overview: no library parts, no contacts, no layout. Read back, it becomes a builder definition
// (harnessDefFromDemand) the schematic harness builder opens and generates from.
//
// Pure data and pure functions, like the builder's own model: the workbook is bytes in and bytes out.

struct DemandInfo {
    QString customer, project, harnessName, partNumber, revision, date, contact, email, notes;
};

struct DemandConnector {
    QString designator;     // J1, P2 ...
    QString end;            // "A" (drawn on the left) or "B" (on the right)
    QString description;
    QString matingPart;     // the mating connector or its type, if known
    int pins = 0;           // 0: as many as the connections use
    QString pinNames;       // "Numbers" or "Letters"
    bool overallShield = false;
    QString drainTo;        // "None", "Body" or "Pin"
    QString drainPin;
    QString notes;
};

struct DemandConnection {
    QString from, fromPin, to, toPin;   // connector designators and pin names; an empty connector is a flying lead
    QString signal;
    QString type;           // "Wire" or "Coax"
    QString twistGroup, shieldGroup;
    QString gauge, color;
    QString coaxShieldFromPin, coaxShieldToPin;   // where a coax's braid lands at each end
    QString notes;
};

struct DemandGroup {
    QString name;
    QString kind;           // "Twisted" or "Shielded"
    QString drainConnector; // a shielded group's drain: the connector and pin it is taken to
    QString drainPin;
    QString notes;
};

struct HarnessDemand {
    DemandInfo info;
    std::vector<DemandConnector> connectors;
    std::vector<DemandConnection> connections;
    std::vector<DemandGroup> groups;
};

// Something in a demand that could not be read as it was meant: the sheet and the row it is on (1 = the
// heading row, so the first filled row is 2; 0 = the sheet as a whole).
struct DemandIssue {
    QString sheet;
    int row = 0;
    QString text;
};

// THE WORKBOOK: Instructions, Info, Connectors, Connections, Groups, and the lists the drop-downs offer. Written
// with whatever the demand holds - nothing for a blank template.
QByteArray harnessDemandToXlsx(const HarnessDemand& demand);
// And read back from what Excel (or anything else) saved. Columns are found by their headings, so a sheet with
// its columns moved about still reads. False with `error` set when it is not a demand workbook at all.
bool harnessDemandFromXlsx(const QByteArray& bytes, HarnessDemand& out, QString* error = nullptr);

// A DEMAND AS A DEFINITION: each connector on its end, each connection a wire (a coax its two rows), the twisted
// and shielded groups, the overall shields and their drains. What could not be taken as written is reported.
HarnessDef harnessDefFromDemand(const HarnessDemand& demand, std::vector<DemandIssue>* issues = nullptr);
// And a definition as a demand: a builder's harness sent out to be checked or finished by the customer.
HarnessDemand harnessDemandFromDef(const HarnessDef& def);

// THE DEMAND ON ONE SHEET OF PAPER, as a PDF: the details, the harness drawn by the builder, and each
// connector's pinout. For sending back to the customer to confirm what was understood.
bool writeHarnessDemandPdf(const HarnessDemand& demand, const QString& path, QString* error = nullptr);

} // namespace Loopwire

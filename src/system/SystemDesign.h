#pragma once

#include <QColor>
#include <QRectF>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

// THE SYSTEM DIAGRAM: the units of a system - boxes, each with its part number and revision - their ports (where a
// cable plugs in), and the cables between ports. Each cable has its own part number and revision and can be linked to
// the drawing that defines it (a harness drawing, DWX or DXF), so the diagram can say whether the drawing linked is
// the revision the system calls for. Saved with the drawing.

// A PORT ON A UNIT: where a cable plugs in - on one of its sides, so far along it (0 to 1), named, with the connector it
// takes and how many pins. A connector through the unit's wall: its inside (always there, inside the wall) and its
// outside (drawn once a cable is plugged into it from outside), each with its own connector part number.
struct SysPort {
    QString id;
    QString name;
    QString side = QStringLiteral("right");   // "left", "right", "top", "bottom"
    double at = 0.5;
    QString connector;   // the outside's mating connector part number, as the system calls for it
    QString insidePn;    // the inside's connector part number
    int pins = 0;
};

// A UNIT: a box of the system - a controller, a sensor, a panel - where it is drawn, and its ports. What it is: a
// mechanical part ("mech", as it starts) or a printed circuit board assembly ("pcba").
struct SysUnit {
    QString id;
    QString kind = QStringLiteral("mech");
    QString name;
    QString pn;
    QString rev;
    QString description;
    double x = 0.0, y = 0.0, w = 160.0, h = 100.0;
    QColor color;          // none: as units of its kind are
    double opacity = 1.0;  // how solid its fill is, 0 to 1
    // THE UNIT IT IS IN - a PCBA in its housing - kept inside it, moved with it. Empty: in none.
    QString parent;
    // WHAT IS NOT SHOWN ON THE DIAGRAM of its own: "name", "pn", "rev", "desc".
    QStringList hidden;
    bool shows(const QString& what) const { return !hidden.contains(what); }
    QRectF rect() const { return QRectF(x, y, w, h); }
    std::vector<SysPort> ports;
    const SysPort* port(const QString& id) const;
    SysPort* port(const QString& id);
};

// A CABLE between two ports: its name (W1, W2...), part number and revision, how long, how many wires, and the drawing
// that defines it - with the number and revision read off that drawing when it was last checked.
struct SysCable {
    QString id;
    QString name;
    QString pn;
    QString rev;
    QString description;
    QString aUnit, aPort, bUnit, bPort;
    // AN END ON A PORT'S INSIDE: from within the unit - another port of the same unit, or a unit inside it.
    bool aInside = false, bInside = false;
    double lengthMm = 0.0;
    int wires = 0;
    QColor color;
    QString drawing;      // the file, as linked
    QString drawingNo;    // read off it at the last check
    QString drawingRev;
    // WHAT IS NOT SHOWN ON ITS LABEL: "name", "pn", "rev", "desc" (its description, as it starts).
    QStringList hidden{QStringLiteral("desc")};
    bool shows(const QString& what) const { return !hidden.contains(what); }
};

// WHAT A DRAWING SAYS ABOUT ITSELF: its drawing number and its current revision, read off its title block (the field
// "drawingNo"), its revision table (the last revision written in it), its revision boxes and its template.
struct DrawingInfo {
    bool found = false;   // the file is there and could be read
    QString drawingNo;
    QString revision;
    QString title;
    // THE DESIGNATORS IN IT, in the order they are drawn: its cables' (W1, W2...) and its connectors' (X1, J2...).
    QStringList cables;
    QStringList connectors;
};

// A NAME ON THE DIAGRAM THE DRAWING CALLS OTHERWISE: the cable's own, or a port at one of its ends - from what to what.
struct DesignatorChange {
    bool cable = false;
    QString unit, port;   // the port's (a port only)
    QString from, to;
};
DrawingInfo readDrawingInfo(const QString& path);

// ONE THING THE CHECK FOUND: how bad (0 a note, 1 a warning, 2 an error), what, and on what.
struct SysIssue {
    int level = 1;
    QString what;
    QString unit;
    QString cable;
};

// A NOTE ON THE DIAGRAM: text in a box, where it is put.
struct SysNote {
    QString id;
    QString text;
    double x = 0.0, y = 0.0, w = 180.0, h = 70.0;
};

// HOW THE LINK OF A CABLE TO ITS DRAWING STANDS: none linked, the file not there, its revision not the one the cable
// calls for, or matching.
enum class SysLinkState { None, Missing, Mismatch, Match };

struct SystemDesign {
    QString name;
    QString pnPrefix = QStringLiteral("CBL-");
    int pnDigits = 3;   // how many digits the number after the prefix has (CBL-001)
    std::vector<SysUnit> units;
    std::vector<SysCable> cables;
    std::vector<SysNote> notes;

    bool empty() const { return units.empty() && cables.empty() && notes.empty(); }
    const SysNote* note(const QString& id) const;
    SysNote* note(const QString& id);
    // A NOTE put at a place; its id. One taken away.
    QString addNote(double x, double y, const QString& text = QString());
    void removeNote(const QString& id);
    // UNITS IN UNITS: those right inside one (empty: those in none), whether one is somewhere inside another, how deep
    // one is; a unit moved with everything inside it; the box everything inside a unit takes.
    std::vector<QString> childrenOf(const QString& unit) const;
    bool isWithin(const QString& unit, const QString& ancestor) const;
    int depth(const QString& unit) const;
    void moveUnit(const QString& unit, double dx, double dy);
    QRectF contentsOf(const QString& unit) const;
    // THE UNIT A BOX WOULD BE IN: the innermost one round the point (not `except`, nor one inside it).
    QString unitAround(const QPointF& point, const QString& except = QString()) const;
    // The cables plugged into one side of a port - its inside or its outside.
    std::vector<const SysCable*> cablesAt(const QString& unit, const QString& port, bool inside) const;
    const SysUnit* unit(const QString& id) const;
    SysUnit* unit(const QString& id);
    const SysCable* cable(const QString& id) const;
    SysCable* cable(const QString& id);

    // A NEW UNIT, named U1, U2... where none is given, at a place; its id.
    QString addUnit(double x, double y, const QString& name = QString());
    // A NEW PORT on a unit's side, so far along; named P1, P2... on that unit; its id (empty: no such unit).
    QString addPort(const QString& unit, const QString& side, double at, const QString& name = QString());
    // A NEW CABLE from one port to another, named W1, W2...; its id (empty where either port is not there, or they are
    // the same port).
    QString addCable(const QString& aUnit, const QString& aPort, const QString& bUnit, const QString& bPort);
    // A unit taken away with its cables and every unit inside it; a port with its cables; a cable.
    void removeUnit(const QString& id);
    void removePort(const QString& unit, const QString& port);
    void removeCable(const QString& id);
    // The cables plugged into a port.
    std::vector<const SysCable*> cablesAt(const QString& unit, const QString& port) const;

    // PART NUMBERS for the cables that have none: the prefix and the next free number, so many digits (CBL-001). How many.
    int numberCables();

    // How a cable's link to its drawing stands - against the number and revision last read off it.
    static SysLinkState linkState(const SysCable& c);
    // EACH CABLE'S DRAWING READ AGAIN: its number and revision off the file (by `read`, readDrawingInfo where none is
    // given). How many were read.
    int refreshDrawings(const std::function<DrawingInfo(const QString&)>& read = {});

    // THE DRAWING'S DESIGNATORS SET AGAINST THE DIAGRAM'S: the cable's name against the drawing's cable, the ports at its
    // ends against the drawing's connectors (one already alike kept where it is, the other given the one left; neither
    // alike - in the order drawn). Nothing where they agree, or the drawing names none.
    std::vector<DesignatorChange> designatorChanges(const QString& cable, const DrawingInfo& info) const;
    void applyDesignatorChanges(const QString& cable, const std::vector<DesignatorChange>& changes);

    // THE CHECK: ports with nothing plugged in, or more than one cable; cables with no part number, the same part number
    // on cables that are not alike, no drawing linked, the drawing not found, its revision or number not the cable's;
    // units with no part number.
    std::vector<SysIssue> check() const;

    // THE CABLE LIST, one line a cable, as comma-separated values with a heading line.
    QString cableListCsv() const;

    QJsonObject toJson() const;
    static SystemDesign fromJson(const QJsonObject& o);

    // A fresh id for a kind of thing ("u", "p", "c").
    QString freshId(const QString& prefix) const;
};

} // namespace Edraw_Desktop

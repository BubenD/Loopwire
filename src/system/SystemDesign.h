#pragma once

#include <QColor>
#include <QRectF>
#include <QJsonObject>
#include <QJsonArray>
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
// takes and its contact part number. The inside/outside cable attachment remains physical geometry; the port has
// one connector P/N. Older inside P/N and contact counts are retained when an existing design is saved.
struct SysPort {
    QString id;
    QString name;
    QString side = QStringLiteral("right");   // "left", "right", "top", "bottom"
    double at = 0.5;
    QString connector;   // the port's connector part number
    QString insidePn;    // legacy data, retained for older drawings
    int pins = 0;        // legacy contact count, retained for older drawings
    QString contactPn;
    QString description;   // what the port is for ("CAN bus", "24 V in")
    double labelDx = 0.0, labelDy = 0.0;   // designator offset from its default place above the connection
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
    QByteArray picture;    // a picture shown in it (PNG), none: no picture
    bool pictureShown = true;    // drawn in the unit, or kept but not drawn
    double pictureScale = 1.0;   // how much of the room in the unit it takes, 0.1 to 1
    // WHERE IN THE ROOM IT SITS: "" (the middle), "top", "bottom", "left", "right", "top-left", "top-right",
    // "bottom-left", "bottom-right".
    QString pictureAlign;
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
// A FURTHER END OF A HARNESS: the harness spread to one more port, branching off its trunk.
struct SysBranch {
    QString unit, port;
    bool inside = false;   // on the port's inside, as a cable's end may be
    QPointF at;            // A LOOSE END - no unit - where it ends on the diagram
};

struct SysCable {
    QString id;
    QString name;
    QString pn;
    QString rev;
    QString description;
    QString aUnit, aPort, bUnit, bPort;
    // A LOOSE END - its unit empty, taken off its port (Shift+drag of the unit) - where it ends on the diagram.
    QPointF aAt, bAt;
    // AN END ON A PORT'S INSIDE: from within the unit - another port of the same unit, or a unit inside it.
    bool aInside = false, bInside = false;
    // THE HARNESS SPREAD TO MORE PORTS: each further end a branch off the trunk from a to b.
    std::vector<SysBranch> branches;
    double lengthMm = 0.0;
    int wires = 0;
    QColor color;
    std::vector<QPointF> waypoints;
    QString drawing;      // the file, as linked
    QString drawingNo;    // read off it at the last check
    QString drawingRev;
    QString drawingTitle;
    QString expectedTitle;
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
    struct Connection {
        QString name, fromConnector, fromContact, toConnector, toContact;
        QString issue;
        int level = 0;
    };
    std::vector<Connection> connections;
    bool schematic = false;
    QStringList fieldIssues;
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
    QString rule;
    QString port;
    QString note;
    QString key() const;
};

// A NOTE ON THE DIAGRAM: text in a box, where it is put.
struct SysNote {
    QString id;
    QString text;
    double x = 0.0, y = 0.0, w = 180.0, h = 70.0;
    QColor color;
    QString unit, port, cable;   // optional leader to one unit, port or cable
    bool showLink = true;        // the leader drawn; hidden, the note stays linked all the same
};

// HOW THE LINK OF A CABLE TO ITS DRAWING STANDS: none linked, the file not there, its revision not the one the cable
// calls for, or matching.
enum class SysLinkState { None, Missing, Mismatch, Match };

struct SysConnection {
    QString cable, line, from, to, status;
    int level = 0;
    QString aUnit, aPort, aContact, bUnit, bPort, bContact;
};

// A SHEET ROUND THE DIAGRAM: a drawing template's frame and title block - its content kept here, so the design carries it
// wherever it goes - where it lies on the diagram, how big (the diagram's units to the template's millimetre), and what is
// written in its fields.
struct SysSheet {
    QString name;               // the template's; empty: no sheet
    QJsonArray content;         // its frame and title block, as the template draws them (millimetres, up the page)
    double widthMm = 420.0, heightMm = 297.0;
    double x = 0.0, y = 0.0;    // its top left corner on the diagram
    double scale = 2.0;         // diagram units to the millimetre
    QJsonObject fields;         // "title", "drawingNo", "revision"... written into the template's fields
    // WHERE A FIELD'S TEXT COMES FROM, by its key: one of sysFieldSources() - the project's name, its drawing number,
    // the latest revision, the date... None: what is typed in `fields`.
    QJsonObject sources;
    // A LOGO OF ITS OWN in the template's logo box (PNG); none: the template's, where it has one.
    QByteArray logo;
    QJsonArray layers;          // the template's layers, its frame's colours with them
    bool empty() const { return name.isEmpty(); }
    QRectF rect() const { return QRectF(x, y, widthMm * scale, heightMm * scale); }
};

// ONE ISSUE OF THE DESIGN, as a template's revision table lists it: its columns, by the same names.
struct SysRevision {
    QString rev, note, eco, drawn, checked, approved, date;
};

// WHAT A SHEET'S FIELD CAN TAKE ITS TEXT FROM: the id kept in SysSheet::sources and what the list says for it. The first
// ("") is the text typed in.
const std::vector<std::pair<QString, QString>>& sysFieldSources();

struct SystemDesign {
    QString name;
    SysSheet sheet;
    QString pnPrefix = QStringLiteral("CBL-");
    QStringList disabledRules;
    QJsonObject ruleLevels;
    std::vector<SysIssue> bypassed;
    // MORE SHEETS than the first: each a template round its own part of the diagram, a page of its own in the PDF.
    std::vector<SysSheet> moreSheets;
    // THE REVISIONS, oldest first: written into every sheet's revision table, the last the revision a field can take.
    std::vector<SysRevision> revisions;
    // WHERE THE TEXT SITS in the revision table's cells, a column each in SysRevision's order: "TL" to "BR"; "" (or none):
    // as the template has it.
    QStringList revisionAlign;
    // A SHEET BY ITS PLACE: 0 the first (`sheet`), then `moreSheets`; how many there are (an empty first counts none).
    int sheetCount() const { return sheet.empty() ? 0 : 1 + static_cast<int>(moreSheets.size()); }
    const SysSheet* sheetAt(int i) const { return i == 0 ? &sheet : i > 0 && i <= static_cast<int>(moreSheets.size()) ? &moreSheets[static_cast<size_t>(i - 1)] : nullptr; }
    SysSheet* sheetAt(int i) { return const_cast<SysSheet*>(static_cast<const SystemDesign*>(this)->sheetAt(i)); }
    // WHAT A SHEET'S FIELDS SAY: what is typed, or what their source gives. Its content with them written in, and the
    // revisions in its revision table - the newest where there are more than it has rows.
    QJsonObject sheetTexts(const SysSheet& s) const;
    QJsonArray sheetContent(const SysSheet& s) const;
    QColor background = QColor(48, 52, 58);
    // THE DESIGNATORS' COLOUR - units', ports' and cables' names. None: Auto, each one dark or light against what is
    // behind it, so a label dragged from a pale unit onto a dark canvas stays readable.
    QColor designatorColor;
    int ruleSeverity(const QString& rule, int defaultLevel = 2) const {
        return disabledRules.contains(rule) ? 0 : ruleLevels.value(rule).toInt(defaultLevel);
    }
    int pnDigits = 3;   // how many digits the number after the prefix has (CBL-001)
    std::vector<SysUnit> units;
    std::vector<SysCable> cables;
    std::vector<SysNote> notes;

    bool empty() const { return units.empty() && cables.empty() && notes.empty() && disabledRules.isEmpty()
                               && ruleLevels.isEmpty() && bypassed.empty() && background == QColor(48, 52, 58)
                               && !designatorColor.isValid() && sheet.empty() && moreSheets.empty() && revisions.empty()
                               && revisionAlign.isEmpty(); }
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
    // A FURTHER END for a harness: a branch off it to one more port. False where the port is not there or already an end.
    bool addBranch(const QString& cable, const QString& unit, const QString& port);
    // COPY AND PASTE: units (with everything inside them), the cables among them and notes, as a piece of a design; that
    // piece put in, moved by an offset, everything given fresh ids and names that are free. The ids put in.
    QJsonObject copyPiece(const QStringList& units, const QStringList& cables, const QStringList& notes) const;
    QStringList pastePiece(const QJsonObject& piece, const QPointF& offset);
    // A unit taken away with its cables and every unit inside it; a port with its cables; a cable.
    // Keeping contents promotes immediate children to this unit's parent without moving them.
    void removeUnit(const QString& id, bool removeContents = true);
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
    // ALL LINKED SCHEMATICS: named electrical paths, and names at shared unit/port/contact interfaces.
    std::vector<SysConnection> checkConnections(const std::function<DrawingInfo(const QString&)>& read = {}) const;

    // THE CABLE LIST, one line a cable, as comma-separated values with a heading line.
    QString cableListCsv() const;

    QJsonObject toJson() const;
    static SystemDesign fromJson(const QJsonObject& o);

    // A fresh id for a kind of thing ("u", "p", "c").
    QString freshId(const QString& prefix) const;
};

} // namespace Edraw_Desktop

#pragma once

#include "model/Entity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// The parts library: the raw materials a harness is built from. Every part carries what a
// purchasing department needs (description, manufacturer and its part number), the technical
// parameters its KIND calls for, and - optionally - two drawings of it: a schematic symbol and a
// mechanical drawing. Both are stored as entities, so a part drawn once can be dropped into any
// sheet afterwards, and shown in the library the way it will look on the drawing.
enum class PartKind {
    Wire,
    // A cable is not a wire: it is a jacket with a make-up inside it - so many pairs, so many
    // singles, a coax, screens at one level or two. What it is made of is described by a CableSpec
    // (harness/CableSpec.h) kept in its "structure" parameter, and the harness builder expands it
    // into its conductors rather than making a person type them out.
    Cable,
    Connector,
    ContactPin,
    Ferrule,
    Crimp,          // ring / fork / pin / blade terminals
    Splice,
    Heatshrink,
    HeatshrinkBoot,
    Backshell,
    YSplit,
    ShieldBraid,
    // Beyond the list asked for: the other things a harness is actually made of.
    Grommet,
    CableGland,
    Sleeve,         // spiral wrap / braided sleeving
    Conduit,
    CableTie,
    Marker,         // label / sleeve marker / hot stamp
    Tape,
    Terminal,       // terminal block / stud / bus bar
    Overmould,
    // Components a harness carries in-line rather than merely joins: these are IN the circuit, and
    // each has a standard schematic symbol (see builtinSchematicSymbol).
    Resistor,
    Capacitor,
    Inductor,       // coil / choke
    FerriteBead,
    Other
};

// The part's drawing with ONE of its objects marked as the connection line - where the harness attaches
// to it - and the mark taken off everything else. A part has one place the harness meets it, so this is
// exclusive by construction rather than by asking whoever is clicking to be careful. An index outside
// the drawing leaves nothing marked, which is how the mark is cleared.
QJsonArray partDrawingWithConnectionLine(const QJsonArray& drawing, int index);
// Which object of the drawing is marked, or -1.
int partDrawingConnectionLine(const QJsonArray& drawing);

// Every kind, in the order the library lists them.
std::vector<PartKind> allPartKinds();
// "Heatshrink boot", "Y-split heatshrink", ... - what the UI shows.
QString partKindName(PartKind kind);
// The stable key a part file stores ("heatshrink-boot"), so renaming a label never breaks a file.
QString partKindKey(PartKind kind);
PartKind partKindFromKey(const QString& key);

// A unit of measure a part is bought and issued in. `dim` groups the ones that can be added
// together (every length is a length); `toBase` converts to that group's base unit, which is what
// lets a bill of materials total 300 mm of wire and 1.2 m of wire as one line.
struct PartUnit {
    QString code;   // "mm", "m", "each", ... - what a file stores and a row shows
    QString label;  // "millimeter", "each", ...
    QString dim;    // "length" | "count" | "mass" | "volume" | "area" | "time"
    double toBase = 1.0;
};
const std::vector<PartUnit>& partUnits();
QStringList partUnitCodes();
const PartUnit* partUnitByCode(const QString& code);
QString partUnitDimension(const QString& code);
// Can two units be added together at all? (mm and m: yes. mm and each: no.)
bool partUnitsCompatible(const QString& a, const QString& b);
// `value` in `from`, expressed in `to`. False - and `out` untouched - when they do not belong to
// the same dimension, so a bill of materials never silently adds metres to pieces.
bool convertPartUnit(double value, const QString& from, const QString& to, double& out);
// What a kind is normally issued in: wire by the metre, a connector each, heatshrink by the
// millimetre. The unit a new part starts with.
QString defaultUnitForKind(PartKind kind);

// One technical parameter of a kind. `Range` is a min/max pair - a crimp fits a range of studs, a
// boot a range of cable diameters - and is stored as two values, "<key>Min" and "<key>Max".
struct PartField {
    enum class Type { Text, Number, Range, Bool, Choice };
    QString key;
    QString label;
    Type type = Type::Text;
    QStringList choices; // Choice only
    QString unit;        // shown after the field ("mm", "AWG", ...)
};

// Which parameters a kind has, in order. The library's form is built from this, so a kind gains a
// parameter by being given one here - there is no per-kind form to write.
std::vector<PartField> partFields(PartKind kind);
// The choices one named Choice field of a kind offers - so a window other than the library's own form
// (the cable builder, say) offers the same list rather than a copy of it that drifts.
QStringList partFieldChoices(PartKind kind, const QString& key);
// ---- Choices a person has added -----------------------------------------------------------------
// A material this shop uses that is not on the built-in list, a plating, a jacket compound. Kept with
// the application's settings rather than in a library file, so it is offered for every part in every
// library from then on - and offered by every window that asks for that field, not only the one it was
// typed into.
QStringList addedChoices(const QString& key);
void rememberChoice(const QString& key, const QString& value, const QStringList& builtIn);
// The built-in list plus what has been added, which is what a chooser should offer.
QStringList choicesFor(PartKind kind, const QString& key);

// A part in the library.
struct LibraryPart {
    QString id;   // stable, unique within the library
    PartKind kind = PartKind::Wire;
    QString description;
    QString mfrName;
    QString mfrPn;
    // The house part number, when there is one: a bill of materials is ordered against it.
    QString internalPn;
    // What it is issued in (see PartUnit). Set from the kind when the part is created.
    QString unit;
    // The technical parameters, by field key (a Range's two halves are "<key>Min"/"<key>Max",
    // a Bool is "1"/"0"). Text, so a value can be blank / "as required" / a range written by hand.
    QMap<QString, QString> params;
    // The files this part points at, each by the name it is filed under in the datasheet folder - not
    // a path, so a library can be moved between machines and the folder found again. Several parts
    // pointing at one file is normal (a family shares one datasheet), which is why removing one only
    // deletes the file when nothing else points at it.
    //
    // Several FILES per part is just as normal: the datasheet, an outline drawing, a qualification
    // report, an approved-parts certificate. The first is "the" datasheet where only one can be shown.
    QStringList datasheets;
    QString datasheet() const { return datasheets.isEmpty() ? QString() : datasheets.front(); }
    bool hasDatasheet() const { return !datasheets.isEmpty(); }
    // The two drawings, as entities. Empty until one is captured from a sheet.
    QJsonArray schematic;
    QJsonArray mechanical;

    // The contacts this connector takes: library part ids, in the order they were tied on. A connector is
    // ordered with its contacts and cannot be built without them, and there is often more than one to
    // choose from - a pin and a socket, two gauges, a sealed one and a plain one. So they are tied to the
    // part here, and the one to fit is chosen when the connector is placed.
    QStringList contactIds() const;
    void setContactIds(const QStringList& ids);
    // How many contacts one of these connectors needs: its way count, or one if it does not say.
    int wayCount() const;

    QString value(const QString& key) const { return params.value(key); }
    bool hasSchematic() const { return !schematic.isEmpty(); }
    bool hasMechanical() const { return !mechanical.isEmpty(); }
    // "P/N — description", or whatever of the two there is: what the list shows.
    QString title() const;
};

// The library itself: parts, plus where they are kept.
struct PartsLibrary {
    std::vector<LibraryPart> parts;
    // Whether the standard heat-shrink shelf has been put in this library. Recorded IN THE FILE, so it
    // happens once: a size somebody deliberately deleted must not come back every time the library is
    // opened. A library written before this existed has it false, which is what brings the shelf to a
    // library that was made before the shelf did.
    bool standardHeatshrink = false;

    // %APPDATA%/Whatson/Edraw/library/edrawlib.plb (created on first save). A library is a .plb
    // file - a part library - so it can be kept beside a project and opened by name.
    static QString defaultPath();
    static QString fileFilter(); // "Part library (*.plb);;All files (*)"
    bool load(const QString& path);
    bool save(const QString& path) const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& root);

    LibraryPart* byId(const QString& id);
    const LibraryPart* byId(const QString& id) const;
    // Add a part of `kind` with an id nothing else has, and return it.
    LibraryPart& add(PartKind kind);
    void remove(const QString& id);
    // A copy of `id`, "(copy)" appended to its description.
    LibraryPart* duplicate(const QString& id);
    // Every part of one kind, in list order.
    std::vector<const LibraryPart*> ofKind(PartKind kind) const;
};

// The libraries in use at once: a project's own beside a company one, say. Which files are
// connected is remembered between sessions; one of them is the one being WORKED ON (what a new part
// is added to), while everything that only reads parts - the bill of materials, the harness
// builder's pickers - sees all of them together.
class PartsCatalog {
public:
    // The connected files, in order, and which is being worked on.
    const QStringList& paths() const { return paths_; }
    int activeIndex() const { return active_; }
    QString activePath() const;
    void setActiveIndex(int i);
    void setActivePath(const QString& path);

    // Connect a library file (loading it) / disconnect one. Connecting a file already connected
    // just makes it the one being worked on.
    bool connectLibrary(const QString& path);
    void disconnectLibrary(const QString& path);
    // Load every remembered file (and the default one on a first run).
    void openRemembered();
    // What is remembered between sessions. A driver or a test can switch this off, so a run of its
    // own never leaves the application pointed at a file it made up.
    void remember() const;
    void setRemembering(bool on) { remembering_ = on; }

    // The library being worked on - what "New part" adds to. Never null once openRemembered() has
    // run; a catalog with nothing connected keeps one empty library of its own.
    PartsLibrary& active();
    const PartsLibrary& active() const;
    PartsLibrary* byPath(const QString& path);
    // Save every library that is connected, each to its own file.
    bool saveAll() const;

    // Reading across all of them.
    const LibraryPart* byId(const QString& id) const;
    std::vector<const LibraryPart*> ofKind(PartKind kind) const;
    int partCount() const;
    // Which connected file a part lives in ("" when nothing has it).
    QString pathOf(const QString& partId) const;
    // All the parts of every connected library, as one library to read from.
    PartsLibrary merged() const;

private:
    QStringList paths_;
    std::vector<PartsLibrary> libs_;
    int active_ = 0;
    bool remembering_ = true;
    PartsLibrary empty_; // stands in when nothing is connected
};

// Where the library used to be written (the app-data folder with the names repeated). Kept so a
// library saved by an earlier version is taken over rather than left behind.
QString legacyLibraryPath();

// A description written from what the part says it IS: its kind, and the parameters that matter for
// that kind, in the order a catalogue would write them ("Hook-up wire, 22 AWG, PVC, tinned copper,
// 300 V"). Nothing is invented - a parameter that is blank is simply left out - so the sentence is
// only ever as complete as the data behind it.
QString describePart(const LibraryPart& part);

// A library of real parts to start from, so the shelf is not bare on a first run: Alpha Wire
// hook-up wire from 14 to 30 AWG, with the manufacturer's own part numbers and the technical
// figures its data sheets give.
PartsLibrary seedLibrary();

// The heat-shrink families a harness shop keeps on the shelf, in every size they are made in: DR-25 and
// RT-375 and RNF-100 tubing, ATUM adhesive-lined, and TMS-SCE identification sleeve in white and yellow.
// Adds only what is not there already (matched on manufacturer part number), and returns how many it
// added, so it can be offered to a library that already exists as well as seeded into a new one.
//
// The sizes and the designations are the manufacturers' own. The DIMENSIONS are nominal - taken from the
// size designation itself, which is what the size means: an expanded inside diameter that recovers by the
// family's ratio. Wall thickness is left blank rather than guessed at, and every part says in its notes
// that the figures are nominal and the datasheet is the authority. A harness that must hold a tolerance is
// checked against the datasheet, not against a starter library.
int addStandardHeatshrink(PartsLibrary& lib);

// Put the shelf in if it has never been put in. Returns how many parts it added, and marks the library so
// that it is not done again - which is what makes it safe to call every time a library is opened.
int ensureStandardHeatshrink(PartsLibrary& lib);

// The standard schematic symbol for the kinds that have one - the components a harness carries in
// line: a resistor, a capacitor, a coil, a ferrite bead. Drawn to the same scale as the rest of the
// schematic, so a part of one of those kinds shows a symbol and can be dropped on a sheet without
// anyone drawing it first. Empty for kinds with no symbol, because they are not in the circuit: a
// boot does not appear on a schematic.
QJsonArray builtinSchematicSymbol(PartKind kind);

// A connector's schematic symbol, built from what the part already says: the way count above all, and the
// part number to letter it with. It comes out as a real CONNECTOR object - the body, the pin stubs, the pin
// numbers - and not as loose lines, so what is placed can be edited with the connector tools, wired up, and
// counted on the bill of materials like any other connector. Empty for a part that is not a connector or
// says nothing about its ways.
QJsonArray connectorSymbolFromData(const LibraryPart& part);
// The symbol a part actually shows: the one captured for it, or failing that the built-in one for
// its kind. What the library previews and what Insert puts on the sheet.
QJsonArray partSchematicDrawing(const LibraryPart& part);

// The entities of one of a part's drawings, ready to be added to a document (ids cleared, so the
// document gives them its own).
std::vector<Entity> partDrawingEntities(const QJsonArray& drawing);
// The other way: the entities of a drawing, as stored in a part.
QJsonArray partDrawingFromEntities(const std::vector<Entity>& ents);

} // namespace Edraw_Desktop

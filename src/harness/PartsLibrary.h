#pragma once

#include "harness/MechModels.h"
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

// WHETHER WIRES GO INTO THIS KIND OF PART, and so whether "the face they leave by" means anything for it. A
// connector, a contact, a ferrule, a crimped terminal, a splice, a backshell, a gland, a grommet: wires enter
// and leave, and where they leave is a property of the part. A length of tape, a cable tie, a marker sleeve
// has no such thing - offering it a face to mark would be offering it a setting with no meaning.
bool partKindTakesWires(PartKind kind);



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
// AND TAKEN OFF AGAIN. A list that can only be added to fills up with a misspelling somebody typed once, and
// from then on every part offers it. Only what was ADDED can be forgotten - the built-in materials are the
// program's, not the office's, and a list that can be emptied is a list somebody empties by accident.
void forgetChoice(const QString& key, const QString& value);
// The built-in list plus what has been added, which is what a chooser should offer.
QStringList choicesFor(PartKind kind, const QString& key);

// Which of a part's drawings is meant: its mechanical body, its schematic symbol, or one of its two
// fanout views. Kept as one enum rather than three separate "which drawing" parameters everywhere a
// part's drawing is opened for editing, so a session only has to remember one thing to know which
// QJsonArray it reads and writes.
enum class LibraryPartDrawing { Mechanical, Schematic, Fanout, FanoutOpposite };

// A part in the library.
struct LibraryPart {
    QString id;   // stable, unique within the library
    PartKind kind = PartKind::Wire;
    QString description;
    QString mfrName;
    QString mfrPn;
    // The house part number, when there is one: a bill of materials is ordered against it.
    QString internalPn;
    // Where the part lives on the web: the manufacturer's page or the distributor's listing. One field on
    // every kind of part rather than a parameter of some of them, because every part has a page and it is
    // asked for in the same breath as the part number.
    QString link;
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
    // The drawings, as entities. Empty until one is captured from a sheet.
    QJsonArray schematic;
    QJsonArray mechanical;
    // The FANOUT: how the wires leave the connector - which way they break out, and in what order. It is a
    // third drawing because it is a third thing: the mechanical view is the connector's body, the schematic
    // is its circuit, and the fanout is what the harness does at it. Drawn for one side, front or back, and
    // placed mirrored for the other.
    QJsonArray fanout;
    // The OTHER side's fanout, captured/drawn independently rather than computed as a mirror of
    // `fanout` every time one is placed - a real connector's two faces are not always perfect
    // mirror images of each other (keying, an asymmetric rib, a polarising tab), so a live mirror
    // transform is a starting point at best. "Mirror from opposite fanout" seeds this field from a
    // one-off mirror of `fanout` (or vice versa); after that it is its own drawing, hand-correctable
    // like any other. Empty until the button has been used, in which case placement falls back to
    // the old runtime mirror so a part nobody has touched since keeps working exactly as it did.
    QJsonArray fanoutOpposite;

    // Which side the fanout was drawn from: "front" (looking at the face) or "back" (looking at the wires).
    QString fanoutSide() const {
        const QString s = params.value(QStringLiteral("fanoutSide"));
        return s == QStringLiteral("back") ? s : QStringLiteral("front");
    }
    void setFanoutSide(const QString& side) {
        params.insert(QStringLiteral("fanoutSide"),
                      side == QStringLiteral("back") ? side : QStringLiteral("front"));
    }
    // A fanout ON EITHER SIDE. Drawing only the back left the part looking as though it had none - no menu
    // entry, no tick on the shelf - because this asked about the front alone, and the front is only the
    // side most people start with rather than the side a fanout has to be on.
    bool hasFanout() const { return !fanout.isEmpty() || !fanoutOpposite.isEmpty(); }
    bool hasFanoutFront() const { return !fanout.isEmpty(); }
    bool hasFanoutOpposite() const { return !fanoutOpposite.isEmpty(); }

    // The three marks a default mechanical model is placed by (see MechModel), on the MECHANICAL
    // drawing only: where it connects and which way it runs from there, and its two ends. A
    // connector's mechanical drawing used to only carry a single "connection line" flagged on one
    // of its own entities (see partDrawingConnectionLine()); these replace that for parts marked up
    // from here on, kept alongside it rather than migrated, so an older part's line still shows.
    bool hasConnect = false;
    Vec2 connectPoint;
    double connectAngleRad = 0.0;
    bool hasNearPoint = false;
    Vec2 nearPoint;
    bool hasFarPoint = false;
    Vec2 farPoint;
    // THE FACES THE CONDUCTORS LEAVE BY. A connector, a ferrule, a ring - anything wires go into - has one
    // or more faces the wires come out of, and the generator hands the branches out in the order they were
    // marked. A default model has had these all along; a library part is the same part with a part number on
    // it, and it could only be told where it connects, never where the wires leave.
    std::vector<MechOutputFace> outputs;

    // The contacts this connector takes: library part ids, in the order they were tied on. A connector is
    // ordered with its contacts and cannot be built without them, and there is often more than one to
    // choose from - a pin and a socket, two gauges, a sealed one and a plain one. So they are tied to the
    // part here, and the one to fit is chosen when the connector is placed.
    QStringList contactIds() const;
    void setContactIds(const QStringList& ids);
    // The pin contact this connector is used with: ONE, for both drawings. A connector is built with one
    // kind of contact in it, and which one is a property of the connector rather than of the moment it is
    // placed - so it is set once, in the library, and every copy placed carries it.
    QString contactId() const { const QStringList ids = contactIds(); return ids.isEmpty() ? QString() : ids.front(); }
    void setContactId(const QString& contact) {
        setContactIds(contact.isEmpty() ? QStringList() : QStringList{contact});
    }

    // The other end of the same tie: the connectors this CONTACT is used in. A tie made from either end is
    // one tie, and it is written on both parts so that either can be looked at on its own - a contact opened
    // in the library says what it goes in, and a connector says what goes in it.
    QStringList connectorIds() const;
    void setConnectorIds(const QStringList& ids);
    QString connectorId() const {
        const QStringList ids = connectorIds();
        return ids.isEmpty() ? QString() : ids.front();
    }
    void setConnectorId(const QString& connector) {
        setConnectorIds(connector.isEmpty() ? QStringList() : QStringList{connector});
    }
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
    bool standardCables = false;
    // And the Spec 55 shelf: 55A0111 wire, 55A0121 pair and 55A1121 screened pair, every size.
    bool standardSpec55 = false;
    // And the Alpha Wire hook-up shelf: 2626 and 3079, in every colour each is made in.
    bool standardAlphaHookup = false;

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
// THE COAXIAL SHELF, by RG designation. Cables by RG and not by manufacturer, because the RG number is
// where a cable's outside diameter is genuinely standard - which is the figure a bundle is added up from.
int addStandardCables(PartsLibrary& lib);

// THE SPEC 55 SHELF: Raychem 55A0111 wire in black, red and white, the 55A0121 unscreened pair and
// the 55A1121 screened and jacketed pair, in every AWG size from 30 to 8 that TE makes them in. The
// figures are read off TE's own specification control drawings and converted from the inches they are
// written in - the outside diameters above all, since that is what a bundle is added up from.
int addStandardSpec55(PartsLibrary& lib);

// THE ALPHA WIRE HOOK-UP SHELF: 2626, the 26 AWG nickel-plated PTFE wire that goes where it is hot,
// and 3079, the 14 AWG tinned PVC wire that carries the current - each in every colour Alpha's own
// specification lists it in, which is what "all colours" means for these two. The figures are read
// off Alpha's customer specifications and converted from the inches they are written in.
int addStandardAlphaHookup(PartsLibrary& lib);

// Tie a connector and a contact together, from whichever end the user was looking at. Writes BOTH sides where
// both parts can be reached, so the tie shows on either part when it is opened on its own; passing an empty
// contact unties whatever the connector had, and the other way about.
void tieContactToConnector(PartsLibrary& lib, const QString& connectorId, const QString& contactId);

// What a part is tied to, as it should be SHOWN: what it says itself, and what says it. A drawing office that
// tied the contact from the connector's side before this existed still sees the tie on the contact, because
// the answer is worked out from both ends rather than read from one.
QStringList tiedContactsOf(const PartsLibrary& lib, const QString& connectorId);
// SEVERAL CONTACTS TO ONE CONNECTOR - a pin and a socket, two sizes, two makers' equivalents: one more tied (both
// ends of the tie written), or one taken off (off at both ends).
void addContactTie(PartsLibrary& lib, const QString& connectorId, const QString& contactId);
void removeContactTie(PartsLibrary& lib, const QString& connectorId, const QString& contactId);
QStringList tiedConnectorsOf(const PartsLibrary& lib, const QString& contactId);

// Put the shelf in if it has never been put in. Returns how many parts it added, and marks the library so
// that it is not done again - which is what makes it safe to call every time a library is opened.
int ensureStandardHeatshrink(PartsLibrary& lib);
// The same for the coaxial shelf: once per library, and recorded in the file so a type somebody deleted
// does not come back the next time it is opened.
int ensureStandardCables(PartsLibrary& lib);
// And the same for the Spec 55 shelf: once per library, recorded in the file.
int ensureStandardSpec55(PartsLibrary& lib);
// And the same for the Alpha hook-up shelf: once per library, recorded in the file.
int ensureStandardAlphaHookup(PartsLibrary& lib);

// The standard schematic symbol for the kinds that have one - the components a harness carries in
// line: a resistor, a capacitor, a coil, a ferrite bead. Drawn to the same scale as the rest of the
// schematic, so a part of one of those kinds shows a symbol and can be dropped on a sheet without
// anyone drawing it first. Empty for kinds with no symbol, because they are not in the circuit: a
// boot does not appear on a schematic.
QJsonArray builtinSchematicSymbol(PartKind kind);

// A connector's FANOUT, built from what the part already says: one wire per way, leaving the back of the
// housing and gathering into the bundle. It is what the harness DOES at the connector, and it can be had from
// the way count and the pitch alone - so a part with no fanout drawn for it still has one to place, and a
// draughtsman who wants it different draws over it or captures his own in the library.
QJsonArray fanoutFromData(const LibraryPart& part);

// Is a fanout a thing this part HAS? A connector has one - it is where the harness leaves it - and so does
// anything whose data gives a way or pin count. A heat-shrink sleeve does not, and wayCount() answers 1 for
// everything (a connector with no count still needs one contact), so the question has to be asked separately
// or every sleeve on the shelf would be offered a one-way fanout.
bool canHaveFanout(const LibraryPart& part);

// A connector's schematic symbol, built from what the part already says: the way count above all, and the
// part number to letter it with. It comes out as a real CONNECTOR object - the body, the pin stubs, the pin
// numbers - and not as loose lines, so what is placed can be edited with the connector tools, wired up, and
// counted on the bill of materials like any other connector. Empty for a part that is not a connector or
// says nothing about its ways.
QJsonArray connectorSymbolFromData(const LibraryPart& part);

// ---- A connector that comes with its pins fitted ------------------------------------------------
//
// Most connectors are ordered as a housing and its contacts separately, and the contact is a part of its
// own tied to the housing ("Use with pin contact"). Some come with the pins ALREADY IN THEM - a moulded
// header, an overmoulded lead, a power connector with its terminals fitted - and for those there is no
// contact to order, so nothing to tie: what matters instead is what those pins ARE. A connector can carry
// more than one size of pin, which is the whole point of a power connector: two heavy ways and six signal
// ways in one housing.
//
// So a pin type is a size, a gauge, and WHICH PINS are that size - the last of these is what lets a
// schematic symbol label its pins with their own sizes rather than one figure for the whole connector.
struct PartPinType {
    QString size;    // what the maker calls it: "16", "20", "6.3 mm blade"
    QString unit;    // "AWG" or "mm2" - which of the two the gauge below is in
    QString gauge;   // the conductor it takes, in that unit
    QString amps;    // its current rating, if the datasheet gives one
    QString pins;    // which pins are this type: "1-3", "1,2,5", empty = all of them
    QString gender;  // "male" (pins) or "female" (sockets); empty = not said
};

// Whether this connector comes with its pins fitted. With them fitted there is no contact to order, so the
// tie to a contact part is neither offered nor kept.
bool partIncludesPins(const LibraryPart& part);
void setPartIncludesPins(LibraryPart& part, bool on);
// The pin types it has, in the order they were entered.
std::vector<PartPinType> partPinTypes(const LibraryPart& part);
void setPartPinTypes(LibraryPart& part, const std::vector<PartPinType>& types);
// ---- A connector that comes WITH ITS WIRES: a lead, a pigtail, an overmoulded cable end ----
// Each pin's wire as the part is bought: its colour, its size (in AWG or mm2), its insulation and how thick
// it is over the insulation. A wire is only kept when something is said about it.
struct PartPinWire {
    int pin = 0;             // 1..wayCount
    QString color;           // "Red", "White/Blue" - as the maker names it
    QString size;            // the conductor: "24", "0.25"
    QString unit = QStringLiteral("AWG");   // "AWG" or "mm2"
    QString insulation;      // "PTFE", "PVC", ...
    QString od;              // outside diameter over the insulation, mm
    bool empty() const {
        return color.trimmed().isEmpty() && size.trimmed().isEmpty() && insulation.trimmed().isEmpty() &&
               od.trimmed().isEmpty();
    }
};
// A DRAWING THAT DOES NOT APPLY to this part - a cable tie has no schematic symbol, a label no fanout - marked
// so, rather than left looking like work not yet done. `which` is "mechanical", "schematic" or "fanout".
bool partDrawingNotApplicable(const LibraryPart& part, const QString& which);
void setPartDrawingNotApplicable(LibraryPart& part, const QString& which, bool na);

bool partIsWired(const LibraryPart& part);
void setPartWired(LibraryPart& part, bool on);
// One per pin, 1..wayCount (or further, when wires were described for more pins than it now has), with
// whatever was said about each.
std::vector<PartPinWire> partPinWires(const LibraryPart& part);
void setPartPinWires(LibraryPart& part, const std::vector<PartPinWire>& wires);
// What the wire columns offer: the usual colours and insulations (both can be typed over).
QStringList wireColorChoices();
// THE COLOUR CODES a wired connector's wires can be coloured by, pin 1 first: each a name and the colours in
// order ("White/Blue" a white wire with a blue stripe). A connector with more pins than a code has colours
// starts the code again.
struct WireColorStandard {
    QString name;
    QStringList colors;
};
const std::vector<WireColorStandard>& wireColorStandards();
// Every wire coloured by that code, by its pin; false for a code there is not.
bool applyWireColorStandard(std::vector<PartPinWire>& wires, int standard);
QStringList wireInsulationChoices();

// HOW A WIRED CONNECTOR'S WIRES ARE SHOWN. On the mechanical drawing: fanned out of its output face, to one
// side or the other, running straight for a while, then spread to a pitch, and as long as the leads are. On
// the schematic: a lead out of every pin, with what the wire is written above it.
struct WiredDisplay {
    bool mechanical = false;
    bool flipSide = false;       // toward the body rather than away from it
    double straightMm = 10.0;    // straight out of the face before the fan spreads
    double pitchMm = 4.0;        // between the wires once spread
    double lengthMm = 300.0;     // how long the leads are
    bool schematic = false;
    bool labelColor = true;
    bool labelSize = true;
    double schemLeadMm = 30.0;   // how long each wire is drawn on the schematic
};
WiredDisplay partWiredDisplay(const LibraryPart& part);
void setPartWiredDisplay(LibraryPart& part, const WiredDisplay& d);
// The drawing with a wired connector's wires added, as WiredDisplay says - unchanged for any other part.
// `mechanical` picks which of the two the drawing is.
QJsonArray withWiredWires(const LibraryPart& part, bool mechanical, const QJsonArray& drawing);

// ---- A part drawn to proportion, sized by its entry face ------------------------------------------
//
// A mechanical drawing whose exact size is not known is drawn to good PROPORTION - the shape, not the size - and
// sized by one dimension: its ENTRY FACE, the first face the wires leave by. That one figure is:
//   Known       typed in (the datasheet's), and the whole drawing is scaled so the face is that long;
//   FromParams  read from the part's own figures - its cable entry (the most it takes), or entry / main side;
//   Auto        left to the drawing: the harness wizard makes it the drawing's usual entry size;
//   AsDrawn     none of this - the drawing is the part's real size.
// Every one but AsDrawn is a NOMINAL size, and says so wherever the part is shown.
enum class PartSizeMode { AsDrawn, Known, FromParams, Auto };
PartSizeMode partSizeMode(const LibraryPart& part);
void setPartSizeMode(LibraryPart& part, PartSizeMode mode);
double partSizeKnownMm(const LibraryPart& part);
void setPartSizeKnownMm(LibraryPart& part, double mm);
// The entry face as drawn, and as the part's own figures give it - nought when there is none.
double partDrawnEntryMm(const LibraryPart& part);
double partEntryFromParamsMm(const LibraryPart& part);
// What the drawing is scaled by for its size (1 as drawn, on Auto, or when the figure is missing), and the
// entry face that comes to.
double partSizeFactor(const LibraryPart& part);
bool partIsNominal(const LibraryPart& part);
// The mechanical drawing and its faces at that size, scaled about the drawing's origin.
QJsonArray partMechanicalSized(const LibraryPart& part);
std::vector<MechOutputFace> partOutputsSized(const LibraryPart& part);
// The part with its drawing, faces and marks put at that size - what is placed and drawn with.
LibraryPart partSized(const LibraryPart& part);
// A wire colour's name as a colour to draw it in; empty (the layer's own) for black and for anything unknown.
QString leadColorHex(const QString& name);
// A wire colour as a swatch to show it by - black as black (not left to the layer), the body colour of a
// striped "White/Blue". Grey for a name it does not know.
QString wireSwatchHex(const QString& name);

// The gauge of every pin, 1..wayCount, worked out from the types' pin lists - what a schematic symbol
// labels its pins with. A pin no type claims is left empty rather than guessed at.
QStringList partPinGauges(const LibraryPart& part);
// And what each pin is rated to carry, the same way - what the pin table's own column shows.
QStringList partPinAmps(const LibraryPart& part);
// The pins a "1-3, 5" style list names, as numbers. Out-of-order or overlapping lists are taken as
// written: it is a note about a real connector, not a puzzle to be normalised.
std::vector<int> pinsNamedBy(const QString& list, int ways);
// The symbol a part actually shows: the one captured for it, or failing that the built-in one for
// its kind. What the library previews and what Insert puts on the sheet.
QJsonArray partSchematicDrawing(const LibraryPart& part);

// The entities of one of a part's drawings, ready to be added to a document (ids cleared, so the
// document gives them its own).
std::vector<Entity> partDrawingEntities(const QJsonArray& drawing);
// The other way: the entities of a drawing, as stored in a part.
QJsonArray partDrawingFromEntities(const std::vector<Entity>& ents);

} // namespace Edraw_Desktop

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
    Other
};

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
    QString label;  // "millimetre", "each", ...
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
    // The two drawings, as entities. Empty until one is captured from a sheet.
    QJsonArray schematic;
    QJsonArray mechanical;

    QString value(const QString& key) const { return params.value(key); }
    bool hasSchematic() const { return !schematic.isEmpty(); }
    bool hasMechanical() const { return !mechanical.isEmpty(); }
    // "P/N — description", or whatever of the two there is: what the list shows.
    QString title() const;
};

// The library itself: parts, plus where they are kept.
struct PartsLibrary {
    std::vector<LibraryPart> parts;

    // %APPDATA%/Whatson/Edraw/parts-library.plb (created on first save). A library is a .plb
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

// The entities of one of a part's drawings, ready to be added to a document (ids cleared, so the
// document gives them its own).
std::vector<Entity> partDrawingEntities(const QJsonArray& drawing);
// The other way: the entities of a drawing, as stored in a part.
QJsonArray partDrawingFromEntities(const std::vector<Entity>& ents);

} // namespace Edraw_Desktop

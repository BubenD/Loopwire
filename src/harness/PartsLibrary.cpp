#include "harness/PartsLibrary.h"

#include "model/Entity.h"

#include "io/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace Edraw_Desktop {
namespace {

// One row of the kind table: the key a file stores, and the name shown.
struct KindRow {
    PartKind kind;
    const char* key;
    const char* name;
};

const KindRow kKinds[] = {
    {PartKind::Wire, "wire", "Wire"},
    {PartKind::Connector, "connector", "Connector"},
    {PartKind::ContactPin, "contact-pin", "Contact pin"},
    {PartKind::Ferrule, "ferrule", "Ferrule"},
    {PartKind::Crimp, "crimp", "Crimp terminal"},
    {PartKind::Splice, "splice", "Joint"},
    {PartKind::Heatshrink, "heatshrink", "Heatshrink"},
    {PartKind::HeatshrinkBoot, "heatshrink-boot", "Heatshrink boot"},
    {PartKind::Backshell, "backshell", "Backshell"},
    {PartKind::YSplit, "y-split", "Y-split heatshrink"},
    {PartKind::ShieldBraid, "shield-braid", "Shield braid"},
    {PartKind::Grommet, "grommet", "Grommet"},
    {PartKind::Cable, "cable", "Cable"},
    {PartKind::CableGland, "cable-gland", "Cable gland"},
    {PartKind::Sleeve, "sleeve", "Sleeve / spiral wrap"},
    {PartKind::Conduit, "conduit", "Conduit"},
    {PartKind::CableTie, "cable-tie", "Cable tie"},
    {PartKind::Marker, "marker", "Marker / label"},
    {PartKind::Tape, "tape", "Tape"},
    {PartKind::Terminal, "terminal", "Terminal / stud"},
    {PartKind::Overmould, "overmould", "Overmould"},
    {PartKind::Resistor, "resistor", "Resistor"},
    {PartKind::Capacitor, "capacitor", "Capacitor"},
    {PartKind::Inductor, "inductor", "Coil / inductor"},
    {PartKind::FerriteBead, "ferrite-bead", "Ferrite bead"},
    {PartKind::Other, "other", "Other"},
};

PartField text(const char* key, const char* label, const char* unit = "") {
    return {QString::fromLatin1(key), QString::fromLatin1(label), PartField::Type::Text, {},
            QString::fromLatin1(unit)};
}
PartField num(const char* key, const char* label, const char* unit = "") {
    return {QString::fromLatin1(key), QString::fromLatin1(label), PartField::Type::Number, {},
            QString::fromLatin1(unit)};
}
PartField range(const char* key, const char* label, const char* unit = "") {
    return {QString::fromLatin1(key), QString::fromLatin1(label), PartField::Type::Range, {},
            QString::fromLatin1(unit)};
}
PartField yesNo(const char* key, const char* label) {
    return {QString::fromLatin1(key), QString::fromLatin1(label), PartField::Type::Bool, {}, {}};
}
PartField choice(const char* key, const char* label, const QStringList& choices) {
    return {QString::fromLatin1(key), QString::fromLatin1(label), PartField::Type::Choice, choices,
            {}};
}

const QStringList kMetals = {QStringLiteral("Copper"),
                             QStringLiteral("Tinned copper"),
                             QStringLiteral("Silver-plated copper"),
                             QStringLiteral("Nickel-plated copper"),
                             QStringLiteral("Gold-plated"),
                             QStringLiteral("Brass"),
                             QStringLiteral("Phosphor bronze"),
                             QStringLiteral("Stainless steel"),
                             QStringLiteral("Aluminium")};

const QStringList kPolymers = {QStringLiteral("Polyolefin"),
                               QStringLiteral("PVC"),
                               QStringLiteral("PTFE"),
                               QStringLiteral("FEP"),
                               QStringLiteral("PVDF"),
                               QStringLiteral("Viton"),
                               QStringLiteral("Neoprene"),
                               QStringLiteral("Silicone"),
                               QStringLiteral("Nylon")};

} // namespace

const std::vector<PartUnit>& partUnits() {
    // Ordered the way a harness shop thinks: the discrete ones first, then length, then the rest.
    static const std::vector<PartUnit> units = {
        {QStringLiteral("each"), QStringLiteral("each"), QStringLiteral("count"), 1.0},
        {QStringLiteral("pcs"), QStringLiteral("pieces"), QStringLiteral("count"), 1.0},
        {QStringLiteral("pair"), QStringLiteral("pair"), QStringLiteral("count"), 2.0},
        {QStringLiteral("set"), QStringLiteral("set"), QStringLiteral("set"), 1.0},
        {QStringLiteral("kit"), QStringLiteral("kit"), QStringLiteral("set"), 1.0},
        {QStringLiteral("roll"), QStringLiteral("roll"), QStringLiteral("set"), 1.0},
        {QStringLiteral("mm"), QStringLiteral("millimeter"), QStringLiteral("length"), 1.0},
        {QStringLiteral("cm"), QStringLiteral("centimeter"), QStringLiteral("length"), 10.0},
        {QStringLiteral("m"), QStringLiteral("meter"), QStringLiteral("length"), 1000.0},
        {QStringLiteral("in"), QStringLiteral("inch"), QStringLiteral("length"), 25.4},
        {QStringLiteral("ft"), QStringLiteral("foot"), QStringLiteral("length"), 304.8},
        {QStringLiteral("g"), QStringLiteral("gram"), QStringLiteral("mass"), 1.0},
        {QStringLiteral("kg"), QStringLiteral("kilogram"), QStringLiteral("mass"), 1000.0},
        {QStringLiteral("ml"), QStringLiteral("milliliter"), QStringLiteral("volume"), 1.0},
        {QStringLiteral("l"), QStringLiteral("liter"), QStringLiteral("volume"), 1000.0},
        {QStringLiteral("mm2"), QStringLiteral("square millimeter"), QStringLiteral("area"), 1.0},
        {QStringLiteral("m2"), QStringLiteral("square meter"), QStringLiteral("area"), 1e6},
        {QStringLiteral("hr"), QStringLiteral("hour"), QStringLiteral("time"), 1.0},
    };
    return units;
}

QStringList partUnitCodes() {
    QStringList out;
    for (const PartUnit& u : partUnits()) out << u.code;
    return out;
}

const PartUnit* partUnitByCode(const QString& code) {
    for (const PartUnit& u : partUnits())
        if (u.code.compare(code, Qt::CaseInsensitive) == 0) return &u;
    return nullptr;
}

QString partUnitDimension(const QString& code) {
    const PartUnit* u = partUnitByCode(code);
    return u ? u->dim : QString();
}

bool partUnitsCompatible(const QString& a, const QString& b) {
    if (a.compare(b, Qt::CaseInsensitive) == 0) return true;
    const QString da = partUnitDimension(a), db = partUnitDimension(b);
    return !da.isEmpty() && da == db;
}

bool convertPartUnit(double value, const QString& from, const QString& to, double& out) {
    const PartUnit* f = partUnitByCode(from);
    const PartUnit* t = partUnitByCode(to);
    if (!f || !t || f->dim != t->dim || t->toBase == 0.0) return false;
    out = value * f->toBase / t->toBase;
    return true;
}

QString defaultUnitForKind(PartKind kind) {
    switch (kind) {
        // Bought and used by length.
        case PartKind::Wire:
        case PartKind::Cable:
        case PartKind::ShieldBraid:
        case PartKind::Sleeve:
        case PartKind::Conduit:
            return QStringLiteral("m");
        // Cut to length from a length, but counted in millimetres on a drawing.
        case PartKind::Heatshrink:
        case PartKind::Tape:
            return QStringLiteral("mm");
        // Everything else is a piece.
        default:
            break;
    }
    return QStringLiteral("each");
}

// ---- Choices a person has added ------------------------------------------------------------------

QStringList addedChoices(const QString& key) {
    QSettings st;
    return st.value(QStringLiteral("edraw/library/choices/") + key).toStringList();
}

void rememberChoice(const QString& key, const QString& value, const QStringList& builtIn) {
    const QString v = value.trimmed();
    if (v.isEmpty() || builtIn.contains(v, Qt::CaseInsensitive)) return;
    QStringList have = addedChoices(key);
    if (have.contains(v, Qt::CaseInsensitive)) return;
    have << v;
    QSettings st;
    st.setValue(QStringLiteral("edraw/library/choices/") + key, have);
}

QStringList choicesFor(PartKind kind, const QString& key) {
    QStringList out = partFieldChoices(kind, key);
    for (const QString& mine : addedChoices(key))
        if (!out.contains(mine, Qt::CaseInsensitive)) out << mine;
    return out;
}

QStringList partFieldChoices(PartKind kind, const QString& key) {
    for (const PartField& f : partFields(kind))
        if (f.key == key) return f.choices;
    return {};
}

QJsonArray partDrawingWithConnectionLine(const QJsonArray& drawing, int index) {
    QJsonArray out;
    for (int i = 0; i < drawing.size(); ++i) {
        QJsonObject o = drawing[i].toObject();
        if (i == index) o.insert(QLatin1String(kConnectionLineKey), true);
        else o.remove(QLatin1String(kConnectionLineKey));
        out.append(o);
    }
    return out;
}

int partDrawingConnectionLine(const QJsonArray& drawing) {
    for (int i = 0; i < drawing.size(); ++i)
        if (drawing[i].toObject().value(QLatin1String(kConnectionLineKey)).toBool()) return i;
    return -1;
}

std::vector<PartKind> allPartKinds() {
    std::vector<PartKind> out;
    for (const KindRow& r : kKinds) out.push_back(r.kind);
    return out;
}

QString partKindName(PartKind kind) {
    for (const KindRow& r : kKinds)
        if (r.kind == kind) return QString::fromLatin1(r.name);
    return QStringLiteral("Other");
}

QString partKindKey(PartKind kind) {
    for (const KindRow& r : kKinds)
        if (r.kind == kind) return QString::fromLatin1(r.key);
    return QStringLiteral("other");
}

PartKind partKindFromKey(const QString& key) {
    for (const KindRow& r : kKinds)
        if (key == QLatin1String(r.key)) return r.kind;
    return PartKind::Other;
}

std::vector<PartField> partFields(PartKind kind) {
    switch (kind) {
        case PartKind::Wire:
            // What a wire is bought by - the same things a wire on the drawing carries.
            return {choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    text("gauge", "Gauge"),
                    choice("material", "Conductor material", kMetals),
                    choice("conductor", "Conductor",
                           {QStringLiteral("Stranded"), QStringLiteral("Solid")}),
                    text("strands", "Strand definition"),
                    choice("insulation", "Insulation", kPolymers),
                    text("voltage", "Voltage rating", "V"),
                    num("od", "Outer diameter", "mm"),
                    // A data sheet gives the conductor's resistance per 1000 ft; a harness is
                    // measured in metres, so it is kept per metre (ohm/1000ft / 304.8).
                    num("resistance", "Conductor resistance", "ohm/m"),
                    num("bendRadius", "Minimum bend radius", "mm"),
                    text("colour", "Colour"),
                    text("temp", "Temperature range", "C"),
                    text("spec", "Specification")};
        case PartKind::Cable:
            // What a cable IS, at the level a purchase order and a harness both need. Its make-up -
            // the pairs, the coaxes, the screens, the colours - is a structure of its own and is
            // edited in the cable builder; it lives in "structure" as one compact description, so it
            // is saved, copied and duplicated with the part like every other parameter.
            return {num("od", "Outer diameter", "mm"),
                    choice("jacket", "Jacket material", kPolymers),
                    text("jacketColour", "Jacket colour"),
                    choice("material", "Conductor material", kMetals),
                    choice("conductor", "Conductors",
                           {QStringLiteral("Stranded"), QStringLiteral("Solid")}),
                    // The make-up in the shorthand a cable is spoken about in:
                    // "3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black". Typed here and turned into
                    // the make-up by the button beside it, or written here from the make-up when that
                    // is built in the cable-structure window. The make-up ITSELF - every element, its
                    // colours, its screens - is kept in "makeup", which is not a field anyone types.
                    text("structure", "Structure"),
                    num("cores", "Conductors"),
                    yesNo("screened", "Screened"),
                    num("bendRadius", "Minimum bend radius", "mm"),
                    text("voltage", "Voltage rating", "V"),
                    text("temp", "Temperature range", "C"),
                    text("spec", "Specification")};
        case PartKind::Connector:
            // The connector's own build, plus whether the pins come with it.
            return {num("pins", "Pins"),
                    num("pitch", "Pitch", "mm"),
                    text("shellSize", "Shell size"),
                    text("gender", "Gender"),
                    choice("material", "Shell material", kMetals + kPolymers),
                    choice("plating", "Plating", kMetals),
                    yesNo("withPins", "Includes pins"),
                    choice("pinGaugeMode", "Pin gauge in",
                           {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("pinGauge", "Pin gauge range"),
                    text("temp", "Temperature range", "C"),
                    text("spec", "Specification")};
        case PartKind::ContactPin:
            return {choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("gauge", "Gauge range"),
                    choice("type", "Type",
                           {QStringLiteral("Pin"), QStringLiteral("Socket"),
                            QStringLiteral("Crimp"), QStringLiteral("Solder cup")}),
                    choice("material", "Material", kMetals),
                    choice("plating", "Plating", kMetals),
                    text("spec", "Specification")};
        case PartKind::Ferrule:
            return {text("size", "Size"),
                    range("stud", "Stud insertion size", "mm"),
                    choice("material", "Material", kMetals),
                    choice("insulated", "Insulated",
                           {QStringLiteral("No"), QStringLiteral("Yes")}),
                    text("spec", "Specification")};
        case PartKind::Crimp:
            return {choice("type", "Type",
                           {QStringLiteral("Ring"), QStringLiteral("Fork / spade"),
                            QStringLiteral("Pin"), QStringLiteral("Blade"),
                            QStringLiteral("Butt"), QStringLiteral("Hook"),
                            QStringLiteral("Flag")}),
                    text("size", "Size"),
                    range("stud", "Stud insertion size", "mm"),
                    choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("gauge", "Wire gauge range"),
                    choice("material", "Material", kMetals),
                    choice("insulated", "Insulated",
                           {QStringLiteral("No"), QStringLiteral("Yes")}),
                    text("spec", "Specification")};
        case PartKind::Splice:
            return {choice("type", "Type",
                           {QStringLiteral("Crimp"), QStringLiteral("Solder"),
                            QStringLiteral("Solder sleeve"), QStringLiteral("Ultrasonic weld")}),
                    range("insertion", "Insertion size", "mm"),
                    choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("gauge", "Wire gauge range"),
                    text("ways", "Ways"),
                    text("spec", "Specification")};
        case PartKind::Heatshrink:
            return {choice("material", "Material", kPolymers),
                    range("entry", "Entry size", "mm"),
                    yesNo("adhesive", "Adhesive lined"),
                    text("ratio", "Shrink ratio"),
                    num("wall", "Recovered wall thickness", "mm"),
                    num("shrinkTemp", "Minimum shrink temperature", "C"),
                    text("colour", "Colour"),
                    text("temp", "Temperature range", "C"),
                    text("spec", "Specification")};
        case PartKind::HeatshrinkBoot:
            return {text("shellSize", "Shell size"),
                    range("connEntry", "Connector side entry", "mm"),
                    range("cableEntry", "Cable entry", "mm"),
                    choice("shape", "Shape",
                           {QStringLiteral("Straight"), QStringLiteral("45 degree"),
                            QStringLiteral("90 degree"), QStringLiteral("Transition")}),
                    choice("material", "Material", kPolymers),
                    yesNo("adhesive", "Adhesive lined"),
                    text("spec", "Specification")};
        case PartKind::Backshell:
            return {text("shellSize", "Shell size"),
                    range("connEntry", "Entry size, connector side", "mm"),
                    range("cableEntry", "Entry size, cable side", "mm"),
                    choice("angle", "Angle",
                           {QStringLiteral("Straight"), QStringLiteral("45 degree"),
                            QStringLiteral("90 degree")}),
                    choice("material", "Material", kMetals + kPolymers),
                    choice("plating", "Plating", kMetals),
                    yesNo("shielded", "Shield termination"),
                    text("spec", "Specification")};
        case PartKind::YSplit:
            return {range("main", "Main side", "mm"),
                    choice("legs", "Legs",
                           {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}),
                    range("leg1", "Leg 1 entry", "mm"),
                    range("leg2", "Leg 2 entry", "mm"),
                    range("leg3", "Leg 3 entry", "mm"),
                    choice("material", "Material", kPolymers),
                    yesNo("adhesive", "Adhesive lined"),
                    text("spec", "Specification")};
        case PartKind::ShieldBraid:
            // Braid is bought by a size that is written differently by every maker, so it is a
            // field to fill in rather than a list to choose from.
            return {text("size", "Size"),
                    range("diameter", "Diameter range", "mm"),
                    choice("material", "Material", kMetals),
                    text("coverage", "Coverage", "%"),
                    num("bendRadius", "Minimum bend radius", "mm"),
                    text("spec", "Specification")};
        case PartKind::Grommet:
            return {text("size", "Size"),
                    range("hole", "Panel hole", "mm"),
                    range("cableEntry", "Cable entry", "mm"),
                    choice("material", "Material", kPolymers),
                    text("spec", "Specification")};
        case PartKind::CableGland:
            return {text("thread", "Thread"),
                    range("cableEntry", "Cable entry", "mm"),
                    text("ip", "IP rating"),
                    choice("material", "Material", kMetals + kPolymers),
                    text("spec", "Specification")};
        case PartKind::Sleeve:
            return {choice("type", "Type",
                           {QStringLiteral("Spiral wrap"), QStringLiteral("Braided sleeving"),
                            QStringLiteral("Convoluted"), QStringLiteral("Woven")}),
                    range("diameter", "Bundle diameter", "mm"),
                    num("wall", "Wall thickness", "mm"),
                    choice("material", "Material", kPolymers),
                    text("colour", "Colour"),
                    text("spec", "Specification")};
        case PartKind::Conduit:
            return {text("size", "Nominal size"),
                    range("id", "Inside diameter", "mm"),
                    choice("material", "Material", kPolymers),
                    yesNo("split", "Split"),
                    text("spec", "Specification")};
        case PartKind::CableTie:
            return {num("length", "Length", "mm"),
                    num("width", "Width", "mm"),
                    range("bundle", "Bundle diameter", "mm"),
                    choice("material", "Material", kPolymers),
                    text("tensile", "Tensile strength", "N"),
                    text("spec", "Specification")};
        case PartKind::Marker:
            return {choice("type", "Type",
                           {QStringLiteral("Heatshrink sleeve"), QStringLiteral("Wrap label"),
                            QStringLiteral("Flag label"), QStringLiteral("Clip-on"),
                            QStringLiteral("Hot stamp")}),
                    range("entry", "Fits wire diameter", "mm"),
                    num("width", "Print width", "mm"),
                    choice("material", "Material", kPolymers),
                    text("spec", "Specification")};
        case PartKind::Tape:
            return {choice("type", "Type",
                           {QStringLiteral("PVC"), QStringLiteral("Cloth"),
                            QStringLiteral("Fleece"), QStringLiteral("Kapton"),
                            QStringLiteral("Self-amalgamating")}),
                    num("width", "Width", "mm"),
                    num("length", "Roll length", "m"),
                    text("colour", "Colour"),
                    text("spec", "Specification")};
        case PartKind::Terminal:
            return {choice("type", "Type",
                           {QStringLiteral("Terminal block"), QStringLiteral("Stud"),
                            QStringLiteral("Bus bar"), QStringLiteral("Earth boss")}),
                    text("size", "Size / thread"),
                    num("ways", "Ways"),
                    choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("gauge", "Wire gauge range"),
                    choice("material", "Material", kMetals),
                    text("spec", "Specification")};
        case PartKind::Overmould:
            return {text("shellSize", "Shell size"),
                    range("cableEntry", "Cable entry", "mm"),
                    choice("material", "Material", kPolymers),
                    text("colour", "Colour"),
                    text("spec", "Specification")};
        case PartKind::Resistor:
            return {text("value", "Resistance", "ohm"),
                    text("tolerance", "Tolerance", "%"),
                    text("power", "Power rating", "W"),
                    choice("mounting", "Mounting",
                           {QStringLiteral("Axial leaded"), QStringLiteral("In-line potted"),
                            QStringLiteral("Wire-wound"), QStringLiteral("Surface mount")}),
                    text("tempco", "Temperature coefficient", "ppm/K"),
                    text("voltage", "Working voltage", "V"),
                    text("spec", "Specification")};
        case PartKind::Capacitor:
            return {text("value", "Capacitance", "uF"),
                    text("tolerance", "Tolerance", "%"),
                    text("voltage", "Voltage rating", "V"),
                    choice("dielectric", "Dielectric",
                           {QStringLiteral("Ceramic X7R"), QStringLiteral("Ceramic X5R"),
                            QStringLiteral("Ceramic C0G/NP0"), QStringLiteral("Film"),
                            QStringLiteral("Electrolytic"), QStringLiteral("Tantalum")}),
                    yesNo("polarised", "Polarised"),
                    choice("mounting", "Mounting",
                           {QStringLiteral("Axial leaded"), QStringLiteral("Radial leaded"),
                            QStringLiteral("In-line potted"), QStringLiteral("Surface mount")}),
                    text("spec", "Specification")};
        case PartKind::Inductor:
            return {text("value", "Inductance", "uH"),
                    text("current", "Rated current", "A"),
                    text("dcr", "DC resistance", "ohm"),
                    text("srf", "Self-resonant frequency", "MHz"),
                    choice("core", "Core",
                           {QStringLiteral("Ferrite"), QStringLiteral("Iron powder"),
                            QStringLiteral("Air"), QStringLiteral("Toroid")}),
                    choice("mounting", "Mounting",
                           {QStringLiteral("Axial leaded"), QStringLiteral("Radial leaded"),
                            QStringLiteral("Toroid, wound in place"),
                            QStringLiteral("Surface mount")}),
                    text("spec", "Specification")};
        case PartKind::FerriteBead:
            // A bead is bought by the impedance it shows at a frequency, not by an inductance - so
            // the frequency that figure was measured at is a parameter of its own.
            return {text("impedance", "Impedance", "ohm"),
                    text("atFreq", "at frequency", "MHz"),
                    text("current", "Rated current", "A"),
                    text("dcr", "DC resistance", "ohm"),
                    choice("style", "Style",
                           {QStringLiteral("Clamp-on / split"), QStringLiteral("Solid sleeve"),
                            QStringLiteral("Toroid"), QStringLiteral("Flat cable"),
                            QStringLiteral("Surface mount")}),
                    range("cableEntry", "Fits cable diameter", "mm"),
                    num("turns", "Turns through it"),
                    text("spec", "Specification")};
        case PartKind::Other:
            break;
    }
    // "Other": no fixed parameters - what it is goes in the description, and anything technical in
    // the free-text rows every kind has.
    return {text("size", "Size"), text("material", "Material"), text("spec", "Specification"),
            text("notes", "Notes")};
}

// ---- The standard symbols -----------------------------------------------------------------------
// IEC forms, because the rest of this application is metric and writes "colour": a resistor is a
// plain rectangle rather than a zigzag, and a coil is a row of humps. Everything is centred on the
// origin and sized to sit on a schematic beside the connectors this app draws (a 20-unit body with
// 10-unit leads, against a 15-unit pin pitch), so a symbol dropped on a sheet needs no scaling.
namespace {

Entity symLine(const Vec2& a, const Vec2& b) {
    Entity e;
    e.type = EntityType::Line;
    e.a = a;
    e.b = b;
    return e;
}

Entity symRect(double halfLen, double halfWide) {
    Entity e;
    e.type = EntityType::Rect;
    e.points = {{-halfLen, -halfWide}, {halfLen, -halfWide}, {halfLen, halfWide},
                {-halfLen, halfWide}};
    return e;
}

// One hump of a coil: a half circle standing above the lead line.
Entity symHump(double cx, double r) {
    Entity e;
    e.type = EntityType::Arc;
    e.center = Vec2{cx, 0.0};
    e.radius = r;
    e.startAngle = 0.0;
    e.endAngle = 3.14159265358979323846;   // anticlockwise from +x to -x: the top half
    return e;
}

// A coil of `humps` humps of radius `r`, with leads out to +/-`leadTo`. The pieces are appended to
// `out`, which is what lets a bead be a coil with a core drawn over it.
void appendCoil(std::vector<Entity>& out, int humps, double r, double leadTo) {
    const double span = humps * 2.0 * r;
    const double x0 = -span / 2.0 + r;
    for (int i = 0; i < humps; ++i) out.push_back(symHump(x0 + i * 2.0 * r, r));
    out.push_back(symLine({-leadTo, 0.0}, {-span / 2.0, 0.0}));
    out.push_back(symLine({span / 2.0, 0.0}, {leadTo, 0.0}));
}

} // namespace

QJsonArray builtinSchematicSymbol(PartKind kind) {
    std::vector<Entity> ents;
    switch (kind) {
        case PartKind::Resistor:
            // A rectangle on the line: IEC 60617, and the form a European drawing expects.
            ents.push_back(symRect(10.0, 4.0));
            ents.push_back(symLine({-20.0, 0.0}, {-10.0, 0.0}));
            ents.push_back(symLine({10.0, 0.0}, {20.0, 0.0}));
            break;
        case PartKind::Capacitor:
            // Two plates with a gap: the leads stop at the plates, which is what makes it read as a
            // capacitor rather than as a broken wire.
            ents.push_back(symLine({-2.0, -7.0}, {-2.0, 7.0}));
            ents.push_back(symLine({2.0, -7.0}, {2.0, 7.0}));
            ents.push_back(symLine({-20.0, 0.0}, {-2.0, 0.0}));
            ents.push_back(symLine({2.0, 0.0}, {20.0, 0.0}));
            break;
        case PartKind::Inductor:
            appendCoil(ents, 4, 4.0, 26.0);
            break;
        case PartKind::FerriteBead:
            // The coil with a core over it: two bars above the humps. That is the difference between
            // a plain coil and something wound on ferrite, and it is the whole point of the part.
            appendCoil(ents, 4, 4.0, 26.0);
            ents.push_back(symLine({-16.0, 6.0}, {16.0, 6.0}));
            ents.push_back(symLine({-16.0, 8.0}, {16.0, 8.0}));
            break;
        default:
            return {};   // not in the circuit: nothing to draw on a schematic
    }
    return partDrawingFromEntities(ents);
}

QJsonArray partSchematicDrawing(const LibraryPart& part) {
    return part.hasSchematic() ? part.schematic : builtinSchematicSymbol(part.kind);
}

QString LibraryPart::title() const {
    if (!mfrPn.isEmpty() && !description.isEmpty())
        return QStringLiteral("%1 — %2").arg(mfrPn, description);
    if (!mfrPn.isEmpty()) return mfrPn;
    if (!description.isEmpty()) return description;
    return QStringLiteral("(unnamed %1)").arg(partKindName(kind).toLower());
}

QString PartsLibrary::defaultPath() {
    // The application's own library, and it has a name of its own: edrawlib. Libraries live in a
    // folder of their own, so a shelf of them can be kept side by side and the application's files do
    // not all sit in one heap.
    const QString path = AppPaths::libraryDir() + QStringLiteral("/edrawlib.plb");
    // Every place a library of ours has ever been written is taken over on the way past - MOVED, not
    // copied, so there is never a second copy of the same shelf to wonder which one is being edited.
    // The parts already in it are the parts that must still be there afterwards.
    AppPaths::adopt(AppPaths::libraryDir() + QStringLiteral("/parts-library.plb"), path);
    AppPaths::adopt(AppPaths::root() + QStringLiteral("/parts-library.plb"), path);
    AppPaths::adopt(legacyLibraryPath(), path);
    return path;
}

// Where the library used to be written: the same folder with the organisation and application name
// repeated. A library kept there is taken over rather than left behind.
QString legacyLibraryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/Whatson/Edraw/parts-library.plb");
}

QString PartsLibrary::fileFilter() {
    return QStringLiteral("Part library (*.plb);;All files (*)");
}

QJsonObject PartsLibrary::toJson() const {
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("edraw-parts-library");
    root[QStringLiteral("version")] = 1;
    QJsonArray arr;
    for (const LibraryPart& p : parts) {
        QJsonObject o;
        o[QStringLiteral("id")] = p.id;
        o[QStringLiteral("kind")] = partKindKey(p.kind);
        o[QStringLiteral("description")] = p.description;
        o[QStringLiteral("mfrName")] = p.mfrName;
        o[QStringLiteral("mfrPn")] = p.mfrPn;
        o[QStringLiteral("internalPn")] = p.internalPn;
        o[QStringLiteral("unit")] = p.unit;
        QJsonObject params;
        for (auto it = p.params.constBegin(); it != p.params.constEnd(); ++it)
            params[it.key()] = it.value();
        o[QStringLiteral("params")] = params;
        if (!p.schematic.isEmpty()) o[QStringLiteral("schematic")] = p.schematic;
        if (!p.datasheets.isEmpty()) {
            QJsonArray sheets;
            for (const QString& s : p.datasheets) sheets.append(s);
            o[QStringLiteral("datasheets")] = sheets;
            // The first one under the old key as well, so a library written here still opens in a
            // build that only knows about one datasheet per part.
            o[QStringLiteral("datasheet")] = p.datasheets.front();
        }
        if (!p.mechanical.isEmpty()) o[QStringLiteral("mechanical")] = p.mechanical;
        arr.append(o);
    }
    root[QStringLiteral("parts")] = arr;
    return root;
}

void PartsLibrary::fromJson(const QJsonObject& root) {
    parts.clear();
    for (const auto& v : root.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject o = v.toObject();
        LibraryPart p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.kind = partKindFromKey(o.value(QStringLiteral("kind")).toString());
        p.description = o.value(QStringLiteral("description")).toString();
        p.mfrName = o.value(QStringLiteral("mfrName")).toString();
        p.mfrPn = o.value(QStringLiteral("mfrPn")).toString();
        p.internalPn = o.value(QStringLiteral("internalPn")).toString();
        p.unit = o.value(QStringLiteral("unit")).toString(defaultUnitForKind(p.kind));
        const QJsonObject params = o.value(QStringLiteral("params")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            p.params.insert(it.key(), it.value().toString());
        p.schematic = o.value(QStringLiteral("schematic")).toArray();
        // The list if there is one, else the single name an older library was written with.
        if (o.contains(QStringLiteral("datasheets"))) {
            for (const auto& v : o.value(QStringLiteral("datasheets")).toArray()) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty() && !p.datasheets.contains(s)) p.datasheets << s;
            }
        } else {
            const QString one = o.value(QStringLiteral("datasheet")).toString().trimmed();
            if (!one.isEmpty()) p.datasheets << one;
        }
        p.mechanical = o.value(QStringLiteral("mechanical")).toArray();
        if (p.id.isEmpty()) p.id = QStringLiteral("p%1").arg(parts.size() + 1);
        parts.push_back(p);
    }
}

bool PartsLibrary::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    fromJson(doc.object());
    return true;
}

bool PartsLibrary::save(const QString& path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

LibraryPart* PartsLibrary::byId(const QString& id) {
    for (LibraryPart& p : parts)
        if (p.id == id) return &p;
    return nullptr;
}

const LibraryPart* PartsLibrary::byId(const QString& id) const {
    for (const LibraryPart& p : parts)
        if (p.id == id) return &p;
    return nullptr;
}

LibraryPart& PartsLibrary::add(PartKind kind) {
    LibraryPart p;
    p.kind = kind;
    p.unit = defaultUnitForKind(kind);
    for (int i = static_cast<int>(parts.size()) + 1;; ++i) {
        const QString id = QStringLiteral("%1-%2").arg(partKindKey(kind)).arg(i);
        if (!byId(id)) {
            p.id = id;
            break;
        }
    }
    parts.push_back(p);
    return parts.back();
}

void PartsLibrary::remove(const QString& id) {
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].id == id) {
            parts.erase(parts.begin() + static_cast<long>(i));
            return;
        }
}

LibraryPart* PartsLibrary::duplicate(const QString& id) {
    const LibraryPart* src = byId(id);
    if (!src) return nullptr;
    LibraryPart copy = *src;
    LibraryPart& fresh = add(src->kind);
    const QString newId = fresh.id;
    copy.id = newId;
    copy.description = src->description.isEmpty()
                           ? QStringLiteral("(copy)")
                           : src->description + QStringLiteral(" (copy)");
    fresh = copy;
    return byId(newId);
}

std::vector<const LibraryPart*> PartsLibrary::ofKind(PartKind kind) const {
    std::vector<const LibraryPart*> out;
    for (const LibraryPart& p : parts)
        if (p.kind == kind) out.push_back(&p);
    return out;
}

QString describePart(const LibraryPart& part) {
    // What each kind leads with, then the parameters worth naming, in reading order. A range is
    // written "a-b"; a yes/no is named only when it is yes.
    struct Piece {
        const char* key;    // parameter key, or "" for a fixed word
        const char* suffix; // written after the value ("AWG", "mm", "V", ...)
        bool range = false;
        bool flagOnly = false; // a yes/no: the label is written when it is on
        const char* label = "";
    };
    // A CABLE is described by what is inside it, which is not a list of parameters: "Cable 3x24AWG +
    // FTP 2x2x28AWG + coax, foil, braid, black, PTFE" is what a purchase order and a drawing both want
    // to read, and the structure field already says most of it. So it is written here rather than
    // assembled from pieces like the rest.
    if (part.kind == PartKind::Cable) {
        QStringList bits;
        bits << QStringLiteral("Cable");
        const QString structure = part.value(QStringLiteral("structure")).trimmed();
        if (!structure.isEmpty()) bits << structure;
        const QString jacket = part.value(QStringLiteral("jacket")).trimmed();
        if (!jacket.isEmpty()) bits << jacket;
        const QString od = part.value(QStringLiteral("od")).trimmed();
        if (!od.isEmpty() && od.toDouble() > 0.0) bits << QStringLiteral("%1 mm OD").arg(od);
        return bits.join(QStringLiteral(", "));
    }

    QString lead;
    std::vector<Piece> order;
    switch (part.kind) {
        case PartKind::Wire:
            lead = QStringLiteral("Hook-up wire");
            order = {{"gauge", "AWG"}, {"notes", ""}, {"conductor", ""}, {"material", ""},
                     {"insulation", ""}, {"voltage", "V"}, {"colour", ""}};
            break;
        case PartKind::Connector:
            lead = QStringLiteral("Connector");
            order = {{"pins", "way"}, {"pitch", "mm pitch"}, {"shellSize", "", false, false, "shell"},
                     {"gender", ""}, {"material", ""}, {"plating", ""},
                     {"withPins", "", false, true, "with contacts"}};
            break;
        case PartKind::ContactPin:
            lead = QStringLiteral("Contact");
            order = {{"type", ""}, {"gauge", "AWG", true}, {"material", ""}, {"plating", ""}};
            break;
        case PartKind::Ferrule:
            lead = QStringLiteral("Ferrule");
            order = {{"size", ""}, {"stud", "mm sleeve", true}, {"material", ""},
                     {"insulated", "", false, true, "insulated"}};
            break;
        case PartKind::Crimp:
            lead = QStringLiteral("Crimp terminal");
            order = {{"type", ""}, {"size", "stud"}, {"gauge", "AWG", true}, {"material", ""},
                     {"insulated", "", false, true, "insulated"}};
            break;
        case PartKind::Splice:
            lead = QStringLiteral("Joint");
            order = {{"type", ""}, {"gauge", "AWG", true}, {"insertion", "mm", true},
                     {"ways", "way"}};
            break;
        case PartKind::Heatshrink:
            lead = QStringLiteral("Heatshrink");
            order = {{"entry", "mm", true}, {"ratio", "shrink"}, {"material", ""},
                     {"adhesive", "", false, true, "adhesive lined"}, {"colour", ""}};
            break;
        case PartKind::HeatshrinkBoot:
            lead = QStringLiteral("Heatshrink boot");
            order = {{"shape", ""}, {"shellSize", "", false, false, "shell"},
                     {"connEntry", "mm connector side", true}, {"cableEntry", "mm cable", true},
                     {"material", ""}, {"adhesive", "", false, true, "adhesive lined"}};
            break;
        case PartKind::Backshell:
            lead = QStringLiteral("Backshell");
            order = {{"angle", ""}, {"shellSize", "", false, false, "shell"},
                     {"cableEntry", "mm cable", true}, {"material", ""}, {"plating", ""},
                     {"shielded", "", false, true, "shield termination"}};
            break;
        case PartKind::YSplit:
            lead = QStringLiteral("Y-split heatshrink");
            order = {{"legs", "leg"}, {"main", "mm main", true}, {"material", ""},
                     {"adhesive", "", false, true, "adhesive lined"}};
            break;
        case PartKind::ShieldBraid:
            lead = QStringLiteral("Shield braid");
            order = {{"size", ""}, {"diameter", "mm", true}, {"material", ""},
                     {"coverage", "% coverage"}};
            break;
        // The components: a catalogue names these by their value first, which is how anyone looks
        // for one - "100 ohm, 1 %, 0.25 W" rather than "resistor, axial".
        case PartKind::Resistor:
            lead = QStringLiteral("Resistor");
            order = {{"value", "ohm"}, {"tolerance", "%"}, {"power", "W"}, {"mounting", ""},
                     {"tempco", "ppm/K"}};
            break;
        case PartKind::Capacitor:
            lead = QStringLiteral("Capacitor");
            order = {{"value", "uF"}, {"tolerance", "%"}, {"voltage", "V"}, {"dielectric", ""},
                     {"mounting", ""}, {"polarised", "", false, true, "polarised"}};
            break;
        case PartKind::Inductor:
            lead = QStringLiteral("Coil");
            order = {{"value", "uH"}, {"current", "A"}, {"dcr", "ohm"},
                     {"core", "", false, false, "core"}, {"mounting", ""}};
            break;
        case PartKind::FerriteBead:
            lead = QStringLiteral("Ferrite bead");
            // The impedance means nothing without the frequency it was measured at, so the two are
            // written together.
            order = {{"impedance", "ohm"}, {"atFreq", "MHz", false, false, "at"},
                     {"current", "A"}, {"style", ""}, {"cableEntry", "mm cable", true}};
            break;
        default:
            // Everything else reads from the parameters it happens to have, led by its kind.
            lead = partKindName(part.kind);
            order = {{"type", ""}, {"size", ""}, {"thread", ""}, {"length", "mm"},
                     {"width", "mm"}, {"material", ""}, {"colour", ""}};
            break;
    }
    QStringList bits;
    for (const Piece& piece : order) {
        const QString key = QString::fromLatin1(piece.key);
        if (piece.flagOnly) {
            if (part.value(key) == QLatin1String("1") ||
                part.value(key).compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0)
                bits << QString::fromLatin1(piece.label);
            continue;
        }
        QString value;
        if (piece.range) {
            const QString lo = part.value(key + QStringLiteral("Min"));
            const QString hi = part.value(key + QStringLiteral("Max"));
            if (lo.isEmpty() && hi.isEmpty()) continue;
            value = (lo == hi || hi.isEmpty()) ? lo
                                               : (lo.isEmpty() ? hi
                                                               : QStringLiteral("%1-%2").arg(lo, hi));
        } else {
            value = part.value(key).trimmed();
        }
        if (value.isEmpty()) continue;
        QString bit = value;
        if (*piece.label) bit = QStringLiteral("%1 %2").arg(QString::fromLatin1(piece.label), value);
        if (*piece.suffix) bit += QStringLiteral(" ") + QString::fromLatin1(piece.suffix);
        bits << bit;
    }
    if (bits.isEmpty()) return lead;
    return lead + QStringLiteral(", ") + bits.join(QStringLiteral(", "));
}

// ---- Several libraries at once ----------------------------------------------------------

namespace {
constexpr char kCatalogKey[] = "partsLibrary/connected";
} // namespace

QString PartsCatalog::activePath() const {
    if (active_ < 0 || active_ >= paths_.size()) return QString();
    return paths_.at(active_);
}

void PartsCatalog::setActiveIndex(int i) {
    if (i >= 0 && i < static_cast<int>(libs_.size())) active_ = i;
}

void PartsCatalog::setActivePath(const QString& path) {
    const int at = paths_.indexOf(path);
    if (at >= 0) active_ = at;
}

bool PartsCatalog::connectLibrary(const QString& path) {
    if (path.isEmpty()) return false;
    const int at = paths_.indexOf(path);
    if (at >= 0) { // already connected: just work on it
        active_ = at;
        return true;
    }
    PartsLibrary lib;
    const bool read = lib.load(path); // a path that is not there yet becomes a new library
    paths_ << path;
    libs_.push_back(lib);
    active_ = static_cast<int>(libs_.size()) - 1;
    remember();
    return read;
}

void PartsCatalog::disconnectLibrary(const QString& path) {
    const int at = paths_.indexOf(path);
    if (at < 0) return;
    paths_.removeAt(at);
    libs_.erase(libs_.begin() + at);
    if (active_ >= static_cast<int>(libs_.size())) active_ = static_cast<int>(libs_.size()) - 1;
    if (active_ < 0) active_ = 0;
    remember();
}

void PartsCatalog::openRemembered() {
    // The default library was called parts-library.plb until it was renamed to edrawlib.plb, and
    // defaultPath() moves it. What is REMEMBERED has to follow it, or the old name would be opened,
    // found missing, and a second empty library made in its place.
    {
        QSettings st;
        const QString key = QLatin1String(kCatalogKey);
        QStringList remembered = st.value(key).toStringList();
        bool changed = false;
        const QString oldName = QStringLiteral("/parts-library.plb");
        const QString newPath = AppPaths::libraryDir() + QStringLiteral("/edrawlib.plb");
        for (QString& p : remembered)
            if (p.endsWith(oldName)) {
                p = newPath;
                changed = true;
            }
        if (changed) {
            remembered.removeDuplicates();
            st.setValue(key, remembered);
        }
    }
    paths_.clear();
    libs_.clear();
    QSettings st;
    QStringList want = st.value(QLatin1String(kCatalogKey)).toStringList();
    // The default library is always connected: it is where the parts everyone starts from live, so
    // it can never be left out of the list (however that list was last written).
    const QString def = PartsLibrary::defaultPath();
    if (!want.contains(def)) want.prepend(def);
    for (const QString& p : want) {
        PartsLibrary lib;
        // The old location is never connected in its own right: whatever is in it is taken over
        // into the default library below, and connecting both would list every part twice.
        if (p == legacyLibraryPath() && p != def) continue;
        const bool read = lib.load(p);
        // A library that is not there at all, and is not the default one, is dropped from the list
        // rather than kept as an empty entry - it was connected from somewhere that has gone.
        if (!read && p != def && !QFile::exists(p)) continue;
        if (p == def && lib.parts.empty()) {
            // A library written where it used to be kept comes across, parts and all, before the
            // shelf is seeded - nothing anyone put in it is lost to the move.
            PartsLibrary old;
            if (old.load(legacyLibraryPath()) && !old.parts.empty()) {
                lib = old;
                lib.save(p);
            }
        }
        if (p == def && lib.parts.empty()) {
            // Nothing on the shelf: fill it with the parts a harness actually starts from, and keep
            // the file so it can be edited afterwards. This also repairs a library left empty by an
            // earlier version.
            lib = seedLibrary();
            lib.save(p);
        }
        paths_ << p;
        libs_.push_back(lib);
    }
    // The default library is the one worked on unless something else is chosen.
    active_ = std::max(0, static_cast<int>(paths_.indexOf(def)));
    remember();
}

void PartsCatalog::remember() const {
    if (!remembering_) return;
    QSettings st;
    st.setValue(QLatin1String(kCatalogKey), paths_);
}

PartsLibrary& PartsCatalog::active() {
    if (active_ >= 0 && active_ < static_cast<int>(libs_.size())) return libs_[active_];
    return empty_;
}

const PartsLibrary& PartsCatalog::active() const {
    if (active_ >= 0 && active_ < static_cast<int>(libs_.size())) return libs_[active_];
    return empty_;
}

PartsLibrary* PartsCatalog::byPath(const QString& path) {
    const int at = paths_.indexOf(path);
    return at >= 0 ? &libs_[at] : nullptr;
}

bool PartsCatalog::saveAll() const {
    bool all = true;
    for (int i = 0; i < static_cast<int>(libs_.size()); ++i)
        if (!libs_[i].save(paths_.at(i))) all = false;
    return all;
}

const LibraryPart* PartsCatalog::byId(const QString& id) const {
    for (const PartsLibrary& lib : libs_)
        if (const LibraryPart* p = lib.byId(id)) return p;
    // The library of its own that active() hands back when no file is connected. Without this a
    // part added to it could be written and never read again.
    if (libs_.empty()) return empty_.byId(id);
    return nullptr;
}

std::vector<const LibraryPart*> PartsCatalog::ofKind(PartKind kind) const {
    std::vector<const LibraryPart*> out;
    for (const PartsLibrary& lib : libs_)
        for (const LibraryPart* p : lib.ofKind(kind)) out.push_back(p);
    if (libs_.empty())
        for (const LibraryPart* p : empty_.ofKind(kind)) out.push_back(p);
    return out;
}

int PartsCatalog::partCount() const {
    int n = 0;
    for (const PartsLibrary& lib : libs_) n += static_cast<int>(lib.parts.size());
    if (libs_.empty()) n += static_cast<int>(empty_.parts.size());
    return n;
}

QString PartsCatalog::pathOf(const QString& partId) const {
    for (int i = 0; i < static_cast<int>(libs_.size()); ++i)
        if (libs_[i].byId(partId)) return paths_.at(i);
    return QString();
}

PartsLibrary PartsCatalog::merged() const {
    PartsLibrary out;
    for (const PartsLibrary& lib : libs_)
        for (const LibraryPart& p : lib.parts) out.parts.push_back(p);
    if (libs_.empty())
        for (const LibraryPart& p : empty_.parts) out.parts.push_back(p);
    return out;
}

// ---- The library a first run starts from ------------------------------------------------
// Real parts, with the manufacturer's own numbers and the figures from their data sheets (checked
// against DigiKey's listings). Alpha Wire's premium hook-up wire covers 16 to 30 AWG as the 3050
// family - tinned copper, PVC, 300 V, -40 to +105 C - and 14 AWG comes from the 891441 and 7035
// series, so the range asked for is covered end to end.
namespace {

// An imperial size as it is written on the reel, and what it comes to in millimetres.
struct ShrinkSize {
    const char* designation;   // "3/16", "1-1/2"
    double expandedMm;         // the size itself: the inside diameter as supplied
};

// The imperial sizes the tubing families are made in. Which of them a given family covers differs, so each
// family names the range it uses out of this list.
const ShrinkSize kImperial[] = {
    {"3/64", 1.2},  {"1/16", 1.6},  {"3/32", 2.4},  {"1/8", 3.2},   {"3/16", 4.8},
    {"1/4", 6.4},   {"3/8", 9.5},   {"1/2", 12.7},  {"3/4", 19.1},  {"1", 25.4},
    {"1-1/4", 31.8},{"1-1/2", 38.1},{"2", 50.8},    {"2-1/2", 63.5},{"3", 76.2},
    {"4", 101.6}};

// A part number already in the library? Families are added by the shopfloor as well, and adding a second
// DR-25 3/8 would be a second line on every bill of materials that used it.
bool hasPn(const PartsLibrary& lib, const QString& pn) {
    for (const LibraryPart& part : lib.parts)
        if (part.mfrPn.compare(pn, Qt::CaseInsensitive) == 0) return true;
    return false;
}

} // namespace

int addStandardHeatshrink(PartsLibrary& lib) {
    int added = 0;
    const QString nominal =
        QStringLiteral("Sizes and designations are the manufacturer's. Diameters are NOMINAL, from the "
                       "size designation and the shrink ratio; wall thickness is not filled in. Check the "
                       "current datasheet before committing a build to a tolerance.");

    // ---- tubing, by family ---------------------------------------------------------------------------
    struct Family {
        const char* pn;          // how the part number is written, with %1 for the size
        const char* mfr;
        const char* what;        // the description, with %1 for the size
        const char* material;
        const char* ratio;
        double ratioValue;
        bool adhesive;
        const char* colour;
        const char* temp;        // continuous operating range, as the family is published
        const char* shrinkTemp;  // minimum temperature for full recovery, blank if not certain
        const char* spec;
        const char* firstSize;   // the smallest size the family is made in
        const char* lastSize;    // and the largest
    };
    const Family families[] = {
        // Flexible, fluid-resistant elastomeric tubing - the aerospace harness default.
        {"DR-25-%1-0", "TE Connectivity (Raychem)",
         "Heatshrink tubing DR-25, %1, 2:1, fluid resistant, black", "Elastomer", "2:1", 2.0, false,
         "Black", "-75 to +150", "175", "SAE-AMS-DTL-23053/16", "3/16", "3"},
        // General purpose thin wall, flame retardant.
        {"RNF-100-%1-0", "TE Connectivity (Raychem)",
         "Heatshrink tubing RNF-100, %1, 2:1, thin wall, flame retardant, black", "Polyolefin", "2:1",
         2.0, false, "Black", "-55 to +135", "100", "SAE-AMS-DTL-23053/5", "3/64", "2"},
        // Clear, so a joint can be inspected through it after it is covered.
        {"RT-375-%1-C", "TE Connectivity (Raychem)",
         "Heatshrink tubing RT-375, %1, 2:1, clear — the joint stays visible", "Fluoropolymer", "2:1",
         2.0, false, "Clear", "-55 to +150", "", "Raychem RT-375", "1/16", "2"},
    };
    for (const Family& f : families) {
        bool inRange = false;
        for (const ShrinkSize& s : kImperial) {
            if (!inRange && QLatin1String(s.designation) == QLatin1String(f.firstSize)) inRange = true;
            if (!inRange) continue;
            const QString size = QString::fromLatin1(s.designation);
            const QString pn = QString::fromLatin1(f.pn).arg(size);
            if (!hasPn(lib, pn)) {
                LibraryPart& part = lib.add(PartKind::Heatshrink);
                part.description = QString::fromLatin1(f.what).arg(size);
                part.mfrName = QString::fromLatin1(f.mfr);
                part.mfrPn = pn;
                part.unit = QStringLiteral("m");
                part.params.insert(QStringLiteral("material"), QString::fromLatin1(f.material));
                // What bundle it fits: from fully recovered up to as supplied.
                part.params.insert(QStringLiteral("entryMin"),
                                   QString::number(s.expandedMm / f.ratioValue, 'f', 1));
                part.params.insert(QStringLiteral("entryMax"), QString::number(s.expandedMm, 'f', 1));
                part.params.insert(QStringLiteral("adhesive"),
                                   f.adhesive ? QStringLiteral("1") : QStringLiteral("0"));
                part.params.insert(QStringLiteral("ratio"), QString::fromLatin1(f.ratio));
                if (*f.shrinkTemp)
                    part.params.insert(QStringLiteral("shrinkTemp"),
                                       QString::fromLatin1(f.shrinkTemp));
                part.params.insert(QStringLiteral("colour"), QString::fromLatin1(f.colour));
                part.params.insert(QStringLiteral("temp"), QString::fromLatin1(f.temp));
                part.params.insert(QStringLiteral("spec"), QString::fromLatin1(f.spec));
                part.params.insert(QStringLiteral("notes"),
                                   QStringLiteral("Size %1 in (%2 mm as supplied, %3 mm recovered). %4")
                                       .arg(size)
                                       .arg(s.expandedMm, 0, 'f', 1)
                                       .arg(s.expandedMm / f.ratioValue, 0, 'f', 1)
                                       .arg(nominal));
                ++added;
            }
            if (QLatin1String(s.designation) == QLatin1String(f.lastSize)) break;
        }
    }

    // ---- ATUM: adhesive lined, 4:1, and sized in millimetres as expanded/recovered -------------------
    struct AtumSize { const char* designation; double expandedMm; double recoveredMm; };
    const AtumSize atum[] = {{"4/1", 4.0, 1.0},    {"6/2", 6.0, 2.0},    {"8/2", 8.0, 2.0},
                             {"10/3", 10.0, 3.0},  {"12/3", 12.0, 3.0},  {"16/4", 16.0, 4.0},
                             {"20/6", 20.0, 6.0},  {"24/6", 24.0, 6.0},  {"32/8", 32.0, 8.0},
                             {"40/12", 40.0, 12.0},{"52/16", 52.0, 16.0}};
    for (const AtumSize& a : atum) {
        const QString pn = QStringLiteral("ATUM-%1-0").arg(QString::fromLatin1(a.designation));
        if (hasPn(lib, pn)) continue;
        LibraryPart& part = lib.add(PartKind::Heatshrink);
        part.description =
            QStringLiteral("Heatshrink tubing ATUM %1, 4:1, adhesive lined, black")
                .arg(QString::fromLatin1(a.designation));
        part.mfrName = QStringLiteral("TE Connectivity (Raychem)");
        part.mfrPn = pn;
        part.unit = QStringLiteral("m");
        part.params.insert(QStringLiteral("material"), QStringLiteral("Polyolefin"));
        part.params.insert(QStringLiteral("entryMin"), QString::number(a.recoveredMm, 'f', 1));
        part.params.insert(QStringLiteral("entryMax"), QString::number(a.expandedMm, 'f', 1));
        part.params.insert(QStringLiteral("adhesive"), QStringLiteral("1"));
        part.params.insert(QStringLiteral("ratio"), QStringLiteral("4:1"));
        part.params.insert(QStringLiteral("colour"), QStringLiteral("Black"));
        part.params.insert(QStringLiteral("temp"), QStringLiteral("-55 to +110"));
        part.params.insert(QStringLiteral("spec"), QStringLiteral("Raychem ATUM"));
        part.params.insert(
            QStringLiteral("notes"),
            QStringLiteral("The designation IS the size: %1 mm as supplied, %2 mm recovered. Adhesive "
                           "lined, so it seals as well as covers. %3")
                .arg(a.expandedMm, 0, 'f', 0)
                .arg(a.recoveredMm, 0, 'f', 0)
                .arg(nominal));
        ++added;
    }

    // ---- TMS-SCE: the identification sleeve, in white and in yellow ----------------------------------
    //
    // A marker, not tubing: it is put on to be PRINTED, and what matters about it is what it fits and how
    // much room there is to print on. Which is why it goes in as a Marker - it appears on a drawing as a
    // wire marking, and on a bill of materials as a sleeve.
    struct SleeveColour { const char* code; const char* name; };
    const SleeveColour sleeveColours[] = {{"9", "White"}, {"4", "Yellow"}};
    const ShrinkSize sleeveSizes[] = {{"3/32", 2.4}, {"1/8", 3.2},   {"3/16", 4.8}, {"1/4", 6.4},
                                      {"3/8", 9.5},  {"1/2", 12.7},  {"3/4", 19.1}, {"1", 25.4},
                                      {"1-1/2", 38.1}, {"2", 50.8}};
    for (const SleeveColour& c : sleeveColours) {
        for (const ShrinkSize& s : sleeveSizes) {
            const QString size = QString::fromLatin1(s.designation);
            // The middle figure is the printed length of the sleeve; 2.0 in is the usual stock length.
            const QString pn = QStringLiteral("TMS-SCE-%1-2.0-%2")
                                   .arg(size, QString::fromLatin1(c.code));
            if (hasPn(lib, pn)) continue;
            LibraryPart& part = lib.add(PartKind::Marker);
            part.description = QStringLiteral("Identification sleeve TMS-SCE %1, 2:1, %2, printable")
                                   .arg(size, QString::fromLatin1(c.name).toLower());
            part.mfrName = QStringLiteral("TE Connectivity (Raychem)");
            part.mfrPn = pn;
            part.unit = QStringLiteral("ea");
            part.params.insert(QStringLiteral("type"), QStringLiteral("Heatshrink sleeve"));
            part.params.insert(QStringLiteral("entryMin"), QString::number(s.expandedMm / 2.0, 'f', 1));
            part.params.insert(QStringLiteral("entryMax"), QString::number(s.expandedMm, 'f', 1));
            part.params.insert(QStringLiteral("material"), QStringLiteral("Polyolefin"));
            part.params.insert(QStringLiteral("width"), QStringLiteral("50.8"));   // 2.0 in of print
            part.params.insert(QStringLiteral("spec"), QStringLiteral("SAE-AMS-DTL-23053/5"));
            part.params.insert(
                QStringLiteral("notes"),
                QStringLiteral("%1 sleeve, size %2 in, 2.0 in printed length (other lengths are ordered "
                               "by changing the middle figure of the part number). %3")
                    .arg(QString::fromLatin1(c.name), size, nominal));
            ++added;
        }
    }
    return added;
}

PartsLibrary seedLibrary() {
    PartsLibrary lib;
    struct WireRow {
        const char* pn;       // manufacturer part number (black, 100 ft spool)
        const char* awg;
        const char* mm2;
        const char* strands;  // stranding, as the data sheet writes it
        const char* od;       // nominal outside diameter, mm
        const char* dcr;      // conductor resistance, ohm per 1000 ft at 20 C
    };
    // The 3050 family, gauge by gauge, plus the two 14 AWG series.
    const WireRow rows[] = {
        {"7035 BK005", "14", "2.08", "41/30", "3.30", "2.8"},
        {"891441 BK005", "14", "2.08", "105/34", "3.56", "2.8"},
        {"3057 BK005", "16", "1.31", "26/30", "2.36", "4.4"},
        {"3055 BK005", "18", "0.82", "16/30", "2.08", "7.0"},
        {"3053 BK005", "20", "0.52", "10/30", "1.80", "11.1"},
        {"3051 BK005", "22", "0.33", "7/30", "1.60", "17.5"},
        {"3050 BK005", "24", "0.20", "7/32", "1.42", "27.7"},
        {"3049 BK005", "26", "0.13", "7/34", "1.24", "44.0"},
        {"3048 BK005", "28", "0.08", "7/36", "1.09", "70.8"},
        {"3047 BK005", "30", "0.05", "7/38", "0.94", "113.0"},
    };
    for (const WireRow& r : rows) {
        LibraryPart& p = lib.add(PartKind::Wire);
        p.description = QStringLiteral("Hook-up wire, %1 AWG, PVC, tinned copper, black")
                            .arg(QString::fromLatin1(r.awg));
        p.mfrName = QStringLiteral("Alpha Wire");
        p.mfrPn = QString::fromLatin1(r.pn);
        p.unit = QStringLiteral("m");
        p.params.insert(QStringLiteral("gaugeMode"), QStringLiteral("AWG"));
        p.params.insert(QStringLiteral("gauge"), QString::fromLatin1(r.awg));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Tinned copper"));
        p.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
        p.params.insert(QStringLiteral("strands"), QString::fromLatin1(r.strands));
        p.params.insert(QStringLiteral("insulation"), QStringLiteral("PVC"));
        p.params.insert(QStringLiteral("voltage"), QStringLiteral("300"));
        p.params.insert(QStringLiteral("od"), QString::fromLatin1(r.od));
        // The data sheet gives ohm per 1000 ft; a harness is measured in metres, so it is kept per
        // metre - 1000 ft is 304.8 m.
        p.params.insert(QStringLiteral("resistance"),
                        QString::number(QString::fromLatin1(r.dcr).toDouble() / 304.8, 'f', 4));
        // Ten times the outside diameter, the rule the data sheet gives.
        p.params.insert(QStringLiteral("bendRadius"),
                        QString::number(QString::fromLatin1(r.od).toDouble() * 10.0, 'f', 1));
        p.params.insert(QStringLiteral("colour"), QStringLiteral("Black"));
        p.params.insert(QStringLiteral("temp"), QStringLiteral("-40 to +105"));
        p.params.insert(QStringLiteral("spec"),
                        QString::fromLatin1(r.pn).startsWith(QLatin1String("7035"))
                            ? QStringLiteral("MIL-W-16878E Type C")
                            : QStringLiteral("Alpha Wire premium hook-up wire"));
        // The cross-section in mm2 as well, since a harness drawn in mm2 asks for it.
        p.params.insert(QStringLiteral("notes"),
                        QStringLiteral("%1 mm2").arg(QString::fromLatin1(r.mm2)));
    }

    // ---- Connectors: Molex rectangular, and Amphenol MIL-DTL-38999 Series III ----
    struct MolexRow {
        const char* pn;
        int pins;
        const char* series;
        const char* what;
    };
    // Molex Micro-Fit 3.0 (43025 / 43645 / 43020 / 43045) and Micro-Fit Plus Versa, all 3.00 mm.
    const MolexRow molex[] = {
        {"0430250200", 2, "Micro-Fit 3.0", "receptacle housing"},
        {"0430250208", 2, "Micro-Fit 3.0", "receptacle housing"},
        {"0436450200", 2, "Micro-Fit 3.0", "receptacle housing"},
        {"0436450300", 3, "Micro-Fit 3.0", "receptacle housing"},
        {"0430250400", 4, "Micro-Fit 3.0", "receptacle housing"},
        {"0436450400", 4, "Micro-Fit 3.0", "receptacle housing"},
        {"0430250600", 6, "Micro-Fit 3.0", "receptacle housing"},
        {"0430250800", 8, "Micro-Fit 3.0", "receptacle housing"},
        {"0430200601", 6, "Micro-Fit 3.0", "plug housing"},
        {"2157591002", 2, "Micro-Fit Plus Versa", "receptacle housing"},
    };
    for (const MolexRow& m : molex) {
        LibraryPart& p = lib.add(PartKind::Connector);
        p.description = QStringLiteral("%1 %2, %3 way, 3.00 mm")
                            .arg(QString::fromLatin1(m.series),
                                 QString::fromLatin1(m.what))
                            .arg(m.pins);
        p.mfrName = QStringLiteral("Molex");
        p.mfrPn = QString::fromLatin1(m.pn);
        p.unit = QStringLiteral("each");
        p.params.insert(QStringLiteral("pins"), QString::number(m.pins));
        p.params.insert(QStringLiteral("pitch"), QStringLiteral("3.00"));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Nylon"));
        p.params.insert(QStringLiteral("gender"),
                        QString::fromLatin1(m.what).startsWith(QLatin1String("plug"))
                            ? QStringLiteral("Plug")
                            : QStringLiteral("Receptacle"));
        // A housing comes without contacts: they are ordered as their own part.
        p.params.insert(QStringLiteral("withPins"), QStringLiteral("0"));
        p.params.insert(QStringLiteral("pinGaugeMode"), QStringLiteral("AWG"));
        p.params.insert(QStringLiteral("pinGaugeMin"), QStringLiteral("30"));
        p.params.insert(QStringLiteral("pinGaugeMax"), QStringLiteral("20"));
        p.params.insert(QStringLiteral("spec"), QString::fromLatin1(m.series));
    }

    // MIL-DTL-38999 Series III, Amphenol Aerospace. The shell letter is the standard's shell size
    // (B=11, C=13, D=15, E=17, G=21, J=25) and the figure after it is the insert arrangement; the
    // contact count is only recorded where the supplier states it.
    struct MilRow {
        const char* pn;
        const char* shell;   // shell size, from the standard's letter
        const char* what;    // plug / receptacle
        const char* contacts; // "" when not stated
    };
    const MilRow mil[] = {
        {"D38999/26WC35PN", "13", "Plug, pin contacts", "22"},
        {"D38999/26WC35SA", "13", "Plug, socket contacts", ""},
        {"D38999/26WD35PN", "15", "Plug, pin contacts", ""},
        {"D38999/26WG29PN", "21", "Plug, pin contacts", ""},
        {"D38999/26WJ4PN", "25", "Plug, pin contacts", ""},
        {"D38999/26WJ4SN", "25", "Plug, socket contacts", ""},
        {"D38999/26KB2PN", "11", "Plug, pin contacts", ""},
        {"D38999/26GE26PN", "17", "Plug, pin contacts", ""},
        {"D38999/20WC35PN", "13", "Wall-mount receptacle", ""},
        {"D38999/24FE26PN", "17", "Jam-nut receptacle", ""},
    };
    for (const MilRow& m : mil) {
        LibraryPart& p = lib.add(PartKind::Connector);
        p.description = QStringLiteral("MIL-DTL-38999 III %1, shell %2")
                            .arg(QString::fromLatin1(m.what), QString::fromLatin1(m.shell));
        p.mfrName = QStringLiteral("Amphenol Aerospace");
        p.mfrPn = QString::fromLatin1(m.pn);
        p.unit = QStringLiteral("each");
        p.params.insert(QStringLiteral("shellSize"), QString::fromLatin1(m.shell));
        if (*m.contacts) p.params.insert(QStringLiteral("pins"), QString::fromLatin1(m.contacts));
        p.params.insert(QStringLiteral("gender"),
                        QString::fromLatin1(m.what).startsWith(QLatin1String("Plug"))
                            ? QStringLiteral("Plug")
                            : QStringLiteral("Receptacle"));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Aluminium"));
        p.params.insert(QStringLiteral("plating"), QStringLiteral("Nickel-plated copper"));
        p.params.insert(QStringLiteral("withPins"), QStringLiteral("0"));
        p.params.insert(QStringLiteral("temp"), QStringLiteral("-65 to +175"));
        p.params.insert(QStringLiteral("spec"),
                        QStringLiteral("MIL-DTL-38999 Series III, Tri-Start thread"));
    }

    // ---- Ferrules: Phoenix Contact AI series, a spread of sizes ----
    // The designation says what it is: AI <mm2>-<sleeve length>. The order number is given where
    // the supplier's listing confirmed it.
    struct FerruleRow {
        const char* designation;
        const char* order;   // "" when not confirmed
        const char* mm2;
        const char* awg;
        const char* len;
        const char* colour;
    };
    const FerruleRow ferrules[] = {
        {"AI 0,25-8", "", "0.25", "24", "8", "Violet"},
        {"AI 0,34-8", "", "0.34", "22", "8", "Turquoise"},
        {"AI 0,5-8", "3240663", "0.5", "20", "8", "White"},
        {"AI 0,5-10", "1212521", "0.5", "20", "10", "White"},
        {"AI 0,75-8", "", "0.75", "18", "8", "Grey"},
        {"AI 1-8", "", "1.0", "17", "8", "Red"},
        {"AI 1,5-8", "", "1.5", "16", "8", "Black"},
        {"AI 2,5-8", "", "2.5", "14", "8", "Blue"},
        {"AI 4-10", "", "4.0", "12", "10", "Grey"},
        {"AI 6-12", "", "6.0", "10", "12", "Yellow"},
    };
    for (const FerruleRow& f : ferrules) {
        LibraryPart& p = lib.add(PartKind::Ferrule);
        p.description = QStringLiteral("Insulated ferrule %1 mm2, %2 mm sleeve, %3")
                            .arg(QString::fromLatin1(f.mm2), QString::fromLatin1(f.len),
                                 QString::fromLatin1(f.colour));
        p.mfrName = QStringLiteral("Phoenix Contact");
        p.mfrPn = *f.order ? QString::fromLatin1(f.order) : QString::fromLatin1(f.designation);
        p.unit = QStringLiteral("each");
        p.params.insert(QStringLiteral("size"),
                        QStringLiteral("%1 mm2 / %2 AWG")
                            .arg(QString::fromLatin1(f.mm2), QString::fromLatin1(f.awg)));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Tinned copper"));
        p.params.insert(QStringLiteral("insulated"), QStringLiteral("Yes"));
        // What it goes into: the sleeve length is the depth it is inserted to.
        p.params.insert(QStringLiteral("studMin"), QString::fromLatin1(f.len));
        p.params.insert(QStringLiteral("studMax"), QString::fromLatin1(f.len));
        p.params.insert(QStringLiteral("spec"),
                        *f.order ? QStringLiteral("Phoenix Contact %1")
                                       .arg(QString::fromLatin1(f.designation))
                                 : QStringLiteral("Phoenix Contact %1 (order number to confirm)")
                                       .arg(QString::fromLatin1(f.designation)));
    }

    // ---- Ring terminals: TE Connectivity / AMP, a spread of studs ----
    struct RingRow {
        const char* pn;
        const char* awg;
        const char* stud;
        const char* studMm; // the stud diameter in mm, for the range fields
        const char* insulation;
    };
    const RingRow rings[] = {
        {"2-36161-4", "10", "#10", "4.8", "Insulated, yellow"},
        {"50881", "16-14", "M3.5", "3.5", "Insulated, blue"},
        {"130663", "16-14", "M5", "5.0", "Insulated, blue"},
        {"130090", "16-14", "M5", "5.0", "Insulated, blue"},
        {"2-130090-1", "16-14", "M5", "5.0", "Insulated, blue"},
        {"323682", "16-14", "M6", "6.0", "Insulated, yellow"},
        {"31904", "16-14", "1/4 in", "6.4", "Insulated, blue"},
        {"36199", "16-14", "1/2 in", "12.7", "Non-insulated"},
        {"1958456-1", "16-14", "#10", "4.8", "Non-insulated"},
        {"32503-2", "16-14", "#8", "4.2", "Non-insulated"},
    };
    for (const RingRow& r : rings) {
        LibraryPart& p = lib.add(PartKind::Crimp);
        p.description = QStringLiteral("Ring terminal, %1 AWG, %2 stud, %3")
                            .arg(QString::fromLatin1(r.awg), QString::fromLatin1(r.stud),
                                 QString::fromLatin1(r.insulation));
        p.mfrName = QStringLiteral("TE Connectivity");
        p.mfrPn = QString::fromLatin1(r.pn);
        p.unit = QStringLiteral("each");
        p.params.insert(QStringLiteral("type"), QStringLiteral("Ring"));
        p.params.insert(QStringLiteral("size"), QString::fromLatin1(r.stud));
        p.params.insert(QStringLiteral("studMin"), QString::fromLatin1(r.studMm));
        p.params.insert(QStringLiteral("studMax"), QString::fromLatin1(r.studMm));
        p.params.insert(QStringLiteral("gaugeMode"), QStringLiteral("AWG"));
        {
            const QString awg = QString::fromLatin1(r.awg);
            const QStringList halves = awg.split(QLatin1Char('-'));
            p.params.insert(QStringLiteral("gaugeMin"), halves.value(0));
            p.params.insert(QStringLiteral("gaugeMax"), halves.value(halves.size() > 1 ? 1 : 0));
        }
        p.params.insert(QStringLiteral("material"), QStringLiteral("Tinned copper"));
        p.params.insert(QStringLiteral("insulated"),
                        QString::fromLatin1(r.insulation).startsWith(QLatin1String("Insulated"))
                            ? QStringLiteral("Yes")
                            : QStringLiteral("No"));
        p.params.insert(QStringLiteral("spec"), QStringLiteral("TE / AMP ring tongue terminal"));
    }
    // And the heat-shrink shelf: DR-25, RNF-100, RT-375, ATUM and TMS-SCE, every size.
    addStandardHeatshrink(lib);
    return lib;
}

std::vector<Entity> partDrawingEntities(const QJsonArray& drawing) {
    std::vector<Entity> out;
    for (const auto& v : drawing) {
        Entity e = Entity::fromJson(v.toObject());
        e.id.clear(); // the document gives it one
        out.push_back(e);
    }
    return out;
}

QJsonArray partDrawingFromEntities(const std::vector<Entity>& ents) {
    QJsonArray arr;
    for (const Entity& e : ents) arr.append(e.toJson());
    return arr;
}

} // namespace Edraw_Desktop

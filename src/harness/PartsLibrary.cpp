#include "harness/PartsLibrary.h"

#include "harness/Harness.h"
#include "harness/PartFromPdf.h"

#include "model/Entity.h"
#include "tools/EntityTransforms.h"

#include "io/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <iterator>

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

void forgetChoice(const QString& key, const QString& value) {
    const QString v = value.trimmed();
    if (v.isEmpty()) return;
    QStringList have = addedChoices(key);
    const int before = have.size();
    have.removeIf([&v](const QString& x) { return x.compare(v, Qt::CaseInsensitive) == 0; });
    if (have.size() == before) return;   // not one of ours: a built-in stays
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

// ---- A connector that comes with its pins fitted ------------------------------------------------

bool partIncludesPins(const LibraryPart& part) {
    const QString v = part.value(QStringLiteral("includePins"));
    return v == QStringLiteral("1") || v.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0 ||
           v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

void setPartIncludesPins(LibraryPart& part, bool on) {
    if (on) part.params.insert(QStringLiteral("includePins"), QStringLiteral("1"));
    else part.params.remove(QStringLiteral("includePins"));
}

// ---- A connector that comes with its wires ------------------------------------------------------

bool partDrawingNotApplicable(const LibraryPart& part, const QString& which) {
    return part.value(QStringLiteral("na_") + which) == QStringLiteral("1");
}

void setPartDrawingNotApplicable(LibraryPart& part, const QString& which, bool na) {
    if (na) part.params.insert(QStringLiteral("na_") + which, QStringLiteral("1"));
    else part.params.remove(QStringLiteral("na_") + which);
}

bool partIsWired(const LibraryPart& part) {
    const QString v = part.value(QStringLiteral("wired"));
    return v == QStringLiteral("1") || v.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0 ||
           v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

void setPartWired(LibraryPart& part, bool on) {
    // Only the tick: what was said about the wires is kept either way, so taking the tick off by mistake
    // and putting it back loses nothing.
    if (on) part.params.insert(QStringLiteral("wired"), QStringLiteral("1"));
    else part.params.remove(QStringLiteral("wired"));
}

std::vector<PartPinWire> partPinWires(const LibraryPart& part) {
    QHash<int, PartPinWire> said;
    int most = part.wayCount();
    const QJsonDocument doc = QJsonDocument::fromJson(part.value(QStringLiteral("pinWires")).toUtf8());
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        PartPinWire w;
        w.pin = o.value(QStringLiteral("pin")).toInt();
        if (w.pin < 1) continue;
        w.color = o.value(QStringLiteral("color")).toString();
        w.size = o.value(QStringLiteral("size")).toString();
        w.unit = o.value(QStringLiteral("unit")).toString(QStringLiteral("AWG"));
        w.insulation = o.value(QStringLiteral("insulation")).toString();
        w.od = o.value(QStringLiteral("od")).toString();
        said.insert(w.pin, w);
        most = std::max(most, w.pin);
    }
    std::vector<PartPinWire> out;
    out.reserve(static_cast<size_t>(most));
    for (int pin = 1; pin <= most; ++pin) {
        PartPinWire w = said.value(pin);
        w.pin = pin;
        if (w.unit.isEmpty()) w.unit = QStringLiteral("AWG");
        out.push_back(w);
    }
    return out;
}

void setPartPinWires(LibraryPart& part, const std::vector<PartPinWire>& wires) {
    QJsonArray arr;
    for (const PartPinWire& w : wires) {
        if (w.pin < 1 || w.empty()) continue;
        QJsonObject o;
        o.insert(QStringLiteral("pin"), w.pin);
        if (!w.color.trimmed().isEmpty()) o.insert(QStringLiteral("color"), w.color.trimmed());
        if (!w.size.trimmed().isEmpty()) o.insert(QStringLiteral("size"), w.size.trimmed());
        o.insert(QStringLiteral("unit"), w.unit == QStringLiteral("mm2") ? w.unit : QStringLiteral("AWG"));
        if (!w.insulation.trimmed().isEmpty()) o.insert(QStringLiteral("insulation"), w.insulation.trimmed());
        if (!w.od.trimmed().isEmpty()) o.insert(QStringLiteral("od"), w.od.trimmed());
        arr.append(o);
    }
    if (arr.isEmpty()) part.params.remove(QStringLiteral("pinWires"));
    else
        part.params.insert(QStringLiteral("pinWires"),
                           QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QStringList wireColorChoices() {
    // DIN 47100 / IEC 60757 order, the one a wiring list is usually read in.
    return {QStringLiteral("White"),  QStringLiteral("Brown"),  QStringLiteral("Green"),
            QStringLiteral("Yellow"), QStringLiteral("Grey"),   QStringLiteral("Pink"),
            QStringLiteral("Blue"),   QStringLiteral("Red"),    QStringLiteral("Black"),
            QStringLiteral("Violet"), QStringLiteral("Orange"), QStringLiteral("Turquoise"),
            QStringLiteral("Green/Yellow")};
}

const std::vector<WireColorStandard>& wireColorStandards() {
    auto list = [](const char* s) { return QString::fromLatin1(s).split(QLatin1Char(',')); };
    static const std::vector<WireColorStandard> kCodes = {
        {QStringLiteral("DIN 47100 / IEC 60757"),
         list("White,Brown,Green,Yellow,Grey,Pink,Blue,Red,Black,Violet,Grey/Pink,Red/Blue,White/Green,"
              "Brown/Green,White/Yellow,Yellow/Brown,White/Grey,Grey/Brown,White/Pink,Pink/Brown")},
        {QStringLiteral("MIL-STD-681 (numbered code)"),
         list("Black,Brown,Red,Orange,Yellow,Green,Blue,Violet,Grey,White,White/Black,White/Brown,White/Red,"
              "White/Orange,White/Yellow,White/Green,White/Blue,White/Violet,White/Grey,Black/Brown")},
        {QStringLiteral("MIL-DTL-27500 (cable cores)"),
         list("White,Blue,Orange,Green,Red,Black,Yellow,Violet,Grey,Brown")},
        {QStringLiteral("IEC 60062 (resistor code)"),
         list("Brown,Red,Orange,Yellow,Green,Blue,Violet,Grey,White,Black")},
        {QStringLiteral("TIA-568 / ISO/IEC 11801 (pairs)"),
         list("White/Blue,Blue/White,White/Orange,Orange/White,White/Green,Green/White,White/Brown,Brown/White,"
              "White/Grey,Grey/White,Red/Blue,Blue/Red,Red/Orange,Orange/Red,Red/Green,Green/Red,Red/Brown,"
              "Brown/Red,Red/Grey,Grey/Red")},
        {QStringLiteral("IEC 60445 (power, L1 L2 L3 N PE)"), list("Brown,Black,Grey,Blue,Green/Yellow")},
        {QStringLiteral("NEC / US power (L1 L2 L3 N G)"), list("Black,Red,Blue,White,Green")},
    };
    return kCodes;
}

bool applyWireColorStandard(std::vector<PartPinWire>& wires, int standard) {
    const auto& codes = wireColorStandards();
    if (standard < 0 || standard >= static_cast<int>(codes.size())) return false;
    const QStringList& c = codes[static_cast<size_t>(standard)].colors;
    if (c.isEmpty()) return false;
    for (PartPinWire& w : wires)
        if (w.pin >= 1) w.color = c.at((w.pin - 1) % c.size());
    return true;
}

QStringList wireInsulationChoices() {
    return {QStringLiteral("PVC"),  QStringLiteral("PTFE"),      QStringLiteral("FEP"),
            QStringLiteral("PFA"),  QStringLiteral("ETFE"),      QStringLiteral("XL-ETFE"),
            QStringLiteral("XLPE"), QStringLiteral("Silicone"),  QStringLiteral("PE"),
            QStringLiteral("PUR"),  QStringLiteral("Polyimide"), QStringLiteral("TPE")};
}

WiredDisplay partWiredDisplay(const LibraryPart& part) {
    WiredDisplay d;
    auto flag = [&](const char* key, bool dflt) {
        const QString v = part.value(QLatin1String(key));
        return v.isEmpty() ? dflt : v == QStringLiteral("1");
    };
    auto num = [&](const char* key, double dflt) {
        bool ok = false;
        const double v = part.value(QLatin1String(key)).toDouble(&ok);
        return ok && v > 0.0 ? v : dflt;
    };
    d.mechanical = flag("wiredShowMech", false);
    d.flipSide = part.value(QStringLiteral("wiredSide")) == QStringLiteral("toward");
    d.straightMm = num("wiredStraight", d.straightMm);
    d.pitchMm = num("wiredPitch", d.pitchMm);
    d.lengthMm = num("wiredLength", d.lengthMm);
    d.schematic = flag("wiredShowSchem", false);
    d.labelColor = flag("wiredLabelColor", true);
    d.labelSize = flag("wiredLabelSize", true);
    d.schemLeadMm = num("wiredSchemLead", d.schemLeadMm);
    return d;
}

void setPartWiredDisplay(LibraryPart& part, const WiredDisplay& d) {
    auto flag = [&](const char* key, bool on) { part.params.insert(QLatin1String(key), on ? "1" : "0"); };
    flag("wiredShowMech", d.mechanical);
    part.params.insert(QStringLiteral("wiredSide"), d.flipSide ? QStringLiteral("toward") : QStringLiteral("away"));
    part.params.insert(QStringLiteral("wiredStraight"), QString::number(d.straightMm));
    part.params.insert(QStringLiteral("wiredPitch"), QString::number(d.pitchMm));
    part.params.insert(QStringLiteral("wiredLength"), QString::number(d.lengthMm));
    flag("wiredShowSchem", d.schematic);
    flag("wiredLabelColor", d.labelColor);
    flag("wiredLabelSize", d.labelSize);
    part.params.insert(QStringLiteral("wiredSchemLead"), QString::number(d.schemLeadMm));
}

QString leadColorHex(const QString& name) {
    static const QHash<QString, QString> kHex = {
        {QStringLiteral("white"), QStringLiteral("#f0f0f0")},  {QStringLiteral("brown"), QStringLiteral("#9a5b2e")},
        {QStringLiteral("green"), QStringLiteral("#2ea043")},  {QStringLiteral("yellow"), QStringLiteral("#e3c400")},
        {QStringLiteral("grey"), QStringLiteral("#9a9a9a")},   {QStringLiteral("gray"), QStringLiteral("#9a9a9a")},
        {QStringLiteral("pink"), QStringLiteral("#ff8fc0")},   {QStringLiteral("blue"), QStringLiteral("#2f6fd6")},
        {QStringLiteral("red"), QStringLiteral("#d63031")},    {QStringLiteral("violet"), QStringLiteral("#8e44ad")},
        {QStringLiteral("purple"), QStringLiteral("#8e44ad")}, {QStringLiteral("orange"), QStringLiteral("#f0892a")},
        {QStringLiteral("turquoise"), QStringLiteral("#20b2aa")}};
    // "White/Blue" is a white wire with a blue stripe: drawn in its body colour.
    const QString first = name.section(QLatin1Char('/'), 0, 0).trimmed().toLower();
    return kHex.value(first);
}

PartSizeMode partSizeMode(const LibraryPart& part) {
    const QString m = part.value(QStringLiteral("sizeMode"));
    if (m == QLatin1String("known")) return PartSizeMode::Known;
    if (m == QLatin1String("params")) return PartSizeMode::FromParams;
    if (m == QLatin1String("auto")) return PartSizeMode::Auto;
    return PartSizeMode::AsDrawn;
}

void setPartSizeMode(LibraryPart& part, PartSizeMode mode) {
    if (mode == PartSizeMode::AsDrawn) {
        part.params.remove(QStringLiteral("sizeMode"));
        return;
    }
    part.params.insert(QStringLiteral("sizeMode"), mode == PartSizeMode::Known        ? QStringLiteral("known")
                                                   : mode == PartSizeMode::FromParams ? QStringLiteral("params")
                                                                                      : QStringLiteral("auto"));
}

double partSizeKnownMm(const LibraryPart& part) {
    bool ok = false;
    const double v = part.value(QStringLiteral("sizeKnownMm")).toDouble(&ok);
    return ok && v > 0.0 ? v : 0.0;
}

void setPartSizeKnownMm(LibraryPart& part, double mm) {
    if (mm > 0.0) part.params.insert(QStringLiteral("sizeKnownMm"), QString::number(mm));
    else part.params.remove(QStringLiteral("sizeKnownMm"));
}

double partDrawnEntryMm(const LibraryPart& part) {
    if (part.outputs.empty()) return 0.0;
    return distance(part.outputs.front().a, part.outputs.front().b);
}

double partEntryFromParamsMm(const LibraryPart& part) {
    // The most the entry takes, from whichever of the part's figures says it - a range's top, else its bottom,
    // else a single figure.
    auto number = [&part](const QString& key) {
        bool ok = false;
        const double v = part.value(key).trimmed().toDouble(&ok);
        return ok && v > 0.0 ? v : 0.0;
    };
    for (const char* k : {"cableEntry", "entry", "main", "connEntry", "insertion", "hole", "diameter"}) {
        const QString key = QLatin1String(k);
        for (const QString& v : {key + QStringLiteral("Max"), key + QStringLiteral("Min"), key})
            if (const double mm = number(v); mm > 0.0) return mm;
    }
    return 0.0;
}

double partSizeFactor(const LibraryPart& part) {
    const double drawn = partDrawnEntryMm(part);
    if (drawn <= 1e-9) return 1.0;
    double want = 0.0;
    switch (partSizeMode(part)) {
        case PartSizeMode::Known: want = partSizeKnownMm(part); break;
        case PartSizeMode::FromParams: want = partEntryFromParamsMm(part); break;
        default: return 1.0;
    }
    return want > 0.0 ? std::clamp(want / drawn, 0.01, 100.0) : 1.0;
}

bool partIsNominal(const LibraryPart& part) { return partSizeMode(part) != PartSizeMode::AsDrawn; }

QJsonArray partMechanicalSized(const LibraryPart& part) {
    const double k = partSizeFactor(part);
    if (std::abs(k - 1.0) < 1e-12 || part.mechanical.isEmpty()) return part.mechanical;
    std::vector<Entity> ents = partDrawingEntities(part.mechanical);
    for (Entity& e : ents) scaleEntity(e, Vec2{0.0, 0.0}, k);
    return partDrawingFromEntities(ents);
}

std::vector<MechOutputFace> partOutputsSized(const LibraryPart& part) {
    const double k = partSizeFactor(part);
    std::vector<MechOutputFace> out = part.outputs;
    for (MechOutputFace& f : out) {
        f.a = f.a * k;
        f.b = f.b * k;
    }
    return out;
}

LibraryPart partSized(const LibraryPart& part) {
    const double k = partSizeFactor(part);
    if (std::abs(k - 1.0) < 1e-12) return part;
    LibraryPart p = part;
    p.mechanical = partMechanicalSized(part);
    p.outputs = partOutputsSized(part);
    p.connectPoint = p.connectPoint * k;
    p.nearPoint = p.nearPoint * k;
    p.farPoint = p.farPoint * k;
    return p;
}

QString wireSwatchHex(const QString& name) {
    const QString first = name.section(QLatin1Char('/'), 0, 0).trimmed().toLower();
    if (first == QStringLiteral("black")) return QStringLiteral("#202020");
    const QString hex = leadColorHex(first);
    return hex.isEmpty() ? QStringLiteral("#808080") : hex;
}

QJsonArray withWiredWires(const LibraryPart& part, bool mechanical, const QJsonArray& drawing) {
    if (part.kind != PartKind::Connector || !partIsWired(part) || drawing.isEmpty()) return drawing;
    const WiredDisplay d = partWiredDisplay(part);
    if (mechanical ? !d.mechanical : !d.schematic) return drawing;
    const std::vector<PartPinWire> wires = partPinWires(part);
    const int n = static_cast<int>(wires.size());
    if (n <= 0) return drawing;
    QJsonArray out = drawing;
    const std::vector<Entity> ents = partDrawingEntities(drawing);
    if (mechanical) {
        // WHICH WAY THE HARNESS GOES FROM THE PART - the connector's own direction, as its marks say it: the
        // way it runs from its connection point, or from its far point to its near point. Only when it has
        // neither is it guessed, as out of the face away from the middle of the drawing.
        Bounds body;
        for (const Entity& e : ents) body.expand(e.bounds());
        if (!body.valid) return drawing;
        Vec2 dir{0.0, 0.0};
        if (part.hasConnect) {
            // The arrow runs along the part, marked either way round (into the body, as a new part starts, or out
            // of it): the harness leaves the connection point on the side AWAY from the body.
            dir = Vec2{std::cos(part.connectAngleRad), std::sin(part.connectAngleRad)};
            if (dot(part.connectPoint - body.center(), dir) < 0.0) dir = dir * -1.0;
        } else if (part.hasNearPoint && part.hasFarPoint) {
            dir = (part.nearPoint - part.farPoint).normalized();
        }
        const bool haveDir = dir.lengthSq() > 1e-12;
        // THE ENTRY FACE the fan fits in: the part's first output face; else across the end of the part the
        // harness leaves by, as wide as the drawing is there; else its right-hand end.
        Vec2 a, b;
        if (!part.outputs.empty()) {
            a = part.outputs.front().a;
            b = part.outputs.front().b;
        } else if (haveDir) {
            const Vec2 across = dir.perp();
            double endAt = -1e300, lo = 1e300, hi = -1e300, from = 1e300;
            std::vector<Vec2> pts;
            for (const Entity& e : ents) {
                const Bounds eb = e.bounds();
                if (!eb.valid) continue;
                for (const Vec2& c : {eb.min, eb.max, Vec2{eb.min.x, eb.max.y}, Vec2{eb.max.x, eb.min.y}}) {
                    pts.push_back(c);
                    endAt = std::max(endAt, dot(c, dir));
                    from = std::min(from, dot(c, dir));
                }
            }
            // What is within the last tenth of the part is its end.
            const double near = endAt - std::max(1e-6, (endAt - from) * 0.1);
            for (const Vec2& c : pts)
                if (dot(c, dir) >= near) {
                    lo = std::min(lo, dot(c, across));
                    hi = std::max(hi, dot(c, across));
                }
            if (hi - lo < 1e-6) {
                lo = -1.0;
                hi = 1.0;
            }
            a = dir * endAt + across * lo;
            b = dir * endAt + across * hi;
        } else {
            a = {body.max.x, body.min.y};
            b = {body.max.x, body.max.y};
        }
        const Vec2 along = (b - a).normalized();
        if (along.lengthSq() < 1e-12) return drawing;
        Vec2 out1 = along.perp();
        const Vec2 faceMid = (a + b) * 0.5;
        // Out of the face, the way the harness goes - away from the body, never back through it.
        if (haveDir ? dot(out1, dir) < 0.0 : dot(out1, faceMid - body.center()) < 0.0) out1 = out1 * -1.0;
        if (d.flipSide) out1 = out1 * -1.0;
        const double straight = std::max(0.0, d.straightMm);
        const double spreadAt = straight * 2.0;
        const double length = std::max(d.lengthMm, spreadAt + 1.0);
        for (int i = 0; i < n; ++i) {
            // Out of the face in pin order, pin 1 at the face's first end; then spread to the pitch.
            const Vec2 start = a + (b - a) * ((i + 0.5) / n);
            const Vec2 spread = faceMid + along * ((i - (n - 1) / 2.0) * d.pitchMm);
            Entity w;
            w.type = EntityType::Polyline;
            w.layer = QStringLiteral("0");
            w.color = leadColorHex(wires[static_cast<size_t>(i)].color);
            w.points = {start, start + out1 * straight, spread + out1 * spreadAt, spread + out1 * length};
            w.meta.insert(QStringLiteral("wiredLead"), wires[static_cast<size_t>(i)].pin);
            out.append(w.toJson());
        }
        return out;
    }
    // THE SCHEMATIC: a lead out of every pin of the connector symbol, and above it what the wire is.
    for (const Entity& e : ents) {
        if (e.type != EntityType::Connector) continue;
        const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
        const ConnectorGeometry g = connectorGeometry(e.a, spec);
        const double pitch = spec.pitch > 0.0 ? spec.pitch : 2.54;
        const double lead = d.schemLeadMm > 0.0 ? d.schemLeadMm : std::max(20.0, 10.0 * pitch);
        const double textH = std::max(1.0, pitch * 0.45);
        for (size_t k = 0; k < g.pinEnd.size() && k < g.pinTip.size(); ++k) {
            const int pin = static_cast<int>(k) + 1;
            if (pin > n) break;
            const PartPinWire& wire = wires[static_cast<size_t>(pin - 1)];
            Vec2 dir = (g.pinEnd[k] - g.pinTip[k]).normalized();
            if (dir.lengthSq() < 1e-12) dir = {1.0, 0.0};
            const Vec2 from = g.pinEnd[k], to = from + dir * lead;
            Entity l;
            l.type = EntityType::Line;
            l.layer = QStringLiteral("0");
            l.color = leadColorHex(wire.color);
            l.a = from;
            l.b = to;
            l.meta.insert(QStringLiteral("wiredLead"), pin);
            out.append(l.toJson());
            QStringList said;
            if (d.labelColor && !wire.color.trimmed().isEmpty()) said << wire.color.trimmed();
            if (d.labelSize && !wire.size.trimmed().isEmpty())
                said << wire.size.trimmed() + (wire.unit == QStringLiteral("mm2") ? QStringLiteral(" mm²")
                                                                                   : QStringLiteral(" AWG"));
            if (said.isEmpty()) continue;
            // Above the lead, in the middle of it, reading left to right whichever way the pin points.
            Vec2 up = dir.perp();
            if (up.y < 0.0 || (std::abs(up.y) < 1e-9 && up.x > 0.0)) up = up * -1.0;
            Entity t;
            t.type = EntityType::Text;
            t.layer = QStringLiteral("0");
            t.a = (from + to) * 0.5 + up * (textH * 0.35);
            t.rotation = std::abs(dir.x) >= std::abs(dir.y) ? 0.0 : M_PI / 2.0;
            t.meta.insert(QStringLiteral("text"), said.join(QStringLiteral("  ")));
            t.meta.insert(QStringLiteral("size"), textH);
            t.meta.insert(QStringLiteral("justify"), QStringLiteral("BC"));
            t.meta.insert(QStringLiteral("wiredLead"), pin);
            out.append(t.toJson());
        }
    }
    return out;
}

std::vector<PartPinType> partPinTypes(const LibraryPart& part) {
    std::vector<PartPinType> out;
    const QString raw = part.value(QStringLiteral("pinTypes"));
    if (raw.isEmpty()) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray()) return out;
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        PartPinType t;
        t.size = o.value(QStringLiteral("size")).toString();
        t.unit = o.value(QStringLiteral("unit")).toString(QStringLiteral("AWG"));
        t.gauge = o.value(QStringLiteral("gauge")).toString();
        t.amps = o.value(QStringLiteral("amps")).toString();
        t.pins = o.value(QStringLiteral("pins")).toString();
        t.gender = o.value(QStringLiteral("gender")).toString();
        out.push_back(t);
    }
    return out;
}

void setPartPinTypes(LibraryPart& part, const std::vector<PartPinType>& types) {
    QJsonArray arr;
    for (const PartPinType& t : types) {
        // A row with nothing in it is not a pin type, it is a row somebody started and left.
        if (t.size.trimmed().isEmpty() && t.gauge.trimmed().isEmpty() && t.amps.trimmed().isEmpty() &&
            t.gender.trimmed().isEmpty())
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("size"), t.size.trimmed());
        o.insert(QStringLiteral("unit"), t.unit.isEmpty() ? QStringLiteral("AWG") : t.unit);
        // "20 - 16", "20-16", "20 to 16" all mean the same range: kept as one form, so the same connector
        // entered by two people reads the same on the drawing.
        QString gauge = t.gauge.trimmed();
        gauge.replace(QRegularExpression(QStringLiteral("\\s*(?:-|to|\\.\\.)\\s*")),
                      QStringLiteral("-"));
        o.insert(QStringLiteral("gauge"), gauge);
        if (!t.amps.trimmed().isEmpty()) o.insert(QStringLiteral("amps"), t.amps.trimmed());
        if (!t.pins.trimmed().isEmpty()) o.insert(QStringLiteral("pins"), t.pins.trimmed());
        if (!t.gender.trimmed().isEmpty()) o.insert(QStringLiteral("gender"), t.gender.trimmed());
        arr.append(o);
    }
    if (arr.isEmpty()) part.params.remove(QStringLiteral("pinTypes"));
    else
        part.params.insert(QStringLiteral("pinTypes"),
                           QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QStringList partPinAmps(const LibraryPart& part) {
    const int ways = part.wayCount();
    if (ways <= 0) return {};
    QStringList out;
    for (int i = 0; i < ways; ++i) out << QString();
    for (const PartPinType& t : partPinTypes(part)) {
        const QString says = t.amps.trimmed();
        if (says.isEmpty()) continue;
        for (int pin : pinsNamedBy(t.pins, ways))
            if (pin >= 1 && pin <= ways) out[pin - 1] = says;
    }
    return out;
}

std::vector<int> pinsNamedBy(const QString& list, int ways) {
    std::vector<int> out;
    const QString t = list.trimmed();
    if (t.isEmpty()) {
        for (int i = 1; i <= ways; ++i) out.push_back(i);
        return out;
    }
    for (const QString& piece : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString one = piece.trimmed();
        const int dash = one.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            bool okA = false, okB = false;
            const int from = one.left(dash).trimmed().toInt(&okA);
            const int to = one.mid(dash + 1).trimmed().toInt(&okB);
            if (okA && okB)
                for (int i = std::min(from, to); i <= std::max(from, to); ++i)
                    if (i >= 1 && (ways <= 0 || i <= ways)) out.push_back(i);
            continue;
        }
        bool ok = false;
        const int one1 = one.toInt(&ok);
        if (ok && one1 >= 1 && (ways <= 0 || one1 <= ways)) out.push_back(one1);
    }
    return out;
}

QStringList partPinGauges(const LibraryPart& part) {
    const int ways = part.wayCount();
    if (ways <= 0) return {};
    QStringList out;
    for (int i = 0; i < ways; ++i) out << QString();
    for (const PartPinType& t : partPinTypes(part)) {
        // What goes on a pin is the conductor it takes, in the unit the maker states it in - "16 AWG",
        // "2.5 mm2". The size of the pin itself is a different figure and is not what a wire is chosen by.
        QString says = t.gauge.trimmed();
        if (says.isEmpty()) continue;
        if (!t.unit.isEmpty()) says += QLatin1Char(' ') + t.unit;
        for (int pin : pinsNamedBy(t.pins, ways))
            if (pin >= 1 && pin <= ways) out[pin - 1] = says;
    }
    return out;
}

bool partKindTakesWires(PartKind kind) {
    switch (kind) {
    case PartKind::Connector:
    case PartKind::ContactPin:
    case PartKind::Ferrule:
    case PartKind::Crimp:
    case PartKind::Splice:
    case PartKind::Backshell:
    case PartKind::HeatshrinkBoot:
    case PartKind::YSplit:
    case PartKind::Terminal:
    case PartKind::Grommet:
    case PartKind::CableGland:
    case PartKind::Overmould:
        return true;
    default:
        return false;
    }
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
            // The connector's own build. WHETHER THE PINS COME WITH IT, and what they are, is asked once -
            // in "Its own pins", where there is a row per size of pin with the conductor it takes and which
            // pins are that size. It was asked here as well, in three fields that could only ever say less:
            // one tick, one unit for the whole connector and one gauge range for the whole connector, on a
            // housing whose whole point may be that it carries two sizes at once. Two places to say one
            // thing is two places to disagree.
            return {num("pins", "Pins"),
                    num("pitch", "Pitch", "mm"),
                    text("shellSize", "Shell size"),
                    choice("gender", "Gender", {QStringLiteral("Male"), QStringLiteral("Female"), QStringLiteral("Mixed")}),
                    // HOW IT IS MOUNTED: on the end of a cable or through a panel, and which half of the pair.
                    choice("mounting", "Mounting",
                           {QStringLiteral("Cable plug"), QStringLiteral("Cable receptacle"),
                            QStringLiteral("Panel receptacle"), QStringLiteral("Panel plug")}),
                    // THE CABLE IT TAKES, across: what a drawing of it drawn to proportion is sized by.
                    range("cableEntry", "Cable entry", "mm"),
                    // HOW ITS CONTACTS TAKE A WIRE: crimped, or soldered - a solid wire is never crimped.
                    choice("termination", "Contact termination", {QStringLiteral("Crimp"), QStringLiteral("Solder")}),
                    choice("material", "Shell material", kMetals + kPolymers),
                    choice("plating", "Contact material", kMetals),
                    text("temp", "Temperature range", "C"),
                    text("spec", "Specification")};
        case PartKind::ContactPin:
            // The current rating belongs on a contact as much as its gauge does: a contact is what carries
            // the current, and it is the contact - not the wire - that a circuit is derated against when the
            // connector is loaded. Written as text rather than a number so that "7.5" and "5 (continuous)"
            // are both sayable, which is how a datasheet says them.
            return {choice("gaugeMode", "Gauge in", {QStringLiteral("AWG"), QStringLiteral("mm2")}),
                    range("gauge", "Gauge range"),
                    text("current", "Current rating", "A"),
                    choice("type", "Type",
                           {QStringLiteral("Pin"), QStringLiteral("Socket"),
                            QStringLiteral("Crimp"), QStringLiteral("Solder cup")}),
                    choice("material", "Material", kMetals),
                    choice("plating", "Plating", kMetals),
                    text("spec", "Specification")};
        case PartKind::Ferrule:
            // THE WIRE IT TAKES, picked from the ferrule sizes there are (or typed): its entry size.
            return {choice("size", "Entry size",
                           {QStringLiteral("0.03 mm2 (32 AWG)"), QStringLiteral("0.05 mm2 (30 AWG)"),
                            QStringLiteral("0.08 mm2 (28 AWG)"), QStringLiteral("0.14 mm2 (26 AWG)"),
                            QStringLiteral("0.25 mm2 (24 AWG)"), QStringLiteral("0.34 mm2 (22 AWG)"),
                            QStringLiteral("0.5 mm2 (20 AWG)"), QStringLiteral("0.75 mm2 (18 AWG)"),
                            QStringLiteral("1.0 mm2 (18 AWG)"), QStringLiteral("1.5 mm2 (16 AWG)"),
                            QStringLiteral("2.5 mm2 (14 AWG)"), QStringLiteral("4 mm2 (12 AWG)"),
                            QStringLiteral("6 mm2 (10 AWG)"), QStringLiteral("10 mm2 (8 AWG)"),
                            QStringLiteral("16 mm2 (6 AWG)"), QStringLiteral("25 mm2 (4 AWG)"),
                            QStringLiteral("35 mm2 (2 AWG)"), QStringLiteral("50 mm2 (1 AWG)")}),
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
                    // ITS SIZE when made: how long a joint it is, and how big across - what it adds to the bundle there.
                    num("length", "Length", "mm"),
                    num("od", "Outer diameter", "mm"),
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
                    num("wall", "Recovered wall thickness", "mm"),
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
                    num("wall", "Recovered wall thickness", "mm"),
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

QStringList partPinGenders(const LibraryPart& part) {
    const int ways = part.wayCount();
    QStringList out;
    for (int i = 0; i < ways; ++i) out << QString();
    for (const PartPinType& t : partPinTypes(part)) {
        const QString g = t.gender.trimmed().toLower();
        if (g != QLatin1String("male") && g != QLatin1String("female")) continue;
        for (const int pin : pinsNamedBy(t.pins, ways))
            if (pin >= 1 && pin <= ways) out[pin - 1] = g;
    }
    return out;
}

bool partPinsMixed(const LibraryPart& part) {
    const QStringList g = partPinGenders(part);
    return g.contains(QStringLiteral("male")) && g.contains(QStringLiteral("female"));
}

QJsonArray connectorSymbolFromData(const LibraryPart& part) {
    if (part.kind != PartKind::Connector) return {};
    const int ways = part.wayCount();
    if (ways <= 0) return {};
    ConnectorSpec spec = defaultConnectorSpec(ways);
    // The part number goes on it; the designator does not - that belongs to the place it is used, and is
    // given when it is placed on a sheet.
    spec.partNumber = part.mfrPn.isEmpty() ? part.internalPn : part.mfrPn;
    spec.designator.clear();
    // A pitch in the part's data is a real dimension: a schematic drawn at the connector's own pitch reads
    // against the datasheet. Anything under a millimetre is not a pitch, it is an empty field.
    const double pitch = part.value(QStringLiteral("pitch")).toDouble();
    if (pitch >= 1.0) spec.pitch = pitch;
    // The pins are numbered 1..n unless the data says letters. Nothing else about a connector's schematic
    // symbol can be had from its data, and nothing else is invented here.
    if (part.value(QStringLiteral("annotation")).startsWith(QStringLiteral("letter"),
                                                           Qt::CaseInsensitive))
        spec.annotation = QStringLiteral("letters");
    // AND WHAT EACH PIN IS, when the part has its pinout from the datasheet: each pin labelled with its
    // signal beside its number, "1 GND", so the symbol is wired from without the datasheet open beside it.
    // A pinout in letters letters the symbol, whatever the annotation said.
    const std::vector<PdfPin> pinout = partPinout(part);
    if (!pinout.empty()) {
        const QString first = pinout.front().pin;
        if (first.size() == 1 && first[0].isLetter()) spec.annotation = QStringLiteral("letters");
        regenPinLabels(spec);
        for (QString& label : spec.pins)
            for (const PdfPin& p : pinout)
                if (p.pin.compare(label, Qt::CaseInsensitive) == 0) {
                    label = pinLabelFor(p);
                    break;
                }
    }
    // A MIXED CONNECTOR - pins and sockets in one housing - says which each is, beside its number: a full circle a
    // pin, an empty one a socket. Wired from without the datasheet, a pin cannot be taken for a socket.
    if (partPinsMixed(part)) {
        const QStringList genders = partPinGenders(part);
        for (int i = 0; i < spec.pins.size() && i < genders.size(); ++i) {
            if (genders[i].isEmpty()) continue;
            const QString mark = genders[i] == QLatin1String("male") ? QString(QChar(0x25CF)) : QString(QChar(0x25CB));
            QString& label = spec.pins[i];
            const int cut = label.indexOf(QLatin1Char(' '));
            label = cut < 0 ? label + QLatin1Char(' ') + mark : label.left(cut) + QLatin1Char(' ') + mark + label.mid(cut);
        }
    }
    // AND WHAT EACH PIN TAKES, when the connector comes with its pins and they are not all the same. A
    // power connector is two heavy ways and six signal ways in one housing - that is the whole reason for
    // buying one - and a symbol that says one gauge for the lot is a symbol somebody wires the wrong way
    // from. A pin no type claims is left blank rather than guessed at.
    const QStringList gauges = partPinGauges(part);
    if (!gauges.isEmpty()) {
        bool anySaid = false;
        for (const QString& g : gauges)
            if (!g.isEmpty()) anySaid = true;
        if (anySaid) spec.pinGauge = gauges;
    }
    // AND WHAT EACH PIN CARRIES, from the same rows. The pin table on a placed connector has a column for
    // it, and a table filled in by hand from a datasheet the library already holds is work done twice - and
    // done differently the second time.
    const QStringList amps = partPinAmps(part);
    if (!amps.isEmpty()) {
        bool anySaid = false;
        for (const QString& a : amps)
            if (!a.isEmpty()) anySaid = true;
        if (anySaid) spec.pinAmp = amps;
    }
    Entity e;
    e.type = EntityType::Connector;
    e.layer = QStringLiteral("0");
    e.a = Vec2{0.0, 0.0};
    writeConnectorSpec(e.meta, spec);
    e.meta.insert(QStringLiteral("libPart"), part.id);
    return partDrawingFromEntities({e});
}

bool canHaveFanout(const LibraryPart& part) {
    if (part.kind == PartKind::Connector) return true;
    for (const QString& key : {QStringLiteral("pins"), QStringLiteral("ways")})
        if (part.value(key).toInt() > 0) return true;
    return false;
}

QJsonArray fanoutFromData(const LibraryPart& part) {
    if (!canHaveFanout(part)) return {};
    const int ways = part.wayCount();
    if (ways <= 0) return {};
    // The pitch is a real dimension when the data has one; under a millimetre it is an empty field, and
    // 2.54 is the pitch of most of what a harness shop has on the shelf.
    double pitch = part.value(QStringLiteral("pitch")).toDouble();
    if (pitch < 1.0) pitch = 2.54;
    const double bodyW = 6.0;                       // the housing, seen from the side
    const double bodyH = ways * pitch + pitch;      // a way's worth of shoulder at each end
    const double open = 8.0;                        // the conductors run open before they gather
    const double gather = 14.0;                     // and then into the bundle
    std::vector<Entity> ents;
    // The housing.
    {
        Entity e;
        e.type = EntityType::Rect;
        e.layer = QStringLiteral("0");
        e.points = {{-bodyW / 2.0, -bodyH / 2.0},
                    {bodyW / 2.0, -bodyH / 2.0},
                    {bodyW / 2.0, bodyH / 2.0},
                    {-bodyW / 2.0, bodyH / 2.0}};
        ents.push_back(std::move(e));
    }
    // One wire per way: out of the back of the housing, straight for the open length, then to the point
    // where the bundle starts. Drawn as one polyline each, which is what a wire IS on a fanout - a run with
    // a knee in it - so it can be picked up and re-routed a wire at a time.
    const double first = -(ways - 1) * pitch / 2.0;
    for (int i = 0; i < ways; ++i) {
        const double y = first + i * pitch;
        Entity w;
        w.type = EntityType::Polyline;
        w.layer = QStringLiteral("0");
        w.points = {{-bodyW / 2.0, y}, {-bodyW / 2.0 - open, y}, {-bodyW / 2.0 - open - gather, 0.0}};
        ents.push_back(std::move(w));
    }
    // The bundle it gathers into, so the drawing says where the harness goes.
    {
        Entity b;
        b.type = EntityType::Line;
        b.layer = QStringLiteral("0");
        b.a = {-bodyW / 2.0 - open - gather, 0.0};
        b.b = {-bodyW / 2.0 - open - gather - 12.0, 0.0};
        ents.push_back(std::move(b));
    }
    for (Entity& e : ents) e.meta.insert(QStringLiteral("libPart"), part.id);
    return partDrawingFromEntities(ents);
}

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

QStringList LibraryPart::contactIds() const {
    // Kept as one parameter, semicolon separated: a parameter map is what a part is written with, and a
    // list of ids in it travels through the file, the copy and the merge without any of them knowing.
    const QString raw = params.value(QStringLiteral("contacts"));
    QStringList out;
    for (const QString& piece : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString one = piece.trimmed();
        if (!one.isEmpty() && !out.contains(one)) out << one;
    }
    return out;
}

void LibraryPart::setContactIds(const QStringList& ids) {
    QStringList clean;
    for (const QString& one : ids) {
        const QString trimmed = one.trimmed();
        if (!trimmed.isEmpty() && !clean.contains(trimmed)) clean << trimmed;
    }
    if (clean.isEmpty()) params.remove(QStringLiteral("contacts"));
    else params.insert(QStringLiteral("contacts"), clean.join(QLatin1Char(';')));
}

QStringList LibraryPart::connectorIds() const {
    const QString raw = params.value(QStringLiteral("connectors"));
    QStringList out;
    for (const QString& piece : raw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString one = piece.trimmed();
        if (!one.isEmpty() && !out.contains(one)) out << one;
    }
    return out;
}

void LibraryPart::setConnectorIds(const QStringList& ids) {
    QStringList clean;
    for (const QString& one : ids) {
        const QString trimmed = one.trimmed();
        if (!trimmed.isEmpty() && !clean.contains(trimmed)) clean << trimmed;
    }
    if (clean.isEmpty()) params.remove(QStringLiteral("connectors"));
    else params.insert(QStringLiteral("connectors"), clean.join(QLatin1Char(';')));
}

void addContactTie(PartsLibrary& lib, const QString& connectorId, const QString& contactId) {
    if (contactId.isEmpty()) return;
    if (LibraryPart* connector = lib.byId(connectorId)) {
        QStringList ids = connector->contactIds();
        if (!ids.contains(contactId)) ids << contactId;
        connector->setContactIds(ids);
    }
    if (LibraryPart* contact = lib.byId(contactId)) {
        QStringList ids = contact->connectorIds();
        if (!ids.contains(connectorId)) ids << connectorId;
        contact->setConnectorIds(ids);
    }
}

void removeContactTie(PartsLibrary& lib, const QString& connectorId, const QString& contactId) {
    if (LibraryPart* connector = lib.byId(connectorId)) {
        QStringList ids = connector->contactIds();
        ids.removeAll(contactId);
        connector->setContactIds(ids);
    }
    if (LibraryPart* contact = lib.byId(contactId)) {
        QStringList ids = contact->connectorIds();
        ids.removeAll(connectorId);
        contact->setConnectorIds(ids);
    }
}

void tieContactToConnector(PartsLibrary& lib, const QString& connectorId, const QString& contactId) {
    LibraryPart* connector = lib.byId(connectorId);
    if (!connector) return;
    const QString wasContact = connector->contactId();
    connector->setContactId(contactId);
    // The contact that was tied before, if it is being replaced, forgets this connector: a tie taken off one
    // end is off at both, or the two ends would disagree about what is tied to what.
    if (!wasContact.isEmpty() && wasContact != contactId)
        if (LibraryPart* was = lib.byId(wasContact)) {
            QStringList ids = was->connectorIds();
            ids.removeAll(connectorId);
            was->setConnectorIds(ids);
        }
    if (contactId.isEmpty()) return;
    if (LibraryPart* contact = lib.byId(contactId)) {
        QStringList ids = contact->connectorIds();
        if (!ids.contains(connectorId)) ids.prepend(connectorId);   // the newest tie leads
        contact->setConnectorIds(ids);
    }
}

QStringList tiedContactsOf(const PartsLibrary& lib, const QString& connectorId) {
    QStringList out;
    if (const LibraryPart* connector = lib.byId(connectorId)) out = connector->contactIds();
    // And any contact that names this connector: a tie made from the contact's end, or one written before
    // both ends were kept.
    for (const LibraryPart& part : lib.parts) {
        if (part.kind != PartKind::ContactPin && part.kind != PartKind::Crimp) continue;
        if (!part.connectorIds().contains(connectorId)) continue;
        if (!out.contains(part.id)) out << part.id;
    }
    return out;
}

QStringList tiedConnectorsOf(const PartsLibrary& lib, const QString& contactId) {
    QStringList out;
    if (const LibraryPart* contact = lib.byId(contactId)) out = contact->connectorIds();
    for (const LibraryPart& part : lib.parts) {
        if (part.kind != PartKind::Connector) continue;
        if (!part.contactIds().contains(contactId)) continue;
        if (!out.contains(part.id)) out << part.id;
    }
    return out;
}

int LibraryPart::wayCount() const {
    // "pins" on a connector, "ways" on the things that are described that way. One if neither is filled
    // in: a connector with no way count still needs a contact, and one is the honest guess.
    for (const QString& key : {QStringLiteral("pins"), QStringLiteral("ways")}) {
        const int n = params.value(key).toInt();
        if (n > 0) return n;
    }
    return 1;
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
    root[QStringLiteral("standardHeatshrink")] = standardHeatshrink;
    root[QStringLiteral("standardCables")] = standardCables;
    root[QStringLiteral("standardSpec55")] = standardSpec55;
    root[QStringLiteral("standardAlphaHookup")] = standardAlphaHookup;
    QJsonArray arr;
    for (const LibraryPart& p : parts) {
        QJsonObject o;
        o[QStringLiteral("id")] = p.id;
        o[QStringLiteral("kind")] = partKindKey(p.kind);
        o[QStringLiteral("description")] = p.description;
        o[QStringLiteral("mfrName")] = p.mfrName;
        o[QStringLiteral("mfrPn")] = p.mfrPn;
        o[QStringLiteral("internalPn")] = p.internalPn;
        o[QStringLiteral("link")] = p.link;
        if (!p.fanout.isEmpty()) o[QStringLiteral("fanout")] = p.fanout;
        if (!p.fanoutOpposite.isEmpty()) o[QStringLiteral("fanoutOpposite")] = p.fanoutOpposite;
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
        if (p.hasConnect) {
            o[QStringLiteral("connect")] = QJsonObject{{"x", p.connectPoint.x}, {"y", p.connectPoint.y}};
            o[QStringLiteral("connectAngle")] = p.connectAngleRad;
        }
        if (p.hasNearPoint)
            o[QStringLiteral("nearPoint")] = QJsonObject{{"x", p.nearPoint.x}, {"y", p.nearPoint.y}};
        if (p.hasFarPoint)
            o[QStringLiteral("farPoint")] = QJsonObject{{"x", p.farPoint.x}, {"y", p.farPoint.y}};
        if (!p.outputs.empty()) {
            // In the ORDER THEY WERE MARKED, which is the order the generator hands the branches out in -
            // so an array and not an object, and never sorted.
            QJsonArray faces;
            for (const MechOutputFace& f : p.outputs)
                faces.append(QJsonObject{{"x1", f.a.x}, {"y1", f.a.y}, {"x2", f.b.x}, {"y2", f.b.y}});
            o[QStringLiteral("outputs")] = faces;
        }
        arr.append(o);
    }
    root[QStringLiteral("parts")] = arr;
    return root;
}

void PartsLibrary::fromJson(const QJsonObject& root) {
    parts.clear();
    standardHeatshrink = root.value(QStringLiteral("standardHeatshrink")).toBool(false);
    standardCables = root.value(QStringLiteral("standardCables")).toBool(false);
    standardSpec55 = root.value(QStringLiteral("standardSpec55")).toBool(false);
    standardAlphaHookup = root.value(QStringLiteral("standardAlphaHookup")).toBool(false);
    for (const auto& v : root.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject o = v.toObject();
        LibraryPart p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.kind = partKindFromKey(o.value(QStringLiteral("kind")).toString());
        p.description = o.value(QStringLiteral("description")).toString();
        p.mfrName = o.value(QStringLiteral("mfrName")).toString();
        p.mfrPn = o.value(QStringLiteral("mfrPn")).toString();
        p.internalPn = o.value(QStringLiteral("internalPn")).toString();
        p.link = o.value(QStringLiteral("link")).toString();
        p.fanout = o.value(QStringLiteral("fanout")).toArray();
        p.fanoutOpposite = o.value(QStringLiteral("fanoutOpposite")).toArray();
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
        const QJsonObject connect = o.value(QStringLiteral("connect")).toObject();
        p.hasConnect = !connect.isEmpty();
        if (p.hasConnect)
            p.connectPoint = {connect.value(QStringLiteral("x")).toDouble(),
                              connect.value(QStringLiteral("y")).toDouble()};
        p.connectAngleRad = o.value(QStringLiteral("connectAngle")).toDouble();
        const QJsonObject near = o.value(QStringLiteral("nearPoint")).toObject();
        p.hasNearPoint = !near.isEmpty();
        if (p.hasNearPoint)
            p.nearPoint = {near.value(QStringLiteral("x")).toDouble(),
                           near.value(QStringLiteral("y")).toDouble()};
        const QJsonObject far = o.value(QStringLiteral("farPoint")).toObject();
        p.hasFarPoint = !far.isEmpty();
        if (p.hasFarPoint)
            p.farPoint = {far.value(QStringLiteral("x")).toDouble(),
                          far.value(QStringLiteral("y")).toDouble()};
        for (const auto& fv : o.value(QStringLiteral("outputs")).toArray()) {
            const QJsonObject f = fv.toObject();
            p.outputs.push_back({{f.value(QStringLiteral("x1")).toDouble(),
                                  f.value(QStringLiteral("y1")).toDouble()},
                                 {f.value(QStringLiteral("x2")).toDouble(),
                                  f.value(QStringLiteral("y2")).toDouble()}});
        }
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
    LibraryPart& fresh = add(copy.kind);
    const QString newId = fresh.id;
    copy.id = newId;
    copy.description = copy.description.isEmpty()
                           ? QStringLiteral("(copy)")
                           : copy.description + QStringLiteral(" (copy)");
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
                     {"gender", ""}, {"material", ""}, {"plating", ""}};
            break;
        case PartKind::ContactPin:
            lead = QStringLiteral("Contact");
            order = {{"type", ""}, {"gauge", "AWG", true}, {"current", "A"}, {"material", ""},
                     {"plating", ""}};
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
    // AND WHAT ITS OWN PINS ARE, when it comes with them. A connector bought with its pins in it is not
    // described by its way count alone - what those pins take is half of what somebody orders it for, and on
    // a housing with two sizes in it that is the whole story: "8 way, 2 x 20 AWG, 6 x 22 AWG" is a line a
    // buyer can act on. Written from the rows in "Its own pins", so it says exactly what was entered there.
    if (part.kind == PartKind::Connector && partIncludesPins(part)) {
        const int ways = part.wayCount();
        QStringList saidPins;
        for (const PartPinType& t : partPinTypes(part)) {
            const QString takes = t.gauge.trimmed();
            if (takes.isEmpty()) continue;
            const int howMany = static_cast<int>(pinsNamedBy(t.pins, ways).size());
            QString one = howMany > 0 && ways > 0 ? QStringLiteral("%1 x %2").arg(howMany).arg(takes)
                                                  : takes;
            if (!t.unit.isEmpty()) one += QLatin1Char(' ') + t.unit;
            if (!t.amps.trimmed().isEmpty()) one += QStringLiteral(" (%1 A)").arg(t.amps.trimmed());
            saidPins << one;
        }
        if (saidPins.isEmpty()) bits << QStringLiteral("pins fitted");
        else bits << QStringLiteral("pins fitted: %1").arg(saidPins.join(QStringLiteral(" + ")));
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
            // THE HOUSE LIBRARY SHIPS WITH THE APPLICATION, inside the executable. A shelf built up over
            // months - the parts, their numbers, their captured drawings - only ever existed in the machine's
            // own app-data folder, so a fresh install started from the bare seed and every drawing made on it
            // was made without the house parts until somebody entered them all again.
            PartsLibrary shipped;
            if (shipped.load(QStringLiteral(":/defaults/edrawlib.plb")) && !shipped.parts.empty()) {
                lib = shipped;
                lib.save(p);
            }
        }
        if (p == def && lib.parts.empty()) {
            // Nothing on the shelf and nothing shipped: fill it with the parts a harness actually starts
            // from, and keep the file so it can be edited afterwards. This also repairs a library left empty
            // by an earlier version.
            lib = seedLibrary();
            lib.save(p);
        }
        // The heat-shrink shelf, in a library that was made before the shelf was: DR-25, RNF-100, RT-375,
        // ATUM and TMS-SCE, in every size. Done ONCE and recorded in the file, so a size somebody
        // deliberately deleted does not come back the next time the library is opened - and so a library
        // that has had them since it was seeded is left alone.
        // The same for the coaxial shelf, and for the same reason: a library made before it existed
        // has no cable in it at all, so the bundle tool had nothing to offer when asked for one.
        int stocked = ensureStandardHeatshrink(lib);
        if (p == def) stocked += ensureStandardCables(lib);
        // And the Spec 55 shelf, for the same reason again: a library made before it existed has no
        // 55A0111 in it, and that is the wire half these harnesses are built from.
        if (p == def) stocked += ensureStandardSpec55(lib);
        // And the Alpha hook-up shelf, which is the other half: 2626 where it is hot, 3079 where
        // the current is.
        if (p == def) stocked += ensureStandardAlphaHookup(lib);
        if (p == def && stocked > 0) lib.save(p);
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

int ensureStandardHeatshrink(PartsLibrary& lib) {
    if (lib.standardHeatshrink) return 0;
    const int added = addStandardHeatshrink(lib);
    lib.standardHeatshrink = true;   // done, and recorded in the file: a deleted size stays deleted
    return added;
}


int ensureStandardCables(PartsLibrary& lib) {
    if (lib.standardCables) return 0;
    const int added = addStandardCables(lib);
    lib.standardCables = true;   // done, and recorded in the file: a deleted type stays deleted
    return added;
}

int addStandardCables(PartsLibrary& lib) {
    // THE COAXIAL SHELF, BY RG DESIGNATION.
    //
    // Cables are shipped by RG type and NOT by manufacturer, because the RG designation is the one
    // place a cable's outside diameter is genuinely standard: MIL-DTL-17 fixes it, every maker builds
    // to it, and RG-58 is RG-58 whoever supplies it. A screened multicore is the opposite - its
    // diameter is that maker's construction and nobody else's - so those are left for a shop to add
    // from its own data sheets rather than guessed at here and quietly believed.
    //
    // The diameter is why this shelf exists at all: the bundle-diameter tool adds up conductors by
    // their outside diameter, and until there was a cable in the library there was nothing to pick.
    struct Coax {
        const char* pn;
        const char* what;
        double odMm;         // over the jacket, from the RG designation
        double impedance;
        const char* jacket;
        const char* temp;
        double bendMm;       // the usual ten times the outside diameter, rounded
    };
    // Jacket diameters as MIL-DTL-17 publishes them, converted from the inch figures they are
    // specified in: 0.100 in, 0.195 in, 0.242 in, 0.390 in and so on.
    const Coax coaxes[] = {
        {"RG-316/U", "Coaxial cable RG-316/U, 50 ohm, PTFE, silver-plated", 2.49, 50, "PTFE",
         "-55 to +200", 25},
        {"RG-174/U", "Coaxial cable RG-174/U, 50 ohm, PVC, miniature", 2.55, 50, "PVC",
         "-40 to +80", 25},
        {"RG-179/U", "Coaxial cable RG-179/U, 75 ohm, PTFE, silver-plated", 2.54, 75, "PTFE",
         "-55 to +200", 25},
        {"RG-58C/U", "Coaxial cable RG-58C/U, 50 ohm, PVC, tinned braid", 4.95, 50, "PVC",
         "-40 to +80", 50},
        {"RG-142B/U", "Coaxial cable RG-142B/U, 50 ohm, PTFE, double braid", 4.95, 50, "PTFE",
         "-55 to +200", 50},
        {"RG-400/U", "Coaxial cable RG-400/U, 50 ohm, PTFE, double braid, flexible", 4.95, 50,
         "PTFE", "-55 to +200", 50},
        {"RG-59B/U", "Coaxial cable RG-59B/U, 75 ohm, PVC, video", 6.15, 75, "PVC", "-40 to +80", 62},
        {"RG-393/U", "Coaxial cable RG-393/U, 50 ohm, PTFE, low loss", 9.91, 50, "PTFE",
         "-55 to +200", 100},
    };
    const QString nominal =
        QStringLiteral("Diameter is the NOMINAL figure for the RG designation (MIL-DTL-17); a given "
                       "maker's build can differ by a tenth of a millimetre or so. Check the current "
                       "datasheet before committing a build to a tolerance.");
    int added = 0;
    for (const Coax& c : coaxes) {
        const QString pn = QString::fromLatin1(c.pn);
        if (hasPn(lib, pn)) continue;
        LibraryPart& part = lib.add(PartKind::Cable);
        part.description = QString::fromLatin1(c.what);
        part.mfrName = QStringLiteral("MIL-DTL-17 (any qualified source)");
        part.mfrPn = pn;
        part.unit = QStringLiteral("m");
        part.params.insert(QStringLiteral("od"), QString::number(c.odMm, 'f', 2));
        part.params.insert(QStringLiteral("jacket"), QString::fromLatin1(c.jacket));
        part.params.insert(QStringLiteral("jacketColour"), QStringLiteral("Black"));
        part.params.insert(QStringLiteral("material"), QStringLiteral("Copper"));
        part.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
        // A coax is one element: the inner, its dielectric and the screen round it.
        part.params.insert(QStringLiteral("structure"), QStringLiteral("coax"));
        part.params.insert(QStringLiteral("cores"), QStringLiteral("1"));
        part.params.insert(QStringLiteral("screened"), QStringLiteral("1"));
        part.params.insert(QStringLiteral("bendRadius"), QString::number(c.bendMm, 'f', 0));
        part.params.insert(QStringLiteral("temp"), QString::fromLatin1(c.temp));
        part.params.insert(QStringLiteral("spec"), QStringLiteral("MIL-DTL-17"));
        part.params.insert(QStringLiteral("notes"),
                           QStringLiteral("%1 ohm coaxial cable, %2 mm over the jacket. %3")
                               .arg(c.impedance, 0, 'f', 0)
                               .arg(c.odMm, 0, 'f', 2)
                               .arg(nominal));
        ++added;
    }
    return added;
}

// ---- Raychem Spec 55: the single wire, the pair, and the screened pair ---------------------------
//
// Every figure below is read off TE's own specification control drawings - 55A0111 rev R, 55A0121
// rev E, 55A1121 rev B1 - and none of it is worked out from anything else. The drawings give inches;
// a harness is drawn in millimetres, so the inch figure is what is written down here and the
// conversion is done once, in code, rather than typed out twice and disagreeing with itself.
//
// The three go together and are stocked together, because they are the same wire three ways: the
// pair is two 55A0111s laid up, and the screened pair is that pair with a braid and a jacket over
// it. A shop that has one on the shelf wants the others beside it.
int addStandardSpec55(PartsLibrary& lib) {
    // One AWG size, across all three drawings.
    struct Spec55Size {
        int awg;
        const char* stranding;   // 55A0111 Table I, "number x AWG"
        double wireOdIn;         // 55A0111 finished wire diameter, nominal
        double dcrPerKft;        // 55A0111 maximum conductor resistance at 20 C, ohm/1000 ft
        double pairOdIn;         // 55A0121 outside diameter, nominal
        double pairMaxIn;        // 55A0121 outside diameter, maximum
        double screenedOdIn;     // 55A1121 outside diameter, nominal
        double screenedMaxIn;    // 55A1121 outside diameter, maximum
        int shieldAwg;           // 55A1121 shield size
    };
    // 30 through 8 AWG - the whole range all three drawings are made in.
    const Spec55Size sizes[] = {
        {30, "7 x 38",   0.024, 108.4,  0.048, 0.052, 0.081, 0.086, 38},
        {28, "7 x 36",   0.027,  68.6,  0.054, 0.058, 0.087, 0.092, 38},
        {26, "19 x 38",  0.032,  41.3,  0.064, 0.068, 0.097, 0.102, 38},
        {24, "19 x 36",  0.037,  26.2,  0.074, 0.078, 0.107, 0.112, 38},
        {22, "19 x 34",  0.043,  16.2,  0.086, 0.090, 0.119, 0.125, 38},
        {20, "19 x 32",  0.050,   9.88, 0.100, 0.104, 0.135, 0.141, 38},
        {18, "19 x 30",  0.060,   6.23, 0.120, 0.124, 0.155, 0.162, 38},
        {16, "19 x 29",  0.068,   4.81, 0.136, 0.140, 0.171, 0.178, 38},
        {14, "19 x 27",  0.085,   3.06, 0.170, 0.176, 0.205, 0.213, 38},
        {12, "37 x 28",  0.103,   2.02, 0.206, 0.212, 0.243, 0.251, 38},
        {10, "37 x 26",  0.128,   1.26, 0.256, 0.268, 0.291, 0.306, 38},
        { 8, "133 x 29", 0.188,   0.701, 0.376, 0.392, 0.417, 0.438, 36},
    };
    // MIL-STD-681 colour designators, which is what the "*" on the drawings stands for.
    struct Spec55Colour {
        const char* code;
        const char* name;
    };
    // The single wire is stocked in the three colours a harness is mostly made of.
    const Spec55Colour singles[] = {{"0", "Black"}, {"2", "Red"}, {"9", "White"}};

    // The drawings give ohm per 1000 ft; a harness is measured in metres, and 1000 ft is 304.8 m.
    const double kKftToM = 304.8;
    auto mm = [](double in) { return QString::number(in * 25.4, 'f', 2); };
    auto in3 = [](double in) { return QString::number(in, 'f', 3); };
    // Ten times the outside diameter, the rule the rest of the shelf uses.
    auto bend = [](double in) { return QString::number(in * 25.4 * 10.0, 'f', 1); };
    const QString nominal =
        QStringLiteral("Diameters are the NOMINAL figures from TE's specification control drawing, "
                       "converted from the inches it is written in. The drawing puts a tolerance on "
                       "the wire and a maximum on the cables; check it before committing a build to "
                       "a tolerance.");
    int added = 0;

    // ---- 55A0111: the wire itself ----
    for (const Spec55Size& s : sizes) {
        for (const Spec55Colour& c : singles) {
            const QString pn =
                QStringLiteral("55A0111-%1-%2").arg(s.awg).arg(QString::fromLatin1(c.code));
            if (hasPn(lib, pn)) continue;
            LibraryPart& p = lib.add(PartKind::Wire);
            p.description = QStringLiteral("Spec 55 wire, %1 AWG, crosslinked ETFE, tin-coated "
                                           "copper, lightweight, %2")
                                .arg(s.awg)
                                .arg(QString::fromLatin1(c.name).toLower());
            p.mfrName = QStringLiteral("TE Connectivity (Raychem)");
            p.mfrPn = pn;
            p.unit = QStringLiteral("m");
            p.params.insert(QStringLiteral("gaugeMode"), QStringLiteral("AWG"));
            p.params.insert(QStringLiteral("gauge"), QString::number(s.awg));
            p.params.insert(QStringLiteral("material"), QStringLiteral("Tin-coated copper"));
            p.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
            p.params.insert(QStringLiteral("strands"), QString::fromLatin1(s.stranding));
            p.params.insert(QStringLiteral("insulation"),
                            QStringLiteral("Radiation-crosslinked modified ETFE"));
            p.params.insert(QStringLiteral("voltage"), QStringLiteral("600"));
            p.params.insert(QStringLiteral("od"), mm(s.wireOdIn));
            p.params.insert(QStringLiteral("resistance"),
                            QString::number(s.dcrPerKft / kKftToM, 'f', 4));
            p.params.insert(QStringLiteral("bendRadius"), bend(s.wireOdIn));
            p.params.insert(QStringLiteral("colour"), QString::fromLatin1(c.name));
            p.params.insert(QStringLiteral("temp"), QStringLiteral("-65 to +150"));
            p.params.insert(QStringLiteral("spec"),
                            QStringLiteral("Raychem Spec 55A, SCD 55A0111 rev R"));
            p.params.insert(
                QStringLiteral("notes"),
                QStringLiteral("Stranding %1. Finished diameter %2 in (%3 mm). Conductor resistance "
                               "%4 ohm/1000 ft maximum at 20 C. Colour code %5 = %6 (MIL-STD-681). "
                               "%7")
                    .arg(QString::fromLatin1(s.stranding), in3(s.wireOdIn), mm(s.wireOdIn),
                         QString::number(s.dcrPerKft, 'f', 3), QString::fromLatin1(c.code),
                         QString::fromLatin1(c.name), nominal));
            ++added;
        }
    }

    // ---- 55A0121: two of them laid up, no screen and no jacket ----
    //
    // The two component wires' colours are written with a SLASH between them, which is the drawing's
    // own notation: 6/9 is a blue one and a white one.
    for (const Spec55Size& s : sizes) {
        const QString pn = QStringLiteral("55A0121-%1-6/9").arg(s.awg);
        if (hasPn(lib, pn)) continue;
        LibraryPart& p = lib.add(PartKind::Cable);
        p.description = QStringLiteral("Spec 55 two-conductor cable, unshielded, unjacketed, "
                                       "%1 AWG, blue and white")
                            .arg(s.awg);
        p.mfrName = QStringLiteral("TE Connectivity (Raychem)");
        p.mfrPn = pn;
        p.unit = QStringLiteral("m");
        p.params.insert(QStringLiteral("od"), mm(s.pairOdIn));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Tin-coated copper"));
        p.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
        p.params.insert(QStringLiteral("insulation"),
                        QStringLiteral("Radiation-crosslinked modified ETFE"));
        p.params.insert(QStringLiteral("voltage"), QStringLiteral("600"));
        // One twisted pair, unscreened - written the way the make-up reader takes it, so the builder
        // can expand the cable into its two conductors without anybody filling in a table.
        p.params.insert(QStringLiteral("structure"), QStringLiteral("UTP 1x2x%1AWG").arg(s.awg));
        p.params.insert(QStringLiteral("cores"), QStringLiteral("2"));
        p.params.insert(QStringLiteral("screened"), QStringLiteral("0"));
        p.params.insert(QStringLiteral("bendRadius"), bend(s.pairOdIn));
        p.params.insert(QStringLiteral("temp"), QStringLiteral("-65 to +150"));
        p.params.insert(QStringLiteral("spec"),
                        QStringLiteral("Raychem Spec 55A, SCD 55A0121 rev E"));
        p.params.insert(
            QStringLiteral("notes"),
            QStringLiteral("Component wires 55A0111. Outside diameter %1 in nominal (%2 mm), %3 in "
                           "maximum. Colours 6/9 = blue and white component wires (MIL-STD-681). %4")
                .arg(in3(s.pairOdIn), mm(s.pairOdIn), in3(s.pairMaxIn), nominal));
        ++added;
    }

    // ---- 55A1121: the pair with a braid and a jacket over it ----
    //
    // The jacket colour comes after a DASH, again the drawing's own notation: 6/9-9 is blue and
    // white cores in a white jacket.
    for (const Spec55Size& s : sizes) {
        const QString pn = QStringLiteral("55A1121-%1-6/9-9").arg(s.awg);
        if (hasPn(lib, pn)) continue;
        LibraryPart& p = lib.add(PartKind::Cable);
        p.description = QStringLiteral("Spec 55 two-conductor cable, shielded and jacketed, "
                                       "%1 AWG, blue and white cores, white jacket")
                            .arg(s.awg);
        p.mfrName = QStringLiteral("TE Connectivity (Raychem)");
        p.mfrPn = pn;
        p.unit = QStringLiteral("m");
        p.params.insert(QStringLiteral("od"), mm(s.screenedOdIn));
        p.params.insert(QStringLiteral("material"), QStringLiteral("Tin-coated copper"));
        p.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
        p.params.insert(QStringLiteral("insulation"),
                        QStringLiteral("Radiation-crosslinked modified ETFE"));
        p.params.insert(QStringLiteral("jacket"),
                        QStringLiteral("Radiation-crosslinked modified ETFE"));
        p.params.insert(QStringLiteral("jacketColour"), QStringLiteral("White"));
        p.params.insert(QStringLiteral("voltage"), QStringLiteral("600"));
        // A braid over the whole cable at 85 per cent coverage, and a white jacket over that.
        p.params.insert(QStringLiteral("structure"),
                        QStringLiteral("1x2x%1AWG, braid 85%, white").arg(s.awg));
        p.params.insert(QStringLiteral("cores"), QStringLiteral("2"));
        p.params.insert(QStringLiteral("screened"), QStringLiteral("1"));
        p.params.insert(QStringLiteral("shield"),
                        QStringLiteral("Tin-coated copper braid, %1 AWG, 85% coverage")
                            .arg(s.shieldAwg));
        p.params.insert(QStringLiteral("bendRadius"), bend(s.screenedOdIn));
        p.params.insert(QStringLiteral("temp"), QStringLiteral("-65 to +150"));
        p.params.insert(QStringLiteral("spec"),
                        QStringLiteral("Raychem Spec 55A, SCD 55A1121 rev B1"));
        p.params.insert(
            QStringLiteral("notes"),
            QStringLiteral("Component wires 55A0111, over a %1 AWG tin-coated copper braid at 85 "
                           "per cent coverage, ETFE jacket. Outside diameter %2 in nominal (%3 mm), "
                           "%4 in maximum. Colours 6/9-9 = blue and white component wires, white "
                           "jacket (MIL-STD-681). %5")
                .arg(QString::number(s.shieldAwg), in3(s.screenedOdIn), mm(s.screenedOdIn),
                     in3(s.screenedMaxIn), nominal));
        ++added;
    }
    return added;
}

int ensureStandardSpec55(PartsLibrary& lib) {
    if (lib.standardSpec55) return 0;
    const int added = addStandardSpec55(lib);
    lib.standardSpec55 = true;   // done, and recorded in the file: a size somebody deleted stays gone
    return added;
}

// ---- Alpha Wire hook-up: 2626 and 3079, every colour --------------------------------------------
//
// Two wires and nothing between them, which is the point of stocking both: 2626 is 26 AWG on
// nickel-plated strands under a fluoropolymer wall and goes to 300 C, and 3079 is 14 AWG on tinned
// strands under a PVC wall and carries thirty times the current at a tenth of the temperature. A
// harness wants the first where it runs past something hot and the second where it feeds something.
//
// Every figure is read off Alpha's own customer specification for the part - conductor, wall,
// finished diameter, resistance, temperature range and bend radius - and converted once, in code,
// from the inches it is written in. Nothing here is worked out from anything else.
int addStandardAlphaHookup(PartsLibrary& lib) {
    // A COLOUR AS ALPHA WRITES IT, and the two-letter code that goes in the part number.
    //
    // The code is NOT on the specification - the specification names the colours in words - so it is
    // filled in here only for the ten solid colours whose Alpha code is the standard one every
    // manufacturer of hook-up wire uses. For the light blue and the four striped colours it is left
    // empty on purpose: the part is still stocked, because the specification says the wire is made
    // in it, but the part number carries the colour in the words the specification uses rather than
    // a suffix nobody has checked. A part number that is obviously a description is a question; one
    // that looks like a code and is wrong is a wrong reel.
    struct Colour {
        const char* code;    // Alpha's suffix, or nullptr where it is not known here
        const char* onSpec;  // exactly as the specification names it
        const char* name;    // as a drawing office would write it
    };
    const Colour kWhite{"WH", "WHITE", "White"};
    const Colour kBlack{"BK", "BLACK", "Black"};
    const Colour kRed{"RD", "RED", "Red"};
    const Colour kGreen{"GR", "GREEN", "Green"};
    const Colour kYellow{"YL", "YELLOW", "Yellow"};
    const Colour kBlue{"BL", "BLUE", "Blue"};
    const Colour kBrown{"BR", "BROWN", "Brown"};
    const Colour kOrange{"OR", "ORANGE", "Orange"};
    const Colour kSlate{"SL", "SLATE", "Slate"};
    const Colour kViolet{"VI", "VIOLET", "Violet"};
    // 2626: the eight the specification lists.
    const Colour colours2626[] = {kWhite, kBlack,  kRed,   kGreen,
                                  kYellow, kBlue,  kBrown, kOrange};
    // 3079: fifteen, the last five of them without a code here - see above.
    const Colour colours3079[] = {kWhite,
                                  kBlack,
                                  kRed,
                                  kGreen,
                                  kYellow,
                                  kBlue,
                                  kBrown,
                                  kOrange,
                                  kSlate,
                                  kViolet,
                                  {nullptr, "LT BLUE", "Light blue"},
                                  {nullptr, "GREEN/YELLOW", "Green/yellow"},
                                  {nullptr, "YELLOW/GREEN", "Yellow/green"},
                                  {nullptr, "WHITE/BLUE", "White/blue"},
                                  {nullptr, "WHITE/BLACK", "White/black"}};

    const double kKftToM = 304.8;   // the specifications give ohms per 1000 ft
    auto mm = [](double in) { return QString::number(in * 25.4, 'f', 2); };
    auto in3 = [](double in) { return QString::number(in, 'f', 3); };
    const QString putUp = QStringLiteral("005");
    const QString unchecked = QStringLiteral(
        "ALPHA'S COLOUR SUFFIX FOR THIS COLOUR IS NOT ON THE SPECIFICATION, which names the colours "
        "in words. The part number here carries the colour in those words rather than a code nobody "
        "has checked; put Alpha's own suffix in before ordering against it.");

    // One wire, everything about it that does not change with the colour.
    struct Hookup {
        const char* base;
        int awg;
        const char* stranding;
        double condIn;        // bare conductor diameter
        double wallIn;        // insulation wall, nominal
        double odIn;          // finished diameter, nominal
        double odTolIn;       // and what the specification allows either way
        double dcrPerKft;     // conductor resistance at 20 C, nominal
        const char* material;
        const char* insulation;
        const char* temp;
        const char* spec;
        const char* what;
        const Colour* colours;
        int count;
    };
    const Hookup wires[] = {
        {"2626", 26, "19 x 38", 0.019, 0.007, 0.033, 0.002, 40.0,
         "Nickel-plated copper (10 per cent)", "Fluoropolymer, 0.007 in wall", "-150 to +300",
         "UL AWM style 11905 or 11540, 300 C / 600 V; sunlight, chemical and water resistant; CE "
         "Low Voltage Directive 2014/35/EU",
         "Alpha Wire 2626 hook-up wire, 26 AWG, fluoropolymer over nickel-plated copper, 300 C",
         colours2626, int(std::size(colours2626))},
        {"3079", 14, "41 x 30", 0.074, 0.032, 0.138, 0.004, 2.8, "Tinned copper",
         "PVC, 0.032 in wall", "-40 to +105",
         "UL AWM style 1015 and 1230, MTW, TW, VW-1, 600 V; CSA TEW-105 and AWM I A/B, FT1; IEC EN "
         "60332-1; CE Low Voltage Directive 2014/35/EU",
         "Alpha Wire 3079 hook-up wire, 14 AWG, PVC over tinned copper, 105 C", colours3079,
         int(std::size(colours3079))}};

    int added = 0;
    for (const Hookup& w : wires) {
        for (int i = 0; i < w.count; ++i) {
            const Colour& c = w.colours[i];
            const QString base = QString::fromLatin1(w.base);
            const QString pn =
                c.code ? QStringLiteral("%1 %2%3").arg(base, QString::fromLatin1(c.code), putUp)
                       : QStringLiteral("%1 %2 %3").arg(base, QString::fromLatin1(c.onSpec), putUp);
            if (hasPn(lib, pn)) continue;
            LibraryPart& p = lib.add(PartKind::Wire);
            p.description = QStringLiteral("%1, %2")
                                .arg(QString::fromLatin1(w.what),
                                     QString::fromLatin1(c.name).toLower());
            p.mfrName = QStringLiteral("Alpha Wire");
            p.mfrPn = pn;
            p.unit = QStringLiteral("m");
            p.params.insert(QStringLiteral("gaugeMode"), QStringLiteral("AWG"));
            p.params.insert(QStringLiteral("gauge"), QString::number(w.awg));
            p.params.insert(QStringLiteral("material"), QString::fromLatin1(w.material));
            p.params.insert(QStringLiteral("conductor"), QStringLiteral("Stranded"));
            p.params.insert(QStringLiteral("strands"), QString::fromLatin1(w.stranding));
            p.params.insert(QStringLiteral("insulation"), QString::fromLatin1(w.insulation));
            p.params.insert(QStringLiteral("voltage"), QStringLiteral("600"));
            p.params.insert(QStringLiteral("od"), mm(w.odIn));
            p.params.insert(QStringLiteral("resistance"),
                            QString::number(w.dcrPerKft / kKftToM, 'f', 4));
            // FIVE TIMES THE DIAMETER, which is what Alpha's own specification says for both of
            // these - not the ten times the rest of the shelf assumes for want of being told.
            p.params.insert(QStringLiteral("bendRadius"),
                            QString::number(w.odIn * 25.4 * 5.0, 'f', 1));
            p.params.insert(QStringLiteral("colour"), QString::fromLatin1(c.name));
            p.params.insert(QStringLiteral("temp"), QString::fromLatin1(w.temp));
            p.params.insert(QStringLiteral("spec"), QString::fromLatin1(w.spec));
            QString notes =
                QStringLiteral("Conductor %1 AWG, stranding %2, bare diameter %3 in (%4 mm). "
                               "Insulation %5. Finished diameter %6 in +/- %7 in (%8 mm). Conductor "
                               "resistance %9 ohm/1000 ft at 20 C, nominal. Bend radius 5 x the "
                               "finished diameter. Colour on the specification: %10.")
                    .arg(QString::number(w.awg), QString::fromLatin1(w.stranding), in3(w.condIn),
                         mm(w.condIn), QString::fromLatin1(w.insulation), in3(w.odIn),
                         in3(w.odTolIn), mm(w.odIn), QString::number(w.dcrPerKft, 'f', 1),
                         QString::fromLatin1(c.onSpec));
            if (!c.code) notes += QStringLiteral(" ") + unchecked;
            p.params.insert(QStringLiteral("notes"), notes);
            ++added;
        }
    }
    return added;
}

int ensureStandardAlphaHookup(PartsLibrary& lib) {
    if (lib.standardAlphaHookup) return 0;
    const int added = addStandardAlphaHookup(lib);
    lib.standardAlphaHookup = true;   // once, and recorded: a colour somebody deleted stays gone
    return added;
}

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
    ensureStandardHeatshrink(lib);
    // And the coaxial shelf, so a fresh library has a cable in it as well as wire.
    ensureStandardCables(lib);
    // And the Spec 55 shelf: 55A0111 wire, and the pair and screened pair built from it.
    ensureStandardSpec55(lib);
    // And the Alpha hook-up shelf: 2626 and 3079, in every colour each is made in.
    ensureStandardAlphaHookup(lib);
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

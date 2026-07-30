#include "harness/PartsLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
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
    {PartKind::Splice, "splice", "Splice"},
    {PartKind::Heatshrink, "heatshrink", "Heatshrink"},
    {PartKind::HeatshrinkBoot, "heatshrink-boot", "Heatshrink boot"},
    {PartKind::Backshell, "backshell", "Backshell"},
    {PartKind::YSplit, "y-split", "Y-split heatshrink"},
    {PartKind::ShieldBraid, "shield-braid", "Shield braid"},
    {PartKind::Grommet, "grommet", "Grommet"},
    {PartKind::CableGland, "cable-gland", "Cable gland"},
    {PartKind::Sleeve, "sleeve", "Sleeve / spiral wrap"},
    {PartKind::Conduit, "conduit", "Conduit"},
    {PartKind::CableTie, "cable-tie", "Cable tie"},
    {PartKind::Marker, "marker", "Marker / label"},
    {PartKind::Tape, "tape", "Tape"},
    {PartKind::Terminal, "terminal", "Terminal / stud"},
    {PartKind::Overmould, "overmould", "Overmould"},
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
        {QStringLiteral("mm"), QStringLiteral("millimetre"), QStringLiteral("length"), 1.0},
        {QStringLiteral("cm"), QStringLiteral("centimetre"), QStringLiteral("length"), 10.0},
        {QStringLiteral("m"), QStringLiteral("metre"), QStringLiteral("length"), 1000.0},
        {QStringLiteral("in"), QStringLiteral("inch"), QStringLiteral("length"), 25.4},
        {QStringLiteral("ft"), QStringLiteral("foot"), QStringLiteral("length"), 304.8},
        {QStringLiteral("g"), QStringLiteral("gram"), QStringLiteral("mass"), 1.0},
        {QStringLiteral("kg"), QStringLiteral("kilogram"), QStringLiteral("mass"), 1000.0},
        {QStringLiteral("ml"), QStringLiteral("millilitre"), QStringLiteral("volume"), 1.0},
        {QStringLiteral("l"), QStringLiteral("litre"), QStringLiteral("volume"), 1000.0},
        {QStringLiteral("mm2"), QStringLiteral("square millimetre"), QStringLiteral("area"), 1.0},
        {QStringLiteral("m2"), QStringLiteral("square metre"), QStringLiteral("area"), 1e6},
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
                    num("resistance", "Resistance", "ohm/m"),
                    text("colour", "Colour"),
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
        case PartKind::Other:
            break;
    }
    // "Other": no fixed parameters - what it is goes in the description, and anything technical in
    // the free-text rows every kind has.
    return {text("size", "Size"), text("material", "Material"), text("spec", "Specification"),
            text("notes", "Notes")};
}

QString LibraryPart::title() const {
    if (!mfrPn.isEmpty() && !description.isEmpty())
        return QStringLiteral("%1 — %2").arg(mfrPn, description);
    if (!mfrPn.isEmpty()) return mfrPn;
    if (!description.isEmpty()) return description;
    return QStringLiteral("(unnamed %1)").arg(partKindName(kind).toLower());
}

QString PartsLibrary::defaultPath() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/Whatson/Edraw");
    return dir + QStringLiteral("/parts-library.plb");
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

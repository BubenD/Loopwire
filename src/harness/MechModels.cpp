#include "harness/MechModels.h"

#include <QJsonArray>

#include "harness/PartsLibrary.h"
#include "io/AppPaths.h"
#include "tools/EntityTransforms.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>

#include <algorithm>
#include <array>
#include <cmath>

namespace Loopwire {

namespace {

struct KindName {
    MechModelKind kind;
    const char* key;
    const char* label;
};

// The order the window lists them in: the ends first, then the lengths, then the pieces at a junction.
// IN THE ORDER A HARNESS IS BUILT UP, which is the order a drawing office thinks in: the connectors at each
// end, what is crimped into them, what goes on the back of them, then the cable that runs between, then what
// is shrunk over it, then what is written on it. The list grew instead in the order the kinds were added to
// the program, so a ring terminal came first because it was written first and the connectors - the things
// every harness starts from - were most of the way down.
//
// This order is what the manager lists. Nothing else depends on it: a model is stored under its kind's KEY,
// so the order can be changed whenever the reading of it is wrong without touching a saved file.
//
// Several kinds are ONE KIND with several sub-drawings, held as named models of that kind: a backshell is a
// backshell whether it leaves straight, at 45 or at 90 degrees, and a split is a split whether it makes three
// branches, four, or a T. A kind per angle would put the same part in three places and give the generator
// three things to keep in step.
const std::array<KindName, 18> kKinds = {{
    // The connectors, first - a harness is two ends and what runs between them.
    {MechModelKind::CircularConnector, "circular-connector", "Circular connector (MIL-DTL)"},
    {MechModelKind::DTypeConnector, "d-type-connector", "D-type connector"},
    // What is crimped on.
    {MechModelKind::FerruleInsulated, "ferrule", "Ferrule"},
    {MechModelKind::RingInsulated, "ring", "Ring terminal"},
    // What goes on the back of a connector.
    {MechModelKind::CircularBackshell, "circular-backshell",
     "Circular backshell (straight / 45 / 90)"},
    {MechModelKind::Boot, "boot", "Boot"},
    // What runs between them.
    {MechModelKind::Cable, "cable", "Cable"},
    {MechModelKind::CableBody, "cable-body", "Cable body"},
    {MechModelKind::Wire, "wire", "Wire"},
    {MechModelKind::OpenWires, "open-wires", "Open wires"},
    {MechModelKind::ConnectorToCableWires, "connector-to-cable-wires",
     "Wires between a connector and its cable"},
    {MechModelKind::YSplit, "y-split", "Y-split"},
    {MechModelKind::TSplit, "t-split", "T-split"},
    // What is shrunk over it.
    {MechModelKind::Heatshrink, "heatshrink", "Heatshrink"},
    {MechModelKind::TransitionHeatshrink, "transition-heatshrink", "Transition heatshrink"},
    {MechModelKind::BranchSplitHeatshrink, "branch-split-heatshrink", "Branch split heatshrink"},
    // And what is written on it.
    {MechModelKind::Label, "label", "Label"},
    {MechModelKind::LabelUnderHeatshrink, "label-under-heatshrink",
     "Label under clear heatshrink"},
}};

// A label on a run, as one is drawn: the run through it, a flag standing on the run, and a line inside the
// flag for the text that goes on it. Under clear heatshrink, a second outline over the top, drawn dashed
// because that is how a see-through sleeve is shown.
// HEATSHRINK AS IT SITS ONCE RECOVERED, so the button on the ribbon places the same shape the harness
// renderer draws for a covering along a run - see makeShrunkSleeve. One drawn by hand and one generated
// looking different is the kind of thing that gets noticed on a released sheet and cannot be explained.
//
// It pulls down onto whatever is under it, so it grips at each END and stands off over the MIDDLE where
// the joint it covers is thicker. Both ends equally: one tapered end is a boot, and drawing heatshrink
// that way says it goes on one way round.
//
// The taper lengths become the model HEAD and TAIL, so stretching a placed one to fit a longer run
// lengthens the straight middle and leaves the necks the size they recover to.
std::vector<Entity> builtInHeatshrink() {
    // Nominal, in the drawing's own millimetres: 24 long over a 6 bundle, necked to 4.9 - the same 18%
    // step the renderer uses, so the two agree.
    const double x0 = 0.0, x1 = 24.0, taper = 4.0;
    const double bodyHalf = 3.0, endHalf = 2.45;
    Entity body;
    body.id = QStringLiteral("shrink");
    body.type = EntityType::Polygon;
    body.points = {{x0, -endHalf},         {x0 + taper, -bodyHalf}, {x1 - taper, -bodyHalf},
                   {x1, -endHalf},         {x1, endHalf},           {x1 - taper, bodyHalf},
                   {x0 + taper, bodyHalf}, {x0, endHalf}};
    // What the application scales by, as the label's own outline is: the sleeve IS the whole model.
    body.meta.insert(QStringLiteral("modelScaleRef"), true);
    body.meta.insert(QStringLiteral("shrunkEnds"), true);
    return {body};
}

std::vector<Entity> builtInLabel(bool underShrink) {
    std::vector<Entity> es;
    auto line = [&](const char* id, Vec2 a, Vec2 b, bool dashed = false) {
        Entity e;
        e.id = QString::fromLatin1(id);
        e.type = EntityType::Line;
        e.a = a;
        e.b = b;
        if (dashed) e.meta.insert(QStringLiteral("linetype"), QStringLiteral("HIDDEN"));
        es.push_back(e);
    };
    // The label itself: a rectangle wrapped round the run. It is drawn as a closed rectangle rather than as
    // three sides off a reference wire, because the wire it goes on is already on the drawing it is placed on
    // - a fourth line along the run added nothing and, once the label was scaled by the label rather than by
    // the whole picture, stuck a hundred millimetres out of either end of a forty-millimetre label.
    //
    // MARKED AS WHAT THE APP SCALES BY. For a plain label that is the whole of it and changes nothing; under
    // a clear sleeve it is the difference between fitting the label to the bundle and fitting the sleeve to
    // it and leaving the label short.
    const double x0 = 0.0, x1 = 6.0, yBot = 0.0, yTop = 7.0;
    Entity body;
    body.id = QStringLiteral("label");
    body.type = EntityType::Polyline;
    body.points = {{x0, yBot}, {x1, yBot}, {x1, yTop}, {x0, yTop}, {x0, yBot}};
    body.meta.insert(QStringLiteral("modelScaleRef"), true);
    es.push_back(body);
    // Where the marking goes, so it reads as a label rather than as a blank tab.
    line("text1", {x0 + 1.0, yTop - 2.0}, {x1 - 1.0, yTop - 2.0});
    line("text2", {x0 + 1.0, yTop - 4.0}, {x1 - 1.6, yTop - 4.0});
    if (underShrink) {
        // The clear sleeve over the whole of it, standing off all round. Dashed: it is transparent, and a
        // solid second outline would read as a second label. It is NOT the scale reference and it is not
        // masked - the wires under the sleeve but outside the label are meant to show, which is the one
        // thing that says the sleeve is clear.
        const double m = 1.6;
        line("shrinkL", {x0 - m, yBot - m}, {x0 - m, yTop + m}, true);
        line("shrinkR", {x1 + m, yBot - m}, {x1 + m, yTop + m}, true);
        line("shrinkTop", {x0 - m, yTop + m}, {x1 + m, yTop + m}, true);
        line("shrinkBot", {x0 - m, yBot - m}, {x1 + m, yBot - m}, true);
    }
    return es;
}

MechModelKind canonicalKind(MechModelKind kind) {
    if (kind == MechModelKind::RingUninsulated) return MechModelKind::RingInsulated;
    if (kind == MechModelKind::FerruleUninsulated) return MechModelKind::FerruleInsulated;
    return kind;
}

bool isInsulationKind(MechModelKind kind) {
    return kind == MechModelKind::RingInsulated || kind == MechModelKind::RingUninsulated ||
           kind == MechModelKind::FerruleInsulated || kind == MechModelKind::FerruleUninsulated;
}

} // namespace

MechModel builtInMechModel(MechModelKind kind) {
    MechModel m;
    m.kind = kind;
    if (kind == MechModelKind::Heatshrink) {
        m.drawing = partDrawingFromEntities(builtInHeatshrink());
        m.drawnLengthMm = 24.0;
        m.hasConnect = true;
        m.connectPoint = {12.0, 0.0};      // the middle of it, on the run
        m.connectAngleRad = 0.0;
        // The necks are held and the straight middle stretches - see builtInHeatshrink.
        m.headMm = 4.0;
        m.tailMm = 4.0;
        m.exceedMm = 1.0;
        return m;
    }
    if (kind != MechModelKind::Label && kind != MechModelKind::LabelUnderHeatshrink) return m;
    const std::vector<Entity> es = builtInLabel(kind == MechModelKind::LabelUnderHeatshrink);
    m.drawing = partDrawingFromEntities(es);
    m.drawnLengthMm = 6.0;
    // It goes ON the run, at the middle of the piece of run it covers, running along it.
    m.hasConnect = true;
    m.connectPoint = {3.0, 0.0};
    m.connectAngleRad = 0.0;
    // A label does not stretch at all: it is one size whatever it is wrapped round, and a longer run gets
    // the same label, not a longer one. So the whole of it is held: head and tail meet in the middle.
    m.headMm = 3.0;
    m.tailMm = 3.0;
    // Clear of the first and last wire it crosses, at each end. A number rather than a proportion, because
    // an office that draws 2 mm clear draws 2 mm clear whether the bundle is three wires or thirty.
    m.exceedMm = 2.0;
    return m;
}

std::vector<MechModelKind> allMechModelKinds() {
    std::vector<MechModelKind> out;
    for (const KindName& k : kKinds) out.push_back(k.kind);
    return out;
}

QString mechModelName(MechModelKind kind) {
    kind = canonicalKind(kind);
    for (const KindName& k : kKinds)
        if (k.kind == kind) return QString::fromLatin1(k.label);
    return QStringLiteral("Model");
}

QString mechModelKey(MechModelKind kind) {
    kind = canonicalKind(kind);
    for (const KindName& k : kKinds)
        if (k.kind == kind) return QString::fromLatin1(k.key);
    return QStringLiteral("model");
}

bool mechModelKindFromKey(const QString& key, MechModelKind& out) {
    if (key == QLatin1String("ring-insulated")) {
        out = MechModelKind::RingInsulated;
        return true;
    }
    if (key == QLatin1String("ring-uninsulated")) {
        out = MechModelKind::RingUninsulated;
        return true;
    }
    if (key == QLatin1String("ferrule-insulated")) {
        out = MechModelKind::FerruleInsulated;
        return true;
    }
    if (key == QLatin1String("ferrule-uninsulated")) {
        out = MechModelKind::FerruleUninsulated;
        return true;
    }
    for (const KindName& k : kKinds)
        if (key == QLatin1String(k.key)) {
            out = k.kind;
            return true;
        }
    return false;
}

bool mechModelStretches(MechModelKind kind) {
    switch (kind) {
    case MechModelKind::TransitionHeatshrink:
    case MechModelKind::Cable:
    case MechModelKind::Wire:
    case MechModelKind::OpenWires:
    case MechModelKind::ConnectorToCableWires:
    case MechModelKind::Heatshrink:
    case MechModelKind::Boot:
    case MechModelKind::CableBody:
    // A label and a label under heatshrink are drawn along the run and cover a length of it; the
    // connectors, backshells and split boots are things of their own size, placed.
    case MechModelKind::Label:
    case MechModelKind::LabelUnderHeatshrink:
        return true;
    default:
        // A ring, a ferrule, a Y-split: a thing of its own size, placed and not stretched.
        return false;
    }
}

bool mechModelPerWire(MechModelKind kind) {
    return kind == MechModelKind::OpenWires || kind == MechModelKind::ConnectorToCableWires;
}

bool mechModelHasSchematicSlot(MechModelKind kind) {
    kind = canonicalKind(kind);
    // THE THINGS A SCHEMATIC DRAWS A SYMBOL FOR. A crimp - a ring terminal, a ferrule - and a connector.
    // Everything else on this list is harness fabric: a length of cable, a heatshrink sleeve, a boot, a
    // label. A schematic does not draw those at all, so a second drawing for them would be a window nobody
    // could put anything in.
    //
    // A connector is the clearest case of the two: it is the one part that appears on BOTH drawings and
    // appears differently - a body with a face and a backshell thread in the metal, a numbered stack of pins
    // on the circuit - and until now the manager could only be told the first of them.
    return kind == MechModelKind::RingInsulated || kind == MechModelKind::FerruleInsulated ||
           kind == MechModelKind::CircularConnector || kind == MechModelKind::DTypeConnector;
}

std::vector<QString> defaultSubModelNames(MechModelKind kind) {
    switch (kind) {
    case MechModelKind::Label:
    case MechModelKind::LabelUnderHeatshrink:
        // These two are already a kind each - the one under a sleeve is built differently, not just drawn
        // differently - so between them they ARE the two sub-drawings a label comes in.
        return {};
    case MechModelKind::Boot:
        return {QStringLiteral("Straight"), QStringLiteral("45 degrees"), QStringLiteral("90 degrees")};
    case MechModelKind::RingInsulated:
    case MechModelKind::FerruleInsulated:
        // Insulation is also a property on the model, which is what the generator reads. These are the two
        // DRAWINGS: an insulated crimp is drawn with the sleeve on it, and they are not the same picture.
        return {QStringLiteral("Insulated"), QStringLiteral("Uninsulated")};
    case MechModelKind::CircularConnector:
        // The forms a circular connector actually comes in, which is not a numbered list: a PLUG is the half that
        // goes on the cable, a RECEPTACLE is the half that goes on the box and is held either by a jam nut through
        // a single hole or by a flange bolted round it - and each half has a CAP on a lanyard for when it is
        // unmated. Five different outlines, and a drawing office draws each one once.
        return {QStringLiteral("Plug"), QStringLiteral("Receptacle jam nut"),
                QStringLiteral("Receptacle flange"), QStringLiteral("Plug cap"),
                QStringLiteral("Receptacle cap")};
    case MechModelKind::CircularBackshell:
        return {QStringLiteral("Straight"), QStringLiteral("45 degrees"), QStringLiteral("90 degrees")};
    case MechModelKind::DTypeConnector:
        return {QStringLiteral("Male")};
    case MechModelKind::YSplit:
        return {QStringLiteral("3 branches"), QStringLiteral("4 branches")};
    case MechModelKind::BranchSplitHeatshrink:
        return {QStringLiteral("3 branches"), QStringLiteral("4 branches")};
    case MechModelKind::ConnectorToCableWires:
        // Two run straight, then two to eight spread out. How far they spread is drawn rather than worked out:
        // it is the one thing about this part that is entirely a house style.
        return {QStringLiteral("2 wires straight"), QStringLiteral("2 wires spread"),
                QStringLiteral("3 wires spread"),   QStringLiteral("4 wires spread"),
                QStringLiteral("5 wires spread"),   QStringLiteral("6 wires spread"),
                QStringLiteral("7 wires spread"),   QStringLiteral("8 wires spread")};
    default:
        return {};
    }
}

// ---- one model ----------------------------------------------------------------------------------

QJsonObject MechModel::toJson() const {
    QJsonObject o;
    if (!name.isEmpty()) o.insert(QStringLiteral("name"), name);
    if (isDefault) o.insert(QStringLiteral("default"), true);
    if (hasConnect) {
        o.insert(QStringLiteral("connect"),
                 QJsonObject{{"x", connectPoint.x}, {"y", connectPoint.y}});
        o.insert(QStringLiteral("connectAngle"), connectAngleRad);
    }
    if (gapMm > 0.0) o.insert(QStringLiteral("gap"), gapMm);
    if (exceedMm > 0.0) o.insert(QStringLiteral("exceed"), exceedMm);
    // The faces the wires leave by, in the order they were marked - which is the order the generator hands
    // a split's branches out in, so it is written down rather than sorted.
    if (!outputs.empty()) {
        QJsonArray outs;
        for (const OutputLine& l : outputs)
            outs.append(QJsonArray{l.a.x, l.a.y, l.b.x, l.b.y});
        o.insert(QStringLiteral("outputs"), outs);
    }
    o.insert(QStringLiteral("kind"), mechModelKey(kind));
    if (isInsulationKind(kind)) o.insert(QStringLiteral("insulated"), insulated);
    if (hasNearPoint)
        o.insert(QStringLiteral("nearPoint"), QJsonObject{{"x", nearPoint.x}, {"y", nearPoint.y}});
    if (hasFarPoint)
        o.insert(QStringLiteral("farPoint"), QJsonObject{{"x", farPoint.x}, {"y", farPoint.y}});
    o.insert(QStringLiteral("drawing"), drawing);
    if (!schematicDrawing.isEmpty()) o.insert(QStringLiteral("schematicDrawing"), schematicDrawing);
    if (drawnLengthMm > 0.0) o.insert(QStringLiteral("drawnLength"), drawnLengthMm);
    if (headMm > 0.0) o.insert(QStringLiteral("head"), headMm);
    if (tailMm > 0.0) o.insert(QStringLiteral("tail"), tailMm);
    if (drawnAcrossMm > 0.0) o.insert(QStringLiteral("drawnAcross"), drawnAcrossMm);
    if (drawnWireCount > 0) o.insert(QStringLiteral("drawnWires"), drawnWireCount);
    return o;
}

void MechModel::fromJson(const QJsonObject& o) {
    name = o.value(QStringLiteral("name")).toString();
    isDefault = o.value(QStringLiteral("default")).toBool();
    const QJsonObject connect = o.value(QStringLiteral("connect")).toObject();
    hasConnect = !connect.isEmpty();
    if (hasConnect)
        connectPoint = {connect.value(QStringLiteral("x")).toDouble(),
                        connect.value(QStringLiteral("y")).toDouble()};
    connectAngleRad = o.value(QStringLiteral("connectAngle")).toDouble();
    gapMm = o.value(QStringLiteral("gap")).toDouble();
    exceedMm = o.value(QStringLiteral("exceed")).toDouble();
    outputs.clear();
    for (const auto v : o.value(QStringLiteral("outputs")).toArray()) {
        const QJsonArray l = v.toArray();
        if (l.size() < 4) continue;
        outputs.push_back({{l[0].toDouble(), l[1].toDouble()}, {l[2].toDouble(), l[3].toDouble()}});
    }
    const QString key = o.value(QStringLiteral("kind")).toString();
    MechModelKind k = MechModelKind::Wire;
    if (mechModelKindFromKey(key, k)) kind = canonicalKind(k);
    insulated = o.contains(QStringLiteral("insulated"))
                    ? o.value(QStringLiteral("insulated")).toBool(true)
                    : k != MechModelKind::RingUninsulated &&
                          k != MechModelKind::FerruleUninsulated;
    const QJsonObject near = o.value(QStringLiteral("nearPoint")).toObject();
    const QJsonObject far = o.value(QStringLiteral("farPoint")).toObject();
    hasNearPoint = !near.isEmpty();
    hasFarPoint = !far.isEmpty();
    if (hasNearPoint) nearPoint = {near.value(QStringLiteral("x")).toDouble(),
                                   near.value(QStringLiteral("y")).toDouble()};
    if (hasFarPoint) farPoint = {far.value(QStringLiteral("x")).toDouble(),
                                 far.value(QStringLiteral("y")).toDouble()};
    drawing = o.value(QStringLiteral("drawing")).toArray();
    schematicDrawing = o.value(QStringLiteral("schematicDrawing")).toArray();
    drawnLengthMm = o.value(QStringLiteral("drawnLength")).toDouble(0.0);
    headMm = o.value(QStringLiteral("head")).toDouble(0.0);
    tailMm = o.value(QStringLiteral("tail")).toDouble(0.0);
    drawnAcrossMm = o.value(QStringLiteral("drawnAcross")).toDouble(0.0);
    drawnWireCount = o.value(QStringLiteral("drawnWires")).toInt(0);
}

// ---- the set ------------------------------------------------------------------------------------

const MechModel* MechModelSet::of(MechModelKind kind) const {
    kind = canonicalKind(kind);
    const MechModel* first = nullptr;
    for (const MechModel& m : models_) {
        if (m.kind != kind || m.isEmpty()) continue;
        if (m.isDefault) return &m;
        if (!first) first = &m;
    }
    return first;
}

const MechModel* MechModelSet::of(MechModelKind kind, const QString& name) const {
    kind = canonicalKind(kind);
    for (const MechModel& m : models_)
        if (m.kind == kind && m.name == name) return &m;
    return nullptr;
}

std::vector<const MechModel*> MechModelSet::all(MechModelKind kind) const {
    kind = canonicalKind(kind);
    std::vector<const MechModel*> out;
    for (const MechModel& m : models_)
        if (m.kind == kind) out.push_back(&m);
    return out;
}

void MechModelSet::set(const MechModel& model) {
    MechModel normalized = model;
    if (normalized.kind == MechModelKind::RingUninsulated ||
        normalized.kind == MechModelKind::FerruleUninsulated)
        normalized.insulated = false;
    normalized.kind = canonicalKind(normalized.kind);
    // By NAME: the same name replaces, a new name is another model of that kind. Replacing whatever was
    // there of the kind - which is what this did - made a second model impossible to keep.
    for (MechModel& m : models_)
        if (m.kind == normalized.kind && m.name == normalized.name) {
            m = normalized;
            if (normalized.isDefault) setDefault(normalized.kind, normalized.name);
            return;
        }
    models_.push_back(normalized);
    // The first of a kind is its default, or a kind could have models and no default at all.
    bool anyDefault = false;
    for (const MechModel& m : models_)
        if (m.kind == normalized.kind && m.isDefault) anyDefault = true;
    if (!anyDefault) setDefault(normalized.kind, normalized.name);
    else if (normalized.isDefault) setDefault(normalized.kind, normalized.name);
}

void MechModelSet::remove(MechModelKind kind, const QString& name) {
    kind = canonicalKind(kind);
    const bool wasDefault = [&] {
        const MechModel* m = of(kind, name);
        return m && m->isDefault;
    }();
    models_.erase(std::remove_if(models_.begin(), models_.end(),
                                 [kind, &name](const MechModel& m) {
                                     return m.kind == kind && m.name == name;
                                 }),
                  models_.end());
    // The default went with it: the next one of that kind takes over, so a kind is never left with models
    // and nothing to use.
    if (!wasDefault) return;
    for (MechModel& m : models_)
        if (m.kind == kind) {
            m.isDefault = true;
            return;
        }
}

bool MechModelSet::rename(MechModelKind kind, const QString& from, const QString& to) {
    kind = canonicalKind(kind);
    if (of(kind, to)) return false;   // that name is taken on this kind
    for (MechModel& m : models_)
        if (m.kind == kind && m.name == from) {
            m.name = to;
            return true;
        }
    return false;
}

bool MechModelSet::setDefault(MechModelKind kind, const QString& name) {
    kind = canonicalKind(kind);
    if (!of(kind, name)) return false;
    for (MechModel& m : models_)
        if (m.kind == kind) m.isDefault = (m.name == name);
    return true;
}

void MechModelSet::remove(MechModelKind kind) {
    kind = canonicalKind(kind);
    models_.erase(std::remove_if(models_.begin(), models_.end(),
                                 [kind](const MechModel& m) { return m.kind == kind; }),
                  models_.end());
}

QJsonObject MechModelSet::toJson() const {
    QJsonArray arr;
    for (const MechModel& m : models_)
        if (!m.isEmpty()) arr.append(m.toJson());
    QJsonObject o;
    o.insert(QStringLiteral("format"), QStringLiteral("edraw-mech-models"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("models"), arr);
    return o;
}

void MechModelSet::fromJson(const QJsonObject& o) {
    models_.clear();
    for (const auto v : o.value(QStringLiteral("models")).toArray()) {
        MechModel m;
        m.fromJson(v.toObject());
        if (!m.isEmpty()) set(m);
    }
}

QString MechModelSet::defaultPath() {
    return QDir(AppPaths::root()).filePath(QStringLiteral("mech-models.json"));
}

// THE HOUSE MODELS SHIP WITH THE APPLICATION. They were only ever in the machine's own app-data folder,
// which means a fresh install - a new machine, another draughtsman, a rebuilt PC - came up with nothing
// drawn for any kind, and every drawing made on it was made without the house style until somebody drew all
// of them again. A set of default models is not personal data; it is part of what the program IS.
//
// Inside the executable rather than beside it, so there is no file to lose, no install step to forget, and
// no folder that a copied Loopwire.exe leaves behind.
QString MechModelSet::shippedPath() {
    return QStringLiteral(":/defaults/mech-models.json");
}

namespace {
bool g_useShipped = true;
}
void MechModelSet::setShippedDefaultsEnabled(bool on) { g_useShipped = on; }
bool MechModelSet::shippedDefaultsEnabled() { return g_useShipped; }

bool MechModelSet::load(const QString& path) {
    const QString use = path.isEmpty() ? defaultPath() : path;
    QFile f(use);
    // NOTHING OF ITS OWN YET: the models the application was built with. The machine's own file wins
    // wherever there is one - what somebody has drawn and captured is theirs and is never overwritten by
    // what was shipped - so this is a fallback and not a default that fights with them.
    if (g_useShipped && !f.exists() && (path.isEmpty() || path == defaultPath())) {
        QFile shipped(shippedPath());
        if (shipped.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(shipped.readAll());
            if (doc.isObject()) {
                fromJson(doc.object());
                return true;
            }
        }
    }
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    fromJson(doc.object());
    return true;
}

bool MechModelSet::save(const QString& path) const {
    const QString use = path.isEmpty() ? defaultPath() : path;
    QDir().mkpath(QFileInfo(use).absolutePath());
    QSaveFile f(use);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray payload = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (f.write(payload) != payload.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

// ---- drawing one, at the length it is wanted at -------------------------------------------------

namespace {

// A function applied to every point of an entity. The radius of a circle is left alone: stretching a
// drawing lengthwise must not turn a ferrule's round end into an oval, and a round thing that has to
// change size is a thing of another size rather than the same one stretched.
template <class F>
void mapEntityPoints(Entity& e, F fn) {
    e.a = fn(e.a);
    e.b = fn(e.b);
    e.center = fn(e.center);
    for (Vec2& p : e.points) p = fn(p);
}

Bounds drawingBounds(const QJsonArray& drawing) {
    Bounds b;
    for (const Entity& e : partDrawingEntities(drawing)) {
        const Bounds eb = e.bounds();
        if (eb.valid) {
            b.expand(eb.min);
            b.expand(eb.max);
        }
    }
    return b;
}

} // namespace

// ---- the marks, in one place ---------------------------------------------------------------------
//
// Moved here out of the canvas, which had them, and away from the manager's preview, which had a rougher
// version of its own: three plain rings in colours the preview then painted grey, no arrow head, no bars
// across the output faces and no numbers on them. The same marks in two windows drawn by two pieces of code
// is two answers to one question.
std::vector<Entity> placementMarkEntities(bool hasConnect, const Vec2& connectPoint,
                                          double connectAngleRad, bool hasNear, const Vec2& nearPoint,
                                          bool hasFar, const Vec2& farPoint, double markerRadius,
                                          double armMm) {
    std::vector<Entity> out;
    const double r = std::max(1e-6, markerRadius);
    // Everything here is chrome, and a preview paints a drawing in one quiet grey so that it reads as a
    // drawing rather than as a colour chart. These have to keep their colours - the colour IS the name of
    // the mark - so they say so, and the preview lets them through.
    auto mark = [](Entity& e, const QString& color) {
        e.layer = QLatin1String(kMechModelGuideLayer);
        e.color = color;
        e.meta.insert(QLatin1String(kPreviewMarkKey), true);
    };

    // THE THREE POINTS, in a fixed order, each with a name and a colour. A connection point sitting exactly on
    // the near point - which it usually does, since both mark where the part meets the run - drew as ONE
    // circle: whichever was added second painted over the first and there was no way to tell the near point
    // had ever been set. So every earlier mark this one lands on grows it a little, and one circle becomes two
    // concentric rings with each colour still its own.
    struct Mark { QString id, tag, color; Vec2 point; };
    std::vector<Mark> marks;
    if (hasConnect)
        marks.push_back({QStringLiteral("model-connect-point"), QStringLiteral("connect"),
                         QStringLiteral("#5aa0ff"), connectPoint});
    if (hasNear)
        marks.push_back({QStringLiteral("model-near-point"), QStringLiteral("near"),
                         QStringLiteral("#e6a23c"), nearPoint});
    if (hasFar)
        marks.push_back({QStringLiteral("model-far-point"), QStringLiteral("far"),
                         QStringLiteral("#62c370"), farPoint});
    for (size_t i = 0; i < marks.size(); ++i) {
        int stacked = 0;
        for (size_t j = 0; j < i; ++j)
            if (distance(marks[i].point, marks[j].point) < r * 0.5) ++stacked;
        Entity marker;
        marker.id = marks[i].id;
        marker.type = EntityType::Circle;
        marker.center = marks[i].point;
        marker.radius = r * (1.0 + 0.45 * stacked);
        mark(marker, marks[i].color);
        marker.meta.insert(QStringLiteral("modelAnchor"), marks[i].tag);
        out.push_back(marker);
    }

    // AND WHICH WAY IT RUNS FROM THE CONNECTION POINT - an arrow, with a head on it, because the whole reason
    // for marking any of this on a model is to be able to SEE what the wizard will do with it, and a bare line
    // says which line and not which way.
    if (hasConnect) {
        const double arm = armMm > 0.0 ? armMm : 6.0 * r;
        const double c = std::cos(connectAngleRad), s = std::sin(connectAngleRad);
        const Vec2 tip{connectPoint.x + arm * c, connectPoint.y + arm * s};
        Entity shaft;
        shaft.id = QStringLiteral("model-connect-dir");
        shaft.type = EntityType::Line;
        shaft.a = connectPoint;
        shaft.b = tip;
        mark(shaft, QStringLiteral("#5aa0ff"));
        out.push_back(shaft);
        const double headLen = arm * 0.22, spread = 0.35;
        for (const double side : {1.0, -1.0}) {
            Entity barb;
            barb.id = QStringLiteral("model-connect-barb%1").arg(side > 0 ? 1 : 2);
            barb.type = EntityType::Line;
            barb.a = tip;
            const double a2 = connectAngleRad + 3.14159265358979323846 + side * spread;
            barb.b = {tip.x + headLen * std::cos(a2), tip.y + headLen * std::sin(a2)};
            mark(barb, QStringLiteral("#5aa0ff"));
            out.push_back(barb);
        }
    }
    return out;
}

std::vector<Entity> outputFaceMarkEntities(const std::vector<MechOutputFace>& faces,
                                           double markerRadius) {
    std::vector<Entity> out;
    const MechModel model = [&faces] {
        MechModel m;
        m.outputs = faces;
        return m;
    }();
    const double r = std::max(1e-6, markerRadius);
    // Everything here is chrome, and a preview paints a drawing in one quiet grey so that it reads as a
    // drawing rather than as a colour chart. These have to keep their colours - the colour IS the name of
    // the mark - so they say so, and the preview lets them through.
    auto mark = [](Entity& e, const QString& color) {
        e.layer = QLatin1String(kMechModelGuideLayer);
        e.color = color;
        e.meta.insert(QLatin1String(kPreviewMarkKey), true);
    };

    // THE FACES THE WIRES COME OUT OF, each a bar across the part with a stop at either end and its number
    // beside it. The order they were marked is the order the generator hands the branches out in, so which one
    // is which has to be visible or a three-branch split cannot be checked.
    for (size_t i = 0; i < model.outputs.size(); ++i) {
        const MechModel::OutputLine& o = model.outputs[i];
        Entity bar;
        bar.id = QStringLiteral("model-output-%1").arg(i);
        bar.type = EntityType::Line;
        bar.a = o.a;
        bar.b = o.b;
        mark(bar, QStringLiteral("#c678dd"));
        bar.meta.insert(QStringLiteral("modelOutput"), static_cast<int>(i));
        out.push_back(bar);
        // A stop at each end, across the face, so it reads as a face WITH LIMITS rather than as a line
        // somebody drew: the wires may not run off either end of it.
        const Vec2 along = (o.b - o.a).normalized();
        const Vec2 across = along.perp() * std::max(1.0, r * 0.9);
        for (int end = 0; end < 2; ++end) {
            const Vec2 at = end == 0 ? o.a : o.b;
            Entity stop;
            stop.id = QStringLiteral("model-output-%1-end%2").arg(i).arg(end);
            stop.type = EntityType::Line;
            stop.a = at + across;
            stop.b = at - across;
            mark(stop, QStringLiteral("#c678dd"));
            out.push_back(stop);
        }
        Entity num;
        num.id = QStringLiteral("model-output-%1-no").arg(i);
        num.type = EntityType::Text;
        num.a = lerp(o.a, o.b, 0.5) + across * 1.6;
        mark(num, QStringLiteral("#c678dd"));
        num.meta.insert(QStringLiteral("text"), QString::number(i + 1));
        num.meta.insert(QStringLiteral("size"), std::max(1.5, r * 1.2));
        out.push_back(num);
    }
    return out;
}

std::vector<Entity> mechModelMarkEntities(const MechModel& model, double markerRadius, double armMm) {
    std::vector<Entity> out =
        placementMarkEntities(model.hasConnect, model.connectPoint, model.connectAngleRad,
                              model.hasNearPoint, model.nearPoint, model.hasFarPoint, model.farPoint,
                              markerRadius, armMm);
    for (Entity& e : outputFaceMarkEntities(model.outputs, markerRadius))
        out.push_back(std::move(e));
    return out;
}

double mechModelDrawnLength(const MechModel& model) {
    if (model.drawnLengthMm > 0.0) return model.drawnLengthMm;
    const Bounds b = drawingBounds(model.drawing);
    return b.valid ? std::max(1e-6, b.max.x - b.min.x) : 0.0;
}

double mechModelDrawnAcross(const MechModel& model) {
    if (model.drawnAcrossMm > 0.0) return model.drawnAcrossMm;
    const Bounds b = drawingBounds(model.drawing);
    return b.valid ? std::max(1e-6, b.max.y - b.min.y) : 0.0;
}

double mechModelAnchorLength(const MechModel& model) {
    if (!model.hasNearPoint || !model.hasFarPoint) return 0.0;
    return distance(model.nearPoint, model.farPoint);
}

std::vector<Entity> mechModelEntities(const MechModel& model, const Vec2& at, double dirRad,
                                      double lengthMm, double acrossMm, int wireCount, bool flip) {
    std::vector<Entity> out = partDrawingEntities(model.drawing);
    if (out.empty()) return out;
    const Bounds b = drawingBounds(model.drawing);
    if (!b.valid) return out;
    const double drawn = mechModelDrawnLength(model);
    const double drawnAcross = mechModelDrawnAcross(model);
    const double x0 = b.min.x;
    const double yMid = (b.min.y + b.max.y) / 2.0;

    // How the length is made up: the two ends keep their size, the middle is stretched to fit. A
    // length shorter than the two ends together cannot keep them both, so they are shrunk in
    // proportion rather than crossing over one another.
    // The GAP a cable or a wire leaves at each end of its run: the piece it does not cover, where the
    // conductors come out and are worked on. The run itself is what the harness says when the drawing is
    // generated - the model never carries a length - so the gap is taken off it here, at each end, and the
    // drawing is moved along by one gap so it sits in the middle of the run it was asked for. Zero for
    // everything else, and zero draws exactly as it always did.
    const double gap = MechModel::kindHasGap(model.kind) ? std::max(0.0, model.gapMm) : 0.0;
    const double askedFor = lengthMm > 0.0 && gap > 0.0 ? std::max(1e-6, lengthMm - 2.0 * gap) : lengthMm;
    const bool stretch = mechModelStretches(model.kind) && askedFor > 0.0 && drawn > 1e-9;
    double head = std::max(0.0, model.headMm);
    double tail = std::max(0.0, model.tailMm);
    if (head + tail > drawn) {
        const double k = drawn / std::max(1e-9, head + tail);
        head *= k;
        tail *= k;
    }
    double want = stretch ? askedFor : drawn;
    if (stretch && want < head + tail) {
        const double k = want / std::max(1e-9, head + tail);
        head *= k;
        tail *= k;
    }
    const double middleDrawn = std::max(0.0, drawn - head - tail);
    const double middleWant = std::max(0.0, want - head - tail);
    const double xScale = middleDrawn > 1e-9 ? middleWant / middleDrawn : 1.0;
    const double yScale = acrossMm > 0.0 && drawnAcross > 1e-9 ? acrossMm / drawnAcross : 1.0;

    auto mapPoint = [&](const Vec2& p) {
        // Moved along by one gap, so what is drawn sits in the MIDDLE of the run it was asked for and the
        // gap is left at both ends rather than all of it at one.
        const double x = p.x - x0;                     // from the start of the model
        double nx = x;
        if (!stretch) nx = x;
        else if (x <= head) nx = x;                    // the head keeps its own size
        else if (x >= drawn - tail) nx = want - (drawn - x);   // and so does the tail
        else nx = head + (x - head) * xScale;          // the middle makes up the difference
        // Moved along by one gap, so what is drawn sits in the MIDDLE of the run it was asked for and the
        // gap is left at both ends rather than all of it at one end.
        nx += gap;
        const double ny = (p.y - yMid) * yScale;
        return Vec2{nx, ny};
    };

    // Every conductor of a per-wire model spaced across the width it is asked for, instead of the
    // drawing being squashed to fit a different number in.
    std::vector<Entity> built;
    const int copies = mechModelPerWire(model.kind) && wireCount > 0 && model.drawnWireCount > 0
                           ? wireCount
                           : 0;
    if (copies > 0) {
        // One conductor of the drawing - the middle one - repeated across, evenly.
        const double pitch = (acrossMm > 0.0 ? acrossMm : drawnAcross) /
                             std::max(1, copies);
        for (int i = 0; i < copies; ++i) {
            const double y = -((copies - 1) * pitch) / 2.0 + pitch * i;
            for (const Entity& e : out) {
                Entity c = e;
                mapEntityPoints(c, [&](const Vec2& p) {
                    const Vec2 q = mapPoint(p);
                    return Vec2{q.x, y};   // every conductor a line of its own, at its own height
                });
                built.push_back(c);
            }
        }
    } else {
        for (const Entity& e : out) {
            Entity c = e;
            mapEntityPoints(c, mapPoint);
            built.push_back(c);
        }
    }

    // THE SIDE. The same part on the other side of the run is the same drawing seen the other way up, so it
    // is mirrored about the run's own axis - which, before it is turned, is the x axis. Done here, in the
    // model's own coordinates, so what is mirrored is the part and not its place on the sheet.
    if (flip)
        for (Entity& e : built) mirrorEntity(e, Vec2{0.0, 0.0}, Vec2{1.0, 0.0});

    // WHERE IT CONNECTS AND WHICH WAY IT RUNS. A model that says both is turned by the difference between
    // the run's direction and its own, and set down so its connection point lands exactly on the point asked
    // for. A model that says neither is what it always was: drawn along +x, from its own origin.
    const double turn = model.hasConnect ? dirRad - model.connectAngleRad : dirRad;
    Vec2 anchor{0.0, 0.0};
    if (model.hasConnect) anchor = mapPoint(model.connectPoint);
    else if (!stretch && model.hasNearPoint) anchor = mapPoint(model.nearPoint);
    if (flip) anchor.y = -anchor.y;   // mirrored with the rest of it
    Vec2 translation = at;
    if (model.hasConnect || (!stretch && model.hasNearPoint)) {
        const Vec2 turned{anchor.x * std::cos(turn) - anchor.y * std::sin(turn),
                          anchor.x * std::sin(turn) + anchor.y * std::cos(turn)};
        translation -= turned;
    }
    for (Entity& e : built) {
        if (std::abs(turn) > 1e-9) rotateEntity(e, Vec2{0.0, 0.0}, turn);
        translateEntity(e, translation);
    }
    return built;
}

} // namespace Loopwire

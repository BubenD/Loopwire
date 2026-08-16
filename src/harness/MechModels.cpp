#include "harness/MechModels.h"

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

namespace Edraw_Desktop {

namespace {

struct KindName {
    MechModelKind kind;
    const char* key;
    const char* label;
};

// The order the window lists them in: the ends first, then the lengths, then the pieces at a junction.
const std::array<KindName, 11> kKinds = {{
    {MechModelKind::RingInsulated, "ring", "Ring terminal"},
    {MechModelKind::FerruleInsulated, "ferrule", "Ferrule"},
    {MechModelKind::TransitionHeatshrink, "transition-heatshrink", "Transition heatshrink"},
    {MechModelKind::Cable, "cable", "Cable"},
    {MechModelKind::Wire, "wire", "Wire"},
    {MechModelKind::OpenWires, "open-wires", "Open wires"},
    {MechModelKind::ConnectorToCableWires, "connector-to-cable-wires",
     "Wires between a connector and its cable"},
    {MechModelKind::YSplit, "y-split", "Y-split"},
    {MechModelKind::Heatshrink, "heatshrink", "Heatshrink"},
    {MechModelKind::Boot, "boot", "Boot"},
    {MechModelKind::CableBody, "cable-body", "Cable body"},
}};

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
        return true;
    default:
        // A ring, a ferrule, a Y-split: a thing of its own size, placed and not stretched.
        return false;
    }
}

bool mechModelPerWire(MechModelKind kind) {
    return kind == MechModelKind::OpenWires || kind == MechModelKind::ConnectorToCableWires;
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
    o.insert(QStringLiteral("kind"), mechModelKey(kind));
    if (isInsulationKind(kind)) o.insert(QStringLiteral("insulated"), insulated);
    if (hasNearPoint)
        o.insert(QStringLiteral("nearPoint"), QJsonObject{{"x", nearPoint.x}, {"y", nearPoint.y}});
    if (hasFarPoint)
        o.insert(QStringLiteral("farPoint"), QJsonObject{{"x", farPoint.x}, {"y", farPoint.y}});
    o.insert(QStringLiteral("drawing"), drawing);
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

bool MechModelSet::load(const QString& path) {
    const QString use = path.isEmpty() ? defaultPath() : path;
    QFile f(use);
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

} // namespace Edraw_Desktop

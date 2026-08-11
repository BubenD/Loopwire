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
    for (const MechModel& m : models_)
        if (m.kind == kind && !m.isEmpty()) return &m;
    return nullptr;
}

void MechModelSet::set(const MechModel& model) {
    MechModel normalized = model;
    if (normalized.kind == MechModelKind::RingUninsulated ||
        normalized.kind == MechModelKind::FerruleUninsulated)
        normalized.insulated = false;
    normalized.kind = canonicalKind(normalized.kind);
    for (MechModel& m : models_)
        if (m.kind == normalized.kind) {
            m = normalized;
            return;
        }
    models_.push_back(normalized);
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
                                      double lengthMm, double acrossMm, int wireCount) {
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
    const bool stretch = mechModelStretches(model.kind) && lengthMm > 0.0 && drawn > 1e-9;
    double head = std::max(0.0, model.headMm);
    double tail = std::max(0.0, model.tailMm);
    if (head + tail > drawn) {
        const double k = drawn / std::max(1e-9, head + tail);
        head *= k;
        tail *= k;
    }
    double want = stretch ? lengthMm : drawn;
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
        const double x = p.x - x0;                     // from the start of the model
        double nx = x;
        if (!stretch) nx = x;
        else if (x <= head) nx = x;                    // the head keeps its own size
        else if (x >= drawn - tail) nx = want - (drawn - x);   // and so does the tail
        else nx = head + (x - head) * xScale;          // the middle makes up the difference
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

    // Placed: turned to the run's direction and set down at the point.
    Vec2 translation = at;
    if (!stretch && model.hasNearPoint) {
        const Vec2 local = mapPoint(model.nearPoint);
        const Vec2 turned{local.x * std::cos(dirRad) - local.y * std::sin(dirRad),
                          local.x * std::sin(dirRad) + local.y * std::cos(dirRad)};
        translation -= turned;
    }
    for (Entity& e : built) {
        if (std::abs(dirRad) > 1e-9) rotateEntity(e, Vec2{0.0, 0.0}, dirRad);
        translateEntity(e, translation);
    }
    return built;
}

} // namespace Edraw_Desktop

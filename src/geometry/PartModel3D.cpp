#include "geometry/PartModel3D.h"

#ifdef LOOPWIRE_HAVE_OCCT
#include "geometry/StepOcct.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace Loopwire {

namespace {

struct ShapeInfo {
    BodyShape shape;
    const char* key;
    const char* name;
    const char* a;
    const char* b;
    const char* c;
    double da, db, dc;
};

const ShapeInfo kShapes[] = {
    {BodyShape::Box, "box", "Box", "Length (X)", "Width (Y)", "Height (Z)", 20, 12, 10},
    {BodyShape::Cylinder, "cylinder", "Cylinder", "Length", "Diameter", "", 20, 12, 0},
    {BodyShape::Tube, "tube", "Tube", "Length", "Outside diameter", "Inside diameter", 20, 12, 8},
    {BodyShape::Cone, "cone", "Cone", "Length", "Diameter at the start", "Diameter at the end", 20, 12, 6},
    {BodyShape::Sphere, "sphere", "Sphere", "Diameter", "", "", 12, 0, 0},
    {BodyShape::Torus, "torus", "Ring (torus)", "Ring diameter", "Section diameter", "", 16, 3, 0},
    {BodyShape::Hex, "hex", "Hexagon (nut)", "Length", "Across the flats", "", 8, 12, 0},
    {BodyShape::Turned, "turned", "Turned from the drawing", "", "", "", 0, 0, 0},
    {BodyShape::Extruded, "extruded", "Extruded from the drawing", "", "", "", 0, 0, 0},
    {BodyShape::Pins, "pins", "Pins from the pinout", "", "", "", 0, 0, 0},
    {BodyShape::Sketch, "sketch", "Sketch, extruded", "Depth", "", "", 5, 0, 0},
    {BodyShape::Pin, "pin", "Pin", "Diameter", "Length", "", 1, 4, 0},
    {BodyShape::Revolved, "revolved", "Sketch, revolved", "Angle (°)", "", "", 360, 0, 0},
};

const struct {
    BodyOp op;
    const char* key;
    const char* name;
} kOps[] = {{BodyOp::None, "", "None"}, {BodyOp::Add, "add", "Add"}, {BodyOp::Subtract, "subtract", "Subtract"},
            {BodyOp::Intersect, "intersect", "Intersect"}};

QJsonArray loopJson(const SketchLoop& loop) {
    QJsonArray pts;
    for (const auto& [u, v] : loop) pts.append(QJsonArray{u, v});
    return pts;
}

SketchLoop loopFrom(const QJsonValue& value) {
    SketchLoop loop;
    for (const QJsonValue& p : value.toArray()) {
        const QJsonArray uv = p.toArray();
        if (uv.size() == 2) loop.emplace_back(uv.at(0).toDouble(), uv.at(1).toDouble());
    }
    return loop;
}

// AN OUTLINE TURNED round the plane's first direction (0) or its second (1), in the plane's own space - its open ends
// left open: what is shown where Open CASCADE is not.
Mesh3D latheMesh(const SketchLoop& loop, int axis, double degrees) {
    Mesh3D m;
    const int n = static_cast<int>(loop.size());
    if (n < 3) return m;
    const double sweep = std::clamp(std::abs(degrees), 0.5, 360.0) * M_PI / 180.0;
    const int steps = std::max(8, static_cast<int>(std::ceil(sweep / (M_PI / 24.0))));
    for (int s = 0; s <= steps; ++s) {
        const double t = sweep * s / steps, ct = std::cos(t), st = std::sin(t);
        for (const auto& [u, v] : loop) m.addVertex(axis == 1 ? V3{u * ct, v, -u * st} : V3{u, v * ct, v * st});
    }
    for (int s = 0; s < steps; ++s)
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n, a = s * n + i, b = s * n + j, c = (s + 1) * n + i, d = (s + 1) * n + j;
            m.addTri(a, b, d);
            m.addTri(a, d, c);
        }
    return m;
}

const ShapeInfo& info(BodyShape s) {
    for (const ShapeInfo& i : kShapes)
        if (i.shape == s) return i;
    return kShapes[0];
}

QJsonArray v3Json(const V3& p) { return {p.x, p.y, p.z}; }
V3 v3From(const QJsonValue& v, const V3& otherwise) {
    const QJsonArray a = v.toArray();
    return a.size() == 3 ? V3{a[0].toDouble(), a[1].toDouble(), a[2].toDouble()} : otherwise;
}

QJsonObject meshJson(const Mesh3D& m) {
    QJsonArray vs, fs;
    for (const V3& p : m.v) {
        vs.append(p.x);
        vs.append(p.y);
        vs.append(p.z);
    }
    for (const auto& t : m.f) {
        fs.append(t[0]);
        fs.append(t[1]);
        fs.append(t[2]);
    }
    return {{QStringLiteral("v"), vs}, {QStringLiteral("f"), fs}};
}

Mesh3D meshFrom(const QJsonObject& o) {
    Mesh3D m;
    const QJsonArray vs = o.value(QStringLiteral("v")).toArray(), fs = o.value(QStringLiteral("f")).toArray();
    for (qsizetype i = 0; i + 2 < vs.size(); i += 3) m.addVertex({vs[i].toDouble(), vs[i + 1].toDouble(), vs[i + 2].toDouble()});
    const int n = static_cast<int>(m.v.size());
    for (qsizetype i = 0; i + 2 < fs.size(); i += 3) {
        const int a = fs[i].toInt(), b = fs[i + 1].toInt(), c = fs[i + 2].toInt();
        if (a >= 0 && b >= 0 && c >= 0 && a < n && b < n && c < n) m.addTri(a, b, c);
    }
    return m;
}

// A ROUND SOLID ALONG X, its radius from one end to the other.
Mesh3D roundAlongX(double length, double r0, double r1, int sides = 40) {
    const double h = std::max(1e-6, length) / 2.0;
    return tubeMeshVar({{-h, 0, 0}, {h, 0, 0}}, [&](double along) { return r0 + (r1 - r0) * along / (2.0 * h); }, sides, true);
}

Mesh3D sphereMesh(double r) {
    Mesh3D m;
    const int rings = 24, segs = 40;
    for (int i = 0; i <= rings; ++i) {
        const double th = M_PI * i / rings;
        for (int j = 0; j < segs; ++j) {
            const double ph = 2.0 * M_PI * j / segs;
            m.addVertex({r * std::cos(th), r * std::sin(th) * std::cos(ph), r * std::sin(th) * std::sin(ph)});
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < segs; ++j) {
            const int a = i * segs + j, b = i * segs + (j + 1) % segs, c = a + segs, d = b + segs;
            m.addTri(a, b, d);
            m.addTri(a, d, c);
        }
    return m;
}

// A RING ROUND THE X AXIS.
Mesh3D torusMesh(double ringR, double r) {
    Mesh3D m;
    const int around = 48, section = 20;
    for (int i = 0; i < around; ++i) {
        const double u = 2.0 * M_PI * i / around;
        for (int j = 0; j < section; ++j) {
            const double w = 2.0 * M_PI * j / section;
            const double rr = ringR + r * std::cos(w);
            m.addVertex({r * std::sin(w), rr * std::cos(u), rr * std::sin(u)});
        }
    }
    for (int i = 0; i < around; ++i)
        for (int j = 0; j < section; ++j) {
            const int a = i * section + j, b = i * section + (j + 1) % section;
            const int c = ((i + 1) % around) * section + j, d = ((i + 1) % around) * section + (j + 1) % section;
            m.addTri(a, b, d);
            m.addTri(a, d, c);
        }
    return m;
}

// A HOLLOW TUBE ALONG X: outside, inside, and the two rings closing its ends.
Mesh3D hollowMesh(double length, double ro, double ri) {
    Mesh3D m;
    const int sides = 40;
    const double h = std::max(1e-6, length) / 2.0;
    ri = std::clamp(ri, 0.0, ro * 0.98);
    for (int end = 0; end < 2; ++end)
        for (int ring = 0; ring < 2; ++ring)
            for (int k = 0; k < sides; ++k) {
                const double a = 2.0 * M_PI * k / sides, r = ring == 0 ? ro : ri;
                m.addVertex({end == 0 ? -h : h, r * std::cos(a), r * std::sin(a)});
            }
    auto at = [&](int end, int ring, int k) { return (end * 2 + ring) * sides + (k % sides); };
    for (int k = 0; k < sides; ++k) {
        // Outside and inside along it.
        for (int ring = 0; ring < 2; ++ring) {
            m.addTri(at(0, ring, k), at(1, ring, k), at(1, ring, k + 1));
            m.addTri(at(0, ring, k), at(1, ring, k + 1), at(0, ring, k + 1));
        }
        // Its two ends.
        for (int end = 0; end < 2; ++end) {
            m.addTri(at(end, 0, k), at(end, 1, k), at(end, 1, k + 1));
            m.addTri(at(end, 0, k), at(end, 1, k + 1), at(end, 0, k + 1));
        }
    }
    return m;
}

// THE TURN: round X, then Y, then Z.
V3 turn(const V3& p, const V3& deg) {
    const double ax = deg.x * M_PI / 180.0, ay = deg.y * M_PI / 180.0, az = deg.z * M_PI / 180.0;
    V3 q{p.x, p.y * std::cos(ax) - p.z * std::sin(ax), p.y * std::sin(ax) + p.z * std::cos(ax)};
    q = {q.x * std::cos(ay) + q.z * std::sin(ay), q.y, -q.x * std::sin(ay) + q.z * std::cos(ay)};
    q = {q.x * std::cos(az) - q.y * std::sin(az), q.x * std::sin(az) + q.y * std::cos(az), q.z};
    return q;
}

Mesh3D placed(const Body3D& b, Mesh3D m) {
    for (V3& v : m.v) v = bodyPlace(b, v);
    return m;
}

} // namespace

QString bodyShapeKey(BodyShape shape) { return QString::fromLatin1(info(shape).key); }

BodyShape bodyShapeFrom(const QString& key) {
    for (const ShapeInfo& i : kShapes)
        if (key == QLatin1String(i.key)) return i.shape;
    return BodyShape::Box;
}

QString bodyShapeName(BodyShape shape) { return QString::fromLatin1(info(shape).name); }

QStringList bodySizeLabels(BodyShape shape) {
    const ShapeInfo& i = info(shape);
    return {QString::fromLatin1(i.a), QString::fromLatin1(i.b), QString::fromLatin1(i.c)};
}

void bodyDefaultSizes(BodyShape shape, double& a, double& b, double& c) {
    const ShapeInfo& i = info(shape);
    a = i.da;
    b = i.db;
    c = i.dc;
}

bool bodyIsPrimitive(BodyShape shape) {
    return shape != BodyShape::Turned && shape != BodyShape::Extruded && shape != BodyShape::Pins;
}

bool bodyIsSolid(BodyShape shape) { return shape != BodyShape::Pins; }

QString bodyOpKey(BodyOp op) {
    for (const auto& o : kOps)
        if (o.op == op) return QString::fromLatin1(o.key);
    return {};
}

BodyOp bodyOpFrom(const QString& key) {
    for (const auto& o : kOps)
        if (!key.isEmpty() && key == QLatin1String(o.key)) return o.op;
    return BodyOp::None;
}

QString bodyOpName(BodyOp op) {
    for (const auto& o : kOps)
        if (o.op == op) return QString::fromLatin1(o.name);
    return {};
}

void sketchAxes(int plane, V3& u, V3& v, V3& w) {
    // Each as the view from there sees it - the outline's right and up, and toward the one looking at it.
    if (plane == 1) {
        u = {1, 0, 0};
        v = {0, 0, 1};
        w = {0, -1, 0};
    } else if (plane == 2) {
        u = {0, 1, 0};
        v = {0, 0, 1};
        w = {1, 0, 0};
    } else {
        u = {1, 0, 0};
        v = {0, 1, 0};
        w = {0, 0, 1};
    }
}

V3 turnAlong(const V3& normal) {
    const V3 n = normal.normalized();
    if (n.length() < 0.5) return {0, 0, 0};
    // Round Y first, then Z: (1,0,0) to (cos b cos g, cos b sin g, -sin b).
    return {0.0, -std::asin(std::clamp(n.z, -1.0, 1.0)) * 180.0 / M_PI, std::atan2(n.y, n.x) * 180.0 / M_PI};
}

V3 turnPoint(const V3& p, const V3& deg) { return turn(p, deg); }

V3 turnPointBack(const V3& p, const V3& deg) {
    const double ax = -deg.x * M_PI / 180.0, ay = -deg.y * M_PI / 180.0, az = -deg.z * M_PI / 180.0;
    V3 q{p.x * std::cos(az) - p.y * std::sin(az), p.x * std::sin(az) + p.y * std::cos(az), p.z};
    q = {q.x * std::cos(ay) + q.z * std::sin(ay), q.y, -q.x * std::sin(ay) + q.z * std::cos(ay)};
    q = {q.x, q.y * std::cos(ax) - q.z * std::sin(ax), q.y * std::sin(ax) + q.z * std::cos(ax)};
    return q;
}

double loopArea(const SketchLoop& loop) {
    double area = 0.0;
    for (size_t i = 0; i < loop.size(); ++i) {
        const auto& p = loop[i];
        const auto& q = loop[(i + 1) % loop.size()];
        area += p.first * q.second - q.first * p.second;
    }
    return area / 2.0;
}

bool loopContains(const SketchLoop& loop, double u, double v) {
    bool in = false;
    for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const auto& a = loop[i];
        const auto& b = loop[j];
        if ((a.second > v) != (b.second > v) && u < (b.first - a.first) * (v - a.second) / (b.second - a.second) + a.first) in = !in;
    }
    return in;
}

std::vector<SketchRegion> sketchRegions(const std::vector<SketchLoop>& loops) {
    // EACH OUTLINE BY HOW MANY IT IS INSIDE: none, or two, a solid; one, or three, a hole in the least one round it.
    const size_t n = loops.size();
    auto inside = [&](size_t a, size_t b) {
        if (a == b || loops[a].empty() || loops[b].size() < 3) return false;
        if (std::abs(loopArea(loops[a])) >= std::abs(loopArea(loops[b]))) return false;
        for (const auto& [u, v] : loops[a])
            if (!loopContains(loops[b], u, v)) return false;
        return true;
    };
    std::vector<int> depth(n, 0), around(n, -1);
    for (size_t a = 0; a < n; ++a)
        for (size_t b = 0; b < n; ++b)
            if (inside(a, b)) {
                ++depth[a];
                if (around[a] < 0 || std::abs(loopArea(loops[b])) < std::abs(loopArea(loops[static_cast<size_t>(around[a])])))
                    around[a] = static_cast<int>(b);
            }
    std::vector<SketchRegion> out;
    std::vector<int> regionOf(n, -1);
    for (size_t a = 0; a < n; ++a)
        if (loops[a].size() >= 3 && depth[a] % 2 == 0) {
            regionOf[a] = static_cast<int>(out.size());
            out.push_back({loops[a], {}});
        }
    for (size_t a = 0; a < n; ++a)
        if (loops[a].size() >= 3 && depth[a] % 2 == 1 && around[a] >= 0 && regionOf[static_cast<size_t>(around[a])] >= 0)
            out[static_cast<size_t>(regionOf[static_cast<size_t>(around[a])])].holes.push_back(loops[a]);
    return out;
}

QJsonObject PartModel3D::toJson() const {
    QJsonArray bs;
    for (const Body3D& b : bodies) {
        QJsonObject o{{QStringLiteral("id"), b.id},
                      {QStringLiteral("name"), b.name},
                      {QStringLiteral("shape"), bodyShapeKey(b.shape)},
                      {QStringLiteral("a"), b.a},
                      {QStringLiteral("b"), b.b},
                      {QStringLiteral("c"), b.c},
                      {QStringLiteral("at"), v3Json(b.at)},
                      {QStringLiteral("turn"), v3Json(b.turnDeg)},
                      {QStringLiteral("scale"), v3Json(b.scale)},
                      {QStringLiteral("color"), b.color.name()},
                      {QStringLiteral("texture"), b.texture},
                      {QStringLiteral("cut"), b.cut},
                      {QStringLiteral("shown"), b.shown}};
        if (b.fillet > 0.0) o.insert(QStringLiteral("fillet"), b.fillet);
        if (b.chamfer > 0.0) o.insert(QStringLiteral("chamfer"), b.chamfer);
        if (bodyIsSketch(b.shape)) {
            o.insert(QStringLiteral("plane"), b.plane);
            o.insert(QStringLiteral("profile"), loopJson(b.profile));
            if (!b.holes.empty()) {
                QJsonArray hs;
                for (const SketchLoop& h : b.holes) hs.append(loopJson(h));
                o.insert(QStringLiteral("holes"), hs);
            }
            if (b.both) o.insert(QStringLiteral("both"), true);
            if (b.shape == BodyShape::Revolved) o.insert(QStringLiteral("axis"), b.axis);
        }
        if (b.op != BodyOp::None) {
            o.insert(QStringLiteral("op"), bodyOpKey(b.op));
            o.insert(QStringLiteral("target"), b.target);
        }
        if (b.shape == BodyShape::Pin) {
            o.insert(QStringLiteral("female"), b.female);
            o.insert(QStringLiteral("number"), b.number);
        }
        if (!b.made.empty()) o.insert(QStringLiteral("made"), meshJson(b.made));
        if (!b.madeSockets.empty()) o.insert(QStringLiteral("madeSockets"), meshJson(b.madeSockets));
        bs.append(o);
    }
    return {{QStringLiteral("edrawPartModel"), 1}, {QStringLiteral("bodies"), bs}};
}

PartModel3D PartModel3D::fromJson(const QJsonObject& o) {
    PartModel3D m;
    for (const QJsonValue& v : o.value(QStringLiteral("bodies")).toArray()) {
        const QJsonObject j = v.toObject();
        Body3D b;
        b.id = j.value(QStringLiteral("id")).toString();
        b.name = j.value(QStringLiteral("name")).toString();
        b.shape = bodyShapeFrom(j.value(QStringLiteral("shape")).toString());
        b.a = j.value(QStringLiteral("a")).toDouble(b.a);
        b.b = j.value(QStringLiteral("b")).toDouble(b.b);
        b.c = j.value(QStringLiteral("c")).toDouble(b.c);
        b.at = v3From(j.value(QStringLiteral("at")), {0, 0, 0});
        b.turnDeg = v3From(j.value(QStringLiteral("turn")), {0, 0, 0});
        b.scale = v3From(j.value(QStringLiteral("scale")), {1, 1, 1});
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) b.color = c;
        b.texture = j.value(QStringLiteral("texture")).toInt(1);
        b.cut = j.value(QStringLiteral("cut")).toBool(false);
        b.shown = j.value(QStringLiteral("shown")).toBool(true);
        b.made = meshFrom(j.value(QStringLiteral("made")).toObject());
        b.madeSockets = meshFrom(j.value(QStringLiteral("madeSockets")).toObject());
        b.fillet = j.value(QStringLiteral("fillet")).toDouble(0.0);
        b.chamfer = j.value(QStringLiteral("chamfer")).toDouble(0.0);
        b.plane = std::clamp(j.value(QStringLiteral("plane")).toInt(0), 0, 2);
        b.profile = loopFrom(j.value(QStringLiteral("profile")));
        for (const QJsonValue& h : j.value(QStringLiteral("holes")).toArray()) {
            SketchLoop loop = loopFrom(h);
            if (loop.size() >= 3) b.holes.push_back(std::move(loop));
        }
        b.both = j.value(QStringLiteral("both")).toBool(false);
        b.axis = std::clamp(j.value(QStringLiteral("axis")).toInt(0), 0, 1);
        b.op = bodyOpFrom(j.value(QStringLiteral("op")).toString());
        b.target = j.value(QStringLiteral("target")).toString();
        b.female = j.value(QStringLiteral("female")).toBool(false);
        b.number = j.value(QStringLiteral("number")).toInt(0);
        if (b.id.isEmpty()) b.id = m.freshId();
        m.bodies.push_back(std::move(b));
    }
    return m;
}

bool PartModel3D::save(const QString& path, QString* error) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    return true;
}

bool PartModel3D::load(const QString& path, PartModel3D& model, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject() || !doc.object().contains(QStringLiteral("edrawPartModel"))) {
        if (error) *error = QStringLiteral("not a part model made in Loopwire");
        return false;
    }
    model = fromJson(doc.object());
    return true;
}

QString PartModel3D::freshId() const {
    for (int n = static_cast<int>(bodies.size()) + 1;; ++n) {
        const QString id = QStringLiteral("b%1").arg(n);
        bool taken = false;
        for (const Body3D& b : bodies) taken = taken || b.id == id;
        if (!taken) return id;
    }
}

int PartModel3D::indexOf(const QString& id) const {
    for (size_t i = 0; i < bodies.size(); ++i)
        if (bodies[i].id == id) return static_cast<int>(i);
    return -1;
}

bool PartModel3D::canCombine(int body, int target) const {
    const int n = static_cast<int>(bodies.size());
    if (body < 0 || target < 0 || body >= n || target >= n || body == target) return false;
    if (!bodyIsSolid(bodies[static_cast<size_t>(body)].shape) || !bodyIsSolid(bodies[static_cast<size_t>(target)].shape)) return false;
    // Up from the target through what each is combined into: never back to the body.
    int at = target;
    for (int steps = 0; at >= 0 && steps <= n; ++steps) {
        if (at == body) return false;
        const Body3D& b = bodies[static_cast<size_t>(at)];
        at = b.op == BodyOp::None ? -1 : indexOf(b.target);
    }
    return true;
}

bool isPartModelFile(const QString& path) {
    return QFileInfo(path).suffix().compare(partModelExtension(), Qt::CaseInsensitive) == 0;
}

Mesh3D bodyLocalMesh(const Body3D& b) {
    const double a = std::max(0.01, b.a), bb = std::max(0.01, b.b), c = std::max(0.0, b.c);
    switch (b.shape) {
        case BodyShape::Box: return boxMesh({-a / 2, -bb / 2, -std::max(0.01, c) / 2}, {a / 2, bb / 2, std::max(0.01, c) / 2});
        case BodyShape::Cylinder: return roundAlongX(a, bb / 2, bb / 2);
        case BodyShape::Tube: return hollowMesh(a, bb / 2, c / 2);
        case BodyShape::Cone: return roundAlongX(a, bb / 2, std::max(0.0, c) / 2);
        case BodyShape::Sphere: return sphereMesh(a / 2);
        case BodyShape::Torus: return torusMesh(a / 2, bb / 2);
        case BodyShape::Hex: {
            // Across the flats: the corners a little further out.
            const double r = bb / 2.0 / std::cos(M_PI / 6.0);
            Mesh3D m = roundAlongX(a, r, r, 6);
            return m;
        }
        case BodyShape::Turned:
        case BodyShape::Extruded: return b.made;
        case BodyShape::Pins: {
            Mesh3D m = b.made;
            m.append(b.madeSockets);
            return m;
        }
        case BodyShape::Sketch: {
            // The outline given depth - toward the one who drew it, or away (a negative depth), or half each way - then
            // laid in its plane. Its holes only where Open CASCADE makes it.
            const double depth = std::max(0.01, std::abs(b.a));
            Mesh3D m = prismMesh(b.profile, depth);
            if (b.a < 0.0 || b.both)
                for (V3& p : m.v) p.z -= b.both ? depth / 2.0 : depth;
            V3 u, v, w;
            sketchAxes(b.plane, u, v, w);
            for (V3& p : m.v) p = u * p.x + v * p.y + w * p.z;
            return m;
        }
        case BodyShape::Pin: {
            // A pin stands out of the face; a socket goes in.
            Mesh3D m = roundAlongX(bb, a / 2, a / 2, 16);
            for (V3& p : m.v) p.x += b.female ? -bb / 2 : bb / 2;
            return m;
        }
        case BodyShape::Revolved: {
            Mesh3D m = latheMesh(b.profile, b.axis, b.a);
            V3 u, v, w;
            sketchAxes(b.plane, u, v, w);
            for (V3& p : m.v) p = u * p.x + v * p.y + w * p.z;
            return m;
        }
    }
    return {};
}

V3 bodyPlace(const Body3D& b, const V3& local) {
    return b.at + turn({local.x * b.scale.x, local.y * b.scale.y, local.z * b.scale.z}, b.turnDeg);
}

Mesh3D bodyMesh(const Body3D& b) { return placed(b, bodyLocalMesh(b)); }

#ifdef LOOPWIRE_HAVE_OCCT
namespace {

// WHAT OPEN CASCADE MADE, kept by what it was made from: a body dragged across the view is made again at each step,
// and what it is combined with is not.
void putKey(QByteArray& k, double d) { k.append(reinterpret_cast<const char*>(&d), sizeof d); }

void putKey(QByteArray& k, const SketchLoop& loop) {
    putKey(k, static_cast<double>(loop.size()));
    for (const auto& [u, v] : loop) {
        putKey(k, u);
        putKey(k, v);
    }
}

void putKey(QByteArray& k, const OcctSolid& s) {
    for (double d : {static_cast<double>(s.shape), s.a, s.b, s.c, s.fillet, s.chamfer, s.u.x, s.u.y, s.u.z, s.v.x, s.v.y, s.v.z,
                     s.w.x, s.w.y, s.w.z, s.female ? 1.0 : 0.0, s.both ? 1.0 : 0.0, static_cast<double>(s.axis)})
        putKey(k, d);
    for (const auto& row : s.m)
        for (double d : row) putKey(k, d);
    putKey(k, s.profile);
    putKey(k, static_cast<double>(s.holes.size()));
    for (const SketchLoop& h : s.holes) putKey(k, h);
    if (s.mesh) {
        putKey(k, static_cast<double>(s.mesh->v.size()));
        putKey(k, static_cast<double>(qHashBits(s.mesh->v.data(), s.mesh->v.size() * sizeof(V3))));
        putKey(k, static_cast<double>(qHashBits(s.mesh->f.data(), s.mesh->f.size() * sizeof(s.mesh->f[0]))));
    }
}

void putKey(QByteArray& k, const OcctFeature& f) {
    putKey(k, static_cast<double>(f.op));
    putKey(k, f.solid);
    putKey(k, static_cast<double>(f.with.size()));
    for (const OcctFeature& g : f.with) putKey(k, g);
}

struct Made {
    Mesh3D mesh;
    bool ok = false;
    QString why;
    quint64 used = 0;
};

bool buildCached(const OcctFeature& f, const std::vector<OcctSolid>& cuts, Mesh3D& out, QString* why) {
    static std::mutex hold;
    static std::map<QByteArray, Made> made;
    static quint64 clock = 0;
    QByteArray key;
    putKey(key, f);
    putKey(key, static_cast<double>(cuts.size()));
    for (const OcctSolid& c : cuts) putKey(key, c);
    {
        const std::lock_guard<std::mutex> lock(hold);
        auto it = made.find(key);
        if (it != made.end()) {
            it->second.used = ++clock;
            out = it->second.mesh;
            if (why && !it->second.why.isEmpty()) *why = it->second.why;
            return it->second.ok;
        }
    }
    Made m;
    m.ok = occtBuildFeature(f, cuts, m.mesh, &m.why);
    out = m.mesh;
    if (why && !m.why.isEmpty()) *why = m.why;
    const std::lock_guard<std::mutex> lock(hold);
    // The least lately used let go, past sixty-four.
    if (made.size() >= 64) {
        auto oldest = made.begin();
        for (auto it = made.begin(); it != made.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        made.erase(oldest);
    }
    m.used = ++clock;
    const bool ok = m.ok;
    made.emplace(std::move(key), std::move(m));
    return ok;
}

} // namespace
#endif

std::vector<ModelItem3D> modelItems(const PartModel3D& model, QString* note) {
    std::vector<ModelItem3D> out;
    const int n = static_cast<int>(model.bodies.size());
    auto isCut = [](const Body3D& b) { return b.cut && b.shown && bodyIsSolid(b.shape); };
    bool anyCut = false;
    for (const Body3D& b : model.bodies) anyCut = anyCut || isCut(b);
    // WHAT EACH IS COMBINED INTO: the body it names, shown and a solid - never round in a ring back to itself.
    std::vector<int> into(static_cast<size_t>(n), -1);
    for (int i = 0; i < n; ++i) {
        const Body3D& b = model.bodies[static_cast<size_t>(i)];
        if (b.op == BodyOp::None || b.cut || !b.shown || !bodyIsSolid(b.shape)) continue;
        const int t = model.indexOf(b.target);
        if (t < 0 || t == i) continue;
        const Body3D& tb = model.bodies[static_cast<size_t>(t)];
        if (tb.shown && !tb.cut && bodyIsSolid(tb.shape)) into[static_cast<size_t>(i)] = t;
    }
    for (int i = 0; i < n; ++i) {
        int at = into[static_cast<size_t>(i)];
        for (int steps = 0; at >= 0 && steps <= n; ++steps) {
            if (at == i) {
                into[static_cast<size_t>(i)] = -1;
                break;
            }
            at = into[static_cast<size_t>(at)];
        }
    }
    auto combinedWith = [&](int i) {
        std::vector<int> with;
        for (int j = 0; j < n; ++j)
            if (into[static_cast<size_t>(j)] == i) with.push_back(j);
        return with;
    };
    // MADE BY OPEN CASCADE: one with cuts to take out of it or others combined with it, edges to round or cut off, or an
    // outline sketched.
    auto byOcct = [&](int i) {
        const Body3D& b = model.bodies[static_cast<size_t>(i)];
        if (!bodyIsSolid(b.shape)) return false;
        if (anyCut || !combinedWith(i).empty() || bodyIsSketch(b.shape)) return true;
        return bodyIsPrimitive(b.shape) && (b.fillet > 1e-6 || b.chamfer > 1e-6);
    };
#ifdef LOOPWIRE_HAVE_OCCT
    std::vector<OcctSolid> cuts;
    auto solidOf = [](const Body3D& b) {
        OcctSolid s;
        s.shape = static_cast<int>(b.shape);
        s.a = b.shape == BodyShape::Sketch ? (b.a < 0.0 ? std::min(-0.01, b.a) : std::max(0.01, b.a)) : b.a;
        s.b = b.b;
        s.c = b.c;
        s.fillet = bodyIsPrimitive(b.shape) ? b.fillet : 0.0;
        s.chamfer = bodyIsPrimitive(b.shape) ? b.chamfer : 0.0;
        s.profile = b.profile;
        s.holes = b.holes;
        s.both = b.both;
        s.axis = b.axis;
        if (b.shape == BodyShape::Turned || b.shape == BodyShape::Extruded) s.mesh = &b.made;
        sketchAxes(b.plane, s.u, s.v, s.w);
        s.female = b.female;
        // The turn and the scale as a matrix (each column where an axis goes), and where it is put.
        const V3 ex = turn({b.scale.x, 0, 0}, b.turnDeg), ey = turn({0, b.scale.y, 0}, b.turnDeg), ez = turn({0, 0, b.scale.z}, b.turnDeg);
        const double m[3][4] = {{ex.x, ey.x, ez.x, b.at.x}, {ex.y, ey.y, ez.y, b.at.y}, {ex.z, ey.z, ez.z, b.at.z}};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) s.m[r][c] = m[r][c];
        return s;
    };
    if (anyCut)
        for (const Body3D& b : model.bodies)
            if (isCut(b)) cuts.push_back(solidOf(b));
    std::function<OcctFeature(int)> featureOf = [&](int i) {
        const Body3D& b = model.bodies[static_cast<size_t>(i)];
        OcctFeature f{solidOf(b), static_cast<int>(b.op), {}};
        for (int j : combinedWith(i)) f.with.push_back(featureOf(j));
        return f;
    };
#endif
    // WITHOUT OPEN CASCADE (or where it fails): what is added put with it; what is taken out, or kept only where both
    // are, shown see-through red.
    std::function<void(int, Mesh3D&, bool)> plainOf = [&](int i, Mesh3D& mesh, bool red) {
        for (int j : combinedWith(i)) {
            const Body3D& c = model.bodies[static_cast<size_t>(j)];
            if (c.op == BodyOp::Add) {
                mesh.append(bodyMesh(c));
                plainOf(j, mesh, red);
            } else if (red) {
                out.push_back({bodyMesh(c), QColor(230, 60, 60), 0, c.id, 0.35});
            }
        }
    };
    for (int i = 0; i < n; ++i) {
        const Body3D& b = model.bodies[static_cast<size_t>(i)];
        if (!b.shown || into[static_cast<size_t>(i)] >= 0) continue;
        if (isCut(b)) {
#ifdef LOOPWIRE_HAVE_OCCT
            continue;   // taken out of the others
#else
            // NO OPEN CASCADE: the hole shown where it is, see-through red, and not taken out.
            out.push_back({bodyMesh(b), QColor(230, 60, 60), 0, b.id, 0.35});
            continue;
#endif
        }
        if (b.shape == BodyShape::Pins) {
            out.push_back({placed(b, b.made), QColor(212, 175, 80), 1, b.id, 1.0});
            out.push_back({placed(b, b.madeSockets), QColor(18, 18, 20), 3, b.id, 1.0});
            continue;
        }
        Mesh3D mesh;
        bool made = false;
#ifdef LOOPWIRE_HAVE_OCCT
        if (byOcct(i)) {
            QString why;
            made = buildCached(featureOf(i), cuts, mesh, &why);
            if (!made) mesh = Mesh3D();
            if (note && !why.isEmpty()) *note = why;
        }
#endif
        if (!made) {
            mesh = bodyMesh(b);
            plainOf(i, mesh, true);
        }
        if (mesh.empty() && made) continue;   // cut away whole, or nothing both held
        out.push_back({std::move(mesh), b.color, b.texture, b.id, 1.0});
    }
#ifndef LOOPWIRE_HAVE_OCCT
    bool anyOp = false;
    for (int i = 0; i < n; ++i) anyOp = anyOp || (into[static_cast<size_t>(i)] >= 0 && model.bodies[static_cast<size_t>(i)].op != BodyOp::Add);
    if ((anyCut || anyOp) && note)
        *note = QStringLiteral("Cutting needs Open CASCADE, which this build has not got - the cuts are shown in red, and not taken out");
#endif
    return out;
}

bool modelBounds(const std::vector<ModelItem3D>& items, V3& lo, V3& hi) {
    bool any = false;
    for (const ModelItem3D& it : items) {
        if (it.alpha < 0.99) continue;
        V3 a, b;
        if (!it.mesh.bounds(a, b)) continue;
        lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
        hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
        any = true;
    }
    return any;
}

bool exportModelStl(const std::vector<ModelItem3D>& items, const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << "solid edraw\n";
    for (const ModelItem3D& it : items) {
        if (it.alpha < 0.99) continue;
        for (const auto& t : it.mesh.f) {
            const V3 &a = it.mesh.v[static_cast<size_t>(t[0])], &b = it.mesh.v[static_cast<size_t>(t[1])],
                     &c = it.mesh.v[static_cast<size_t>(t[2])];
            const V3 n = cross3(b - a, c - a).normalized();
            out << "facet normal " << n.x << ' ' << n.y << ' ' << n.z << "\n outer loop\n";
            for (const V3* p : {&a, &b, &c}) out << "  vertex " << p->x << ' ' << p->y << ' ' << p->z << "\n";
            out << " endloop\nendfacet\n";
        }
    }
    out << "endsolid edraw\n";
    return true;
}

bool exportModelObj(const std::vector<ModelItem3D>& items, const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    int base = 1, k = 0;
    for (const ModelItem3D& it : items) {
        if (it.alpha < 0.99) continue;
        out << "o " << (it.body.isEmpty() ? QStringLiteral("part") : it.body) << "_" << ++k << "\n";
        for (const V3& p : it.mesh.v) out << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        for (const auto& t : it.mesh.f) out << "f " << t[0] + base << ' ' << t[1] + base << ' ' << t[2] + base << "\n";
        base += static_cast<int>(it.mesh.v.size());
    }
    return true;
}

} // namespace Loopwire

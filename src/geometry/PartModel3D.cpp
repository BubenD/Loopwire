#include "geometry/PartModel3D.h"

#ifdef EDRAW_HAVE_OCCT
#include "geometry/StepOcct.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

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
};

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
        if (error) *error = QStringLiteral("not a part model made in Edraw");
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
    }
    return {};
}

V3 bodyPlace(const Body3D& b, const V3& local) {
    return b.at + turn({local.x * b.scale.x, local.y * b.scale.y, local.z * b.scale.z}, b.turnDeg);
}

Mesh3D bodyMesh(const Body3D& b) { return placed(b, bodyLocalMesh(b)); }

std::vector<ModelItem3D> modelItems(const PartModel3D& model, QString* note) {
    std::vector<ModelItem3D> out;
    bool anyCut = false;
    for (const Body3D& b : model.bodies) anyCut = anyCut || (b.cut && b.shown && bodyIsPrimitive(b.shape));
#ifdef EDRAW_HAVE_OCCT
    std::vector<OcctSolid> cuts;
    auto solidOf = [](const Body3D& b) {
        OcctSolid s;
        s.shape = static_cast<int>(b.shape);
        s.a = b.a;
        s.b = b.b;
        s.c = b.c;
        // The turn and the scale as a matrix (each column where an axis goes), and where it is put.
        const V3 ex = turn({b.scale.x, 0, 0}, b.turnDeg), ey = turn({0, b.scale.y, 0}, b.turnDeg), ez = turn({0, 0, b.scale.z}, b.turnDeg);
        const double m[3][4] = {{ex.x, ey.x, ez.x, b.at.x}, {ex.y, ey.y, ez.y, b.at.y}, {ex.z, ey.z, ez.z, b.at.z}};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) s.m[r][c] = m[r][c];
        return s;
    };
    if (anyCut)
        for (const Body3D& b : model.bodies)
            if (b.cut && b.shown && bodyIsPrimitive(b.shape)) cuts.push_back(solidOf(b));
#endif
    for (const Body3D& b : model.bodies) {
        if (!b.shown) continue;
        if (b.cut) {
#ifdef EDRAW_HAVE_OCCT
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
#ifdef EDRAW_HAVE_OCCT
        if (anyCut && bodyIsPrimitive(b.shape)) {
            QString why;
            if (!occtCutSolid(solidOf(b), cuts, mesh, &why)) {
                mesh = bodyMesh(b);
                if (note) *note = why;
            }
        }
#endif
        if (mesh.empty()) mesh = bodyMesh(b);
        out.push_back({std::move(mesh), b.color, b.texture, b.id, 1.0});
    }
#ifndef EDRAW_HAVE_OCCT
    if (anyCut && note) *note = QStringLiteral("Cutting needs Open CASCADE, which this build has not got - the cuts are shown "
                                               "in red, and not taken out");
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

} // namespace Edraw_Desktop

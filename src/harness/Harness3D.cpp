#include "harness/Harness3D.h"

#include "harness/Harness.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

struct Cached {
    QDateTime when;
    Mesh3D mesh;
    QString how;
};

V3 at3(const Vec2& p) { return {p.x, p.y, 0.0}; }

double pathLength(const std::vector<Vec2>& path) {
    double len = 0.0;
    for (size_t i = 1; i < path.size(); ++i) len += distance(path[i - 1], path[i]);
    return len;
}

// The stretch of a path from `s0` to `s1` along it, its corners kept.
std::vector<V3> stretch(const std::vector<Vec2>& path, double s0, double s1) {
    std::vector<V3> out;
    if (path.size() < 2 || s1 <= s0) return out;
    double acc = 0.0;
    for (size_t i = 1; i < path.size(); ++i) {
        const double seg = distance(path[i - 1], path[i]);
        if (seg < 1e-12) continue;
        const double a = acc, b = acc + seg;
        auto pointAt = [&](double s) { return at3(path[i - 1] + (path[i] - path[i - 1]) * ((s - a) / seg)); };
        if (b >= s0 && a <= s1) {
            if (out.empty()) out.push_back(pointAt(std::max(a, s0)));
            out.push_back(pointAt(std::min(b, s1)));
        }
        acc = b;
        if (acc > s1) break;
    }
    return out;
}

// THE WIRES' COLOURS, as a harness is often made up.
QColor wireColour(int k) {
    static const QColor c[] = {QColor(220, 60, 50), QColor(235, 235, 235), QColor(40, 40, 40), QColor(60, 110, 220),
                               QColor(60, 170, 80), QColor(230, 200, 50), QColor(150, 90, 50), QColor(240, 140, 40)};
    return c[k % 8];
}

} // namespace

const Mesh3D& cachedMesh3D(const QString& path, QString* how) {
    static QHash<QString, Cached> cache;
    const QDateTime when = QFileInfo(path).lastModified();
    auto it = cache.find(path);
    if (it == cache.end() || it->when != when) {
        Cached c;
        c.when = when;
        c.mesh = loadMesh3D(path, &c.how);
        it = cache.insert(path, c);
    }
    if (how) *how = it->how;
    return it->mesh;
}

namespace {
// The points of an object's drawing, close enough together to read its shape from.
void sampleInto(const Entity& e, std::vector<Vec2>& pts) {
    auto seg = [&](const Vec2& a, const Vec2& b) {
        const int n = std::clamp(static_cast<int>(distance(a, b) / 0.5), 1, 200);
        for (int k = 0; k <= n; ++k) pts.push_back(a + (b - a) * (static_cast<double>(k) / n));
    };
    auto poly = [&](const std::vector<Vec2>& p, bool closed) {
        for (size_t i = 1; i < p.size(); ++i) seg(p[i - 1], p[i]);
        if (closed && p.size() > 2) seg(p.back(), p.front());
    };
    switch (e.type) {
        case EntityType::Line: seg(e.a, e.b); break;
        case EntityType::Polyline:
        case EntityType::Spline: poly(e.points, e.meta.value(QStringLiteral("closed")).toBool()); break;
        case EntityType::Rect:
        case EntityType::Polygon:
        case EntityType::Hatch: poly(e.points, true); break;
        case EntityType::Circle:
            for (int k = 0; k <= 48; ++k) {
                const double a = 2.0 * M_PI * k / 48.0;
                pts.push_back(e.center + Vec2{std::cos(a), std::sin(a)} * e.radius);
            }
            break;
        default: {
            const Bounds b = e.bounds();
            if (b.valid) poly({b.min, Vec2{b.max.x, b.min.y}, b.max, Vec2{b.min.x, b.max.y}}, true);
            break;
        }
    }
}

// A SOLID FROM A PART'S 2D DRAWING. Along the way it goes in (`A`, from `lo` to `hi` about `c`), how far its outline
// reaches either side (`B`) at each step - turned round A (a round part), or given `depth` up and down (a square one).
Mesh3D solidFromDrawing(const std::vector<Vec2>& pts, const Vec2& c, const V3& A, const V3& B, double lo, double hi,
                        bool revolve, double depth) {
    const int n = 40;
    std::vector<double> top(n + 1, -1e300), bot(n + 1, 1e300);
    const Vec2 a2{A.x, A.y}, b2{B.x, B.y};
    for (const Vec2& p : pts) {
        const double t = dot(p - c, a2), d = dot(p - c, b2);
        const int i = std::clamp(static_cast<int>(std::lround((t - lo) / std::max(1e-9, hi - lo) * n)), 0, n);
        top[static_cast<size_t>(i)] = std::max(top[static_cast<size_t>(i)], d);
        bot[static_cast<size_t>(i)] = std::min(bot[static_cast<size_t>(i)], d);
    }
    // A step with nothing drawn across it takes after its neighbours.
    for (int i = 0; i <= n; ++i) {
        if (top[static_cast<size_t>(i)] > -1e299) continue;
        int l = i - 1, r = i + 1;
        while (l >= 0 && top[static_cast<size_t>(l)] < -1e299) --l;
        while (r <= n && top[static_cast<size_t>(r)] < -1e299) ++r;
        if (l < 0 && r > n) return {};
        const int from = l >= 0 ? l : r, to = r <= n ? r : l;
        const double u = to == from ? 0.0 : static_cast<double>(i - from) / (to - from);
        top[static_cast<size_t>(i)] = top[static_cast<size_t>(from)] + (top[static_cast<size_t>(to)] - top[static_cast<size_t>(from)]) * u;
        bot[static_cast<size_t>(i)] = bot[static_cast<size_t>(from)] + (bot[static_cast<size_t>(to)] - bot[static_cast<size_t>(from)]) * u;
    }
    const V3 centre{c.x, c.y, 0.0}, Z{0.0, 0.0, 1.0};
    Mesh3D m;
    const int sides = revolve ? 28 : 4;
    for (int i = 0; i <= n; ++i) {
        const double t = lo + (hi - lo) * i / n;
        const V3 at = centre + A * t;
        if (revolve) {
            const double r = std::max(0.2, std::max(std::abs(top[static_cast<size_t>(i)]), std::abs(bot[static_cast<size_t>(i)])));
            for (int k = 0; k < sides; ++k) {
                const double ang = 2.0 * M_PI * k / sides;
                m.addVertex(at + B * (r * std::cos(ang)) + Z * (r * std::sin(ang)));
            }
        } else {
            const double up = top[static_cast<size_t>(i)], dn = std::min(bot[static_cast<size_t>(i)], up - 0.2);
            m.addVertex(at + B * up + Z * (depth / 2.0));
            m.addVertex(at + B * up - Z * (depth / 2.0));
            m.addVertex(at + B * dn - Z * (depth / 2.0));
            m.addVertex(at + B * dn + Z * (depth / 2.0));
        }
    }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < sides; ++k) {
            const int a = i * sides + k, b = i * sides + (k + 1) % sides, cc = a + sides, d = b + sides;
            m.addTri(a, b, d);
            m.addTri(a, d, cc);
        }
    // Its two ends closed.
    const int e0 = m.addVertex(centre + A * lo), e1 = m.addVertex(centre + A * hi);
    for (int k = 0; k < sides; ++k) {
        m.addTri(e0, (k + 1) % sides, k);
        m.addTri(e1, n * sides + k, n * sides + (k + 1) % sides);
    }
    return m;
}
} // namespace

Harness3DScene buildHarness3D(const std::vector<Entity>& ents, const std::function<QString(const QString&)>& modelOf,
                              PartLook look) {
    Harness3DScene scene;
    // ---- THE RUNS ----------------------------------------------------------------------------------
    std::vector<Vec2> runEnds;
    QHash<QString, bool> isRun;   // a group (copy mark or brick) with a run in it is a run, not a part
    auto groupOf = [](const Entity& e) {
        const QString copy = e.meta.value(QStringLiteral("partCopy")).toString();
        return !copy.isEmpty() ? copy : e.meta.value(QStringLiteral("brickId")).toString();
    };
    auto brickOf = [](const Entity& e) {
        const QString id = e.meta.value(QStringLiteral("brickId")).toString();
        return !id.isEmpty() ? id : e.meta.value(QStringLiteral("harnessBrick")).toString();
    };
    // A wire's colour: the one the run carries for it, else the general colours.
    auto colourOf = [](const Entity& e, int k) {
        const QJsonArray said = e.meta.value(QStringLiteral("wireColours")).toArray();
        const QColor c(k < said.size() ? said.at(k).toString() : QString());
        return c.isValid() ? c : wireColour(k);
    };
    const size_t firstRunItem = scene.items.size();
    (void)firstRunItem;
    for (const Entity& e : ents) {
        // A RUN OF ONE WIRE is drawn as a wire - a double line, not a bundle: one tube along it. A HEATSHRINK SLEEVE drawn
        // on the sheet is one too: a black tube as wide as it is drawn.
        if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool() && e.points.size() >= 2) {
            const bool sleeve = e.meta.value(QStringLiteral("shrunkEnds")).toBool();
            if (!groupOf(e).isEmpty()) isRun.insert(groupOf(e), true);
            if (!sleeve) {
                ++scene.runs;
                runEnds.push_back(e.points.front());
                runEnds.push_back(e.points.back());
            }
            std::vector<V3> path;
            for (const Vec2& p : e.points) path.push_back(at3(p));
            const double across = sleeve ? e.meta.value(QStringLiteral("shrinkWidth")).toDouble(e.meta.value(QStringLiteral("gap")).toDouble(4.0))
                                         : e.meta.value(QStringLiteral("gap")).toDouble(1.0);
            Harness3DItem wi;
            wi.mesh = tubeMesh(path, std::max(0.2, across) / 2.0, sleeve ? 18 : 8, true);
            wi.color = sleeve ? QColor(32, 32, 35) : colourOf(e, 0);
            wi.what = sleeve ? QStringLiteral("sleeve") : QStringLiteral("wire");
            wi.brick = brickOf(e);
            scene.items.push_back(std::move(wi));
            continue;
        }
        if (e.type != EntityType::TwistedPair || e.points.size() < 2) continue;
        isRun.insert(groupOf(e), true);
        ++scene.runs;
        runEnds.push_back(e.points.front());
        runEnds.push_back(e.points.back());
        const std::vector<Vec2>& route = e.points;
        const double total = pathLength(route);
        const double ww = std::max(0.2, e.meta.value(QStringLiteral("twistWireWidth")).toDouble(1.0));
        const QString body = e.meta.value(QStringLiteral("twistBody")).toString();
        const double bodyW = e.meta.value(QStringLiteral("twistBodyWidth")).toDouble(0.0);
        const double fanA = std::max(0.0, e.meta.value(QStringLiteral("twistEnds")).toDouble(0.0));
        const double fanB = std::max(0.0, e.meta.value(QStringLiteral("twistEndsFinish")).toDouble(fanA));
        const bool covered = !body.isEmpty() && bodyW > 1e-6;
        // ITS BODY: a cable, or heatshrink over the wires, between where it fans out at each end.
        if (covered && total > fanA + fanB + 0.5) {
            Harness3DItem it;
            it.mesh = tubeMesh(stretch(route, fanA, total - fanB), bodyW / 2.0, 18, true);
            it.color = body == QLatin1String("heatshrink") ? QColor(42, 42, 46) : QColor(70, 74, 82);
            it.what = QStringLiteral("run");
            it.brick = brickOf(e);
            scene.items.push_back(std::move(it));
            // ITS TRANSITION SLEEVES, on the body at the ends it says.
            const QString sides = e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
            const double bootA = e.meta.value(QStringLiteral("twistBoot")).toDouble(0.0);
            const double bootB = e.meta.value(QStringLiteral("twistBootFinish")).toDouble(bootA);
            const double wA = e.meta.value(QStringLiteral("twistBootWidth")).toDouble(bodyW * 1.25);
            const double wB = e.meta.value(QStringLiteral("twistBootWidthFinish")).toDouble(wA);
            auto sleeve = [&](double s0, double s1, double width) {
                if (s1 - s0 < 0.2) return;
                Harness3DItem sl;
                sl.mesh = tubeMesh(stretch(route, s0, s1), std::max(width, bodyW * 1.05) / 2.0, 18, true);
                sl.color = QColor(28, 28, 30);
                sl.what = QStringLiteral("transition");
                sl.brick = brickOf(e);
                scene.items.push_back(std::move(sl));
            };
            if (sides == QLatin1String("both") || sides == QLatin1String("start")) sleeve(fanA, fanA + bootA, wA);
            if (sides == QLatin1String("both") || sides == QLatin1String("finish"))
                sleeve(total - fanB - bootB, total - fanB, wB);
        }
        // A TWISTED PAIR OR BUNDLE: its wires laid up round one another for real - each a helix round the run's middle,
        // a whole turn every pitch, the wires evenly round it and touching - not the flat crossings it is drawn with.
        // Twisted unless it is said to be straight - as the drawing reads it, so a twisted pair drawn on the sheet is
        // twisted here too.
        if (!e.meta.value(QStringLiteral("twistStraight")).toBool(false)) {
            const int n = std::max(2, e.meta.value(QStringLiteral("twistWires")).toInt(2));
            const double pitch = std::max(2.0, e.meta.value(QStringLiteral("twistPitch")).toDouble(18.0));
            // Each wire as thick as it is said - or, drawn on the sheet by its overall width, as thick as the wires
            // laid round one another that wide.
            double wire = ww;
            if (!e.meta.contains(QStringLiteral("twistWireWidth")) && e.meta.value(QStringLiteral("twistWidth")).toDouble() > 0.0)
                wire = e.meta.value(QStringLiteral("twistWidth")).toDouble() / (1.0 / std::sin(M_PI / n) + 1.0);
            const double rad = wire / (2.0 * std::sin(M_PI / n));
            // STRAIGHT AT ITS ENDS for as long as the drawing has them straight, twisted between.
            const double ends = std::clamp(e.meta.value(QStringLiteral("twistEnds")).toDouble(0.0), 0.0, total * 0.45);
            const double step = std::min(pitch / 18.0, std::max(0.2, total / 400.0));
            // The run's middle, square across it in the sheet, and up out of it.
            auto frameAt = [&](double s, V3& at, V3& across) {
                double acc = 0.0;
                for (size_t i = 1; i < route.size(); ++i) {
                    const double seg = distance(route[i - 1], route[i]);
                    if (seg < 1e-12) continue;
                    if (acc + seg >= s || i + 1 == route.size()) {
                        const double u = std::clamp((s - acc) / seg, 0.0, 1.0);
                        const Vec2 p = route[i - 1] + (route[i] - route[i - 1]) * u;
                        const Vec2 t = (route[i] - route[i - 1]).normalized();
                        at = at3(p);
                        across = V3{-t.y, t.x, 0.0};
                        return;
                    }
                    acc += seg;
                }
                at = at3(route.back());
                across = V3{0.0, 1.0, 0.0};
            };
            for (int k = 0; k < n; ++k) {
                std::vector<V3> helix;
                for (double s = 0.0;; s += step) {
                    const double ss = std::min(s, total);
                    V3 at, across;
                    frameAt(ss, at, across);
                    const double twisted = std::clamp(ss - ends, 0.0, std::max(0.0, total - 2.0 * ends));
                    const double th = 2.0 * M_PI * (twisted / pitch + static_cast<double>(k) / n);
                    helix.push_back(at + across * (rad * std::cos(th)) + V3{0.0, 0.0, 1.0} * (rad * std::sin(th)));
                    if (ss >= total) break;
                }
                Harness3DItem wi;
                wi.mesh = tubeMesh(helix, wire / 2.0, 8, true);
                wi.color = colourOf(e, k);
                wi.what = QStringLiteral("wire");
                wi.brick = brickOf(e);
                scene.items.push_back(std::move(wi));
            }
            continue;
        }
        // ITS WIRES: each one round, where it fans out (the whole way, with no body over them).
        const std::vector<std::vector<Vec2>> strands = twistedPairStrands(e);
        const int wires = std::max(1, e.meta.value(QStringLiteral("twistWires")).toInt(2));
        int k = 0;
        for (size_t j = 0; j + 1 < strands.size() && k < wires; j += 2, ++k) {
            const auto& ea = strands[j];
            const auto& eb = strands[j + 1];
            if (ea.size() != eb.size() || ea.size() < 2) continue;
            std::vector<Vec2> c(ea.size());
            for (size_t q = 0; q < ea.size(); ++q) c[q] = (ea[q] + eb[q]) * 0.5;
            const double len = pathLength(c);
            auto wire = [&](double s0, double s1) {
                const std::vector<V3> piece = stretch(c, s0, s1);
                if (piece.size() < 2) return;
                Harness3DItem wi;
                wi.mesh = tubeMesh(piece, ww / 2.0, 8, false);
                wi.color = colourOf(e, k);
                wi.what = QStringLiteral("wire");
                wi.brick = brickOf(e);
                scene.items.push_back(std::move(wi));
            };
            if (!covered) wire(0.0, len);
            else {
                // A little into the body, so the wires are seen to go into it.
                wire(0.0, std::min(len, fanA + 1.0));
                wire(std::max(0.0, len - fanB - 1.0), len);
            }
        }
    }
    // ---- THE PARTS ---------------------------------------------------------------------------------
    struct Part {
        Bounds box;
        QString lib, name, brick;
        std::vector<Vec2> outline;   // the points of its drawing, for a solid made from it
    };
    QHash<QString, Part> parts;
    QStringList order;
    for (const Entity& e : ents) {
        const QString g = groupOf(e);
        if (g.isEmpty() || isRun.value(g) || e.type == EntityType::TwistedPair) continue;
        // A run's own marks (a stripped end, a label) are the run's, not a part.
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("strip")) continue;
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        if (role == QLatin1String("designator")) {
            parts[g].name = e.meta.value(QStringLiteral("text")).toString();
            continue;
        }
        if (e.type == EntityType::Text || e.type == EntityType::Dimension || e.type == EntityType::Wipeout ||
            role == QLatin1String("balloon") || role == QLatin1String("dim") || e.meta.value(QStringLiteral("previewOnly")).toBool())
            continue;
        if (!parts.contains(g)) order << g;
        Part& p = parts[g];
        p.box.expand(e.bounds());
        if (look != PartLook::Box) sampleInto(e, p.outline);
        if (p.lib.isEmpty()) p.lib = e.meta.value(QStringLiteral("libPart")).toString();
        if (p.name.isEmpty()) p.name = e.meta.value(QStringLiteral("partName")).toString();
        if (p.brick.isEmpty()) p.brick = brickOf(e);
    }
    for (const QString& g : order) {
        const Part& p = parts.value(g);
        if (!p.box.valid) continue;
        ++scene.parts;
        const Vec2 c{(p.box.min.x + p.box.max.x) / 2.0, (p.box.min.y + p.box.max.y) / 2.0};
        // WHICH WAY IT GOES INTO THE HARNESS: from the end of the run nearest it, in towards its middle.
        Vec2 axis{1.0, 0.0};
        double best = 1e300;
        for (const Vec2& v : runEnds) {
            const double dx = std::max({p.box.min.x - v.x, 0.0, v.x - p.box.max.x});
            const double dy = std::max({p.box.min.y - v.y, 0.0, v.y - p.box.max.y});
            const double d = std::hypot(dx, dy);
            if (d < best && distance(v, c) > 1e-6) {
                best = d;
                axis = (c - v).normalized();
            }
        }
        // Snapped to the sheet's axes where it is nearly along one: a part is drawn square to them.
        if (std::abs(axis.x) > 0.92) axis = {axis.x > 0 ? 1.0 : -1.0, 0.0};
        else if (std::abs(axis.y) > 0.92) axis = {0.0, axis.y > 0 ? 1.0 : -1.0};
        const Vec2 across{-axis.y, axis.x};
        // Its length along that way and its width across, as drawn.
        double lo = 1e300, hi = -1e300, wlo = 1e300, whi = -1e300;
        for (const Vec2& q : {p.box.min, p.box.max, Vec2{p.box.min.x, p.box.max.y}, Vec2{p.box.max.x, p.box.min.y}}) {
            lo = std::min(lo, dot(q - c, axis));
            hi = std::max(hi, dot(q - c, axis));
            wlo = std::min(wlo, dot(q - c, across));
            whi = std::max(whi, dot(q - c, across));
        }
        const double length = std::max(1.0, hi - lo), width = std::max(1.0, whi - wlo);
        const V3 A{axis.x, axis.y, 0.0}, B{across.x, across.y, 0.0}, Z{0.0, 0.0, 1.0};
        const V3 centre{c.x, c.y, 0.0};
        Harness3DItem it;
        it.what = !p.name.isEmpty() ? p.name : p.lib;
        it.brick = p.brick;
        // ITS OWN 3D MODEL, where one is attached to its library part: its longest side along the way it goes in, its
        // next across, its shortest standing up - scaled to the length it is drawn.
        const QString file = modelOf && !p.lib.isEmpty() ? modelOf(p.lib) : QString();
        QString how;
        const Mesh3D* model = file.isEmpty() ? nullptr : &cachedMesh3D(file, &how);
        V3 mlo, mhi;
        if (model && !model->empty() && model->bounds(mlo, mhi)) {
            const V3 ext = mhi - mlo, mid = (mlo + mhi) * 0.5;
            const double e[3] = {ext.x, ext.y, ext.z};
            int ord[3] = {0, 1, 2};
            std::sort(ord, ord + 3, [&](int a, int b) { return e[a] > e[b]; });
            const double k = length / std::max(1e-9, e[ord[0]]);
            const V3 to[3] = {A, B, Z};
            it.mesh = *model;
            for (V3& v : it.mesh.v) {
                const double m[3] = {v.x - mid.x, v.y - mid.y, v.z - mid.z};
                V3 out = centre;
                for (int r = 0; r < 3; ++r) out = out + to[r] * (m[ord[r]] * k);
                v = out;
            }
            it.color = QColor(176, 182, 192);
            it.model = true;
            ++scene.modelled;
        } else {
            if (!file.isEmpty())
                scene.notes << QStringLiteral("%1: its 3D model could not be read (%2) - shown from its drawing")
                                   .arg(it.what.isEmpty() ? QStringLiteral("a part") : it.what, how);
            // NONE: A SOLID FROM ITS 2D DRAWING, when that is asked for - its outline turned round the way it goes
            // in, or given its width in depth.
            if (look != PartLook::Box && !p.outline.empty()) {
                it.mesh = solidFromDrawing(p.outline, c, A, B, lo, hi, look == PartLook::Revolved, width);
                it.color = QColor(150, 158, 170);
            }
            // OR A BOX as long and as wide as it is drawn, and as tall as it is wide.
            if (it.mesh.empty()) {
                const V3 corner = centre - A * (length / 2.0) - B * (width / 2.0) - Z * (width / 2.0);
                Mesh3D box = boxMesh({0, 0, 0}, {1, 1, 1});
                for (V3& v : box.v) v = corner + A * (v.x * length) + B * (v.y * width) + Z * (v.z * width);
                it.mesh = box;
                it.color = QColor(92, 128, 176);
                it.cube = true;
            }
        }
        scene.items.push_back(std::move(it));
    }
    return scene;
}

} // namespace Edraw_Desktop

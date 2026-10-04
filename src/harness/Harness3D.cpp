#include "harness/Harness3D.h"

#include "harness/Harness.h"
#include "harness/PartsLibrary.h"

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

PartPins partPinsOf(const LibraryPart* p) {
    PartPins pins;
    if (!p || p->kind != PartKind::Connector) return pins;
    pins.count = std::clamp(p->wayCount(), 1, 400);
    const QString said = (p->value(QStringLiteral("gender")) + QLatin1Char(' ') + p->value(QStringLiteral("mounting"))).toLower();
    pins.female = said.contains(QLatin1String("female")) || said.contains(QLatin1String("receptacle")) ||
                  said.contains(QLatin1String("socket"));
    return pins;
}

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
                        bool revolve, double depth, double* faceRadius) {
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
    // A LINE OR TWO STANDING OUT ON THEIR OWN - a mark at the part's very end, a tick - is not its outline: each step
    // taken as the middle of itself and its two neighbours, so a lone spike does not turn into a disc.
    auto median3 = [n](std::vector<double>& v) {
        std::vector<double> out = v;
        for (int i = 0; i <= n; ++i) {
            const int a = std::max(0, i - 1), b = i, cc = std::min(n, i + 1);
            double t[3] = {v[static_cast<size_t>(a)], v[static_cast<size_t>(b)], v[static_cast<size_t>(cc)]};
            if (i == 0 || i == n) {   // at an end: the end and its next two
                const int j = i == 0 ? 1 : n - 1, k = i == 0 ? std::min(n, 2) : std::max(0, n - 2);
                t[0] = v[static_cast<size_t>(i)];
                t[1] = v[static_cast<size_t>(j)];
                t[2] = v[static_cast<size_t>(k)];
            }
            std::sort(t, t + 3);
            out[static_cast<size_t>(i)] = t[1];
        }
        v = out;
    };
    median3(top);
    median3(bot);
    if (faceRadius) *faceRadius = std::max(0.2, std::max(std::abs(top[static_cast<size_t>(n)]), std::abs(bot[static_cast<size_t>(n)])));
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
                              PartLook look, const std::function<PartPins(const QString&)>& pinsOf) {
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
    // A run's route: its points - or, drawn by its two ends alone (a twisted pair clicked end to end), those two.
    auto routeOf = [](const Entity& e) {
        if (e.points.size() >= 2) return e.points;
        return std::vector<Vec2>{e.a, e.b};
    };
    // A SLEEVE OVER THE END OF A PATH: so long, a little over the wire.
    auto endSleeve = [&](const std::vector<V3>& path, bool atEnd, double len, double radius, const QString& brick) {
        if (path.size() < 2 || len <= 1e-6) return;
        std::vector<V3> p = path;
        if (atEnd) std::reverse(p.begin(), p.end());
        std::vector<V3> piece{p.front()};
        double acc = 0.0;
        for (size_t i = 1; i < p.size() && acc < len; ++i) {
            const double seg = (p[i] - p[i - 1]).length();
            if (acc + seg >= len) {
                piece.push_back(p[i - 1] + (p[i] - p[i - 1]) * ((len - acc) / std::max(1e-12, seg)));
                acc = len;
            } else {
                piece.push_back(p[i]);
                acc += seg;
            }
        }
        Harness3DItem sl;
        sl.mesh = tubeMesh(piece, radius, 10, true);
        sl.color = QColor(36, 36, 40);
        sl.what = QStringLiteral("sleeve");
        sl.brick = brick;
        scene.items.push_back(std::move(sl));
    };
    for (const Entity& e : ents) {
        // A SLEEVE OVER A WIRE'S END, as the harness wizard draws one (its outline round the wire): a tube round the wire.
        if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fanShrink") &&
            e.points.size() >= 5) {
            const size_t half = (e.points.size() - 1) / 2;
            std::vector<V3> mid;
            double across = 0.0;
            for (size_t k = 0; k < half; ++k) {
                const Vec2 l = e.points[k], r = e.points[2 * half - 1 - k];
                mid.push_back(at3((l + r) * 0.5));
                across = std::max(across, distance(l, r));
            }
            Harness3DItem sl;
            sl.mesh = tubeMesh(mid, std::max(0.2, across / 2.0), 10, true);
            sl.color = QColor(36, 36, 40);
            sl.what = QStringLiteral("sleeve");
            sl.brick = brickOf(e);
            scene.items.push_back(std::move(sl));
            continue;
        }
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
            if (sleeve) {
                // ITS ENDS SHRUNK DOWN onto what it grips: necked to its face over its taper at each end - or, over a
                // cable's ends only, the cable whole and a sleeve at each end; a transition square at its body end.
                const double R = std::max(0.2, across) / 2.0;
                double L = 0.0;
                for (size_t i = 1; i < path.size(); ++i) L += (path[i] - path[i - 1]).length();
                const double faceS = e.meta.value(QStringLiteral("shrinkFaceStart")).toDouble(across * 0.82) / 2.0;
                const double faceE = e.meta.value(QStringLiteral("shrinkFaceEnd")).toDouble(across * 0.82) / 2.0;
                const double tapS = std::clamp(e.meta.value(QStringLiteral("shrinkTaperStart")).toDouble(4.0), 0.0, L * 0.45);
                const double tapE = std::clamp(e.meta.value(QStringLiteral("shrinkTaperEnd")).toDouble(4.0), 0.0, L * 0.45);
                auto ease = [](double from, double to, double u) {
                    u = std::clamp(u, 0.0, 1.0);
                    return from + (to - from) * u * u * (3.0 - 2.0 * u);
                };
                // Points enough along it for the tapers to be smooth.
                std::vector<V3> fine;
                const double step = std::max(0.2, std::min({tapS > 0 ? tapS / 6.0 : 1e9, tapE > 0 ? tapE / 6.0 : 1e9, L / 60.0}));
                for (double s = 0.0;; s += step) {
                    const double ss = std::min(s, L);
                    double acc = 0.0;
                    V3 p = path.back();
                    for (size_t i = 1; i < path.size(); ++i) {
                        const double seg = (path[i] - path[i - 1]).length();
                        if (acc + seg >= ss) {
                            p = path[i - 1] + (path[i] - path[i - 1]) * ((ss - acc) / std::max(1e-12, seg));
                            break;
                        }
                        acc += seg;
                    }
                    fine.push_back(p);
                    if (ss >= L) break;
                }
                if (e.meta.value(QStringLiteral("shrinkEndsOnly")).toBool()) {
                    const double cable = e.meta.value(QStringLiteral("shrinkCableWidth")).toDouble(across * 0.82) / 2.0;
                    const double coverS = e.meta.value(QStringLiteral("shrinkCoverStart")).toDouble(20.0);
                    const double coverE = e.meta.value(QStringLiteral("shrinkCoverEnd")).toDouble(20.0);
                    Harness3DItem cab;
                    cab.mesh = tubeMesh(fine, cable, 16, true);
                    cab.color = QColor(70, 74, 82);
                    cab.what = QStringLiteral("run");
                    cab.brick = brickOf(e);
                    scene.items.push_back(std::move(cab));
                    wi.mesh = tubeMeshVar(fine, [&](double s) {
                        if (s > coverS && s < L - coverE) return 1e-4;   // no sleeve between the two ends
                        return s <= coverS ? ease(faceS, R, s / std::max(1e-9, tapS)) : ease(faceE, R, (L - s) / std::max(1e-9, tapE));
                    }, 18, true);
                } else {
                    const bool transition = e.meta.value(QStringLiteral("shrinkTransition")).toBool();
                    wi.mesh = tubeMeshVar(fine, [&](double s) {
                        if (!transition && tapS > 1e-9 && s < tapS) return ease(faceS, R, s / tapS);
                        if (tapE > 1e-9 && s > L - tapE) return ease(faceE, R, (L - s) / tapE);
                        return R;
                    }, 18, true);
                }
            } else {
                wi.mesh = tubeMesh(path, std::max(0.2, across) / 2.0, 8, true);
            }
            wi.color = sleeve ? QColor(32, 32, 35) : colourOf(e, 0);
            wi.what = sleeve ? QStringLiteral("sleeve") : QStringLiteral("wire");
            wi.brick = brickOf(e);
            scene.items.push_back(std::move(wi));
            continue;
        }
        if (e.type != EntityType::TwistedPair) continue;
        const std::vector<Vec2> route = routeOf(e);
        if (route.size() < 2 || distance(route.front(), route.back()) + (route.size() > 2 ? 1.0 : 0.0) < 1e-6) continue;
        isRun.insert(groupOf(e), true);
        ++scene.runs;
        runEnds.push_back(route.front());
        runEnds.push_back(route.back());
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
        // WHICH ENDS FAN: as the drawing says - both, unless one is.
        const QString spreadSaid = e.meta.value(QStringLiteral("twistSpread")).toString();
        const bool spreadStart = spreadSaid != QLatin1String("finish") && spreadSaid != QLatin1String("right");
        const bool spreadFinish = spreadSaid != QLatin1String("start") && spreadSaid != QLatin1String("left");
        const bool endShrink = e.meta.value(QStringLiteral("twistEndShrink")).toBool();
        const double endShrinkLen = std::max(0.5, e.meta.value(QStringLiteral("twistEndShrinkLen")).toDouble(10.0));
        // A FAN OF BARE WIRES: each wire where the drawing has it across the fan - the pitch it is drawn at - and gathered
        // into a round bundle where the drawing converges them, every wire its own place in it (none on top of another).
        if (!covered) {
            const std::vector<std::vector<Vec2>> lanes = twistedPairStrands(e);
            const int wires = std::max(1, e.meta.value(QStringLiteral("twistWires")).toInt(2));
            // How far along the route a point is, and how far to one side of it.
            auto project = [&](const Vec2& p, double& s, double& d) {
                double best = 1e300, acc = 0.0;
                for (size_t i = 1; i < route.size(); ++i) {
                    const Vec2 a = route[i - 1], b = route[i];
                    const double seg = distance(a, b);
                    if (seg < 1e-12) continue;
                    const Vec2 t = (b - a) / seg;
                    const double u = std::clamp(dot(p - a, t), 0.0, seg);
                    const Vec2 foot = a + t * u;
                    const double dist = distance(p, foot);
                    if (dist < best) {
                        best = dist;
                        s = acc + u;
                        d = dot(p - foot, Vec2{-t.y, t.x});
                    }
                    acc += seg;
                }
            };
            auto frameAt = [&](double s, V3& at, V3& across) {
                double acc = 0.0;
                for (size_t i = 1; i < route.size(); ++i) {
                    const double seg = distance(route[i - 1], route[i]);
                    if (seg < 1e-12) continue;
                    if (acc + seg >= s || i + 1 == route.size()) {
                        const double u = std::clamp((s - acc) / seg, 0.0, 1.0);
                        const Vec2 t = (route[i] - route[i - 1]).normalized();
                        at = at3(route[i - 1] + (route[i] - route[i - 1]) * u);
                        across = V3{-t.y, t.x, 0.0};
                        return;
                    }
                    acc += seg;
                }
                at = at3(route.back());
                across = V3{0.0, 1.0, 0.0};
            };
            // Each wire's place in the bundle: rings round its middle, one wire across apart.
            auto packed = [&](int k, double& py, double& pz) {
                if (wires == 1 || k == 0) {
                    py = pz = 0.0;
                    if (wires == 2) py = (k == 0 ? -0.5 : 0.5) * ww;
                    if (wires == 2 || k == 0) return;
                }
                int ring = 1, first = 1;
                while (k >= first + 6 * ring) {
                    first += 6 * ring;
                    ++ring;
                }
                const int inRing = std::min(6 * ring, wires - first);
                const double ang = 2.0 * M_PI * (k - first) / std::max(1, inRing);
                py = ring * ww * std::cos(ang);
                pz = ring * ww * std::sin(ang);
                if (wires == 2) py = (k == 0 ? -0.5 : 0.5) * ww, pz = 0.0;
            };
            auto smooth = [](double u) {
                u = std::clamp(u, 0.0, 1.0);
                return u * u * (3.0 - 2.0 * u);
            };
            const double step = std::clamp(total / 300.0, 0.15, 1.0);
            int k = 0;
            for (size_t j = 0; j + 1 < lanes.size() && k < wires; j += 2, ++k) {
                const auto& ea = lanes[j];
                const auto& eb = lanes[j + 1];
                if (ea.size() != eb.size() || ea.size() < 2) continue;
                // Where the drawing has this wire across the run, along it.
                std::vector<std::pair<double, double>> sd;
                for (size_t q = 0; q < ea.size(); ++q) {
                    double s = 0.0, d = 0.0;
                    project((ea[q] + eb[q]) * 0.5, s, d);
                    sd.push_back({s, d});
                }
                std::sort(sd.begin(), sd.end());
                auto drawnAt = [&](double s, bool& known) {
                    known = !sd.empty() && s >= sd.front().first - 0.5 && s <= sd.back().first + 0.5;
                    if (sd.empty()) return 0.0;
                    if (s <= sd.front().first) return sd.front().second;
                    if (s >= sd.back().first) return sd.back().second;
                    const auto it = std::lower_bound(sd.begin(), sd.end(), std::make_pair(s, -1e300));
                    const auto& b = *it;
                    const auto& a = *(it - 1);
                    const double u = b.first - a.first > 1e-9 ? (s - a.first) / (b.first - a.first) : 0.0;
                    return a.second + (b.second - a.second) * u;
                };
                double py = 0.0, pz = 0.0;
                packed(k, py, pz);
                std::vector<V3> path;
                for (double s = 0.0;; s += step) {
                    const double ss = std::min(s, total);
                    bool known = false;
                    const double d = drawnAt(ss, known);
                    // Gathered: fully in the bundle between the fans, spread as drawn at a fanning end.
                    double w = 1.0;
                    if (known) {
                        if (spreadStart && fanA > 1e-6) w = std::min(w, smooth(ss / fanA));
                        if (spreadFinish && fanB > 1e-6) w = std::min(w, smooth((total - ss) / fanB));
                    }
                    V3 at, across;
                    frameAt(ss, at, across);
                    path.push_back(at + across * ((1.0 - w) * d + w * py) + V3{0.0, 0.0, 1.0} * (w * pz));
                    if (ss >= total) break;
                }
                Harness3DItem wi;
                wi.mesh = tubeMesh(path, ww / 2.0, 8, true);
                wi.color = colourOf(e, k);
                wi.what = QStringLiteral("wire");
                wi.brick = brickOf(e);
                scene.items.push_back(std::move(wi));
                // HEATSHRINK OVER EACH WIRE'S END, where the drawing has it.
                if (endShrink) {
                    if (spreadStart) endSleeve(path, false, endShrinkLen, ww / 2.0 + 0.45, brickOf(e));
                    if (spreadFinish) endSleeve(path, true, endShrinkLen, ww / 2.0 + 0.45, brickOf(e));
                }
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
            // A little into the body, so the wires are seen to go into it.
            wire(0.0, std::min(len, fanA + 1.0));
            wire(std::max(0.0, len - fanB - 1.0), len);
            if (endShrink) {
                std::vector<V3> c3;
                for (const Vec2& q : c) c3.push_back(at3(q));
                if (spreadStart) endSleeve(c3, false, endShrinkLen, ww / 2.0 + 0.45, brickOf(e));
                if (spreadFinish) endSleeve(c3, true, endShrinkLen, ww / 2.0 + 0.45, brickOf(e));
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
            double faceR = width / 2.0;
            if (look != PartLook::Box && !p.outline.empty()) {
                it.mesh = solidFromDrawing(p.outline, c, A, B, lo, hi, look == PartLook::Revolved, width, &faceR);
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
                faceR = width / 2.0;
            }
            // ITS PINOUT on its front face - the end away from the harness: male pins standing out of it, or the
            // female sockets in it - as many as it has, spread evenly over the face.
            const PartPins pins = pinsOf && !p.lib.isEmpty() ? pinsOf(p.lib) : PartPins{};
            if (pins.count > 0) {
                const V3 face = centre + A * hi;
                const double R = std::max(0.3, faceR * (look == PartLook::Revolved ? 0.78 : 0.7));
                const double pinR = std::clamp(R * std::sqrt(1.0 / pins.count) * 0.42, 0.12, R * 0.3);
                const double stand = pins.female ? 0.25 : std::max(0.8, length * 0.14);
                Harness3DItem pinsItem;
                for (int i = 0; i < pins.count; ++i) {
                    // A sunflower's spiral: as even a spread over a round face as there is, for any number.
                    const double rr = pins.count == 1 ? 0.0 : R * 0.82 * std::sqrt((i + 0.5) / pins.count);
                    const double th = i * 2.399963229728653;
                    const V3 at = face + B * (rr * std::cos(th)) + Z * (rr * std::sin(th));
                    pinsItem.mesh.append(tubeMesh({at - A * 0.05, at + A * stand}, pins.female ? pinR * 1.15 : pinR, 10, true));
                }
                pinsItem.color = pins.female ? QColor(18, 18, 20) : QColor(212, 175, 80);
                pinsItem.what = pins.female ? QStringLiteral("sockets") : QStringLiteral("pins");
                pinsItem.brick = p.brick;
                scene.items.push_back(std::move(pinsItem));
            }
        }
        scene.items.push_back(std::move(it));
    }
    return scene;
}

} // namespace Edraw_Desktop

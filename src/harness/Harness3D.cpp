#include "harness/Harness3D.h"

#include "harness/Harness.h"
#include "harness/PartsLibrary.h"
#include "render/Render3D.h"
#include "geometry/PartModel3D.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QPainter>
#include <QFontMetrics>

#include <algorithm>
#include <cmath>

namespace Loopwire {

// Print glyphs onto the curved sleeve as surface triangles. They share the cable's
// depth and orientation in both renderers, rather than floating above it on screen.
Mesh3D labelTextMesh3D(const std::vector<V3>& path, double radius, const QString& text, double textHeight, const V3& facing) {
    Mesh3D mesh;
    if (path.size() < 2 || text.isEmpty()) return mesh;
    QFont font; font.setPixelSize(32);
    QFontMetrics fm(font);
    int width = 1;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const auto& line : lines) width = std::max(width, fm.horizontalAdvance(line) + 4);
    QImage glyphs(std::min(1024,width), std::min(256, fm.height()*int(lines.size())+4), QImage::Format_ARGB32);
    glyphs.fill(Qt::transparent);
    { QPainter p(&glyphs); p.setFont(font); p.setPen(Qt::white); p.drawText(glyphs.rect(), Qt::AlignCenter, text); }
    QRect inkBounds;
    for (int y = 0; y < glyphs.height(); ++y) for (int x = 0; x < glyphs.width(); ++x)
        if (qAlpha(glyphs.pixel(x,y)) >= 100) inkBounds = inkBounds.united(QRect(x,y,1,1));
    if (!inkBounds.isValid()) return mesh;
    glyphs = glyphs.copy(inkBounds);
    std::vector<double> lengths{0.0};
    for (size_t i = 1; i < path.size(); ++i) lengths.push_back(lengths.back() + (path[i]-path[i-1]).length());
    const double total = lengths.back();
    if (total < 1e-6) return mesh;
    const double height = std::min({textHeight > 0 ? textHeight : 2.5, radius*1.6,
                                   total*0.9*glyphs.height()/glyphs.width()});
    const double textLength = height*glyphs.width()/glyphs.height();
    auto surface = [&](double x, double y) {
        const double along = (total-textLength)*0.5 + x/glyphs.width()*textLength;
        size_t i = 1;
        while (i+1 < path.size() && lengths[i] < along) ++i;
        const V3 tangent = (path[i]-path[i-1]).normalized();
        const double span = lengths[i]-lengths[i-1];
        const V3 centre = path[i-1] + (path[i]-path[i-1])*(span > 1e-9 ? (along-lengths[i-1])/span : 0.0);
        V3 normal = facing-tangent*dot3(facing,tangent);
        if (normal.length() < 1e-6) normal = V3{0,0,1}-tangent*tangent.z;
        if (normal.length() < 1e-6) normal = V3{1,0,0}-tangent*tangent.x;
        normal = normal.normalized();
        const V3 across = cross3(normal,tangent).normalized();
        const double angle = (0.5-y/glyphs.height())*height/radius;
        return centre + (normal*std::cos(angle)+across*std::sin(angle))*(radius+0.035);
    };
    for (int y = 0; y < glyphs.height(); ++y) for (int x = 0; x < glyphs.width();) {
        if (qAlpha(glyphs.pixel(x,y)) < 100) { ++x; continue; }
        const int start = x++;
        while (x < glyphs.width() && x-start < 3 && qAlpha(glyphs.pixel(x,y)) >= 100) ++x;
        const int a = mesh.addVertex(surface(start,y)); const int b = mesh.addVertex(surface(x,y));
        const int c = mesh.addVertex(surface(x,y+1)); const int d = mesh.addVertex(surface(start,y+1));
        mesh.addTri(a,b,c); mesh.addTri(a,c,d);
    }
    return mesh;
}



QString harness3DPartKey(const Entity& e) {
    for (const char* key : {"brickId", "harnessBrick"}) {
        const QString id = e.meta.value(QLatin1String(key)).toString();
        if (!id.isEmpty()) return id;
    }
    for (const char* key : {"partCopy", "blockInst"}) {
        const QString id = e.meta.value(QLatin1String(key)).toString();
        if (!id.isEmpty()) return QStringLiteral("drawing:") + id;
    }
    return e.type == EntityType::Connector ? QStringLiteral("drawing:") + e.id : QString();
}

namespace {

// A BRAID'S COLOURS: a screen braid tinned copper; a braided sleeve, of plastic strands, nearly black.
const QColor kBraidShield(168, 172, 180);
const QColor kBraidColour(58, 62, 70);

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

std::vector<PartPins::Pin> pinoutPositions(const QJsonArray& drawing) {
    struct Circ {
        Vec2 c;
        double r;
    };
    struct Num {
        int n;
        Vec2 at;
    };
    std::vector<Circ> circles;
    std::vector<Num> numbers;
    static const QRegularExpression digits(QStringLiteral("^\\d{1,3}$"));
    for (const Entity& e : partDrawingEntities(drawing)) {
        if (e.type == EntityType::Circle && e.radius > 1e-9) circles.push_back({e.center, e.radius});
        if (e.type == EntityType::Text) {
            const QString t = e.meta.value(QStringLiteral("text")).toString().trimmed();
            if (!digits.match(t).hasMatch()) continue;
            double size = e.meta.value(QStringLiteral("size")).toDouble();
            if (size <= 0.0) size = e.meta.value(QStringLiteral("height")).toDouble(1.0);
            // The middle of the number, from where it is set: its bottom left, unless it says it is set otherwise.
            const QString j = e.meta.value(QStringLiteral("justify")).toString().toUpper();
            Vec2 mid = e.a + Vec2{0.3 * size * t.size(), 0.35 * size};
            if (j.contains(QLatin1Char('C'))) mid.x = e.a.x;
            if (j.startsWith(QLatin1Char('M'))) mid.y = e.a.y;
            numbers.push_back({t.toInt(), mid});
        }
    }
    if (circles.size() < 2 || numbers.empty()) return {};
    // THE FACE: the biggest circle; the pins, the circles well inside it.
    const auto face = std::max_element(circles.begin(), circles.end(), [](const Circ& a, const Circ& b) { return a.r < b.r; });
    const Circ outer = *face;
    std::vector<Circ> holes;
    for (const Circ& c : circles)
        if (c.r < outer.r * 0.5 && distance(c.c, outer.c) < outer.r) holes.push_back(c);
    // EACH NUMBER TO ITS CIRCLE: the nearest pairs first, each circle and each number taken once - so a number set
    // between two circles goes to the one no nearer number has.
    struct Pair {
        double d;
        size_t n, h;
    };
    std::vector<Pair> pairs;
    for (size_t n = 0; n < numbers.size(); ++n)
        for (size_t h = 0; h < holes.size(); ++h) {
            const double d = distance(numbers[n].at, holes[h].c);
            if (d < holes[h].r * 4.0 + outer.r * 0.15) pairs.push_back({d, n, h});
        }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    std::vector<bool> numberUsed(numbers.size(), false), holeUsed(holes.size(), false);
    // AND WHAT IS LEFT, each to the nearest circle left: a number set further off than most still has its circle,
    // and every circle drawn is a pin.
    for (size_t n = 0; n < numbers.size(); ++n)
        for (size_t h = 0; h < holes.size(); ++h) {
            const double d = distance(numbers[n].at, holes[h].c);
            if (d >= holes[h].r * 4.0 + outer.r * 0.15) pairs.push_back({1e9 + d, n, h});
        }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    std::vector<PartPins::Pin> out;
    for (const Pair& p : pairs) {
        if (numberUsed[p.n] || holeUsed[p.h]) continue;
        numberUsed[p.n] = holeUsed[p.h] = true;
        PartPins::Pin pin;
        pin.number = numbers[p.n].n;
        pin.placed = true;
        pin.u = (holes[p.h].c.x - outer.c.x) / outer.r;
        pin.v = (holes[p.h].c.y - outer.c.y) / outer.r;
        pin.r = holes[p.h].r / outer.r;
        out.push_back(pin);
    }
    return out;
}

bool PartPins::inFrame(double u, double v) const {
    if (frameN <= 0 || std::abs(u) >= 1.0 || std::abs(v) >= 1.0) return false;
    const int i = std::clamp(static_cast<int>((u + 1.0) / 2.0 * frameN), 0, frameN - 1);
    const int j = std::clamp(static_cast<int>((v + 1.0) / 2.0 * frameN), 0, frameN - 1);
    return frame[static_cast<size_t>(j) * frameN + i] != 0;
}

void pinoutFrame(const QJsonArray& drawing, PartPins& pins) {
    pins.frameN = 0;
    pins.frame.clear();
    std::vector<Vec2> male, female;
    // FROM THE PINS SAID TO BE PINS: one taken for a pin only because nothing says otherwise may be a socket's place.
    for (const PartPins::Pin& p : pins.each) {
        if (!p.placed) continue;
        if (p.female) female.push_back({p.u, p.v});
        else if (p.said) male.push_back({p.u, p.v});
    }
    if (male.empty()) return;
    const std::vector<Entity> ents = partDrawingEntities(drawing);
    // The face: the biggest circle.
    const Entity* outer = nullptr;
    for (const Entity& e : ents)
        if (e.type == EntityType::Circle && (!outer || e.radius > outer->radius)) outer = &e;
    if (!outer || outer->radius <= 1e-9) return;
    const Vec2 o = outer->center;
    const double R = outer->radius;
    const int n = 200;
    // THE LINES, as walls on the grid: everything drawn but the face's circle, the pins' own round outlines and fills,
    // and the words. A wall a cell either side, so a fill cannot slip through between two cells of it.
    std::vector<unsigned char> wall(static_cast<size_t>(n) * n, 0);
    auto cellOf = [&](const Vec2& q, int& i, int& j) {
        i = static_cast<int>(std::floor(((q.x - o.x) / R + 1.0) / 2.0 * n));
        j = static_cast<int>(std::floor(((q.y - o.y) / R + 1.0) / 2.0 * n));
    };
    auto mark = [&](const Vec2& a, const Vec2& b) {
        const double cells = distance(a, b) / R * n;
        const int steps = std::max(1, static_cast<int>(std::ceil(cells * 2.0)));
        for (int k = 0; k <= steps; ++k) {
            int i = 0, j = 0;
            cellOf(a + (b - a) * (static_cast<double>(k) / steps), i, j);
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int ii = i + di, jj = j + dj;
                    if (ii >= 0 && jj >= 0 && ii < n && jj < n) wall[static_cast<size_t>(jj) * n + ii] = 1;
                }
        }
    };
    // A closed shape round about one point, all its points near alike from its middle: a pin's outline.
    auto roundish = [&](const std::vector<Vec2>& pts) {
        if (pts.size() < 6) return false;
        Vec2 mid{0, 0};
        for (const Vec2& q : pts) mid = mid + q;
        mid = mid / static_cast<double>(pts.size());
        double lo = 1e300, hi = 0.0;
        for (const Vec2& q : pts) {
            const double d = distance(q, mid);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        return hi < R * 0.4 && lo > hi * 0.8 && distance(pts.front(), pts.back()) < hi * 0.3;
    };
    for (const Entity& e : ents) {
        switch (e.type) {
            case EntityType::Line: mark(e.a, e.b); break;
            case EntityType::Arc: {
                double a0 = e.startAngle, a1 = e.endAngle;
                while (a1 < a0) a1 += 2.0 * M_PI;
                const int k = std::max(8, static_cast<int>((a1 - a0) * 24));
                for (int s = 0; s < k; ++s) {
                    const double t0 = a0 + (a1 - a0) * s / k, t1 = a0 + (a1 - a0) * (s + 1) / k;
                    mark(e.center + Vec2{std::cos(t0), std::sin(t0)} * e.radius, e.center + Vec2{std::cos(t1), std::sin(t1)} * e.radius);
                }
                break;
            }
            case EntityType::Polyline:
            case EntityType::Polygon:
            case EntityType::Rect:
                if (roundish(e.points)) break;
                for (size_t k = 1; k < e.points.size(); ++k) mark(e.points[k - 1], e.points[k]);
                if (e.type != EntityType::Polyline || e.meta.value(QStringLiteral("closed")).toBool())
                    if (e.points.size() > 2) mark(e.points.back(), e.points.front());
                break;
            default: break;   // circles (the pins, the face), fills, words
        }
    }
    // FILLED OUT FROM EACH MALE PIN to the walls - never past the face's edge. Each pin's region on its own, and kept
    // only where it is a frame: not more than half the face, no socket in it. A pin outside the frame - its region
    // the rest of the face - adds nothing.
    auto inside = [&](int i, int j) {
        const double u = (i + 0.5) / n * 2.0 - 1.0, v = (j + 0.5) / n * 2.0 - 1.0;
        return u * u + v * v < 0.98;
    };
    size_t disc = 0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            if (inside(i, j)) ++disc;
    std::vector<unsigned char> fill(static_cast<size_t>(n) * n, 0);
    std::vector<unsigned char> one(static_cast<size_t>(n) * n, 0);
    std::vector<int> stack, region;
    for (const Vec2& m : male) {
        int i = static_cast<int>((m.x + 1.0) / 2.0 * n), j = static_cast<int>((m.y + 1.0) / 2.0 * n);
        if (i < 0 || j < 0 || i >= n || j >= n) continue;
        // A pin's middle on a wall (a line through it): the fill starts at its nearest cell clear of one.
        if (wall[static_cast<size_t>(j) * n + i]) {
            bool found = false;
            for (int r = 1; r < 6 && !found; ++r)
                for (int dj = -r; dj <= r && !found; ++dj)
                    for (int di = -r; di <= r && !found; ++di) {
                        const int ii = i + di, jj = j + dj;
                        if (ii >= 0 && jj >= 0 && ii < n && jj < n && !wall[static_cast<size_t>(jj) * n + ii]) {
                            i = ii;
                            j = jj;
                            found = true;
                        }
                    }
            if (!found) continue;
        }
        if (fill[static_cast<size_t>(j) * n + i]) continue;   // in a region already kept
        std::fill(one.begin(), one.end(), 0);
        region.clear();
        one[static_cast<size_t>(j) * n + i] = 1;
        stack.push_back(j * n + i);
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            region.push_back(at);
            const int ci = at % n, cj = at / n;
            const int ni[4] = {ci + 1, ci - 1, ci, ci}, nj[4] = {cj, cj, cj + 1, cj - 1};
            for (int k = 0; k < 4; ++k) {
                if (ni[k] < 0 || nj[k] < 0 || ni[k] >= n || nj[k] >= n) continue;
                const size_t idx = static_cast<size_t>(nj[k]) * n + ni[k];
                if (one[idx] || wall[idx] || !inside(ni[k], nj[k])) continue;
                one[idx] = 1;
                stack.push_back(nj[k] * n + ni[k]);
            }
        }
        // A FRAME IS THE PINS' REGION: more of them in it than sockets, two to one - a drawing's numbers set between two
        // circles can give a socket's number to a pin's circle, and one such is no reason to lose the frame.
        auto count = [&](const std::vector<Vec2>& pts) {
            int k = 0;
            for (const Vec2& q : pts) {
                const int qi = static_cast<int>((q.x + 1.0) / 2.0 * n), qj = static_cast<int>((q.y + 1.0) / 2.0 * n);
                if (qi >= 0 && qj >= 0 && qi < n && qj < n && one[static_cast<size_t>(qj) * n + qi]) ++k;
            }
            return k;
        };
        const int pinsIn = count(male), socketsIn = count(female);
        if (pinsIn < 2 || pinsIn <= 2 * socketsIn || region.size() * 2 > disc) continue;
        for (const int at : region) fill[static_cast<size_t>(at)] = 1;
    }
    size_t filled = 0;
    for (unsigned char f : fill) filled += f;
    if (filled == 0) return;
    // The frame's own lines are its edge: the walls next to the fill taken in with it.
    std::vector<unsigned char> frame = fill;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const size_t idx = static_cast<size_t>(j) * n + i;
            if (!wall[idx]) continue;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int ii = i + di, jj = j + dj;
                    if (ii >= 0 && jj >= 0 && ii < n && jj < n && fill[static_cast<size_t>(jj) * n + ii]) frame[idx] = 1;
                }
        }
    pins.frameN = n;
    pins.frame = std::move(frame);
}

PartPins partPinsOf(const LibraryPart* p) {
    PartPins pins;
    if (!p || p->kind != PartKind::Connector) return pins;
    pins.count = std::clamp(p->wayCount(), 1, 400);
    const QString said = (p->value(QStringLiteral("gender")) + QLatin1Char(' ') + p->value(QStringLiteral("mounting"))).toLower();
    pins.female = said.contains(QLatin1String("female")) || said.contains(QLatin1String("receptacle")) ||
                  said.contains(QLatin1String("socket"));
    // EACH PIN: a pin or a socket as its type says (as the connector says, where no type does), and where its pinout
    // drawing puts it - the front, looking at the face; or the back turned round, where only the back is drawn.
    const QStringList genders = partPinGenders(*p);
    std::vector<PartPins::Pin> placed = pinoutPositions(p->fanout);
    const bool fromBack = placed.empty();
    if (fromBack) {
        placed = pinoutPositions(p->fanoutOpposite);
        for (PartPins::Pin& q : placed) q.u = -q.u;
    }
    for (int n = 1; n <= pins.count; ++n) {
        PartPins::Pin pin;
        pin.number = n;
        const QString g = n - 1 < genders.size() ? genders[n - 1] : QString();
        pin.female = g.isEmpty() ? pins.female : g == QLatin1String("female");
        pin.said = !g.isEmpty();
        for (const PartPins::Pin& q : placed)
            if (q.number == n) {
                pin.placed = true;
                pin.u = q.u;
                pin.v = q.v;
                pin.r = q.r;
            }
        pins.each.push_back(pin);
    }
    // THE MALE PINS' FRAME, where the drawing outlines one - seen from the face, as the pins are.
    if (!fromBack) pinoutFrame(p->fanout, pins);
    else {
        PartPins seenFromBack = pins;
        for (PartPins::Pin& q : seenFromBack.each) q.u = -q.u;
        pinoutFrame(p->fanoutOpposite, seenFromBack);
        if (seenFromBack.frameN > 0) {
            const int n = seenFromBack.frameN;
            pins.frameN = n;
            pins.frame.assign(static_cast<size_t>(n) * n, 0);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) pins.frame[static_cast<size_t>(j) * n + i] = seenFromBack.frame[static_cast<size_t>(j) * n + (n - 1 - i)];
        }
    }
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

// A PART'S PROFILE from its 2D drawing: along the way it goes in (`A`, from `lo` to `hi` about `c`, in `n` steps), how
// far its outline reaches either side (`B`) at each step. False when nothing is drawn across it at all.
bool profileOf(const std::vector<Vec2>& pts, const Vec2& c, const V3& A, const V3& B, double lo, double hi, int n,
               std::vector<double>& top, std::vector<double>& bot) {
    top.assign(static_cast<size_t>(n + 1), -1e300);
    bot.assign(static_cast<size_t>(n + 1), 1e300);
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
        if (l < 0 && r > n) return false;
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
    return true;
}

// A RECTANGLE ALONG ITS LENGTH: as wide, and in the same place, at every step from end to end - a box seen from the
// side, as a rectangular connector's housing is drawn. A step may be a little off (a latch, a rib); not more than one
// in ten of them, nor by more than a tenth of its width.
// `across`: how wide it is, from its profile - a lone mark standing out of the drawing does not widen it.
bool rectangularProfile(const std::vector<Vec2>& pts, const Vec2& c, const V3& A, const V3& B, double lo, double hi,
                        double* across = nullptr) {
    const int n = 40;
    std::vector<double> top, bot;
    if (!profileOf(pts, c, A, B, lo, hi, n, top, bot)) return false;
    std::vector<double> ts = top, bs = bot;
    std::nth_element(ts.begin(), ts.begin() + n / 2, ts.end());
    std::nth_element(bs.begin(), bs.begin() + n / 2, bs.end());
    const double t = ts[static_cast<size_t>(n / 2)], b = bs[static_cast<size_t>(n / 2)];
    const double tol = std::max(0.05, 0.1 * (t - b));
    int off = 0;
    for (int i = 0; i <= n; ++i)
        if (std::abs(top[static_cast<size_t>(i)] - t) > tol || std::abs(bot[static_cast<size_t>(i)] - b) > tol) ++off;
    if (across) *across = t - b;
    return t - b > 1e-6 && off * 10 <= n + 1;
}

// A SOLID FROM A PART'S 2D DRAWING: its profile turned round A (a round part), or given `depth` up and down (a
// square one).
Mesh3D solidFromDrawing(const std::vector<Vec2>& pts, const Vec2& c, const V3& A, const V3& B, double lo, double hi,
                        bool revolve, double depth, double* faceRadius, bool farCap = true) {
    const int n = 40;
    std::vector<double> top, bot;
    if (!profileOf(pts, c, A, B, lo, hi, n, top, bot)) return {};
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
        if (farCap) m.addTri(e1, n * sides + k, n * sides + (k + 1) % sides);
    }
    return m;
}
// THE PINOUT'S FACE: the largest closed outline on the pinout - the ring, the rectangle or the closed shape the pins sit
// in, which is the shape the connector shows from the front - gone round evenly in `count` points, about its own
// middle, and how tall it is. Only CLOSED outlines: a pinout drawing also carries the lines that call each pin out to
// its label, and those reach well past the face; taken in, they made a big octagon of a round connector. False when
// the pinout has no closed outline.
bool pinoutFace(const LibraryPart& part, int count, std::vector<Vec2>& face, double& tall) {
    std::vector<Vec2> pts;
    double largest = 0.0;
    for (const Entity& e : partDrawingEntities(part.fanout)) {
        const bool closedPoly = (e.type == EntityType::Polyline || e.type == EntityType::Spline) &&
                                (e.meta.value(QStringLiteral("closed")).toBool() ||
                                 (e.points.size() >= 3 && distance(e.points.front(), e.points.back()) < 1e-6));
        const bool closed = e.type == EntityType::Circle || e.type == EntityType::Ellipse || e.type == EntityType::Rect ||
                            e.type == EntityType::Polygon || closedPoly;
        if (!closed) continue;
        std::vector<Vec2> ring;
        sampleInto(e, ring);
        if (ring.size() < 3) continue;
        double area = 0.0;
        for (size_t i = 0; i < ring.size(); ++i) {
            const Vec2& p = ring[i];
            const Vec2& q = ring[(i + 1) % ring.size()];
            area += p.x * q.y - q.x * p.y;
        }
        area = std::abs(area) / 2.0;
        if (area > largest) {
            largest = area;
            pts = ring;
        }
    }
    if (pts.size() < 3) return false;
    // The ordered contour preserves keyed recesses; a convex hull would fill them in.
    std::vector<Vec2> hull;
    for (const auto& point : pts)
        if (hull.empty() || distance(hull.back(), point) > 1e-9) hull.push_back(point);
    if (hull.size() > 2 && distance(hull.front(), hull.back()) < 1e-9) hull.pop_back();
    if (hull.size() < 3) return false;
    double signedArea = 0.0;
    for (size_t i = 0; i < hull.size(); ++i) {
        const auto& a = hull[i]; const auto& b = hull[(i+1)%hull.size()];
        signedArea += a.x*b.y - b.x*a.y;
    }
    if (signedArea < 0.0) std::reverse(hull.begin(), hull.end());
    Bounds b;
    for (const Vec2& p : hull) b.expand(p);
    tall = b.height();
    if (tall <= 1e-9 || b.width() <= 1e-9) return false;
    const Vec2 mid = b.center();
    // Evenly round it, so that every step along the part has the same points, in the same order.
    double perimeter = 0.0;
    for (size_t i = 0; i < hull.size(); ++i) perimeter += distance(hull[i], hull[(i + 1) % hull.size()]);
    face.clear();
    size_t edge = 0;
    double edgeStart = 0.0;
    for (int j = 0; j < count; ++j) {
        const double want = perimeter * j / count;
        while (edge < hull.size() && edgeStart + distance(hull[edge], hull[(edge + 1) % hull.size()]) < want) {
            edgeStart += distance(hull[edge], hull[(edge + 1) % hull.size()]);
            ++edge;
        }
        const Vec2 a = hull[edge % hull.size()], bb = hull[(edge + 1) % hull.size()];
        const double len = std::max(1e-12, distance(a, bb));
        face.push_back(lerp(a, bb, std::clamp((want - edgeStart) / len, 0.0, 1.0)) - mid);
    }
    return true;
}

// A CONNECTOR BUILT FROM ITS FACE, carried along its length: the pinout's outline is where the solid starts, and it is
// carried down the part grown and shrunk to the mechanical drawing's contour at every step - as tall as the drawing is
// there, and the same shape. A round face makes a round body, a D a D-shaped one, and the steps of the housing the
// drawing shows are steps in the solid.
Mesh3D solidFromFace(const std::vector<Vec2>& pts, const Vec2& c, double lo, double hi, const std::vector<Vec2>& face,
                     double faceTall) {
    const int n = 40;
    std::vector<double> top, bot;
    if (!profileOf(pts, c, {1, 0, 0}, {0, 1, 0}, lo, hi, n, top, bot)) return {};
    const int sides = static_cast<int>(face.size());
    Mesh3D m;
    for (int i = 0; i <= n; ++i) {
        const double t = lo + (hi - lo) * i / n;
        const double up = top[static_cast<size_t>(i)], dn = std::min(bot[static_cast<size_t>(i)], up - 0.2);
        const double s = (up - dn) / faceTall, middle = (up + dn) / 2.0;
        // Its face's up and down along the drawing's; its across, square to the sheet.
        for (const Vec2& f : face) m.addVertex(V3{c.x + t, c.y + middle + f.y * s, f.x * s});
    }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < sides; ++k) {
            const int a = i * sides + k, b = i * sides + (k + 1) % sides, cc = a + sides, d = b + sides;
            m.addTri(a, b, d);
            m.addTri(a, d, cc);
        }
    // Ear-clipped caps retain the face's concave recesses instead of filling them with a centre fan.
    std::vector<std::pair<double, double>> profile;
    for (const auto& p : face) profile.emplace_back(p.x, p.y);
    const Mesh3D cap = prismMesh(profile, 1.0);
    for (const auto& triangle : cap.f) {
        if (triangle[0] >= sides || triangle[1] >= sides || triangle[2] >= sides) continue;
        m.addTri(triangle[0], triangle[1], triangle[2]);
        m.addTri(n*sides + triangle[0], n*sides + triangle[2], n*sides + triangle[1]);
    }
    return m;
}
} // namespace

Mesh3D partDrawingSolid(const LibraryPart& part, bool revolve) {
    std::vector<Vec2> outline;
    Bounds box;
    for (const Entity& e : partDrawingEntities(part.mechanical)) {
        if (e.type == EntityType::Text || e.type == EntityType::Dimension) continue;
        sampleInto(e, outline);
        box.expand(e.bounds());
    }
    if (outline.empty() || !box.valid) return {};
    const Vec2 c = box.center();
    const double width = std::max(0.5, box.height());
    // EXTRUDED, AND THE PART HAS A PINOUT: its face, carried along the drawing - see solidFromFace. Without a pinout,
    // the box it always was.
    if (!revolve) {
        std::vector<Vec2> face;
        double faceTall = 0.0;
        if (pinoutFace(part, 48, face, faceTall)) {
            Mesh3D lofted = solidFromFace(outline, c, box.min.x - c.x, box.max.x - c.x, face, faceTall);
            if (!lofted.v.empty()) {
                for (V3& v : lofted.v) v = v - V3{c.x, c.y, 0.0};
                return lofted;
            }
        }
    }
    Mesh3D m = solidFromDrawing(outline, c, {1, 0, 0}, {0, 1, 0}, box.min.x - c.x, box.max.x - c.x, revolve, width, nullptr);
    for (V3& v : m.v) v = v - V3{c.x, c.y, 0.0};
    return m;
}

void partPinMeshes(const LibraryPart& part, double faceDiameter, double depth, Mesh3D& pins, Mesh3D& sockets) {
    pins = Mesh3D();
    sockets = Mesh3D();
    const PartPins pp = partPinsOf(&part);
    if (pp.count <= 0) return;
    const double R = std::max(0.3, faceDiameter / 2.0);
    const double spread = R * 0.78;
    const double fallbackR = std::clamp(spread * std::sqrt(1.0 / pp.count) * 0.42, 0.08, spread * 0.3);
    for (int i = 0; i < pp.count; ++i) {
        const PartPins::Pin* one = i < static_cast<int>(pp.each.size()) ? &pp.each[static_cast<size_t>(i)] : nullptr;
        double y = 0.0, z = 0.0, r = fallbackR;
        if (one && one->placed) {
            y = one->u * R * 0.96;
            z = one->v * R * 0.96;
            r = std::max(0.06, one->r * R * 0.96 * 0.75);
        } else {
            const double rr = pp.count == 1 ? 0.0 : spread * 0.82 * std::sqrt((i + 0.5) / pp.count);
            const double th = i * 2.399963229728653;
            y = rr * std::cos(th);
            z = rr * std::sin(th);
        }
        const bool female = one ? one->female : pp.female;
        (female ? sockets : pins).append(tubeMesh({{-depth, y, z}, {0.02, y, z}}, female ? r * 1.15 : r, 12, true));
    }
}

namespace {
// A BUILT MODEL, read once and again only when its file changes.
const std::vector<ModelItem3D>& cachedPartModel(const QString& path, QString* how) {
    struct Built {
        QDateTime when;
        std::vector<ModelItem3D> items;
        QString how;
    };
    static QHash<QString, Built> cache;
    const QDateTime when = QFileInfo(path).lastModified();
    auto it = cache.find(path);
    if (it == cache.end() || it->when != when) {
        Built b;
        b.when = when;
        PartModel3D model;
        if (PartModel3D::load(path, model, &b.how)) b.items = modelItems(model, &b.how);
        it = cache.insert(path, b);
    }
    if (how) *how = it->how;
    return it->items;
}
} // namespace

Harness3DScene buildHarness3D(const std::vector<Entity>& ents, const std::function<QString(const QString&)>& modelOf,
                              PartLook look, const std::function<PartPins(const QString&)>& pinsOf,
                              const QHash<QString, QColor>& colours) {
    Harness3DScene scene;
    // ITS PART'S OWN COLOUR, where the bill (by a balloon) or its own part gives one; else the colour it would have.
    auto own = [&colours](const Entity& e, const QColor& otherwise) { return colours.value(e.id, otherwise); };
    // ---- THE RUNS ----------------------------------------------------------------------------------
    std::vector<Vec2> runEnds;
    QHash<QString, bool> isRun;   // a group (copy mark or brick) with a run in it is a run, not a part
    // WHICH PART AN OBJECT IS OF: a harness placed from the wizard says so (its copy, or its brick); a library part
    // put on the sheet by hand is one by the insertion it came in; a connector drawn with the Connector tool is a part
    // of its own. Anything else is no part.
    auto groupOf = [](const Entity& e) {
        for (const char* key : {"partCopy", "brickId", "blockInst"}) {
            const QString g = e.meta.value(QLatin1String(key)).toString();
            if (!g.isEmpty()) return g;
        }
        if (e.type == EntityType::Connector && !e.id.isEmpty()) return QStringLiteral("connector:") + e.id;
        return QString();
    };
    auto brickOf = [](const Entity& e) {
        const QString key = harness3DPartKey(e);
        if (!key.isEmpty()) return key;
        if (e.type == EntityType::TwistedPair || e.type == EntityType::Wire || e.type == EntityType::DoubleLine)
            return QStringLiteral("drawing:run:") + e.id;
        return QStringLiteral("drawing:special:") + e.id;
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
        sl.smoothContours = true; sl.mesh = tubeMesh(piece, radius, 10, true);
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
            sl.smoothContours = true; sl.mesh = tubeMesh(mid, std::max(0.2, across / 2.0), 10, true);
            sl.color = QColor(36, 36, 40);
            sl.what = QStringLiteral("sleeve");
            sl.brick = brickOf(e);
            scene.items.push_back(std::move(sl));
            continue;
        }
        if ((e.type == EntityType::DoubleLine || e.type == EntityType::Wire) && groupOf(e).isEmpty()) {
            const auto path2 = routeOf(e);
            if (path2.size() < 2 || pathLength(path2) < 1e-6) continue;
            std::vector<V3> path;
            for (const Vec2& point : path2) path.push_back(at3(point));
            Harness3DItem wire;
            wire.smoothContours = true; wire.mesh = tubeMesh(path, std::max(0.2, e.meta.value(QStringLiteral("gap")).toDouble(1.0)) / 2.0, 10, true);
            wire.what = QStringLiteral("wire");
            wire.color = own(e, colourOf(e, 0));
            scene.items.push_back(std::move(wire));
            ++scene.runs;
            runEnds.push_back(path2.front());
            runEnds.push_back(path2.back());
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
                    cab.smoothContours = true; cab.mesh = tubeMesh(fine, cable, 16, true);
                    // A BRAID BETWEEN THE TWO SLEEVES, woven; or a cable.
                    const bool braided = sleeveIsBraid(e);
                    cab.color = own(e, braided ? (sleeveIsShield(e) ? kBraidShield : kBraidColour) : QColor(70, 74, 82));
                    cab.what = braided ? QStringLiteral("braid") : QStringLiteral("run");
                    cab.brick = brickOf(e);
                    scene.items.push_back(std::move(cab));
                    wi.smoothContours = true; wi.mesh = tubeMeshVar(fine, [&](double s) {
                        if (s > coverS && s < L - coverE) return 1e-4;   // no sleeve between the two ends
                        return s <= coverS ? ease(faceS, R, s / std::max(1e-9, tapS)) : ease(faceE, R, (L - s) / std::max(1e-9, tapE));
                    }, 18, true);
                } else {
                    const bool transition = e.meta.value(QStringLiteral("shrinkTransition")).toBool();
                    wi.smoothContours = true; wi.mesh = tubeMeshVar(fine, [&](double s) {
                        if (!transition && tapS > 1e-9 && s < tapS) return ease(faceS, R, s / tapS);
                        if (tapE > 1e-9 && s > L - tapE) return ease(faceE, R, (L - s) / tapE);
                        return R;
                    }, 18, true);
                }
            } else {
                wi.smoothContours = true; wi.mesh = tubeMesh(path, std::max(0.2, across) / 2.0, 8, true);
            }
            // A BRAIDED SLEEVE over the whole run, woven.
            const bool braidRun = sleeve && sleeveIsBraid(e) && !e.meta.value(QStringLiteral("shrinkEndsOnly")).toBool();
            const bool overEnds = sleeve && e.meta.value(QStringLiteral("shrinkEndsOnly")).toBool();
            // (The sleeves over a cable's ends are heatshrink, black: the colour of the part is the cable's.)
            wi.color = braidRun ? own(e, sleeveIsShield(e) ? kBraidShield : kBraidColour)
                       : overEnds ? QColor(32, 32, 35) : sleeve ? own(e, QColor(32, 32, 35)) : own(e, colourOf(e, 0));
            wi.what = braidRun ? QStringLiteral("braid") : sleeve ? QStringLiteral("sleeve") : QStringLiteral("wire");
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
        const TwistFigures figures = twistedPairFigures(e);
        const double ww = std::max(0.2, figures.ok ? figures.wireWidth : e.meta.value(QStringLiteral("twistWireWidth")).toDouble(1.0));
        const QString body = e.meta.value(QStringLiteral("twistBody")).toString();
        const double bodyW = figures.ok ? figures.bodyWidth : e.meta.value(QStringLiteral("twistBodyWidth")).toDouble(0.0);
        const double fanA = figures.spreadStart ? std::max(0.0, figures.fanLen) : 0.0;
        const double fanB = figures.spreadFinish ? std::max(0.0, figures.fanLenFinish) : 0.0;
        const bool covered = !body.isEmpty() && bodyW > 1e-6;
        // ITS BODY: a cable, or heatshrink over the wires, between where it fans out at each end.
        if (covered && total > fanA + fanB + 0.5) {
            Harness3DItem it;
            it.smoothContours = true; it.mesh = tubeMesh(stretch(route, fanA, total - fanB), bodyW / 2.0, 18, true);
            const bool braided = e.meta.value(QStringLiteral("twistBodyBraid")).toBool();
            it.color = own(e, braided ? kBraidShield : body == QLatin1String("heatshrink") ? QColor(42, 42, 46) : QColor(70, 74, 82));
            it.what = braided ? QStringLiteral("braid") : body == QLatin1String("heatshrink") ? QStringLiteral("sleeve") : QStringLiteral("run");
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
                sl.smoothContours = true; sl.mesh = tubeMesh(stretch(route, s0, s1), std::max(width, bodyW * 1.05) / 2.0, 18, true);
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
            // WHERE IT IS TWISTED, AS THE DRAWING HAS IT: a fan at each end - each its own length - and between them a
            // whole number of turns. The drawing works those out (shortening the fans where the run is too short for
            // them and a turn); taken from it, the 3D twist is where the sheet's is, and the wires come out of it the
            // way they went in.
            const TwistFigures figs = twistedPairFigures(e);
            double leadA = figs.ok ? figs.fanLen : std::clamp(e.meta.value(QStringLiteral("twistEnds")).toDouble(0.0), 0.0, total * 0.45);
            double leadB = figs.ok ? figs.fanLenFinish : leadA;
            if (leadA + leadB > total) {
                const double k = total / std::max(1e-9, leadA + leadB);
                leadA *= k;
                leadB *= k;
            }
            const double woven = std::max(0.0, total - leadA - leadB);
            const double turn = woven > 1e-9 ? woven / std::max(1.0, std::round(woven / (figs.ok && figs.turn > 1e-9 ? figs.turn : pitch)))
                                             : pitch;
            const double step = std::min(turn / 18.0, std::max(0.2, total / 400.0));
            // EACH WIRE'S PLACE IN ITS FAN, across the run, as the drawing spreads it - so a fan opens out to the face it
            // goes into rather than the wires lying side by side, touching, all the way to it.
            const std::vector<std::vector<Vec2>> drawnStrands = twistedPairStrands(e);
            auto across2 = [&](const Vec2& p, double& s, double& d) {
                double best = 1e300, acc = 0.0;
                for (size_t i = 1; i < route.size(); ++i) {
                    const Vec2 a = route[i - 1], b = route[i];
                    const double seg = distance(a, b);
                    if (seg < 1e-12) continue;
                    const Vec2 t = (b - a) / seg;
                    const double u = std::clamp(dot(p - a, t), 0.0, seg);
                    const Vec2 foot = a + t * u;
                    if (distance(p, foot) < best) {
                        best = distance(p, foot);
                        s = acc + u;
                        d = dot(p - foot, Vec2{-t.y, t.x});
                    }
                    acc += seg;
                }
            };
            // For wire k: its offset across the run at each point of its two fans, by how far along the run.
            std::vector<std::vector<std::pair<double, double>>> fanStart(static_cast<size_t>(n)), fanEnd(static_cast<size_t>(n));
            for (int k = 0; k < n && static_cast<size_t>(2 * k + 1) < drawnStrands.size(); ++k) {
                const auto& ea = drawnStrands[static_cast<size_t>(2 * k)];
                const auto& eb = drawnStrands[static_cast<size_t>(2 * k + 1)];
                if (ea.size() != eb.size()) continue;
                for (size_t q = 0; q < ea.size(); ++q) {
                    double s = 0.0, d = 0.0;
                    across2((ea[q] + eb[q]) * 0.5, s, d);
                    if (s <= leadA + 1e-6) fanStart[static_cast<size_t>(k)].push_back({s, d});
                    if (s >= total - leadB - 1e-6) fanEnd[static_cast<size_t>(k)].push_back({s, d});
                }
                std::sort(fanStart[static_cast<size_t>(k)].begin(), fanStart[static_cast<size_t>(k)].end());
                std::sort(fanEnd[static_cast<size_t>(k)].begin(), fanEnd[static_cast<size_t>(k)].end());
            }
            auto fanAt = [&](int k, double s, bool& known) {
                const auto& sd = (s <= leadA ? fanStart : fanEnd)[static_cast<size_t>(k)];
                known = !sd.empty();
                if (sd.empty()) return 0.0;
                if (s <= sd.front().first) return sd.front().second;
                if (s >= sd.back().first) return sd.back().second;
                const auto it = std::lower_bound(sd.begin(), sd.end(), std::make_pair(s, -1e300));
                const auto& a = *(it - 1);
                const auto& b = *it;
                const double u = b.first - a.first > 1e-9 ? (s - a.first) / (b.first - a.first) : 0.0;
                return a.second + (b.second - a.second) * u;
            };
            // WHICH WAY ROUND THE WIRES START: the first wire on the side of the run its fan leaves it on, so a fan and
            // the twist meet without the wires crossing over where they do.
            double phase = 0.0;
            if (leadA > 1e-6) {
                bool known = false;
                const double d0 = fanAt(0, leadA * 0.9, known);
                if (known && d0 < 0.0) phase = M_PI;
            }
            auto smooth = [](double u) {
                u = std::clamp(u, 0.0, 1.0);
                return u * u * (3.0 - 2.0 * u);
            };
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
                    const double twisted = std::clamp(ss - leadA, 0.0, woven);
                    const double th = 2.0 * M_PI * (twisted / turn + static_cast<double>(k) / n) + phase;
                    double py = rad * std::cos(th), pz = rad * std::sin(th);
                    // In a fan: from where the drawing spreads it, into its place in the twist.
                    if (ss < leadA || ss > total - leadB) {
                        bool known = false;
                        const double d = fanAt(k, ss, known);
                        if (known) {
                            const double w = ss < leadA ? smooth(ss / leadA) : smooth((total - ss) / leadB);
                            py = (1.0 - w) * d + w * py;
                            pz = w * pz;
                        }
                    }
                    helix.push_back(at + across * py + V3{0.0, 0.0, 1.0} * pz);
                    if (ss >= total) break;
                }
                Harness3DItem wi;
                wi.smoothContours = true; wi.mesh = tubeMesh(helix, wire / 2.0, 8, true);
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
                wi.smoothContours = true; wi.mesh = tubeMesh(path, ww / 2.0, 8, true);
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
                wi.smoothContours = true; wi.mesh = tubeMesh(piece, ww / 2.0, 8, false);
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
    // LABELS AND LACING ARE FITTINGS ON A RUN, rather than grey solids made from their 2D lines.
    auto sleeve = [](const V3& centre, const V3& axis, double length, double radius, double wall) {
        Mesh3D mesh;
        const V3 across{-axis.y, axis.x, 0};
        const int sides = 32;
        for (int end = 0; end < 2; ++end) for (int skin = 0; skin < 2; ++skin)
            for (int k = 0; k < sides; ++k) {
                const double angle = 2 * M_PI * k / sides;
                const double r = radius + (skin ? wall : 0.0);
                mesh.v.push_back(centre + axis * ((end ? 0.5 : -0.5) * length) +
                                 across * (r * std::cos(angle)) + V3{0, 0, r * std::sin(angle)});
            }
        for (int k = 0; k < sides; ++k) {
            const int next = (k + 1) % sides;
            for (int skin = 0; skin < 2; ++skin) {
                const int a = skin * sides + k, b = skin * sides + next;
                const int c = a + 2 * sides, d = b + 2 * sides;
                if (skin) { mesh.addTri(a, b, c); mesh.addTri(b, d, c); }
                else { mesh.addTri(a, c, b); mesh.addTri(b, c, d); }
            }
            for (int end = 0; end < 2; ++end) {
                const int a = end * 2 * sides + k, b = end * 2 * sides + next;
                const int c = a + sides, d = b + sides;
                if (end) { mesh.addTri(a, c, b); mesh.addTri(b, c, d); }
                else { mesh.addTri(a, b, c); mesh.addTri(b, d, c); }
            }
        }
        return mesh;
    };
    for (const auto& mask : ents) {
        if (!mask.meta.value(QStringLiteral("labelMask")).toBool() || mask.points.size() < 4) continue;
        const Vec2 centre2 = (mask.points[0] + mask.points[2]) * 0.5;
        const Vec2 axis2 = (mask.points[1] - mask.points[0]).normalized();
        const double length = distance(mask.points[0], mask.points[1]);
        const double radius = distance(mask.points[1], mask.points[2]) * 0.5;
        if (length < 1e-6 || radius < 1e-6) continue;
        Harness3DItem label;
        label.mesh = sleeve({centre2.x, centre2.y, 0}, {axis2.x, axis2.y, 0}, length, radius, 0.12);
        label.color = QColor(255, 215, 45); label.what = QStringLiteral("Label");
        label.ownLook = true; label.brick = brickOf(mask); scene.items.push_back(label);
        QString caption = mask.meta.value(QStringLiteral("labelCaption")).toString();
        double textHeight = mask.meta.value(QStringLiteral("labelTextHeight")).toDouble();
        const QString labelGroup = mask.meta.value(QStringLiteral("labelGroup")).toString();
        if (!labelGroup.isEmpty()) for (const auto& entity : ents) {
            if (entity.meta.value(QStringLiteral("labelGroup")).toString() != labelGroup || entity.type != EntityType::Text) continue;
            caption = entity.meta.value(QStringLiteral("text")).toString(caption);
            textHeight = entity.meta.value(QStringLiteral("height")).toDouble(textHeight);
        }
        Harness3DItem ink;
        const V3 centre{centre2.x,centre2.y,0}, along{axis2.x,axis2.y,0};
        ink.surfaceText = caption;
        ink.textAlwaysVisible = mask.meta.value(QStringLiteral("labelTextAlwaysVisible")).toBool(true);
        ink.textPath = {centre-along*(length*0.5),centre+along*(length*0.5)};
        ink.textRadius = radius+0.12; ink.textHeight = textHeight;
        ink.mesh = labelTextMesh3D({centre-along*(length*0.5),centre+along*(length*0.5)}, radius+0.12, caption, textHeight);
        ink.color = Qt::black; ink.what = QStringLiteral("Label text"); ink.brick = label.brick; ink.ownLook = true;
        if (!ink.mesh.empty()) scene.items.push_back(std::move(ink));
        bool clear = false;
        const QString group = mask.meta.value(QStringLiteral("labelGroup")).toString();
        for (const auto& e : ents) {
            if (e.meta.value(QStringLiteral("linetype")).toString() != QLatin1String("HIDDEN")) continue;
            const bool same = !group.isEmpty() && e.meta.value(QStringLiteral("labelGroup")).toString() == group;
            const Bounds bounds = e.bounds();
            const Vec2 mid = bounds.valid ? (bounds.min + bounds.max) * 0.5 : Vec2{};
            bool nearestLabel = false;
            if (group.isEmpty() && e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("label")) {
                const Entity* closest = nullptr; double bestDistance = 1e300;
                for (const auto& candidate : ents) if (candidate.meta.value(QStringLiteral("labelMask")).toBool() && candidate.points.size() >= 4) {
                    const double d = distance(mid, (candidate.points[0] + candidate.points[2]) * 0.5);
                    if (d < bestDistance) { closest = &candidate; bestDistance = d; }
                }
                nearestLabel = closest == &mask && bestDistance < length + radius;
            }
            if (same || nearestLabel) { clear = true; break; }
        }
        if (clear) {
            Harness3DItem cover;
            cover.mesh = sleeve({centre2.x, centre2.y, 0}, {axis2.x, axis2.y, 0}, length + 4.0, radius + 0.3, 0.18);
            cover.color = QColor(220, 240, 250); cover.alpha = 0.22; cover.ownLook = true;
            cover.what = QStringLiteral("Clear label sleeve"); cover.brick = label.brick;
            scene.items.push_back(cover);
        }
    }
    std::vector<const Entity*> tieMasks;
    for (const auto& e : ents) if (e.type == EntityType::Wipeout && e.meta.value(QStringLiteral("lacingTie")).toBool()) tieMasks.push_back(&e);
    for (const auto& e : ents) {
        if (e.type != EntityType::Polyline || !e.meta.value(QStringLiteral("lacingTie")).toBool() || e.points.size() < 2) continue;
        const Entity* mask = nullptr; double nearest = 1e300;
        for (const auto* candidate : tieMasks) {
            const Bounds box = candidate->bounds();
            const double d = distance(e.points.front(), (box.min + box.max) * 0.5);
            if (d < nearest) { nearest = d; mask = candidate; }
        }
        if (!mask || mask->points.size() < 4) continue;
        const Vec2 centre2 = (mask->points[0] + mask->points[2]) * 0.5;
        const Vec2 along = (mask->points[1] - mask->points[0]).normalized(), across = along.perp();
        const double radius = std::max(0.4, distance(mask->points[1], mask->points[2]) * 0.5);
        const double cord = std::clamp(radius * 0.06, 0.25, 0.8);
        std::vector<V3> path;
        if (e.points.size() == 2) {
            const double x0 = dot(e.points[0] - centre2, along), x1 = dot(e.points[1] - centre2, along);
            const bool turn = std::abs(dot(e.points[1] - e.points[0], across)) > radius;
            if (turn) for (int k = 0; k <= 48; ++k) {
                const double t = k / 48.0, angle = 2 * M_PI * t;
                const Vec2 xy = centre2 + along * (x0 + (x1 - x0) * t) + across * ((radius + cord) * std::cos(angle));
                path.push_back({xy.x, xy.y, (radius + cord) * std::sin(angle)});
            }
        }
        if (path.empty()) for (const Vec2& q : e.points) {
            const double offset = dot(q - centre2, across);
            path.push_back({q.x, q.y, std::sqrt(std::max(0.0, radius * radius - offset * offset)) + cord});
        }
        Harness3DItem tie; tie.smoothContours = true; tie.mesh = tubeMesh(path, cord, 10, true);
        tie.color = QColor(225, 215, 185); tie.what = QStringLiteral("Lacing tie"); tie.ownLook = true;
        tie.brick = brickOf(e); scene.items.push_back(tie);
    }
    // ---- THE PARTS ---------------------------------------------------------------------------------
    struct Part {
        Bounds box;
        QString lib, name, brick;
        int ways = 0;                // a connector drawn on the sheet: its pin count, for its pinout
        std::vector<Vec2> outline;   // the points of its drawing, for a solid made from it
    };
    QHash<QString, Part> parts;
    QStringList order;
    for (const Entity& e : ents) {
        const QString g = groupOf(e);
        if (e.meta.value(QStringLiteral("labelMask")).toBool() ||
            !e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty() ||
            e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("label") ||
            e.meta.value(QStringLiteral("lacingTie")).toBool()) continue;
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
        if (e.type == EntityType::Connector) {
            const ConnectorSpec spec = connectorSpecFromMeta(e.meta);
            p.ways = spec.pinCount;
            if (p.name.isEmpty()) p.name = !spec.designator.isEmpty() ? spec.designator : spec.partNumber;
        }
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
        // A MODEL BUILT IN LOOPWIRE: each of its solids in its own colour and what it is made of, its X along the way the
        // part goes in, its face towards the end away from the harness - scaled to the length it is drawn.
        bool built = false;
        if (!file.isEmpty() && isPartModelFile(file)) {
            const std::vector<ModelItem3D>& made = cachedPartModel(file, &how);
            V3 blo, bhi;
            if (modelBounds(made, blo, bhi)) {
                const V3 mid = (blo + bhi) * 0.5;
                const double k = length / std::max(1e-9, bhi.x - blo.x);
                bool first = true;
                for (const ModelItem3D& mi : made) {
                    if (mi.alpha < 0.99 || mi.mesh.empty()) continue;
                    Harness3DItem one;
                    one.mesh = mi.mesh;
                    for (V3& v : one.mesh.v) v = centre + A * ((v.x - mid.x) * k) + B * ((v.y - mid.y) * k) + Z * ((v.z - mid.z) * k);
                    one.color = mi.color;
                    one.texture = mi.texture;
                    one.ownLook = true;
                    one.model = true;
                    one.what = it.what;
                    one.brick = it.brick;
                    if (first) {
                        it = std::move(one);
                        first = false;
                    } else {
                        scene.items.push_back(std::move(one));
                    }
                }
                built = !first;
                if (built) ++scene.modelled;
            }
        }
        const Mesh3D* model = built || file.isEmpty() ? nullptr : &cachedMesh3D(file, &how);
        V3 mlo, mhi;
        if (built) {
            // made above
        } else if (model && !model->empty() && model->bounds(mlo, mhi)) {
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
            PartPins pins = pinsOf && !p.lib.isEmpty() ? pinsOf(p.lib) : PartPins{};
            if (pins.count <= 0 && p.ways > 0) pins.count = p.ways;   // a connector drawn on the sheet: its own pins
            // A RECTANGULAR CONNECTOR IS A BLOCK, not a cylinder. Its side view is a rectangle - but so is a round
            // part's, so only a connector is taken to be square: a ferrule, a ring, a sleeve drawn as a rectangle are
            // still turned. Its depth is its width, as an extruded part's is - the width its outline has, not a
            // mark standing out past it.
            double blockWidth = width;
            const bool block = look == PartLook::Revolved && pins.count > 0 && !p.outline.empty() &&
                               rectangularProfile(p.outline, c, A, B, lo, hi, &blockWidth);
            // A ROUND CONNECTOR WITH A FRAME ROUND ITS MALE PINS: its face left open, to be made again with the pocket in.
            const bool pocket = look == PartLook::Revolved && !block && pins.frameN > 0;
            if (look != PartLook::Box && !p.outline.empty()) {
                it.mesh = solidFromDrawing(p.outline, c, A, B, lo, hi, look == PartLook::Revolved && !block,
                                           block ? blockWidth : width, &faceR, !pocket || p.outline.empty());
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
            // ITS PINOUT at its front face - the end away from the harness: male pins, or female sockets, as many as
            // it has, spread evenly over the face. Inside the connector, each run in from the face and ending on it -
            // its tip at the connector's far point, nothing standing out past it.
            if (pins.count > 0) {
                const V3 face = centre + A * hi;
                const double R = std::max(0.3, faceR * (look == PartLook::Revolved ? 0.78 : 0.7));
                const double pinR = std::clamp(R * std::sqrt(1.0 / pins.count) * 0.42, 0.12, R * 0.3);
                // As long as two fifths of what a pin standing out of it would have been.
                const double depth = 0.4 * std::max(0.8, length * 0.14);
                Harness3DItem pinsItem, socketsItem;
                // ON THE FACE AS THE PINOUT DRAWING HAS THEM, where it does: its circle to the face's.
                const double faceHalf = block ? std::min(faceR, (block ? blockWidth : width) / 2.0) : faceR;
                // On a block, in rows and columns over its face, as a rectangular connector's cavities are.
                const double faceW = 2.0 * faceR * 0.8, faceH = (block ? blockWidth : width) * 0.8;
                const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(pins.count * faceW / std::max(1e-6, faceH)))));
                const int rows = (pins.count + cols - 1) / cols;
                const double cell = std::min(faceW / cols, faceH / rows);
                const double blockPinR = std::max(0.08, cell * 0.22);
                for (int i = 0; i < pins.count; ++i) {
                    const PartPins::Pin* one = i < static_cast<int>(pins.each.size()) ? &pins.each[static_cast<size_t>(i)] : nullptr;
                    const bool female = one ? one->female : pins.female;
                    V3 at;
                    if (one && one->placed) {
                        at = face + B * (one->u * faceHalf * 0.96) + Z * (one->v * faceHalf * 0.96);
                    } else if (block) {
                        const int row = i / cols, col = i % cols;
                        const int inRow = row + 1 == rows ? pins.count - row * cols : cols;
                        at = face + B * ((col - (inRow - 1) / 2.0) * cell) + Z * (((rows - 1) / 2.0 - row) * cell);
                    } else {
                        // A sunflower's spiral: as even a spread over a round face as there is, for any number.
                        const double rr = pins.count == 1 ? 0.0 : R * 0.82 * std::sqrt((i + 0.5) / pins.count);
                        const double th = i * 2.399963229728653;
                        at = face + B * (rr * std::cos(th)) + Z * (rr * std::sin(th));
                    }
                    const double r = one && one->placed ? std::max(0.06, one->r * faceHalf * 0.96 * 0.75) : block ? blockPinR : pinR;
                    // Its tip a hair proud of the face, so it shows on it rather than flickering in it.
                    const double proud = std::min(0.02, depth * 0.05);
                    (female ? socketsItem : pinsItem).mesh.append(tubeMesh({at - A * depth, at + A * proud}, female ? r * 1.15 : r, 10, true));
                }
                // THE POCKET round the male pins: the face again with the frame cut out of it, and the frame drawn into
                // the connector - its walls and its floor - with the pins standing in it.
                if (pocket && !it.cube) {
                    const double s = faceHalf * 0.96;
                    const double Rn = faceR / s;   // the face's edge, in the drawing's share
                    const double sink = std::min(depth * 0.8, faceR * 0.3);
                    const int n = pins.frameN;
                    const double h = 2.0 / n;
                    auto P = [&](double u, double v, double d) { return face + B * (u * s) + Z * (v * s) - A * d; };
                    auto quad = [](Mesh3D& m, const V3& a, const V3& b, const V3& cc, const V3& d) {
                        const int i = m.addVertex(a), j = m.addVertex(b), k = m.addVertex(cc), l = m.addVertex(d);
                        m.addTri(i, j, k);
                        m.addTri(i, k, l);
                    };
                    Mesh3D cap, pit;
                    const int below = static_cast<int>(std::ceil((Rn - 1.0) / h));
                    for (int row = -below; row < n + below; ++row) {
                        const double v0 = -1.0 + row * h, v1 = v0 + h;
                        if (v1 <= -Rn || v0 >= Rn) continue;
                        const double c0 = std::max(v0, -Rn), c1 = std::min(v1, Rn);
                        const double w0 = std::sqrt(std::max(0.0, Rn * Rn - c0 * c0)), w1 = std::sqrt(std::max(0.0, Rn * Rn - c1 * c1));
                        // Along the row, the runs of face and of frame.
                        double runStart = -1e9;
                        bool runFrame = false;
                        auto close = [&](double uEnd) {
                            if (runStart < -1e8) return;
                            if (runFrame) {
                                quad(pit, P(runStart, c0, sink), P(uEnd, c0, sink), P(uEnd, c1, sink), P(runStart, c1, sink));
                            } else {
                                const double a0 = std::max(runStart, -w0), b0 = std::min(uEnd, w0);
                                const double a1 = std::max(runStart, -w1), b1 = std::min(uEnd, w1);
                                if (b0 > a0 || b1 > a1)
                                    quad(cap, P(a0, c0, 0), P(std::max(a0, b0), c0, 0), P(std::max(a1, b1), c1, 0), P(a1, c1, 0));
                            }
                        };
                        const double from = -std::max(w0, w1);
                        const int colsHere = static_cast<int>(std::ceil((2.0 * std::max(w0, w1)) / h)) + 2;
                        for (int k = 0; k <= colsHere; ++k) {
                            const double u0 = std::floor(from / h) * h + k * h;
                            const bool f = pins.inFrame(u0 + h / 2, (c0 + c1) / 2);
                            if (runStart < -1e8 || f != runFrame) {
                                close(u0);
                                runStart = u0;
                                runFrame = f;
                            }
                        }
                        close(std::floor(from / h) * h + (colsHere + 1) * h);
                    }
                    // THE WALLS: each edge of the frame, from the face down to the floor.
                    for (int j = 0; j < n; ++j)
                        for (int i = 0; i < n; ++i) {
                            if (!pins.frame[static_cast<size_t>(j) * n + i]) continue;
                            const double u0 = -1.0 + i * h, u1 = u0 + h, v0 = -1.0 + j * h, v1 = v0 + h;
                            auto out = [&](int ii, int jj) {
                                return ii < 0 || jj < 0 || ii >= n || jj >= n || !pins.frame[static_cast<size_t>(jj) * n + ii];
                            };
                            if (out(i - 1, j)) quad(pit, P(u0, v0, 0), P(u0, v1, 0), P(u0, v1, sink), P(u0, v0, sink));
                            if (out(i + 1, j)) quad(pit, P(u1, v0, 0), P(u1, v0, sink), P(u1, v1, sink), P(u1, v1, 0));
                            if (out(i, j - 1)) quad(pit, P(u0, v0, 0), P(u0, v0, sink), P(u1, v0, sink), P(u1, v0, 0));
                            if (out(i, j + 1)) quad(pit, P(u0, v1, 0), P(u1, v1, 0), P(u1, v1, sink), P(u0, v1, sink));
                        }
                    it.mesh.append(cap);
                    Harness3DItem insert;
                    insert.mesh = std::move(pit);
                    insert.color = QColor(46, 50, 58);
                    insert.what = QStringLiteral("insert");
                    insert.brick = p.brick;
                    scene.items.push_back(std::move(insert));
                }
                // PINS AND SOCKETS, each as what it is: a connector with both shows both.
                pinsItem.color = QColor(212, 175, 80);
                pinsItem.what = QStringLiteral("pins");
                socketsItem.color = QColor(18, 18, 20);
                socketsItem.what = QStringLiteral("sockets");
                for (Harness3DItem* x : {&pinsItem, &socketsItem}) {
                    if (x->mesh.empty()) continue;
                    x->brick = p.brick;
                    scene.items.push_back(std::move(*x));
                }
            }
        }
        scene.items.push_back(std::move(it));
    }
    // WHAT EACH LOOKS MADE OF: a part and its pins metal, a wire's insulation plastic, a jacket, a sleeve, a socket's
    // insert matte.
    for (Harness3DItem& it : scene.items) {
        if (it.ownLook) continue;
        if (it.what == QLatin1String("wire")) it.texture = Texture3D::Plastic;
        else if (it.what == QLatin1String("braid")) it.texture = Texture3D::Braid;
        else if (it.what == QLatin1String("run") || it.what == QLatin1String("sleeve") ||
                 it.what == QLatin1String("transition") || it.what == QLatin1String("sockets") || it.what == QLatin1String("insert"))
            it.texture = Texture3D::Matte;
        else it.texture = Texture3D::Metal;
    }
    return scene;
}

} // namespace Loopwire

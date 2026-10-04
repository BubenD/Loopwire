#include "geometry/Mesh3D.h"

#ifdef EDRAW_HAVE_OCCT
#include "geometry/StepOcct.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointF>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Edraw_Desktop {

bool Mesh3D::bounds(V3& lo, V3& hi) const {
    if (v.empty()) return false;
    lo = hi = v.front();
    for (const V3& p : v) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return true;
}

void Mesh3D::append(const Mesh3D& o) {
    const int base = static_cast<int>(v.size());
    v.insert(v.end(), o.v.begin(), o.v.end());
    for (const auto& t : o.f) f.push_back({t[0] + base, t[1] + base, t[2] + base});
}

Mesh3D boxMesh(const V3& lo, const V3& hi) {
    Mesh3D m;
    for (int i = 0; i < 8; ++i)
        m.addVertex({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
    const int q[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const auto& s : q) {
        m.addTri(s[0], s[1], s[2]);
        m.addTri(s[0], s[2], s[3]);
    }
    return m;
}

Mesh3D tubeMesh(const std::vector<V3>& pathIn, double radius, int sides, bool caps) {
    return tubeMeshVar(pathIn, [radius](double) { return radius; }, sides, caps);
}

Mesh3D tubeMeshVar(const std::vector<V3>& pathIn, const std::function<double(double)>& radiusAt, int sides, bool caps) {
    Mesh3D m;
    const double radius = 1.0;   // checked below, per ring
    std::vector<V3> path;
    for (const V3& p : pathIn)
        if (path.empty() || (p - path.back()).length() > 1e-6) path.push_back(p);
    if (path.size() < 2 || radius <= 0.0) return m;
    sides = std::max(3, sides);
    const size_t n = path.size();
    // How far along each point is, for its radius.
    std::vector<double> along(n, 0.0);
    for (size_t i = 1; i < n; ++i) along[i] = along[i - 1] + (path[i] - path[i - 1]).length();
    // FRAMES CARRIED ALONG THE PATH: each ring square to the path there, turned as little as it can be from the last.
    std::vector<V3> tan(n);
    for (size_t i = 0; i < n; ++i) {
        const V3 a = path[i > 0 ? i - 1 : 0], b = path[i + 1 < n ? i + 1 : n - 1];
        tan[i] = (b - a).normalized();
    }
    V3 nrm = std::abs(tan[0].z) < 0.9 ? cross3(tan[0], V3{0, 0, 1}).normalized() : cross3(tan[0], V3{1, 0, 0}).normalized();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            nrm = (nrm - tan[i] * dot3(nrm, tan[i])).normalized();
            if (nrm.length() < 0.5) nrm = std::abs(tan[i].z) < 0.9 ? cross3(tan[i], V3{0, 0, 1}).normalized()
                                                                   : cross3(tan[i], V3{1, 0, 0}).normalized();
        }
        const V3 bin = cross3(tan[i], nrm).normalized();
        const double r = std::max(1e-4, radiusAt(along[i]));
        for (int k = 0; k < sides; ++k) {
            const double a = 2.0 * M_PI * k / sides;
            m.addVertex(path[i] + (nrm * std::cos(a) + bin * std::sin(a)) * r);
        }
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 0; k < sides; ++k) {
            const int a = static_cast<int>(i) * sides + k, b = static_cast<int>(i) * sides + (k + 1) % sides;
            const int c = a + sides, d = b + sides;
            m.addTri(a, b, d);
            m.addTri(a, d, c);
        }
    if (caps) {
        const int c0 = m.addVertex(path.front()), c1 = m.addVertex(path.back());
        const int last = static_cast<int>(n - 1) * sides;
        for (int k = 0; k < sides; ++k) {
            m.addTri(c0, (k + 1) % sides, k);
            m.addTri(c1, last + k, last + (k + 1) % sides);
        }
    }
    return m;
}

// ---- STL and OBJ ---------------------------------------------------------------------------------

Mesh3D loadStl(const QString& path, QString* how) {
    Mesh3D m;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (how) *how = QStringLiteral("cannot open %1").arg(QFileInfo(path).fileName());
        return m;
    }
    const QByteArray data = file.readAll();
    // BINARY: an 80-byte header, a count, and 50 bytes a triangle - checked by the size, as a text STL may well begin
    // with the word "solid" too.
    if (data.size() >= 84) {
        quint32 count = 0;
        std::memcpy(&count, data.constData() + 80, 4);
        if (static_cast<qint64>(84) + static_cast<qint64>(count) * 50 == data.size()) {
            const char* p = data.constData() + 84;
            for (quint32 t = 0; t < count; ++t, p += 50) {
                float c[12];
                std::memcpy(c, p, 48);
                const int a = m.addVertex({c[3], c[4], c[5]});
                const int b = m.addVertex({c[6], c[7], c[8]});
                const int d = m.addVertex({c[9], c[10], c[11]});
                m.addTri(a, b, d);
            }
            if (how) *how = QStringLiteral("binary STL, %1 triangles").arg(count);
            return m;
        }
    }
    // TEXT: every "vertex x y z", three to a facet.
    static const QRegularExpression vertex(QStringLiteral("vertex\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    std::vector<V3> corners;
    for (auto it = vertex.globalMatch(QString::fromLatin1(data)); it.hasNext();) {
        const auto mt = it.next();
        corners.push_back({mt.captured(1).toDouble(), mt.captured(2).toDouble(), mt.captured(3).toDouble()});
    }
    for (size_t i = 0; i + 2 < corners.size(); i += 3) {
        const int a = m.addVertex(corners[i]), b = m.addVertex(corners[i + 1]), c = m.addVertex(corners[i + 2]);
        m.addTri(a, b, c);
    }
    if (how) *how = m.empty() ? QStringLiteral("no triangles in it") : QStringLiteral("STL, %1 triangles").arg(m.f.size());
    return m;
}

Mesh3D loadObj(const QString& path, QString* how) {
    Mesh3D m;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (how) *how = QStringLiteral("cannot open %1").arg(QFileInfo(path).fileName());
        return m;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const QStringList w = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (w.size() >= 4 && w[0] == QLatin1String("v")) {
            m.addVertex({w[1].toDouble(), w[2].toDouble(), w[3].toDouble()});
        } else if (w.size() >= 4 && w[0] == QLatin1String("f")) {
            // "f 1 2 3", "f 1/1 2/2 3/3", "f -3 -2 -1": the corner's number first, from the end where negative.
            std::vector<int> idx;
            for (int i = 1; i < w.size(); ++i) {
                const int k = w[i].section(QLatin1Char('/'), 0, 0).toInt();
                idx.push_back(k < 0 ? static_cast<int>(m.v.size()) + k : k - 1);
            }
            for (size_t i = 1; i + 1 < idx.size(); ++i)
                if (idx[0] >= 0 && idx[i] >= 0 && idx[i + 1] >= 0 && idx[0] < static_cast<int>(m.v.size()) &&
                    idx[i] < static_cast<int>(m.v.size()) && idx[i + 1] < static_cast<int>(m.v.size()))
                    m.addTri(idx[0], idx[i], idx[i + 1]);
        }
    }
    if (how) *how = m.empty() ? QStringLiteral("no faces in it") : QStringLiteral("OBJ, %1 triangles").arg(m.f.size());
    return m;
}

// ---- STEP ------------------------------------------------------------------------------------------

namespace {

// One argument of a STEP entity: a number, a #reference, a 'string', an .ENUM., a list, or a typed value.
struct Arg {
    enum Kind { None, Num, Ref, Str, Enum, List } kind = None;
    double num = 0.0;
    int ref = 0;
    QByteArray text;
    std::vector<Arg> list;
};

struct Parser {
    const QByteArray& s;
    int i = 0;
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    Arg value() {
        ws();
        Arg a;
        if (i >= s.size()) return a;
        const char c = s[i];
        if (c == '(') {
            ++i;
            a.kind = Arg::List;
            ws();
            if (i < s.size() && s[i] == ')') {
                ++i;
                return a;
            }
            while (i < s.size()) {
                a.list.push_back(value());
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ')') ++i;
                break;
            }
        } else if (c == '#') {
            ++i;
            int start = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            a.kind = Arg::Ref;
            a.ref = s.mid(start, i - start).toInt();
        } else if (c == '\'') {
            ++i;
            a.kind = Arg::Str;
            while (i < s.size()) {
                if (s[i] == '\'') {
                    if (i + 1 < s.size() && s[i + 1] == '\'') {
                        a.text += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                a.text += s[i++];
            }
        } else if (c == '.') {
            ++i;
            int start = i;
            while (i < s.size() && s[i] != '.') ++i;
            a.kind = Arg::Enum;
            a.text = s.mid(start, i - start);
            ++i;
        } else if (c == '$' || c == '*') {
            ++i;
        } else if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
            int start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '-' ||
                                    s[i] == '+' || s[i] == 'E' || s[i] == 'e'))
                ++i;
            a.kind = Arg::Num;
            a.num = s.mid(start, i - start).toDouble();
        } else {
            // A typed value - LENGTH_MEASURE(2.) - or a keyword: its inside, or nothing.
            int start = i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
            if (i == start) {
                ++i;
                return a;
            }
            ws();
            if (i < s.size() && s[i] == '(') {
                Arg inner = value();
                if (inner.list.size() == 1) return inner.list.front();
                return inner;
            }
        }
        return a;
    }
};

struct Ent {
    QByteArray type;
    Arg args;
};

struct Placement {
    V3 loc, x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
};

struct Step {
    QHash<int, Ent> ents;

    const Ent* ent(int id) const {
        const auto it = ents.constFind(id);
        return it == ents.cend() ? nullptr : &it.value();
    }
    static const Arg* at(const Ent* e, size_t k) {
        return e && e->args.kind == Arg::List && k < e->args.list.size() ? &e->args.list[k] : nullptr;
    }
    static int refAt(const Ent* e, size_t k) {
        const Arg* a = at(e, k);
        return a && a->kind == Arg::Ref ? a->ref : 0;
    }
    static double numAt(const Ent* e, size_t k) {
        const Arg* a = at(e, k);
        return a && a->kind == Arg::Num ? a->num : 0.0;
    }
    static bool flagAt(const Ent* e, size_t k) {
        const Arg* a = at(e, k);
        return !(a && a->kind == Arg::Enum && a->text == "F");
    }
    V3 point(int id) const {
        const Arg* a = at(ent(id), 1);
        if (!a || a->kind != Arg::List) return {};
        V3 p;
        if (a->list.size() > 0) p.x = a->list[0].num;
        if (a->list.size() > 1) p.y = a->list[1].num;
        if (a->list.size() > 2) p.z = a->list[2].num;
        return p;
    }
    V3 direction(int id, const V3& otherwise) const {
        if (!ent(id)) return otherwise;
        const V3 d = point(id).normalized();
        return d.length() > 0.5 ? d : otherwise;
    }
    Placement placement(int id) const {
        Placement pl;
        const Ent* e = ent(id);
        if (!e) return pl;
        pl.loc = point(refAt(e, 1));
        pl.z = direction(refAt(e, 2), V3{0, 0, 1});
        V3 x = direction(refAt(e, 3), std::abs(pl.z.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0});
        x = (x - pl.z * dot3(x, pl.z)).normalized();
        if (x.length() < 0.5) x = cross3(pl.z, V3{1, 0, 0}).normalized();
        pl.x = x;
        pl.y = cross3(pl.z, pl.x).normalized();
        return pl;
    }
    V3 vertex(int id) const { return point(refAt(ent(id), 1)); }

    // AN EDGE AS POINTS, from its first vertex to its second: a line straight, a circle or an ellipse sampled every
    // fifteen degrees or so, a spline by its control points.
    std::vector<V3> curvePoints(int curveId, const V3& p1, const V3& p2, bool sameSense) const {
        const Ent* c = ent(curveId);
        if (!c) return {p1, p2};
        if (c->type == "SURFACE_CURVE" || c->type == "SEAM_CURVE") return curvePoints(refAt(c, 1), p1, p2, sameSense);
        if (c->type == "CIRCLE" || c->type == "ELLIPSE") {
            const Placement pl = placement(refAt(c, 1));
            const double r1 = numAt(c, 2), r2 = c->type == "ELLIPSE" ? numAt(c, 3) : r1;
            auto angle = [&](const V3& p) {
                const V3 d = p - pl.loc;
                return std::atan2(dot3(d, pl.y) / std::max(1e-12, r2), dot3(d, pl.x) / std::max(1e-12, r1));
            };
            double a1 = angle(p1), a2 = angle(p2);
            const bool closed = (p1 - p2).length() < 1e-7 * std::max(1.0, r1);
            if (sameSense) {
                while (a2 <= a1 + (closed ? 1e-9 : 0.0)) a2 += 2.0 * M_PI;
                if (closed) a2 = a1 + 2.0 * M_PI;
            } else {
                while (a2 >= a1 - (closed ? 1e-9 : 0.0)) a2 -= 2.0 * M_PI;
                if (closed) a2 = a1 - 2.0 * M_PI;
            }
            const int n = std::max(2, static_cast<int>(std::ceil(std::abs(a2 - a1) / (M_PI / 12.0))));
            std::vector<V3> out;
            for (int k = 0; k <= n; ++k) {
                const double a = a1 + (a2 - a1) * k / n;
                out.push_back(pl.loc + pl.x * (r1 * std::cos(a)) + pl.y * (r2 * std::sin(a)));
            }
            return out;
        }
        if (c->type.startsWith("B_SPLINE_CURVE")) {
            const Arg* pts = at(c, 2);
            std::vector<V3> out;
            if (pts && pts->kind == Arg::List)
                for (const Arg& r : pts->list)
                    if (r.kind == Arg::Ref) out.push_back(point(r.ref));
            if (out.size() >= 2) {
                if ((out.front() - p1).length() > (out.back() - p1).length()) std::reverse(out.begin(), out.end());
                out.front() = p1;
                out.back() = p2;
                return out;
            }
        }
        return {p1, p2};
    }

    std::vector<V3> loopPoints(int loopId) const {
        const Ent* l = ent(loopId);
        std::vector<V3> out;
        if (!l) return out;
        if (l->type == "POLY_LOOP") {
            const Arg* pts = at(l, 1);
            if (pts && pts->kind == Arg::List)
                for (const Arg& r : pts->list)
                    if (r.kind == Arg::Ref) out.push_back(point(r.ref));
            return out;
        }
        const Arg* edges = at(l, 1);
        if (!edges || edges->kind != Arg::List) return out;
        for (const Arg& r : edges->list) {
            const Ent* oe = ent(r.ref);   // ORIENTED_EDGE('', *, *, edge, orientation)
            if (!oe) continue;
            const Ent* ec = ent(refAt(oe, 3));   // EDGE_CURVE('', v1, v2, curve, sameSense)
            if (!ec) continue;
            std::vector<V3> pts =
                curvePoints(refAt(ec, 3), vertex(refAt(ec, 1)), vertex(refAt(ec, 2)), flagAt(ec, 4));
            if (!flagAt(oe, 4)) std::reverse(pts.begin(), pts.end());
            for (const V3& p : pts)
                if (out.empty() || (p - out.back()).length() > 1e-9) out.push_back(p);
        }
        if (out.size() > 1 && (out.front() - out.back()).length() < 1e-9) out.pop_back();
        return out;
    }
};

// EAR CLIPPING: a simple polygon in the plane into triangles, by the indices of its corners.
std::vector<std::array<int, 3>> earClip(const std::vector<QPointF>& pts) {
    std::vector<std::array<int, 3>> out;
    const int n = static_cast<int>(pts.size());
    if (n < 3) return out;
    double area = 0.0;
    for (int i = 0; i < n; ++i) {
        const QPointF& a = pts[static_cast<size_t>(i)];
        const QPointF& b = pts[static_cast<size_t>((i + 1) % n)];
        area += a.x() * b.y() - b.x() * a.y();
    }
    std::vector<int> idx(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) idx[static_cast<size_t>(i)] = area >= 0.0 ? i : n - 1 - i;
    auto crossz = [&](int a, int b, int c) {
        const QPointF &p = pts[static_cast<size_t>(a)], &q = pts[static_cast<size_t>(b)], &r = pts[static_cast<size_t>(c)];
        return (q.x() - p.x()) * (r.y() - p.y()) - (q.y() - p.y()) * (r.x() - p.x());
    };
    auto inside = [&](int a, int b, int c, int p) {
        return crossz(a, b, p) >= 0.0 && crossz(b, c, p) >= 0.0 && crossz(c, a, p) >= 0.0;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10 * n) {
        bool clipped = false;
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            const int a = idx[static_cast<size_t>((i + m - 1) % m)], b = idx[static_cast<size_t>(i)],
                      c = idx[static_cast<size_t>((i + 1) % m)];
            if (crossz(a, b, c) <= 1e-12) continue;
            bool ear = true;
            for (int k = 0; k < m && ear; ++k) {
                const int p = idx[static_cast<size_t>(k)];
                if (p == a || p == b || p == c) continue;
                if (inside(a, b, c, p)) ear = false;
            }
            if (!ear) continue;
            out.push_back({a, b, c});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    // Whatever is left - a degenerate stretch ear clipping cannot make sense of - as a fan.
    for (size_t i = 1; i + 1 < idx.size(); ++i) out.push_back({idx[0], idx[i], idx[i + 1]});
    return out;
}

// The angle of a point round an axis, unwrapped along a loop so it runs on without jumping.
std::vector<double> unwrapAngles(const std::vector<V3>& loop, const Placement& pl) {
    std::vector<double> a;
    for (const V3& p : loop) {
        const V3 d = p - pl.loc;
        double t = std::atan2(dot3(d, pl.y), dot3(d, pl.x));
        if (!a.empty()) {
            while (t - a.back() > M_PI) t -= 2.0 * M_PI;
            while (t - a.back() < -M_PI) t += 2.0 * M_PI;
        }
        a.push_back(t);
    }
    return a;
}

// A loop going right round an axis, sampled at so many equal angles from `from`.
std::vector<V3> ringAt(const std::vector<V3>& loop, const Placement& pl, double from, int n) {
    std::vector<double> a = unwrapAngles(loop, pl);
    std::vector<V3> pts = loop;
    if (a.size() >= 2 && a.back() < a.front()) {
        std::reverse(a.begin(), a.end());
        std::reverse(pts.begin(), pts.end());
    }
    // Closed round: the first point again, a turn on.
    pts.push_back(pts.front());
    a.push_back(a.front() + 2.0 * M_PI);
    std::vector<V3> out;
    for (int k = 0; k < n; ++k) {
        double t = from + 2.0 * M_PI * k / n;
        while (t < a.front()) t += 2.0 * M_PI;
        while (t > a.back()) t -= 2.0 * M_PI;
        size_t j = 0;
        while (j + 2 < a.size() && a[j + 1] < t) ++j;
        const double span = a[j + 1] - a[j];
        const double u = span > 1e-12 ? std::clamp((t - a[j]) / span, 0.0, 1.0) : 0.0;
        out.push_back(pts[j] + (pts[j + 1] - pts[j]) * u);
    }
    return out;
}

} // namespace

Mesh3D loadStepText(const QByteArray& raw, QString* how) {
    Mesh3D m;
    // Without its comments, and from the DATA section on.
    QByteArray text = raw;
    for (int a = text.indexOf("/*"); a >= 0; a = text.indexOf("/*", a)) {
        const int b = text.indexOf("*/", a + 2);
        text.remove(a, (b < 0 ? text.size() : b + 2) - a);
    }
    const int data = text.indexOf("DATA;");
    if (data < 0) {
        if (how) *how = QStringLiteral("not a STEP file (no DATA section)");
        return m;
    }
    Step st;
    // EACH ENTITY: "#12=TYPE(...);" - a ';' inside a string is not its end.
    int i = data + 5;
    while (i < text.size()) {
        const int hash = text.indexOf('#', i);
        if (hash < 0) break;
        int end = hash;
        bool quoted = false;
        while (end < text.size()) {
            const char c = text[end];
            if (c == '\'') quoted = !quoted;
            else if (c == ';' && !quoted) break;
            ++end;
        }
        const QByteArray one = text.mid(hash, end - hash);
        i = end + 1;
        const int eq = one.indexOf('=');
        if (eq < 0) continue;
        const int id = one.mid(1, eq - 1).trimmed().toInt();
        QByteArray rest = one.mid(eq + 1).trimmed();
        if (rest.startsWith('(')) continue;   // a complex entity: none of the shapes read here
        const int open = rest.indexOf('(');
        if (open < 0) continue;
        Ent e;
        e.type = rest.left(open).trimmed().toUpper();
        QByteArray inside = rest.mid(open);
        Parser p{inside};
        e.args = p.value();
        st.ents.insert(id, e);
    }
    int faces = 0, analytic = 0, filled = 0;
    for (auto it = st.ents.cbegin(); it != st.ents.cend(); ++it) {
        const Ent& face = it.value();
        if (face.type != "ADVANCED_FACE" && face.type != "FACE_SURFACE" && face.type != "FACE")
            continue;
        ++faces;
        // ITS BOUNDARIES: the outer one first, each as points.
        std::vector<std::vector<V3>> loops;
        int outer = -1;
        if (const Arg* bounds = Step::at(&face, 1); bounds && bounds->kind == Arg::List)
            for (const Arg& r : bounds->list) {
                const Ent* fb = st.ent(r.ref);
                if (!fb) continue;
                std::vector<V3> pts = st.loopPoints(Step::refAt(fb, 1));
                if (!Step::flagAt(fb, 2)) std::reverse(pts.begin(), pts.end());
                if (pts.size() < 2) continue;
                if (fb->type == "FACE_OUTER_BOUND" && outer < 0) outer = static_cast<int>(loops.size());
                loops.push_back(pts);
            }
        if (loops.empty()) continue;
        if (outer < 0) outer = 0;
        const Ent* surf = st.ent(Step::refAt(&face, 2));
        const QByteArray stype = surf ? surf->type : QByteArray();
        const bool revolved = stype == "CYLINDRICAL_SURFACE" || stype == "CONICAL_SURFACE";
        const Placement pl = surf ? st.placement(Step::refAt(surf, 1)) : Placement{};
        // A BAND ROUND AN AXIS between two loops that each go right round it - a cylinder or a cone between two
        // circles: a strip of quads between the two, sampled at the same angles.
        if (revolved && loops.size() == 2) {
            const std::vector<double> a0 = unwrapAngles(loops[0], pl), a1 = unwrapAngles(loops[1], pl);
            const bool round0 = loops[0].size() > 3 && std::abs(a0.back() - a0.front()) > 2.0 * M_PI * 0.85;
            const bool round1 = loops[1].size() > 3 && std::abs(a1.back() - a1.front()) > 2.0 * M_PI * 0.85;
            if (round0 && round1) {
                const int n = 36;
                const std::vector<V3> r0 = ringAt(loops[0], pl, 0.0, n), r1 = ringAt(loops[1], pl, 0.0, n);
                const int base = static_cast<int>(m.v.size());
                for (const V3& p : r0) m.addVertex(p);
                for (const V3& p : r1) m.addVertex(p);
                for (int k = 0; k < n; ++k) {
                    const int a = base + k, b = base + (k + 1) % n, c = base + n + k, d = base + n + (k + 1) % n;
                    m.addTri(a, b, d);
                    m.addTri(a, d, c);
                }
                ++analytic;
                continue;
            }
        }
        // OTHERWISE ITS OUTER BOUNDARY, laid flat and cut into triangles: on a plane in the plane's own axes, round an
        // axis by angle and height, on any other surface across the plane it best lies in.
        const std::vector<V3>& loop = loops[static_cast<size_t>(outer)];
        if (loop.size() < 3) continue;
        std::vector<QPointF> flat;
        if (stype == "PLANE") {
            for (const V3& p : loop) flat.emplace_back(dot3(p - pl.loc, pl.x), dot3(p - pl.loc, pl.y));
            ++analytic;
        } else if (revolved) {
            const double r = std::max(1e-6, Step::numAt(surf, 2));
            const std::vector<double> a = unwrapAngles(loop, pl);
            for (size_t k = 0; k < loop.size(); ++k) flat.emplace_back(a[k] * r, dot3(loop[k] - pl.loc, pl.z));
            ++analytic;
        } else {
            // Newell's normal of the loop, and two directions across it.
            V3 n;
            for (size_t k = 0; k < loop.size(); ++k) {
                const V3& p = loop[k];
                const V3& q = loop[(k + 1) % loop.size()];
                n = n + V3{(p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)};
            }
            n = n.normalized();
            if (n.length() < 0.5) n = {0, 0, 1};
            const V3 u = (std::abs(n.z) < 0.9 ? cross3(n, V3{0, 0, 1}) : cross3(n, V3{1, 0, 0})).normalized();
            const V3 w = cross3(n, u);
            for (const V3& p : loop) flat.emplace_back(dot3(p, u), dot3(p, w));
            ++filled;
        }
        const int base = static_cast<int>(m.v.size());
        for (const V3& p : loop) m.addVertex(p);
        for (const auto& t : earClip(flat)) m.addTri(base + t[0], base + t[1], base + t[2]);
    }
    if (how) {
        if (faces == 0) *how = QStringLiteral("no faces in it - not a solid this reader takes");
        else
            *how = QStringLiteral("STEP: %1 face(s), %2 on planes, cylinders and cones, %3 filled from their edges - %4 triangles")
                       .arg(faces)
                       .arg(analytic)
                       .arg(filled)
                       .arg(m.f.size());
    }
    return m;
}

Mesh3D loadStep(const QString& path, QString* how) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (how) *how = QStringLiteral("cannot open %1").arg(QFileInfo(path).fileName());
        return {};
    }
    return loadStepText(file.readAll(), how);
}

Mesh3D loadMesh3D(const QString& path, QString* how) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("stl")) return loadStl(path, how);
    if (ext == QLatin1String("obj")) return loadObj(path, how);
#ifdef EDRAW_HAVE_OCCT
    // A STEP MODEL WHOLE, every surface, through Open CASCADE - its solids as one mesh; Edraw's own reader where Open
    // CASCADE cannot take the file.
    if (ext == QLatin1String("stp") || ext == QLatin1String("step")) {
        std::vector<EnvPart3D> parts;
        QString said;
        if (readStepOcct(path, parts, &said)) {
            Mesh3D all;
            for (const EnvPart3D& p : parts) all.append(p.mesh);
            if (how) *how = said;
            return all;
        }
    }
#endif
    if (ext == QLatin1String("stp") || ext == QLatin1String("step")) return loadStep(path, how);
    if (how) *how = QStringLiteral("a .%1 file is not a model this reads (STEP, STL, OBJ)").arg(ext);
    return {};
}

QString mesh3DFileFilter() {
    return QStringLiteral("3D models (*.stp *.step *.stl *.obj);;STEP (*.stp *.step);;STL (*.stl);;OBJ (*.obj);;All files (*)");
}

} // namespace Edraw_Desktop

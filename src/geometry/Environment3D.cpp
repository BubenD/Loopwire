#include "geometry/Environment3D.h"

#ifdef EDRAW_HAVE_OCCT
#include "geometry/StepOcct.h"
#endif

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Edraw_Desktop {

bool Environment3D::empty() const {
    for (const EnvPart3D& p : parts)
        if (!p.mesh.empty()) return false;
    return true;
}

size_t Environment3D::triangles() const {
    size_t n = 0;
    for (const EnvPart3D& p : parts) n += p.mesh.f.size();
    return n;
}

bool Environment3D::bounds(V3& lo, V3& hi) const {
    bool any = false;
    for (const EnvPart3D& p : parts) {
        V3 a, b;
        if (!p.mesh.bounds(a, b)) continue;
        lo = any ? V3{std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)} : a;
        hi = any ? V3{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)} : b;
        any = true;
    }
    return any;
}

bool fullStepReader() {
#ifdef EDRAW_HAVE_OCCT
    return true;
#else
    return false;
#endif
}

QString environment3DFileFilter() {
    return QStringLiteral("CAD models (*.stp *.step *.stl *.obj);;STEP (*.stp *.step);;STL (*.stl);;OBJ (*.obj);;All files (*)");
}

Environment3D loadEnvironment3D(const QString& path, const Load3DProgress& progress) {
    Environment3D env;
    env.path = path;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (progress) progress(2, QStringLiteral("Reading the file"));
#ifdef EDRAW_HAVE_OCCT
    if (ext == QLatin1String("stp") || ext == QLatin1String("step")) {
        if (readStepOcct(path, env.parts, &env.how, progress)) return env;
        if (progress) progress(60, QStringLiteral("Reading it with Edraw's own reader"));
        // Not a file Open CASCADE takes whole - Edraw's own reader may still find faces in it.
        const QString why = env.how;
        QElapsedTimer clock;
        clock.start();
        EnvPart3D one;
        QString how;
        one.mesh = loadStep(path, &how);
        if (one.mesh.empty()) {
            env.how = why;
            return env;
        }
        one.name = QFileInfo(path).completeBaseName();
        env.parts.push_back(std::move(one));
        env.how = QStringLiteral("%1 (%2)").arg(how, why);
        return env;
    }
#endif
    QElapsedTimer clock;
    clock.start();
    EnvPart3D one;
    QString how;
    one.mesh = loadMesh3D(path, &how);
    if (one.mesh.empty()) {
        env.how = how;
        return env;
    }
    one.name = QFileInfo(path).completeBaseName();
    env.parts.push_back(std::move(one));
    env.how = QStringLiteral("%1: %2, read in %3 s").arg(QFileInfo(path).fileName(), how).arg(clock.elapsed() / 1000.0, 0, 'f', 1);
    return env;
}

// ---- The tree of boxes ----------------------------------------------------------------------------

void RayIndex3D::clear() {
    tris_.clear();
    nodes_.clear();
}

void RayIndex3D::build(const std::vector<const Mesh3D*>& meshes) {
    clear();
    for (size_t m = 0; m < meshes.size(); ++m) {
        if (!meshes[m]) continue;
        const Mesh3D& mesh = *meshes[m];
        for (const auto& t : mesh.f)
            tris_.push_back({mesh.v[static_cast<size_t>(t[0])], mesh.v[static_cast<size_t>(t[1])],
                             mesh.v[static_cast<size_t>(t[2])], static_cast<int>(m)});
    }
    if (tris_.empty()) return;
    std::vector<V3> mid(tris_.size());
    for (size_t i = 0; i < tris_.size(); ++i) mid[i] = (tris_[i].a + tris_[i].b + tris_[i].c) / 3.0;
    std::vector<int> order(tris_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    nodes_.reserve(tris_.size() / 2 + 1);
    nodes_.push_back({});
    nodes_[0].first = 0;
    nodes_[0].count = static_cast<int>(order.size());
    // SPLIT, BOX BY BOX, at the middle triangle along its longest side, until a handful is left in each.
    std::vector<int> todo{0};
    while (!todo.empty()) {
        const int ni = todo.back();
        todo.pop_back();
        Node& n = nodes_[static_cast<size_t>(ni)];
        const double inf = std::numeric_limits<double>::max();
        n.lo = {inf, inf, inf};
        n.hi = {-inf, -inf, -inf};
        for (int k = n.first; k < n.first + n.count; ++k) {
            const Tri& t = tris_[static_cast<size_t>(order[static_cast<size_t>(k)])];
            for (const V3* p : {&t.a, &t.b, &t.c}) {
                n.lo = {std::min(n.lo.x, p->x), std::min(n.lo.y, p->y), std::min(n.lo.z, p->z)};
                n.hi = {std::max(n.hi.x, p->x), std::max(n.hi.y, p->y), std::max(n.hi.z, p->z)};
            }
        }
        if (n.count <= 8) continue;
        const V3 size = n.hi - n.lo;
        const int axis = size.x >= size.y && size.x >= size.z ? 0 : size.y >= size.z ? 1 : 2;
        auto along = [&](int i) {
            const V3& c = mid[static_cast<size_t>(i)];
            return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
        };
        const int first = n.first, count = n.count;
        auto begin = order.begin() + first;
        std::nth_element(begin, begin + count / 2, begin + count, [&](int a, int b) { return along(a) < along(b); });
        const int left = static_cast<int>(nodes_.size());
        nodes_[static_cast<size_t>(ni)].left = left;
        Node l, r;
        l.first = first;
        l.count = count / 2;
        r.first = first + count / 2;
        r.count = count - count / 2;
        nodes_.push_back(l);
        nodes_.push_back(r);
        todo.push_back(left);
        todo.push_back(left + 1);
    }
    // The triangles in the tree's order, so a leaf's are side by side.
    std::vector<Tri> sorted;
    sorted.reserve(tris_.size());
    for (int i : order) sorted.push_back(tris_[static_cast<size_t>(i)]);
    tris_.swap(sorted);
}

Pick3D RayIndex3D::cast(const V3& origin, const V3& dirIn) const {
    Pick3D best;
    if (nodes_.empty()) return best;
    const V3 dir = dirIn.normalized();
    // True infinity, not the largest number: a ray lying in a box's face makes 0 x infinity, which is NaN and is taken as
    // unbounded below - 0 x the largest number is 0, and would cut the ray off at its start.
    const double inf = std::numeric_limits<double>::infinity();
    const V3 inv{std::abs(dir.x) > 1e-300 ? 1.0 / dir.x : inf, std::abs(dir.y) > 1e-300 ? 1.0 / dir.y : inf,
                 std::abs(dir.z) > 1e-300 ? 1.0 / dir.z : inf};
    double nearest = inf;
    auto boxHit = [&](const Node& n) {
        double t0 = 0.0, t1 = nearest;
        const double o[3] = {origin.x, origin.y, origin.z}, iv[3] = {inv.x, inv.y, inv.z}, lo[3] = {n.lo.x, n.lo.y, n.lo.z},
                     hi[3] = {n.hi.x, n.hi.y, n.hi.z};
        for (int k = 0; k < 3; ++k) {
            double a = (lo[k] - o[k]) * iv[k], b = (hi[k] - o[k]) * iv[k];
            if (std::isnan(a)) a = -inf;
            if (std::isnan(b)) b = inf;
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
        return true;
    };
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const Node& n = nodes_[static_cast<size_t>(stack.back())];
        stack.pop_back();
        if (!boxHit(n)) continue;
        if (n.left >= 0) {
            stack.push_back(n.left);
            stack.push_back(n.left + 1);
            continue;
        }
        for (int k = n.first; k < n.first + n.count; ++k) {
            // Moller-Trumbore - a hair's tolerance at the edges, so a ray down the edge two triangles share meets one
            // of them rather than slipping between both.
            const Tri& t = tris_[static_cast<size_t>(k)];
            const V3 e1 = t.b - t.a, e2 = t.c - t.a;
            const V3 p = cross3(dir, e2);
            const double det = dot3(e1, p);
            if (std::abs(det) < 1e-14) continue;
            const double id = 1.0 / det;
            const V3 s = origin - t.a;
            const double eps = 1e-9;
            const double u = dot3(s, p) * id;
            if (u < -eps || u > 1.0 + eps) continue;
            const V3 q = cross3(s, e1);
            const double v = dot3(dir, q) * id;
            if (v < -eps || u + v > 1.0 + eps) continue;
            const double d = dot3(e2, q) * id;
            if (d <= 1e-9 || d >= nearest) continue;
            nearest = d;
            best.hit = true;
            best.distance = d;
            best.point = origin + dir * d;
            V3 nrm = cross3(e1, e2).normalized();
            if (dot3(nrm, dir) > 0.0) nrm = nrm * -1.0;
            best.normal = nrm;
            best.part = t.part;
        }
    }
    return best;
}

} // namespace Edraw_Desktop

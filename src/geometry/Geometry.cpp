#include "geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace Edraw_Desktop {

void Bounds::expand(const Vec2& p) {
    if (!valid) {
        min = max = p;
        valid = true;
        return;
    }
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
}

void Bounds::expand(const Bounds& b) {
    if (!b.valid) return;
    expand(b.min);
    expand(b.max);
}

Bounds boundsOf(const std::vector<Vec2>& pts) {
    Bounds b;
    for (const auto& p : pts) b.expand(p);
    return b;
}

ClosestOnSegment closestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const double lenSq = ab.lengthSq();
    double t = 0.0;
    if (lenSq > 1e-12) {
        t = dot(p - a, ab) / lenSq;
        t = std::clamp(t, 0.0, 1.0);
    }
    const Vec2 point = a + ab * t;
    return {point, t, distance(p, point)};
}

std::optional<Vec2> lineLineIntersection(const Vec2& a1, const Vec2& a2,
                                         const Vec2& b1, const Vec2& b2) {
    const Vec2 d1 = a2 - a1;
    const Vec2 d2 = b2 - b1;
    const double denom = cross(d1, d2);
    if (std::abs(denom) < 1e-12) return std::nullopt; // parallel
    const double t = cross(b1 - a1, d2) / denom;
    return a1 + d1 * t;
}

std::optional<Vec2> segmentSegmentIntersection(const Vec2& a1, const Vec2& a2,
                                               const Vec2& b1, const Vec2& b2) {
    const Vec2 d1 = a2 - a1;
    const Vec2 d2 = b2 - b1;
    const double denom = cross(d1, d2);
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const double t = cross(b1 - a1, d2) / denom;
    const double u = cross(b1 - a1, d1) / denom;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
    return a1 + d1 * t;
}

double shortAngleDelta(double current, double target) {
    double d = target - current;
    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
    while (d <= -kTwoPi / 2.0) d += kTwoPi;
    while (d > kTwoPi / 2.0) d -= kTwoPi;
    return d;
}

double polylineLength(const std::vector<Vec2>& pts) {
    double len = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) len += distance(pts[i - 1], pts[i]);
    return len;
}

bool samplePolyline(const std::vector<Vec2>& pts, double dist, bool closed,
                    Vec2& pt, Vec2& tangent) {
    if (pts.size() < 2) return false;
    const double total = polylineLength(pts);
    if (total < 1e-12) return false;
    if (closed) {
        dist = std::fmod(dist, total);
        if (dist < 0.0) dist += total;
    } else {
        dist = std::clamp(dist, 0.0, total);
    }
    double walked = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double seg = distance(pts[i - 1], pts[i]);
        if (seg < 1e-12) continue;
        if (walked + seg >= dist - 1e-12) {
            pt = lerp(pts[i - 1], pts[i], (dist - walked) / seg);
            tangent = (pts[i] - pts[i - 1]).normalized();
            return true;
        }
        walked += seg;
    }
    // Rounding overshoot: land on the final vertex along the last real segment.
    for (size_t i = pts.size(); i-- > 1;) {
        if (distance(pts[i - 1], pts[i]) < 1e-12) continue;
        pt = pts[i];
        tangent = (pts[i] - pts[i - 1]).normalized();
        return true;
    }
    return false;
}

std::vector<Vec2> offsetPolyline(const std::vector<Vec2>& pts, double dist) {
    const int n = static_cast<int>(pts.size());
    if (n < 2) return pts;

    // Offset each segment perpendicular by `dist`.
    std::vector<Vec2> segA(n - 1), segB(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 off = (pts[i + 1] - pts[i]).normalized().perp() * dist;
        segA[i] = pts[i] + off;
        segB[i] = pts[i + 1] + off;
    }

    // Vertices = ends of the offset chain, with mitered interior joints.
    std::vector<Vec2> out;
    out.reserve(n);
    out.push_back(segA[0]);
    for (int i = 1; i < n - 1; ++i) {
        const auto x = lineLineIntersection(segA[i - 1], segB[i - 1], segA[i], segB[i]);
        out.push_back(x.value_or(segB[i - 1])); // parallel -> just use the seam point
    }
    out.push_back(segB[n - 2]);
    return out;
}

std::vector<Vec2> offsetClosedPolygon(const std::vector<Vec2>& pts, double dist) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    // Shift every edge parallel by `dist` (perp), then each new vertex is where the
    // two shifted edges meeting there intersect (mitered, wrapping around the loop).
    std::vector<Vec2> segA(n), segB(n);
    for (int i = 0; i < n; ++i) {
        const Vec2 off = (pts[(i + 1) % n] - pts[i]).normalized().perp() * dist;
        segA[i] = pts[i] + off;
        segB[i] = pts[(i + 1) % n] + off;
    }
    std::vector<Vec2> out(n);
    for (int i = 0; i < n; ++i) {
        const int p = (i - 1 + n) % n; // edge arriving at vertex i
        const auto x = lineLineIntersection(segA[p], segB[p], segA[i], segB[i]);
        out[i] = x.value_or(segA[i]);
    }
    return out;
}

std::optional<std::vector<Vec2>> traceEnclosingLoop(
    const std::vector<std::pair<Vec2, Vec2>>& segments, const Vec2& seed) {
    constexpr double kPi = 3.14159265358979323846;

    // 1. Split every segment at its intersections with the others, so crossings
    //    become shared vertices (a planar arrangement).
    struct Seg { Vec2 a, b; };
    std::vector<Seg> segs;
    const int rawN = static_cast<int>(segments.size());
    std::vector<std::vector<double>> splits(rawN);
    for (int i = 0; i < rawN; ++i) {
        const Vec2 a = segments[i].first, b = segments[i].second;
        const Vec2 d = b - a;
        const double L2 = d.lengthSq();
        if (L2 < 1e-12) continue;
        for (int j = i + 1; j < rawN; ++j) {
            const Vec2 a2 = segments[j].first, b2 = segments[j].second;
            if (auto x = segmentSegmentIntersection(a, b, a2, b2)) {
                splits[i].push_back(dot(*x - a, d) / L2);
                const Vec2 d2 = b2 - a2;
                const double L22 = d2.lengthSq();
                if (L22 > 1e-12) splits[j].push_back(dot(*x - a2, d2) / L22);
            }
        }
    }
    for (int i = 0; i < rawN; ++i) {
        const Vec2 a = segments[i].first, b = segments[i].second;
        if ((b - a).lengthSq() < 1e-12) continue;
        auto& sp = splits[i];
        sp.push_back(0.0);
        sp.push_back(1.0);
        std::sort(sp.begin(), sp.end());
        for (size_t k = 1; k < sp.size(); ++k) {
            if (sp[k] - sp[k - 1] < 1e-6) continue;
            const Vec2 p0 = a + (b - a) * sp[k - 1];
            const Vec2 p1 = a + (b - a) * sp[k];
            if ((p1 - p0).lengthSq() > 1e-12) segs.push_back({p0, p1});
        }
    }
    if (segs.empty()) return std::nullopt;

    // 2. Build the node set + adjacency (snap near-equal points to one node).
    std::map<std::pair<long long, long long>, int> nodeIdx;
    std::vector<Vec2> nodes;
    auto getNode = [&](const Vec2& p) -> int {
        const std::pair<long long, long long> k{std::llround(p.x / 1e-4),
                                                std::llround(p.y / 1e-4)};
        auto it = nodeIdx.find(k);
        if (it != nodeIdx.end()) return it->second;
        const int id = static_cast<int>(nodes.size());
        nodeIdx.emplace(k, id);
        nodes.push_back(p);
        return id;
    };
    std::vector<std::pair<int, int>> edges;
    for (const auto& s : segs) {
        const int u = getNode(s.a), v = getNode(s.b);
        if (u != v) edges.emplace_back(u, v);
    }
    std::vector<std::vector<int>> adj(nodes.size());
    for (const auto& e : edges) {
        auto& au = adj[e.first];
        auto& av = adj[e.second];
        if (std::find(au.begin(), au.end(), e.second) == au.end()) au.push_back(e.second);
        if (std::find(av.begin(), av.end(), e.first) == av.end()) av.push_back(e.first);
    }

    // 3. Ray-cast +X from the seed; the nearest edge to the right bounds the cell.
    const double Y = seed.y;
    int hitU = -1, hitV = -1;
    double bestX = std::numeric_limits<double>::max();
    for (const auto& e : edges) {
        const Vec2 p = nodes[e.first], q = nodes[e.second];
        const bool straddle = (p.y <= Y && q.y > Y) || (q.y <= Y && p.y > Y);
        if (!straddle) continue;
        const double x = p.x + (Y - p.y) / (q.y - p.y) * (q.x - p.x);
        if (x > seed.x + 1e-9 && x < bestX) { bestX = x; hitU = e.first; hitV = e.second; }
    }
    if (hitU < 0) return std::nullopt;

    // Orient the start half-edge upward (+Y) so the seed (to its west) is on the
    // left; the left-hand rule then walks the cell that contains the seed.
    int start = hitU, end = hitV;
    if (nodes[start].y > nodes[end].y) std::swap(start, end);

    // 4. Walk the face: at each node, take the edge immediately clockwise from the
    //    reverse of the incoming direction.
    std::vector<int> loop = {start};
    int cu = start, cv = end;
    const int maxSteps = static_cast<int>(edges.size()) * 2 + 16;
    bool closed = false;
    for (int step = 0; step < maxSteps; ++step) {
        loop.push_back(cv);
        const double revAng =
            std::atan2(nodes[cu].y - nodes[cv].y, nodes[cu].x - nodes[cv].x);
        int next = -1;
        double bestGap = std::numeric_limits<double>::max();
        for (int nb : adj[cv]) {
            const double ang =
                std::atan2(nodes[nb].y - nodes[cv].y, nodes[nb].x - nodes[cv].x);
            double gap = std::fmod(revAng - ang + 2.0 * kPi, 2.0 * kPi);
            if (gap < 1e-9) gap = 2.0 * kPi; // the reverse edge: only if a dead end
            if (gap < bestGap) { bestGap = gap; next = nb; }
        }
        if (next < 0) return std::nullopt;
        if (cv == start && next == end) { closed = true; break; }
        cu = cv;
        cv = next;
    }
    if (!closed) return std::nullopt;
    if (!loop.empty() && loop.front() == loop.back()) loop.pop_back();
    if (loop.size() < 3) return std::nullopt;

    std::vector<Vec2> out;
    out.reserve(loop.size());
    for (int id : loop) out.push_back(nodes[id]);
    return out;
}

bool filletRadiusFits(const std::vector<Vec2>& pts, double radius) {
    const int n = static_cast<int>(pts.size());
    if (n < 3 || radius <= 0.0) return true;
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 1; i < n - 1; ++i) {
        const Vec2 p0 = pts[i - 1], p1 = pts[i], p2 = pts[i + 1];
        const double cosT = std::clamp(dot((p0 - p1).normalized(), (p2 - p1).normalized()), -1.0, 1.0);
        const double theta = std::acos(cosT);
        if (theta < 1e-3 || theta > kPi - 1e-3) continue; // straight/degenerate: nothing to round
        const double t = radius / std::tan(theta / 2.0); // tangent setback from corner
        const double maxT = 0.5 * std::min(distance(p0, p1), distance(p1, p2));
        if (t > maxT + 1e-9) return false;                // too big to fit this corner
    }
    return true;
}

std::vector<Vec2> roundedPolylinePerCorner(const std::vector<Vec2>& pts,
                                           const std::vector<double>& radii,
                                           int arcSegments) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<Vec2> out;
    out.push_back(pts[0]);
    for (int i = 1; i < n - 1; ++i) {
        const double radius = (i < static_cast<int>(radii.size())) ? radii[i] : 0.0;
        const Vec2 p0 = pts[i - 1], p1 = pts[i], p2 = pts[i + 1];
        const Vec2 d1 = (p0 - p1).normalized(); // corner -> previous vertex
        const Vec2 d2 = (p2 - p1).normalized(); // corner -> next vertex
        const double cosT = std::clamp(dot(d1, d2), -1.0, 1.0);
        const double theta = std::acos(cosT);   // interior angle at the corner
        if (radius <= 0.0 || theta < 1e-3 || theta > kPi - 1e-3) {
            out.push_back(p1);
            continue;
        }

        double t = radius / std::tan(theta / 2.0); // tangent setback from corner
        const double maxT = 0.5 * std::min(distance(p0, p1), distance(p1, p2));
        t = std::min(t, maxT);
        const double r = t * std::tan(theta / 2.0); // radius after clamping

        const Vec2 t1 = p1 + d1 * t;            // tangent point toward previous (start joint)
        const Vec2 t2 = p1 + d2 * t;            // tangent point toward next (end joint)
        const Vec2 bis = (d1 + d2).normalized();
        const Vec2 c = p1 + bis * (r / std::sin(theta / 2.0)); // arc centre

        double a1 = std::atan2(t1.y - c.y, t1.x - c.x);
        const double a2 = std::atan2(t2.y - c.y, t2.x - c.x);
        double sweep = a2 - a1;
        while (sweep <= -kPi) sweep += 2 * kPi;
        while (sweep > kPi) sweep -= 2 * kPi;

        out.push_back(t1); // arc starts tangent to the straight run coming in
        for (int s = 1; s < arcSegments; ++s) {
            const double a = a1 + sweep * s / arcSegments;
            out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        out.push_back(t2); // arc ends tangent to the straight run going out
    }
    out.push_back(pts[n - 1]);
    return out;
}

std::vector<Vec2> roundedPolygonPerCorner(const std::vector<Vec2>& pts,
                                          const std::vector<double>& radii,
                                          int arcSegments) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<Vec2> out;
    for (int i = 0; i < n; ++i) { // EVERY vertex is a corner (closed loop, wraps)
        const double radius = (i < static_cast<int>(radii.size())) ? radii[i] : 0.0;
        const Vec2 p0 = pts[(i - 1 + n) % n], p1 = pts[i], p2 = pts[(i + 1) % n];
        const Vec2 d1 = (p0 - p1).normalized();
        const Vec2 d2 = (p2 - p1).normalized();
        const double cosT = std::clamp(dot(d1, d2), -1.0, 1.0);
        const double theta = std::acos(cosT);
        if (radius <= 0.0 || theta < 1e-3 || theta > kPi - 1e-3) {
            out.push_back(p1);
            continue;
        }
        double t = radius / std::tan(theta / 2.0);
        const double maxT = 0.5 * std::min(distance(p0, p1), distance(p1, p2));
        t = std::min(t, maxT);
        const double r = t * std::tan(theta / 2.0);
        const Vec2 t1 = p1 + d1 * t;
        const Vec2 t2 = p1 + d2 * t;
        const Vec2 bis = (d1 + d2).normalized();
        const Vec2 c = p1 + bis * (r / std::sin(theta / 2.0));
        double a1 = std::atan2(t1.y - c.y, t1.x - c.x);
        const double a2 = std::atan2(t2.y - c.y, t2.x - c.x);
        double sweep = a2 - a1;
        while (sweep <= -kPi) sweep += 2 * kPi;
        while (sweep > kPi) sweep -= 2 * kPi;
        out.push_back(t1);
        for (int s = 1; s < arcSegments; ++s) {
            const double a = a1 + sweep * s / arcSegments;
            out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        out.push_back(t2);
    }
    return out;
}

std::vector<Vec2> roundedPolyline(const std::vector<Vec2>& pts, double radius,
                                  int arcSegments) {
    if (radius <= 0.0) return pts;
    return roundedPolylinePerCorner(pts, std::vector<double>(pts.size(), radius), arcSegments);
}

namespace {
// Emit one corner p0->p1->p2 into `out`: a chamfer cut (both setbacks>0), a tangent
// fillet arc (radius>0), or a sharp vertex. Chamfer wins if set. The chamfer cut runs
// from a point chamferIn back along the incoming edge (toward p0) to a point
// chamferOut along the outgoing edge (toward p2) — asymmetric distances allowed, so a
// chamfer can pass "exactly between" two picked points.
void emitCorner(std::vector<Vec2>& out, const Vec2& p0, const Vec2& p1, const Vec2& p2,
                double radius, double chamferIn, double chamferOut, int arcSegments) {
    constexpr double kPi = 3.14159265358979323846;
    const Vec2 d1 = (p0 - p1).normalized();
    const Vec2 d2 = (p2 - p1).normalized();
    const double cosT = std::clamp(dot(d1, d2), -1.0, 1.0);
    const double theta = std::acos(cosT);
    const double maxT = 0.5 * std::min(distance(p0, p1), distance(p1, p2));
    const bool bendable = theta > 1e-3 && theta < kPi - 1e-3;
    if (chamferIn > 0.0 && chamferOut > 0.0 && bendable) {
        const double sIn = std::min(chamferIn, distance(p0, p1));
        const double sOut = std::min(chamferOut, distance(p1, p2));
        out.push_back(p1 + d1 * sIn);
        out.push_back(p1 + d2 * sOut);
        return;
    }
    if (radius > 0.0 && bendable) {
        double t = radius / std::tan(theta / 2.0);
        t = std::min(t, maxT);
        const double r = t * std::tan(theta / 2.0);
        const Vec2 t1 = p1 + d1 * t;
        const Vec2 t2 = p1 + d2 * t;
        const Vec2 bis = (d1 + d2).normalized();
        const Vec2 c = p1 + bis * (r / std::sin(theta / 2.0));
        const double a1 = std::atan2(t1.y - c.y, t1.x - c.x);
        const double a2 = std::atan2(t2.y - c.y, t2.x - c.x);
        double sweep = a2 - a1;
        while (sweep <= -kPi) sweep += 2 * kPi;
        while (sweep > kPi) sweep -= 2 * kPi;
        out.push_back(t1);
        for (int s = 1; s < arcSegments; ++s) {
            const double a = a1 + sweep * s / arcSegments;
            out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        out.push_back(t2);
        return;
    }
    out.push_back(p1);
}
} // namespace

std::vector<Vec2> filletChamferPolyline(const std::vector<Vec2>& pts,
                                        const std::vector<double>& radii,
                                        const std::vector<double>& chamfersIn,
                                        const std::vector<double>& chamfersOut, bool closed,
                                        int arcSegments) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    auto at = [](const std::vector<double>& v, int i) {
        return i < static_cast<int>(v.size()) ? v[i] : 0.0;
    };
    std::vector<Vec2> out;
    if (!closed) out.push_back(pts[0]);
    const int begin = closed ? 0 : 1;
    const int end = closed ? n : n - 1;
    for (int i = begin; i < end; ++i)
        emitCorner(out, pts[(i - 1 + n) % n], pts[i], pts[(i + 1) % n], at(radii, i),
                   at(chamfersIn, i), at(chamfersOut, i), arcSegments);
    if (!closed) out.push_back(pts[n - 1]);
    return out;
}

std::vector<Vec2> chamferedPolylinePerCorner(const std::vector<Vec2>& pts,
                                             const std::vector<double>& dists) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    std::vector<Vec2> out;
    out.push_back(pts[0]);
    for (int i = 1; i < n - 1; ++i) {
        const double d = (i < static_cast<int>(dists.size())) ? dists[i] : 0.0;
        const Vec2 p0 = pts[i - 1], p1 = pts[i], p2 = pts[i + 1];
        const Vec2 d1 = (p0 - p1).normalized(); // corner -> previous vertex
        const Vec2 d2 = (p2 - p1).normalized(); // corner -> next vertex
        // Clamp the setback to half the shorter neighbouring run so the two chamfers
        // of adjacent corners never overlap.
        const double s = std::min(d, 0.5 * std::min(distance(p0, p1), distance(p1, p2)));
        if (d <= 0.0 || s <= 1e-9) {
            out.push_back(p1);
            continue;
        }
        out.push_back(p1 + d1 * s); // straight cut across the corner
        out.push_back(p1 + d2 * s);
    }
    out.push_back(pts[n - 1]);
    return out;
}

std::vector<Vec2> chamferedPolygonPerCorner(const std::vector<Vec2>& pts,
                                            const std::vector<double>& dists) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    std::vector<Vec2> out;
    for (int i = 0; i < n; ++i) { // EVERY vertex is a corner (closed loop, wraps)
        const double d = (i < static_cast<int>(dists.size())) ? dists[i] : 0.0;
        const Vec2 p0 = pts[(i - 1 + n) % n], p1 = pts[i], p2 = pts[(i + 1) % n];
        const Vec2 d1 = (p0 - p1).normalized();
        const Vec2 d2 = (p2 - p1).normalized();
        const double s = std::min(d, 0.5 * std::min(distance(p0, p1), distance(p1, p2)));
        if (d <= 0.0 || s <= 1e-9) {
            out.push_back(p1);
            continue;
        }
        out.push_back(p1 + d1 * s);
        out.push_back(p1 + d2 * s);
    }
    return out;
}

bool chamferDistFits(const std::vector<Vec2>& pts, double dist) {
    const int n = static_cast<int>(pts.size());
    if (n < 3 || dist <= 0.0) return true;
    for (int i = 1; i < n - 1; ++i) {
        const double maxS = 0.5 * std::min(distance(pts[i - 1], pts[i]), distance(pts[i], pts[i + 1]));
        if (dist > maxS + 1e-9) return false;
    }
    return true;
}

std::vector<Vec2> splinePoints(const std::vector<Vec2>& pts, int samplesPerSegment) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    const int seg = std::max(2, samplesPerSegment);
    std::vector<Vec2> out;
    out.reserve(static_cast<size_t>((n - 1) * seg + 1));
    // Catmull-Rom through every point; duplicate the ends so the curve reaches them.
    auto at = [&](int i) -> Vec2 { return pts[std::clamp(i, 0, n - 1)]; };
    out.push_back(pts[0]);
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int s = 1; s <= seg; ++s) {
            const double t = static_cast<double>(s) / seg;
            const double t2 = t * t, t3 = t2 * t;
            // Catmull-Rom basis (tension 0.5).
            const double b0 = -0.5 * t3 + t2 - 0.5 * t;
            const double b1 = 1.5 * t3 - 2.5 * t2 + 1.0;
            const double b2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
            const double b3 = 0.5 * t3 - 0.5 * t2;
            out.push_back({p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
                           p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3});
        }
    }
    return out;
}

std::vector<Vec2> bsplinePoints(const std::vector<Vec2>& ctrl, int samplesPerSegment) {
    const int n = static_cast<int>(ctrl.size());
    if (n < 3) return ctrl;
    const int seg = std::max(2, samplesPerSegment);
    // Clamp the ends by tripling the first and last control points so the curve
    // starts and ends exactly on them (uniform cubic B-spline otherwise floats).
    std::vector<Vec2> p;
    p.reserve(n + 4);
    p.push_back(ctrl.front());
    p.push_back(ctrl.front());
    for (const auto& c : ctrl) p.push_back(c);
    p.push_back(ctrl.back());
    p.push_back(ctrl.back());
    std::vector<Vec2> out;
    out.push_back(ctrl.front());
    for (size_t i = 0; i + 3 < p.size(); ++i) {
        for (int s = 1; s <= seg; ++s) {
            const double t = static_cast<double>(s) / seg;
            const double t2 = t * t, t3 = t2 * t;
            const double ompt = 1.0 - t;
            const double b0 = ompt * ompt * ompt / 6.0;
            const double b1 = (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0;
            const double b2 = (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0;
            const double b3 = t3 / 6.0;
            out.push_back({p[i].x * b0 + p[i + 1].x * b1 + p[i + 2].x * b2 + p[i + 3].x * b3,
                           p[i].y * b0 + p[i + 1].y * b1 + p[i + 2].y * b2 + p[i + 3].y * b3});
        }
    }
    return out;
}

std::vector<Vec2> lineCircleIntersection(const Vec2& p, const Vec2& d,
                                         const Vec2& c, double r) {
    std::vector<Vec2> out;
    const double len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-12) return out;
    const Vec2 u{d.x / len, d.y / len};
    const double proj = (c.x - p.x) * u.x + (c.y - p.y) * u.y;
    const Vec2 foot{p.x + u.x * proj, p.y + u.y * proj};
    const double fx = c.x - foot.x, fy = c.y - foot.y;
    const double dist2 = fx * fx + fy * fy;
    if (dist2 > r * r + 1e-7) return out;
    const double h = std::sqrt(std::max(0.0, r * r - dist2));
    out.push_back({foot.x + u.x * h, foot.y + u.y * h});
    if (h > 1e-9) out.push_back({foot.x - u.x * h, foot.y - u.y * h});
    return out;
}

std::vector<Vec2> bulgeArcPoints(const Vec2& p0, const Vec2& p1, double bulge, int samples) {
    if (std::abs(bulge) < 1e-9) return {p0, p1};
    const double dx = p1.x - p0.x, dy = p1.y - p0.y;
    const double chord = std::sqrt(dx * dx + dy * dy);
    if (chord < 1e-12) return {p0};
    const double delta = 4.0 * std::atan(bulge); // signed central angle
    const double half = delta / 2.0;
    if (std::abs(std::tan(half)) < 1e-9) return {p0, p1};
    const Vec2 mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5};
    const Vec2 n{-dy / chord, dx / chord}; // left normal of the chord
    const double dist = (chord / 2.0) / std::tan(half);
    const Vec2 c{mid.x + n.x * dist, mid.y + n.y * dist};
    const double r = std::sqrt((p0.x - c.x) * (p0.x - c.x) + (p0.y - c.y) * (p0.y - c.y));
    const double a0 = std::atan2(p0.y - c.y, p0.x - c.x);
    const double pi = std::acos(-1.0);
    const int steps = std::max(4, static_cast<int>(std::ceil(std::abs(delta) / (pi / 24.0))));
    std::vector<Vec2> out;
    out.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double a = a0 + delta * (static_cast<double>(i) / steps);
        out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
    }
    return out;
}

double tangentBulge(const Vec2& p0, const Vec2& p1, const Vec2& tangent) {
    const double cx = p1.x - p0.x, cy = p1.y - p0.y;
    const double clen = std::sqrt(cx * cx + cy * cy);
    const double tlen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
    if (clen < 1e-12 || tlen < 1e-12) return 0.0;
    const Vec2 ch{cx / clen, cy / clen};
    const Vec2 t{tangent.x / tlen, tangent.y / tlen};
    // Signed angle from the tangent to the chord; the tangent-arc bulge is tan(angle/2).
    const double alpha = std::atan2(t.x * ch.y - t.y * ch.x, t.x * ch.x + t.y * ch.y);
    return std::tan(alpha / 2.0);
}

std::vector<Vec2> expandBulgePolyline(const std::vector<Vec2>& points,
                                      const std::vector<double>& bulges) {
    std::vector<Vec2> out;
    if (points.empty()) return out;
    out.push_back(points[0]);
    for (size_t i = 1; i < points.size(); ++i) {
        const double b = (i < bulges.size()) ? bulges[i] : 0.0;
        if (std::abs(b) > 1e-9) {
            const auto arc = bulgeArcPoints(points[i - 1], points[i], b);
            for (size_t k = 1; k < arc.size(); ++k) out.push_back(arc[k]);
        } else {
            out.push_back(points[i]);
        }
    }
    return out;
}

namespace {

// One basis function of the B-spline, by the Cox-de Boor recursion. The degree is 3 in practice, so
// the recursion is a handful of terms and there is nothing to be gained by unrolling it.
double bsplineBasis(int i, int k, double t, const std::vector<double>& U) {
    if (i + k + 1 >= static_cast<int>(U.size())) return 0.0;
    if (k == 0) return (t >= U[i] && t < U[i + 1]) ? 1.0 : 0.0;
    const double d1 = U[i + k] - U[i];
    const double d2 = U[i + k + 1] - U[i + 1];
    double out = 0.0;
    if (d1 > 1e-12) out += (t - U[i]) / d1 * bsplineBasis(i, k - 1, t, U);
    if (d2 > 1e-12) out += (U[i + k + 1] - t) / d2 * bsplineBasis(i + 1, k - 1, t, U);
    return out;
}

} // namespace

std::vector<Vec2> nurbsCurvePoints(const std::vector<Vec2>& ctrl, const std::vector<double>& knots,
                                   int degree, const std::vector<double>& weights,
                                   int samplesPerSegment) {
    const int n = static_cast<int>(ctrl.size());
    if (n == 0) return {};
    if (n == 1) return ctrl;
    const int p = std::clamp(degree, 1, std::max(1, n - 1));
    if (n <= p) return ctrl;   // not enough points to be a curve of that degree

    // The knot vector the file stated, when it stated a usable one. Otherwise a CLAMPED uniform vector:
    // p+1 zeroes, the interior spaced evenly, p+1 ones - which starts and ends the curve on the first
    // and last control point, the reading anybody would expect of a file that left them out.
    std::vector<double> U;
    if (static_cast<int>(knots.size()) == n + p + 1) {
        U = knots;
    } else {
        U.assign(n + p + 1, 0.0);
        const int interior = n - p;   // number of spans
        for (int i = 0; i < n + p + 1; ++i) {
            if (i <= p) U[i] = 0.0;
            else if (i >= n) U[i] = 1.0;
            else U[i] = double(i - p) / double(interior);
        }
    }
    // Non-decreasing, or the basis is meaningless.
    for (size_t i = 1; i < U.size(); ++i)
        if (U[i] < U[i - 1]) U[i] = U[i - 1];

    const double t0 = U[p], t1 = U[n];
    if (!(t1 > t0)) return ctrl;

    const bool rational = static_cast<int>(weights.size()) == n;
    const int steps = std::max(8, samplesPerSegment * std::max(1, n - p));
    std::vector<Vec2> out;
    out.reserve(steps + 1);
    for (int s = 0; s <= steps; ++s) {
        // The very end of the span is nudged inside it: a basis function is defined on a half-open
        // interval, so every one of them is zero exactly at t1 and the point would come out at the
        // origin. The last sample is placed by hand below instead.
        double t = t0 + (t1 - t0) * (double(s) / steps);
        if (s == steps) t = t1 - (t1 - t0) * 1e-9;
        double wx = 0.0, wy = 0.0, wsum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double b = bsplineBasis(i, p, t, U);
            if (b == 0.0) continue;
            const double w = rational ? weights[i] : 1.0;
            wx += ctrl[i].x * b * w;
            wy += ctrl[i].y * b * w;
            wsum += b * w;
        }
        if (std::abs(wsum) > 1e-12) out.push_back({wx / wsum, wy / wsum});
    }
    if (out.empty()) return ctrl;
    return out;
}

Vec2 ellipsePointAt(double t, const Vec2& c, double rx, double ry, double rot) {
    const double cc = std::cos(rot), ss = std::sin(rot);
    const double lx = rx * std::cos(t), ly = ry * std::sin(t);
    return {c.x + lx * cc - ly * ss, c.y + lx * ss + ly * cc};
}

double ellipseParamAt(const Vec2& pt, const Vec2& c, double rx, double ry, double rot) {
    const double cc = std::cos(rot), ss = std::sin(rot);
    const Vec2 d = pt - c;
    // Onto the ellipse's own axes, then squashed to a circle: the angle there IS the parameter.
    const double lx = (d.x * cc + d.y * ss) / std::max(1e-12, rx);
    const double ly = (-d.x * ss + d.y * cc) / std::max(1e-12, ry);
    return std::atan2(ly, lx);
}

std::vector<Vec2> lineEllipseIntersection(const Vec2& p, const Vec2& d, const Vec2& c, double rx,
                                          double ry, double rot) {
    std::vector<Vec2> out;
    if (rx < 1e-12 || ry < 1e-12) return out;
    const double cc = std::cos(rot), ss = std::sin(rot);
    // The line, on the ellipse's axes and scaled so the ellipse is the unit circle.
    const Vec2 rel = p - c;
    const double px = (rel.x * cc + rel.y * ss) / rx, py = (-rel.x * ss + rel.y * cc) / ry;
    const double dx = (d.x * cc + d.y * ss) / rx, dy = (-d.x * ss + d.y * cc) / ry;
    const double A = dx * dx + dy * dy;
    if (A < 1e-18) return out;
    const double B = 2.0 * (px * dx + py * dy);
    const double C = px * px + py * py - 1.0;
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return out;
    const double sq = std::sqrt(disc);
    for (const double t : {(-B - sq) / (2.0 * A), (-B + sq) / (2.0 * A)}) {
        out.push_back({p.x + d.x * t, p.y + d.y * t});
        if (disc < 1e-12) break;   // grazes it: one point, not two of the same
    }
    return out;
}

std::vector<Vec2> circleCircleIntersection(const Vec2& c1, double r1,
                                           const Vec2& c2, double r2) {
    std::vector<Vec2> out;
    const double dx = c2.x - c1.x, dy = c2.y - c1.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d < 1e-9 || d > r1 + r2 + 1e-7 || d < std::abs(r1 - r2) - 1e-7) return out;
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, r1 * r1 - a * a));
    const Vec2 mid{c1.x + dx * a / d, c1.y + dy * a / d};
    const Vec2 perp{-dy / d * h, dx / d * h};
    out.push_back({mid.x + perp.x, mid.y + perp.y});
    if (h > 1e-9) out.push_back({mid.x - perp.x, mid.y - perp.y});
    return out;
}

} // namespace Edraw_Desktop

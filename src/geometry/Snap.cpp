#include "geometry/Snap.h"

#include "geometry/Geometry.h"
#include "model/Document.h"
#include "model/Entity.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// World coordinates of a table's grid lines: column boundaries xb[0..cols] (left ->
// right) and row boundaries yb[0..rows] (top -> down, world y-up). False if `e` isn't
// a table with fraction arrays.
bool tableGridLines(const Entity& e, std::vector<double>& xb, std::vector<double>& yb) {
    if (e.type != EntityType::Rect || !e.meta.value("table").toBool() || e.points.size() != 4)
        return false;
    const QJsonArray cf = e.meta.value("colFrac").toArray();
    const QJsonArray rf = e.meta.value("rowFrac").toArray();
    if (cf.isEmpty() || rf.isEmpty()) return false;
    double minX = e.points[0].x, maxX = e.points[0].x;
    double minY = e.points[0].y, maxY = e.points[0].y;
    for (const auto& p : e.points) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const double w = maxX - minX, h = maxY - minY;
    xb.clear(); yb.clear();
    double x = minX; xb.push_back(x);
    for (const auto& v : cf) { x += v.toDouble() * w; xb.push_back(x); }
    double y = maxY; yb.push_back(y);
    for (const auto& v : rf) { y -= v.toDouble() * h; yb.push_back(y); }
    return true;
}

// Collect the segment list that represents an entity's linear skeleton, used
// for midpoint/nearest/intersection snapping.
void entitySegments(const Entity& e, std::vector<std::pair<Vec2, Vec2>>& out) {
    switch (e.type) {
    case EntityType::Line:
        out.emplace_back(e.a, e.b);
        break;
    case EntityType::DoubleLine: {
        // Only the two VISIBLE parallel lines — NOT the centerline. A centerline
        // segment would put a phantom midpoint/nearest snap in the empty gap
        // between the lines, off the grips, so grabbing a visible end would snap
        // the cursor away and miss the joint.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        out.emplace_back(e.a + off, e.b + off);
        out.emplace_back(e.a - off, e.b - off);
        break;
    }
    case EntityType::Polyline:
        // A DOUBLE polyline snaps to its two VISIBLE runs (so you can measure from
        // anywhere along the lines), not the centerline in the empty gap.
        if (e.meta.value("double").toBool() && e.points.size() >= 2) {
            const double gap = e.meta.value("gap").toDouble(10.0);
            for (double s : {gap / 2.0, -gap / 2.0}) {
                const auto run = offsetPolyline(e.points, s);
                for (size_t i = 1; i < run.size(); ++i) out.emplace_back(run[i - 1], run[i]);
            }
            break;
        }
        [[fallthrough]];
    case EntityType::Spline:
    case EntityType::Wire:
        for (size_t i = 1; i < e.points.size(); ++i)
            out.emplace_back(e.points[i - 1], e.points[i]);
        break;
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        for (size_t i = 0; i < e.points.size(); ++i)
            out.emplace_back(e.points[i], e.points[(i + 1) % e.points.size()]);
        // A table also snaps along its interior grid lines, broken per cell so each
        // cell edge gets its own midpoint / nearest snap.
        if (std::vector<double> xb, yb; tableGridLines(e, xb, yb)) {
            for (size_t j = 0; j + 1 < yb.size(); ++j)      // horizontal cell edges
                for (size_t i = 0; i + 1 < xb.size(); ++i)
                    out.emplace_back(Vec2{xb[i], yb[j]}, Vec2{xb[i + 1], yb[j]});
            for (size_t i = 0; i + 1 < xb.size(); ++i)      // vertical cell edges
                for (size_t j = 0; j + 1 < yb.size(); ++j)
                    out.emplace_back(Vec2{xb[i], yb[j]}, Vec2{xb[i], yb[j + 1]});
        }
        break;
    case EntityType::Dimension: {
        // The drawn dimension line (+ extension lines) so midpoint/nearest snap works.
        const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
        if ((ty == QLatin1String("linear") || ty == QLatin1String("aligned") ||
             ty == QLatin1String("arclength")) &&
            e.points.size() >= 3) {
            const Vec2 p1 = e.points[0], p2 = e.points[1], off = e.points[2];
            Vec2 d1, d2;
            if (ty == QLatin1String("linear")) {
                const Vec2 mid = lerp(p1, p2, 0.5);
                const bool h = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
                if (h) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; }
                else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; }
            } else {
                const Vec2 n = (p2 - p1).normalized().perp();
                const double t = dot(off - p1, n);
                d1 = p1 + n * t;
                d2 = p2 + n * t;
            }
            out.emplace_back(d1, d2);   // dimension line
            out.emplace_back(p1, d1);   // extension lines
            out.emplace_back(p2, d2);
        }
        break;
    }
    default:
        break;
    }
}

void entityEndpoints(const Entity& e, std::vector<Vec2>& out) {
    switch (e.type) {
    case EntityType::Line:
        out.push_back(e.a);
        out.push_back(e.b);
        break;
    case EntityType::DoubleLine: {
        // The four ends of the two VISIBLE parallel lines only — no centerline
        // ends, which would float in the gap between the lines (off the grips)
        // and pull the cursor away when grabbing a visible joint.
        const double gap = e.meta.value("gap").toDouble(10.0);
        const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
        out.push_back(e.a + off);
        out.push_back(e.a - off);
        out.push_back(e.b + off);
        out.push_back(e.b - off);
        break;
    }
    case EntityType::Circle:
        // The 4 quadrant joints (top/bottom/left/right), like AutoCAD QUAdrant snap.
        out.push_back({e.center.x + e.radius, e.center.y});
        out.push_back({e.center.x - e.radius, e.center.y});
        out.push_back({e.center.x, e.center.y + e.radius});
        out.push_back({e.center.x, e.center.y - e.radius});
        // A balloon's leader joints: the attach point, the landing knee, and each
        // arrow tip, so object snap catches them.
        if (e.meta.value("balloon").toBool()) {
            std::vector<Vec2> tips;
            for (const auto v : e.meta.value("leaders").toArray()) {
                const QJsonArray a = v.toArray();
                if (a.size() >= 2) tips.push_back({a[0].toDouble(), a[1].toDouble()});
            }
            if (e.meta.contains("leaderX"))
                tips.push_back({e.meta.value("leaderX").toDouble(),
                                e.meta.value("leaderY").toDouble()});
            if (!tips.empty()) {
                int side;
                if (e.meta.contains("landingSide"))
                    side = e.meta.value("landingSide").toDouble() >= 0 ? 1 : -1;
                else {
                    double sx = 0;
                    for (const auto& t : tips) sx += t.x;
                    side = (sx / tips.size() >= e.center.x) ? 1 : -1;
                }
                const double landing = std::max(0.0, e.meta.value("landing").toDouble(8.0));
                out.push_back({e.center.x + side * e.radius, e.center.y}); // attach
                if (landing > 0) out.push_back({e.center.x + side * (e.radius + landing), e.center.y});
                for (const auto& t : tips) out.push_back(t); // arrow tips
            }
        }
        break;
    case EntityType::Arc: {
        // Arc endpoints + the quadrant points that fall within the sweep.
        auto on = [&](double a) {
            return Vec2{e.center.x + e.radius * std::cos(a), e.center.y + e.radius * std::sin(a)};
        };
        out.push_back(on(e.startAngle));
        out.push_back(on(e.endAngle));
        double sweep = e.endAngle - e.startAngle;
        while (sweep < 0.0) sweep += 2.0 * M_PI;
        for (int k = 0; k < 4; ++k) {
            const double q = k * M_PI / 2.0;
            double rel = q - e.startAngle;
            while (rel < 0.0) rel += 2.0 * M_PI;
            if (rel <= sweep) out.push_back(on(q));
        }
        break;
    }
    case EntityType::Ellipse: {
        // The 4 axis endpoints (rotated with the ellipse).
        const double c = std::cos(e.rotation), s = std::sin(e.rotation);
        auto axis = [&](double ax, double ay) {
            return Vec2{e.center.x + ax * c - ay * s, e.center.y + ax * s + ay * c};
        };
        out.push_back(axis(e.radius, 0));
        out.push_back(axis(-e.radius, 0));
        out.push_back(axis(0, e.radiusY));
        out.push_back(axis(0, -e.radiusY));
        break;
    }
    case EntityType::Hatch:
        // A circle-boundary hatch snaps to its 4 quadrant joints too.
        if (e.meta.value("shape").toString() == QLatin1String("circle")) {
            out.push_back({e.center.x + e.radius, e.center.y});
            out.push_back({e.center.x - e.radius, e.center.y});
            out.push_back({e.center.x, e.center.y + e.radius});
            out.push_back({e.center.x, e.center.y - e.radius});
            break;
        }
        for (const auto& p : e.points) out.push_back(p);
        break;
    case EntityType::Polygon:
        for (const auto& p : e.points) out.push_back(p);
        // Centre joint of the polygon (snap to the middle).
        if (!e.points.empty()) {
            Vec2 c{0, 0};
            for (const auto& p : e.points) c = c + p;
            out.push_back(c * (1.0 / static_cast<double>(e.points.size())));
        }
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Wipeout:
        for (const auto& p : e.points) out.push_back(p);
        // A table snaps to every grid intersection (all cell corners), not just the
        // four outer corners.
        if (std::vector<double> xb, yb; tableGridLines(e, xb, yb))
            for (double y : yb)
                for (double x : xb) out.push_back({x, y});
        break;
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        out.push_back(e.a);
        break;
    case EntityType::Dimension: {
        // Snap to a dimension's joints: the measured points, and the dimension-line
        // ends (for radius/diameter, the centre + the leader tip).
        const QString ty = e.meta.value("dimType").toString(QStringLiteral("linear"));
        if ((ty == QLatin1String("linear") || ty == QLatin1String("aligned") ||
             ty == QLatin1String("arclength")) &&
            e.points.size() >= 3) {
            const Vec2 p1 = e.points[0], p2 = e.points[1], off = e.points[2];
            out.push_back(p1);
            out.push_back(p2);
            Vec2 d1, d2;
            if (ty == QLatin1String("linear")) {
                const Vec2 mid = lerp(p1, p2, 0.5);
                const bool h = std::abs(off.y - mid.y) >= std::abs(off.x - mid.x);
                if (h) { d1 = {p1.x, off.y}; d2 = {p2.x, off.y}; }
                else { d1 = {off.x, p1.y}; d2 = {off.x, p2.y}; }
            } else {
                const Vec2 n = (p2 - p1).normalized().perp();
                const double t = dot(off - p1, n);
                d1 = p1 + n * t;
                d2 = p2 + n * t;
            }
            out.push_back(d1);
            out.push_back(d2);
        } else {
            out.push_back(e.a);
            out.push_back(e.b);
            if (ty == QLatin1String("radius") || ty == QLatin1String("diameter") ||
                ty == QLatin1String("jogged"))
                out.push_back(e.center);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

QString snapKindLabel(SnapKind k) {
    switch (k) {
    case SnapKind::Grid: return QStringLiteral("Grid");
    case SnapKind::Endpoint: return QStringLiteral("Endpoint");
    case SnapKind::Midpoint: return QStringLiteral("Midpoint");
    case SnapKind::Center: return QStringLiteral("Center");
    case SnapKind::GeometricCenter: return QStringLiteral("Geometric Center");
    case SnapKind::Node: return QStringLiteral("Node");
    case SnapKind::Quadrant: return QStringLiteral("Quadrant");
    case SnapKind::Intersection: return QStringLiteral("Intersection");
    case SnapKind::Insertion: return QStringLiteral("Insertion");
    case SnapKind::Perpendicular: return QStringLiteral("Perpendicular");
    case SnapKind::Tangent: return QStringLiteral("Tangent");
    case SnapKind::Nearest: return QStringLiteral("Nearest");
    case SnapKind::Ortho: return QStringLiteral("Ortho");
    case SnapKind::Polar: return QStringLiteral("Polar");
    default: return QString();
    }
}

SnapResult resolveSnap(const Document& doc, const Vec2& raw,
                       const std::optional<Vec2>& anchor, double tolWorld,
                       const SnapSettings& s,
                       const std::vector<QString>& ignoreIds) {
    SnapResult result{raw, SnapKind::None, false};

    auto ignored = [&](const QString& id) {
        return std::find(ignoreIds.begin(), ignoreIds.end(), id) != ignoreIds.end();
    };

    // --- Object snap (highest priority): endpoints/centers, then mid/inter/near.
    if (s.objectSnap) {
        double best = tolWorld;
        Vec2 bestPt = raw;
        SnapKind bestKind = SnapKind::None;

        auto consider = [&](const Vec2& p, SnapKind kind) {
            const double d = distance(raw, p);
            if (d < best) { best = d; bestPt = p; bestKind = kind; }
        };

        std::vector<std::pair<Vec2, Vec2>> allSegs;
        for (const auto& e : doc.entities()) {
            if (ignored(e.id)) continue;
            if (const Layer* l = doc.findLayer(e.layer); l && !l->visible) continue;

            if (s.snapEndpoint) {
                std::vector<Vec2> pts;
                entityEndpoints(e, pts);
                for (const auto& p : pts) consider(p, SnapKind::Endpoint);
            }
            if (s.snapCenter &&
                (e.type == EntityType::Circle || e.type == EntityType::Arc ||
                 e.type == EntityType::Ellipse)) {
                consider(e.center, SnapKind::Center);
            }
            // Quadrant: the 4 cardinal points of a circle/arc/ellipse.
            if (s.snapQuadrant &&
                (e.type == EntityType::Circle || e.type == EntityType::Arc ||
                 e.type == EntityType::Ellipse)) {
                const double rx = e.radius, ry = (e.radiusY > 0 ? e.radiusY : e.radius);
                consider({e.center.x + rx, e.center.y}, SnapKind::Quadrant);
                consider({e.center.x - rx, e.center.y}, SnapKind::Quadrant);
                consider({e.center.x, e.center.y + ry}, SnapKind::Quadrant);
                consider({e.center.x, e.center.y - ry}, SnapKind::Quadrant);
            }
            // Geometric centre: centroid of a closed shape.
            if (s.snapGeometricCenter &&
                (e.type == EntityType::Rect || e.type == EntityType::Polygon ||
                 e.type == EntityType::Hatch) &&
                !e.points.empty()) {
                Vec2 c{0, 0};
                for (const auto& pt : e.points) c = c + pt;
                consider(c * (1.0 / e.points.size()), SnapKind::GeometricCenter);
            }
            // Node: point-like objects (splices, markers, net labels, text anchors).
            if (s.snapNode &&
                (e.type == EntityType::Splice || e.type == EntityType::NotConnected ||
                 e.type == EntityType::NetLabel || e.type == EntityType::Text)) {
                consider(e.a, SnapKind::Node);
            }
            // Insertion: the insertion point of text / connectors / balloons.
            if (s.snapInsertion &&
                (e.type == EntityType::Text || e.type == EntityType::Connector ||
                 e.meta.value("balloon").toBool())) {
                consider(e.a, SnapKind::Insertion);
            }
            // Tangent: tangent points on a circle from the anchor (needs a from-point).
            if (s.snapTangent && anchor && e.type == EntityType::Circle) {
                const double d = distance(*anchor, e.center);
                if (d > e.radius + 1e-6) {
                    const double aBase = std::atan2(anchor->y - e.center.y, anchor->x - e.center.x);
                    const double off = std::acos(e.radius / d);
                    consider(pointOnCircle(e.center, e.radius, aBase + off), SnapKind::Tangent);
                    consider(pointOnCircle(e.center, e.radius, aBase - off), SnapKind::Tangent);
                }
            }
            std::vector<std::pair<Vec2, Vec2>> segs;
            entitySegments(e, segs);
            if (s.snapMidpoint)
                for (const auto& seg : segs)
                    consider(lerp(seg.first, seg.second, 0.5), SnapKind::Midpoint);
            // Perpendicular: foot of the perpendicular from the anchor onto a segment.
            if (s.snapPerpendicular && anchor)
                for (const auto& seg : segs) {
                    const Vec2 d = seg.second - seg.first;
                    const double L2 = d.lengthSq();
                    if (L2 < 1e-12) continue;
                    double t = dot(*anchor - seg.first, d) / L2;
                    t = std::clamp(t, 0.0, 1.0);
                    consider(seg.first + d * t, SnapKind::Perpendicular);
                }
            for (auto& seg : segs) allSegs.push_back(seg);
        }

        // Intersections between segments near the cursor.
        if (s.snapIntersection) {
            for (size_t i = 0; i < allSegs.size(); ++i)
                for (size_t j = i + 1; j < allSegs.size(); ++j)
                    if (auto x = segmentSegmentIntersection(
                            allSegs[i].first, allSegs[i].second,
                            allSegs[j].first, allSegs[j].second))
                        consider(*x, SnapKind::Intersection);
        }

        // Nearest point on an object (lowest priority among object snaps): the
        // nearest point along a segment, OR on a circle/arc/ellipse ring.
        if (s.snapNearest && bestKind == SnapKind::None) {
            for (const auto& seg : allSegs) {
                const auto c = closestPointOnSegment(raw, seg.first, seg.second);
                if (c.distance < best) { best = c.distance; bestPt = c.point; bestKind = SnapKind::Nearest; }
            }
            for (const auto& e : doc.entities()) {
                if (ignored(e.id)) continue;
                if (const Layer* l = doc.findLayer(e.layer); l && !l->visible) continue;
                if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
                    const double d = distance(raw, e.center);
                    if (d < 1e-9) continue;
                    double ang = std::atan2(raw.y - e.center.y, raw.x - e.center.x);
                    if (e.type == EntityType::Arc) {
                        // Clamp the angle to the arc's sweep (else its endpoints).
                        double s0 = e.startAngle, sweep = e.endAngle - e.startAngle;
                        while (sweep < 0) sweep += 2 * M_PI;
                        double rel = ang - s0;
                        while (rel < 0) rel += 2 * M_PI;
                        if (rel > sweep) ang = (rel - sweep < 2 * M_PI - rel) ? e.endAngle : s0;
                    }
                    const Vec2 pt = pointOnCircle(e.center, e.radius, ang);
                    const double dd = distance(raw, pt);
                    if (dd < best) { best = dd; bestPt = pt; bestKind = SnapKind::Nearest; }
                } else if (e.type == EntityType::Ellipse) {
                    // Approximate: map the cursor into the ellipse's local frame, take
                    // the parametric point in that direction, map back.
                    const double c = std::cos(e.rotation), sr = std::sin(e.rotation);
                    const Vec2 v = raw - e.center;
                    const double lx = v.x * c + v.y * sr, ly = -v.x * sr + v.y * c;
                    const double ry = e.radiusY > 0 ? e.radiusY : e.radius;
                    const double t = std::atan2(ly / std::max(1e-9, ry), lx / std::max(1e-9, e.radius));
                    const double px = e.radius * std::cos(t), py = ry * std::sin(t);
                    const Vec2 pt{e.center.x + px * c - py * sr, e.center.y + px * sr + py * c};
                    const double dd = distance(raw, pt);
                    if (dd < best) { best = dd; bestPt = pt; bestKind = SnapKind::Nearest; }
                }
            }
        }

        if (bestKind != SnapKind::None)
            return {bestPt, bestKind, true};
    }

    // --- Ortho / polar constrain relative to the anchor.
    if (anchor && (s.ortho || s.polar)) {
        const Vec2 d = raw - *anchor;
        const double len = d.length();
        if (len > 1e-9) {
            const double ang = std::atan2(d.y, d.x);
            if (s.ortho) {
                // Ortho is a hard constraint to the nearest 90 degrees.
                const double stepRad = M_PI / 2.0;
                const double snapped = std::round(ang / stepRad) * stepRad;
                result.point = {anchor->x + len * std::cos(snapped),
                                anchor->y + len * std::sin(snapped)};
                result.kind = SnapKind::Ortho;
                result.snapped = true;
                return result;
            }
            // Polar: free angle, but jump to the nearest tracking increment only
            // when the cursor is within ~2 degrees of it (AutoCAD-style).
            const double stepRad = qDegreesToRadians(s.polarStepDeg);
            if (stepRad > 1e-9) {
                const double snapped = std::round(ang / stepRad) * stepRad;
                if (std::abs(ang - snapped) <= qDegreesToRadians(2.0)) {
                    result.point = {anchor->x + len * std::cos(snapped),
                                    anchor->y + len * std::sin(snapped)};
                    result.kind = SnapKind::Polar;
                    result.snapped = true;
                    return result;
                }
            }
            // Otherwise leave the angle free (fall through to grid / none).
        }
    }

    // --- Grid snap (fallback).
    if (s.gridSnap && s.gridSize > 0.0) {
        result.point = {std::round(raw.x / s.gridSize) * s.gridSize,
                        std::round(raw.y / s.gridSize) * s.gridSize};
        result.kind = SnapKind::Grid;
        result.snapped = true;
        return result;
    }

    return result;
}

} // namespace Edraw_Desktop

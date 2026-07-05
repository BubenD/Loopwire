#include "geometry/Snap.h"

#include "geometry/Geometry.h"
#include "model/Document.h"
#include "model/Entity.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

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
    case SnapKind::Intersection: return QStringLiteral("Intersection");
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
            std::vector<std::pair<Vec2, Vec2>> segs;
            entitySegments(e, segs);
            if (s.snapMidpoint)
                for (const auto& seg : segs)
                    consider(lerp(seg.first, seg.second, 0.5), SnapKind::Midpoint);
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

        // Nearest point on a segment (lowest priority among object snaps).
        if (s.snapNearest && bestKind == SnapKind::None) {
            for (const auto& seg : allSegs) {
                const auto c = closestPointOnSegment(raw, seg.first, seg.second);
                if (c.distance < best) { best = c.distance; bestPt = c.point; bestKind = SnapKind::Nearest; }
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

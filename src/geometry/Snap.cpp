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
    case EntityType::DoubleLine:
        out.emplace_back(e.a, e.b);
        break;
    case EntityType::Polyline:
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
    default:
        break;
    }
}

void entityEndpoints(const Entity& e, std::vector<Vec2>& out) {
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        out.push_back(e.a);
        out.push_back(e.b);
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Hatch:
    case EntityType::Wipeout:
        for (const auto& p : e.points) out.push_back(p);
        break;
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        out.push_back(e.a);
        break;
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
            double ang = std::atan2(d.y, d.x);
            double stepRad;
            if (s.ortho) stepRad = M_PI / 2.0;
            else stepRad = qDegreesToRadians(s.polarStepDeg);
            const double snapped = std::round(ang / stepRad) * stepRad;
            result.point = {anchor->x + len * std::cos(snapped),
                            anchor->y + len * std::sin(snapped)};
            result.kind = s.ortho ? SnapKind::Ortho : SnapKind::Polar;
            result.snapped = true;
            return result;
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

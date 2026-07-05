#pragma once

#include "geometry/Vec2.h"

#include <QString>
#include <optional>
#include <vector>

namespace Edraw_Desktop {

class Document;

enum class SnapKind {
    None,
    Grid,
    Endpoint,
    Midpoint,
    Center,
    GeometricCenter,
    Node,
    Quadrant,
    Intersection,
    Insertion,
    Perpendicular,
    Tangent,
    Nearest,
    Ortho,
    Polar,
};

struct SnapResult {
    Vec2 point;
    SnapKind kind = SnapKind::None;
    bool snapped = false;
};

// Drafting settings, mirroring Edraw's snap/ortho/polar toggles.
struct SnapSettings {
    bool gridSnap = false;
    double gridSize = 10.0;
    bool ortho = false;
    bool polar = false;
    double polarStepDeg = 15.0;
    bool objectSnap = true;
    bool snapEndpoint = true;
    bool snapMidpoint = true;
    bool snapCenter = true;
    bool snapGeometricCenter = false;
    bool snapNode = true;
    bool snapQuadrant = false;
    bool snapIntersection = true;
    bool snapInsertion = false;
    bool snapPerpendicular = false;
    bool snapTangent = false;
    bool snapNearest = true;
    // Object-snap pixel tolerance is converted to world units by the caller.
};

// Resolve a snapped point for the cursor.
//   raw        - cursor in world coordinates
//   anchor     - previous point of an in-progress draw (for ortho/polar), or null
//   tolWorld   - object-snap search tolerance in world units (pixels / view.scale)
//   ignoreIds  - entity ids to skip (e.g. the entity being edited)
SnapResult resolveSnap(const Document& doc, const Vec2& raw,
                       const std::optional<Vec2>& anchor, double tolWorld,
                       const SnapSettings& settings,
                       const std::vector<QString>& ignoreIds = {});

QString snapKindLabel(SnapKind k);

} // namespace Edraw_Desktop

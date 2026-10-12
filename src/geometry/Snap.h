#pragma once

#include "geometry/Vec2.h"

#include <QString>
#include <optional>
#include <vector>

namespace Loopwire {

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

// ---- What a caught snap is DRAWN as --------------------------------------------------------------
//
// A snap marker has one job: to say what has been caught, in the fraction of a second before the click
// lands. The end and the middle of a short line can be five pixels apart, and a crossing can sit on top
// of both - so a single shape meaning "something was caught" withholds the only thing worth knowing.
//
// Three shapes, each standing for a kind of place rather than for one snap:
//
//   Square     A JOINT: a definite point that belongs to the object - an end, a vertex, a node, an
//              insertion point. The thing a line is connected TO.
//   Triangle   The MIDDLE of something: the midpoint of a line, the centre of a circle, the centroid
//              of a shape. A point that is derived from the object rather than built into it.
//   Hourglass  A CROSSING: where two objects meet at a point that belongs to neither of them. Two bars
//              with a waist between them, which is what the draughting convention has always used for
//              an intersection and reads as such at any zoom.
//
// The shape is decided here rather than in the overlay so that the decision is one function with one
// answer per kind, and so the correspondence can be asserted in a test. A marker that quietly drifts
// back to "one shape for everything" is invisible in a screenshot and obvious in a mapping.
enum class SnapMarker {
    Square,
    Triangle,
    Hourglass,
};

SnapMarker snapMarkerFor(SnapKind k);

// Drafting settings, mirroring Loopwire's snap/ortho/polar toggles.
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

} // namespace Loopwire

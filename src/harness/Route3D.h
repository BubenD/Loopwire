#pragma once

#include "geometry/Mesh3D.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

// THE HARNESS ROUTED IN 3D, through the environment: where its ends are, where it divides and where it is clipped
// down, and the way each bundle runs between them - over the faces it was laid along, kept off them by its own radius,
// its corners turned no tighter than its bend radius. Each bundle's length is read off that path. Saved with the
// drawing (Document::routing3d).

enum class RouteNodeKind { Connector, Branch, Clip };
QString routeNodeKindName(RouteNodeKind kind);
RouteNodeKind routeNodeKindFrom(const QString& name);

// A POINT A BUNDLE IS LAID THROUGH: on a face of the environment, and the way that face looks.
struct RouteVia3D {
    V3 at;
    V3 normal{0.0, 0.0, 1.0};
};

// AN END OR A JOINT: a connector on a face (the bundle leaves it along the face's normal), a branch where bundles
// meet, a clip a bundle is held by. `at` is on the face; the bundle's middle is its radius off it.
struct RouteNode3D {
    QString id;
    RouteNodeKind kind = RouteNodeKind::Branch;
    QString label;
    V3 at;
    // THE WAY IT POINTS - the way its bundle leaves it. Out of the face it was put on, unless it has been turned.
    V3 normal{0.0, 0.0, 1.0};
    // The face it was put on looks this way: what "out of its face" puts it back to.
    V3 face{0.0, 0.0, 1.0};
    // TURNED ROUND ITS OWN AXIS, in degrees: a rectangular connector's housing lies one way or another across it.
    double spinDeg = 0.0;
    // THE HARNESS PART IT IS PUT FOR: the wizard's End brick, by its id, and that part's designator as it was then.
    // Empty: a node of the route's own.
    QString brick;
    QString part;
    // HOW A CONNECTOR LOOKS: its form - "rect" (as it starts), "round", "tee" (a flange and a body), "angle" (a right
    // angle), "dsub" (a D-shaped shell) - its sizes across, up and along (0: from the bundle's diameter and the lead),
    // and its colour (none: as connectors are).
    QString form;
    double formW = 0.0, formH = 0.0, formL = 0.0;
    QColor color;
};

// A BUNDLE between two nodes, through the points it was laid along. Its diameter, or the route's where it has none.
struct RouteSegment3D {
    QString id;
    QString a, b;
    std::vector<RouteVia3D> via;
    double diameter = 0.0;
    // Its colour - none: as bundles are.
    QColor color;
};

struct Route3D {
    // For a bundle that gives none, in mm.
    double diameter = 8.0;
    // The tightest a bundle may be bent; 0 or less: six times its diameter.
    double bendRadius = 0.0;
    // How far a bundle comes straight out of a connector before it may turn, in mm.
    double lead = 10.0;
    std::vector<RouteNode3D> nodes;
    std::vector<RouteSegment3D> segments;
    // THE SOLIDS A BUNDLE CANNOT PASS THROUGH: where a ray from a point along a direction first meets one, within so far
    // - set by the 3D view from the model it is routed in. A bundle's middle that would pass through one is laid over
    // it instead, its radius off it, and its length is the length over it. Not saved.
    std::function<bool(const V3& origin, const V3& dir, double far, V3& hit)> solids;

    bool empty() const { return nodes.empty() && segments.empty(); }
    const RouteNode3D* node(const QString& id) const;
    RouteNode3D* node(const QString& id);
    const RouteSegment3D* segment(const QString& id) const;
    RouteSegment3D* segment(const QString& id);
    // A new node, labelled by its kind in turn - X1, X2 for connectors, B1 for branches, C1 for clips.
    QString addNode(RouteNodeKind kind, const V3& at, const V3& normal);
    // A NODE TURNED: pointing along `dir` (out of its face when `dir` is nought), turned `spinDeg` round that.
    bool aimNode(const QString& id, const V3& dir, double spinDeg);
    // A new bundle between two nodes; empty when either is not there or they are the same.
    QString addSegment(const QString& a, const QString& b, std::vector<RouteVia3D> via, double diameter = 0.0);
    // A node taken away, and every bundle to it.
    void removeNode(const QString& id);
    void removeSegment(const QString& id);
    // A BUNDLE DIVIDED at a point of its path, by a new node of a kind there: the points it was laid along before it
    // go to the first half, those after to the second. `afterVia` is how many of them come before the point. The new
    // node's id, or empty.
    QString splitSegment(const QString& id, const V3& at, const V3& normal, int afterVia, RouteNodeKind kind);
    // A BUNDLE MADE LONGER - to `target` mm along it - its ends where they are: laid through more points where it has few,
    // and bowed out from what it lies on until it is that long (or as near as it will go). False where there is none.
    bool lengthen(const QString& id, double target);
    // How many bundles meet at a node.
    int degree(const QString& id) const;
    double diameterOf(const RouteSegment3D& s) const;
    double bendRadiusOf(const RouteSegment3D& s) const;

    // THE MIDDLE OF A BUNDLE: out of a connector straight along its face's normal for the lead, through each point it
    // was laid along its radius off the face, into the far node - every corner turned on an arc of the bend radius,
    // or as near it as the straight runs either side leave room for. `tight`, where given, is how many corners
    // could not be turned that wide.
    std::vector<V3> centreline(const RouteSegment3D& s, int* tight = nullptr) const;
    // Its length along that middle, in mm.
    double length(const RouteSegment3D& s) const;
    // The corners the path turns before arcs are put in - where the middle goes, point by point.
    std::vector<V3> corners(const RouteSegment3D& s) const;

    QJsonObject toJson() const;
    static Route3D fromJson(const QJsonObject& o);
    // The way each corner's face looks, corner for corner with corners().
    std::vector<V3> cornerNormals(const RouteSegment3D& s) const;
    // The corners laid over what they would pass through (as they are, where there are no solids).
    std::vector<V3> overSolids(const std::vector<V3>& corners, const std::vector<V3>& normals, double radius) const;

private:
    QString freshId(const QString& prefix) const;
};

} // namespace Edraw_Desktop

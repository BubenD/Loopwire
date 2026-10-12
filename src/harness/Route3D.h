#pragma once

#include "geometry/Mesh3D.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <unordered_map>
#include <vector>

namespace Loopwire {

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
    // A CONNECTOR'S CABLE OUT OF ITS BACK - a connector through a panel, mated outside, its wires out behind it through
    // the cutout. Otherwise out of its front, the way it points.
    bool back = false;
};

// A BUNDLE between two nodes, through the points it was laid along. Its diameter, or the route's where it has none.
struct RouteSegment3D {
    QString id;
    QString a, b;
    std::vector<RouteVia3D> via;
    double diameter = 0.0;
    // Its colour - none: as bundles are.
    QColor color;
    // HOW TAUT IT IS PUT IN, in newtons - nought: slack, hanging by its length; less than nought: the route's.
    double tension = -1.0;
    // A BUNDLE OF SO MANY WIRES, each so thick (mm) - drawn wire by wire, its diameter what they pack to. Nought: the
    // route's (or a plain bundle, where it gives its own diameter).
    int wires = 0;
    double wireD = 0.0;
    // ITS WIRES' COLOURS: all one (its colour, or as bundles are), or each its own - those given, the rest a colour each.
    bool wireSame = false;
    std::vector<QColor> wireColors;
};

// A KEEP-OUT: where no cable may go - a level (a plane across X, Y or Z, at a place along it) or a zone (a box). A cable
// through one is refused (`block`) or allowed and warned of (an alert). Saved with the route.
struct KeepOut3D {
    QString id;      // K1, K2 ...
    QString name;    // empty: "Level Z 80.0", "Zone K2"
    bool zone = false;
    int axis = 2;    // a level's: 0 X, 1 Y, 2 Z
    double at = 0.0;
    V3 lo, hi;       // a zone's corners
    bool block = false;
};

struct Route3D {
    // For a bundle that gives none, in mm.
    double diameter = 8.0;
    // The tightest a bundle may be bent; 0 or less: six times its diameter.
    double bendRadius = 0.0;
    // How far a bundle comes straight out of a connector before it may turn, in mm.
    double lead = 10.0;
    // How taut a bundle that gives none is put in, in newtons (RouteGravity).
    double tension = 2.0;
    // A BUNDLE OF SO MANY WIRES, each so thick, where one gives none of its own - and its diameter what they pack to.
    // Nought wires: a plain bundle of the diameter above.
    int wires = 0;
    double wireD = 0.6;
    std::vector<RouteNode3D> nodes;
    std::vector<RouteSegment3D> segments;
    // THE SOLIDS A BUNDLE CANNOT PASS THROUGH: where a ray from a point along a direction first meets one, within so far
    // - set by the 3D view from the model it is routed in. A bundle's middle that would pass through one is laid over
    // it instead, its radius off it, and its length is the length over it. Not saved.
    std::function<bool(const V3& origin, const V3& dir, double far, V3& hit)> solids;
    // WHETHER A POINT IS INSIDE ONE OF THEM - a point a bundle was laid or pulled through, pushed back out of it the way
    // its face looks. Not saved.
    std::function<bool(const V3& point)> insideSolid;

    // WHERE NO CABLE MAY GO (RouteKeepOut).
    std::vector<KeepOut3D> keepOuts;
    // A keep-out added, its id the next in turn (K1, K2 ...).
    QString addKeepOut(KeepOut3D k);
    const KeepOut3D* keepOut(const QString& id) const;
    KeepOut3D* keepOut(const QString& id);

    bool empty() const { return nodes.empty() && segments.empty() && keepOuts.empty(); }
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
    // HOW MANY WIRES A BUNDLE IS DRAWN AS, and how thick each - its own, or the route's (none for a bundle given its own
    // diameter). Nought: a plain bundle.
    int wiresOf(const RouteSegment3D& s) const;
    double wireDOf(const RouteSegment3D& s) const;
    // THE DIAMETER SO MANY WIRES PACK TO, round, about three quarters full (seven of them: three across).
    static double packedDiameter(int wires, double wireD);
    // Where each wire lies across a bundle, from its middle (mm), ring by ring.
    static std::vector<std::pair<double, double>> wireLayout(int wires, double wireD);
    double bendRadiusOf(const RouteSegment3D& s) const;
    // HOW FAR A CONNECTOR'S HOUSING STANDS OUT OF ITS FACE: its own length, or (Auto) from the lead and the diameter.
    double housingLength(const RouteNode3D& n) const;
    // ITS BODY THE CABLE COMES OUT OF: the housing out of its front, or a shorter body behind its face.
    double bodyLength(const RouteNode3D& n) const;
    // THE WAY A CONNECTOR'S CABLE LEAVES IT - out of its front or its back - and how far straight before it may turn:
    // the lead, and always past its body, so it comes out of the end of the body and not through its side.
    V3 exitOf(const RouteNode3D& n) const;
    double leadOf(const RouteNode3D& n, double radius) const;
    // What is laid over the solids is kept, by where it was laid from and to - forgotten when the solids change.
    void solidsChanged() const {
        overCache_.clear();
        insideCache_.clear();
    }

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
    // `freeFirst` / `freeLast`: the first or last stretch passes through as it is (a connector's lead, out of its body
    // and through the cutout it is fitted in).
    std::vector<V3> overSolids(const std::vector<V3>& corners, const std::vector<V3>& normals, double radius,
                               bool freeFirst = false, bool freeLast = false) const;

private:
    QString freshId(const QString& prefix) const;
    // A STRETCH LAID OVER WHAT IT PASSES THROUGH, by its ends, its radius and the ways its ends' faces look. Not saved.
    mutable std::unordered_map<std::string, std::vector<V3>> overCache_;
    // Whether a point is inside a solid, by where it is (to a hundredth of a millimetre). Not saved.
    mutable std::unordered_map<std::string, bool> insideCache_;
    bool inside(const V3& p) const;
    std::vector<V3> overOne(const V3& a, const V3& b, const V3& na, const V3& nb, double r) const;
};

} // namespace Loopwire
